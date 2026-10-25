"""Verify the integrated War Tuba selectors, native loops and file decays in a real plug-in."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np
import soundfile as sf

from recording_audit import compare_arrays,plot_pair

ROOT=Path(__file__).resolve().parents[1]
CACHE=ROOT/'assets/cache/karoryfer.war-tuba'


def loop_body(source,frames,start,end,fade):
    positions=np.arange(frames);positions=np.where(positions<end,positions,start+(positions-end)%(end-start))
    result=source[positions].astype(float)
    blend=np.clip((positions-(end-fade))/fade,0,1)
    mask=positions>=end-fade
    incoming=start-fade+(positions[mask]-(end-fade))
    result[mask]=result[mask]*(1-blend[mask])+source[incoming]*blend[mask]
    return result


def run(plugin,host,output):
    if output.exists():raise ValueError('Choose a fresh capture directory')
    output.mkdir(parents=True);cases=[];details={}
    for voice,kind,velocities in [(1,'staccato',[32,33,64,65,96,97,127]),(2,'staccatissimo',[42,43,85,86,127])]:
        rows=json.loads((ROOT/'artifacts/recording-audit/war-tuba-audition'/kind/'sources.json').read_text());details[voice]=rows
        for key in range(29,56):
            for velocity in velocities:
                identifier=f'{voice}-{key}-{velocity}';folder=output/identifier;folder.mkdir()
                takes=3 if voice==1 else 4;events=[]
                for take in range(takes):events.extend([[take*8192,144,key,velocity],[take*8192+4096,176,120,0]])
                cases.append(dict(id=identifier,tuba_voice=voice,part=3,instrument=0,articulation=1,rate=44100,root=key,velocity=velocity,
                                  frames=takes*8192,post_release_frames=0,directory=str(folder),midi_events=events))
    for key in range(29,56):
        identifier=f'sustain-{key}';folder=output/identifier;folder.mkdir()
        cases.append(dict(id=identifier,tuba_voice=1,part=3,instrument=0,articulation=0,rate=44100,root=key,velocity=127,
                          frames=8192,post_release_frames=0,directory=str(folder),midi_events=[[0,144,key,127],[4096,176,120,0]]))
    for voice in [1,2]:
        identifier=f'loop-{voice}';folder=output/identifier;folder.mkdir()
        cases.append(dict(id=identifier,tuba_voice=voice,part=3,instrument=0,articulation=0,rate=44100,root=36,velocity=127,
                          frames=529200,post_release_frames=132300,directory=str(folder)))
    for voice in [0,1,2]:
        identifier=f'section-{voice}';folder=output/identifier;folder.mkdir()
        cases.append(dict(id=identifier,tuba_voice=voice,part=3,instrument=0,articulation=1,ensemble=True,horn_feel=.4,rate=48000,root=60,velocity=112,
                          frames=192000,post_release_frames=96000,directory=str(folder),midi_events=[[0,144,60,112],[48,144,64,104],[96,144,67,109],[192000,176,120,0]]))
    config=output/'config.json';config.write_text(json.dumps(dict(cases=cases),indent=2)+'\n');subprocess.run([str(host),str(plugin),str(config)],check=True)
    matches=[]
    for case in cases:
        if case['id'].startswith(('loop','section')):continue
        rows=details[case['tuba_voice']]
        selected=sorted([row for row in rows if row['articulation']==case['articulation'] and row['root']==case['root'] and row['low_velocity']<=case['velocity']<=row['high_velocity']],key=lambda row:row['take'])
        actual,rate=sf.read(Path(case['directory'])/'neutral-plugin-raw.wav',always_2d=True);assert rate==44100
        for take,row in enumerate(selected):
            original,sr=sf.read(CACHE/row['path'],dtype='float32');assert sr==rate
            gain=10**((-24+(9 if case['articulation']==0 else 0))/20)*2**(-.5)*(case['velocity']/127)**1.3
            reference=original[:4096,None]*gain;played=actual[take*8192:take*8192+4096]
            residual=played-reference;error=float(np.max(np.abs(residual)));db=float(10*np.log10(max(np.mean(residual**2)/np.mean(reference**2),1e-30)))
            assert error<1e-7 and db< -100,(case['id'],take,error,db)
            matches.append(dict(case=case['id'],take=take,source=row['path'],max_error=error,residual_relative_db=db))
    assert len(matches)==1134
    loops=json.loads((ROOT/'artifacts/war-tuba-audit/loop-audit.json').read_text())['recordings_data'];row=next(row for row in loops if row['path']=='Samples/c1_sus_cnd.wav')
    source,rate=sf.read(CACHE/row['path'],dtype='float32');hold=529200;ending=132300
    reference=loop_body(source,hold+ending,row['loop_start'],row['loop_end'],1764)
    release=row['release_selection'];duration=len(source)-release['start'];blend=np.minimum(1,np.arange(duration)/release['crossfade'])
    reference[hold:hold+duration]=reference[hold:hold+duration]*(1-blend)+source[release['start']:]*blend
    reference[hold:hold+duration]*=np.clip((len(source)-1-(release['start']+np.arange(duration)))/(rate*.003),0,1)
    reference[hold+duration:]=0;reference=np.column_stack([reference,reference])*10**(-15/20)*2**(-.5)
    loop_checks=[]
    for voice in [1,2]:
        actual,sr=sf.read(output/f'loop-{voice}/neutral-plugin-raw.wav',always_2d=True);assert sr==rate
        error=actual-reference;maximum=float(np.max(np.abs(error)));residual=float(10*np.log10(max(np.mean(error**2)/np.mean(reference**2),1e-30)))
        assert maximum<1e-7 and residual< -100,(voice,maximum,residual)
        assert np.max(np.abs(actual[-44100:]))==0
        loop_checks.append(dict(voice=voice,max_error=maximum,residual_relative_db=residual))
    first,_=sf.read(output/'loop-1/neutral-plugin-raw.wav',always_2d=True);second,_=sf.read(output/'loop-2/neutral-plugin-raw.wav',always_2d=True);assert np.array_equal(first,second)
    metrics=compare_arrays(reference,first,rate)
    plot_pair(reference,first,rate,metrics,0,len(reference),output/'loop-and-release.png','Declared native-loop/file-decay reference vs actual VST; fixed gains retained',row['loop_end']-1764)
    versions={json.loads((Path(case['directory'])/'neutral-host.json').read_text())['version'] for case in cases};assert versions=={'0.17.0'},versions
    for case in cases:
        metadata=json.loads((Path(case['directory'])/'neutral-host.json').read_text());parameters={p['name']:p for p in metadata['parameters']}
        assert parameters['Tuba voice']['text']==['Original tuba','War staccato','War staccatissimo'][case['tuba_voice']]
    report=dict(version='0.17.0',plugin=str(plugin),source_matches=matches,loop_and_release=loop_checks,
                war_sustain_styles_bit_identical=True,all_passed=True,musical_realism_accepted=False,
                scope='1134 native root/force/take attacks;12s native loop and note-off with3s ending; style/section captures. This is playback/reference fidelity, not a perceptual seam or sound-quality verdict.')
    (output/'results.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:1134 integrated source selections, native loops/file decay, shared sustain styles and real section captures')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('plugin',type=Path);parser.add_argument('host',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args();run(args.plugin.resolve(),args.host.resolve(),args.output.resolve())
