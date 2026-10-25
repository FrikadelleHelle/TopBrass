#include "Engine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
float read(const std::vector<float>& audio, double position) {
    const int index = static_cast<int>(position);
    const float t = static_cast<float>(position-index), t2=t*t, t3=t2*t;
    const auto at = [&](int n) { return audio[static_cast<std::size_t>(std::clamp(n, 0, static_cast<int>(audio.size())-1))]; };
    return at(index-1)*(-.5f*t+t2-.5f*t3) + at(index)*(1-2.5f*t2+1.5f*t3)
         + at(index+1)*(.5f*t+2*t2-1.5f*t3) + at(index+2)*(-.5f*t2+.5f*t3);
}
void render(topbrass::Engine& engine, std::vector<float>& left, std::vector<float>& right, int frames, int block=127) {
    left.assign(static_cast<std::size_t>(frames), 0); right.assign(left.size(), 0);
    for (int offset=0; offset<frames;) {
        const int count=std::min(block, frames-offset);
        engine.render(left.data()+offset, right.data()+offset, count); offset+=count;
    }
}
}
int main(int argc, char** argv) {
    try {
        check(argc==2, "Expected bank path");
        topbrass::Bank bank; std::string error; check(bank.load(argv[1], error), error.c_str());
        int count=0; float worst=0;
        for (const auto& sample:bank.samples()) {
            if ((sample.instrument!=0 && sample.instrument!=3) || sample.articulation!=0) continue;
            check(sample.releaseStart>=sample.loopEnd && sample.releaseStart>0 && sample.releaseCrossfade>0,
                  "Tenor/tuba sustains must map a recorded ending after their loop");
            std::set<int> layers;
            for (const auto& other:bank.samples())
                if (other.instrument==sample.instrument && other.articulation==0 && other.root==sample.root) layers.insert(other.layer);
            const float velocity=(static_cast<float>(std::distance(layers.begin(), layers.find(sample.layer)))+.5f)/static_cast<float>(layers.size());
            const float gain=.125f*std::pow(velocity,1.3f);
            topbrass::Settings settings; settings.articulation=topbrass::Articulation::sustain; settings.gain=1;
            for (auto& part:settings.parts) part.enabled=false;
            settings.parts[static_cast<std::size_t>(sample.instrument)]={true,.125f,0,0,0};
            const std::array<std::pair<int,int>,3> cases{{{sample.rate,sample.root},
                {sample.rate==48000?44100:48000,sample.root},{48000,sample.root-1}}};
            for (const auto& [rate,target]:cases) {
                if (target!=sample.root && sample.root!=bank.keyRange(sample.instrument,0).low) continue;
                topbrass::Engine engine(bank); engine.setSettings(settings); engine.prepare(rate);
                engine.noteOn(1,target,velocity);
                std::vector<float> left,right; render(engine,left,right,rate);
                engine.noteOff(1,target);
                const double step=static_cast<double>(sample.rate)/rate*std::exp2((target-sample.root)/12.0);
                const int frames=static_cast<int>(std::ceil((sample.frames()-sample.releaseStart)/step))+64;
                render(engine,left,right,frames);
                double position=sample.releaseStart; float blend=0;
                for (int frame=0; frame<frames; ++frame) {
                    if (blend==1 && position<sample.frames()) {
                        const float endingGain=std::clamp(static_cast<float>((sample.frames()-1-position)/(sample.rate*.003)),0.0f,1.0f);
                        worst=std::max({worst,std::abs(left[static_cast<std::size_t>(frame)]-read(sample.audio,position)*gain*endingGain),
                            std::abs(right[static_cast<std::size_t>(frame)]-read(sample.rightAudio,position)*gain*endingGain)});
                    }
                    position+=step;
                    blend=std::min(1.0f,blend+static_cast<float>(step/sample.releaseCrossfade));
                }
                check(engine.activeVoices()==0, "Recorded endings must terminate at native and converted sample rates");
                check(left.back()==0 && right.back()==0, "Recorded endings must leave no output after EOF");
                if (sample.instrument==3 && sample.layer==1 && (sample.root==34 || sample.root==41 || sample.root==53)) {
                    float sourceDifference=0;
                    for (int i=sample.frames()-sample.rate/10;i<sample.frames();++i)
                        sourceDifference=std::max({sourceDifference,std::abs(sample.audio[static_cast<std::size_t>(i)]-sample.audio[static_cast<std::size_t>(i-1)]),
                            std::abs(sample.rightAudio[static_cast<std::size_t>(i)]-sample.rightAudio[static_cast<std::size_t>(i-1)])});
                    const auto end=static_cast<std::size_t>(std::ceil((sample.frames()-1-sample.releaseStart)/step));
                    check(std::max(std::abs(left[end-1]),std::abs(right[end-1]))<=sourceDifference*gain+1e-9f,
                          "Soft tuba EOF jump must not exceed its local source sample differences");
                    topbrass::Engine split(bank);split.setSettings(settings);split.prepare(rate);split.noteOn(1,target,velocity);
                    std::vector<float> splitLeft,splitRight;render(split,splitLeft,splitRight,rate,31);split.noteOff(1,target);
                    render(split,splitLeft,splitRight,frames,31);
                    check(left==splitLeft && right==splitRight,"Ending ramp must be bit-identical across render block partitions");
                }
                engine.noteOnPart(sample.instrument,1,sample.root,sample.root,velocity); render(engine,left,right,1024);
                engine.noteOffPart(sample.instrument,1,sample.root);
                engine.noteOnPart(sample.instrument,1,sample.root,sample.root,velocity); render(engine,left,right,rate/4);
                check(engine.activeVoices()==1, "Part retrigger must force-fade the previous ending while retaining the new player");
                engine.endPart(sample.instrument); render(engine,left,right,rate/4);
                check(engine.activeVoices()==0, "Part mute must force-fade instead of starting a new recorded ending");
                engine.noteOn(1,sample.root,velocity); render(engine,left,right,1024);
                engine.controller(1,64,127); engine.noteOff(1,sample.root); render(engine,left,right,rate*2);
                check(engine.activeVoices()==1, "Pedal must keep the source body held");
                engine.controller(1,64,0); render(engine,left,right,frames);
                check(engine.activeVoices()==0, "Pedal-up must finish the recorded ending");
                engine.noteOn(1,sample.root,velocity); render(engine,left,right,1024);
                engine.noteOff(1,sample.root); engine.allNotesOff(0,true); render(engine,left,right,256);
                check(std::all_of(left.begin(),left.end(),[](float value){return value==0;}) && engine.activeVoices()==0,
                      "Panic must clear a recorded ending immediately");
                auto fallSettings=settings; fallSettings.articulation=topbrass::Articulation::fall;
                topbrass::Engine fall(bank), forced(bank);
                for (auto* player:{&fall,&forced}) {
                    player->setSettings(fallSettings); player->prepare(rate);
                    player->noteOn(1,sample.root,velocity); render(*player,left,right,rate/5);
                }
                fall.noteOff(1,sample.root); forced.allNotesOff(1,false);
                std::vector<float> expectedLeft,expectedRight;
                render(fall,left,right,rate/2); render(forced,expectedLeft,expectedRight,rate/2);
                check(left==expectedLeft && right==expectedRight,
                      "Synthetic falls must retain their generic release instead of jumping into a natural sustain ending");
            }
            ++count;
        }
        check(count==69, "Cover all tenor and tuba sustained recordings and velocity layers");
        check(worst<1e-7f, "Native endings must retain stereo PCM through conversion except for the final3ms output ramp");
        std::cout<<"PASS: "<<count<<" recorded sustain endings, native/converted rates, borrowed edge notes, EOF ramp, block partitions, retrigger, mute, pedal and panic; max error="<<worst<<'\n';
    } catch (const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n';return 1; }
}
