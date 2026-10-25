#include "Engine.h"
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void word(std::ostream& out,std::uint32_t value) {out.write(reinterpret_cast<const char*>(&value),4);}
void scalar(std::ostream& out,float value) {out.write(reinterpret_cast<const char*>(&value),4);}
void fixture(const std::filesystem::path& path,int missing=-1,bool mapped=true,int invalidArt=-1,bool shortLoop=false) {
    std::ofstream out(path,std::ios::binary);out.write("TBBANK04",8);
    word(out,17-(missing>=0));word(out,mapped?5-(missing>=0):0);
    for(int index=0;index<17;++index) {
        if(index==missing)continue;
        const int instrument=index<8?index/2:index<12?4:6;
        const int art=index<8?index%2:index<12?index-8:index==12?0:index<15?1:2;
        const bool loop=index==12 || (shortLoop && index==13);
        for(int value:{instrument,index==16&&invalidArt>=0?invalidArt:art,60,1,48000,48000,
                      loop?4000:0,loop?24000:0,loop?1920:0,index==12?38400:0,index==12?1920:0,loop?1:0,0,0,1})word(out,static_cast<std::uint32_t>(value));
        for(int frame=0;frame<48000;++frame) {
            float value=index<12?.02f:.1f*(index-11);
            if(index==12)value+=.02f*std::sin(2*3.14159265358979323846f*480*frame/48000);
            if(frame>=38400)value*=static_cast<float>(47999-frame)/9599;
            scalar(out,value);
        }
    }
    if(mapped)for(int index=12;index<17;++index) {
        if(index==missing)continue;
        const int art=index==12?0:index<15?1:2;
        const int low=index==14?65:index==16?86:0,high=index==13?64:index==15?85:127;
        for(int value:{index-(missing>=0&&index>missing),6,index==16&&invalidArt>=0?invalidArt:art,60,59,61,low,high,0,0})word(out,static_cast<std::uint32_t>(value));
        scalar(out,0);scalar(out,1);
    }
}
topbrass::Settings settings(topbrass::TubaVoice voice,topbrass::Articulation art) {
    topbrass::Settings result;result.tubaVoice=voice;result.articulation=art;result.gain=.5f;
    for(auto& part:result.parts){part.enabled=false;part.gain=.1f;part.octave=0;}
    result.parts[3].enabled=true;result.parts[3].pan=-1;result.parts[0].pan=1;
    return result;
}
std::array<float,2> frame(topbrass::Engine& engine) {
    std::array<float,2> result{};engine.render(&result[0],&result[1],1);return result;
}
std::vector<float> advance(topbrass::Engine& engine,int count) {
    std::vector<float> left(static_cast<std::size_t>(count)),right(left.size());engine.render(left.data(),right.data(),count);
    for(std::size_t i=0;i<left.size();++i)check(std::isfinite(left[i])&&std::isfinite(right[i]),"War playback must remain finite");
    return left;
}
void playback(const topbrass::Bank& bank) {
    using namespace topbrass;
    for(auto voice:{TubaVoice::warStaccato,TubaVoice::warStaccatissimo}) {
        auto config=settings(voice,Articulation::shortNote);Engine engine(bank);engine.setSettings(config);engine.prepare(48000);
        check(engine.partKeyRange(3).low==59 && engine.partKeyRange(3).high==61,"War Short must expose its mapped key range");
        for(int velocity:{64,65,85,86,127}) {
            engine.prepare(48000);engine.noteOn(1,60,velocity/127.f);const auto audio=frame(engine);
            const float sample=voice==TubaVoice::warStaccato?(velocity<=64?.2f:.3f):(velocity<=85?.4f:.5f);
            check(std::abs(audio[0]-sample*std::pow(velocity/127.f,1.3f)*.05f)<1e-7f,"Each War style must select its eligible recorded force");
            engine.controller(1,64,127);engine.noteOff(1,60);advance(engine,6000);engine.controller(1,64,0);
            check(engine.activeVoices()==1,"Recorded Short endings must survive note-off and pedal-up");
            advance(engine,50000);check(engine.activeVoices()==0,"Recorded Short must finish at finite EOF");
        }
        engine.prepare(48000);engine.noteOnPart(3,1,60,60,1);advance(engine,500);
        engine.noteOnPart(3,1,60,60,1);advance(engine,4000);
        check(engine.activeVoices()==1,"Retonguing must retire the previous recorded Short");
        engine.allNotesOff(0,true);const auto silent=frame(engine);check(silent[0]==0 && engine.activeVoices()==0,"Panic must clear War voices");
        config.articulation=Articulation::sustain;engine.setSettings(config);engine.prepare(48000);engine.noteOn(1,60,1);
        advance(engine,120000);check(engine.activeVoices()==1,"War sustain must continue through its loop");
        engine.noteOff(1,60);advance(engine,16000);check(engine.activeVoices()==0,"War sustain note-off must finish its recorded file decay");
    }
    for(auto art:{Articulation::shortNote,Articulation::sustain}) {
        auto config=settings(TubaVoice::warStaccato,art);config.parts[0].enabled=true;
        Engine engine(bank),reference(bank);engine.setSettings(config);reference.setSettings(config);engine.prepare(48000);reference.prepare(48000);
        engine.noteOn(1,60,1);reference.noteOn(1,60,1);
        config.tubaVoice=TubaVoice::warStaccatissimo;engine.setSettings(config);advance(engine,4000);advance(reference,4000);
        const auto changed=frame(engine),held=frame(reference);
        check(std::abs(changed[1]-held[1])<1e-7f,"Changing tuba voice must preserve another held instrument");
        if(art==Articulation::sustain)check(changed==held && engine.activeVoices()==2,"War style changes must preserve the shared held sustain");
        else check(std::abs(changed[0])<1e-7f && engine.activeVoices()==1,"War Short style changes must release the old articulation without retriggering");
        config.tubaVoice=TubaVoice::original;engine.setSettings(config);advance(engine,4000);
        check(std::abs(frame(engine)[0])<1e-7f,"Returning to Original must release held War notes");
        engine.noteOnPart(3,1,60,60,1);check(std::abs(frame(engine)[0]-.001f)<1e-7f,"The next note must use Original after a source change");
    }
    {
        auto config=settings(TubaVoice::warStaccato,Articulation::fall);Engine engine(bank);engine.setSettings(config);engine.prepare(48000);engine.noteOn(1,60,1);
        auto audio=advance(engine,20000);
        const auto crossings=[&](int start,int end){int count=0;for(int i=start+1;i<end;++i)if(audio[static_cast<std::size_t>(i-1)]<.005f && audio[static_cast<std::size_t>(i)]>=.005f)++count;return count;};
        check(crossings(12000,16000)<crossings(2000,6000),"Synthetic Fall must descend instead of substituting a recorded Short");
        advance(engine,96000);check(engine.activeVoices()==0,"Synthetic Fall must terminate even with a held key");
    }
}

}
int main(int argc,char** argv) {
    try {
        check(argc==2,"Expected fixture directory");std::filesystem::create_directories(argv[1]);
        const auto path=std::filesystem::path(argv[1])/"war-tuba.bank";fixture(path);
        topbrass::Bank bank;std::string error;
        check(bank.load(path,error),"Optional mapped tuba sustain/staccato/staccatissimo must load without changing required sources");
        playback(bank);
        for(int variant=0;variant<4;++variant) {
            fixture(path,variant==0?12:-1,variant!=1,variant==2?3:-1,variant==3);
            check(!bank.load(path,error),"Incomplete optional capabilities, unsupported articulations and Short loops must be rejected");
            check(bank.samples().size()==17 && bank.isMapped(6,2),"Invalid replacement must retain the previous complete bank");
        }
        std::cout<<"PASS:War source capabilities, force selection, loops/decays, Short endings, style/source changes, independent held parts, retongue, panic and synthetic Fall\n";
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
