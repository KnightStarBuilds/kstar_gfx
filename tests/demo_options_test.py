#!/usr/bin/env python3
"""CLI errors are rejected before attempting to initialize a graphics device."""
import json
from pathlib import Path
import subprocess
import sys

exe = Path(sys.argv[1]).resolve()
root = Path(__file__).resolve().parents[1]
catalog = json.loads((root / 'assets/scenes.json').read_text())
listed = subprocess.run([str(exe), '--list-scenes'], text=True, capture_output=True, check=True).stdout
for scene in catalog['scenes']:
    assert scene['id'] + ' - ' in listed
for args in [
    ['--demo', 'absent'], ['--demo', 'pbr', '--view', 'water'],
    ['--demo', 'triangle', '--sequence', 'interact'], ['--seed', '-1'],
    ['--seed', '4294967296'], ['--preset', 'ultra'], ['--width', '0'],
    ['--headless'], ['--samples', '2'], ['--frames', '2', '--seconds', '2'],
    ['--demo', 'cubes', '--diagnostic', 'overflow'],
    ['--demo', 'cubes', '--diagnostic', 'baked'],
    ['--demo', 'lighting_baked', '--diagnostic', 'unknown'],
    ['--ticks-per-frame', '0'], ['--ticks-per-frame', '3'],
    ['--ticks-per-frame', '2'],
    ['--demo', 'triangle', '--frames', '4', '--ticks-per-frame', '2'],
]:
    result = subprocess.run([str(exe), *args], capture_output=True, text=True)
    assert result.returncode != 0, args
    assert 'graphics device' not in result.stderr.lower(), (args, result.stderr)
for scene in catalog['scenes']:
    subprocess.run([str(exe), '--demo', scene['id'], '--help'], capture_output=True, check=True)
for diagnostic in ['baked', 'bounce', 'wetness', 'roughness', 'probes', 'timers']:
    subprocess.run([str(exe), '--demo', 'lighting_baked', '--diagnostic', diagnostic,
                    '--help'], capture_output=True, check=True)
print('Scene catalog and command-line validation passed')
