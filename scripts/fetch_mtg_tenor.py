"""Fetch pinned MTG tenor details or the complete pitched tenor recordings."""
from concurrent.futures import ThreadPoolExecutor
import argparse
import hashlib
import json
from pathlib import Path
import urllib.parse
import urllib.request

import numpy as np
import soundfile as sf

ROOT=Path(__file__).resolve().parents[1]
CACHE=ROOT/'assets/cache/MTG.SoloSax'
COMMIT='b494d256549b3d088fdec176ce82867f8a1f58b2'
REPO='sfzinstruments/MTG.SoloSax'


def source_file(entry):
    path=CACHE/entry['path']
    if path.exists():data=path.read_bytes()
    else:
        url=f'https://raw.githubusercontent.com/{REPO}/{COMMIT}/'+urllib.parse.quote(entry['path'])
        with urllib.request.urlopen(url,timeout=45) as response:data=response.read(entry['size']+1)
    assert len(data)==entry['size'] and hashlib.sha1(f'blob {len(data)}\0'.encode()+data).hexdigest()==entry['sha'],entry['path']
    if not path.exists():path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
    return path,data


def fetch(entry):
    path,data=source_file(entry)
    audio,rate=sf.read(path,dtype='float32',always_2d=True)
    assert rate==48000 and audio.shape[1]==1 and np.isfinite(audio).all() and np.max(np.abs(audio))<=1
    energy=float(np.mean(audio.astype(float)**2));assert energy>1e-20
    return dict(path=entry['path'],git_blob_sha1=entry['sha'],file_sha256=hashlib.sha256(data).hexdigest(),
                decoded_pcm_sha256=hashlib.sha256(audio.T.astype('<f4').tobytes()).hexdigest(),frames=len(audio),rate=rate,
                peak=float(np.max(np.abs(audio))),rms=float(np.sqrt(energy)),
                classification='source-labelled breath' if path.name.startswith('ten_b_') else 'source-labelled key click' if path.name.startswith('ten_k_') else 'pitched tenor recording',
                source_pcm_unchanged=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--pitched',action='store_true',help='Fetch all66 native tenor notes into a separate catalogue');args=parser.parse_args()
    tree_path=CACHE/'source-tree.json'
    if not tree_path.exists():
        with urllib.request.urlopen(f'https://api.github.com/repos/{REPO}/git/trees/{COMMIT}?recursive=1',timeout=45) as response:tree=json.load(response)
        assert not tree['truncated']
        CACHE.mkdir(parents=True,exist_ok=True)
        tree_path.write_text(json.dumps(dict(repo=REPO,commit=COMMIT,**tree),indent=2)+'\n')
    tree=json.loads(tree_path.read_text());assert tree['repo']==REPO and tree['commit']==COMMIT and not tree['truncated']
    refs=[entry for entry in tree['tree'] if entry['type']=='blob' and (entry['path'].endswith(('.txt','.sfz','.md')) or entry['path']=='LICENSE')]
    with ThreadPoolExecutor(max_workers=6) as pool:list(pool.map(source_file,refs))
    if args.pitched:
        selected=[entry for entry in tree['tree'] if '/Samples/ten_' in entry['path'] and ('/ten_p_' in entry['path'] or '/ten_f_' in entry['path'])]
    else:
        selected=[entry for entry in tree['tree'] if '/Samples/ten_' in entry['path'] and
                  (('/ten_b_' in entry['path'] or '/ten_k_' in entry['path']) or entry['path'].endswith(('ten_p_17.flac','ten_f_17.flac')))]
    assert len(selected)==(66 if args.pitched else 99)
    with ThreadPoolExecutor(max_workers=6) as pool:rows=list(pool.map(fetch,selected))
    report=dict(repository=REPO,commit=COMMIT,license='CC BY 4.0, repository LICENSE retained',
                attribution='Samples by Music Technology Group, Universitat Pompeu Fabra; SFZ preparation by kinwie.',
                maps=['MTG Solo Saxophones/Data/group_ten.txt', 'MTG Solo Saxophones/Data/ten_p_rr1.txt', 'MTG Solo Saxophones/Data/ten_f_rr1.txt'] if args.pitched else ['MTG Solo Saxophones/Data/noise_ten.txt'],
                legato='Source legato uses sustained recordings with offset and envelope changes, not recorded interval transitions.',
                recording_count=len(rows),recordings=rows,installed_instrument_changed=False)
    (CACHE/('tenor-notes.json' if args.pitched else 'tenor-details.json')).write_text(json.dumps(report,indent=2)+'\n')
    print(f'Verified{len(rows)} original FLAC files; installed instrument unchanged')


if __name__=='__main__':main()
