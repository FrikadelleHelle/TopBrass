#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach-o/dyld.h>
#include <pthread/qos.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
void checked(OSStatus status,const char* operation) {
    if (status!=noErr) throw std::runtime_error(std::string(operation)+": "+std::to_string(status));
}
constexpr UInt32 code(const char* text) {
    return static_cast<UInt32>(text[0])<<24 | static_cast<UInt32>(text[1])<<16 | static_cast<UInt32>(text[2])<<8 | static_cast<UInt32>(text[3]);
}
template<class T> T property(AudioObjectID object,AudioObjectPropertySelector selector) {
    AudioObjectPropertyAddress address{selector,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    T result{};UInt32 size=sizeof(result);
    checked(AudioObjectGetPropertyData(object,&address,0,nullptr,&size,&result),"Read device property");
    return result;
}
struct Unit {
    AudioUnit value=nullptr;
    bool isOutput=false;
    ~Unit() {if(value) {if(isOutput) AudioOutputUnitStop(value);AudioUnitUninitialize(value);AudioComponentInstanceDispose(value);}}
    void open(AudioComponentDescription description) {
        const auto component=AudioComponentFindNext(nullptr,&description);
        if (!component) throw std::runtime_error("Audio component unavailable");
        checked(AudioComponentInstanceNew(component,&value),"Open audio component");
        isOutput=description.componentType==kAudioUnitType_Output;
    }
};
struct Parameter {AudioUnitParameterID id;float minimum,maximum;};
std::map<std::string,Parameter> parameters(AudioUnit unit) {
    UInt32 bytes=0;
    checked(AudioUnitGetPropertyInfo(unit,kAudioUnitProperty_ParameterList,kAudioUnitScope_Global,0,&bytes,nullptr),"List parameters");
    std::vector<AudioUnitParameterID> ids(bytes/sizeof(AudioUnitParameterID));
    checked(AudioUnitGetProperty(unit,kAudioUnitProperty_ParameterList,kAudioUnitScope_Global,0,ids.data(),&bytes),"Read parameters");
    std::map<std::string,Parameter> result;
    for (const auto id:ids) {
        AudioUnitParameterInfo info{};UInt32 size=sizeof(info);
        checked(AudioUnitGetProperty(unit,kAudioUnitProperty_ParameterInfo,kAudioUnitScope_Global,id,&info,&size),"Read parameter metadata");
        std::array<char,512> name{};
        if ((info.flags&kAudioUnitParameterFlag_HasCFNameString) && info.cfNameString) {
            if (!CFStringGetCString(info.cfNameString,name.data(),name.size(),kCFStringEncodingUTF8)) throw std::runtime_error("Read parameter name");
            if (info.flags&kAudioUnitParameterFlag_CFNameRelease) CFRelease(info.cfNameString);
        } else std::strncpy(name.data(),info.name,name.size()-1);
        result.emplace(name.data(),Parameter{id,info.minValue,info.maxValue});
    }
    return result;
}
void set(AudioUnit unit,const std::map<std::string,Parameter>& params,const std::string& name,float normalized) {
    const auto& parameter=params.at(name);
    const float value=parameter.minimum+normalized*(parameter.maximum-parameter.minimum);
    checked(AudioUnitSetParameter(unit,parameter.id,kAudioUnitScope_Global,0,value,0),"Set parameter");
    float observed=0;
    checked(AudioUnitGetParameter(unit,parameter.id,kAudioUnitScope_Global,0,&observed),"Verify parameter");
    if (std::abs(observed-value)>1e-5f) throw std::runtime_error("Parameter did not retain requested value: "+name);
}
double cpuMicroseconds() noexcept {
    timespec now{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID,&now)!=0) return -1;
    return now.tv_sec*1e6+now.tv_nsec/1e3;
}
struct Row {
    UInt64 entry=0,exit=0,timestampHost=0;
    double sampleTime=0,wall=0,cpu=0,renderWall=0,renderCpu=0,energy=0;
    UInt32 frames=0,timestampFlags=0;OSStatus status=0;
};
struct Context {
    AudioUnit instrument=nullptr;
    bool control=false;
    std::vector<Row> rows;
    std::vector<float> left,right;
    std::atomic<std::size_t> published{0};
    std::atomic<UInt32> overloads{0};
    UInt32 qos=0;boolean_t defaultPolicy=true;OSStatus policyStatus=0;
};
OSStatus overload(AudioObjectID,UInt32,const AudioObjectPropertyAddress*,void* pointer) {
    static_cast<Context*>(pointer)->overloads.fetch_add(1,std::memory_order_relaxed);return noErr;
}
struct OverloadObserver {
    AudioDeviceID device=0;Context* context=nullptr;bool active=false;
    AudioObjectPropertyAddress address{kAudioDeviceProcessorOverload,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    ~OverloadObserver() {if(active) AudioObjectRemovePropertyListener(device,&address,overload,context);}
    bool attach(AudioDeviceID id,Context& value) {
        device=id;context=&value;
        if (!AudioObjectHasProperty(device,&address)) return false;
        checked(AudioObjectAddPropertyListener(device,&address,overload,context),"Observe device overloads");
        active=true;return true;
    }
    void detach() {
        if(active) {checked(AudioObjectRemovePropertyListener(device,&address,overload,context),"Remove overload observer");active=false;}
    }
};
struct StereoBuffers {UInt32 count=2;AudioBuffer buffers[2];};
OSStatus render(void* pointer,AudioUnitRenderActionFlags* flags,const AudioTimeStamp* timestamp,UInt32,UInt32 frames,AudioBufferList* output) noexcept {
    const auto begin=std::chrono::steady_clock::now();const UInt64 entry=mach_absolute_time();const double cpuBegin=cpuMicroseconds();
    for (UInt32 index=0;index<output->mNumberBuffers;++index)
        if(output->mBuffers[index].mData) std::memset(output->mBuffers[index].mData,0,output->mBuffers[index].mDataByteSize);
    *flags|=kAudioUnitRenderAction_OutputIsSilence;
    auto& context=*static_cast<Context*>(pointer);
    const auto index=context.published.load(std::memory_order_relaxed);
    if (index>=context.rows.size()) return noErr;
    Row row;row.entry=entry;row.frames=frames;row.timestampHost=timestamp->mHostTime;row.sampleTime=timestamp->mSampleTime;row.timestampFlags=timestamp->mFlags;
    if (index==0) {
        context.qos=qos_class_self();
        thread_time_constraint_policy_data_t policy{};mach_msg_type_number_t size=THREAD_TIME_CONSTRAINT_POLICY_COUNT;
        const auto thread=mach_thread_self();
        context.policyStatus=thread_policy_get(thread,THREAD_TIME_CONSTRAINT_POLICY,reinterpret_cast<thread_policy_t>(&policy),&size,&context.defaultPolicy);
        mach_port_deallocate(mach_task_self(),thread);
    }
    if (frames==0) row.status=kAudio_ParamError;
    if (frames>context.left.size()) row.status=kAudioUnitErr_TooManyFramesToProcess;
    if (!context.control && row.status==noErr) {
        const auto midi=[&](UInt32 status,UInt32 first,UInt32 second) {
            const auto result=MusicDeviceMIDIEvent(context.instrument,status,first,second,0);
            if (result!=noErr) row.status=result;
        };
        if (index==0) {
            midi(176,11,127);midi(176,1,100);
            for (int note=0;note<128;++note) midi(static_cast<UInt32>(144+note/32),static_cast<UInt32>(48+note%32),114);
        }
        const auto detailPeriod=std::max<std::size_t>(2,12800/frames);
        if (index%detailPeriod==detailPeriod-1) for (UInt32 channel=0;channel<8;++channel) midi(176+channel,20,0);
        if (index%detailPeriod==0) for (UInt32 channel=0;channel<8;++channel) midi(176+channel,20,127);
        std::fill_n(context.left.data(),frames,0);std::fill_n(context.right.data(),frames,0);
        StereoBuffers buffers{2,{{1,frames*static_cast<UInt32>(sizeof(float)),context.left.data()},
                                 {1,frames*static_cast<UInt32>(sizeof(float)),context.right.data()}}};
        AudioUnitRenderActionFlags renderFlags=0;
        const auto dspBegin=std::chrono::steady_clock::now();const auto dspCpu=cpuMicroseconds();
        const auto result=AudioUnitRender(context.instrument,&renderFlags,timestamp,0,frames,reinterpret_cast<AudioBufferList*>(&buffers));
        const auto dspCpuEnd=cpuMicroseconds();row.renderCpu=dspCpu>=0 && dspCpuEnd>=0?dspCpuEnd-dspCpu:-1;
        row.renderWall=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-dspBegin).count();
        if (result!=noErr) row.status=result;
        for (const auto& buffer:buffers.buffers) {
            const auto* data=static_cast<const float*>(buffer.mData);
            if (!data || buffer.mDataByteSize<frames*sizeof(float)) {row.status=kAudio_ParamError;break;}
            for (UInt32 frame=0;frame<frames;++frame) row.energy+=static_cast<double>(data[frame])*data[frame];
        }
    }
    const auto cpuEnd=cpuMicroseconds();row.cpu=cpuBegin>=0 && cpuEnd>=0?cpuEnd-cpuBegin:-1;
    if (row.cpu<0 || row.renderCpu<0) row.status=kAudio_ParamError;
    row.exit=mach_absolute_time();row.wall=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count();
    context.rows[index]=row;
    context.published.store(index+1,std::memory_order_release);
    return noErr;
}
}
int main(int argc,char** argv) {
    try {
        if (argc!=4) throw std::runtime_error("Usage: NativeAudioAudit report-prefix callbacks instrument|control");
        const std::filesystem::path prefix=argv[1];
        if (std::filesystem::exists(prefix.string()+".json") || std::filesystem::exists(prefix.string()+".csv")) throw std::runtime_error("Choose a new report prefix");
        const int count=std::stoi(argv[2]);const std::string mode=argv[3];
        if(count<100 || count>100000 || (mode!="instrument" && mode!="control")) throw std::runtime_error("Invalid callback count or mode");
        const auto device=property<AudioDeviceID>(kAudioObjectSystemObject,kAudioHardwarePropertyDefaultOutputDevice);
        const auto rate=property<Float64>(device,kAudioDevicePropertyNominalSampleRate);
        const auto deviceFrames=property<UInt32>(device,kAudioDevicePropertyBufferFrameSize);
        UInt32 maximum=std::max<UInt32>(4096,deviceFrames);
        if (rate<8000 || rate>192000 || maximum>32768) throw std::runtime_error("Unsupported current device configuration");
        Context context;Unit instrument,output;OverloadObserver observer;
        instrument.open({kAudioUnitType_MusicDevice,code("Tbrs"),code("Tpbr"),0,0});
        UInt32 version=0;checked(AudioComponentGetVersion(AudioComponentInstanceGetComponent(instrument.value),&version),"Read component version");
        auto params=parameters(instrument.value);
        const std::array<std::string,6> names{"Saxophone","Trumpet","Trombone","Tuba","Saxophone II","Trumpet II"};
        for (std::size_t index=0;index<names.size();++index) {
            set(instrument.value,params,names[index]+" on",index==0?1:0);
            set(instrument.value,params,names[index]+" level",28.0f/54);
            set(instrument.value,params,names[index]+" pan",.5f);set(instrument.value,params,names[index]+" octave",.5f);
            set(instrument.value,params,names[index]+" MIDI channel, 0 is all",0);
        }
        for (const auto& [name,value]:std::map<std::string,float>{{"Playing mode",0},{"Articulation",.5f},{"Output",.875f},{"Tone",.5f},{"Attack accent",0},{"Sax voice",0},{"Baritone keys",0},{"Baritone airflow",0},{"Tenor detail MIDI",1},{"Tenor detail level",48.0f/84}})
            set(instrument.value,params,name,value);
        const auto formatFlags=static_cast<AudioFormatFlags>(kAudioFormatFlagsNativeFloatPacked)|static_cast<AudioFormatFlags>(kAudioFormatFlagIsNonInterleaved);
        AudioStreamBasicDescription format{rate,kAudioFormatLinearPCM,formatFlags,4,1,4,2,32,0};
        checked(AudioUnitSetProperty(instrument.value,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Output,0,&format,sizeof(format)),"Set instrument output format");
        checked(AudioUnitSetProperty(instrument.value,kAudioUnitProperty_MaximumFramesPerSlice,kAudioUnitScope_Global,0,&maximum,sizeof(maximum)),"Set maximum instrument slice");
        checked(AudioUnitInitialize(instrument.value),"Initialize instrument");
        std::string image;
        for (UInt32 index=0;index<_dyld_image_count();++index) {
            const std::string path=_dyld_get_image_name(index);
            if (path.ends_with("TopBrass.component/Contents/MacOS/TopBrass")) image=path;
        }
        if (image.empty()) throw std::runtime_error("Loaded TopBrass component image not located");
        context.instrument=instrument.value;context.control=mode=="control";context.rows.resize(static_cast<std::size_t>(count));context.left.resize(maximum);context.right.resize(maximum);
        output.open({kAudioUnitType_Output,kAudioUnitSubType_DefaultOutput,kAudioUnitManufacturer_Apple,0,0});
        checked(AudioUnitSetProperty(output.value,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input,0,&format,sizeof(format)),"Set output client format");
        AURenderCallbackStruct callback{render,&context};
        checked(AudioUnitSetProperty(output.value,kAudioUnitProperty_SetRenderCallback,kAudioUnitScope_Input,0,&callback,sizeof(callback)),"Set output callback");
        checked(AudioUnitInitialize(output.value),"Initialize output");
        const bool overloadSupported=observer.attach(device,context);
        checked(AudioOutputUnitStart(output.value),"Start silent-output audit");
        const auto timeout=std::chrono::steady_clock::now()+std::chrono::seconds(300);
        while (context.published.load(std::memory_order_acquire)<context.rows.size()) {
            if(std::chrono::steady_clock::now()>timeout) {AudioOutputUnitStop(output.value);throw std::runtime_error("Audio callback observation timeout");}
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        checked(AudioOutputUnitStop(output.value),"Stop audit");
        observer.detach();
        if (property<AudioDeviceID>(kAudioObjectSystemObject,kAudioHardwarePropertyDefaultOutputDevice)!=device
            || property<Float64>(device,kAudioDevicePropertyNominalSampleRate)!=rate
            || property<UInt32>(device,kAudioDevicePropertyBufferFrameSize)!=deviceFrames) throw std::runtime_error("Device configuration changed during audit");
        mach_timebase_info_data_t timebase{};checked(mach_timebase_info(&timebase),"Read host time scale");
        const double tickUs=static_cast<double>(timebase.numer)/timebase.denom/1000;
        std::ofstream csv(prefix.string()+".csv"),json(prefix.string()+".json");
        csv<<std::setprecision(12)<<"callback,frames,sample_time,host_time,entry_host_time,exit_host_time,timestamp_flags,callback_wall_us,callback_cpu_us,au_render_wall_us,au_render_cpu_us,energy,render_status,start_gap_us,host_time_lead_us\n";
        std::vector<double> wall,cpu;int misses=0,startupMisses=0,errors=0,nonzero=0,discontinuities=0,validHost=0,validSample=0;double energy=0;bool finite=true;
        for (std::size_t index=0;index<context.rows.size();++index) {
            const auto& row=context.rows[index];
            const bool sampleValid=row.timestampFlags&kAudioTimeStampSampleTimeValid;
            if(sampleValid) ++validSample;
            if(row.timestampFlags&kAudioTimeStampHostTimeValid) ++validHost;
            if (index>0 && sampleValid && (context.rows[index-1].timestampFlags&kAudioTimeStampSampleTimeValid)
                && std::abs(row.sampleTime-context.rows[index-1].sampleTime-context.rows[index-1].frames)>.5) ++discontinuities;
            csv<<index<<','<<row.frames<<','<<row.sampleTime<<','<<row.timestampHost<<','<<row.entry<<','<<row.exit<<','<<row.timestampFlags<<','<<row.wall<<','<<row.cpu<<','<<row.renderWall<<','<<row.renderCpu<<','<<row.energy<<','<<row.status<<','<<(index?(row.entry-context.rows[index-1].entry)*tickUs:0)<<','<<(static_cast<double>(row.timestampHost)-static_cast<double>(row.entry))*tickUs<<'\n';
            const double budget=row.frames/rate*1e6;
            if (row.wall>budget) {if(index==0) ++startupMisses;else ++misses;}
            if (row.status!=noErr || row.cpu<0) ++errors;
            if (row.energy>0) ++nonzero;
            finite=finite && std::isfinite(row.energy);energy+=row.energy;
            if (index>0) {wall.push_back(row.wall);cpu.push_back(row.cpu);}
        }
        std::sort(wall.begin(),wall.end());std::sort(cpu.begin(),cpu.end());
        json<<std::setprecision(12)<<"{\"mode\":"<<std::quoted(mode)<<",\"component_version\":"<<version<<",\"component_image\":"<<std::quoted(image)<<",\"callbacks\":"<<count<<",\"device_id\":"<<device<<",\"sample_rate\":"<<rate<<",\"device_buffer_frames\":"<<deviceFrames<<",\"callback_qos\":"<<context.qos<<",\"default_time_constraint_policy\":"<<(context.defaultPolicy?"true":"false")<<",\"thread_policy_status\":"<<context.policyStatus
            <<",\"callback_wall_p99_us\":"<<wall[wall.size()*99/100]<<",\"callback_wall_max_us\":"<<wall.back()<<",\"callback_cpu_p99_us\":"<<cpu[cpu.size()*99/100]<<",\"callback_cpu_max_us\":"<<cpu.back()<<",\"steady_service_budget_overruns\":"<<misses<<",\"startup_service_budget_overruns\":"<<startupMisses<<",\"startup_callback_wall_us\":"<<context.rows.front().wall<<",\"startup_callback_cpu_us\":"<<context.rows.front().cpu<<",\"render_errors\":"<<errors<<",\"valid_host_timestamp_callbacks\":"<<validHost<<",\"valid_sample_timestamp_callbacks\":"<<validSample<<",\"sample_time_discontinuities\":"<<discontinuities<<",\"nonzero_render_callbacks\":"<<nonzero<<",\"finite_energy\":"<<(finite?"true":"false")<<",\"pre_silencing_energy\":"<<energy<<",\"device_overload_notifications_supported\":"<<(overloadSupported?"true":"false")<<",\"device_overload_notifications\":"<<context.overloads.load()<<",\"device_settings_changed\":false,\"output_silent\":true,\"microphone_input\":false,\"workload\":\"128 held tenor MIDI notes on4channels, CC1=100 onchannel1, recorded breath CC20 on8channels rearmed near every12800frames. Control skips all MIDI and AU render.\",\"limits\":\"Callback service-time window includes MIDI dispatch, rendering and energy measurement, excluding final telemetry publication. Overruns compare service time to frames/rate, not actual hardware dropout. Device overload notifications cannot attribute other clients.\"}\n";
        if (!csv || !json) throw std::runtime_error("Write audit result");
        if (errors || !finite || (!context.control && nonzero<count-1) || (context.control && nonzero)) throw std::runtime_error("Actual callback render validation failed");
        std::cout<<mode<<": "<<count<<" callbacks, "<<rate<<"Hz, "<<deviceFrames<<" device frames, "<<misses<<" steady service-budget overruns, "<<errors<<" render errors\n";
        if (validHost!=count || validSample!=count || discontinuities || context.overloads.load()!=0) {
            std::cerr<<"Device qualification is inconclusive: timestamp integrity or device overload observations require review\n";
            return 3;
        }
        return misses || startupMisses?2:0;
    } catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
