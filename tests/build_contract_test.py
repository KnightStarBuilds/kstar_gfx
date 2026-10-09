#!/usr/bin/env python3
"""Exercise destructive build flags in an isolated checkout, never the live tree."""
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile

source=Path(__file__).resolve().parents[1]
scratch=source/'.tmp'
scratch.mkdir(exist_ok=True)
def run(args,cwd,success=True):
    result=subprocess.run(args,cwd=cwd,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    if (result.returncode==0)!=success:
        raise RuntimeError(' '.join(map(str,args))+'\n'+result.stdout)
    return result.stdout
with tempfile.TemporaryDirectory(prefix='build contract ',dir=scratch) as temp:
    root=Path(temp)/'standalone checkout'
    root.mkdir()
    for directory in ('include','cmake','scripts','src/opengl'):
        shutil.copytree(source/directory,root/directory)
    for name in ('CMakeLists.txt','LICENSE','build.sh','run.sh'):
        shutil.copy2(source/name,root/name)
    system={'Darwin':'macos','Linux':'linux','Windows':'windows'}[platform.system()]
    arch={'aarch64':'arm64','amd64':'x86_64'}.get(platform.machine().lower(),platform.machine().lower())
    tree=root/'build'/f'{system}-{arch}'
    tree.mkdir(parents=True)
    sentinel=tree/'old-output';sentinel.write_text('replace me')
    preserve=[root/'assets'/'source'/'asset.bin',root/'deps'/'cache'/'dependency.tar.gz',root/'build'/'another-platform'/'keep.bin']
    for path in preserve:
        path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(os.urandom(1024))
    before={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in preserve}
    run(['./build.sh','--clean','--config=not-valid'],root,False)
    assert sentinel.exists(),'invalid arguments mutated outputs'
    # A valid-looking cross SDK without its package converter must be rejected
    # before --clean removes previously built PS4 output.
    ps4=root/'build/ps4-x86_64/standalone';ps4.mkdir(parents=True)
    cross_sentinel=ps4/'keep';cross_sentinel.write_text('previous PS4 build')
    sdk=root/'incomplete SDK';(sdk/'lib').mkdir(parents=True)
    (sdk/'lib/crt1.o').touch();(sdk/'link.x').touch()
    llvm=root/'fake LLVM';llvm.mkdir()
    for name in ('clang','clang++','ld.lld','llvm-ar','llvm-ranlib'):
        (llvm/(name+('.exe' if system=='windows' else ''))).touch()
    rejected=run([sys.executable,str(root/'scripts/build_gnm.py'),'--clean','--probes-only','--sdk',str(sdk),'--llvm-bin',str(llvm)],root,False)
    assert 'SDK is missing packaging input' in rejected,rejected
    assert cross_sentinel.exists(),'incomplete cross SDK caused destructive clean'
    # Missing editable dependencies must also preserve an otherwise valid tree.
    types=sdk/'include/bits/alltypes.h';types.parent.mkdir(parents=True)
    types.write_text('/* isolated preflight fixture */\n')
    (root/'deps/sdk-compat.json').write_text(json.dumps({
        'source_header':'include/bits/alltypes.h',
        'source_header_sha256':hashlib.sha256(types.read_bytes()).hexdigest()}))
    cross=[sys.executable,str(root/'scripts/build_gnm.py'),'--clean','--offline','--sdk',str(sdk),'--llvm-bin',str(llvm)]
    rejected=run([*cross,'--library-only'],root,False)
    assert 'Missing opengnm submodule' in rejected,rejected
    assert cross_sentinel.exists(),'missing OpenGNM submodule caused destructive clean'
    fork=root/'deps/opengnm';fork.mkdir()
    run(['git','init',str(fork)],root)
    run(['git','-c','user.name=Build contract','-c','user.email=build-contract@example.invalid','commit','--allow-empty','-m','Fixture'],fork)
    converter={'macos':'bin/macos/create-fself-macos','linux':'bin/linux/create-fself','windows':'bin/windows/create-fself.exe'}[system]
    for name in (converter,'samples/hello_world/sce_module/libc.prx','samples/hello_world/sce_module/libSceFios2.prx'):
        item=sdk/name;item.parent.mkdir(parents=True,exist_ok=True);item.touch()
    rejected=run([*cross,'--probes-only'],root,False)
    assert 'Missing opengnm-psbc submodule' in rejected,rejected
    assert cross_sentinel.exists(),'missing PSBC submodule caused destructive clean'
    run(['./build.sh','--clean','--offline','--library-only'],root)
    assert not sentinel.exists(),'clean retained stale selected output'
    assert before=={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in preserve},'clean touched immutable inputs or another platform'
    lib=tree/'Debug'/'libkstar_gfx.a'
    if lib.exists():
        timestamp=lib.stat().st_mtime_ns
        run(['./build.sh','--offline','--library-only'],root)
        assert lib.stat().st_mtime_ns==timestamp,'incremental offline rebuild needlessly rebuilt archive'
    run(['./build.sh','--offline','--library-only','--config=Release'],root)
    prefix=root/'install prefix'
    run(['cmake','--install',str(tree),'--config','Release','--prefix',str(prefix)],root)
    consumer=root/'consumer project';consumer.mkdir()
    common='add_executable(consumer main.c)\ntarget_link_libraries(consumer PRIVATE kstar_gfx::gfx)\nset_target_properties(consumer PROPERTIES LINKER_LANGUAGE CXX)\n'
    preamble='cmake_minimum_required(VERSION 3.24)\nproject(consumer LANGUAGES C CXX)\n'
    (consumer/'CMakeLists.txt').write_text(preamble+'find_package(kstar_gfx CONFIG REQUIRED)\n'+common)
    (consumer/'main.c').write_text('#include <kstar_gfx/gfx.h>\n#include <string.h>\nint main(void){return strcmp(kgfx_backend_name(),"OpenGL")!=0;}\n')
    configure=['cmake','-S',str(consumer),'-B',str(consumer/'build'),'-G','Ninja Multi-Config','-DCMAKE_PREFIX_PATH='+str(prefix)]
    if system=='macos':
        for language,compiler in [('C','clang'),('CXX','clang++')]:
            configure.append('-DCMAKE_'+language+'_COMPILER='+subprocess.check_output(['xcrun','--find',compiler],text=True).strip())
    run(configure,root)
    run(['cmake','--build',str(consumer/'build'),'--config','Release'],root)
    exe=consumer/'build'/'Release'/('consumer.exe' if system=='windows' else 'consumer')
    run([str(exe)],root)
    # Source dependency integration must work with the same public target and
    # default to library-only when this project is not the top-level checkout.
    (consumer/'CMakeLists.txt').write_text(preamble+'set(KSTAR_GFX_OFFLINE ON CACHE BOOL "")\nadd_subdirectory(".." kstar_gfx)\n'+common)
    embedded=consumer/'submodule build'
    source_configure=list(configure)
    source_configure[source_configure.index('-B')+1]=str(embedded)
    run(source_configure,root)
    run(['cmake','--build',str(embedded),'--config','Release'],root)
    run([str(embedded/'Release'/('consumer.exe' if system=='windows' else 'consumer'))],root)
    print('PASS: invalid flags/SDKs/missing submodules preserve outputs; clean preserves cache/assets/other platforms; standalone offline Debug+Release; path with spaces; installed and source-subdirectory C consumers')
