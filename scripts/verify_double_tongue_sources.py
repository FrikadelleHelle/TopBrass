"""Verify new brass PCM and mappings directly against pinned XML and FLAC."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import xml.etree.ElementTree as ET

import numpy as np
import soundfile as sf

from audit_bell_samples import CACHE, SOURCE_HASHES


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify(bank_path, report_path):
    samples = {}
    with bank_path.open('rb') as bank:
        if bank.read(8) != b'TBBANK04':
            raise ValueError('Expected the stereo mapped bank')
        count, zone_count = struct.unpack('<II', bank.read(8))
        for index in range(count):
            h = struct.unpack('<15I', bank.read(60))
            length = (h[5]*h[14]+h[13])*4
            if h[1] == 5:
                if h[14] != 1 or h[13] != 0 or h[6:12] != (0, 0, 0, 0, 0, 0):
                    raise ValueError('Unexpected double-tongue recording metadata')
                samples[index] = (h, np.frombuffer(bank.read(length), dtype='<f4'))
            else:
                bank.seek(length, 1)
        regions = {}
        for _ in range(zone_count):
            z = struct.unpack('<10I2f', bank.read(48))
            if z[2] == 5:
                key = z[1:10]
                if key in regions:
                    raise ValueError('Duplicate double-tongue mapping')
                regions[key] = z
    extraction = json.loads((CACHE/'extraction.json').read_text())
    checked = []
    for instrument, name in [(1, 'trumpet'), (2, 'trombone')]:
        mapping = CACHE/'public-source'/f'{name}_articulations.xml'
        if digest(mapping) != SOURCE_HASHES[mapping.name]:
            raise ValueError('Pinned articulation map changed')
        monolith = CACHE/f'{name}_articulations.ch1.flac'
        expected = next(row['sha256'] for row in extraction['entries'] if row['file'] == monolith.name)
        if digest(monolith) != expected:
            raise ValueError('Pinned archive FLAC changed')
        with sf.SoundFile(monolith) as source:
            for element in ET.parse(mapping).getroot():
                row = element.attrib
                if '/doubleTongue/' not in row['FileName']:
                    continue
                key = (instrument, 5, int(row['Root']), int(row['LoKey']), int(row['HiKey']),
                       int(row['LoVel']), int(row['HiVel']), 0, int(row['RRGroup'])-9)
                z = regions.pop(key)
                h, pcm = samples[z[0]]
                start, end = int(row['SampleStart']), int(row['SampleEnd'])
                source.seek(int(row['MonolithOffset'])+start)
                original = source.read(end-start, dtype='float32')
                gain = (float(row.get('NormalizedPeak', 1)) if int(row.get('Normalized', 0)) else 1)
                gain *= math.pow(10, float(row.get('Volume', 0))/20)
                if z[10] != float(np.float32(float(row.get('Pitch', 0)))) or z[11] != float(np.float32(gain)):
                    raise ValueError('Authored tuning/gain changed or an unintended articulation gain was applied')
                if h[0] != instrument or h[2] != int(row['Root']) or h[4] != source.samplerate or not np.array_equal(pcm, original):
                    raise ValueError('Double-tongue PCM, identity or sample rate changed')
                checked.append(dict(instrument=name, source_file=row['FileName'], sample=z[0],
                                    frames=len(original), round_robin=key[-1],
                                    source_pcm_sha256=hashlib.sha256(original.astype('<f4').tobytes()).hexdigest()))
    if regions or len(samples) != 472 or len(checked) != 472:
        raise ValueError('Double-tongue corpus coverage differs from the pinned maps')
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(dict(recordings=len(samples), regions=len(checked),
                                          original_pcm_exact=True, authored_gain_and_tuning_exact=True,
                                          articulation_gain_db=0, checked=checked), indent=2)+'\n')
    print('PASS:472 original double-tongue recordings/regions, exact PCM, crops, tuning and0dB articulation calibration')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('bank', type=Path)
    parser.add_argument('report', type=Path)
    args = parser.parse_args()
    verify(args.bank, args.report)
