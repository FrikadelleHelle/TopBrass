#include "Engine.h"
#include <algorithm>
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
        out.put(static_cast<char>(n >> (i * 8)));
}
void scalar(std::ostream& out, float x) { out.write(reinterpret_cast<const char*>(&x), 4); }
void music(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    out.write("TBBANK04", 8);
    word(out, 36);
    word(out, 0);
    for (int root : {50, 62, 74})
        for (int part = 0; part < 5; ++part)
            for (int art = 0; art < (part == 4 ? 4 : 2); ++art) {
                const int frames = art == 1 ? 14400 : 24000;
                for (int n : {part, art, root, 1, 48000, frames, art == 0 ? 4000 : 0, art == 0 ? 20000 : 0, 0, 0, 0, 0,
                              0, 0, 1})
                    word(out, n);
                for (int i = 0; i < frames; ++i)
                    scalar(out, 0);
            }
}
void air(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    out.write("TBAIR001", 8);
    for (int n : {48000, 24000, 4000, 20000, 36, 68, 50})
        word(out, n);
    scalar(out, .1f);
    scalar(out, .4f);
    for (int i = 0; i < 24000; ++i)
        scalar(out, .03f);
}
struct Audio {
    std::vector<float> l, r;
    explicit Audio(int n) : l(n), r(n) {};
};
Audio render(topbrass::Engine& e, int frames, int block) {
    Audio out(frames);
    for (int offset = 0; offset < frames;) {
        const int count = std::min(block, frames - offset);
        e.render(out.l.data() + offset, out.r.data() + offset, count);
        offset += count;
    }
    return out;
}
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected fixture directory");
        const auto dir = std::filesystem::path(argv[1]);
        const auto musical = dir / "airflow-music.bank", wind = dir / "airflow-layer.bank";
        music(musical);
        air(wind);
        topbrass::Bank bank;
        std::string error;
        check(bank.load(musical, error), error.c_str());
        check(bank.loadAirflow(wind, error), error.c_str());
        topbrass::Settings s;
        for (auto& p : s.parts)
            p.enabled = false;
        s.parts[0].enabled = true;
        s.parts[0].gain = .5f;
        s.gain = .5f;
        s.saxVoice = topbrass::SaxVoice::baritone;
        s.articulation = topbrass::Articulation::sustain;
        s.airflowAmount = 1;
        const auto play = [&](int block, float velocity, int notes, topbrass::SaxVoice voice) {
            topbrass::Engine e(bank);
            auto settings = s;
            settings.saxVoice = voice;
            e.setSettings(settings);
            e.prepare(48000);
            for (int n = 0; n < notes; ++n)
                e.noteOn(1, 50 + n, velocity);
            return render(e, 30000, block);
        };
        const auto single = play(128, 1, 1, topbrass::SaxVoice::baritone),
                   partitioned = play(31, 1, 1, topbrass::SaxVoice::baritone),
                   quiet = play(128, .2f, 1, topbrass::SaxVoice::baritone),
                   overlap = play(128, 1, 2, topbrass::SaxVoice::baritone),
                   marcato = play(128, 1, 1, topbrass::SaxVoice::marcato);
        check(single.l == partitioned.l, "Engine airflow must be invariant to host block boundaries");
        check(single.l == quiet.l, "Airflow must be independent of note velocity");
        float overlapError = 0, marcatoError = 0;
        for (std::size_t i = 0; i < single.l.size(); ++i) {
            overlapError = std::max(overlapError, std::abs(single.l[i] - overlap.l[i]));
            marcatoError = std::max(marcatoError, std::abs(single.l[i] - marcato.l[i]));
        }
        std::cout << "Overlap error=" << overlapError << ", marcato error=" << marcatoError << "\n";
        check(overlapError < 1e-7f && marcatoError < 1e-7f, "Overlaps and marcato must not stack airflow");
        check(std::abs(single.l[1000] - .03f * .4f * .5f * .5f * .70710678f) < 1e-7f,
              "Airflow calibration must reach the actual engine output once");
        topbrass::Engine e(bank);
        const auto reset = [&] {
            e.setSettings(s);
            e.prepare(48000);
        };
        s.articulation = topbrass::Articulation::shortNote;
        reset();
        e.noteOn(1, 50, 1);
        const auto shortNote = render(e, 30000, 127);
        check(std::all_of(shortNote.l.end() - 1000, shortNote.l.end(), [](float x) { return x == 0; }) &&
                  e.activeVoices() == 0,
              "A held short must not drive endless airflow after musical completion");
        s.articulation = topbrass::Articulation::sustain;
        reset();
        e.noteOn(1, 50, 1);
        e.controller(1, 11, 0);
        const auto pressure = render(e, 1000, 128);
        check(std::all_of(pressure.l.begin(), pressure.l.end(), [](float x) { return x == 0; }),
              "Same-sample pressure zero must suppress air");
        reset();
        e.noteOn(1, 50, 1);
        e.noteOff(1, 50);
        const auto canceled = render(e, 1000, 128);
        check(std::all_of(canceled.l.begin(), canceled.l.end(), [](float x) { return x == 0; }),
              "A zero-duration note must not start airflow");
        reset();
        e.noteOn(1, 50, 1);
        render(e, 1000, 128);
        e.controller(1, 64, 127);
        e.noteOff(1, 50);
        const auto pedal = render(e, 24000, 128);
        check(pedal.l.back() > 0, "Audible pedal-held sustain must drive air");
        e.controller(1, 64, 0);
        render(e, 48000, 128);
        check(e.activeVoices() == 0, "Pedal-up must release airflow with the horn");
        reset();
        e.noteOn(1, 50, 1);
        render(e, 1000, 128);
        e.controller(1, 120, 0);
        const auto panic = render(e, 1000, 128);
        check(std::all_of(panic.l.begin(), panic.l.end(), [](float x) { return x == 0; }),
              "Immediate panic must clear airflow");
        for (int key : {52, 64, 74}) {
            reset();
            e.noteOn(1, key, 1);
            const auto note = render(e, 1000, 128);
            check((note.l.back() > 0) == (key <= 68),
                  "Airflow must honor its own range including keys without mechanical mappings");
        }
        reset();
        for (int channel = 1; channel <= 16; ++channel)
            e.noteOn(channel, 50, 1);
        render(e, 1000, 128);
        check(e.activeVoices() == 32, "Sixteen MIDI players must use sixteen musical and sixteen airflow streams");
        std::filesystem::remove(musical);
        std::filesystem::remove(wind);
        std::cout << "PASS: channel airflow, musical gates, pressure, velocity independence, overlap/marcato, source "
                     "range, pedal, panic and block invariance\n";
    } catch (const std::exception& ex) {
        std::cerr << "FAIL: " << ex.what() << '\n';
        return 1;
    }
}
