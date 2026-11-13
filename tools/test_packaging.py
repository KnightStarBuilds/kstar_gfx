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
        (assets / 'cook.stamp').write_text(json.dumps({'schema': 1, 'identity': hashlib.sha256(b'test').hexdigest(), 'outputs': {
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
    volumes=assets/'effects/volumes'
    (volumes/'small_campfire').mkdir(parents=True)
    (volumes/'small_campfire/volume.kgtex').write_bytes(b'volume0')
    lighting=assets/'lighting';lighting.mkdir()
    (lighting/'industrial.kglight').write_bytes(b'baked lighting')
    (lighting/'cook.stamp').write_text(json.dumps({'schema':1,'identity':hashlib.sha256(b'bake').hexdigest(),
        'outputs':{'lighting/industrial.kglight':hashlib.sha256(b'baked lighting').hexdigest()}}))
    package()
    assert (out/'assets/cooked/lighting/industrial.kglight').read_bytes()==b'baked lighting'
    assert 'lighting_cook_identity' in json.loads((out/'package-index.json').read_text())
    (lighting/'industrial.kglight').write_bytes(b'corrupt bake');package(False)
    assert (out/'assets/cooked/lighting/industrial.kglight').read_bytes()==b'baked lighting'
    (lighting/'industrial.kglight').write_bytes(b'baked lighting')
    (volumes/'cook.stamp').write_text(json.dumps({'schema':1,'identity':hashlib.sha256(b'vdb').hexdigest(),
        'outputs':{'small_campfire/volume.kgtex':hashlib.sha256(b'volume0').hexdigest()}}))
    package()
    volume_target=out/'assets/cooked/effects/volumes/small_campfire/volume.kgtex'
    assert volume_target.read_bytes()==b'volume0'
    assert (out/'notices/volume-manifest.json').is_file()
    assert (out/'notices/openvdb.txt').is_file()
    assert 'volume_cook_identity' in json.loads((out/'package-index.json').read_text())
    (volumes/'small_campfire/volume.kgtex').write_bytes(b'broken volume')
    package(False)
    assert volume_target.read_bytes()==b'volume0','Corrupt volume must not replace a packaged payload'
    (volumes/'small_campfire/volume.kgtex').write_bytes(b'volume0')
    target = out / 'assets/cooked/probe.kgmodel'
    first = target.stat().st_mtime_ns
    package(); assert first == target.stat().st_mtime_ns
    assert (out / 'notices/NOTICE.txt').is_file()
    assert (out / 'notices/assets-manifest.json').is_file()
    # Legacy directory copies included an unindexed build-only stamp. Migrate
    # only its recognized generated format, retaining source cache/user data.
    source_stamp=assets / 'cook.stamp'
    source_bytes=source_stamp.read_bytes()
    runtime_stamp=out / 'assets/cooked/cook.stamp'
    runtime_stamp.write_bytes(source_bytes)
    notes=out / 'assets/cooked/user-notes.txt';notes.write_text('retained notes')
    assert 'assets/cooked/cook.stamp' not in json.loads((out / 'package-index.json').read_text())['files']
    package();assert not runtime_stamp.exists()
    assert source_stamp.read_bytes()==source_bytes and notes.read_text()=='retained notes'
    for content in ['user data', '[]', json.dumps({'schema': 1, 'identity': 'custom', 'outputs': {}})]:
        runtime_stamp.write_text(content);package();assert runtime_stamp.read_text()==content
    runtime_stamp.unlink();runtime_stamp.symlink_to(source_stamp)
    package();assert runtime_stamp.is_symlink() and source_stamp.read_bytes()==source_bytes
    runtime_stamp.unlink();runtime_stamp.write_bytes(source_bytes)
    # A later corrupt payload must not replace an earlier valid changed one.
    (assets / 'probe.kgmodel').write_bytes(b'asset1'); stamp()
    (assets / 'second.kgtex').write_bytes(b'corrupt')
    package(False); assert target.read_bytes() == b'asset0'
    assert runtime_stamp.read_bytes()==source_bytes, 'Failed preflight must not prune the legacy runtime stamp'
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
print('Packaging passed: verified preflight, rollback, unchanged timestamps, path containment, legacy stamp migration, user-file retention and notices')
