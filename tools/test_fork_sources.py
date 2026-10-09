#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise dirty-source snapshot invalidation without touching real forks."""
import os
import shutil
import subprocess
import tempfile
from pathlib import Path
from fork_sources import ROOT, identity, snapshot


def main():
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='fork snapshot spaces ', dir=ROOT / '.tmp') as temporary:
        root = Path(temporary)
        (root / 'cmake').mkdir()
        shutil.copy2(ROOT / 'cmake/ForkSources.cmake', root / 'cmake/ForkSources.cmake')
        source = root / 'deps/opengnm'
        source.mkdir(parents=True)
        def git(*args):
            subprocess.run(['git', '-C', str(source), *args], check=True,
                           stdout=subprocess.DEVNULL)
        git('init', '-q')
        tracked = source / 'source with spaces.c'
        tracked.write_text('int value = 1;\n')
        (source / '.gitignore').write_text('ignored/\n')
        git('add', '.')
        git('-c', 'user.name=Snapshot fixture', '-c', 'user.email=fixture@example.invalid',
            'commit', '-qm', 'Fixture input')
        destination = root / 'build copy'
        first = snapshot('opengnm', destination, root)
        generated = destination / 'generated.o'
        generated.write_bytes(b'build result')
        assert snapshot('opengnm', destination, root) == first
        assert generated.read_bytes() == b'build result'
        previous_time = tracked.stat().st_mtime_ns
        tracked.write_text('int value = 2;\n')
        os.utime(tracked, ns=(previous_time, previous_time))
        (source / 'new source.c').write_text('int added = 3;\n')
        second = snapshot('opengnm', destination, root)
        assert second['identity'] != first['identity'] and second['dirty']
        assert not generated.exists() and (destination / 'new source.c').is_file()
        assert (destination / tracked.name).read_text() == tracked.read_text()
        (source / 'ignored').mkdir()
        (source / 'ignored/cache.o').write_bytes(b'ignored generated data')
        assert identity('opengnm', root) == second
        tracked.unlink()
        third = snapshot('opengnm', destination, root)
        assert third['identity'] != second['identity'] and not (destination / tracked.name).exists()
        # No generated files entered the maintained child working tree.
        assert not (source / '.kgfx-source.json').exists()
        assert not (source / 'generated.o').exists()
    print('Editable fork snapshots passed: dirty/untracked/deleted files, unchanged mtimes, spaces, ignored outputs and incremental reuse')


if __name__ == '__main__':
    main()
