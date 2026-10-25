# Validation boundary

The tests run against the decoded recordings and the production engine. They were added after the first implementation, rather than through a test-first process.

`Tests/Verify.cpp` checks that each instrument and articulation renders finite audio, released notes end, pedal-held notes persist, disabling instruments fades their voices, MIDI channels route independently, and panic respects its channel. It compares rendering a phrase as one block against many smaller blocks. It traps C++ allocations around note selection, stealing, controllers, and rendering, checks the voice bound, and exercises multiple sample rates. Bank loading must reject corrupt data without replacing a valid bank.

The engine benchmark records the actual active sample playheads for one, eight, and 32 section notes at 48 kHz in 128-sample blocks. CC1 vibrato is enabled for the latest benchmark. It records mean, p99, maximum render time, and the mean share of the 2.667-millisecond block deadline in `artifacts/performance.json`. These are single-process engine measurements on an Apple M2 Pro. They exclude the DAW, its other plugins, driver latency, GUI work, and live MIDI dispatch. A passing benchmark is not a guarantee against live audio dropouts.

`Tests/HostVerify.cpp` instantiates the actual processor and editor. It verifies resource discovery, MIDI sample offsets, parameter state recall, invalid-state preservation, panic, onscreen notes, FIFO overflow recovery, factory preset loading, and an editor snapshot. The screenshot is `artifacts/plugin-ui.png`.

The section and individual short-note WAVs use the same engine as the plugin. `sustain-and-falls.wav` presents each instrument's long note followed by a synthetic fall. These files support listening checks. Their existence and finite samples do not establish musical realism.

Apple's `auval` and Tracktion's [pluginval](https://github.com/Tracktion/pluginval) provide independent plugin compatibility checks. Run `scripts/validate.sh` after installing the current build. Set `PLUGINVAL` to the pluginval executable to include its tests. The script first enumerates AUs because discovery can lag after a fresh install. It preserves validator logs in `artifacts`.

The initial installed build passed Apple's AU validator, pluginval level 5 for AU and VST3, and pluginval level 10 for VST3. Changes made after that run require revalidation. The final run results are recorded separately in `artifacts/validation-summary.json`.

Logic Pro and MainStage sessions have not been exercised here. Neither application was found in the inspected `/Applications` and user Applications directories. AU validation is a compatibility check, not a substitute for opening a saved session and performing a live set in those hosts.

The following remain musical acceptance work:

- Compare solo notes and section riffs against Lettuce, Tower of Power, Amy Winehouse-style soul parts, and Stevie Wonder-style arrangements.
- Listen for pitch imbalance, loop repetition, release behavior, and tone changes across dynamic layers.
- Evaluate long expressive lines with expression and delayed vibrato, including low trombone and tuba notes.
- Compare close-recorded pop players where the current orchestral-source brass does not produce the required attack or tone.

A purpose-recorded bank should capture the intended players and room, soft through aggressive dynamics, multiple repetitions, short and accented lengths, releases, slurred intervals, falls, scoops, and growls. That recording work or acquisition of an explicitly redistributable equivalent remains separate from the functioning plugin implementation.

## Prior installed checks

The final installed binaries and bank files match the build by SHA-256. Apple AU validation, pluginval level 10 for VST3, pluginval level 5 for AU, the then-current CTest executables, and the UndefinedBehaviorSanitizer engine run passed. Preset loading and independent tuba toggling were also exercised in the running standalone app through its accessibility controls.

AddressSanitizer did not reach `main`. A process stack sample showed its runtime recursively waiting for initialization during shadow-memory setup through macOS dyld and malloc. The process was terminated. This is not an ASan pass. `artifacts/sanitizer-sample.txt` preserves the observed stack. UndefinedBehaviorSanitizer ran in a separate build and completed.

## Version 0.2 checks

The bank expansion adds tests for all five sax voices, layered marcato, finite growl duration, documented upper-register fallback, deterministic rotation of recorded alternatives, native sustain loops, and release on changing sax voice. A synthetic bank isolates the loop-at-EOF regression. Real baritone sustained notes are also rendered beyond their source WAV duration.

The performance report now includes four note-ons and four note-offs per 128-sample block while rendering a section with baritone marcato. It also records thread CPU time separately from elapsed wall time, including wall-clock deadline misses. A normal command-line process can be preempted; a green p99 check does not erase a maximum-time deadline miss.

`artifacts/v0.2/baritone-pitch-audit.json` records the pitch audit and imported loop endpoints. The measured error covers one stable window per sustain sample, not every moment of its performance. Musical listening and Logic/MainStage session qualification remain open.

Version 0.1 state compatibility is tested after selecting a baritone voice: a saved state without the new parameter restores the tenor and retains routing. JUCE already supplies the missing parameter default, so no additional migration code was introduced. Every factory preset also renders through the actual processor in the test.

## Version 0.3 source-fidelity checks

`Tests/NaturalPlayback.cpp` first failed against the rejected engine. It compares native-pitch playback against the actual packed recording, including attack and initial pan. It also compares held versus released short recordings, including pedal and controller reset paths, checks mute/panic, and drives the output knee on both channels. These checks establish source fidelity and bounded output, not listening realism.

The sound comparison script now compiles the current engine and the archived rejected v0.2 engine without changing either installed bundle. It renders identical native-pitch notes, with and without the demo's short gate. Comparison files present the rejected player, current player, then original source, with energy matching. The old snapshot comes from `artifacts/v0.2-rejected/source.tar` when available.

The source-null cases cover both short and sustained attacks at left, center, and right pan. Pending-voice regressions first failed during the revision and now verify cancellation before the first rendered frame and dense same-frame MIDI stealing.

The core riff renderer now sends individual note-offs for its musical notes. The earlier demo used the force-stop helper, which would bypass the new natural-short behavior. Deliberate panic and diagnostic-reset calls retain the force-stop helper.


## Version 0.4 checks

All four production test executables pass. `Tests/MappedPlayback.cpp` first failed against the prior bank loader. It verifies mapped key bounds, simultaneous dynamic layers, expression, pre-start loop wrapping, authored release playback, pedal, zero-duration cancellation, and gain envelopes with nonzero source offsets through a loop and release. A separate saturation regression first reproduced incorrect stealing of held notes ahead of recorded release tails and passes after correcting their lifecycle classification.

Source fidelity checks now distinguish legacy raw playback from mapped brass playback with authored tuning and gain. The actual mapped source comparisons had maximum errors below 4e-8 in the tested attack windows. Natural short endings, mute and panic remain covered. The main engine test counts physical playheads rather than assuming one sample per instrument. Allocation trapping and host-block subdivision checks pass with the mapped bank.

The full preparation command reproduced bank SHA-256 `b6dafcf00891c9a2579129481cc657a59e7e0fea173f960a57d6a2d3ccee4a87`. It has 1,607 recordings, 1,190 mapping zones and 19 gain envelopes. The baritone audit retained 64 native loops with maximum measured pitch error 2.115 cents in its chosen windows. The mapped, source-fidelity and engine executables passed UBSan.

The installed v0.4 AU, VST3 and standalone executables and banks match the build by hash. The standalone was restarted after finding its old executable still loaded from the backup; the new process displays 521.889 MiB of decoded samples and retains the saved controls. Apple AU validation, pluginval level10 for VST3, and level5 for AU pass. Exact hashes and logs are in `artifacts/v0.4/validation-summary.json` and adjacent files.

The latest CLI benchmark measured 128 physical playheads at 269.856 microseconds mean and 418.75 microseconds p99 per 128 frames at48kHz. Maximum wall time was7,536.17 microseconds, with seven measured blocks exceeding the2,666.67-microsecond deadline; maximum measured thread CPU time was1,468.21 microseconds. The green p99 gate does not qualify those outliers or establish live DAW dropout behavior. Dense MIDI plus rendering measured507.75 microseconds p99. Actual Logic/MainStage session qualification and musical acceptance remain open.

`artifacts/v0.4/section-performances` preserves full funk/soul renders and exposed stems from the actual processor, with exact parameter XML and file hashes. These are new listening candidates, not accepted realism evidence.


## Version 0.5 checks

The recorded-fall fixture first failed against the prior loader. It now checks one-shot selection, natural endings after note-off, no additional synthetic pitch fall, pedal/reset behavior, mute/retrigger/panic, and rejection of unsupported instrument/articulation assignments. The baritone articulation IDs remain unchanged. The original TBBANK02 bank also passes the complete engine verifier with its914 recordings.

Mapped source comparisons exercise both instruments at their different velocity boundaries, four RR takes and three pan positions. The processor verifier renders `fall-audition.mid` and checks that the Fall control selects one recorded playhead. Full fall WAVs and parameter XML are in `artifacts/v0.5/section-performances`. These checks establish playback and source mapping, not artistic realism.

The active bank is2,050 recordings and1,658 zones,589,349,456bytes on disk. The full preparation command reproduced the candidate bank exactly. All four production test executables and the mapped/source/engine UBSan executables pass. The installed AU and VST3 pass Apple AU validation, pluginval level10 VST3 and level5 AU; exact installed hashes and source fingerprints are retained in `artifacts/v0.5/validation-summary.json`.

The latest CLI run measured128 physical playheads at265.768microseconds mean and540.625microseconds p99 per128frames at48kHz; maximum2,158.88microseconds, with zero measured render-block deadline misses in this run. Dense MIDI plus render measured974.041microseconds p99 and2,718.67microseconds maximum against the2,666.67microsecond deadline. The p99 gate passes, but the dense-MIDI maximum exceeds one deadline. These remain command-line measurements rather than live-host qualification. Connected phrasing and musical acceptance remain open.


## Version 0.6 source preservation

The independent original-source check first failed on the prior bank’s channel count. It now verifies all741 original CC0 WAVs by Git blob, exact PCM, channel count, frame count and sample rate. `Tests/StereoPlayback.cpp` first failed against the old loader and now covers separate channel impulses/polarity, neutral balance, separate filter state, fractional loops and recorded releases at96kHz, unsupported channel counts and truncated right-channel data. Historical Bank02/03 source-playback checks also pass.

The actual-processor startup-gain check was run red before fixing initialization. A separate original-WAV oracle uses JUCE’s decoder and a frame-zero native-pitch note; maximum error over4096 stereo attack frames was2.7002e-10 at the intended−30dB gain. It exercises one original tenor attack, not every note and dynamic layer. Core tests cover source windows and mapped brass separately.

All six release CTest checks pass. The four C++ core executables and the independent Python source verifier also pass in the UBSan configuration; the processor was not included in that sanitizer build. Stereo retention adds124,478,564bytes to the bank. PCM and gain tables occupy713,625,420bytes; the actual-processor/editor/render harness measured729,382,912bytes peak RSS on this machine. The latter is a measured process high-water mark, not a guaranteed DAW footprint.

The benchmark adds128 simultaneous stereo tenor playheads after the three mixed-section scenarios. All use128-frame blocks at48kHz with CC1 enabled. See the versioned report for mean/p99/max and misses. A first run overlapping compilation failed the dense-MIDI p99 timing gate; that failure is retained as `artifacts/v0.6/performance-during-build-failed.json`. Subsequent release checks pass the existing gate, but measured wall-time outliers remain. Do not infer live-host dropout guarantees.

The AU, VST3 and standalone update preserves the settings file byte-for-byte and retains the prior installed bundles in the recorded backup. New dry section/stem files and exact parameter XML are in `artifacts/v0.6/section-performances`. These are listening candidates. Source transparency does not establish nearby-player realism, and recorded mechanical/airflow layers are still not integrated.


## Version 0.7 recorded keys

The key-noise fixture was written before the production API and first failed to compile because those methods did not exist; this was not a pre-implementation behavioral red run. A later pan-automation regression was run red against the unsmoothed implementation before its fix.

The fixture verifies calibrated close/open PCM, four alternatives, a physical release after musical short completion, pedal and marcato behavior, zero-duration cancellation, repeated note-off, captured octave, absent-map keys, mute and panic. It also compares a nonzero musical sentinel while saturating mechanical playback, and verifies44.1kHz mechanical PCM rendered at48kHz with authored octave reuse. The production allocation trap now loads the detail bank and enables key playback while stressing note selection/rendering. The dense MIDI benchmark also enables mechanical detail. These checks do not imply complete live-host qualification.

The original-source verifier checks all152 WAVs and248 mapping regions, including source Git blobs, exact PCM, sample rate/frame count, authored roots and−12dB gain. Source curves controlled by supplier CC100/121 are not reproduced as an SFZ interpreter: TopBrass supplies a dedicated amount control, velocity follows its existing engine curve, and CC121 retains MIDI controller-reset behavior. The four source random alternatives rotate deterministically.

The actual processor compares its added close/open signal against JUCE-decoded original WAVs, with Tone, Accent, CC1 and pitch wheel active on the musical note; the measured difference is recorded in `artifacts/v0.7/processor.log`. Baritone with keys and Uncolored baritone preset demos use identical performance parameters except for the key amount, making a controlled listening pair. No key recordings are normalized or artificially enhanced for these demos. Neither this recorded layer nor its tests establishes whole-section realism.


Key initial-volume/pan and rapid-retrigger regressions were also run red before their fixes. Controls are initialized on the first positive render, so later MIDI messages at the same timestamp apply before a recorded close/open frame. Pending cancellation affects only unplayed close sounds; it preserves a genuine open queued by an earlier played note. The allocation trap includes note-offs as well as note-ons so this release path is exercised.

One initial UBSan run failed its wall-time benchmark gate on128 stereo playheads (p99=2,826.42µs against2,666.67µs). That log is retained as `ubsan-first-timing-failure.log`; it did not report undefined behavior. A subsequent complete UBSan run passed with the same timing gate intact. Final results after the control-order/retrigger fixes are recorded separately in the versioned validation summary. Sanitizer timing and release timing remain distinct measurements.


## Version 0.8 recorded airflow

The first airflow fixture attempt failed configuration because its production .cpp did not exist; no initial behavioral-red claim is made. Source PCM and RIFF native-loop metadata are verified independently. The changing-waveform interpolation oracle measured maximum error1.81608e-8 across fractional source-rate conversion, a note-target change and loop wrapping. One initial overlap assertion required bit equality despite different interpolation fractions; diagnosis measured2.32831e-10 maximum difference with identical gain, so that assertion now uses the existing source-fidelity tolerance principle. Marcato’s isolated gate output was bit-identical.

All11 release checks pass in the initial complete run. A native actual-processor original-airflow comparison measured2.32831e-10 maximum difference. Its existing musical/source and mechanical comparisons remain unchanged. Allocation trapping includes active airflow and note-off handling. Benchmark case5 drives16 baritone players and16 air streams; its32 active playheads are counted after rendering, since airflow is intentionally initialized on the first audible frame rather than at note-on. The initial run averaged38.0372µs per128frames at48kHz, p99=54.208µs. Maximum wall time3,063.38µs exceeded one2,666.67µs deadline while maximum thread CPU time was107.834µs. Preserve that scheduling outlier; a passing p99 does not establish live-host dropout behavior.

The legacy source-comparison tool now stages/links Airflow.cpp only for the current player and leaves the archived player’s compilation independent. It ran successfully with the frozen Bank02 corpus. Versioned validation summaries carry the final release/sanitizer/plugin/installed results; Logic/MainStage sessions, inhalations and whole-section musical acceptance remain unverified or incomplete.


## Version 0.9 section performance

The part-routing and scheduler fixtures were introduced before their APIs/implementation; the initial attempts failed compilation/configuration, rather than a behavioral-red run. Explicit part routing preserves the independent sax/trumpet signal sum and scoped releases. Recorded sax mechanical gestures retain their captured target when another instrument receives the same source MIDI note. Legacy natural/mapped/key checks passed after extracting the common part-start operation.

The scheduler's same-timestamp note replacement regression was run red before treating already-due releases as reusable player slots. A bass-role fallback regression was initially contaminated by a prior test's host clock; it was corrected to prepare a fresh clock. A separately compiled copy of the prior modulo-role mapping then failed that regression; the role-preserving mapping passes. That comparison was after the role fix, not a test-first claim.

The actual processor checks the fifteen-key boundary, timestamp-preserving first lead, panic/no future notes, continuous host tempo and seek reset, same-timestamp replacement, saved-state compatibility and sample-identical section audio across512/31-frame block partitions. The common MIDI input is rendered in Layered, Relay and Groove modes with identical neutral sound parameters and current three-instrument selection for the listening comparison. The allocation trap now exercises performance planning, routed on/off events and sampler rendering together. Legacy projects restore Layered mode and MIDI60 as the window start.

Core tests and full release/sanitizer/installed validator results are recorded in the versioned validation summary. Musical quality and live Logic/MainStage qualification remain separate; no sample source quality claim follows from performance-mode tests.

## Version0.9.1 tight section correction

The prior groove deliberately passed rhythmic roles between players; the user asked for shared, tightly syncopated section hits instead. Every enabled player now participates in each authored hit, with at most2ms of onset spread at maximum Horn feel. Same-timestamp and physically staggered chord entry are tested separately; nearby chord tones do not cause repeated whole-section attacks. Fixed instrument roles replace rotating rhythmic roles.

The grouped-hit, staggered-chord and pressure/strike regressions were run behaviorally red before their fixes. Groove sustain dynamics capture a per-note strike-times-pressure policy; other playing modes retain their previous pressure convention. A real-bank comparison verifies the recorded blend against the no-pressure reference, verifies silence at zero pressure and removes level differences before asserting a timbre difference. Tests and binary/plugin qualification are recorded in the versioned summary. The later-tenor-loop research candidate is separate and is not part of this update.

## Version0.9.2 tenor loops

Loop-selection regressions were run red for early alteration and accepting a source that had already ended, then passed after requiring a late, amplitude-compatible joint-channel match. The expanded native playback check also failed against the old bank before bank replacement. It now checks all46 tenor sustains and their two recorded dynamic layers through13-second holds, repeated crossfades in both channels, and release completion. Its independent frame-position oracle retains the original source before the loop, then computes the repeated positions and crossfade directly from packed metadata. The maximum source-domain output error is recorded in the versioned log.

The generated bank is byte-identical to the prior qualified offline candidate. Only46 tenor loop coordinate triplets differ from the preceding bank; every PCM payload, gain table, mapping and other header remains unchanged. The source verifier still checks all741 original CC0 recordings against independently decoded, pinned WAVs. The versioned summary separates these source/loop checks from plugin validation and listening acceptance.

The initial0.9.2 UBSan suite passed12 checks but failed the128-stereo-playhead elapsed-time p99 gate:3483.96µs against2666.67µs. Thread CPU p99 was1308.92µs, and that run reported no undefined behavior. Its log and incomplete timing report are retained. The same engine test passed an isolated recheck without changing code or the timing threshold. Those results are combined evidence, not a claim that the initial full suite passed. Release validation passed all14 checks. Maximum-time outliers remain distinct from p99 qualification.

## Version0.10 recorded double tonguing

The old loader rejected the new articulation ID before its domain was extended. After the metadata/API fields existed but before selection was implemented, the native-source comparison failed because it still played ordinary short samples. The implemented selector passes the source comparison. A separate full-waveform oracle checks every one of472 regions, including borrowed keys, tuning, velocity bands, all alternatives, natural ending after early note-off and unsupported-instrument/other-articulation isolation. An independent Python verifier reads the pinned XML and FLAC directly rather than trusting the extraction catalogue. Existing recording headers/PCM and mapping prefixes are compared byte-for-byte.

The first release suite caught two test assumptions: an invalid-ID fixture hardcoded5, which is now valid, and the new allocation exercise left the later timing workload in Short mode. The fixture now uses the articulation-domain boundary and checks both mapped gestures against unsupported instruments and legacy format. Each benchmark explicitly selects Sustain and verifies its full expected stream count. The failed run and invalid-workload metrics are retained; they are not performance evidence. All16 release checks and15 sanitizer checks subsequently passed. Final installed/plugin/source comparisons are recorded separately.

Preset auditions now export24-bit WAVs and exact parameter XML. Double-tongue auditions use enough spacing for the original figures; section-preset MIDI stays inside its15-key window. These are listening candidates, not realism acceptance. The two switches remain off in the user's retained patch.

The final label check uses a hidden native window peer: JUCE supplies no accessibility handler for an offscreen editor. The initial fixture failed for that reason, so it was not a valid automated red run for duplicate labels. The duplicate labels were observed in the running app. After setting instrument-specific titles, the corrected processor fixture and the installed app both expose distinct trumpet/trombone controls.

A later release validation passed the15 functional checks but failed the engine elapsed-time gate: the128-stream case recorded p99=7332.42µs against2666.67µs, with thread CPU p99=1248.79µs. The same engine passed an isolated recheck without code or threshold changes. Both reports are retained; no particular cause of the elapsed-time outliers has been established. This does not qualify live Logic/MainStage performance.

Final installed AU/VST3 captures cover12 native source cases in neutral and warm settings, plus3 section scenes in both settings. All30 format pairs are bit-identical. All12 neutral source comparisons pass when the supplier's authored gain envelopes are included. The archived0.9.2 comparisons also check the unchanged articulation paths. The AU retains its original32 parameter names in their original order; new parameter version hints place the two switches afterward. Exact executable/resource hashes and comparison results are in `artifacts/v0.10.0/validation-summary.json`.

## Version0.11 sustain endings

The new native-ending fixture first failed against the0.10 bank because tenor/tuba release metadata was absent. After adding metadata, its synthetic-fall regression failed: note-off jumped into the newly available natural ending. Release selection now excludes synthetic falls, preserving their prior fade. The regression passes with that correction.

The69-source fixture compares fully crossfaded original endings at native and converted sample rates, and exercises part retrigger, mute, pedal and panic. Source-derived length now bounds normal sustain-release checks. The earlier quarter-second/one-second assumptions describe generic fades and cannot bound a full recorded ending. Forced-stop checks keep their existing deadlines.

The selector's initial test attempt failed to import its absent API, not a behavioral-red run. Five synthetic-envelope cases cover quiet playing after a loud attack, late room decay, a recovering phrase dip, unfinished/silent input and an ending before the loop. These do not label acoustic note-off or prove musical acceptance. The production bank comparison checks that only69 release-coordinate pairs changed; all PCM, gain tables, loops and zones remain byte-identical. Full release/sanitizer/installed results belong in the versioned validation summary after execution.

The final EOF-step regression first failed against the mapped endings without an output ramp. Three soft tuba sources stop above zero by10–12times their local maximum sample difference. The final3ms source-frame ramp is now an explicit exception in the playback reference, rather than a relaxed null-test tolerance. The fixture also checks borrowed notes below each instrument's lowest sampled root, converted rates and bit-identical block partitions for those endpoint cases. Pre-declick validation/capture evidence is retained separately; final installed captures must identify the final executable hash.

The final release suite passed all18 checks. The final sanitizer suite passed16 checks but failed the128-voice elapsed-time p99 gate:3110.54µs against2666.67µs, with thread CPU p99=1598.12µs. That failure and its incomplete timing report are retained. An isolated engine recheck uses identical code and threshold; its result is recorded separately. Do not combine these into a claim that the complete final sanitizer suite passed. The earlier pre-declick build had passed17 checks, but it is a different executable.

The isolated final engine recheck passed. Final installed AU validation, VST3 pluginval level10 and AU level5 passed. All30 AU/VST3 capture pairs are bit-identical, and all12 neutral native-source comparisons pass in their pre-loop windows with authored gain envelopes accounted for. Five archived0.10.0/installed0.11.0 VST3 pairs are bit-identical before MIDI note-off; their ending references explicitly include the final3ms ramp. The largest ending-reference error is4.656612873077393e−10. Stored PCM and resident decoded sample bytes remain unchanged. Logic/MainStage live sessions and musical realism acceptance remain unverified.

## Version 0.12 note selection

The old engine scanned the complete recording list three times for an unmapped selection, or scanned all zones per mapped layer and sorted up to sixteen matches. The bank now builds an immutable index at load time. It retains original sample and zone indices rather than pointers, so copied banks resolve their own storage and failed reloads retain the previous index with the previous data. The old runtime scan methods are removed; their rules remain only in the differential test oracle and frozen baseline source.

The initial selector test build failed because the new Bank API was absent. This was an API-level red run, not a pre-implementation behavioral test. After implementation, 30,818,304 actual-bank and 17,470,464 adversarial selected identities matched the v0.11 reference. Checks include all MIDI velocities, adjacent float values around layer/rounding boundaries, RR wrap including UINT32_MAX, reversed root ties, sparse-layer fallback, candidate caps, duplicate RR numbers, expressive source bounds, copies and failed/successful reloads. The initial build warning about the group take-array initializer was corrected explicitly.

`Tools/SelectionBenchmark.cpp` is compiled against the frozen v0.11 engine and current engine. Three alternating paired runs require byte-identical full float audio. No-render note-trigger batches isolate selection/routing work; the separate dense workload renders four note-ons and four note-offs per 128-frame block with recorded baritone keys and air. Paired reports and raw fingerprints are retained under `artifacts/v0.12.0/paired-performance`.

The no-render trigger measurements improve by roughly 28–99 times across six cases. Dense thread CPU mean decreases in every pair. Ratios of aggregate medians and medians of within-pair reductions are different summaries; the report keeps both raw runs so either can be reproduced. Wall-clock p99 does not improve consistently and is worse in the aggregate result. These measurements show less CPU work, not guaranteed live-host deadline behavior.

`Bank::bytes()` retains its decoded-sample semantics. `selectionBytes()` separately counts the index object and container capacities: 515,718 bytes for this bank. It excludes allocator overhead and the shared-pointer control block and is not an RSS measurement. The sample-bank bytes, plugin parameter layout, recordings and playback DSP remain unchanged. Final release/sanitizer/installed results are recorded separately after execution.
