#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Validate the offline ABI metadata and require a strict GLSL410 resource ABI."""
import argparse,json,pathlib,subprocess,tempfile

def main():
    p=argparse.ArgumentParser();p.add_argument('shaders',type=pathlib.Path);p.add_argument('--cc',required=True);a=p.parse_args();root=pathlib.Path(__file__).resolve().parents[1];root.joinpath('.tmp').mkdir(exist_ok=True)
    reflections=list(a.shaders.glob('*.vert.json'))+list(a.shaders.glob('*.frag.json'));assert reflections,'Missing reflection artifacts'
    with tempfile.TemporaryDirectory(prefix='shader abi ',dir=root/'.tmp') as temp:
        tmp=pathlib.Path(temp)
        for reflection in reflections:
            data=json.loads(reflection.read_text());base=reflection.with_suffix('');glsl=base.with_suffix(base.suffix+'.glsl').read_text();assert '#version 410' in glsl;assert 'layout(binding' not in glsl and 'layout (binding' not in glsl;assert 'GL_ARB_shading_language_420pack' not in glsl
            header=pathlib.Path(str(base)+'.h');assert pathlib.Path(str(base)+'.gnm.spv').exists();package=json.loads(pathlib.Path(str(base)+'.package.json').read_text());assert package['schema']==1 and package['artifacts']
            for block in data.get('ubos',[]):
                members=data['types'][block['type']]['members'];declarations=[];position=0
                for field in members:
                    offset=field['offset']
                    if offset>position:declarations.append(f'char padding_{position}[{offset-position}];')
                    count={'mat4':16,'vec4':4,'vec3':3,'vec2':2,'float':1}[field['type']];declarations.append(f'float {field["name"]}[{count}];');position=offset+4*count
                if position<block['block_size']:declarations.append(f'char tail[{block["block_size"]-position}];')
                prefix='KGFX_'+reflection.name.replace('.json','.spv').replace('.','_').upper()+'_'+block['name'].upper()
                source=tmp/'layout.c';body='#include "'+str(header.resolve())+'"\ntypedef struct Uniforms { '+''.join(declarations)+' } Uniforms;\n'+prefix+'_ASSERT_LAYOUT(Uniforms);\n'
                source.write_text(body);result=subprocess.run([a.cc,'-std=c11','-fsyntax-only',str(source)],capture_output=True,text=True);assert result.returncode==0,result.stderr
                source.write_text(body.replace('struct Uniforms {','struct Uniforms { float wrong;'));result=subprocess.run([a.cc,'-std=c11','-fsyntax-only',str(source)],capture_output=True,text=True);assert result.returncode!=0,'Broken host uniform layout was accepted'
    print('Shader ABI passed: strict GLSL410, descriptor metadata, package identity, matching C layouts and rejected mismatches')
if __name__=='__main__':main()
