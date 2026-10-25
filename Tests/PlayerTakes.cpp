#include "Engine.h"
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void word(std::ostream& out, std::uint32_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}
void scalar(std::ostream& out, float value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}
void fixture(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    out.write("TBBANK03", 8); word(out, 24); word(out, 10);
    for (int index = 0; index < 24; ++index) {
        const int instrument = index < 8 ? index/2 : index < 12 || index >= 22 ? 4 : index < 14 || index >= 16 ? 1 : 4;
        const int articulation = index < 8 ? index%2 : index < 12 ? index-8 : index < 16 ? 1 : 0;
        const int layer = index < 16 || index >= 22 ? 1 : (index-16)/2+1;
        const int frames = articulation == 0 ? 48000 : 24000;
        for (int value : {instrument, articulation, 60, layer, 48000, frames, 0, 0, 0, 0, 0, 0, 0, 0}) word(out, static_cast<std::uint32_t>(value));
        for (int frame = 0; frame < frames; ++frame) scalar(out, .01f*(index+1));
    }
    for (int take = 0; take < 4; ++take) {
        const int sample = take < 2 ? 3 : take == 2 ? 12 : 13;
        for (int value : {sample, 1, 1, 60, 60, 60, take < 3 ? 1 : 101, take < 3 ? 100 : 127, 0, take}) word(out, static_cast<std::uint32_t>(value));
        scalar(out, 0); scalar(out, 1);
    }
    for (int index = 16; index < 22; ++index) {
        const int layer = (index-16)/2+1;
        for (int value : {index, 1, 0, 60, 60, 60, 1, 127, layer, index%2}) word(out, static_cast<std::uint32_t>(value));
        scalar(out, 0); scalar(out, 1);
    }
}
topbrass::Settings settings(int primary, topbrass::SaxVoice sax = topbrass::SaxVoice::tenor) {
    topbrass::Settings result;
    result.articulation = topbrass::Articulation::shortNote;
    result.saxVoice = sax;
    result.gain = .5f;
    for (auto& part : result.parts) { part.enabled = false; part.gain = .1f; part.octave = 0; }
    result.parts[static_cast<std::size_t>(primary)].enabled = true;
    result.parts[static_cast<std::size_t>(primary+4)].enabled = true;
    result.parts[static_cast<std::size_t>(primary)].pan = -1;
    result.parts[static_cast<std::size_t>(primary+4)].pan = 1;
    return result;
}
std::array<float, 2> frame(topbrass::Engine& engine) {
    std::array<float, 2> result{};
    engine.render(&result[0], &result[1], 1);
    return result;
}
void paired(const topbrass::Bank& bank, int primary, topbrass::SaxVoice sax, float velocity, bool distinct,
            topbrass::Articulation articulation = topbrass::Articulation::shortNote, int target = 60) {
    for (bool reversed : {false, true}) for (int history = 0; history < 7; ++history) for (int gap : {0, 1, 96}) {
        auto config = settings(primary, sax);
        config.articulation = articulation;
        topbrass::Engine engine(bank);
        engine.setSettings(config); engine.prepare(48000);
        for (int index = 0; index < history; ++index) {
            engine.noteOnPart(primary, 1, 60, target, velocity);
            engine.allNotesOff(0, true);
        }
        const int first = reversed ? primary+4 : primary;
        const int second = reversed ? primary : primary+4;
        engine.noteOnPart(first, 1, 60, target, velocity);
        for (int index = 0; index < gap; ++index) frame(engine);
        engine.noteOnPart(second, 1, 60, target, velocity);
        const auto audio = frame(engine);
        check(audio[0] > 0 && audio[1] > 0, "Both paired players must sound");
        check(distinct ? std::abs(audio[0]-audio[1]) > 1e-5f : std::abs(audio[0]-audio[1]) < 1e-7f,
              "Paired players must choose distinct eligible recordings, retaining single-take fallback");
        const auto before = audio;
        auto muted = config; muted.parts[static_cast<std::size_t>(second)].enabled = false;
        engine.setSettings(muted);
        const auto after = frame(engine);
        check(std::abs(after[static_cast<std::size_t>(first == primary ? 0 : 1)]-before[static_cast<std::size_t>(first == primary ? 0 : 1)]) < 1e-7f,
              "Muting the counterpart must not change the held player's recording");
    }
}
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected fixture directory");
        std::filesystem::create_directories(argv[1]);
        const auto path = std::filesystem::path(argv[1])/"player-takes.bank";
        fixture(path);
        topbrass::Bank bank; std::string error;
        check(bank.load(path, error), error.c_str());
        paired(bank, 1, topbrass::SaxVoice::tenor, .7f, true);
        paired(bank, 1, topbrass::SaxVoice::tenor, 1, false);
        paired(bank, 0, topbrass::SaxVoice::baritone, 1, true);
        paired(bank, 0, topbrass::SaxVoice::tenor, 1, false);
        paired(bank, 0, topbrass::SaxVoice::marcato, 1, true);
        paired(bank, 0, topbrass::SaxVoice::subtone, 1, false);
        paired(bank, 0, topbrass::SaxVoice::growl, 1, false);
        paired(bank, 0, topbrass::SaxVoice::subtone, 1, true, topbrass::Articulation::shortNote, 61);
        paired(bank, 0, topbrass::SaxVoice::growl, 1, true, topbrass::Articulation::shortNote, 61);
        paired(bank, 1, topbrass::SaxVoice::tenor, .7f, true, topbrass::Articulation::sustain);
        for (auto articulation : {topbrass::Articulation::shortNote, topbrass::Articulation::sustain}) {
            auto config = settings(0, topbrass::SaxVoice::marcato); config.articulation = articulation;
            topbrass::Engine engine(bank); engine.setSettings(config); engine.prepare(48000);
            engine.noteOn(1, 60, 1);
            std::array<float, 25000> left{}, right{};
            engine.render(left.data(), right.data(), static_cast<int>(left.size()));
            check(left.back() > 0 && right.back() > 0 && std::abs(left.back()-right.back()) > 1e-5f,
                  "Marcato must avoid duplicate sustained recordings after both recorded attacks finish");
        }
        for (int primary : {0, 1}) {
            auto config = settings(primary);
            for (int part : {primary, primary+4}) {
                auto solo = config;
                solo.parts[static_cast<std::size_t>(primary == part ? primary+4 : primary)].enabled = false;
                topbrass::Engine engine(bank); engine.setSettings(solo); engine.prepare(48000);
                engine.noteOnPart(part, 1, 60, 60, .7f);
                const auto audio = frame(engine);
                const auto* sample = primary == 0 ? bank.selectSample(0, 1, 60, .7f, 0)
                                                 : &bank.samples()[static_cast<std::size_t>(bank.selectZone(1,1,60,89,0,0)->sample)];
                const float expected = sample->audio[0] * std::pow(.7f,1.3f) * .1f * .5f;
                check(std::abs(audio[static_cast<std::size_t>(part == primary ? 0 : 1)]-expected) < 1e-7f,
                      "Isolated primary and extra players must preserve ordinary take selection");
            }
        }
        for (int changed : {0, 1}) {
            topbrass::Engine engine(bank); engine.setSettings(settings(1)); engine.prepare(48000);
            engine.noteOnPart(1, 1, 60, 60, .7f);
            engine.noteOnPart(5, changed == 0 ? 2 : 1, changed == 1 ? 61 : 60, 60, .7f);
            const auto audio = frame(engine);
            check(std::abs(audio[0]-audio[1]) < 1e-7f, "Unrelated MIDI gestures must retain ordinary take selection");
        }
        {
            topbrass::Engine engine(bank); engine.setSettings(settings(1)); engine.prepare(48000);
            engine.noteOnPart(1, 1, 60, 60, .7f);
            engine.noteOnPart(5, 1, 60, 61, .7f);
            const auto audio = frame(engine);
            check(audio[0] > 0 && std::abs(audio[1]) < 1e-7f, "A different target must not borrow an ineligible neighboring recording");
        }
        std::cout << "PASS: paired recorded takes, both onset orders, independent histories, two-millisecond gaps, eligible velocity, aliases, single-take fallback, solos and mute\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
