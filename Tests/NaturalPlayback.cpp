#include "Engine.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Stereo { std::vector<float> left, right; };
Stereo play(const topbrass::Bank& bank, const topbrass::Settings& settings, int note, int rate,
            int frames, int offAt = -1, int releaseController = -1, float velocity = 1, int repetition = 0) {
    topbrass::Engine engine(bank); engine.setSettings(settings); engine.prepare(rate);
    for (int take = 0; take < repetition; ++take) { engine.noteOn(1,note,velocity); engine.allNotesOff(0,true); }
    engine.noteOn(1, note, velocity);
    if (releaseController == 64) engine.controller(1, 64, 127);
    Stereo out {std::vector<float>(static_cast<std::size_t>(frames)), std::vector<float>(static_cast<std::size_t>(frames))};
    for (int offset = 0; offset < frames;) {
        if (offset == offAt) {
            engine.noteOff(1, note);
            if (releaseController >= 0) engine.controller(1, releaseController, 0);
        }
        int count = std::min(128, frames - offset);
        if (offAt > offset) count = std::min(count, offAt - offset);
        engine.render(out.left.data()+offset, out.right.data()+offset, count); offset += count;
    }
    return out;
}
float sourceError(const Stereo& out, const topbrass::Sample& sample, float pan) {
    const float scale=sample.channels()==2?1.41421356237f:1;
    const float l = .125f * std::min(1.0f,scale*std::cos((pan + 1) * 3.14159265358979323846f / 4));
    const float r = .125f * std::min(1.0f,scale*std::sin((pan + 1) * 3.14159265358979323846f / 4));
    float worst = 0;
    for (std::size_t frame = 0; frame < out.left.size(); ++frame) {
        worst = std::max(worst, std::abs(out.left[frame] - sample.audio[frame] * l));
        worst = std::max(worst, std::abs(out.right[frame] - (sample.channels()==2?sample.rightAudio[frame]:sample.audio[frame]) * r));
    }
    return worst;
}
const topbrass::Sample& choose(const topbrass::Bank& bank, int instrument, int target, int articulation = 1) {
    const topbrass::Sample* picked = nullptr;
    for (const auto& sample : bank.samples()) if (sample.instrument == instrument && sample.articulation == articulation) {
        if (!picked || std::abs(sample.root-target) < std::abs(picked->root-target)
            || (sample.root == picked->root && sample.layer > picked->layer)) picked = &sample;
    }
    if (!picked) throw std::runtime_error("Missing source recording");
    return *picked;
}
void mappedSource(const topbrass::Bank& bank, int instrument, int art, topbrass::Settings settings, float pan, int velocity = 127, int repetition = 0) {
    const topbrass::Zone* zone = nullptr;
    for (const auto& z : bank.zones())
        if (z.instrument == instrument && z.articulation == art && z.lowVelocity <= velocity && z.highVelocity >= velocity && z.roundRobin == repetition
            && z.dynamicLayer == (art == 0 ? 3 : 0) && z.lowKey <= z.root && z.root <= z.highKey) { zone = &z; break; }
    check(zone != nullptr, "Mapped source comparison requires a native-pitch mapping");
    const auto& source = bank.samples()[static_cast<std::size_t>(zone->sample)];
    settings.articulation = art == 0 ? topbrass::Articulation::sustain
                         : art == topbrass::recordedFallArticulation ? topbrass::Articulation::fall : topbrass::Articulation::shortNote;
    settings.parts[static_cast<std::size_t>(instrument == 4 ? 0 : instrument)].doubleTongue = art == topbrass::recordedDoubleTongueArticulation;
    const int frames = std::min(source.rate/4, static_cast<int>(source.audio.size())/2);
    const auto out = play(bank, settings, zone->root, source.rate, frames, -1, -1, velocity/127.0f, repetition);
    const double step = std::exp2(zone->pitchCents/1200.0);
    float worst = 0;
    for (int frame = 0; frame < frames; ++frame) {
        const double position = frame*step;
        const int index = static_cast<int>(position);
        const float t = static_cast<float>(position-index), t2=t*t, t3=t2*t;
        const auto read = [&](int n) { return source.audio[static_cast<std::size_t>(std::clamp(n, 0, static_cast<int>(source.audio.size())-1))]; };
        float sample = read(index-1)*(-.5f*t+t2-.5f*t3) + read(index)*(1-2.5f*t2+1.5f*t3)
                     + read(index+1)*(.5f*t+2*t2-1.5f*t3) + read(index+2)*(-.5f*t2+.5f*t3);
        if (!source.gainTable.empty()) sample *= source.gainTable[std::min(static_cast<std::size_t>(std::lround((position+source.gainOffset)/32)), source.gainTable.size()-1)];
        sample *= .125f*zone->gain*std::pow(velocity/127.0f,1.3f);
        const float l=sample*std::cos((pan+1)*3.14159265358979323846f/4), r=sample*std::sin((pan+1)*3.14159265358979323846f/4);
        worst=std::max({worst,std::abs(out.left[static_cast<std::size_t>(frame)]-l),std::abs(out.right[static_cast<std::size_t>(frame)]-r)});
    }
    std::cout << "mapped instrument=" << instrument << " art=" << art << " velocity=" << velocity << " take=" << repetition << " source_error=" << worst << '\n';
    check(worst < 1e-6f, "Mapped playback must preserve source audio with authored tuning, gain and pan");
}
void tenorSustains(const topbrass::Bank& bank) {
    topbrass::Settings settings;
    for (auto& part : settings.parts) part.enabled = false;
    settings.parts[0].enabled = true;
    settings.parts[0].gain = .25f;
    settings.gain = .5f;
    settings.articulation = topbrass::Articulation::sustain;
    int count = 0;
    float worst = 0;
    for (const auto& sample : bank.samples()) {
        if (sample.instrument != 0 || sample.articulation != 0) continue;
        check(sample.loopEnd - sample.loopCrossfade >= sample.rate * 4,
              "Tenor sustain must preserve at least four seconds before its loop crossfade");
        const float velocity = sample.layer == 2 ? .4f : 1;
        const float gain = .125f * std::pow(velocity, 1.3f);
        const int heldFrames = sample.rate * 13;
        const int endingFrames = sample.releaseStart>0 ? sample.frames()-sample.releaseStart : sample.rate/4;
        const auto output = play(bank, settings, sample.root, sample.rate, heldFrames + std::max(endingFrames,sample.rate/2)+2, heldFrames, -1, velocity);
        const int period = sample.loopEnd - sample.loopStart - sample.loopCrossfade;
        check(period > 0, "Tenor loop must have a positive repeated period");
        for (int frame = 0; frame < heldFrames; ++frame) {
            const int position = frame < sample.loopEnd ? frame
                : sample.loopStart + sample.loopCrossfade + (frame-sample.loopEnd) % period;
            const auto expected = [&](const std::vector<float>& pcm) {
                float value = pcm[static_cast<std::size_t>(position)];
                if (position >= sample.loopEnd-sample.loopCrossfade) {
                    const int offset = position-(sample.loopEnd-sample.loopCrossfade);
                    const float blend = static_cast<float>(offset)/sample.loopCrossfade;
                    value = value*(1-blend) + pcm[static_cast<std::size_t>(sample.loopStart+offset)]*blend;
                }
                return value*gain;
            };
            worst = std::max({worst, std::abs(output.left[static_cast<std::size_t>(frame)]-expected(sample.audio)),
                              std::abs(output.right[static_cast<std::size_t>(frame)]-expected(sample.rightAudio))});
        }
        for (std::size_t frame = static_cast<std::size_t>(heldFrames + endingFrames); frame < output.left.size(); ++frame)
            check(output.left[frame] == 0 && output.right[frame] == 0,
                  "A long-loop tenor must still finish its release without stuck voices");
        check(std::all_of(output.left.begin(), output.left.end(), [](float x) { return std::isfinite(x); }) &&
              std::all_of(output.right.begin(), output.right.end(), [](float x) { return std::isfinite(x); }),
              "Repeated tenor loops and releases must remain finite");
        ++count;
    }
    check(count == 46, "Qualify every recorded tenor sustain and dynamic layer");
    check(worst < 1e-6f, "Tenor playback must preserve the source and both channels through repeated loop crossfades");
    std::cout << "Tenor full-source/loop oracle: " << count << " recordings,13s holds, max error=" << worst << '\n';
}
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Usage: TopBrassNaturalPlayback bank");
        topbrass::Bank bank; std::string error; check(bank.load(argv[1], error), error.c_str());
        tenorSustains(bank);
        constexpr std::array<int, topbrass::requiredInstrumentCount> notes {60, 60, 48, 36, 48};
        for (int instrument = 0; instrument < topbrass::requiredInstrumentCount; ++instrument) {
            const auto& sample = choose(bank, instrument, notes[static_cast<std::size_t>(instrument)]);
            const int part = instrument == 4 ? 0 : instrument;
            topbrass::Settings settings;
            for (auto& strip : settings.parts) strip.enabled = false;
            settings.parts[static_cast<std::size_t>(part)].enabled = true;
            settings.parts[static_cast<std::size_t>(part)].gain = .25f;
            settings.saxVoice = instrument == 4 ? topbrass::SaxVoice::baritone : topbrass::SaxVoice::tenor;
            settings.gain = .5f; settings.tone = 0; settings.tightness = 0;
            const int frames = std::min(static_cast<int>(sample.audio.size()) - 2, sample.rate / 4);
            for (float pan : {-1.0f, 0.0f, 1.0f}) {
                settings.parts[static_cast<std::size_t>(part)].pan = pan;
                if (bank.isMapped(instrument, 1)) mappedSource(bank, instrument, 1, settings, pan);
                else {
                    const auto rendered = play(bank, settings, sample.root, sample.rate, frames);
                    const float worst = sourceError(rendered, sample, pan);
                    check(worst < 1e-6f, "Native-pitch neutral playback must preserve the recording, including its attack and pan");
                }
                const auto& longRecording = choose(bank, instrument, notes[static_cast<std::size_t>(instrument)], 0);
                auto longSettings = settings; longSettings.articulation = topbrass::Articulation::sustain;
                const int longFrames = std::min(static_cast<int>(longRecording.audio.size())-2, longRecording.rate/4);
                if (bank.isMapped(instrument, 0)) mappedSource(bank, instrument, 0, settings, pan);
                else {
                    const auto longOutput = play(bank, longSettings, longRecording.root, longRecording.rate, longFrames);
                    check(sourceError(longOutput, longRecording, pan) < 1e-6f, "Sustained-note attacks must preserve their native recording");
                }
                if (bank.isMapped(instrument, topbrass::recordedFallArticulation)) {
                    const int boundary = instrument==1 ? 63 : 84;
                    for (int velocity : {1,boundary,boundary+1,127})
                        for (int take=0; take<4; ++take)
                            mappedSource(bank,instrument,topbrass::recordedFallArticulation,settings,pan,velocity,take);
                }
                if (instrument == 1 || instrument == 2) {
                    check(bank.isMapped(instrument, topbrass::recordedDoubleTongueArticulation), "Both brass instruments require recorded double-tongue mappings");
                    for (int velocity : {1,63,64,127})
                        for (int take=0; take<4; ++take)
                            mappedSource(bank,instrument,topbrass::recordedDoubleTongueArticulation,settings,pan,velocity,take);
                }
            }
            const int duration = static_cast<int>(sample.audio.size()) + sample.rate / 10;
            const auto held = play(bank, settings, sample.root, sample.rate, duration);
            for (int controller : {-1, 64, 121}) {
                const auto released = play(bank, settings, sample.root, sample.rate, duration, sample.rate / 20, controller);
                check(held.left == released.left && held.right == released.right,
                      "Explicit short recordings must preserve natural endings across note-off, pedal-up and controller reset");
            }
            for (int controller : {7, 11}) {
                topbrass::Engine sameFrame(bank); sameFrame.setSettings(settings); sameFrame.prepare(sample.rate);
                sameFrame.noteOn(1, sample.root, 1);
                std::array<float, 1024> left {}, right {};
                sameFrame.render(left.data(), right.data(), 0);
                sameFrame.controller(1, controller, 0);
                sameFrame.render(left.data(), right.data(), 1024);
                check(std::all_of(left.begin(), left.end(), [](float x) { return x == 0; })
                      && std::all_of(right.begin(), right.end(), [](float x) { return x == 0; }),
                      "Controllers at the note-on sample must apply before initializing audible gain");
            }
            {
                topbrass::Engine retrigger(bank); retrigger.setSettings(settings); retrigger.prepare(sample.rate);
                retrigger.noteOn(1, sample.root, 1); retrigger.noteOn(1, sample.root, 1);
                check(retrigger.activeVoices() == 1, "Retrigger before the first frame must cancel the unstarted previous voice");
                auto sustain = settings; sustain.articulation = topbrass::Articulation::sustain;
                topbrass::Engine zeroDuration(bank); zeroDuration.setSettings(sustain); zeroDuration.prepare(sample.rate);
                zeroDuration.noteOn(1, sample.root, 1); zeroDuration.noteOff(1, sample.root);
                std::array<float, 1024> left {}, right {}; zeroDuration.render(left.data(), right.data(), 1024);
                check(zeroDuration.activeVoices() == 0 && std::all_of(right.begin(), right.end(), [](float x) { return x == 0; }),
                      "Zero-duration sustained notes must be silent");
            }
            settings.parts[static_cast<std::size_t>(part)].pan = 0;
            settings.parts[static_cast<std::size_t>(part)].gain = 32;
            settings.gain = 1;
            const auto hot = play(bank, settings, sample.root, sample.rate, frames);
            for (const auto* channel : {&hot.left, &hot.right}) {
                float peak = 0;
                for (float value : *channel) {
                    check(std::isfinite(value) && std::abs(value) <= 1, "Overloaded output must remain bounded");
                    peak = std::max(peak, std::abs(value));
                }
                check(peak > .95f, "Each overloaded channel must actually reach the safety knee");
            }
            topbrass::Engine controlled(bank); controlled.setSettings(settings); controlled.prepare(sample.rate);
            controlled.noteOn(1, sample.root, 1);
            std::array<float, 128> left {}, right {}; controlled.render(left.data(), right.data(), 128);
            settings.parts[static_cast<std::size_t>(part)].enabled = false; controlled.setSettings(settings);
            for (int elapsed = 0; elapsed < sample.rate; elapsed += 128) {
                left.fill(0); right.fill(0); controlled.render(left.data(), right.data(), 128);
            }
            check(controlled.activeVoices() == 0, "Muting must terminate a natural short tail");
            settings.parts[static_cast<std::size_t>(part)].enabled = true; controlled.setSettings(settings);
            controlled.noteOn(1, sample.root, 1); controlled.allNotesOff(0, true);
            check(controlled.activeVoices() == 0, "Immediate panic must terminate natural short tails");
        }
        {
            topbrass::Settings settings;
            for (auto& part : settings.parts) part.enabled = false;
            settings.parts[0].enabled = true; settings.articulation = topbrass::Articulation::sustain;
            topbrass::Engine packedMidi(bank); packedMidi.setSettings(settings); packedMidi.prepare(48000);
            for (int event = 0; event < topbrass::maxVoices + topbrass::maxStealTails; ++event)
                packedMidi.noteOn(1 + event / 24, 48 + event % 24, 1);
            check(packedMidi.activeVoices() == topbrass::maxVoices,
                  "Stealing unstarted notes must not create audible release tails");
        }
        std::cout << "PASS: source-null fidelity, immediate pan, natural short tails, and overload safety\n";
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
