"""Identify the original recordings used by paired players in actual plug-in captures."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import struct
import subprocess

import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def recordings(path):
    result = []
    with path.open('rb') as stream:
        assert stream.read(8) == b'TBBANK04'
        count, zone_count = struct.unpack('<2I', stream.read(8))
        for index in range(count):
            header = struct.unpack('<15I', stream.read(60))
            wanted = header[0] in (1, 4) and header[1] == 1 and header[2] == 60 and header[3] == 3
            size = header[5]*header[14]*4
            if wanted:
                audio = np.frombuffer(stream.read(size), dtype='<f4').reshape(header[14], header[5]).T.astype(float)
                gains = np.frombuffer(stream.read(header[13]*4), dtype='<f4')
                if len(gains):
                    positions = np.minimum(np.floor((np.arange(len(audio))+header[12])/32+.5).astype(int), len(gains)-1)
                    audio *= gains[positions, None]
                result.append(dict(index=index, instrument=header[0], audio=audio, rate=header[4]))
            else:
                stream.seek(size+header[13]*4, 1)
        zones = [struct.unpack('<10I2f', stream.read(48)) for _ in range(zone_count)]
        assert stream.read() == b''
    eligible = sorted((zone for zone in zones if zone[1:3] == (1,1) and zone[4] <= 60 <= zone[5] and zone[6] <= 100 <= zone[7] and zone[8] == 0), key=lambda zone:zone[9])
    assert len(eligible) >= 2 and all(zone[3] == 60 and zone[10] == 0 for zone in eligible)
    by_index = {row['index']:row for row in result}
    trumpet = [by_index[zone[0]] for zone in eligible]
    baritone = [row for row in result if row['instrument'] == 4]
    assert len(baritone) >= 2 and all(len(row['audio']) >= 4096 for row in trumpet+baritone)
    assert all(len({row['rate'] for row in takes}) == 1 for takes in (trumpet, baritone))
    return dict(trumpet=trumpet, baritone=baritone)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=Path, required=True)
    parser.add_argument('--after', type=Path, required=True)
    parser.add_argument('--host', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        raise ValueError('Choose a new capture directory')
    output.mkdir(parents=True)
    plugins = dict(before=args.before.resolve(), after=args.after.resolve())
    banks = {name:sha(path/'Contents/Resources/TopBrass.bank') for name,path in plugins.items()}
    assert len(set(banks.values())) == 1
    identities = {name:dict(path=str(path), executable_sha256=sha(path/'Contents/MacOS/TopBrass'), bank_sha256=banks[name]) for name,path in plugins.items()}
    sources = recordings(plugins['after']/'Contents/Resources/TopBrass.bank')
    native_events = [[0,176,11,127], [0,176,1,0]]
    starts = [128+24000*index for index in range(4)]
    for start in starts:
        native_events += [[start-128,176,120,0], [start,144,60,100]]
    native_events += [[95000,176,120,0]]
    groove_events = [[0,176,11,127], [128,144,60,100], [177600,128,60,0], [190000,176,120,0]]
    versions = {}; captured = []
    for name,plugin in plugins.items():
        folder = output/name; folder.mkdir()
        cases = []
        for label,instrument,part in [('trumpet',1,1),('baritone',4,0)]:
            for kind in ('native','groove'):
                directory = folder/f'{label}-{kind}'; directory.mkdir()
                cases.append(dict(id=f'{label}-{kind}', instrument=instrument, part=part, articulation=1,
                                  paired_players=True, split_players=kind=='native', ensemble=kind=='groove',
                                  rate=sources[label][0]['rate'] if kind=='native' else 48000,
                                  frames=96000 if kind=='native' else 192000, post_release_frames=0,
                                  directory=str(directory), midi_events=native_events if kind=='native' else groove_events))
        config = folder/'config.json'; config.write_text(json.dumps(dict(cases=cases), indent=2)+'\n')
        with (folder/'host.log').open('w') as log:
            subprocess.run([str(args.host.resolve()),str(plugin),str(config)], stdout=log, stderr=subprocess.STDOUT, check=True)
        for item in cases:
            directory = Path(item['directory'])
            info = json.loads((directory/'neutral-host.json').read_text()); versions[name] = info['version']
            audio, rate = sf.read(directory/'neutral-plugin-raw.wav', always_2d=True)
            assert rate == item['rate'] and np.isfinite(audio).all() and np.max(np.abs(audio)) > 0
            assert np.max(np.abs(audio[:128])) == 0
            assert np.max(np.abs(audio[95000 if item['id'].endswith('native') else 190000:])) == 0
            captured.append(dict(version=versions[name], variant=name, case=item['id'], capture_sha256=sha(directory/'neutral-plugin-raw.wav')))
            if item['id'].endswith('groove'):
                gain = 10**(18/20); assert np.max(np.abs(audio*gain)) < 1
                sf.write(folder/(item['id']+'-listen.wav'), audio*gain, rate, subtype='FLOAT')
    matches = []
    for name in plugins:
        for instrument, takes in sources.items():
            audio, _ = sf.read(output/name/f'{instrument}-native/neutral-plugin-raw.wav', always_2d=True)
            for hit,start in enumerate(starts):
                pair = []
                for channel in range(2):
                    played = audio[start:start+4096,channel]
                    candidates = []
                    for take in takes:
                        original = take['audio'][:4096,min(channel,take['audio'].shape[1]-1)]
                        gain = float(np.dot(original,played)/np.dot(original,original))
                        residual = float(10*np.log10(max(np.mean((played-original*gain)**2)/np.mean(played**2),1e-30)))
                        candidates.append((residual,take['index'],gain))
                    residual,index,gain = min(candidates)
                    assert residual < -100 and gain > 0, (name,instrument,hit,channel,candidates)
                    pair.append(index)
                    matches.append(dict(variant=name, instrument=instrument, hit=hit, player=channel+1,
                                        recording=index, residual_relative_db=residual, constant_gain=gain))
                assert (pair[0] == pair[1]) == (name == 'before'), (name,instrument,hit,pair)
    for name,path in plugins.items():
        assert sha(path/'Contents/MacOS/TopBrass') == identities[name]['executable_sha256']
        assert sha(path/'Contents/Resources/TopBrass.bank') == banks[name]
    report = dict(versions=versions, identities=identities, capture_host_sha256=sha(args.host.resolve()),
                  matches=matches, captures=captured, playback_gain_db=18,
                  method='Native split-channel diagnostic, first4096 source frames after each exact MIDI onset; one constant gain removed, no alignment, EQ or tuning correction. Panic between diagnostic notes retains each player repetition counter.',
                  limits='Qualifies distinct eligible recordings for trumpet and baritone Short in the exercised duet. Tenor and trumpet sustains have no extra recorded takes. Musical realism is unaccepted.')
    (output/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    doc = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Paired recorded takes</title><style>body{font:18px/1.6 system-ui;background:#151719;color:#eee;max-width:900px;margin:40px auto;padding:20px}audio{width:100%}a{color:#dfbb75}section{border-top:1px solid #555;padding-top:16px;margin-top:28px}</style><h1>Two players, different recorded takes</h1><p>Both players share the same tight Groove rhythm. Before the fix, paired players with matching repetition histories can use the identical recording. The update chooses another eligible recorded take when one is available. No extra timing spread, detuning, EQ or new samples are added.</p><p>These are paired trumpet and paired baritone Short articulations. Tenor and trumpet sustains still have only one recorded take per pitch and force layer. This change does not create another performer for those sounds.</p>'''
    for instrument in sources:
        doc += f'<section><h2>{html.escape(instrument.title())}, two players</h2>'
        for name in plugins:
            doc += f'<p>{html.escape(name.title())}, version {html.escape(versions[name])}</p><audio controls preload="none" src="{name}/{instrument}-groove-listen.wav"></audio>'
        doc += '</section>'
    doc += '<p>Identical MIDI and neutral Tone/Accent settings, with one fixed +18 dB listening gain on every file. Diagnostic native captures separately identify the selected original recordings. A technical pass does not establish that the section sounds convincing.</p><p><a href="results.json">Recording identities, comparisons and limits</a> · <a href="../">Listening bench</a></p><script>document.addEventListener("play",e=>{if(e.target.tagName==="AUDIO")for(const a of document.querySelectorAll("audio"))if(a!==e.target)a.pause()},true)</script></html>'
    (output/'index.html').write_text(doc)
    print('PASS:',len(matches),'actual plug-in/source identities;',versions)


if __name__ == '__main__':
    main()
