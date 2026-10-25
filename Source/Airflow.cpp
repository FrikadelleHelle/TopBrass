#include "Airflow.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace topbrass {
namespace {
std::uint32_t word(std::istream& in) {
    unsigned char b[4]{};
    in.read(reinterpret_cast<char*>(b), 4);
    return std::uint32_t(b[0]) | std::uint32_t(b[1]) << 8 | std::uint32_t(b[2]) << 16 | std::uint32_t(b[3]) << 24;
}
float scalar(std::istream& in) {
    const auto bits = word(in);
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}
float read(const AirflowRecording& source, double position, bool looped) noexcept {
    const int index = static_cast<int>(position);
    const auto tap = [&](int frame) {
        if (frame >= source.loopEnd)
            frame = source.loopStart + frame - source.loopEnd;
        if (looped && frame < source.loopStart)
            frame = source.loopEnd - (source.loopStart - frame);
        return source.audio[static_cast<std::size_t>(std::clamp(frame, 0, static_cast<int>(source.audio.size()) - 1))];
    };
    const float t = static_cast<float>(position - index), a = tap(index - 1), b = tap(index), c = tap(index + 1),
                d = tap(index + 2);
    return b + .5f * t * (c - a + t * (2 * a - 5 * b + 4 * c - d + t * (3 * (b - c) + d - a)));
}
}
bool AirflowRecording::load(const std::filesystem::path& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    char magic[8]{};
    in.read(magic, 8);
    AirflowRecording next;
    next.rate = static_cast<int>(word(in));
    const auto frames = word(in);
    next.loopStart = static_cast<int>(word(in));
    next.loopEnd = static_cast<int>(word(in));
    next.lowKey = static_cast<int>(word(in));
    next.highKey = static_cast<int>(word(in));
    next.root = static_cast<int>(word(in));
    next.pitchTrack = scalar(in);
    next.gain = scalar(in);
    if (!in || std::memcmp(magic, "TBAIR001", 8) != 0 || next.rate < 8000 || next.rate > 192000 || frames < 8 ||
        frames > 192000 * 30 || next.loopStart < 0 || next.loopEnd > static_cast<int>(frames) ||
        next.loopEnd <= next.loopStart || next.loopEnd - next.loopStart < next.rate / 20 || next.lowKey < 0 ||
        next.highKey > 127 || next.lowKey > next.highKey || next.root < 0 || next.root > 127 ||
        !std::isfinite(next.pitchTrack) || next.pitchTrack < 0 || next.pitchTrack > 1 || !std::isfinite(next.gain) ||
        next.gain < 0 || next.gain > 1) {
        error = "Invalid or missing airflow recording: " + path.string();
        return false;
    }
    next.audio.resize(frames);
    for (auto& x : next.audio)
        x = scalar(in);
    if (!in ||
        !std::all_of(next.audio.begin(), next.audio.end(),
                     [](float x) { return std::isfinite(x) && std::abs(x) <= 1; }) ||
        in.peek() != std::char_traits<char>::eof()) {
        error = "Invalid airflow PCM";
        return false;
    }
    *this = std::move(next);
    error.clear();
    return true;
}
void AirflowPlayer::prepare(double sampleRate) noexcept {
    rate = sampleRate > 0 ? sampleRate : 48000;
    streams = {};
    smooth = 1.0f - std::exp(-1.0f / static_cast<float>(rate * .008));
    attack = 1.0f / static_cast<float>(rate * .005);
    release = 1.0f / static_cast<float>(rate * .020);
    for (std::size_t key = 0; key < increments.size(); ++key)
        increments[key] =
            source.rate / rate * std::exp2((static_cast<double>(key) - source.root) * source.pitchTrack / 12);
}
void AirflowPlayer::stop(int channel) noexcept {
    if (channel == 0)
        streams = {};
    else if (channel >= 1 && channel <= 16)
        streams[static_cast<std::size_t>(channel - 1)] = {};
}
void AirflowPlayer::render(int channel, std::span<const AirflowFrame> frames, const AirflowControls& controls,
                           float* left, float* right) noexcept {
    if (source.audio.empty() || channel < 1 || channel > 16)
        return;
    auto& stream = streams[static_cast<std::size_t>(channel - 1)];
    const float pan = std::clamp(controls.pan, -1.0f, 1.0f);
    const float panL = std::cos((pan + 1) * 3.14159265358979323846f / 4),
                panR = std::sin((pan + 1) * 3.14159265358979323846f / 4);
    const float gain = source.gain * controls.amount * controls.expression * controls.volume * controls.partGain;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const auto& input = frames[i];
        const bool eligible = controls.amount > 0 && input.target >= source.lowKey && input.target <= source.highKey;
        const float gate = eligible ? std::clamp(input.gate, 0.0f, 1.0f) : 0;
        if (!stream.active) {
            if (gate == 0)
                continue;
            stream = {};
            stream.active = true;
            stream.increment = increments[static_cast<std::size_t>(input.target)];
            stream.gain = gain;
            stream.panL = panL;
            stream.panR = panR;
        }
        stream.gate += std::clamp(gate - stream.gate, -release, attack);
        if (stream.gate <= 0) {
            stream = {};
            continue;
        }
        stream.gain += smooth * (gain - stream.gain);
        stream.panL += smooth * (panL - stream.panL);
        stream.panR += smooth * (panR - stream.panR);
        if (gate > 0)
            stream.increment += smooth * (increments[static_cast<std::size_t>(input.target)] - stream.increment);
        const float sample = read(source, stream.position, stream.looped) * stream.gain * stream.gate;
        left[i] += sample * stream.panL;
        right[i] += sample * stream.panR;
        stream.position += stream.increment;
        if (stream.position >= source.loopEnd) {
            stream.position =
                source.loopStart + std::fmod(stream.position - source.loopEnd, source.loopEnd - source.loopStart);
            stream.looped = true;
        }
    }
}
int AirflowPlayer::activeVoices() const noexcept {
    return static_cast<int>(
        std::count_if(streams.begin(), streams.end(), [](const Stream& stream) { return stream.active; }));
}
}
