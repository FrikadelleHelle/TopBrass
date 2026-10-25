#include "Engine.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

int main(int argc, char** argv) {
    try {
        if (argc != 6) throw std::runtime_error("Usage: SustainReleaseAudit one-sample.bank hold-seconds release-ms output.f32 block-size");
        topbrass::Bank bank;
        std::string error;
        if (!bank.load(argv[1], error)) throw std::runtime_error(error);
        if (bank.samples().size() != 12 || bank.samples().front().articulation != 0)
            throw std::runtime_error("Expected one sustained recording and eleven silent coverage records");
        for (std::size_t i = 1; i < bank.samples().size(); ++i)
            if (!std::all_of(bank.samples()[i].audio.begin(), bank.samples()[i].audio.end(), [](float value) { return value == 0; }))
                throw std::runtime_error("Coverage records must be silent");
        const auto& sample = bank.samples().front();
        if (sample.instrument != 0 && sample.instrument != 3)
            throw std::runtime_error("Expected tenor or tuba");
        const int rate = sample.rate;
        const int hold = static_cast<int>(std::stod(argv[2]) * rate);
        const float releaseMs = std::stof(argv[3]);
        const int block = std::stoi(argv[5]);
        if (hold <= 0 || hold > rate * 30 || releaseMs < 5 || releaseMs > 2000 || block <= 0 || block > 4096)
            throw std::runtime_error("Invalid capture bounds");
        const int begin = std::max(0, hold - rate / 4);
        const int tail = sample.releaseStart > 0 ? sample.frames() - sample.releaseStart : 0;
        const int total = hold + std::max(rate, tail + rate / 4);
        std::vector<float> left(static_cast<std::size_t>(total)), right(left.size());
        topbrass::Settings settings;
        for (auto& part : settings.parts) part.enabled = false;
        settings.parts[static_cast<std::size_t>(sample.instrument)] = {true, .125f, 0, 0, 0};
        settings.articulation = topbrass::Articulation::sustain;
        settings.gain = 1; settings.tone = 0; settings.tightness = 0; settings.releaseMs = releaseMs;
        topbrass::Engine engine(bank);
        engine.setSettings(settings); engine.prepare(rate);
        engine.noteOn(1, sample.root, 1);
        for (int offset = 0; offset < total;) {
            if (offset == hold) engine.noteOff(1, sample.root);
            int count = std::min(block, total - offset);
            if (offset < hold) count = std::min(count, hold - offset);
            engine.render(left.data() + offset, right.data() + offset, count);
            offset += count;
        }
        if (engine.activeVoices() != 0) throw std::runtime_error("Release did not terminate");
        std::ofstream output(argv[4], std::ios::binary);
        for (int frame = begin; frame < total; ++frame) {
            const std::array<float, 2> pair{left[static_cast<std::size_t>(frame)], right[static_cast<std::size_t>(frame)]};
            output.write(reinterpret_cast<const char*>(pair.data()), sizeof(pair));
        }
        if (!output) throw std::runtime_error("Capture write failed");
        std::cout << "{\"rate\":" << rate << ",\"note_off_frame\":" << hold - begin
                  << ",\"begin_source_time_frame\":" << begin << ",\"frames\":" << total - begin << "}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
