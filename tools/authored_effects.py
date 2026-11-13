#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Original deterministic effect artwork, cooked directly to RGBA8 KGTEX v2.

Particle atlases use 4x4 cells, row-major from the top left. Each 128px cell has
transparent edge padding. Tile-aware mip filtering stops before cells collapse.
No network, image libraries, random global state, or runtime synthesis required.
"""
import argparse
import json
import math
from pathlib import Path
import struct


def clamp(x):
    return max(0., min(1., x))


def noise(x, y, t):
    return .5 + .20*math.sin(x*7.1+y*4.3+t)*math.sin(y*8.7-x*2.4-t*.71) + .14*math.sin(x*18.7-y*13.2+t*1.7) + .09*math.sin(x*37.4+y*29.1-t*.4)


def value_noise(x, y, seed):
    # Hashed lattice values and quintic interpolation avoid directional bands
    # from crossing sine waves while keeping the offline artwork reproducible.
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = x-ix, y-iy
    sx, sy = fx*fx*fx*(fx*(fx*6-15)+10), fy*fy*fy*(fy*(fy*6-15)+10)
    def lattice(px, py):
        value = (px*0x1f123bb5 ^ py*0x5f356495 ^ seed*0x6c8e9cf5) & 0xffffffff
        value ^= value >> 16
        value = (value*0x7feb352d) & 0xffffffff
        value ^= value >> 15
        value = (value*0x846ca68b) & 0xffffffff
        value ^= value >> 16
        return (value & 0xffffff)/16777215
    a, b, c, d = lattice(ix, iy), lattice(ix+1, iy), lattice(ix, iy+1), lattice(ix+1, iy+1)
    return (a+(b-a)*sx)*(1-sy)+(c+(d-c)*sx)*sy


def smoke_noise(x, y, t):
    # Circular advection has the same 16-frame loop as the atlas. Rotated
    # octaves and a low-frequency warp give overlapping, soft billows rather
    # than a visible grid as many translucent sprites accumulate.
    flow_x, flow_y = .28*math.cos(t), .28*math.sin(t)
    warp_x = value_noise(x*1.3+flow_x, y*1.3+flow_y, 11)-.5
    warp_y = value_noise(x*1.3-flow_y, y*1.3+flow_x, 37)-.5
    x, y = x*2.2+warp_x*.9+flow_x, y*2.2+warp_y*.9+flow_y
    density = 0.
    for octave, weight in enumerate((.57,.27,.12,.04)):
        density += weight*value_noise(x, y, 71+octave*29)
        x, y = (x*.8-y*.6)*2.03+7.1, (x*.6+y*.8)*2.03-3.7
    return density


def pixel(kind, x, y, frame):
    t = frame*math.tau/16
    edge = clamp((1-max(abs(x), abs(y)))*8)
    radius = math.sqrt(x*x+y*y)
    if kind == 'smoke':
        n = smoke_noise(x, y, t)
        outline = clamp((.87-radius+(n-.5)*.21)*3.1)
        outline = outline*outline*(3-2*outline)
        a = outline*clamp(.26+n*.94)*edge
        light = .5+.38*n
        return (light, light, light, a*.82)
    n = noise(x, y, t)
    if kind == 'surface_masks':
        # Independent UV-authored masks: porous patches, narrow leaks, paired
        # footprints and grime. All channels are linear material data.
        n = smoke_noise(x*1.6, y*1.6, 0)
        if frame == 0:
            coverage = clamp((n-.43)*6 + .16*math.sin(x*5+y*3))
        elif frame == 1:
            stream = x+.1*math.sin(y*5)+.035*math.sin(y*18)
            coverage = clamp((.18+.12*n-abs(stream))*12)*clamp((1-abs(y))*7)
            coverage = max(coverage, clamp((.46-math.hypot(x,y-.57)+(n-.5)*.17)*7))
        elif frame == 2:
            coverage = 0.
            for sx, sy in ((-.24,-.48),(.24,.08),(-.24,.65)):
                dx, dy = (x-sx)/.18, (y-sy)/.32
                sole = clamp((1-dx*dx-dy*dy)*8)
                tread = .62+.38*clamp(math.sin((y-sy)*92)*4)
                coverage = max(coverage,sole*tread)
        else:
            coverage = clamp((n-.39)*5)*clamp((1-max(abs(x),abs(y)))*7)
        coverage *= edge
        return (coverage, clamp(.32+.65*n), clamp(.5+(n-.5)*coverage), coverage)
    if kind == 'fire':
        # Upward tongues are animated continuously; intensity remains in alpha.
        width = .46*(1-.62*(1-y)/2)
        xx = x + .11*math.sin(y*7-t*2) + .05*math.sin(y*17+t)
        a = clamp((1-abs(xx)/max(.08,width))*.9 + n*.65-.32)*clamp((1-y)*2)*edge
        a *= clamp((y+1)*3)
        hot = clamp(a*1.5 + (y+1)*.18)
        return (1., .12+hot*.70, .01+hot*hot*.27, a)
    if kind == 'water_spray':
        a = 0.
        for i in range(7):
            angle = i*2.399+t*.2
            r = .12+.055*i+.016*frame
            dx,dy=x-math.cos(angle)*r,y-math.sin(angle)*r
            a=max(a,clamp(1-(dx*dx+dy*dy)/(.018+.002*i)))
        return (.78,.9,1.,a*edge)
    if kind == 'sparks':
        a=clamp(1-abs(x)*12)*clamp(1-abs(y))*edge
        return (1.,.74,.22,a)
    if kind == 'water_normal':
        # Tileable periodic derivative field, encoded in tangent space.
        dx=.20*math.cos(x*math.tau*3+y*math.tau*2)+.10*math.cos(x*math.tau*7-y*math.tau*4)
        dy=.18*math.cos(y*math.tau*4-x*math.tau*2)+.08*math.cos(y*math.tau*9+x*math.tau*3)
        inv=1/math.sqrt(dx*dx+dy*dy+1)
        return (.5+.5*dx*inv,.5+.5*dy*inv,.5+.5*inv,1.)
    if kind == 'foam':
        a=clamp((n-.42)*4)
        return (a,a,a,1.)
    a=clamp((n-.35)*2.2)
    return (.12+.2*a,.095+.16*a,.055+.10*a,a)


def write_texture(path, kind):
    atlas = kind in ('fire','smoke','water_spray','surface_masks')
    columns = 2 if kind == 'surface_masks' else 4
    srgb = kind in ('fire','sparks','grime')
    width = 512 if atlas else 256
    values=[]
    cell=width//columns if atlas else width
    for y in range(width):
        for x in range(width):
            frame=(y//cell)*columns+x//cell if atlas else 0
            values.extend(pixel(kind,2*((x%cell)+.5)/cell-1,2*((y%cell)+.5)/cell-1,frame))
    payload=bytearray();size=width;mips=0
    while True:
        payload.extend(round(clamp(v)*255) for v in values);mips+=1
        # At 4px tiles stop mip generation so neighboring flipbook cells never mix.
        if size == (16 if atlas else 1):break
        small=size//2;filtered=[]
        for y in range(small):
            for x in range(small):
                samples=[values[((2*y+dy)*size+2*x+dx)*4:((2*y+dy)*size+2*x+dx)*4+4] for dy in (0,1) for dx in (0,1)]
                p=[sum(v[c] for v in samples)/4 for c in range(4)]
                if srgb:
                    for c in range(3):
                        linear=sum(v[c]/12.92 if v[c]<=.04045 else ((v[c]+.055)/1.055)**2.4 for v in samples)/4
                        p[c]=12.92*linear if linear<=.0031308 else 1.055*linear**(1/2.4)-.055
                if kind=='water_normal':
                    n=[c*2-1 for c in p[:3]];length=math.sqrt(sum(c*c for c in n))
                    p[:3]=[.5+.5*c/length for c in n]
                filtered.extend(p)
        values=filtered;size=small
    header=struct.pack('<8s6I2Q',b'KGTEX',2,width,width,mips,1,int(srgb),48,len(payload))
    path.write_bytes(header+payload)
    return {'texture':path.name,'width':width,'height':width,'columns':columns if atlas else 1,'rows':columns if atlas else 1,'frames':columns*columns if atlas else 1,'fps':16 if atlas and kind!='surface_masks' else 0,'srgb':srgb,'mips':mips}


def generate(directory):
    directory=Path(directory);directory.mkdir(parents=True,exist_ok=True)
    records=[write_texture(directory/(kind+'.kgtex'),kind) for kind in ('fire','smoke','water_spray','sparks','water_normal','foam','grime','surface_masks')]
    (directory/'atlas.json').write_text(json.dumps({'schema':1,'license':'MIT','source':'tools/authored_effects.py','origin':'top-left','effects':records},indent=2)+'\n')


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('output',type=Path);generate(parser.parse_args().output)
