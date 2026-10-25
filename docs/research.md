# Sound and architecture decisions

The target is a playable pop section with four switchable instruments, clear attacks, short horn hits, sustained phrases, and live expression. Saxophone is grouped with the brass controls because it belongs in this section, although it is a woodwind.

## Why samples first

Three approaches were considered.

- Physical modeling can respond continuously to breath, embouchure, and pitch. Audio Modeling's SWAM brass demonstrates that a commercial product can do this without samples. Reproducing that quality requires solving and tuning excitation, nonlinear bore behavior, radiation, and playing transitions. No validated model or calibration data existed in this project.
- Multisampling preserves a musician's attack and instrument spectrum directly. It trades storage and RAM for a small, predictable playback cost. Its realism depends on recorded dynamics, note density, repeats, and transitions.
- Samples with expressive DSP preserve recorded attacks while adding expression, pitch bend, looped sustains, and parameter smoothing. This is the implemented approach. It avoids training or inference on the audio thread.

Choosing samples is an engineering judgment for this corpus and first build, rather than a claim that samples universally sound better. The [SWAM brass manual](https://support.audiomodeling.com/guides/brass150/SWAM%20Solo%20Brass%20-%20User%20Manual%20-%20v1.5.0.pdf) describes its physical-model approach. [Time-domain numerical modeling of brass instruments](https://arxiv.org/abs/1511.04247) illustrates the acoustic processes a more complete solver must address.

## What the source recordings provide

[VCSL](https://github.com/sgossner/VCSL/tree/c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e) provides tenor saxophone. [VSCO Community Edition](https://github.com/sgossner/VSCO-2-CE/tree/440300901dfe9275fd84e0b7763af1f8443ae62e) provided the original trumpet, tenor trombone, and tuba. Version 0.4 retains its tuba. Both pinned repository licenses are CC0. Their license texts are retained in `assets/plugin-resources`.

The version 0.3 bank had 914 recordings: 86 tenor saxophone, 568 baritone saxophone, 86 trumpet, 87 trombone, and 87 tuba. Each has sustained and short notes. Available layers and repetitions vary by pitch. In particular, a saxophone filename containing `rr7` does not prove that seven repetitions exist. The engine rotates only the actual available recordings for a selected pitch and layer.

The source filenames use C3 for MIDI 60. Source sample rates and bit depths vary. Preparation decodes each WAV, sums its channels to mono, removes pre-roll, applies a two-millisecond edge fade, normalizes each recording to a peak of 0.65, and packs native-rate float32 audio. Normalization removes recording-level differences. Continuous MIDI velocity supplies loudness while recorded layers supply different tone. This is not a physically calibrated model of player dynamics.

VCSL and VSCO sustain loops are selected offline using normalized cross-correlation and amplitude balance over 40-millisecond overlap windows. Candidates search an early stable portion of each note. Preparation rejects loops below correlation 0.85 or with excessive level mismatch. `assets/bank-audit.json` records endpoints, correlations, gains, and the final bank checksum.

The original sample choice was a principal quality limitation. VSCO is an orchestral collection. Version 0.4 replaces its trumpet and trombone with Bell & Bone; tuba remains VSCO. Processing cannot invent the missing accented pop attacks, authentic falls, scoops, growls, or interval transitions. [Session Horns Pro's articulation table](https://www.native-instruments.com/de/products/session-horns-pro) provides a useful commercial comparison for recorded articulations and repetitions. Its recordings are not redistribution assets for this project.

[Bear Sax](https://github.com/sfzinstruments/karoryfer.bear-sax/tree/7abb3c652525a15dfac80e1b5dfbba9964ee568f) now supplies the baritone saxophone. [Karoryfer Weresax](https://shop.karoryfer.com/pages/free-weresax) remains an unintegrated alto candidate. [Philharmonia's sample terms](https://philharmonia.co.uk/resources/sound-samples/) prohibit redistribution as a sampler instrument, so those recordings were excluded.

## Plugin and state ownership

The host supplies MIDI and automatable parameters to `TopBrassProcessor`. The processor reads cached atomic parameter pointers once per block, translates MIDI, and splits rendering at event sample offsets. MIDI velocity-zero note-ons become note-offs. Host state uses JUCE's parameter ValueTree. Instrument toggles, levels, pans, octave offsets, MIDI channels, articulation, output, release, tone, and accent are restored with the project.

`Bank` owns immutable decoded recordings. Plugin instances in the same loaded module share a bank while any instance remains alive. Loading and the cache mutex run during construction. The engine references stable recording memory and never changes the bank during playback.

`Engine` owns 128 primary voices and 16 short steal tails. Storage is fixed. Note selection scans bounded bank metadata and rotates actual recordings. A stolen voice fades for three milliseconds in a tail slot. An extreme burst can replace a tail before its fade ends. The engine is bounded at 144 active voices including tails.

Each instrument can listen to all channels or one channel. Layering plays enabled instruments at their octave offsets. Separate MIDI channels allow deliberate voicings. There is no automatic section arranger or claim that playing an arbitrary chord creates a realistic four-player arrangement.

Audio uses cubic sample interpolation, equal-power pan, smoothed gain and pan, recorded attacks, release fades for sustained notes, source-specific sustain looping, a smoothed warm-to-bright tilt around 700 Hz, delayed CC1 vibrato, and a safety knee near clipping. Ordinary-level audio remains linear. The safety knee colors overloaded peaks above 0.95 full scale. Pitch bend and synthetic falls are playback-rate changes. Large pitch shifts and cubic interpolation are quality limits; there is no bandlimited resampler or formant-preserving pitch shifter.

The onscreen keyboard sends fixed-size messages through a single-producer, single-consumer FIFO. The audio callback drains it without a mutex. Overflow requests panic after queued messages are drained, so a dropped note-off cannot leave a note held. System-exclusive messages are ignored before constructing an allocating MIDI message.

## Host formats and release boundary

[Apple documents Audio Unit support in Logic Pro](https://support.apple.com/en-gb/102082) and [Audio Units in MainStage](https://support.apple.com/en-ie/109315). TopBrass therefore builds an AU music device as well as VST3 and a standalone application. This does not imply every host or operating system has been tested.

[JUCE's CMake API](https://github.com/juce-framework/JUCE/blob/9.0.4/docs/CMake%20API.md) supplies the plugin wrappers. JUCE 9.0.4 is pinned to commit `3fb5cbc0340758596dac39f7c8de47d562b4896e`. [JUCE's license](https://juce.com/get-juce/) remains a separate distribution obligation from the CC0 recordings. [Steinberg publishes the current VST SDK under MIT](https://www.steinberg.net/developers/vstsdk/), subject to its copyright and trademark terms.

The implementation targets a reviewable local instrument. Convincing pop realism requires auditioning the section and solo recordings, comparison against a reference performance, and better recorded articulations where the current bank falls short. Host validators prove parts of plugin compatibility, not musical realism.

## Musical range and remaining recording work

The requested reference range includes Lettuce, Tower of Power, soul parts associated with Amy Winehouse, and Stevie Wonder, spanning punchy, mellow, and heartfelt playing. The four starting presets adjust articulation, instrument balance, tone, attack accent, and release. They are starting points, not artist emulations or evidence of equivalent realism. Recorded transitions and player articulation remain the decisive gap for expressive lines.

## Baritone recordings and playback

The Bear Sax import is pinned to `7abb3c652525a15dfac80e1b5dfbba9964ee568f`. It adds 64 sustain recordings, 448 staccato recordings, 28 subtones, and 28 growls. Source mapping files and Git blob hashes are recorded alongside each imported entry. The upstream CC0 license and attribution ship in the bundles.

The importer reads `<group>` and `<region>` inheritance from four specific SFZ files. It uses numeric `pitch_keycenter`, including the SFZ default of 60, rather than the WAV unity-note field. Some WAV unity notes disagree with the map. A sounding-pitch audit of all 64 sustains verifies the imported roots in a stable half-second window. Run `scripts/audit_baritone.py` to repeat that check.

The baritone's 64 sustained WAVs have authored forward loops. Preparation retains those points, translates inclusive RIFF ends to exclusive ends, and adjusts for trimmed pre-roll. It preserves the audio at the loop seam and applies no additional loop crossfade. In version 0.3 the other 120 sustain recordings retained their matched crossfades. TBBANK02 stores a separate crossfade length so these two loop treatments remain explicit.

The renderer wraps interpolation taps over native loop boundaries, including loops ending at the WAV's final frame. A regression test first reproduced premature note termination at that boundary and now checks continued sound beyond the original file duration.

The baritone's short-note velocity bands retain the author's 1–40, 41–70, 71–100, and 101–127 divisions. Actual alternatives rotate deterministically. Sustain layers are currently selected by note velocity in TopBrass. This differs from the upstream player's CC1 dynamic crossfades; TopBrass retains CC1 for delayed vibrato and CC2/CC11 for expression.

Baritone marcato layers a short recording with a sustain whose envelope waits 100 milliseconds and rises over 150 milliseconds. It is not a distinct recorded articulation. Recorded growls cover roots 37–50, and subtones cover roots 39–52. TopBrass uses ordinary baritone sustains outside these ranges. The noisy recordings have no authored loops and end naturally. This avoids claiming that pitch stretching or a periodic loop supplies missing performances.

## Further brass sources

[Libre Wave Bell & Bone](https://librewave.com/product/bell-and-bone/) is a dry jazz trumpet and trombone source with idiomatic articulations. The user supplied version 2.1.0 in Downloads. `scripts/extract_bell_archive.py` extracts its four FLAC payloads without transcoding and records archive and payload SHA-256 hashes in `assets/cache/bell-and-bone/extraction.json`. Its [sample license](https://librewave.com/software-license/) is CC BY-NC-SA with additional musical-work and loop permissions. The source license is retained with the extracted recordings. It is not a CC0 asset. Version 0.4 integrates sustain and staccatissimo recordings from it.

`scripts/audit_bell_samples.py` checks the published maps at commit `36139c0bb0983416c6bb3f856a6f9241d81c0c0b` against the archive. Every mapped span is contiguous and the total matches each of the four FLAC lengths exactly. This establishes a structural correspondence, not equality with the archive's embedded maps. Public project metadata says 2.0.1 while the archive says 2.1.0. The importer retains every velocity/key mapping zone, including repeated filenames stored at different monolith offsets, and deduplicates only after comparing decoded audio hashes. Sustain and staccatissimo extracts roundtrip exactly through 24-bit WAV. They retain authored start/end crops and have no additional normalization or processing. Raw source auditions are in `artifacts/bell-and-bone`.

The three sustain groups are dynamic layers, not round robins. The source maps provide authored loops and release positions within each sustained recording. Integration must preserve those distinctions; a plain nearest-note/velocity import would discard part of the library's playing behavior. Missing access to one source library does not block independent engine, performance, source, or articulation work.

The [University of Iowa Musical Instrument Samples](https://theremin.music.uiowa.edu/MIS.html) expressly permit any-project use without restrictions and provide direct AIFF downloads, including [trumpet pitches](https://theremin.music.uiowa.edu/MIS-Pitches-2012/MISBbTrumpet2012.html). That supplies another accessible recording source. It has not yet been compared or integrated, and access alone does not establish that it matches the desired pop articulation range.

## Version 0.3 source fidelity

The rejected version started each voice's gain at zero and smoothed it over eight milliseconds. It also added an attack ramp and always passed output through a nonlinear saturator. Those decisions altered the recordings before the user could evaluate them.

The new playback path initializes gain and pan at the first rendered frame, after same-sample MIDI controls, retains smoothing for later control changes, and applies no extra attack by default. The deliberate marcato sustain envelope remains. Audio below 0.95 full scale passes unchanged through the output stage. A continuous knee bounds higher peaks.

Explicit short recordings now finish naturally after MIDI note-off, pedal release, or controller reset. Mute, retrigger, stealing, and panic still terminate voices. Sustained recordings continue to use the release envelope. This changes playing behavior; the source performance determines the duration of a short articulation.

Native-pitch null tests compare five instruments against their packed source samples with neutral tone and accent, fixed gain, and left, center, and right pan. Passing those tests demonstrates fidelity to the packed waveforms. It does not qualify the recordings, their preprocessing, or their musical performance as realistic.

Continuous quiet/loud layer blending was deferred. The measured baritone layer correlations include negative values, and per-file trimming changes some relative timing. Adding an unvalidated blend would introduce another possible timbral alteration.

A scheduled voice that has rendered no frames is cancelled directly on retrigger, stealing, mute, or sustained-note cancellation. It does not create a release tail. Regression tests cover same-frame controllers, duplicate triggers, and zero-duration sustained notes.

## Version 0.4 playback

The mapped bank has 1,607 recordings: 86 tenor saxophone, 568 baritone, 87 tuba, 485 trumpet and 381 trombone. Its 1,190 trumpet/trombone zones reference 866 unique recordings. The bank separates immutable PCM, authored gain envelopes and mapping-zone gain/tuning. The player honors key/velocity zones, advances all three sustain dynamic layers, blends adjacent layer weights linearly, and uses initial velocity until a CC2/CC11 expression event arrives. Expression also controls amplitude. This is a TopBrass control rule, not a claim of identical vendor performance-patch behavior.

Authored loop crossfades use the pre-loop-start region and wrap to loopStart. Note-off and pedal-up crossfade into finite recorded release regions without allocating another voice. Gain tables retain source timing, apply after loop mixing, and follow recorded release position. Source-position lookup replaces the vendor’s block-ramped gain approximation. Recorded release tails receive stealing priority ahead of held notes. The fixed limit is 128 sample playheads plus 16 steal tails, so one held brass note consumes three playheads. Recorded falls, connected note transitions, and additional brass articulations remain unfinished; the global Fall control still produces a synthetic pitch fall.


## Version 0.5 recorded falls

The Fall control now selects Bell & Bone staccato-fall recordings for trumpet and trombone. The source manifest groups1–4 are four alternative takes. Authored key/velocity regions and tuning are retained, with the source articulation’s -3dB level applied once after sample-map gain. Trumpet uses velocity bands0–63 and64–127; trombone uses0–84 and85–127. Each fall uses one finite playhead and keeps its recorded ending after note-off. The synthetic pitch-fall path is disabled for those recordings. Saxophone and tuba retain the prior synthesized fall.

The active bank has2,050 recording buffers and1,658 zones. The443 additional fall buffers include260 trumpet and183 trombone. One trombone filename has two different authored crops, both retained. Sample articulation4 is separate from the host Fall choice2 and baritone subtone/growl IDs2/3. Older v0.4 binaries intentionally reject this expanded bank domain; rebuild/update bank and binary together. The bank’s binary layout remains TBBANK03.
