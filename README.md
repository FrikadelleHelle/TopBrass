# TopBrass

A sample-based pop, funk and soul horn instrument for macOS. Six independently switchable players: two saxophones, two trumpets, trombone and tuba. Includes AU for Logic Pro/MainStage, VST3 and a standalone app, with tight section grooves, independent mixing, alternate sax sources and War Tuba articulations.

**Beta:** musical realism remains a listening/acceptance task. The downloadable build targets Apple Silicon, macOS 12 or newer. Intel and Windows have not been qualified. See [validation](docs/validation.md) for the limits of automated checks.

## Download and install

Get the compiled bundles from [GitHub Releases](https://github.com/FrikadelleHelle/TopBrass/releases). Each download includes its complete sample resources; no compiler or original sample-library download is required.

Quit your host, unzip the desired format and copy:

- `TopBrass.component` to `~/Library/Audio/Plug-Ins/Components/` (Logic Pro and MainStage).
- `TopBrass.vst3` to `~/Library/Audio/Plug-Ins/VST3/` (VST3 hosts).
- `TopBrass.app` to `/Applications/` or `~/Applications/` (standalone).

Back up an existing installation before replacing it. Restart the host after installation. This beta is ad-hoc signed, not Developer ID signed or notarized; macOS may require explicit approval in Privacy & Security. Do not disable Gatekeeper globally.

## Build from a fresh checkout

Requires macOS, Xcode command-line tools, Git, CMake 3.25+, Ninja, Python 3.11+ and enough disk space for sample inputs, generated banks and bundles (allow 10 GB). Install those tools before running:

```sh
git clone https://github.com/FrikadelleHelle/TopBrass.git
cd TopBrass
./scripts/bootstrap.sh
```

Bootstrap pins JUCE 9.0.4 to an exact commit, installs pinned Python dependencies, downloads the checksummed sample-input release automatically, rebuilds all four banks from those original inputs, builds AU/VST3/standalone and runs CTest. **No file from the developer's Downloads folder is needed.** Inputs and their licenses are listed in `assets/sample-inputs.lock.json` and [LICENSES.md](LICENSES.md). Most inputs are additionally checked against upstream Git blobs; Bell & Bone audio and maps have pinned SHA-256 hashes. The archive includes original sample inputs, not merely precompiled banks.

The sample-input release is versioned independently and should be retained when publishing later code releases. Standard build dependencies still require internet on the first build. A fresh checkout has been tested on the release machine; this does not promise bit-identical executable signatures or successful builds on untested platforms.

Build products are in `build/TopBrass_artefacts/Release/{AU,VST3,Standalone}`. After source changes:

```sh
cmake --build build --parallel 6
ctest --test-dir build --output-on-failure
```

`./scripts/install.sh` installs a local build for the current user and refuses to overwrite an existing installation. For raw-source comparison, spectra and actual-plugin capture tools see [recording audit](docs/recording-audit.md). Optional plotting/audit dependencies are in `scripts/requirements-audit.txt`; they are separate from bootstrap's build/test dependencies.

## Licensing

The plugin code uses **AGPLv3**. Samples retain separate CC0, CC BY 4.0, and Bell & Bone **CC BY-NC-SA 4.0 + CC+** terms. This release includes noncommercial sample material; do not assume that the instrument can be sold or all recordings can be relicensed. Commercial musical works have the explicit permissions described in the vendor license. Read [LICENSES.md](LICENSES.md) and the bundled attribution before redistributing.

## Playing

1. Restart your DAW after installation.
2. In Logic Pro or MainStage, create a software-instrument channel and choose **AU Instruments → TopBrass → TopBrass → Stereo**.
3. Click musicians in the shared courtyard to recruit or dismiss players. Subtle idle sway, breathing and drifting embers animate the scene. Sergeant and Staff Sergeant play saxophone; Lieutenant and Colonel play trumpet; Captain plays trombone; Major plays tuba. Both extra players start off. Open **Show configuration** for independent level, pan, octave, and MIDI channel controls.
4. Play MIDI notes. Velocity controls loudness and recorded attack selection. Sustained trumpet and trombone blend three recorded dynamic layers; CC2 or CC11 controls their dynamics and expression. Other instruments use amplitude expression. CC1 adds delayed vibrato. CC7 controls volume. Pitch bend spans two semitones in each direction. CC64 sustains notes.
5. Open **Show configuration** to choose the shared sax voice. **Tenor** and **Baritone** follow the global articulation. The named baritone styles override it. Marcato combines short attacks with a delayed sustain. Subtone and growl play finite recordings in their recorded ranges, and use ordinary sustains outside those ranges.
6. For a neutral reference, choose **Uncolored section** or an **Uncolored** solo and keep CC1/mod wheel at0. These set Tone and Attack accent to0. The existing funk/soul starting points deliberately color the recordings. The tone control tilts the sound from warm to bright.
7. Choose **Short / punch**, **Sustain**, or **Fall**. In Layered and Relay, short recordings finish their natural tails after key release. Trumpet and trombone sustains play recorded release regions. The release control fades other sustains and forced stops. Fall plays recorded trumpet and trombone staccato falls, with natural endings and four alternative takes. Saxophone and tuba retain the synthesized pitch drop.

The Baritone funk, Smoky baritone, and Baritone growl presets set the saxophone an octave down to reach its low register. The original presets keep the tenor.

The default saxophone and trumpet play at the incoming pitch. Trombone plays an octave down and tuba two octaves down. Set all octaves to zero when sending separate concert-pitch parts on individual MIDI channels. MIDI channel zero in a host parameter means all channels. Trumpet and trombone honor authored key and velocity zones. Other instruments are silent more than seven semitones beyond the selected articulation's recorded range.

Use **Humanize** in the main view for tiny per-player timing differences and slow volume wavering in every mode. New sounds start subtly humanized; set it to zero for exact timing and no added level drift. Older saved patches retain zero until you raise it.

Use **All notes off** to silence held voices. The standalone app also accepts external MIDI and provides an onscreen keyboard.

For recorded mechanical detail, select **Baritone with keys**, or choose a baritone sax voice and raise **Baritone keys**. It uses the baritone’s actual close/open recordings; it does not add breathing or noises to the other instruments. Existing saved patches default to zero key noise. **Baritone close detail** adds the separately adjustable **Baritone airflow** layer. It uses a recorded under-note air sound, not a phrase inhalation.

## Section playing

**Tenor breath** and **Tenor keys** provide explicit recorded one-shots. MIDI CC20/21 triggering is opt-in; send0 before each nonzero trigger. Detail level0dB retains the quiet source level before sax/output gain.


Groove gates end recorded notes with a short 5 ms fade so tails do not accumulate; Layered and Relay retain natural endings.

Choose **Section groove** to play inside a movable fifteen-key window (default MIDI60–74). Held notes drive tight, shared syncopated hits across every enabled instrument, with accents and softer ghost hits. **Section relay** passes successive key presses between players. **Layered** preserves the original behavior.

With Humanize at zero, the first played lead starts immediately. **Section accents** adds coordinated playing-force and note-length variation; **Humanize** controls stagger and level drift in every mode; tempo follows the host or the standalone tempo control. Only your held pitches are used, with octave placement in each instrument’s sampled register. Per-strip MIDI channel filters apply in Layered mode; section modes route directly to enabled players. See [section-playing.md](docs/section-playing.md) for pedal, transport and release behavior.

## Recorded double tonguing

With **Short / punch** selected, **Double tongue** on the trumpet or trombone strip selects a recorded two-attack figure. The switches are independent and start off. **Double-tongued trumpet** and **Double-tongued trombone** are neutral solo starting points. The four recorded alternatives and two velocity bands retain their original timing. Layered and Relay retain their natural endings; Groove gates can shorten the figure. A new note can retongue the player; the figure is not stretched to tempo. Saxophone and tuba keep their ordinary articulations.
