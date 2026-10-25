#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok,const juce::String& error) { if(!ok)throw std::runtime_error(error.toStdString()); }
void set(juce::AudioPluginInstance& plugin,const juce::String& name,float normalized,bool required=true) {
    for(auto* p:plugin.getParameters()) if(p->getName(256)==name) {p->setValueNotifyingHost(normalized);return;}
    if(required)throw std::runtime_error("Plugin parameter not found: "+name.toStdString());
}
void write(const juce::File& path,const juce::AudioBuffer<float>& audio,double rate) {
    auto stream=path.createOutputStream();check(stream!=nullptr,"Create output");stream->setPosition(0);stream->truncate();
    std::unique_ptr<juce::OutputStream> output=std::move(stream);
    auto writer=juce::WavAudioFormat{}.createWriterFor(output,juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(2).withBitsPerSample(32));
    check(writer!=nullptr&&writer->writeFromAudioSampleBuffer(audio,0,audio.getNumSamples()),"Write plugin WAV");
}
}
int main(int argc,char** argv) {
    const juce::ScopedJuceInitialiser_GUI gui;
    try {
        check(argc==3,"Usage: RecordingAudit /installed/TopBrass.vst3-or-component /audit/config.json");
        const auto config=juce::JSON::parse(juce::File(juce::String::fromUTF8(argv[2])));
        const juce::File bundle(juce::String::fromUTF8(argv[1]));
        std::unique_ptr<juce::AudioPluginFormat> format;
        if(bundle.hasFileExtension("vst3"))format=std::make_unique<juce::VST3PluginFormat>();
       #if JUCE_MAC
        else if(bundle.hasFileExtension("component"))format=std::make_unique<juce::AudioUnitPluginFormat>();
       #endif
        check(format!=nullptr,"Expected a VST3 or supported Audio Unit bundle");
        juce::AudioPluginFormatManager manager;auto* backend=format.get();manager.addFormat(std::move(format));
        juce::OwnedArray<juce::PluginDescription> types;backend->findAllTypesForFile(types,bundle.getFullPathName());
        check(types.size()==1,"Expected exactly one instrument in the requested format");
        const auto* cases=config["cases"].getArray();check(cases!=nullptr,"Read cases");
        juce::String error; auto plugin=manager.createPluginInstance(*types[0],48000,512,error); check(plugin!=nullptr,error);
        juce::AudioProcessorParameter* feelParameter=nullptr;
        for(auto* parameter:plugin->getParameters()) if(parameter->getName(256)=="Horn feel") feelParameter=parameter;
        check(feelParameter!=nullptr,"Horn feel parameter is required");
        const float defaultFeel=feelParameter->getValue();
        for(const auto& item:*cases) {
            const double rate=static_cast<double>(item["rate"]);
            const bool ensemble=static_cast<bool>(item["ensemble"]);
            for(const auto& variant:juce::StringArray{"neutral","current-tone"}) {
                const bool paired=static_cast<bool>(item["paired_players"]);
                const bool split=static_cast<bool>(item["split_players"]);
                const int primary=static_cast<int>(item["part"]);
                check(!paired || primary==0 || primary==1,"Paired players require primary sax or trumpet");
                check(!split || paired,"Split players requires a paired capture");
                const bool isolate=item.hasProperty("isolate_player");
                const int isolated=static_cast<int>(item["isolate_player"]);
                check(!isolate || (isolated>=0 && isolated<6 && static_cast<double>(item["isolate_player"])==isolated),"Isolation requires a player index0..5");
                const float feel=item.hasProperty("horn_feel")?static_cast<float>(item["horn_feel"]):defaultFeel;
                check(std::isfinite(feel) && feel>=0 && feel<=1,"Horn feel must be finite and normalized");
                const bool extra=static_cast<bool>(item["extra_players"]) || paired;
                for(int part=0;part<6;++part) {
                    const juce::String label=juce::StringArray{"Saxophone","Trumpet","Trombone","Tuba","Saxophone II","Trumpet II"}[part];
                    const bool enabled=paired ? part==primary || part==primary+4 : (part<4||extra)&&(ensemble||part==primary);
                    set(*plugin,label+" on",enabled?1:0,part<4||extra);
                    set(*plugin,label+" level",36.0f/54,part<4||extra);set(*plugin,label+" pan",isolate?(part==isolated?0:1):split?(part==primary?0:1):.5f,part<4||extra);set(*plugin,label+" octave",ensemble&&(part==2||part==3)?(part==2?.25f:0):.5f,part<4||extra);set(*plugin,label+" MIDI channel, 0 is all",0,part<4||extra);
                }
                set(*plugin,"Output",.75f);set(*plugin,"Tone",variant=="neutral"?.5f:0);
                set(*plugin,"Attack accent",0);set(*plugin,"Baritone keys",static_cast<float>(item["keys"]));set(*plugin,"Baritone airflow",static_cast<float>(item["airflow"]));
                set(*plugin,"Playing mode",ensemble?1:0);
                if(feelParameter->getValue()!=feel) feelParameter->setValueNotifyingHost(feel);
                const bool tenorDetail=static_cast<bool>(item["tenor_detail_midi"]);
                set(*plugin,"Tenor detail MIDI",tenorDetail?1:0,tenorDetail);
                set(*plugin,"Tenor detail level",(static_cast<float>(item["tenor_detail_db"])+48)/84,tenorDetail);
                const bool doubled=static_cast<bool>(item["double_tongue"]);
                set(*plugin,"Trumpet double tongue",doubled?1:0,doubled);set(*plugin,"Trombone double tongue",doubled?1:0,doubled);
                for(int part : {0,4}) {
                    const juce::String label=part==0?"Saxophone":"Saxophone II";
                    const bool alternate=static_cast<bool>(item[part==0?"alternate_tenor":"alternate_tenor_ii"]);
                    set(*plugin,label+" tenor source",alternate?1:0,alternate);
                }
                const int tubaVoice=static_cast<int>(item["tuba_voice"]);
                check(tubaVoice>=0 && tubaVoice<=2,"Tuba voice must be Original, War staccato or War staccatissimo");
                set(*plugin,"Tuba voice",tubaVoice*.5f,tubaVoice!=0);
                set(*plugin,"Sax voice",static_cast<int>(item["instrument"])==4?.25f:0);
                set(*plugin,"Articulation",static_cast<int>(item["articulation"])==0?.5f:0);
                plugin->setNonRealtime(true);plugin->prepareToPlay(rate,512);
                juce::AudioBuffer<float> warm(2,512);juce::MidiBuffer empty;
                for(int i=0;i<24;++i){warm.clear();plugin->processBlock(warm,empty);}
                const int hold=static_cast<int>(item["frames"]);
                const int ending=item.hasProperty("post_release_frames")?static_cast<int>(item["post_release_frames"]):static_cast<int>(rate*.25);
                check(ending>=0 && ending<=rate*60,"Invalid release capture duration");
                const int total=hold+ending;
                juce::AudioBuffer<float> output(2,total);output.clear();
                juce::MidiBuffer timeline;
                if(const auto* events=item["midi_events"].getArray()) {
                    for(const auto& row:*events) {
                        const auto* values=row.getArray();check(values!=nullptr&&values->size()==4,"MIDI events require frame/status/data1/data2");
                        const int frame=static_cast<int>((*values)[0]),status=static_cast<int>((*values)[1]);
                        const int data1=static_cast<int>((*values)[2]),data2=static_cast<int>((*values)[3]);
                        check(frame>=0&&frame<total&&status>=128&&status<240&&data1>=0&&data1<128&&data2>=0&&data2<128,"Invalid MIDI event");
                        check((status&240)==128||(status&240)==144||(status&240)==176||(status&240)==224,"Only note, controller and pitch-wheel events are supported");
                        const std::array<juce::uint8,3> bytes{static_cast<juce::uint8>(status),static_cast<juce::uint8>(data1),static_cast<juce::uint8>(data2)};
                        timeline.addEvent(juce::MidiMessage(bytes.data(),3),frame);
                    }
                } else {
                    timeline.addEvent(juce::MidiMessage::controllerEvent(1,11,127),0);timeline.addEvent(juce::MidiMessage::controllerEvent(1,1,0),0);
                    timeline.addEvent(juce::MidiMessage::noteOn(1,static_cast<int>(item["root"]),static_cast<juce::uint8>(static_cast<int>(item["velocity"]))),0);
                    timeline.addEvent(juce::MidiMessage::noteOff(1,static_cast<int>(item["root"])),hold);
                }
                for(int offset=0;offset<total;offset+=512) {
                    const int count=std::min(512,total-offset);
                    std::array<float*,2> ptr{output.getWritePointer(0)+offset,output.getWritePointer(1)+offset};juce::AudioBuffer<float> block(ptr.data(),2,count);juce::MidiBuffer midi;
                    midi.addEvents(timeline,offset,count,-offset);
                    plugin->processBlock(block,midi);
                }
                const auto file=juce::File(item["directory"].toString()).getChildFile(variant+"-plugin-raw.wav");write(file,output,rate);
                juce::DynamicObject::Ptr info=new juce::DynamicObject;info->setProperty("plugin",types[0]->name);info->setProperty("version",types[0]->version);info->setProperty("latency_samples",plugin->getLatencySamples());info->setProperty("warmup_frames",24*512);
                info->setProperty("format",types[0]->pluginFormatName);info->setProperty("identifier",types[0]->fileOrIdentifier);
                juce::Array<juce::var> parameters;
                for(auto* p:plugin->getParameters()){juce::DynamicObject::Ptr row=new juce::DynamicObject;row->setProperty("name",p->getName(256));row->setProperty("normalized",p->getValue());row->setProperty("text",p->getText(p->getValue(),256));parameters.add(juce::var(row.get()));}
                info->setProperty("parameters",parameters);file.getSiblingFile(variant+"-host.json").replaceWithText(juce::JSON::toString(juce::var(info.get())));
                plugin->releaseResources();std::cout<<item["id"].toString()<<" / "<<variant<<": "<<output.getNumSamples()<<" frames, peak="<<output.getMagnitude(0,total)<<'\n';
            }
        }
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
