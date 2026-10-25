#!/usr/bin/env python3
"""Fetch a small, attributed source audition using bounded HTTP ZIP range reads."""
import io
import json
from pathlib import Path
import re
import urllib.request
import zipfile
import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'artifacts/trumpet-source-audition'


class RangeReader(io.RawIOBase):
    def __init__(self, url, length):
        self.url, self.length, self.position = url, length, 0
        self.bytes_fetched = 0
    def seekable(self):
        return True
    def seek(self, offset, whence=0):
        self.position = offset if whence == 0 else self.position + offset if whence == 1 else self.length + offset
        if not 0 <= self.position <= self.length:
            raise ValueError('Seek outside archive')
        return self.position
    def tell(self):
        return self.position
    def read(self, size=-1):
        size = min(self.length-self.position, size if size >= 0 else self.length-self.position)
        if size == 0:
            return b''
        if size > 8 * 1024 * 1024 or self.bytes_fetched + size > 45 * 1024 * 1024:
            raise ValueError('Audition acquisition byte limit exceeded')
        start, end = self.position, self.position+size-1
        request = urllib.request.Request(self.url, headers={'Range': f'bytes={start}-{end}', 'Cache-Control': 'no-cache'})
        with urllib.request.urlopen(request, timeout=45) as response:
            content_range = response.headers.get('Content-Range', '')
            if response.status != 206 or content_range != f'bytes {start}-{end}/{self.length}':
                raise ValueError(f'Server did not honor bounded range: {response.status}, {content_range}')
            data = response.read(size+1)
        if len(data) != size:
            raise ValueError('Incomplete archive range')
        self.position += size; self.bytes_fetched += size
        return data


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    record_file = OUT/'record.json'
    if not record_file.exists():
        record_file.write_bytes(urllib.request.urlopen('https://zenodo.org/api/records/4588740', timeout=45).read())
    record = json.loads(record_file.read_text())
    for name in ('sounds.json', 'takes.json'):
        target = OUT/name
        if not target.exists():
            source = next(f['links']['self'] for f in record['files'] if f['key']==name)
            target.write_bytes(urllib.request.urlopen(source, timeout=45).read())
    sounds = json.loads((OUT/'sounds.json').read_text())
    takes = json.loads((OUT/'takes.json').read_text())
    entry = next(f for f in record['files'] if f['key']=='good-sounds.zip')
    remote = RangeReader(entry['links']['self'], entry['size'])
    with zipfile.ZipFile(remote) as archive:
        names = archive.namelist()
        (OUT/'archive-inventory.json').write_text(json.dumps(names,indent=2)+'\n')
        print('Archive entries',len(names),'metadata bytes',remote.bytes_fetched,flush=True)
        for name in names:
            if re.search(r'(^|/)(license|readme|copying|copyright)(\.|$)',name,re.I) and archive.getinfo(name).file_size < 100000:
                data=archive.read(name); (OUT/('archive-'+Path(name).name)).write_bytes(data)
                print('License/reference',name, data[:1800].decode(errors='replace'),flush=True)
        clips=[]
        for pitch in ('C','E','G'):
            candidates=[s for s in sounds.values() if s['instrument']=='trumpet' and s['klass']=='good-sound'
                        and s['note']==pitch and s['octave']==4 and s['dynamics']=='f']
            if not candidates:
                continue
            sound=candidates[0]
            recordings=[t for t in takes.values() if t['sound_id']==sound['id']]
            take=next((t for t in recordings if t['microphone']=='neumann'),recordings[0])
            stem=take['filename'].rsplit('.',1)[0]
            matches=[n for n in names if n.endswith(stem+'.flac') or n.endswith(stem+'.wav')]
            if len(matches)!=1:
                raise ValueError(f'Could not uniquely locate {stem}: {matches[:5]}')
            member=matches[0]; data=archive.read(member)
            original=OUT/Path(member).name; original=original.with_name(f"trumpet-{pitch}4-f-source"+original.suffix)
            original.write_bytes(data)
            x,sr=sf.read(io.BytesIO(data),always_2d=True)
            start=max(0,int(sound['attack'] or 0)-int(sr*.002))
            end=min(len(x),int(sound['offset'] or len(x))+int(sr*.02))
            audition=x[start:end]
            if len(audition)==0:
                raise ValueError('Empty annotated source')
            rms=float(np.sqrt(np.mean(audition*audition)))
            gain=min(.09/max(rms,1e-12),.9/max(float(np.max(np.abs(audition))),1e-12))
            sf.write(OUT/f'trumpet-{pitch}4-f-audition.wav',audition*gain,sr,subtype='PCM_24')
            clips.append(dict(sound=sound,take=take,archive_member=member,source_rate=sr,
                              frames=len(x),trim_start=start,trim_end=end,audition_gain=gain))
            print('Fetched',member,'sound',sound['id'],'rate',sr,flush=True)
    report=dict(record_url='https://zenodo.org/records/4588740',record_metadata_license=record['metadata'].get('license'),
                audio_license='CC BY-NC 4.0, as stated in the record description',
                compressed_archive_bytes=entry['size'],bytes_fetched=remote.bytes_fetched,clips=clips,
                integrated_into_plugin=False,source_tone_acceptance='unverified')
    (OUT/'audition-provenance.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Source audition complete; bytes fetched',remote.bytes_fetched,flush=True)


if __name__=='__main__':
    main()
