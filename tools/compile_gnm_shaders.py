#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile shared GLSL to validated base-PS4 shader packages, entirely offline."""
import argparse,hashlib,json,os,pathlib,subprocess,sys,tempfile
from validate_gnm_shader import validate
from package_gnm_shader import compile_package, verify_compiler_provenance
ROOT=pathlib.Path(__file__).resolve().parents[1]

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def find_tool(build,name,configuration):
    suffix='.exe' if os.name=='nt' else ''
    sub={'glslang':'tools/glslang/StandAlone','spirv-cross':'tools/spirv-cross','spirv-val':'tools/spirv-tools/tools'}[name]
    for config in [configuration,'Release','Debug']:
        for path in [build/'bin'/config/(name+suffix),build/sub/config/(name+suffix)]:
            if path.is_file():return path.resolve()
    raise RuntimeError('Build the pinned native '+name+' tool in '+str(build))
def run(command):
    subprocess.run([str(x) for x in command],check=True)
def main():
    p=argparse.ArgumentParser();p.add_argument('--host-tools',type=pathlib.Path,required=True);p.add_argument('--psbc',type=pathlib.Path,required=True);p.add_argument('--source',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--config',choices=['Debug','Release'],default='Debug');p.add_argument('--names',nargs='*');a=p.parse_args()
    tools={name:find_tool(a.host_tools,name,a.config) for name in ['glslang','spirv-cross','spirv-val']};tools['psbc']=a.psbc.resolve()
    if not tools['psbc'].is_file():raise RuntimeError('Build the host PSBC fork compiler first')
    verify_compiler_provenance(tools['psbc'])
    sources=[a.source/name for name in a.names] if a.names else sorted(list(a.source.glob('*.vert'))+list(a.source.glob('*.frag')))
    if not sources:raise RuntimeError('No vertex/fragment shaders in '+str(a.source))
    a.output.mkdir(parents=True,exist_ok=True);scratch=ROOT/'.tmp';scratch.mkdir(exist_ok=True)
    provenance={name:sha(path) for name,path in tools.items()};provenance['compiler_build']=sha(tools['psbc'].parent/'build-provenance.json');provenance['shader_driver']=sha(pathlib.Path(__file__));provenance['gnm_specialization']=sha(ROOT/'tools/prepare_gnm_shader.py');provenance['reflection_driver']=sha(ROOT/'tools/shader_metadata.py');provenance['toolchain_lock']=sha(ROOT/'deps/toolchain-lock.json');provenance['native_validation']=sha(ROOT/'tools/validate_gnm_shader.py');provenance['native_packaging']=sha(ROOT/'tools/package_gnm_shader.py')
    psbc_provenance=tools['psbc'].parent/'build-provenance.json'
    for source in sources:
        if source.suffix not in ['.vert','.frag']:raise RuntimeError('Only vertex/fragment stages are supported: '+str(source))
        fingerprint=dict(provenance,source=sha(source),configuration=a.config)
        old=a.output/(source.name+'.native-package.json')
        if old.is_file():
            try:metadata=json.loads(old.read_text())
            except (ValueError,OSError):metadata={}
            if not isinstance(metadata,dict):metadata={}
            safe_artifacts=isinstance(metadata.get('artifacts'),dict) and all(pathlib.PurePosixPath(name).name==name and '\\' not in name for name in metadata.get('artifacts',{}))
            if safe_artifacts and metadata.get('inputs')==fingerprint and metadata.get('artifacts') and all((a.output/name).is_file() and sha(a.output/name)==digest for name,digest in metadata['artifacts'].items()):
                print('Reusing verified base-PS4 shader '+source.name);continue
        with tempfile.TemporaryDirectory(prefix='gnm-shader-',dir=scratch) as directory:
            stage=pathlib.Path(directory);base=stage/source.name;spv=pathlib.Path(str(base)+'.spv');gnm=pathlib.Path(str(base)+'.gnm.glsl');native_spv=pathlib.Path(str(base)+'.gnm.spv');reflection=pathlib.Path(str(base)+'.json');header=pathlib.Path(str(base)+'.h');binary=pathlib.Path(str(base)+'.sb')
            run([tools['glslang'],'-V','--target-env','vulkan1.0',source.resolve(),'-o',spv])
            run([tools['spirv-val'],'--target-env','vulkan1.0',spv])
            run([sys.executable,ROOT/'tools/prepare_gnm_shader.py',source.resolve(),gnm])
            run([tools['glslang'],'-V','--target-env','vulkan1.0','-S',source.suffix[1:],gnm,'-o',native_spv])
            run([tools['spirv-val'],'--target-env','vulkan1.0',native_spv])
            run([tools['spirv-cross'],spv,'--version','410','--no-es','--no-420pack-extension','--output',str(base)+'.glsl'])
            run([tools['spirv-cross'],spv,'--reflect','--output',reflection])
            run([sys.executable,ROOT/'tools/shader_metadata.py',reflection,spv,header,source.resolve()])
            compile_package(tools['psbc'],native_spv,'vertex' if source.suffix=='.vert' else 'fragment',binary)
            data=binary.read_bytes()
            validate(data)
            artifacts={path.name:sha(path) for path in stage.iterdir() if path.is_file() and path!=gnm}
            for name in artifacts:
                destination=a.output/name
                if not destination.exists() or sha(destination)!=artifacts[name]:os.replace(stage/name,destination)
            metadata={'schema':1,'target':'base-PS4 GFX7','entry_point':'main','inputs':fingerprint,'artifacts':artifacts}
            if psbc_provenance.is_file():metadata['psbc_build']=json.loads(psbc_provenance.read_text())
            part=old.with_suffix(old.suffix+'.part');part.write_text(json.dumps(metadata,indent=2)+'\n');os.replace(part,old)
        print('Compiled base-PS4 shader '+source.name)
if __name__=='__main__':
    try:main()
    except (OSError,ValueError,RuntimeError,subprocess.CalledProcessError) as e:print('GNM shader compilation: '+str(e),file=sys.stderr);raise SystemExit(1)
