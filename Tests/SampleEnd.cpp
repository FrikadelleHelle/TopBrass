#include "Engine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool ok,const char* message) {if (!ok) throw std::runtime_error(message);}
void word(std::ostream& output,std::uint32_t value) {for (int byte=0;byte<4;++byte) output.put(static_cast<char>(value>>(8*byte)));}
void scalar(std::ostream& output,float value) {output.write(reinterpret_cast<const char*>(&value),sizeof(value));}
void fixture(const std::filesystem::path& path,int channels) {
    std::ofstream output(path,std::ios::binary);output.write("TBBANK04",8);word(output,12);word(output,0);
    for (int instrument=0;instrument<5;++instrument) for (int art=0;art<(instrument==4?4:2);++art) {
        for (int value:{instrument,art,60,1,48000,8,0,0,0,0,0,0,0,0,channels}) word(output,static_cast<std::uint32_t>(value));
        for (int channel=0;channel<channels;++channel) for (int frame=0;frame<8;++frame)
            scalar(output,frame==7?(channel==0?.25f:-.125f):0);
    }
}
struct Audio {std::array<float,40> left{},right{};};
Audio play(const topbrass::Bank& bank,int channels,int kind,int rate,int target,int block) {
    topbrass::Settings settings;for (auto& part:settings.parts) part.enabled=false;
    settings.parts[0]={true,.125f,0,0,0};settings.gain=1;
    settings.articulation=kind==0?topbrass::Articulation::shortNote:topbrass::Articulation::sustain;
    settings.saxVoice=kind==2?topbrass::SaxVoice::growl:topbrass::SaxVoice::tenor;
    topbrass::Engine engine(bank);engine.setSettings(settings);engine.prepare(rate);engine.noteOn(1,target,1);
    Audio audio;
    const double step=48000.0/rate*std::exp2((target-60)/12.0);
    const int end=static_cast<int>(std::ceil(8/step));
    for (int offset=0;offset<static_cast<int>(audio.left.size());) {
        const int frames=std::min({block,static_cast<int>(audio.left.size())-offset,offset<end?end-offset:block});
        engine.render(audio.left.data()+offset,audio.right.data()+offset,frames);offset+=frames;
        if (offset==end) check(engine.activeVoices()==0,"Exact block-edge EOF must clear the completed voice");
    }
    float worst=0;
    for (std::size_t frame=0;frame<audio.left.size();++frame) {
        const double position=frame*step;float expected=0;
        if (position<8) {
            const int index=static_cast<int>(position);const float t=static_cast<float>(position-index),t2=t*t,t3=t2*t;
            const auto read=[&](int at){return std::clamp(at,0,7)==7?.25f:0;};
            expected=read(index-1)*(-.5f*t+t2-.5f*t3)+read(index)*(1-2.5f*t2+1.5f*t3)
                    +read(index+1)*(.5f*t+2*t2-1.5f*t3)+read(index+2)*(-.5f*t2+.5f*t3);
        }
        const float gain=.125f*(channels==2?1:std::cos(3.14159265358979323846f*.25f));
        worst=std::max({worst,std::abs(audio.left[frame]-expected*gain),
                       std::abs(audio.right[frame]-expected*gain*(channels==2?-.5f:1))});
    }
    check(worst<1e-7f,"Finite playback must include the final source frame and its fractional final cell");
    return audio;
}
}
int main(int argc,char** argv) {
    try {
        check(argc==2,"Expected fixture directory");const auto path=std::filesystem::path(argv[1])/"sample-end.bank";
        int count=0;
        for (int channels:{1,2}) {
            fixture(path,channels);topbrass::Bank bank;std::string error;check(bank.load(path,error),error.c_str());
            for (int kind=0;kind<3;++kind) for (int rate:{48000,96000,44100}) for (int target:{60,61}) {
                if (kind==2 && target!=60) continue;
                const auto whole=play(bank,channels,kind,rate,target,40),split=play(bank,channels,kind,rate,target,3);
                check(whole.left==split.left && whole.right==split.right,"Finite final-frame playback must be block-partition invariant");++count;
            }
        }
        std::filesystem::remove(path);
        std::cout<<"PASS: "<<count<<" mono/stereo finite endings, fractional cells, borrowed notes, exact EOF lifetime and block partitions\n";
    } catch (const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
