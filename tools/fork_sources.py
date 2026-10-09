#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Identify editable fork inputs and create CMake-owned build snapshots."""
import hashlib
import json
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FORKS = ('opengnm', 'opengnm-psbc')


def identity(name, root=ROOT):
    source = root / 'deps' / name
    if name not in FORKS or not (source / '.git').exists():
        raise RuntimeError('Missing fork submodule; run git submodule update --init --recursive')
    def git(*args):
        return subprocess.check_output(['git', '-C', str(source), *args])
    if Path(git('rev-parse', '--show-toplevel').decode().strip()).resolve() != source.resolve():
        raise RuntimeError('Expected a separate Git source checkout: ' + str(source))
    revision = git('rev-parse', 'HEAD').decode().strip()
    files = sorted(set(git('ls-files', '-z', '--cached', '--others', '--exclude-standard').decode().split('\0')) - {''})
    entries = []
    for name_in_tree in files:
        if any(c in name_in_tree for c in ('\n', '\r', '\t')) or name_in_tree.startswith('"'):
            raise RuntimeError('Unsupported fork source pathname: ' + name_in_tree)
        path = source / name_in_tree
        if path.is_symlink():
            kind, data = 'link', os.readlink(path).encode()
        elif path.is_file():
            kind, data = 'file', path.read_bytes()
        elif path.is_dir():
            raise RuntimeError('Nested gitlink requires explicit snapshot support: ' + str(path))
        else:
            continue
        entries.append(name_in_tree + '\t' + kind + '\t' + hashlib.sha256(data).hexdigest() + '\n')
    tree = hashlib.sha256(''.join(entries).encode()).hexdigest()
    digest = hashlib.sha256(('kstar-gfx-submodule-v1\n' + revision + '\n' + tree + '\n').encode()).hexdigest()
    return {'schema': 1, 'name': name, 'revision': revision, 'tree_sha256': tree,
            'identity': digest, 'dirty': bool(git('status', '--porcelain', '--untracked-files=all').strip())}


def snapshot(name, destination, root=ROOT):
    scratch = root / '.tmp'
    scratch.mkdir(exist_ok=True)
    with tempfile.NamedTemporaryFile(prefix='fork-source-', suffix='.json', dir=scratch, delete=False) as temporary:
        result = Path(temporary.name)
    try:
        subprocess.run(['cmake', '-DKGFX_FORK_SOURCE=' + str(root / 'deps' / name),
                        '-DKGFX_FORK_NAME=' + name,
                        '-DKGFX_FORK_DESTINATION=' + str(destination),
                        '-DKGFX_FORK_MANIFEST=' + str(result),
                        '-P', str(root / 'cmake/ForkSources.cmake')], check=True)
        observed = json.loads(result.read_text())
        if observed != identity(name, root):
            raise RuntimeError('Source changed during snapshot or source identity differs: ' + name)
        return observed
    finally:
        result.unlink(missing_ok=True)
