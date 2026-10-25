"""Append unchanged MTG tenor PCM and authored neighboring-note mappings to the original bank."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

import numpy as np
import soundfile as sf

from fetch_mtg_tenor import CACHE, COMMIT, REPO, source_file
from build_bank import sustain_release

BASE_SHA256 = 'de20cdf23d7c610bbfdf3df1e41759f0a38b7fc3d1baf128f2ba9c513f33715f'


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()


def corpus():
    tree = json.loads((CACHE/'source-tree.json').read_text())
    assert tree['repo']==REPO and tree['commit']==COMMIT and not tree['truncated']
    entries = {row['path']:row for row in tree['tree']}

    def checked(path):
        return source_file(entries[path])

    _, group = checked('MTG Solo Saxophones/Data/group_ten.txt')
    assert re.findall(r'(hivel|lovel)=(\d+)',group.decode())==[('hivel','100'),('lovel','101')]
    _, no_loop = checked('MTG Solo Saxophones/MTG Tenor Sax (NL).sfz')
    assert 'loop_mode=no_loop' in no_loop.decode()
    records = []; indices = {}
    for force in ('p','f'):
        for number in range(1,34):
            relative = f'MTG Solo Saxophones/Samples/ten_{force}_{number:02}.flac'
            path, raw = checked(relative)
            audio, rate = sf.read(path,dtype='float32',always_2d=True)
            assert rate==48000 and audio.shape[1]==1 and np.isfinite(audio).all() and 0<np.max(np.abs(audio))<=1
            release = sustain_release(audio,rate,0)
            assert release is not None,relative
            pcm = audio.T.astype('<f4').tobytes()
            indices[f'ten_{force}_{number:02}.$EXT'] = len(records)
            records.append(dict(path=relative,force=force,root=43+number,rate=rate,frames=len(audio),
                                header=(5,0,43+number,1 if force=='p' else 2,rate,len(audio),0,0,0,release['start'],release['crossfade'],0,0,0,1),
                                pcm=pcm,decoded_pcm_sha256=hashlib.sha256(pcm).hexdigest(),
                                file_sha256=hashlib.sha256(raw).hexdigest(),git_blob_sha1=entries[relative]['sha'],release_selection=release))
    zones = []; maps = []
    for force in ('p','f'):
        for take in range(1,4):
            relative = f'MTG Solo Saxophones/Data/ten_{force}_rr{take}.txt'
            _, raw = checked(relative);maps.append(dict(path=relative,git_blob_sha1=entries[relative]['sha']))
            regions = re.findall(r'<region>([^<]*)',re.sub(r'//[^\n]*','',raw.decode()))
            assert len(regions)==33
            for body in regions:
                attrs = dict(re.findall(r'(\w+)=([^\s]+)',body))
                assert set(attrs)<={'region_label','seq_position','key','pitch_keycenter','sample','volume','tune'}
                key = int(attrs['key']); sample = indices[attrs['sample']]
                root = int(attrs.get('pitch_keycenter',key))
                assert int(attrs['seq_position'])==take and root==records[sample]['root'] and abs(key-root)<=2
                assert records[sample]['force']==force
                cents = float(attrs.get('tune',0));db = float(attrs.get('volume',0))
                assert np.isfinite(cents) and np.isfinite(db) and abs(cents)<=1200 and -96<=db<=24
                zones.append(dict(sample=sample,root=root,key=key,low_velocity=0 if force=='p' else 101,
                                  high_velocity=100 if force=='p' else 127,round_robin=take-1,pitch_cents=cents,gain=10**(db/20),
                                  force=force,source_mapping=relative))
    assert len(records)==66 and len(zones)==198
    assert {(row['key'],row['force'],row['round_robin']) for row in zones}=={(key,force,take) for key in range(44,77) for force in ('p','f') for take in range(3)}
    return records,zones,maps


def layout(path):
    with path.open('rb') as stream:
        assert stream.read(8)==b'TBBANK04'
        samples,zones = struct.unpack('<2I',stream.read(8))
        for _ in range(samples):
            header = struct.unpack('<15I',stream.read(60))
            assert header[0]<5,'Original bank already contains an alternate source'
            stream.seek((header[5]*header[14]+header[13])*4,1)
        offset = stream.tell()
        assert offset+zones*48==path.stat().st_size
    return samples,zones,offset


def copy_region(source,destination,size):
    digest = hashlib.sha256()
    while size:
        data = source.read(min(size,1024*1024))
        if not data:
            raise ValueError('Original bank ended inside a record')
        digest.update(data);destination.write(data);size-=len(data)
    return digest.hexdigest()


def extend(base,destination):
    base = Path(base);destination = Path(destination)
    assert sha(base)==BASE_SHA256,'Original source bank changed; qualify that change before extending it'
    sample_count,zone_count,zone_offset = layout(base)
    records,zones,maps = corpus()
    temporary = destination.with_name(destination.name+'.partial')
    if temporary.exists():
        raise ValueError('Unfinished bank retained: '+str(temporary))
    destination.parent.mkdir(parents=True,exist_ok=True)
    with base.open('rb') as source,temporary.open('xb') as bank:
        bank.write(b'TBBANK04'+struct.pack('<2I',sample_count+len(records),zone_count+len(zones)))
        source.seek(16)
        original_records_sha256 = copy_region(source,bank,zone_offset-16)
        for record in records:
            bank.write(struct.pack('<15I',*record['header']));bank.write(record['pcm'])
        original_zones_sha256 = copy_region(source,bank,zone_count*48)
        assert source.read()==b''
        for zone in zones:
            bank.write(struct.pack('<10I2f',sample_count+zone['sample'],5,0,zone['root'],zone['key'],zone['key'],
                                   zone['low_velocity'],zone['high_velocity'],0,zone['round_robin'],zone['pitch_cents'],zone['gain']))
    temporary.replace(destination)
    metadata = json.loads(base.with_suffix('.json').read_text())
    report = metadata|dict(samples=sample_count+len(records),zones=zone_count+len(zones),
                           bank_sha256=sha(destination),bytes=destination.stat().st_size,
                           base_bank_sha256=BASE_SHA256,base_sample_records_sha256=original_records_sha256,
                           base_zone_records_sha256=original_zones_sha256,
                           alternate_tenor=dict(repository=REPO,commit=COMMIT,instrument=5,recordings=len(records),zones=len(zones),
                                                native_keys=[44,76],velocity_windows=[[0,100],[101,127]],maps=maps,
                                                sources=[{k:v for k,v in row.items() if k not in ('pcm','header')} for row in records],
                                                loop_mode='none; complete finite recordings',musical_realism_accepted=False,
                                                variation='One native recording per key and force. Authored sequences2/3 reuse neighboring recorded notes with their authored root, tuning and gain.',
                                                short='Gated sustain, not dedicated recorded staccato',
                                                sfz_scope='Selected recording mappings only; SFZ filters, LFOs, envelopes, offsets, velocity response and legato engine are not recreated.'))
    destination.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({key:report[key] for key in ('samples','zones','bytes','bank_sha256')},indent=2))
    return report


if __name__=='__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base',type=Path);parser.add_argument('destination',type=Path)
    args = parser.parse_args();extend(args.base,args.destination)
