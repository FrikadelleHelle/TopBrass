#!/usr/bin/env python3
"""Compare player revisions using the preserved CC0 reference recordings."""
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile
import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'artifacts/natural-playback'


def rms(x):
    return float(np.sqrt(np.mean(x * x)))


def render(name, source, header, airflow=None):
    stage = OUT / name
    stage.mkdir(parents=True, exist_ok=True)
    (stage / 'Engine.cpp').write_text(source)
    (stage / 'Engine.h').write_text(header)
    extra=[]
    if airflow is not None:
        (stage/'Airflow.cpp').write_text(airflow[0]); (stage/'Airflow.h').write_text(airflow[1])
        extra=[str(stage/'Airflow.cpp')]
    executable = stage / 'probe'
    subprocess.run(['c++', '-std=c++20', '-O3', '-DNDEBUG', '-I', str(stage),
                    str(stage / 'Engine.cpp'), *extra, str(ROOT / 'Tools/SourceProbe.cpp'), '-o', str(executable)], check=True)
    subprocess.run([str(executable), str(ROOT / 'assets/cache/cc0-reference.bank'), str(OUT), name], check=True)
    return hashlib.sha256(source.encode()).hexdigest()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    versions = {'current': render('current', (ROOT / 'Source/Engine.cpp').read_text(), (ROOT / 'Source/Engine.h').read_text(), ((ROOT/'Source/Airflow.cpp').read_text(),(ROOT/'Source/Airflow.h').read_text()))}
    baseline = ROOT / 'artifacts/v0.2-rejected/source.tar'
    if baseline.exists():
        with tarfile.open(baseline) as archive:
            versions['baseline'] = render('baseline', archive.extractfile('Source/Engine.cpp').read().decode(),
                                          archive.extractfile('Source/Engine.h').read().decode())
    lock = json.loads((ROOT / 'assets/samples.lock.json').read_text())
    report = dict(source_sha256=versions, cases={}, listening_acceptance='not established')
    for instrument, root, label in ((4, 43, 'baritone'), (1, 60, 'trumpet')):
        entries = [e for e in lock if e['instrument'] == instrument and e['root'] == root and e['articulation'] == 1]
        entry = sorted(entries, key=lambda e: (-e['layer'], e['path']))[0]
        source_file = ROOT / 'assets/cache' / entry['repo'].split('/')[-1] / entry['path']
        raw, rate = sf.read(source_file, always_2d=True)
        sf.write(OUT / f'{label}-original.wav', raw, rate, subtype='PCM_24')
        measurements = {}
        for version in versions:
            held, sr = sf.read(OUT / f'{label}-{version}-held.wav'); assert sr == rate
            gated, sr = sf.read(OUT / f'{label}-{version}-gated.wav'); assert sr == rate
            measurements[version] = dict(first_5ms_rms=rms(held[:int(rate * .005)]),
                                         gated_held_max_difference=float(np.max(np.abs(held - gated))))
        stages = (['baseline-held'] if 'baseline' in versions else []) + ['current-held', 'original']
        segments = []
        for stage in stages:
            x, sr = sf.read(OUT / f'{label}-{stage}.wav', always_2d=True); assert sr == rate
            if x.shape[1] == 1:
                x = np.repeat(x, 2, axis=1)
            y = np.zeros((rate * 2, 2)); y[:min(len(x), len(y))] = x[:len(y)]
            gain = min(.08 / max(rms(y[:rate//2]), 1e-12), .9 / max(float(np.max(np.abs(y))), 1e-12))
            segments.extend((y * gain, np.zeros((rate//2, 2))))
        sf.write(OUT / f'{label}-comparison.wav', np.concatenate(segments), rate, subtype='PCM_24')
        report['cases'][label] = dict(source=entry, measurements=measurements, comparison_order=stages)
        assert measurements['current']['gated_held_max_difference'] == 0, 'Natural shorts were truncated by note-off'
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    (OUT / 'README.txt').write_text(
        'Comparisons use energy-matched notes. Order: rejected v0.2 when available, current player, original source.\n'
        'The same short performance is used at native pitch. No pitch shifting or arrangement changes.\n'
        'This establishes an audible comparison, not a claim of realism.\n')
    print('Wrote source/player comparisons to', OUT)


if __name__ == '__main__':
    main()
