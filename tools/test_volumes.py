#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Verify portable volume layout, offline reuse and archive boundary rules."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import tarfile
import tempfile
import subprocess
import sys
from cook_lock import lock_directory
from cook_volumes import selected_frames
from volume_inputs import extract_tar, fetch, safe_relative


def contracts(root):
    scratch = root/'.tmp/volume-tests'
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=scratch) as directory:
        directory = Path(directory)
        lock = directory/'test.lock'
        child_code = 'from cook_lock import lock_directory; import sys,time;\nwith lock_directory(sys.argv[1]):\n print("locked",flush=True); time.sleep(30)'
        child = subprocess.Popen([sys.executable, '-c', child_code, str(lock)],
                                 cwd=root/'tools', stdout=subprocess.PIPE, text=True)
        try:
            assert child.stdout.readline().strip() == 'locked'
            try:
                with lock_directory(lock, timeout=.2):
                    raise AssertionError('Concurrent cook acquired an owned lock')
            except RuntimeError:
                pass
            child.kill()
            child.wait(timeout=5)
            with lock_directory(lock, timeout=1):
                pass
        finally:
            if child.poll() is None:
                child.kill()
                child.wait(timeout=5)
            child.stdout.close()
        data = b'verified volume archive'
        cached = directory/'source.zip'
        cached.write_bytes(data)
        digest = hashlib.sha256(data).hexdigest()
        assert fetch('https://unavailable.invalid/source', digest, cached, True) == cached
        try:fetch('https://unavailable.invalid/source', '0'*64, cached, True)
        except RuntimeError:pass
        else:raise AssertionError('Offline corrupt sources must fail')
        assert cached.read_bytes() == data
        for name in ('../escape', '/absolute', 'safe/../../escape', 'dir\\escape'):
            try:safe_relative(name)
            except ValueError:pass
            else:raise AssertionError('Unsafe archive path accepted')
        malicious = directory/'malicious.tar'
        with tarfile.open(malicious, 'w') as archive:
            member = tarfile.TarInfo('root/link')
            member.type = tarfile.SYMTYPE
            member.linkname = '/tmp'
            archive.addfile(member)
        try:extract_tar(malicious, directory/'extract')
        except ValueError:pass
        else:raise AssertionError('Archive links must not be extracted')
    selected = selected_frames(['fire_'+str(i)+'.vdb' for i in range(101)])
    assert selected[0] == 'fire_20.vdb' and selected[-1] == 'fire_90.vdb'
    assert len(selected) == len(set(selected)) == 16


def cooked(root):
    total = 0
    for name in ('small_campfire', 'simple_fire', 'chimney_smoke'):
        folder = root/'effects/volumes'/name
        metadata = json.loads((folder/'volume.json').read_text())
        blob = (folder/'volume.kgtex').read_bytes()
        magic, version, width, height, mips, format, srgb, offset, size = struct.unpack_from('<8s6I2Q', blob)
        assert magic.rstrip(b'\0') == b'KGTEX' and version == 2
        assert (width,height,mips,format,srgb,offset) == (1088,544,1,1,0,48)
        assert size == width*height*4 and len(blob) == size+offset
        pixels = blob[offset:]
        assert max(pixels[0::4]) > 5 and min(pixels[0::4]) == 0
        if name != 'chimney_smoke':assert max(pixels[1::4]) > 20
        # Identical end frame and edge replication prevent loop/slice seams.
        row_bytes = 34*width*4
        assert pixels[:row_bytes] == pixels[-row_bytes:]
        for frame in range(16):
            for z in range(32):
                for y in (0,1,16,32,33):
                    left = ((frame*34+y)*width+z*34)*4
                    right = left+33*4
                    assert pixels[left:left+4] == pixels[left+4:left+8]
                    assert pixels[right:right+4] == pixels[right-4:right]
        total += size+len((folder/'distant.kgtex').read_bytes())-48
        assert metadata['resident_bytes'] == size+128*128*4
    assert total < 128*1024*1024
    print('Verified three real VDB atlases, slice gutters, smooth loop endpoints, and',total,'resident bytes')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cooked', type=Path)
    args = parser.parse_args()
    contracts(Path(__file__).resolve().parents[1])
    if args.cooked:cooked(args.cooked)
    print('Volume acquisition contracts passed')


if __name__ == '__main__':main()
