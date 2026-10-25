"""Compare one raw recording with playback through an isolated copy of TopBrass VST3."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import plistlib
import shutil
import struct
import subprocess

import numpy as np
import soundfile as sf

from recording_audit import ROOT, analyze, digest, prepare_pair


def pack_recording(source, destination, key):
    audio,rate=sf.read(source,dtype='float32',always_2d=True)
    if not 0<=key<=127:raise ValueError('Playback anchor must be a MIDI key from 0 to 127')
    if not 8000<=rate<=192000 or not 2048<=len(audio)<=rate*60 or audio.shape[1] not in(1,2):
        raise ValueError('Spectral audition needs mono/stereo audio, 2048 frames through 60 seconds, at 8–192 kHz')
    if not np.isfinite(audio).all() or np.max(np.abs(audio))>1:
        raise ValueError('Recording must contain finite PCM within [-1,1]; no normalization is applied')
    if np.mean(audio.astype(float)**2)<1e-20:raise ValueError('Recording energy is below the analysis floor')
    pcm=audio.T.astype('<f4').tobytes()
    destination.parent.mkdir(parents=True,exist_ok=True)
    with destination.open('wb') as bank:
        bank.write(b'TBBANK04'+struct.pack('<2I',12,0))
        for instrument in range(5):
            for art in range(4 if instrument==4 else 2):
                real=instrument==0 and art==1
                header=[instrument,art,key,1,rate,len(audio) if real else 8]+[0]*8+[audio.shape[1] if real else 1]
                bank.write(struct.pack('<15I',*header));bank.write(pcm if real else bytes(8*4))
    return audio,rate,hashlib.sha256(pcm).hexdigest()


def prepare(source,plugin,output,key,label):
    if output.exists():raise ValueError('Choose a new output folder to retain earlier auditions')
    if not plugin.is_dir():raise ValueError('Expected an existing TopBrass VST3 bundle')
    bank=output/'TopBrass.bank'
    audio,rate,pcm_sha=pack_recording(source,bank,key)
    original=output/('source-original'+source.suffix.lower());shutil.copy2(source,original)
    retained,retained_rate=sf.read(original,dtype='float32',always_2d=True)
    if retained_rate!=rate or hashlib.sha256(retained.T.astype('<f4').tobytes()).hexdigest()!=pcm_sha:
        raise ValueError('Recording changed during preparation; incomplete audition retained')
    sf.write(output/'original-native.wav',audio,rate,subtype='FLOAT')
    clone=output/'plugin/TopBrass.vst3'
    shutil.copytree(plugin,clone,symlinks=True,ignore=lambda path,names:['TopBrass.bank'] if Path(path).name=='Resources' else [])
    shutil.copy2(bank,clone/'Contents/Resources/TopBrass.bank')
    subprocess.run(['codesign','--force','--deep','--sign','-',str(clone)],check=True)
    subprocess.run(['codesign','--verify','--deep','--strict',str(clone)],check=True)
    info=plistlib.loads((clone/'Contents/Info.plist').read_bytes())
    capture=output/'capture';capture.mkdir()
    case=dict(id='recording',label=label or source.stem,instrument=0,part=0,articulation=1,
              root=key,velocity=127,rate=rate,frames=len(audio),post_release_frames=int(rate*.25),
              directory=str(capture),double_tongue=False,keys=0,airflow=0)
    (output/'config.json').write_text(json.dumps({'variants':['neutral'],'cases':[case]},indent=2)+'\n')
    manifest=dict(label=case['label'],source_path=str(source),original_file=str(original),
                  original_file_sha256=digest(original),decoded_pcm_sha256=pcm_sha,
                  original_native_sha256=digest(output/'original-native.wav'),config_sha256=digest(output/'config.json'),
                  frames=len(audio),rate=rate,channels=audio.shape[1],anchor_key=key,
                  anchor_is_playback_mapping_not_pitch_detection=True,
                  plugin_template=str(plugin),plugin_template_executable_sha256=digest(plugin/'Contents/MacOS/TopBrass'),
                  plugin_version=info['CFBundleShortVersionString'],clone_path=str(clone),
                  clone_executable_sha256=digest(clone/'Contents/MacOS/TopBrass'),
                  clone_bank_sha256=digest(bank),fixture_recordings=12,silent_coverage_recordings=11,
                  description='Single-recording diagnostic fixture. The installed VST and settings are not changed. Full decoded PCM, native rate/pitch, no loop, crop, normalization or sample gain table. Eleven silent records satisfy the production loader coverage contract; only the real sax-slot recording is enabled. This is not a complete instrument or an SFZ-player recreation.')
    (output/'input.json').write_text(json.dumps(manifest,indent=2)+'\n')


def capture(output):
    manifest=json.loads((output/'input.json').read_text());clone=Path(manifest['clone_path'])
    if digest(Path(manifest['original_file']))!=manifest['original_file_sha256']:
        raise ValueError('Retained original recording changed')
    if digest(output/'original-native.wav')!=manifest['original_native_sha256'] or digest(output/'config.json')!=manifest['config_sha256']:
        raise ValueError('Decoded reference or capture configuration changed')
    if digest(clone/'Contents/MacOS/TopBrass')!=manifest['clone_executable_sha256'] or digest(clone/'Contents/Resources/TopBrass.bank')!=manifest['clone_bank_sha256']:
        raise ValueError('Audition plugin or bank changed')
    host=ROOT/'build/TopBrassRecordingAudit_artefacts/Release/TopBrass Recording Audit.app/Contents/MacOS/TopBrass Recording Audit'
    subprocess.run([str(host),str(clone),str(output/'config.json')],check=True)
    manifest['capture_host_executable_sha256']=digest(host)
    manifest['raw_capture_sha256']=digest(output/'capture/neutral-plugin-raw.wav')
    (output/'input.json').write_text(json.dumps(manifest,indent=2)+'\n')


def report(output):
    manifest=json.loads((output/'input.json').read_text())
    original=output/'original-native.wav';played=output/'capture/neutral-plugin-raw.wav'
    if digest(original)!=manifest['original_native_sha256'] or digest(played)!=manifest['raw_capture_sha256']:
        raise ValueError('Reference or plugin capture changed since capture')
    analysis=output/'analysis'
    prepare_pair(original,played,analysis)
    config=json.loads((analysis/'config.json').read_text());config['cases'][0]['label']=manifest['label'];config['cases'][0]['root']=manifest['anchor_key']
    (analysis/'config.json').write_text(json.dumps(config,indent=2)+'\n')
    result=analyze(analysis)[0]
    raw,rate=sf.read(played,dtype='float32',always_2d=True)
    compensation=10**(24/20)*(np.sqrt(2) if manifest['channels']==1 else 1)
    sf.write(output/'source-level-plugin.wav',raw*compensation,rate,subtype='FLOAT')
    result.update(input=manifest,raw_plugin_sha256=digest(played),source_level_listening_gain=float(compensation),
                  source_level_listening_gain_description='One fixed24dB compensation for solo/output gains, plus3.0103dB for the mono center pan law. No per-recording RMS normalization. Original audio remains at its recorded level.')
    (output/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    passed=result['metrics']['transparency_pass']
    (output/'index.html').write_text(f'''<!doctype html><meta charset="utf-8"><title>TopBrass raw recording audition</title>
<style>body{{font:17px/1.6 system-ui;background:#151719;color:#eee;max-width:1000px;margin:40px auto;padding:20px}}audio,img{{width:100%}}a{{color:#dfbb75}}pre{{white-space:pre-wrap}}</style>
<h1>{html.escape(manifest['label'])}</h1>
<p>Original recording, then actual TopBrass {html.escape(manifest['plugin_version'])} VST3 playback at native rate and pitch. This isolated one-recording fixture changes neither the installed plugin nor your patch. It does not reproduce a supplier's instrument mapping or effects.</p>
<p>Both listening files retain recorded dynamics. The plugin file has one fixed gain to compensate its neutral solo/output levels and mono pan law. No EQ, compression, denoising, tuning, time alignment, crop, loop or time stretching was added. The complete source duration is included in the comparison.</p>
<h2>Original, decoded without processing</h2><audio controls preload="none" src="original-native.wav"></audio>
<h2>Actual VST3, fixed source-level gain</h2><audio controls preload="none" src="source-level-plugin.wav"></audio>
<p>Technical transparency check: <strong>{'pass' if passed else 'FAIL'}</strong>. This is a playback-fidelity check, not a realism score.</p>
<img src="analysis/pair/neutral-analysis.png" alt="Original versus VST3 spectrograms, frequency bands, envelopes and waveform residual">
<p><a href="{html.escape(Path(manifest['original_file']).name)}">Exact original file</a> · <a href="capture/neutral-plugin-raw.wav">Raw plugin capture</a> · <a href="results.json">Metrics and provenance</a></p>''')
    print(json.dumps({'output':str(output),'transparency_pass':bool(passed),'residual_relative_db':result['metrics']['residual_relative_db']}))
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('recording',type=Path,nargs='?')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--plugin',type=Path,default=Path.home()/'Library/Audio/Plug-Ins/VST3/TopBrass.vst3')
    parser.add_argument('--key',type=int,default=60,help='MIDI anchor that plays the original pitch; not automatic pitch detection')
    parser.add_argument('--label')
    parser.add_argument('--stage',choices=('prepare','capture','analyze','all'),default='all')
    args=parser.parse_args();output=args.output.resolve()
    if args.stage in('prepare','all'):
        if args.recording is None:parser.error('Supply the recording to prepare')
        prepare(args.recording.resolve(),args.plugin.resolve(),output,args.key,args.label)
    if args.stage in('capture','all'):capture(output)
    if args.stage in('analyze','all'):report(output)
