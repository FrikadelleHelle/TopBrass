#include "Engine.h"
#include <array>
#include <cmath>
#include <fstream>
#include <vector>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
void word(std::ostream& out,std::uint32_t value) {out.write(reinterpret_cast<const char*>(&value),sizeof(value));}
void scalar(std::ostream& out,float value) {out.write(reinterpret_cast<const char*>(&value),sizeof(value));}
void fixture(const std::filesystem::path& path,int optionalArt=0,bool mapped=true,bool looped=false) {
    std::ofstream out(path,std::ios::binary);
    out.write("TBBANK04",8);word(out,15);word(out,mapped?6:0);
    for (int index=0;index<15;++index) {
        const int instrument=index<8?index/2:index<12?4:5;
        const int articulation=index<8?index%2:index<12?index-8:optionalArt;
        const int root=index==13?61:60,frames=index<12?48000:24000;
        for(int value:{instrument,articulation,root,index==14?2:1,48000,frames,0,index>=12 && looped?12000:0,0,index>=12?19200:0,index>=12?1920:0,0,0,0,1}) word(out,static_cast<std::uint32_t>(value));
        for(int frame=0;frame<frames;++frame) {
            float sample=index<12?.02f:(index==12?.10f:index==13?.15f:.25f)+frame*.000001f;
            if(index>=12 && frame>=19200) sample*=static_cast<float>(frames-1-frame)/(frames-1-19200);
            scalar(out,sample);
        }
    }
    if(mapped) for(int force=0;force<2;++force) for(int take=0;take<3;++take) {
        const int sample=force?14:take==1?13:12;
        for(int value:{sample,5,optionalArt,sample==13?61:60,60,60,force?101:0,force?127:100,0,take}) word(out,static_cast<std::uint32_t>(value));
        scalar(out,0);scalar(out,1);
    }
}
topbrass::Settings settings() {
    topbrass::Settings result;
    result.articulation=topbrass::Articulation::sustain;
    result.gain=.5f;
    for(auto& part:result.parts) {part.enabled=false;part.gain=.1f;part.octave=0;}
    result.parts[0].enabled=true;result.parts[0].pan=-1;
    result.parts[4].enabled=true;result.parts[4].pan=1;
    result.parts[0].tenorSource=topbrass::TenorSource::mtg;
    return result;
}
std::array<float,2> frame(topbrass::Engine& engine) {
    std::array<float,2> result{};engine.render(&result[0],&result[1],1);return result;
}
void advance(topbrass::Engine& engine,int count) {
    std::vector<float> left(static_cast<std::size_t>(count)),right(left.size());
    engine.render(left.data(),right.data(),count);
    for(std::size_t i=0;i<left.size();++i) check(std::isfinite(left[i]) && std::isfinite(right[i]),"Playback must stay finite");
}
void playback(const topbrass::Bank& bank) {
    for(auto articulation:{topbrass::Articulation::sustain,topbrass::Articulation::shortNote,topbrass::Articulation::fall}) {
        auto config=settings();config.articulation=articulation;
        topbrass::Engine engine(bank);engine.setSettings(config);engine.prepare(48000);
        check(engine.partKeyRange(0).low==60 && engine.partKeyRange(0).high==60,"Alternate tenor must expose its mapped concert range for every playing articulation");
        engine.noteOn(1,60,100.f/127);
        auto audio=frame(engine);
        const float force=std::pow(100.f/127,1.3f)*.05f;
        check(std::abs(audio[0]-.10f*force)<1e-7f,"Velocity100 must play the alternate soft recording");
        check(std::abs(audio[1]-.02f*force)<1e-7f,"Other sax slot must retain its independently selected original source");
        engine.allNotesOff(0,true);
        engine.noteOn(1,60,101.f/127);audio=frame(engine);
        check(std::abs(audio[0]-.25f*std::pow(101.f/127,1.3f)*.05f)<1e-7f,"Velocity101 must cross into the alternate strong recording");
        engine.allNotesOff(0,true);
        engine.prepare(48000);
        engine.noteOnPart(0,1,60,60,1);
        advance(engine,30000);
        check(engine.activeVoices()==0,"An alternate held note must finish at its finite recording end without looping");
        engine.noteOnPart(0,1,60,60,1);advance(engine,1000);engine.noteOffPart(0,1,60);
        advance(engine,14000);
        check(engine.activeVoices()==0,"Every alternate articulation must respond to note-off, including gated Short");
    }
    {
        auto config=settings();config.parts[4].tenorSource=topbrass::TenorSource::mtg;
        topbrass::Engine engine(bank);engine.setSettings(config);engine.prepare(48000);
        engine.noteOn(1,60,100.f/127);const auto audio=frame(engine);
        check(audio[0]>0 && audio[1]>0 && std::abs(audio[0]-audio[1])>1e-5f,"Paired alternate saxes must select distinct eligible actual recordings");
    }
    {
        auto config=settings();topbrass::Engine engine(bank),reference(bank);
        engine.setSettings(config);reference.setSettings(config);engine.prepare(48000);reference.prepare(48000);
        engine.noteOn(1,60,1);reference.noteOn(1,60,1);
        config.parts[0].tenorSource=topbrass::TenorSource::original;engine.setSettings(config);
        advance(engine,4000);advance(reference,4000);
        const auto changed=frame(engine),held=frame(reference);
        check(std::abs(changed[0])<1e-7f && std::abs(changed[1]-held[1])<1e-7f,"Changing one sax source must release only that sax and preserve the other held player");
        engine.noteOnPart(0,1,60,60,1);
        check(std::abs(frame(engine)[0]-.001f)<1e-7f,"The next note must use the newly selected original recording");
        engine.allNotesOff(0,true);const auto silent=frame(engine);
        check(engine.activeVoices()==0 && silent[0]==0 && silent[1]==0,"Panic must clear alternate and original voices together");
    }
    {
        auto config=settings();config.saxVoice=topbrass::SaxVoice::baritone;
        topbrass::Engine engine(bank),reference(bank);
        engine.setSettings(config);reference.setSettings(config);engine.prepare(48000);reference.prepare(48000);
        engine.noteOn(1,60,1);reference.noteOn(1,60,1);
        config.parts[0].tenorSource=topbrass::TenorSource::original;engine.setSettings(config);
        advance(engine,4000);advance(reference,4000);
        const auto changed=frame(engine),held=frame(reference);
        check(changed==held && engine.activeVoices()==2,"An inactive tenor source choice must not disturb held baritone playback");
    }
}

}
int main(int argc,char** argv) {
    try {
        check(argc==2,"Expected fixture directory");
        std::filesystem::create_directories(argv[1]);
        const auto path=std::filesystem::path(argv[1])/"alternate-tenor.bank";
        fixture(path);
        topbrass::Bank bank;std::string error;
        check(bank.load(path,error),"A mapped sustain-only alternate tenor must load beside the five required instruments");
        check(bank.keyRange(5,0).low==60 && bank.keyRange(5,1).high<0,"Alternate tenor must not claim recorded Short coverage");
        playback(bank);
        for(int variant=0;variant<3;++variant) {
            fixture(path,variant==0?1:0,variant!=1,variant==2);
            check(!bank.load(path,error),"Unsupported alternate articulations, absent mappings and loops must be rejected");
            check(bank.samples().size()==15 && bank.isMapped(5,0),"Invalid replacement must preserve the previously loaded bank");
        }
        std::cout<<"PASS: independent sources, force boundary, distinct paired recordings, gated articulations, finite EOF, source changes, baritone override, panic and transactional validation\n";
    } catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
