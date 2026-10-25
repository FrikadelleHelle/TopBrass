"""Package the licensed upstream inputs needed to rebuild every sample bank."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import tarfile

ROOT = Path(__file__).resolve().parents[1]


def package(destination):
    files = {}
    cache = ROOT / 'assets/cache'
    for name in ('VCSL', 'VSCO-2-CE', 'karoryfer.bear-sax', 'MTG.SoloSax', 'karoryfer.war-tuba'):
        for path in sorted((cache / name).rglob('*')):
            if path.is_file() and path.name != '.DS_Store':
                files[path.relative_to(ROOT).as_posix()] = path
    files['assets/cache/bear-details/source-tree.json'] = cache / 'bear-details/source-tree.json'
    bell = cache / 'bell-and-bone'
    extraction = json.loads((bell / 'extraction.json').read_text())
    extraction['source'] = 'Bell and Bone_2_1_0_Samples.hr1'
    extraction['entries'] = [e for e in extraction['entries'] if e['file'].endswith('.ch1.flac')]
    if len(extraction['entries']) != 4:
        raise ValueError('Expected exactly four original Bell & Bone audio monoliths')
    files['assets/cache/bell-and-bone/extraction.json'] = (json.dumps(extraction, indent=2) + '\n').encode()
    for entry in extraction['entries']:
        path = bell / entry['file']
        with path.open('rb') as stream:
            if hashlib.file_digest(stream, 'sha256').hexdigest() != entry['sha256']:
                raise ValueError('Changed Bell & Bone input: ' + entry['file'])
        files[path.relative_to(ROOT).as_posix()] = path
    for path in sorted((bell / 'public-source').iterdir()):
        if path.is_file() and path.suffix in ('.xml', '.txt'):
            files[path.relative_to(ROOT).as_posix()] = path
    for path in sorted((ROOT / 'assets/plugin-resources').iterdir()):
        if path.is_file():
            files[path.relative_to(ROOT).as_posix()] = path
    manifest = []
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(destination, 'w:gz', compresslevel=1) as archive:
        for name, source in sorted(files.items()):
            data = source.read_bytes() if isinstance(source, Path) else source
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mode = 0o644
            archive.addfile(info, io.BytesIO(data))
            manifest.append(dict(path=name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
    with destination.open('rb') as stream:
        checksum = hashlib.file_digest(stream, 'sha256').hexdigest()
    report = dict(url='https://github.com/FrikadelleHelle/TopBrass/releases/download/sample-inputs-v1/' + destination.name,
                  bytes=destination.stat().st_size, sha256=checksum, files=manifest)
    (ROOT / 'assets/sample-inputs.lock.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'{destination}: {len(manifest)} files, {destination.stat().st_size} bytes, SHA256 {checksum}', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    package(parser.parse_args().output)
