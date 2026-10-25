"""Compare actual installed AU/VST3 captures without gain or time correction."""
import argparse
import html
import json
from pathlib import Path
import subprocess

import numpy as np
import soundfile as sf

from recording_audit import ROOT, digest, prepare


def section_events():
    events = [[0, 176, 11, 100], [0, 176, 1, 0], [0, 224, 0, 64]]
    events += [[128 + i*48, 144, note, velocity] for i, (note, velocity) in enumerate([(60, 110), (63, 98), (67, 106)])]
    events += [[12000, 176, 64, 127]]
    events += [[24000, 128, note, 0] for note in (60, 63, 67)]
    events += [[36000, 176, 1, 48], [48000, 176, 11, 56], [72000, 224, 62, 71],
               [96000, 176, 11, 110], [120000, 176, 1, 0], [132000, 176, 64, 0]]
    events += [[144128 + i*48, 144, note, 114] for i, note in enumerate((62, 65, 69))]
    events += [[163200, 128, note, 0] for note in (62, 65, 69)]
    events += [[177600, 176, 120, 0]]
    return events


def capture(plugin, output):
    config = prepare(output, plugin)
    scenes=[(0, 'section-tenor'), (4, 'section-baritone-detail')]
    if any(case.get('double_tongue') for case in config['cases']):scenes.append((0, 'section-double-tongue'))
    for instrument, identifier in scenes:
        directory = output / identifier
        directory.mkdir(exist_ok=True)
        config['cases'].append(dict(id=identifier, ensemble=True, instrument=instrument,
                                   part=0, articulation=1 if identifier=='section-double-tongue' else 0, double_tongue=identifier=='section-double-tongue', rate=48000, frames=192000,
                                   keys=.5 if instrument == 4 else 0, airflow=.5 if instrument == 4 else 0,
                                   directory=str(directory.resolve()), midi_events=section_events()))
    for source_id,part,identifier in [('tenor-sax-sustain',4,'second-sax-solo'),('trumpet-sustain',5,'second-trumpet-solo')]:
        source=next(case for case in config['cases'] if case['id']==source_id)
        directory=output/identifier;directory.mkdir(exist_ok=True)
        config['cases'].append(source|dict(id=identifier,part=part,extra_players=True,directory=str(directory.resolve())))
    directory=output/'section-six-players';directory.mkdir(exist_ok=True)
    config['cases'].append(dict(id='section-six-players',ensemble=True,extra_players=True,instrument=0,part=0,articulation=0,rate=48000,frames=192000,directory=str(directory.resolve()),midi_events=section_events()))
    for source_id,instrument,part,label in [('trumpet-short',1,1,'trumpet'),('baritone-sax-short',4,0,'baritone')]:
        source=next(case for case in config['cases'] if case['id']==source_id)
        identifier=f'paired-{label}-short';directory=output/identifier;directory.mkdir(exist_ok=True)
        config['cases'].append(source|dict(id=identifier,paired_players=True,split_players=True,extra_players=True,directory=str(directory.resolve())))
        identifier=f'section-paired-{label}-short';directory=output/identifier;directory.mkdir(exist_ok=True)
        config['cases'].append(dict(id=identifier,ensemble=True,paired_players=True,extra_players=True,instrument=instrument,part=part,articulation=1,rate=48000,frames=192000,directory=str(directory.resolve()),midi_events=section_events()))
    config['description'] = 'Native solos, independently addressed second sax/trumpet, four/six-player Groove, and paired recorded-take Short captures, with pedal, expression, vibrato, bend and panic.'
    (output / 'config.json').write_text(json.dumps(config, indent=2) + '\n')
    host = ROOT / 'build/TopBrassRecordingAudit_artefacts/Release/TopBrass Recording Audit.app/Contents/MacOS/TopBrass Recording Audit'
    subprocess.run([str(host), str(plugin), str(output / 'config.json')], check=True)
    if digest(plugin / 'Contents/MacOS/TopBrass') != config['plugin_executable_sha256']:
        raise ValueError('Plugin executable changed during capture')
    if digest(plugin / 'Contents/Resources/TopBrass.bank') != config['bank_sha256']:
        raise ValueError('Plugin bank changed during capture')
    return config


def verify(au, vst, output):
    output.mkdir(parents=True, exist_ok=True)
    configs = {name: capture(plugin, output/name) for name, plugin in [('AU', au), ('VST3', vst)]}
    if configs['AU']['bank_sha256'] != configs['VST3']['bank_sha256']:
        raise ValueError('The two formats do not contain the same bank')
    comparisons = []
    versions = set()
    for case in configs['AU']['cases']:
        for variant in ('neutral', 'current-tone'):
            captures = {}
            for name in configs:
                base = output / name / case['id']
                path = base / (variant + '-plugin-raw.wav')
                audio, rate = sf.read(path, always_2d=True)
                info = json.loads((base / (variant + '-host.json')).read_text())
                if info['format'] != ('AudioUnit' if name == 'AU' else 'VST3'):
                    raise ValueError('Capture used the wrong plugin format')
                versions.add(info['version'])
                if not np.isfinite(audio).all() or np.max(np.abs(audio)) < 1e-9:
                    raise ValueError('Capture is silent or non-finite')
                if case.get('ensemble'):
                    if np.any(audio[:128] != 0) or np.max(np.abs(audio[128:1024])) == 0:
                        raise ValueError('Section onset did not preserve its MIDI offset')
                    if np.any(audio[177600:] != 0):
                        raise ValueError('Panic left a sounding or scheduled section note')
                captures[name] = (audio, rate, digest(path))
            a, ar, ah = captures['AU']
            b, br, bh = captures['VST3']
            if ar != br or a.shape != b.shape:
                raise ValueError('Formats returned different sample rates or dimensions')
            error = float(np.max(np.abs(a-b)))
            if error > 1e-6:
                raise ValueError(f"Format audio differs for {case['id']}/{variant}: {error}")
            comparisons.append(dict(case=case['id'], variant=variant, rate=ar, frames=len(a),
                                    bit_identical=bool(np.array_equal(a, b)), max_error=error,
                                    au_sha256=ah, vst3_sha256=bh))
    for format in configs:
        for secondary,primary in [('second-sax-solo','tenor-sax-sustain'),('second-trumpet-solo','trumpet-sustain')]:
            for variant in ['neutral','current-tone']:
                a,ar=sf.read(output/format/secondary/(variant+'-plugin-raw.wav'),always_2d=True)
                b,br=sf.read(output/format/primary/(variant+'-plugin-raw.wav'),always_2d=True)
                if ar!=br or not np.array_equal(a,b):raise ValueError('Independent duplicate player changed source playback: '+secondary)
    if len(versions) != 1:
        raise ValueError('The two formats report different versions')
    report = dict(version=versions.pop(), comparisons=comparisons,
                  bit_identical=all(row['bit_identical'] for row in comparisons),
                  maximum_error=max(row['max_error'] for row in comparisons),
                  plugin_identities={name: {k: config[k] for k in ('plugin_path', 'plugin_format', 'plugin_executable_sha256', 'bank_sha256')} for name, config in configs.items()},
                  method='Raw decoded samples; no gain, alignment, EQ or pitch correction.',
                  limits='This qualifies the exercised AU/VST3 paths in this test host, not live Logic Pro/MainStage sessions or musical realism.')
    (output/'results.json').write_text(json.dumps(report, indent=2) + '\n')
    sections = []
    for identifier, label in [('section-tenor', 'Four-player section with tenor'), ('section-baritone-detail', 'Four-player section with baritone keys and airflow'), ('section-double-tongue', 'Double-tongue switches: original recorded timing'), ('section-six-players','Six players: two saxes, two trumpets, trombone and tuba')]:
        if identifier not in {row['case'] for row in comparisons}:continue
        players = ''.join(f'<p>{name}</p><audio controls preload="none" src="{name}/{identifier}/neutral-plugin-raw.wav"></audio>' for name in configs)
        sections.append(f'<section><h2>{label}</h2>{players}</section>')
    page = '''<!doctype html><meta charset="utf-8"><title>TopBrass AU/VST3 comparison</title><style>body{font:18px system-ui;background:#151719;color:#eee;max-width:850px;margin:40px auto;padding:20px;line-height:1.6}audio{width:100%}section{border-top:1px solid #555;padding:15px 0}a{color:#dfbb75}</style><h1>AU and VST3: same MIDI, same audio</h1>'''
    page += f'<p>{len(comparisons)} pairs. All bit-identical: {report["bit_identical"]}. Maximum sample difference: {report["maximum_error"]}.</p>'
    page += '<p>Native solo source cases and section phrases, each with neutral and warm Tone settings. Section phrases include expression, vibrato, pitch bend, pedal, staggered chord entry and panic. These are raw exports with identical settings.</p>' + ''.join(sections)
    page += '<p>' + html.escape(report['limits']) + '</p><p><a href="results.json">Exact comparisons and installed plugin identities</a></p>'
    (output/'index.html').write_text(page)
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--au', type=Path, default=Path.home()/'Library/Audio/Plug-Ins/Components/TopBrass.component')
    parser.add_argument('--vst3', type=Path, default=Path.home()/'Library/Audio/Plug-Ins/VST3/TopBrass.vst3')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = verify(args.au.resolve(), args.vst3.resolve(), args.output.resolve())
    print(f"PASS: {len(report['comparisons'])} AU/VST3 pairs, maximum raw sample error {report['maximum_error']}")
