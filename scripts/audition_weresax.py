"""Build an isolated dry Weresax alto audition from pinned condenser recordings."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import html
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import urllib.parse
import urllib.request

import numpy as np
import soundfile as sf

ROOT=Path(__file__).resolve().parents[1]
CACHE=ROOT/'assets/cache/karoryfer.weresax'
COMMIT='a4d756b21d2a573aca0d840cce7e71ba5effd4c6'
REPO='sfzinstruments/karoryfer.weresax'


def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def git_blob(data):return hashlib.sha1(f'blob {len(data)}\0'.encode()+data).hexdigest()


def mappings():
    result=[]
    tree=json.loads((CACHE/'source-tree.json').read_text())
    assert tree['repo']==REPO and tree['commit']==COMMIT
    entries={entry['path']:entry for entry in tree['tree']}
    for force in ('piano','forte'):
        mapping=CACHE/f'Programs/alto_map_{force}_condenser.sfz'
        assert git_blob(mapping.read_bytes())==entries[mapping.relative_to(CACHE).as_posix()]['sha']
        text=mapping.read_text()
        group={}
        for kind,body in re.findall(r'<(group|region)>([^<]*)',text):
            attrs=dict(re.findall(r'(\w+)=([^\s]+)',re.sub(r'//[^\n]*','',body)))
            if kind=='group':group=attrs;continue
            attrs=group|attrs
            key=int(attrs['lokey']);root=int(attrs.get('pitch_keycenter',60))
            assert int(attrs['hikey'])==key==root and int(attrs['seq_length'])==2
            path=Path(attrs['sample'].replace('\\','/')).as_posix().removeprefix('../')
            result.append(dict(path=path,key=key,root=root,low_velocity=int(attrs.get('lovel',0)),high_velocity=int(attrs.get('hivel',127)),take=int(attrs.get('seq_position',1)),force=force,amp_veltrack=int(attrs['amp_veltrack'])))
    assert len(result)==128 and len({r['path'] for r in result})==128
    assert {(r['key'],r['force'],r['take']) for r in result}=={(key,force,take) for key in range(49,81) for force in ('piano','forte') for take in (1,2)}
    assert all((r['low_velocity'],r['high_velocity'])==((0,63) if r['force']=='piano' else (64,127)) for r in result)
    return result


def fetch(rows):
    tree=json.loads((CACHE/'source-tree.json').read_text());assert tree['repo']==REPO and tree['commit']==COMMIT
    entries={e['path']:e for e in tree['tree']}
    def one(row):
        entry=entries[row['path']];path=CACHE/row['path']
        if path.exists():data=path.read_bytes()
        else:
            url=f'https://raw.githubusercontent.com/{REPO}/{COMMIT}/'+urllib.parse.quote(row['path'])
            with urllib.request.urlopen(url,timeout=45) as response:data=response.read(entry['size']+1)
        if len(data)!=entry['size'] or git_blob(data)!=entry['sha']:raise ValueError('Pinned Git blob mismatch: '+row['path'])
        if not path.exists():path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
        row['git_blob_sha1']=entry['sha'];row['file_sha256']=hashlib.sha256(data).hexdigest()
    with ThreadPoolExecutor(max_workers=6) as pool:list(pool.map(one,rows))


def pack(rows,output):
    destination=output/'TopBrass.bank';details=[]
    with destination.open('wb') as bank:
        bank.write(b'TBBANK04'+struct.pack('<2I',139,128))
        for row in rows:
            path=CACHE/row['path'];audio,rate=sf.read(path,dtype='float32',always_2d=True)
            assert audio.shape[1]==1 and rate==44100 and np.isfinite(audio).all() and 2048<len(audio)<rate*60
            assert np.max(np.abs(audio))<=1 and np.mean(audio.astype(float)**2)>1e-20
            pcm=audio.T.astype('<f4').tobytes()
            header=[0,0,row['root'],1,rate,len(audio)]+[0]*8+[1]
            bank.write(struct.pack('<15I',*header));bank.write(pcm)
            energy=lambda x:float(np.sqrt(np.mean(x.astype(float)**2)))
            tail=energy(audio[-int(rate*.02):]);body=energy(audio)
            details.append(row|dict(frames=len(audio),rate=rate,channels=1,decoded_pcm_sha256=hashlib.sha256(pcm).hexdigest(),peak=float(np.max(np.abs(audio))),rms=body,last_20ms_rms=tail,last_to_whole_rms_db=float(20*np.log10(max(tail,1e-20)/body)),last_sample=float(audio[-1,0])))
        for instrument in range(5):
            for art in range(4 if instrument==4 else 2):
                if instrument==0 and art==0:continue
                bank.write(struct.pack('<15I',instrument,art,60,1,44100,8,*([0]*8),1));bank.write(bytes(32))
        for index,row in enumerate(rows):
            bank.write(struct.pack('<10I2f',index,0,0,row['root'],row['key'],row['key'],row['low_velocity'],row['high_velocity'],0,row['take']-1,0,1))
    # Independently decode the completed bank, checking every PCM and zone against the source.
    with destination.open('rb') as stream:
        assert stream.read(8)==b'TBBANK04' and struct.unpack('<2I',stream.read(8))==(139,128)
        for index in range(139):
            header=struct.unpack('<15I',stream.read(60));pcm=stream.read(header[5]*header[14]*4)
            if index<128:
                row=details[index];assert hashlib.sha256(pcm).hexdigest()==row['decoded_pcm_sha256']
                assert header[:6]==(0,0,row['root'],1,44100,row['frames']) and header[6:14]==(0,)*8
            else:assert pcm==bytes(32)
        for index,row in enumerate(rows):
            zone=struct.unpack('<10I2f',stream.read(48))
            assert zone==(index,0,0,row['root'],row['key'],row['key'],row['low_velocity'],row['high_velocity'],0,row['take']-1,0,1)
        assert stream.read()==b''
    return details


def clone(plugin,output):
    target=output/'plugin/TopBrass.vst3'
    shutil.copytree(plugin,target,symlinks=True,ignore=lambda path,names:['TopBrass.bank'] if Path(path).name=='Resources' else [])
    shutil.copy2(output/'TopBrass.bank',target/'Contents/Resources/TopBrass.bank')
    subprocess.run(['codesign','--force','--deep','--sign','-',str(target)],check=True)
    subprocess.run(['codesign','--verify','--deep','--strict',str(target)],check=True)
    return target


def render(plugin,output):
    cases=[];rate=44100
    for key in range(49,81):
        for velocity in (63,64,127):
            identifier=f'key-{key}-velocity-{velocity}';directory=output/'captures'/identifier;directory.mkdir(parents=True)
            events=[[0,176,11,127],[0,176,1,0],[0,144,key,velocity],[8820,128,key,0],[22050,144,key,velocity],[30870,128,key,0],[39690,176,120,0]]
            cases.append(dict(id=identifier,instrument=0,part=0,articulation=0,root=key,velocity=velocity,rate=rate,frames=44100,post_release_frames=0,directory=str(directory),midi_events=events))
    notes=[(60,90,.28),(63,67,.14),(65,115,.25),(67,102,.18),(65,58,.12),(63,80,.45),(60,55,.65),(58,100,.16),(60,120,.45)]
    events=[[0,176,11,127],[0,176,1,0]];start=0
    for key,velocity,length in notes:
        end=start+int(length*rate);events += [[start,144,key,velocity],[end,128,key,0]];start=end+int(rate*.10)
    events += [[start+int(rate*.5),176,120,0]]
    directory=output/'captures/phrase';directory.mkdir(parents=True)
    cases.append(dict(id='phrase',instrument=0,part=0,articulation=0,root=60,velocity=100,rate=rate,frames=start+rate,post_release_frames=0,directory=str(directory),midi_events=events))
    config=output/'config.json';config.write_text(json.dumps(dict(cases=cases),indent=2)+'\n')
    host=ROOT/'build/TopBrassRecordingAudit_artefacts/Release/TopBrass Recording Audit.app/Contents/MacOS/TopBrass Recording Audit'
    subprocess.run([str(host),str(plugin),str(config)],check=True)
    return cases


def verify(details,cases,output):
    rows=[]
    for case in cases:
        if case['id']=='phrase':continue
        audio,rate=sf.read(Path(case['directory'])/'neutral-plugin-raw.wav',always_2d=True)
        for take,start in [(1,0),(2,22050)]:
            force='piano' if case['velocity']<64 else 'forte'
            row=next(r for r in details if (r['key'],r['force'],r['take'])==(case['root'],force,take))
            source,sr=sf.read(CACHE/row['path'],dtype='float32');assert sr==rate
            original=np.repeat(source[:8820,None],2,axis=1).astype(float);played=audio[start:start+8820]
            gain=float(np.sum(original*played)/np.sum(original*original))
            residual=float(10*np.log10(max(float(np.mean((played-gain*original)**2))/float(np.mean(played**2)),1e-30)))
            assert gain>0 and residual<-100,(case['id'],take,residual)
            rows.append(dict(key=case['root'],velocity=case['velocity'],take=take,source=row['path'],gain=gain,residual_relative_db=residual))
        assert np.max(np.abs(audio[39690:]))==0,'Panic left audio'
    assert len(rows)==192
    (output/'selection-checks.json').write_text(json.dumps(rows,indent=2)+'\n')
    return rows



def finish(output,plugin):
    provenance=json.loads((output/'provenance.json').read_text())
    assert sha(plugin/'Contents/MacOS/TopBrass')==provenance['template_executable_sha256']
    assert sha(output/'TopBrass.bank')==provenance['source_bank_sha256']
    details=json.loads((output/'sources.json').read_text())
    config=json.loads((output/'config.json').read_text())
    phrase=next(case for case in config['cases'] if case['id']=='phrase')
    current=output/'current-tenor';current.mkdir(exist_ok=True)
    comparison=phrase|{'directory':str(current)}
    path=current/'config.json';path.write_text(json.dumps({'cases':[comparison]},indent=2)+'\n')
    host=ROOT/'build/TopBrassRecordingAudit_artefacts/Release/TopBrass Recording Audit.app/Contents/MacOS/TopBrass Recording Audit'
    subprocess.run([str(host),str(plugin),str(path)],check=True)
    audio,rate=sf.read(current/'neutral-plugin-raw.wav',always_2d=True)
    gain=10**(24/20)*np.sqrt(2)
    sf.write(output/'current-tenor-fixed-gain.wav',audio*gain,rate,subtype='FLOAT')
    tree=json.loads((CACHE/'source-tree.json').read_text());entries={entry['path']:entry for entry in tree['tree']}
    refs=output/'source-mapping';refs.mkdir(exist_ok=True)
    for name in ['LICENSE','readme.txt','Programs/alto_map_piano_condenser.sfz','Programs/alto_map_forte_condenser.sfz']:
        source=CACHE/name;assert git_blob(source.read_bytes())==entries[name]['sha']
        shutil.copy2(source,refs/source.name)
    clips=[]
    for force,take in [('piano',1),('piano',2),('forte',1),('forte',2)]:
        row=next(r for r in details if (r['key'],r['force'],r['take'])==(60,force,take))
        source,sr=sf.read(CACHE/row['path'],dtype='float32')
        assert sha(CACHE/row['path'])==row['file_sha256']
        file=f'original-key60-{force}-take{take}.wav';sf.write(output/file,source,sr,subtype='FLOAT')
        clips.append((file,f'{force}, take {take}'))
    report=dict(recordings=len(details),source_ending_measurement='Last 20ms RMS relative to complete-recording RMS; this is not a perceptual release detector.',median_last_to_whole_rms_db=float(np.median([r['last_to_whole_rms_db'] for r in details])),endings_within_6db_of_whole_rms=sum(r['last_to_whole_rms_db']>-6 for r in details),current_tenor_capture_sha256=sha(current/'neutral-plugin-raw.wav'),candidate_phrase_capture_sha256=sha(output/'captures/phrase/neutral-plugin-raw.wav'),capture_host_sha256=sha(host),capture_config_sha256=sha(output/'config.json'),playback_gain=gain,source_pcm_unchanged=True,installed_resources_changed=False,source_quality_accepted=False)
    (output/'comparison.json').write_text(json.dumps(report,indent=2)+'\n')
    content='''<!doctype html><meta charset="utf-8"><title>Complete dry alto source audition</title><style>body{font:18px/1.6 system-ui;background:#151719;color:#eee;max-width:900px;margin:40px auto;padding:20px}audio{width:100%}a{color:#dfbb75}section{border-top:1px solid #555;padding-top:15px;margin-top:30px}</style><h1>Dry alto source audition</h1><p>First hear the raw recordings, then the same MIDI phrase through the current tenor and candidate alto. The candidate contains 128 clean-condenser recordings, every MIDI key 49–80, soft and strong playing, with two takes per force. This is a source candidate; your installed instrument and patch are unchanged.</p><section><h2>Raw original recordings at MIDI key 60</h2><p>Complete decoded PCM at the recorded level. No normalization or effects. The originals end abruptly; no recorded release is available in these maps.</p>'''
    for file,label in clips:content+=f'<p>{html.escape(label)}</p><audio controls preload="none" src="{file}"></audio>'
    content+='''<section><h2>Same MIDI phrase, current tenor</h2><audio controls preload="none" src="current-tenor-fixed-gain.wav"></audio><h2>Same MIDI phrase, candidate alto</h2><audio controls preload="none" src="phrase-fixed-gain.wav"></audio><p>Actual TopBrass VST3, identical MIDI and neutral settings. The same fixed output/pan gain compensation applies to both. Recorded level differences remain. Different instruments and recordings are being compared; this is not a frequency-response equivalence test.</p></section><section><h2>What this source supplies</h2><p>Original attacks, held tone, two recorded force levels and repeated takes. All 192 native VST key/velocity/take checks match the expected source waveforms. No EQ, dynamic microphone blend, added vibrato or synthesized noise is used.</p><h2>What remains unresolved</h2><p>All 128 source endings have last-20ms RMS within 6 dB of the complete-recording RMS. These are held-tone recordings ending at appreciable level, not natural recorded releases. There are no dedicated short notes, legato transitions or recorded release samples in these maps. VST note-offs use the engine envelope, and its velocity curve and note overlap behavior differ from the supplier SFZ. This audition cannot establish full pop-section realism.</p><p>The private VST is sustain-only; other instruments/articulations are silent coverage fixtures, and its GUI retains the Tenor label. It has not replaced your installed bank.</p><p><a href="provenance.json">Source and playback provenance</a> · <a href="comparison.json">Comparison measurements</a> · <a href="sources.json">All recording measurements</a> · <a href="selection-checks.json">192 native VST selection checks</a> · <a href="source-mapping/readme.txt">Pinned author readme</a> · <a href="source-mapping/LICENSE">Source license</a> · <a href="../raw-source-candidates-v0.13.0/">Original versus VST spectrograms</a></p></section>'''
    (output/'index.html').write_text(content)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--stage',choices=('all','finish'),default='all')
    parser.add_argument('--plugin',type=Path,default=Path.home()/'Library/Audio/Plug-Ins/VST3/TopBrass.vst3')
    args=parser.parse_args();output=args.output.resolve();plugin=args.plugin.resolve()
    if args.stage=='finish':finish(output,plugin);return
    if output.exists():raise ValueError('Choose a new output directory to retain earlier auditions')
    rows=mappings();fetch(rows);output.mkdir(parents=True)
    details=pack(rows,output);candidate=clone(plugin,output)
    (output/'sources.json').write_text(json.dumps(details,indent=2)+'\n')
    cases=render(candidate,output);checks=verify(details,cases,output)
    capture=output/'captures/phrase/neutral-plugin-raw.wav';audio,rate=sf.read(capture,always_2d=True)
    sf.write(output/'phrase-fixed-gain.wav',audio*10**(24/20)*np.sqrt(2),rate,subtype='FLOAT')
    provenance=dict(repository=REPO,commit=COMMIT,recordings=128,keys=[49,80],takes_per_force=2,force_velocity_windows=[[0,63],[64,127]],source_pcm_unchanged=True,source_bank_sha256=sha(output/'TopBrass.bank'),template_plugin=str(plugin),template_executable_sha256=sha(plugin/'Contents/MacOS/TopBrass'),clone_executable_sha256=sha(candidate/'Contents/MacOS/TopBrass'),native_selection_checks=len(checks),worst_residual_relative_db=max(r['residual_relative_db'] for r in checks),installed_bank_changed=False,complete_sfz_recreation=False,limits=['Sustain only; other instruments/articulations are silent coverage fixtures.','No source loops or recorded releases are supplied by these condenser maps. Note-off uses the engine envelope.','Supplier group choke, velocity response, mic blending, EQ, LFOs and synthetic noise are not recreated.','GUI retains the original Tenor label for this isolated diagnostic bundle.','No source quality or realism acceptance follows from PCM/selection checks.'])
    (output/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')

    finish(output,plugin)
    print(json.dumps(provenance,indent=2))


if __name__=='__main__':main()
