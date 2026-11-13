#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Offline cooking keyed by source hashes and exact cooking implementation.

The persistent cache survives build-tree cleaning. Cross-process lock and staged
publication keep interrupted cooks from replacing previously usable packages.
"""
import argparse,hashlib,json,os,pathlib,shutil,subprocess,tempfile,time
from authored_effects import generate as generate_effects
from lighting_lookup import generate as generate_lighting_lookup
from cook_lock import lock_directory

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--cooker',required=True,type=pathlib.Path);parser.add_argument('--source',required=True,type=pathlib.Path);parser.add_argument('--output',required=True,type=pathlib.Path);parser.add_argument('--manifest',required=True,type=pathlib.Path);args=parser.parse_args()
    manifest=json.loads(args.manifest.read_text());root=pathlib.Path(__file__).resolve().parent
    fingerprint=hashlib.sha256(args.manifest.read_bytes())
    for file in [root/'asset_cooker.cpp',root/'include/kgfx_asset_format.h',root/'cook_assets.py',root/'authored_effects.py',root/'lighting_lookup.py']:
        fingerprint.update(file.read_bytes())
    for entry in manifest['files']:
        source=args.source/entry['file']
        if sha(source)!=entry['sha256']:raise RuntimeError('Source SHA256 mismatch: '+str(source))
    tool_lock=json.loads((root.parent/"deps/toolchain-lock.json").read_text())
    fingerprint.update(json.dumps([d for d in tool_lock["dependencies"] if d["name"] in ["cgltf","stb","tinyexr","mikktspace"]],sort_keys=True).encode())
    identity=fingerprint.hexdigest();args.output.mkdir(parents=True,exist_ok=True)
    with lock_directory(args.output/'cook.lock'):
        stamp=args.output/'cook.stamp';current=json.loads(stamp.read_text()) if stamp.exists() else {}
        if current.get('identity')==identity and all((args.output/p).exists() and sha(args.output/p)==digest for p,digest in current.get('outputs',{}).items()) and current.get('outputs'):
            print('Reusing verified offline cooked assets');os.utime(stamp,None);return
        staging=args.output/'staging';shutil.rmtree(staging,ignore_errors=True);staging.mkdir()
        for record in manifest['cook']:
            source=args.source/record['source'];destination=staging/record['output']
            command=[str(args.cooker),record['kind']]
            if record['kind']=='texture':command.append(record['usage'])
            subprocess.run(command+[str(source),str(destination)],check=True)
            if record.get('components'):
                nodes=json.loads(source.read_text())['nodes']
                for node in nodes:
                    if 'mesh' not in node:continue
                    name=node['name']
                    if not name.replace('_','').isalnum():raise RuntimeError('Unsafe component name: '+name)
                    subprocess.run([str(args.cooker),'model',str(source),str(staging/(name+'.kgmodel')),name],check=True)
        generate_effects(staging/'effects')
        generate_lighting_lookup(args.source,staging)
        outputs={p.relative_to(staging).as_posix():sha(p) for p in sorted(staging.rglob('*')) if p.is_file()}
        for relative in outputs:
            dest=args.output/relative;dest.parent.mkdir(parents=True,exist_ok=True);os.replace(staging/relative,dest)
        shutil.rmtree(staging);temporary=args.output/'cook.stamp.part';temporary.write_text(json.dumps({'schema':1,'identity':identity,'outputs':outputs},indent=2)+'\n');os.replace(temporary,stamp)
if __name__=='__main__':main()
