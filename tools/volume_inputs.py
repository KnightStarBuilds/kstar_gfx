#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Resumable HTTPS acquisition shared by the host converter and CC0 VDB cook."""
import hashlib
import html
from cook_lock import lock_directory
from pathlib import Path, PurePosixPath
import re
import subprocess
import tarfile
import time
import urllib.request


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as source:
        for chunk in iter(lambda: source.read(1024*1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def safe_relative(name):
    path = PurePosixPath(name)
    if path.is_absolute() or '..' in path.parts or '\\' in name:
        raise ValueError('Unsafe archive/manifest member: '+name)
    return path


def fetch(url, digest, destination, offline=False, mediafire=False):
    destination = Path(destination)
    if not re.fullmatch('[0-9a-f]{64}', digest):
        raise ValueError('Invalid SHA256')
    if destination.is_file() and sha(destination) == digest:
        return destination
    if offline:
        raise RuntimeError('Offline input missing or corrupt: '+str(destination))
    if not url.startswith('https://'):
        raise ValueError('Downloads require HTTPS')
    destination.parent.mkdir(parents=True, exist_ok=True)
    if mediafire:
        with urllib.request.urlopen(url, timeout=60) as page:
            links = re.findall(r'https://download[^"\s<>]+', page.read().decode('utf-8'))
        links = [html.unescape(link) for link in links if '.mediafire.com/' in link]
        if not links:
            raise RuntimeError('MediaFire page has no direct download: '+url)
        url = links[0]
    partial = destination.with_name(destination.name+'.part')
    command = ['curl', '--location', '--fail', '--retry', '3', '--retry-all-errors',
               '--connect-timeout', '30', '--max-time', '1800', '--continue-at', '-',
               '--output', str(partial), url]
    subprocess.run(command, check=True)
    if sha(partial) != digest:
        # Preserve both the prior usable destination and failed partial for
        # investigation. Never publish unverified bytes into the source cache.
        raise RuntimeError('Downloaded SHA256 differs from manifest: '+str(partial))
    partial.replace(destination)
    return destination


def extract_tar(archive, destination):
    """Reject links and traversal before extracting a pinned source archive."""
    destination = Path(destination)
    with tarfile.open(archive) as source:
        members = source.getmembers()
        for member in members:
            safe_relative(member.name)
            if member.issym() or member.islnk() or not (member.isdir() or member.isfile()):
                raise ValueError('Unsupported archive entry: '+member.name)
        roots = {PurePosixPath(m.name).parts[0] for m in members}
        if len(roots) != 1:
            raise ValueError('Dependency archive must contain a single root')
        destination.mkdir(parents=True, exist_ok=True)
        source.extractall(destination)
        return destination / next(iter(roots))
