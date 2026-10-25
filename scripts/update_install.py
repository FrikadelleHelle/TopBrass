"""Update the per-user macOS bundles while retaining the previous install and settings."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]


def standalone_target(home=None,applications=None):
    home=Path.home() if home is None else Path(home)
    applications=Path('/Applications') if applications is None else Path(applications)
    existing=[path for path in [applications/'TopBrass.app',home/'Applications/TopBrass.app'] if path.exists()]
    if len(existing)!=1:
        raise ValueError('Expected exactly one standalone installation; select an explicit --standalone path')
    return existing[0]


def update(report_path,standalone=None,dry_run=False):
    if report_path.exists():
        raise ValueError('Choose a new report path; existing installation history is retained')
    running=subprocess.run(['pgrep','-x','TopBrass'],stdout=subprocess.DEVNULL).returncode
    if running not in (0,1):raise ValueError('Could not determine whether the standalone is running')
    if running==0 and not dry_run:raise ValueError('Close the standalone app before replacing its bundles')
    bundles=[('AU',Path.home()/'Library/Audio/Plug-Ins/Components/TopBrass.component'),
             ('VST3',Path.home()/'Library/Audio/Plug-Ins/VST3/TopBrass.vst3'),
             ('Standalone',standalone_target() if standalone is None else standalone)]
    versions=[]
    for kind,path in bundles:
        if not path.exists():raise ValueError('Expected an existing complete installation: '+str(path))
        built=ROOT/'build/TopBrass_artefacts/Release'/kind/path.name
        versions.append(plistlib.loads((built/'Contents/Info.plist').read_bytes())['CFBundleShortVersionString'])
        subprocess.run(['codesign','--verify','--deep','--strict',str(built)],check=True)
    if len(set(versions))!=1:raise ValueError('Built bundle versions differ')
    settings=Path.home()/'Library/Application Support/TopBrass.settings'
    before=settings.read_bytes() if settings.exists() else None
    if dry_run:
        print(json.dumps(dict(version=versions[0],standalone_running=running==0,targets=[dict(format=kind,path=str(path)) for kind,path in bundles],settings_sha256=hashlib.sha256(before).hexdigest() if before is not None else None),indent=2));return
    backup=Path(tempfile.mkdtemp(prefix=f'installed-before-v{versions[0]}-',dir=ROOT/'artifacts'))
    if before is not None:shutil.copy2(settings,backup/settings.name)
    report={'version':versions[0],'backup':str(backup),'timestamp_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'settings_sha256':hashlib.sha256(before).hexdigest() if before is not None else None,
            'bundles':[{'format':kind,'original':str(path),'backup':str(backup/kind/path.name)} for kind,path in bundles]}
    report_path.parent.mkdir(parents=True,exist_ok=True)
    report_path.write_text(json.dumps(report,indent=2)+'\n')
    for bundle in report['bundles']:
        target=Path(bundle['backup']);target.parent.mkdir();shutil.move(bundle['original'],target)
    subprocess.run([str(ROOT/'scripts/install.sh')],check=True,cwd=ROOT,env=os.environ|{'TOPBRASS_APPS_DIR':str(bundles[-1][1].parent)})
    after=settings.read_bytes() if settings.exists() else None
    if before!=after:raise ValueError('Settings changed during installation; original retained in backup')
    print(f'Installed {versions[0]}; previous bundles and settings retained at {backup}')


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--report',type=Path,required=True);parser.add_argument('--standalone',type=Path);parser.add_argument('--dry-run',action='store_true');args=parser.parse_args()
    update(args.report.resolve(),args.standalone.resolve() if args.standalone else None,args.dry_run)
