"""Append source-preserving War Tuba notes and mappings to the qualified0.16 bank."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import numpy as np
import soundfile as sf

from war_tuba_source import CACHE,COMMIT,REPO,checked,mapping
from prepare_samples import embedded_loop
from build_bank import sustain_release

BASE_SHA256='74502c084e52190acf8842625ad5c4f3a30e1e87354fd0efd09dedd5ca56723b'


def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def copy_bytes(source,destination,count):
    digest=hashlib.sha256()
    while count:
        chunk=source.read(min(count,1024*1024))
        if not chunk:raise ValueError('Truncated base bank')
        digest.update(chunk);destination.write(chunk);count-=len(chunk)
    return digest.hexdigest()


def extend(base,destination):
    base=Path(base);destination=Path(destination);assert sha(base)==BASE_SHA256
    with base.open('rb') as stream:
        assert stream.read(8)==b'TBBANK04';samples,zones=struct.unpack('<2I',stream.read(8));assert (samples,zones)==(2588,2328)
        for _ in range(samples):
            header=struct.unpack('<15I',stream.read(60));assert header[0]<6
            stream.seek((header[5]*header[14]+header[13])*4,1)
        offset=stream.tell();assert offset+zones*48==base.stat().st_size
    rows=mapping('sustain')+mapping('staccato')+mapping('staccatissimo');assert len(rows)==675
    temporary=destination.with_name(destination.name+'.partial');assert not temporary.exists()
    destination.parent.mkdir(parents=True,exist_ok=True);records=[]
    with base.open('rb') as source,temporary.open('xb') as output:
        output.write(b'TBBANK04'+struct.pack('<2I',samples+len(rows),zones+len(rows)));source.seek(16)
        original_pcm_records=copy_bytes(source,output,offset-16)
        for row in rows:
            path=checked(row['path']);audio,rate=sf.read(path,dtype='float32',always_2d=True)
            assert rate==44100 and audio.shape[1]==1 and np.isfinite(audio).all() and 0<np.max(np.abs(audio))<=1
            art={'sustain':0,'staccato':1,'staccatissimo':2}[row['kind']]
            loop=embedded_loop(path) if art==0 else None
            release=sustain_release(audio,rate,loop[1]) if loop else None
            if art==0:assert loop and loop[0]>=1764 and loop[1]>=rate*4 and release and release['start']>=loop[1]
            pcm=audio.T.astype('<f4').tobytes()
            header=(6,art,row['root'],1,rate,len(audio),loop[0] if loop else 0,loop[1] if loop else 0,1764 if loop else 0,
                    release['start'] if release else 0,release['crossfade'] if release else 0,int(loop is not None),0,0,1)
            output.write(struct.pack('<15I',*header));output.write(pcm)
            records.append(row|dict(index=samples+len(records),articulation=art,frames=len(audio),rate=rate,channels=1,
                                    loop_start=header[6],loop_end=header[7],loop_crossfade=header[8],pre_start_crossfade=bool(loop),
                                    release_selection=release,decoded_pcm_sha256=hashlib.sha256(pcm).hexdigest(),file_sha256=sha(path)))
        original_zone_records=copy_bytes(source,output,zones*48);assert source.read()==b''
        for row in records:
            output.write(struct.pack('<10I2f',row['index'],6,row['articulation'],row['root'],row['low_key'],row['high_key'],row['low_velocity'],row['high_velocity'],0,row['take'],0,10**(row['volume_db']/20)))
    temporary.replace(destination)
    metadata=json.loads(base.with_suffix('.json').read_text())
    report=metadata|dict(samples=samples+len(rows),zones=zones+len(rows),bytes=destination.stat().st_size,bank_sha256=sha(destination),
                         previous_bank_sha256=BASE_SHA256,previous_sample_records_sha256=original_pcm_records,previous_zone_records_sha256=original_zone_records,
                         war_tuba=dict(repository=REPO,commit=COMMIT,instrument=6,recordings=records,
                                       gain='Author sustain+9dB scalar preserved; no source EQ, normalization or velocity-curve emulation.',
                                       loops='Native forward loops, 40ms pre-start crossfade; original PCM unchanged. File-decay coordinates are detected.',
                                       takes='Deterministic cycles through actual recordings; source random selection is not recreated.',musical_realism_accepted=False))
    destination.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({key:report[key] for key in ['samples','zones','bytes','bank_sha256']},indent=2));return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('base',type=Path);parser.add_argument('destination',type=Path);args=parser.parse_args();extend(args.base,args.destination)
