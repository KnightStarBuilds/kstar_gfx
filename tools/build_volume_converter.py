#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build pinned OpenVDB on the host, isolating generated source/build copies.

No Blender, system package installation, or target runtime dependency is used.
Ninja Multi-Config is required. Cached archives survive selected-tree cleaning.
"""
import argparse
import json
from pathlib import Path
import platform
import shutil
import subprocess
from volume_inputs import fetch, extract_tar, sha


def build(root, build_root, offline=False, config='Release', jobs=3):
    root, build_root = Path(root).resolve(), Path(build_root).resolve()
    lock = json.loads((root/'deps/volume-toolchain-lock.json').read_text())
    install = build_root/'install'
    common = ['-G', 'Ninja Multi-Config', '-DCMAKE_CXX_STANDARD=23', '-DCMAKE_CXX_EXTENSIONS=OFF',
              '-DCMAKE_INSTALL_PREFIX='+str(install), '-DCMAKE_PREFIX_PATH='+str(install)]
    if platform.system() == 'Darwin':
        for language, compiler in (('C', 'clang'), ('CXX', 'clang++')):
            path = subprocess.check_output(['xcrun', '--find', compiler], text=True).strip()
            common.append('-DCMAKE_'+language+'_COMPILER='+path)
    options = {
        'onetbb': ['-DTBB_TEST=OFF', '-DTBB_STRICT=OFF', '-DTBBMALLOC_BUILD=OFF'],
        'blosc': ['-DCMAKE_POLICY_VERSION_MINIMUM=3.5', '-DBUILD_TESTS=OFF', '-DBUILD_BENCHMARKS=OFF',
                  '-DBUILD_FUZZERS=OFF', '-DBUILD_SHARED=OFF'],
        'openvdb': ['-DOPENVDB_BUILD_BINARIES=OFF', '-DOPENVDB_CORE_SHARED=ON', '-DOPENVDB_CORE_STATIC=OFF',
                    '-DOPENVDB_USE_DELAYED_LOADING=OFF', '-DUSE_IMATH_HALF=OFF',
                    '-DUSE_EXPLICIT_INSTANTIATION=OFF', '-DUSE_BLOSC=ON'],
    }
    if platform.machine().lower() in ('arm64', 'aarch64'):
        options['blosc'] += ['-DDEACTIVATE_AVX2=ON', '-DDEACTIVATE_SSE2=ON']
    records = {item['name']: item for item in lock['dependencies']}
    for name in ('onetbb', 'blosc', 'openvdb'):
        entry = records[name]
        cache = root/'deps/cache/volume-archives'
        archive = cache/(name+'-'+entry['revision']+'.tar.gz')
        fetch(entry['url'], entry['sha256'], archive, offline)
        immutable = root/'deps/cache/volume-sources'/(name+'-'+entry['sha256'][:16])
        marker = immutable/'.verified'
        if not marker.exists():
            staging = root/'deps/cache/volume-sources'/(name+'.extract')
            if staging.exists():shutil.rmtree(staging)
            extracted = extract_tar(archive, staging)
            if immutable.exists():shutil.rmtree(immutable)
            extracted.rename(immutable)
            marker.write_text(entry['sha256']+'\n')
            shutil.rmtree(staging)
        if marker.read_text().strip() != entry['sha256']:
            raise RuntimeError('Immutable source pin changed: '+str(immutable))
        source = build_root/'sources'/name
        copy_marker = source/'.verified'
        if not copy_marker.exists():
            if source.exists():shutil.rmtree(source)
            shutil.copytree(immutable, source)
        if copy_marker.read_text().strip() != entry['sha256']:
            raise RuntimeError('Clean the selected converter build tree after a pin change')
        directory = build_root/name
        subprocess.run(['cmake', '-S', str(source), '-B', str(directory), *common, *options[name]], check=True)
        subprocess.run(['cmake', '--build', str(directory), '--config', config, '-j', str(jobs)], check=True)
        subprocess.run(['cmake', '--install', str(directory), '--config', config], check=True)
    converter = build_root/'converter'
    subprocess.run(['cmake', '-S', str(root/'tools/volumes'), '-B', str(converter), *common,
                    '-DVOLUME_DEPENDENCY_PREFIX='+str(install)], check=True)
    subprocess.run(['cmake', '--build', str(converter), '--config', config, '-j', str(jobs)], check=True)
    binary = converter/config/('kgfx_volume_converter.exe' if platform.system() == 'Windows' else 'kgfx_volume_converter')
    return binary


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--build', type=Path)
    parser.add_argument('--offline', action='store_true')
    parser.add_argument('--config', choices=('Debug', 'Release'), default='Release')
    parser.add_argument('--jobs', type=int, default=3)
    args = parser.parse_args()
    host_platform = 'macos' if platform.system() == 'Darwin' else platform.system().lower()
    build_root = args.build or args.root/'build'/(host_platform+'-'+platform.machine().lower())/'host-volumes'
    print(build(args.root, build_root, args.offline, args.config, args.jobs))


if __name__ == '__main__':main()
