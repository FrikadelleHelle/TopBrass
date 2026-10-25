"""Pack the original baritone airflow recording and its native sustain loop."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import numpy as np
import soundfile as sf
from audit_bear_details import audit
from prepare_samples import embedded_loop
ROOT=Path(__file__).resolve().parents[1]


def build(destination):
    audit()
    catalog=json.loads((ROOT/'assets/cache/bear-details/catalog.json').read_text())
    zone=next(z for z in catalog['zones'] if z['kind']=='airflow')
    record=next(r for r in catalog['recordings'] if r['path']==zone['source'])
    path=ROOT/record['local_file'];raw=path.read_bytes()
    if hashlib.sha1(f'blob {len(raw)}\0'.encode()+raw).hexdigest()!=record['blob']:raise ValueError('Original airflow changed')
    audio,rate=sf.read(path,dtype='float32');loop=embedded_loop(path);op=zone['opcodes']
    if audio.ndim!=1 or loop is None or not np.isfinite(audio).all() or np.max(abs(audio))>1:raise ValueError('Invalid airflow source')
    if op['amp_veltrack']!='0' or op['loop_mode']!='loop_continuous':raise ValueError('Unexpected airflow source mapping')
    metadata=(rate,len(audio),*loop,int(op['lokey']),int(op['hikey']),int(op['pitch_keycenter']))
    track=float(op['pitch_keytrack'])/100;gain=float(op['amplitude_oncc122'])/100
    temporary=destination.with_name(destination.name+'.partial');destination.parent.mkdir(parents=True,exist_ok=True)
    with temporary.open('wb') as out:
        out.write(b'TBAIR001'+struct.pack('<7I2f',*metadata,track,gain));out.write(audio.astype('<f4').tobytes())
    temporary.replace(destination)
    report=dict(format='TBAIR001',source=record,rate=rate,frames=len(audio),loop_start=loop[0],loop_end_exclusive=loop[1],
                gain_at_full_amount=gain,pitch_tracking=track,original_pcm=True,scope='Recorded under-note airflow, not a phrase inhalation or complete SFZ interpreter',
                sha256=hashlib.sha256(destination.read_bytes()).hexdigest(),bytes=destination.stat().st_size)
    destination.with_name(destination.name+'.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('destination',type=Path);args=parser.parse_args();build(args.destination)
