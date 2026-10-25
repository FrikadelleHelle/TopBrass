#include "Engine.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Audio {std::vector<float> left,right;};
Audio render(topbrass::Engine& engine,int frames,int block=128) {
    Audio result{std::vector<float>(frames),std::vector<float>(frames)};
    for(int offset=0;offset<frames;offset+=block)engine.render(result.left.data()+offset,result.right.data()+offset,std::min(block,frames-offset));
    return result;
}
topbrass::Settings neutral() {
    topbrass::Settings settings;for(auto& part:settings.parts)part.enabled=false;
    settings.parts[0]={true,.125f,0,0,0};settings.gain=1;return settings;
}
}
int main(int argc,char** argv) {
    try {
        check(argc==4,"Expected musical bank, tenor detail bank and fixture directory");
        topbrass::Bank bank;std::string error;check(bank.load(argv[1],error),error.c_str());
        const auto before=bank.bytes();check(bank.loadTenorDetails(argv[2],error),error.c_str());
        check(bank.bytes()>before,"Detail PCM must be included in memory accounting");
        auto settings=neutral();
        for(auto kind:{topbrass::TenorDetail::breath,topbrass::TenorDetail::keys}) {
            topbrass::Engine engine(bank);engine.setSettings(settings);engine.prepare(48000);
            const int count=kind==topbrass::TenorDetail::breath?64:33;
            for(int take=0;take<count+1;++take) {
                const auto* source=bank.tenorDetail(kind,static_cast<std::uint32_t>(take));check(source&&source->rate==48000,"Expected native source");
                engine.triggerTenorDetail(kind);const auto output=render(engine,source->frames());
                float worst=0;const float gain=.125f*std::cos(3.14159265358979323846f*.25f);
                for(std::size_t frame=0;frame<output.left.size();++frame) {
                    const float expected=frame<source->audio.size()?source->audio[frame]*gain:0;
                    worst=std::max({worst,std::abs(output.left[frame]-expected),std::abs(output.right[frame]-expected)});
                }
                check(worst<1e-8f,"Every native detail take must retain complete PCM and level");
                check(engine.activeVoices()==0,"Exact block-edge EOF must terminate details");
                const auto silence=render(engine,256);check(std::all_of(silence.left.begin(),silence.left.end(),[](float x){return x==0;}),"Detail must remain silent after EOF");
            }
        }
        const auto play=[&](int block,double rate) {
            topbrass::Engine engine(bank);engine.setSettings(settings);engine.prepare(rate);engine.triggerTenorDetail(topbrass::TenorDetail::breath);
            return render(engine,50000,block);
        };
        for(double rate:{44100.,48000.,96000.}) {
            const auto a=play(37,rate),b=play(512,rate);check(a.left==b.left&&a.right==b.right,"Detail playback must be block invariant");
        }
        topbrass::Engine engine(bank);engine.setSettings(settings);engine.prepare(48000);
        engine.triggerTenorDetail(topbrass::TenorDetail::breath);const auto plain=render(engine,2048);
        auto colored=settings;colored.tone=1;colored.tightness=1;
        engine.setSettings(colored);engine.prepare(48000);engine.controller(1,11,0);engine.controller(1,1,127);engine.pitchWheel(1,16383);
        engine.triggerTenorDetail(topbrass::TenorDetail::breath);const auto bypass=render(engine,2048);
        check(plain.left==bypass.left&&plain.right==bypass.right,"Detail must bypass musical color, pressure, vibrato and bend");
        auto controlled=settings;controlled.tenorDetailGain=.25f;controlled.parts[0].pan=-1;
        engine.setSettings(controlled);engine.prepare(48000);engine.controller(1,7,64);engine.triggerTenorDetail(topbrass::TenorDetail::keys);
        const auto level=render(engine,512);const auto* keySource=bank.tenorDetail(topbrass::TenorDetail::keys,0);
        for(std::size_t frame=0;frame<level.left.size();++frame)check(std::abs(level.left[frame]-keySource->audio[frame]*.125f*.25f*(64.f/127))<1e-8f && level.right[frame]==0,"Details must follow source gain, part pan and CC7");
        engine.setSettings(settings);engine.prepare(48000);
        engine.triggerTenorDetail(topbrass::TenorDetail::breath,1);engine.triggerTenorDetail(topbrass::TenorDetail::keys,2);
        engine.allNotesOff(1,true);check(engine.activeVoices()==1,"Detail panic must respect channels");
        engine.allNotesOff(0,true);check(engine.activeVoices()==0,"Global panic must stop details immediately");
        engine.triggerTenorDetail(topbrass::TenorDetail::breath);render(engine,128);
        auto muted=settings;muted.parts[0].enabled=false;engine.setSettings(muted);const auto quiet=render(engine,1024);check(engine.activeVoices()==0 && std::all_of(quiet.left.end()-512,quiet.left.end(),[](float x){return x==0;}),"Mute must end details");
        engine.triggerTenorDetail(topbrass::TenorDetail::breath);check(engine.activeVoices()==0,"Muted sax must reject details");
        engine.setSettings(settings);engine.triggerTenorDetail(topbrass::TenorDetail::keys);render(engine,128);
        auto baritone=settings;baritone.saxVoice=topbrass::SaxVoice::baritone;engine.setSettings(baritone);render(engine,512);
        check(engine.activeVoices()==0,"Sax switching must stop tenor detail");engine.triggerTenorDetail(topbrass::TenorDetail::keys);check(engine.activeVoices()==0,"Baritone must reject tenor details");
        settings.articulation=topbrass::Articulation::sustain;engine.setSettings(settings);engine.prepare(48000);engine.noteOn(1,60,.8f);const int musical=engine.activeVoices();
        for(int hit=0;hit<1000;++hit){engine.triggerTenorDetail(topbrass::TenorDetail::keys);render(engine,1);check(engine.activeVoices()<=musical+topbrass::maxTenorDetails+topbrass::maxTenorDetailTails,"Detail polyphony must be bounded");}
        render(engine,96000);check(engine.activeVoices()==musical,"Recorded detail must not steal the held musical voice");
        engine.allNotesOff(0,true);check(engine.activeVoices()==0,"Stress panic must clear every voice");
        const auto fixture=std::filesystem::path(argv[3])/"tenor-details-invalid.bank";
        {std::ofstream output(fixture,std::ios::binary);output<<"TBTEN001";}
        const auto* retained=bank.tenorDetail(topbrass::TenorDetail::breath,0);
        check(!bank.loadTenorDetails(fixture,error)&&bank.tenorDetail(topbrass::TenorDetail::breath,0)==retained,"Invalid reload must preserve the previous resource");
        std::filesystem::remove(fixture);
        std::cout<<"PASS: all97 native detail recordings plus wrap, color/pressure isolation, block partitions, mute/sax/panic, independent bounded voices and reload preservation\n";
    } catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
