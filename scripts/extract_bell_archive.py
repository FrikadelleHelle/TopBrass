"""Extract FLAC payloads without a lossy transcode from a HISE .hr1 archive.

Format reference: HISE hi_lac/hlac/CompressionHelpers.cpp, HlacArchiver.
Only single-part archives are accepted. The original download is never modified.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def extract(source, destination):
    destination.mkdir(parents=True, exist_ok=True)
    entries = []
    with source.open('rb') as stream:
        def integer(format='<I'):
            length = struct.calcsize(format)
            data = stream.read(length)
            if len(data) != length:
                raise ValueError('Truncated archive')
            return struct.unpack(format, data)[0]

        def flag(expected):
            actual = integer()
            if actual != expected:
                raise ValueError(f'Expected flag {expected}, got {actual} at {stream.tell()-4}')

        def string():
            data = bytearray()
            while True:
                char = stream.read(1)
                if not char or len(data) > 65536:
                    raise ValueError('Invalid archive string')
                if char == b'\0':
                    return data.decode('utf-8')
                data.extend(char)

        def payload(target, size):
            if size < 0 or size > source.stat().st_size - stream.tell():
                raise ValueError('Invalid archive payload length')
            digest = hashlib.sha256()
            with target.open('wb') as output:
                remaining = size
                while remaining:
                    data = stream.read(min(1024*1024, remaining))
                    if not data:
                        raise ValueError('Truncated archive payload')
                    output.write(data); digest.update(data); remaining -= len(data)
            return digest.hexdigest()

        flag(0); metadata = json.loads(string()); flag(1)
        current = integer()
        if current == 13:
            size = integer('<q')
            checksum = payload(destination/'header.dat', size)
            entries.append(dict(file='header.dat', bytes=size, sha256=checksum))
            flag(14); current = integer()
        while current == 15:
            size = integer('<q'); name = string()
            if Path(name).name != name:
                raise ValueError('Additional file must be a basename')
            checksum = payload(destination/name, size)
            entries.append(dict(file=name, bytes=size, sha256=checksum))
            flag(16); current = integer()
        while current == 2:
            name = string(); flag(3); flag(4); timestamp = string(); flag(5)
            if Path(name).name != name:
                raise ValueError('Monolith must be a basename')
            flag(6); size = integer('<q'); flag(7); flag(8)
            target = destination/(name+'.flac')
            checksum = payload(target, size)
            flag(9)
            with target.open('rb') as audio:
                if audio.read(4) != b'fLaC':
                    raise ValueError('Expected FLAC payload')
            entries.append(dict(file=target.name, monolith=name, bytes=size, sha256=checksum, timestamp=timestamp))
            current = integer()
        if current != 12 or stream.read(1):
            raise ValueError('Unsupported multipart archive or trailing data')
    report = dict(source=str(source), source_sha256=hashlib.file_digest(source.open('rb'), 'sha256').hexdigest(),
                  metadata=metadata, entries=entries)
    (destination/'extraction.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    extract(args.source, args.destination)
