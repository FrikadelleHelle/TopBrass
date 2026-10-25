#pragma once
#include "Engine.h"
#include <array>
#include <cstdint>
#include <limits>
#include <optional>

namespace topbrass {
constexpr int sectionKeyCount = 15;
enum class PlayingMode { layered, relay, groove };
struct SectionConfig {
    PlayingMode mode = PlayingMode::layered;
    int lowestKey = 60;
    unsigned enabled = 15;
    float feel = .4f;
    bool operator==(const SectionConfig&) const = default;
};
struct SectionEvent {
    bool on = false;
    int part = 0, channel = 1, note = 60, offset = 0;
    float velocity = 0;
};
class SectionPlayer {
  public:
    using Sink = void (*)(void*, const SectionEvent&);
    void prepare(double sampleRate) noexcept;
    bool configure(SectionConfig) noexcept;
    bool clock(double bpm, bool hostRunning, std::optional<double> ppq) noexcept;
    void noteOn(int channel, int note, float velocity) noexcept;
    void noteOff(int channel, int note) noexcept;
    void controller(int channel, int cc, int value) noexcept;
    void reset() noexcept;
    void advance(int frames, void* context, Sink) noexcept;
    int heldNotes() const noexcept { return heldCount; }

  private:
    static constexpr std::int64_t never = std::numeric_limits<std::int64_t>::max();
    struct Key {
        bool physical = false, held = false;
        float velocity = 0;
    };
    struct Note {
        int channel = 1, note = 60;
        float velocity = 0;
    };
    struct Part {
        std::optional<Note> active, pending;
        std::int64_t onDue = never, offDue = never;
        int duration = 0;
    };
    SectionConfig config;
    std::array<std::array<Key, 128>, 16> keys{};
    std::array<bool, 16> pedal{};
    std::array<Part, partCount> parts{};
    double rate = 48000, tempo = 120, beat = 0, nextBeat = 0;
    std::int64_t sample = 0;
    std::int64_t lastHit = never;
    std::uint32_t phrase = 0, sequence = 0;
    int heldCount = 0, relay = 0, nextStep = 0;
    bool clockKnown = false, wasRunning = false, rhythm = false;
    std::array<int, partCount> enabledParts(int& count) const noexcept;
    void releaseKey(int channel, int note) noexcept;
    void plan(int part, Note note, float accent, int delay, int duration) noexcept;
    void hit(float accent, float length, int stepIndex) noexcept;
    void step() noexcept;
    std::optional<Note> pitch(int role) const noexcept;
    float variation(int part, int step) const noexcept;
};
}
