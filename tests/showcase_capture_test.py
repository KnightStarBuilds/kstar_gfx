#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Completed-GPU regressions for interaction, depth and refractive layer masks.

Repeated images establish determinism, not artistic correctness. The layer and
linear-depth diagnostics have analytic pixel constraints independent of scene
lighting. Failed captures remain in the requested evidence directory.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from scene_catalog import load as load_scene_catalog
from demo_capture_test import read_ppm, validate_scene
SCENES={s['id']:s for s in load_scene_catalog()['scenes'] if s['walkthrough']}
VIEWS={'facility':'glass','glass_lab':'glass','waterworks':'water','boiler_room':'fire','lighting_baked':'overview','lighting_dynamic':'overview'}


def capture(args,directory,name,sequence='idle',diagnostic='shaded',frames=132):
    output=directory/(name+'.ppm')
    command=[str(args.executable.resolve()),'--demo',args.scene,'--view',VIEWS[args.scene],
             '--sequence',sequence,'--seed','8675309','--frames',str(frames),
             '--width','320','--height','180','--samples',str(args.samples),
             '--preset','performance','--environment',str(args.environment),
             '--diagnostic',diagnostic,'--headless','--output',str(output)]
    if args.resources:command+=['--resources',str(args.resources.resolve())]
    result=subprocess.run(command,capture_output=True,text=True,timeout=240,cwd=directory)
    output.with_suffix('.log').write_text(result.stdout+result.stderr)
    assert result.returncode==0,result.stdout+result.stderr
    image=read_ppm(output);metadata=json.loads(output.with_suffix('.json').read_text())
    assert image[:2]==(320,180)
    assert metadata['demo']==args.scene and metadata['view']==VIEWS[args.scene]
    assert metadata['frames']==metadata['capture_frame']==metadata['scene_tick']==frames
    assert metadata['sequence']==sequence and metadata['seed']==8675309
    assert metadata['quality']['msaa']==args.samples
    assert metadata['environment']==args.environment
    assert metadata['capture_source']=='completed GPU LDR render target'
    assert metadata['stats']['draws']>0 and metadata['stats']['steady_frames']==frames-1
    assert metadata['stats']['steady_frame_allocations']==0
    return image,metadata


def validate_interactions(args,directory):
    before,before_state=capture(args,directory,'idle')
    after,after_state=capture(args,directory,'interact',sequence='interact')
    repeated,_=capture(args,directory,'repeat',sequence='interact')
    validate_scene(before);validate_scene(after)
    assert max(abs(a-b) for a,b in zip(after[2],repeated[2]))<=1,'Fixed seed/tick capture is nondeterministic'
    changed=sum(any(abs(a-b)>3 for a,b in zip(before[2][i:i+3],after[2][i:i+3])) for i in range(0,len(before[2]),3))
    assert changed>32, 'Interaction did not visibly change the rendered scene'
    a=before_state['interactions'];b=after_state['interactions']
    assert a['particles']>0 and b['particles']>0
    if args.scene in ('facility','glass_lab'):assert a['glass_variant']==0 and b['glass_variant']==1
    if args.scene in ('facility','waterworks'):assert a['water_on'] and not b['water_on']
    if args.scene in ('facility','boiler_room'):
        assert not a['sprinklers_on'] and b['sprinklers_on']
        assert b['fire_level']<a['fire_level']*.8
    if args.scene=='facility':assert a['lights_on'] and not b['lights_on']


def validate_diagnostics(args,directory):
    depth,_=capture(args,directory,'depth',diagnostic='depth',frames=4)
    assert all(max(depth[2][i:i+3])-min(depth[2][i:i+3])<=1 for i in range(0,len(depth[2]),3)), 'Linear depth must be grayscale'
    assert max(depth[2])-min(depth[2])>40,'Linear depth lost scene geometry'
    layers,_=capture(args,directory,'layers',diagnostic='layers',frames=4)
    # The final diagnostic writes these three display-byte colors directly.
    palette=((6,6,6),(13,166,115),(38,115,255));counts=[0,0,0]
    for i in range(0,len(layers[2]),3):
        observed=layers[2][i:i+3]
        distances=[max(abs(a-b) for a,b in zip(observed,color)) for color in palette]
        which=min(range(3),key=distances.__getitem__)
        assert distances[which]<=1,('Invalid layer diagnostic pixel',tuple(observed))
        counts[which]+=1
    assert counts[0]>100 and counts[1]>100,'Refractive surface was not peeled against opaque depth'
    if args.scene in ('glass_lab','waterworks'):
        assert counts[2]>8,'Authored overlapping transparent interfaces lost their second layer'
    overflow,_=capture(args,directory,'overflow',diagnostic='overflow',frames=4)
    marked=sum(r>=250 and g<=5 and 198<=b<=208 for r,g,b in zip(overflow[2][::3],overflow[2][1::3],overflow[2][2::3]))
    assert marked==0,'Authored inspection view exceeds the two-interface transparency budget'


def main():
    parser=argparse.ArgumentParser();parser.add_argument('executable',type=Path)
    parser.add_argument('--scene',choices=tuple(SCENES),required=True)
    parser.add_argument('--scratch',type=Path,required=True)
    parser.add_argument('--resources',type=Path)
    parser.add_argument('--samples',type=int,choices=(1,4),default=1)
    parser.add_argument('--environment',type=int,choices=(0,1),default=0)
    parser.add_argument('--diagnostics',action='store_true')
    args=parser.parse_args();args.scratch=args.scratch.resolve();args.scratch.mkdir(parents=True,exist_ok=True)
    directory=Path(tempfile.mkdtemp(prefix=args.scene+'-',dir=args.scratch))
    try:
        if args.diagnostics:validate_diagnostics(args,directory)
        else:validate_interactions(args,directory)
    except Exception:
        print('Retained failed GPU evidence: '+str(directory),file=sys.stderr)
        raise
    shutil.rmtree(directory)
    print(f'{args.scene} {args.samples}x: '+('analytic depth/layer/overflow checks' if args.diagnostics else 'interaction pixels, deterministic simulation and allocation stability')+' passed')


if __name__=='__main__':main()
