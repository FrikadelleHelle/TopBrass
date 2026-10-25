#include "Engine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
void check(bool good, const char* text) {
    if (!good)
        throw std::runtime_error(text);
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected bank path");
        topbrass::Bank bank;
        std::string error;
        check(bank.load(argv[1], error), error.c_str());
        for (auto articulation : {topbrass::Articulation::shortNote, topbrass::Articulation::sustain, topbrass::Articulation::fall}) {
            for (int part=0; part<topbrass::partCount; ++part) {
                topbrass::Settings gated;
                for (auto& slot:gated.parts) slot.enabled=false;
                gated.parts[static_cast<std::size_t>(part)].enabled=true;
                gated.articulation=articulation;
                gated.releaseMs=500;
                topbrass::Engine player(bank);
                player.setSettings(gated); player.prepare(48000);
                const auto range=player.partKeyRange(part);
                const int target=std::clamp(60,range.low,range.high);
                std::array<float,512> left{},right{};
                player.controller(1,64,127);
                player.noteOnPart(part,1,60,target,.9f);
                player.render(left.data(),right.data(),512);
                check(player.activeVoices()>0,"Gate fixture must sound a real recorded instrument");
                player.noteOffPart(part,1,60,true);
                left.fill(0);right.fill(0);
                player.render(left.data(),right.data(),512);
                check(player.activeVoices()==0,"A forced groove gate must finish all recorded layers within ten milliseconds despite pedal and long release settings");
            }
        }
        topbrass::Settings s;
        s.articulation = topbrass::Articulation::sustain;
        s.gain = .5f;
        for (auto& part : s.parts) {
            part.gain = .1f;
            part.octave = 0;
        }
        topbrass::Engine combined(bank), sax(bank), trumpet(bank);
        for (auto* e : {&combined, &sax, &trumpet}) {
            e->setSettings(s);
            e->prepare(48000);
        }
        combined.noteOnPart(0, 1, 60, 60, 1);
        sax.noteOnPart(0, 1, 60, 60, 1);
        std::array<float, 512> l{}, r{}, sl{}, sr{}, tl{}, tr{};
        combined.render(l.data(), r.data(), 512);
        sax.render(sl.data(), sr.data(), 512);
        check(l == sl && r == sr, "Targeted sax must match its isolated playback");
        combined.noteOnPart(1, 1, 60, 60, 1);
        trumpet.noteOnPart(1, 1, 60, 60, 1);
        l.fill(0);
        r.fill(0);
        sl.fill(0);
        sr.fill(0);
        combined.render(l.data(), r.data(), 512);
        sax.render(sl.data(), sr.data(), 512);
        trumpet.render(tl.data(), tr.data(), 512);
        float errorMax = 0;
        for (std::size_t i = 0; i < l.size(); ++i)
            errorMax = std::max({errorMax, std::abs(l[i] - sl[i] - tl[i]), std::abs(r[i] - sr[i] - tr[i])});
        check(errorMax < 1e-6f,
              "A targeted trumpet note must not release or alter an existing sax note with the same MIDI identity");
        combined.noteOffPart(1, 1, 60);
        std::array<float, 48000> tailL{}, tailR{};
        combined.render(tailL.data(), tailR.data(), 48000);
        check(combined.activeVoices() == 1, "Targeted trumpet release must leave the sax held");
        combined.endPart(0, 1);
        tailL.fill(0);
        tailR.fill(0);
        combined.render(tailL.data(), tailR.data(), 48000);
        check(combined.activeVoices() == 0, "Ending the owned part must terminate its remaining notes");
        combined.noteOnPart(1, 1, 60, 60, 1);
        combined.render(l.data(), r.data(), 512);
        combined.noteOnPart(1, 1, 64, 64, 1);
        combined.render(tailL.data(), tailR.data(), 48000);
        check(combined.activeVoices() == 3, "A routed player must retongue the new pitch instead of holding two pitches");
        combined.allNotesOff(0, true);
        combined.noteOnPart(-1, 1, 60, 60, 1);
        combined.noteOnPart(topbrass::partCount, 1, 60, 60, 1);
        check(combined.activeVoices() == 0, "Invalid targeted parts must not play");
        check(!s.parts[4].enabled && !s.parts[5].enabled, "Additional players must default to off");
        for (const auto pair : {std::array<int, 2>{0, 4}, std::array<int, 2>{1, 5}}) {
            auto duetSettings = s;
            for (auto& part : duetSettings.parts) part.enabled = false;
            duetSettings.parts[static_cast<std::size_t>(pair[0])].enabled = true;
            duetSettings.parts[static_cast<std::size_t>(pair[1])].enabled = true;
            topbrass::Engine duet(bank), first(bank), second(bank);
            for (auto* e : {&duet, &first, &second}) {
                e->setSettings(duetSettings);
                e->prepare(48000);
            }
            duet.noteOn(1, 60, 1);
            first.noteOnPart(pair[0], 1, 60, 60, 1);
            second.noteOnPart(pair[1], 1, 60, 60, 1);
            std::array<float, 512> dl{}, dr{}, al{}, ar{}, bl{}, br{};
            duet.render(dl.data(), dr.data(), 512);
            first.render(al.data(), ar.data(), 512);
            second.render(bl.data(), br.data(), 512);
            float difference = 0, energy = 0;
            for (std::size_t i = 0; i < dl.size(); ++i) {
                difference = std::max({difference, std::abs(dl[i] - al[i] - bl[i]), std::abs(dr[i] - ar[i] - br[i])});
                energy += bl[i] * bl[i];
            }
            check(energy > 0 && difference < 1e-6f, "Layered duplicate instruments must independently play and sum");
            duet.noteOffPart(pair[1], 1, 60);
            tailL.fill(0); tailR.fill(0);
            duet.render(tailL.data(), tailR.data(), 48000);
            tailL.fill(0); tailR.fill(0);
            first.render(tailL.data(), tailR.data(), 48000);
            check(duet.activeVoices() > 0 && duet.activeVoices() == first.activeVoices(), "Releasing a duplicate must leave its original player held");
        }
        topbrass::Engine force(bank), reference(bank), legacy(bank), silent(bank);
        for (auto* e : {&force, &reference, &legacy, &silent}) {
            e->setSettings(s);
            e->prepare(48000);
        }
        force.controller(1, 11, 127);
        legacy.controller(1, 11, 127);
        silent.controller(1, 11, 0);
        force.noteOnPart(1, 1, 60, 60, .25f, topbrass::NoteDynamics::strikeAndPressure);
        silent.noteOnPart(1, 1, 60, 60, .25f, topbrass::NoteDynamics::strikeAndPressure);
        reference.noteOnPart(1, 1, 60, 60, .25f);
        legacy.noteOnPart(1, 1, 60, 60, .25f);
        std::array<float, 4096> fl{}, fr{}, rl{}, rr{}, ll{}, lr{}, zl{}, zr{};
        force.render(fl.data(), fr.data(), 4096);
        reference.render(rl.data(), rr.data(), 4096);
        legacy.render(ll.data(), lr.data(), 4096);
        silent.render(zl.data(), zr.data(), 4096);
        check(fl == rl && fr == rr,
              "Section strike strength must still select recorded dynamics when expression is at full pressure");
        check(std::all_of(zl.begin(), zl.end(), [](float v) { return v == 0; }),
              "Zero pressure must remain silent with section strike dynamics");
        double forceEnergy = 0, legacyEnergy = 0, difference = 0;
        for (std::size_t i = 0; i < fl.size(); ++i) {
            forceEnergy += fl[i] * fl[i];
            legacyEnergy += ll[i] * ll[i];
        }
        check(forceEnergy > 0 && legacyEnergy > 0, "Both dynamic-layer comparisons must sound");
        const double gain = std::sqrt(forceEnergy / legacyEnergy);
        for (std::size_t i = 0; i < fl.size(); ++i)
            difference += std::pow(fl[i] - ll[i] * gain, 2);
        check(difference / forceEnergy > .01,
              "Section force must change the recorded timbre rather than just output volume");
        std::cout
            << "PASS: explicit part onset, isolated summation, scoped retrigger/release and owned-part termination\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
