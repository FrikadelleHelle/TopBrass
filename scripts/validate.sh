#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p artifacts
cmake --build build --parallel 6 > artifacts/build.log 2>&1
ctest --test-dir build --output-on-failure
for bundle in build/TopBrass_artefacts/Release/AU/TopBrass.component build/TopBrass_artefacts/Release/VST3/TopBrass.vst3 build/TopBrass_artefacts/Release/Standalone/TopBrass.app; do
    codesign --verify --deep --strict "$bundle"
done
if ! cmp -s "build/TopBrass_artefacts/Release/AU/TopBrass.component/Contents/MacOS/TopBrass" "$HOME/Library/Audio/Plug-Ins/Components/TopBrass.component/Contents/MacOS/TopBrass"; then
    echo "Install the current AU build before validating it." >&2
    exit 1
fi
# AU discovery can lag immediately after first installation.
auval -a > artifacts/au-components.log
auval -v aumu Tbrs Tpbr > artifacts/auval.log 2>&1
if [ "${PLUGINVAL:-}" != "" ]; then
    "$PLUGINVAL" --strictness-level 10 --random-seed 12345 --output-dir artifacts/pluginval-stress --validate "$PWD/build/TopBrass_artefacts/Release/VST3/TopBrass.vst3" > artifacts/pluginval-stress.log 2>&1
    "$PLUGINVAL" --strictness-level 5 --random-seed 12345 --output-dir artifacts/pluginval-au --validate "$HOME/Library/Audio/Plug-Ins/Components/TopBrass.component" > artifacts/pluginval-au.log 2>&1
fi
echo "Validation complete. Logs are in artifacts."
