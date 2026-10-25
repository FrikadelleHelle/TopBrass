#!/usr/bin/env python3
"""Check sounding pitch and native loop coverage in the actual packed sax bank."""
import argparse
import json
from pathlib import Path
import struct
import numpy as np


def audit(bank):
    rows = []
    with bank.open('rb') as f:
        magic=f.read(8)
        if magic not in (b'TBBANK02',b'TBBANK03',b'TBBANK04'):
            raise ValueError('Expected a supported TopBrass bank')
        count = struct.unpack('<I', f.read(4))[0]
        if magic != b'TBBANK02':
            f.read(4)
        for _ in range(count):
            instrument, art, root, layer, sr, length, loop_start, loop_end, crossfade = struct.unpack('<9I', f.read(36))
            gain_count=0
            if magic != b'TBBANK02':
                _,_,_,_,gain_count=struct.unpack('<5I',f.read(20))
            channels=struct.unpack('<I',f.read(4))[0] if magic==b'TBBANK04' else 1
            x = np.frombuffer(f.read(length * 4), dtype='<f4')
            f.read(length*4*(channels-1)+gain_count*4)
            if instrument != 4 or art != 0:
                continue
            w = x[int(sr * .6):int(sr * 1.1)].astype(float)
            w -= w.mean()
            fft_length = 1 << (2 * len(w) - 1).bit_length()
            correlation = np.fft.irfft(np.abs(np.fft.rfft(w, fft_length)) ** 2, fft_length)[:len(w)]
            expected = 440 * 2 ** ((root - 69) / 12)
            lo, hi = int(sr / expected * .9), int(sr / expected * 1.1)
            lag = lo + int(np.argmax(correlation[lo:hi]))
            denominator = correlation[lag-1] - 2*correlation[lag] + correlation[lag+1]
            refined = lag + (.5 * (correlation[lag-1] - correlation[lag+1]) / denominator if denominator else 0)
            frequency = sr / refined
            rows.append(dict(root=root, layer=layer, frequency_hz=frequency,
                             cents_error=1200*np.log2(frequency/expected),
                             loop_start=loop_start, loop_end=loop_end, crossfade=crossfade))
    if len(rows) != 64 or any(not (r['loop_end'] > r['loop_start'] > 0 and r['crossfade'] == 0) for r in rows):
        raise ValueError('Expected all 64 native sustain loops')
    errors = [abs(r['cents_error']) for r in rows]
    if max(errors) > 10:
        raise ValueError('A sustain is more than 10 cents from its SFZ root in the measured window')
    return dict(samples=len(rows), absolute_cents_error_median=float(np.median(errors)),
                absolute_cents_error_max=float(max(errors)), recordings=rows)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('bank', type=Path)
    parser.add_argument('report', type=Path)
    args = parser.parse_args()
    result = audit(args.bank)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(f"PASS: {result['samples']} native loops; median pitch error {result['absolute_cents_error_median']:.3f} cents, max {result['absolute_cents_error_max']:.3f} cents")


if __name__ == '__main__':
    main()
