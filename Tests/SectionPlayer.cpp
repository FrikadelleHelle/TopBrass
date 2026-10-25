#include "SectionPlayer.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace topbrass;
struct Recorded {
    SectionEvent event;
    std::int64_t sample;
};
struct Sink {
    std::vector<Recorded> events;
    std::int64_t base = 0;
    static void accept(void* ptr, const SectionEvent& event) {
        auto& self = *static_cast<Sink*>(ptr);
        self.events.push_back({event, self.base + event.offset});
    }
};
void check(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}
std::vector<Recorded> run(int block) {
    SectionPlayer player;
    player.prepare(48000);
    player.configure({PlayingMode::groove, 60, 15, .7f});
    player.clock(120, false, {});
    player.noteOn(1, 60, .9f);
    player.noteOn(1, 64, .8f);
    player.noteOn(1, 67, .85f);
    Sink sink;
    for (int offset = 0; offset < 192000;) {
        int n = std::min(block, 192000 - offset);
        sink.base = offset;
        player.advance(n, &sink, Sink::accept);
        offset += n;
    }
    return sink.events;
}
void checkSectionHits(float feel, unsigned mask) {
    SectionPlayer player;
    player.prepare(48000);
    player.configure({PlayingMode::groove, 60, mask, feel});
    player.noteOn(1, 60, .9f);
    Sink sink;
    player.advance(96000, &sink, Sink::accept);
    unsigned members = 0;
    std::int64_t first = -1, last = -1;
    int hits = 0;
    const auto finish = [&] {
        check(members == mask, "Every groove hit must contain all enabled instruments");
        check(last - first <= (feel == 0 ? 0 : 24), "Section players must stay within half a millisecond of each other");
    };
    float softest = 1, strongest = 0;
    for (const auto& row : sink.events) {
        if (!row.event.on)
            continue;
        if (first < 0 || row.sample - first > 2400) {
            if (first >= 0)
                finish();
            first = row.sample;
            members = 0;
            ++hits;
        }
        check((members & (1u << row.event.part)) == 0, "A player may trigger only once per section hit");
        members |= 1u << row.event.part;
        last = row.sample;
        softest = std::min(softest, row.event.velocity);
        strongest = std::max(strongest, row.event.velocity);
    }
    finish();
    check(hits == 9, "The section must share the authored nine-hit syncopated bar");
    check(strongest - softest > .15f, "Ghost and accented section hits must have different playing force");
}
int main() {
    try {
        checkSectionHits(0, 15);
        checkSectionHits(1, 15);
        checkSectionHits(1, 5);
        checkSectionHits(0, 63);
        checkSectionHits(1, 63);
        checkSectionHits(1, 48);
        {
            SectionPlayer nearGrid;
            nearGrid.prepare(48000);
            nearGrid.configure({PlayingMode::groove, 60, 15, 0});
            nearGrid.clock(120, true, .499);
            nearGrid.noteOn(1, 60, .9f);
            Sink notes;
            nearGrid.advance(960, &notes, Sink::accept);
            check(std::count_if(notes.events.begin(), notes.events.end(), [](auto e) { return e.event.on; }) == 4,
                  "A played section hit just before the host grid must not flam with the scheduled hit");
        }
        {
            SectionPlayer voiced;
            voiced.prepare(48000);
            voiced.configure({PlayingMode::groove, 60, 15, 0});
            voiced.noteOn(1, 60, .8f);
            voiced.noteOn(1, 64, .8f);
            voiced.noteOn(1, 67, .8f);
            Sink chord;
            voiced.advance(1, &chord, Sink::accept);
            const std::array<int, 4> expected{64, 67, 60, 60};
            check(chord.events.size() == 4, "A same-timestamp chord must produce one four-player section attack");
            for (const auto& row : chord.events)
                check(row.event.on && row.event.note == expected[static_cast<std::size_t>(row.event.part)],
                      "A section chord must retain trumpet top, sax middle and lower brass roles");
        }
        {
            SectionPlayer nearGrid;
            nearGrid.prepare(48000);
            nearGrid.configure({PlayingMode::groove, 60, 15, 0});
            nearGrid.noteOn(1, 60, .9f);
            Sink notes;
            nearGrid.advance(12001, &notes, Sink::accept);
            notes.events.clear();
            notes.base = 12001;
            nearGrid.noteOn(1, 64, .85f);
            nearGrid.advance(960, &notes, Sink::accept);
            check(std::none_of(notes.events.begin(), notes.events.end(), [](auto e) { return e.event.on; }),
                  "A new chord tone just after a scheduled hit must join that gesture without a second attack");
            notes.events.clear();
            nearGrid.noteOn(1, 64, .85f);
            nearGrid.advance(1, &notes, Sink::accept);
            check(std::count_if(notes.events.begin(), notes.events.end(), [](auto e) { return e.event.on; }) == 4,
                  "An intentional same-key retrigger must still start a new section hit");
        }
        {
            SectionPlayer chord;
            chord.prepare(48000);
            chord.configure({PlayingMode::groove, 60, 15, 1});
            Sink notes;
            chord.noteOn(1, 60, .9f);
            chord.advance(48, &notes, Sink::accept);
            notes.base = 48;
            chord.noteOn(1, 64, .85f);
            chord.advance(432, &notes, Sink::accept);
            notes.base = 480;
            chord.noteOn(1, 67, .8f);
            chord.advance(480, &notes, Sink::accept);
            check(std::count_if(notes.events.begin(), notes.events.end(), [](auto e) { return e.event.on; }) == 4,
                  "Chord tones arriving within ten milliseconds must form one section attack without flams");
        }
        auto a = run(512), b = run(31);
        check(a.size() == b.size() && a.size() > 20, "A held chord must generate a rhythmic section");
        for (std::size_t i = 0; i < a.size(); ++i) {
            check(a[i].sample == b[i].sample && a[i].event.part == b[i].event.part &&
                      a[i].event.note == b[i].event.note && a[i].event.velocity == b[i].event.velocity &&
                      a[i].event.on == b[i].event.on,
                  "Scheduling must be independent of audio block partitions");
            check(a[i].event.note == 60 || a[i].event.note == 64 || a[i].event.note == 67,
                  "Patterns must use held pitches rather than inventing harmony");
        }
        check(a.front().sample == 0 && a.front().event.on, "The lead note must respond without scheduling latency");
        SectionPlayer p;
        p.prepare(48000);
        p.configure({PlayingMode::relay, 60, 15, 0});
        Sink sink;
        p.noteOn(1, 59, 1);
        p.noteOn(1, 75, 1);
        p.advance(100, &sink, Sink::accept);
        check(sink.events.empty(), "Only the fifteen inclusive notes60..74 may trigger");
        for (int note = 60; note < 64; ++note) {
            p.noteOn(1, note, 1);
            p.advance(1, &sink, Sink::accept);
        }
        std::array<bool, 4> used{};
        for (auto e : sink.events)
            if (e.event.on)
                used[e.event.part] = true;
        check(std::all_of(used.begin(), used.end(), [](bool x) { return x; }),
              "Relay must distribute successive notes across enabled parts");
        p.reset();
        sink.events.clear();
        p.noteOn(1, 60, 1);
        p.noteOff(1, 60);
        p.advance(100, &sink, Sink::accept);
        check(sink.events.empty(), "Zero-duration input must not trigger a pending performance");
        p.reset();
        p.configure({PlayingMode::groove, 60, 2, .5f});
        sink.events.clear();
        p.noteOn(1, 60, 1);
        p.advance(48000, &sink, Sink::accept);
        for (auto e : sink.events)
            check(e.event.part == 1, "Disabled instruments must not receive generated notes");
        p.noteOff(1, 60);
        sink.events.clear();
        p.advance(96000, &sink, Sink::accept);
        check(std::none_of(sink.events.begin(), sink.events.end(), [](auto e) { return e.event.on; }),
              "Releasing the final note must cancel future syncopation");
        p.reset();
        p.noteOn(1, 60, 1);
        p.controller(1, 64, 127);
        p.noteOff(1, 60);
        check(p.heldNotes() == 1, "Pedal must latch the input chord");
        p.controller(1, 64, 0);
        check(p.heldNotes() == 0, "Pedal-up must release latched input notes");
        p.noteOn(1, 60, 1);
        p.clock(120, true, 0);
        check(p.heldNotes() == 0, "Starting transport must invalidate prior free-running ownership");
        p.noteOn(1, 60, 1);
        p.clock(120, true, 8);
        check(p.heldNotes() == 0, "A transport seek must clear held/generated ownership");
        p.reset();
        p.configure({PlayingMode::relay, 60, 2, 0});
        sink.events.clear();
        p.noteOn(1, 60, 1);
        p.advance(1, &sink, Sink::accept);
        p.noteOn(1, 64, 1);
        p.advance(1, &sink, Sink::accept);
        sink.events.clear();
        p.noteOff(1, 60);
        p.advance(100, &sink, Sink::accept);
        check(sink.events.empty(), "An old key release must not terminate a newer assignment on the same player");
        p.reset();
        p.configure({PlayingMode::groove, 60, 15, 1});
        sink.events.clear();
        p.noteOn(1, 60, 1);
        p.advance(12000, &sink, Sink::accept);
        p.configure({PlayingMode::groove, 60, 1, 1});
        sink.events.clear();
        p.advance(48000, &sink, Sink::accept);
        for (const auto& event : sink.events)
            if (event.event.on)
                check(event.event.part == 0,
                      "Muting must cancel that player's pending work while retaining the remaining section");
        p.reset();
        p.configure({PlayingMode::groove, 60, 15, .4f});
        sink.events.clear();
        p.noteOn(1, 60, 1);
        p.advance(12000, &sink, Sink::accept);
        check(p.configure({PlayingMode::groove, 72, 15, .4f}) && p.heldNotes() == 0,
              "Changing the fifteen-key window must clear its old ownership");
        p.reset();
        p.configure({PlayingMode::groove, 60, 2, 0});
        sink.events.clear();
        p.noteOn(1, 60, 1);
        p.advance(1, &sink, Sink::accept);
        sink.events.clear();
        p.noteOff(1, 60);
        p.noteOn(1, 64, 1);
        p.advance(1, &sink, Sink::accept);
        check(sink.events.size() == 2 && !sink.events[0].event.on && sink.events[1].event.on &&
                  sink.events[1].event.note == 64,
              "A same-sample release/new note must reuse the player immediately instead of dropping the new lead");
        p.reset();
        p.configure({PlayingMode::layered, 60, 15, 0});
        check(!p.configure({PlayingMode::layered, 72, 15, 0}),
              "An inactive section range must not reset Layered playback");
        p.prepare(48000);
        p.configure({PlayingMode::groove, 60, 7, 0});
        sink.events.clear();
        p.noteOn(1, 60, 1);
        p.advance(12001, &sink, Sink::accept);
        int lastPart = -1;
        for (const auto& event : sink.events)
            if (event.event.on)
                lastPart = event.event.part;
        check(lastPart == 2,
              "With tuba disabled, bass punctuation must go to the trombone rather than wrap back to trumpet");
        checkSectionHits(0, 63);
        checkSectionHits(.7f, 63);
        p.prepare(48000);
        p.configure({PlayingMode::relay, 60, 63, 0});
        sink.events.clear();
        for (int i = 0; i < partCount; ++i) {
            p.noteOn(1, 60 + i, 1);
            p.advance(1, &sink, Sink::accept);
        }
        unsigned routed = 0;
        for (const auto& row : sink.events) if (row.event.on) routed |= 1u << row.event.part;
        check(routed == 63, "Relay must visit both additional players independently");
        std::cout << "PASS: fifteen-key section relay/groove, held pitches, enabled parts, immediate lead, "
                     "deterministic timing, release, pedal and transport reset\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
