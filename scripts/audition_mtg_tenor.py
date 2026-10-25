"""Qualify all pinned MTG tenor recordings through a private installed-VST clone."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

import numpy as np
import soundfile as sf

from audition_weresax import clone, git_blob, sha
from build_bank import sustain_release
from fetch_mtg_tenor import CACHE, COMMIT, REPO
from recording_audit import compare_arrays, plot_pair, stereo

ROOT = Path(__file__).resolve().parents[1]


def unsigned_executable_sha(path):
    with tempfile.TemporaryDirectory(prefix='topbrass-code-identity-') as directory:
        copy = Path(directory)/'TopBrass'
        shutil.copy2(path, copy)
        subprocess.run(['codesign', '--remove-signature', str(copy)], check=True)
        return sha(copy)


def sources():
    tree = json.loads((CACHE / 'source-tree.json').read_text())
    assert tree['repo'] == REPO and tree['commit'] == COMMIT
    entries = {entry['path']: entry for entry in tree['tree']}

    def checked(path):
        data = (CACHE / path).read_bytes()
        assert git_blob(data) == entries[path]['sha'], path
        return data

    group = checked('MTG Solo Saxophones/Data/group_ten.txt').decode()
    assert re.findall(r'(hivel|lovel)=(\d+)', group) == [('hivel', '100'), ('lovel', '101')]
    rows = []
    for force in ('p', 'f'):
        mapping = checked(f'MTG Solo Saxophones/Data/ten_{force}_rr1.txt').decode()
        for body in re.findall(r'<region>([^<]*)', mapping):
            attrs = dict(re.findall(r'(\w+)=([^\s]+)', body))
            key = int(attrs['key'])
            path = 'MTG Solo Saxophones/Samples/' + attrs['sample'].replace('$EXT', 'flac')
            data = checked(path)
            audio, rate = sf.read(CACHE / path, dtype='float32', always_2d=True)
            assert rate == 48000 and audio.shape[1] == 1 and np.isfinite(audio).all()
            assert 8 < len(audio) < rate * 60 and 0 < np.max(np.abs(audio)) <= 1
            release = sustain_release(audio, rate, 0)
            assert release is not None, path
            rows.append(dict(path=path, key=key, force=force, rate=rate, frames=len(audio),
                             low_velocity=0 if force == 'p' else 101,
                             high_velocity=100 if force == 'p' else 127,
                             tuning_cents=float(attrs.get('tune', 0)),
                             volume_db=float(attrs.get('volume', 0)), release_selection=release,
                             file_sha256=hashlib.sha256(data).hexdigest(),
                             decoded_pcm_sha256=hashlib.sha256(audio.T.astype('<f4').tobytes()).hexdigest(),
                             last_to_whole_rms_db=float(10*np.log10(np.mean(audio[-960:].astype(float)**2) / np.mean(audio.astype(float)**2)))))
    assert len(rows) == 66
    assert {(r['key'], r['force']) for r in rows} == {(k, f) for k in range(44, 77) for f in ('p', 'f')}
    return rows


def pack(rows, folder, musical):
    folder.mkdir()
    path = folder / 'TopBrass.bank'
    with path.open('wb') as stream:
        stream.write(b'TBBANK04' + struct.pack('<2I', 77, 66))
        for row in rows:
            audio, rate = sf.read(CACHE / row['path'], dtype='float32', always_2d=True)
            release = row['release_selection']
            header = [0, 0, row['key'], 1, rate, len(audio), 0, 0, 0,
                      release['start'] if musical else 0, release['crossfade'] if musical else 0, 0, 0, 0, 1]
            stream.write(struct.pack('<15I', *header))
            stream.write(audio.T.astype('<f4').tobytes())
        for instrument in range(5):
            for articulation in range(4 if instrument == 4 else 2):
                if (instrument, articulation) == (0, 0):
                    continue
                stream.write(struct.pack('<15I', instrument, articulation, 60, 1, 48000, 8, *([0]*8), 1))
                stream.write(bytes(32))
        for index, row in enumerate(rows):
            stream.write(struct.pack('<10I2f', index, 0, 0, row['key'], row['key'], row['key'],
                                     row['low_velocity'], row['high_velocity'], 0, 0,
                                     row['tuning_cents'] if musical else 0,
                                     10**(row['volume_db']/20) if musical else 1))
    with path.open('rb') as stream:
        assert stream.read(16) == b'TBBANK04' + struct.pack('<2I', 77, 66)
        for index in range(77):
            header = struct.unpack('<15I', stream.read(60))
            pcm = stream.read(header[5]*header[14]*4)
            if index < 66:
                row = rows[index]
                assert header[:9] == (0, 0, row['key'], 1, row['rate'], row['frames'], 0, 0, 0)
                expected = row['release_selection']
                assert header[9:11] == ((expected['start'], expected['crossfade']) if musical else (0, 0))
                assert hashlib.sha256(pcm).hexdigest() == row['decoded_pcm_sha256']
            else:
                assert pcm == bytes(32)
        for index, row in enumerate(rows):
            zone = struct.unpack('<10I2f', stream.read(48))
            assert zone[:10] == (index, 0, 0, row['key'], row['key'], row['key'], row['low_velocity'], row['high_velocity'], 0, 0)
            assert zone[10] == (row['tuning_cents'] if musical else 0)
            assert np.isclose(zone[11], 10**(row['volume_db']/20) if musical else 1, rtol=1e-7)
        assert stream.read() == b''
    return path


def case(identifier, directory, frames, key=60, velocity=100, events=None):
    directory.mkdir(parents=True)
    result = dict(id=identifier, directory=str(directory), frames=frames, post_release_frames=0,
                  rate=48000, instrument=0, part=0, articulation=0, root=key, velocity=velocity)
    if events is not None:
        result['midi_events'] = events
    return result


def render(host, plugin, cases, folder):
    config = folder / 'config.json'
    config.write_text(json.dumps(dict(cases=cases), indent=2) + '\n')
    with (folder / 'host.log').open('w') as log:
        subprocess.run([str(host), str(plugin), str(config)], stdout=log, stderr=subprocess.STDOUT, check=True)


def phrase_events(force):
    events = [[0, 176, 11, 127], [0, 176, 1, 0]]
    cursor = 0
    for key, duration, velocity in [(60,.32,86), (63,.16,91), (65,.26,98), (67,.18,82),
                                     (65,.14,72), (63,.48,95), (60,.72,78), (58,.18,87), (60,.65,100)]:
        velocity = velocity if force == 'soft' else min(127, velocity+28)
        end = cursor + round(duration*48000)
        events += [[cursor,144,key,velocity], [end,128,key,0]]
        cursor = end + 4800
    return events, cursor + 48000


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--plugin', type=Path, default=Path.home()/'Library/Audio/Plug-Ins/VST3/TopBrass.vst3')
    parser.add_argument('--host', type=Path, default=ROOT/'artifacts/v0.15.0/source-snapshot/build/TopBrassRecordingAudit_artefacts/Release/TopBrass Recording Audit.app/Contents/MacOS/TopBrass Recording Audit')
    args = parser.parse_args()
    output, plugin, host = args.output.resolve(), args.plugin.resolve(), args.host.resolve()
    if output.exists():
        raise ValueError('Choose a new output directory to preserve previous evidence')
    release = json.loads((ROOT/'artifacts/validation-summary.json').read_text())
    installed = next(row for row in release['bundles'] if row['format'] == 'VST3')
    identity = sha(plugin/'Contents/MacOS/TopBrass')
    assert identity == installed['executable_sha256']
    code_identity = unsigned_executable_sha(plugin/'Contents/MacOS/TopBrass')
    resources = {name: sha(plugin/'Contents/Resources'/name) for name in installed['resources']}
    assert resources == installed['resources']
    rows = sources()
    output.mkdir(parents=True)
    (output/'sources.json').write_text(json.dumps(rows, indent=2)+'\n')
    variants = {}
    for name, musical in [('raw', False), ('musical', True)]:
        folder = output/name
        pack(rows, folder, musical)
        variants[name] = clone(plugin, folder)
        assert unsigned_executable_sha(variants[name]/'Contents/MacOS/TopBrass') == code_identity
    cases = [case(f"key-{r['key']}-{r['force']}", output/'raw'/f"key-{r['key']}-{r['force']}",
                  r['frames']+512, r['key'], 100 if r['force']=='p' else 127) for r in rows]
    render(host, variants['raw'], cases, output/'raw')
    checks = []
    for row, item in zip(rows, cases):
        original, rate = sf.read(CACHE/row['path'], always_2d=True)
        audio, sr = sf.read(Path(item['directory'])/'neutral-plugin-raw.wav', always_2d=True)
        assert rate == sr and len(audio) == row['frames']+512
        metrics = compare_arrays(original, audio[:len(original)], rate)
        assert metrics['transparency_pass'] and metrics['residual_relative_db'] < -100, (row['path'], metrics)
        assert np.max(np.abs(audio[len(original):])) == 0
        checks.append(dict(source=row['path'], key=row['key'], force=row['force'], metrics=metrics))
        if row['key'] in (48, 60, 72):
            directory = Path(item['directory'])
            sf.write(directory/'original-native.wav', original, rate, subtype='FLOAT')
            matched = audio[:len(original)]*metrics['gain_correction']
            sf.write(directory/'plugin-level-matched.wav', matched, rate, subtype='FLOAT')
            plot_pair(stereo(original), matched, rate, metrics, 0, len(original), directory/'analysis.png',
                      f"MTG tenor MIDI {row['key']}, {row['force']}, complete source vs actual VST")
    (output/'checks.json').write_text(json.dumps(checks, indent=2)+'\n')
    for name in ('musical', 'current'):
        folder = output/name
        folder.mkdir(exist_ok=True)
        phrases = []
        for force in ('soft', 'strong'):
            events, frames = phrase_events(force)
            phrases.append(case(force, folder/force, frames, events=events))
        render(host, variants['musical'] if name=='musical' else plugin, phrases, folder)
    gain = 10**(18/20)
    listening = []
    for name in ('musical', 'current'):
        for force in ('soft', 'strong'):
            raw = output/name/force/'neutral-plugin-raw.wav'
            audio, rate = sf.read(raw, always_2d=True)
            assert np.max(np.abs(audio*gain)) < 1
            target = output/f'{name}-{force}.wav'
            sf.write(target, audio*gain, rate, subtype='FLOAT')
            listening.append(dict(file=target.name, source_sha256=sha(raw), gain_db=18,
                                  file_sha256=sha(target), peak=float(np.max(np.abs(audio*gain)))))
    refs = output/'source-mapping'
    refs.mkdir()
    for path in ('LICENSE', 'README.md', 'MTG Solo Saxophones/Data/group_ten.txt',
                 'MTG Solo Saxophones/Data/ten_p_rr1.txt', 'MTG Solo Saxophones/Data/ten_f_rr1.txt'):
        source = CACHE/path
        if source.exists():
            shutil.copy2(source, refs/source.name)
    assert sha(plugin/'Contents/MacOS/TopBrass') == identity
    assert resources == {name: sha(plugin/'Contents/Resources'/name) for name in resources}
    report = dict(repository=REPO, commit=COMMIT, version=release['version'], installed_executable_sha256=identity,
                  unsigned_executable_sha256=code_identity,
                  clone_executable_sha256={name:sha(path/'Contents/MacOS/TopBrass') for name,path in variants.items()},
                  installed_resources=resources, host_sha256=sha(host), recordings=66, native_keys=[44,76],
                  complete_source_pcm_checks=len(checks), worst_residual_db=max(c['metrics']['residual_relative_db'] for c in checks),
                  candidate_bank_sha256={name:sha(output/name/'TopBrass.bank') for name in variants}, listening=listening,
                  installed_resources_changed=False, musical_realism_accepted=False,
                  limits=['Finite sustain recordings, not recorded short articulations or interval transitions.',
                          'Publisher loops are not used. Held notes finish after the complete source file.',
                          'Musical candidate imports authored per-note pitch/level and detected same-file decay coordinates.',
                          'Raw fidelity candidate uses unity mapping gain and zero tuning, with complete native-rate PCM.',
                          'Other instruments and articulations in private clones are silent diagnostic coverage fixtures.',
                          'Source SFZ filters, offsets, envelopes, velocity response, LFOs and neighbor-note round robins are not recreated.',
                          'Note-off musical crossfades and the engine final 3ms ending fade differ from uninterrupted original playback.'])
    (output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    doc = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Complete tenor source comparison</title><style>body{font:18px/1.6 system-ui;background:#151719;color:#eee;max-width:960px;margin:40px auto;padding:20px}audio,img{width:100%}a{color:#dfbb75}section{border-top:1px solid #555;padding-top:16px;margin-top:28px}</style><h1>Complete tenor source comparison</h1><p>First hear the recordings themselves. Then compare the same MIDI through the current tenor and the MTG candidate. Your installed instrument and patch are unchanged.</p><p>66 original recordings cover MIDI 44 through 76, with soft and strong playing at every key. Source samples by Music Technology Group, Universitat Pompeu Fabra. SFZ preparation by kinwie. <a href="source-mapping/LICENSE">CC BY 4.0</a>.</p><h2>1. Original recording, then that complete recording through VST</h2><p>Original recorded levels. The adjacent VST export removes only one constant gain. Native pitch and rate, no EQ or extra vibrato. These six examples include the complete attacks and recorded-file decays.</p>'''
    for row in rows:
        if row['key'] not in (48,60,72):
            continue
        path = f"raw/key-{row['key']}-{row['force']}"
        doc += f'''<section><h3>MIDI {row['key']}, {'soft' if row['force']=='p' else 'strong'}</h3><p>Original</p><audio controls preload="none" src="{path}/original-native.wav"></audio><p>Actual VST, constant level matched</p><audio controls preload="none" src="{path}/plugin-level-matched.wav"></audio><details><summary>Spectrograms and frequency response</summary><img loading="lazy" src="{path}/analysis.png" alt="Original and VST spectrograms, spectrum, envelope and residual"></details></section>'''
    doc += '<h2>2. Same MIDI phrase through both sources</h2><p>One fixed +18 dB listening gain for all four files, with no per-note normalization or EQ. Candidate uses the source author pitch and level corrections plus detected recorded-file endings. The phrase uses gated sustains, not recorded staccatos. Different source levels and our velocity curve affect loudness.</p>'
    for force in ('soft', 'strong'):
        for name, label in [('current','Current VCSL tenor'), ('musical','MTG tenor candidate')]:
            doc += f'<section><h3>{html.escape(label)}, {force} phrase</h3><audio controls preload="none" src="{name}-{force}.wav"></audio></section>'
    doc += '<h2>What the checks establish</h2><p>All 66 complete native recordings pass the source-to-VST comparison. This establishes playback fidelity, not whether you like the recorded sax tone. No new source has been selected for your installed instrument.</p><p>The candidate has no dedicated short notes or recorded legato transitions. Its sustained notes end after about four seconds because this comparison does not use loops. Musical note-offs crossfade into detected file decays; that is not evidence of a recorded transition between two notes.</p><p><a href="report.json">Capture identities and limits</a> · <a href="sources.json">All original mappings and measurements</a> · <a href="checks.json">All 66 complete-source comparisons</a> · <a href="../">Listening bench</a></p><script>document.addEventListener("play",e=>{if(e.target.tagName==="AUDIO")for(const a of document.querySelectorAll("audio"))if(a!==e.target)a.pause()},true)</script></html>'
    (output/'index.html').write_text(doc)
    print(json.dumps(dict(recordings=len(rows), worst_residual_db=report['worst_residual_db'], output=str(output)), indent=2))


if __name__ == '__main__':
    main()
