#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Verify cooked asset topology, normals, mips, linear/HDR data and alpha repair."""
import argparse,ctypes as C,json,math,pathlib,struct
class Model(C.LittleEndianStructure):
    _fields_=[('magic',C.c_char*8),('version',C.c_uint32),('materials',C.c_uint32),('primitives',C.c_uint32),('vertices',C.c_uint32),('indices',C.c_uint32),('reserved',C.c_uint32),('mo',C.c_uint64),('po',C.c_uint64),('vo',C.c_uint64),('io',C.c_uint64),('bounds',C.c_float*6)]
class Material(C.LittleEndianStructure):
    _fields_=[('values',C.c_float*12),('alpha',C.c_uint32),('double_sided',C.c_uint32),('reserved',C.c_uint32*2),('base',C.c_char*160),('normal',C.c_char*160),('arm',C.c_char*160),('emissive',C.c_char*160)]
class Texture(C.LittleEndianStructure):
    _fields_=[('magic',C.c_char*8),('version',C.c_uint32),('w',C.c_uint32),('h',C.c_uint32),('mips',C.c_uint32),('format',C.c_uint32),('srgb',C.c_uint32),('offset',C.c_uint64),('bytes',C.c_uint64)]
class Environment(C.LittleEndianStructure):
    _fields_=[('magic',C.c_char*8),('version',C.c_uint32),('size',C.c_uint32),('mips',C.c_uint32),('irr',C.c_uint32),('brdf',C.c_uint32),('reserved',C.c_uint32),('so',C.c_uint64),('io',C.c_uint64),('bo',C.c_uint64),('bytes',C.c_uint64)]
def read(cls,data):return cls.from_buffer_copy(data[:C.sizeof(cls)])
def main():
    p=argparse.ArgumentParser();p.add_argument('root',type=pathlib.Path);args=p.parse_args();root=args.root
    for name,triangles in [('bolt_action_rifle_7_62',19985),('fishermans_hat',10304)]:
        blob=(root/(name+'.kgmodel')).read_bytes();h=read(Model,blob);assert h.magic==b'KGMODEL' and h.version==2;assert h.indices%3==0;assert h.io+h.indices*4==len(blob);assert h.indices//3==triangles,(name,h.indices//3)
        assert all(i<h.vertices for (i,) in struct.iter_unpack('<I',blob[h.io:]));assert h.vo+h.vertices*48==h.io
        for v in struct.iter_unpack('<12f',blob[h.vo:h.io]):
            assert all(math.isfinite(x) for x in v);assert abs(sum(x*x for x in v[3:6])-1)<.001;assert abs(sum(x*x for x in v[6:9])-1)<.001;assert abs(sum(a*b for a,b in zip(v[3:6],v[6:9])))<.001;assert abs(abs(v[9])-1)<.001
        for i in range(h.materials):
            m=read(Material,blob[h.mo+i*C.sizeof(Material):]);assert m.double_sided==1
            for field in ['base','normal','arm']:
                path=getattr(m,field).decode();tb=(root/path).read_bytes();th=read(Texture,tb);assert th.magic==b'KGTEX' and th.version==2 and th.w==th.h==2048 and th.mips==12 and th.format==1;assert th.offset+th.bytes==len(tb);assert th.srgb==(field=='base');expected=sum(max(1,2048>>level)**2*4 for level in range(12));assert expected==th.bytes
                if m.alpha==2 and field=='base':
                    alphas=tb[th.offset+3:th.offset+2048*2048*4:4];assert min(alphas)<255 and max(alphas)>0,'Rifle glass must retain repaired alpha'
    for name in ['industrial_pipe_and_valve_01','qwantani_night']:
        blob=(root/(name+'.kgenv')).read_bytes();h=read(Environment,blob);assert h.magic==b'KGENV' and h.version==2 and h.size==512 and h.mips==6 and h.irr==32 and h.brdf==256;assert len(blob)==h.bytes;assert h.size>>(h.mips-1)>=16;assert h.io==h.so+sum((h.size>>level)**2*6*16 for level in range(h.mips));assert h.bo==h.io+h.irr*h.irr*6*16;assert h.bytes==h.bo+h.brdf*h.brdf*16;peak=0
        for rgba in struct.iter_unpack('<4f',blob[h.so:h.io]):
            assert all(math.isfinite(x) for x in rgba);peak=max(peak,*rgba[:3]);assert rgba[3]==1
        assert peak>1,'HDR radiance must not clip to LDR'
        for values in struct.iter_unpack('<4f',blob[h.bo:]):assert all(math.isfinite(x) for x in values) and values[0]>=0 and values[1]>=0
    print('Cooked assets passed: full source triangles, orthonormal MikkTSpace, 2K maps/mips, glass alpha, finite unclipped IBL')
if __name__=='__main__':main()
