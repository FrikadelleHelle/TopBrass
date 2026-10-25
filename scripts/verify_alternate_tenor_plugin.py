"""Capture the real plug-in and verify all198 alternate tenor mappings against source PCM."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np
import soundfile as sf

from verify_mtg_tenor_sources import records, digest
from recording_audit import plot_pair, compare_arrays

ROOT=Path(__file__).resolve().parents[1]


def expected(recording, root, target, cents, frames, velocity, gain):
    positions=np.arange(frames)*2**((target-root)/12+cents/1200)
    indices=np.floor(positions).astype(int); t=positions-indices
    a,b,c,d=[recording[np.clip(indices+delta,0,len(recording)-1)].astype(np.float64) for delta in (-1,0,1,2)]
    sample=b+.5*t*(c-a+t*(2*a-5*b+4*c-d+t*(3*(b-c)+d-a)))
    return sample*10**(-24/20)*2**(-.5)*(velocity/127)**1.3*gain


def run(plugin,host,output,version):
    assert not (output/'results.json').exists(),'Retain previous evidence; choose a new output'
    output.mkdir(parents=True,exist_ok=True)
    bank=plugin/'Contents/Resources/TopBrass.bank'
    _,zones,_=records(bank);alternate=[zone for zone in zones if zone[1]==5 and zone[2]==0]
    assert len(alternate)==198
    cases=[]
    for force,velocity in [('soft',100),('strong',101)]:
        for key in range(44,77):
            identifier=f'{force}-{key}';folder=output/identifier;folder.mkdir(exist_ok=True)
            events=[]
            for take in range(3):
                events.extend([[take*12288,144,key,velocity],[take*12288+8192,176,120,0]])
            cases.append(dict(id=identifier,part=0,instrument=0,articulation=0,alternate_tenor=True,
                              rate=48000,root=key,velocity=velocity,frames=36864,post_release_frames=0,
                              midi_events=events,directory=str(folder.resolve())))
    for label,art,frames in [('held',0,288000),('gated-sustain',0,4800),('gated-short',1,4800)]:
        folder=output/label;folder.mkdir(exist_ok=True)
        cases.append(dict(id=label,part=0,instrument=0,articulation=art,alternate_tenor=True,
                          rate=48000,root=60,velocity=101,frames=frames,post_release_frames=96000,directory=str(folder.resolve())))
    events=[[0,144,60,112],[48,144,64,98],[96,144,67,105],
            [96000,128,60,0],[96000,128,64,0],[96000,128,67,0]]
    for alternate_source in [False,True]:
        identifier='groove-alternate' if alternate_source else 'groove-original'
        folder=output/identifier;folder.mkdir(exist_ok=True)
        cases.append(dict(id=identifier,part=0,instrument=0,articulation=0,ensemble=True,
                          alternate_tenor=alternate_source,rate=48000,frames=96000,post_release_frames=96000,
                          midi_events=events,directory=str(folder.resolve())))
    config=output/'config.json';config.write_text(json.dumps(dict(cases=cases),indent=2)+'\n')
    subprocess.run([str(host),str(plugin),str(config)],check=True)
    matches=[]
    for case in cases[:66]:
        capture,rate=sf.read(Path(case['directory'])/'neutral-plugin-raw.wav',always_2d=True)
        assert rate==48000 and np.isfinite(capture).all()
        for take in range(3):
            zone=next(z for z in alternate if z[4]==case['root'] and z[6]<=case['velocity']<=z[7] and z[9]==take)
            index=zone[0]-2522;force='p' if index<33 else 'f';number=index%33+1
            source=ROOT/f'assets/cache/MTG.SoloSax/MTG Solo Saxophones/Samples/ten_{force}_{number:02}.flac'
            original,sr=sf.read(source,dtype='float32');assert sr==rate
            predicted=expected(original,zone[3],case['root'],zone[10],8192,case['velocity'],zone[11])
            actual=capture[take*12288:take*12288+8192]
            error=actual-predicted[:,None]
            peak=float(np.max(np.abs(error)))
            residual=float(20*np.log10(max(np.sqrt(np.mean(error**2)),1e-30)/max(np.sqrt(np.mean(predicted**2)),1e-30)))
            assert np.max(np.abs(actual))>1e-7 and peak<1e-7 and residual<-100,(case['id'],take,peak,residual)
            matches.append(dict(key=case['root'],velocity=case['velocity'],take=take,recording=zone[0],
                                source=str(source.relative_to(ROOT)),tuning_cents=zone[10],region_gain=zone[11],
                                max_error=peak,residual_relative_db=residual))
            if case['root']==60 and take==0:
                folder=Path(case['directory']);sf.write(folder/'mapped-source-reference.wav',np.column_stack([predicted,predicted]),rate,subtype='FLOAT')
                sf.write(folder/'played-attack.wav',actual,rate,subtype='FLOAT')
                reference=np.column_stack([predicted,predicted])
                metrics=compare_arrays(reference,actual,rate)
                plot_pair(reference,actual,rate,metrics,0,len(actual),folder/'attack-spectrum.png','Authored tuning/gain source reference vs actual neutral plug-in attack')
    for label in ['held','gated-sustain','gated-short']:
        samples,rate=sf.read(output/label/'neutral-plugin-raw.wav',always_2d=True)
        assert np.isfinite(samples).all() and np.max(np.abs(samples))>1e-7
        assert np.max(np.abs(samples[-48000:]))==0,label+' must finish without a loop or hung tail'
    sustain,rate=sf.read(output/'gated-sustain/neutral-plugin-raw.wav',always_2d=True)
    short,_=sf.read(output/'gated-short/neutral-plugin-raw.wav',always_2d=True)
    assert np.array_equal(sustain,short),'Alternate Short must gate its sustain rather than claim a recorded Short articulation'
    versions={json.loads((Path(case['directory'])/'neutral-host.json').read_text())['version'] for case in cases}
    assert versions=={version},versions
    report=dict(version=version,format=plugin.suffix,plugin=str(plugin),bank_sha256=digest(bank),
                mapped_attacks=matches,all_source_checks_passed=True,held_recording_finishes=True,
                short_is_gated_sustain=True,musical_realism_accepted=False)
    (output/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS:198 actual plug-in source mappings, authored tuning/gain/force/sequence, finite hold and gated Short')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('plugin',type=Path);parser.add_argument('host',type=Path);parser.add_argument('output',type=Path)
    parser.add_argument('--version',default='0.17.0')
    args=parser.parse_args();run(args.plugin.resolve(),args.host.resolve(),args.output.resolve(),args.version)
