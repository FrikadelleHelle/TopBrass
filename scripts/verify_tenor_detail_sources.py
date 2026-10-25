"""Independently compare the complete tenor-detail resource with pinned source FLACs."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import numpy as np
import soundfile as sf
ROOT=Path(__file__).resolve().parents[1]
path=Path(sys.argv[1]);output=Path(sys.argv[2]);cache=ROOT/'assets/cache/MTG.SoloSax'
tree=json.loads((cache/'source-tree.json').read_text());assert tree['commit']=='b494d256549b3d088fdec176ce82867f8a1f58b2'
entries={entry['path']:entry for entry in tree['tree']};rows=[]
with path.open('rb') as stream:
    assert stream.read(8)==b'TBTEN001' and struct.unpack('<2I',stream.read(8))==(64,33)
    for prefix,count in [('b',64),('k',33)]:
        for take in range(1,count+1):
            name=f'MTG Solo Saxophones/Samples/ten_{prefix}_{take:02}.flac';source=cache/name;data=source.read_bytes()
            assert hashlib.sha1(f'blob {len(data)}\0'.encode()+data).hexdigest()==entries[name]['sha']
            audio,rate=sf.read(source,dtype='float32',always_2d=True);assert audio.shape[1]==1
            stored_rate,frames=struct.unpack('<2I',stream.read(8));assert (stored_rate,frames)==(rate,len(audio))
            pcm=stream.read(frames*4);assert pcm==audio.T.astype('<f4').tobytes()
            rows.append(dict(path=name,frames=frames,rate=rate,git_blob_sha1=entries[name]['sha'],pcm_sha256=hashlib.sha256(pcm).hexdigest()))
    assert stream.read()==b''
output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(dict(verified_recordings=len(rows),sources=rows),indent=2)+'\n')
print('PASS:97 original pinned FLACs, all frames/rates and exact decoded PCM; no importer-report dependency')
