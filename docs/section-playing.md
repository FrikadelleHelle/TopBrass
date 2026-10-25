# Section playing

Layered retains the original behavior: every enabled part receives incoming notes according to its MIDI channel filter. Section relay and Section groove accept fifteen inclusive MIDI notes. The default window is 60–74 and its lowest key is movable. Notes outside the window do not trigger; changing the window or mode cancels the old performance.

Relay assigns successive key presses across enabled players, starting with trumpet, then saxophone, trombone and tuba. Groove plays every enabled instrument on the same syncopated hits. Each instrument keeps its chord role: trumpet takes the highest held pitch, saxophone the middle, trombone a lower pitch and tuba the lowest. Muting another player does not change those roles. With one held pitch, all horns share it in their own octave settings. Patterns use only held pitches and do not infer harmony.

Section scheduling responds immediately; Humanize can add its small per-player delay. Additional chord tones arriving within12ms of an input or scheduled section hit update pending voices and the held voicing for subsequent hits, without restarting attacks already underway. Same-timestamp chords receive their complete voicing on the initial hit; a physically staggered chord cannot change an attack that has already sounded. A later key press or an intentional same-key retrigger starts another immediate section hit. A scheduled grid step within12ms after a played hit is skipped while the rhythmic grid continues. Each player has one assignment. Groove gates fade short, fall and sustained notes over 5 ms, bypassing recorded endings, the release-time control and the output sustain pedal. Input pedal still latches the held chord. Layered and Relay retain natural recorded endings.

The authored groove has nine shared hits per bar, with stronger accents and softer ghost hits. The opening gate lasts 0.28 beats; repeated gates last 0.16 beats, with a 0.12-beat final pickup, leaving space between attacks. Section accents adds a shared force and gate contour. Humanize controls player timing in every mode. With Humanize at zero, section events have no added onset delay. Gate-length variation is limited to 0.25 ms per player around the common contour. No random detuning or audio EQ is added. Each instrument's octave setting is applied, then octave folding keeps the target in its sampled register while preserving pitch class.

Playing force changes note velocity, which selects recorded short-note layers and the initial sustain blend. For groove trumpet/trombone sustains, received CC2/CC11 pressure multiplies strike velocity for the dynamic-layer blend, so softer hits still use softer recorded timbres at full expression. Pressure continues to control amplitude. This policy is captured per note; Layered and Relay retain their existing pressure behavior, and tails do not change policy when the playing mode changes. Saxophone/tuba use the recorded velocity layers available in their sources.

Host tempo is used when available. The tempo control is the fallback for standalone/no host tempo. Playing transport follows host PPQ; start/stop/seek clears old generated ownership. With stopped transport, new live notes can start a free-running performance. Tempo changes affect subsequent rhythmic boundaries; an already started note retains its scheduled duration. Sustain pedal latches the input set in section modes. It does not sustain every generated hit inside the sampler. Pedal-up, all-notes-off and panic cancel input/queued performance appropriately.

Per-strip MIDI channel filters apply in Layered mode. Section modes route directly to enabled instruments and retain the original input channel for expression, volume and pitch-wheel controls. The channel-filter controls are disabled in section modes. Source MIDI note identity is separate from arranged concert pitch, so a lower trombone note can be released correctly even after the octave control changes.

Old projects restore Layered mode; the new controls do not change its note routing. Changing the inactive section range does not reset Layered playback. Section pocket is a neutral short-note factory starting point. The fifteen-key keyboard expands to fill the on-screen playing area. The status counter is labelled sample voices because dynamic sample layers are not separate players.

The scheduler uses six fixed part slots, each with a current note/off time and a pending onset. Replacing a slot replaces its scheduled off together with the assignment, avoiding stale generated releases. Same-sample input is processed before the following audio interval; an already due release makes a player available immediately. There is no callback allocation or unbounded event queue.

Validation includes scheduler block partitioning, fifteen-key boundaries, held-pitch-only output, enabled-player routing, release/pedal/transport changes, same-sample key replacement, old-release ownership and fixed chord roles. Actual processor checks cover MIDI offsets, host tempo/seek, block-size-invariant audio, panic, state recall and audible syncopated section hits. The allocation trap drives scheduler and sampler together with dense MIDI. The comparative WAVs use the same two held triads with the same instrument levels and neutral Tone/Accent.

These are performance behaviors, not evidence that the underlying recordings or full pop-horn realism have been accepted. Source spectral analysis remains separate in recording-audit.md.

## Paired recorded takes

Version0.15.1 avoids identical recordings between the two sax slots or the two trumpet slots when another eligible take exists. The players must share the input MIDI channel, source note, and concert pitch. Whichever player starts second skips the held counterpart's recording within its own articulation, pitch, velocity, and dynamic-layer candidates. Independent repetition counters remain intact. The rule works with either player arriving first and adds no onset delay, detuning, or audio processing.

The current bank supplies alternative takes for trumpet Short and baritone Short. Tenor and trumpet sustains have one take per selected pitch and force layer. A single-take group keeps its ordinary recording. These slots do not become independently recorded musicians merely because both are enabled. Isolated players and the original four-player setup retain their existing take selection.

The paired-take check includes both onset orders, unequal repetition histories, gaps through2ms, alias mappings, dynamic layers, composite marcato notes, articulation fallback, unrelated MIDI gestures, single-take groups, and mute behavior. The real-plugin audit in `scripts/audit_paired_takes.py` separately identifies recordings from split-channel output and renders before/after Groove comparisons.

## Humanize in every mode

Humanize is available in the main view for Layered, Relay and Groove. It adds a
small, independently varying onset delay to each player and slow, smooth level
drift. At 100%, onset delays stay within 8 ms and level drift within +/-0.6 dB.
At the default 35%, those limits are 2.8 ms and +/-0.21 dB. Live notes are delayed,
never played before the incoming MIDI event; there is no lookahead buffer.

Each player's simultaneous chord tones and sample layers share an onset offset.
Its level drift continues across notes rather than restarting a tremolo on every
attack. Pitch and recorded take selection stay unchanged. The optional baritone airflow stream retains its own level; musical notes and key sounds receive the drift. Releasing a note before
its delayed onset cancels it instead of leaving a late ghost note.

New instances and musical presets start at 35%. Uncolored/reference presets and
older saved patches start at zero. The parameter is saved and host-automatable.
Zero adds no onset staggering or level drift. Changes to level drift are smoothed.
The separate Section accents control shapes authored accents and gate contours;
it no longer adds timing offsets. Groove retains its short gated endings.

Validation for this change includes per-player timing and gain bounds, coherent
sample layers and chords, pre-onset cancellation, automation smoothing, callback
allocation checks, and block-size-invariant audio in all three modes. The
functional suite passes. The CLI timing stress guard still fails in some
unhumanized high-polyphony workloads under the current machine load; the
full-Humanize 128-voice p99 case passes. The retained measurements are in
`artifacts/humanize-performance-0.18.0.json`. These measurements do not replace a
native audio-callback qualification.
