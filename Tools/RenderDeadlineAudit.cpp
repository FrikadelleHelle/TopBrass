#include "Engine.h"
#include <mach/mach.h>
#include <pthread/qos.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
double cpuMicroseconds() {
    timespec now{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID,&now)!=0) throw std::runtime_error("Read thread CPU clock");
    return now.tv_sec*1e6+now.tv_nsec/1e3;
}
task_events_info_data_t events() {
    task_events_info_data_t result{};
    mach_msg_type_number_t size=TASK_EVENTS_INFO_COUNT;
    if (task_info(mach_task_self(),TASK_EVENTS_INFO,reinterpret_cast<task_info_t>(&result),&size)!=KERN_SUCCESS)
        throw std::runtime_error("Read process event counters");
    return result;
}
struct Row {
    double wall, cpu, checksum;
    int voices, faults, pageins, switches;
    bool detail;
};
}
int main(int argc,char** argv) {
    try {
        if (argc<4 || argc>6) throw std::runtime_error("Usage: RenderDeadlineAudit bank prefix blocks [inherit|interactive] [mixed|music|empty|inject-wait]");
        const int blocks=std::stoi(argv[3]);
        if (blocks<100 || blocks>100000) throw std::runtime_error("Blocks must be100..100000");
        const std::string qos=argc>4?argv[4]:"inherit";
        if (qos!="inherit" && qos!="interactive") throw std::runtime_error("Unknown QoS mode");
        const std::string workload=argc>5?argv[5]:"mixed";
        const bool inject=workload=="inject-wait", detail=workload=="mixed" || inject, music=workload!="empty";
        if (workload!="mixed" && workload!="music" && workload!="empty" && !inject) throw std::runtime_error("Unknown workload");
        if (std::filesystem::exists(std::string(argv[2])+".csv") || std::filesystem::exists(std::string(argv[2])+".json"))
            throw std::runtime_error("Choose a new report prefix");
        topbrass::Bank bank;std::string error;
        const std::filesystem::path path=argv[1], folder=path.parent_path();
        if (!bank.load(path,error) || !bank.loadTenorDetails(folder/"TopBrass.tenor",error)) throw std::runtime_error(error);
        topbrass::Settings settings;
        for (auto& part:settings.parts) {part.enabled=false;part.gain=.1f;part.octave=0;}
        settings.parts[0].enabled=true;
        settings.articulation=topbrass::Articulation::sustain;settings.gain=.5f;
        topbrass::Engine engine(bank);engine.setSettings(settings);engine.prepare(48000);
        if (music) for (int note=0;note<128;++note) engine.noteOn(1+note/32,48+note%32,.9f);
        engine.controller(1,1,100);
        if (detail) for (int hit=0;hit<topbrass::maxTenorDetails;++hit) engine.triggerTenorDetail(topbrass::TenorDetail::breath);
        if (engine.activeVoices()!=(music?128:0)+(detail?8:0)) throw std::runtime_error("Initial playhead count does not match requested workload");
        if (qos=="interactive" && pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE,0)!=0)
            throw std::runtime_error("Set this diagnostic thread's QoS");
        const auto observedQos=qos_class_self();
        thread_time_constraint_policy_data_t policy{};
        mach_msg_type_number_t policySize=THREAD_TIME_CONSTRAINT_POLICY_COUNT;
        boolean_t defaultPolicy=true;
        const auto thread=mach_thread_self();
        const auto policyStatus=thread_policy_get(thread,THREAD_TIME_CONSTRAINT_POLICY,reinterpret_cast<thread_policy_t>(&policy),&policySize,&defaultPolicy);
        mach_port_deallocate(mach_task_self(),thread);
        if (policyStatus!=KERN_SUCCESS) throw std::runtime_error("Read scheduling policy");
        std::array<float,128> left{},right{};
        std::vector<Row> rows(static_cast<std::size_t>(blocks));
        for (int block=0;block<blocks;++block) {
            left.fill(0);right.fill(0);
            const auto before=events();
            const auto begin=std::chrono::steady_clock::now();const double cpuBegin=cpuMicroseconds();
            if (detail && block%100==0) for (int hit=0;hit<topbrass::maxTenorDetails;++hit) engine.triggerTenorDetail(topbrass::TenorDetail::breath);
            if (inject && block==50) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            engine.render(left.data(),right.data(),128);
            const double cpu=cpuMicroseconds()-cpuBegin;
            const double wall=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count();
            const auto after=events();
            rows[static_cast<std::size_t>(block)]={wall,cpu,std::accumulate(left.begin(),left.end(),0.0),engine.activeVoices(),
                                                 after.faults-before.faults,after.pageins-before.pageins,after.csw-before.csw,detail && block%100==0};
        }
        if ((!detail && engine.activeVoices()!=(music?128:0)) || engine.activeVoices()<128*static_cast<int>(music))
            throw std::runtime_error("Measured workload lost musical playheads");
        std::ofstream csv(std::string(argv[2])+".csv"), report(std::string(argv[2])+".json");
        csv<<std::setprecision(12)<<"block,wall_us,thread_cpu_us,off_cpu_us,voices,page_faults,pageins,context_switches,detail_trigger,checksum\n";
        constexpr double deadline=128.0/48000*1e6;
        int misses=0,cpuMisses=0,offCpuMisses=0,missesWithFaults=0,missesWithPageins=0,missesWithSwitches=0;
        std::vector<double> wall,cpu;wall.reserve(rows.size());cpu.reserve(rows.size());
        for (std::size_t index=0;index<rows.size();++index) {
            const auto& row=rows[index];
            if (!std::isfinite(row.checksum)) throw std::runtime_error("Non-finite audio");
            csv<<index<<','<<row.wall<<','<<row.cpu<<','<<row.wall-row.cpu<<','<<row.voices<<','<<row.faults<<','<<row.pageins<<','<<row.switches<<','<<row.detail<<','<<row.checksum<<'\n';
            wall.push_back(row.wall);cpu.push_back(row.cpu);
            if (row.wall>deadline) {
                ++misses;
                if (row.cpu>deadline) ++cpuMisses;else ++offCpuMisses;
                if (row.faults) ++missesWithFaults;
                if (row.pageins) ++missesWithPageins;
                if (row.switches) ++missesWithSwitches;
            }
        }
        std::sort(wall.begin(),wall.end());std::sort(cpu.begin(),cpu.end());
        report<<std::setprecision(12)<<"{\"blocks\":"<<blocks<<",\"block_size\":128,\"sample_rate\":48000,\"workload\":\""<<workload<<"\",\"musical_notes\":"<<(music?128:0)<<",\"final_active_voices\":"<<engine.activeVoices()<<",\"deadline_us\":"<<deadline
              <<",\"qos_requested\":\""<<qos<<"\",\"qos_observed\":"<<observedQos<<",\"injected_wait\":"<<(inject?"true":"false")
              <<",\"default_time_constraint_policy\":"<<(defaultPolicy?"true":"false")
              <<",\"wall_misses\":"<<misses<<",\"cpu_misses\":"<<cpuMisses<<",\"wall_misses_with_cpu_below_deadline\":"<<offCpuMisses
              <<",\"misses_with_page_faults\":"<<missesWithFaults<<",\"misses_with_pageins\":"<<missesWithPageins<<",\"misses_with_context_switches\":"<<missesWithSwitches
              <<",\"wall_p99_us\":"<<wall[rows.size()*99/100]<<",\"wall_max_us\":"<<wall.back()<<",\"cpu_p99_us\":"<<cpu[rows.size()*99/100]<<",\"cpu_max_us\":"<<cpu.back()
              <<",\"method\":\"Unpaced engine-only CLI loop. Paired per-block wall and thread CPU; process event counters bracket the timer interval. No audio-device callback or real-time scheduling contract. Normal outputs are collected after measurement.\"}\n";
        if (!csv || !report) throw std::runtime_error("Write audit files");
        std::cout<<"wall misses="<<misses<<", CPU misses="<<cpuMisses<<", wall p99="<<wall[rows.size()*99/100]<<"us, CPU p99="<<cpu[rows.size()*99/100]<<"us\n";
        return misses?2:0;
    } catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
