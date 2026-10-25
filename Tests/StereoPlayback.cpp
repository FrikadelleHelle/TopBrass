#include "Engine.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool good,const char* message) { if (!good) throw std::runtime_error(message); }
void word(std::ostream& out,std::uint32_t x) { for (int b=0;b<4;++b) out.put(static_cast<char>(x>>(8*b))); }
void scalar(std::ostream& out,float x) { out.write(reinterpret_cast<const char*>(&x),4); }
void fixture(const std::filesystem::path& path,bool sustained=false,int channels=2,bool truncateRight=false) {
    std::ofstream out(path,std::ios::binary); out.write("TBBANK04",8);word(out,12);word(out,0);
    for (int instrument=0;instrument<5;++instrument) for (int art=0;art<(instrument==4?4:2);++art) {
        const bool stereo=instrument==0 && art==0;
        for (int value : {instrument,art,60,1,48000,24000,
                          sustained && stereo?4000:0,sustained && stereo?16000:0,0,
                          sustained && stereo?20000:0,sustained && stereo?1000:0,0,0,0,stereo?channels:1})
            word(out,static_cast<std::uint32_t>(value));
        for (int channel=0;channel<(stereo?channels:1);++channel) {
            for (int frame=0;frame<24000;++frame) {
                if (truncateRight && stereo && channel==1 && frame==12000) return;
                float value=.01f;
                if (stereo && sustained) value=frame>=20000?(channel==0?.3f:-.1f):(.05f+frame*.000001f)*(channel==0?1:-2);
                else if (stereo) value=(channel==0 && frame==32)?.2f:(channel==1 && frame==96)?-.1f:0;
                scalar(out,value);
            }
        }
    }
}
struct Audio {
    std::vector<float> left,right;
    explicit Audio(int frames):left(static_cast<std::size_t>(frames)),right(static_cast<std::size_t>(frames)){}
    void render(topbrass::Engine& engine) { engine.render(left.data(),right.data(),static_cast<int>(left.size())); }
};
}
int main(int argc,char** argv) {
    try {
        check(argc==2,"Expected fixture directory");std::filesystem::create_directories(argv[1]);
        const auto path=std::filesystem::path(argv[1])/"stereo-playback.bank";
        fixture(path);
        topbrass::Bank bank;std::string error;check(bank.load(path,error),error.c_str());
        topbrass::Settings settings;for (auto& p:settings.parts)p.enabled=false;
        settings.parts[0].enabled=true;settings.parts[0].gain=.25f;
        settings.gain=.5f;settings.tightness=0;settings.articulation=topbrass::Articulation::sustain;
        topbrass::Engine engine(bank);engine.setSettings(settings);engine.prepare(48000);engine.noteOn(1,60,1);
        Audio raw(512);raw.render(engine);
        check(std::abs(raw.left[32]-.025f)<1e-7f && raw.right[32]==0,"Left source impulse must stay in the left channel at unity stereo balance");
        check(std::abs(raw.right[96]+.0125f)<1e-7f && raw.left[96]==0,"Right source impulse must retain polarity, time and channel");
        settings.tone=.7f;engine.setSettings(settings);engine.prepare(48000);engine.noteOn(1,60,1);
        Audio filtered(96);filtered.render(engine);
        check(std::all_of(filtered.right.begin(),filtered.right.end(),[](float x){return x==0;}),"Stereo tone history must not leak across channels");
        fixture(path,true);topbrass::Bank loopBank;check(loopBank.load(path,error),error.c_str());
        topbrass::Engine loop(loopBank);settings.tone=0;loop.setSettings(settings);loop.prepare(96000);loop.noteOn(1,60,1);
        Audio held(100000);held.render(loop);
        for (std::size_t frame=0;frame<held.left.size();++frame)
            check(std::abs(held.right[frame]+2*held.left[frame])<1e-7f,"Fractional stereo loop playback must share position and interpolation");
        loop.noteOff(1,60);Audio release(10000);release.render(loop);
        check(std::abs(release.left[3000]-.0375f)<1e-6f && std::abs(release.right[3000]+.0125f)<1e-6f,"Both recorded release channels must share the transition");
        check(loop.activeVoices()==0,"Stereo release must terminate");
        for (int channels:{0,3}) { fixture(path,false,channels);check(!bank.load(path,error),"Unsupported channel counts must fail cleanly"); }
        fixture(path,false,2,true);check(!bank.load(path,error),"Truncated right PCM must fail without replacing the valid bank");
        std::filesystem::remove(path);
        std::cout<<"PASS: original stereo channels, polarity, balance, independent filters, fractional loops/releases and malformed input\n";
    } catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n';return 1; }
}
