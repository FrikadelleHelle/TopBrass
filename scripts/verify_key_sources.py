"""Compare mechanical bank PCM and zone identity against the pinned original sources."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import numpy as np
import soundfile as sf

ROOT=Path(__file__).resolve().parents[1]


def verify(path,report):
    catalog=json.loads((ROOT/'assets/cache/bear-details/catalog.json').read_text())
    for mapping in catalog['maps']:
        original=ROOT/'assets/cache/karoryfer.bear-sax'/mapping['path']
        data=original.read_bytes()
        if hashlib.sha1(f'blob {len(data)}\0'.encode()+data).hexdigest()!=mapping['blob']:raise ValueError('Mapping source changed')
    expected_zones=[z for z in catalog['zones'] if z['kind']!='airflow']
    paths={z['source'] for z in expected_zones}
    originals=[r for r in catalog['recordings'] if r['path'] in paths]
    indices={r['path']:i for i,r in enumerate(originals)}
    with path.open('rb') as bank:
        if bank.read(8)!=b'TBKEYS01':raise ValueError('Invalid detail format')
        count,zones=struct.unpack('<2I',bank.read(8))
        if (count,zones)!=(152,248) or count!=len(originals):raise ValueError('Incomplete mechanical corpus')
        for record in originals:
            data=(ROOT/record['local_file']).read_bytes()
            if hashlib.sha1(f'blob {len(data)}\0'.encode()+data).hexdigest()!=record['blob']:raise ValueError('WAV source changed')
            original,rate=sf.read(ROOT/record['local_file'],dtype='float32',always_2d=True)
            packed_rate,frames=struct.unpack('<2I',bank.read(8))
            pcm=np.frombuffer(bank.read(frames*4),dtype='<f4')
            if packed_rate!=rate or original.shape!=(frames,1) or not np.array_equal(pcm,original[:,0]):raise ValueError('Original mechanical PCM altered')
        coverage=set()
        for source in expected_zones:
            record,key,root,opening,take,gain=struct.unpack('<5If',bank.read(24))
            op=source['opcodes']
            expected=(indices[source['source']],int(op['lokey']),int(op.get('pitch_keycenter',60)),int(source['kind']=='key-open'),round(float(op.get('lorand',0))*4))
            if (record,key,root,opening,take)!=expected or abs(gain-10**(-12/20))>1e-8:raise ValueError('Authored key mapping changed')
            coverage.add((key,opening,take))
        wanted={(key,trigger,take) for key in range(36,69) if key not in (52,64) for trigger in (0,1) for take in range(4)}
        if coverage!=wanted or bank.read(1):raise ValueError('Mechanical coverage or trailing data differs')
    result=dict(recordings=count,zones=zones,original_pcm_exact=True,authored_root_and_gain=True,missing_keys=[52,64],airflow_included=False)
    report.parent.mkdir(parents=True,exist_ok=True);report.write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: 152 original mechanical recordings, 248 authored regions, exact PCM/roots/gain and coverage')


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('bank',type=Path);parser.add_argument('report',type=Path);args=parser.parse_args();verify(args.bank,args.report)
