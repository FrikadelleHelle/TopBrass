#include "Engine.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::runtime_error("Usage:WarTubaBenchmark bank output.json");
        topbrass::Bank bank;std::string error;if(!bank.load(argv[1],error))throw std::runtime_error(error);
        topbrass::Settings settings;settings.articulation=topbrass::Articulation::sustain;settings.tubaVoice=topbrass::TubaVoice::warStaccato;settings.gain=.005f;
        for(auto& part:settings.parts){part.enabled=false;part.gain=.1f;part.octave=0;}settings.parts[3].enabled=true;
        topbrass::Engine engine(bank);engine.setSettings(settings);engine.prepare(48000);
        for(int channel=1;channel<=16;++channel){engine.controller(channel,1,100);engine.controller(channel,11,127);}
        for(int note=0;note<128;++note)engine.noteOn(1+note/27,29+note%27,.8f);
        if(engine.activeVoices()!=128)throw std::runtime_error("Stress must begin with128 measured War sustain voices");
        std::vector<double> wall,cpu;wall.reserve(4000);cpu.reserve(4000);std::array<float,128> left{},right{};
        const auto micros=[] {timespec now{};if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&now)!=0)throw std::runtime_error("CPU clock unavailable");return now.tv_sec*1e6+now.tv_nsec/1e3;};
        int misses=0;double energy=0;
        for(int block=0;block<4000;++block) {
            left.fill(0);right.fill(0);const double before=micros();const auto start=std::chrono::steady_clock::now();
            engine.render(left.data(),right.data(),128);
            const double elapsed=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();const double consumed=micros()-before;
            wall.push_back(elapsed);cpu.push_back(consumed);if(elapsed>128/48000.*1e6)++misses;
            if(engine.activeVoices()!=128)throw std::runtime_error("Every measured War voice must remain alive through native loops");
            for(float sample:left){if(!std::isfinite(sample))throw std::runtime_error("Nonfinite stress output");energy+=sample*sample;}
        }
        if(energy<=0)throw std::runtime_error("Stress must render audible finite samples");
        std::sort(wall.begin(),wall.end());std::sort(cpu.begin(),cpu.end());
        std::ofstream out(argv[2]);out<<"{\"source\":\"War sustain,128 directly measured playheads,CC1=100,all native roots,4000 unpaced128-frame blocks at48kHz\",\"audio_seconds\":"<<4000*128/48000.<<",\"voices_every_block\":128,\"wall_p99_us\":"<<wall[3959]<<",\"wall_max_us\":"<<wall.back()<<",\"cpu_p99_us\":"<<cpu[3959]<<",\"cpu_max_us\":"<<cpu.back()<<",\"wall_deadline_misses\":"<<misses<<",\"budget_us\":"<<128/48000.*1e6<<",\"limits\":\"Unpaced engine workload, not a native audio callback or live host guarantee. Includes native loop crossings and synthesized CC1 vibrato.\"}\n";
        if(!out)throw std::runtime_error("Report write failed");
        if(wall[3959]>=128/48000.*1e6)return 2;
        std::cout<<"PASS:128 retained War voices through native loops; wall p99="<<wall[3959]<<"us\n";
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
