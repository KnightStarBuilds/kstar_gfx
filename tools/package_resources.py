#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Refresh executable-relative resources, notices and provenance incrementally."""
import argparse,hashlib,json,os,pathlib,shutil
from fork_sources import identity as fork_identity

def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def copy(source,dest):
    if dest.exists() and dest.stat().st_size==source.stat().st_size and digest(dest)==digest(source):return
    dest.parent.mkdir(parents=True,exist_ok=True);temp=dest.with_name(dest.name+'.part');shutil.copyfile(source,temp);os.replace(temp,dest)
def put(dest,data):
    if dest.exists() and dest.read_text()==data:return
    dest.parent.mkdir(parents=True,exist_ok=True);temp=dest.with_name(dest.name+'.part');temp.write_text(data);os.replace(temp,dest)
def owned_path(root,name):
    path=pathlib.PurePosixPath(name)
    if not name or path.is_absolute() or '..' in path.parts or '\\' in name:
        raise RuntimeError('Package path is not a relative owned path: '+name)
    result=root/path
    if root.resolve() not in result.resolve().parents:
        raise RuntimeError('Package path escapes its directory: '+name)
    return result

def main():
    p=argparse.ArgumentParser();p.add_argument('--root',type=pathlib.Path,required=True);p.add_argument('--assets',type=pathlib.Path,required=True);p.add_argument('--shaders',type=pathlib.Path,required=True);p.add_argument('--destination',type=pathlib.Path,required=True);a=p.parse_args();index=[]
    stamp=json.loads((a.assets/'cook.stamp').read_text())
    # Validate all payloads before replacing any previously usable package file.
    assets=[]
    for name,expected in stamp['outputs'].items():
        source=owned_path(a.assets,name)
        if digest(source)!=expected:raise RuntimeError('Cooked asset checksum mismatch: '+name)
        assets.append((source,owned_path(a.destination,'assets/cooked/'+name)))
    verified=set()
    manifests=list(a.shaders.glob('*.package.json'))+list(a.shaders.glob('*.native-package.json'))
    for manifest_path in manifests:
        metadata=json.loads(manifest_path.read_text())
        if metadata.get('schema')!=1 or not metadata.get('artifacts'):
            raise RuntimeError('Invalid shader package metadata: '+str(manifest_path))
        for name,expected in metadata['artifacts'].items():
            source=owned_path(a.shaders,name)
            if not source.is_file() or digest(source)!=expected:
                raise RuntimeError('Shader artifact checksum mismatch: '+name)
            verified.add(name)
    for source in a.shaders.iterdir():
        if source.suffix in ['.glsl','.spv','.sb'] and not source.name.endswith('.gnm.glsl') and source.name not in verified:
            raise RuntimeError('Shader artifact has no verified package metadata: '+source.name)
    for source,target in assets:
        copy(source,target);index.append(target.relative_to(a.destination).as_posix())
    for source in sorted(a.shaders.iterdir()):
        if source.is_file() and source.suffix in ['.glsl','.spv','.json','.h','.sb'] and not source.name.endswith('.gnm.glsl'):
            target=a.destination/'shaders'/source.name;copy(source,target);index.append(target.relative_to(a.destination).as_posix())
    asset_manifest=json.loads((a.root/'assets/manifest.json').read_text());tool_lock=json.loads((a.root/'deps/toolchain-lock.json').read_text());runtime_lock=json.loads((a.root/'deps/lock.json').read_text());notices=a.destination/'notices'
    for name,source in [('assets-manifest.json',a.root/'assets/manifest.json'),('toolchain-lock.json',a.root/'deps/toolchain-lock.json'),('runtime-lock.json',a.root/'deps/lock.json'),('kstar_gfx-LICENSE.txt',a.root/'LICENSE')]:copy(source,notices/name)
    summary=['kstar_gfx demo resources','', 'Curated assets are CC0-1.0: https://polyhaven.com/license','']
    seen=set()
    for entry in asset_manifest['files']:
        if entry['asset'] not in seen:
            summary += [entry['asset']+' — '+entry['author'],entry['asset_page'],''];seen.add(entry['asset'])
    summary += ['The source assets remain unchanged; runtime packages contain derived geometry, mipmaps and image-based lighting.','The rifle glass alpha is restored from its separate source alpha texture.','','Dependency notices (offline tools are included for provenance):','']
    for entry in tool_lock['dependencies']+runtime_lock['dependencies']:
        name=entry['name'];revision=entry.get('revision',entry.get('version'));source=(a.root/entry['path']) if entry.get('kind')=='submodule' else a.root/'deps/cache/sources'/(name+'-'+revision)
        if entry.get('kind')=='submodule' and source.exists():
            info=fork_identity(name,a.root);revision=info['revision']+' tree '+info['tree_sha256']
            put(notices/(name+'-source.json'),json.dumps(info,indent=2)+'\n')
        # Dependencies omitted by a particular backend are not part of this package.
        if not source.exists():continue
        licenses=entry.get('licenses',[{'source':f,'destination':name+'-'+pathlib.Path(f).name} for f in entry.get('license_files',[])])
        if not licenses:raise RuntimeError('No license record for '+name)
        summary += [name+' '+revision,entry.get('repository',entry.get('url')),'']
        for record in licenses:
            path=source/record['source']
            if not path.is_file():raise RuntimeError('Missing dependency license '+str(path))
            copy(path,notices/record['destination'])
    put(notices/'NOTICE.txt','\n'.join(summary)+'\n')
    # Only remove files recorded by our previous package; retain user additions.
    previous=a.destination/'package-index.json'
    if previous.exists():
        for name in json.loads(previous.read_text()).get('files',[]):
            path=pathlib.PurePosixPath(name)
            if name not in index and '..' not in path.parts and not path.is_absolute():
                target=a.destination/name
                if target.is_file():target.unlink()
    put(previous,json.dumps({'schema':1,'cook_identity':stamp['identity'],'files':sorted(index)},indent=2)+'\n')
if __name__=='__main__':main()
