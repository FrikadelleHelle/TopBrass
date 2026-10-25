from pathlib import Path
import sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from update_install import standalone_target
with tempfile.TemporaryDirectory() as directory:
    base=Path(directory);home=base/'home';system=base/'Applications';user=home/'Applications/TopBrass.app';app=system/'TopBrass.app'
    for state in ['absent','user','both','system']:
        if state=='user':user.mkdir(parents=True)
        if state=='both':app.mkdir(parents=True)
        if state=='system':user.rmdir()
        if state in ['absent','both']:
            try:standalone_target(home,system)
            except ValueError:pass
            else:raise AssertionError('An absent or ambiguous target must not be silently chosen')
        else:assert standalone_target(home,system)==(user if state=='user' else app)
print('PASS:standalone target detection preserves system/user installations and rejects ambiguity')
