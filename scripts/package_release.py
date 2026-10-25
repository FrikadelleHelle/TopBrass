"""Package built macOS bundles without changing their signatures."""
import argparse
import hashlib
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def package(build, output):
    output.mkdir(parents=True, exist_ok=True)
    checksums = []
    for format, name in [('AU', 'TopBrass.component'), ('VST3', 'TopBrass.vst3'), ('Standalone', 'TopBrass.app')]:
        source = build / 'TopBrass_artefacts/Release' / format / name
        subprocess.run(['codesign', '--verify', '--deep', '--strict', str(source)], check=True)
        with (source / 'Contents/Info.plist').open('rb') as stream:
            version = plistlib.load(stream)['CFBundleShortVersionString']
        target = output / f'TopBrass-{version}-macOS-arm64-{format}.zip'
        if target.exists():
            raise FileExistsError(target)
        with tempfile.TemporaryDirectory() as scratch:
            stage = Path(scratch)
            subprocess.run(['ditto', str(source), str(stage / name)], check=True)
            for filename in ('LICENSE', 'LICENSES.md', 'README.md'):
                shutil.copy2(ROOT / filename, stage / filename)
            subprocess.run(['ditto', '-c', '-k', '--sequesterRsrc', str(stage), str(target.resolve())], check=True)
        if target.stat().st_size >= 2 * 1024**3:
            raise ValueError('Release asset exceeds GitHub limit: ' + str(target))
        with target.open('rb') as stream:
            checksum = hashlib.file_digest(stream, 'sha256').hexdigest()
        checksums.append(checksum + '  ' + target.name)
        print(checksums[-1], flush=True)
    (output / 'SHA256SUMS.txt').write_text('\n'.join(checksums) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    package(args.build, args.output)
