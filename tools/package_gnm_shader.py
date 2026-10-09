#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build native PS4 shader packages, retaining fast FP16 and precise FP32 exports."""
import argparse, hashlib, json, os, pathlib, struct, subprocess, tempfile
from validate_gnm_shader import validate
from fork_sources import FORKS, identity

ROOT = pathlib.Path(__file__).resolve().parents[1]


def verify_compiler_provenance(psbc):
    """Local fork edits must never silently relabel an older compiler binary."""
    manifest_path = psbc.resolve().parent / 'build-provenance.json'
    try:
        provenance = json.loads(manifest_path.read_text())
        compiler_hash = hashlib.sha256(psbc.read_bytes()).hexdigest()
        locked = {item['name']: item for item in json.loads((ROOT / 'deps/toolchain-lock.json').read_text())['dependencies']}
        dependencies = provenance['dependencies']
        required = {'vulkan-headers-psbc', 'spirv-headers', 'mako', 'markupsafe', 'pyyaml'}
        valid = (provenance['schema'] == 2 and provenance['compiler_sha256'] == compiler_hash
                 and provenance['forks'] == {name: identity(name) for name in FORKS}
                 and provenance['build_driver_sha256'] == hashlib.sha256((ROOT / 'tools/build_psbc.py').read_bytes()).hexdigest()
                 and {item['name'] for item in dependencies} == required
                 and all(locked.get(item['name']) == item for item in dependencies))
    except (OSError, ValueError, KeyError, TypeError):
        valid = False
    if not valid:
        raise RuntimeError('PSBC provenance does not match its binary, editable fork sources and locked inputs; rebuild it with tools/build_psbc.py')


def compile_package(psbc, source, stage, output):
    verify_compiler_provenance(psbc)
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='native-package-', dir=output.parent) as directory:
        first = pathlib.Path(directory) / 'fp16.sb'
        command = [str(psbc.resolve()), '-4', '-s', stage, '-f', str(source.resolve())]
        subprocess.run(command + ['-o', str(first)], check=True)
        data = first.read_bytes()
        validate(data)
        if stage == 'fragment':
            second = pathlib.Path(directory) / 'fp32.sb'
            subprocess.run(command + ['--color-fp32', '-o', str(second)], check=True)
            precise = second.read_bytes()
            validate(precise)
            data += precise + struct.pack('<4sIII', b'KGFX', 1, len(data), len(precise))
        validate(data)
        first.write_bytes(data)
        os.replace(first, output)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--psbc', type=pathlib.Path, required=True)
    p.add_argument('--input', type=pathlib.Path, required=True)
    p.add_argument('--output', type=pathlib.Path, required=True)
    p.add_argument('--stage', choices=['vertex', 'fragment'], required=True)
    a = p.parse_args()
    compile_package(a.psbc, a.input, a.stage, a.output)


if __name__ == '__main__':
    main()
