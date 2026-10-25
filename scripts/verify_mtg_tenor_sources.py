"""Independently verify the appended tenor bank against pinned files and authored maps."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]
BASE_HASH = 'de20cdf23d7c610bbfdf3df1e41759f0a38b7fc3d1baf128f2ba9c513f33715f'
COMMIT = 'b494d256549b3d088fdec176ce82867f8a1f58b2'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def records(path):
    with path.open('rb') as stream:
        assert stream.read(8) == b'TBBANK04'
        count, zone_count = struct.unpack('<2I', stream.read(8))
        offsets = []
        for _ in range(count):
            start = stream.tell()
            header = struct.unpack('<15I', stream.read(60))
            stream.seek(4 * (header[5] * header[14] + header[13]), 1)
            offsets.append((start, stream.tell(), header))
        zone_offset = stream.tell()
        zones = [struct.unpack('<10I2f', stream.read(48)) for _ in range(zone_count)]
        assert not stream.read(), 'Unexpected bank trailer'
        return offsets, zones, zone_offset


def verify(base, bank, report):
    assert digest(base) == BASE_HASH, 'Unqualified original bank'
    old_records, old_zones, old_zone_offset = records(base)
    new_records, new_zones, new_zone_offset = records(bank)
    assert len(old_records) == 2522 and len(new_records) == 2588
    assert len(old_zones) == 2130 and len(new_zones) == 2328
    with base.open('rb') as original, bank.open('rb') as extended:
        original.seek(16); extended.seek(16)
        remaining = old_zone_offset - 16
        while remaining:
            count = min(remaining, 1024*1024)
            assert original.read(count) == extended.read(count), 'An original sample record changed'
            remaining -= count
        original.seek(old_zone_offset); extended.seek(new_zone_offset)
        assert original.read() == extended.read(len(old_zones)*48), 'An original mapping changed'
    cache = ROOT/'assets/cache/MTG.SoloSax'
    tree = json.loads((cache/'source-tree.json').read_text())
    assert tree['commit'] == COMMIT and tree['repo'] == 'sfzinstruments/MTG.SoloSax' and not tree['truncated']
    pinned = {entry['path']: entry['sha'] for entry in tree['tree'] if entry['type'] == 'blob'}

    def source(relative):
        path = cache/relative
        raw = path.read_bytes()
        assert hashlib.sha1(f'blob {len(raw)}\0'.encode()+raw).hexdigest() == pinned[relative], relative
        return path, raw

    verified = []; source_indices = {}
    with bank.open('rb') as stream:
        for index, (start, end, header) in enumerate(new_records[2522:], 2522):
            force = 'p' if index < 2555 else 'f'
            number = (index-2522) % 33 + 1
            relative = f'MTG Solo Saxophones/Samples/ten_{force}_{number:02}.flac'
            path, raw = source(relative)
            audio, rate = sf.read(path, dtype='float32', always_2d=True)
            assert header[:6] == (5, 0, 43+number, 1 if force == 'p' else 2, rate, len(audio))
            assert header[6:9] == (0, 0, 0) and header[11:15] == (0, 0, 0, 1)
            assert 0 < header[9] < len(audio) and 0 < header[10] <= len(audio)-header[9]
            expected = audio.T.astype('<f4').tobytes()
            stream.seek(start+60)
            assert stream.read(end-start-60) == expected, 'Altered source PCM: '+relative
            source_indices[f'ten_{force}_{number:02}.$EXT'] = index
            verified.append(dict(index=index, source=relative, frames=len(audio),
                                 source_sha256=hashlib.sha256(raw).hexdigest(), pcm_sha256=hashlib.sha256(expected).hexdigest()))
    expected_zones = []
    for force in ('p', 'f'):
        for take in range(1, 4):
            _, raw = source(f'MTG Solo Saxophones/Data/ten_{force}_rr{take}.txt')
            rows = [row for row in raw.decode().splitlines() if row.strip().startswith('<region>')]
            assert len(rows) == 33
            for row in rows:
                attrs = dict(field.split('=', 1) for field in row.split()[1:])
                assert int(attrs['seq_position']) == take
                key = int(attrs['key']); root = int(attrs.get('pitch_keycenter', key))
                record = source_indices[attrs['sample']]
                assert root == new_records[record][2][2]
                gain = struct.unpack('<f', struct.pack('<f', 10**(float(attrs.get('volume', 0))/20)))[0]
                expected_zones.append((record, 5, 0, root, key, key, 0 if force == 'p' else 101,
                                       100 if force == 'p' else 127, 0, take-1, float(attrs.get('tune', 0)), gain))
    assert new_zones[2130:] == expected_zones, 'Authored source, tuning, gain, force or sequence mapping changed'
    assert {(z[4], z[6], z[9]) for z in expected_zones} == {(key, velocity, take) for key in range(44, 77) for velocity in (0, 101) for take in range(3)}
    _, groups = source('MTG Solo Saxophones/Data/group_ten.txt')
    assert re.findall(r'(hivel|lovel)=(\d+)', groups.decode()) == [('hivel', '100'), ('lovel', '101')]
    _, program = source('MTG Solo Saxophones/MTG Tenor Sax (NL).sfz')
    assert 'loop_mode=no_loop' in program.decode()
    result = dict(original_sample_records_identical=2522, original_zone_records_identical=2130,
                  appended_pcm_exact=True, source_recordings=verified, authored_zones_verified=len(expected_zones),
                  bank_sha256=digest(bank), commit=COMMIT, loop_mode='none', musical_realism_accepted=False)
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(result, indent=2)+'\n')
    print('PASS:2522 original records,2130 original zones,66 exact source recordings and198 authored alternate mappings')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base', type=Path); parser.add_argument('bank', type=Path); parser.add_argument('report', type=Path)
    args = parser.parse_args(); verify(args.base, args.bank, args.report)
