"""Audition metadata-only release experiments through the unchanged production engine."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import struct
import subprocess
import tempfile

import numpy as np
import soundfile as sf

from recording_audit import ROOT, digest
from build_bank import sustain_release


def records(bank):
    with bank.open('rb') as stream:
        if stream.read(8) != b'TBBANK04':
            raise ValueError('Expected Bank04')
        count, _ = struct.unpack('<2I', stream.read(8))
        for index in range(count):
            header = list(struct.unpack('<15I', stream.read(60)))
            size = (header[5] * header[14] + header[13]) * 4
            if header[0] in (0, 3) and header[1] == 0:
                yield index, header, stream.read(size)
            else:
                stream.seek(size, 1)


def measure(audio, rate, off):
    reference = np.sqrt(np.mean(audio[max(0, off-rate//10):off]**2))
    hop = int(rate * .02)
    post = audio[off:]
    blocks = post[:len(post)//hop*hop].reshape(-1, hop, 2)
    rms = np.sqrt(np.mean(blocks**2, axis=(1, 2)))
    db = 20 * np.log10(np.maximum(rms / max(reference, 1e-20), 1e-15))
    last = {}
    for threshold in (-6, -12, -24, -40):
        above = np.flatnonzero(db > threshold)
        last[str(threshold)] = float((above[-1] + 1) * hop / rate) if len(above) else 0
    audible = np.flatnonzero(np.max(np.abs(post), axis=1) > 1e-10)
    differences = np.max(np.abs(np.diff(audio, axis=0)), axis=1)
    body = differences[max(0, off-rate//10):off-1]
    transition = differences[max(0, off-1):off+int(rate*.06)]
    return {'reference_pre_note_off_rms': float(reference),
            'last_20ms_block_end_above_relative_db_seconds': last,
            'last_nonzero_after_note_off_seconds': float((audible[-1]+1)/rate) if len(audible) else 0,
            'transition_max_first_difference_to_body_max_ratio': float(np.max(transition)/max(np.max(body), 1e-20)),
            'first_40ms_rms_relative_db': db[:2].tolist()}


def source_tail_profile(audio, rate):
    hop = int(rate*.02)
    blocks = audio[:len(audio)//hop*hop].reshape(-1, hop, audio.shape[1])
    rms = np.sqrt(np.mean(blocks.astype(np.float64)**2, axis=(1, 2)))
    peak = float(np.max(rms))
    if peak < 1e-10:
        raise ValueError('Cannot locate release in a silent source')
    db = 20*np.log10(np.maximum(rms/peak, 1e-15))
    return {'block_seconds': .02, 'reference': 'peak full-block RMS across source channels',
            'last_block_start_above_relative_db': {str(threshold): float(np.flatnonzero(db > threshold)[-1]*hop/rate)
                                                  for threshold in (-6, -12, -24, -40)},
            'final_full_block_relative_db': float(db[-1]), 'discarded_partial_end_frames': len(audio) % hop}


def run(output, renderer, release_rule='global'):
    output.mkdir(parents=True, exist_ok=True)
    bank = ROOT / 'assets/TopBrass.bank'
    bank_sha = digest(bank)
    lock = json.loads((ROOT / 'assets/samples.lock.json').read_text())
    original_audit = json.loads((ROOT / 'assets/TopBrass.json').read_text())['originals']
    examples = set()
    for instrument, targets in [(0, (44, 60, 80)), (3, (29, 36, 58))]:
        candidates = [r for r in original_audit if r['instrument'] == instrument and r['articulation'] == 0]
        for target in targets:
            root = min(candidates, key=lambda r: abs(r['root']-target))['root']
            examples.update((instrument, r['root'], r['layer']) for r in candidates if r['root'] == root)
    results = []
    with tempfile.TemporaryDirectory(prefix='topbrass-release-') as temporary:
        temporary = Path(temporary)
        for index, original_header, pcm in records(bank):
            instrument, _, root, layer, rate, frames = original_header[:6]
            key = instrument, root, layer
            source_entry = next(e for e in lock if e['path'] == original_audit[index]['path'])
            source = ROOT / 'assets/cache' / source_entry['repo'].split('/')[-1] / source_entry['path']
            raw = source.read_bytes()
            if hashlib.sha1(f'blob {len(raw)}\0'.encode()+raw).hexdigest() != source_entry['blob']:
                raise ValueError('Original source Git blob differs')
            source_audio, source_rate = sf.read(source, dtype='float32', always_2d=True)
            packed = np.frombuffer(pcm, dtype='<f4').reshape(2, frames).T
            if source_rate != rate or not np.array_equal(source_audio, packed):
                raise ValueError('Bank/source PCM differs')
            if (source_entry['instrument'], source_entry['articulation'], source_entry['root'], source_entry['layer']) != tuple(original_header[:4]):
                raise ValueError('Bank/source mapping differs')
            profile = source_tail_profile(source_audio, rate)
            selected = sustain_release(source_audio, rate, original_header[7]) if release_rule == 'local' else None
            start = (selected['start'] if selected else None) if release_rule == 'local' else int((profile['last_block_start_above_relative_db']['-6'] - .1) * rate)
            eligible = start is not None and original_header[7] <= start < frames - int(rate*.04) - 2
            entry = {'bank_recording_index': index, 'instrument': instrument, 'root': root, 'layer': layer,
                     'source_path': str(source), 'source_sha256': digest(source),
                     'source_envelope_profile': profile,
                     'original_pcm_exact': True, 'candidate_release_start_frame': start,
                     'candidate_tail_seconds': (frames-start)/rate if start is not None else None,
                     'local_selection': selected,
                     'candidate_eligible': bool(eligible), 'captures': []}
            if not eligible:
                entry['rejection'] = 'No qualifying terminal decay after loop end with a complete crossfade; no replacement offset chosen'
            identifier = f'{"tenor" if instrument == 0 else "tuba"}-{root}-layer{layer}'
            example = key in examples and eligible
            if example:
                sf.write(output / f'{identifier}-original-ending.wav', source_audio[max(0, start-rate//4):] * .125,
                         rate, subtype='PCM_24')
            for hold in (.12, 1., 8.):
                captured = {}
                for variant, release_ms in [('fade65', 65), ('fade130', 130), ('recorded', 65)]:
                    if variant == 'recorded' and not eligible:
                        continue
                    header = original_header.copy()
                    header[9:11] = [0, 0]
                    if variant == 'recorded':
                        header[9:11] = [start, int(rate * .04)]
                    one = temporary / 'one.bank'
                    packed_bank = b'TBBANK04' + struct.pack('<2I', 12, 0) + struct.pack('<15I', *header) + pcm
                    for part, art in [(i, a) for i in range(5) for a in range(2)] + [(4, 2), (4, 3)]:
                        if (part, art) == (instrument, 0):
                            continue
                        sentinel = [part, art, 60, 1, rate, 8] + [0]*8 + [1]
                        packed_bank += struct.pack('<15I', *sentinel) + bytes(8*4)
                    one.write_bytes(packed_bank)
                    raw_output = temporary / 'capture.f32'
                    command = [str(renderer), str(one), str(hold), str(release_ms), str(raw_output)]
                    meta = json.loads(subprocess.check_output(command + ['128'], text=True))
                    audio = np.fromfile(raw_output, dtype='<f4').reshape(-1, 2)
                    if len(audio) != meta['frames'] or not np.isfinite(audio).all():
                        raise ValueError('Invalid engine capture')
                    off = meta['note_off_frame']
                    metrics = measure(audio, rate, off)
                    if variant == 'recorded':
                        count = frames-start-1
                        blend = np.float32(0)
                        step = np.float32(1 / int(rate*.04))
                        fade = 0
                        while blend < 1:
                            blend = np.float32(min(1, blend + step))
                            fade += 1
                        positions = np.arange(start+fade,start+count)
                        ending_gain = np.clip((frames-1-positions)/(rate*.003),0,1)
                        expected = source_audio[start+fade:start+count] * .125 * ending_gain[:,None]
                        observed = audio[off+fade:off+count]
                        error = float(np.max(np.abs(expected-observed)))
                        if error > 1e-7:
                            raise ValueError(f'Recorded tail oracle failed: {error}')
                        metrics['post_crossfade_reference_max_error'] = error
                        metrics['reference_includes_final_3ms_output_ramp'] = True
                    captured[variant] = audio
                    filename = f'{identifier}-hold{hold:g}-{variant}.wav'
                    if example:
                        sf.write(output / filename, audio, rate, subtype='PCM_24')
                    entry['captures'].append({'hold_seconds': hold, 'variant': variant,
                                               'metrics': metrics, 'audition': filename if example else None})
                    if example and hold == 1:
                        subprocess.run(command + ['31'], check=True, stdout=subprocess.DEVNULL)
                        partitioned = np.fromfile(raw_output, dtype='<f4').reshape(-1, 2)
                        if not np.array_equal(audio, partitioned):
                            raise ValueError('Block partition changed release output')
                if 'recorded' in captured:
                    n = int(min(hold, .25)*rate)
                    if not np.array_equal(captured['fade65'][:n], captured['recorded'][:n]):
                        raise ValueError('Candidate changed audio before note-off')
                if example and hold == 1:
                    plot(output / f'{identifier}.png', source_audio[max(0, start-rate//4):] * .125,
                         captured, rate, identifier)
            results.append(entry)
            print(f'{identifier}: recorded-tail eligible={eligible}', flush=True)
    report = {'bank_sha256': bank_sha, 'renderer_sha256': digest(renderer), 'release_rule': release_rule,
              'release_selector_source_sha256': digest(ROOT / 'scripts/build_bank.py'),
              'engine_source_sha256': digest(ROOT / 'Source/Engine.cpp'),
              'candidate_rule': ('First local160ms-median drop of8dB after loop end, with no95th-percentile rebound within6dB,18dB terminal decay after500ms and pre-drop reference above-36dB source peak; retain80ms preceding context;40ms stereo-shared crossfade' if release_rule == 'local' else 'last full20ms source RMS block above -6dB relative to peak block RMS, minus100ms;40ms linear stereo-shared crossfade; full remaining original ending'),
              'interpretation': 'Offline temporary-bank experiment, identified by renderer hash. Candidate rule does not identify acoustic note-off. Three native-pitch holds test different body/loop phases. No gain matching, EQ, tuning or tail normalization; all audio uses the engine gain0.125 and final3ms source-frame output ramp. Metrics are diagnostics, not realism acceptance or an installed-plugin check.',
              'measurements': 'Crossings are last20ms block END after MIDI note-off relative to the100ms preceding MIDI note-off. Nonzero threshold1e-10. First-difference ratio compares the largest sample difference in60ms after note-off to100ms before. No threshold implies perceptual acceptance.',
              'recordings': results}
    (output / 'results.json').write_text(json.dumps(report, indent=2)+'\n')
    write_page(output, results)
    if digest(bank) != bank_sha:
        raise ValueError('Production bank changed during experiment')
    return report


def plot(path, original, captures, rate, identifier):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from scipy import signal
    fig, axes = plt.subplots(4, 1, figsize=(11, 11), layout='constrained')
    hop = int(rate*.02)
    for label, audio in [('Original ending excerpt', original), *captures.items()]:
        n = len(audio)//hop
        rms = np.sqrt(np.mean(audio[:n*hop].reshape(n, hop, 2)**2, axis=(1, 2)))
        axes[0].plot(np.arange(n)*hop/rate-.25, 20*np.log10(np.maximum(rms, 1e-10)), label=label)
    axes[0].legend(fontsize=9)
    axes[0].set(title=identifier+' |20ms RMS', ylabel='dBFS', ylim=(-110, 0))
    rows = [('Original ending excerpt', original), ('Current130ms fade', captures['fade130'])]
    if 'recorded' in captures:
        rows.append(('Experimental recorded ending', captures['recorded']))
    reference = None
    for axis, (label, audio) in zip(axes[1:], rows):
        f, t, powers = signal.spectrogram(audio, rate, nperseg=4096, noverlap=3072, axis=0)
        powers = np.mean(powers, axis=1)
        if reference is None:
            reference = max(float(np.max(powers)), 1e-20)
        axis.pcolormesh(t-.25, f, 10*np.log10(np.maximum(powers/reference, 1e-10)),
                        vmin=-80, vmax=0, cmap='magma', shading='auto', rasterized=True)
        axis.set(title=label+' | same spectral power reference', yscale='log', ylim=(30, 16000), ylabel='Hz')
    for axis in axes:
        axis.axvline(0, color='gray', linestyle='--')
        axis.set(xlim=(-.25, max(1.25, len(original)/rate-.25)), xlabel='Seconds from MIDI note-off; source excerpt aligned at candidate start')
    fig.savefig(path, dpi=110)
    plt.close(fig)


def write_page(output, results):
    choices = [r for r in results if any(c['audition'] for c in r['captures'])]
    sections = []
    for row in choices:
        identifier = f'{"tenor" if row["instrument"] == 0 else "tuba"}-{row["root"]}-layer{row["layer"]}'
        sounds = [f'<p>Original ending excerpt</p><audio controls preload="none" src="{identifier}-original-ending.wav"></audio>']
        for capture in row['captures']:
            if capture['hold_seconds'] != 1 or capture['audition'] is None:
                continue
            label = {'fade65': 'Current65ms fade', 'fade130': 'Current130ms fade', 'recorded': 'Experimental recorded ending'}[capture['variant']]
            sounds.append(f'<p>{label}</p><audio controls preload="none" src="{capture["audition"]}"></audio>')
        sections.append(f'<section><h2>{html.escape(identifier)}</h2>'+''.join(sounds)+
                        f'<p><a href="{identifier}.png">Envelope and spectrogram comparison</a></p></section>')
    page = '''<!doctype html><meta charset="utf-8"><title>TopBrass release study</title>
<style>body{font:17px/1.6 system-ui;background:#151719;color:#eee;max-width:850px;margin:40px auto;padding:20px}audio{width:100%}a{color:#dfbb75}section{border-top:1px solid #555;padding:20px 0}</style>
<h1>What note-off discards</h1>
<p>This offline experiment uses the production engine, with one original tenor/tuba recording isolated at its native pitch and rate. The original ending excerpt and all engine renders use the same constant gain0.125. No EQ, normalization, compression, tuning or time stretching was added.</p>
<p>Each engine clip contains250ms before a one-second MIDI note-off, then the complete release. The experimental version jumps into the same recording near its ending with a40ms crossfade and tapers its final3ms of source frames. The offset comes from a coarse envelope rule, not an annotated breath or acoustic note-off. These are temporary-bank experiments. <a href="../installed-releases-v0.11.0-final/">Installed-plugin captures</a> are separate.</p>
<p>The complete report also measures120ms and8s holds for every source recording. A longer recorded ending may blur tight section cutoffs or expose a noisy source boundary. Listen before treating it as an improvement.</p>
<p><a href="results.json">Complete measurements and source identities</a></p>'''
    (output / 'index.html').write_text(page+''.join(sections))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, default=ROOT / 'artifacts/recording-audit/release-tail-study')
    parser.add_argument('--renderer', type=Path, default=ROOT / 'build/TopBrassSustainReleaseAudit')
    parser.add_argument('--release-rule', choices=('global', 'local'), default='global')
    args = parser.parse_args()
    report = run(args.output.resolve(), args.renderer.resolve(), args.release_rule)
    print(json.dumps({'recordings': len(report['recordings']),
                      'eligible': sum(r['candidate_eligible'] for r in report['recordings'])}))
