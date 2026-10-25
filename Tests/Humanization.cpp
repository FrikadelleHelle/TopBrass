#include "Engine.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <new>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

static std::atomic<bool> trackAllocations {false};
static std::atomic<int> allocations {0};
void* operator new(std::size_t bytes) {
    if (trackAllocations.load(std::memory_order_relaxed)) ++allocations;
    if (auto* pointer = std::malloc(std::max<std::size_t>(bytes, 1))) return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
namespace {
void check(bool good, const char* message) { if (!good) throw std::runtime_error(message); }
void word(std::ostream& out, int value) {
    for (int b = 0; b < 4; ++b) out.put(static_cast<char>(static_cast<unsigned>(value) >> (8 * b)));
}
void scalar(std::ostream& out, float value) { out.write(reinterpret_cast<const char*>(&value), 4); }
void fixture(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    out.write("TBBANK03", 8); word(out, 15); word(out, 3);
    for (int index = 0; index < 15; ++index) {
        const bool mapped = index >= 12;
        const int instrument = mapped ? 1 : index < 8 ? index / 2 : 4;
        const int art = mapped ? 0 : index < 8 ? index % 2 : index - 8;
        const int layer = mapped ? index - 11 : 1;
        for (int value : {instrument, art, 60, layer, 48000, 24000, 4000, 16000, 0, 0, 0, 0, 0, 0}) word(out, value);
        for (int frame = 0; frame < 24000; ++frame) scalar(out, .01f * layer + frame * .0000001f);
    }
    for (int layer = 1; layer <= 3; ++layer) {
        for (int value : {11 + layer, 1, 0, 60, 60, 60, 1, 127, layer, 0}) word(out, value);
        scalar(out, 0); scalar(out, 1);
    }
}
void keyFixture(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    out.write("TBKEYS01", 8); word(out, 8); word(out, 8);
    for (int take = 0; take < 8; ++take) {
        word(out, 48000); word(out, 1000);
        for (int frame = 0; frame < 1000; ++frame) scalar(out, .1f);
    }
    for (int opening = 0; opening < 2; ++opening) for (int take = 0; take < 4; ++take) {
        for (int value : {opening * 4 + take, 60, 60, opening, take}) word(out, value);
        scalar(out, 1);
    }
}
struct Audio {
    std::vector<float> l, r;
    explicit Audio(int frames) : l(static_cast<std::size_t>(frames)), r(l.size()) {}
};
Audio render(topbrass::Engine& engine, int frames, int block = 512) {
    Audio result(frames);
    for (int offset = 0; offset < frames; offset += block)
        engine.render(result.l.data() + offset, result.r.data() + offset, std::min(block, frames - offset));
    return result;
}
topbrass::Settings settingsFor(int part, float humanize, topbrass::Articulation art = topbrass::Articulation::sustain) {
    topbrass::Settings s;
    for (auto& strip : s.parts) { strip.enabled = false; strip.gain = .1f; strip.pan = -1; }
    s.parts[static_cast<std::size_t>(part)].enabled = true;
    s.gain = .5f; s.articulation = art; s.humanize = humanize;
    return s;
}
int onset(const Audio& audio) {
    const auto at = std::find_if(audio.l.begin(), audio.l.end(), [](float x) { return x != 0; });
    return static_cast<int>(at - audio.l.begin());
}
}
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Expected artifact directory and original bank");
        const auto path = std::filesystem::path(argv[1]) / "humanization.bank";
        fixture(path); topbrass::Bank bank; std::string error;
        check(bank.load(path, error), error.c_str());
        std::set<int> delays;
        for (int part = 0; part < topbrass::partCount; ++part) for (double rate : {44100., 48000., 96000.}) {
            auto neutral = settingsFor(part, 0);
            auto human = settingsFor(part, 1);
            topbrass::Engine original(bank), shifted(bank), split(bank);
            original.setSettings(neutral); shifted.setSettings(human); split.setSettings(human);
            for (auto* e : {&original, &shifted, &split}) e->prepare(rate);
            for (auto* e : {&original, &shifted, &split}) e->noteOnPart(part, 1, 60, 60, .25f);
            const auto reference = render(original, 4096), audio = render(shifted, 4096), divided = render(split, 4096, 31);
            check(onset(reference) == 0, "Zero amount must retain immediate source onset");
            const int delay = onset(audio);
            check(delay <= static_cast<int>(std::ceil(rate * .008)), "Human timing must remain inside eight milliseconds");
            delays.insert(delay);
            check(audio.l == divided.l && audio.r == divided.r, "Humanization must be bit-identical across block partitions");
            const float gain = audio.l[static_cast<std::size_t>(delay)] / reference.l[0];
            for (int frame = 0; frame < 128; ++frame)
                check(std::abs(audio.l[static_cast<std::size_t>(delay + frame)] / reference.l[static_cast<std::size_t>(frame)] - gain) < .0001f,
                      "Timing must shift the complete source attack and all mapped dynamic layers together");
            shifted.prepare(rate); shifted.noteOnPart(part, 1, 60, 60, .25f);
            check(render(shifted, 4096).l == audio.l, "Prepare must reset deterministic player timing and drift");
        }
        check(delays.size() > 6 && *delays.rbegin() > 0, "Players must have distinct nonzero onset offsets");
        std::set<int> firstPlayers;
        for (int gesture = 0; gesture < 12; ++gesture) {
            int firstPart = -1, earliest = 100000;
            for (int part = 0; part < topbrass::partCount; ++part) {
                topbrass::Engine engine(bank); engine.setSettings(settingsFor(part, 1)); engine.prepare(48000);
                for (int n = 0; n <= gesture; ++n) {
                    engine.noteOnPart(part, 1, 60, 60, .5f);
                    if (n != gesture) { render(engine, 512); engine.allNotesOff(0, true); }
                }
                const int delay = onset(render(engine, 1024));
                if (delay < earliest) { earliest = delay; firstPart = part; }
            }
            firstPlayers.insert(firstPart);
        }
        check(firstPlayers.size() > 2, "Timing variation must not always put the same player first");
        for (auto art : {topbrass::Articulation::shortNote, topbrass::Articulation::sustain}) {
            topbrass::Engine engine(bank); engine.setSettings(settingsFor(0, 1, art)); engine.prepare(48000);
            engine.noteOnPart(0, 1, 60, 60, 1); render(engine, 1); engine.noteOffPart(0, 1, 60);
            const auto cancelled = render(engine, 4096);
            check(engine.activeVoices() == 0 && onset(cancelled) == 4096, "Releasing before human onset must cancel delayed short and sustain notes");
        }
        {
            const auto settings = settingsFor(0, 1, topbrass::Articulation::shortNote);
            topbrass::Engine timing(bank), cancelled(bank);
            for (auto* e : {&timing, &cancelled}) { e->setSettings(settings); e->prepare(48000); e->noteOnPart(0, 1, 60, 60, 1); }
            const int delay = onset(render(timing, 1024));
            check(delay > 0, "Boundary cancellation fixture must have a pending onset");
            render(cancelled, delay); cancelled.noteOffPart(0, 1, 60);
            check(onset(render(cancelled, 1024)) == 1024, "Release exactly at delayed onset must cancel before the first audible frame");
        }
        {
            const auto settings = settingsFor(0, 1);
            topbrass::Engine chord(bank), lower(bank), upper(bank);
            for (auto* e : {&chord, &lower, &upper}) { e->setSettings(settings); e->prepare(48000); }
            chord.noteOn(1, 60, 1); chord.noteOn(1, 72, 1);
            lower.noteOn(1, 60, 1); upper.noteOn(1, 72, 1);
            const auto both = render(chord, 4096), low = render(lower, 4096), high = render(upper, 4096);
            for (std::size_t frame = 0; frame < both.l.size(); ++frame)
                check(std::abs(both.l[frame] - low.l[frame] - high.l[frame]) < 1e-7f,
                      "Same-timestamp layered chords must share one player onset and level timeline");
        }
        {
            auto neutral = settingsFor(0, 0), human = settingsFor(0, 1);
            neutral.saxVoice = topbrass::SaxVoice::marcato; human.saxVoice = neutral.saxVoice;
            topbrass::Engine wet(bank), dry(bank), controlWet(bank), controlDry(bank);
            wet.setSettings(human); dry.setSettings(neutral);
            controlWet.setSettings(settingsFor(0, 1)); controlDry.setSettings(settingsFor(0, 0));
            for (auto* e : {&wet, &dry, &controlWet, &controlDry}) { e->prepare(48000); e->noteOnPart(0, 1, 60, 60, 1); }
            const auto audio = render(wet, 12000), reference = render(dry, 12000);
            const auto factors = render(controlWet, 12000), unity = render(controlDry, 12000);
            const int delay = onset(factors);
            for (int frame = delay; frame < 12000; ++frame) {
                const float gain = factors.l[static_cast<std::size_t>(frame)] / unity.l[static_cast<std::size_t>(frame - delay)];
                check(std::abs(audio.l[static_cast<std::size_t>(frame)] - reference.l[static_cast<std::size_t>(frame - delay)] * gain) < 1e-7f,
                      "Marcato attack and its intentional sustain crossfade must receive the same human onset shift");
            }
        }
        {
            const auto keysPath = std::filesystem::path(argv[1]) / "humanization.keys";
            keyFixture(keysPath); check(bank.loadKeyNoises(keysPath, error), error.c_str());
            auto s = settingsFor(0, 1, topbrass::Articulation::shortNote);
            s.saxVoice = topbrass::SaxVoice::baritone; s.keyNoise = 1;
            topbrass::Engine withKeys(bank), music(bank), cancelled(bank);
            withKeys.setSettings(s); cancelled.setSettings(s); s.keyNoise = 0; music.setSettings(s);
            for (auto* e : {&withKeys, &music, &cancelled}) { e->prepare(48000); e->noteOnPart(0, 1, 60, 60, 1); }
            const auto dry = render(music, 1024), detailed = render(withKeys, 1024);
            check(onset(detailed) == onset(dry) && detailed.l[static_cast<std::size_t>(onset(dry))] > dry.l[static_cast<std::size_t>(onset(dry))], "Mechanical key sounds must start with the delayed musical player");
            render(cancelled, onset(dry)); cancelled.noteOffPart(0, 1, 60);
            check(onset(render(cancelled, 4096)) == 4096, "Cancellation before onset must not emit detached close or opening key sounds");
            std::filesystem::remove(keysPath);
        }
        {
            topbrass::Bank originalBank; check(originalBank.load(argv[2], error), error.c_str());
            for (int part : {0, 1}) {
                auto neutral = settingsFor(part, 0), human = settingsFor(part, 1);
                topbrass::Engine wet(originalBank), dry(originalBank), controlWet(bank), controlDry(bank);
                wet.setSettings(human); controlWet.setSettings(human); dry.setSettings(neutral); controlDry.setSettings(neutral);
                for (auto* e : {&wet, &dry, &controlWet, &controlDry}) { e->prepare(48000); e->noteOnPart(part, 1, 60, 60, .25f); }
                const auto audio = render(wet, 4096), reference = render(dry, 4096);
                const auto factors = render(controlWet, 4096), unity = render(controlDry, 4096);
                const int delay = onset(factors);
                for (int frame = delay; frame < 4096; ++frame) {
                    const float gain = factors.l[static_cast<std::size_t>(frame)] / unity.l[static_cast<std::size_t>(frame - delay)];
                    check(std::abs(audio.l[static_cast<std::size_t>(frame)] - reference.l[static_cast<std::size_t>(frame - delay)] * gain) < 1e-6f,
                          "Real recordings must preserve source attacks and mapped timbre under shared player timing and level drift");
                }
            }
        }
        topbrass::Engine drifting(bank), partitioned(bank), dry(bank);
        auto wetSettings = settingsFor(0, 1), drySettings = settingsFor(0, 0);
        drifting.setSettings(wetSettings); partitioned.setSettings(wetSettings); dry.setSettings(drySettings);
        for (auto* e : {&drifting, &partitioned, &dry}) { e->prepare(48000); e->noteOnPart(0, 1, 60, 60, 1); }
        const auto wet = render(drifting, 48000 * 12), split = render(partitioned, 48000 * 12, 31);
        check(wet.l == split.l, "Long player drift must be independent of host block sizes");
        const int delay = onset(wet);
        const auto reference = render(dry, 48000 * 12);
        float smallest = 2, largest = 0, previous = 1;
        for (int frame = delay; frame < static_cast<int>(wet.l.size()); ++frame) {
            const float ratio = wet.l[static_cast<std::size_t>(frame)] / reference.l[static_cast<std::size_t>(frame - delay)];
            check(ratio >= std::pow(10.f, -.6f / 20) - 1e-5f && ratio <= std::pow(10.f, .6f / 20) + 1e-5f, "Level drift must remain inside plus/minus 0.6 dB");
            if (frame > delay) check(std::abs(ratio - previous) < .00002f, "Player level drift must be smooth rather than tremolo or gain jumps");
            smallest = std::min(smallest, ratio); largest = std::max(largest, ratio); previous = ratio;
        }
        check(largest - smallest > .01f, "A held player must slowly vary its level");
        auto live = settingsFor(0, 0); topbrass::Engine automated(bank); automated.setSettings(live); automated.prepare(48000); automated.noteOnPart(0, 1, 60, 60, 1);
        render(automated, 48000); live.humanize = 1; automated.setSettings(live); const auto rising = render(automated, 48000);
        live.humanize = 0; automated.setSettings(live); const auto falling = render(automated, 48000);
        check(automated.activeVoices() == 1 && onset(rising) == 0 && onset(falling) == 0, "Live amount automation must not restart or delay held players");
        std::array<float, 512> callbackLeft {}, callbackRight {};
        trackAllocations.store(true);
        for (int pass = 0; pass < 20; ++pass) {
            live.humanize = static_cast<float>(pass % 2); automated.setSettings(live);
            automated.noteOnPart(0, 1, 60, 60, 1);
            automated.render(callbackLeft.data(), callbackRight.data(), 512);
            automated.noteOffPart(0, 1, 60, true);
        }
        trackAllocations.store(false);
        check(allocations.load() == 0, "Enabled humanization, automation, onset and forced gates must allocate nothing on the audio thread");
        automated.allNotesOff(0, true); check(onset(render(automated, 4096)) == 4096, "Panic must cancel every pending and sounding humanized player");
        std::filesystem::remove(path);
        std::cout << "PASS: bounded shared-layer onset, cancellation, deterministic drift, zero latency and live automation\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
