#include "Engine.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct Audio { std::vector<float> left, right; };
Audio play(const topbrass::Bank& bank, int part, int note, float velocity, int take, int frames,
           topbrass::Articulation articulation, bool doubled, int rate = 48000, bool release = false) {
    topbrass::Settings settings;
    for (auto& strip : settings.parts) strip.enabled = false;
    settings.parts[static_cast<std::size_t>(part)].enabled = true;
    settings.parts[static_cast<std::size_t>(part)].gain = .125f;
    settings.parts[static_cast<std::size_t>(part)].doubleTongue = doubled;
    settings.gain = .5f;
    settings.articulation = articulation;
    topbrass::Engine engine(bank);
    engine.setSettings(settings); engine.prepare(rate);
    for (int i = 0; i < take; ++i) { engine.noteOn(1, note, velocity); engine.allNotesOff(0, true); }
    engine.noteOn(1, note, velocity);
    Audio audio{std::vector<float>(static_cast<std::size_t>(frames)), std::vector<float>(static_cast<std::size_t>(frames))};
    if (release) {
        engine.render(audio.left.data(), audio.right.data(), 127);
        engine.noteOff(1, note);
        engine.render(audio.left.data()+127, audio.right.data()+127, frames-127);
    } else engine.render(audio.left.data(), audio.right.data(), frames);
    return audio;
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected bank path");
        topbrass::Bank bank; std::string error;
        check(bank.load(argv[1], error), error.c_str());
        int count = 0;
        float worst = 0;
        for (const auto& zone : bank.zones()) {
            if (zone.articulation != topbrass::recordedDoubleTongueArticulation) continue;
            check((zone.instrument == 1 || zone.instrument == 2) && zone.dynamicLayer == 0 && zone.roundRobin < 4,
                  "Double-tongue regions must use a brass instrument and four authored alternatives");
            const auto& source = bank.samples()[static_cast<std::size_t>(zone.sample)];
            check(source.channels() == 1 && source.loopEnd == 0 && source.releaseStart == 0 && source.gainTable.empty(),
                  "The pinned double-tongue recordings are finite mono performances");
            const int note = zone.lowKey;
            const int midiVelocity = (zone.lowVelocity+zone.highVelocity)/2;
            const float velocity = midiVelocity/127.0f;
            const double step = source.rate/48000.0*std::exp2((note-zone.root)/12.0+zone.pitchCents/1200.0);
            const int frames = static_cast<int>(std::ceil(source.frames()/step))+128;
            const auto held = play(bank, zone.instrument, note, velocity, zone.roundRobin, frames,
                                   topbrass::Articulation::shortNote, true);
            const auto released = play(bank, zone.instrument, note, velocity, zone.roundRobin, frames,
                                       topbrass::Articulation::shortNote, true, 48000, true);
            check(held.left == released.left && held.right == released.right,
                  "Early note-off must preserve both recorded tongue attacks and the natural ending");
            const float gain = .0625f*zone.gain*std::pow(velocity, 1.3f)*std::cos(3.14159265358979323846f*.25f);
            for (int frame = 0; frame < frames; ++frame) {
                const double position = frame*step;
                float expected = 0;
                if (position < source.frames()) {
                    const int index = static_cast<int>(position);
                    const float t = static_cast<float>(position-index), t2=t*t, t3=t2*t;
                    const auto read = [&](int at) { return source.audio[static_cast<std::size_t>(std::clamp(at, 0, source.frames()-1))]; };
                    expected = (read(index-1)*(-.5f*t+t2-.5f*t3) + read(index)*(1-2.5f*t2+1.5f*t3)
                              + read(index+1)*(.5f*t+2*t2-1.5f*t3) + read(index+2)*(-.5f*t2+.5f*t3))*gain;
                }
                worst = std::max({worst, std::abs(held.left[static_cast<std::size_t>(frame)]-expected),
                                  std::abs(held.right[static_cast<std::size_t>(frame)]-expected)});
            }
            ++count;
        }
        check(count == 472 && worst < 1e-6f, "Every double-tongue region must preserve its authored pitch, gain, take and complete source");
        for (int part = 0; part < 4; ++part)
            for (auto articulation : {topbrass::Articulation::sustain, topbrass::Articulation::fall, topbrass::Articulation::shortNote}) {
                if ((part == 1 || part == 2) && articulation == topbrass::Articulation::shortNote) continue;
                const int note = part < 2 ? 60 : part == 2 ? 48 : 36;
                const auto normal = play(bank, part, note, .8f, 0, 4096, articulation, false);
                const auto doubled = play(bank, part, note, .8f, 0, 4096, articulation, true);
                check(normal.left == doubled.left && normal.right == doubled.right,
                      "Double tonguing must not change other articulations or invent saxophone/tuba gestures");
            }
        std::cout << "PASS: " << count << " complete double-tongue regions, max error=" << worst
                  << "; recorded endings and scoped style selection\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
