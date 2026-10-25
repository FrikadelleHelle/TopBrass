# Section evaluation performances

These are original programmed MIDI examples, not recordings of human performances or evidence that TopBrass meets the requested realism. Regenerate with `python3 scripts/make_section_scores.py`; the generator checks source pitch ranges and non-overlapping held notes. `manifest.json` records the pitches, velocities, durations, tempo and MIDI hashes. C4 is MIDI 60.

The four MIDI channels are tenor saxophone (1), trumpet (2), trombone (3), tuba (4). Set every octave control to zero and route each part to its own channel when importing the MIDI into a DAW.

- Funk: 104 BPM, Dm9 / Bb13 / C9 / A7. Short recordings, syncopated upper voices, sparse trombone and independent bass. MIDI note-offs do not truncate short recordings in v0.3; acoustic tails may extend beyond the notated gates.
- Soul: 76 BPM, Cmaj9 / Am9 / Dm9 / G13. Sustains, trumpet/saxophone call and response, support-note expression curves and breath gaps. CC11 changes amplitude; it does not morph the recorded dynamic layer.

`ctest --test-dir build -R processor --output-on-failure` renders both scores through the actual processor into `artifacts/section-performances`, with full section and four exposed stems for each score. Each WAV has an adjacent XML file with exact processor settings. The render uses neutral tone and attack accent, tenor saxophone, no added vibrato, no reverb and no mastering. The isolated stems retain the same level and pan as the section. Files are stereo 24-bit / 48 kHz with two seconds of tail after the last MIDI event.

Listen to exposed attacks, repeated pitches, held notes and endings as well as the full mix. A more musical arrangement can improve presentation without fixing the recording quality, missing interval transitions, synthetic falls or dynamic-layer behavior. Sound acceptance remains pending.

`fall-audition.mid` is an exposed eight-note diagnostic for trumpet/channel2 and trombone/channel3. Select Fall. The first four notes repeat one pitch and velocity to expose the four recorded alternatives; later notes vary register and velocity. The processor test renders each brass part separately and checks that Fall selects a single recorded playhead. This audition is not a claim of accepted realism.
