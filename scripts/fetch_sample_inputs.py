"""Fetch and verify the versioned, separately licensed sample build inputs."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def unpack(archive_path, root, manifest):
    expected = {row['path']: row for row in manifest}
    if len(expected) != len(manifest):
        raise ValueError('Duplicate paths in sample manifest')
    with tempfile.TemporaryDirectory(dir=root) as scratch:
        stage = Path(scratch)
        with tarfile.open(archive_path, 'r:gz') as archive:
            seen = set()
            for member in archive:
                name = PurePosixPath(member.name)
                if name.is_absolute() or '..' in name.parts or not member.isfile() or member.name not in expected or member.name in seen:
                    raise ValueError('Unexpected archive member: ' + member.name)
                row = expected[member.name]
                if member.size != row['bytes']:
                    raise ValueError('Wrong input length: ' + member.name)
                target = stage / member.name
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.extractfile(member) as source, target.open('wb') as output:
                    shutil.copyfileobj(source, output)
                if digest(target) != row['sha256']:
                    raise ValueError('Wrong input checksum: ' + member.name)
                seen.add(member.name)
            if seen != set(expected):
                raise ValueError('Sample archive is incomplete')
        for name in sorted(seen):
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            (stage / name).replace(target)


def fetch():
    lock = json.loads((ROOT / 'assets/sample-inputs.lock.json').read_text())
    if all((ROOT / row['path']).is_file() and digest(ROOT / row['path']) == row['sha256'] for row in lock['files']):
        print('Sample build inputs already verified', flush=True)
        return
    cache = ROOT / 'assets/cache'
    cache.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=cache) as scratch:
        archive = Path(scratch) / 'inputs.tar.gz'
        print('Downloading licensed sample inputs: ' + lock['url'], flush=True)
        request = urllib.request.Request(lock['url'], headers={'User-Agent': 'TopBrass-bootstrap'})
        with urllib.request.urlopen(request, timeout=120) as source, archive.open('wb') as output:
            total = 0
            while chunk := source.read(1024 * 1024):
                total += len(chunk)
                if total > lock['bytes']:
                    raise ValueError('Sample download exceeds pinned size')
                output.write(chunk)
        if total != lock['bytes'] or digest(archive) != lock['sha256']:
            raise ValueError('Sample download checksum mismatch')
        unpack(archive, ROOT, lock['files'])
    print('All sample build inputs verified', flush=True)


if __name__ == '__main__':
    fetch()
