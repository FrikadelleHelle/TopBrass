"""Pack the original MTG tenor breath and key recordings without audio processing."""
import hashlib
import io
import json
from pathlib import Path
import shutil
import struct

import numpy as np
import soundfile as sf

ROOT=Path(__file__).resolve().parents[1]
CACHE=ROOT/'assets/cache/MTG.SoloSax'


def main():
    tree=json.loads((CACHE/'source-tree.json').read_text())
    assert tree['repo']=='sfzinstruments/MTG.SoloSax' and tree['commit']=='b494d256549b3d088fdec176ce82867f8a1f58b2'
    entries={entry['path']:entry for entry in tree['tree']}
    catalogue=[]
    output=ROOT/'assets/TopBrass.tenor'
    with io.BytesIO() as stream:
        stream.write(b'TBTEN001'+struct.pack('<2I',64,33))
        for kind,count in [('breath',64),('keys',33)]:
            prefix='b' if kind=='breath' else 'k'
            for take in range(1,count+1):
                name=f'MTG Solo Saxophones/Samples/ten_{prefix}_{take:02}.flac';path=CACHE/name
                data=path.read_bytes();assert hashlib.sha1(f'blob {len(data)}\0'.encode()+data).hexdigest()==entries[name]['sha']
                audio,rate=sf.read(path,dtype='float32',always_2d=True)
                assert audio.shape[1]==1 and rate==48000 and np.isfinite(audio).all() and np.max(np.abs(audio))<=1
                pcm=audio.T.astype('<f4').tobytes();stream.write(struct.pack('<2I',rate,len(audio)));stream.write(pcm)
                catalogue.append(dict(kind=kind,take=take,path=name,git_blob_sha1=entries[name]['sha'],source_sha256=hashlib.sha256(data).hexdigest(),pcm_sha256=hashlib.sha256(pcm).hexdigest(),frames=len(audio),rate=rate))
        payload=stream.getvalue()
    refs=ROOT/'assets/plugin-resources'
    for source,target in [('LICENSE','MTG-SoloSax-LICENSE.txt'),('README.md','MTG-SoloSax-README.md')]:
        assert hashlib.sha1(f'blob {(CACHE/source).stat().st_size}\0'.encode()+(CACHE/source).read_bytes()).hexdigest()==entries[source]['sha']
        shutil.copy2(CACHE/source,refs/target)
    (refs/'MTG-SoloSax-attribution.txt').write_text('Tenor detail recordings: Music Technology Group, Universitat Pompeu Fabra, Barcelona.\nSFZ preparation: kinwie.\nSource: https://github.com/sfzinstruments/MTG.SoloSax\nPinned commit: b494d256549b3d088fdec176ce82867f8a1f58b2\nLicense: Creative Commons Attribution4.0 International; see MTG-SoloSax-LICENSE.txt.\nTopBrass retains complete decoded PCM and provides explicit one-shot triggers; it does not recreate the source SFZ instrument or automatically schedule phrase inhalations. These recordings come from another tenor source than the main VCSL tenor.\n')
    if not output.exists() or output.read_bytes()!=payload:
        temporary=output.with_suffix('.tenor.tmp');temporary.write_bytes(payload);temporary.replace(output)
    report=dict(repository=tree['repo'],commit=tree['commit'],recordings=catalogue,bank_sha256=hashlib.sha256(output.read_bytes()).hexdigest(),decoded_pcm_bytes=sum(r['frames']*4 for r in catalogue),processing='Full original decoded mono PCM, native rate; no crop, normalization, fade, denoising, tuning or looping. Source publisher preparation is described in the retained readme.')
    (ROOT/'assets/TopBrass.tenor.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Packed97 source recordings;',report['decoded_pcm_bytes'],'decoded PCM bytes')


if __name__=='__main__':main()
