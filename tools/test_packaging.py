#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Test resource refresh, verified preflight, rollback and owned-file removal."""
import hashlib, json, pathlib, subprocess, sys, tempfile
root = pathlib.Path(__file__).resolve().parents[1]
(root / '.tmp').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='package paths ', dir=root / '.tmp') as directory:
    temp = pathlib.Path(directory)
    assets, shaders, out = temp / 'cooked', temp / 'shaders', temp / 'app/resources'
    assets.mkdir(); shaders.mkdir()
    (assets / 'probe.kgmodel').write_bytes(b'asset0')
    (assets / 'second.kgtex').write_bytes(b'asset2')
    shader = shaders / 'probe.vert.glsl'
    shader.write_text('#version 410\nvoid main(){}\n')
    metadata = shaders / 'probe.vert.package.json'
    def stamp():
        (assets / 'cook.stamp').write_text(json.dumps({'identity': 'test', 'outputs': {
            name: hashlib.sha256((assets / name).read_bytes()).hexdigest()
            for name in ['probe.kgmodel', 'second.kgtex']}}))
    def shader_stamp():
        metadata.write_text(json.dumps({'schema': 1, 'artifacts': {
            shader.name: hashlib.sha256(shader.read_bytes()).hexdigest()}}))
    def package(success=True):
        result = subprocess.run([sys.executable, str(root / 'tools/package_resources.py'),
            '--root', str(root), '--assets', str(assets), '--shaders', str(shaders),
            '--destination', str(out)], capture_output=True, text=True)
        assert (result.returncode == 0) == success, result.stdout + result.stderr
    stamp(); shader_stamp(); package()
    target = out / 'assets/cooked/probe.kgmodel'
    first = target.stat().st_mtime_ns
    package(); assert first == target.stat().st_mtime_ns
    assert (out / 'notices/NOTICE.txt').is_file()
    assert (out / 'notices/assets-manifest.json').is_file()
    # A later corrupt payload must not replace an earlier valid changed one.
    (assets / 'probe.kgmodel').write_bytes(b'asset1'); stamp()
    (assets / 'second.kgtex').write_bytes(b'corrupt')
    package(False); assert target.read_bytes() == b'asset0'
    (assets / 'second.kgtex').write_bytes(b'asset2'); stamp()
    original_shader = shader.read_bytes(); shader.write_bytes(b'corrupt shader')
    package(False); assert target.read_bytes() == b'asset0'
    shader.write_bytes(original_shader); package(); assert target.read_bytes() == b'asset1'
    # Metadata is generated, but corrupted paths must never escape ownership.
    original_metadata = metadata.read_bytes()
    metadata.write_text(json.dumps({'schema': 1, 'artifacts': {'../outside': '0' * 64}}))
    package(False); metadata.write_bytes(original_metadata)
    metadata.unlink(); package(False); shader_stamp()
    (out / 'user-file.txt').write_text('retained')
    shader.unlink(); metadata.unlink(); package()
    assert not (out / 'shaders/probe.vert.glsl').exists()
    assert (out / 'user-file.txt').exists()
print('Packaging passed: verified asset/shader preflight, rollback, unchanged timestamps, path containment, owned cleanup and notices')
