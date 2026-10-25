# Original recording versus installed-plugin audit

This audit is separate from feature development. It loads the installed VST3 or macOS Audio Unit as an external plugin; it does not substitute the current Engine implementation. It creates new instances and does not change the running standalone patch. VST3 is the default; pass the installed .component path through `--plugin` to capture AU instead. The actual format, version and identifier are recorded in each host JSON file.

Run from the project directory:

```sh
.venv/bin/python -m pip install -r scripts/requirements-audit.txt
cmake --build build --target TopBrassRecordingAudit -j 4
.venv/bin/python scripts/recording_audit.py
.venv/bin/python Tests/RecordingAudit.py
.venv/bin/python -m http.server 8794 --bind 127.0.0.1 --directory artifacts/recording-audit
```

Open http://127.0.0.1:8794/. The default cases are short and sustained tenor, trumpet, trombone, tuba and baritone notes. MIDI pitch matches the source root, sample rate is native, and the first recorded alternative is selected. Full CC11 expression isolates the forte sustain layer; selecting its native-pitch zone sometimes requires a lower MIDI velocity. The host uses24 silent512-frame blocks to settle parameter delivery before note-on. Full source WAVs, raw VST3 output, actual host parameter dumps and checksums remain available. Native Bell notes are original PCM extracted from the supplier monolith at its authored sample boundaries.

For an arbitrary matched recording/render pair:

```sh
.venv/bin/python scripts/recording_audit.py --reference /absolute/original.wav --rendered /absolute/rendered.wav --output /absolute/audit-output
```

Both inputs must be mono/stereo at the same sample rate. Ensure the same note/performance is present; a comparison between different performances is not a measurement of an EQ transfer function. Generic mode uses the overlapping duration; default instrument cases restrict their primary measurement to before the first loop seam. Later loop/release changes remain visible in the full-note plots.

Listening files have one constant gain adjustment to remove the plugin's static output level, followed by the same playback gain for both members of the pair. No EQ, pitch correction, phase/time alignment, compression or denoising is applied. Original native files are copied byte-for-byte. Analysis uses channel power rather than a mono sum, preserving the significance of stereo phase cancellation. It includes STFT spectrograms, Welch spectral profiles, third-octave band differences, waveform residual, channel correlation and lag. Source bands more than60dB below the strongest band are excluded from the summary. The engineering transparency gate is less than0.25dB band error, waveform residual below−60dB relative to source, and zero estimated lag. It does not judge musical realism.

The five analysis regressions check constant gain, anti-phase stereo, a known filter, delay, and a silent render. The filter and delay cases prevent a misleading pass based only on spectral resemblance.

The initial installed0.7 audit found nine of ten neutral cases essentially unchanged in their native pre-loop windows. Sustained trombone retained a supplier-authored time-varying gain envelope; accounting for that envelope reduced its residual from−25.59dB to−145.51dB relative to source. That is an envelope difference, not evidence that corrective EQ is needed. Tone−1 produced large band changes in every case. Consult the saved report for exact values and source identities. These ten notes do not qualify every key, velocity, crossfade, transposition, loop or section voicing.

Methods: [SciPy ShortTimeFFT](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.ShortTimeFFT.html), [SciPy Welch](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.welch.html).

## Sustain loops beyond the transparency window

The installed0.9 capture is retained separately in `artifacts/recording-audit/v0.9`, without replacing the original0.7 audit. Run `.venv/bin/python scripts/audit_sustain_loops.py artifacts/recording-audit/v0.9` after completing that capture. Its `sustain-loops/index.html` compares the original and actual VST3 throughout the note, retaining the single gain match from the pre-loop window. Half-second measurement windows at0,1,2,4 and6seconds are included where the source is long enough. These include level differences and waveform residuals, alongside spectral shape after removing only the window's constant level.

All46 tenor sustains have median source duration7.430seconds, first loop alteration0.910seconds and repeated period0.324seconds;44 begin looping before2seconds. The23 tuba sustains have corresponding medians7.449,1.060 and0.588seconds, with18 looping before2seconds. These measurements come from the captured0.9 bank metadata. They show how much recorded material is replaced; they do not establish that every seam is audible.

Once looping begins, source and player are different performances. Their spectral differences cannot be read as an EQ transfer function. The native first-segment pass remains valid within its window, while providing no evidence of whole-note fidelity. The extended plots and listening pairs expose that distinction. Version0.9.2 integrates the qualified later tenor loops; the earlier experiment captures remain separate. Source preservation and loop checks do not establish listening acceptance.

## AU and VST3 comparison

The diagnostic host enables AU support only on macOS and selects its backend from the requested bundle extension. This does not change the installed plugin binaries. To capture the AU against the source recordings:

```sh
.venv/bin/python scripts/recording_audit.py \
  --plugin "$HOME/Library/Audio/Plug-Ins/Components/TopBrass.component" \
  --output artifacts/recording-audit/au-v0.9.2
```

For a direct comparison between the two installed formats:

```sh
.venv/bin/python scripts/verify_plugin_formats.py \
  --output artifacts/recording-audit/format-compatibility-v0.9.2
```

This captures ten solo source cases and two full-section phrases in both formats, each at neutral Tone and Tone−1. Section phrases use all four players, staggered chord entry, sustain pedal, changing CC11 expression, CC1 vibrato, pitch bend and panic. The second phrase enables baritone keys and airflow. Native and section MIDI timelines are dispatched at their original sample offsets. Raw decoded output is compared without gain correction, alignment, EQ or pitch correction. Silent/non-finite output, a misplaced section onset, continued sound after panic or a raw difference exceeding1e−6 fails the comparison.

The0.9.2 run produced24 bit-identical AU/VST3 pairs, with maximum sample error0. The section cases retained silence before frame128, began sounding afterward and stopped at the panic frame177600. The initial attempt to load the component through the old VST3-only host failed before format support was added. The extended scene checks were implemented and then executed; no separate test-first claim is made for those cases. The five existing analysis regressions also pass.

Custom diagnostic cases may provide `midi_events` as `[frame, status, data1, data2]` rows for note-on/off, controller and pitch-wheel messages. Without that field, the host retains the original single-note protocol. `ensemble: true` selects Groove and enables all four players at octave offsets0,0,−1,−2. Optional `keys` and `airflow` amounts affect baritone detail. These controls are for the capture tool, not additional plugin parameters.

Logic Pro and MainStage were not located in their standard Applications paths or the Spotlight exact-name search on this machine. The comparison qualifies the exercised paths in this diagnostic host; it does not qualify live Logic/MainStage sessions, arbitrary host automation or musical realism.

## Audition any raw recording

Run the same complete recording through an isolated copy of the actual VST3, then compare its waveform and frequency profile:

```sh
.venv/bin/python scripts/audition_recording.py /absolute/path/recording.wav \
  --key 60 --output artifacts/recording-audit/my-recording
```

The MIDI key is the playback anchor, not detected pitch. Mono/stereo WAV or FLAC recordings need at least 2048 frames, at most 60 seconds, and an 8–192 kHz native rate. Invalid, silent, non-finite or out-of-range PCM is rejected rather than repaired. Choose a new output folder for each audition. The exact source file and decoded float PCM hash are retained. The private plugin bundle uses one complete recording and eleven silent coverage records; it does not alter the installed bank or saved patch. This diagnoses the playback path without recreating a supplier's complete instrument mapping.

The report plays the unprocessed original followed by plugin playback with fixed output/pan gain compensation, preserving relative source dynamics. Its whole-source spectrograms, band errors, lag and waveform residual distinguish spectral resemblance from actual fidelity. A technical pass does not judge realism. Preparation, capture and analysis can also run separately with `--stage`.

The 0.13.0 source auditions are at `artifacts/recording-audit/raw-source-candidates-v0.13.0/index.html`. Four clean condenser Weresax alto recordings are candidates only; they are not integrated as a playable instrument. The supplier's SFZ effects and synthesized wind-noise oscillator are excluded. The fifth pair uses an existing VCSL stereo tenor recording. All five complete-source captures pass the transparency gate.

Finite playback now includes the source interval `[0, frame_count)`, including the final source frame and its fractional interpolation cell. Cubic neighbour reads remain clamped to the valid source indices. Native recorded-ending fades still reach zero at the last source frame. The regression failed before the correction and covers mono/stereo sources, resampling, transposition and exact block-edge cleanup afterward. This is a correctness correction, not evidence that the previous one-frame truncation explained the overall tone.

A separate load study measured five fresh Bank instances with uncontrolled filesystem caching. Median total bank/key/air load was 749.627 ms. It excludes UI launch, host scanning, signature validation and destruction. Musical PCM already used bulk reads. These measurements did not justify attributing a slower UI-opening operation to the bank loader, and the production loader was left unchanged. Rerun `TopBrassBankLoadBenchmark` to measure those boundaries separately.

## Current listening bench and historical captures

The server root is now generated by `scripts/listening_page.py`. It uses the current validated version, verifies the installed VST executable against that release and the AU/VST capture identity, and publishes neutral four-player section captures with one fixed18dB listening gain. These diagnostic settings differ from the saved live patch. The version0.7 root audit is preserved byte-for-byte as `original-audit-v0.7.html`; its relative recording links continue to work.

`listening-index.json` reserves the hub output folder. Capture preparation, supplied-pair preparation, analysis and HTML generation reject that folder before writing. The recording-audit CLI defaults to a timestamped folder under `audits/`, so running it cannot silently restore the old root page. Existing audit subfolders remain usable. A direct attempted root analysis was rejected, with the current page, original historical page, config and raw audio hashes unchanged. The five spectral/waveform analysis regressions pass after this change.

`mtg-tenor-detail/` contains six original-versus-VST auditions from the pinned MTG SoloSax repository. `scripts/fetch_mtg_tenor.py` independently verifies99 original FLAC Git blobs and decoded PCM identities:64 source-labelled breath recordings,33 key clicks and two native-pitch tenor references. Attribution is Music Technology Group, Universitat Pompeu Fabra, samples, and kinwie, SFZ preparation; the CC BY4.0 license is retained. The supplier's prep includes trimming, loops and FLAC conversion, so source fidelity does not imply untouched microphone recordings.

All six selected whole-recording comparisons pass. Noise listening files use a fixed additional36dB for both original and plugin to expose the quiet detail. The original files remain available unchanged. This boost is not a mix recommendation and does not normalize individual recordings. The source maps trigger breath/key sounds on manual MIDI39/40; they do not establish automatic inhalation scheduling. Their legato map reuses sustains with offsets and envelope changes, not dedicated recorded interval transitions. The catalogue and auditions do not change the installed instrument.

## Recorded attack timing

`scripts/audit_attacks.py` measures exact-zero prefixes and energy growth across the bank, then records the actual four-player VST section with Feel0. It retains membership while isolating one output through pan, and supplies Short/Sustain comparisons and first150ms waveform plots. See [attack-timing.md](attack-timing.md) for the measured0.16.0 evidence and limits. These are recording-growth metrics, not detected perceptual onsets or approved timing corrections.

## War Tuba candidate

`scripts/audition_war_tuba.py` retains complete condenser recordings, verifies675 unique pitched PCM payloads and all original key/velocity regions across two private banks, and performs1,107 actual VST short-note selection checks. It compares the current tuba with finite sustain, staccato and staccatissimo candidates, alongside complete-source spectral comparisons. See [war-tuba.md](war-tuba.md) for mappings, source/program differences and remaining integration work.
