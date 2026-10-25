"""Pinned War Tuba note maps shared by preparation and isolated auditions."""
import hashlib
import json
import re
from fetch_war_tuba import CACHE,COMMIT,REPO

def checked(relative):
    tree=json.loads((CACHE/'source-tree.json').read_text());assert tree['commit']==COMMIT and tree['repo']==REPO
    entry=next(row for row in tree['tree'] if row['path']==relative)
    data=(CACHE/relative).read_bytes();assert hashlib.sha1(f'blob {len(data)}\0'.encode()+data).hexdigest()==entry['sha'],relative
    return CACHE/relative


def mapping(kind):
    source=checked('Programs/poly/'+kind+'_cnd.sfz');text=re.sub(r'//[^\n]*','',source.read_text());group={};rows=[]
    for section,body in re.findall(r'<(group|region)>([^<]*)',text):
        attributes=dict(re.findall(r'(\w+)=([^\s]+)',body))
        if section=='group':group=attributes;continue
        attrs=group|attributes;path=attrs['sample'].replace('\\','/').removeprefix('../')
        checked(path);match=re.search(r'_rr(\d+)_',path)
        root=int(attrs['pitch_keycenter']);low=int(attrs['lokey']);high=int(attrs['hikey'])
        assert 27<=low<=root<=high<=60
        rows.append(dict(volume_db=float(attrs.get('volume',0)),path=path,kind=kind,root=root,low_key=low,high_key=high,
                         low_velocity=int(attrs.get('lovel',0)),high_velocity=int(attrs.get('hivel',127)),take=int(match[1])-1 if match else 0))
    assert len(rows)==(27 if kind=='sustain' else 324)
    expected=[(0,127)] if kind=='sustain' else [(0,32),(33,64),(65,96),(97,127)] if kind=='staccato' else [(0,42),(43,85),(86,127)]
    assert {(r['low_velocity'],r['high_velocity']) for r in rows}==set(expected)
    assert len({r['path'] for r in rows})==len(rows)
    return rows
