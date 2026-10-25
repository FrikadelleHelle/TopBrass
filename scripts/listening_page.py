"""Publish the current listening bench while retaining the historical recording audit."""
import hashlib
import html
import json
from pathlib import Path
import shutil

import numpy as np
import soundfile as sf

ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/'artifacts/recording-audit'


def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def main():
    release=json.loads((ROOT/'artifacts/validation-summary.json').read_text());version=release['version']
    vst=next(row for row in release['bundles'] if row['format']=='VST3')
    assert sha(Path(vst['path'])/'Contents/MacOS/TopBrass')==vst['executable_sha256']
    report=BASE/f'format-compatibility-v{version}'
    formats=json.loads((report/'results.json').read_text());assert formats['version']==version
    assert formats['plugin_identities']['VST3']['plugin_executable_sha256']==vst['executable_sha256']
    old=BASE/'original-audit-v0.7.html'
    if not (BASE/'listening-index.json').exists():
        assert json.loads((BASE/'tenor-sax-sustain/neutral-host.json').read_text())['version']=='0.7.0'
        original=(BASE/'index.html').read_bytes()
        if old.exists():assert old.read_bytes()==original
        else:old.write_bytes(original)
    assert old.exists()
    demo=BASE/'current-section';demo.mkdir(exist_ok=True)
    captures=[]
    for case,label in [('section-tenor','Tight four-horn section with tenor'),('section-baritone-detail','Tight four-horn section with baritone keys and air'),('section-six-players','Six players with both saxes and trumpets')]:
        source=report/'VST3'/case/'neutral-plugin-raw.wav';audio,rate=sf.read(source,always_2d=True)
        gain=10**(18/20);assert np.isfinite(audio).all() and np.max(np.abs(audio*gain))<1
        destination=demo/(case+'.wav');sf.write(destination,audio*gain,rate,subtype='FLOAT')
        captures.append(dict(label=label,source=str(source),source_sha256=sha(source),listening_gain_db=18,listening_file=destination.relative_to(BASE).as_posix(),listening_sha256=sha(destination)))
    metadata=dict(kind='TopBrass listening hub',version=version,installed_vst_executable_sha256=vst['executable_sha256'],historical_audit='original-audit-v0.7.html',historical_audit_sha256=sha(old),captures=captures)
    content=f'''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>TopBrass listening bench · {html.escape(version)}</title><style>body{{font:18px/1.6 system-ui;background:#151719;color:#eee;max-width:1000px;margin:40px auto;padding:20px}}audio{{width:100%}}a{{color:#dfbb75}}section{{border-top:1px solid #555;padding-top:16px;margin-top:28px}}.muted{{color:#bbb}}</style><h1>TopBrass listening bench</h1><p>Section captures from version {html.escape(version)}. Start here, then compare individual original recordings with actual VST playback.</p><p class="muted">These section captures use neutral Tone and Attack accent, centered pan and diagnostic instrument levels. They do not reproduce your saved patch. One fixed +18 dB listening gain applies to both exports; there is no EQ, compression or per-note normalization.</p>'''
    for row in captures:content+=f'<section><h2>{html.escape(row["label"])}</h2><audio controls preload="none" src="{row["listening_file"]}"></audio></section>'
    links=[(f'installed-tenor-details-v{version}/','Recorded tenor detail in the installed instrument','Explicit breath/key MIDI triggers, originals and actual AU/VST3 captures. No automatic phrase breathing.'),('raw-source-candidates-v0.13.0/','Raw originals versus VST, with spectrograms','Original recordings followed by actual VST playback at native pitch and rate. Technical fidelity is separate from source realism.'),('complete-alto-audition/','Current tenor versus candidate alto','The same MIDI phrase through both sources. The alto is an isolated candidate with incomplete natural endings.'),('mtg-tenor-detail/','Recorded tenor breath, keys and tone candidate','Original recordings and actual VST captures. Source-labelled breath does not establish a phrase inhalation.'),('tight-section/','Tight versus earlier spaced section','Historical playing-mode comparison, with soft and strong dynamics.'),('original-audit-v0.7.html','Historical version0.7 recording audit','Preserved earlier evidence. These recordings do not describe the current installed player.')]
    links.insert(1, ('complete-tenor-audition/', 'Current tenor versus complete MTG tenor candidate', 'Raw soft and strong recordings, complete-source VST spectrograms and the same MIDI phrase through both sources.'))
    paired=f'paired-takes-v{version}/' if (BASE/f'paired-takes-v{version}/index.html').exists() else 'paired-takes-v0.15.1/'
    links.insert(0, (paired, 'Paired recorded takes, version0.15.1 before/after reference', 'Actual VST3 comparison of tightly syncopated trumpet and baritone Short duets, with the selected original recordings identified.'))
    links.insert(0, (f'alternate-tenor-v{version}/', 'Play and compare the alternate tenor', 'The same tightly syncopated section with each tenor source, soft and strong source references, actual VST attacks and spectrograms. Each sax has an independent source selector.'))

    links.insert(0, ('attack-timing-v0.16.0-verified/', 'Compare recorded attack shapes', 'The same four-player rhythm with Feel0, Short versus Sustain, a separate reference click and first150ms waveform views. Measurements distinguish digital silence from energy growth.'))
    links.insert(0, ('war-tuba-audition/', 'Current tuba versus War Tuba', 'The same groove with Sustain, staccato and staccatissimo, plus complete source/VST pairs and original breath/valve examples. A private candidate; the installed tuba is unchanged.'))
    for path,title,description in links:
        target=BASE/path
        if target.is_dir() and not (target/'index.html').exists():continue
        if not target.exists():continue
        content+=f'<section><h2><a href="{path}">{html.escape(title)}</a></h2><p>{html.escape(description)}</p></section>'
    content+='<p>Musical realism remains unaccepted. <a href="listening-index.json">Capture identities and listening gains</a>.</p><script>document.addEventListener("play",e=>{if(e.target.tagName==="AUDIO")for(const a of document.querySelectorAll("audio"))if(a!==e.target)a.pause()},true)</script></html>'
    (BASE/'index.html').write_text(content)
    (BASE/'listening-index.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print('Published current listening bench; historical0.7 audit preserved at',old)


if __name__=='__main__':main()
