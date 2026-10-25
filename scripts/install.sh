#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
source_dir="$PWD/build/TopBrass_artefacts/Release"
components="$HOME/Library/Audio/Plug-Ins/Components"
vst3="$HOME/Library/Audio/Plug-Ins/VST3"
apps="${TOPBRASS_APPS_DIR:-$HOME/Applications}"
for target in "$components/TopBrass.component" "$vst3/TopBrass.vst3" "$apps/TopBrass.app"; do
    if [ -e "$target" ]; then
        echo "Already present, move it to a backup before reinstalling: $target" >&2
        exit 1
    fi
done
for bundle in "$source_dir/AU/TopBrass.component" "$source_dir/VST3/TopBrass.vst3" "$source_dir/Standalone/TopBrass.app"; do
    codesign --verify --deep --strict "$bundle"
done
mkdir -p "$components" "$vst3" "$apps"
cp -cRp "$source_dir/AU/TopBrass.component" "$components/TopBrass.component"
cp -cRp "$source_dir/VST3/TopBrass.vst3" "$vst3/TopBrass.vst3"
cp -cRp "$source_dir/Standalone/TopBrass.app" "$apps/TopBrass.app"
for target in "$components/TopBrass.component" "$vst3/TopBrass.vst3" "$apps/TopBrass.app"; do
    codesign --verify --deep --strict "$target"
done
echo "Installed AU, VST3, and standalone for the current user."
