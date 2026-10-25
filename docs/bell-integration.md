# Bell & Bone integration

The user supplied `Bell and Bone_2_1_0_Samples.hr1` in Downloads. It is extracted and audited locally. Version 0.5 integrates its sustain, staccatissimo and staccato-fall recordings. Missing assets affect their own integration tasks, not the whole project.

Reproduce the extraction and audit:

```sh
.venv/bin/python scripts/extract_bell_archive.py "$HOME/Downloads/Bell and Bone_2_1_0_Samples.hr1" assets/cache/bell-and-bone
.venv/bin/python scripts/audit_bell_samples.py
```

The audit fetches checksum-verified maps and license from pinned public source if absent. It uses the existing NumPy and SoundFile dependencies. `Tools/BellHeader.cpp` is an optional JUCE ValueTree inspector, built with `cmake --build build --target TopBrassBellHeader`; it is not needed for the above import.

The four FLAC chunks match the complete contiguous spans in the public maps. Public source says 2.0.1 and the supplied archive says 2.1.0, so this is structural correspondence rather than a claim that their embedded maps are identical. The extracted sustain and staccatissimo recordings roundtrip through 24-bit WAV with identical decoded audio. The importer preserves each map zone and deduplicates only identical source-filename/decoded-audio pairs. Raw listening examples are in `artifacts/bell-and-bone`.

`assets/cache/bell-and-bone/playback-catalog.json` separates recordings from mapping zones for integration. Preserve these contracts:

- Sustain groups 1–3 are simultaneous dynamic layers. Velocity selects the key borrowing zone. Do not rotate dynamic layers as round robins.
- Staccatissimo groups 5–8 are four repetitions, with velocity bands selecting recorded dynamics.
- Key bounds and root pitch have different meanings. Keep authored key/velocity regions rather than replacing them with global nearest-root selection.
- `SampleEnd` is exclusive. Crop coordinates are translated by `SampleStart`.
- The loop crossfade blends the material before `loopStart` into the end of the loop, then wraps to `loopStart`. The existing TopBrass crossfade convention differs. Preserve this explicitly or bake the authored loop crossfade offline.
- `ReleaseStart` points into the same recording after the loop. Release playback must not loop. Bound the release crossfade to the remaining audio; one region is shorter than the nominal 8,192-frame upstream crossfade.
- Pitch correction is in cents. NormalizedPeak is a linear scalar, and Volume is in dB. The catalog keeps authored gain outside the PCM. Baking it can produce values slightly over one; do not clamp or independently renormalize recordings to satisfy the old bank loader.
- Nineteen trombone sustains have authored GainTable edits. The player now applies decoded immutable gain lookup tables after sample interpolation and loop mixing. Table time includes the original sample-start offset; recorded release gain blends into the release region. This uses source-position evaluation rather than reproducing HISE’s block-ramped gain approximation. Raw audition extracts remain unprocessed.

The source license is retained in the cache and audition directory. Its terms differ from the retained CC0 saxophone and tuba sources. The bank retains 741 CC0 saxophone/tuba recordings and adds 1,309 Bell & Bone recordings with 1,658 mapping zones. The engine retains a fixed pool of 128 physical sample playheads plus 16 steal tails. A held trumpet or trombone note uses three playheads. Its two adjacent dynamic weights sum to one; initial dynamics use note velocity, then CC2/CC11 controls dynamics and amplitude. All layers advance even while their weight is zero. This does not establish listening acceptance.

Build the mapped bank with `scripts/prepare_samples.py`. The preserved CC0 reference is `assets/cache/cc0-reference.bank`, used by historical player comparisons. `scripts/build_bank.py` writes original CC0 channels and levels together with Bell recordings, zones and authored gain tables in TBBANK04. The player also reads historical TBBANK02/03 banks. Version0.4 binaries reject the additional recorded-fall articulation, so update the binary and bank together.

Recorded staccato falls use sample articulation4, preserving baritone subtone/growl at2/3 and the host Fall parameter at choice2. They are finite one-shots, retain authored tuning and zone gain, and do not receive the synthetic pitch fall. Note-off, pedal-up and controller reset keep the recorded ending; mute, stealing, retrigger and panic still stop or fade it. Trumpet uses velocity bands0–63/64–127; trombone uses0–84/85–127. RR groups1–4 map to four alternative takes, with the source articulation’s −3dB gain applied separately from sample-map gain.

One trombone filename has two different authored crops, with21,912 and22,170frames. The importer retains both decoded variants, giving443 additional fall recordings across468 zones. It does not collapse by filename alone.
