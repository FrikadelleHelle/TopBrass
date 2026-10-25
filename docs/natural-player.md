# Natural-player sound audit

The user wants the whole section to sound like nearby horn players, with breathing, key/valve noises and natural embellishments. The supplied mixing transcript discusses deliberate articulations, slurs, dynamics and a produced pop chain with compression, saturation, width, EQ and reverb. Preserve a trustworthy dry instrument first; treat the production chain as an optional later mix choice. Listening feedback and access to an individual library do not block independent work.

## What TopBrass changes

- Through v0.5.1, import of VCSL/VSCO/Bear musical samples averaged stereo channels to mono, trimmed pre-roll/tails, faded edges and independently peak-normalized recordings to0.65. Version0.6 replaces that preparation: both original channels, all frames and unity recording gain are preserved. Baritone retains authored loops in original coordinates; tenor/tuba loop matching scores both channels independently and requires their joint minimum correlation to reach0.85. Loops still alter sustained playback after the original attack.
- Bell & Bone import: authored sample start/end crops, retained key/velocity mappings, gain/tuning and gain envelopes. Its PCM is not independently normalized or downmixed by this importer; these archive chunks are mono. The supplier describes the library as dry and its dynamic layers as phase-aligned. That does not establish that its recordings are unedited or un-denoised.
- Playback: sample transposition/interpolation, sustain loops, dynamic-layer blending, gain/release envelopes and panning. Tone is a700Hz split with a large high/low tilt at extreme settings. Attack accent increases the first part of a note by up to25%. CC1 adds synthetic vibrato. A safety curve acts above0.95 output amplitude. Trumpet/trombone falls are recorded; sax/tuba falls are synthesized.
- There is no compressor, reverb, chorus or stereo-widening effect in the current TopBrass source path. These are absent from our player, not a claim about processing baked into third-party recordings.

At the live audit, the open patch had Tone=-1 and Attack accent=1. That is substantial coloration. New Uncolored section/solo presets set both to0, and fresh instances now default Attack accent to0. Existing saved settings and colored presets remain intact. Set external CC1/mod wheel to0 for a neutral comparison. These presets bypass our coloration; they do not restore absent source detail or undo the supplier’s recording edits.

## Concrete source concerns

`artifacts/natural-details/import-audit.json` records representative original-channel measurements. One tenor sustain had channel correlation0.457 and a mono RMS reduction of1.47dB relative to average channel power. One tuba sustain had correlation-0.283 and a4.39dB reduction. The mono fold therefore cancels some audio before peak normalization. This is a reason to correct the import, not proof of the cause of every perceived artifact. These are individual recordings, not a full-corpus conclusion. Preserve original channel data or select a justified close microphone; do not silently sum and then normalize away the level change.

Independent peak normalization also changes original loudness relationships between recordings. A source-preserving import should retain recorded dynamics and quiet incidental sounds rather than forcing every file to the same peak. Compare original, prepared and neutral-player audio at matched musical-note gain before selecting the replacement.

## Recorded detail that is available

`scripts/audit_bear_details.py` fetched and checksum-verified153 original baritone WAVs:152 fingering recordings plus one airflow recording. Its249 resolved SFZ regions include248 mechanical mappings. The catalogue is `assets/cache/bear-details/catalog.json`; source-level examples and CC0 license are in `artifacts/natural-details`. The WAV bytes are unchanged, with no normalization, trimming or downmixing.

The mechanical maps trigger close sounds on note-on and open sounds on note-off, with four alternatives. Source calibration is-12dB before the source controller fade. Coverage is MIDI36–68 except52 and64; some octaves intentionally reuse recordings. The airflow sample is an under-note noise layer, not a player inhaling before a phrase. Version0.7 imports the152 mechanical recordings into a separate bank with248 mappings. The airflow recording remains outside playback. These recordings belong to this baritone; do not label them trumpet, trombone or tuba noises.

The supplier maps fingering noise toCC121, which TopBrass correctly uses for MIDI Reset All Controllers. Implement dedicated controls without copying that conflict. Noise must not sound after panic, canceled pending notes or disabled instruments. Preserve source-relative levels rather than normalizing the quiet recordings into prominent effects.

## Humanization priorities

The current section starts enabled instruments from the same incoming note time and velocity. It does not yet model player-specific phrase breathing or connected-note/slur behavior. Those are performance gaps that mix effects cannot fill.

Preserve tight intentional section hits and the intended dynamic band. Add meaningful detached/slurred behavior, breath gaps and pressure-driven timbre, independent parts and recorded repetitions. Use constrained player variation after that; blanket random timing, velocity and pitch wobble can weaken a convincing performance. The main notes already contain performance variation, which should be preserved before layering more movement.

## Acceptance for the import correction

The current bank-based source-null tests cannot catch destructive choices already baked into the bank. The replacement import must be compared against independently decoded original pinned WAVs: both channels, frame/sample-rate identity, one documented instrument gain, relative dynamics and native loop points. Test separate left/right impulses, neutral center playback, fractional loop/release alignment and old-bank compatibility. Measure pre-roll before choosing a playback-start coordinate; preserve original PCM rather than deleting quiet lead-ins. Score generated loop seams jointly across both channels. Stereo retention increases memory and interpolation work, so qualify the mixed section and worst-case stereo load.

The supplied [Bear Sax source](https://shop.karoryfer.com/pages/free-bear-sax) documents recorded breath/mechanical noises. [Bell & Bone](https://librewave.com/product/bell-and-bone/) documents dry recording and phase-aligned dynamics. Those descriptions do not establish an untouched close-microphone signal.


## Version 0.6 source preservation

The independent verifier checks all741 original CC0 recordings against the pinned WAV Git blobs, channel counts, sample rates, frame counts and exact PCM. It does not compare the bank against the importer’s own report. All source-relative note, take and dynamic-layer levels are retained. Bell’s1309 recording payloads, gain tables and1658 zones remain byte-identical to the previous bank; their supplier-authored gain/crop behavior remains active.

At an amplitude threshold of0.0001 (−80dBFS), the longest original onset across each corpus is0.604ms for tenor,0.522ms for tuba and0.930ms for baritone. These measure first detectable audio, not perceived musical attack. Playback starts at source frame0 without destructive trimming.

Stereo uses a balance control: center preserves both channels at unity, and moving toward either side attenuates the opposite source channel. This retains the recorded microphone signal; it does not create a close microphone from a distant recording. Mono sources retain their existing equal-power pan law. Separate filter histories prevent left/right leakage, while shared positions keep loops and release crossfades aligned.

The first-note output-gain regression was reproduced through the actual processor before its fix. Current settings are now applied before engine preparation, avoiding an unintended startup gain ramp while preserving subsequent automation smoothing. A separate processor comparison decodes a pinned original stereo WAV through JUCE and tests its first4096 frames from a frame-zero note with neutral controls at native pitch/rate.


## Recorded baritone keys

Version0.7 adds a dedicated Baritone keys parameter and a Baritone with keys preset. Existing projects and fresh instances default to0. The preset enables half the authored mechanical amount. At100%, the original recordings retain their−12dB source calibration, then follow the horn’s part/output level, pan, CC7 volume and the engine’s note-velocity curve. No individual normalization, EQ, Accent, pressure expression, pitch-wheel bend or synthetic vibrato is applied to these recordings. The control is disabled while tenor is selected. These recordings are not an inhalation and do not add mechanical sound to the other instruments.

`TopBrass.keys` is a separate TBKEYS01 resource:152 full mono recordings and248 mappings, leaving the musical bank unchanged. Playback rotates the four authored random alternatives deterministically. Missing keys52/64 stay missing. Some source groups omit pitch_keycenter and use the SFZ default60 ([SFZ specification](https://sfzformat.com/opcodes/pitch_keycenter/)); the importer resolves that default rather than reading a musical root from the filename.

Physical key gestures are stored separately from musical playheads. A note arms one close/open pair only after a mapped baritone musical note can play. Its target pitch, velocity and take are captured at onset, so release still works after a short note finishes or the octave control changes. Pedal holds do not defer or duplicate physical open noises. Zero-duration/pending notes are canceled. Mute, changing sax voice, amount0 and MIDI all-notes-off clear armed gestures; immediate panic also clears mechanical playback. CC121 retains its MIDI Reset All Controllers meaning.

Mechanical playback has32 dedicated voices and four short stealing tails. It cannot steal a musical voice. Full-source PCM and source-mapping checks are independent of the builder’s report. The actual processor compares its added close/open signal with an independently decoded original WAV, including with musical Tone, Accent, CC1 and pitch wheel enabled. This establishes the recorded-layer behavior, not the realism of the entire section. Airflow, inhalations and connected phrasing remain separate work.


## Recorded baritone airflow

Version0.8 adds Baritone airflow, default0, and a Baritone close detail preset with keys/air amounts0.5. The original mono PCM and its authored RIFF sustain loop (39492..241156, exclusive end,44100Hz) are stored unchanged in a separate TBAIR001 resource. The independent verifier reads RIFF loop data directly rather than using the importer’s loop helper.

This is adapted recorded-air playback, not a full SFZ interpreter or a physical breath model. One stream per MIDI channel follows the maximum eligible musical envelope at each audio frame. This avoids doubling air for marcato layers and overlapping notes, preserves continuity across connected notes, and ends air after a held short recording finishes. Pedal-held sounding sustains keep driving it. Keys36..68 are eligible, including52/64, which lack mechanical mappings. Newest eligible note chooses the source’s10% pitch tracking, with8ms rate smoothing. Output gate uses5ms attack and20ms release; part/output level, pan, CC7 and CC2/11 expression also apply. Note velocity does not scale the air. Immediate panic clears it.

At amount1, the source maximum air amplitude is0.40. PCM is not normalized, EQed or saturated separately, and does not receive musical Tone, Accent, CC1 vibrato or pitch-wheel changes. The supplier’s CC1 means dynamics, whereas TopBrass CC1 means vibrato, so copying that binding would be incorrect. Its source random LFO/tremolo controller network is not reproduced. See [SFZ amplitude](https://sfzformat.com/opcodes/amplitude/) and [loop behavior](https://sfzformat.com/opcodes/loop_mode/) for the source conventions. No inhale is played in rests, and these baritone recordings are not applied to tenor, trumpet, trombone or tuba.

The module’s nonconstant waveform oracle exercises fractional44100→48000 playback, authored-loop wrapping and a pitch-target change. Engine checks cover per-frame gates, pressure, note-velocity independence, overlap/marcato, source range, sustained pedal, short completion, panic and block partitioning. The actual processor’s added-air signal is compared with a JUCE-decoded original WAV at native rate and root, with musical color/vibrato/bend enabled separately. The MIDI allocation trap enables keys and airflow, and the benchmark includes16 simultaneous airflow streams. These prove the added layer’s behavior, not whole-section realism or true connected-note brass transitions.

## Tenor sustain preservation

Version0.9.2 changes the generated loops for all46 tenor sustains. At native pitch/rate, the first crossfade now begins between4.00 and6.35seconds, with a median4.45seconds, rather than the prior median0.91seconds. Original PCM, channel balance, gain, note mappings and the other instruments are unchanged. The reproducible builder produces the same bank bytes as the earlier offline later-loop candidate.

The search retains the original40ms crossfade. It first looks for a jointly correlated match to the early stable recording, then tries later starting regions when needed. Both channels must reach correlation0.85 and their endpoint RMS ratios must reach0.90. It rejects a recording with no qualifying late match instead of silently reverting to a short early loop. Tuba keeps its previous search because its source durations and usable sustain lengths vary; baritone and Bell & Bone keep their authored loops.

The source continues unchanged for at least four native-pitch seconds. Transposing a sample changes that duration in playback time. Later looping still replaces part of the performance, and the tenor's release remains the existing fade rather than the original recorded ending. A longer loop is not a legato transition or a claim of convincing realism.

## Recorded tenor and tuba endings

Version0.11 maps the original endings of all46 tenor and23 tuba sustains into the existing stereo release path. PCM, source levels, tuning, channels, loops, other recording headers and mappings remain unchanged. The new metadata changes only release start/crossfade coordinates. No additional audio is resident in memory.

The selector uses20ms joint-channel RMS blocks. It finds the first local160ms-median drop of at least8dB after the loop, rejects a later rebound within6dB, requires an18dB terminal decay after500ms or at the final block for shorter endings, and excludes transitions whose preceding reference is already below−36dB of the source peak. It retains80ms of preceding context and uses a40ms linear crossfade with one shared stereo position. A source with no qualifying ending retains the generic fade. These are envelope heuristics, not annotations of breath, mechanical noise or acoustic note-off.

The earlier global-peak threshold mistook quiet tuba playing for an ending. In the D#1 soft recording it selected2.84s, retaining most of the sustained body. The local rule selects7.28s near the final decay. The complete offline comparison is in `artifacts/recording-audit/release-tail-local`; the earlier experiment remains separate.

Natural note-off and pedal-up now play the recorded endings. New section hits, part mute, stealing and panic still use their existing interruption behavior. Baritone keeps its existing fade and recorded detail layers. Synthetic sax/tuba falls retain their generic release instead of jumping from the pitched fall into a natural sustain ending. The Release knob controls those fades and forced stops; it does not stretch or shorten an original ending.

Three soft tuba sources have nonzero terminal offsets, with EOF steps10–12times their largest internal sample difference over the final100ms. The player applies one stereo-shared linear output ramp over the final3ms of tenor/tuba release source frames. Stored PCM stays exact, and the preceding performance is unchanged. The ramp follows source position, so it remains consistent across pitch/sample-rate conversion and buffer sizes. It does not affect short recordings, mapped Bell endings or ordinary sustain loops. Three milliseconds is an engineering choice; audibility of the original steps has not been established.

The dry comparison tool captures the complete mapped ending, rather than always stopping250ms after note-off. Source identity and playback fidelity do not establish that these recordings meet the musical realism brief.

## Recorded brass double tonguing

Version0.10 adds472 Bell & Bone double-tongue recordings:264 trumpet and208 trombone, with source RR groups9–12 and velocity bands0–63/64–127. A separate switch per brass strip applies only to Short / punch; existing projects and presets leave both switches off. Two neutral solo starting points expose the figures. Their second attacks retain their recorded timing. These are finite performed gestures, not tempo-synchronized section patterns or synthesized pulses. The timing audit's coarse20ms energy landmarks vary across the two instruments, so a tight paired second attack is not claimed.

The musical bank now has2522 recordings and2130 zones. Every prior2050 recording header/PCM payload and1658 mapping is an identical prefix. The new cropped PCM uses about28.92MiB; each triggered figure consumes one playback stream. All741 pinned CC0 originals are still retained exactly. Bank layout remains TBBANK04, but sample-articulation ID5 requires the updated binary; install bank and binary together. Older formats remain readable, and unsupported instrument/gesture combinations are rejected.

The source's authored start/end crops, normalization scalar, pitch corrections and velocity/key/RR maps are preserved. The public Manifest assigns no articulation gain, and ConfigurationHandler defaults that to0dB. Fall's−3dB is not copied. This is not an exact vendor-player recreation: its neighboring-note borrowing, global random round robin/reset and1000ms release envelope are not reproduced. TopBrass uses deterministic per-target alternatives and lets the original figure finish after note-off, with ordinary retrigger, mute, stealing and panic interruption.

The original three-choice Articulation parameter and its normalized values are unchanged. New controls append after the original32 parameters with AU version hint2, following JUCE's later-parameter ordering rule for Logic/GarageBand. The UI disables them under Sustain/Fall. Playable-range lookup and sample selection now share the Engine's articulation resolver.

## Explicit recorded tenor detail

Version0.14 adds the verified MTG SoloSax tenor breath/key recordings as explicit one-shot sounds. They come from a different tenor recording session than the main VCSL musical notes. The source labels64 clips as breath and33 as key clicks; this does not establish inhalation direction or justify adding one before every phrase. Complete mono PCM and native rates are retained in the separate `TopBrass.tenor` resource. Its14,916,728 decoded PCM bytes are included in Bank memory accounting. Pinned source Git blobs, complete frames and decoded PCM are checked independently of the builder manifest. Source attribution, preparation readme and CC BY4.0 license ship with the plugin.

The Tenor breath and Tenor keys buttons advance separate deterministic take counters. They work in Layered, Relay and Groove, with the sax strip enabled and Tenor selected. Main horn notes do not trigger these sounds automatically. Detail level defaults to0dB, retaining source amplitude before part/channel/master gain. The recordings are quiet; the gain control can raise them for audition. Detail gain, sax level/pan, CC7 and output apply. MIDI note velocity, CC2/11 pressure, musical Tone/Accent, synthetic vibrato, bend and octave settings do not alter their pitch or timbre. Subsequent gain/pan changes retain existing8ms control smoothing.

Tenor detail MIDI defaults off. Enabling it assigns CC20 to breath and CC21 to keys. Each controller triggers only on a transition from0 to a nonzero value. Edge state is per channel and per controller; resets/panic rearm it. Layered mode honors the sax channel filter, while section modes retain their existing direct-player routing. The MIDI sample offset is preserved. GUI buttons route directly through the fixed UI-event FIFO, independently of the fifteen-key note window. Queue overflow retains the existing drain-then-panic behavior.

Eight dedicated voices and two short stealing tails cannot take a musical voice. Finite recordings include the last frame and clear at exact block-edge EOF. Sax mute or changing sax voice fades active detail over3ms, and immediate panic stops it. Channel-specific all-notes-off respects ownership. Turning MIDI triggering off disarms further controller triggers; already sounding recordings finish unless stopped separately. No automatic breath scheduling, coherent recorded legato, trumpet valve noise or tuba/trombone mechanical noise is claimed.

The new parameter indices34/35 append after the existing controls. Their AU version hint3 sorts after the existing version2 brass tonguing controls. Existing projects restore MIDI triggering off. Playback source identity, realtime allocation checks, controller behavior and actual AU/VST captures qualify the implementation separately from musical realism.
