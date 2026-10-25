#include "Engine.h"
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <sys/resource.h>

namespace {
double cpuMicroseconds() {
    timespec now{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID,&now)!=0) throw std::runtime_error("Thread CPU clock unavailable");
    return now.tv_sec*1e6+now.tv_nsec/1e3;
}
template<class Load> void measure(std::ostream& report,Load load) {
    const auto begin=std::chrono::steady_clock::now();const auto cpuBegin=cpuMicroseconds();
    load();
    const double cpu=cpuMicroseconds()-cpuBegin;
    const auto wall=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count();
    report<<"{\"wall_us\":"<<wall<<",\"thread_cpu_us\":"<<cpu<<"}";
}
}
int main(int argc,char** argv) {
    try {
        if (argc!=3) throw std::runtime_error("Usage: BankLoadBenchmark bank report.json");
        std::ofstream report(argv[2]);report<<std::setprecision(12)<<"{\"description\":\"Five sequential fresh Bank instances including all detail banks; filesystem cache is uncontrolled, no purge. Timings exclude UI, host scanning, validation signatures and object destruction.\",\"rounds\":[\n";
        const auto folder=std::filesystem::path(argv[1]).parent_path();
        for (int round=0;round<5;++round) {
            topbrass::Bank bank;std::string error;
            report<<"{\"musical\":";
            measure(report,[&]{if (!bank.load(argv[1],error)) throw std::runtime_error(error);});
            report<<",\"keys\":";
            measure(report,[&]{if (!bank.loadKeyNoises(folder/"TopBrass.keys",error)) throw std::runtime_error(error);});
            report<<",\"airflow\":";
            measure(report,[&]{if (!bank.loadAirflow(folder/"TopBrass.air",error)) throw std::runtime_error(error);});
            report<<",\"tenor_details\":";
            measure(report,[&]{if(!bank.loadTenorDetails(folder/"TopBrass.tenor",error))throw std::runtime_error(error);});
            rusage usage{};if(getrusage(RUSAGE_SELF,&usage)!=0)throw std::runtime_error("Resident-memory accounting unavailable");
            report<<",\"process_peak_rss_platform_units\":"<<usage.ru_maxrss;
            report<<",\"bank_accounted_bytes\":"<<bank.bytes()<<",\"index_storage_bytes\":"<<bank.selectionBytes()<<"}"<<(round==4?"\n":",\n");
        }
        report<<"]}\n";
        if (!report) throw std::runtime_error("Report write failed");
        std::cout<<"Measured musical, key-noise, airflow and tenor-detail bank loading independently\n";
    } catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
