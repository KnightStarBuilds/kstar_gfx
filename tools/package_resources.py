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
def remove_legacy_cook_stamp(destination,source_stamp):
    # Older resource copies included this build-only stamp without indexing it.
    # Recognize that exact generated format; preserve user files and the source
    # cooker cache, including packages that link back to a source directory.
    target=destination/'assets/cooked/cook.stamp'
    if not target.is_file() or target.is_symlink() or target.resolve()==source_stamp.resolve():return
    if destination.resolve() not in target.resolve().parents:return
    try:metadata=json.loads(target.read_text())
    except (OSError,UnicodeError,json.JSONDecodeError):return
    if not isinstance(metadata,dict):return
    identity=metadata.get('identity')
    if metadata.get('schema')==1 and isinstance(identity,str) and len(identity)==64 and all(c in '0123456789abcdefABCDEF' for c in identity) and isinstance(metadata.get('outputs'),dict):
        target.unlink()

def main():
    p=argparse.ArgumentParser();p.add_argument('--root',type=pathlib.Path,required=True);p.add_argument('--assets',type=pathlib.Path,required=True);p.add_argument('--shaders',type=pathlib.Path,required=True);p.add_argument('--destination',type=pathlib.Path,required=True);a=p.parse_args();index=[]
    stamp=json.loads((a.assets/'cook.stamp').read_text())
    # Validate all payloads before replacing any previously usable package file.
    assets=[]
    for name,expected in stamp['outputs'].items():
        source=owned_path(a.assets,name)
        if digest(source)!=expected:raise RuntimeError('Cooked asset checksum mismatch: '+name)
        assets.append((source,owned_path(a.destination,'assets/cooked/'+name)))
    volume_stamp_path=a.assets/'effects/volumes/cook.stamp'
    volume_stamp=json.loads(volume_stamp_path.read_text()) if volume_stamp_path.exists() else None
    if volume_stamp:
        if volume_stamp.get('schema')!=1 or not volume_stamp.get('outputs'):
            raise RuntimeError('Invalid volume cook stamp')
        for name,expected in volume_stamp['outputs'].items():
            source=owned_path(a.assets/'effects/volumes',name)
            if digest(source)!=expected:raise RuntimeError('Cooked volume checksum mismatch: '+name)
            assets.append((source,owned_path(a.destination,'assets/cooked/effects/volumes/'+name)))
    lighting_stamp_path=a.assets/'lighting/cook.stamp'
    lighting_stamp=json.loads(lighting_stamp_path.read_text()) if lighting_stamp_path.exists() else None
    if lighting_stamp:
        if lighting_stamp.get('schema')!=1 or not lighting_stamp.get('outputs'):
            raise RuntimeError('Invalid lighting cook stamp')
        for name,expected in lighting_stamp['outputs'].items():
            source=owned_path(a.assets,name)
            if digest(source)!=expected:raise RuntimeError('Cooked lighting checksum mismatch: '+name)
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
        if source.suffix in ['.glsl','.spv','.sb'] and not source.name.endswith(('.gnm.glsl','.expanded.glsl')) and source.name not in verified:
            raise RuntimeError('Shader artifact has no verified package metadata: '+source.name)
    catalog=a.root/'assets/scenes.json'
    if catalog.exists():
        copy(catalog,a.destination/'assets/scenes.json');index.append('assets/scenes.json')
    for source,target in assets:
        copy(source,target);index.append(target.relative_to(a.destination).as_posix())
    for source in sorted(a.shaders.iterdir()):
        if source.is_file() and source.suffix in ['.glsl','.spv','.json','.h','.sb'] and not source.name.endswith(('.gnm.glsl','.expanded.glsl')):
            target=a.destination/'shaders'/source.name;copy(source,target);index.append(target.relative_to(a.destination).as_posix())
    asset_manifest=json.loads((a.root/'assets/manifest.json').read_text());tool_lock=json.loads((a.root/'deps/toolchain-lock.json').read_text());runtime_lock=json.loads((a.root/'deps/lock.json').read_text());notices=a.destination/'notices'
    for name,source in [('assets-manifest.json',a.root/'assets/manifest.json'),('toolchain-lock.json',a.root/'deps/toolchain-lock.json'),('runtime-lock.json',a.root/'deps/lock.json'),('kstar_gfx-LICENSE.txt',a.root/'LICENSE')]:copy(source,notices/name)
    summary=['kstar_gfx demo resources','', 'Poly Haven assets are CC0-1.0: https://polyhaven.com/license; lighting tables retain their own license.','']
    seen=set()
    for entry in asset_manifest['files']:
        if entry['asset'] not in seen:
            summary += [entry['asset']+' — '+entry['author'],entry['asset_page'],entry['license']+' — '+entry.get('license_url',''),''];seen.add(entry['asset'])
    summary += ['Original generated effect artwork: tools/authored_effects.py (MIT).','','The source assets remain unchanged; runtime packages contain derived geometry, mipmaps and image-based lighting.','The rifle glass alpha is restored from its separate source alpha texture.','','Dependency notices (offline tools are included for provenance):','']
    if volume_stamp:
        volume_manifest=json.loads((a.root/'assets/volume-manifest.json').read_text())
        volume_lock=json.loads((a.root/'deps/volume-toolchain-lock.json').read_text())
        copy(a.root/'assets/volume-manifest.json',notices/'volume-manifest.json')
        copy(a.root/'deps/volume-toolchain-lock.json',notices/'volume-toolchain-lock.json')
        summary += ['JangaFX EmberGen VDB simulations: CC0-1.0.','https://jangafx.com/software/embergen/download/free-vdb-animations','']
        for entry in volume_manifest['files']:
            summary += [entry['asset']+' — '+entry['author'],entry['source'],'']
        for entry in volume_lock['dependencies']:
            summary += [entry['name']+' '+entry['revision']+' (host converter only)',entry['repository'],'']
            for record in entry['licenses']:
                source=owned_path(a.root,record['record'])
                if digest(source)!=record['sha256']:raise RuntimeError('Volume dependency license checksum mismatch')
                copy(source,notices/source.name)
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
    remove_legacy_cook_stamp(a.destination,a.assets/'cook.stamp')
    # Only remove files recorded by our previous package; retain user additions.
    previous=a.destination/'package-index.json'
    if previous.exists():
        for name in json.loads(previous.read_text()).get('files',[]):
            path=pathlib.PurePosixPath(name)
            if name not in index and '..' not in path.parts and not path.is_absolute():
                target=a.destination/name
                if target.is_file():target.unlink()
    package_index={'schema':1,'cook_identity':stamp['identity'],'files':sorted(set(index))}
    if volume_stamp:package_index['volume_cook_identity']=volume_stamp['identity']
    if lighting_stamp:package_index['lighting_cook_identity']=lighting_stamp['identity']
    put(previous,json.dumps(package_index,indent=2)+'\n')
if __name__=='__main__':main()
