#include "Engine.h"
#include "SectionPlayer.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <new>
#include <numeric>
#include <stdexcept>

static std::atomic<bool> trackAllocations { false };
static std::atomic<std::size_t> allocations { 0 };
void* operator new(std::size_t bytes) {
    if (trackAllocations.load(std::memory_order_relaxed)) ++allocations;
    if (auto* p = std::malloc(std::max<std::size_t>(bytes, 1))) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
double threadMicroseconds() {
    timespec now {};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) != 0) throw std::runtime_error("Thread CPU clock unavailable");
    return static_cast<double>(now.tv_sec) * 1e6 + static_cast<double>(now.tv_nsec) / 1e3;
}
float energy(const std::vector<float>& a) { return std::inner_product(a.begin(), a.end(), a.begin(), 0.0f); }
int releaseHorizon(const topbrass::Bank& bank, int rate, float releaseMs) {
    double seconds=releaseMs*.001;
    for (const auto& sample:bank.samples()) if (sample.releaseStart>0) {
        double stretch=std::exp2(7.0/12);
        if (bank.isMapped(sample.instrument,sample.articulation)) {
            stretch=0;
            for (const auto& zone:bank.zones()) if (&sample==&bank.samples()[static_cast<std::size_t>(zone.sample)])
                stretch=std::max(stretch,std::exp2((zone.root-zone.lowKey)/12.0-zone.pitchCents/1200.0));
        }
        seconds=std::max(seconds,(sample.frames()-sample.releaseStart)*stretch/sample.rate);
    }
    return static_cast<int>(std::ceil(seconds*rate))+2;
}
void writeU32(std::ostream& out, std::uint32_t value) {
    const char b[4] { static_cast<char>(value), static_cast<char>(value >> 8), static_cast<char>(value >> 16), static_cast<char>(value >> 24) };
    out.write(b, 4);
}
void writeWave(const std::filesystem::path& file, const std::vector<float>& l, const std::vector<float>& r, int sr) {
    std::ofstream out(file, std::ios::binary);
    const auto bytes = static_cast<std::uint32_t>(l.size() * 4);
    out.write("RIFF", 4); writeU32(out, 36 + bytes); out.write("WAVEfmt ", 8); writeU32(out, 16);
    const char fmt[4] {1, 0, 2, 0}; out.write(fmt, 4); writeU32(out, static_cast<std::uint32_t>(sr)); writeU32(out, static_cast<std::uint32_t>(sr * 4));
    const char align[4] {4, 0, 16, 0}; out.write(align, 4); out.write("data", 4); writeU32(out, bytes);
    for (std::size_t i = 0; i < l.size(); ++i) for (float value : {l[i], r[i]}) {
        const auto sample = static_cast<std::int16_t>(std::clamp(value, -1.0f, 1.0f) * 32767);
        const char b[2] { static_cast<char>(sample), static_cast<char>(sample >> 8) }; out.write(b, 2);
    }
}
void render(topbrass::Engine& engine, std::vector<float>& l, std::vector<float>& r, int frames, int block = 128) {
    l.assign(static_cast<std::size_t>(frames), 0); r.assign(static_cast<std::size_t>(frames), 0);
    for (int offset = 0; offset < frames; offset += block)
        engine.render(l.data() + offset, r.data() + offset, std::min(block, frames - offset));
}
void verifyLoopAtFileEnd(const std::filesystem::path& output) {
    const auto file = output / "loop-at-end.bank";
    {
        std::ofstream out(file, std::ios::binary);
        out.write("TBBANK02", 8); writeU32(out, 12);
        for (int part = 0; part < 5; ++part) for (int art = 0; art < (part == 4 ? 4 : 2); ++art) {
            for (auto value : {part, art, 60, 1, 48000, 24000, 4000, 24000, 0}) writeU32(out, static_cast<std::uint32_t>(value));
            for (int n = 0; n < 24000; ++n) {
                const float value = 0.1f * std::sin(6.283185307f * 261.625565f * n / 48000);
                out.write(reinterpret_cast<const char*>(&value), sizeof(value));
            }
        }
    }
    topbrass::Bank bank; std::string error;
    check(bank.load(file, error), error.c_str());
    topbrass::Engine engine(bank); topbrass::Settings settings;
    for (int p = 0; p < 4; ++p) settings.parts[static_cast<std::size_t>(p)].enabled = p == 0;
    settings.articulation = topbrass::Articulation::sustain;
    engine.setSettings(settings); engine.prepare(48000); engine.noteOn(1, 60, 1);
    std::vector<float> l, r; render(engine, l, r, 96000);
    std::filesystem::remove(file);
    check(engine.activeVoices() == 1 && energy(std::vector<float>(l.end()-4800, l.end())) > 0.001f,
          "An author loop ending at the last file frame must sustain beyond the recording");
}
}
int main(int argc, char** argv) {
    try {
        check(argc >= 3, "Usage: TopBrassVerify bank artifact-directory");
        const std::filesystem::path output(argv[2]); std::filesystem::create_directories(output);
        verifyLoopAtFileEnd(output);
        topbrass::Bank bank; std::string error;
        check(bank.load(argv[1], error), error.c_str());
        const auto expectedSamples = argc > 3 ? static_cast<std::size_t>(std::stoul(argv[3])) : 914;
        check(bank.samples().size() == expectedSamples, "Unexpected locked-bank sample count");
        if (argc > 4) check(bank.loadKeyNoises(argv[4], error), error.c_str());
        if (argc > 5) check(bank.loadAirflow(argv[5], error), error.c_str());
        if (argc > 6) check(bank.loadTenorDetails(argv[6],error),error.c_str());
        topbrass::Engine engine(bank);
        topbrass::Settings settings;
        settings.parts[2].octave = -1; settings.parts[3].octave = -2;
        engine.setSettings(settings); engine.prepare(48000);
        std::vector<float> l, r;
        render(engine, l, r, 512); check(energy(l) == 0, "Idle engine must be silent");
        for (int part = 0; part < 4; ++part) {
            for (int p = 0; p < 4; ++p) settings.parts[static_cast<std::size_t>(p)].enabled = p == part;
            for (auto art : {topbrass::Articulation::shortNote, topbrass::Articulation::sustain, topbrass::Articulation::fall}) {
                engine.prepare(48000); settings.articulation = art; engine.setSettings(settings);
                engine.noteOn(1, 60, 0.85f); render(engine, l, r, 24000);
                check(energy(l) > 0.001f, "Each instrument/articulation must make sound");
                check(std::all_of(l.begin(), l.end(), [](float x) { return std::isfinite(x) && std::abs(x) <= 1; }), "Invalid output");
                engine.noteOff(1, 60); render(engine, l, r, releaseHorizon(bank,48000,settings.releaseMs));
                check(engine.activeVoices() == 0, "Note release must terminate");
            }
        }
        // Every sax voice must use its real playback path and terminate on key release.
        const std::array<topbrass::SaxVoice, 5> saxVoices { topbrass::SaxVoice::tenor, topbrass::SaxVoice::baritone,
            topbrass::SaxVoice::marcato, topbrass::SaxVoice::subtone, topbrass::SaxVoice::growl };
        const std::array<const char*, 5> saxFiles {"tenor-sustain.wav", "baritone-sustain.wav", "baritone-marcato.wav", "baritone-subtone.wav", "baritone-growl.wav"};
        for (std::size_t voice = 0; voice < saxVoices.size(); ++voice) {
            settings.saxVoice = saxVoices[voice]; settings.articulation = topbrass::Articulation::sustain;
            settings.parts[0].octave = -1;
            for (int p = 0; p < 4; ++p) settings.parts[static_cast<std::size_t>(p)].enabled = p == 0;
            engine.setSettings(settings); engine.prepare(48000); engine.noteOn(1, 60, 0.9f);
            check(engine.activeVoices() == (voice == 2 ? 2 : 1), "Marcato must layer short attack and sustain, other sax voices use one recording");
            render(engine, l, r, 48000 * 4);
            check(energy(l) > 0.001f, "Sax voice must be audible");
            writeWave(output / saxFiles[voice], l, r, 48000);
            if (voice == 4) check(engine.activeVoices() == 0, "Unlooped recorded growl must finish naturally");
            engine.noteOff(1, 60); render(engine, l, r, releaseHorizon(bank,48000,settings.releaseMs));
            check(engine.activeVoices() == 0, "Every sax style must release");
        }
        settings.parts[0].octave = 0;
        settings.saxVoice = topbrass::SaxVoice::baritone; engine.setSettings(settings); engine.prepare(48000);
        engine.noteOn(1, 60, 0.9f); render(engine, l, r, 8192); const auto upperSustain = l;
        settings.saxVoice = topbrass::SaxVoice::growl; engine.setSettings(settings); engine.prepare(48000);
        engine.noteOn(1, 60, 0.9f); render(engine, l, r, 8192);
        check(l == upperSustain, "Above recorded growl range, the patch must use the documented ordinary sustain");
        settings.parts[0].octave = -1;
        settings.saxVoice = topbrass::SaxVoice::baritone; settings.articulation = topbrass::Articulation::shortNote;
        engine.setSettings(settings); engine.prepare(48000);
        std::vector<float> firstTake, secondTake;
        for (int take = 0; take < 5; ++take) {
            engine.allNotesOff(0, true); engine.noteOn(1, 60, 1); render(engine, l, r, 8192);
            if (take == 0) firstTake = l;
            if (take == 1) secondTake = l;
            if (take == 4) check(l == firstTake && l != secondTake, "Four recorded staccato alternatives must rotate deterministically");
        }
        settings.articulation = topbrass::Articulation::sustain; engine.setSettings(settings); engine.prepare(48000);
        engine.noteOn(1, 60, 1); render(engine, l, r, 48000 * 12);
        check(engine.activeVoices() == 1 && energy(std::vector<float>(l.end()-4800, l.end())) > 0.001f,
              "Imported baritone author loops must sustain beyond their original WAV duration");
        settings.saxVoice = topbrass::SaxVoice::tenor; engine.setSettings(settings); render(engine, l, r, 48000);
        check(engine.activeVoices() == 0, "Changing sax voice must release the previous voice");
        settings.parts[0].octave = 0;
        // Recorded dynamic layers supply tone; velocity must still control continuous loudness.
        float quietEnergy = 0, loudEnergy = 0;
        for (float velocity : {1.0f / 127.0f, 1.0f}) {
            engine.prepare(48000); settings.articulation = topbrass::Articulation::shortNote;
            for (int p = 0; p < 4; ++p) settings.parts[static_cast<std::size_t>(p)].enabled = p == 0;
            engine.setSettings(settings); engine.noteOn(1, 60, velocity); render(engine, l, r, 12000);
            if (velocity < 0.01f) quietEnergy = energy(l); else loudEnergy = energy(l);
        }
        check(loudEnergy > quietEnergy * 1000, "Velocity 1 must be much quieter than velocity 127");
        for (std::size_t p = 0; p < settings.parts.size(); ++p) settings.parts[p].enabled = p < 4;
        settings.articulation = topbrass::Articulation::sustain; engine.setSettings(settings);
        engine.prepare(48000); engine.noteOn(1, 60, 1); render(engine, l, r, 48000 * 8);
        const int sectionPlayheads = 4 + (bank.isMapped(1, 0) ? 2 : 0) + (bank.isMapped(2, 0) ? 2 : 0);
        check(energy(std::vector<float>(l.end() - 48000, l.end())) > 0.001f, "Held sustains must continue through loops");
        engine.controller(1, 64, 127); engine.noteOff(1, 60); render(engine, l, r, 48000);
        check(engine.activeVoices() == sectionPlayheads, "Pedal must retain all layered playheads for released keys");
        engine.controller(1, 64, 0); render(engine, l, r, releaseHorizon(bank,48000,settings.releaseMs));
        check(engine.activeVoices() == 0, "Pedal release must end sustained voices");
        engine.noteOn(1, 60, 1); render(engine, l, r, 512);
        for (auto& part : settings.parts) part.enabled = false;
        engine.setSettings(settings); render(engine, l, r, 48000);
        check(engine.activeVoices() == 0 && energy(std::vector<float>(l.end() - 1000, l.end())) == 0, "Muting all parts must become silent");
        for (std::size_t p = 0; p < settings.parts.size(); ++p) { settings.parts[p].enabled = p < 4; settings.parts[p].channel = 2; }
        engine.setSettings(settings); engine.noteOn(1, 60, 1); check(engine.activeVoices() == 0, "MIDI channel routing");
        engine.noteOn(2, 60, 1); check(engine.activeVoices() == sectionPlayheads, "MIDI routed note");
        engine.allNotesOff(1, true); check(engine.activeVoices() == sectionPlayheads, "Panic must respect channel");
        engine.allNotesOff(2, true); check(engine.activeVoices() == 0, "Panic channel termination");
        for (auto& part : settings.parts) part.channel = 0;
        engine.setSettings(settings); engine.prepare(48000);
        engine.noteOn(1, 60, 0.9f); render(engine, l, r, 8192, 8192);
        topbrass::Engine segmented(bank); segmented.setSettings(settings); segmented.prepare(48000); segmented.noteOn(1, 60, 0.9f);
        std::vector<float> sl, sr; render(segmented, sl, sr, 8192, 31);
        check(l == sl && r == sr, "Render must be invariant to host block subdivision");
        // Exercise note selection, stealing, controllers and loops under an allocation trap.
        std::array<float, 128> al {}, ar {};
        for (auto& part : settings.parts) part.enabled = true;
        settings.saxVoice = topbrass::SaxVoice::marcato; settings.parts[0].octave = -1; settings.keyNoise = 1; settings.airflowAmount = 1; engine.setSettings(settings);
        settings.articulation=topbrass::Articulation::shortNote;settings.parts[1].doubleTongue=true;settings.parts[2].doubleTongue=true;engine.setSettings(settings);
        topbrass::SectionPlayer performer;performer.prepare(48000);performer.configure({topbrass::PlayingMode::groove,60,63,1});
        struct PerformedBlock { topbrass::Engine* engine;const topbrass::Settings* settings;float* left;float* right;int cursor; } performed{&engine,&settings,al.data(),ar.data(),0};
        allocations.store(0); trackAllocations.store(true);
        for (int step = 0; step < 300; ++step) {
            settings.tubaVoice=static_cast<topbrass::TubaVoice>(step%3);
            settings.parts[0].tenorSource=step%2?topbrass::TenorSource::mtg:topbrass::TenorSource::original;
            settings.parts[4].tenorSource=step%3?topbrass::TenorSource::mtg:topbrass::TenorSource::original;
            settings.parts[1].doubleTongue=settings.parts[5].doubleTongue=step%2==0;
            settings.saxVoice=step%32<16?topbrass::SaxVoice::marcato:topbrass::SaxVoice::tenor;engine.setSettings(settings);
            engine.triggerTenorDetail(step%2?topbrass::TenorDetail::breath:topbrass::TenorDetail::keys,1+step%16);
            engine.noteOff(1 + step % 16, 48 + step % 36);
            engine.noteOn(1 + step % 16, 48 + step % 36, 0.8f);
            engine.controller(1, 1, step % 128); engine.controller(1, 11, step % 128); engine.pitchWheel(1, (step * 97) % 16384);
            performer.noteOff(1+step%16,60+step%15);performer.noteOn(1+step%16,60+step%15,.8f);
            al.fill(0);ar.fill(0);performed.cursor=0;
            performer.advance(128,&performed,[](void* pointer,const topbrass::SectionEvent& event){
                auto& c=*static_cast<PerformedBlock*>(pointer);c.engine->render(c.left+c.cursor,c.right+c.cursor,event.offset-c.cursor);c.cursor=event.offset;
                if(event.on)c.engine->noteOnPart(event.part,event.channel,event.note,event.note+12*c.settings->parts[static_cast<std::size_t>(event.part)].octave,event.velocity,topbrass::NoteDynamics::strikeAndPressure);
                else c.engine->noteOffPart(event.part,event.channel,event.note,true);
            });
            engine.render(al.data()+performed.cursor,ar.data()+performed.cursor,128-performed.cursor);
        }
        trackAllocations.store(false);
        check(allocations.load() == 0, "Audio engine allocated on render/note/control path");
        check(engine.activeVoices() <= topbrass::maxVoices + topbrass::maxStealTails + topbrass::maxKeyVoices + topbrass::maxKeyTails + topbrass::maxTenorDetails + topbrass::maxTenorDetailTails + 16, "Polyphony must be bounded");
        settings.tubaVoice=topbrass::TubaVoice::original;
        settings.parts[0].tenorSource=settings.parts[4].tenorSource=topbrass::TenorSource::original;
        settings.parts[1].doubleTongue=false;settings.parts[2].doubleTongue=false;settings.parts[5].doubleTongue=false;
        settings.saxVoice = topbrass::SaxVoice::tenor; settings.parts[0].octave = 0; settings.keyNoise = 0; settings.airflowAmount = 0; engine.setSettings(settings);
        for (double sampleRate : {44100.0, 48000.0, 96000.0}) {
            engine.prepare(sampleRate); engine.noteOn(1, 60, 1); render(engine, l, r, 4096);
            check(energy(l) > 0, "Supported sample rate must render");
        }
        std::ofstream metrics(output / "performance.json");
        metrics << "{\n  \"bank_bytes\": " << bank.bytes() << ",\n  \"samples\": " << bank.samples().size() << ",\n  \"audio_path_allocations\": " << allocations.load() << ",\n  \"benchmarks\": [\n";
        bool renderDeadlineMet = true;
        for (int scenario = 0; scenario < 7; ++scenario) {
            settings.articulation = topbrass::Articulation::sustain;
            for (std::size_t p = 0; p < settings.parts.size(); ++p) settings.parts[p].enabled = scenario < 3 ? p < 4 : p == 0;
            settings.saxVoice = scenario == 4 ? topbrass::SaxVoice::baritone : topbrass::SaxVoice::tenor;
            settings.airflowAmount = scenario == 4 ? 1 : 0;
            settings.humanize = scenario == 6 ? 1 : 0;
            engine.setSettings(settings); engine.prepare(48000);
            const int count = scenario == 0 ? 1 : scenario == 1 ? 8 : scenario == 2 ? 32 : (scenario == 3 || scenario==5 || scenario==6) ? 128 : 16;
            for (int n = 0; n < count; ++n) engine.noteOn(scenario == 4 ? n+1 : (scenario == 3 || scenario==5 || scenario==6) ? 1+n/32 : 1, scenario == 4 ? 50 : 48 + n%32, 0.9f);
            if (scenario==5) for(int hit=0;hit<topbrass::maxTenorDetails;++hit)engine.triggerTenorDetail(topbrass::TenorDetail::breath);
            int voices = engine.activeVoices();
            std::vector<double> times, cpuTimes; times.reserve(4000); cpuTimes.reserve(4000);
            engine.controller(1, 1, 100);
            for (int b = 0; b < 4000; ++b) {
                al.fill(0); ar.fill(0);
                const auto start = std::chrono::steady_clock::now(); const auto cpuStart = threadMicroseconds();
                if(scenario==5 && b%100==0)for(int hit=0;hit<topbrass::maxTenorDetails;++hit)engine.triggerTenorDetail(topbrass::TenorDetail::breath);
                engine.render(al.data(), ar.data(), 128);
                cpuTimes.push_back(threadMicroseconds() - cpuStart);
                const auto end = std::chrono::steady_clock::now(); times.push_back(std::chrono::duration<double, std::micro>(end-start).count());
            }
            voices = engine.activeVoices();
            const std::array<int,7> expectedVoices{8,64,128,128,argc>5?32:16,argc>6?128+topbrass::maxTenorDetails:128,128};
            check(voices == expectedVoices[static_cast<std::size_t>(scenario)], "Sustain benchmarks must retain their complete sample/airflow workload");
            std::sort(times.begin(), times.end()); std::sort(cpuTimes.begin(), cpuTimes.end());
            const double average = std::accumulate(times.begin(), times.end(), 0.0) / times.size();
            const double p99 = times[3960], maximum = times.back(), budget = 128.0 / 48000 * 1e6;
            metrics << "    {\"voices\": " << voices << ", \"humanize\": " << settings.humanize << ", \"block_size\": 128, \"sample_rate\": 48000, \"cc1_vibrato\": 100, \"mean_us\": " << average << ", \"p99_us\": " << p99 << ", \"max_us\": " << maximum << ", \"mean_audio_budget_percent\": " << average / budget * 100 << ", \"wall_deadline_misses\": " << std::count_if(times.begin(), times.end(), [budget](double us) { return us > budget; })
                    << ", \"thread_cpu_mean_us\": " << std::accumulate(cpuTimes.begin(), cpuTimes.end(), 0.0) / cpuTimes.size()
                    << ", \"thread_cpu_p99_us\": " << cpuTimes[3960] << ", \"thread_cpu_max_us\": " << cpuTimes.back() << "}" << (scenario == 6 ? "\n" : ",\n");
            std::cout << voices << " voices, Humanize=" << settings.humanize << ": " << average << "us mean, " << p99 << "us p99, " << maximum << "us max / " << budget << "us deadline\n";
            renderDeadlineMet = (p99 < budget) && renderDeadlineMet;
        }
        for (auto& part : settings.parts) part.enabled = true;
        settings.keyNoise = 1; settings.airflowAmount = 1; settings.humanize = 0;
        settings.saxVoice = topbrass::SaxVoice::marcato; settings.parts[0].octave = -1; engine.setSettings(settings); engine.prepare(48000);
        std::vector<double> midiTimes; midiTimes.reserve(2000);
        for (int block = 0; block < 2000; ++block) {
            al.fill(0); ar.fill(0);
            const auto begin = std::chrono::steady_clock::now();
            for (int event = 0; event < 4; ++event) {
                engine.noteOff(1, 48 + (block * 4 + event + 8) % 24);
                engine.noteOn(1, 48 + (block * 4 + event) % 24, 0.9f);
            }
            engine.controller(1, 1, 100); engine.controller(1, 11, 95);
            engine.render(al.data(), ar.data(), 128);
            midiTimes.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count());
        }
        std::sort(midiTimes.begin(), midiTimes.end());
        const auto midiMean = std::accumulate(midiTimes.begin(), midiTimes.end(), 0.0) / midiTimes.size();
        metrics << "  ],\n  \"midi_storm\": {\"enabled_players\": 6, \"note_ons_per_block\": 4, \"note_offs_per_block\": 4, \"mean_us\": " << midiMean << ", \"p99_us\": " << midiTimes[1980] << ", \"max_us\": " << midiTimes.back() << "}\n}\n";
        check(renderDeadlineMet,"p99 audio render exceeded block deadline");
        check(midiTimes[1980] < 128.0 / 48000 * 1e6, "Dense section MIDI plus rendering must meet the block deadline");
        settings.parts[1].doubleTongue=false;settings.parts[2].doubleTongue=false;
        settings.saxVoice = topbrass::SaxVoice::tenor; settings.parts[0].octave = 0; settings.keyNoise = 0; settings.airflowAmount = 0;
        // Reproducible MIDI-style pop riff at 112 BPM; identical phrases in each solo stem.
        const int duration = 48000 * 9, beat = static_cast<int>(48000 * 60.0 / 112);
        const std::array<int, 16> melody {60, 63, 65, 67, 70, 67, 65, 63, 60, 60, 63, 65, 67, 65, 63, 60};
        const std::array<const char*, 5> demoNames {"pop-section.wav", "tenor-sax.wav", "trumpet.wav", "trombone.wav", "tuba.wav"};
        for (int demo = 0; demo < 5; ++demo) {
            engine.prepare(48000); settings.articulation = topbrass::Articulation::shortNote;
            for (std::size_t p = 0; p < settings.parts.size(); ++p) settings.parts[p].enabled = p < 4 && (demo == 0 || p == static_cast<std::size_t>(demo-1));
            engine.setSettings(settings); l.assign(duration, 0); r.assign(duration, 0);
            int event = 0, offAt = -1;
            std::array<int, 2> playedNotes {-1, -1};
            for (int offset = 0; offset < duration; ++offset) {
                if (event < 16 && offset == event * beat / 2) {
                    playedNotes = { melody[static_cast<std::size_t>(event)], -1 };
                    engine.noteOn(1, playedNotes[0], 0.72f + 0.08f * (event % 4));
                    if (demo == 0 && event % 4 == 0) { playedNotes[1] = playedNotes[0] + 7; engine.noteOn(1, playedNotes[1], 0.7f); }
                    offAt = offset + beat / 3; ++event;
                }
                if (offset == offAt) for (const int note : playedNotes) if (note >= 0) engine.noteOff(1, note);
                engine.render(&l[static_cast<std::size_t>(offset)], &r[static_cast<std::size_t>(offset)], 1);
            }
            writeWave(output / demoNames[static_cast<std::size_t>(demo)], l, r, 48000);
        }
        // Long-note diagnostic: each instrument alone, then its synthetic fall.
        const int auditionLength = 48000 * 20;
        l.assign(auditionLength, 0); r.assign(auditionLength, 0);
        engine.prepare(48000);
        for (int p = 0; p < 4; ++p) {
            const int offset = p * 48000 * 5;
            for (int other = 0; other < 4; ++other) settings.parts[static_cast<std::size_t>(other)].enabled = other == p;
            settings.articulation = topbrass::Articulation::sustain; engine.setSettings(settings);
            engine.noteOn(1, 60, 0.9f);
            for (int n = 0; n < 48000 * 3; n += 128)
                engine.render(l.data() + offset + n, r.data() + offset + n, std::min(128, 48000 * 3 - n));
            engine.allNotesOff(0, true); settings.articulation = topbrass::Articulation::fall; engine.setSettings(settings);
            engine.noteOn(1, 60, 0.9f);
            for (int n = 48000 * 3; n < 48000 * 5; n += 128)
                engine.render(l.data() + offset + n, r.data() + offset + n, std::min(128, 48000 * 5 - n));
            engine.allNotesOff(0, true);
        }
        writeWave(output / "sustain-and-falls.wav", l, r, 48000);
        // Corrupt input must fail without replacing the bank already in use.
        { std::ofstream invalid(output / "invalid.bank"); invalid << "invalid"; }
        check(!bank.load(output / "invalid.bank", error) && bank.samples().size() == expectedSamples, "Invalid bank must preserve previous bank");
        std::filesystem::remove(output / "invalid.bank");
        std::cout << "PASS: sound coverage, releases, sustain, mute, channels, panic, block invariance, bounded voices, zero allocations, sample rates, bank rejection\n";
    } catch (const std::exception& error) {
        trackAllocations.store(false); std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
