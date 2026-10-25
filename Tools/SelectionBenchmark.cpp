#include "Engine.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace {
double cpuMicroseconds() {
    timespec now{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID,&now)!=0) throw std::runtime_error("Thread CPU clock unavailable");
    return now.tv_sec*1e6+now.tv_nsec/1e3;
}
template<class B> std::size_t indexBytes(const B& bank) {
    if constexpr (requires { bank.selectionBytes(); }) return bank.selectionBytes();
    return 0;
}
void append(std::ofstream& output,const float* left,const float* right,int frames) {
    for (int frame=0;frame<frames;++frame) {
        const std::array<float,2> pair{left[frame],right[frame]};
        output.write(reinterpret_cast<const char*>(pair.data()),sizeof(pair));
    }
}
}
int main(int argc,char** argv) {
    try {
        if (argc!=3) throw std::runtime_error("Usage: SelectionBenchmark bank report-prefix");
        topbrass::Bank bank;std::string error;
        if (!bank.load(argv[1],error)) throw std::runtime_error(error);
        const auto folder=std::filesystem::path(argv[1]).parent_path();
        if (!bank.loadKeyNoises(folder/"TopBrass.keys",error) || !bank.loadAirflow(folder/"TopBrass.air",error)) throw std::runtime_error(error);
        std::ofstream report(std::string(argv[2])+".json"),audio(std::string(argv[2])+".f32",std::ios::binary);
        report<<std::setprecision(10)<<"{\"pcm_bytes\":"<<bank.bytes()<<",\"index_bytes\":"<<indexBytes(bank)<<",\"note_trigger\":[\n";
        constexpr std::array<const char*,6> labels{"tenor_short","baritone_short","trumpet_sustain","trumpet_double_tongue","trombone_short","four_part_sustain"};
        topbrass::Settings settings;
        settings.gain=.5f;
        for (auto& part:settings.parts) part={false,.125f,0,0,0};
        topbrass::Engine engine(bank);
        for (int scenario=0;scenario<6;++scenario) {
            const int solo=scenario==2 || scenario==3?1:scenario==4?2:0;
            for (int part=0;part<4;++part) {
                settings.parts[static_cast<std::size_t>(part)].enabled=scenario==5 || part==solo;
                settings.parts[static_cast<std::size_t>(part)].octave=scenario==5 && part>=2?1-part:0;
                settings.parts[static_cast<std::size_t>(part)].doubleTongue=scenario==3 && part==1;
            }
            settings.saxVoice=scenario==1?topbrass::SaxVoice::baritone:topbrass::SaxVoice::tenor;
            settings.articulation=scenario==2 || scenario==5?topbrass::Articulation::sustain:topbrass::Articulation::shortNote;
            engine.setSettings(settings);engine.prepare(48000);
            const auto trigger=[&](int event) {
                const int note=scenario==0?44+event%45:scenario==1?36+event%33:scenario==4?36+event%36:60+event%15;
                const float velocity=(1+event%127)/127.0f;
                for (int part=0;part<4;++part) if (settings.parts[static_cast<std::size_t>(part)].enabled)
                    engine.noteOnPart(part,1,note,note+12*settings.parts[static_cast<std::size_t>(part)].octave,velocity);
            };
            for (int event=0;event<512;++event) trigger(event);
            std::array<double,8> rounds{};
            for (auto& time:rounds) {
                const double begin=cpuMicroseconds();
                for (int event=0;event<2048;++event) trigger(event);
                time=(cpuMicroseconds()-begin)/2048;
            }
            report<<"{\"case\":\""<<labels[static_cast<std::size_t>(scenario)]<<"\",\"events_per_round\":2048,\"thread_cpu_us_per_event\":[";
            for (std::size_t i=0;i<rounds.size();++i) report<<(i?",":"")<<rounds[i];
            report<<"]}"<<(scenario==5?"\n":",\n");
            engine.prepare(48000);
            std::array<float,2560> left{},right{};
            for (int note=0;note<64;++note) {
                trigger(note);left.fill(0);right.fill(0);
                engine.render(left.data(),right.data(),1536);
                for (int part=0;part<4;++part) if (settings.parts[static_cast<std::size_t>(part)].enabled)
                    engine.noteOffPart(part,1,scenario==0?44+note%45:scenario==1?36+note%33:scenario==4?36+note%36:60+note%15);
                engine.render(left.data()+1536,right.data()+1536,1024);
                append(audio,left.data(),right.data(),2560);
            }
        }
        for (auto& part:settings.parts) {part.enabled=true;part.doubleTongue=false;}
        settings.articulation=topbrass::Articulation::sustain;settings.saxVoice=topbrass::SaxVoice::marcato;
        settings.parts[0].octave=-1;settings.keyNoise=1;settings.airflowAmount=1;
        engine.setSettings(settings);engine.prepare(48000);
        std::array<float,128> left{},right{};std::vector<double> wall,cpu;wall.reserve(2000);cpu.reserve(2000);
        for (int block=0;block<2000;++block) {
            left.fill(0);right.fill(0);
            const auto begin=std::chrono::steady_clock::now();const double cpuBegin=cpuMicroseconds();
            for (int event=0;event<4;++event) {
                engine.noteOff(1,48+(block*4+event+8)%24);engine.noteOn(1,48+(block*4+event)%24,.9f);
            }
            engine.controller(1,1,100);engine.controller(1,11,95);engine.render(left.data(),right.data(),128);
            cpu.push_back(cpuMicroseconds()-cpuBegin);
            wall.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count());
            append(audio,left.data(),right.data(),128);
        }
        std::sort(wall.begin(),wall.end());std::sort(cpu.begin(),cpu.end());
        report<<"],\"dense_midi\":{\"blocks\":2000,\"frames_per_block\":128,\"sample_rate\":48000,\"note_ons_per_block\":4,\"note_offs_per_block\":4,"
              <<"\"wall_mean_us\":"<<std::accumulate(wall.begin(),wall.end(),0.0)/wall.size()<<",\"wall_p99_us\":"<<wall[1980]<<",\"wall_max_us\":"<<wall.back()
              <<",\"thread_cpu_mean_us\":"<<std::accumulate(cpu.begin(),cpu.end(),0.0)/cpu.size()<<",\"thread_cpu_p99_us\":"<<cpu[1980]<<",\"thread_cpu_max_us\":"<<cpu.back()<<"}}\n";
        if (!report || !audio) throw std::runtime_error("Benchmark write failed");
        std::cout<<"Saved note-trigger timings, dense MIDI/render timings and full float audio fingerprint\n";
    } catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
