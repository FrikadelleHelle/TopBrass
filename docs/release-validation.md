# macOS beta 0.18.0 validation

Validated on 25 October 2026 using a separate clone from the public GitHub repository. Bootstrap fetched pinned JUCE and Python dependencies, downloaded the public sample-input archive without GitHub credentials, regenerated all four banks, built AU/VST3/standalone, and passed all **29 CTest tests**. The clean build exposed missing output-parent creation and SciPy dependencies; both were corrected in the published source and the build rerun successfully.

All four regenerated banks matched the working instrument's banks byte for byte. The main bank SHA-256 is `9fa6d7691ac51955bcef49512491c60aa50bf8445bcea376ac0caeec95f800f4`. The sample-input archive SHA-256 is `881b89545e2b174d86698cea74072969faa910d2b709efa8a8b6f5f3aeb25f07`.

The actual built VST3 then passed 24 finite, non-silent stereo render checks through the JUCE audit host: six individual player slots, baritone, both alternate tenor slots, both War Tuba short styles and a six-player section, each with neutral and colored tone settings. The audit host was updated to use the current Section accents label and explicit humanization. Run `scripts/smoke_release.py --output artifacts/release-smoke` to repeat this check after bootstrap.

Release assets contain separate AU, VST3 and standalone bundles with their sample resources, project/sample licenses and JUCE dependency notices. The packager checks matching 0.18.0 versions, arm64 architecture and bundle signatures. The release includes the exact JUCE source archive and file checksums, alongside GitHub's tagged TopBrass source archives. See the attached validation JSON and SHA256SUMS.txt for machine-readable results and hashes.

These are Apple Silicon builds, ad-hoc signed. Developer ID signing and notarization are not provided. Intel/Windows and live Logic Pro/MainStage sessions have not been qualified in this release. Rendering finite audio and passing automated checks do not establish natural instrumental realism or a dropout-free live set.
