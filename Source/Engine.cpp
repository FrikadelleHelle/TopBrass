#include "Engine.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

namespace topbrass {
namespace {
constexpr float pi = 3.14159265358979323846f;
float humanVariation(int part, std::uint32_t gesture, std::uint32_t salt) noexcept {
    auto value = (static_cast<std::uint32_t>(part) + 1) * 0x9e3779b9u + gesture * 0x85ebca6bu + salt;
    value ^= value >> 16; value *= 0x7feb352du; value ^= value >> 15;
    return static_cast<float>(value & 65535u) / 65535.0f;
}
float humanizeAmount(float value) noexcept { return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f; }
std::uint32_t readU32(std::istream& in) {
    unsigned char b[4] {};
    in.read(reinterpret_cast<char*>(b), 4);
    return std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8) | (std::uint32_t(b[2]) << 16) | (std::uint32_t(b[3]) << 24);
}
float readFloat(std::istream& in) {
    const auto bits = readU32(in);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
float protectOutput(float sample) noexcept {
    const float magnitude = std::abs(sample);
    constexpr float knee = 0.95f;
    if (magnitude <= knee) return sample;
    const float excess = magnitude - knee;
    return std::copysign(knee + (1.0f - knee) * excess / ((1.0f - knee) + excess), sample);
}
float interpolate(const Sample& s, const std::vector<float>& audio, double position, bool wrapEnd = false, bool wrapStart = false) noexcept {
    const auto index = static_cast<int>(position);
    const float t = static_cast<float>(position - index);
    const int last = static_cast<int>(audio.size()) - 1;
    const auto read = [&](int frame) {
        if (wrapEnd && frame >= s.loopEnd) frame = s.loopStart + (frame - s.loopEnd);
        if (wrapStart && frame < s.loopStart) frame = s.loopEnd - (s.loopStart - frame);
        return audio[static_cast<std::size_t>(std::clamp(frame, 0, last))];
    };
    const float a = read(index - 1), b = read(index), c = read(index + 1), d = read(index + 2);
    return b + 0.5f * t * (c - a + t * (2*a - 5*b + 4*c - d + t * (3*(b-c) + d-a)));
}
float envelopeGain(const Sample& sample, double position) noexcept {
    if (sample.gainTable.empty()) return 1;
    const auto index = static_cast<std::size_t>(std::max(0L, std::lround((position+sample.gainOffset)/32)));
    return sample.gainTable[std::min(index, sample.gainTable.size()-1)];
}
}
struct Bank::SelectionIndex {
    struct SampleGroup {
        int instrument, articulation, root;
        int layerCount=0;
        std::array<bool,9> layers{};
        std::array<std::vector<std::uint16_t>,9> takes{};
    };
    std::array<std::int16_t,instrumentCount*sampleArticulationCount*128> roots;
    std::vector<SampleGroup> groups;
    std::vector<std::vector<std::uint16_t>> zoneBuckets;
    static std::size_t slot(int instrument,int art,int key) noexcept {
        return static_cast<std::size_t>((instrument*sampleArticulationCount+art)*128+key);
    }
    SelectionIndex(const std::vector<Sample>& samples,const std::vector<Zone>& zones)
        :zoneBuckets(instrumentCount*sampleArticulationCount*128*4) {
        roots.fill(-1);
        auto groupIds=roots;
        for (std::size_t index=0;index<samples.size();++index) {
            const auto& sample=samples[index];
            auto& id=groupIds[slot(sample.instrument,sample.articulation,sample.root)];
            if (id<0) {
                id=static_cast<std::int16_t>(groups.size());
                groups.push_back({sample.instrument,sample.articulation,sample.root});
            }
            auto& group=groups[static_cast<std::size_t>(id)];
            const auto layer=static_cast<std::size_t>(sample.layer);
            if (!group.layers[layer]) {group.layers[layer]=true;++group.layerCount;}
            if (group.takes[layer].size()<16) group.takes[layer].push_back(static_cast<std::uint16_t>(index));
        }
        for (int instrument=0;instrument<instrumentCount;++instrument)
            for (int art=0;art<sampleArticulationCount;++art) {
                int low=128,high=-1;
                for (const auto& group:groups) if (group.instrument==instrument && group.articulation==art) {
                    low=std::min(low,group.root);high=std::max(high,group.root);
                }
                for (int key=0;key<128;++key) {
                    int distance=128;std::int16_t chosen=-1;
                    for (std::size_t index=0;index<groups.size();++index) {
                        const auto& group=groups[index];
                        if (group.instrument==instrument && group.articulation==art && std::abs(group.root-key)<distance) {
                            distance=std::abs(group.root-key);chosen=static_cast<std::int16_t>(index);
                        }
                    }
                    if (distance<=7 && (art<2 || (key>=low && key<=high))) roots[slot(instrument,art,key)]=chosen;
                }
            }
        for (std::size_t index=0;index<zones.size();++index) {
            const auto& zone=zones[index];
            for (int key=zone.lowKey;key<=zone.highKey;++key)
                zoneBuckets[slot(zone.instrument,zone.articulation,key)*4+static_cast<std::size_t>(zone.dynamicLayer)].push_back(static_cast<std::uint16_t>(index));
        }
    }
    std::size_t bytes() const noexcept {
        std::size_t bytes=sizeof(*this)+groups.capacity()*sizeof(SampleGroup)+zoneBuckets.capacity()*sizeof(zoneBuckets.front());
        for (const auto& group:groups) for (const auto& takes:group.takes) bytes+=takes.capacity()*sizeof(std::uint16_t);
        for (const auto& bucket:zoneBuckets) bytes+=bucket.capacity()*sizeof(std::uint16_t);
        return bytes;
    }
};
bool Bank::load(const std::filesystem::path& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    char magic[8] {};
    in.read(magic, 8);
    const bool stereoFormat = std::memcmp(magic, "TBBANK04", 8) == 0;
    const bool mappedFormat = stereoFormat || std::memcmp(magic, "TBBANK03", 8) == 0;
    if (!in || (!mappedFormat && std::memcmp(magic, "TBBANK02", 8) != 0)) {
        error = "Sample bank is missing or has an invalid header: " + path.string();
        return false;
    }
    const auto count = readU32(in);
    const auto zoneCount = mappedFormat ? readU32(in) : 0;
    if (count == 0 || count > 4096) { error = "Invalid sample count"; return false; }
    if (zoneCount > 16384) { error = "Invalid zone count"; return false; }
    std::vector<Sample> loaded;
    loaded.reserve(count);
    std::uint64_t totalFrames = 0;
    std::array<std::array<KeyRange, sampleArticulationCount>, instrumentCount> loadedRanges {};
    std::array<std::array<bool, sampleArticulationCount>, instrumentCount> coverage {};
    for (std::uint32_t i = 0; i < count; ++i) {
        Sample s;
        s.instrument = static_cast<int>(readU32(in));
        s.articulation = static_cast<int>(readU32(in));
        s.root = static_cast<int>(readU32(in));
        s.layer = static_cast<int>(readU32(in));
        s.rate = static_cast<int>(readU32(in));
        const auto length = readU32(in);
        s.loopStart = static_cast<int>(readU32(in));
        s.loopEnd = static_cast<int>(readU32(in));
        s.loopCrossfade = static_cast<int>(readU32(in));
        std::uint32_t gainCount = 0;
        if (mappedFormat) {
            s.releaseStart = static_cast<int>(readU32(in));
            s.releaseCrossfade = static_cast<int>(readU32(in));
            const auto convention = readU32(in);
            if (convention > 1) { error = "Invalid loop convention"; return false; }
            s.preStartCrossfade = convention == 1;
            s.gainOffset = static_cast<int>(readU32(in));
            gainCount = readU32(in);
        }
        const auto channels = stereoFormat ? readU32(in) : 1;
        totalFrames += static_cast<std::uint64_t>(length) * channels + gainCount;
        const bool nativeGesture = mappedFormat && (s.instrument == 1 || s.instrument == 2)
            && (s.articulation == recordedFallArticulation || s.articulation == recordedDoubleTongueArticulation);
        if (!in || s.instrument < 0 || s.instrument >= instrumentCount || s.articulation < 0 || s.articulation >= sampleArticulationCount
            || (s.articulation > 1 && s.instrument != 4 && !nativeGesture
                && !(s.instrument == warTubaInstrument && s.articulation == 2))
            || (s.instrument == 4 && s.articulation >= recordedFallArticulation)
            || (s.instrument == mtgTenorInstrument && (s.articulation != 0 || s.loopEnd != 0))
            || (s.instrument == warTubaInstrument && (s.articulation > 2 || (s.articulation != 0 && s.loopEnd != 0)))
            || s.root < 0 || s.root > 127 || s.layer < 1 || s.layer > 8
            || s.rate < 8000 || s.rate > 192000 || length < 8 || length > 192000 * 60
            || (channels != 1 && channels != 2)
            || totalFrames > 256000000 || s.loopStart < 0 || s.loopEnd < s.loopStart
            || s.loopEnd > static_cast<int>(length) || s.loopCrossfade < 0
            || s.loopCrossfade > (s.loopEnd - s.loopStart) / 2
            || (s.preStartCrossfade && s.loopCrossfade > s.loopStart)
            || s.releaseStart < 0 || s.releaseStart >= static_cast<int>(length)
            || (s.releaseStart > 0 && s.releaseStart < s.loopEnd)
            || s.releaseCrossfade < 0 || s.releaseCrossfade > static_cast<int>(length)-s.releaseStart
            || s.gainOffset < 0 || s.gainOffset > 192000*60 || gainCount > 400000
            || (s.loopEnd != 0 && s.loopEnd - s.loopStart < s.rate / 5)) {
            error = "Invalid sample metadata"; return false;
        }
        s.audio.resize(length);
        in.read(reinterpret_cast<char*>(s.audio.data()), static_cast<std::streamsize>(length * sizeof(float)));
        if (!in || !std::all_of(s.audio.begin(), s.audio.end(), [](float x) { return std::isfinite(x) && std::abs(x) <= 1.0f; })) {
            error = "Truncated or invalid audio in sample bank"; return false;
        }
        if (channels == 2) {
            s.rightAudio.resize(length);
            in.read(reinterpret_cast<char*>(s.rightAudio.data()), static_cast<std::streamsize>(length * sizeof(float)));
            if (!in || !std::all_of(s.rightAudio.begin(), s.rightAudio.end(), [](float x) { return std::isfinite(x) && std::abs(x) <= 1; })) {
                error = "Truncated or invalid right-channel audio"; return false;
            }
        }
        s.gainTable.resize(gainCount);
        for (auto& value : s.gainTable) value = readFloat(in);
        if (!in || !std::all_of(s.gainTable.begin(), s.gainTable.end(), [](float x) { return std::isfinite(x) && x >= 0 && x <= 8; })) {
            error = "Invalid recording gain envelope"; return false;
        }
        coverage[static_cast<std::size_t>(s.instrument)][static_cast<std::size_t>(s.articulation)] = true;
        auto& range = loadedRanges[static_cast<std::size_t>(s.instrument)][static_cast<std::size_t>(s.articulation)];
        range.low = std::min(range.low,s.root); range.high = std::max(range.high,s.root);
        loaded.push_back(std::move(s));
    }
    for (int instrument = 0; instrument < requiredInstrumentCount; ++instrument)
        if (!coverage[static_cast<std::size_t>(instrument)][0] || !coverage[static_cast<std::size_t>(instrument)][1]) {
            error = "Sample bank lacks a required instrument/articulation"; return false;
        }
    if (!coverage[4][2] || !coverage[4][3]) { error = "Baritone expressive recordings are missing"; return false; }
    std::vector<Zone> zones;
    zones.reserve(zoneCount);
    std::array<std::array<KeyRange, sampleArticulationCount>, instrumentCount> zoneRanges {};
    std::array<std::array<bool, sampleArticulationCount>, instrumentCount> zoneCoverage {};
    for (std::uint32_t index = 0; index < zoneCount; ++index) {
        Zone z;
        z.sample = static_cast<int>(readU32(in)); z.instrument = static_cast<int>(readU32(in));
        z.articulation = static_cast<int>(readU32(in)); z.root = static_cast<int>(readU32(in));
        z.lowKey = static_cast<int>(readU32(in)); z.highKey = static_cast<int>(readU32(in));
        z.lowVelocity = static_cast<int>(readU32(in)); z.highVelocity = static_cast<int>(readU32(in));
        z.dynamicLayer = static_cast<int>(readU32(in)); z.roundRobin = static_cast<int>(readU32(in));
        z.pitchCents = readFloat(in); z.gain = readFloat(in);
        if (!in || z.sample < 0 || z.sample >= static_cast<int>(loaded.size())
            || z.instrument < 0 || z.instrument >= instrumentCount || z.articulation < 0 || z.articulation >= sampleArticulationCount
            || z.root < 0 || z.root > 127 || z.lowKey < 0 || z.lowKey > z.highKey || z.highKey > 127
            || z.lowVelocity < 0 || z.lowVelocity > z.highVelocity || z.highVelocity > 127
            || z.dynamicLayer < 0 || z.dynamicLayer > 3 || z.roundRobin < 0 || z.roundRobin > 15
            || !std::isfinite(z.pitchCents) || std::abs(z.pitchCents) > 1200
            || !std::isfinite(z.gain) || z.gain < 0 || z.gain > 16
            || loaded[static_cast<std::size_t>(z.sample)].instrument != z.instrument
            || loaded[static_cast<std::size_t>(z.sample)].articulation != z.articulation) {
            error = "Invalid sample zone"; return false;
        }
        zoneCoverage[static_cast<std::size_t>(z.instrument)][static_cast<std::size_t>(z.articulation)] = true;
        auto& range = zoneRanges[static_cast<std::size_t>(z.instrument)][static_cast<std::size_t>(z.articulation)];
        range.low = std::min(range.low,z.lowKey); range.high = std::max(range.high,z.highKey);
        zones.push_back(z);
    }
    for (std::size_t i = 0; i < loadedRanges.size(); ++i)
        for (std::size_t a = 0; a < loadedRanges[i].size(); ++a) if (zoneCoverage[i][a]) loadedRanges[i][a] = zoneRanges[i][a];
    if (coverage[mtgTenorInstrument][0] && !zoneCoverage[mtgTenorInstrument][0]) {
        error = "Alternate tenor mappings are missing"; return false;
    }
    if (std::any_of(coverage[warTubaInstrument].begin(),coverage[warTubaInstrument].end(),[](bool present){return present;}))
        for(int articulation=0;articulation<3;++articulation)
            if(!coverage[warTubaInstrument][static_cast<std::size_t>(articulation)] || !zoneCoverage[warTubaInstrument][static_cast<std::size_t>(articulation)]) {
                error="War Tuba recordings or mappings are incomplete";return false;
            }
    auto nextSelection=std::make_shared<SelectionIndex>(loaded,zones);
    ranges = loadedRanges;
    recordings = std::move(loaded);
    mappings = std::move(zones);
    mapped = zoneCoverage;
    selectionIndex = std::move(nextSelection);
    error.clear();
    return true;
}
bool Bank::loadKeyNoises(const std::filesystem::path& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    char magic[8] {}; in.read(magic, 8);
    const auto count = readU32(in), zoneCount = readU32(in);
    if (!in || std::memcmp(magic, "TBKEYS01", 8) != 0 || count == 0 || count > 512 || zoneCount == 0 || zoneCount > 2048) {
        error = "Invalid or missing key-noise bank: " + path.string(); return false;
    }
    std::vector<Sample> sounds; sounds.reserve(count);
    std::uint64_t total = 0;
    for (std::uint32_t index = 0; index < count; ++index) {
        Sample sample; sample.rate = static_cast<int>(readU32(in));
        const auto frames = readU32(in); total += frames;
        if (!in || sample.rate < 8000 || sample.rate > 192000 || frames < 8 || frames > 192000*4 || total > 16000000) {
            error = "Invalid mechanical recording metadata"; return false;
        }
        sample.audio.resize(frames);
        for (auto& value : sample.audio) value = readFloat(in);
        if (!in || !std::all_of(sample.audio.begin(), sample.audio.end(), [](float x) { return std::isfinite(x) && std::abs(x) <= 1; })) {
            error = "Invalid mechanical recording PCM"; return false;
        }
        sounds.push_back(std::move(sample));
    }
    std::vector<KeyNoiseZone> zones; zones.reserve(zoneCount);
    std::array<std::array<std::array<bool, 4>, 2>, 128> coverage {};
    for (std::uint32_t index = 0; index < zoneCount; ++index) {
        KeyNoiseZone zone;
        zone.recording = static_cast<int>(readU32(in)); zone.key = static_cast<int>(readU32(in));
        zone.root = static_cast<int>(readU32(in)); const auto opening = readU32(in);
        zone.take = static_cast<int>(readU32(in)); zone.gain = readFloat(in);
        if (!in || zone.recording < 0 || zone.recording >= static_cast<int>(sounds.size()) || zone.key < 0 || zone.key > 127
            || zone.root < 0 || zone.root > 127 || opening > 1 || zone.take < 0 || zone.take > 3
            || !std::isfinite(zone.gain) || zone.gain < 0 || zone.gain > 1) {
            error = "Invalid mechanical mapping"; return false;
        }
        zone.opening = opening == 1;
        auto& present = coverage[static_cast<std::size_t>(zone.key)][opening][static_cast<std::size_t>(zone.take)];
        if (present) { error = "Duplicate mechanical mapping"; return false; }
        present = true; zones.push_back(zone);
    }
    for (const auto& key : coverage) {
        int present = 0; for (const auto& trigger : key) for (bool take : trigger) present += take;
        if (present != 0 && present != 8) { error = "Incomplete mechanical alternatives"; return false; }
    }
    if (in.peek() != std::char_traits<char>::eof()) { error = "Unexpected mechanical bank data"; return false; }
    keyRecordings = std::move(sounds); keyMappings = std::move(zones); error.clear(); return true;
}
const KeyNoiseZone* Bank::keyNoise(int key, bool opening, std::uint32_t repetition) const noexcept {
    for (const auto& zone : keyMappings)
        if (zone.key == key && zone.opening == opening && zone.take == static_cast<int>(repetition % 4)) return &zone;
    return nullptr;
}
bool Bank::loadTenorDetails(const std::filesystem::path& path, std::string& error) {
    std::ifstream input(path,std::ios::binary);
    char magic[8] {};input.read(magic,8);
    const auto breaths=readU32(input), keys=readU32(input);
    if (!input || std::memcmp(magic,"TBTEN001",8)!=0 || breaths==0 || keys==0 || breaths>128 || keys>128) {
        error="Invalid tenor detail header";return false;
    }
    std::vector<Sample> loaded;
    loaded.reserve(breaths+keys);
    std::size_t total=0;
    for (std::uint32_t index=0;index<breaths+keys;++index) {
        Sample sample;sample.rate=static_cast<int>(readU32(input));const auto frames=readU32(input);
        total+=frames;
        if (!input || sample.rate<8000 || sample.rate>192000 || frames<8 || frames>192000*5 || total>8000000) {
            error="Invalid tenor detail dimensions";return false;
        }
        sample.audio.resize(frames);
        input.read(reinterpret_cast<char*>(sample.audio.data()),static_cast<std::streamsize>(frames*sizeof(float)));
        if (!input || !std::all_of(sample.audio.begin(),sample.audio.end(),[](float value){return std::isfinite(value)&&std::abs(value)<=1;})) {
            error="Invalid tenor detail PCM";return false;
        }
        loaded.push_back(std::move(sample));
    }
    if (input.peek()!=std::char_traits<char>::eof()) {error="Trailing tenor detail data";return false;}
    tenorRecordings=std::move(loaded);tenorBreathCount=breaths;error.clear();return true;
}
const Sample* Bank::tenorDetail(TenorDetail kind, std::uint32_t repetition) const noexcept {
    if (tenorRecordings.empty()) return nullptr;
    const auto first=kind==TenorDetail::breath?0:tenorBreathCount;
    const auto count=kind==TenorDetail::breath?tenorBreathCount:tenorRecordings.size()-tenorBreathCount;
    return &tenorRecordings[first+repetition%count];
}
std::size_t Bank::bytes() const noexcept {
    std::size_t size = 0;
    for (const auto& s : recordings) size += (s.audio.size() + s.rightAudio.size() + s.gainTable.size()) * sizeof(float);
    for (const auto& sample : keyRecordings) size += sample.audio.size()*sizeof(float);
    size += airRecording.audio.size()*sizeof(float);
    for (const auto& sample:tenorRecordings) size+=sample.audio.size()*sizeof(float);
    return size;
}
Engine::Engine(const Bank& source) : bank(source), airPlayer(source.airflow()) {
    for (std::size_t i = 0; i < vibratoWave.size(); ++i)
        vibratoWave[i] = std::sin(2.0f * pi * static_cast<float>(i) / 1024.0f);
}
void Engine::prepare(double sampleRate) noexcept {
    rate = sampleRate > 0 ? sampleRate : 48000;
    voices = {};
    stealTails = {};
    nextTail = 0;
    serial = 0;
    humanSamples = 0;
    humanAmount = humanizeAmount(settings.humanize);
    humanPlayers = {};
    for (int part = 0; part < partCount; ++part) {
        auto& player = humanPlayers[static_cast<std::size_t>(part)];
        player.phaseStep = 1 / (rate * (2.5 + part * .31));
        player.from = humanVariation(part, 0, 0x4cf5ad43u) * 2 - 1;
        player.to = humanVariation(part, 1, 0x4cf5ad43u) * 2 - 1;
    }
    channels = {};
    repetitions = {};
    keyGestures = {}; keyVoices = {}; keyTails = {}; keySerial = 0; nextKeyTail = 0;
    tenorDetails={};tenorDetailTails={};tenorRepetitions={};tenorSerial=0;nextTenorTail=0;
    masterGain = settings.gain;
    airPlayer.prepare(rate);
}
void Engine::setSettings(const Settings& next) noexcept {
    const auto sourceChanged = [&](int part) {
        return isSaxPart(part) && settings.saxVoice == SaxVoice::tenor
            && settings.parts[static_cast<std::size_t>(part)].tenorSource != next.parts[static_cast<std::size_t>(part)].tenorSource;
    };
    if ((settings.parts[0].enabled&&!next.parts[0].enabled) || settings.saxVoice!=next.saxVoice || sourceChanged(0)) clearTenorDetails(0,false);
    if ((settings.parts[0].enabled && !next.parts[0].enabled) || settings.saxVoice != next.saxVoice
        || (settings.keyNoise > 0 && next.keyNoise <= 0)) clearKeys(0, false);
    for (auto& v : voices)
        if (v.sample && (!next.parts[static_cast<std::size_t>(v.part)].enabled
            || (isSaxPart(v.part) && settings.saxVoice != next.saxVoice) || sourceChanged(v.part)
            || (v.part == 3 && settings.tubaVoice != next.tubaVoice
                && (settings.tubaVoice == TubaVoice::original || next.tubaVoice == TubaVoice::original || v.sample->articulation != 0))))
            { v.airflowEligible = false; release(v); }
    if (!next.parts[0].enabled || settings.saxVoice != next.saxVoice)
        for (auto& v : stealTails) if (v.part == 0) v.airflowEligible = false;
    settings = next;
}
std::uint64_t Engine::onsetDelayForPart(int part) noexcept {
    auto& player = humanPlayers[static_cast<std::size_t>(part)];
    if (player.gestureSample != humanSamples) {
        player.gestureSample = humanSamples;
        player.onsetOffset = humanVariation(part, player.gestures++, 0x165667b1u);
    }
    return static_cast<std::uint64_t>(std::llround(rate * .008 * humanizeAmount(settings.humanize) * player.onsetOffset));
}
void Engine::renderHumanization(int frames, float smooth) noexcept {
    const float target = humanizeAmount(settings.humanize);
    for (int frame = 0; frame < frames; ++frame) {
        humanAmount += smooth * (target - humanAmount);
        if (target == 0 && humanAmount < 1e-7f) humanAmount = 0;
        for (int part = 0; part < partCount; ++part) {
            auto& player = humanPlayers[static_cast<std::size_t>(part)];
            float gain = 1;
            if (humanAmount > 0) {
                const float phase = static_cast<float>(player.phase);
                const float blend = phase * phase * (3 - 2 * phase);
                const float drift = player.from + blend * (player.to - player.from);
                const float exponent = .0690775528f * drift * humanAmount;
                gain += exponent * (1 + exponent * (.5f + exponent * (1.0f / 6 + exponent / 24)));
            }
            humanGains[static_cast<std::size_t>(part)][static_cast<std::size_t>(frame)] = gain;
            player.phase += player.phaseStep;
            if (player.phase >= 1) {
                player.phase -= 1;
                player.from = player.to;
                player.to = humanVariation(part, ++player.driftNode + 1, 0x4cf5ad43u) * 2 - 1;
            }
        }
    }
    humanSamples += static_cast<std::uint64_t>(frames);
}
Engine::Voice& Engine::allocate() noexcept {
    for (auto& v : voices) if (!v.sample) return v;
    // Prefer an ending voice, then the oldest held voice. Work is bounded.
    auto& victim = *std::min_element(voices.begin(), voices.end(), [](const Voice& a, const Voice& b) {
        const bool endingA = a.released || a.releasePosition >= 0, endingB = b.released || b.releasePosition >= 0;
        if (endingA != endingB) return endingA;
        if (endingA && a.envelope != b.envelope) return a.envelope < b.envelope;
        return a.age < b.age;
    });
    if (victim.renderedSamples == 0) return victim;
    auto& tail = stealTails[nextTail++ % stealTails.size()];
    tail = victim;
    tail.released = true;
    tail.keyDown = false;
    tail.releaseStep = tail.envelope / static_cast<float>(rate * 0.003);
    return victim;
}
void Engine::release(Voice& v) noexcept {
    if (v.released) return;
    release(v, settings.releaseMs);
}
void Engine::release(Voice& v, float milliseconds) noexcept {
    if (v.renderedSamples == 0) { v.sample = nullptr; v.keyDown = false; return; }
    v.released = true;
    v.keyDown = false;
    const float seconds = std::max(0.005f, milliseconds * 0.001f);
    v.releaseStep = std::max(v.releaseStep, v.envelope / static_cast<float>(rate * seconds));
}
void Engine::releaseNote(Voice& v) noexcept {
    if (v.renderedSamples == 0 || v.sample->releaseStart == 0 || v.syntheticFall || v.released) { release(v); return; }
    if (v.releasePosition >= 0) return;
    v.keyDown = false;
    v.releasePosition = v.sample->releaseStart;
    v.releaseBlend = 0;
}
void Engine::start(const Sample& sample, int part, int channel, int note, int target, float velocity, float delaySeconds, float attackSeconds, const Zone* zone, NoteDynamics dynamics, std::uint64_t onsetDelay) noexcept {
    auto& v = allocate();
    v = {};
    v.sample = &sample;
    v.zone = zone;
    v.dynamics = dynamics;
    v.part = part; v.channel = channel; v.note = note; v.targetNote = target;
    v.velocity = std::pow(velocity, 1.3f);
    v.noteVelocity = velocity;
    v.tone = settings.tone;
    v.keyDown = true;
    v.airflowEligible = part == 0 && sample.instrument == 4;
    v.onsetDelaySamples = onsetDelay;
    v.humanOnsetPending = onsetDelay > 0;
    v.delaySamples = static_cast<std::uint64_t>(rate * delaySeconds);
    v.attackStep = attackSeconds > 0 ? 1.0f / static_cast<float>(rate * attackSeconds) : 1.0f;
    v.envelope = v.delaySamples == 0 && attackSeconds <= 0 ? 1.0f : 0.0f;
    v.oneShot = sample.articulation == 1 || sample.articulation == recordedFallArticulation
        || sample.articulation == recordedDoubleTongueArticulation
        || (sample.instrument == warTubaInstrument && sample.articulation == 2);
    v.syntheticFall = settings.articulation == Articulation::fall
        && sample.articulation != recordedFallArticulation
        && (!isSaxPart(part) || settings.saxVoice == SaxVoice::tenor || settings.saxVoice == SaxVoice::baritone);
    const float root = zone ? static_cast<float>(zone->root) : static_cast<float>(sample.root);
    v.increment = static_cast<double>(sample.rate) / rate * std::exp2((target - root) / 12.0 + (zone ? zone->pitchCents / 1200.0 : 0));
    v.age = ++serial;
    v.vibratoPhase = static_cast<float>((serial * 137) % 1024);
}
std::size_t Bank::selectionBytes() const noexcept { return selectionIndex?selectionIndex->bytes():0; }
const Zone* Bank::selectZone(int instrument, int articulation, int target, int velocity, int layer, std::uint32_t repetition, const Sample* avoid) const noexcept {
    if (!selectionIndex) return nullptr;
    std::array<const Zone*, 16> candidates {};
    std::size_t count = 0;
    for (const auto index:selectionIndex->zoneBuckets[SelectionIndex::slot(instrument,articulation,target)*4+static_cast<std::size_t>(layer)]) {
        const auto& zone=mappings[index];
        if (velocity>=zone.lowVelocity && velocity<=zone.highVelocity) {
            candidates[count++]=&zone;
            if (count==candidates.size()) break;
        }
    }
    if (count == 0) return nullptr;
    std::sort(candidates.begin(), candidates.begin()+static_cast<std::ptrdiff_t>(count), [](const Zone* a, const Zone* b) { return a->roundRobin < b->roundRobin; });
    const auto first = repetition % count;
    const auto* original = candidates[first];
    if (!avoid || &recordings[static_cast<std::size_t>(original->sample)] != avoid) return original;
    for (std::size_t offset = 1; offset < count; ++offset) {
        const auto* candidate = candidates[(first+offset)%count];
        if (&recordings[static_cast<std::size_t>(candidate->sample)] != avoid) return candidate;
    }
    return original;
}
const Sample* Bank::selectSample(int instrument, int articulation, int target, float velocity, std::uint32_t repetition, const Sample* avoid) const noexcept {
    if (!selectionIndex) return nullptr;
    const auto root=selectionIndex->roots[SelectionIndex::slot(instrument,articulation,target)];
    if (root<0) return nullptr;
    const auto& group=selectionIndex->groups[static_cast<std::size_t>(root)];
    int layerIndex = std::min(group.layerCount - 1, static_cast<int>(velocity * group.layerCount));
    if (instrument == 4 && articulation == 1) {
        const int midiVelocity = static_cast<int>(std::round(velocity * 127));
        layerIndex = midiVelocity <= 40 ? 0 : midiVelocity <= 70 ? 1 : midiVelocity <= 100 ? 2 : 3;
    }
    int chosenLayer = 1;
    for (int l = 1; l <= 8; ++l) if (group.layers[static_cast<std::size_t>(l)] && layerIndex-- == 0) { chosenLayer = l; break; }
    const auto& takes=group.takes[static_cast<std::size_t>(chosenLayer)];
    if (takes.empty()) return nullptr;
    const auto first = repetition % takes.size();
    const auto* original = &recordings[takes[first]];
    if (!avoid || original != avoid) return original;
    for (std::size_t offset = 1; offset < takes.size(); ++offset) {
        const auto* candidate = &recordings[takes[(first+offset)%takes.size()]];
        if (candidate != avoid) return candidate;
    }
    return original;
}
void Engine::noteOn(int channel, int note, float velocity) noexcept {
    if (channel < 1 || channel > 16 || note < 0 || note > 127) return;
    if (velocity <= 0) { noteOff(channel,note); return; }
    velocity = std::clamp(velocity,0.0f,1.0f);
    cancelPendingClose(channel,note);
    keyGestures[static_cast<std::size_t>(channel-1)][static_cast<std::size_t>(note)] = {};
    for (auto& v : voices) if (v.sample && v.channel == channel && v.note == note) release(v);
    for (int part = 0; part < partCount; ++part) {
        const auto& strip = settings.parts[static_cast<std::size_t>(part)];
        if (strip.enabled && (strip.channel == 0 || strip.channel == channel))
            startPart(part,channel,note,note+12*strip.octave,velocity);
    }
}
void Engine::noteOnPart(int part, int channel, int sourceNote, int target, float velocity, NoteDynamics dynamics) noexcept {
    if (part < 0 || part >= partCount || channel < 1 || channel > 16 || sourceNote < 0 || sourceNote > 127) return;
    if (velocity <= 0) { noteOffPart(part,channel,sourceNote); return; }
    if (!settings.parts[static_cast<std::size_t>(part)].enabled) return;
    if (part == 0) {
        cancelPendingClose(channel,sourceNote);
        keyGestures[static_cast<std::size_t>(channel-1)][static_cast<std::size_t>(sourceNote)] = {};
    }
    for (auto& v : voices) if (v.sample && v.part == part) release(v);
    startPart(part,channel,sourceNote,target,std::clamp(velocity,0.0f,1.0f),dynamics);
}
int Engine::instrumentForPart(int part) const noexcept {
    if (part == 3 && settings.tubaVoice != TubaVoice::original) return warTubaInstrument;
    if (isSaxPart(part)) {
        if (settings.saxVoice != SaxVoice::tenor) return 4;
        if (settings.parts[static_cast<std::size_t>(part)].tenorSource == TenorSource::mtg) return mtgTenorInstrument;
    }
    return baseInstrumentForPart(part);
}
int Engine::articulationForPart(int part) const noexcept {
    const int instrument = instrumentForPart(part);
    if (instrument == mtgTenorInstrument) return 0;
    if (instrument == warTubaInstrument)
        return settings.articulation == Articulation::shortNote ? (settings.tubaVoice == TubaVoice::warStaccatissimo ? 2 : 1) : 0;
    int articulation = settings.articulation == Articulation::shortNote ? 1 : 0;
    if (settings.articulation == Articulation::fall && bank.isMapped(instrument, recordedFallArticulation))
        articulation = recordedFallArticulation;
    if (articulation == 1 && settings.parts[static_cast<std::size_t>(part)].doubleTongue
        && bank.isMapped(instrument, recordedDoubleTongueArticulation))
        articulation = recordedDoubleTongueArticulation;
    if (isSaxPart(part) && settings.saxVoice == SaxVoice::subtone) articulation = 2;
    if (isSaxPart(part) && settings.saxVoice == SaxVoice::growl) articulation = 3;
    return articulation;
}
KeyRange Engine::partKeyRange(int part) const noexcept {
    if (part < 0 || part >= partCount) return {};
    return bank.keyRange(instrumentForPart(part), articulationForPart(part));
}
void Engine::startPart(int p, int channel, int note, int target, float velocity, NoteDynamics dynamics) noexcept {
    if (target < 0 || target > 127) return;
    const auto humanDelay = onsetDelayForPart(p);
    const int instrument = instrumentForPart(p);
    const auto repetition = repetitions[static_cast<std::size_t>(p)][static_cast<std::size_t>(target)]++;
    const int articulation = articulationForPart(p);
    const bool marcato = isSaxPart(p) && settings.saxVoice == SaxVoice::marcato;
    constexpr std::array<int, partCount> counterparts {4, 5, -1, -1, 0, 1};
    const int counterpart = counterparts[static_cast<std::size_t>(p)];
    std::array<std::array<const Sample*, 4>, sampleArticulationCount> avoid {};
    if (counterpart >= 0 && settings.parts[static_cast<std::size_t>(counterpart)].enabled)
        for (const auto& voice : voices)
            if (voice.sample && voice.part == counterpart && voice.channel == channel && voice.note == note
                && voice.targetNote == target && voice.keyDown && !voice.released) {
                avoid[static_cast<std::size_t>(voice.sample->articulation)]
                     [static_cast<std::size_t>(voice.zone ? voice.zone->dynamicLayer : 0)] = voice.sample;
            }
    if (bank.isMapped(instrument, articulation)) {
        const int midiVelocity = static_cast<int>(std::round(velocity * 127));
        const int lastLayer = articulation == 0 ? 3 : 0;
        for (int layer = 0; layer <= lastLayer; ++layer)
            if (const auto* zone = bank.selectZone(instrument, articulation, target, midiVelocity, layer, repetition,
                                                  avoid[static_cast<std::size_t>(articulation)][static_cast<std::size_t>(layer)]))
                start(bank.samples()[static_cast<std::size_t>(zone->sample)], p, channel, note, target, velocity, 0, 0, zone, dynamics, humanDelay);
        return;
    }
    bool started = false;
    if (marcato) {
        if (const auto* attack = bank.selectSample(instrument, 1, target, velocity, repetition, avoid[1][0])) {
            start(*attack, p, channel, note, target, velocity, 0, 0, nullptr, dynamics, humanDelay); started = true;
        }
        if (const auto* sustain = bank.selectSample(instrument, 0, target, velocity, repetition, avoid[0][0])) {
            start(*sustain, p, channel, note, target, velocity, 0.1f, 0.15f, nullptr, dynamics, humanDelay); started = true;
        }
    } else {
        const auto* sample = bank.selectSample(instrument, articulation, target, velocity, repetition, avoid[static_cast<std::size_t>(articulation)][0]);
        if (!sample && articulation >= 2) sample = bank.selectSample(instrument, 0, target, velocity, repetition, avoid[0][0]);
        if (sample) { start(*sample, p, channel, note, target, velocity, 0, 0, nullptr, dynamics, humanDelay); started = true; }
    }
    if (started && p == 0 && instrument == 4 && settings.keyNoise > 0) {
        if (const auto* close = bank.keyNoise(target, false, repetition)) {
            auto& gesture = keyGestures[static_cast<std::size_t>(channel-1)][static_cast<std::size_t>(note)];
            gesture = { target, velocity, repetition, ++keySerial, humanDelay, false };
            startKey(*close, channel, note, gesture);
        }
    }
}
void Engine::noteOff(int channel, int note) noexcept {
    for (int part = 0; part < partCount; ++part) noteOffPart(part,channel,note);
}
void Engine::noteOffPart(int part, int channel, int note, bool forceRelease) noexcept {
    if (part < 0 || part >= partCount || channel < 1 || channel > 16 || note < 0 || note > 127) return;
    if (part == 0) {
    auto& gesture = keyGestures[static_cast<std::size_t>(channel-1)][static_cast<std::size_t>(note)];
    if (gesture.target >= 0) {
        if (gesture.started) {
            if (const auto* open = bank.keyNoise(gesture.target, true, gesture.repetition)) startKey(*open, channel, note, gesture);
        } else cancelPendingClose(channel, note);
        gesture = {};
    }
    }
    for (auto& v : voices) if (v.sample && v.part == part && v.channel == channel && v.note == note) {
        v.keyDown = false;
        if (v.humanOnsetPending) { v.sample = nullptr; continue; }
        if (forceRelease) release(v, 5);
        else if (!v.oneShot && !channels[static_cast<std::size_t>(channel - 1)].pedal) releaseNote(v);
    }

}
void Engine::endPart(int part, int channel) noexcept {
    if (part < 0 || part >= partCount) return;
    for (auto& v : voices) if (v.sample && v.part == part && (channel == 0 || v.channel == channel)) { v.airflowEligible = false; release(v); }
    for (auto& v : stealTails) if (v.sample && v.part == part && (channel == 0 || v.channel == channel)) { v.airflowEligible = false; release(v); }
    if (part == 0) clearKeys(channel,false);
}
void Engine::controller(int channel, int cc, int value) noexcept {
    if (channel < 1 || channel > 16) return;
    auto& c = channels[static_cast<std::size_t>(channel - 1)];
    const float normalized = std::clamp(value, 0, 127) / 127.0f;
    if (cc == 11 || cc == 2) { c.expression = normalized; c.hasExpression = true; }
    if (cc == 7) c.volume = normalized;
    if (cc == 1) c.vibrato = normalized;
    if (cc == 64) {
        c.pedal = value >= 64;
        if (!c.pedal) for (auto& v : voices) if (v.sample && v.channel == channel && !v.keyDown && !v.oneShot) releaseNote(v);
    }
    if (cc == 120) allNotesOff(channel, true);
    if (cc == 123) allNotesOff(channel, false);
    if (cc == 121) {
        c = {};
        for (auto& v : voices) if (v.sample && v.channel == channel && !v.keyDown && !v.oneShot) releaseNote(v);
    }
}
void Engine::pitchWheel(int channel, int value) noexcept {
    if (channel >= 1 && channel <= 16)
        channels[static_cast<std::size_t>(channel - 1)].bend = std::exp2(((std::clamp(value, 0, 16383) - 8192) / 8192.0f) * (2.0f / 12.0f));
}
void Engine::allNotesOff(int channel, bool immediate) noexcept {
    clearKeys(channel, immediate);
    clearTenorDetails(channel, immediate);
    if (immediate) airPlayer.stop(channel);
    for (auto& v : voices) if (v.sample && (channel == 0 || v.channel == channel)) {
        v.airflowEligible = false;
        if (immediate) v.sample = nullptr;
        else release(v);
    }
    for (auto& v : stealTails) if (v.sample && (channel == 0 || v.channel == channel)) {
        v.airflowEligible = false;
        if (immediate) v.sample = nullptr;
        else release(v);
    }
    if (channel == 0) for (auto& c : channels) c.pedal = false;
    else if (channel >= 1 && channel <= 16) channels[static_cast<std::size_t>(channel - 1)].pedal = false;
}
void Engine::triggerTenorDetail(TenorDetail kind,int channel) noexcept {
    if (channel<1 || channel>16 || !settings.parts[0].enabled || settings.saxVoice!=SaxVoice::tenor) return;
    auto& repetition=tenorRepetitions[kind==TenorDetail::breath?0:1];
    const auto* sample=bank.tenorDetail(kind,repetition);
    if (!sample) return;
    ++repetition;
    auto slot=std::find_if(tenorDetails.begin(),tenorDetails.end(),[](const DetailVoice& voice){return !voice.sample;});
    if (slot==tenorDetails.end()) {
        slot=std::min_element(tenorDetails.begin(),tenorDetails.end(),[](const DetailVoice& a,const DetailVoice& b){return a.age<b.age;});
        if (slot->position>0) {
            auto& tail=tenorDetailTails[nextTenorTail++%tenorDetailTails.size()];tail=*slot;
            tail.fadeStep=1.0f/static_cast<float>(rate*.003);
        }
    }
    *slot={};slot->sample=sample;slot->channel=channel;slot->baseGain=1;
    slot->increment=sample->rate/rate;slot->age=++tenorSerial;
}
void Engine::clearTenorDetails(int channel,bool immediate) noexcept {
    const auto stop=[&](DetailVoice& voice) {
        if (voice.sample && (channel==0 || voice.channel==channel)) {
            if (immediate || voice.position==0) voice.sample=nullptr;
            else voice.fadeStep=1.0f/static_cast<float>(rate*.003);
        }
    };
    for (auto& voice:tenorDetails) stop(voice);
    for (auto& voice:tenorDetailTails) stop(voice);
}
void Engine::startKey(const KeyNoiseZone& zone, int channel, int note, const KeyGesture& gesture) noexcept {
    auto slot = std::find_if(keyVoices.begin(), keyVoices.end(), [](const DetailVoice& v) { return !v.sample; });
    if (slot == keyVoices.end()) {
        slot = std::min_element(keyVoices.begin(), keyVoices.end(), [](const DetailVoice& a, const DetailVoice& b) { return a.age < b.age; });
        if (slot->position > 0) {
            auto& tail = keyTails[nextKeyTail++ % keyTails.size()]; tail = *slot;
            tail.fadeStep = 1.0f/static_cast<float>(rate*.003);
        }
    }
    *slot = {};
    slot->sample = &bank.keyRecording(zone.recording);
    slot->channel = channel; slot->note = note; slot->token = gesture.token; slot->age = ++keySerial;
    slot->opening = zone.opening;
    slot->onsetDelaySamples = zone.opening ? 0 : gesture.onsetDelaySamples;
    slot->increment = static_cast<double>(slot->sample->rate)/rate * std::exp2((gesture.target-zone.root)/12.0);
    slot->baseGain = zone.gain*std::pow(gesture.velocity, 1.3f);
}
void Engine::cancelPendingClose(int channel, int note) noexcept {
    for (auto& key : keyVoices)
        if (key.sample && key.channel == channel && key.note == note && !key.opening && key.position == 0) key.sample = nullptr;
}
void Engine::clearKeys(int channel, bool immediate) noexcept {
    for (std::size_t c = 0; c < keyGestures.size(); ++c)
        if (channel == 0 || channel == static_cast<int>(c)+1) keyGestures[c] = {};
    const auto stop = [&](DetailVoice& key) {
        if (key.sample && (channel == 0 || key.channel == channel)) {
            if (immediate || key.position == 0) key.sample = nullptr;
            else key.fadeStep = 1.0f/static_cast<float>(rate*.003);
        }
    };
    for (auto& key : keyVoices) stop(key);
    for (auto& key : keyTails) stop(key);
}
void Engine::renderDetails(float* left, float* right, int frames, float smooth) noexcept {
    const float pan = std::clamp(settings.parts[0].pan, -1.0f, 1.0f);
    const float panL = std::cos((pan+1)*pi/4), panR = std::sin((pan+1)*pi/4);
    const auto render = [&](DetailVoice& key,float amount) {
        if (!key.sample) return;
        const auto& sample = *key.sample;
        const float targetGain = key.baseGain*amount*settings.parts[0].gain*channels[static_cast<std::size_t>(key.channel-1)].volume;
        if (key.position == 0) { key.gain = targetGain; key.panL = panL; key.panR = panR; }
        for (int frame = 0; frame < frames; ++frame) {
            if (key.onsetDelaySamples > 0) { --key.onsetDelaySamples; continue; }
            if (!key.opening && key.position == 0)
                keyGestures[static_cast<std::size_t>(key.channel-1)][static_cast<std::size_t>(key.note)].started = true;
            if (key.position >= sample.frames() || key.fade <= 0) { key.sample = nullptr; break; }
            key.gain += smooth*(targetGain-key.gain);
            const float value = interpolate(sample, sample.audio, key.position)*key.gain*key.fade
                *humanGains[0][static_cast<std::size_t>(frame)];
            key.panL += smooth*(panL-key.panL); key.panR += smooth*(panR-key.panR);
            left[frame] += value*key.panL; right[frame] += value*key.panR;
            key.position += key.increment; key.fade = std::max(0.0f,key.fade-key.fadeStep);
        }
        if (key.position>=sample.frames() || key.fade<=0) key.sample=nullptr;
    };
    for (auto& key : keyVoices) render(key,settings.keyNoise);
    for (auto& key : keyTails) render(key,settings.keyNoise);
    for (auto& voice:tenorDetails) render(voice,settings.tenorDetailGain);
    for (auto& voice:tenorDetailTails) render(voice,settings.tenorDetailGain);
}
void Engine::render(float* left, float* right, int frames) noexcept {
    for (int offset = 0; offset < frames; offset += 128)
        renderChunk(left+offset,right+offset,std::min(128,frames-offset));
}
void Engine::renderChunk(float* left, float* right, int frames) noexcept {
    if (frames <= 0) return;
    const float smooth = 1.0f - std::exp(-1.0f / static_cast<float>(rate * 0.008));
    renderHumanization(frames, smooth);
    const float toneCoefficient = 1.0f - std::exp(-2.0f * pi * 700.0f / static_cast<float>(rate));
    std::array<std::array<AirflowFrame,128>,16> airFrames;
    const bool collectAir = settings.airflowAmount > 0 && settings.parts[0].enabled
        && settings.saxVoice != SaxVoice::tenor && !bank.airflow().audio.empty();
    const bool renderAir = collectAir || airPlayer.activeVoices() > 0;
    if (renderAir) airFrames = {};
    const auto renderVoice = [&](Voice& v) {
        if (!v.sample) return;
        const auto& s = *v.sample;
        const auto& part = settings.parts[static_cast<std::size_t>(v.part)];
        const auto& channel = channels[static_cast<std::size_t>(v.channel - 1)];
        const float pan = std::clamp(part.pan, -1.0f, 1.0f);
        const bool stereo = s.channels() == 2;
        const float scale = stereo ? 1.41421356237f : 1;
        const float panL = std::min(1.0f, scale * std::cos((pan + 1) * pi * 0.25f));
        const float panR = std::min(1.0f, scale * std::sin((pan + 1) * pi * 0.25f));
        float targetGain = part.gain * v.velocity * channel.expression * channel.volume;
        if (v.zone) {
            targetGain *= v.zone->gain;
            if (v.zone->dynamicLayer > 0) {
                float dynamics = channel.hasExpression ? channel.expression : v.noteVelocity;
                if (channel.hasExpression && v.dynamics == NoteDynamics::strikeAndPressure)
                    dynamics *= v.noteVelocity;
                targetGain *= std::max(0.0f, 1.0f - std::abs(dynamics*2 - (v.zone->dynamicLayer-1)));
            }
        }
        if (v.renderedSamples == 0) {
            // Same-sample MIDI controls can arrive after note-on.
            v.gain = targetGain; v.panL = panL; v.panR = panR;
        }
        const float fadeFrames = static_cast<float>(s.loopCrossfade);
        const double endingFade = s.instrument==0 || s.instrument==3 || s.instrument==mtgTenorInstrument || s.instrument==warTubaInstrument ? s.rate*.003 : 0;
        const bool looping = !v.oneShot && s.loopEnd > 0;
        const auto renderSamples = [&]<bool Stereo>() {
        for (int i = 0; i < frames; ++i) {
            if (v.onsetDelaySamples > 0) { --v.onsetDelaySamples; continue; }
            v.humanOnsetPending = false;
            if ((!looping && v.position >= s.frames()) || v.releasePosition >= s.frames()) { v.sample = nullptr; break; }
            const auto read = [&](const std::vector<float>& pcm) {
                float value = interpolate(s, pcm, v.position, looping && fadeFrames == 0, v.hasLooped && fadeFrames == 0);
                if (looping && fadeFrames > 0 && v.position >= s.loopEnd-fadeFrames && v.position < s.loopEnd) {
                    const double offset = v.position-(s.loopEnd-fadeFrames);
                    const float blend = static_cast<float>(offset/fadeFrames);
                    const double start = s.preStartCrossfade ? s.loopStart-fadeFrames : s.loopStart;
                    value = value*(1-blend) + interpolate(s,pcm,start+offset)*blend;
                }
                if (v.releasePosition >= 0)
                    value = value*(1-v.releaseBlend) + interpolate(s,pcm,v.releasePosition)*v.releaseBlend;
                return value;
            };
            float sample = read(s.audio), sampleRight = 0;
            if constexpr (Stereo) sampleRight = read(s.rightAudio);
            float recordingGain = envelopeGain(s, v.position);
            if (v.releasePosition >= 0)
                recordingGain = recordingGain * (1-v.releaseBlend) + envelopeGain(s, v.releasePosition) * v.releaseBlend;
            sample *= recordingGain;
            if constexpr (Stereo) sampleRight *= recordingGain;
            if (v.released) v.envelope = std::max(0.0f, v.envelope - v.releaseStep);
            else if (v.renderedSamples >= v.delaySamples) v.envelope = std::min(1.0f, v.envelope + v.attackStep);
            if (v.released && v.envelope <= 0) { v.sample = nullptr; break; }
            if (collectAir && v.airflowEligible && v.envelope > 0 && v.targetNote >= bank.airflow().lowKey && v.targetNote <= bank.airflow().highKey) {
                auto& frame = airFrames[static_cast<std::size_t>(v.channel-1)][static_cast<std::size_t>(i)];
                frame.gate = std::max(frame.gate,v.envelope);
                if (v.age >= frame.age) { frame.age = v.age; frame.target = v.targetNote; }
            }
            v.gain += smooth * (targetGain - v.gain);
            v.panL += smooth * (panL - v.panL);
            v.panR += smooth * (panR - v.panR);
            // Tightness adds a modest transient accent, without replacing recorded attacks.
            const float time = static_cast<float>(v.position / s.rate);
            const float accent = 1.0f + settings.tightness * 0.25f * std::max(0.0f, 1.0f - time * 12.0f);
            v.tone += smooth * (settings.tone - v.tone);
            v.toneLow[0] += toneCoefficient * (sample - v.toneLow[0]);
            const float lowGain = 1.0f - 0.35f * v.tone;
            const float highGain = 1.0f + 0.85f * v.tone;
            sample = v.toneLow[0] * lowGain + (sample - v.toneLow[0]) * highGain;
            // These original tenor/tuba endings can stop above zero.
            const float endingGain = endingFade>0 && v.releasePosition>=0
                ? std::clamp(static_cast<float>((s.frames()-1-v.releasePosition)/endingFade),0.0f,1.0f) : 1.0f;
            const float playerGain = humanGains[static_cast<std::size_t>(v.part)][static_cast<std::size_t>(i)];
            const float out = sample * v.envelope * v.gain * accent * endingGain * playerGain;
            left[i] += out * v.panL;
            if constexpr (Stereo) {
                v.toneLow[1] += toneCoefficient * (sampleRight-v.toneLow[1]);
                sampleRight = v.toneLow[1]*lowGain + (sampleRight-v.toneLow[1])*highGain;
                right[i] += sampleRight*v.envelope*v.gain*accent*endingGain*playerGain*v.panR;
            } else right[i] += out * v.panR;
            double increment = v.increment * channel.bend;
            if (channel.vibrato > 0) {
                const auto index = static_cast<std::size_t>(v.vibratoPhase);
                const float fraction = v.vibratoPhase - static_cast<float>(index);
                const float wave = vibratoWave[index] + fraction * (vibratoWave[index + 1] - vibratoWave[index]);
                const float onset = std::clamp((static_cast<float>(v.renderedSamples / rate) - 0.2f) / 0.2f, 0.0f, 1.0f);
                const float modulation = wave * channel.vibrato * onset * (0.69314718056f * 0.25f / 12.0f);
                // Cubic exp is accurate within 0.001 cent across this +/-25-cent range.
                increment *= 1.0f + modulation * (1.0f + modulation * (0.5f + modulation / 6.0f));
                v.vibratoPhase += (5.0f + 0.17f * v.part) * 1024.0f / static_cast<float>(rate);
                if (v.vibratoPhase >= 1024.0f) v.vibratoPhase -= 1024.0f;
            }
            ++v.renderedSamples;
            if (v.syntheticFall && time > 0.12f) {
                v.fallSemitones = std::min(12.0f, v.fallSemitones + 18.0f / static_cast<float>(rate));
                increment *= std::exp2(-v.fallSemitones / 12.0f);
                if (v.fallSemitones > 8) release(v);
            }
            v.position += increment;
            if (v.releasePosition >= 0) {
                v.releasePosition += increment;
                v.releaseBlend = std::min(1.0f, v.releaseBlend + static_cast<float>(increment / std::max(1, s.releaseCrossfade)));
            }
            if (looping && v.position >= s.loopEnd) {
                v.position = s.loopStart + (s.preStartCrossfade ? 0 : fadeFrames) + (v.position - s.loopEnd);
                v.hasLooped = true;
            }
        }
        if ((!looping && v.position>=s.frames()) || v.releasePosition>=s.frames()) v.sample=nullptr;
        };
        if (stereo) renderSamples.operator()<true>();
        else renderSamples.operator()<false>();
    };
    for (auto& key : keyVoices) if (key.sample && key.position == 0 && !key.opening) {
        auto& gesture = keyGestures[static_cast<std::size_t>(key.channel-1)][static_cast<std::size_t>(key.note)];
        if (gesture.token != key.token) { key.sample = nullptr; continue; }
        const bool playable = std::any_of(voices.begin(), voices.end(), [&](const Voice& v) {
            return v.sample && v.part == 0 && v.channel == key.channel && v.note == key.note;
        });
        if (playable) { if (key.onsetDelaySamples == 0) gesture.started = true; }
        else { key.sample = nullptr; gesture = {}; }
    }
    for (auto& v : voices) renderVoice(v);
    for (auto& v : stealTails) renderVoice(v);
    renderDetails(left, right, frames, smooth);
    if (renderAir) for (std::size_t channel = 0; channel < channels.size(); ++channel) {
        const auto& c = channels[channel];
        const AirflowControls controls {settings.airflowAmount,c.expression,c.volume,settings.parts[0].gain,settings.parts[0].pan};
        airPlayer.render(static_cast<int>(channel)+1,std::span(airFrames[channel].data(),static_cast<std::size_t>(frames)),controls,left,right);
    }
    for (int i = 0; i < frames; ++i) {
        masterGain += smooth * (settings.gain - masterGain);
        const float l = left[i] * masterGain, r = right[i] * masterGain;
        left[i] = protectOutput(l);
        right[i] = protectOutput(r);
    }
}
int Engine::activeVoices() const noexcept {
    int count = 0;
    for (const auto& v : voices) if (v.sample) ++count;
    for (const auto& v : stealTails) if (v.sample) ++count;
    for (const auto& v : keyVoices) if (v.sample) ++count;
    for (const auto& v : keyTails) if (v.sample) ++count;
    for (const auto& voice:tenorDetails) if (voice.sample) ++count;
    for (const auto& voice:tenorDetailTails) if (voice.sample) ++count;
    return count+airPlayer.activeVoices();
}
}
