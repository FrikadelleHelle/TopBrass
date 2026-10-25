#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace topbrass {
struct AirflowRecording {
    int rate = 44100, loopStart = 0, loopEnd = 0, lowKey = 36, highKey = 68, root = 50;
    float pitchTrack = .1f, gain = .4f;
    std::vector<float> audio;
    bool load(const std::filesystem::path&, std::string& error);
};
struct AirflowFrame {
    float gate;
    int target;
    std::uint64_t age;
};
struct AirflowControls {
    float amount = 0, expression = 1, volume = 1, partGain = .5f, pan = 0;
};
class AirflowPlayer {
  public:
    explicit AirflowPlayer(const AirflowRecording& recording) : source(recording) {}
    void prepare(double sampleRate) noexcept;
    void stop(int channel = 0) noexcept;
    void render(int channel, std::span<const AirflowFrame>, const AirflowControls&, float* left, float* right) noexcept;
    int activeVoices() const noexcept;

  private:
    struct Stream {
        double position = 0, increment = 1;
        float gate = 0, gain = 0, panL = 0, panR = 0;
        bool active = false, looped = false;
    };
    const AirflowRecording& source;
    std::array<Stream, 16> streams{};
    std::array<double, 128> increments{};
    double rate = 48000;
    float smooth = 0, attack = 0, release = 0;
};
}
