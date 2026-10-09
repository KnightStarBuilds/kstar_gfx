#!/usr/bin/env python3
"""Validated standalone desktop build/run entry points."""
import argparse
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
mode = sys.argv[1]
parser = argparse.ArgumentParser()
parser.add_argument('--config', choices=('Debug', 'Release'), default='Debug')
if mode == 'build':
    parser.add_argument('--clean', action='store_true')
    parser.add_argument('--run', action='store_true')
    parser.add_argument('--offline', action='store_true')
    parser.add_argument('--library-only', action='store_true')
    parser.add_argument('--jobs', type=int, default=min(os.cpu_count() or 4, 12))
    args = parser.parse_args(sys.argv[2:])
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if args.library_only and args.run:
        parser.error('--library-only cannot be combined with --run')
    remaining = []
else:
    raw = sys.argv[2:]
    boundary = raw.index('--') if '--' in raw else len(raw)
    args = parser.parse_args(raw[:boundary])
    remaining = raw[boundary + 1:]
system = {'Darwin': 'macos', 'Linux': 'linux', 'Windows': 'windows'}.get(platform.system())
if system is None:
    parser.error('unsupported host operating system')
arch = platform.machine().lower()
arch = {'aarch64': 'arm64', 'amd64': 'x86_64'}.get(arch, arch)
build = root / 'build' / f'{system}-{arch}'
exe = build / 'bin' / args.config / ('kstar_gfx_demo.exe' if system == 'windows' else 'kstar_gfx_demo')
if mode == 'build':
    for program in ('cmake', 'ninja'):
        if shutil.which(program) is None:
            parser.error(f'{program} is required')
    configure = ['cmake', '-S', str(root), '-B', str(build), '-G', 'Ninja Multi-Config',
                 '-DKSTAR_GFX_BACKEND=OPENGL',
                 f'-DKSTAR_GFX_OFFLINE={"ON" if args.offline else "OFF"}',
                 f'-DKSTAR_GFX_BUILD_DEMOS={"OFF" if args.library_only else "ON"}',
                 f'-DKSTAR_GFX_BUILD_TESTS={"OFF" if args.library_only else "ON"}']
    if system == 'macos':
        for language, compiler in [('C', 'clang'), ('CXX', 'clang++')]:
            path = subprocess.check_output(['xcrun', '--find', compiler], text=True).strip()
            configure.append(f'-DCMAKE_{language}_COMPILER={path}')
    # Validation above deliberately precedes deleting the selected output tree.
    if args.clean and build.exists():
        shutil.rmtree(build)
    subprocess.run(configure, check=True)
    subprocess.run(['cmake', '--build', str(build), '--config', args.config, '--parallel', str(args.jobs)], check=True)
    if not args.run:
        sys.exit(0)
if not exe.is_file():
    parser.error(f'demo is not built: {exe}; run ./build.sh --config={args.config}')
subprocess.run([str(exe), *remaining], check=True)
