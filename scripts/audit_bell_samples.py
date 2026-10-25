"""Validate published Bell & Bone maps against the supplied archive and extract core recordings."""
import hashlib
import json
import math
from pathlib import Path
import xml.etree.ElementTree as ET
import urllib.request
import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT/'assets/cache/bell-and-bone'
COMMIT = '36139c0bb0983416c6bb3f856a6f9241d81c0c0b'
SOURCE_HASHES = {
    'trumpet_sustain.xml': 'fac80cc4e22e4dd531dff3e7f22853118928579521c709bb55fb44e9f909d5f5',
    'trombone_sustain.xml': '52f0a468edcfabb37a1873fdcc2b9c1b4149c7976aeed7d875e2c94c065705a3',
    'trumpet_articulations.xml': '413fbcc422cb219ed39e956e77a50365089da898761a04bfaccbd5123d6201b0',
    'trombone_articulations.xml': '69bf690729ae62d78fffc94005bc9e922135d5b3ce903ba7e6ca6b3220b0e077',
    'License.txt': '9f95dfa251e20d1367403a41f71807ad392faf8a58f5e6a83ee3decbeae1f2ab',
}


def require(condition, context):
    if not condition:
        raise ValueError(context)


def fetch_maps():
    directory = CACHE/'public-source'; directory.mkdir(parents=True, exist_ok=True)
    for name, expected in SOURCE_HASHES.items():
        target = directory/name
        if not target.exists():
            relative = name if name == 'License.txt' else 'SampleMaps/'+name
            url = 'https://codeberg.org/LibreWave/BellAndBone/raw/commit/'+COMMIT+'/'+relative
            with urllib.request.urlopen(url, timeout=30) as response:
                data = response.read()
            if hashlib.sha256(data).hexdigest() != expected:
                raise ValueError('Unexpected source hash: '+name)
            target.write_bytes(data)
        if hashlib.sha256(target.read_bytes()).hexdigest() != expected:
            raise ValueError('Source cache hash mismatch: '+name)


def audit():
    fetch_maps()
    extraction = json.loads((CACHE/'extraction.json').read_text())
    for entry in extraction['entries']:
        with (CACHE/entry['file']).open('rb') as source:
            checksum = hashlib.file_digest(source, 'sha256').hexdigest()
        require(checksum == entry['sha256'], 'Archive payload checksum mismatch: '+entry['file'])
    records, maps = [], []
    output = CACHE/'recordings'; output.mkdir(exist_ok=True)
    for path in sorted((CACHE/'public-source').glob('*.xml')):
        tree = ET.parse(path).getroot()
        seen = {}
        with sf.SoundFile(str(CACHE/(path.stem+'.ch1.flac'))) as source:
            spans = set()
            for row in tree:
                props = dict(row.attrib)
                offset, length = int(props['MonolithOffset']), int(props['MonolithLength'])
                start, end = int(props.get('SampleStart', 0)), int(props['SampleEnd'])
                require(0 <= offset < offset+length <= source.frames, props)
                require(0 <= start < end <= length, props)
                if int(props.get('LoopEnabled', 0)):
                    a, b = int(props['LoopStart']), int(props['LoopEnd'])
                    require(start <= a < b < end, props)
                    require(0 <= int(props.get('LoopXFade', 0)) <= min((b-a)//2, a-start), props)
                    require(b <= int(props['ReleaseStart']) < end, props)
                spans.add((offset, length))
                name = (props['FileName'], offset, length)
                if name in seen:
                    require(all(seen[name][0][key] == props[key] for key in ('MonolithOffset','MonolithLength','Root','SampleStart','SampleEnd')), props)
                    seen[name].append(props)
                else:
                    seen[name] = [props]
            cursor = 0
            for offset, length in sorted(spans):
                require(offset == cursor, (path, offset, cursor))
                cursor += length
            require(cursor == source.frames, (path, cursor, source.frames))
            for (filename, offset, length), zones in seen.items():
                props = zones[0]
                kind = filename.split('/')[-2]
                if kind not in ('sustain', 'staccatissimo', 'staccato', 'doubleTongue'):
                    continue
                source.seek(int(props['MonolithOffset'])+int(props.get('SampleStart',0)))
                count = int(props['SampleEnd'])-int(props.get('SampleStart',0))
                audio = source.read(count, dtype='float32', always_2d=True)
                require(audio.shape == (count, source.channels) and np.isfinite(audio).all(), filename)
                require(np.max(np.abs(audio)) > 0, filename)
                instrument = path.stem.split('_')[0]
                target = output/(instrument+'-'+Path(filename).stem+'-'+str(offset)+'.wav')
                sf.write(str(target), audio, source.samplerate, subtype='PCM_24')
                decoded, decoded_rate = sf.read(str(target), dtype='float32', always_2d=True)
                require(decoded_rate == source.samplerate and np.array_equal(decoded, audio), 'WAV roundtrip: '+filename)
                records.append(dict(file=str(target.relative_to(ROOT)), source_file=filename,
                                    map=path.name, frames=count, rate=source.samplerate,
                                    peak=float(np.max(np.abs(audio))), sha256=hashlib.sha256(target.read_bytes()).hexdigest(),
                                    audio_sha256=hashlib.sha256(audio.astype('<f4').tobytes()).hexdigest(),
                                    zones=zones))
            maps.append(dict(file=path.name, sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                             rows=len(tree), monolith_entries=len(seen), monolith_frames=source.frames,
                             exact_contiguous_coverage=True))
    records.sort(key=lambda record: '/doubleTongue/' in record['source_file'])
    unique = {}
    for record in records:
        key = (record['source_file'], record['audio_sha256'])
        if key in unique:
            unique[key]['zones'].extend(record['zones'])
        else:
            unique[key] = record
    report = dict(public_repository='https://codeberg.org/LibreWave/BellAndBone', public_commit=COMMIT,
                  map_project_version='2.0.1', archive_version='2.1.0',
                  correspondence='All four monolith lengths and every contiguous sample span match. No claim of embedded-map equality.',
                  processing='Authored start/end crop only. No per-recording normalization, gain correction, pitch correction or new fades.',
                  maps=maps, monolith_entries=len(records), recordings=list(unique.values()))
    (CACHE/'recording-audit.json').write_text(json.dumps(report, indent=2)+'\n')
    catalog = dict(recordings=[], zones=[])
    for index, record in enumerate(unique.values()):
        first = record['zones'][0]
        start = int(first.get('SampleStart', 0))
        sustain = '/sustain/' in record['source_file']
        fall = '/staccato/' in record['source_file']
        double = '/doubleTongue/' in record['source_file']
        catalog['recordings'].append(dict(
            file=record['file'], rate=record['rate'], frames=record['frames'], audio_sha256=record['audio_sha256'],
            loop_start=int(first['LoopStart'])-start if sustain else 0,
            loop_end=int(first['LoopEnd'])-start if sustain else 0,
            loop_crossfade=int(first.get('LoopXFade', 0)) if sustain else 0,
            loop_convention='pre-loop-start' if sustain else 'none',
            release_start=int(first['ReleaseStart'])-start if sustain else 0,
            gain_table=first.get('GainTable')))
        for zone in record['zones']:
            gain = float(zone.get('NormalizedPeak', 1)) if int(zone.get('Normalized', 0)) else 1.0
            gain *= math.pow(10, float(zone.get('Volume', 0))/20)
            catalog['zones'].append(dict(
                recording=index, instrument=record['map'].split('_')[0],
                articulation='sustain' if sustain else 'staccato-fall' if fall else 'double-tongue' if double else 'staccatissimo', root=int(zone['Root']),
                low_key=int(zone['LoKey']), high_key=int(zone['HiKey']),
                low_velocity=int(zone['LoVel']), high_velocity=int(zone['HiVel']),
                dynamic_layer=int(zone['RRGroup']) if sustain else None,
                round_robin=0 if sustain else int(zone['RRGroup'])-(1 if fall else 9 if double else 5),
                pitch_cents=float(zone.get('Pitch', 0)), gain=gain,
                articulation_gain_db=-3 if fall else 0))
    (CACHE/'playback-catalog.json').write_text(json.dumps(catalog, indent=2)+'\n')
    print(json.dumps(dict(maps=maps, extracted_entries=len(records), distinct_recordings=len(unique),
                         distinct_decoded_frames=sum(r['frames'] for r in unique.values())), indent=2))
    audition = ROOT/'artifacts/bell-and-bone'; audition.mkdir(exist_ok=True)
    credits = (CACHE/'public-source/License.txt').read_text()
    (audition/'LICENSE.txt').write_text(credits)
    for instrument, root in (('trumpet',60), ('trombone',48)):
        for kind in ('sustain','staccatissimo','staccato','doubleTongue'):
            candidates = [r for r in records if Path(r['file']).name.startswith(instrument+'-')
                          and int(r['zones'][0]['Root']) == root and '/'+kind+'/' in r['source_file']]
            candidates.sort(key=lambda r:(int(r['zones'][0]['RRGroup']),int(r['zones'][0]['LoVel'])))
            chosen = candidates[-1] if kind == 'sustain' else candidates[0]
            audio, rate = sf.read(str(ROOT/chosen['file']), dtype='float32')
            sf.write(str(audition/(instrument+'-'+kind+'-native.wav')), audio, rate, subtype='PCM_24')
            (audition/(instrument+'-'+kind+'-native.json')).write_text(json.dumps(chosen,indent=2)+'\n')
    (audition/'README.txt').write_text('Bell & Bone by Libre Wave. Source https://librewave.com/product/bell-and-bone/\n'
        'Downloaded by the user; archive SHA-256 in assets/cache/bell-and-bone/extraction.json.\n'
        'Native-pitch recording extracts using published authored start/end. Unprocessed levels; no plugin effects.\n'
        'These files are source auditions, not proof that the plugin is realistic. License retained in LICENSE.txt.\n')


if __name__ == '__main__':
    audit()
