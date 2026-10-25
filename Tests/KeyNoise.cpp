#include "Engine.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool good, const char* message) {
    if (!good)
        throw std::runtime_error(message);
}
void word(std::ostream& out, std::uint32_t n) {
    for (int i = 0; i < 4; ++i)
        out.put(static_cast<char>(n >> (8 * i)));
}
void scalar(std::ostream& out, float x) {
    out.write(reinterpret_cast<const char*>(&x), 4);
}
void music(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    out.write("TBBANK04", 8);
    word(out, 24);
    word(out, 0);
    for (int root : {36, 48})
        for (int instrument = 0; instrument < 5; ++instrument)
            for (int art = 0; art < (instrument == 4 ? 4 : 2); ++art) {
                for (int n : {instrument, art, root, 1, 48000, 960, 0, 0, 0, 0, 0, 0, 0, 0, 1})
                    word(out, n);
                for (int i = 0; i < 960; ++i)
                    scalar(out, instrument == 1 ? .02f : 0);
            }
}
void details(const std::filesystem::path& path, bool invalid = false, int rate = 48000, bool reuse = false) {
    std::ofstream out(path, std::ios::binary);
    out.write("TBKEYS01", 8);
    word(out, 8);
    word(out, reuse ? 16 : 8);
    for (int take = 0; take < 8; ++take) {
        word(out, rate);
        word(out, 4000);
        for (int i = 0; i < 4000; ++i)
            scalar(out, (take + 1) * .01f + (reuse ? i * .000001f : 0));
    }
    for (int key = reuse ? 36 : 48; key <= 48; key += 12)
        for (int take = 0; take < 8; ++take) {
            for (int n : {take, invalid ? 128 : key, key, take / 4, take % 4})
                word(out, n);
            scalar(out, .25f);
        }
}
float peak(const std::vector<float>& x) {
    float p = 0;
    for (float v : x)
        p = std::max(p, std::abs(v));
    return p;
}
struct Audio {
    std::vector<float> l, r;
    explicit Audio(int n) : l(n), r(n) {};
    void render(topbrass::Engine& e) {
        e.render(l.data(), r.data(), static_cast<int>(l.size()));
    }
};
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected fixture directory");
        const auto dir = std::filesystem::path(argv[1]);
        std::filesystem::create_directories(dir);
        const auto musical = dir / "key-musical.bank", keys = dir / "key-details.bank";
        music(musical);
        details(keys);
        topbrass::Bank bank;
        std::string error;
        check(bank.load(musical, error), error.c_str());
        check(bank.loadKeyNoises(keys, error), error.c_str());
        topbrass::Settings s;
        for (auto& p : s.parts)
            p.enabled = false;
        s.parts[0].enabled = true;
        s.parts[0].gain = .5f;
        s.saxVoice = topbrass::SaxVoice::baritone;
        s.gain = .5f;
        s.keyNoise = 1;
        topbrass::Engine e(bank);
        const auto reset = [&] {
            e.setSettings(s);
            e.prepare(48000);
        };
        for (int take = 0; take < 4; ++take) {
            if (take == 0)
                reset();
            e.noteOn(1, 48, 1);
            Audio close(5000);
            close.render(e);
            check(std::abs(close.l[100] - (take + 1) * .01f * .25f * .25f * .70710678f) < 1e-7f,
                  "Close must preserve take PCM and calibrated gain");
            check(e.activeVoices() == 0, "Short musical note and key click must finish before physical release");
            e.noteOff(1, 48);
            Audio open(5000);
            open.render(e);
            check(std::abs(open.l[100] - (take + 5) * .01f * .25f * .25f * .70710678f) < 1e-7f,
                  "Physical release after short completion must play exactly one matching open take");
            e.noteOff(1, 48);
            Audio duplicate(100);
            duplicate.render(e);
            check(peak(duplicate.l) == 0, "Duplicate note-off must not retrigger open");
        }
        reset();
        e.noteOn(1, 48, 1);
        Audio finishedClose(5000);
        finishedClose.render(e);
        e.noteOff(1, 48);
        e.noteOn(1, 48, 1);
        e.noteOff(1, 48);
        Audio genuineOpen(100);
        genuineOpen.render(e);
        check(std::abs(genuineOpen.l[50] - .05f * .25f * .25f * .70710678f) < 1e-7f,
              "Canceling a same-frame retrigger must retain the genuine open from the preceding played note");
        for (bool opening : {false, true}) {
            reset();
            e.noteOn(1, 48, 1);
            if (opening) {
                Audio closed(5000);
                closed.render(e);
                e.noteOff(1, 48);
            }
            e.controller(1, 7, 0);
            Audio mutedAtStart(100);
            mutedAtStart.render(e);
            check(peak(mutedAtStart.l) == 0 && peak(mutedAtStart.r) == 0,
                  "Same-sample CC7 must apply before the first close/open frame");
        }
        reset();
        e.noteOn(1, 48, 1);
        s.parts[0].pan = 1;
        e.setSettings(s);
        Audio initialPan(100);
        initialPan.render(e);
        check(peak(initialPan.l) < 1e-9f, "Initial key pan must use final same-sample settings");
        s.parts[0].pan = 0;
        reset();
        e.noteOn(1, 48, 1);
        Audio beforePan(100);
        beforePan.render(e);
        s.parts[0].pan = 1;
        e.setSettings(s);
        Audio afterPan(1);
        afterPan.render(e);
        check(afterPan.l[0] > .9f * beforePan.l.back(),
              "Pan automation must smooth a ringing key recording rather than jump channels");
        s.parts[0].pan = 0;
        reset();
        e.noteOn(1, 48, 1);
        e.noteOff(1, 48);
        Audio pending(5000);
        pending.render(e);
        check(peak(pending.l) == 0, "Zero-duration gesture must cancel pending close without an open");
        for (auto voice : {topbrass::SaxVoice::tenor, topbrass::SaxVoice::marcato, topbrass::SaxVoice::baritone}) {
            s.saxVoice = voice;
            reset();
            e.noteOn(1, 48, 1);
            Audio held(5000);
            held.render(e);
            check((peak(held.l) > 0) == (voice != topbrass::SaxVoice::tenor),
                  "Only baritone styles get mechanical detail");
            e.controller(1, 64, 127);
            e.noteOff(1, 48);
            Audio release(5000);
            release.render(e);
            if (voice != topbrass::SaxVoice::tenor)
                check(std::abs(release.l[100] - .05f * .25f * .25f * .70710678f) < 1e-7f,
                      "Marcato and pedal must produce one open gesture");
        }
        s.saxVoice = topbrass::SaxVoice::baritone;
        for (int cc : {120, 123}) {
            reset();
            e.noteOn(1, 48, 1);
            Audio a(100);
            a.render(e);
            e.controller(1, cc, 0);
            e.noteOff(1, 48);
            Audio b(5000);
            b.render(e);
            check(peak(std::vector<float>(b.l.end() - 500, b.l.end())) == 0,
                  "Panic must not leave or trigger mechanical tails");
        }
        reset();
        e.noteOn(1, 48, 1);
        Audio resetHeld(5000);
        resetHeld.render(e);
        e.controller(1, 121, 0);
        e.noteOff(1, 48);
        Audio resetOpen(5000);
        resetOpen.render(e);
        check(std::abs(resetOpen.l[100] - .05f * .25f * .25f * .70710678f) < 1e-7f,
              "CC121 must remain controller reset and retain the physical release gesture");
        reset();
        for (int event = 0; event < 400; ++event) {
            e.noteOn(1 + event % 16, 48, 1);
            Audio tick(1);
            tick.render(e);
        }
        check(e.activeVoices() <=
                  topbrass::maxVoices + topbrass::maxStealTails + topbrass::maxKeyVoices + topbrass::maxKeyTails,
              "Dense details must remain bounded separately from musical voices");
        reset();
        e.noteOn(1, 48, 1);
        Audio first(100);
        first.render(e);
        e.allNotesOff(0, true);
        e.noteOff(1, 48);
        Audio silent(100);
        silent.render(e);
        check(peak(silent.l) == 0, "Immediate panic must be silent");
        reset();
        e.noteOn(1, 48, 1);
        Audio begin(5000);
        begin.render(e);
        s.parts[0].octave = 1;
        e.setSettings(s);
        e.noteOff(1, 48);
        Audio captured(5000);
        captured.render(e);
        check(std::abs(captured.l[100] - .05f * .25f * .25f * .70710678f) < 1e-7f,
              "Open must use captured target rather than changed octave");
        s.parts[0].octave = 0;
        reset();
        e.noteOn(1, 48, 1);
        Audio playing(100);
        playing.render(e);
        s.parts[0].enabled = false;
        e.setSettings(s);
        e.noteOff(1, 48);
        Audio muted(5000);
        muted.render(e);
        check(peak(std::vector<float>(muted.l.end() - 100, muted.l.end())) == 0,
              "Mute must clear gesture and fade detail");
        s.parts[0].enabled = true;
        for (int note : {47, 52, 64}) {
            reset();
            e.noteOn(1, note, 1);
            Audio a(5000);
            a.render(e);
            e.noteOff(1, note);
            Audio b(5000);
            b.render(e);
            check(peak(a.l) == 0 && peak(b.l) == 0, "Missing detail mappings must not borrow neighboring keys");
        }
        reset();
        s.keyNoise = 0;
        e.setSettings(s);
        e.noteOn(1, 48, 1);
        Audio disabled(5000);
        disabled.render(e);
        check(peak(disabled.l) == 0, "Zero amount leaves musical output unchanged");
        s.keyNoise = 1;
        s.parts[0].pan = -1;
        s.parts[0].channel = 1;
        s.parts[1].enabled = true;
        s.parts[1].channel = 2;
        s.parts[1].pan = 1;
        reset();
        e.noteOn(2, 48, 1);
        for (int event = 0; event < 400; ++event) {
            e.noteOn(1, 48, 1);
            Audio tick(1);
            tick.render(e);
            check(std::abs(tick.r[0] - .005f) < 1e-7f,
                  "Musical sentinel must survive saturated mechanical playback unchanged");
        }
        s.parts[1].enabled = false;
        s.parts[0].channel = 0;
        s.parts[0].pan = 0;
        details(keys, true);
        check(!bank.loadKeyNoises(keys, error), "Invalid key mapping must fail transactionally");
        check(bank.keyNoise(48, false, 0) != nullptr, "Failed load must preserve prior detail mappings");
        details(keys, false, 44100, true);
        check(bank.loadKeyNoises(keys, error), error.c_str());
        std::vector<float> reused;
        for (int key : {36, 48}) {
            topbrass::Engine converted(bank);
            converted.setSettings(s);
            converted.prepare(48000);
            converted.noteOn(1, key, 1);
            Audio resampled(2000);
            resampled.render(converted);
            for (int frame = 4; frame < 1900; ++frame) {
                const float expected = (.01f + frame * (44100.0f / 48000) * .000001f) * .25f * .25f * .70710678f;
                check(std::abs(resampled.l[frame] - expected) < 1e-7f,
                      "Mechanical source-rate conversion must preserve authored root rather than transpose reused "
                      "octave recordings");
            }
            if (key == 36)
                reused = resampled.l;
            else
                check(reused == resampled.l, "Authored octave reuse must preserve mechanical pitch");
        }
        details(keys);check(bank.loadKeyNoises(keys,error),error.c_str());s.parts[0].channel=0;s.parts[0].pan=0;s.parts[1].enabled=true;s.parts[1].channel=0;s.keyNoise=1;s.saxVoice=topbrass::SaxVoice::baritone;
        topbrass::Engine routed(bank);routed.setSettings(s);routed.prepare(48000);routed.noteOnPart(0,1,60,48,1);Audio saxClose(5000);saxClose.render(routed);routed.noteOnPart(1,1,60,48,1);Audio trumpetNote(5000);trumpetNote.render(routed);routed.noteOffPart(0,1,60);Audio saxOpen(1000);saxOpen.render(routed);check(std::abs(saxOpen.l[100]-.05f*.25f*.25f*.70710678f)<1e-7f,"A different instrument receiving the same source note must not clear the sax physical-key gesture");
        std::filesystem::remove(musical);
        std::filesystem::remove(keys);
        std::cout << "PASS: recorded mechanical gain/takes, physical gesture lifecycle, pedal, pending cancellation, "
                     "routing, panic and optional detail\n";
    } catch (const std::exception& ex) {
        std::cerr << "FAIL: " << ex.what() << '\n';
        return 1;
    }
}
