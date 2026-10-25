# Qualify audio callback performance

Run the native AU audit to measure the installed instrument on the current Core Audio output device. The tool renders the instrument into scratch buffers and sends silence to the device. It does not open microphone input or change the device rate or buffer size.

Build the macOS diagnostic targets:

```sh
cmake -S . -B artifacts/audio-audit-build -DCMAKE_BUILD_TYPE=Release -DTOPBRASS_BUILD_PLUGIN=OFF
cmake --build artifacts/audio-audit-build --target TopBrassNativeAudioAudit TopBrassRenderDeadlineAudit --parallel 4
```

Choose a new output folder for each qualification:

```sh
.venv/bin/python scripts/qualify_audio_callbacks.py \
  --tool artifacts/audio-audit-build/TopBrassNativeAudioAudit \
  --output artifacts/audio-callback-qualification
```

The script captures an empty-output control and two instrument runs, each with 2,000 callbacks. It verifies the loaded AU image against the selected release summary. Pass a versioned `--baseline` to select another retained release summary. The AU itself must match that release.

The instrument workload sends 128 held tenor notes across four MIDI channels, CC1=100 on channel 1, and breath CC20 triggers on eight channels near every 12,800 frames. These are MIDI inputs. The AU interface does not expose the number of retained sample playheads. Finite, nonzero rendered energy verifies active rendering before the output is silenced.

Each CSV row retains the actual frame count, callback timestamps, complete service-window wall and thread CPU time, AU render time, rendered energy, and render status. The service window includes MIDI dispatch and energy measurement. It excludes final telemetry publication. Startup MIDI dispatch has separate timing fields. Device overload notifications are observations across the device and cannot identify the responsible client.

The script recomputes summary counts and percentiles from the CSV. Silent rendering, non-finite results, or inconsistent summaries fail verification. Missing timestamps, sample-time discontinuities, unsupported overload notifications, observed device overloads, or changed device settings make qualification inconclusive. A service window longer than its frame period exceeds the service budget. This does not, by itself, establish an audible hardware dropout.

Recheck an existing report without replacing it:

```sh
.venv/bin/python scripts/qualify_audio_callbacks.py --verify \
  --tool artifacts/audio-audit-build/TopBrassNativeAudioAudit \
  --output artifacts/audio-callback-qualification
```

Verification requires the recorded tool and selected installed AU identities. `--analyze` can create a report from existing raw captures that do not yet have `native-results.json`. Counterexample checks ensure that silent output, unavailable timing evidence, device problems, and excessive wall time cannot qualify as clean runs.

## Interpret CLI and device observations separately

`TopBrassRenderDeadlineAudit` measures an unpaced engine loop with paired wall and thread CPU times. Its process event counters bracket the timer interval. Context switches and page faults can correlate with a late block without establishing its cause. A low CPU time does not exclude blocking. The existing engine latency guards remain unchanged.

```sh
artifacts/audio-audit-build/TopBrassRenderDeadlineAudit \
  assets/TopBrass.bank artifacts/cli-deadline-probe 4000 inherit mixed
```

Use `music` to omit recorded details, `empty` to omit all notes, or `inject-wait` to validate detection of a deliberately delayed CLI block. These runs do not open an audio device. The command returns exit code `2` when any measured wall window exceeds the 128-frame comparison budget. This diagnostic outcome is separate from the existing p99 test gate.

The native tool uses the output audio framework's callback thread. It does not create auxiliary rendering threads or set thread priority. Apple documents automatic device workgroup participation for framework-provided real-time audio threads in [Understanding Audio Workgroups](https://developer.apple.com/documentation/audiotoolbox/understanding-audio-workgroups). Recorded QoS and Mach policy values are diagnostic metadata, not proof of workgroup membership.

## Retained version 0.15.1 observations

`artifacts/deadline-diagnosis/native-results.json` contains two 2,000-callback instrument runs and a 2,000-callback empty-output control. The current device used 48 kHz and 512-frame buffers. Both instrument runs produced finite, nonzero audio in every callback. Their callback wall p99 values were 1,716.958 µs and 1,764.042 µs against a 10,666.667 µs frame period. Neither run observed a startup or steady service budget overrun, render error, sample-time discontinuity, or device overload notification. These runs exercise sustain loops and repeated breath triggers.

The earlier CLI baseline recorded 18 wall windows over 2,666.667 µs, with no corresponding CPU-budget overruns. All 18 coincided with a context switch. Two also recorded page faults; none recorded page-ins. Subsequent empty, music-only, and mixed CLI controls recorded no overruns. This establishes intermittency, not a scheduling or renderer root cause.

The native results qualify these observed 48 kHz/512-frame runs. They do not qualify 128-frame hardware operation, Logic Pro, MainStage, every workload, or musical realism. The installed AU, VST3, standalone application, and saved patch remain version 0.15.1. No engine change follows from these measurements alone.
