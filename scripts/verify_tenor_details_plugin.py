"""Verify actual AU/VST3 explicit tenor-detail playback against original FLAC recordings."""
import argparse
import hashlib
import html
import json
import plistlib
from pathlib import Path
import subprocess

import numpy as np
import soundfile as sf

ROOT=Path(__file__).resolve().parents[1]


def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def verify(output,plugins):
    if output.exists():raise ValueError('Choose a new output folder to retain prior captures')
    output.mkdir(parents=True)
    sources={kind:ROOT/f'assets/cache/MTG.SoloSax/MTG Solo Saxophones/Samples/ten_{prefix}_01.flac' for kind,prefix in [('breath','b'),('keys','k')]}
    host=ROOT/'build/TopBrassRecordingAudit_artefacts/Release/TopBrass Recording Audit.app/Contents/MacOS/TopBrass Recording Audit'
    versions={plistlib.loads((plugin/'Contents/Info.plist').read_bytes())['CFBundleShortVersionString'] for plugin in plugins.values()}
    assert len(versions)==1;version=versions.pop()
    records=[]
    for format,plugin in plugins.items():
        cases=[]
        for kind,source in sources.items():
            audio,rate=sf.read(source,dtype='float32',always_2d=True);assert audio.shape[1]==1 and rate==48000
            for mode in ['layered','groove']:
                identifier=f'{kind}-{mode}';directory=output/format/identifier;directory.mkdir(parents=True)
                controller=20 if kind=='breath' else 21
                events=[[0,176,11,0],[0,176,1,127],[0,224,127,127],[128,176,controller,127],[512,176,controller,127],[len(audio)+384,176,120,0]]
                cases.append(dict(id=identifier,instrument=0,part=0,articulation=0,root=60,velocity=127,rate=rate,frames=len(audio)+1024,post_release_frames=0,directory=str(directory),ensemble=mode=='groove',tenor_detail_midi=True,tenor_detail_db=0,midi_events=events))
        config=output/format/'config.json';config.write_text(json.dumps(dict(cases=cases),indent=2)+'\n')
        with (output/format/'capture.log').open('w') as log:subprocess.run([str(host),str(plugin),str(config)],stdout=log,stderr=subprocess.STDOUT,check=True)
        identity=dict(format=format,plugin=str(plugin),executable_sha256=sha(plugin/'Contents/MacOS/TopBrass'),detail_bank_sha256=sha(plugin/'Contents/Resources/TopBrass.tenor'))
        assert identity['detail_bank_sha256']==sha(ROOT/'assets/TopBrass.tenor')
        for case in cases:
            kind=case['id'].split('-')[0];original,rate=sf.read(sources[kind],dtype='float32',always_2d=True)
            expected=np.zeros((case['frames'],2));expected[128:128+len(original)]=np.repeat(original,2,axis=1)
            for variant in ['neutral','current-tone']:
                path=Path(case['directory'])/(variant+'-plugin-raw.wav');played,sr=sf.read(path,always_2d=True)
                info=json.loads(path.with_name(variant+'-host.json').read_text());assert info['version']==version and sr==rate
                assert played.shape==expected.shape and np.isfinite(played).all()
                gain=float(np.sum(played*expected)/np.sum(expected*expected));assert gain>0
                residual=played/gain-expected;energy=float(np.mean(expected**2))
                relative=float(10*np.log10(max(float(np.mean(residual**2))/energy,1e-30)))
                assert relative<-100,(format,case['id'],variant,relative)
                assert np.max(np.abs(played[:128]))==0 and np.max(np.abs(played[128+len(original):]))==0
                expected_gain=10**(-24/20)/np.sqrt(2);assert abs(20*np.log10(gain/expected_gain))<.0001
                records.append(dict(format=format,case=case['id'],variant=variant,residual_relative_db=relative,raw_gain_db=float(20*np.log10(gain)),raw_sha256=sha(path),path=str(path),source_sha256=sha(sources[kind]),identity=identity,version=info['version']))
    pairs=[]
    for case in {row['case'] for row in records}:
        members=[row for row in records if row['case']==case]
        audios=[sf.read(row['path'],dtype='float32',always_2d=True)[0] for row in members]
        assert all(np.array_equal(audios[0],audio) for audio in audios[1:]),case
        pairs.append(dict(case=case,au_vst3_and_tone_variants_bit_identical=True))
    content='''<!doctype html><html lang="en"><meta charset="utf-8"><title>Installed tenor detail playback</title><style>body{font:18px/1.6 system-ui;background:#151719;color:#eee;max-width:900px;margin:40px auto;padding:20px}audio{width:100%}a{color:#dfbb75}</style><h1>Recorded tenor detail in TopBrassVERSION</h1><p>Actual installed AU and VST3 playback, triggered by MIDI controllers. Source breath and key recordings are retained at native pitch and rate. The same additional36dB listening gain is applied to originals and source-level plugin playback to expose the quiet detail. This is an audition level, not per-file normalization.</p>'''
    for kind,source in sources.items():
        original,rate=sf.read(source,dtype='float32',always_2d=True);row=next(row for row in records if row['format']=='VST3' and row['case']==kind+'-layered' and row['variant']=='neutral')
        played,_=sf.read(row['path'],always_2d=True);gain=10**(36/20)
        original=np.pad(original,((128,1024-128),(0,0)))
        for label,audio in [('original',original),('vst3',played/(10**(-24/20)/np.sqrt(2)))]:
            assert np.max(np.abs(audio*gain))<1
            sf.write(output/f'{kind}-{label}-listen.wav',audio*gain,rate,subtype='FLOAT')
        content+=f'<h2>{html.escape(kind.title())}: original</h2><audio controls preload="none" src="{kind}-original-listen.wav"></audio><h2>Same recording through installed VST3</h2><audio controls preload="none" src="{kind}-vst3-listen.wav"></audio>'
    content=content.replace('TopBrassVERSION','TopBrass'+version)
    content+='''<p>Enable Tenor detail MIDI to use CC20 for breath and CC21 for keys. Send0 then a nonzero value for each trigger. The GUI buttons work directly. Main horn notes do not automatically generate these sounds. They come from MTG SoloSax, a different tenor source than the main VCSL musical recordings.</p><p>Both formats and both Tone settings produce identical detail audio in Layered and Groove. Pressure, vibrato and pitch bend were active in the MIDI test and do not alter these one-shots.</p><p>Samples: Music Technology Group, Universitat Pompeu Fabra; SFZ preparation: kinwie. CC BY4.0. <a href="results.json">Exact measurements and identities</a> · <a href="../">Listening bench</a>.</p></html>'''
    report=dict(version=version,captures=records,comparisons=pairs,capture_host_sha256=sha(host),all_passed=True,automatic_phrase_breathing=False)
    (output/'results.json').write_text(json.dumps(report,indent=2)+'\n');(output/'index.html').write_text(content)
    print('PASS:16 actual AU/VST3 captures; all original-source matches, exact MIDI offsets, held-controller suppression and tone/format identity')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();home=Path.home()
    verify(args.output.resolve(),{'AU':home/'Library/Audio/Plug-Ins/Components/TopBrass.component','VST3':home/'Library/Audio/Plug-Ins/VST3/TopBrass.vst3'})
