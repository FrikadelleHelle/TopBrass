"""Independently compare packed CC0 PCM against the pinned original WAVs."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import numpy as np
import soundfile as sf

ROOT=Path(__file__).resolve().parents[1]


def verify(bank_path, report_path):
    expected=[e for e in json.loads((ROOT/'assets/samples.lock.json').read_text()) if e['instrument'] in (0,3,4)]
    verified=[]
    with bank_path.open('rb') as bank:
        magic=bank.read(8)
        if magic not in (b'TBBANK02',b'TBBANK03',b'TBBANK04'):
            raise ValueError('Unknown bank format')
        count,=struct.unpack('<I',bank.read(4))
        if magic!=b'TBBANK02':bank.read(4)
        if count<len(expected):raise ValueError('Required originals absent')
        for index,e in enumerate(expected):
            size=9 if magic==b'TBBANK02' else 14 if magic==b'TBBANK03' else 15
            fields=struct.unpack('<'+str(size)+'I',bank.read(size*4))
            instrument,art,root,layer,rate,frames=fields[:6]
            channels=fields[14] if magic==b'TBBANK04' else 1
            path=ROOT/'assets/cache'/e['repo'].split('/')[-1]/e['path']
            source_bytes=path.read_bytes()
            blob=hashlib.sha1(f'blob {len(source_bytes)}\0'.encode()+source_bytes).hexdigest()
            if blob!=e['blob']:raise ValueError('Original source hash mismatch: '+str(path))
            original,sr=sf.read(str(path),dtype='float32',always_2d=True)
            if (instrument,art,root,layer)!=(e['instrument'],e['articulation'],e['root'],e['layer']):
                raise ValueError('Pinned sample ordering/identity mismatch at '+str(index))
            if channels!=original.shape[1]:
                raise ValueError(f'{e["path"]}: packed {channels} channels, original {original.shape[1]}')
            if rate!=sr or frames!=len(original):
                raise ValueError(f'{e["path"]}: source frame count/sample rate changed')
            packed=np.frombuffer(bank.read(frames*channels*4),dtype='<f4').reshape(channels,frames).T
            if not np.array_equal(packed,original):
                raise ValueError('Original PCM was altered: '+e['path'])
            if magic!=b'TBBANK02':bank.read(fields[13]*4)
            onset=np.flatnonzero(np.max(np.abs(original),axis=1)>1e-4)
            verified.append(dict(onset_above_minus80db_ms=float(onset[0]*1000/sr) if len(onset) else None,sample=index,path=e['path'],frames=frames,channels=channels,rate=rate,
                                 original_pcm_sha256=hashlib.sha256(original.T.astype('<f4').tobytes()).hexdigest()))
    report=dict(samples=len(verified),original_pcm_exact=True,instrument_gain=1,
                relative_note_take_and_dynamic_levels_preserved=True,recordings=verified)
    report_path.parent.mkdir(parents=True,exist_ok=True)
    report_path.write_text(json.dumps(report,indent=2)+'\n')
    print('PASS:',len(verified),'original recordings, exact channels/frames/rate/PCM and source-relative levels')


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('bank',type=Path);parser.add_argument('report',type=Path)
    args=parser.parse_args();verify(args.bank,args.report)
