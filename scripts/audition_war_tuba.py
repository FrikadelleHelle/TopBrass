"""Audition complete original War Tuba condenser recordings through private VST copies."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess

import numpy as np
import soundfile as sf

from fetch_war_tuba import CACHE,COMMIT,REPO
from war_tuba_source import checked,mapping
from build_bank import sustain_release
from audition_weresax import clone
from recording_audit import compare_arrays,plot_pair,stereo

ROOT=Path(__file__).resolve().parents[1]
HOST=ROOT/'build/TopBrassRecordingAudit_artefacts/Release/TopBrass Recording Audit.app/Contents/MacOS/TopBrass Recording Audit'


def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def pack(rows,folder):
    details=[];bank=folder/'TopBrass.bank'
    with bank.open('wb') as output:
        output.write(b'TBBANK04'+struct.pack('<2I',len(rows)+10,len(rows)))
        for row in rows:
            audio,rate=sf.read(checked(row['path']),dtype='float32',always_2d=True)
            assert rate==44100 and audio.shape[1]==1 and np.isfinite(audio).all() and 0<np.max(np.abs(audio))<=1
            art=0 if row['kind']=='sustain' else 1
            release=sustain_release(audio,rate,0) if art==0 else None
            pcm=audio.T.astype('<f4').tobytes()
            output.write(struct.pack('<15I',3,art,row['root'],1,rate,len(audio),0,0,0,
                                     release['start'] if release else 0,release['crossfade'] if release else 0,0,0,0,1))
            output.write(pcm)
            rms=float(np.sqrt(np.mean(audio.astype(float)**2)));tail=float(np.sqrt(np.mean(audio[-882:].astype(float)**2)))
            details.append(row|dict(index=len(details),articulation=art,frames=len(audio),rate=rate,channels=1,
                                    decoded_pcm_sha256=hashlib.sha256(pcm).hexdigest(),source_sha256=sha(CACHE/row['path']),
                                    release_selection=release,last_20ms_to_whole_rms_db=float(20*np.log10(max(tail,1e-20)/rms))))
        for instrument in [0,1,2,4]:
            for art in range(4 if instrument==4 else 2):
                output.write(struct.pack('<15I',instrument,art,60,1,44100,8,*([0]*8),1));output.write(bytes(32))
        for row in details:
            output.write(struct.pack('<10I2f',row['index'],3,row['articulation'],row['root'],row['low_key'],row['high_key'],
                                     row['low_velocity'],row['high_velocity'],0,row['take'],0,1))
    with bank.open('rb') as stream:
        assert stream.read(16)==b'TBBANK04'+struct.pack('<2I',len(rows)+10,len(rows))
        for row in details:
            header=struct.unpack('<15I',stream.read(60));assert header[:6]==(3,row['articulation'],row['root'],1,44100,row['frames'])
            assert header[6:9]==(0,0,0) and header[11:]==(0,0,0,1)
            pcm=stream.read(row['frames']*4);original,_=sf.read(CACHE/row['path'],dtype='float32')
            assert pcm==original.astype('<f4').tobytes()
        for _ in range(10):
            header=struct.unpack('<15I',stream.read(60));assert header[5]==8 and stream.read(32)==bytes(32)
        for row in details:
            zone=struct.unpack('<10I2f',stream.read(48))
            assert zone==(row['index'],3,row['articulation'],row['root'],row['low_key'],row['high_key'],row['low_velocity'],row['high_velocity'],0,row['take'],0,1)
        assert not stream.read()
    (folder/'sources.json').write_text(json.dumps(details,indent=2)+'\n')
    return details


def phrase():
    events=[[0,176,11,127],[0,176,1,0]]
    for bar,key in enumerate([60,63,65,67]):
        start=bar*88200;events.extend([[start,144,key,112],[start+85000,128,key,0]])
    return events


def render(details,plugin,folder,kind):
    cases=[];velocities=[32,33,64,65,96,97,127] if kind=='staccato' else [42,43,85,86,127]
    for key in range(29,56):
        for velocity in velocities:
            identifier=f'{key}-{velocity}';directory=folder/'captures'/identifier;directory.mkdir(parents=True)
            selected=[row for row in details if row['articulation']==1 and row['root']==key and row['low_velocity']<=velocity<=row['high_velocity']]
            assert len(selected)==(3 if kind=='staccato' else 4)
            events=[]
            for take in range(len(selected)):events.extend([[take*8192,144,key,velocity],[take*8192+4096,176,120,0]])
            cases.append(dict(id=identifier,instrument=0,part=3,articulation=1,root=key,velocity=velocity,rate=44100,
                              frames=len(selected)*8192,post_release_frames=0,directory=str(directory),midi_events=events))
    full=next(row for row in details if row['articulation']==1 and row['root']==36 and row['high_velocity']==127 and row['take']==0)
    for label,row in [('full-short',full),('full-sustain',next(row for row in details if row['articulation']==0 and row['root']==36))]:
        directory=folder/'captures'/label;directory.mkdir(parents=True)
        cases.append(dict(id=label,instrument=0,part=3,articulation=row['articulation'],root=36,velocity=127,rate=44100,
                          frames=row['frames'],post_release_frames=22050,directory=str(directory)))
    for art,label in [(0,'phrase-sustain'),(1,'phrase-short')]:
        directory=folder/'captures'/label;directory.mkdir(parents=True)
        cases.append(dict(id=label,instrument=0,part=3,articulation=art,ensemble=True,isolate_player=3,horn_feel=.4,
                          root=60,velocity=112,rate=44100,frames=352800,post_release_frames=88200,directory=str(directory),midi_events=phrase()))
    config=folder/'config.json';config.write_text(json.dumps(dict(cases=cases),indent=2)+'\n')
    subprocess.run([str(HOST),str(plugin),str(config)],check=True)
    return cases


def verify(details,cases,folder):
    checks=[]
    for case in cases:
        if not case['id'][0].isdigit():continue
        audio,rate=sf.read(Path(case['directory'])/'neutral-plugin-raw.wav',always_2d=True);assert rate==44100
        expected=sorted([row for row in details if row['articulation']==1 and row['root']==case['root'] and row['low_velocity']<=case['velocity']<=row['high_velocity']],key=lambda row:row['take'])
        for take,row in enumerate(expected):
            original,_=sf.read(CACHE/row['path'],dtype='float32');window=original[:4096].astype(float)
            gain=10**(-24/20)*2**(-.5)*(case['velocity']/127)**1.3
            played=audio[take*8192:take*8192+4096];error=played-window[:,None]*gain
            residual=float(10*np.log10(max(np.mean(error**2)/np.mean((window*gain)**2),1e-30)))
            assert np.max(np.abs(error))<1e-7 and residual<-100,(case['id'],take,residual)
            checks.append(dict(key=row['root'],velocity=case['velocity'],take=take,source=row['path'],residual_relative_db=residual))
    for label in ['full-short','full-sustain']:
        case=next(case for case in cases if case['id']==label)
        row=next(row for row in details if row['articulation']==case['articulation'] and row['root']==36 and row['high_velocity']==127 and row['take']==0)
        original,rate=sf.read(CACHE/row['path'],dtype='float32',always_2d=True)
        played,sr=sf.read(Path(case['directory'])/'neutral-plugin-raw.wav',always_2d=True);assert sr==rate
        reference=stereo(original);matched=played[:len(original)]*10**(24/20)*np.sqrt(2)
        metrics=compare_arrays(reference,matched,rate);assert metrics['transparency_pass'] and metrics['residual_relative_db']< -100 and metrics['lag_samples']==0
        sf.write(folder/(label+'-source.wav'),original,rate,subtype='FLOAT');sf.write(folder/(label+'-vst.wav'),matched,rate,subtype='FLOAT')
        plot_pair(reference,matched,rate,metrics,0,len(original),folder/(label+'-analysis.png'),label+': original recording vs actual neutral VST, fixed playback gain removed')
        (folder/(label+'-metrics.json')).write_text(json.dumps(metrics,indent=2)+'\n')
    (folder/'selection-checks.json').write_text(json.dumps(checks,indent=2)+'\n')
    return checks


def finish(output,plugin,summary,before,bank_before):
    links=[('current/phrase-sustain','Current VSCO tuba, Sustain'),('staccato/captures/phrase-sustain','War Tuba, finite Sustain'),
           ('current/phrase-short','Current VSCO tuba, Short'),('staccato/captures/phrase-short','War Tuba, staccato'),('staccatissimo/captures/phrase-short','War Tuba, staccatissimo')]
    page='''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Tuba source comparison</title><style>body{font:18px/1.6 system-ui;background:#151719;color:#eee;max-width:1000px;margin:40px auto;padding:20px}audio,img{width:100%}a{color:#dfbb75}section{border-top:1px solid #555;padding-top:16px;margin-top:28px}</style><h1>Tuba source comparison</h1><p>Private tuba audition; the installed tuba recordings are unchanged. Same four-bar input through the actual VST, with only the tuba heard. Current VSCO recordings versus Karoryfer War Tuba condenser recordings. Horn feel is 0.4. The main phrases match mean level using one constant gain per whole clip. Our playback adds no EQ or compression. Original-level exports are available below.</p>'''
    gains=[];original_level_players=[]
    for path,title in links:
        audio,rate=sf.read(output/path/'neutral-plugin-raw.wav',always_2d=True);mono=audio[:,0]
        rms=float(np.sqrt(np.mean(mono**2)));gain=.05/rms
        matched=mono*gain;assert np.max(np.abs(matched))<1
        stem=title.lower().replace(' ','-').replace(',','')
        target=output/(stem+'-level-matched.wav');sf.write(target,np.column_stack([matched,matched]),rate,subtype='FLOAT')
        original=mono*10**(18/20);assert np.max(np.abs(original))<1
        raw=output/(stem+'.wav');sf.write(raw,np.column_stack([original,original]),rate,subtype='FLOAT')
        page+=f'<section><h2>{html.escape(title)}</h2><audio controls preload="metadata" src="{target.name}"></audio></section>'
        original_level_players.append(f'<h3>{html.escape(title)}</h3><audio controls preload="metadata" src="{raw.name}"></audio>')
        gains.append(dict(label=title,source_capture=str(output/path/'neutral-plugin-raw.wav'),raw_rms=rms,
                          level_matched_gain=gain,level_matched_gain_db=float(20*np.log10(gain)),target_rms=.05,
                          level_matched_peak=float(np.max(np.abs(matched))),original_level_listening_gain_db=18))
    (output/'comparison-gains.json').write_text(json.dumps(dict(method='One constant gain per complete phrase; no per-note adjustment, EQ, compression or time shifts.',phrases=gains),indent=2)+'\n')
    page+='<details><summary>Compare original recorded levels</summary><p>These exports all use the same fixed +18 dB gain. Relative source levels and dynamics remain unchanged.</p>'+''.join(original_level_players)+'</details>'
    page+='''<section><h2>Original recording versus actual VST</h2><p>Complete recordings at MIDI key 36. The VST export removes only the known fixed output/player/pan gain. No gain fitting, EQ or time shift is applied to these listening pairs. The held note is allowed to finish with the original file.</p>'''
    for kind,label in [('staccato','Staccato'),('staccatissimo','Staccatissimo')]:
        for name in ['full-short','full-sustain'] if kind=='staccato' else ['full-short']:
            page+=f'<h3>{label if name=="full-short" else "Sustain"}</h3><p>Original</p><audio controls preload="metadata" src="{kind}/{name}-source.wav"></audio><p>Actual VST</p><audio controls preload="metadata" src="{kind}/{name}-vst.wav"></audio><img src="{kind}/{name}-analysis.png" alt="Original and VST spectrograms, spectrum and residual">'
    page+='''</section><section><h2>Original breath and valve examples</h2><p>Source-labelled noises at their recorded levels. These examples are not automatically inserted into the phrases or installed instrument.</p>'''
    for name,label in [('breath_rr1_cnd.wav','Breath'),('valve_close_rr1_cnd.wav','Valve close'),('valve_open_rr1_cnd.wav','Valve open')]:
        source=checked('Samples/'+name);shutil.copy2(source,output/name)
        page+=f'<h3>{label}</h3><audio controls preload="metadata" src="{name}"></audio>'
    page+='''</section><p>The private copies contain the tuba and silent fixtures for all other instruments. Their GUI keeps the original labels. These are audition copies, not a new installed instrument. Sustains are complete finite recordings with one recorded force, not multiple dynamics. Source note-off crossfades use detected file decays. Source SFZ EQ, +9 dB sustain gain, mod-wheel dynamics, velocity curves, randomized selection, simulated ensembles and legato offsets are not recreated. Short has four recorded forces and three takes; Staccatissimo has three forces and four takes. Our deterministic take cycle and velocity response are disclosed differences. The publisher’s processing before publication is not established by our PCM checks. Musical realism remains unaccepted.</p><p><a href="provenance.json">Source and playback provenance</a>. <a href="https://shop.karoryfer.com/pages/free-war-tuba">Publisher</a>. <a href="../">Listening bench</a>.</p><script>document.addEventListener('play',e=>{if(e.target.tagName==='AUDIO')for(const a of document.querySelectorAll('audio'))if(a!==e.target)a.pause()},true)</script></html>'''
    (output/'index.html').write_text(page)
    assert sha(plugin/'Contents/MacOS/TopBrass')==before and sha(plugin/'Contents/Resources/TopBrass.bank')==bank_before
    report=dict(repository=REPO,commit=COMMIT,license='CC0 1.0 Universal; original LICENSE retained',template_vst_executable_sha256=before,
                template_bank_sha256=bank_before,host_executable_sha256=sha(HOST),auditions=summary,
                installed_instrument_changed=False,source_quality_accepted=False,full_sfz_recreation=False,
                scope='Complete condenser recordings and original key/velocity mappings; deterministic cycles replace source random choice. Other instruments are silent fixtures.')
    (output/'provenance.json').write_text(json.dumps(report,indent=2)+'\n')
    shutil.copy2(CACHE/'LICENSE',output/'LICENSE');shutil.copy2(CACHE/'readme.txt',output/'source-readme.txt')
    print('PASS:complete raw tuba auditions,',sum(row['native_selection_checks'] for row in summary),'actual VST source selections; installed instrument unchanged')


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--plugin',type=Path,default=Path.home()/'Library/Audio/Plug-Ins/VST3/TopBrass.vst3');parser.add_argument('--finish-only',action='store_true');args=parser.parse_args()
    output=args.output.resolve();plugin=args.plugin.resolve()
    if args.finish_only:
        report=json.loads((output/'provenance.json').read_text())
        assert sha(HOST)==report['host_executable_sha256']
        finish(output,plugin,report['auditions'],report['template_vst_executable_sha256'],report['template_bank_sha256']);return
    if output.exists():raise ValueError('Choose a fresh audition folder')
    output.mkdir(parents=True);before=sha(plugin/'Contents/MacOS/TopBrass');bank_before=sha(plugin/'Contents/Resources/TopBrass.bank')
    sustain=mapping('sustain');summary=[]
    for kind in ['staccato','staccatissimo']:
        folder=output/kind;folder.mkdir();details=pack(sustain+mapping(kind),folder);candidate=clone(plugin,folder)
        cases=render(details,candidate,folder,kind);checks=verify(details,cases,folder)
        summary.append(dict(kind=kind,recordings=len(details),source_bank_sha256=sha(folder/'TopBrass.bank'),native_selection_checks=len(checks),
                            worst_residual_relative_db=max(row['residual_relative_db'] for row in checks),
                            source_pcm_unchanged=True,sustains_with_detected_file_decay=sum(row['release_selection'] is not None for row in details)))
    current=output/'current';current.mkdir();cases=[]
    for art,label in [(0,'phrase-sustain'),(1,'phrase-short')]:
        directory=current/label;directory.mkdir()
        cases.append(dict(id=label,instrument=0,part=3,articulation=art,ensemble=True,isolate_player=3,horn_feel=.4,root=60,velocity=112,
                          rate=44100,frames=352800,post_release_frames=88200,directory=str(directory),midi_events=phrase()))
    config=current/'config.json';config.write_text(json.dumps(dict(cases=cases),indent=2)+'\n');subprocess.run([str(HOST),str(plugin),str(config)],check=True)
    finish(output,plugin,summary,before,bank_before)



if __name__=='__main__':main()
