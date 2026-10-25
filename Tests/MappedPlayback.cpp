#include "Engine.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool good, const char* message) { if (!good) throw std::runtime_error(message); }
void word(std::ostream& out, std::uint32_t value) {
    for (int byte = 0; byte < 4; ++byte) out.put(static_cast<char>(value >> (8*byte)));
}
void scalar(std::ostream& out, float value) { out.write(reinterpret_cast<const char*>(&value), 4); }
void fixture(const std::filesystem::path& path, bool gainEnvelope = false, bool capacityCase = false, bool varyingGain = false) {
    std::ofstream out(path, std::ios::binary);
    out.write("TBBANK03", 8); word(out, 16); word(out, 4);
    for (int index = 0; index < 16; ++index) {
        const bool mapped = index >= 12 && index < 15;
        const bool fall = index == 15;
        const int instrument = mapped || fall ? 1 : index < 8 ? index/2 : 4;
        const int art = fall ? 4 : mapped ? 0 : index < 8 ? index%2 : index-8;
        const int layer = mapped ? index-11 : 1;
        for (int value : {instrument, art, 60, layer, 48000, 24000, fall ? 0 : 4000, fall ? 0 : 16000,
                          mapped ? 2000 : 0, mapped ? 20000 : 0, mapped ? 1000 : 0, mapped ? 1 : 0,
                          varyingGain && mapped ? 64 : 0, gainEnvelope && mapped ? (varyingGain ? 753 : 2) : 0})
            word(out, static_cast<std::uint32_t>(value));
        for (int frame = 0; frame < 24000; ++frame)
            scalar(out, fall ? .05f+frame*.000001f : mapped ? (frame >= 20000 ? .6f : .1f*layer + frame*.000001f) : .01f);
        if (gainEnvelope && mapped)
            for (int index = 0; index < (varyingGain ? 753 : 2); ++index)
                scalar(out, varyingGain ? .25f+index*.0005f : .5f);
    }
    for (int layer = 1; layer <= 3; ++layer) {
        for (int value : {11+layer, 1, 0, 60, 60, capacityCase ? 68 : 60, 1, 127,
                          capacityCase ? 0 : layer, capacityCase ? layer-1 : 0}) word(out, static_cast<std::uint32_t>(value));
        scalar(out, 0); scalar(out, 1);
    }
    for (int value : {15, 1, 4, 60, 60, 60, 1, 127, 0, 0}) word(out, static_cast<std::uint32_t>(value));
    scalar(out, 0); scalar(out, 1);
}
struct Audio {
    std::vector<float> left, right;
    explicit Audio(int size) : left(static_cast<std::size_t>(size)), right(static_cast<std::size_t>(size)) {}
    void render(topbrass::Engine& engine) { engine.render(left.data(), right.data(), static_cast<int>(left.size())); }
};
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected fixture directory");
        std::filesystem::create_directories(argv[1]);
        const auto file = std::filesystem::path(argv[1])/"mapped-playback.bank";
        fixture(file);
        topbrass::Bank bank; std::string error;
        check(bank.load(file, error), error.c_str());
        topbrass::Settings settings;
        for (int part = 0; part < 4; ++part) settings.parts[static_cast<std::size_t>(part)].enabled = part == 1;
        settings.parts[1].gain = .1f; settings.parts[1].pan = -1;
        settings.gain = 1; settings.tone = 0; settings.tightness = 0;
        settings.articulation = topbrass::Articulation::sustain;
        topbrass::Engine engine(bank); engine.setSettings(settings); engine.prepare(48000);
        engine.noteOn(1, 60, 1);
        check(engine.activeVoices() == 3, "Sustain dynamic groups must play simultaneously");
        Audio held(17000); held.render(engine);
        check(std::abs(held.left[0]-.03f) < 1e-6f, "Initial velocity selects the forte dynamic without tripling gain");
        check(std::abs(held.left[16000]-.0304f) < 1e-6f, "Authored pre-start crossfade wraps to loopStart");
        engine.controller(1, 11, 64);
        Audio expression(5000); expression.render(engine);
        check(expression.left.back() > .009f && expression.left.back() < .012f, "Expression must move to the middle dynamic layer");
        engine.controller(1, 11, 127);
        Audio settled(5000); settled.render(engine);
        engine.noteOff(1, 60);
        Audio release(5000); release.render(engine);
        check(release.left[1500] > .055f, "Note-off must play the authored release region");
        check(engine.activeVoices() == 0, "Recorded release must end without looping");
        engine.prepare(48000); engine.noteOn(1, 61, 1);
        Audio outside(512); outside.render(engine);
        check(engine.activeVoices() == 0, "Mapped instruments must honor authored key bounds");
        engine.prepare(48000); engine.noteOn(1, 60, 1); engine.noteOff(1, 60);
        Audio pending(512); pending.render(engine);
        check(engine.activeVoices() == 0 && pending.left == outside.left, "Zero-duration notes must cancel without a phantom release");
        engine.prepare(48000); engine.noteOn(1, 60, 1); Audio attack(1000); attack.render(engine);
        engine.controller(1, 64, 127); engine.noteOff(1, 60);
        Audio pedal(5000); pedal.render(engine);
        check(engine.activeVoices() == 3, "Pedal must defer the recorded release");
        engine.controller(1, 64, 0); Audio pedalRelease(5000); pedalRelease.render(engine);
        check(engine.activeVoices() == 0 && pedalRelease.left[1500] > .055f, "Pedal-up must play the recorded release");
        settings.articulation = topbrass::Articulation::fall;
        engine.setSettings(settings); engine.prepare(48000); engine.noteOn(1,60,1);
        check(engine.activeVoices() == 1, "A recorded fall must use one finite recording rather than three sustained layers");
        Audio fallAttack(1000); fallAttack.render(engine); engine.noteOff(1,60);
        Audio fallTail(25000); fallTail.render(engine);
        check(std::abs(fallAttack.left[0]-.005f) < 1e-6f, "Fall must select its authored recording");
        check(std::abs(fallTail.left[17000]-.0068f) < 1e-6f, "Recorded fall pitch and natural ending must survive note-off without an extra pitch fall");
        check(engine.activeVoices() == 0, "Recorded falls must finish naturally");
        for (int controller : {64,121}) {
            engine.prepare(48000); engine.noteOn(1,60,1);
            if (controller==64) engine.controller(1,64,127);
            Audio first(1000); first.render(engine); engine.noteOff(1,60); engine.controller(1,controller,0);
            Audio tail(25000); tail.render(engine);
            check(tail.left == fallTail.left, "Pedal-up and controller reset must preserve native fall endings");
        }
        engine.prepare(48000); engine.noteOn(1,60,1); engine.noteOn(1,60,1);
        check(engine.activeVoices()==1,"Pending fall retriggers must cancel the previous unstarted take");
        Audio startedFall(1000); startedFall.render(engine);
        auto muted=settings; muted.parts[1].enabled=false; engine.setSettings(muted);
        Audio mutedFall(5000); mutedFall.render(engine);
        check(engine.activeVoices()==0,"Mute must stop recorded falls");
        engine.setSettings(settings); engine.noteOn(1,60,1); engine.controller(1,120,0);
        check(engine.activeVoices()==0,"All sound off must stop recorded falls immediately");
        settings.articulation = topbrass::Articulation::sustain;
        engine.setSettings(settings);
        fixture(file, true);
        topbrass::Bank gainBank; check(gainBank.load(file, error), error.c_str());
        topbrass::Engine gainEngine(gainBank); gainEngine.setSettings(settings); gainEngine.prepare(48000);
        gainEngine.noteOn(1, 60, 1); Audio gainAudio(512); gainAudio.render(gainEngine);
        check(std::abs(gainAudio.left[0]-.015f) < 1e-6f, "Recording gain envelopes must apply after playback without altering PCM");
        fixture(file, true, false, true);
        topbrass::Bank movingGainBank; check(movingGainBank.load(file, error), error.c_str());
        topbrass::Engine movingGain(movingGainBank); movingGain.setSettings(settings); movingGain.prepare(48000);
        movingGain.noteOn(1,60,1); Audio moving(17000); moving.render(movingGain);
        check(std::abs(moving.left[0]-.03f*.251f) < 1e-6f, "Gain lookup must retain the original sample-start offset");
        check(std::abs(moving.left[16000]-.0304f*.3135f) < 1e-6f, "Gain lookup must follow the looped source position");
        movingGain.noteOff(1,60); Audio movingRelease(5000); movingRelease.render(movingGain);
        check(std::abs(movingRelease.left[1500]-.06f*.587f) < 1e-6f, "Gain lookup must follow the recorded release position");
        fixture(file, false, true);
        topbrass::Bank fullBank; check(fullBank.load(file, error), error.c_str());
        topbrass::Engine full(fullBank); full.setSettings(settings); full.prepare(48000);
        full.noteOn(1, 60, 1); Audio one(1); one.render(full);
        for (int note = 0; note < topbrass::maxVoices-1; ++note) {
            const int channel=2+note/9, key=60+note%9;
            full.noteOn(channel,key,1); Audio frame(1); frame.render(full); full.noteOff(channel,key);
        }
        check(full.activeVoices() == topbrass::maxVoices, "Saturation fixture must actually fill the physical playhead pool");
        full.noteOn(1, 61, 1); Audio stolenFade(256); stolenFade.render(full);
        const int beforePanic=full.activeVoices(); full.allNotesOff(1,true);
        check(full.activeVoices() == beforePanic-2, "Stealing must retain both held notes while preferring already-released native tails");
        for (int articulation : {topbrass::recordedFallArticulation, topbrass::recordedDoubleTongueArticulation})
            for (int instrument : {0,3,4}) {
                std::fstream malformed(file,std::ios::binary|std::ios::in|std::ios::out);
                malformed.seekp(16+15*(14*4+24000*4)); word(malformed,static_cast<std::uint32_t>(instrument)); word(malformed,static_cast<std::uint32_t>(articulation)); malformed.close();
                check(!bank.load(file,error) && error=="Invalid sample metadata" && bank.samples().size()==16,
                      "Recorded brass gesture IDs must reject unsupported instruments without replacing the bank");
            }
        {
            std::fstream malformed(file,std::ios::binary|std::ios::in|std::ios::out);
            malformed.seekp(16+15*(14*4+24000*4)); word(malformed,1); word(malformed,topbrass::sampleArticulationCount);
        }
        check(!bank.load(file,error) && error=="Invalid sample metadata", "Out-of-range articulation IDs must fail before indexing coverage");
        for (int articulation : {topbrass::recordedFallArticulation, topbrass::recordedDoubleTongueArticulation}) {
            std::ofstream legacy(file,std::ios::binary|std::ios::trunc);
            legacy.write("TBBANK02",8); word(legacy,1);
            for (int value : {1,articulation,60,1,48000,8,0,0,0}) word(legacy,static_cast<std::uint32_t>(value));
            for (int frame=0; frame<8; ++frame) scalar(legacy,.01f);
            legacy.close();
            check(!bank.load(file,error) && error=="Invalid sample metadata", "Legacy bank format must reject mapped brass gesture IDs");
        }
        std::filesystem::remove(file);
        std::cout << "PASS: mapped bounds, dynamics, loops, releases, gain, stealing, recorded fall pitch and lifecycle\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
