"""Pack original Bear Sax mechanical recordings and their authored mappings."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import numpy as np
import soundfile as sf
from audit_bear_details import audit

ROOT = Path(__file__).resolve().parents[1]


def build(destination):
    audit()
    catalog=json.loads((ROOT/'assets/cache/bear-details/catalog.json').read_text())
    zones=[z for z in catalog['zones'] if z['kind'] in ('key-close','key-open')]
    paths={z['source'] for z in zones}
    recordings=[r for r in catalog['recordings'] if r['path'] in paths]
    indices={r['path']:i for i,r in enumerate(recordings)}
    if len(recordings)!=152 or len(zones)!=248:raise ValueError('Unexpected pinned mechanical corpus')
    destination.parent.mkdir(parents=True,exist_ok=True)
    temporary=destination.with_name(destination.name+'.partial')
    with temporary.open('wb') as out:
        out.write(b'TBKEYS01'+struct.pack('<2I',len(recordings),len(zones)))
        for record in recordings:
            path=ROOT/record['local_file'];raw=path.read_bytes()
            blob=hashlib.sha1(f'blob {len(raw)}\0'.encode()+raw).hexdigest()
            if blob!=record['blob']:raise ValueError('Pinned original changed: '+str(path))
            audio,rate=sf.read(path,dtype='float32')
            if audio.ndim!=1 or not np.isfinite(audio).all() or np.max(np.abs(audio))>1:raise ValueError('Invalid mechanical PCM')
            out.write(struct.pack('<2I',rate,len(audio)));out.write(audio.astype('<f4').tobytes())
        for zone in zones:
            op=zone['opcodes'];key=int(op['lokey']);root=int(op.get('pitch_keycenter',60))
            take=int(round(float(op.get('lorand',0))*4))
            if (int(op['hikey'])!=key or op['loop_mode']!='one_shot' or float(op['volume'])!=-12
                    or take not in range(4) or abs(float(op.get('hirand',1))-(take+1)/4)>1e-6):
                raise ValueError('Unsupported mechanical mapping')
            out.write(struct.pack('<5If',indices[zone['source']],key,root,int(zone['kind']=='key-open'),take,10**(-12/20)))
    temporary.replace(destination)
    report=dict(format='TBKEYS01',recordings=len(recordings),zones=len(zones),pcm='Original mono PCM, full frames, unity recording gain',
                mapping_gain_db=-12,alternative_selection='Deterministic rotation of the four source random alternatives',
                airflow_included=False,sha256=hashlib.sha256(destination.read_bytes()).hexdigest(),bytes=destination.stat().st_size)
    destination.with_name(destination.name+'.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('destination',type=Path);args=parser.parse_args();build(args.destination)
