#pragma once
#include "Airflow.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <limits>
#include <string>
#include <vector>

namespace topbrass {
constexpr int partCount = 6;
constexpr int baseInstrumentForPart(int part) noexcept {
    constexpr std::array<int, partCount> instruments {0, 1, 2, 3, 0, 1};
    return instruments[static_cast<std::size_t>(part)];
}
constexpr bool isSaxPart(int part) noexcept { return baseInstrumentForPart(part) == 0; }
constexpr int requiredInstrumentCount = 5;
constexpr int mtgTenorInstrument = requiredInstrumentCount;
constexpr int warTubaInstrument = mtgTenorInstrument + 1;
constexpr int instrumentCount = warTubaInstrument + 1;
constexpr int maxVoices = 128;
constexpr int maxStealTails = 16;
constexpr int maxKeyVoices = 32;
constexpr int maxKeyTails = 4;
constexpr int maxTenorDetails = 8, maxTenorDetailTails = 2;
enum class TenorDetail { breath, keys };
constexpr int recordedFallArticulation = 4;
constexpr int recordedDoubleTongueArticulation = 5;
constexpr int sampleArticulationCount = 6;
enum class Articulation { sustain, shortNote, fall };
enum class SaxVoice { tenor, baritone, marcato, subtone, growl };
enum class TenorSource { original, mtg };
enum class TubaVoice { original, warStaccato, warStaccatissimo };
enum class NoteDynamics { pressure, strikeAndPressure };
struct Sample {
    int instrument = 0, articulation = 0, root = 60, layer = 1, rate = 48000;
    int loopStart = 0, loopEnd = 0, loopCrossfade = 0;
    int releaseStart = 0, releaseCrossfade = 0;
    bool preStartCrossfade = false;
    int gainOffset = 0;
    std::vector<float> audio;
    std::vector<float> rightAudio;
    std::vector<float> gainTable;
    int channels() const noexcept { return rightAudio.empty() ? 1 : 2; }
    int frames() const noexcept { return static_cast<int>(audio.size()); }
};
struct Zone {
    int sample = 0, instrument = 0, articulation = 0, root = 60;
    int lowKey = 0, highKey = 127, lowVelocity = 1, highVelocity = 127;
    int dynamicLayer = 0, roundRobin = 0;
    float pitchCents = 0, gain = 1;
};
struct KeyNoiseZone {
    int recording = 0, key = 0, root = 0, take = 0;
    bool opening = false;
    float gain = 1;
};
struct KeyRange { int low = 128, high = -1; };
struct Part {
    bool enabled = true;
    float gain = 0.5f, pan = 0.0f;
    int octave = 0;
    int channel = 0; // 0 listens to all MIDI channels.
    bool doubleTongue = false;
    TenorSource tenorSource = TenorSource::original;
};
struct Settings {
    std::array<Part, partCount> parts {Part{}, Part{}, Part{}, Part{}, Part{false}, Part{false}};
    Articulation articulation = Articulation::shortNote;
    SaxVoice saxVoice = SaxVoice::tenor;
    float keyNoise = 0, airflowAmount = 0, tenorDetailGain = 1;
    float gain = 0.7f, releaseMs = 65.0f, tightness = 0.0f, tone = 0.0f;
    TubaVoice tubaVoice = TubaVoice::original;
    float humanize = 0; // 0..1: up to 8 ms onset delay and +/-0.6 dB slow player drift.
};
class Bank {
public:
    bool load(const std::filesystem::path&, std::string& error);
    bool loadKeyNoises(const std::filesystem::path&, std::string& error);
    bool loadTenorDetails(const std::filesystem::path&, std::string& error);
    const Sample* tenorDetail(TenorDetail, std::uint32_t repetition) const noexcept;
    bool loadAirflow(const std::filesystem::path& path, std::string& error) { return airRecording.load(path,error); }
    const AirflowRecording& airflow() const noexcept { return airRecording; }
    const KeyNoiseZone* keyNoise(int key, bool opening, std::uint32_t repetition) const noexcept;
    const Sample& keyRecording(int index) const noexcept { return keyRecordings[static_cast<std::size_t>(index)]; }
    const std::vector<Sample>& samples() const noexcept { return recordings; }
    const std::vector<Zone>& zones() const noexcept { return mappings; }
    KeyRange keyRange(int instrument, int articulation) const noexcept { return ranges[static_cast<std::size_t>(instrument)][static_cast<std::size_t>(articulation)]; }
    bool isMapped(int instrument, int articulation) const noexcept { return mapped[static_cast<std::size_t>(instrument)][static_cast<std::size_t>(articulation)]; }
    // Selectors receive validated MIDI keys, normalized velocity and bank-domain IDs.
    const Sample* selectSample(int instrument, int articulation, int target, float velocity, std::uint32_t repetition, const Sample* avoid = nullptr) const noexcept;
    const Zone* selectZone(int instrument, int articulation, int target, int velocity, int layer, std::uint32_t repetition, const Sample* avoid = nullptr) const noexcept;
    std::size_t bytes() const noexcept;
    std::size_t selectionBytes() const noexcept;
private:
    struct SelectionIndex;
    std::shared_ptr<const SelectionIndex> selectionIndex;
    AirflowRecording airRecording;
    std::vector<Sample> tenorRecordings;
    std::size_t tenorBreathCount = 0;
    std::vector<Sample> keyRecordings;
    std::vector<KeyNoiseZone> keyMappings;
    std::array<std::array<KeyRange, sampleArticulationCount>, instrumentCount> ranges {};
    std::vector<Sample> recordings;
    std::vector<Zone> mappings;
    std::array<std::array<bool, sampleArticulationCount>, instrumentCount> mapped {};
};
class Engine {
public:
    explicit Engine(const Bank& source);
    void prepare(double sampleRate) noexcept;
    void setSettings(const Settings&) noexcept;
    void triggerTenorDetail(TenorDetail, int channel = 1) noexcept;
    void noteOn(int channel, int note, float velocity) noexcept;
    void noteOff(int channel, int note) noexcept;
    // Source note owns the gesture; target is the arranged concert pitch.
    void noteOnPart(int part, int channel, int sourceNote, int target, float velocity,
                    NoteDynamics = NoteDynamics::pressure) noexcept;
    // Forced gates use a 5 ms fade instead of recorded endings or the sustain pedal.
    void noteOffPart(int part, int channel, int sourceNote, bool forceRelease = false) noexcept;
    void endPart(int part, int channel = 0) noexcept;
    void controller(int channel, int cc, int value) noexcept;
    void pitchWheel(int channel, int value) noexcept;
    void allNotesOff(int channel = 0, bool immediate = false) noexcept;
    void render(float* left, float* right, int frames) noexcept;
    int activeVoices() const noexcept;
    KeyRange partKeyRange(int part) const noexcept;
private:
    struct Voice {
        const Sample* sample = nullptr;
        const Zone* zone = nullptr;
        NoteDynamics dynamics = NoteDynamics::pressure;
        double position = 0, increment = 1;
        double releasePosition = -1;
        float releaseBlend = 0, noteVelocity = 1;
        float envelope = 0, velocity = 1, panL = 0.707f, panR = 0.707f;
        float gain = 0, releaseStep = 0, tone = 0, fallSemitones = 0;
        std::array<float, 2> toneLow {};
        int channel = 1, note = 60, part = 0, targetNote = 60;
        float vibratoPhase = 0, attackStep = 0;
        // Human timing waits without advancing the source or its articulation envelope.
        std::uint64_t onsetDelaySamples = 0;
        std::uint64_t delaySamples = 0;
        std::uint64_t renderedSamples = 0;
        bool airflowEligible = false, humanOnsetPending = false;
        bool released = false, keyDown = false, oneShot = false, syntheticFall = false, hasLooped = false;
        std::uint64_t age = 0;
    };
    struct KeyGesture {
        int target = -1;
        float velocity = 0;
        std::uint32_t repetition = 0;
        std::uint64_t token = 0, onsetDelaySamples = 0;
        bool started = false;
    };
    struct DetailVoice {
        const Sample* sample = nullptr;
        double position = 0, increment = 1;
        float baseGain = 0, gain = 0, fade = 1, fadeStep = 0, panL = 0, panR = 0;
        int channel = 1, note = 0;
        std::uint64_t token = 0, age = 0, onsetDelaySamples = 0;
        bool opening = false;
    };
    struct Channel {
        float expression = 1, volume = 1, bend = 1, vibrato = 0;
        bool pedal = false, hasExpression = false;
    };
    std::array<std::array<KeyGesture, 128>, 16> keyGestures {};
    std::array<DetailVoice, maxKeyVoices> keyVoices {};
    std::array<DetailVoice, maxKeyTails> keyTails {};
    std::array<DetailVoice, maxTenorDetails> tenorDetails {};
    std::array<DetailVoice, maxTenorDetailTails> tenorDetailTails {};
    std::array<std::uint32_t, 2> tenorRepetitions {};
    std::uint64_t tenorSerial = 0;
    std::size_t nextTenorTail = 0;
    void clearTenorDetails(int channel, bool immediate) noexcept;
    std::uint64_t keySerial = 0;
    std::size_t nextKeyTail = 0;
    void startKey(const KeyNoiseZone&, int channel, int note, const KeyGesture&) noexcept;
    void clearKeys(int channel, bool immediate) noexcept;
    void cancelPendingClose(int channel, int note) noexcept;
    void renderDetails(float* left, float* right, int frames, float smooth) noexcept;
    void startPart(int part, int channel, int note, int target, float velocity,
                   NoteDynamics = NoteDynamics::pressure) noexcept;
    int instrumentForPart(int part) const noexcept;
    int articulationForPart(int part) const noexcept;
    void renderChunk(float* left, float* right, int frames) noexcept;
    const Bank& bank;
    AirflowPlayer airPlayer;
    Settings settings;
    std::array<Voice, maxVoices> voices {};
    std::array<Channel, 16> channels {};
    std::array<Voice, maxStealTails> stealTails {};
    std::size_t nextTail = 0;
    std::array<std::array<std::uint32_t, 128>, partCount> repetitions {};
    double rate = 48000;
    float masterGain = 0.7f;
    std::array<float, 1025> vibratoWave {};
    std::uint64_t serial = 0;
    struct PlayerHumanization {
        std::uint64_t gestureSample = std::numeric_limits<std::uint64_t>::max();
        float onsetOffset = 0;
        std::uint32_t gestures = 0, driftNode = 0;
        double phase = 0, phaseStep = 0;
        float from = 0, to = 0;
    };
    std::array<PlayerHumanization, partCount> humanPlayers {};
    std::array<std::array<float, 128>, partCount> humanGains {};
    std::uint64_t humanSamples = 0;
    float humanAmount = 0;
    std::uint64_t onsetDelayForPart(int part) noexcept;
    void renderHumanization(int frames, float smooth) noexcept;
    Voice& allocate() noexcept;
    void release(Voice&) noexcept;
    void release(Voice&, float milliseconds) noexcept;
    void releaseNote(Voice&) noexcept;
    void start(const Sample&, int part, int channel, int note, int target, float velocity,
               float delaySeconds = 0, float attackSeconds = 0.0f, const Zone* zone = nullptr,
               NoteDynamics = NoteDynamics::pressure, std::uint64_t onsetDelay = 0) noexcept;
};
}
