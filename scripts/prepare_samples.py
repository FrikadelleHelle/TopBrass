#!/usr/bin/env python3
"""Prepare pinned source recordings and build the mapped resident bank."""
import argparse
import concurrent.futures
import hashlib
import json
import re
import struct
import urllib.parse
import urllib.request
from pathlib import Path
import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]
SOURCES = [
    ('sgossner/VCSL', 'c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e', 0,
     {'Aerophones/Reed Aerophones/Tenor Saxophone/Non-Vibrato': 0,
      'Aerophones/Reed Aerophones/Tenor Saxophone/Staccato': 1}),
    ('sgossner/VSCO-2-CE', '440300901dfe9275fd84e0b7763af1f8443ae62e', 1,
     {'Brass/Trumpet/sus': 0, 'Brass/Trumpet/stac': 1}),
    ('sgossner/VSCO-2-CE', '440300901dfe9275fd84e0b7763af1f8443ae62e', 2,
     {'Brass/Tenor Trombone/sus': 0, 'Brass/Tenor Trombone/stac': 1}),
    ('sgossner/VSCO-2-CE', '440300901dfe9275fd84e0b7763af1f8443ae62e', 3,
     {'Brass/Tuba/sus': 0, 'Brass/Tuba/stac': 1}),
]
BEAR_REPO = 'sfzinstruments/karoryfer.bear-sax'
BEAR_COMMIT = '7abb3c652525a15dfac80e1b5dfbba9964ee568f'
NOTES = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def get(url):
    for attempt in range(3):
        try:
            return urllib.request.urlopen(url, timeout=90).read()
        except Exception:
            if attempt == 2:
                raise


def bear_entries():
    tree = json.loads(get(f'https://api.github.com/repos/{BEAR_REPO}/git/trees/{BEAR_COMMIT}?recursive=1'))['tree']
    blobs = {item['path']: item for item in tree}
    entries = {}
    maps = {'dynfade_map.sfz': 0, 'staccato_map.sfz': 1,
            'sub_map.sfz': 2, 'growl_map.sfz': 3}
    for name, articulation in maps.items():
        map_path = 'Programs/poly/' + name
        mapping = dict(repo=BEAR_REPO, commit=BEAR_COMMIT, path=map_path, blob=blobs[map_path]['sha'])
        text = fetch(mapping).read_text()
        text = re.sub(r'//[^\n]*', '', text)
        group = {}
        for tag, body in re.findall(r'<(group|region)>([^<]*)', text):
            values = dict(re.findall(r'([A-Za-z0-9_]+)=([^\s]+)', body))
            if tag == 'group':
                group = values
                continue
            region = group | values
            filename = Path(region['sample']).name
            if articulation == 3 and not filename.startswith('growl'):
                continue
            path = 'Samples/' + filename
            root = int(region.get('pitch_keycenter', '60'))
            layer_match = re.search(r'_vl(\d+)_', filename)
            layer = int(layer_match[1]) if layer_match else (2 if filename.endswith('_f.wav') else 1)
            key = (path, articulation, root)
            entries[key] = dict(repo=BEAR_REPO, commit=BEAR_COMMIT, path=path,
                                blob=blobs[path]['sha'], bytes=blobs[path]['size'], instrument=4,
                                articulation=articulation, root=root, layer=layer,
                                mapping=map_path, mapping_blob=mapping['blob'])
    return list(entries.values())


def manifest():
    entries = []
    trees = {}
    for repo, commit, instrument, folders in SOURCES:
        if repo not in trees:
            trees[repo] = json.loads(get(f'https://api.github.com/repos/{repo}/git/trees/{commit}?recursive=1'))['tree']
        for item in trees[repo]:
            parent = str(Path(item['path']).parent).rstrip()
            if parent not in folders or not item['path'].endswith('.wav'):
                continue
            name = Path(item['path']).name
            match = re.search(r'_([A-G])(#?)(-?\d)_v(?:l)?(\d+)', name)
            if not match:
                raise ValueError(f'Unrecognized sample pitch/layer: {name}')
            letter, sharp, octave, velocity = match.groups()
            # Both libraries use C3 for MIDI 60. Verify acoustic pitch in the audit.
            root = 24 + int(octave) * 12 + NOTES[letter] + bool(sharp)
            entries.append(dict(repo=repo, commit=commit, path=item['path'],
                                blob=item['sha'], bytes=item['size'], instrument=instrument,
                                articulation=folders[parent], root=root, layer=int(velocity)))
    entries.extend(bear_entries())
    return sorted(entries, key=lambda e: (e['instrument'], e['articulation'], e['root'], e['layer'], e['path']))


def fetch(entry):
    dest = ROOT / 'assets/cache' / entry['repo'].split('/')[-1] / entry['path']
    if not dest.exists():
        data = get(f"https://raw.githubusercontent.com/{entry['repo']}/{entry['commit']}/" + urllib.parse.quote(entry['path']))
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
    data = dest.read_bytes()
    digest = hashlib.sha1(f'blob {len(data)}\0'.encode() + data).hexdigest()
    if digest != entry['blob']:
        raise ValueError(f'Checksum mismatch: {dest}')
    return dest


def choose_loop(mono, sr):
    fade = int(sr * 0.04)
    lo, hi = int(sr * 0.45), int(sr * 0.65)
    section = mono[lo:hi + fade].astype(np.float64)
    sq = np.r_[0.0, np.cumsum(section * section)]
    incoming_power = sq[fade:] - sq[:-fade]
    first_end = max(hi + fade + int(sr * 0.2), int(sr * 0.9))
    last_end = min(len(mono) - int(sr * 0.2), int(sr * 2.8))
    best = None
    for end in range(first_end, last_end + 1, int(sr * 0.05)):
        outgoing = mono[end - fade:end].astype(np.float64)
        power = float(np.dot(outgoing, outgoing))
        ratio = np.minimum(power, incoming_power) / np.maximum(np.maximum(power, incoming_power), 1e-20)
        scores = np.correlate(section, outgoing, 'valid') / np.maximum(np.sqrt(power * incoming_power), 1e-20)
        scores[ratio < 0.49] = -2
        index = int(scores.argmax())
        candidate = (float(scores[index]), lo + index, end, float(np.sqrt(ratio[index])))
        if best is None or candidate[0] > best[0]:
            best = candidate
    if best is None or best[0] < 0.85:
        raise ValueError('No phase/amplitude-matched sustain loop found')
    return best[1], best[2], best[0], best[3]


def embedded_loop(filename):
    data = filename.read_bytes()
    position = 12
    while position + 8 <= len(data):
        tag, length = struct.unpack_from('<4sI', data, position)
        if tag == b'smpl' and length >= 60:
            loops = struct.unpack_from('<I', data, position + 8 + 28)[0]
            if loops:
                _, kind, start, end, fraction, play_count = struct.unpack_from('<6I', data, position + 8 + 36)
                if kind != 0 or fraction != 0 or play_count != 0:
                    raise ValueError(f'Unsupported embedded loop: {filename}')
                return start, end + 1
        position += 8 + length + length % 2
    return None


def build_cc0(entries, files, output):
    output.parent.mkdir(parents=True, exist_ok=True)
    audit = []
    with output.with_suffix('.tmp').open('wb') as bank:
        bank.write(b'TBBANK02' + struct.pack('<I', len(entries)))
        for e, filename in zip(entries, files):
            channels, sr = sf.read(filename, dtype='float32', always_2d=True)
            mono = channels.mean(axis=1)
            peak = float(np.max(np.abs(mono)))
            # Remove pre-roll, retain 2ms before the first audible transient.
            active = np.flatnonzero(np.abs(mono) > max(0.0001, peak * 0.008))
            if len(active) == 0:
                raise ValueError(f'Silent recording: {filename}')
            start = max(0, int(active[0]) - int(sr * 0.002))
            end = min(len(mono), int(active[-1]) + int(sr * 0.05))
            author_loop = embedded_loop(filename) if e['repo'] == BEAR_REPO else None
            if author_loop:
                if not (start < author_loop[0] < author_loop[1] <= len(mono)):
                    raise ValueError(f'Invalid author loop: {filename}')
                end = max(end, author_loop[1])
            mono = np.ascontiguousarray(mono[start:end], dtype='<f4')
            fade = min(int(sr * 0.002), len(mono) // 4)
            mono[:fade] *= np.linspace(0, 1, fade)
            if not author_loop:
                mono[-fade:] *= np.linspace(1, 0, fade)
            normalization = 0.65 / max(float(np.max(np.abs(mono))), 1e-6)
            mono *= normalization
            loop_start = loop_end = 0
            correlation = rms_ratio = None
            loop_crossfade = 0
            if author_loop:
                loop_start, loop_end = (point - start for point in author_loop)
            elif e['repo'] != BEAR_REPO and e['articulation'] == 0 and len(mono) > sr * 1.2:
                loop_start, loop_end, correlation, rms_ratio = choose_loop(mono, sr)
                loop_crossfade = int(sr * 0.04)
            bank.write(struct.pack('<9I', e['instrument'], e['articulation'], e['root'],
                                   e['layer'], sr, len(mono), loop_start, loop_end, loop_crossfade))
            bank.write(mono.tobytes())
            audit.append(dict(path=e['path'], root=e['root'], layer=e['layer'],
                              source_sample_rate=sr, frames=len(mono), pre_roll_removed=start,
                              source_peak=peak, normalization_gain=normalization,
                              loop_start=loop_start, loop_end=loop_end, loop_crossfade=loop_crossfade,
                              loop_source='author_smpl' if author_loop else 'matched' if loop_end else 'none',
                              loop_correlation=correlation, loop_rms_ratio=rms_ratio))
    output.with_suffix('.tmp').replace(output)
    report = dict(format='TBBANK02', samples=len(entries), bytes=output.stat().st_size,
                  sha256=hashlib.sha256(output.read_bytes()).hexdigest(), recordings=audit)
    output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f"Built {output}: {len(entries)} recordings, {output.stat().st_size / 1e6:.1f} MB", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--refresh-manifest', action='store_true')
    parser.add_argument('--bell-archive', type=Path, default=Path.home()/'Downloads/Bell and Bone_2_1_0_Samples.hr1')
    args = parser.parse_args()
    lock = ROOT / 'assets/samples.lock.json'
    if args.refresh_manifest or not lock.exists():
        entries = manifest()
        lock.write_text(json.dumps(entries, indent=2) + '\n')
    else:
        entries = json.loads(lock.read_text())
    print(f'Verifying/fetching {len(entries)} source recordings', flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        files = list(pool.map(fetch, entries))
    base = ROOT/'assets/cache/cc0-reference.bank'
    if not base.exists():
        build_cc0(entries, files, base)
    from extract_bell_archive import extract
    from audit_bell_samples import audit
    from build_bank import build
    bell = ROOT/'assets/cache/bell-and-bone'
    if not all((bell/(name+'.ch1.flac')).exists() for name in ('trumpet_sustain','trumpet_articulations','trombone_sustain','trombone_articulations')):
        extract(args.bell_archive, bell)
    audit()
    output = ROOT/'assets/TopBrass.bank'
    build(output)
    (ROOT/'assets/bank-audit.json').write_text(output.with_suffix('.json').read_text())
    from build_keys import build as build_keys
    build_keys(ROOT/'assets/TopBrass.keys')
    from build_airflow import build as build_airflow
    build_airflow(ROOT/'assets/TopBrass.air')

if __name__ == '__main__':
    main()
