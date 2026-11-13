#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Validate the companion bake, xatlas mappings and separate directional fields."""
import array
import math
import pathlib
import struct
import sys

root=pathlib.Path(sys.argv[1])/'lighting'
probe=(root/'industrial.kglight').read_bytes()
magic,version,low,high,count,surfaces,static_lights,bounces=struct.unpack_from('<8s7I',probe)
assert magic==b'KGLIGHT1' and version==1 and 0<count<=192 and static_lights==2 and bounces==3
assert len(probe)==36+count*108+surfaces*152
mapping=(root/'industrial.kglmap').read_bytes()
magic,version,map_low,map_high,width,height,receivers=struct.unpack_from('<8s6I',mapping)
assert magic==b'KGLMAP1\0' and version==1 and (low,high)==(map_low,map_high)
assert 0<receivers<=surfaces and len(mapping)==32+receivers*200
seen=set()
for offset in range(32,len(mapping),200):
    object_id=struct.unpack_from('<Q',mapping,offset)[0]
    assert object_id and object_id not in seen
    seen.add(object_id)
    values=struct.unpack_from('<48f',mapping,offset+8)
    assert all(math.isfinite(value) for value in values)
    for face in range(6):
        assert sum(abs(values[face*4+n]) for n in range(3))>0
        assert sum(abs(values[24+face*4+n]) for n in range(3))>0
blob=(root/'industrial_lightmap.kgtex').read_bytes()
magic,version,w,h,mips,format_,srgb,offset,byte_count=struct.unpack_from('<8s6I2Q',blob)
assert magic.startswith(b'KGTEX') and version==2 and (w,h)==(width*4,height*3)
assert mips==1 and format_==2 and srgb==0 and byte_count==w*h*16 and len(blob)==offset+byte_count
pixels=array.array('f');pixels.frombytes(blob[offset:])
if sys.byteorder!='little':pixels.byteswap()
assert all(math.isfinite(value) and value>=0 for value in pixels)
energy=[0.0]*12
for y in range(h):
    for x in range(w):
        i=(y*w+x)*4;tile=(y//height)*4+x//width
        energy[tile]+=pixels[i]+pixels[i+1]+pixels[i+2]
assert sum(energy[:6])>0 and sum(energy[6:])>0,'Direct and three-bounce fields must both contain energy'
print(f'Lighting assets: {count} probes, {receivers} xatlas receivers, twelve directional fields and matching scene hashes passed')
