#include "Engine.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <tuple>

namespace {
void writeU32(std::ostream& out, std::uint32_t x) {
    const char bytes[4] {static_cast<char>(x), static_cast<char>(x >> 8), static_cast<char>(x >> 16), static_cast<char>(x >> 24)};
    out.write(bytes, 4);
}
void writeWave(const std::filesystem::path& path, const std::vector<float>& mono, int rate) {
    std::ofstream out(path, std::ios::binary);
    out.write("RIFF", 4); writeU32(out, static_cast<std::uint32_t>(36 + mono.size() * 2));
    out.write("WAVEfmt ", 8); writeU32(out, 16);
    const char format[4] {1, 0, 1, 0}; out.write(format, 4);
    writeU32(out, static_cast<std::uint32_t>(rate)); writeU32(out, static_cast<std::uint32_t>(rate * 2));
    const char align[4] {2, 0, 16, 0}; out.write(align, 4);
    out.write("data", 4); writeU32(out, static_cast<std::uint32_t>(mono.size() * 2));
    for (auto sample : mono) {
        const auto value = static_cast<std::int16_t>(std::clamp(sample, -1.0f, 1.0f) * 32767);
        const char bytes[2] {static_cast<char>(value), static_cast<char>(value >> 8)}; out.write(bytes, 2);
    }
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 4) throw std::runtime_error("Usage: source-probe bank output-directory variant-name");
        const std::filesystem::path output(argv[2]); std::filesystem::create_directories(output);
        topbrass::Bank bank; std::string error;
        if (!bank.load(argv[1], error)) throw std::runtime_error(error);
        for (const auto& [instrument, root, label] : {
                std::tuple {4, 43, "baritone"}, std::tuple {1, 60, "trumpet"}}) {
            const topbrass::Sample* recording = nullptr;
            for (const auto& sample : bank.samples())
                if (sample.instrument == instrument && sample.articulation == 1 && sample.root == root
                    && (!recording || sample.layer > recording->layer)) recording = &sample;
            if (!recording) throw std::runtime_error("Exact-pitch diagnostic recording is missing");
            writeWave(output / (std::string(label) + "-packed.wav"), recording->audio, recording->rate);
            for (const bool gated : {false, true}) {
                topbrass::Settings settings;
                for (auto& part : settings.parts) part.enabled = false;
                const auto part = static_cast<std::size_t>(instrument == 4 ? 0 : instrument);
                settings.parts[part].enabled = true; settings.parts[part].gain = 1; settings.parts[part].pan = 0;
                settings.saxVoice = instrument == 4 ? topbrass::SaxVoice::baritone : topbrass::SaxVoice::tenor;
                settings.gain = 1; settings.tightness = 0; settings.tone = 0; settings.releaseMs = 45;
                settings.articulation = topbrass::Articulation::shortNote;
                topbrass::Engine engine(bank); engine.setSettings(settings); engine.prepare(recording->rate);
                engine.noteOn(1, root, 1);
                const int frames = recording->rate * 2;
                const int off = static_cast<int>(recording->rate * (60.0 / 112 / 3));
                std::vector<float> left(static_cast<std::size_t>(frames)), right(static_cast<std::size_t>(frames));
                int position = 0;
                while (position < frames) {
                    if (gated && position == off) engine.noteOff(1, root);
                    int count = std::min(128, frames - position);
                    if (gated && position < off) count = std::min(count, off - position);
                    engine.render(left.data() + position, right.data() + position, count); position += count;
                }
                for (int i = 0; i < frames; ++i) left[static_cast<std::size_t>(i)] = (left[static_cast<std::size_t>(i)] + right[static_cast<std::size_t>(i)]) * 0.5f;
                writeWave(output / (std::string(label) + "-" + argv[3] + (gated ? "-gated.wav" : "-held.wav")), left, recording->rate);
            }
            std::cout << label << ": root=" << root << " layer=" << recording->layer << " rate=" << recording->rate << '\n';
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
