# War Tuba source audition

The publisher describes [War Tuba](https://shop.karoryfer.com/pages/free-war-tuba) as a folk/jazz-oriented tuba recorded with Jakub Lewicki. Its [repository](https://github.com/sfzinstruments/karoryfer.war-tuba/tree/5b62dd6ef6b00281bb734769e44a63f5e013018a) is pinned at5b62dd6ef6b00281bb734769e44a63f5e013018a. The pinned LICENSE is CC0 1.0 Universal. The publisher's [free-library page](https://shop.karoryfer.com/pages/free-samples) also states the current CC0 policy.

`scripts/fetch_war_tuba.py` fetches the pinned archive and verifies all1,552 file Git blobs, including1,443 original WAV files. It retains the source tree, archive digest, per-file hashes, original license and readme. It does not execute any supplier code.

`scripts/audition_war_tuba.py` creates two private copies of the installed VST, replacing only their bank resources. Each contains27 complete condenser sustain recordings and324 dedicated short recordings, with silent fixtures for the other instruments. The two banks together retain675 unique pitched condenser recordings. Native recordings are mono44.1kHz16-bit; decoded PCM is packed without filtering, normalization, resampling or cropping. Sustains are finite5.15–6.52s recordings. Detected file-decay coordinates supply note-off crossfades; this is not a claim of dedicated release samples.

The original maps cover MIDI27–60 with native roots29–55. Staccato has four force windows0–32,33–64,65–96 and97–127, with three actual takes per root and force. Staccatissimo has0–42,43–85 and86–127, with four takes. Each sustain has one recorded force. The supplier's sustain mod-wheel dynamics include EQ and gain changes; the audition does not import them.

The renderer keeps source PCM and key/velocity regions, but uses TopBrass's velocity response and a deterministic take cycle. It omits supplier EQ, +9dB sustain gain, microphone blending, randomized selection, simulated two/three-player ensembles, modulation, velocity curves and legato offsets. Supplier preprocessing before publication is not established by these playback checks. The source readme explicitly says its duo/trio programs reuse one sampled tuba; they are not additional recorded musicians.

The real VST passes1,107 native short-note key/velocity/take checks against the intended originals, spanning every native root, every recorded take and both sides of all force boundaries. Four complete-source checks compare native-key36 short and sustain recordings through the two private banks. The listening page presents three of those complete pairs, since the sustain recording is shared. The source bank writer separately verifies every PCM payload and mapping. These checks establish playback/source identity, not musical acceptance.

Five phrase exports compare current VSCO Sustain/Short against War Sustain/staccato/staccatissimo. All use the same four-bar input, Horn feel0.4 and neutral Tone/Accent. All four scheduler slots remain enabled; only tuba's hard-left output is presented, duplicated to stereo. Main clips are matched to whole-phrase RMS0.05 with one constant gain per clip. Original-level exports use the same18dB gain for every phrase. Neither version changes individual note gains or timing; exact gains are retained in comparison-gains.json. The other candidate slots are silent fixtures, so this audition qualifies the isolated tuba, not a replacement full section. Original breath and valve examples are included separately and are not automatically scheduled.

The installed0.16.0 VST and bank remain unchanged. The private copies retain the original GUI labels and should not be confused with a new installed release. Reproduce in a fresh folder:

```
.venv/bin/python scripts/fetch_war_tuba.py
.venv/bin/python scripts/audition_war_tuba.py --output artifacts/recording-audit/war-tuba-new
```

Use `--finish-only` to regenerate the presentation of an already verified audition while checking its captured host and installed-template identities. Listening acceptance, integrated instrument selection, source-specific state/preset behavior, complete section playback and integrated load/performance qualification remain open.

## Authored sustain loops

All27 condenser sustain WAVs contain authored forward loops. Their first loop ends are4.329–5.756s into the original recordings; loop lengths are2.049–4.662s. Detected file decays follow every loop. The private audition deliberately ignores the loops so that complete originals can be compared through EOF. This is not evidence that a production War voice should be finite. `artifacts/war-tuba-audit/audit_loops.py` preserves marker and seam measurements for an integration decision. One seam step is about3.35 times the99th-percentile adjacent step within its loop; marker presence does not prove an inaudible seam. Loop and release behavior need their own real-player qualification before promotion.
