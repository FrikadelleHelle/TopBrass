"""Fetch and catalogue the recorded baritone fingering/airflow detail without changing audio."""
import concurrent.futures
import hashlib
import json
from pathlib import Path
import re
import shutil
import soundfile as sf
from prepare_samples import ROOT, BEAR_REPO, BEAR_COMMIT, fetch, get


def audit():
    cache = ROOT/'assets/cache/bear-details'; cache.mkdir(parents=True,exist_ok=True)
    tree_file=cache/'source-tree.json'
    if not tree_file.exists():
        tree_file.write_bytes(get(f'https://api.github.com/repos/{BEAR_REPO}/git/trees/{BEAR_COMMIT}?recursive=1'))
    blobs={item['path']:item for item in json.loads(tree_file.read_text())['tree']}
    zones=[]; maps=[]
    for name in ('noises_map.sfz','breath.sfz'):
        path='Programs/poly/'+name
        entry=dict(repo=BEAR_REPO,commit=BEAR_COMMIT,path=path,blob=blobs[path]['sha'])
        mapping=fetch(entry)
        text=re.sub(r'//[^\n]*','',mapping.read_text())
        maps.append(dict(path=path,blob=entry['blob'],sha256=hashlib.sha256(mapping.read_bytes()).hexdigest()))
        group={}
        for tag,body in re.findall(r'<(group|region)>([^<]*)',text):
            values=dict(re.findall(r'([A-Za-z0-9_]+)=([^\s]+)',body))
            if tag=='group':
                group=values
            else:
                region=group|values
                zones.append(dict(kind='airflow' if name=='breath.sfz' else 'key-open' if region.get('trigger')=='release' else 'key-close',
                                  source='Samples/'+Path(region['sample']).name, opcodes=region))
    entries=[dict(repo=BEAR_REPO,commit=BEAR_COMMIT,path=path,blob=blobs[path]['sha']) for path in sorted({z['source'] for z in zones})]
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        files=list(pool.map(fetch,entries))
    recordings=[]
    for entry,file in zip(entries,files):
        info=sf.info(str(file))
        recordings.append(dict(path=entry['path'],local_file=str(file.relative_to(ROOT)),blob=entry['blob'],
                               sha256=hashlib.sha256(file.read_bytes()).hexdigest(),frames=info.frames,rate=info.samplerate,
                               channels=info.channels,format=info.subtype))
    report=dict(repository='https://github.com/'+BEAR_REPO,commit=BEAR_COMMIT,
                processing='Original WAV bytes, no crop, downmix, normalization, EQ or fades.',
                interpretation='Recorded fingering close/open plus looped airflow, not a recorded inhalation.',
                playback_integration='Catalogue only; active resources and playback validation establish integration status.',maps=maps,recordings=recordings,zones=zones)
    (cache/'catalog.json').write_text(json.dumps(report,indent=2)+'\n')
    output=ROOT/'artifacts/natural-details';output.mkdir(exist_ok=True)
    for label,source in (('baritone-key-close-native.wav','c2_close_rr1.wav'),('baritone-key-open-native.wav','c2_open_rr1.wav'),('baritone-airflow-native.wav','noise_breath.wav')):
        shutil.copy2(ROOT/'assets/cache'/BEAR_REPO.split('/')[-1]/'Samples'/source,output/label)
    license_path='LICENSE'
    license_file=fetch(dict(repo=BEAR_REPO,commit=BEAR_COMMIT,path=license_path,blob=blobs[license_path]['sha']))
    shutil.copy2(license_file,output/'LICENSE.txt')
    (output/'README.txt').write_text('Bear Sax recorded baritone detail, Karoryfer Samples, CC0.\n'
        'Source https://github.com/'+BEAR_REPO+'/tree/'+BEAR_COMMIT+'\n'
        'Original recording levels. No normalization or enhancement. Fingering source map applies -12 dB in playback.\n'
        'Airflow is a recorded under-note noise layer, not a player inhalation. Mechanical recordings have a separate playback bank; airflow is not integrated.\n')
    print(json.dumps(dict(recordings=len(recordings),zones=len(zones),source_bytes=sum(p.stat().st_size for p in files)),indent=2))


if __name__=='__main__':
    audit()
