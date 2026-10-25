# Attack timing and source character

The attack audit separates MIDI scheduling, digital silence and recording energy growth. Run it against an installed plug-in:

```
cmake --build build --target TopBrassRecordingAudit --parallel 6
.venv/bin/python scripts/audit_attacks.py --output artifacts/recording-audit/attack-timing-new
```

Choose a fresh output directory. The audit retains the actual VST output, parameter snapshots, native recording measurements, plots and a listening page. It adds no instrument processing.

For the measured0.16.0 bank, all2,588 recordings contain a nonzero sample within0.794ms. The same bound holds after the19 authored gain tables. This contradicts the hypothesis that long exact-zero prefixes explain the loose section. It does not prove that every early sample is audible, useful or musically intentional.

The actual section test holds MIDI60 at velocity112 for one bar at120BPM. All four instruments remain enabled, with their existing octave settings and Feel0. Tone and Attack accent are neutral. Each isolated capture pans the selected player hard-left and all other players hard-right. This keeps scheduler membership, repetition histories and strike velocities unchanged. The analysis uses the left output; it is not a full stereo stem for the original tenor. Separate all-right captures bound leakage below−124dB relative to each measured player's first250ms peak.

For that first hit, the5ms RMS window reaches half of its first250ms maximum at about20ms for original tenor sustain,32ms for trumpet sustain,38ms for trombone sustain and80ms for tuba sustain. Short changes those figures to about19,14,24 and44ms. The alternate tenor is about46ms in both cases because its Short gates the same sustained recording. These are one key, force and selected recording sequence, not general timing offsets for the instruments.

These crossings describe energy growth. They are not perceptual onsets. Noise, tonguing, pitch, low-note cycles and the chosen reference window all influence the result. The report retains1,5 and10ms RMS windows, absolute amplitude crossings and exact-zero prefixes. Waveform panels show the first150ms without shifting any audio. Listening files apply one fixed18dB gain. A separate click track marks the scheduled shared hits; the horns remain unprocessed.

The measurements justify comparing articulation and source character before modifying scheduler timing. They do not justify automatic sample trimming, attack compression, per-player delays or live lookahead. Removing a rise could remove the nearby breath and tongue detail the instrument is meant to retain. Musical acceptance remains open.

The diagnostic host adds optional `isolate_player` and `horn_feel` fields. Cases without a requested feel use the plug-in's observed initial normalized value rather than a hard-coded approximation. A custom case can precede an ordinary case without retaining its feel. Default compatibility checks compare44 VST WAVs and parameter snapshots against the shipped host under the same input history. A separate mixed-history check distinguishes the49 instrument parameters from the VST wrapper's cached MIDI-controller parameters.
