#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Hash-verified CC0 VDB acquisition and resumable, staged host conversion."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import zipfile
from volume_inputs import fetch, safe_relative, sha, lock_directory


def selected_frames(names):
    names = sorted((name for name in names if name.lower().endswith('.vdb')),
                   key=lambda name: int(re.search(r'(\d+)\.vdb$', name, re.I)[1]))
    if len(names) < 16:raise ValueError('Volume sequence must have at least sixteen frames')
    return [names[round((len(names)-1)*(.2+.7*i/15))] for i in range(16)]


def extract_frames(archive, output, container):
    output.mkdir(parents=True, exist_ok=True)
    if container == 'zip':
        with zipfile.ZipFile(archive) as source:
            names = selected_frames(source.namelist())
            for name in names:
                safe_relative(name)
                destination = output/name
                destination.parent.mkdir(parents=True, exist_ok=True)
                with source.open(name) as incoming, destination.open('wb') as outgoing:
                    shutil.copyfileobj(incoming, outgoing)
    else:
        tar = shutil.which('bsdtar')
        if not tar:raise RuntimeError('Simple Fire RAR requires bsdtar (libarchive); Blender is not used')
        names = selected_frames(subprocess.check_output([tar, '-tf', str(archive)], text=True).splitlines())
        for name in names:
            safe_relative(name)
            # Extract bytes through stdout instead of honoring archive links.
            destination = output/name
            destination.parent.mkdir(parents=True, exist_ok=True)
            with destination.open('wb') as outgoing:
                subprocess.run([tar, '-xOf', str(archive), name], stdout=outgoing, check=True)
    return [output/name for name in names]


def _cook(root, source, output, converter=None, offline=False, host_build=None):
    root, source, output = map(lambda p: Path(p).resolve(), (root, source, output))
    manifest_path = root/'assets/volume-manifest.json'
    manifest = json.loads(manifest_path.read_text())
    fingerprint = hashlib.sha256(manifest_path.read_bytes())
    for name in ('volumes/volume_converter.cpp', 'volumes/CMakeLists.txt', 'cook_volumes.py',
                 'volume_inputs.py', 'build_volume_converter.py'):
        fingerprint.update((root/'tools'/name).read_bytes())
    fingerprint.update((root/'deps/volume-toolchain-lock.json').read_bytes())
    identity = fingerprint.hexdigest()
    destination = output/'effects/volumes'
    stamp = destination/'cook.stamp'
    current = json.loads(stamp.read_text()) if stamp.exists() else {}
    if current.get('identity') == identity and current.get('outputs') and all(
            (destination/name).is_file() and sha(destination/name) == digest
            for name, digest in current['outputs'].items()):
        print('Reusing verified offline VDB volumes')
        return
    for entry in manifest['files']:
        safe_relative(entry['file'])
        fetch(entry['source'], entry['sha256'], source/entry['file'], offline, mediafire=True)
    if converter is None:
        import platform
        from build_volume_converter import build
        host_platform = 'macos' if platform.system() == 'Darwin' else platform.system().lower()
        converter = build(root, host_build or root/'build'/(host_platform+'-'+platform.machine().lower())/'host-volumes', offline)
    destination.mkdir(parents=True, exist_ok=True)
    staging = destination/'staging'
    if staging.exists():shutil.rmtree(staging)
    staging.mkdir()
    for entry in manifest['files']:
        frames = extract_frames(source/entry['file'], source/'volumes'/entry['asset'], entry['format'])
        subprocess.run([str(converter), str(staging/entry['asset']), *map(str, frames)], check=True)
    outputs = {path.relative_to(staging).as_posix(): sha(path) for path in sorted(staging.rglob('*')) if path.is_file()}
    resident = sum(json.loads((staging/entry['asset']/'volume.json').read_text())['resident_bytes'] for entry in manifest['files'])
    if resident > 128*1024*1024:raise RuntimeError('Cooked VDB effects exceed the resident texture budget')
    for name in outputs:
        target = destination/name
        target.parent.mkdir(parents=True, exist_ok=True)
        (staging/name).replace(target)
    shutil.rmtree(staging)
    temporary = stamp.with_suffix('.part')
    temporary.write_text(json.dumps({'schema': 1, 'identity': identity, 'resident_bytes': resident, 'outputs': outputs}, indent=2)+'\n')
    temporary.replace(stamp)


def cook(root, source, output, converter=None, offline=False, host_build=None):
    with lock_directory(Path(output)/'effects/volumes/cook.lock'):
        _cook(root, source, output, converter, offline, host_build)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--source', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--converter', type=Path)
    parser.add_argument('--host-build', type=Path)
    parser.add_argument('--offline', action='store_true')
    args = parser.parse_args()
    cook(args.root, args.source or args.root/'assets/source', args.output or args.root/'assets/cooked', args.converter, args.offline, args.host_build)


if __name__ == '__main__':main()
