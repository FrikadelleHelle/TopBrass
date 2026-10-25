#include "SectionPlayer.h"
#include <algorithm>
#include <cmath>

namespace topbrass {
namespace { constexpr double gestureSeconds = .012; }
void SectionPlayer::prepare(double sampleRate) noexcept {
    rate = std::isfinite(sampleRate) && sampleRate >= 8000 && sampleRate <= 384000 ? sampleRate : 48000;
    sample = 0;
    beat = 0;
    tempo = 120;
    wasRunning = false;
    clockKnown = true;
    phrase = 0;
    reset();
}
void SectionPlayer::reset() noexcept {
    keys = {};
    pedal = {};
    parts = {};
    heldCount = 0;
    relay = 0;
    sequence = 0;
    rhythm = false;
    lastHit = never;
}
bool SectionPlayer::configure(SectionConfig next) noexcept {
    next.lowestKey = std::clamp(next.lowestKey, 0, 128 - sectionKeyCount);
    next.enabled &= (1u << partCount) - 1;
    next.feel = std::isfinite(next.feel) ? std::clamp(next.feel, 0.0f, 1.0f) : .4f;
    const bool hard =
        next.mode != config.mode || (next.mode != PlayingMode::layered && next.lowestKey != config.lowestKey);
    if (hard)
        reset();
    for (std::size_t part = 0; part < parts.size(); ++part)
        if (!(next.enabled & (1u << part))) {
            parts[part].pending.reset();
            parts[part].onDue = never;
            if (parts[part].active)
                parts[part].offDue = sample;
        }
    config = next;
    return hard;
}
bool SectionPlayer::clock(double bpm, bool running, std::optional<double> ppq) noexcept {
    if (std::isfinite(bpm) && bpm >= 20 && bpm <= 400)
        tempo = bpm;
    if (ppq && (!std::isfinite(*ppq) || std::abs(*ppq) > 1e9))
        ppq.reset();
    const bool seek = running && wasRunning && ppq && std::isfinite(*ppq) && std::abs(*ppq - beat) > .001;
    const bool changed = clockKnown && (running != wasRunning || seek);
    if (changed)
        reset();
    if (running && ppq && std::isfinite(*ppq))
        beat = *ppq;
    clockKnown = true;
    wasRunning = running;
    return changed;
}
std::array<int, partCount> SectionPlayer::enabledParts(int& count) const noexcept {
    std::array<int, partCount> result{};
    count = 0;
    for (int part : {1, 0, 2, 3, 5, 4})
        if (config.enabled & (1u << part))
            result[static_cast<std::size_t>(count++)] = part;
    return result;
}
float SectionPlayer::variation(int part, int stepIndex) const noexcept {
    std::uint32_t value = (phrase + 1) * 0x9e3779b9u + (sequence + 1) * 0x85ebca6bu +
                          static_cast<std::uint32_t>(part * 97 + stepIndex * 31);
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    return static_cast<float>(value & 65535) / 32767.5f - 1;
}
void SectionPlayer::plan(int part, Note note, float accent, int delay, int duration) noexcept {
    auto& slot = parts[static_cast<std::size_t>(part)];
    note.velocity = std::clamp(note.velocity * accent, 1.0f / 127, 1.0f);
    slot.pending = note;
    slot.onDue = sample + delay;
    slot.duration = duration;
    ++sequence;
}
void SectionPlayer::noteOn(int channel, int note, float velocity) noexcept {
    if (config.mode == PlayingMode::layered || channel < 1 || channel > 16 || note < config.lowestKey ||
        note >= config.lowestKey + sectionKeyCount)
        return;
    if (!std::isfinite(velocity))
        return;
    if (velocity <= 0) {
        noteOff(channel, note);
        return;
    }
    auto& key = keys[static_cast<std::size_t>(channel - 1)][static_cast<std::size_t>(note)];
    const bool first = heldCount == 0;
    const bool newKey = !key.held;
    if (!key.held)
        ++heldCount;
    key = {true, true, std::clamp(velocity, 0.0f, 1.0f)};
    if (first) {
        ++phrase;
        sequence = 0;
        if (wasRunning) {
            const auto grid = static_cast<std::int64_t>(std::floor(beat * 4 + 1e-8)) + 1;
            nextStep = static_cast<int>((grid % 16 + 16) % 16);
            nextBeat = grid * .25;
        } else {
            nextStep = 1;
            nextBeat = beat + .25;
        }
        rhythm = true;
    }
    int count = 0;
    const auto enabled = enabledParts(count);
    if (count == 0)
        return;
    if (config.mode == PlayingMode::groove) {
        if (!first && newKey && lastHit != never && sample - lastHit <= static_cast<std::int64_t>(rate * gestureSeconds)) {
            for (int index = 0; index < count; ++index) {
                const int p = enabled[static_cast<std::size_t>(index)];
                auto& pending = parts[static_cast<std::size_t>(p)].pending;
                if (pending)
                    if (const auto updated = pitch(p)) {
                        const float original = keys[static_cast<std::size_t>(pending->channel - 1)][static_cast<std::size_t>(pending->note)].velocity;
                        const float accent = pending->velocity / original;
                        *pending = *updated;
                        pending->velocity = std::clamp(pending->velocity * accent, 1.0f / 127, 1.0f);
                    }
            }
            return;
        }
        hit(1, .28f, 0);
        return;
    }
    const int part = enabled[static_cast<std::size_t>(relay++ % count)];
    const float accent = 1 + variation(part, 0) * config.feel * .04f;
    plan(part, {channel, note, key.velocity}, accent, 0, 0);
}
void SectionPlayer::releaseKey(int channel, int note) noexcept {
    auto& key = keys[static_cast<std::size_t>(channel - 1)][static_cast<std::size_t>(note)];
    if (key.held) {
        key.held = false;
        --heldCount;
    }
    for (auto& part : parts) {
        if (part.pending && part.pending->channel == channel && part.pending->note == note) {
            part.pending.reset();
            part.onDue = never;
        }
        if (part.active && part.active->channel == channel && part.active->note == note)
            part.offDue = sample;
    }
    if (heldCount == 0)
        rhythm = false;
}
void SectionPlayer::noteOff(int channel, int note) noexcept {
    if (channel < 1 || channel > 16 || note < 0 || note > 127)
        return;
    keys[static_cast<std::size_t>(channel - 1)][static_cast<std::size_t>(note)].physical = false;
    if (!pedal[static_cast<std::size_t>(channel - 1)])
        releaseKey(channel, note);
}
void SectionPlayer::controller(int channel, int cc, int value) noexcept {
    if (channel < 1 || channel > 16)
        return;
    const auto c = static_cast<std::size_t>(channel - 1);
    if (cc == 64 || cc == 121) {
        pedal[c] = cc == 64 && value >= 64;
        if (!pedal[c])
            for (int note = 0; note < 128; ++note)
                if (!keys[c][static_cast<std::size_t>(note)].physical)
                    releaseKey(channel, note);
    }
    if (cc == 120 || cc == 123) {
        pedal[c] = false;
        for (int note = 0; note < 128; ++note) {
            keys[c][static_cast<std::size_t>(note)].physical = false;
            releaseKey(channel, note);
        }
    }
}
std::optional<SectionPlayer::Note> SectionPlayer::pitch(int part) const noexcept {
    constexpr std::array<int, 4> roles{1, 0, 2, 3};
    const int role = roles[static_cast<std::size_t>(baseInstrumentForPart(part))];
    if (heldCount == 0)
        return {};
    int rank = role == 0   ? heldCount - 1
               : role == 3 ? 0
               : role == 1 ? heldCount / 2
                           : std::max(0, heldCount / 2 - 1);
    for (int note = 0; note < 128; ++note)
        for (int channel = 1; channel <= 16; ++channel) {
            const auto& key = keys[static_cast<std::size_t>(channel - 1)][static_cast<std::size_t>(note)];
            if (key.held && rank-- == 0)
                return Note{channel, note, key.velocity};
        }
    return {};
}
void SectionPlayer::hit(float accent, float length, int stepIndex) noexcept {
    lastHit = sample;
    int count = 0;
    const auto enabled = enabledParts(count);
    const float group = variation(4, stepIndex);
    const int duration = static_cast<int>(rate * 60 / tempo * length * (1 + group * config.feel * .08f));
    for (int index = 0; index < count; ++index) {
        const int part = enabled[static_cast<std::size_t>(index)];
        if (const auto note = pitch(part)) {
            const float player = variation(part, stepIndex);
            plan(part, *note, accent * (1 + group * config.feel * .08f + player * config.feel * .035f),
                 0, std::max(1, duration + static_cast<int>(player * rate * .00025 * config.feel)));
        }
    }
}
void SectionPlayer::step() noexcept {
    bool play = true;
    float accent = .85f, length = .16f;
    switch (nextStep) {
    case 0:
    case 8:
        accent = 1;
        break;
    case 2:
    case 10:
        accent = .88f;
        break;
    case 3:
    case 11:
        accent = nextStep == 11 ? .5f : .82f;
        break;
    case 6:
    case 14:
        break;
    case 15:
        accent = .8f;
        length = .12f;
        break;
    default:
        play = false;
        break;
    }
    if (play && (lastHit == never || sample - lastHit > static_cast<std::int64_t>(rate * gestureSeconds)))
        hit(accent, length, nextStep);
    nextStep = (nextStep + 1) % 16;
    nextBeat += .25;
}
void SectionPlayer::advance(int frames, void* context, Sink sink) noexcept {
    if (frames <= 0)
        return;
    const auto start = sample, end = sample + frames;
    const double beatsPerSample = tempo / (60 * rate);
    while (sample < end) {
        auto due = end;
        for (const auto& part : parts)
            due = std::min({due, part.onDue, part.offDue});
        const auto stepDue = config.mode == PlayingMode::groove && rhythm
                                 ? sample + std::max<std::int64_t>(0, std::llround((nextBeat - beat) / beatsPerSample))
                                 : never;
        due = std::max(sample, std::min(due, stepDue));
        if (due >= end) {
            beat += (end - sample) * beatsPerSample;
            sample = end;
            break;
        }
        beat += (due - sample) * beatsPerSample;
        sample = due;
        for (std::size_t index = 0; index < parts.size(); ++index) {
            auto& part = parts[index];
            if (part.offDue <= sample) {
                if (part.active) {
                    const auto note = *part.active;
                    sink(context, {false, static_cast<int>(index), note.channel, note.note,
                                   static_cast<int>(sample - start), 0});
                    part.active.reset();
                }
                part.offDue = never;
            }
            if (part.onDue <= sample) {
                if (part.pending) {
                    const auto note = *part.pending;
                    if (keys[static_cast<std::size_t>(note.channel - 1)][static_cast<std::size_t>(note.note)].held &&
                        (config.enabled & (1u << index))) {
                        if (part.active) {
                            const auto old = *part.active;
                            sink(context, {false, static_cast<int>(index), old.channel, old.note,
                                           static_cast<int>(sample - start), 0});
                        }
                        sink(context, {true, static_cast<int>(index), note.channel, note.note,
                                       static_cast<int>(sample - start), note.velocity});
                        part.active = note;
                        part.offDue = part.duration > 0 ? sample + part.duration : never;
                    }
                }
                part.pending.reset();
                part.onDue = never;
            }
        }
        if (stepDue <= sample)
            step();
    }
}
}
