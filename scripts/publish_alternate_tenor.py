"""Publish retained alternate-tenor captures without altering the original audio."""
import argparse
import html
import json
from pathlib import Path

import numpy as np
import soundfile as sf


def publish(folder):
    report=json.loads((folder/'results.json').read_text())
    assert report['version']=='0.16.0' and report['all_source_checks_passed']
    content='''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>TopBrass alternate tenor</title><style>body{font:18px/1.6 system-ui;background:#151719;color:#eee;max-width:1000px;margin:40px auto;padding:20px}audio,img{width:100%}a{color:#dfbb75}section{border-top:1px solid #555;margin-top:28px;padding-top:16px}.muted{color:#bbb}</style><h1>Alternate tenor, actual VST playback</h1><p>The section keeps its shared syncopation. This comparison changes the primary sax recording set while the other players keep their sources. Neutral Tone and Attack accent throughout.</p><p class="muted">Every player below has one fixed +18dB listening gain. No EQ, compression, per-note normalization or timing correction. The source references include the authored mapping's tuning/gain and the diagnostic player/output gain. They are playback references, not untouched native-pitch files. <a href="../complete-tenor-audition/">Untouched complete original tenor recordings are here.</a></p>'''
    captures=[('groove-original/neutral-plugin-raw.wav','Four-horn Groove with original tenor'),
              ('groove-alternate/neutral-plugin-raw.wav','Same Groove with alternate tenor'),
              ('held/neutral-plugin-raw.wav','Alternate held tenor, finite recording'),
              ('gated-short/neutral-plugin-raw.wav','Alternate Short, gated sustain')]
    for force,title in [('soft','Soft recording, velocity100'),('strong','Strong recording, velocity101')]:
        captures.extend([(f'{force}-60/mapped-source-reference.wav',title+' source reference'),
                         (f'{force}-60/played-attack.wav',title+' actual VST attack')])
    for name,title in captures:
        path=folder/name;audio,rate=sf.read(path,always_2d=True);audio*=10**(18/20)
        assert np.isfinite(audio).all() and np.max(np.abs(audio))<1
        target=path.with_name(path.stem+'-listen.wav');sf.write(target,audio,rate,subtype='FLOAT')
        content+=f'<section><h2>{html.escape(title)}</h2><audio controls preload="none" src="{target.relative_to(folder).as_posix()}"></audio></section>'
    content+='''<section><h2>Source reference versus actual VST spectrum</h2><p>The source reference includes the intended pitch shift and fixed gains. All198 recorded attacks passed without fitting a gain or time shift. These comparisons test playback fidelity, not whether the recordings sound convincing.</p>'''
    for force in ['soft','strong']:
        content+=f'<h3>{force.capitalize()} tenor</h3><img src="{force}-60/attack-spectrum.png" alt="Source reference and VST spectrograms, spectrum, envelope and residual">'
    content+='''</section><p>The alternate source has no dedicated recorded Short or legato transitions. It contains two forces and neighboring-note variation; those neighbors reuse existing recordings. It is available independently for each sax. <a href="results.json">Full source checks</a>. <a href="../">Listening bench</a>.</p><script>document.addEventListener('play',e=>{if(e.target.tagName==='AUDIO')for(const a of document.querySelectorAll('audio'))if(a!==e.target)a.pause()},true)</script></html>'''
    (folder/'index.html').write_text(content)
    print('Published',folder/'index.html')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('folder',type=Path)
    publish(parser.parse_args().folder)
