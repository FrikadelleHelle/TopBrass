#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
if [ ! -f vendor/JUCE/CMakeLists.txt ]; then
    git clone --depth 1 --branch 9.0.4 https://github.com/juce-framework/JUCE.git vendor/JUCE
fi
expected=3fb5cbc0340758596dac39f7c8de47d562b4896e
actual=$(git -C vendor/JUCE rev-parse HEAD)
if [ "$actual" != "$expected" ]; then
    echo "Unexpected JUCE revision: $actual" >&2
    exit 1
fi
python3 -m venv .venv
.venv/bin/python -m pip install -r scripts/requirements.txt
.venv/bin/python scripts/fetch_sample_inputs.py
.venv/bin/python scripts/fetch_mtg_tenor.py --pitched
.venv/bin/python scripts/fetch_war_tuba.py
.venv/bin/python scripts/prepare_samples.py
.venv/bin/python scripts/fetch_mtg_tenor.py
.venv/bin/python scripts/build_tenor_details.py
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 6
ctest --test-dir build --output-on-failure
