#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Validate the offline ABI metadata and require a strict GLSL410 resource ABI."""
import argparse,json,pathlib,subprocess,tempfile,math

def showcase_contract(reflections):
    """Catch cross-stage layouts and texture budgets before native linking."""
    shared={};variants={}
    for reflection in reflections:
        data=json.loads(reflection.read_text())
        textures=data.get('textures',[])+data.get('separate_images',[])
        bindings=[texture['binding'] for texture in textures]
        assert len(bindings)<=16 and len(set(bindings))==len(bindings),(reflection,'Texture binding budget/alias')
        assert all(8<=binding<24 for binding in bindings),(reflection,'Texture exceeds C ABI binding 15')
        if not reflection.name.startswith('showcase'):continue
        variants[reflection.name]=set(texture['name'] for texture in textures)
        for block in data.get('ubos',[]):
            signature=(block['block_size'],[
                tuple((key,json.dumps(member.get(key),sort_keys=True)) for key in
                      ('name','type','offset','array','array_stride','matrix_stride'))
                for member in data['types'][block['type']]['members']])
            previous=shared.setdefault(block['name'],signature)
            assert previous==signature,(reflection,block['name'],'Shared showcase layout diverged')
    # The refractive variant uses the full 16-binding ABI. The opaque variant
    # keeps room by reusing its snapshot/peel slots for probes and authored masks.
    for name in ('showcase_opaque.frag.json','showcase_surface.frag.json'):
        assert name in variants,('Missing split material variant',name)
    assert 'showcase_shafts.frag.json' in variants,'Missing dedicated reduced-resolution shaft pass'
    assert {'kgfx_t11','kgfx_t12'}<=variants['showcase_opaque.frag.json']
    assert not {'kgfx_t14','kgfx_t15'}&variants['showcase_opaque.frag.json']
    assert {'kgfx_t11','kgfx_t12','kgfx_t13','kgfx_t14','kgfx_t15'}<=variants['showcase_surface.frag.json']

def main():
    p=argparse.ArgumentParser();p.add_argument('shaders',type=pathlib.Path);p.add_argument('--cc',required=True);p.add_argument('--cxx',required=True);p.add_argument('--sysroot',default='');a=p.parse_args();root=pathlib.Path(__file__).resolve().parents[1];root.joinpath('.tmp').mkdir(exist_ok=True)
    compiler_flags=['-isysroot',a.sysroot] if a.sysroot else []
    reflections=list(a.shaders.glob('*.vert.json'))+list(a.shaders.glob('*.frag.json'));assert reflections,'Missing reflection artifacts'
    showcase_contract(reflections)
    with tempfile.TemporaryDirectory(prefix='shader abi ',dir=root/'.tmp') as temp:
        tmp=pathlib.Path(temp)
        for reflection in reflections:
            data=json.loads(reflection.read_text());base=reflection.with_suffix('');glsl=base.with_suffix(base.suffix+'.glsl').read_text();assert '#version 410' in glsl;assert 'layout(binding' not in glsl and 'layout (binding' not in glsl;assert 'GL_ARB_shading_language_420pack' not in glsl
            if reflection.name in ('showcase_opaque.frag.json','showcase_surface.frag.json'):
                assert 'shaft' not in glsl.lower(),'Material shading must not repeat the light-shaft integration pass'
            header=pathlib.Path(str(base)+'.h');assert pathlib.Path(str(base)+'.gnm.spv').exists();package=json.loads(pathlib.Path(str(base)+'.package.json').read_text());assert package['schema']==1 and package['artifacts']
            for block in data.get('ubos',[]):
                members=data['types'][block['type']]['members'];declarations=[];position=0
                for field in members:
                    offset=field['offset']
                    if offset>position:declarations.append(f'char padding_{position}[{offset-position}];')
                    count={'mat4':16,'vec4':4,'vec3':3,'vec2':2,'float':1}[field['type']]
                    size=4*count
                    if field.get('array'):size=field['array_stride']*math.prod(field['array'])
                    declarations.append(f'char {field["name"]}[{size}];');position=offset+size
                if position<block['block_size']:declarations.append(f'char tail[{block["block_size"]-position}];')
                prefix='KGFX_'+reflection.name.replace('.json','.spv').replace('.','_').upper()+'_'+block['name'].upper()
                source=tmp/'layout.c';body='#include "'+str(header.resolve())+'"\ntypedef struct Uniforms { '+''.join(declarations)+' } Uniforms;\n'+prefix+'_ASSERT_LAYOUT(Uniforms);\n'
                source.write_text(body);result=subprocess.run([a.cc,*compiler_flags,'-std=c11','-fsyntax-only',str(source)],capture_output=True,text=True);assert result.returncode==0,result.stderr
                source.write_text(body.replace('struct Uniforms {','struct Uniforms { float wrong;'));result=subprocess.run([a.cc,*compiler_flags,'-std=c11','-fsyntax-only',str(source)],capture_output=True,text=True);assert result.returncode!=0,'Broken host uniform layout was accepted'
        # The GLSL local_face/attenuation/LTC branches encode Point=1, Spot=2,
        # Rect=3. A changed C++ enum must fail validation before scene rendering.
        source=tmp/'light_enum.cpp'
        source.write_text('#include "'+str(root/'src/demos/lighting_cpu.hpp')+'"\n'
                          'static_assert(int(demo::LightType::Directional)==0);\n'
                          'static_assert(int(demo::LightType::Point)==1);\n'
                          'static_assert(int(demo::LightType::Spot)==2);\n'
                          'static_assert(int(demo::LightType::Rect)==3);\n'
                          'static_assert(int(demo::LightType::Sky)==4);\n')
        result=subprocess.run([a.cxx,*compiler_flags,'-std=c++23','-fsyntax-only',str(source)],capture_output=True,text=True)
        assert result.returncode==0,result.stderr
    print('Shader ABI passed: strict GLSL410, descriptor metadata, package identity, matching C layouts and rejected mismatches')
if __name__=='__main__':main()
