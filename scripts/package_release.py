"""Package built macOS bundles without changing their signatures."""
import argparse
import hashlib
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def package(build, output):
    output.mkdir(parents=True, exist_ok=True)
    checksums = []
    expected_version = re.search(r'project\(TopBrass VERSION ([0-9.]+)', (ROOT / 'CMakeLists.txt').read_text())[1]
    formats = [('AU', 'TopBrass.component'), ('VST3', 'TopBrass.vst3'), ('Standalone', 'TopBrass.app')]
    for format, name in formats:
        bundle = build / 'TopBrass_artefacts/Release' / format / name
        with (bundle / 'Contents/Info.plist').open('rb') as stream:
            info = plistlib.load(stream)
        if info['CFBundleShortVersionString'] != expected_version:
            raise ValueError('Stale bundle version: ' + str(bundle))
        executable = bundle / 'Contents/MacOS' / info['CFBundleExecutable']
        archs = subprocess.check_output(['lipo', '-archs', str(executable)], text=True).split()
        if archs != ['arm64']:
            raise ValueError('Expected an Apple Silicon build: ' + str(bundle))
    for format, name in formats:
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
            juce = ROOT / 'vendor/JUCE'
            notices = stage / 'ThirdParty/JUCE'
            notices.mkdir(parents=True)
            shutil.copy2(juce / 'JUCE.spdx.json', notices / 'JUCE.spdx.json')
            for path in juce.rglob('*'):
                if path.is_file() and '.git' not in path.relative_to(juce).parts and any(word in path.name.lower() for word in ('license', 'licence', 'copying', 'notice', 'copyright', 'readme')):
                    target_notice = notices / path.relative_to(juce)
                    target_notice.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(path, target_notice)
            subprocess.run(['ditto', '-c', '-k', '--sequesterRsrc', str(stage), str(target.resolve())], check=True)
        if target.stat().st_size >= 2 * 1024**3:
            raise ValueError('Release asset exceeds GitHub limit: ' + str(target))
        with target.open('rb') as stream:
            checksum = hashlib.file_digest(stream, 'sha256').hexdigest()
        checksums.append(checksum + '  ' + target.name)
        print(checksums[-1], flush=True)
    juce_source = output / 'JUCE-9.0.4-source.tar.gz'
    subprocess.run(['git', '-C', str(ROOT / 'vendor/JUCE'), 'archive', '--format=tar.gz', '--prefix=JUCE-9.0.4/', '-o', str(juce_source.resolve()), '3fb5cbc0340758596dac39f7c8de47d562b4896e'], check=True)
    with juce_source.open('rb') as stream:
        checksums.append(hashlib.file_digest(stream, 'sha256').hexdigest() + '  ' + juce_source.name)
    (output / 'SHA256SUMS.txt').write_text('\n'.join(checksums) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    package(args.build, args.output)
