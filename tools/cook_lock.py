# SPDX-License-Identifier: MIT
"""Process-scoped cook lock, released by the OS even after a killed converter."""
from contextlib import contextmanager
from pathlib import Path
import os
import time


@contextmanager
def lock_directory(path, timeout=1800):
    # Keep the file inode stable: unlinking a locked file lets another process
    # lock a different inode. Source/cooked directories are ignored build data.
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    lock = path.with_name(path.name + '.lck').open('a+b')
    if os.name == 'nt':
        import msvcrt
        lock.seek(0, 2)
        if lock.tell() == 0:
            lock.write(b'0')
            lock.flush()
        def acquire():
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        def release():
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
    else:
        import fcntl
        def acquire():
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        def release():
            fcntl.flock(lock, fcntl.LOCK_UN)
    deadline = time.monotonic() + timeout
    try:
        while True:
            try:
                acquire()
                break
            except (BlockingIOError, PermissionError):
                if time.monotonic() >= deadline:
                    raise RuntimeError('Timed out waiting for cook lock: ' + str(path))
                time.sleep(.1)
        try:
            yield
        finally:
            release()
    finally:
        lock.close()
