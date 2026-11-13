#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Hash-verified, resumable lighting-only bake; immutable input assets are untouched."""
import argparse
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import time
from cook_lock import lock_directory

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--cooker',required=True,type=pathlib.Path)
    parser.add_argument('--output',required=True,type=pathlib.Path)
    args=parser.parse_args()
    root=pathlib.Path(__file__).resolve().parent.parent
    files=['tools/cook_lighting.py','tools/lighting_cooker.cpp','src/demos/showcase_world.cpp',
           'src/demos/showcase_world.hpp','src/demos/lighting_cpu.cpp','src/demos/lighting_cpu.hpp',
           'src/demos/math.hpp','src/demos/surface_details.hpp','src/demos/water_surface.hpp',
           'src/demos/stream_geometry.hpp','tools/include/kgfx_asset_format.h']
    fingerprint=hashlib.sha256()
    for name in files:
        fingerprint.update(name.encode());fingerprint.update((root/name).read_bytes())
    dependency=next(d for d in json.loads((root/'deps/toolchain-lock.json').read_text())['dependencies'] if d['name']=='xatlas')
    fingerprint.update(json.dumps(dependency,sort_keys=True).encode())
    for name in ('modular_industrial_pipes_01_pipe01.kgmodel','power_box_01.kgmodel','Barrel_01.kgmodel'):
        fingerprint.update((args.output/name).read_bytes())
    identity=fingerprint.hexdigest()
    target=args.output/'lighting';target.mkdir(parents=True,exist_ok=True)
    with lock_directory(target/'cook.lock'):
        stamp=target/'cook.stamp'
        current=json.loads(stamp.read_text()) if stamp.exists() else {}
        if current.get('identity')==identity and current.get('outputs') and all((args.output/name).is_file() and digest(args.output/name)==value for name,value in current['outputs'].items()):
            print('Reusing verified offline lighting bake');return
        staging=target/'staging';shutil.rmtree(staging,ignore_errors=True);staging.mkdir()
        subprocess.run([str(args.cooker),str(staging),str(args.output)],check=True)
        files=sorted(p for p in (staging/'lighting').iterdir() if p.is_file())
        expected={'industrial.kglight','industrial.kglmap','industrial_lightmap.kgtex'}
        if {p.name for p in files}!=expected:raise RuntimeError('Incomplete lighting bake publication')
        outputs={'lighting/'+p.name:digest(p) for p in files}
        for path in files:os.replace(path,target/path.name)
        shutil.rmtree(staging)
        temporary=target/'cook.stamp.part';temporary.write_text(json.dumps({'schema':1,'identity':identity,'outputs':outputs},indent=2)+'\n');os.replace(temporary,stamp)
if __name__=='__main__':main()
