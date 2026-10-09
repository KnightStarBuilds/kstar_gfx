#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Offline cooking keyed by source hashes and exact cooking implementation.

The persistent cache survives build-tree cleaning. Cross-process lock and staged
publication keep interrupted cooks from replacing previously usable packages.
"""
import argparse,hashlib,json,os,pathlib,shutil,subprocess,tempfile,time

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--cooker',required=True,type=pathlib.Path);parser.add_argument('--source',required=True,type=pathlib.Path);parser.add_argument('--output',required=True,type=pathlib.Path);parser.add_argument('--manifest',required=True,type=pathlib.Path);args=parser.parse_args()
    manifest=json.loads(args.manifest.read_text());root=pathlib.Path(__file__).resolve().parent
    fingerprint=hashlib.sha256(args.manifest.read_bytes())
    for file in [root/'asset_cooker.cpp',root/'include/kgfx_asset_format.h',root/'cook_assets.py']:
        fingerprint.update(file.read_bytes())
    for entry in manifest['files']:
        source=args.source/entry['file']
        if sha(source)!=entry['sha256']:raise RuntimeError('Source SHA256 mismatch: '+str(source))
    tool_lock=json.loads((root.parent/"deps/toolchain-lock.json").read_text())
    fingerprint.update(json.dumps([d for d in tool_lock["dependencies"] if d["name"] in ["cgltf","stb","tinyexr","mikktspace"]],sort_keys=True).encode())
    identity=fingerprint.hexdigest();args.output.mkdir(parents=True,exist_ok=True)
    lock=args.output/'cook.lock'
    deadline=time.monotonic()+1800
    while True:
        try:lock.mkdir();break
        except FileExistsError:
            if time.monotonic()>deadline:raise RuntimeError('Timed out waiting for '+str(lock)+'; remove it only if no cooker is running')
            time.sleep(.5)
    try:
        stamp=args.output/'cook.stamp';current=json.loads(stamp.read_text()) if stamp.exists() else {}
        if current.get('identity')==identity and all((args.output/p).exists() and sha(args.output/p)==digest for p,digest in current.get('outputs',{}).items()) and current.get('outputs'):
            print('Reusing verified offline cooked assets');os.utime(stamp,None);return
        staging=args.output/'staging';shutil.rmtree(staging,ignore_errors=True);staging.mkdir()
        for model in ['bolt_action_rifle_7_62','fishermans_hat']:
            subprocess.run([str(args.cooker),'model',str(args.source/'models'/model/(model+'_2k.gltf')),str(staging/(model+'.kgmodel'))],check=True)
        for environment in ['qwantani_night','industrial_pipe_and_valve_01']:
            subprocess.run([str(args.cooker),'environment',str(args.source/'environments'/(environment+'_2k.exr')),str(staging/(environment+'.kgenv'))],check=True)
        outputs={p.relative_to(staging).as_posix():sha(p) for p in sorted(staging.rglob('*')) if p.is_file()}
        for relative in outputs:
            dest=args.output/relative;dest.parent.mkdir(parents=True,exist_ok=True);os.replace(staging/relative,dest)
        shutil.rmtree(staging);temporary=args.output/'cook.stamp.part';temporary.write_text(json.dumps({'schema':1,'identity':identity,'outputs':outputs},indent=2)+'\n');os.replace(temporary,stamp)
    finally:lock.rmdir()
if __name__=='__main__':main()
