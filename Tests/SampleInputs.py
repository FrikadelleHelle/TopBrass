"""The input downloader accepts complete pinned archives, rejects unsafe/corrupt ones."""
import hashlib
import io
from pathlib import Path
import sys
import tarfile
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from fetch_sample_inputs import unpack

with tempfile.TemporaryDirectory() as scratch:
    root = Path(scratch)
    data = b'original sample bytes'
    name = 'assets/cache/example/sample.wav'
    manifest = [dict(path=name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())]
    def archive(path, member, body=data, kind=tarfile.REGTYPE):
        with tarfile.open(path, 'w:gz') as out:
            info = tarfile.TarInfo(member)
            info.type = kind
            info.size = len(body) if kind == tarfile.REGTYPE else 0
            out.addfile(info, io.BytesIO(body))
    source = root / 'input.tar.gz'
    archive(source, name)
    unpack(source, root, manifest)
    assert (root / name).read_bytes() == data
    for member, body, kind in [('../escape', data, tarfile.REGTYPE), (name, b'X'*len(data), tarfile.REGTYPE), (name, data, tarfile.SYMTYPE), ('unexpected', data, tarfile.REGTYPE)]:
        archive(source, member, body, kind)
        try:
            unpack(source, root, manifest)
        except ValueError:
            pass
        else:
            raise AssertionError('Invalid archive was accepted')
        assert (root / name).read_bytes() == data
    with tarfile.open(source, 'w:gz'):
        pass
    try:
        unpack(source, root, manifest)
    except ValueError:
        pass
    else:
        raise AssertionError('Incomplete archive was accepted')
print('PASS: pinned extraction, integrity, traversal, links and incomplete archive rejection')
