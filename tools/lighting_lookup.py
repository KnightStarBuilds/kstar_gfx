#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Pack the pinned LTC fit and existing split-sum BRDF into one linear atlas.

LTC tables: Heitz, Dupuy, Hill and Neubelt, SIGGRAPH 2016. Redistribution
terms are preserved in deps/ltc-LICENSE.txt and every cooked resource package.
"""
import json
from pathlib import Path
import re
import struct


def generate(source, cooked):
    source, cooked = Path(source), Path(cooked)
    tables = []
    text = (source/'lighting/ltc/ltc.js').read_text()
    for i in (1, 2):
        match = re.search(r'var g_ltc_'+str(i)+r'\s*=\s*\[([^]]+)\]', text)
        if not match:
            raise ValueError('Missing LTC table')
        values = [float(x) for x in match[1].split(',') if x.strip()]
        if len(values) != 64*64*4:
            raise ValueError('Invalid LTC table size')
        tables.append(values)
    env = (cooked/'industrial_pipe_and_valve_01.kgenv').read_bytes()
    header = struct.unpack_from('<8s6I4Q', env)
    size, offset = header[5], header[9]
    if size != 256:
        raise ValueError('Lighting atlas requires the version 2 256px BRDF')
    # Top 256 rows: split-sum BRDF. Next two 64-row regions: LTC inverse
    # transform and amplitude. Padding repeats each row's last texel.
    payload = bytearray(env[offset:offset+size*size*16])
    for values in tables:
        for row in range(64):
            floats = values[row*256:(row+1)*256]
            floats += floats[-4:]*(size-64)
            payload += struct.pack('<'+str(len(floats))+'f', *floats)
    target = cooked/'lighting';target.mkdir(exist_ok=True)
    (target/'lookup.kgtex').write_bytes(struct.pack('<8s6I2Q', b'KGTEX', 2, size, 384, 1, 2, 0, 48, len(payload))+payload)
    (target/'ltc-LICENSE.txt').write_bytes((source/'lighting/ltc/LICENSE').read_bytes())
    (target/'lookup.json').write_text(json.dumps({'schema':1,'brdf':[0,0,256,256],
        'ltc_matrix':[0,256,64,64],'ltc_amplitude':[0,320,64,64],
        'reference':'Real-Time Polygonal-Light Shading with Linearly Transformed Cosines (SIGGRAPH 2016)',
        'license':'ltc-LICENSE.txt'},indent=2)+'\n')
