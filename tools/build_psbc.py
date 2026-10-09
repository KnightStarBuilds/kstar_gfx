#!/usr/bin/env python3
"""Build the editable PSBC fork in an isolated, content-addressed source copy."""
import argparse, hashlib, json, os, platform, shutil, subprocess, sys, tempfile
from pathlib import Path
from fork_sources import FORKS, identity, snapshot
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--build-dir',type=Path,required=True);p.add_argument('--jobs',type=int,default=6);p.add_argument('--offline',action='store_true');a=p.parse_args()
 if a.jobs<1:p.error('--jobs must be positive')
 b=a.build_dir.resolve()
 forbidden=[ROOT/'deps/cache',*[ROOT/'deps'/name for name in FORKS]]
 if b==ROOT or ROOT not in b.parents or any(b==s or s in b.parents for s in forbidden):
  raise RuntimeError('Use a generated build directory below this repository, outside dependency sources')
 required=['vulkan-headers-psbc','spirv-headers','mako','markupsafe','pyyaml']
 scratch=ROOT/'.tmp';scratch.mkdir(exist_ok=True)
 with tempfile.NamedTemporaryFile(mode='w',suffix='.cmake',prefix='psbc-acquire-',dir=scratch,delete=False) as script:
  script.write('include([==['+str(ROOT/'cmake/AcquireInputs.cmake')+']==])\n')
  for name in required:script.write('kgfx_tool_source('+name+' unused)\n')
  acquisition=Path(script.name)
 try:
  acquisition_build=b.parent/'psbc-inputs';acquisition_build.mkdir(parents=True,exist_ok=True)
  subprocess.run(['cmake','-DPROJECT_BINARY_DIR='+str(acquisition_build),
      '-DKSTAR_GFX_OFFLINE='+('ON' if a.offline else 'OFF'),'-P',str(acquisition)],cwd=acquisition_build,check=True)
 finally:acquisition.unlink()
 lock=json.loads((ROOT/'deps/toolchain-lock.json').read_text())
 dependencies=[d for d in lock['dependencies'] if d['name'] in required]
 sources={d['name']:ROOT/'deps/cache/sources'/f"{d['name']}-{d['revision']}" for d in dependencies}
 forks={name:identity(name) for name in FORKS}
 driver_hash=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
 inputs=hashlib.sha256(json.dumps({'forks':forks,'dependencies':dependencies,'driver':driver_hash},sort_keys=True).encode()).hexdigest()
 if b.exists() and (b/'.kgfx-build-inputs').exists() and (b/'.kgfx-build-inputs').read_text()!=inputs:
  if not (b/'.kgfx-source.json').is_file():raise RuntimeError('Refusing to replace an unowned compiler directory')
  shutil.rmtree(b)
 forks['opengnm-psbc']=snapshot('opengnm-psbc',b)
 header_copy=acquisition_build/'deps-src/opengnm'
 forks['opengnm']=snapshot('opengnm',header_copy)
 sources['opengnm']=header_copy
 for name,dep in [('opengnm','opengnm'),('Vulkan-Headers','vulkan-headers-psbc'),('SPIRV-Headers','spirv-headers')]:
  dest=b.parent/name
  if dest.is_symlink() and dest.resolve()!=sources[dep]:dest.unlink()
  if not dest.exists():dest.symlink_to(sources[dep],target_is_directory=True)
  if dest.resolve()!=sources[dep].resolve():raise RuntimeError('Build dependency path is occupied by an unrelated directory: '+str(dest))
 env=os.environ.copy();env['PYTHONPYCACHEPREFIX']=str(b/'python-cache');env['PYTHONPATH']=os.pathsep.join(str(p) for p in [sources['mako'],sources['markupsafe']/'src',sources['pyyaml']/'lib'])
 # These maintained generator/configuration files live in the fork. The parent
 # supplies dependency locations and invokes them without rewriting any source.
 subprocess.run([sys.executable,'tools/generate_host.py'],cwd=b,env=env,check=True)
 options=['CONFIG=config.host.mak','PYTHON='+sys.executable]
 subprocess.run(['make',*options,f'-j{a.jobs}','generated'],cwd=b,env=env,check=True)
 subprocess.run(['make',*options,f'-j{a.jobs}'],cwd=b,env=env,check=True)
 (b/'.kgfx-build-inputs').write_text(inputs)
 manifest={'schema':2,'compiler_sha256':hashlib.sha256((b/'opengnm-psbc').read_bytes()).hexdigest(),
  'forks':forks,'dependencies':dependencies,'build_driver_sha256':driver_hash,
  'host':platform.platform(),'cxx_standard':'c++23'}
 (b/'build-provenance.json').write_text(json.dumps(manifest,indent=2)+'\n')
 print(b/'opengnm-psbc')
if __name__=='__main__':
 try:main()
 except (RuntimeError,OSError,subprocess.CalledProcessError) as e:print(f'PSBC build: {e}',file=sys.stderr);raise SystemExit(1)
