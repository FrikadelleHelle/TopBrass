"""Load the built VST3 in the real audit host and render each player/source."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]


def verify(output):
    output.mkdir(parents=True, exist_ok=False)
    cases = []
    selections = [(0, 0, 60, {}), (1, 1, 60, {}), (2, 2, 48, {}), (3, 3, 36, {}),
                  (0, 4, 60, {}), (1, 5, 60, {}), (4, 0, 48, {}),
                  (0, 0, 60, {'alternate_tenor': True}), (0, 4, 60, {'alternate_tenor_ii': True}),
                  (3, 3, 36, {'tuba_voice': 1}), (3, 3, 36, {'tuba_voice': 2}),
                  (0, 0, 60, {'ensemble': True})]
    for i, (instrument, part, root, options) in enumerate(selections):
        directory = output / str(i)
        directory.mkdir()
        cases.append(dict(id=str(i), instrument=instrument, part=part, root=root, velocity=100,
                          articulation=1, rate=48000, frames=96000, horn_feel=0,
                          extra_players=True, directory=str(directory.resolve()), **options))
    config = output / 'config.json'
    config.write_text(json.dumps(dict(cases=cases), indent=2) + '\n')
    host = ROOT / 'build/TopBrassRecordingAudit_artefacts/Release/TopBrass Recording Audit.app/Contents/MacOS/TopBrass Recording Audit'
    plugin = ROOT / 'build/TopBrass_artefacts/Release/VST3/TopBrass.vst3'
    subprocess.run([str(host), str(plugin), str(config)], check=True)
    version = re.search(r'project\(TopBrass VERSION ([0-9.]+)', (ROOT / 'CMakeLists.txt').read_text())[1]
    results = []
    for case in cases:
        for variant in ('neutral', 'current-tone'):
            directory = Path(case['directory'])
            audio, rate = sf.read(directory / (variant + '-plugin-raw.wav'), always_2d=True)
            metadata = json.loads((directory / (variant + '-host.json')).read_text())
            peak = float(np.max(np.abs(audio)))
            if rate != 48000 or audio.shape[1] != 2 or not np.isfinite(audio).all() or peak <= 0 or metadata['version'] != version:
                raise ValueError('Invalid VST3 capture: ' + case['id'] + '/' + variant)
            results.append(dict(case=case['id'], variant=variant, peak=peak, version=metadata['version'], frames=len(audio)))
    (output / 'summary.json').write_text(json.dumps(dict(format='VST3', captures=results, passed=True), indent=2) + '\n')
    print(f'PASS: {len(results)} actual VST3 captures across all players and alternate sources', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    verify(parser.parse_args().output)
