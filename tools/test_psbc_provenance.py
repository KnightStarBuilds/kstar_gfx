#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Reject stale compiler binaries, fork source identities and dependency pins."""
import argparse, hashlib, json, pathlib, tempfile
from package_gnm_shader import ROOT, verify_compiler_provenance


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('compiler', type=pathlib.Path)
    args = parser.parse_args()
    verify_compiler_provenance(args.compiler)
    original = json.loads((args.compiler.resolve().parent / 'build-provenance.json').read_text())
    with tempfile.TemporaryDirectory(prefix='psbc-provenance-', dir=ROOT / '.tmp') as directory:
        root = pathlib.Path(directory)
        binary = root / 'opengnm-psbc'
        binary.write_bytes(b'provenance relationship fixture')
        original['compiler_sha256'] = hashlib.sha256(binary.read_bytes()).hexdigest()
        manifest = root / 'build-provenance.json'
        manifest.write_text(json.dumps(original))
        verify_compiler_provenance(binary)
        for field in ('compiler_sha256', 'forks', 'dependencies', 'build_driver_sha256'):
            altered = dict(original)
            altered[field] = [] if field in ('dependencies', 'forks') else '0' * 64
            manifest.write_text(json.dumps(altered))
            try:
                verify_compiler_provenance(binary)
            except RuntimeError:
                pass
            else:
                raise AssertionError('Accepted stale ' + field)
        manifest.write_text('[]')
        try:
            verify_compiler_provenance(binary)
        except RuntimeError:
            pass
        else:
            raise AssertionError('Accepted malformed provenance')
    print('PSBC compiler provenance and stale-input rejection passed')


if __name__ == '__main__':
    main()
