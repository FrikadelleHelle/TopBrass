# Rebuilding and publishing a release

Use a clean clone of the intended commit and run `./scripts/bootstrap.sh`. This downloads the pinned sample inputs and JUCE, rebuilds the banks and formats, and runs the tests. Keep the `sample-inputs-v1` release: code checkouts depend on its immutable archive URL and checksums.

After successful validation, package the bundles:

```sh
.venv/bin/python scripts/package_release.py --output artifacts/release
```

The packager rejects incorrect bundle versions/architectures and invalid signatures. It creates three self-contained macOS Apple Silicon ZIPs, a pinned JUCE source archive, and SHA256SUMS.txt. Publish these as assets on a versioned GitHub release targeting the same source commit. GitHub automatically supplies the project source archives for that tag. Include any additional validation report and describe untested hosts/platforms in the release notes.

These are ad-hoc signed beta builds. Developer ID signing/notarization requires a separately configured Apple identity and has not been implemented. Do not advertise notarization or Intel/Windows compatibility without verifying those artifacts.

If sample inputs change, fetch and verify the new sources, run `scripts/package_sample_inputs.py` with a new archive filename, and give the generated lock a new immutable release URL. Publish that input archive with its licenses, then test a fresh checkout again. Never silently replace the archive behind an existing checksum. Bell & Bone input redistribution remains subject to its noncommercial/share-alike terms; code and recordings have separate licenses.
