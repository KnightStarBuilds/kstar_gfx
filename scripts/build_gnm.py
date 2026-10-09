#!/usr/bin/env python3
"""Build host shader tools, pinned GNM, and PS4 ELF/FSELF packages."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--config',choices=('Debug','Release'),default='Debug')
p.add_argument('--sdk',type=Path,default=Path(os.environ['OO_PS4_TOOLCHAIN']) if os.environ.get('OO_PS4_TOOLCHAIN') else None)
p.add_argument('--llvm-bin',type=Path)
p.add_argument('--resources',type=Path,help='Existing host demo resources; otherwise build the desktop package')
p.add_argument('--host-tools',type=Path,help='Existing native CMake build containing shader tools')
p.add_argument('--probes-only',action='store_true',help='Build GPU conformance without curated model/HDRI assets')
p.add_argument('--library-only',action='store_true',help='Build the core with checked-in native internal shaders')
p.add_argument('--clean',action='store_true')
p.add_argument('--offline',action='store_true')
p.add_argument('--jobs',type=int,default=min(os.cpu_count() or 4,12))
a=p.parse_args()
if a.jobs<1:p.error('--jobs must be positive')
if a.library_only and a.probes_only:p.error('--library-only and --probes-only are mutually exclusive')
if a.sdk is None:p.error('Set OO_PS4_TOOLCHAIN or pass --sdk pointing to the pinned OpenOrbis SDK')
sdk=a.sdk.expanduser().resolve()
for file in ('lib/crt1.o','link.x'):
    if not (sdk/file).is_file():p.error('Invalid OpenOrbis SDK: missing '+file)
for program in ('cmake','ninja','git'):
    if shutil.which(program) is None:p.error(program+' is required')
if a.llvm_bin is None:
    compiler=shutil.which('clang')
    if not compiler:p.error('LLVM Clang is required; pass --llvm-bin')
    a.llvm_bin=Path(compiler).resolve().parent
llvm=a.llvm_bin.expanduser().resolve()
suffix='.exe' if platform.system()=='Windows' else ''
cross_tools={}
for file in ('clang','clang++','ld.lld','llvm-ar','llvm-ranlib'):
    candidate=llvm/(file+suffix)
    # Some LLVM distributions package lld separately from Clang.
    if not candidate.is_file() and file=='ld.lld' and shutil.which(file):candidate=Path(shutil.which(file))
    if not candidate.is_file():p.error('LLVM directory is missing '+file)
    cross_tools[file]=candidate
if a.resources is not None:
    a.resources=a.resources.expanduser().resolve()
    if not (a.resources/'assets/cooked').is_dir():p.error('--resources must contain assets/cooked')
if a.host_tools is not None:
    a.host_tools=a.host_tools.expanduser().resolve()
    if not (a.host_tools/'CMakeCache.txt').is_file():p.error('--host-tools is not a configured native build')
system={'Darwin':'macos','Linux':'linux','Windows':'windows'}.get(platform.system())
if system is None:p.error('Unsupported host')
if not a.library_only:
    converter={'macos':'bin/macos/create-fself-macos','linux':'bin/linux/create-fself','windows':'bin/windows/create-fself.exe'}[system]
    for file in (converter,'samples/hello_world/sce_module/libc.prx','samples/hello_world/sce_module/libSceFios2.prx'):
        if not (sdk/file).is_file():p.error('SDK is missing packaging input '+file)
compatibility=json.loads((root/'deps/sdk-compat.json').read_text())
sdk_types=sdk/compatibility['source_header']
if not sdk_types.is_file() or hashlib.sha256(sdk_types.read_bytes()).hexdigest()!=compatibility['source_header_sha256']:
    p.error('SDK type header differs from deps/sdk-compat.json; review the maintained compatibility header before using this SDK')
# Uninitialized or broken fork checkouts must not destroy an existing build.
for name in ('opengnm',) if a.library_only else ('opengnm','opengnm-psbc'):
    fork=root/'deps'/name
    if not (fork/'.git').exists():
        p.error('Missing '+name+' submodule; run git submodule update --init --recursive')
    try:
        top=subprocess.check_output(['git','-C',str(fork),'rev-parse','--show-toplevel'],text=True,stderr=subprocess.DEVNULL).strip()
        subprocess.check_output(['git','-C',str(fork),'rev-parse','--verify','HEAD'],stderr=subprocess.DEVNULL)
    except (OSError,subprocess.CalledProcessError):
        p.error('Invalid '+name+' Git checkout; restore the submodule before building')
    if Path(top).resolve()!=fork.resolve():p.error('Expected a separate Git working tree at '+str(fork))
# CMake can discard target options when a compiler spelling changes. Preserve
# equivalent cached compiler paths and reject an incompatible tree before work.
build=root/'build/ps4-x86_64/standalone'
def cache_values():
    result={}
    cache=build/'CMakeCache.txt'
    if cache.exists():
        for line in cache.read_text().splitlines():
            if not line or line.startswith(('#','//')) or '=' not in line or ':' not in line.split('=',1)[0]:continue
            key,value=line.split('=',1);result[key.split(':',1)[0]]=value
    return result
cached=cache_values()
if cached and not a.clean:
    if cached.get('KSTAR_GFX_BACKEND')!='GNM' or not cached.get('CMAKE_TOOLCHAIN_FILE'):
        p.error('Selected output tree is not a GNM cross build; rerun with --clean')
    for name,key in [('clang','CMAKE_C_COMPILER'),('clang++','CMAKE_CXX_COMPILER'),('ld.lld','CMAKE_LINKER'),('llvm-ar','CMAKE_AR'),('llvm-ranlib','CMAKE_RANLIB')]:
        old=cached.get(key)
        if old and Path(old).resolve()!=cross_tools[name].resolve():p.error('Cross compiler changed; rerun with --clean')
        if old:cross_tools[name]=Path(old)
    if Path(cached.get('OO_PS4_TOOLCHAIN',str(sdk))).resolve()!=sdk:p.error('SDK changed; rerun with --clean')
# All argument/tool/SDK/fork checks precede clean and acquisition.
if a.clean and build.exists():shutil.rmtree(build)
build.mkdir(parents=True,exist_ok=True)
arch={'aarch64':'arm64','amd64':'x86_64'}.get(platform.machine().lower(),platform.machine().lower())
host=root/'build'/f'{system}-{arch}'
def run(command):
    subprocess.run([str(x) for x in command],cwd=root,check=True)
def native_compilers():
    if system!='macos':return []
    return ['-DCMAKE_'+language+'_COMPILER='+subprocess.check_output(['xcrun','--find',name],text=True).strip() for language,name in [('C','clang'),('CXX','clang++')]]
def copy_tree(source,destination):
    for f in source.rglob('*'):
        if not f.is_file():continue
        out=destination/f.relative_to(source)
        if out.exists() and out.stat().st_size==f.stat().st_size and hashlib.sha256(out.read_bytes()).digest()==hashlib.sha256(f.read_bytes()).digest():continue
        out.parent.mkdir(parents=True,exist_ok=True);partial=out.with_name(out.name+'.part');shutil.copyfile(f,partial);os.replace(partial,out)
resources=build/'resources'/a.config
probe_shaders=build/'probe-shaders'/a.config
if not a.library_only:
    if not a.probes_only and a.resources is None:
        command=[sys.executable,root/'scripts/desktop.py','build','--config=Release','--jobs='+str(a.jobs)]
        if a.offline:command.append('--offline')
        run(command)
        a.resources=host/'bin/Release/resources'
    if a.host_tools is None:
        a.host_tools=host if (host/'tools/glslang').is_dir() else host/'gnm-host'
    if not (a.host_tools/'CMakeCache.txt').is_file():
        run(['cmake','-S',root,'-B',a.host_tools,'-G','Ninja Multi-Config',
             '-DKSTAR_GFX_BUILD_DEMOS=OFF','-DKSTAR_GFX_BUILD_TESTS=OFF',
             '-DKSTAR_GFX_BUILD_TOOLS=ON','-DKSTAR_GFX_OFFLINE='+('ON' if a.offline else 'OFF'),*native_compilers()])
    run(['cmake','--build',a.host_tools,'--config','Release','--target','glslang-standalone','spirv-val','spirv-cross','--parallel',a.jobs])
    psbc_build=host/'psbc-src'
    command=[sys.executable,root/'tools/build_psbc.py','--build-dir',psbc_build,'--jobs',a.jobs]
    if a.offline:command.append('--offline')
    run(command)
    psbc=psbc_build/('opengnm-psbc'+suffix)
    compile_base=[sys.executable,root/'tools/compile_gnm_shaders.py','--host-tools',a.host_tools,'--psbc',psbc,'--config','Release']
    run([*compile_base,'--source',root/'tests/gnm/shaders','--output',probe_shaders])
    run([sys.executable,root/'tools/test_gnm_shader_packages.py',probe_shaders])
    if not a.probes_only:
        copy_tree(a.resources,resources)
        run([*compile_base,'--source',root/'shaders','--output',resources/'shaders'])
        run([sys.executable,root/'tools/test_gnm_shader_packages.py',resources/'shaders'])

run(['cmake','-S',root,'-B',build,'-G','Ninja Multi-Config',
     '-DCMAKE_TOOLCHAIN_FILE='+str(root/'cmake/ps4-toolchain.cmake'),'-DKSTAR_GFX_BACKEND=GNM',
     '-DOO_PS4_TOOLCHAIN='+str(sdk),'-DCMAKE_C_COMPILER='+str(cross_tools['clang']),
     '-DCMAKE_CXX_COMPILER='+str(cross_tools['clang++']),'-DCMAKE_LINKER='+str(cross_tools['ld.lld']),
     '-DCMAKE_AR='+str(cross_tools['llvm-ar']),'-DCMAKE_RANLIB='+str(cross_tools['llvm-ranlib']),
     '-DKSTAR_GFX_BUILD_DEMOS='+('OFF' if a.library_only or a.probes_only else 'ON'),
     '-DKSTAR_GFX_BUILD_TESTS='+('OFF' if a.library_only else 'ON'),
     '-DKSTAR_GFX_OFFLINE='+('ON' if a.offline else 'OFF'),'-DKSTAR_GFX_LTO=ON',
     '-DKSTAR_GFX_HOST_RESOURCES='+str(resources),'-DKSTAR_GFX_PROBE_SHADERS='+str(probe_shaders)])
configured=cache_values()
if configured.get('KSTAR_GFX_BACKEND')!='GNM' or not configured.get('CMAKE_TOOLCHAIN_FILE'):
    raise RuntimeError('CMake lost the cross-toolchain configuration; refusing to build the wrong backend. Rerun with --clean.')
for key,expected in [('KSTAR_GFX_BUILD_DEMOS','OFF' if a.library_only or a.probes_only else 'ON'),('KSTAR_GFX_BUILD_TESTS','OFF' if a.library_only else 'ON')]:
    if configured.get(key)!=expected:raise RuntimeError('CMake lost requested build options; rerun with --clean')
run(['cmake','--build',build,'--config',a.config,'--parallel',a.jobs])
try:
    # rev-parse handles worktrees/submodules (.git files) and packed references.
    revision=subprocess.check_output(['git','rev-parse','--short','HEAD'],cwd=root,text=True,stderr=subprocess.DEVNULL).strip()
except (OSError,subprocess.CalledProcessError):
    revision='uncommitted'
provenance={'configuration':a.config,'backend':'GNM','sdk':str(sdk),'llvm_bin':str(llvm),'cross_tools':{name:str(path) for name,path in cross_tools.items()},
            'compiler':subprocess.check_output([str(cross_tools['clang']),'--version'],text=True).splitlines()[0],
            'sdk_crt1_sha256':hashlib.sha256((sdk/'lib/crt1.o').read_bytes()).hexdigest(),
            'sdk_link_script_sha256':hashlib.sha256((sdk/'link.x').read_bytes()).hexdigest(),
            'sdk_types_sha256':hashlib.sha256((sdk/'include/bits/alltypes.h').read_bytes()).hexdigest(),
            'sdk_compatibility_sha256':hashlib.sha256((root/'src/gnm/sdk-include/bits/alltypes.h').read_bytes()).hexdigest(),
            'runtime_lock_sha256':hashlib.sha256((root/'deps/lock.json').read_bytes()).hexdigest(),
            'toolchain_lock_sha256':hashlib.sha256((root/'deps/toolchain-lock.json').read_bytes()).hexdigest(),
            'source_revision':revision}
(build/('provenance-'+a.config+'.json')).write_text(json.dumps(provenance,indent=2)+'\n')
print('GNM build complete: '+str(build/a.config/'libkstar_gfx.a' if a.library_only else build/'app0'/a.config))
