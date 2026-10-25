# Alternate tenor

Version0.16.0 adds a source choice for each sax player. Expand Show configuration and choose Original tenor or Alternate tenor beneath that player's MIDI channel. The Alternate tenor factory preset selects the primary sax alone, with neutral tone and sustain articulation. Existing presets and projects keep the original source.

The alternate source contains66 complete mono MTG tenor recordings, two forces across MIDI44–76, from sfzinstruments/MTG.SoloSax at b494d256549b3d088fdec176ce82867f8a1f58b2. The publisher's198 mappings retain source identity, root, tuning, gain, velocity split100/101 and neighboring-note sequence. The second and third sequence reuse neighboring recordings. They are not extra recordings of the same pitch.

The PCM has no filtering, normalization or compression. Playback applies the authored tuning and region gain, our velocity response, player level/pan and output controls. Tone, accent, CC1 vibrato and the output safety knee still apply when enabled. No source SFZ filters, LFOs, legato offsets or envelope engine are imported. The controls do not simulate a new recording session.

Held notes finish when their source recording ends. Short gates a sustained recording and uses its recorded file decay at note-off. Fall uses the existing synthesized pitch descent. Neither is a dedicated recorded articulation in this source. Baritone styles override the stored tenor source choice until Tenor is selected again.

Groove retains the shared section rhythm and at most2ms relative onset spread at full Feel. It varies strike velocity and gate length per player. The alternate force recordings give that velocity variation another recorded tone to select. This does not establish musical realism; the raw source and the played instrument still need listening judgment.

Run scripts/verify_mtg_tenor_sources.py with the qualified original bank, extended bank and report path to compare all original record and zone bytes, all66 source PCM payloads, and all198 authored mappings. Tests/AlternateTenor.cpp exercises source independence, force selection, paired takes, finite EOF, note-off, source changes, baritone override, panic and invalid-bank replacement. Processor tests check appended automation and old-state restoration. Live Logic and MainStage sessions remain separately unqualified.
