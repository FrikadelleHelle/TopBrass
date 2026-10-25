"""Independently verify original airflow PCM and RIFF loop metadata."""
import argparse,hashlib,json,struct
from pathlib import Path
import numpy as np
import soundfile as sf
ROOT=Path(__file__).resolve().parents[1]


def verify(path,report):
    catalog=json.loads((ROOT/'assets/cache/bear-details/catalog.json').read_text())
    source=next(r for r in catalog['recordings'] if r['path']=='Samples/noise_breath.wav')
    raw=(ROOT/source['local_file']).read_bytes()
    if hashlib.sha1(f'blob {len(raw)}\0'.encode()+raw).hexdigest()!=source['blob']:raise ValueError('Original source changed')
    loop=None;offset=12
    while offset+8<=len(raw):
        tag,size=struct.unpack_from('<4sI',raw,offset);body=offset+8
        if tag==b'smpl':
            count,=struct.unpack_from('<I',raw,body+28)
            if count!=1:raise ValueError('Expected one native loop')
            _,mode,start,end,_,_=struct.unpack_from('<6I',raw,body+36)
            if mode!=0:raise ValueError('Unsupported native loop')
            loop=(start,end+1)
        offset=body+size+(size%2)
    original,rate=sf.read(ROOT/source['local_file'],dtype='float32',always_2d=True)
    with path.open('rb') as bank:
        if bank.read(8)!=b'TBAIR001':raise ValueError('Invalid airflow format')
        sr,frames,start,end,low,high,root,track,gain=struct.unpack('<7I2f',bank.read(36))
        audio=np.frombuffer(bank.read(frames*4),dtype='<f4')
        if (sr,frames)!=(rate,len(original)) or original.shape[1]!=1 or not np.array_equal(audio,original[:,0]):raise ValueError('Original airflow PCM changed')
        if (start,end)!=loop or (low,high,root)!=(36,68,50) or abs(track-.1)>1e-7 or abs(gain-.4)>1e-7 or bank.read(1):raise ValueError('Native airflow mapping changed')
    result=dict(original_pcm_exact=True,native_loop_exact=True,frames=frames,rate=rate,loop_start=start,loop_end_exclusive=end)
    report.parent.mkdir(parents=True,exist_ok=True);report.write_text(json.dumps(result,indent=2)+'\n');print('PASS: original airflow PCM, native loop, rate, root, range and calibration')


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('bank',type=Path);parser.add_argument('report',type=Path);args=parser.parse_args();verify(args.bank,args.report)
