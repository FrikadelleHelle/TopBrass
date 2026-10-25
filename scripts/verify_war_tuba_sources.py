"""Independently check the appended tuba PCM, native loops and authored note mappings."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

import numpy as np
import soundfile as sf

ROOT=Path(__file__).resolve().parents[1]
CACHE=ROOT/'assets/cache/karoryfer.war-tuba'
COMMIT='5b62dd6ef6b00281bb734769e44a63f5e013018a'


def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def layout(path):
    records=[]
    with path.open('rb') as stream:
        assert stream.read(8)==b'TBBANK04';samples,zones=struct.unpack('<2I',stream.read(8))
        for _ in range(samples):
            start=stream.tell();h=struct.unpack('<15I',stream.read(60));stream.seek((h[5]*h[14]+h[13])*4,1);records.append((start,stream.tell(),h))
        offset=stream.tell();assert offset+zones*48==path.stat().st_size
    return records,zones,offset


def native_loop(data):
    cursor=12
    while cursor+8<=len(data):
        tag,size=struct.unpack_from('<4sI',data,cursor)
        if tag==b'smpl':
            count=struct.unpack_from('<I',data,cursor+36)[0]
            if count:
                _,kind,start,end,fraction,repeats=struct.unpack_from('<6I',data,cursor+44);assert (kind,fraction,repeats)==(0,0,0)
                return start,end+1
        cursor+=8+size+size%2
    return None


def verify(base,bank,output):
    assert sha(base)=='74502c084e52190acf8842625ad5c4f3a30e1e87354fd0efd09dedd5ca56723b'
    old,old_zones,old_offset=layout(base);new,new_zones,new_offset=layout(bank)
    assert (len(old),old_zones,len(new),new_zones)==(2588,2328,3263,3003)
    with base.open('rb') as a,bank.open('rb') as b:
        a.seek(16);b.seek(16);remaining=old_offset-16
        while remaining:
            count=min(remaining,1024*1024);assert a.read(count)==b.read(count),'An original sample record changed';remaining-=count
        a.seek(old_offset);b.seek(new_offset);assert a.read()==b.read(old_zones*48),'An original zone changed'
    tree=json.loads((CACHE/'source-tree.json').read_text());assert tree['commit']==COMMIT and not tree['truncated']
    entries={row['path']:row for row in tree['tree']}
    def source(name):
        raw=(CACHE/name).read_bytes();assert hashlib.sha1(f'blob {len(raw)}\0'.encode()+raw).hexdigest()==entries[name]['sha'];return raw
    expected=[]
    for art,kind in enumerate(['sustain','staccato','staccatissimo']):
        text=source('Programs/poly/'+kind+'_cnd.sfz').decode();group={}
        for section,body in re.findall(r'<(group|region)>([^<]*)',re.sub(r'//[^\n]*','',text)):
            attrs=dict(token.split('=',1) for token in body.split() if '=' in token)
            if section=='group':group=attrs;continue
            attrs=group|attrs;name=attrs['sample'].replace('\\','/').removeprefix('../');rr=re.search(r'_rr(\d+)_',name)
            expected.append(dict(art=art,path=name,root=int(attrs['pitch_keycenter']),low=int(attrs['lokey']),high=int(attrs['hikey']),
                                 vl=int(attrs.get('lovel',0)),vh=int(attrs.get('hivel',127)),rr=int(rr[1])-1 if rr else 0,gain=10**(float(attrs.get('volume',0))/20)))
    assert len(expected)==675 and len({row['path'] for row in expected})==675
    checked=[]
    with bank.open('rb') as stream:
        for index,row in enumerate(expected,2588):
            raw=source(row['path']);audio,rate=sf.read(CACHE/row['path'],dtype='float32',always_2d=True)
            start,end,h=new[index];assert h[:6]==(6,row['art'],row['root'],1,rate,len(audio)) and h[12:]==(0,0,1)
            stream.seek(start+60);pcm=stream.read(end-start-60);assert pcm==audio.T.astype('<f4').tobytes()
            if row['art']==0:
                assert (h[6],h[7])==native_loop(raw) and h[8]==1764 and h[11]==1 and h[7]>=rate*4
                assert h[7]<=h[9]<len(audio) and h[10]==1764 and h[9]+h[10]<len(audio)
            else:assert h[6:12]==(0,0,0,0,0,0)
            stream.seek(new_offset+(2328+index-2588)*48)
            zone=stream.read(48);gain=struct.unpack('<f',struct.pack('<f',row['gain']))[0]
            assert struct.unpack('<10I2f',zone)==(index,6,row['art'],row['root'],row['low'],row['high'],row['vl'],row['vh'],0,row['rr'],0,gain)
            checked.append(dict(index=index,source=row['path'],pcm_sha256=hashlib.sha256(pcm).hexdigest(),frames=len(audio),native_loop=[h[6],h[7]],source_gain=gain))
    report=dict(base_records_unchanged=2588,base_zones_unchanged=2328,appended_pcm_exact=675,appended_mappings_verified=675,native_sustain_loops=27,
                source_commit=COMMIT,bank_sha256=sha(bank),recordings=checked,musical_realism_accepted=False)
    output.write_text(json.dumps(report,indent=2)+'\n');print('PASS:2588 unchanged records/2328 zones,675 exact original recordings and authored mappings,27 native loops')


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('base',type=Path);parser.add_argument('bank',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args();verify(args.base,args.bank,args.output)
