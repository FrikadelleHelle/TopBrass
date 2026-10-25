"""Pack original CC0 channels/levels with the authored Bell & Bone mappings."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import numpy as np
import soundfile as sf
from prepare_samples import fetch, embedded_loop, BEAR_REPO

ROOT = Path(__file__).resolve().parents[1]
ALPHABET = '.ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+'
ARTICULATIONS = {'sustain': 0, 'staccatissimo': 1, 'staccato-fall': 4, 'double-tongue': 5}


def gain_table(text, original_end):
    if not text:
        return np.empty(0, dtype='<f4')
    size, payload = text.split('.', 1)
    size = int(size)
    packed = sum(ALPHABET.index(character) << (6*index) for index, character in enumerate(payload))
    raw = (packed & ((1 << (size*8))-1)).to_bytes(size, 'little')
    points = np.frombuffer(raw, dtype='<f4').reshape(-1, 3)
    if (not np.isfinite(points).all() or not np.all(points[:, 2] == .5)
            or points[0, 0] != 0 or points[-1, 0] != 1 or np.any(np.diff(points[:, 0]) < 0)):
        raise ValueError('Unsupported recording gain curve')
    count = original_end//32 + 1
    values = np.interp(np.linspace(0, 1, count-1), points[:, 0], 2*points[:, 1])
    return np.append(values, values[-1]).astype('<f4')


def joint_loop(audio, rate, minimum_natural_seconds=4.0):
    fade = int(rate * .04)
    late = minimum_natural_seconds > 0
    bands = ((.45, .65), (.8, 2.4), (2.4, 3.4)) if late else ((.45, .65),)
    for low, high in bands:
        lo, hi = int(rate * low), int(rate * high)
        section = audio[lo:hi+fade].astype(np.float64)
        squared = np.vstack((np.zeros((1, audio.shape[1])), np.cumsum(section*section, axis=0)))
        incoming = squared[fade:] - squared[:-fade]
        fft_size = 1 << (len(section)+fade-2).bit_length()
        spectrum = np.fft.rfft(section, n=fft_size, axis=0)
        first = int(rate * (minimum_natural_seconds + .04)) if late else max(hi+fade+int(rate*.2), int(rate*.9))
        last = len(audio)-int(rate*.3) if late else min(len(audio)-int(rate*.2), int(rate*2.8))
        best = None
        for end in range(first, last+1, int(rate*.05)):
            outgoing = audio[end-fade:end].astype(np.float64)
            power = np.sum(outgoing*outgoing, axis=0)
            ratio = np.minimum(power, incoming) / np.maximum(np.maximum(power, incoming), 1e-20)
            correlation = np.fft.irfft(spectrum*np.fft.rfft(outgoing[::-1], n=fft_size, axis=0), n=fft_size, axis=0)[fade-1:len(section)]
            scores = correlation / np.maximum(np.sqrt(power*incoming), 1e-20)
            joint = np.min(scores, axis=1)
            joint[np.any(ratio < (.81 if late else .49), axis=1)] = -2
            index = int(np.argmax(joint))
            candidate = (float(joint[index]), lo+index, end, scores[index].tolist(), np.sqrt(ratio[index]).tolist())
            if best is None or candidate[0] > best[0]:
                best = candidate
        if best is not None and best[0] >= .85:
            return best[1], best[2], fade, best[3], best[4]
    raise ValueError('No jointly matched sustain loop with the required natural duration')


def sustain_release(audio, rate, loop_end):
    hop = int(rate*.02)
    blocks = len(audio)//hop
    if blocks < 24:
        return None
    rms = np.sqrt(np.mean(audio[:blocks*hop].astype(np.float64).reshape(blocks, hop, -1)**2, axis=(1, 2)))
    peak = float(np.max(rms))
    if peak < 1e-10:
        return None
    db = 20*np.log10(np.maximum(rms/peak, 1e-15))
    for index in range(max(8, (loop_end+int(rate*.08)+hop-1)//hop), blocks-8):
        before = float(np.median(db[index-8:index]))
        after = float(np.median(db[index:index+8]))
        if before < -36 or before-after < 8:
            continue
        if float(np.quantile(db[index+8:], .95)) > before-6:
            continue
        late = db[min(index+25, blocks-1):]
        if float(np.quantile(late, .95)) > before-18:
            continue
        start = index*hop-int(rate*.08)
        fade = int(rate*.04)
        if start < loop_end or len(audio)-start < fade+2:
            continue
        return dict(start=start, crossfade=fade, local_drop_db=before-after,
                    entry_reference_relative_db=before)
    return None


def original_recordings():
    lock=json.loads((ROOT/'assets/samples.lock.json').read_text())
    entries=[e for e in lock if e['instrument'] in (0,3,4)]
    retained=[];audit=[]
    for entry in entries:
        source=fetch(entry);audio,rate=sf.read(str(source),dtype='float32',always_2d=True)
        if audio.shape[1] not in (1,2) or not np.isfinite(audio).all() or np.max(np.abs(audio))>1:
            raise ValueError('Unsupported original PCM: '+str(source))
        start=end=fade=0;correlation=[];ratios=[]
        native=embedded_loop(source)
        if native:start,end=native
        elif entry['repo']!=BEAR_REPO and entry['articulation']==0:
            try:start,end,fade,correlation,ratios=joint_loop(audio,rate,minimum_natural_seconds=4 if entry['instrument']==0 else 0)
            except ValueError as error:raise ValueError(str(error)+': '+entry['path']) from error
        release = sustain_release(audio, rate, end) if entry['instrument'] in (0,3) and entry['articulation']==0 else None
        header=(entry['instrument'],entry['articulation'],entry['root'],entry['layer'],rate,len(audio),start,end,fade,
                release['start'] if release else 0,release['crossfade'] if release else 0,0,0,0)
        retained.append((header,audio))
        audit.append(dict(path=entry['path'],instrument=entry['instrument'],articulation=entry['articulation'],
                          root=entry['root'],layer=entry['layer'],rate=rate,frames=len(audio),channels=audio.shape[1],gain=1,
                          loop_start=start,loop_end=end,loop_crossfade=fade,loop_source='author_smpl' if native else 'joint_match' if end else 'none',
                          channel_loop_correlations=correlation,channel_loop_rms_ratios=ratios,
                          release_start=release['start'] if release else 0,release_crossfade=release['crossfade'] if release else 0,
                          release_selection=release))
    return retained,audit


def build_base(destination):
    catalog=json.loads((ROOT/'assets/cache/bell-and-bone/playback-catalog.json').read_text())
    audit=json.loads((ROOT/'assets/cache/bell-and-bone/recording-audit.json').read_text())
    source_properties={record['file']:record['zones'][0] for record in audit['recordings']}
    retained,original_audit=original_recordings()
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix('.partial')
    envelope_count = 0
    with temporary.open('wb') as bank:
        bank.write(b'TBBANK04'+struct.pack('<2I', len(retained)+len(catalog['recordings']), len(catalog['zones'])))
        for header, audio in retained:
            bank.write(struct.pack('<15I', *header, audio.shape[1])); bank.write(audio.T.astype('<f4').tobytes())
        for index, record in enumerate(catalog['recordings']):
            zones = [z for z in catalog['zones'] if z['recording'] == index]
            first = zones[0]
            waveform = ROOT/record['file']
            audio, rate = sf.read(str(waveform), dtype='float32')
            if (hashlib.sha256(audio.astype('<f4').tobytes()).hexdigest() != record['audio_sha256']
                    or rate != record['rate'] or len(audio) != record['frames'] or audio.ndim != 1):
                raise ValueError('Source recording changed: '+str(waveform))
            sustain = first['articulation'] == 'sustain'
            # Original coordinates are retained for the source's gain lookup table.
            original_start = int(source_properties[record['file']].get('SampleStart', 0))
            envelope = gain_table(record['gain_table'], len(audio)+original_start)
            envelope_count += int(len(envelope)>0)
            if sustain:
                layer=first['dynamic_layer']
            elif first['articulation']=='staccato-fall':
                layer=1 if first['low_velocity']==0 else 2
            else:
                layer=1+first['low_velocity']//42
            release = record['release_start']
            fade = min(8192, len(audio)-release-2) if release else 0
            header = (1 if first['instrument']=='trumpet' else 2, ARTICULATIONS[first['articulation']], first['root'], layer,
                      rate, len(audio), record['loop_start'], record['loop_end'], record['loop_crossfade'],
                      release, fade, int(sustain), original_start, len(envelope))
            bank.write(struct.pack('<15I', *header, 1)); bank.write(audio.astype('<f4').tobytes()); bank.write(envelope.tobytes())
        for zone in catalog['zones']:
            values = (len(retained)+zone['recording'], 1 if zone['instrument']=='trumpet' else 2,
                      ARTICULATIONS[zone['articulation']], zone['root'], zone['low_key'], zone['high_key'],
                      zone['low_velocity'], zone['high_velocity'], zone['dynamic_layer'] or 0, zone['round_robin'])
            gain=zone['gain']*10**(zone.get('articulation_gain_db',0)/20)
            bank.write(struct.pack('<10I2f', *values, zone['pitch_cents'], gain))
    temporary.replace(destination)
    report = dict(format='TBBANK04', samples=len(retained)+len(catalog['recordings']),
                  zones=len(catalog['zones']), recording_gain_envelopes=envelope_count,
                  originals=original_audit,
                  bank_sha256=hashlib.file_digest(destination.open('rb'), 'sha256').hexdigest(), bytes=destination.stat().st_size,
                  catalog_sha256=hashlib.sha256((ROOT/'assets/cache/bell-and-bone/playback-catalog.json').read_bytes()).hexdigest())
    destination.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'Built original bank: {report["samples"]} recordings, {report["zones"]} zones')


def build(destination):
    from build_mtg_tenor import extend as add_tenor
    from build_war_tuba import extend as add_tuba
    destination=Path(destination)
    destination.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='topbrass-base-',dir=destination.parent) as directory:
        base=Path(directory)/'base.bank'
        build_base(base)
        tenor=Path(directory)/'tenor.bank'
        add_tenor(base,tenor)
        add_tuba(tenor,destination)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    build(args.destination)
