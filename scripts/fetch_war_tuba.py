"""Fetch and verify the pinned original War Tuba repository, including its WAV files."""
import hashlib
import json
from pathlib import Path,PurePosixPath
import urllib.request
import zipfile

ROOT=Path(__file__).resolve().parents[1]
REPO='sfzinstruments/karoryfer.war-tuba'
COMMIT='5b62dd6ef6b00281bb734769e44a63f5e013018a'
CACHE=ROOT/'assets/cache/karoryfer.war-tuba'


def request(url):
    return urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'TopBrass-source-audit'}),timeout=45)


def fetch():
    CACHE.mkdir(parents=True,exist_ok=True)
    tree_path=CACHE/'source-tree.json'
    if not tree_path.exists():
        with request(f'https://api.github.com/repos/{REPO}/git/trees/{COMMIT}?recursive=1') as response:tree=json.load(response)
        assert not tree['truncated']
        tree_path.write_text(json.dumps(dict(repo=REPO,commit=COMMIT,**tree),indent=2)+'\n')
    tree=json.loads(tree_path.read_text());assert tree['repo']==REPO and tree['commit']==COMMIT and not tree['truncated']
    entries=[entry for entry in tree['tree'] if entry['type']=='blob']
    archive=CACHE/'source.zip';temporary=archive.with_suffix('.partial')
    if not archive.exists():
        if temporary.exists():raise ValueError('Unfinished download retained at '+str(temporary))
        with request(f'https://codeload.github.com/{REPO}/zip/{COMMIT}') as response,temporary.open('xb') as output:
            total=0
            while chunk:=response.read(1024*1024):
                total+=len(chunk)
                if total>200_000_000:raise ValueError('Source archive exceeded expected size bound')
                output.write(chunk)
        temporary.replace(archive)
    verified=[]
    prefix='karoryfer.war-tuba-'+COMMIT+'/'
    with zipfile.ZipFile(archive) as package:
        for entry in entries:
            relative=PurePosixPath(entry['path']);assert not relative.is_absolute() and '..' not in relative.parts
            info=package.getinfo(prefix+entry['path']);assert info.file_size==entry['size']
            data=package.read(info)
            assert hashlib.sha1(f'blob {len(data)}\0'.encode()+data).hexdigest()==entry['sha'],entry['path']
            path=CACHE/entry['path']
            if path.exists():assert path.read_bytes()==data,entry['path']
            else:path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
            verified.append(dict(path=entry['path'],bytes=len(data),git_blob_sha1=entry['sha'],sha256=hashlib.sha256(data).hexdigest()))
    assert 'CC0 1.0 Universal' in (CACHE/'LICENSE').read_text()
    with archive.open('rb') as stream:archive_sha=hashlib.file_digest(stream,'sha256').hexdigest()
    report=dict(repository=REPO,commit=COMMIT,files=verified,wav_count=sum(row['path'].endswith('.wav') for row in verified),
                archive_sha256=archive_sha,license='CC0 1.0 Universal, source LICENSE retained',installed_instrument_changed=False)
    (CACHE/'verified-source.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS:',len(verified),'pinned Git blobs,',report['wav_count'],'original WAV files; installed instrument unchanged')
    return report


if __name__=='__main__':fetch()
