#include "Engine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>

namespace {
void check(bool ok,const char* message) {if (!ok) throw std::runtime_error(message);}
template<class T> struct Choices {
    std::array<const T*,16> values{};std::size_t count=0;
    const T* at(std::uint32_t repetition) const {return count?values[repetition%count]:nullptr;}
};
// Frozen v0.11 scan rules provide the differential oracle, including cap-before-sort behavior.
Choices<topbrass::Sample> sampleReference(const topbrass::Bank& bank,int instrument,int art,int target,float velocity) {
    int root=-1,distance=128,lowest=128,highest=-1;
    for (const auto& sample:bank.samples()) if (sample.instrument==instrument && sample.articulation==art) {
        lowest=std::min(lowest,sample.root);highest=std::max(highest,sample.root);
        if (std::abs(sample.root-target)<distance) {root=sample.root;distance=std::abs(sample.root-target);}
    }
    Choices<topbrass::Sample> choices;
    if (root<0 || distance>7 || (art>=2 && (target<lowest || target>highest))) return choices;
    std::array<bool,9> layers{};int layerCount=0;
    for (const auto& sample:bank.samples()) if (sample.instrument==instrument && sample.articulation==art && sample.root==root)
        layers[static_cast<std::size_t>(sample.layer)]=true;
    for (bool present:layers) if (present) ++layerCount;
    int rank=std::min(layerCount-1,static_cast<int>(velocity*layerCount));
    if (instrument==4 && art==1) {
        const int midi=static_cast<int>(std::round(velocity*127));rank=midi<=40?0:midi<=70?1:midi<=100?2:3;
    }
    int chosen=1;
    for (int layer=1;layer<=8;++layer) if (layers[static_cast<std::size_t>(layer)] && rank--==0) {chosen=layer;break;}
    for (const auto& sample:bank.samples())
        if (sample.instrument==instrument && sample.articulation==art && sample.root==root && sample.layer==chosen && choices.count<16)
            choices.values[choices.count++]=&sample;
    return choices;
}
Choices<topbrass::Zone> zoneReference(const topbrass::Bank& bank,int instrument,int art,int target,int velocity,int layer) {
    Choices<topbrass::Zone> choices;
    for (const auto& zone:bank.zones())
        if (zone.instrument==instrument && zone.articulation==art && zone.dynamicLayer==layer
            && target>=zone.lowKey && target<=zone.highKey && velocity>=zone.lowVelocity && velocity<=zone.highVelocity && choices.count<16)
            choices.values[choices.count++]=&zone;
    std::sort(choices.values.begin(),choices.values.begin()+static_cast<std::ptrdiff_t>(choices.count),
              [](const auto* a,const auto* b){return a->roundRobin<b->roundRobin;});
    return choices;
}
std::uint64_t compare(const topbrass::Bank& bank) {
    std::set<float> velocities;
    for (int midi=0;midi<128;++midi) velocities.insert(midi/127.0f);
    const auto boundary=[&](float value) {
        for (float v:{std::nextafter(value,0.0f),value,std::nextafter(value,1.0f)}) velocities.insert(v);
    };
    for (int layers=1;layers<=8;++layers) for (int rank=1;rank<layers;++rank) boundary(static_cast<float>(rank)/layers);
    for (float midi:{40.5f,70.5f,100.5f}) boundary(midi/127);
    std::uint64_t checks=0;
    for (int instrument=0;instrument<topbrass::instrumentCount;++instrument)
        for (int art=0;art<topbrass::sampleArticulationCount;++art) {
            const bool present=std::any_of(bank.samples().begin(),bank.samples().end(),[&](const auto& s){return s.instrument==instrument && s.articulation==art;});
            for (int key=0;key<128;++key) {
                if (present) for (float velocity:velocities) {
                    const auto expected=sampleReference(bank,instrument,art,key,velocity);
                    for (std::uint32_t repetition=0;repetition<33;++repetition) {
                        const auto take=repetition==32?std::numeric_limits<std::uint32_t>::max():repetition;
                        check(bank.selectSample(instrument,art,key,velocity,take)==expected.at(take),"Indexed source identity differs from v0.11 scan");++checks;
                    }
                } else check(bank.selectSample(instrument,art,key,.5f,0)==nullptr,"Absent source articulation must remain empty");
                if (bank.isMapped(instrument,art)) for (int velocity=0;velocity<128;++velocity) for (int layer=0;layer<4;++layer) {
                    const auto expected=zoneReference(bank,instrument,art,key,velocity,layer);
                    for (std::uint32_t repetition=0;repetition<33;++repetition) {
                        const auto take=repetition==32?std::numeric_limits<std::uint32_t>::max():repetition;
                        check(bank.selectZone(instrument,art,key,velocity,layer,take)==expected.at(take),"Indexed zone identity differs from v0.11 scan");++checks;
                    }
                }
            }
        }
    return checks;
}
void word(std::ostream& output,std::uint32_t value) {for (int byte=0;byte<4;++byte) output.put(static_cast<char>(value>>(byte*8)));}
void scalar(std::ostream& output,float value) {output.write(reinterpret_cast<const char*>(&value),sizeof(value));}
void fixture(const std::filesystem::path& path,bool reverse=false) {
    std::vector<std::array<int,4>> samples{{0,0,reverse?58:62,2},{0,0,reverse?62:58,2},{0,1,60,1},
        {1,0,60,1},{1,1,60,1},{2,0,60,1},{2,1,60,1},{3,0,60,1},{3,1,60,1},
        {4,0,60,3},{4,1,60,2},{4,2,60,4},{4,2,64,7},{4,3,48,1}};
    for (int take=0;take<23;++take) samples.push_back({0,0,reverse?58:62,2});
    samples.push_back({0,0,reverse?58:62,5});samples.push_back({4,1,64,1});samples.push_back({4,1,64,4});
    std::vector<std::array<int,10>> zones;
    for (int i=0;i<26;++i) {
        const int index=static_cast<int>(samples.size());samples.push_back({1,1,60,1});
        zones.push_back({index,1,1,60,50,70,0,i<4?30:127,0,(25-i)%16});
    }
    zones.push_back({5,2,0,60,0,127,0,127,3,0});
    const int fall=static_cast<int>(samples.size());samples.push_back({1,4,60,1});
    zones.push_back({fall,1,4,60,0,127,0,127,0,0});
    std::ofstream output(path,std::ios::binary);output.write("TBBANK04",8);
    word(output,static_cast<std::uint32_t>(samples.size()));word(output,static_cast<std::uint32_t>(zones.size()));
    for (std::size_t index=0;index<samples.size();++index) {
        const auto& s=samples[index];
        for (int value:{s[0],s[1],s[2],s[3],48000,8,0,0,0,0,0,0,0,0,1}) word(output,static_cast<std::uint32_t>(value));
        for (int frame=0;frame<8;++frame) scalar(output,.0001f*static_cast<float>(index+1));
    }
    for (const auto& zone:zones) {for (int value:zone) word(output,static_cast<std::uint32_t>(value));scalar(output,0);scalar(output,1);}
}
}
int main(int argc,char** argv) {
    try {
        check(argc==3,"Expected bank and fixture directory");
        topbrass::Bank bank;std::string error;
        check(bank.selectSample(0,0,60,.5f,0)==nullptr && bank.selectZone(0,0,60,64,0,0)==nullptr,"Unloaded bank must select nothing");
        check(bank.load(argv[1],error),error.c_str());
        const auto checks=compare(bank);
        const auto path=std::filesystem::path(argv[2])/"selection-index.bank";fixture(path);
        topbrass::Bank adversarial;check(adversarial.load(path,error),error.c_str());
        check(adversarial.selectSample(0,0,60,.1f,0)->root==62,"Root ties must preserve first bank order");
        check(adversarial.selectSample(0,0,62,.9f,0)->layer==5,"Layers after the first16 takes must remain selectable");
        check(adversarial.selectSample(4,1,60,1,0)==nullptr,"Missing layer1 fallback must remain silent");
        check(adversarial.selectSample(4,1,64,1,0)->layer==1,"Sparse baritone ranks must retain layer1 fallback");
        check(adversarial.selectSample(0,1,67,1,0)!=nullptr && adversarial.selectSample(0,1,68,1,0)==nullptr,"Seven-semitone source borrowing bound must remain exact");
        check(adversarial.selectSample(1,4,59,1,0)==nullptr,"Mapped zone extents must not replace source expressive bounds");
        const auto fixtureChecks=compare(adversarial);
        auto copied=adversarial;
        const auto* previous=adversarial.selectSample(0,0,60,.1f,0);
        const auto* previousZone=adversarial.selectZone(1,1,60,90,0,0);
        const auto bytes=adversarial.selectionBytes();
        std::filesystem::resize_file(path,std::filesystem::file_size(path)-1);
        check(!adversarial.load(path,error),"Truncated mapping must fail");
        check(adversarial.selectSample(0,0,60,.1f,0)==previous && adversarial.selectZone(1,1,60,90,0,0)==previousZone
              && adversarial.selectionBytes()==bytes,"Failed reload must retain samples, zones and their index");
        fixture(path,true);check(adversarial.load(path,error),error.c_str());
        check(adversarial.selectSample(0,0,60,.1f,0)->root==58,"Successful reload must replace root choices");
        check(copied.selectSample(0,0,60,.1f,0)==sampleReference(copied,0,0,60,.1f).at(0),"Copied bank indices must refer to the copy's recordings");
        check(copied.selectZone(1,1,60,90,0,0)==zoneReference(copied,1,1,60,90,0).at(0),"Copied zone indices must refer to the copy's mappings");
        std::filesystem::remove(path);
        std::cout<<"PASS: "<<checks<<" actual-bank and "<<fixtureChecks<<" adversarial selected identities; index bytes="<<bank.selectionBytes()<<'\n';
    } catch (const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
