"""Measure how early generated loops replace the original sustained performance."""
import argparse
import html
import json
from pathlib import Path

import numpy as np
import soundfile as sf

from recording_audit import ROOT, bank_headers, compare_arrays, digest, stereo


def audit(directory):
    config = json.loads((directory / 'config.json').read_text())
    bank = Path(config['plugin_path']) / 'Contents/Resources/TopBrass.bank'
    if digest(bank) != config['bank_sha256']:
        raise ValueError('Installed bank changed since the plugin capture')
    headers = bank_headers(bank)
    corpus = []
    for index, h in enumerate(headers):
        if h[0] not in (0, 3) or h[1] != 0:
            continue
        corpus.append(dict(recording=index, instrument='Tenor' if h[0] == 0 else 'Tuba',
                           root=h[2], source_seconds=h[5] / h[4],
                           first_change_seconds=(h[7] - h[8]) / h[4],
                           repeated_seconds=(h[7] - h[6] - h[8]) / h[4]))
    summary = {}
    for name in ('Tenor', 'Tuba'):
        rows = [r for r in corpus if r['instrument'] == name]
        summary[name] = dict(recordings=len(rows),
                            median_source_seconds=float(np.median([r['source_seconds'] for r in rows])),
                            median_first_change_seconds=float(np.median([r['first_change_seconds'] for r in rows])),
                            median_repeated_seconds=float(np.median([r['repeated_seconds'] for r in rows])),
                            changed_before_two_seconds=sum(r['first_change_seconds'] < 2 for r in rows))
    output = directory / 'sustain-loops'
    output.mkdir(exist_ok=True)
    cases = []
    for case in config['cases']:
        if case['instrument'] not in (0, 3) or case['articulation'] != 0:
            continue
        folder = Path(case['directory'])
        x, rate = sf.read(folder / 'original-native.wav', always_2d=True)
        y, sr = sf.read(folder / 'neutral-plugin-raw.wav', always_2d=True)
        if sr != rate:
            raise ValueError('Source and plugin sample rates differ')
        x = stereo(x)
        end = min(len(x), len(y))
        x, y = x[:end], y[:end]
        pre = compare_arrays(x[:case['analysis_end']], y[:case['analysis_end']], rate)
        y *= pre['gain_correction']
        windows = []
        for start in (0, 1, 2, 4, 6):
            a, b = int(start * rate), min(int((start + .5) * rate), end)
            if b - a < rate * .25:
                continue
            reference, played = x[a:b], y[a:b]
            px, py = np.mean(reference**2), np.mean(played**2)
            metrics = compare_arrays(reference, played, rate)
            windows.append(dict(start_seconds=start, end_seconds=b / rate,
                                level_difference_db=float(10 * np.log10(py / px)),
                                residual_relative_db=float(10 * np.log10(max(np.mean((played-reference)**2) / px, 1e-30))),
                                spectral_shape_max_band_difference_db=metrics['max_abs_band_error_db']))
        gain = min(.12 / np.sqrt(np.mean(x**2)), .9 / max(np.max(abs(x)), np.max(abs(y))))
        for label, audio in [('source', x), ('plugin', y)]:
            sf.write(output / f"{case['id']}-{label}.wav", audio * gain, rate, subtype='PCM_24')
        plot(x, y, rate, case, output / f"{case['id']}.png")
        cases.append(dict(id=case['id'], label=case['label'], windows=windows,
                          source_sha256=case['source_sha256'],
                          plugin_raw_sha256=digest(folder / 'neutral-plugin-raw.wav'),
                          fixed_gain_correction=pre['gain_correction'],
                          pre_loop_residual_relative_db=pre['residual_relative_db']))
    report = dict(plugin_executable_sha256=config['plugin_executable_sha256'],
                  bank_sha256=config['bank_sha256'], corpus_summary=summary,
                  corpus=corpus, cases=cases,
                  interpretation='After a loop begins, these are different performances. Spectral differences are not an EQ transfer function or a realism score. One pre-loop gain correction is retained across the whole note.')
    (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    sections = []
    for case in cases:
        sections.append(f"<section><h2>{html.escape(case['label'])}</h2><p>Original full performance</p>"
                        f"<audio controls preload='none' src='{case['id']}-source.wav'></audio>"
                        f"<p>VST3 capture, neutral settings</p><audio controls preload='none' src='{case['id']}-plugin.wav'></audio>"
                        f"<img src='{case['id']}.png' alt='Full-note and recorded-loop comparison'>"
                        f"<pre>{html.escape(json.dumps(case['windows'], indent=2))}</pre></section>")
    page = """<!doctype html><meta charset="utf-8"><title>TopBrass sustain loops</title>
<style>body{font:17px system-ui;background:#151719;color:#eee;max-width:1000px;margin:40px auto;padding:20px;line-height:1.6}audio,img{width:100%}pre{white-space:pre-wrap}a{color:#dfbb75}section{border-top:1px solid #555;margin-top:35px}</style>
<h1>What the sustain loop replaces</h1><p>The source continues to evolve, while the current player repeats an earlier segment. The dashed line marks the first crossfade into that repetition. This may contribute to an unnaturally static held note; listening decides whether it does.</p>
<p>The audio uses one fixed gain match established before the loop. No EQ, alignment, compression or denoising is added. Post-loop spectral differences compare different performances, so they do not prescribe corrective EQ.</p>"""
    page += '<pre>' + html.escape(json.dumps(summary, indent=2)) + '</pre>' + ''.join(sections)
    page += '<p><a href="results.json">Full measurements and recording identities</a> · <a href="../">Original source audit</a></p>'
    (output / 'index.html').write_text(page)
    return report


def plot(x, y, rate, case, path):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig, axes = plt.subplots(2, 1, figsize=(12, 7), layout='constrained')
    hop = int(rate * .02)
    n = len(x) // hop
    for audio, label in [(x, 'Original'), (y, 'VST3 capture, fixed pre-loop gain match')]:
        rms = np.sqrt(np.mean(audio[:n*hop].reshape(n, hop, 2)**2, axis=(1, 2)))
        axes[0].plot(np.arange(n) * hop / rate, 20 * np.log10(np.maximum(rms, 1e-10)), label=label)
    axes[0].set(title=case['label'] + ': full sustained recording', xlabel='Time (s)', ylabel='20 ms RMS (dBFS)')
    axes[0].legend()
    residual = np.sqrt(np.mean((y-x)[:n*hop].reshape(n, hop, 2)**2, axis=(1, 2)))
    reference = np.sqrt(np.mean(x*x))
    axes[1].plot(np.arange(n) * hop / rate, 20 * np.log10(np.maximum(residual / reference, 1e-10)))
    axes[1].set(title='Waveform difference relative to original whole-note RMS', xlabel='Time (s)', ylabel='Residual (dB)')
    for axis in axes:
        axis.axvline((case['loop_end'] - case['loop_crossfade']) / rate, linestyle='--', color='gray')
        axis.grid(alpha=.2)
    fig.savefig(path, dpi=120)
    plt.close(fig)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('audit_directory', type=Path, help='Completed installed-VST3 recording audit')
    args = parser.parse_args()
    print(json.dumps(audit(args.audit_directory.resolve())['corpus_summary'], indent=2))
