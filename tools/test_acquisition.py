#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise actual CMake acquisition: paths with spaces, offline reuse, rollback."""
import argparse,hashlib,pathlib,subprocess,tempfile,time

def main():
    p=argparse.ArgumentParser();p.add_argument('--cmake',default='cmake');a=p.parse_args()
    root=pathlib.Path(__file__).resolve().parents[1];scratch=root/'.tmp';scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='acquisition test ',dir=scratch) as temp:
        work=pathlib.Path(temp);payload=work/'input with spaces';payload.write_bytes(b'locked dependency\n');digest=hashlib.sha256(payload.read_bytes()).hexdigest();dest=work/'cache with spaces/output';script=work/'test.cmake'
        def run(url,sha=digest,offline=False,success=True):
            script.write_text('cmake_minimum_required(VERSION 3.24)\ninclude("'+str(root/'cmake/AcquireInputs.cmake')+'")\nset(KSTAR_GFX_TEST_ALLOW_LOCAL_DOWNLOADS ON)\nset(KSTAR_GFX_OFFLINE '+('ON' if offline else 'OFF')+')\nkgfx_download_verified("'+url+'" "'+sha+'" "'+str(dest)+'")\n')
            result=subprocess.run([a.cmake,'-P',str(script)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            if (result.returncode==0)!=success:raise AssertionError(result.stdout+result.stderr)
        run(payload.as_uri());assert dest.read_bytes()==payload.read_bytes();mtime=dest.stat().st_mtime_ns
        run('https://unreachable.invalid/asset',offline=True);assert dest.stat().st_mtime_ns==mtime
        run(payload.as_uri(),sha='0'*64,success=False);assert dest.read_bytes()==payload.read_bytes();assert not dest.with_suffix('.part').exists()
        run((work/'missing').as_uri(),sha='1'*64,success=False);assert dest.stat().st_mtime_ns==mtime
        run(payload.as_uri(),sha='1'*64,offline=True,success=False)
        print('CMake acquisition passed: verified replacement, offline reuse, timestamp retention and rollback')
if __name__=='__main__':main()
