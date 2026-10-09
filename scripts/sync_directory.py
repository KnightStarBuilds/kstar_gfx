#!/usr/bin/env python3
"""Refresh owned package files without rewriting unchanged payloads."""
import hashlib
import os
from pathlib import Path
import shutil
import sys

def sync(source,destination):
    source=Path(source);destination=Path(destination)
    for f in source.rglob('*'):
        if not f.is_file():continue
        out=destination/f.relative_to(source)
        if out.exists() and out.stat().st_size==f.stat().st_size and hashlib.sha256(out.read_bytes()).digest()==hashlib.sha256(f.read_bytes()).digest():continue
        out.parent.mkdir(parents=True,exist_ok=True)
        partial=out.with_name(out.name+'.part');shutil.copyfile(f,partial);os.replace(partial,out)
if __name__=='__main__':
    if len(sys.argv)!=3:raise SystemExit('usage: sync_directory.py SOURCE DESTINATION')
    sync(sys.argv[1],sys.argv[2])
