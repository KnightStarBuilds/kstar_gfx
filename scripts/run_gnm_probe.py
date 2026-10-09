#!/usr/bin/env python3
"""Run one owned shadPS4 process with isolated profile and bounded GPU checks."""
import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import time
from pathlib import Path

FEATURES = (
    'CLEAR', 'TRIANGLE', 'TEXTURE', 'MSAA', 'CULL_BACK', 'CULL_FRONT',
    'CULL_FRONT_FRESH', 'FRONTFACE', 'INSTANCE_ATTRIBUTES_FIRST_VERTEX',
    'FP32_1X', 'FP32_4X', 'NARROW_READBACK', 'RECT_COPY',
    'DEPTH_VERTEX_INSTANCE', 'BLEND', 'CUBE_MIP_HDR', 'HDR_MSAA',
    'HDR_ALPHA_4X', 'HDR_ADDITIVE_4X', 'DEPTH_SAMPLE', 'DEPTH_COPY',
    'DISCARD_KILL', 'DISCARD_KEEP', 'ATTACHMENT_CONTRACT',
    'MIP_TARGET_BASE', 'MIP_TARGET_SAMPLE',
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--emulator', type=Path, required=True)
    p.add_argument('--elf', type=Path, required=True)
    p.add_argument('--sdk', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--shaders', type=Path, required=True)
    p.add_argument('--timeout', type=float, default=40)
    scope = p.add_mutually_exclusive_group()
    scope.add_argument('--present', action='store_true',
                   help='Check native VideoOut flips and display-buffer color encoding')
    scope.add_argument('--discard-only', action='store_true',
                   help='Isolate native fragment discard and retained depth checks')
    p.add_argument('--copy-gpu-buffers', action='store_true',
                   help='Enable emulator command-buffer copying for a controlled diagnostic')
    a = p.parse_args()
    emu, elf, sdk, out = (path.resolve() for path in
                          (a.emulator, a.elf, a.sdk, a.output))
    if not emu.is_file() or not elf.is_file() or not (sdk / 'lib/crt1.o').is_file():
        p.error('Missing emulator, executable or SDK')
    if out.exists():
        p.error('Output must be new so previous evidence is preserved')
    if not a.shaders.is_dir() or not list(a.shaders.glob('*.sb')):
        p.error('Pass the compiled native conformance shader directory')
    if a.timeout <= 0:
        p.error('Timeout must be positive')
    converters = {'Darwin': 'bin/macos/create-fself-macos',
                  'Linux': 'bin/linux/create-fself',
                  'Windows': 'bin/windows/create-fself.exe'}
    if platform.system() not in converters:
        p.error('Unsupported host platform')
    converter = sdk / converters[platform.system()]
    if not converter.is_file():
        p.error('Missing SDK executable converter')
    out.mkdir(parents=True)
    app = out / 'app0'
    (app / 'sce_module').mkdir(parents=True)
    for name in ('libc.prx', 'libSceFios2.prx'):
        shutil.copyfile(sdk / 'samples/hello_world/sce_module' / name,
                        app / 'sce_module' / name)
    env = os.environ.copy()
    env['OO_PS4_TOOLCHAIN'] = str(sdk)
    subprocess.run([str(converter), '-in=' + str(elf),
                    '-out=' + str(app / 'probe.oelf'), '--eboot',
                    str(app / 'eboot.bin'), '--paid', '0x3800000000000011'],
                   env=env, check=True)
    for file in a.shaders.glob('*.sb'):
        shutil.copyfile(file, app / file.name)
    profile = out / 'user'
    (profile / 'data/captures').mkdir(parents=True)
    home = profile / 'home'
    for uid in range(1000, 1004):
        for sub in ('savedata', 'trophy', 'inputs'):
            (home / str(uid) / sub).mkdir(parents=True)
    config = {
        'General': {'home_dir': str(home), 'show_splash': False,
                    'enable_upnp': False, 'connected_to_network': False},
        'GPU': {'dump_shaders': True,
                'readback_linear_images_enabled': True, 'readbacks_mode': 2,
                'null_gpu': False, 'rcas_enabled': False},
        'Debug': {'debug_dump': True},
    }
    if a.copy_gpu_buffers:
        config['GPU']['copy_gpu_buffers'] = True
    (profile / 'config.json').write_text(json.dumps(config, indent=2))
    command = [str(emu), '-g', str(app), '--', 'kgfx_gnm_probe']
    if a.present:
        command.append('--present')
    elif a.discard_only:
        command.append('--discard-only')
    result = {
        'scope': ('GNM shader rendering, descriptors, vertex and instance fetch, '
                  'culling/front face, depth, blending, HDR, MSAA, copies and GPU readback'),
        'emulator': str(emu), 'emulator_sha256': sha(emu),
        'elf_sha256': sha(elf), 'command': command,
        'timeout_seconds': a.timeout, 'emulator_config': config,
        'shaders_sha256': {f.name: sha(f) for f in sorted(app.glob('*.sb'))},
        'driver_files_sha256': {},
    }
    for file in sorted(emu.parent.glob('*icd*.json')):
        result['driver_files_sha256'][str(file)] = sha(file)
        try:
            library = Path(json.loads(file.read_text())['ICD']['library_path'])
            if not library.is_absolute():
                library = file.parent / library
            if library.is_file():
                result['driver_files_sha256'][str(library.resolve())] = sha(library)
        except (KeyError, ValueError):
            pass
    begin = time.monotonic()
    with (out / 'emulator.log').open('wb') as log:
        proc = subprocess.Popen(command, cwd=out, env=env, stdout=log,
                                stderr=subprocess.STDOUT, start_new_session=True)
        result['pid'] = proc.pid
        try:
            result['returncode'] = proc.wait(timeout=a.timeout)
        except subprocess.TimeoutExpired:
            result['timed_out'] = True
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
            result['returncode'] = proc.returncode
    result['elapsed_seconds'] = time.monotonic() - begin
    text = (out / 'emulator.log').read_text(errors='replace')
    if (profile / 'log').exists():
        for file in (profile / 'log').glob('*'):
            if file.is_file():
                text += file.read_text(errors='replace')
    result['reported_driver'] = sorted(set(
        re.sub(r'\x1b\[[0-9;]*m', '', line) for line in text.splitlines()
        if 'GPU_Vulkan_Driver:' in line))
    checks = {}
    expected_features = (('PRESENTATION',) if a.present else
                         ('DISCARD_KILL', 'DISCARD_KEEP') if a.discard_only else FEATURES)
    for feature in expected_features:
        matches = re.findall('KGFX_GPU_' + feature + r'_RESULT=(PASS|FAIL)', text)
        checks[feature] = bool(matches) and all(value == 'PASS' for value in matches)
    result['checks'] = checks
    if a.present:
        result['scope'] = 'Native VideoOut flip completion and display-buffer color encoding'
        flips = re.findall(r'GNM_PRESENT submitted=(\d+) completed=(\d+)', text)
        checks['FLIP_COMPLETION'] = bool(flips) and all(
            int(submitted) == 600 and int(completed) == 600 for submitted, completed in flips)
    elif a.discard_only:
        result['scope'] = 'Native fragment discard and retained depth checks'
    else:
        for name in ('clear', 'triangle', 'texture', 'msaa'):
            result['gpu_' + name + '_pass'] = checks[name.upper()]
    result['gpu_features_pass'] = all(checks.values())
    result['status'] = 'passed' if (all(checks.values()) and
        not result.get('timed_out') and result['returncode'] == 0) else 'failed'
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0 if result['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
