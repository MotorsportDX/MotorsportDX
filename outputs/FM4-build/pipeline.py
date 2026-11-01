"""Windows source-only bootstrap. Private outputs stay below build-root."""
import argparse
import contextlib
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import subprocess
import sys
import tarfile
import time
import urllib.request
import zipfile

HERE = Path(__file__).resolve().parent
# Python's embeddable package (the builder's, docs/setup.md#portable-tools) fixes sys.path in its
# ._pth file, without the script's folder; so does -I.
if str(HERE) not in sys.path:
    sys.path.insert(0,str(HERE))
from prepare import write_switches

REPO = HERE.parents[1]
# The runtime (ReXGlue SDK fork, D3D12 only) lives in work/runtime. Its
# third-party libraries still come from the upstream SDK at this revision:
# the directories it vendors and the submodules the D3D12 build needs.
RUNTIME = REPO/'work/runtime'
SDK_REV = 'f5337cdc947ff6d4c4196737e2c807a48f2a1fc2'
SDK_URL = 'https://github.com/rexglue/rexglue-sdk.git'
THIRDPARTY_VENDORED = ('aes_128','crypto','disasm','disruptorplus','dxbc','dxc','ffmpeg-overlay','pe',
                       'renderdoc','stb','tiny-aes-c')
THIRDPARTY_SUBMODULES = ('cli11','libmspack','FFmpeg','tomlplusplus','simde','xxHash','spdlog','fmt',
                         'utfcpp','imgui','sdl3','inja','o1heap')
# AMD FidelityFX SDK for REX_FSR (FSR 3.1 from source, docs/analysis/fsr-plan.md section 10):
# setup fetches a sparse, blob-filtered clone of the files the FSR 3 upscaler and the DX12
# backend build from, pinned to the v1.1.4 tag's commit, outside the repository in
# <BuildRoot>/fidelityfx-sdk, and refuses any other commit; FFX_MIRROR names a fallback.
FFX_URL = 'https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK'
FFX_TAG = 'v1.1.4'
FFX_REV = 'c6efa6bf7f2027b3ec94f28578bb5965eabb9e55'
# /.gitattributes: the repository's line endings (eol=lf), which a checkout from the mirror
# bundle needs to match one from GitHub.
FFX_PATHS = ('/.gitattributes','/LICENSE.txt','/sdk/LICENSE.txt','/sdk/CMakeLists.txt',
             '/sdk/toolchain.cmake','/sdk/include/','/sdk/src/components/fsr3upscaler/',
             '/sdk/src/backends/dx12/','/sdk/src/backends/shared/','/sdk/src/shared/',
             '/sdk/tools/binary_store/')
# Microsoft.GameInput (GameInput API v3, for sdl_joystick_driver=gameinput;
# docs/analysis/modern-gamepads.md): setup fetches the pinned NuGet package into
# <BuildRoot>/gameinput-sdk/<version>, refuses any other SHA-512, and extracts only the files the
# package licenses under MIT, the public header and the static library's loader source.
# GAMEINPUT_MIRROR names a fallback: another URL of the package or a local .nupkg file.
GAMEINPUT_VERSION = '3.5.283'
GAMEINPUT_URL = ('https://api.nuget.org/v3-flatcontainer/microsoft.gameinput/'
                 f'{GAMEINPUT_VERSION}/microsoft.gameinput.{GAMEINPUT_VERSION}.nupkg')
GAMEINPUT_SHA512 = ('59083faf7e0fc669f6caf0c39642b8a4cb9825faf9b17acc4b030d0b8ebe2a72'
                    '19d512397831e98321b281b3a5757a963ac5dcd61ba57f60ff2890fb7fe77705')
GAMEINPUT_FILES = {'native/include/GameInput.h':'include/gameinput.h',
                   'native/src/GameInput.cpp':'src/GameInput.cpp'}
# Patches to the fetched third-party sources (patch, directory under runtime-source/thirdparty),
# applied by patch_thirdparty: each must apply exactly, and setup refuses a mismatch.
THIRDPARTY_PATCHES = (('sdl3-gameinput-v3.patch','sdl3'),)
NOTICES = REPO/'outputs/FM4-source-control/third-party-notices'
# The acknowledgements beside the launcher; its About page opens them (docs/running.md).
CREDITS = HERE/'CREDITS.txt'
# The project's own license (BSD-3-Clause), beside them as LICENSE.txt.
LICENSE = REPO/'LICENSE'
RESOLVE_SHADERS = [f'resolve_full_{bits}bpp{suffix}' for bits in (8,16,32,64) for suffix in ('','_scaled')]
# The fast copy of 32bpp 4x MSAA surfaces with a per-pixel EDRAM address, in place of upstream's
# precompiled one, which read the vertical sample at the depth tile's half swap (d3d-hle-n3.md 16.17).
RESOLVE_SHADERS += ['resolve_fast_32bpp_4xmsaa', 'resolve_fast_32bpp_4xmsaa_scaled']
# Resolves straight into host textures (stage N3 increment 1, docs/analysis/d3d-hle.md 16.7).
RESOLVE_SHADERS.append('resolve_texture_32bpp')
# Resolves straight from the host render targets (stage N3 increment 3, d3d-hle.md 16.9): one
# source (resolve_host_32bpp.xesli) built per source kind, multisampling and scaling.
RESOLVE_HOST_SHADERS = {
    f'resolve_host_32bpp_{kind}{ms}{scaled}': [f'XE_HOST_SOURCE_KIND={index}'] +
    (['XE_HOST_SOURCE_MS'] if ms else []) + (['XE_RESOLVE_RESOLUTION_SCALED'] if scaled else [])
    for index, kind in enumerate(('color', 'uint', 'depth'))
    for ms in ('', '_ms') for scaled in ('', '_scaled')}
# The same source writing the host texture of the destination instead (d3d-hle-n5.md 18.12).
RESOLVE_HOST_SHADERS.update({
    f'resolve_host_texture_32bpp_{kind}{ms}': [f'XE_HOST_SOURCE_KIND={index}'] +
    (['XE_HOST_SOURCE_MS'] if ms else []) + ['XE_RESOLVE_RESOLUTION_SCALED', 'XE_HOST_TEXTURE']
    for index, kind in enumerate(('color', 'uint', 'depth')) for ms in ('', '_ms')})
# The destination from the words of its host texture at the scene's scale (d3d-hle-n6.md 19.7.5).
RESOLVE_HOST_SHADERS.update({
    f'resolve_host_32bpp_words{scaled}': ['XE_HOST_SOURCE_KIND=3'] +
    (['XE_RESOLVE_RESOLUTION_SCALED'] if scaled else []) for scaled in ('', '_scaled')})
EXECUTABLES = ('default.xex','SpeechFacade_default.xex','XMediaFacade_default.xex')
# The supported releases and the SHA-256 of their three executables. The releases differ only
# in the XEX headers (media ID, disc version, game region, ratings, LAN key). The decrypted
# images of the three executables and every other file of both discs are identical, so the
# function bounds and switch tables serve both.
RELEASES = {
    'Japan/Asia': {'media_id':'633772A0','executables':{
        'default.xex': '763636fe8cc9d2ff822ade4c972efef907e193f31175def127863575bf3c4ad5',
        'SpeechFacade_default.xex': 'cf06cac7c34e38a9b45db5c7da4a690c7414c8b461537463d3162465e89431bd',
        'XMediaFacade_default.xex': 'fec814d3066c73b182543e80a50c7c7caf48749e4026ae933bf19654d4f118a7'}},
    'USA': {'media_id':'26FF1101','executables':{
        'default.xex': '3618e85af83e972ddcc1b0d0dd808e78e29ab4ab3890a15b31c210a6a3e93e09',
        'SpeechFacade_default.xex': 'b8a5e5137bb18ec42c7e0e2fe5b3e5e760e11e0b0d5ddf0098186995aa7c0f5e',
        'XMediaFacade_default.xex': '560bb53fc9a335399e82ba92121410e6781841cbabe3e9dfbf0de43dde7258e9'}},
}

def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def copy_source(source, destination):
    """Keep timestamps stable when regenerating an unchanged private project. A copied file
    gets the current time, so an object compiled from another checkout's newer version of it
    does not look up to date."""
    source, destination = Path(source), Path(destination)
    if not destination.is_file() or source.read_bytes() != destination.read_bytes():
        shutil.copyfile(source, destination)
        os.utime(destination)

def git_output(*args):
    """The stripped output of a git command in the repository, or None when it fails."""
    try:
        result=subprocess.run(['git','-C',REPO,*args],capture_output=True,text=True)
    except OSError:
        return None
    return result.stdout.strip() if result.returncode==0 else None

def is_git_checkout():
    """Whether the source is a Git checkout (a worktree's .git is a file), not a snapshot."""
    return (REPO/'.git').exists()

# A source snapshot (make_snapshot, docs/setup.md#source-snapshots) is a git archive of one
# commit, which has no Git metadata: its version stamp names the commit, and source_state reads it.
# STAMP_TREES are the folders whose files the stamp's tree hash covers, the ones source_state's
# dirty flag watches in a checkout.
STAMP_NAME='source-stamp.json'
STAMP_TREES=('work/runtime','work/fm4-rex')

def snapshot_tree(directory):
    """A SHA-256 over the paths and contents of the files of STAMP_TREES under directory."""
    directory=Path(directory)
    files=sorted((path.relative_to(directory).as_posix(),path) for top in STAMP_TREES
                 for path in (directory/top).rglob('*') if path.is_file())
    tree=hashlib.sha256()
    for name,path in files:
        tree.update(name.encode()+b'\0'+bytes.fromhex(digest(path)))
    return tree.hexdigest()

def read_stamp(directory=None):
    """The version stamp of a snapshot, or None when there is none."""
    path=Path(directory or REPO)/STAMP_NAME
    return json.loads(path.read_text(encoding='utf-8')) if path.is_file() else None

def source_state(root):
    """What a build compiled: the HEAD of the checkout that ran it, whether its work/runtime or
    work/fm4-rex differ from HEAD, and a SHA-256 over the paths and contents of the BuildRoot's
    runtime-source (its mirror listing) and the project's title sources (src, tests,
    CMakeLists.txt), which another checkout may have left there. In a snapshot the stamp's
    commit, and whether those folders differ from the stamp's tree hash (None without a stamp)."""
    source,project=root/'runtime-source',root/'project'
    listing=source/'.fm4-runtime-files.json'
    files=[('runtime-source/'+relative,source/relative)
           for relative in (json.loads(listing.read_text()) if listing.is_file() else [])]
    for directory in ('src','tests'):
        files+=[(path.relative_to(root).as_posix(),path)
                for path in (project/directory).rglob('*') if path.is_file()]
    if (project/'CMakeLists.txt').is_file():
        files.append(('project/CMakeLists.txt',project/'CMakeLists.txt'))
    tree=hashlib.sha256()
    for name,path in sorted(files):
        if path.is_file():
            tree.update(name.encode()+b'\0'+bytes.fromhex(digest(path)))
    if not is_git_checkout():
        stamp=read_stamp()
        if stamp is None:
            return {'head':None,'dirty':None,'tree':tree.hexdigest()}
        return {'head':stamp['commit'],'dirty':snapshot_tree(REPO)!=stamp['tree'],'tree':tree.hexdigest()}
    status=git_output('status','--porcelain','--',*STAMP_TREES)
    return {'head':git_output('rev-parse','HEAD'),'dirty':bool(status),'tree':tree.hexdigest()}

def make_snapshot(out,revision='HEAD'):
    """A source snapshot in out (new or empty, outside the repository): the git archive of
    revision unpacked, and STAMP_NAME beside it with the commit, its date, the tag description,
    the number of files and the tree hash of STAMP_TREES (snapshot_tree). Uncommitted changes
    are not in it. The builder package's source folder (docs/analysis/builder-installer.md)."""
    if not is_git_checkout():
        raise RuntimeError(f'snapshot needs a Git checkout; {REPO} is not one.')
    out=Path(out).resolve()
    if out==REPO or REPO in out.parents or out in REPO.parents:
        raise ValueError('The snapshot folder must be outside the repository.')
    if out.exists() and any(out.iterdir()):
        raise ValueError(f'{out} is not empty.')
    commit=git_output('rev-parse','--verify',f'{revision}^{{commit}}')
    if not commit:
        raise ValueError(f'{revision} is not a commit of {REPO}.')
    if commit==git_output('rev-parse','HEAD') and git_output('status','--porcelain'):
        print('The checkout has uncommitted changes; the snapshot holds the commit only.')
    out.mkdir(parents=True,exist_ok=True)
    archive=out.parent/f'{out.name}.tar.partial'
    try:
        with archive.open('wb') as stream:
            subprocess.run(['git','-C',REPO,'archive','--format=tar',commit],stdout=stream,check=True)
        with tarfile.open(archive) as tar:
            tar.extractall(out,filter='data')
    finally:
        archive.unlink(missing_ok=True)
    stamp={'schema':1,'commit':commit,'date':git_output('show','-s','--format=%cI',commit),
           'describe':git_output('describe','--tags','--always',commit),
           'files':sum(1 for path in out.rglob('*') if path.is_file()),
           'tree':snapshot_tree(out)}
    (out/STAMP_NAME).write_text(json.dumps(stamp,indent=1)+'\n',encoding='utf-8')
    print(f'Snapshot of {commit[:7]} in {out}: {stamp["files"]} files.')
    return stamp

# --progress (docs/setup.md#progress-lines): one JSON line for each stage and step as it starts
# and ends, for a builder that shows the build's progress. PROGRESS is the open stream, or None.
PROGRESS=None
PROGRESS_SCHEMA=1
_progress_stage=[None]

def progress_event(event,name,state,elapsed=0.0,log=None,error=None):
    if PROGRESS is None:
        return
    line={'schema':PROGRESS_SCHEMA,'event':event,
          'stage':name if event=='stage' else _progress_stage[0],
          'step':name if event=='step' else None,'state':state,'elapsed':round(elapsed,1),
          'log':str(log) if log else None,
          'time':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())}
    if error is not None:
        line['error']=str(error)
    PROGRESS.write(json.dumps(line)+'\n')
    PROGRESS.flush()

@contextlib.contextmanager
def progress(event,name,log=None):
    """Writes the running line of a stage (setup, generate, build) or a step (a process with
    its log, or a fetch), then its done or failed line with the elapsed seconds."""
    started=time.monotonic()
    if event=='stage':
        _progress_stage[0]=name
    progress_event(event,name,'running',log=log)
    try:
        yield
    except BaseException as error:
        progress_event(event,name,'failed',time.monotonic()-started,log,error)
        raise
    else:
        progress_event(event,name,'done',time.monotonic()-started,log)
    finally:
        if event=='stage':
            _progress_stage[0]=None

def run(args, cwd=None, log=None, env=None):
    args = [str(x) for x in args]
    print('Running:', subprocess.list2cmdline(args), flush=True)
    if log:
        log.parent.mkdir(parents=True, exist_ok=True)
        with progress('step',log.stem,log):
            with log.open('w', encoding='utf-8') as output:
                result = subprocess.run(args, cwd=cwd, stdout=output, stderr=subprocess.STDOUT, env=env)
            if result.returncode:
                print(log.read_text(errors='replace')[-6000:])
                raise RuntimeError(f'Exit {result.returncode}; log: {log}')
    else:
        subprocess.run(args, cwd=cwd, env=env, check=True)

# The C and C++ libraries and the ABI the build targets (--abi, docs/setup.md#toolchain). mingw, the
# default: llvm-mingw's clang with libc++, mingw-w64's headers and import libraries and the
# Universal CRT, through the CMake toolchain file MINGW_TOOLCHAIN_FILE; nothing of Microsoft's
# toolchain or SDK (docs/analysis/mingw-toolchain.md). msvc: LLVM's clang with Microsoft's C and
# C++ libraries and the Windows SDK, the build before the switch, kept only for the A/B against
# the mingw build while the switch is verified, then removed.
ABIS=('mingw','msvc')
ABI='mingw'
MINGW_TOOLCHAIN_FILE=HERE/'llvm-mingw.cmake'
# llvm-mingw's C++ runtime, which every module links as DLLs: staged beside rexglue.exe, the game
# and the tests (the launcher links it statically).
MINGW_RUNTIME_DLLS=('libc++.dll','libunwind.dll')
# The mingw build's debug information: CodeView line tables in PDBs beside fm4_rex.exe, its two
# modules, rexruntime.dll, rexgpu-xenos.dll and rexglue.exe (lld's --pdb with no name: the
# binary's own with .pdb). Without them lld keeps the generated code's line tables in
# fm4_rex.exe as DWARF (51 MB). --strip-all keeps the DWARF of llvm-mingw's own prebuilt objects
# (the CRT's) and the COFF symbol table out of the binaries, as the MSVC build has none:
# llvm-symbolizer reads a binary's DWARF before its PDB and would find no game function.
MINGW_DEBUG_FLAGS='-gline-tables-only -gcodeview'
MINGW_PDB_LINK='-Wl,--pdb= -Wl,--strip-all'
# The runtime's PDBs, written beside its DLLs in runtime-source/out/win-amd64.
RUNTIME_PDBS=('rexruntime.pdb','rexgpu-xenos.pdb')
# The build's tools on PATH, per ABI. mingw: CMake drives Ninja; llvm-mingw's
# x86_64-w64-mingw32-clang and -clang++ compile and link with ld.lld (its MinGW driver); llvm-ar
# makes the static libraries. msvc: clang and clang++ compile and link with lld-link
# (-fuse-ld=lld-link, which CMake adds when it finds lld-link). Nothing of Visual Studio runs (no
# link, no fxc, no vswhere).
DOCTOR_TOOLS={'mingw':('cmake','ninja','x86_64-w64-mingw32-clang','x86_64-w64-mingw32-clang++',
                       'ld.lld','llvm-ar'),
              'msvc':('cmake','ninja','clang','clang++','lld-link','llvm-ar')}
# The compiler whose version doctor reads, and the target it must report.
DOCTOR_COMPILERS={'mingw':('x86_64-w64-mingw32-clang++','x86_64-w64-windows-gnu'),
                  'msvc':('clang++','x86_64-pc-windows-msvc')}
# What the mingw build reads of llvm-mingw's folder: mingw-w64's headers and import libraries,
# the Universal CRT's, libc++ and the runtime DLLs.
MINGW_FILES=('include/windows.h','include/d3d12.h','include/c++/v1/vector',
             'x86_64-w64-mingw32/lib/libucrt.a','x86_64-w64-mingw32/lib/libd3d12.a',
             'x86_64-w64-mingw32/lib/libc++.dll.a',
             *(f'x86_64-w64-mingw32/bin/{name}' for name in MINGW_RUNTIME_DLLS))
# What clang and lld-link need of Microsoft's C and C++ libraries, the Universal CRT and the
# Windows SDK with --abi msvc, looked for in the folders of INCLUDE and LIB: Visual Studio's x64
# environment.
MSVC_HEADERS=('vcruntime.h','corecrt.h','windows.h','d3d12.h')
MSVC_LIBRARIES=('msvcrt.lib','ucrt.lib','kernel32.lib','d3d12.lib')

def mingw_root():
    """The llvm-mingw folder of the x86_64-w64-mingw32-clang++ on PATH."""
    found=shutil.which('x86_64-w64-mingw32-clang++')
    if not found:
        raise RuntimeError('Missing x86_64-w64-mingw32-clang++ (llvm-mingw). See docs/setup.md: the '
                           'portable tools (--toolchain), or Build.ps1 -MingwRoot.')
    return Path(found).resolve().parent.parent

def compilers(*languages):
    """The CMake arguments that pick the compilers of a project: with mingw, the toolchain file
    and llvm-mingw's folder; with msvc, clang or clang++ for each of languages (C, CXX)."""
    if ABI=='mingw':
        return [f'-DCMAKE_TOOLCHAIN_FILE={MINGW_TOOLCHAIN_FILE.as_posix()}',
                f'-DFM4_MINGW_ROOT={mingw_root().as_posix()}']
    return [f'-DCMAKE_{language}_COMPILER={"clang" if language=="C" else "clang++"}'
            for language in languages]

def stage_mingw_runtime(directory):
    """With mingw, MINGW_RUNTIME_DLLS from llvm-mingw's x86_64 folder into directory."""
    if ABI!='mingw':
        return
    source=mingw_root()/'x86_64-w64-mingw32/bin'
    for name in MINGW_RUNTIME_DLLS:
        target=Path(directory)/name
        if not target.is_file() or digest(target)!=digest(source/name):
            shutil.copyfile(source/name,target)

def build_root_abi(root):
    """The ABI a BuildRoot's build folders hold: setup-complete.json's abi; msvc for one set up
    before the switch (no abi there); for a runtime build folder without setup-complete.json, its
    compiler; None for a new BuildRoot."""
    state=root/'setup-complete.json'
    if state.is_file():
        return json.loads(state.read_text(encoding='utf-8')).get('abi','msvc')
    cache=root/'runtime-build/CMakeCache.txt'
    if cache.is_file():
        return 'mingw' if 'x86_64-w64-mingw32-clang' in cache.read_text(errors='replace') else 'msvc'
    return None

def check_abi(root):
    """Refuses to build into a BuildRoot that holds the other ABI's objects: its CMake folders would
    be configured again with the other compiler, beside an install and game build of the old one."""
    abi=build_root_abi(root)
    if abi and abi!=ABI:
        raise RuntimeError(f'{root} holds a build with --abi {abi}, not {ABI}. Pass --abi {abi}, or '
                           'build into another BuildRoot (docs/setup.md#toolchain).')

def msvc_libraries_missing():
    """The files of MSVC_HEADERS and MSVC_LIBRARIES in no folder of INCLUDE and LIB."""
    missing=[]
    for variable,names in (('INCLUDE',MSVC_HEADERS),('LIB',MSVC_LIBRARIES)):
        folders=[Path(folder) for folder in os.environ.get(variable,'').split(os.pathsep) if folder]
        missing+=[f'{name} ({variable})' for name in names
                  if not any((folder/name).is_file() for folder in folders)]
    return missing

def doctor(fetch='git'):
    """The tools of DOCTOR_TOOLS[ABI] on PATH (and git with fetch git; archive, fetch_mode, needs
    no Git), clang 20 for the ABI's target, CMake 3.25+ and Windows' d3dcompiler_47.dll; with
    mingw, llvm-mingw's MINGW_FILES; with msvc, Microsoft's libraries in INCLUDE and LIB
    (msvc_libraries_missing)."""
    if os.name != 'nt' or sys.maxsize <= 2**32 or sys.version_info < (3,11):
        raise RuntimeError('Requires Windows x64 and Python 3.11 or newer (64-bit).')
    for name in (('git',) if fetch=='git' else ())+DOCTOR_TOOLS[ABI]:
        if not shutil.which(name):
            raise RuntimeError(f'Missing {name}. See docs/setup.md: the portable tools (--toolchain), '
                               'or Build.ps1 -MingwRoot'+(' from a VS C++ environment' if ABI=='msvc' else '')+'.')
    compiler,target=DOCTOR_COMPILERS[ABI]
    version = subprocess.check_output([compiler, '--version'], text=True)
    if not re.search(r'clang version 20\.', version):
        raise RuntimeError(f'{compiler}: use clang 20.x (validated: 20.1.8, llvm-mingw 20250709).')
    if not re.search(rf'^Target: {re.escape(target)}$', version, re.M):
        raise RuntimeError(f'{compiler} does not target {target}, which --abi {ABI} needs '
                           '(docs/setup.md#toolchain).')
    cmake_version = subprocess.check_output(['cmake','--version'],text=True)
    match = re.search(r'version (\d+)\.(\d+)',cmake_version)
    if not match or tuple(map(int, match.groups())) < (3,25):
        raise RuntimeError('CMake 3.25+ is required.')
    try:
        d3dcompiler()
    except OSError as error:
        raise RuntimeError(f"Windows' d3dcompiler_47.dll cannot be loaded: {error}") from error
    if ABI=='mingw':
        missing=[name for name in MINGW_FILES if not (mingw_root()/name).is_file()]
        if missing:
            raise RuntimeError(f'llvm-mingw in {mingw_root()} lacks '+', '.join(missing)+
                               ' (docs/setup.md#portable-tools).')
    else:
        missing = msvc_libraries_missing()
        if missing:
            raise RuntimeError('Microsoft C/C++ libraries or Windows SDK not found: '+', '.join(missing)+
                               '. Run from the Visual Studio x64 environment (Build.ps1 -Abi msvc) or '
                               'set INCLUDE and LIB (docs/setup.md).')
    print('Preflight passed. ABI:', ABI, 'Compiler:', version.splitlines()[0], 'CMake:',
          shutil.which('cmake'), 'Fetch:', fetch)

# The builder's portable tools (docs/setup.md#portable-tools, docs/analysis/builder-installer.md
# 4.4 to 4.6): toolchain.json pins each tool's archive by URL, size and SHA-256, with its licence,
# the versions accepted of an installed copy, and the members to keep. The toolchain action finds
# installed tools first (detect_tools) and fetches and unpacks only the missing ones, or all of
# them with --portable, into the --toolchain folder; with --toolchain, the other actions put that
# folder's tools first on PATH (use_toolchain).
TOOLCHAIN=HERE/'toolchain.json'
TOOLCHAIN_SCHEMA=1
TOOLCHAIN_STATE='toolchain-state.json'
TOOL_MARKER='.fm4-tool.json'
TOOL_FORMATS=('zip','tar.xz')
# Per tool: the executable whose version is read, its arguments, the version's pattern, and the
# other executables that must stand beside it.
TOOL_PROBES={'python':('python.exe',['-I','-c','import sys;print("%d.%d.%d"%sys.version_info[:3])'],
                       r'^(\d+(?:\.\d+)+)',()),
             'cmake':('cmake.exe',['--version'],r'cmake version (\d+(?:\.\d+)+)',()),
             'ninja':('ninja.exe',['--version'],r'^(\d+(?:\.\d+)+)',()),
             # llvm-mingw's version is its release date; an installed copy is accepted by its clang's.
             'llvm-mingw':('x86_64-w64-mingw32-clang.exe',['--version'],r'clang version (\d+(?:\.\d+)+)',
                           ('x86_64-w64-mingw32-clang++.exe','ld.lld.exe','llvm-ar.exe'))}

def version_tuple(text):
    return tuple(int(part) for part in text.split('.'))

def load_toolchain(path=TOOLCHAIN):
    """toolchain.json's tools, checked: per tool (one of TOOL_PROBES) its version, an https url,
    size, sha256, license, license_file (a kept member or None), format (TOOL_FORMATS), top (the
    archive's top folder or ''), keep (None for every member, or files and folders ending in
    '/'), bin (the executables' folder in the unpacked tree) and accept ([lowest, below], the
    versions taken from an installed copy; below may be None)."""
    data=json.loads(Path(path).read_text(encoding='utf-8'))
    if data.get('schema')!=TOOLCHAIN_SCHEMA:
        raise ValueError(f'{path}: schema {data.get("schema")}, not {TOOLCHAIN_SCHEMA}.')
    tools=data.get('tools')
    if not isinstance(tools,dict) or set(tools)-set(TOOL_PROBES) or not tools:
        raise ValueError(f'{path}: tools must name some of {", ".join(TOOL_PROBES)}.')
    for name,tool in tools.items():
        fields={'version','url','size','sha256','license','license_file','format','top','keep','bin','accept'}
        problems=[f'fields {sorted(set(tool)^fields)}'] if set(tool)!=fields else []
        if not problems:
            keep,accept=tool['keep'],tool['accept']
            checks=[(isinstance(tool['version'],str) and bool(re.fullmatch(r'\d+(\.\d+)*',tool['version'])),'version'),
                    (isinstance(tool['url'],str) and tool['url'].startswith('https://'),'url'),
                    (type(tool['size']) is int and tool['size']>0,'size'),
                    (isinstance(tool['sha256'],str) and bool(re.fullmatch(r'[0-9a-f]{64}',tool['sha256'])),'sha256'),
                    (isinstance(tool['license'],str) and bool(tool['license']),'license'),
                    (tool['format'] in TOOL_FORMATS and tool['url'].endswith('.'+tool['format']),'format'),
                    (isinstance(tool['top'],str) and '/' not in tool['top'],'top'),
                    (keep is None or isinstance(keep,list) and bool(keep) and all(
                        isinstance(entry,str) and entry and not entry.startswith('/') and
                        '..' not in entry.split('/') and ':' not in entry for entry in keep),'keep'),
                    (tool['license_file'] is None or keep is None or archive_selected(tool['license_file'],keep),
                     'license_file'),
                    (isinstance(tool['bin'],str),'bin'),
                    (isinstance(accept,list) and len(accept)==2 and isinstance(accept[0],str) and
                     (accept[1] is None or isinstance(accept[1],str)),'accept')]
            problems=[field for ok,field in checks if not ok]
        if problems:
            raise ValueError(f'{path}: tool {name}: bad {", ".join(problems)}.')
    return tools

def tool_accepts(tool,version):
    """Whether version (a string) lies in the tool's accept range."""
    lowest,below=tool['accept']
    return version_tuple(version)>=version_tuple(lowest) and (below is None or
                                                              version_tuple(version)<version_tuple(below))

def probe_tool(name,folder,tool):
    """The tool's copy in folder as {path, version, accepted, missing}, or None when folder has
    no executable of it. Runs the executable for its version."""
    executable,arguments,pattern,beside=TOOL_PROBES[name]
    folder=Path(folder)
    if not (folder/executable).is_file():
        return None
    try:
        output=subprocess.run([folder/executable,*arguments],capture_output=True,text=True,
                              timeout=60).stdout
    except (OSError,subprocess.SubprocessError):
        output=''
    match=re.search(pattern,output,re.M)
    version=match.group(1) if match else None
    missing=[other for other in beside if not (folder/other).is_file()]
    return {'path':str(folder),'version':version,'missing':missing,
            'accepted':bool(version) and not missing and tool_accepts(tool,version)}

def tool_folders(name):
    """The folders where an installed copy of a tool may stand, in order: the PATH's, then the
    default install folders (CMake's; the CMake and Ninja that Visual Studio bundles; llvm-mingw has
    no installer). For Python, the folder of the interpreter that runs this."""
    if name=='python':
        return [Path(sys.executable).parent]
    found=shutil.which(TOOL_PROBES[name][0])
    folders=[Path(found).parent] if found else []
    programs=[Path(os.environ[variable]) for variable in ('ProgramFiles','ProgramFiles(x86)')
              if os.environ.get(variable)]
    bundled='Microsoft Visual Studio/*/*/Common7/IDE/CommonExtensions/Microsoft/CMake'
    if name=='cmake':
        folders+=[program/'CMake/bin' for program in programs]
        folders+=[path/'CMake/bin' for program in programs for path in sorted(program.glob(bundled),reverse=True)]
    elif name=='ninja':
        folders+=[path/'Ninja' for program in programs for path in sorted(program.glob(bundled),reverse=True)]
    unique={}
    for folder in folders:
        unique.setdefault(os.path.normcase(os.path.abspath(folder)),folder)
    return list(unique.values())

def detect_tools(tools):
    """The installed copies of each tool found in tool_folders, as {name: [probe_tool, ...]}; the
    first accepted one is the one a build would use."""
    return {name:[probe for probe in (probe_tool(name,folder,tool) for folder in tool_folders(name)) if probe]
            for name,tool in tools.items()}

def fetch_tool(name,tool,downloads,log,mirror=None):
    """<downloads>/<the URL's file name> with the tool's pinned size and SHA-256: kept from an
    earlier run, else copied from the folder mirror (TOOLCHAIN_MIRROR) or downloaded. A kept file
    that fails the check is renamed to .rejected, and fetched again."""
    path=Path(downloads)/tool['url'].rsplit('/',1)[1]
    def valid(candidate):
        return candidate.is_file() and candidate.stat().st_size==tool['size'] and digest(candidate)==tool['sha256']
    if path.is_file():
        if valid(path):
            with Path(log).open('a',encoding='utf-8') as stream:
                stream.write(f'{path.name} {tool["size"]} bytes, SHA-256 checked, kept from an earlier run\n')
            return path
        path.replace(path.with_name(path.name+'.rejected'))
    path.parent.mkdir(parents=True,exist_ok=True)
    partial=path.with_name(path.name+'.partial')
    for source in filter(None,(Path(mirror)/path.name if mirror else None,tool['url'])):
        started=time.monotonic()
        try:
            if isinstance(source,Path):
                if not source.is_file():
                    continue
                shutil.copyfile(source,partial)
            else:
                with urllib.request.urlopen(source,timeout=300) as response, partial.open('wb') as stream:
                    shutil.copyfileobj(response,stream,1<<20)
        except (OSError,ValueError) as error:
            print(f'{name}: {source} failed: {error}')
            continue
        if valid(partial):
            partial.replace(path)
            with Path(log).open('a',encoding='utf-8') as stream:
                stream.write(f'{path.name} {tool["size"]} bytes, SHA-256 checked, from {source} in '
                             f'{time.monotonic()-started:.1f} s\n')
            return path
        print(f'{name}: {source} does not have the pinned size and SHA-256.')
    partial.unlink(missing_ok=True)
    raise RuntimeError(f'{name} {tool["version"]} cannot be fetched with its pinned SHA-256 '
                       '(docs/setup.md#portable-tools).')

def tool_member(name,top,archive):
    """An archive member's path below the top folder ('' for none), or None for the top folder
    itself. Refused: an absolute path, a drive or ':', '..' or an empty part, a member outside the
    top folder."""
    path=name.replace('\\','/')
    parts=path.rstrip('/').split('/')
    if path.startswith('/') or ':' in path or '..' in parts or '' in parts:
        raise RuntimeError(f'{archive}: refused member path {name!r}.')
    if top:
        if parts[0]!=top:
            raise RuntimeError(f'{archive}: {name} is outside its top folder {top}.')
        parts=parts[1:]
    return '/'.join(parts) or None

def unpack_tool(archive,destination,tool):
    """The members of a tool's archive (zip, or tar.xz through Python's lzma) below its top folder
    and in its keep list (archive_selected) into destination, which is created. Refused: the paths
    tool_member refuses, and any member that is not a file or a folder (a link above all); every
    keep entry must match a member. Returns (files, bytes)."""
    destination=Path(destination)
    destination.mkdir(parents=True)
    keep,top=tool['keep'],tool['top']
    matched,files,size=set(),0,0
    def write(name,stream):
        nonlocal files,size
        matched.update(entry for entry in keep or () if archive_selected(name,[entry]))
        target=destination.joinpath(*name.split('/'))
        target.parent.mkdir(parents=True,exist_ok=True)
        with target.open('xb') as output:
            shutil.copyfileobj(stream,output,1<<20)
        files+=1
        size+=target.stat().st_size
    if tool['format']=='zip':
        with zipfile.ZipFile(archive) as package:
            for info in package.infolist():
                name=tool_member(info.filename,top,archive)
                kind=stat.S_IFMT(info.external_attr>>16)
                if kind and kind not in (stat.S_IFREG,stat.S_IFDIR):
                    raise RuntimeError(f'{archive}: {info.filename} is neither a file nor a folder.')
                if name is None or info.is_dir() or not archive_selected(name,keep):
                    continue
                with package.open(info) as stream:
                    write(name,stream)
    else:
        with tarfile.open(archive,'r:xz') as tar:
            for member in tar:
                name=tool_member(member.name,top,archive)
                if member.isdir():
                    continue
                if not member.isfile():
                    raise RuntimeError(f'{archive}: {member.name} is neither a file nor a folder.')
                member=tarfile.data_filter(member,str(destination))
                if name is None or not archive_selected(name,keep):
                    continue
                with tar.extractfile(member) as stream:
                    write(name,stream)
    unmatched=[entry for entry in keep or () if entry not in matched]
    if unmatched:
        raise RuntimeError(f'{archive} has no {", ".join(unmatched)}.')
    return files,size

def install_tool(name,tool,directory,downloads,log,mirror=None):
    """<directory>/<name>-<version>: the tool fetched (fetch_tool) and unpacked (unpack_tool) into
    a .partial folder, then moved into place with a marker of its pin; kept when the marker
    matches."""
    target=Path(directory)/f'{name}-{tool["version"]}'
    marker=target/TOOL_MARKER
    wanted={'sha256':tool['sha256'],'top':tool['top'],'keep':tool['keep']}
    if marker.is_file() and json.loads(marker.read_text(encoding='utf-8'))==wanted:
        return target
    archive=fetch_tool(name,tool,downloads,log,mirror)
    stage=target.with_name(target.name+'.partial')
    shutil.rmtree(stage,ignore_errors=True)
    started=time.monotonic()
    files,size=unpack_tool(archive,stage,tool)
    (stage/TOOL_MARKER).write_text(json.dumps(wanted),encoding='utf-8')
    place(stage,target)
    with Path(log).open('a',encoding='utf-8') as stream:
        stream.write(f'{name} {tool["version"]}: {files} files, {size} bytes in {target}, '
                     f'{time.monotonic()-started:.1f} s\n')
    return target

def toolchain(directory,downloads=None,portable=False,path=TOOLCHAIN):
    """The toolchain action: for each tool of toolchain.json, the first accepted installed copy
    (detect_tools), else, or for every tool with portable, the pinned archive fetched and unpacked
    into directory (install_tool), from downloads (default <directory>/downloads) or the folder
    TOOLCHAIN_MIRROR. Writes <directory>/toolchain-state.json with each tool's source (installed or
    portable), folder and version, which --toolchain reads. Returns {found, tools}."""
    tools=load_toolchain(path)
    directory=Path(directory).resolve()
    downloads=Path(downloads) if downloads else directory/'downloads'
    log=directory/'logs/toolchain.log'
    log.parent.mkdir(parents=True,exist_ok=True)
    found=detect_tools(tools)
    state={}
    for name,tool in tools.items():
        installed=next((probe for probe in found[name] if probe['accepted']),None)
        if installed and not portable:
            state[name]={'source':'installed','bin':installed['path'],'version':installed['version'],
                         'license':tool['license']}
            continue
        with progress('step',f'toolchain-{name}',log):
            target=install_tool(name,tool,directory,downloads,log,os.environ.get('TOOLCHAIN_MIRROR'))
            probe=probe_tool(name,target/tool['bin'],tool)
        if not probe or not probe['accepted']:
            raise RuntimeError(f'{name} in {target} does not run as version {tool["version"]}: {probe}')
        state[name]={'source':'portable','bin':probe['path'],'version':probe['version'],
                     'license':tool['license']}
    (directory/TOOLCHAIN_STATE).write_text(json.dumps({'schema':TOOLCHAIN_SCHEMA,'tools':state},indent=1)+'\n',
                                           encoding='utf-8')
    return {'found':found,'tools':state}

def use_toolchain(directory,skip=()):
    """--toolchain: the folders of <directory>/toolchain-state.json first on PATH, for this
    process and the ones it starts; skip: tools left off (llvm-mingw with --abi msvc, whose clang
    and clang++ would shadow LLVM's)."""
    path=Path(directory)/TOOLCHAIN_STATE
    if not path.is_file():
        raise RuntimeError(f'{path} does not exist: run the toolchain action first.')
    state=json.loads(path.read_text(encoding='utf-8'))
    folders=[tool['bin'] for name,tool in state['tools'].items() if name not in skip]
    os.environ['PATH']=os.pathsep.join([*folders,os.environ.get('PATH','')])
    return folders

# The preflight action (docs/setup.md#preflight): whether this computer can build and run the
# game, before any download, as JSON. Every check has a state: pass, warn (it may work, but is
# not tested), fail, or unchecked (it could not be checked here; never counted as a pass). The
# probes below are separate functions so that the tests can replace them.
PREFLIGHT_SCHEMA=1
# x86-64-v2, which the default build targets (-march=x86-64-v2), beyond x86-64 itself: CPUID
# leaf 1's ECX bits, and leaf 0x80000001's ECX bit 0 for LAHF and SAHF in 64-bit mode.
X86_64_V2={'sse3':(1,0),'ssse3':(1,9),'cmpxchg16b':(1,13),'sse4_1':(1,19),'sse4_2':(1,20),
           'popcnt':(1,23),'lahf_sahf':(0x80000001,0)}
# IsProcessorFeaturePresent's flags for the same features where Windows has one: none for POPCNT
# or LAHF/SAHF, and Windows versions older than the flag report it absent.
X86_64_V2_FLAGS={'sse3':13,'cmpxchg16b':14,'ssse3':36,'sse4_1':37,'sse4_2':38}
# A provisional floor for a first build: the BuildRoot (2.6 GB after build), Disc 2's import
# (2.8 GB), the downloadable content and the builder's toolchain (docs/analysis/builder-installer.md 5).
PREFLIGHT_DISK=20*10**9
# The deepest file measured below a BuildRoot is 187 characters (Disc 2's imported content in
# user-data): a longer BuildRoot path breaks Windows' 260-character limit. The FSR build's objects
# hold the BuildRoot twice (CMake's object paths) and reach 2 x length + 166.
PATH_LIMITS={'build_root':(46,70),'source':(100,150)}
# Nominal RAM per compile job of -Jobs (8 jobs ran on 32 GB; the memory per job is not measured).
JOB_MEMORY=4*2**30

def cpuid(leaf):
    """(EAX, EBX, ECX, EDX) of the CPUID instruction for leaf, sub-leaf 0, run from a small
    x64 routine in executable memory."""
    import ctypes
    from ctypes import wintypes
    # push rbx; mov eax,ecx; mov ecx,edx; cpuid; mov [r8],eax; mov [r8+4],ebx;
    # mov [r8+8],ecx; mov [r8+12],edx; pop rbx; ret
    code=bytes.fromhex('53 89c8 89d1 0fa2 418900 41895804 41894808 4189500c 5b c3'.replace(' ',''))
    kernel32=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel32.VirtualAlloc.restype=ctypes.c_void_p
    kernel32.VirtualAlloc.argtypes=(ctypes.c_void_p,ctypes.c_size_t,wintypes.DWORD,wintypes.DWORD)
    kernel32.VirtualProtect.argtypes=(ctypes.c_void_p,ctypes.c_size_t,wintypes.DWORD,
                                      ctypes.POINTER(wintypes.DWORD))
    kernel32.VirtualFree.argtypes=(ctypes.c_void_p,ctypes.c_size_t,wintypes.DWORD)
    memory=kernel32.VirtualAlloc(None,len(code),0x3000,0x04)  # MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE
    if not memory:
        raise OSError(ctypes.get_last_error(),'VirtualAlloc failed')
    try:
        ctypes.memmove(memory,code,len(code))
        old=wintypes.DWORD()
        if not kernel32.VirtualProtect(memory,len(code),0x20,ctypes.byref(old)):  # PAGE_EXECUTE_READ
            raise OSError(ctypes.get_last_error(),'VirtualProtect failed')
        registers=(ctypes.c_uint32*4)()
        ctypes.CFUNCTYPE(None,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p)(memory)(
            leaf,0,ctypes.addressof(registers))
        return tuple(registers)
    finally:
        kernel32.VirtualFree(memory,0,0x8000)  # MEM_RELEASE

def processor_feature(flag):
    import ctypes
    return bool(ctypes.windll.kernel32.IsProcessorFeaturePresent(flag))

def cpu_features():
    """(method, {feature: True, False or None}) for X86_64_V2: from CPUID, or where CPUID cannot
    run here, from IsProcessorFeaturePresent, which knows no POPCNT or LAHF/SAHF (None) and
    reports a feature it does not know as absent (None as well: not checked)."""
    try:
        highest=cpuid(0)[0]
        extended=cpuid(0x80000000)[0]
        leaves={leaf:cpuid(leaf) for leaf in {leaf for leaf,_ in X86_64_V2.values()}
                if leaf<=highest or 0x80000000<leaf<=extended}
        return 'cpuid',{name:(bool(leaves[leaf][2]>>bit&1) if leaf in leaves else None)
                        for name,(leaf,bit) in X86_64_V2.items()}
    except (OSError,AttributeError,ValueError):
        return 'IsProcessorFeaturePresent',{name:(True if name in X86_64_V2_FLAGS and
                                                  processor_feature(X86_64_V2_FLAGS[name]) else None)
                                            for name in X86_64_V2}

def windows_version():
    """(major, minor, build, native machine): the machine is AMD64, ARM64 or another name, from
    IsWow64Process2 where Windows has it, else from the process."""
    import ctypes
    import platform
    version=sys.getwindowsversion()
    machine=platform.machine().upper()
    try:
        kernel32=ctypes.windll.kernel32
        process,native=ctypes.c_ushort(),ctypes.c_ushort()
        if kernel32.IsWow64Process2(ctypes.c_void_p(kernel32.GetCurrentProcess()),
                                    ctypes.byref(process),ctypes.byref(native)):
            machine={0x8664:'AMD64',0xAA64:'ARM64',0x14C:'X86'}.get(native.value,hex(native.value))
    except AttributeError:
        pass
    return version.major,version.minor,version.build,machine

def d3d12_adapters():
    """Every adapter DXGI lists: its name, vendor ID, dedicated video memory, whether it is
    software (WARP), and whether Direct3D 12 can make a device at feature level 11_0 on it.
    D3D12CreateDevice is asked without an output pointer, so no device is made."""
    import ctypes
    from ctypes import wintypes
    dxgi,d3d12=ctypes.WinDLL('dxgi'),ctypes.WinDLL('d3d12')
    class GUID(ctypes.Structure):
        _fields_=[('a',wintypes.DWORD),('b',wintypes.WORD),('c',wintypes.WORD),('d',ctypes.c_ubyte*8)]
    class DESC1(ctypes.Structure):
        _fields_=[('description',wintypes.WCHAR*128),('vendor',wintypes.UINT),('device',wintypes.UINT),
                  ('subsys',wintypes.UINT),('revision',wintypes.UINT),('dedicated_video',ctypes.c_size_t),
                  ('dedicated_system',ctypes.c_size_t),('shared_system',ctypes.c_size_t),
                  ('luid_low',wintypes.DWORD),('luid_high',wintypes.LONG),('flags',wintypes.UINT)]
    def method(obj,index,*types):
        vtable=ctypes.cast(obj,ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
        return ctypes.WINFUNCTYPE(ctypes.c_long,ctypes.c_void_p,*types)(vtable[index])
    dxgi.CreateDXGIFactory1.restype=ctypes.c_long
    d3d12.D3D12CreateDevice.restype=ctypes.c_long
    d3d12.D3D12CreateDevice.argtypes=(ctypes.c_void_p,ctypes.c_int,ctypes.c_void_p,ctypes.c_void_p)
    iid_factory1=GUID(0x770aae78,0xf26f,0x4dba,(ctypes.c_ubyte*8)(0xa8,0x29,0x25,0x3c,0x83,0xd1,0xb3,0x87))
    iid_device=GUID(0x189819f1,0x1db6,0x4b57,(ctypes.c_ubyte*8)(0xbe,0x54,0x18,0x21,0x33,0x9b,0x85,0xf7))
    factory=ctypes.c_void_p()
    if dxgi.CreateDXGIFactory1(ctypes.byref(iid_factory1),ctypes.byref(factory))<0:
        raise OSError('CreateDXGIFactory1 failed')
    adapters=[]
    try:
        for index in range(64):
            adapter=ctypes.c_void_p()
            # IDXGIFactory1::EnumAdapters1, IDXGIAdapter1::GetDesc1, IUnknown::Release.
            if method(factory,12,wintypes.UINT,ctypes.POINTER(ctypes.c_void_p))(factory,index,ctypes.byref(adapter)):
                break
            try:
                desc=DESC1()
                if method(adapter,10,ctypes.POINTER(DESC1))(adapter,ctypes.byref(desc)):
                    continue
                # Feature level 11_0 (0xb000); S_FALSE (1) says a device could be made.
                result=d3d12.D3D12CreateDevice(adapter,0xb000,ctypes.byref(iid_device),None)
                adapters.append({'name':desc.description,'vendor_id':f'{desc.vendor:04X}',
                                 'dedicated_video_memory':desc.dedicated_video,
                                 'software':bool(desc.flags&2),'d3d12_11_0':result>=0})
            finally:
                method(adapter,2)(adapter)
    finally:
        method(factory,2)(factory)
    return adapters

def memory_size():
    """The installed RAM in bytes that Windows reports (GlobalMemoryStatusEx)."""
    import ctypes
    class MEMORYSTATUSEX(ctypes.Structure):
        _fields_=[('length',ctypes.c_ulong),('load',ctypes.c_ulong),('total',ctypes.c_ulonglong),
                  ('available',ctypes.c_ulonglong),('total_page',ctypes.c_ulonglong),
                  ('available_page',ctypes.c_ulonglong),('total_virtual',ctypes.c_ulonglong),
                  ('available_virtual',ctypes.c_ulonglong),('available_extended',ctypes.c_ulonglong)]
    status=MEMORYSTATUSEX(length=ctypes.sizeof(MEMORYSTATUSEX))
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
        raise OSError('GlobalMemoryStatusEx failed')
    return status.total

def suggested_jobs(memory,threads):
    """-Jobs for a computer: one job per JOB_MEMORY of RAM, at most one per thread, 1 to 64."""
    return max(1,min(threads or 1,round(memory/JOB_MEMORY),64))

def sync_roots():
    """The folders that sync tools keep in the cloud: OneDrive's (its environment variables)
    and every sync root registered with Windows' Cloud Files API (OneDrive, and other tools
    that use it)."""
    import winreg
    roots={os.environ[name] for name in ('OneDrive','OneDriveConsumer','OneDriveCommercial')
           if os.environ.get(name)}
    manager=r'SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\SyncRootManager'
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,manager) as key:
            for index in range(winreg.QueryInfoKey(key)[0]):
                try:
                    with winreg.OpenKey(key,winreg.EnumKey(key,index)+r'\UserSyncRoots') as users:
                        for value in range(winreg.QueryInfoKey(users)[1]):
                            path=winreg.EnumValue(users,value)[1]
                            if isinstance(path,str) and path:
                                roots.add(path)
                except OSError:
                    continue
    except OSError:
        pass
    return sorted(roots)

def cloud_attributes(path):
    """Whether the nearest existing folder of path carries the Cloud Files placeholder
    attributes (pinned, unpinned, recall on open or on data access)."""
    path=Path(path)
    while not path.exists() and path.parent!=path:
        path=path.parent
    attributes=getattr(path.stat(),'st_file_attributes',0)
    return bool(attributes&(0x40000|0x80000|0x100000|0x400000))

def build_root_error(root):
    """Why root cannot be a BuildRoot, or None. Never the repository or one of its parents, nor
    inside the tracked deliverables and source folders; outside a snapshot altogether (the
    builder replaces the snapshot on an update)."""
    root=Path(root).resolve()
    if root in (REPO,*REPO.parents) or any(root==REPO/d or REPO/d in root.parents
                                           for d in ('outputs','work/fm4-rex','work/runtime')):
        return 'BuildRoot must be a dedicated private directory, e.g. work/portable-fm4 or D:/FM4-build.'
    if not is_git_checkout() and REPO in root.parents:
        return f'BuildRoot must be outside the source snapshot {REPO}: pass --build-root.'
    return None

def path_check(path,role):
    """The path problems of the BuildRoot or the source folder: its length (PATH_LIMITS), non-ASCII
    characters, spaces (not tested), a synced folder, and for the BuildRoot build_root_error."""
    path=Path(path).resolve()
    text=str(path)
    warn,limit=PATH_LIMITS[role]
    problems=[]
    if len(text)>limit:
        problems.append(('fail',f'{len(text)} characters; at most {limit}'))
    elif len(text)>warn:
        problems.append(('warn',f'{len(text)} characters; above {warn} the deepest build files '
                                'may pass the 260-character limit'))
    if not text.isascii():
        problems.append(('fail','non-ASCII characters'))
    if ' ' in text:
        problems.append(('warn','spaces (not tested)'))
    folded=os.path.normcase(text)
    synced=[root for root in sync_roots()
            if folded==os.path.normcase(root) or folded.startswith(os.path.normcase(root).rstrip('\\/')+os.sep)]
    if synced:
        problems.append(('fail',f'inside the synced folder {synced[0]}'))
    elif cloud_attributes(path):
        problems.append(('fail','a cloud placeholder folder (Cloud Files attributes)'))
    if role=='build_root' and build_root_error(path):
        problems.append(('fail',build_root_error(path)))
    state='fail' if any(s=='fail' for s,_ in problems) else 'warn' if problems else 'pass'
    return {'state':state,'path':text,'length':len(text),'problems':[message for _,message in problems]}

def preflight(root):
    """The preflight report (PREFLIGHT_SCHEMA): Windows, the CPU's x86-64-v2 features, a GPU
    with Direct3D 12 at feature level 11_0, RAM and a suggested -Jobs, free disk on the
    BuildRoot's drive, and the BuildRoot's and source folder's paths. ok is false when any
    check fails."""
    root=Path(root).resolve()
    checks={}
    def guarded(name,check):
        try:
            checks[name]=check()
        except Exception as error:  # A probe that cannot run here: never a pass.
            checks[name]={'state':'unchecked','detail':f'{type(error).__name__}: {error}'}
    def windows():
        major,minor,build,machine=windows_version()
        version=f'{major}.{minor}.{build}'
        if machine!='AMD64' or sys.maxsize<=2**32:
            return {'state':'fail','version':version,'machine':machine,
                    'detail':'Windows x64 and a 64-bit Python are required'}
        if (major,build)<(10,10240):
            return {'state':'fail','version':version,'machine':machine,
                    'detail':'Windows 10 or 11 is required'}
        if build<22000:
            return {'state':'warn','version':version,'machine':machine,'name':'Windows 10',
                    'detail':'Windows 10 is not tested'}
        return {'state':'pass','version':version,'machine':machine,'name':'Windows 11'}
    def cpu():
        method,features=cpu_features()
        missing=[name for name,present in features.items() if present is False]
        unchecked=[name for name,present in features.items() if present is None]
        state='fail' if missing else 'unchecked' if unchecked else 'pass'
        return {'state':state,'level':'x86-64-v2','method':method,'features':features,
                'missing':missing,'unchecked':unchecked}
    def gpu():
        adapters=d3d12_adapters()
        hardware=[a for a in adapters if not a['software'] and a['d3d12_11_0']]
        return {'state':'pass' if hardware else 'fail','adapters':adapters,
                **({} if hardware else {'detail':'no hardware adapter with Direct3D 12 at '
                                                 'feature level 11_0'})}
    def memory():
        total,threads=memory_size(),os.cpu_count()
        jobs=suggested_jobs(total,threads)
        report={'state':'pass','total_bytes':total,'threads':threads,'suggested_jobs':jobs}
        if total<15*2**30:
            report.update(state='warn',detail='builds with less than 16 GB are not measured')
        return report
    def disk():
        anchor=root
        while not anchor.exists() and anchor.parent!=anchor:
            anchor=anchor.parent
        free=shutil.disk_usage(anchor).free
        return {'state':'pass' if free>=PREFLIGHT_DISK else 'fail','path':anchor.anchor,
                'free_bytes':free,'required_bytes':PREFLIGHT_DISK}
    def paths():
        items={'build_root':path_check(root,'build_root'),'source':path_check(REPO,'source')}
        states=[item['state'] for item in items.values()]
        return {'state':'fail' if 'fail' in states else 'warn' if 'warn' in states else 'pass',
                **items}
    for name,check in (('windows',windows),('cpu',cpu),('gpu',gpu),('memory',memory),
                       ('disk',disk),('paths',paths)):
        guarded(name,check)
    return {'schema':PREFLIGHT_SCHEMA,'ok':all(check['state']!='fail' for check in checks.values()),
            'build_root':str(root),'source':str(REPO),
            'suggested_jobs':checks['memory'].get('suggested_jobs'),'checks':checks}

def disc_release(disc1):
    """The supported release whose three executables Disc 1 holds. A set that mixes releases
    or revisions is rejected."""
    hashes = {}
    for name in EXECUTABLES:
        if not (disc1/name).is_file():
            raise ValueError(f'Missing executable: {disc1/name}.')
        hashes[name] = digest(disc1/name)
    for release, info in RELEASES.items():
        if hashes == info['executables']:
            return release
    supported = ', '.join(f'{release} (media ID {info["media_id"]})' for release, info in RELEASES.items())
    raise ValueError(f'Unsupported executables in {disc1}: the three must be those of one supported '
                     f'release: {supported}.')

def validate_discs(disc1, disc2):
    if not disc1:
        raise ValueError('Provide -Disc1 with the extracted Play Disc directory or its ISO.')
    disc1 = Path(disc1).resolve()
    disc_release(disc1)
    if not (disc1/'Media').is_dir() or not (disc1/'Database.xmplr').is_file():
        raise ValueError('Disc1 must contain the extracted game assets (Media and Database.xmplr), not just the three XEX files.')
    if disc2:
        disc2 = Path(disc2).resolve()
        for package in disc2_packages(disc2):
            if not package.is_file():
                raise ValueError(f'Missing Disc 2 content package: {package}')
    return disc1, disc2

# Disc images (docs/setup.md#disc-images): an Xbox 360 disc image (an ISO) holds a disc's files
# in GDFX, the disc's file system. Its volume descriptor is sector 32 of the game partition, which
# starts at one of the offsets that the runtime's reader tries (DiscImageDevice in
# work/runtime/src/filesystem/devices, from Xenia); each directory is a table of little-endian
# entries that form a binary tree. The launcher's ports are iso.cpp and UnpackDisc in discs.cpp.
GDFX_MAGIC = b'MICROSOFT*XBOX*MEDIA'
GDFX_OFFSETS = (0x00000000, 0x0000FB20, 0x00020600, 0x02080000, 0x0FD90000)
GDFX_SECTOR = 2048
GDFX_DIRECTORY = 0x10
GDFX_MAX_TABLE = 32 << 20
GDFX_UNSAFE = set('<>:"/\\|?*')

def disc_image_entries(image):
    """The directories and files of an Xbox 360 disc image, sorted by path: (relative path with
    '/', offset, size), with offset None for a directory. Raises ValueError for a file that is
    not one, a damaged one, or names that are not safe as Windows file names."""
    image = Path(image)
    size = image.stat().st_size
    def damaged(what):
        return ValueError(f'Damaged disc image ({what}): {image}')
    with image.open('rb') as stream:
        def read(offset, length):
            if offset + length > size:
                raise damaged('a table beyond its end')
            stream.seek(offset)
            return stream.read(length)
        for base in GDFX_OFFSETS:
            if base + 33*GDFX_SECTOR <= size and read(base + 32*GDFX_SECTOR, 20) == GDFX_MAGIC:
                break
        else:
            raise ValueError(f'Not an Xbox 360 disc image: {image}')
        root_sector, root_size = struct.unpack('<II', read(base + 32*GDFX_SECTOR + 20, 8))
        entries, names, tables = [], set(), set()
        directories = [('', root_sector, root_size)]
        while directories:
            parent, sector, length = directories.pop()
            if not length:
                continue
            if length > GDFX_MAX_TABLE or sector in tables:
                raise damaged('a directory table')
            tables.add(sector)
            table = read(base + sector*GDFX_SECTOR, length)
            nodes, visited = [0], set()
            while nodes:
                ordinal = nodes.pop()
                at = ordinal*4
                if ordinal in visited or at + 14 > len(table):
                    raise damaged('a directory entry')
                visited.add(ordinal)
                left, right, entry_sector, entry_size, attributes, name_length = \
                    struct.unpack_from('<HHIIBB', table, at)
                raw = table[at + 14:at + 14 + name_length]
                name = raw.decode('ascii', errors='replace')
                if (len(raw) != name_length or not name or name in ('.', '..') or name[-1] in ' .'
                        or any(c in GDFX_UNSAFE or not 32 <= ord(c) <= 126 for c in name)):
                    raise ValueError(f'Unsupported file name {raw!r} in the disc image {image}')
                path = f'{parent}/{name}' if parent else name
                if path.lower() in names:
                    raise damaged(f'two entries named {path}')
                names.add(path.lower())
                if attributes & GDFX_DIRECTORY:
                    entries.append((path, None, 0))
                    directories.append((path, entry_sector, entry_size))
                else:
                    offset = base + entry_sector*GDFX_SECTOR
                    if offset + entry_size > size:
                        raise damaged(f'{path} beyond its end')
                    entries.append((path, offset, entry_size))
                nodes += [child for child in (right, left) if child]
    return sorted(entries)

def unpack_disc_image(image, entries, out, report=None):
    """Writes entries of disc_image_entries(image) under out; report gets each chunk's size."""
    with Path(image).open('rb') as stream:
        for path, offset, size in entries:
            target = Path(out)/path
            if offset is None:
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            stream.seek(offset)
            with target.open('wb') as output:
                remaining = size
                while remaining:
                    chunk = stream.read(min(remaining, 8 << 20))
                    if not chunk:
                        raise ValueError(f'The disc image ends inside {path}: {image}')
                    output.write(chunk)
                    remaining -= len(chunk)
                    if report:
                        report(len(chunk))

def write_disc_image(path, files, base=GDFX_OFFSETS[1]):
    """A small GDFX disc image for the tests: files maps relative paths with '/' to their bytes,
    or to None for an empty directory. Each table is a balanced tree of its sorted names."""
    tree = {}
    for name, data in files.items():
        node = tree
        *parents, leaf = name.split('/')
        for part in parents:
            node = node.setdefault(part, {})
        node.setdefault(leaf, {} if data is None else data)
    blobs, next_sector = {}, [33]
    def allocate(data):
        sector = next_sector[0]
        next_sector[0] += max(1, -(-len(data)//GDFX_SECTOR))
        blobs[sector] = data
        return sector
    def table(node):
        order = []
        def place(names):
            if not names:
                return None
            index = len(order)
            order.append([names[len(names)//2], None, None])
            order[index][1] = place(names[:len(names)//2])
            order[index][2] = place(names[len(names)//2 + 1:])
            return index
        place(sorted(node))
        offsets, at = [], 0
        for name, _, _ in order:
            offsets.append(at)
            at += -(-(14 + len(name))//4)*4
        data = bytearray()
        for name, left, right in order:
            child = node[name]
            if isinstance(child, dict):
                child_table = table(child)
                sector, size, attributes = (allocate(child_table) if child_table else 0), len(child_table), GDFX_DIRECTORY
            else:
                sector, size, attributes = allocate(child), len(child), 0
            entry = struct.pack('<HHIIBB', 0 if left is None else offsets[left]//4,
                                0 if right is None else offsets[right]//4, sector, size, attributes,
                                len(name)) + name.encode('ascii')
            data += entry + b'\xff'*(-len(entry) % 4)
        return bytes(data)
    root = table(tree)
    root_sector = allocate(root)
    image = bytearray(base + next_sector[0]*GDFX_SECTOR)
    image[base + 32*GDFX_SECTOR:base + 32*GDFX_SECTOR + 28] = GDFX_MAGIC + struct.pack('<II', root_sector, len(root))
    for sector, data in blobs.items():
        image[base + sector*GDFX_SECTOR:base + sector*GDFX_SECTOR + len(data)] = data
    Path(path).write_bytes(image)

def disc_input(root, disc, name):
    """The folder of a Disc given to generate (name disc1 or disc2). A folder is returned as it is;
    a disc image is unpacked once into <BuildRoot>/discs/<name> and that folder is returned. The
    files that identify the Disc are unpacked and checked first, so a wrong image fails in
    seconds. <BuildRoot>/discs/<name>.json records the image, so the same image is not unpacked
    again; the image itself is not needed after that."""
    if not disc or not Path(disc).is_file():
        return disc
    label = 'Disc 1' if name == 'disc1' else 'Disc 2'
    image = Path(disc).resolve()
    discs = root/'discs'
    folder, marker, partial = discs/name, discs/f'{name}.json', discs/f'{name}.partial'
    info = image.stat()
    source = {'image': str(image), 'size': info.st_size, 'mtime_ns': info.st_mtime_ns}
    if folder.is_dir() and marker.is_file() and \
            json.loads(marker.read_text(encoding='utf-8')).get('source') == source:
        print(f'{label}: {folder}, unpacked before from {image}.')
        return folder
    entries = disc_image_entries(image)
    files = {path.lower(): (path, offset, size) for path, offset, size in entries if offset is not None}
    if name == 'disc1':
        wanted = list(EXECUTABLES)
    else:
        wanted = [package.as_posix() for package in disc2_packages(Path())]
    missing = [path for path in wanted if path.lower() not in files]
    if missing:
        raise ValueError(f'{image} is not {label} ({"the Play Disc" if name == "disc1" else "the Content Disc"}): '
                         f'it has no {missing[0]}.')
    first = [files[path.lower()] for path in wanted]
    total = sum(size for _, offset, size in entries if offset is not None)
    discs.mkdir(parents=True, exist_ok=True)
    marker.unlink(missing_ok=True)
    if partial.exists():
        shutil.rmtree(partial)
    free = shutil.disk_usage(discs).free
    if free < total + (1 << 30):
        raise ValueError(f'Unpacking {label} needs {total/2**30:.1f} GB in {discs}; '
                         f'{free/2**30:.1f} GB are free.')
    print(f'Unpacking {label} from {image} into {folder} ({total/2**30:.1f} GB).', flush=True)
    unpack_disc_image(image, first, partial)
    try:
        if name == 'disc1':
            print(f'{label}: the {disc_release(partial)} release.')
        else:
            for package in disc2_packages(partial):
                error = content_error(package)
                if error:
                    raise ValueError(error)
    except ValueError as error:
        shutil.rmtree(partial)
        raise ValueError(f'{image}: {error}') from None
    done, shown = [sum(size for _, _, size in first)], [0]
    def report(count):
        done[0] += count
        percent = done[0]*100//max(total, 1)
        if percent >= shown[0] + 10:
            shown[0] = percent - percent % 10
            print(f'Unpacking {label}: {shown[0]}%', flush=True)
    unpack_disc_image(image, [entry for entry in entries if entry not in first], partial, report)
    if folder.exists():
        shutil.rmtree(folder)
    partial.rename(folder)
    marker.write_text(json.dumps({'source': source, 'files': len(files), 'bytes': total}, indent=1),
                      encoding='utf-8')
    print(f'{label}: {len(files)} files unpacked into {folder}.')
    return folder

# FM4 content packages (Disc 2 and downloadable content) are STFS containers. The header
# fields are big-endian, at the offsets of the Xbox 360 XContent header.
TITLE_ID = 0x4D530910
STFS_MAGICS = (b'CON ', b'LIVE', b'PIRS')
MARKETPLACE_CONTENT = 0x00000002
CONTENT_TYPE_OFFSET, TITLE_ID_OFFSET = 0x344, 0x360

def disc2_packages(disc2):
    packages = Path(disc2)/'content/0000000000000000/4d530910/00000002'
    return [packages/f'4d5309100000000{index}' for index in range(1,5)]

def package_header(path):
    """(magic, content type, title ID) of an STFS package, or None for any other file."""
    with Path(path).open('rb') as stream:
        header = stream.read(TITLE_ID_OFFSET+4)
    if len(header) < TITLE_ID_OFFSET+4 or header[:4] not in STFS_MAGICS:
        return None
    return (header[:4].decode().strip(), int.from_bytes(header[CONTENT_TYPE_OFFSET:CONTENT_TYPE_OFFSET+4],'big'),
            int.from_bytes(header[TITLE_ID_OFFSET:TITLE_ID_OFFSET+4],'big'))

def content_error(package):
    """Why a file is not an FM4 marketplace content package, or None if it is one."""
    header = package_header(package)
    if not header:
        return f'Not an STFS content package: {package}'
    _, content_type, title_id = header
    if title_id != TITLE_ID:
        return f'Content package is for title {title_id:08X}, not FM4 ({TITLE_ID:08X}): {package}'
    if content_type != MARKETPLACE_CONTENT:
        return (f'Content package type {content_type:08X} is not FM4 marketplace content '
                f'({MARKETPLACE_CONTENT:08X}): {package}')
    return None

def validate_content(disc2, content):
    """The packages to import: Disc 2's four, then those found in each -Content path.

    A -Content path is one package file or a directory searched recursively. A named file must
    be an FM4 marketplace content package. Inside a directory, other files and the packages of
    other titles or content types (a system update, a title update) are skipped. File names
    must be unique: the import installs each package under its name."""
    packages = disc2_packages(disc2) if disc2 else []
    for package in packages:
        error = content_error(package)
        if error:
            raise ValueError(error)
    for source in content or ():
        source = Path(source).resolve()
        if source.is_file():
            error = content_error(source)
            if error:
                raise ValueError(error)
            packages.append(source)
        elif source.is_dir():
            found = 0
            for path in sorted(source.rglob('*')):
                if not path.is_file() or not package_header(path):
                    continue
                error = content_error(path)
                if error:
                    print(f'Skipped: {error}')
                else:
                    packages.append(path)
                    found += 1
            if not found:
                raise ValueError(f'No FM4 content packages found in {source}')
        else:
            raise ValueError(f'Content path not found: {source}')
    names = {}
    for package in packages:
        previous = names.setdefault(package.name.lower(), package)
        if previous != package:
            raise ValueError(f'Two content packages share the name {package.name}: {previous} and {package}')
    return list(names.values())

def write_content_manifest(root, packages):
    """The package list the game imports at start (fm4-content-installer.cpp), one UTF-8 path a line."""
    manifest = root/'content-packages.txt'
    manifest.write_text(''.join(f'{package}\n' for package in packages),encoding='utf-8')
    return manifest

def check_layout(root):
    if (root/'sdk-source').exists() and not (root/'runtime-source').exists():
        raise RuntimeError('This BuildRoot uses the old sdk-source/sdk.patch layout. Use a new BuildRoot.')

def sync_runtime(root):
    """Mirror work/runtime into <BuildRoot>/runtime-source. Unchanged files keep their
    timestamps, so incremental runtime builds only recompile what was edited."""
    source = root/'runtime-source'
    listing = source/'.fm4-runtime-files.json'
    previous = set(json.loads(listing.read_text())) if listing.is_file() else set()
    current = set()
    for path in RUNTIME.rglob('*'):
        if path.is_file():
            relative = path.relative_to(RUNTIME).as_posix()
            current.add(relative)
            destination = source/relative
            destination.parent.mkdir(parents=True,exist_ok=True)
            copy_source(path,destination)
    for relative in previous-current:
        (source/relative).unlink(missing_ok=True)
    listing.write_text(json.dumps(sorted(current)))

# The fetch without Git (--fetch archive, the default in a source snapshot; docs/setup.md#fetching-
# without-git, docs/analysis/builder-installer.md 4.3): GitHub's archive of each pinned commit,
# kept in <BuildRoot>/downloads so that a rerun needs no network, and unpacked by unpack_archive.
# GitHub does not promise the same archive bytes over time, so each pin is a tree hash over the
# unpacked files (archive_tree), not a hash of the archive. --fetch git clones with Git instead
# (the default in a Git checkout, with THIRDPARTY_MIRROR and FFX_MIRROR's bundle). A pin that
# moves to another commit gets its tree from the stop of a first fetch, which names it, after
# its files are checked against those of a Git checkout of that commit.
FETCHES=('git','archive')
FETCH=None
ARCHIVE_URL='https://codeload.github.com/{repository}/tar.gz/{commit}'
SDK_REPOSITORY='rexglue/rexglue-sdk'
# The tree of the SDK archive's thirdparty/<THIRDPARTY_VENDORED> folders.
SDK_TREE='6af5992d2ef095868df356f153200f47ce9e152abbb64f7e934e75858e2c6aea'
# The submodules of THIRDPARTY_SUBMODULES at the commits SDK_REV pins, and SIMDe's own submodule:
# (folder under runtime-source/thirdparty, GitHub repository, commit, tree of the unpacked files).
THIRDPARTY_ARCHIVES=(
    ('FFmpeg','wmarti/FFmpeg','0604b464c7cb4ebc94940cf1f324a3b26b87717c',
     '184ac24d36d8bb3bd698e1d8307d1e66a47864e422a13e42197f58626d1daafc'),
    ('cli11','CLIUtils/CLI11','bfffd37e1f804ca4fae1caae106935791696b6a9',
     'eda0373a41e5bf63b44a85237a80c8724e3d76d53f14516bca5d5cb5ef873624'),
    ('fmt','fmtlib/fmt','407c905e45ad75fc29bf0f9bb7c5c2fd3475976f',
     'ba0cfa2a9c50599b7aecf832a3c05ea596cc1369386ffa1b34dd3d571031644c'),
    ('imgui','ocornut/imgui','6d910d5487d11ca567b61c7824b0c78c569d62f0',
     'c95a0108745952ae8de7e9a00f83254d6d65fa0e881dc627185ae323037a0c81'),
    ('inja','pantor/inja','7d1b4600b68595085a949743331c2e5673f511ea',
     '3d8f06bb2480e23bb206cfcea50123f1de82534d2ce8cc20bbc78f321dd82ff5'),
    ('libmspack','kyz/libmspack','305907723a4e7ab2018e58040059ffb5e77db837',
     '819297b5467e2d0291a0c0a4c6b9a573d7fa8928e1b44bd31de07bf0bf1299a9'),
    ('o1heap','pavel-kirienko/o1heap','388a73fd9007300e5130c5fe352d9ce3288b6dde',
     '53818b72775c6277baedb8f87e8d5105eb751cfee31823a213d43556f886616b'),
    ('sdl3','libsdl-org/SDL','8bf3b7215ad9fc3deb583c6a3a37c6c67f2e24e4',
     '8ed4149c4144b6386cc71a35e45daf959249c7f504104e0127ef78a9c5b6d935'),
    ('simde','simd-everywhere/simde','71fd833d9666141edcd1d3c109a80e228303d8d7',
     'b710307fde2f826a94585095390b0fb7ed7cc34c9d2660c8d0ce3a5bd1928727'),
    ('simde/test/munit','nemequ/munit','da8f73412998e4f1adf1100dc187533a51af77fd',
     'eb71663930e8e87b7b23bf6394b9e741a2fe0193b659ad096f50b6017f378405'),
    ('spdlog','gabime/spdlog','79524ddd08a4ec981b7fea76afd08ee05f83755d',
     '9ed7f9f61a45b43a86924832a50227222b3b744cd337c266885ea82d39cf3ffa'),
    ('tomlplusplus','marzer/tomlplusplus','30172438cee64926dc41fdd9c11fb3ba5b2ba9de',
     'e79ec344df246dbba398945b19311a02a0d852f053070ed86605f3698e3fb4da'),
    ('utfcpp','nemtrif/utfcpp','63d64de49fd6b829f7c8694df5ab2ee625cb7134',
     '02329d5bb9ae4b60dbe90b8f39062a8f3337e8fdb692e2325508854d6f9b3e58'),
    ('xxHash','Cyan4973/xxHash','e626a72bc2321cd320e953a0ccf1584cad60f363',
     'e5a779b901f27d027dbaac8297eba6e47c4cf72e6f31f662154c3adb203e4bd2'))

def fetch_mode():
    """--fetch, else archive in a source snapshot and git in a Git checkout."""
    return FETCH or ('git' if is_git_checkout() else 'archive')

def archive_tree(directory):
    """The pin of unpacked files: a SHA-256 over, in posix-path order, path + b'\\0' + the SHA-256
    of the contents with each CRLF read as LF, for the files below directory except Git's (a .git
    file or folder) and the pipeline's markers (.fm4-*). Line endings are read as LF because a Git
    for Windows checkout writes CRLF where the archive has LF; the files keep their own."""
    directory=Path(directory)
    files=sorted((path.relative_to(directory).as_posix(),path) for path in directory.rglob('*')
                 if path.is_file() and not any(part=='.git' or part.startswith('.fm4-')
                                               for part in path.relative_to(directory).parts))
    tree=hashlib.sha256()
    for name,path in files:
        tree.update(name.encode()+b'\0'+hashlib.sha256(path.read_bytes().replace(b'\r\n',b'\n')).digest())
    return tree.hexdigest()

def archive_selected(name,select):
    """Whether an archive member's path below its top folder is in select: a file, or a folder
    ending with '/'. None selects everything."""
    return select is None or any(name==entry or entry.endswith('/') and name.startswith(entry)
                                 for entry in select)

def unpack_archive(archive,destination,commit,select=None):
    """The files of a GitHub archive (a git archive tar.gz with one top folder) below that folder
    into destination, those of select only (archive_selected). Every member passes tarfile's data
    filter and a check of its own path (nothing absolute, no '..', no ':'); a symbolic link is
    written as a file holding its target, as a Git for Windows checkout without symbolic links
    writes it, and any other kind of member is refused. The archive's commit (its pax comment)
    must be commit. Returns the number of files."""
    destination=Path(destination)
    destination.mkdir(parents=True)
    top,count=None,0
    with tarfile.open(archive) as tar:
        for member in tar:
            first,_,name=member.name.partition('/')
            top=top or first
            if first!=top:
                raise RuntimeError(f'{archive}: {member.name} is outside its top folder {top}.')
            name=name.rstrip('/')
            if not name or member.isdir() or not archive_selected(name,select):
                continue
            parts=name.split('/')
            if '..' in parts or '' in parts or ':' in name or '\\' in name:
                raise RuntimeError(f'{archive}: refused member path {member.name!r}.')
            member=tarfile.data_filter(member,str(destination))
            if member.isfile():
                data=tar.extractfile(member).read()
            elif member.issym():
                data=member.linkname.encode()
            else:
                raise RuntimeError(f'{archive}: {member.name} is neither a file nor a symbolic link.')
            target=destination.joinpath(*parts)
            target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(data)
            count+=1
        comment=tar.pax_headers.get('comment')
    if comment not in (None,commit):
        raise RuntimeError(f'{archive} is an archive of {comment}, not of {commit}.')
    return count

def download_archive(repository,commit,downloads,log,mirror=None):
    """<downloads>/<owner>-<repo>-<commit>.tar.gz, GitHub's archive of the commit, kept for later
    runs: fetched only when missing, from mirror first when it is a local file (another URL of
    the archive is tried after GitHub). Its files are checked when they are unpacked."""
    path=Path(downloads)/f'{repository.replace("/","-")}-{commit}.tar.gz'
    if path.is_file():
        return path
    path.parent.mkdir(parents=True,exist_ok=True)
    partial=path.with_name(path.name+'.partial')
    url=ARCHIVE_URL.format(repository=repository,commit=commit)
    local=mirror is not None and Path(mirror).is_file()
    for source in filter(None,(mirror,url) if local else (url,mirror)):
        started=time.monotonic()
        try:
            if Path(source).is_file():
                shutil.copyfile(source,partial)
            else:
                with urllib.request.urlopen(source,timeout=300) as response, partial.open('wb') as stream:
                    shutil.copyfileobj(response,stream,1<<20)
        except (OSError,ValueError) as error:
            print(f'{repository} {commit[:7]}: {source} failed: {error}')
            continue
        partial.replace(path)
        with Path(log).open('a',encoding='utf-8') as stream:
            stream.write(f'{path.name} {path.stat().st_size} bytes from {source} in '
                         f'{time.monotonic()-started:.1f} s\n')
        return path
    partial.unlink(missing_ok=True)
    raise RuntimeError(f'The archive of {repository} {commit} cannot be fetched '
                       '(docs/setup.md#fetching-without-git).')

def unpack_checked(archive,stage,commit,tree,select,log):
    """unpack_archive into stage (emptied first), then the pin: files whose archive_tree differs
    from tree stop the build, and the archive is renamed to .rejected so that a rerun fetches it
    again."""
    started=time.monotonic()
    shutil.rmtree(stage,ignore_errors=True)
    files=unpack_archive(archive,stage,commit,select)
    actual=archive_tree(stage)
    with Path(log).open('a',encoding='utf-8') as stream:
        stream.write(f'{archive.name} {files} files, tree {actual}, {time.monotonic()-started:.1f} s\n')
    if actual!=tree:
        archive.replace(archive.with_name(archive.name+'.rejected'))
        raise RuntimeError(f'{archive.name} does not unpack to the pinned files: tree {actual}, '
                           f'pinned {tree}.')

def place(source,target):
    """Moves the folder source to target, replacing what is there."""
    shutil.rmtree(target,ignore_errors=True)
    target.parent.mkdir(parents=True,exist_ok=True)
    os.replace(source,target)

def fetch_thirdparty_archives(root,destination):
    """--fetch archive: the SDK's archive for THIRDPARTY_VENDORED and THIRDPARTY_ARCHIVES' archives
    for the submodules, each checked against its pin, into destination."""
    archives=[('',SDK_REPOSITORY,SDK_REV,SDK_TREE,[f'thirdparty/{name}/' for name in THIRDPARTY_VENDORED]),
              *[(folder,repository,commit,tree,None) for folder,repository,commit,tree in THIRDPARTY_ARCHIVES]]
    log=root/'logs/thirdparty-download.log'
    log.parent.mkdir(parents=True,exist_ok=True)
    with progress('step','thirdparty-download',log):
        paths=[download_archive(repository,commit,root/'downloads',log)
               for _,repository,commit,_,_ in archives]
    log=root/'logs/thirdparty-unpack.log'
    stage=destination/'.fm4-unpack'
    with progress('step','thirdparty-unpack',log):
        # A folder before the ones inside it (simde before simde/test/munit).
        for (folder,_,commit,tree,select),archive in zip(archives,paths):
            unpack_checked(archive,stage,commit,tree,select,log)
            if folder:
                place(stage,destination/folder)
            else:
                for name in THIRDPARTY_VENDORED:
                    place(stage/'thirdparty'/name,destination/name)
        shutil.rmtree(stage,ignore_errors=True)

def thirdparty_mirror():
    """THIRDPARTY_MIRROR (environment, developers only): a local checkout of the upstream SDK at
    SDK_REV with the submodules of THIRDPARTY_SUBMODULES checked out unchanged, for example another
    BuildRoot's runtime-upstream. fetch_thirdparty copies from it instead of cloning, with no
    network. Refused when it is at another commit or a submodule differs from its pin."""
    mirror=os.environ.get('THIRDPARTY_MIRROR')
    if not mirror:
        return None
    mirror=Path(mirror)
    def git(*args):
        result=subprocess.run(['git','-C',mirror,*args],capture_output=True,text=True)
        if result.returncode:
            raise RuntimeError(f'THIRDPARTY_MIRROR {mirror}: git {" ".join(args)} failed: {result.stderr.strip()}')
        return result.stdout
    if git('rev-parse','HEAD').strip()!=SDK_REV:
        raise RuntimeError(f'THIRDPARTY_MIRROR {mirror} is not at the pinned SDK commit {SDK_REV}.')
    paths=[f'thirdparty/{name}' for name in THIRDPARTY_SUBMODULES]
    status=git('submodule','status','--recursive','--',*paths).splitlines()
    changed=[line for line in status if line[:1]!=' ']
    if changed or len(status)<len(paths) or git('status','--porcelain','--','thirdparty').strip():
        raise RuntimeError(f'THIRDPARTY_MIRROR {mirror}: a submodule is missing or differs from its '
                           f'pin: {changed or "git status shows changes"}')
    return mirror

def fetch_thirdparty(root):
    """Third-party libraries from the upstream SDK revision, outside the repository."""
    upstream, destination = root/'runtime-upstream', root/'runtime-source/thirdparty'
    marker = destination/'.fm4-thirdparty.json'
    wanted = {'revision':SDK_REV,'vendored':THIRDPARTY_VENDORED,'submodules':THIRDPARTY_SUBMODULES}
    if marker.is_file() and json.loads(marker.read_text()) == json.loads(json.dumps(wanted)):
        return
    if fetch_mode()=='archive':
        if os.environ.get('THIRDPARTY_MIRROR'):
            print('THIRDPARTY_MIRROR is read by --fetch git only.')
        fetch_thirdparty_archives(root,destination)
        marker.write_text(json.dumps(wanted))
        return
    mirror = thirdparty_mirror()
    if mirror:
        print(f'Third-party libraries from THIRDPARTY_MIRROR {mirror}.')
        with progress('step','thirdparty-mirror'):
            for name in THIRDPARTY_VENDORED+THIRDPARTY_SUBMODULES:
                shutil.rmtree(destination/name,ignore_errors=True)
                shutil.copytree(mirror/'thirdparty'/name,destination/name,symlinks=True)
        marker.write_text(json.dumps(wanted))
        return
    if not upstream.exists():
        run(['git','clone','--no-checkout',SDK_URL,upstream],log=root/'logs/upstream-clone.log')
    run(['git','-C',upstream,'checkout','--detach',SDK_REV],log=root/'logs/upstream-checkout.log')
    run(['git','-C',upstream,'submodule','update','--init','--recursive','--',
         *[f'thirdparty/{name}' for name in THIRDPARTY_SUBMODULES]],log=root/'logs/upstream-submodules.log')
    for name in THIRDPARTY_VENDORED+THIRDPARTY_SUBMODULES:
        shutil.rmtree(destination/name,ignore_errors=True)
        shutil.copytree(upstream/'thirdparty'/name,destination/name,symlinks=True)
    marker.write_text(json.dumps(wanted))

HUNK=re.compile(rb'^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@')

def parse_patch(data):
    """The files of a git diff that changes existing text files, as [(path, hunks)]; a hunk is
    (old start, old count, new start, new count, [(tag, text, newline)]) with tag ' ', '-' or
    '+', the text without its line ending, and newline False where the diff says 'No newline at
    end of file'. Lines before
    the first 'diff --git' are a comment. Anything else (new, deleted, renamed or binary files)
    is refused."""
    lines=data.replace(b'\r\n',b'\n').split(b'\n')
    if lines and lines[-1]==b'':
        lines.pop()
    files,index=[],0
    while index<len(lines) and not lines[index].startswith(b'diff --git '):
        index+=1
    while index<len(lines):
        line=lines[index]
        if line.startswith(b'diff --git '):
            files.append([None,[]])
        elif line.startswith(b'--- '):
            if not line.startswith(b'--- a/') or line==b'--- a/':
                raise ValueError(f'Unsupported patch line: {line.decode(errors="replace")}')
        elif line.startswith(b'+++ '):
            if not line.startswith(b'+++ b/'):
                raise ValueError(f'Unsupported patch line: {line.decode(errors="replace")}')
            files[-1][0]=line[6:].decode()
        elif line.startswith(b'index '):
            pass
        elif line.startswith(b'@@'):
            match=HUNK.match(line)
            if not match or not files or files[-1][0] is None:
                raise ValueError(f'Bad hunk header: {line.decode(errors="replace")}')
            old_start,old_count,new_start,new_count=(int(value) if value is not None else 1
                                                     for value in match.groups())
            body,old,new=[],0,0
            while old<old_count or new<new_count:
                index+=1
                if index>=len(lines):
                    raise ValueError('The patch ends inside a hunk.')
                text=lines[index]
                if text.startswith(b'\\') and body:
                    body[-1][2]=False
                    continue
                tag=chr(text[0]) if text else ' '
                if tag not in ' -+':
                    raise ValueError(f'Bad hunk line: {text.decode(errors="replace")}')
                body.append([tag,text[1:],True])
                old+=tag!='+'
                new+=tag!='-'
            while index+1<len(lines) and lines[index+1].startswith(b'\\'):
                index+=1
                body[-1][2]=False
            files[-1][1].append((old_start,old_count,new_start,new_count,[tuple(entry) for entry in body]))
        else:
            raise ValueError(f'Unsupported patch line: {line.decode(errors="replace")}')
        index+=1
    for name,_ in files:
        if name is None or name.startswith('/') or '..' in name.split('/') or ':' in name:
            raise ValueError(f'Unsupported patch path: {name}')
    return files

def line_ending(data):
    """A text's line ending: CR CR LF when it has any (the old git apply's copy into a Git for
    Windows BuildRoot wrote it on every line of a patched file), else CRLF when it has any, else
    LF."""
    return next((ending for ending in (b'\r\r\n',b'\r\n') if ending in data),b'\n')

def patch_text(data,hunks,reverse=False):
    """data with hunks applied exactly: each hunk's old lines must stand at its own line (moved
    only by the earlier hunks' added and removed lines), with no fuzz and no other offset; reverse
    swaps each hunk's sides. Returns the new data with data's own line endings (line_ending), or
    raises ValueError."""
    ending=line_ending(data)
    lines=[(line,True) for line in data.replace(ending,b'\n').split(b'\n')]
    if lines[-1][0]==b'':
        lines.pop()
    else:
        lines[-1]=(lines[-1][0],False)
    delta=0
    for number,(old_start,old_count,new_start,new_count,body) in enumerate(hunks,1):
        if reverse:
            old_start,old_count=new_start,new_count
            body=[({'-':'+','+':'-'}.get(tag,tag),line,newline) for tag,line,newline in body]
        old=[(line,newline) for tag,line,newline in body if tag!='+']
        new=[(line,newline) for tag,line,newline in body if tag!='-']
        # An empty old side starts after its line, any other at it.
        position=old_start-(1 if old_count else 0)+delta
        if position<0 or lines[position:position+len(old)]!=old:
            raise ValueError(f'hunk {number} does not match at line {position+1}')
        lines[position:position+len(old)]=new
        delta+=len(new)-len(old)
    output=b'\n'.join(line for line,_ in lines)+(b'\n' if lines and lines[-1][1] else b'')
    return output.replace(b'\n',ending)

def apply_patch(patch, directory, log, reverse=False, check=False):
    """A git diff (parse_patch) applied in Python to a directory whose files may have CRLF line
    endings (Git for Windows checks submodules out with CRLF; an archive has LF): every hunk must
    match exactly (patch_text), as git apply without fuzz requires, and each file keeps its own
    line endings. check: test only. Nothing is written unless every file applies. Raises
    RuntimeError when the patch does not apply exactly."""
    log.parent.mkdir(parents=True,exist_ok=True)
    with progress('step',log.stem,log), log.open('w',encoding='utf-8') as output:
        output.write(f'{"Checking" if check else "Applying"}{" in reverse" if reverse else ""} '
                     f'{patch} in {directory}\n')
        patched={}
        try:
            for name,hunks in parse_patch(patch.read_bytes()):
                path=Path(directory)/name
                try:
                    patched[path]=patch_text(path.read_bytes(),hunks,reverse)
                except (OSError,ValueError) as error:
                    raise ValueError(f'{name}: {error}') from error
                output.write(f'{name}: {len(hunks)} hunks apply\n')
        except ValueError as error:
            output.write(f'error: {error}\n')
            raise RuntimeError(f'{patch.name} does not apply to {directory}: {error}; log: {log}') from error
        if not check:
            for path,data in patched.items():
                path.write_bytes(data)

def patch_thirdparty(root):
    """THIRDPARTY_PATCHES on runtime-source/thirdparty, once: a marker beside each patched
    directory keeps the applied patch, so a changed patch is reversed before the new one is
    applied. Refuses a patch that does not apply to the fetched source."""
    destination=root/'runtime-source/thirdparty'
    for name,directory in THIRDPARTY_PATCHES:
        patch,target=HERE/name,destination/directory
        applied=target/f'.fm4-{name}'
        if applied.is_file() and applied.read_bytes()==patch.read_bytes():
            continue
        log=root/f'logs/patch-{Path(name).stem}.log'
        if applied.is_file():
            apply_patch(applied,target,log,reverse=True)
        try:
            apply_patch(patch,target,log,check=True)
        except RuntimeError as error:
            try:
                # Applied before, without its marker.
                apply_patch(patch,target,log,reverse=True,check=True)
            except RuntimeError:
                raise RuntimeError(f'{name} does not apply to {target}: the fetched source differs '
                                   f'from the one the patch was made for ({log}).') from error
        else:
            apply_patch(patch,target,log)
        shutil.copyfile(patch,applied)

def fetch_gameinput(root):
    """<BuildRoot>/gameinput-sdk/<GAMEINPUT_VERSION>: the pinned Microsoft.GameInput package from
    NuGet, or else from GAMEINPUT_MIRROR, checked against GAMEINPUT_SHA512, and its MIT files
    (GAMEINPUT_FILES) extracted beside it. Nothing else of the package is used."""
    directory=root/'gameinput-sdk'/GAMEINPUT_VERSION
    package=directory/f'microsoft.gameinput.{GAMEINPUT_VERSION}.nupkg'
    def valid(path):
        return path.is_file() and hashlib.sha512(path.read_bytes()).hexdigest()==GAMEINPUT_SHA512
    if not valid(package):
        directory.mkdir(parents=True,exist_ok=True)
        mirror=os.environ.get('GAMEINPUT_MIRROR')
        partial=package.with_suffix('.partial')
        # A local copy first: no network, and the hash is checked all the same.
        local=mirror is not None and Path(mirror).is_file()
        for source in filter(None,(mirror,GAMEINPUT_URL) if local else (GAMEINPUT_URL,mirror)):
            try:
                if Path(source).is_file():
                    shutil.copyfile(source,partial)
                else:
                    with urllib.request.urlopen(source,timeout=120) as response:
                        partial.write_bytes(response.read())
            except (OSError,ValueError) as error:
                print(f'GameInput package: {source} failed: {error}')
                continue
            if valid(partial):
                partial.replace(package)
                break
            print(f'GameInput package: {source} does not have the pinned SHA-512.')
        partial.unlink(missing_ok=True)
        if not valid(package):
            raise RuntimeError(f'Microsoft.GameInput {GAMEINPUT_VERSION} cannot be fetched with its '
                               'pinned SHA-512 (docs/setup.md#the-gameinput-package).')
    with zipfile.ZipFile(package) as archive:
        for member,relative in GAMEINPUT_FILES.items():
            data=archive.read(member)
            target=directory/relative
            if not target.is_file() or target.read_bytes()!=data:
                target.parent.mkdir(parents=True,exist_ok=True)
                target.write_bytes(data)
    return directory

# The DirectX Shader Compiler release for the native path's DXIL shaders (native_shader_dxil,
# docs/analysis/d3d-hle-n6.md 19.3.9, the owner's approval in 19.10): the release zip in
# <BuildRoot>/dxc-download/<DXC_VERSION>, refused with any other SHA-256, fetched from DXC_URL
# only when it is missing (DXC_MIRROR names a local file or another URL). Its x64
# dxcompiler.dll and dxil.dll go beside the game with the release's three license files; an
# export carries none of them (docs/sources-and-licensing.md#third-party-code-directx-shader-compiler).
DXC_VERSION = 'v1.9.2609'
DXC_URL = ('https://github.com/microsoft/DirectXShaderCompiler/releases/download/'
           f'{DXC_VERSION}/dxc_2026_09_29.zip')
DXC_SHA256 = 'ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1'
DXC_FILES = {'bin/x64/dxcompiler.dll':'0689e2fee0a1c9df8662a59680469f24e2a3ad7ce6bbbcc9284f7a83172bfc59',
             'bin/x64/dxil.dll':'64be2368211d257a038a7d4ea6bb3e24c878eb8d588c7b8414fccf759b69c2ac',
             'LICENSE-MS.txt':None,'LICENSE-LLVM.txt':None,'LICENCE-MIT.txt':None}

def fetch_dxc(root):
    """<BuildRoot>/dxc-download/<DXC_VERSION>/x64: the files of DXC_FILES from the pinned release
    zip, which is fetched only when it is missing. Returns the directory."""
    directory=root/'dxc-download'/DXC_VERSION
    package=directory/DXC_URL.rsplit('/',1)[1]
    def valid(path):
        return path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest()==DXC_SHA256
    if not valid(package):
        directory.mkdir(parents=True,exist_ok=True)
        partial=package.with_suffix('.partial')
        for source in filter(None,(os.environ.get('DXC_MIRROR'),DXC_URL)):
            try:
                if Path(source).is_file():
                    shutil.copyfile(source,partial)
                else:
                    with urllib.request.urlopen(source,timeout=300) as response:
                        partial.write_bytes(response.read())
            except (OSError,ValueError) as error:
                print(f'DirectX Shader Compiler: {source} failed: {error}')
                continue
            if valid(partial):
                partial.replace(package)
                break
            print(f'DirectX Shader Compiler: {source} does not have the pinned SHA-256.')
        partial.unlink(missing_ok=True)
        if not valid(package):
            raise RuntimeError(f'The DirectX Shader Compiler {DXC_VERSION} cannot be fetched with its '
                               'pinned SHA-256 (docs/setup.md).')
    target_directory=directory/'x64'
    target_directory.mkdir(exist_ok=True)
    with zipfile.ZipFile(package) as archive:
        # The release names its members with backslashes.
        members={info.filename.replace('\\','/'):info for info in archive.infolist()}
        for member,sha256 in DXC_FILES.items():
            data=archive.read(members[member])
            if sha256 and hashlib.sha256(data).hexdigest()!=sha256:
                raise RuntimeError(f'DirectX Shader Compiler: {member} does not have its pinned SHA-256.')
            target=target_directory/Path(member).name
            if not target.is_file() or target.read_bytes()!=data:
                target.write_bytes(data)
    return target_directory

def stage_dxc(root,build):
    """The DXC files beside the game build, for native_shader_dxil."""
    source=fetch_dxc(root)
    for member in DXC_FILES:
        name=Path(member).name
        if not (build/name).is_file() or (build/name).read_bytes()!=(source/name).read_bytes():
            shutil.copyfile(source/name,build/name)

def dxc_status(root):
    """doctor's line on the DirectX Shader Compiler release."""
    directory=root/'dxc-download'/DXC_VERSION/'x64'
    ready=all((directory/Path(member).name).is_file() for member in DXC_FILES)
    mirror=os.environ.get('DXC_MIRROR')
    return (f'DirectX Shader Compiler: {DXC_VERSION} in {directory} '
            f'{"extracted" if ready else "not extracted yet (setup and build take it)"}; '
            f'{"mirror " + mirror if mirror else "no mirror (DXC_MIRROR is not set)"}.')

def gameinput_status(root):
    """doctor's line on the GameInput package."""
    directory=root/'gameinput-sdk'/GAMEINPUT_VERSION
    ready=all((directory/relative).is_file() for relative in GAMEINPUT_FILES.values())
    state='fetched' if ready else 'not fetched yet (setup and build fetch it)'
    mirror=os.environ.get('GAMEINPUT_MIRROR')
    return (f'GameInput package: Microsoft.GameInput {GAMEINPUT_VERSION} in {directory} {state}; '
            f'{"mirror " + mirror if mirror else "no mirror (GAMEINPUT_MIRROR is not set)"}.')

# The resolve shaders go through D3DCompile in Windows' own d3dcompiler_47.dll (System32), the
# compiler that fxc.exe fronts, by ctypes: nothing to fetch or build, and no Windows SDK tool on the
# builder's PATH (docs/analysis/builder-installer.md 4.8). The flags are fxc's /O3 /Gfp; the strip
# flags are /Qstrip_reflect /Qstrip_debug /Qstrip_priv.
D3DCOMPILE_PREFER_FLOW_CONTROL=1<<10
D3DCOMPILE_OPTIMIZATION_LEVEL3=1<<15
D3DCOMPILER_STRIP_FLAGS=0x1|0x2|0x8
D3D_COMPILE_STANDARD_FILE_INCLUDE=1

def d3dcompiler(path=None):
    """d3dcompiler_47.dll with the ctypes signatures of D3DCompile, D3DStripShader and
    D3DDisassemble; path: another copy of it (System32's by default)."""
    import ctypes
    from ctypes import wintypes
    library=ctypes.WinDLL(str(path) if path else
                          os.path.join(os.environ.get('SystemRoot',r'C:\Windows'),'System32','d3dcompiler_47.dll'))
    blob=ctypes.POINTER(ctypes.c_void_p)
    library.D3DCompile.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_char_p,ctypes.c_void_p,
                                 ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p,wintypes.UINT,
                                 wintypes.UINT,ctypes.POINTER(blob),ctypes.POINTER(blob)]
    library.D3DStripShader.argtypes=[ctypes.c_void_p,ctypes.c_size_t,wintypes.UINT,ctypes.POINTER(blob)]
    library.D3DDisassemble.argtypes=[ctypes.c_void_p,ctypes.c_size_t,wintypes.UINT,ctypes.c_char_p,
                                     ctypes.POINTER(blob)]
    for function in (library.D3DCompile,library.D3DStripShader,library.D3DDisassemble):
        function.restype=ctypes.c_long
    return library

def blob_bytes(blob):
    """The contents of an ID3DBlob (GetBufferPointer, GetBufferSize), which is then released."""
    import ctypes
    table=ctypes.cast(blob[0],ctypes.POINTER(ctypes.c_void_p))
    method=lambda index,result:ctypes.WINFUNCTYPE(result,ctypes.c_void_p)(table[index])
    try:
        return ctypes.string_at(method(3,ctypes.c_void_p)(blob),method(4,ctypes.c_size_t)(blob))
    finally:
        method(2,ctypes.c_ulong)(blob)

def d3d_compile(source,target,defines=(),library=None):
    """(stripped DXBC, its disassembly) of the HLSL file source with entry point main, as fxc
    /T target /O3 /Gfp /Qstrip_reflect /Qstrip_debug /Qstrip_priv with /D defines ('NAME' or
    'NAME=VALUE'; a bare name is 1, as fxc defines it). Includes resolve from source's folder
    (the standard include handler). Raises RuntimeError with the compiler's messages."""
    import ctypes
    library=library or d3dcompiler()
    source=Path(source).resolve()
    data=source.read_bytes()
    pairs=[(name.encode(),value.encode()) for name,_,value in
           (define.partition('=') if '=' in define else (define,'','1') for define in defines)]
    macros=(ctypes.c_char_p*(2*len(pairs)+2))(*[text for pair in pairs for text in pair],None,None)
    code,errors=ctypes.POINTER(ctypes.c_void_p)(),ctypes.POINTER(ctypes.c_void_p)()
    result=library.D3DCompile(data,len(data),str(source).encode(),macros,
                              D3D_COMPILE_STANDARD_FILE_INCLUDE,b'main',target.encode(),
                              D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_PREFER_FLOW_CONTROL,0,
                              ctypes.byref(code),ctypes.byref(errors))
    messages=blob_bytes(errors).decode(errors='replace').rstrip('\0') if errors else ''
    if result<0 or not code:
        raise RuntimeError(f'D3DCompile of {source} ({target}) failed: 0x{result&0xffffffff:08x}\n{messages}')
    compiled=blob_bytes(code)
    stripped=ctypes.POINTER(ctypes.c_void_p)()
    result=library.D3DStripShader(compiled,len(compiled),D3DCOMPILER_STRIP_FLAGS,ctypes.byref(stripped))
    if result<0:
        raise RuntimeError(f'D3DStripShader of {source} failed: 0x{result&0xffffffff:08x}')
    compiled=blob_bytes(stripped)
    listing=ctypes.POINTER(ctypes.c_void_p)()
    result=library.D3DDisassemble(compiled,len(compiled),0,None,ctypes.byref(listing))
    if result<0:
        raise RuntimeError(f'D3DDisassemble of {source} failed: 0x{result&0xffffffff:08x}')
    return compiled,blob_bytes(listing).rstrip(b'\0').decode()

def shader_header(bytecode,disassembly,name):
    """The header that fxc /Fh /Vn name writes: the disassembly in #if 0, then the bytes as
    const BYTE name[], six to a line, with CRLF line endings."""
    rows=[', '.join(f'{value:3d}' for value in bytecode[index:index+6])
          for index in range(0,len(bytecode),6)]
    text=(f'#if 0\n{disassembly}#endif\n\nconst BYTE {name}[] =\n{{\n'+
          ', \n'.join('    '+row for row in rows)+'\n};\n')
    return text.replace('\r\n','\n').replace('\n','\r\n').encode()

def compile_shader(source,target,output,name,defines=(),log=None,library=None):
    """d3d_compile of source into the header output (shader_header) as the step log.stem of
    --progress, with the compiler's error in log."""
    log.parent.mkdir(parents=True,exist_ok=True)
    with progress('step',log.stem,log):
        try:
            bytecode,disassembly=d3d_compile(source,target,defines,library)
        except RuntimeError as error:
            log.write_text(f'{error}\n',encoding='utf-8')
            raise RuntimeError(f'{error}\nlog: {log}') from error
        log.write_text(f'D3DCompile {source} {target} {" ".join(defines)} -> {output} '
                       f'({len(bytecode)} bytes)\n',encoding='utf-8')
        Path(output).write_bytes(shader_header(bytecode,disassembly,name))

def resolve_shader_jobs(source):
    """The shaders of compile_resolve_shaders below the runtime source folder source, as
    (HLSL file, target, header, array name, defines)."""
    shaders=source/'src/graphics/shaders'
    shader_source=shaders/'fm4-resolve'
    bytecode=shaders/'bytecode/d3d12_5_1'
    xe=['SHADING_LANGUAGE_HLSL_XE=1']
    jobs=[(shader_source/f'{name}.cs.xesl','cs_5_1',bytecode/f'{name}_cs.h',f'{name}_cs',xe)
          for name in RESOLVE_SHADERS]
    jobs.append((shaders/'resolve_downscale.cs.hlsl','cs_5_1',bytecode/'resolve_downscale_cs.h',
                 'resolve_downscale_cs',[]))
    jobs+=[(shader_source/'resolve_host_32bpp.xesli','cs_5_1',bytecode/f'{name}_cs.h',f'{name}_cs',
            xe+defines) for name,defines in RESOLVE_HOST_SHADERS.items()]
    jobs.append((source/'src/ui/shaders/guest_output_area.ps.hlsl','ps_5_1',
                 source/'src/ui/shaders/bytecode/d3d12_5_1/guest_output_area_ps.h','guest_output_area_ps',[]))
    return jobs

def compile_resolve_shaders(root):
    """FM4 resolve shaders, the scaled readback downscale shader and the presenter's area
    downscale shader -> DXBC headers through D3DCompile (compile_shader), byte for byte the ones
    fxc.exe writes; only when their sources changed."""
    source = root/'runtime-source'
    jobs = resolve_shader_jobs(source)
    shaders = source/'src/graphics/shaders'
    downscale = shaders/'resolve_downscale.cs.hlsl'
    area = source/'src/ui/shaders/guest_output_area.ps.hlsl'
    inputs = hashlib.sha256(b''.join(path.read_bytes() for path in
                                     sorted((shaders/'fm4-resolve').glob('*.xesl*'))+[downscale,area]))
    marker = source/'.fm4-resolve-shaders'
    if (marker.is_file() and marker.read_text() == inputs.hexdigest() and
            all(output.is_file() for _,_,output,_,_ in jobs)):
        return
    library = d3dcompiler()
    for hlsl,target,output,name,defines in jobs:
        compile_shader(hlsl,target,output,name,defines,root/f'logs/{name.rsplit("_",1)[0]}.log',library)
    marker.write_text(inputs.hexdigest())

def ffx_mirror():
    """FFX_MIRROR (environment, or --ffx-mirror): another Git URL of the FidelityFX SDK that
    holds the pinned tag (a fork, for example on the owner's GitHub), or a local .bundle file
    (ffx_write_bundle). Used when GitHub cannot be reached; a .bundle path that does not exist
    yet is written after a fetch from GitHub."""
    return os.environ.get('FFX_MIRROR') or None

def ffx_is_bundle(mirror):
    return mirror is not None and mirror.lower().endswith('.bundle')

def ffx_head(sdk):
    result=subprocess.run(['git','-C',sdk,'rev-parse','HEAD'],capture_output=True,text=True)
    return result.stdout.strip() if result.returncode==0 else None

def ffx_patterns(repository):
    """The blob of the sparse-checkout patterns (FFX_PATHS) in a repository, for the
    sparse:oid filter."""
    return subprocess.run(['git','-C',repository,'hash-object','-w','--stdin'],
                          input=''.join(f'{path}\n' for path in FFX_PATHS).encode(),
                          capture_output=True,check=True).stdout.decode().strip()

def remove_git_tree(path):
    """shutil.rmtree for a Git repository, whose object files are read-only on Windows."""
    def writable(function, name, _):
        os.chmod(name, stat.S_IWRITE)
        function(name)
    if Path(path).exists():
        shutil.rmtree(path, onerror=writable)

def ffx_restore_bundle(bundle, sdk, log):
    """A checkout of FFX_REV from a bundle of ffx_write_bundle, without network: the bundle
    holds the pinned commit as a shallow commit, its trees and the blobs of FFX_PATHS (Git
    does not clone a filtered bundle, so the pack is unbundled into a new repository)."""
    heads=subprocess.check_output(['git','bundle','list-heads',bundle],text=True).split()
    if FFX_REV not in heads:
        raise RuntimeError(f'{bundle} does not hold the pinned FidelityFX SDK commit {FFX_REV}.')
    run(['git','init','-q',sdk],log=log)
    run(['git','-C',sdk,'bundle','unbundle',bundle],log=log)
    (sdk/'.git/shallow').write_text(FFX_REV+'\n')
    run(['git','-C',sdk,'tag','-f',FFX_TAG,FFX_REV],log=log)
    for key,value in (('core.repositoryformatversion','1'),('extensions.partialclone','origin'),
                      ('remote.origin.url',FFX_URL),('remote.origin.promisor','true'),
                      ('remote.origin.partialclonefilter','blob:none')):
        run(['git','-C',sdk,'config',key,value],log=log)

def ffx_write_bundle(sdk, bundle, log):
    """A bundle of FFX_REV that ffx_restore_bundle reads: a shallow, sparse-filtered local
    clone of the checkout (nothing is fetched from the network), bundled with that filter.
    About 15 MB."""
    temporary=sdk.parent/'fidelityfx-sdk-bundle'
    remove_git_tree(temporary)
    patterns=ffx_patterns(sdk)
    no_network={**os.environ,'GIT_NO_LAZY_FETCH':'1'}
    run(['git','-c','protocol.file.allow=always','clone','-q','--bare','--depth','1','--no-local',
         f'--filter=sparse:oid={patterns}','--branch',FFX_TAG,'-u',
         'git -c uploadpack.allowFilter=true -c uploadpack.allowAnySHA1InWant=true upload-pack',
         sdk.as_uri(),temporary],log=log,env=no_network)
    if ffx_head(temporary)!=FFX_REV:
        raise RuntimeError(f'{temporary} is not at the pinned FidelityFX SDK commit {FFX_REV}.')
    bundle=Path(bundle)
    bundle.parent.mkdir(parents=True,exist_ok=True)
    part=bundle.with_name(bundle.name+'.part')
    run(['git','-C',temporary,'bundle','create',part,f'--filter=sparse:oid={ffx_patterns(temporary)}',
         FFX_TAG],log=log,env=no_network)
    part.replace(bundle)
    remove_git_tree(temporary)
    print(f'FidelityFX SDK bundle written to {bundle}.')

FFX_REPOSITORY='GPUOpen-LibrariesAndSDKs/FidelityFX-SDK'
# The archive_tree of FFX_PATHS unpacked from the archive of FFX_REV.
FFX_TREE='2b72ecbb3f15ed10c0e4066bfc713d039039046c11267a36d397e0342ca42037'
FFX_MARKER='.fm4-fidelityfx.json'

def ffx_archive_wanted():
    return json.loads(json.dumps({'revision':FFX_REV,'paths':FFX_PATHS,'tree':FFX_TREE}))

def fetch_fidelityfx_archive(root):
    """--fetch archive: FFX_PATHS of GitHub's archive of FFX_REV in <BuildRoot>/fidelityfx-sdk,
    checked against FFX_TREE. FFX_MIRROR may name a local copy of that archive (used first) or
    another URL of it."""
    sdk=root/'fidelityfx-sdk'
    marker=sdk/FFX_MARKER
    if marker.is_file() and json.loads(marker.read_text())==ffx_archive_wanted():
        return sdk
    if (sdk/'.git').exists():
        raise RuntimeError(f'{sdk} is a checkout of --fetch git: build with --fetch git, or use a new BuildRoot.')
    mirror=ffx_mirror()
    if ffx_is_bundle(mirror):
        print(f'FFX_MIRROR {mirror} is a bundle, which --fetch git reads; fetching the archive.')
        mirror=None
    log=root/'logs/fidelityfx-download.log'
    log.parent.mkdir(parents=True,exist_ok=True)
    with progress('step','fidelityfx-download',log):
        archive=download_archive(FFX_REPOSITORY,FFX_REV,root/'downloads',log,mirror)
    log=root/'logs/fidelityfx-unpack.log'
    stage=root/'fidelityfx-unpack'
    with progress('step','fidelityfx-unpack',log):
        unpack_checked(archive,stage,FFX_REV,FFX_TREE,[path.lstrip('/') for path in FFX_PATHS],log)
        place(stage,sdk)
    marker.write_text(json.dumps(ffx_archive_wanted()))
    return sdk

def fetch_fidelityfx(root):
    """<BuildRoot>/fidelityfx-sdk at FFX_REV, sparse (FFX_PATHS), from GitHub or else from
    FFX_MIRROR (ffx_mirror). Stops when the checked-out commit differs from the pinned one.
    With --fetch archive, fetch_fidelityfx_archive."""
    if fetch_mode()=='archive':
        return fetch_fidelityfx_archive(root)
    sdk=root/'fidelityfx-sdk'
    log=root/'logs/fidelityfx-fetch.log'
    mirror=ffx_mirror()
    if not (sdk/'.git').is_dir() and ffx_is_bundle(mirror) and Path(mirror).is_file():
        # A local bundle first: no network, and the commit is checked all the same.
        print(f'FidelityFX SDK from the mirror bundle {mirror}.')
        ffx_restore_bundle(Path(mirror),sdk,log)
    if not (sdk/'.git').is_dir():
        clone=['git','clone','--filter=blob:none','--no-checkout','--sparse','--branch',FFX_TAG]
        try:
            run([*clone,FFX_URL,sdk],log=log)
        except RuntimeError:
            remove_git_tree(sdk)
            if not mirror or (ffx_is_bundle(mirror) and not Path(mirror).is_file()):
                raise RuntimeError(f'{FFX_URL} cannot be reached and FFX_MIRROR names no mirror '
                                   'that exists (docs/setup.md#the-fidelityfx-sdk-mirror).')
            print(f'{FFX_URL} cannot be reached; using the mirror {mirror}.')
            if ffx_is_bundle(mirror):
                ffx_restore_bundle(Path(mirror),sdk,log)
            else:
                run([*clone,mirror,sdk],log=log)
    listed=subprocess.run(['git','-C',sdk,'sparse-checkout','list'],capture_output=True,
                          text=True).stdout.split()
    if sorted(listed)!=sorted(FFX_PATHS):
        run(['git','-C',sdk,'sparse-checkout','set','--no-cone',*FFX_PATHS],log=log)
    if ffx_head(sdk)!=FFX_REV or not (sdk/'sdk/LICENSE.txt').is_file():
        run(['git','-C',sdk,'checkout','--detach',FFX_REV],log=log)
    head=ffx_head(sdk)
    if head!=FFX_REV:
        raise RuntimeError(f'{sdk} is at {head}, not the pinned FidelityFX SDK commit {FFX_REV}.')
    if ffx_is_bundle(mirror) and not Path(mirror).is_file():
        ffx_write_bundle(sdk,Path(mirror),log)
    return sdk

def ffx_status(root):
    """doctor's line on the FidelityFX SDK checkout and its mirror."""
    sdk=root/'fidelityfx-sdk'
    marker=sdk/FFX_MARKER
    if marker.is_file():
        state=('unpacked from the archive of the pinned commit'
               if json.loads(marker.read_text())==ffx_archive_wanted() else
               'unpacked from an archive of another pin: a build with FSR unpacks it again')
        return f'FidelityFX SDK: {sdk} {state} {FFX_REV[:7]} ({FFX_TAG}).'
    head=ffx_head(sdk) if (sdk/'.git').is_dir() and shutil.which('git') else None
    state=('not fetched yet (a setup or build with --fsr on fetches it)' if head is None else
           f'at the pinned commit {FFX_REV[:7]} ({FFX_TAG})' if head==FFX_REV else
           f'at {head[:7]}, not the pinned commit {FFX_REV[:7]}: setup checks it out again')
    mirror=ffx_mirror()
    if mirror is None:
        mirror_state='no mirror (FFX_MIRROR is not set)'
    elif ffx_is_bundle(mirror):
        mirror_state=(f'mirror bundle {mirror}' if Path(mirror).is_file() else
                      f'mirror bundle {mirror} not written yet (setup writes it)')
    else:
        mirror_state=f'mirror {mirror}'
    return f'FidelityFX SDK: {sdk} {state}; {mirror_state}.'

def build_runtime(root, jobs, fsr=None, build=None):
    """fsr: True or False sets REX_FSR (AMD FSR 3.1 from the FidelityFX SDK source, fetched
    by fetch_fidelityfx); None keeps the build directory's setting (off when it is new). build:
    another build directory, which is built but not installed."""
    source, install = root/'runtime-source', root/'runtime-install'
    default_build = build is None
    build = root/'runtime-build' if default_build else Path(build)
    sync_runtime(root)
    fetch_thirdparty(root)
    patch_thirdparty(root)
    compile_resolve_shaders(root)
    options=[]
    if fsr is not None:
        options=['-DREX_FSR=ON',f'-DREX_FSR_SDK_DIR={fetch_fidelityfx(root).as_posix()}'] if fsr \
            else ['-DREX_FSR=OFF']
    # SDL's GameInput driver with the package's API v3 header and loader (REX_GAMEINPUT_DIR);
    # a build folder configured without it is configured again.
    gameinput=fetch_gameinput(root).as_posix()
    cache=build/'CMakeCache.txt'
    if not cache.is_file() or f'REX_GAMEINPUT_DIR:PATH={gameinput}' not in cache.read_text(errors='replace'):
        options.append(f'-DREX_GAMEINPUT_DIR={gameinput}')
    if not (build/'build.ninja').is_file() or options:
        # mingw: line tables and PDBs (MINGW_DEBUG_FLAGS).
        flags='-march=x86-64-v2'+(' '+MINGW_DEBUG_FLAGS if ABI=='mingw' else '')
        linker=[f'-DCMAKE_SHARED_LINKER_FLAGS={MINGW_PDB_LINK}',f'-DCMAKE_EXE_LINKER_FLAGS={MINGW_PDB_LINK}'] \
            if ABI=='mingw' else []
        run(['cmake','-S',source,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
             *compilers('C','CXX'),f'-DCMAKE_C_FLAGS={flags}',f'-DCMAKE_CXX_FLAGS={flags}',*linker,
             '-DREXGLUE_ENABLE_TRACY=OFF','-DREXGLUE_BUILD_TESTS=OFF','-DREXGLUE_USE_VULKAN=OFF',
             *options,f'-DCMAKE_INSTALL_PREFIX={install}'],
            log=root/('logs/runtime-configure.log' if default_build else f'logs/{build.name}-configure.log'))
    run(['cmake','--build',build,'--parallel',jobs],log=root/f'logs/{build.name}.log')
    if default_build:
        run(['cmake','--install',build],log=root/'logs/runtime-install.log')
        # Beside rexglue.exe and the runtime DLLs, which build copies beside the game.
        stage_mingw_runtime(install/'bin')

def setup(root, jobs, fsr=None):
    # The FidelityFX SDK only for a build with FSR (build_runtime fetches it).
    # The two fetches without a process of their own are steps of --progress too.
    with progress('step','gameinput-fetch'):
        fetch_gameinput(root)
    with progress('step','dxc-fetch'):
        fetch_dxc(root)
    build_runtime(root, jobs, fsr)
    (root/'setup-complete.json').write_text(json.dumps({'thirdparty_revision':SDK_REV,
                                                        'fidelityfx_revision':FFX_REV,
                                                        'gameinput_version':GAMEINPUT_VERSION,
                                                        'runtime':'work/runtime','abi':ABI}))

def sync_title(project):
    """Copies the title's sources (work/fm4-rex/src and tests, its CMakeLists.txt) into the
    project, unchanged files untouched. Only source files from the source-only checkout are
    copied. No developer cache."""
    inventory=json.loads((REPO/'outputs/FM4-source-control/tracked-files.json').read_text())
    for entry in inventory['files']:
        name=entry['path']
        if name.startswith(('work/fm4-rex/src/','work/fm4-rex/tests/')):
            destination=project/Path(name).relative_to('work/fm4-rex')
            destination.parent.mkdir(parents=True,exist_ok=True)
            copy_source(REPO/name,destination)
    copy_source(REPO/'work/fm4-rex/CMakeLists.txt',project/'CMakeLists.txt')

def generate(root, disc1, disc2, content, jobs):
    install, project = root/'runtime-install', root/'project'
    cli = install/'bin/rexglue.exe'
    if not cli.is_file() or not (root/'setup-complete.json').is_file():
        raise RuntimeError('Run setup first.')
    project.mkdir(exist_ok=True)
    sync_title(project)
    (project/'generated').mkdir(exist_ok=True)
    copy_source(HERE/'rexglue.cmake',project/'generated/rexglue.cmake')
    copy_source(HERE/'guard-generated.py',project/'guard-generated.py')
    for name in ('fm4_bounds.toml','fm4_initializers.toml','fm4_callbacks.toml','fm4_prior_cfg.toml','speech_bounds.toml','xmedia_bounds.toml'):
        copy_source(HERE/name,project/name)
    loader = root/'loader-build'
    run(['cmake','-S',HERE/'loader','-B',loader,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
         *compilers('CXX'),f'-DCMAKE_PREFIX_PATH={install}'],log=root/'logs/loader-configure.log')
    run(['cmake','--build',loader,'--parallel',jobs],log=root/'logs/loader-build.log')
    env = dict(os.environ, PATH=str(install/'bin')+os.pathsep+os.environ['PATH'])
    run([loader/'fm4_dump.exe',disc1/'default.xex',root/'loaded-image.bin'],cwd=root,env=env,log=root/'logs/image-load.log')
    tables = write_switches(root/'loaded-image.bin',HERE/'switch-recipes.json',project/'fm4_switches.toml')
    print(f'Generated {tables} private switch tables from the supplied XEX.')
    quote = lambda path: json.dumps(str(path).replace('\\','/'))
    manifest = '[project]\nname = "fm4_rex"\nsdk_version = "0.10.0"\ngame_root = '+quote(disc1)+'\n'
    manifest += '\n[entrypoint]\nfile_path = '+quote(disc1/'default.xex')+'\nout_directory_path = "generated/default"\n'
    manifest += 'includes = ["fm4_bounds.toml", "fm4_switches.toml", "fm4_initializers.toml", "fm4_callbacks.toml", "fm4_prior_cfg.toml"]\n'
    for name,directory in [('XMediaFacade_default','xmedia'),('SpeechFacade_default','speech')]:
        manifest += '\n[[modules]]\nguest_path = '+json.dumps('game:\\'+name+'.xex')+'\nfile_path = '+quote(disc1/(name+'.xex'))+'\n'
        manifest += f'out_directory_path = "generated/{directory}"\nincludes = ["{directory}_bounds.toml"]\n'
    (project/'fm4_rex_manifest.toml').write_text(manifest,encoding='utf-8')
    run([cli,'codegen',project/'fm4_rex_manifest.toml'],cwd=project,log=root/'logs/codegen.log')
    for directory in ('default','xmedia','speech'):
        args = [sys.executable,project/'guard-generated.py',project/f'generated/{directory}',root/'loaded-image.bin']
        run(args + ([] if directory=='default' else ['--module']),log=root/f'logs/guard-{directory}.log')
    release = disc_release(disc1)
    print(f'Disc 1: {release} release, media ID {RELEASES[release]["media_id"]}.')
    state = {'disc1':str(disc1),'disc2':str(disc2) if disc2 else None,
             'content':[str(Path(path).resolve()) for path in content or ()],'release':release,
             'executables':RELEASES[release]['executables']}
    (root/'local.json').write_text(json.dumps(state,indent=2),encoding='utf-8')

FLAVORS = ('thinlto','v3','pgo-gen','pgo-use')

def game_build_dir(root,flavor=()):
    """game-build for the default build; game-build-<flavor> for opt-in compiler experiments."""
    return root/('game-build' if not flavor else 'game-build-'+'-'.join(sorted(flavor)))

def flavor_flags(root,flavor):
    # Opt-in only: the default flags stay -march=x86-64-v2 without LTO/PGO. The project keeps
    # its own floating-point flags (-ffp-model=strict); these only add to them.
    flags=['-march=x86-64-v3' if 'v3' in flavor else '-march=x86-64-v2']
    if 'thinlto' in flavor: flags.append('-flto=thin')
    profile_dir=root/'pgo'
    if 'pgo-gen' in flavor: flags.append(f'-fprofile-generate={profile_dir.as_posix()}')
    if 'pgo-use' in flavor:
        profile=profile_dir/'fm4.profdata'
        if not profile.is_file():
            raise RuntimeError(f'Missing {profile}; merge the pgo-gen *.profraw files with llvm-profdata first.')
        flags+=[f'-fprofile-use={profile.as_posix()}','-Wno-profile-instr-out-of-date','-Wno-profile-instr-unprofiled']
    return ' '.join(flags)

def build_game(root,jobs,flavor=(),fsr=None,tests='developer'):
    """tests: developer, every test (the default), or player (TEST_SETS)."""
    project, install, build = root/'project',root/'runtime-install',game_build_dir(root,flavor)
    if not (root/'local.json').is_file():
        raise RuntimeError('Run generate first.')
    state=json.loads((root/'local.json').read_text())
    validate_discs(state['disc1'],state['disc2'])
    # Edits in work/runtime are picked up here: sync, incremental build, install.
    build_runtime(root,jobs,fsr)
    # And edits in work/fm4-rex: a build of the project as generate left it ran without the
    # title's hooks of later commits (photo mode of entry 0 at s, 26 October 2026).
    sync_title(project)
    suffix=build.name[len('game-build'):]
    flags=flavor_flags(root,flavor)
    if ABI=='mingw':
        # Line tables and PDBs (MINGW_DEBUG_FLAGS); lld's MinGW driver takes ThinLTO's jobs as
        # --thinlto-jobs.
        flags+=' '+MINGW_DEBUG_FLAGS
        link=MINGW_PDB_LINK+(f' -Wl,--thinlto-jobs={jobs}' if 'thinlto' in flavor else '')
    else:
        # clang++ drives lld-link, so linker options go through -Xlinker.
        link=f'-Xlinker /opt:lldltojobs={jobs}' if 'thinlto' in flavor else ''
    linker=[f'-DCMAKE_EXE_LINKER_FLAGS={link}',f'-DCMAKE_SHARED_LINKER_FLAGS={link}'] if link else []
    run(['cmake','-S',project,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
         *compilers('CXX'),f'-DCMAKE_CXX_FLAGS={flags}',*linker,
         f'-DCMAKE_PREFIX_PATH={install}',f'-DPython3_EXECUTABLE={sys.executable}',
         f'-DFM4_LOADED_IMAGE={root / "loaded-image.bin"}'],log=root/f'logs/game-configure{suffix}.log')
    started=time.time()
    run(['cmake','--build',build,'--parallel',jobs],log=root/f'logs/game-build{suffix}.log')
    print(f'Game build took {time.time()-started:.0f} s.')
    # Always stage the matching runtime/plugin, including incremental builds; with mingw also
    # libc++ and libunwind (in install/bin) and the runtime's PDBs.
    for dll in (install/'bin').glob('*.dll'):
        shutil.copyfile(dll,build/dll.name)
    if ABI=='mingw':
        for name in RUNTIME_PDBS:
            shutil.copyfile(root/'runtime-source/out/win-amd64'/name,build/name)
    stage_dxc(root,build)
    run([build/'fm4_instruction_tests.exe'],cwd=build,log=root/f'logs/instruction-tests{suffix}.log')
    post_pass_tests(root,jobs,tests)
    artifacts = ['fm4_rex.exe','fm4_rex_XMediaFacade_default.dll','fm4_rex_SpeechFacade_default.dll','rexruntime.dll','rexgpu-xenos.dll']
    if ABI=='mingw':
        artifacts += MINGW_RUNTIME_DLLS
    report = {'artifacts':{name:digest(build/name) for name in artifacts},'gameplay_verified':False,
              'source':source_state(root),'tests':tests,'abi':ABI}
    (root/f'build-result{suffix}.json').write_text(json.dumps(report,indent=2))
    if not flavor:
        build_launcher(root,jobs)
        launcher_tests(root)
    print('Build and instruction tests passed. Run with Build.ps1 -Action run using the same -BuildRoot.')

def runtime_fsr(build):
    """The FidelityFX SDK checkout of a runtime build folder configured with REX_FSR on, else
    None."""
    cache=Path(build)/'CMakeCache.txt'
    if not cache.is_file():
        return None
    text=cache.read_text(errors='replace')
    if not re.search(r'^REX_FSR:BOOL=ON$',text,re.M):
        return None
    match=re.search(r'^REX_FSR_SDK_DIR:PATH=(.+)$',text,re.M)
    return Path(match.group(1).strip()) if match else None

# The tests of build (--tests, docs/setup.md#3-build): developer, the default, runs them all;
# player, the builder's build (docs/analysis/builder-installer.md 4.11), keeps the instruction
# tests, the launcher tests and PLAYER_TESTS, the tests of C++ that the player's compiler builds
# into the game, the runtime and the launcher (seconds), and leaves out SHADER_TESTS, the tests of
# the project's HLSL on WARP and with FXC, which is the same on every computer (minutes).
PLAYER_TESTS=('fm4_post_pass_camera_tests','fm4_track_settings_tests','fm4_texture_replacement_tests',
              'fm4_draw_scale_tests','fm4_pad_shaping_tests','fm4_keyboard_model_tests',
              'fm4_native_shader_contract_tests','fm4_wheel_ffb_tests','fm4_wheel_mapping_tests',
              'fm4_wheel_host_tests')
SHADER_TESTS=('fm4_post_pass_shader_tests','fm4_shader_mod_tests','fm4_look_tonemap_tests')
TEST_SETS=('developer','player')

def post_pass_tests(root,jobs,tests='developer'):
    """CPU tests of the runtime's post passes (docs/analysis/post-pass-framework.md) and of the
    shader mods, no GPU: the scene camera math, the GTAO passes of work/fm4-rex/passes on WARP
    (Direct3D 12 on the CPU) as the framework runs them, FXC compiles of the override library and
    the look mods with and without XE_POST_VIEWS (the library's post pass code is only compiled
    with it), the track lighting file edits of the look's track knobs (fm4-track-settings.h), and
    the texture replacement content hash and DDS files (docs/analysis/texture-replacement.md),
    the draw resolution scale as a fraction (docs/analysis/d3d-hle-n5.md), the physical pads'
    stick and trigger shaping (docs/analysis/modern-gamepads.md), the keyboard's steering and
    pedal model (docs/analysis/keyboard-controls.md), the native shader contract's manifest
    and pass classes (docs/analysis/shader-contract.md), and the racing wheel's force feedback
    governor, its driving controls' mapping and its host with an SDL virtual wheel and no force
    device (docs/analysis/racing-wheels.md).
    work/fm4-rex/tests, synced here like the runtime, built against the installed runtime headers. The WARP scene's checks were set with a GTAO radius of 1 m.
    tests: player builds and runs PLAYER_TESTS only (TEST_SETS)."""
    if tests not in TEST_SETS:
        raise ValueError(f'tests is one of {", ".join(TEST_SETS)}, not {tests!r}.')
    developer=tests=='developer'
    source,build=root/'project/tests',root/'tests-build'
    inventory=json.loads((REPO/'outputs/FM4-source-control/tracked-files.json').read_text())
    for entry in inventory['files']:
        if entry['path'].startswith('work/fm4-rex/tests/'):
            destination=source/Path(entry['path']).relative_to('work/fm4-rex/tests')
            destination.parent.mkdir(parents=True,exist_ok=True)
            copy_source(REPO/entry['path'],destination)
    # With a runtime built with REX_FSR, the WARP tests run FSR's pass too (the runtime's
    # fsr.cpp and the FidelityFX SDK's libraries, built again for the tests).
    sdk=runtime_fsr(root/'runtime-build')
    fsr=([f'-DREX_RUNTIME_SOURCE={(root/"runtime-source").as_posix()}']+
         (['-DREX_FSR=ON',f'-DREX_FSR_SDK_DIR={sdk.as_posix()}'] if sdk else ['-DREX_FSR=OFF']))
    run(['cmake','-S',source,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
         *compilers('CXX'),f'-DREX_HEADER_ROOT={root/"runtime-install/include"}',
         f'-DCMAKE_PREFIX_PATH={root/"runtime-install"}',*fsr],
        log=root/'logs/tests-configure.log')
    run(['cmake','--build',build,'--parallel',jobs,'--target',*PLAYER_TESTS,
         *(SHADER_TESTS if developer else ())],log=root/'logs/tests-build.log')
    # The tests run with the toolchain off PATH too.
    stage_mingw_runtime(build)
    run([build/'fm4_track_settings_tests.exe'],cwd=build,log=root/'logs/track-settings-tests.log')
    run([build/'fm4_texture_replacement_tests.exe'],cwd=build,
        log=root/'logs/texture-replacement-tests.log')
    run([build/'fm4_post_pass_camera_tests.exe'],cwd=build,log=root/'logs/post-pass-camera-tests.log')
    run([build/'fm4_draw_scale_tests.exe'],cwd=build,log=root/'logs/draw-scale-tests.log')
    run([build/'fm4_pad_shaping_tests.exe'],cwd=build,log=root/'logs/pad-shaping-tests.log')
    run([build/'fm4_keyboard_model_tests.exe'],cwd=build,log=root/'logs/keyboard-model-tests.log')
    run([build/'fm4_wheel_ffb_tests.exe'],cwd=build,log=root/'logs/wheel-ffb-tests.log')
    run([build/'fm4_wheel_mapping_tests.exe'],cwd=build,log=root/'logs/wheel-mapping-tests.log')
    run([build/'fm4_wheel_host_tests.exe',build],cwd=build,log=root/'logs/wheel-host-tests.log')
    run([build/'fm4_native_shader_contract_tests.exe',LOOK_DIRECTORY/'shaders.toml'],cwd=build,
        log=root/'logs/native-shader-contract-tests.log')
    if not developer:
        print('Player tests: the shader tests on WARP and with FXC are left out (--tests player).')
        return
    run([build/'fm4_look_tonemap_tests.exe',LOOK_DIRECTORY],cwd=build,log=root/'logs/look-tonemap-tests.log')
    run([build/'fm4_post_pass_shader_tests.exe',root/'runtime-source/src/graphics/d3d12/post_pass.hlsli',
         PASSES_DIRECTORY,'GTAO_RADIUS=1'],cwd=build,log=root/'logs/post-pass-shader-tests.log')
    # FSR's checks run on the first hardware adapter (WARP crashes in FSR's shading change
    # pyramid pass), not while the game runs, so a measurement there stays undisturbed.
    if sdk:
        if subprocess.run(['tasklist','/FI','IMAGENAME eq fm4_rex.exe'],capture_output=True,
                          text=True).stdout.count('fm4_rex.exe'):
            print('FSR post pass tests not run: fm4_rex.exe is running.')
        else:
            run([build/'fm4_post_pass_shader_tests.exe',
                 root/'runtime-source/src/graphics/d3d12/post_pass.hlsli',PASSES_DIRECTORY,
                 'GTAO_RADIUS=1','--hardware'],cwd=build,
                log=root/'logs/post-pass-shader-tests-hardware.log')
    run([build/'fm4_shader_mod_tests.exe',root/'runtime-source/src/graphics/d3d12/shader_override.hlsli',
         LOOK_DIRECTORY,';'.join(look_defines(look_knobs(DEFAULT_LOOK)))],cwd=build,
        log=root/'logs/shader-mod-tests.log')

def build_profiling(root,jobs):
    """game-build-prof: game-build with a runtime that has line tables and PDBs, for the
    command processor sampling profiler (REX_CP_PROFILE, docs/e2e/traces.md). The runtime writes its
    outputs into <source>/out, so this compiles a mirror of runtime-source (runtime-source-prof)
    and leaves the default build alone."""
    game=root/'game-build'
    if not (game/'fm4_rex.exe').is_file():
        raise RuntimeError('Run build first.')
    if ABI=='mingw':
        # The mingw build has the line tables and PDBs already (MINGW_DEBUG_FLAGS): a copy of it.
        profiling=root/'game-build-prof'
        profiling.mkdir(exist_ok=True)
        for path in game.iterdir():
            if path.suffix in ('.exe','.dll','.pdb'):
                shutil.copyfile(path,profiling/path.name)
        print(f'Profiling build in {profiling} (game-build with its PDBs): e2e/e2e.py --game-build '
              'game-build-prof, then e2e/cp_profile.py.')
        return
    sync_runtime(root)
    fetch_thirdparty(root)
    compile_resolve_shaders(root)
    source,mirror,build=root/'runtime-source',root/'runtime-source-prof',root/'runtime-build-prof'
    current=set()
    for path in source.rglob('*'):
        relative=path.relative_to(source)
        if relative.parts[0]=='out' or not path.is_file():
            continue
        current.add(relative)
        (mirror/relative).parent.mkdir(parents=True,exist_ok=True)
        copy_source(path,mirror/relative)
    for path in list(mirror.rglob('*')):
        relative=path.relative_to(mirror)
        if relative.parts[0]!='out' and path.is_file() and relative not in current:
            path.unlink()
    flags='-march=x86-64-v2 -gline-tables-only -gcodeview'
    if not (build/'build.ninja').is_file():
        run(['cmake','-S',mirror,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
             *compilers('C','CXX'),f'-DCMAKE_C_FLAGS={flags}',f'-DCMAKE_CXX_FLAGS={flags}',
             '-DCMAKE_SHARED_LINKER_FLAGS=-Xlinker /DEBUG','-DCMAKE_EXE_LINKER_FLAGS=-Xlinker /DEBUG',
             '-DREXGLUE_ENABLE_TRACY=OFF','-DREXGLUE_BUILD_TESTS=OFF','-DREXGLUE_USE_VULKAN=OFF'],
            log=root/'logs/runtime-configure-prof.log')
    run(['cmake','--build',build,'--parallel',jobs],log=root/'logs/runtime-build-prof.log')
    profiling=root/'game-build-prof'
    profiling.mkdir(exist_ok=True)
    for path in game.iterdir():
        if path.suffix in ('.exe','.dll'):
            shutil.copyfile(path,profiling/path.name)
    for name in ('rexgpu-xenos.dll','rexgpu-xenos.pdb','rexruntime.dll','rexruntime.pdb'):
        shutil.copyfile(mirror/'out/win-amd64'/name,profiling/name)
    print(f'Profiling build in {profiling}: e2e/e2e.py --game-build game-build-prof, then e2e/cp_profile.py.')

LOOK_KNOBS={'sharpen':'FM4_LOOK_SHARPEN','bloom':'FM4_LOOK_BLOOM','exposure':'FM4_LOOK_EXPOSURE',
            'contrast':'FM4_LOOK_CONTRAST','saturation':'FM4_LOOK_SATURATION',
            'vignette':'FM4_LOOK_VIGNETTE','motion_blur':'FM4_LOOK_MOTION_BLUR',
            'tonemap':'FM4_LOOK_TONEMAP',
            # The auto exposure's adaptation at the game's speed at any guest vblank rate
            # (work/fm4-rex/look/fm4-look.hlsli): 1 above 60 fps slows the title's per frame
            # adaptation to its time at 60, 0 keeps it.
            'adaptation':'FM4_LOOK_ADAPTATION',
            # Car paint (work/fm4-rex/look/fm4-paint.hlsli).
            'clearcoat':'FM4_LOOK_CLEARCOAT','clearcoat_roughness':'FM4_LOOK_CLEARCOAT_ROUGHNESS',
            'clearcoat_glint':'FM4_LOOK_CLEARCOAT_GLINT','flakes':'FM4_LOOK_FLAKES',
            'flake_scale':'FM4_LOOK_FLAKE_SCALE',
            # Car glass, chrome and rims (work/fm4-rex/look/fm4-car.hlsli); clearcoat_glint
            # limits their highlights too.
            'glass':'FM4_LOOK_GLASS','glass_roughness':'FM4_LOOK_GLASS_ROUGHNESS',
            'chrome':'FM4_LOOK_CHROME','chrome_roughness':'FM4_LOOK_CHROME_ROUGHNESS',
            'rims':'FM4_LOOK_RIMS','rims_roughness':'FM4_LOOK_RIMS_ROUGHNESS',
            # Sky dome and vegetation of the tracks (work/fm4-rex/look/fm4-nature.hlsli): the
            # color of the sky texture and of the green of leaves and grass. The track knob sky
            # (LOOK_TRACK_KNOBS) scales the sky's brightness instead.
            'sky_saturation':'FM4_LOOK_SKY_SATURATION','sky_contrast':'FM4_LOOK_SKY_CONTRAST',
            'sky_warmth':'FM4_LOOK_SKY_WARMTH','sky_horizon':'FM4_LOOK_SKY_HORIZON',
            'foliage_hue':'FM4_LOOK_FOLIAGE_HUE','foliage_saturation':'FM4_LOOK_FOLIAGE_SATURATION',
            'foliage_brightness':'FM4_LOOK_FOLIAGE_BRIGHTNESS',
            # Road surface of the tracks (work/fm4-rex/look/fm4-road.hlsli): the tone of the
            # asphalt texture, the game's sun highlight on the road, its lines, edges and
            # shoulders, and a GGX sheen with Fresnel in its place; road_debug shows the terms
            # of the lit road. The track knob road_gloss (LOOK_TRACK_KNOBS) scales the game's
            # specular amounts instead.
            'road_brightness':'FM4_LOOK_ROAD_BRIGHTNESS','road_contrast':'FM4_LOOK_ROAD_CONTRAST',
            'road_specular':'FM4_LOOK_ROAD_SPECULAR','road_sheen':'FM4_LOOK_ROAD_SHEEN',
            'road_roughness':'FM4_LOOK_ROAD_ROUGHNESS','road_sheen_grain':'FM4_LOOK_ROAD_SHEEN_GRAIN',
            'road_debug':'FM4_LOOK_ROAD_DEBUG',
            # Cockpit interior and driver (work/fm4-rex/look/fm4-cockpit.hlsli): cockpit_fill, the
            # sky's fill of the shaded interior (the game's hemispherical term, weighted by the
            # normal and limited by the track lightmap's shade); cockpit_light, the least share
            # of the sun's direct light that the interior keeps in its own shadow (no preset
            # since 26 October 2026: it swings with the sun's direction and lights tunnels).
            'cockpit_fill':'FM4_LOOK_COCKPIT_FILL','cockpit_light':'FM4_LOOK_COCKPIT_LIGHT',
            # Ambient occlusion of the GTAO post pass on the scene; above 0 it also turns
            # the post passes on (ao_cvars).
            'ao':'FM4_LOOK_AO',
            # Aerial haze: the strength of the post passes' haze amount (work/fm4-rex/passes/
            # haze.hlsl, its param HAZE, look_defines) and the final combine's mix toward the haze
            # color (fm4_haze); above 0 it also turns the post passes on. haze_brightness: the
            # haze's brightness. temperature: the final image's white balance. bloom_width: a
            # wide halo of the bloom.
            'haze':'FM4_LOOK_HAZE','haze_brightness':'FM4_LOOK_HAZE_BRIGHTNESS',
            'temperature':'FM4_LOOK_TEMPERATURE','bloom_width':'FM4_LOOK_BLOOM_WIDTH'}
# Knobs that set runtime cvars instead of shader defines. sun: the strength of the sun glare
# on the RTV path. The title reads its sun visibility from an awaited occlusion query, which
# the host query counts in samples, so the same sun is 1x, 2x and 4x as bright at 1x, 2x and
# 4x MSAA. With the knob the runtime reports guest pixels times the strength, through a soft
# limit (SUN_LIMIT pixels) that keeps the glare from washing out the screen. Without it
# (look original) the counted samples are reported. -Rov always reports its counted samples.
LOOK_RUNTIME_KNOBS=('sun',)
SUN_LIMIT=1200
# Knobs of the post passes instead of the look mods. taa above 0: temporal anti-aliasing at the
# final point (work/fm4-rex/passes/taa.hlsl, docs/analysis/taa.md), with the scene draws
# jittered; the look's sharpening moves after its resolve (TAA_SHARPEN), since sharpening the
# jittered frame before it would sharpen the jitter, and -SingleTile drops its FXAA.
# taa_point=1 moves TAA to the scene_color point (TAA_POINT): before the game's post chain, its
# output written back into the scene, with the occlusion's own temporal filter; the look's
# sharpening then stays in the final combine, after TAA.
# aa=fsr (with taa above 0): AMD FSR 3.1's native anti-aliasing at the scene_color point in
# place of TAA (FSR, work/fm4-rex/passes/fsr.hlsl, docs/analysis/fsr-plan.md), with the scene
# jittered in every frame and the look's sharpening as FSR's RCAS (FSR_SHARPNESS, none in the
# final combine); a runtime built without FSR (Build.ps1 -Fsr) runs TAA at that point instead.
# aa=taau (with taa above 0): TAA at the scene_color point (taa_point=1); while upscaling
# (-Upscale) it is the upscaler (TAA_UPSCALE, docs/analysis/fsr-plan.md 14), with the scene
# jittered in every frame and FSR's sharpening (fsr_sharpness) as its TAA_SHARPEN, none in the
# final combine. aa=taa, the default, is TAA.
# windshield above 0: the glass of the cockpit camera's windshield and side windows, which the
# title does not draw there (work/fm4-rex/passes/windshield.hlsl, docs/analysis/windshield.md):
# the strength of its tint, dashboard reflection, glare and dust (WINDSHIELD), up to 2.
# fsr_sharpen (0 to 1): FSR's RCAS strength in place of the default, the look's sharpening plus
# 0.4 times the upscale ratio above 1 (fsr_sharpness, docs/analysis/fsr-plan.md 13).
LOOK_PASS_KNOBS=('taa','taa_point','aa','windshield','fsr_sharpen')
# Knobs that take a name instead of a number.
LOOK_NAMES={'tonemap':{'filmic':0,'gt':1,'aces':2,'neutral':3,'gt7':4}}
# Knobs that edit the title's per-track lighting files instead of shaders (FM4_TRACK_SETTINGS,
# work/fm4-rex/src/fm4-track-settings.h): multipliers of each track's own values, 1 the
# original. road_gloss scales the specular of the road (RoadSpecular and RoadSpecular2 of
# RoadSpecular); sky scales skyScale, the brightness constant of the sky shaders (SkyScale).
LOOK_TRACK_KNOBS={'road_gloss':('RoadSpecular.RoadSpecular','RoadSpecular.RoadSpecular2'),
                  'sky':('skyScale.value',)}
# Named looks; a -Look value may mix them with knobs ('aces,sharpen=0.6': later items win).
# aces, gt and neutral differ only in the tone curve and share the mods of the earlier pc preset
# (since 20 October 2026; before, aces, gt and neutral had only the clear coat, ambient
# occlusion and the sun knob): the color grade, the clear coat with its sun highlight limited
# to 1 (clearcoat_glint), ambient occlusion, the sun glare, the sky brightness, TAA (taa=1, its
# sharpening after the resolve), the cockpit interior's sky fill (cockpit_fill, since 26
# October 2026 in place of the shadow floor cockpit_light=0.5), the cockpit glass (windshield),
# the road's grained sheen instead of its sun highlight
# (road_specular=0,road_sheen=0.4), the glass, chrome and rim mods, and since 26 October 2026
# the auto exposure at the game's 60 fps speed above 60 fps (adaptation). pc is aces under its
# earlier name (LOOK_ALIASES). original is the game's own picture with TAA and no shader mod;
# 'original,taa=0' is the game's picture as the title draws it, the reference against -Rov.
# No preset has the sky_* or foliage_* knobs: their A/B at 2x of 18 October 2026 showed only a
# slightly deeper sky (docs/analysis/look-mod-coverage.md). Every preset has TAA, so races
# render one tile without MSAA with them (auto_single_tile).
# gt7 (since 29 October 2026) is the look of Gran Turismo 7: its published tone mapping
# (tonemap=gt7, Polyphony Digital's GT7 operator) on the same mods, with a deeper, more
# contrasted sky, a fifth of a stop less exposure, since its curve reaches white 3 stops above
# middle gray, and a light S-curve for its wider range; fm2023 (since 29 October 2026) the look
# of Forza Motorsport (2023): warmer, hazier, a little less saturated and lower highlights, by the
# references' ratios against GT7, its warmth on the near field so that the distance and the sky
# stay a pale blue-gray (docs/analysis/look-presets-gt7-fm2023.md).
_LOOK_MODS=('saturation=1.1,bloom=0.7,sharpen=0.4,clearcoat=1,clearcoat_glint=1,ao=0.6,sun=2,sky=0.7,'
            'taa=1,cockpit_fill=1,windshield=1,road_specular=0,road_sheen=0.4,glass=1,chrome=1,rims=1,'
            'adaptation=1')
LOOK_PRESETS={'aces':'tonemap=aces,'+_LOOK_MODS,'gt':'tonemap=gt,'+_LOOK_MODS,
              'neutral':'tonemap=neutral,'+_LOOK_MODS,
              'gt7':'tonemap=gt7,'+_LOOK_MODS+',sky_saturation=1.5,sky_contrast=0.3,exposure=-0.2,'
                    'contrast=0.15',
              'fm2023':'tonemap=aces,'+_LOOK_MODS+',saturation=1.05,ao=0.9,haze=0.4,temperature=0.7,'
                       'exposure=-0.3',
              'original':'taa=1'}
LOOK_ALIASES={'pc':'aces'}
DEFAULT_LOOK='aces'

# Render quality of races, five tiers of detail (-Quality; measured in
# docs/analysis/quality-presets.md). Each sets the race render scenarios (FM4_RACE_SETTINGS,
# fm4-render-overrides.cpp: controls of the race render scenarios), the GTAO quality of the
# look's ambient occlusion (QUALITY_GTAO) and, for ultra, 4x MSAA on the native path where it
# keeps the picture (QUALITY_MSAA); the render scale, -Upscale and the look stay separate.
# Every tier has SoftParticles: without it the title resolves no scene depth in races, and the
# look's scene passes (GTAO, TAA, the windshield) do not run there.
# low: the title's own with fewer cars and no collidables in the mirror (its other cuts, crowd,
# car LOD distances, half rate bloom and the particle rate, saved nothing measurable; the 16 car
# shader turns the player's paint white). medium: the title's own (xbox, its alias, which had
# no SoftParticles until 26 October 2026). high gives races what its replays and photo mode have:
# 4x MSAA (TilingScenario 0 instead of 1, 2x), shadows of collidable objects, full detail
# wheels, car LOD transitions twice as far, and undoes the 16 car cuts (a cheaper car shader,
# no damage, environment map never updated, half rate bloom, no collidables in the mirror).
# extrahigh also keeps every car at its top LOD; ultra also renders the mirror every frame with
# every car. The tiling scenario applies only where races keep the title's tiles (-Msaa, -Rov,
# a look without TAA).
_HIGH=['TilingScenario=0','SoftParticles=1','CollidableShadows=1','CarMinWheelLOD=0',
       'CarLODTransDist=near:30.0,far:50.0,veryfar:100.0','CarShader=-','CarDamage=1',
       'CarDamageTextures=1','EnvMapFrequencyScale=1.0','HalfRateBloom=-','CollidablesInMirror=-']
QUALITY_PRESETS={'low':['SoftParticles=1','MaxCarsInMirror=3','CollidablesInMirror=0'],
                 'medium':['SoftParticles=1'],'high':_HIGH,
                 'extrahigh':_HIGH+['CarKeepLOD0=1'],
                 'ultra':_HIGH+['CarKeepLOD0=1','HalfRateMirror=0','MaxCarsInMirror=11']}
QUALITY_ALIASES={'xbox':'medium'}
QUALITY_NAMES={'low':'Low','medium':'Medium','high':'High','extrahigh':'Extra High','ultra':'Ultra'}
# GTAO_QUALITY (work/fm4-rex/passes/passes.toml: 0 2x2 slices x steps, 1 2x4, 2 3x6, 3 4x8),
# set where it differs from passes.toml's own (2).
QUALITY_GTAO={'low':0,'medium':1,'high':2,'extrahigh':3,'ultra':3}
# One pass with 4x MSAA on the native path, where it renders the same picture as 2x.
QUALITY_MSAA={'ultra':4}
DEFAULT_QUALITY='high'

def quality_preset(quality):
    """The tier of a -Quality value: DEFAULT_QUALITY for None, an alias resolved."""
    quality=DEFAULT_QUALITY if quality is None else QUALITY_ALIASES.get(quality,quality)
    if quality not in QUALITY_PRESETS:
        raise RuntimeError(f'Unknown quality {quality!r}; use '
                           f'{", ".join([*QUALITY_PRESETS,*QUALITY_ALIASES])}.')
    return quality

def race_settings(quality,single_tile=False):
    """FM4_RACE_SETTINGS of a quality preset; one tile (-SingleTile, auto_single_tile) keeps its
    own tiling scenario."""
    return ';'.join(s for s in QUALITY_PRESETS[quality_preset(quality)]
                    if not (single_tile and s.startswith('TilingScenario=')))

def quality_cvars(quality,cvars):
    """The tier's GTAO quality: GTAO_QUALITY added to shader_mod_defines where the post passes
    run (post_pass_path) and the tier's differs from passes.toml's; a GTAO_QUALITY already
    there wins."""
    gtao=QUALITY_GTAO[quality_preset(quality)]
    defines=[d for d in str(cvars.get('shader_mod_defines','')).split(';') if d]
    if ('post_pass_path' not in cvars or any(d.startswith('GTAO_QUALITY=') for d in defines) or
            gtao==int(passes_settings(cvars['post_pass_path'],{})['GTAO_QUALITY'])):
        return {}
    return {'shader_mod_defines':';'.join(defines+[f'GTAO_QUALITY={gtao}'])}

def quality_msaa_samples(quality,native,msaa_samples,single_tile,msaa,rov,display,fullscreen,
                         supersample,resolution_scale,monitor,upscale):
    """The one-pass MSAA of the tier on the native path and a note for the description: ultra's
    4x where the native plan is a whole scale at 16:9 without -Upscale (one pass with 4x has no
    fractional scale or logical size), so that 4x renders the same picture as 2x; otherwise
    msaa_samples as given (None: launch_plan's 2x). -MsaaSamples, -SingleTile, -Msaa and -Rov
    keep theirs."""
    samples=QUALITY_MSAA.get(quality_preset(quality))
    if not samples or not native or msaa_samples is not None or single_tile or msaa or rov:
        return msaa_samples,''
    name=QUALITY_NAMES[quality_preset(quality)]
    try:
        whole=(upscale_ratio(upscale)==1.0 and
               display_settings(display,fullscreen,supersample,resolution_scale,monitor,True)[0]==
               display_settings(display,fullscreen,supersample,resolution_scale,monitor,False)[0])
    except ValueError:
        whole=False
    if whole:
        return samples,f'; {name}: {samples}x MSAA'
    return msaa_samples,f'; {name}: 2x MSAA ({samples}x needs a whole scale at 16:9 without -Upscale)'

LOOK_DIRECTORY=REPO/'work/fm4-rex/look'
# The files of a look directory that a run reads: the mods and their libraries, the native
# shader contract's manifest shaders.toml (docs/analysis/shader-contract.md), and the
# textures its entries read (look_files).
LOOK_SUFFIXES=('.hlsli','.toml')
# The runtime's post passes (docs/analysis/post-pass-framework.md): passes.toml and its HLSL.
PASSES_DIRECTORY=REPO/'work/fm4-rex/passes'

def look_knob_defaults():
    """Every knob of the look mods and its default: the #ifndef NAME / #define NAME value pairs
    of work/fm4-rex/look (the first file that has one wins)."""
    defaults={}
    for path in sorted(LOOK_DIRECTORY.glob('*.hlsli')):
        for name,value in re.findall(r'^#ifndef (FM4_LOOK_\w+)[ \t]*\r?\n#define \1[ \t]+(\S+)',
                                     path.read_text(encoding='utf-8'),re.M):
            defaults.setdefault(name,value)
    return defaults

def look_knobs(look):
    """The knobs of a -Look value: 'aces', 'sharpen=0.5,bloom=0.8,tonemap=aces' or both (later
    items win), launcher names (LOOK_KNOBS) to their values."""
    knobs={}
    for item in filter(None,(part.strip() for part in look.split(','))):
        if '=' not in item:
            item=LOOK_ALIASES.get(item,item)
            if item not in LOOK_PRESETS:
                raise RuntimeError(f'Unknown look {item!r}; use {", ".join((*LOOK_PRESETS,*LOOK_ALIASES))} '
                                   'or knobs.')
            if item=='original':
                knobs.clear()
            knobs.update(part.split('=',1) for part in filter(None,LOOK_PRESETS[item].split(',')))
            continue
        name,_,value=item.partition('=')
        knobs[name]=value
    return knobs

def taa_enabled(knobs):
    """The taa knob of a look's knobs is above 0."""
    value=float(knobs.get('taa','0'))
    if value<0:
        raise RuntimeError(f'taa is 0 (off) or 1 (on), not {knobs["taa"]!r}.')
    return value>0

def taa_scene_point(knobs):
    """The taa_point knob: 0 (the final point, the default) or 1 (the scene_color point)."""
    value=knobs.get('taa_point','0')
    if value not in ('0','1'):
        raise RuntimeError(f'taa_point is 0 (final) or 1 (scene_color), not {value!r}.')
    return value=='1'

def aa_method(knobs):
    """The aa knob: taa (the default), fsr or taau."""
    value=knobs.get('aa','taa')
    if value not in ('taa','fsr','taau'):
        raise RuntimeError(f'aa is taa, fsr or taau, not {value!r}.')
    return value

def windshield_strength(knobs):
    """The windshield knob: 0 (off, the default) to 2."""
    value=float(knobs.get('windshield','0'))
    if not 0<=value<=2:
        raise RuntimeError(f'windshield is a strength from 0 (off) to 2, not {knobs["windshield"]!r}.')
    return value

def haze_strength(knobs):
    """The haze knob: 0 (off, the default) to 1."""
    value=float(knobs.get('haze','0'))
    if not 0<=value<=1:
        raise RuntimeError(f'haze is a strength from 0 (off) to 1, not {knobs["haze"]!r}.')
    return value

def fsr_sharpness(knobs,upscale_ratio=1.0):
    """FSR's RCAS strength (FSR_SHARPNESS): the fsr_sharpen knob, or the look's sharpening plus
    0.4 times the upscale ratio above 1 (0.2 more at quality's 1.5, nothing at ratio 1), at most 1."""
    if 'fsr_sharpen' in knobs:
        value=float(knobs['fsr_sharpen'])
        if not 0<=value<=1:
            raise RuntimeError(f'fsr_sharpen is a strength from 0 to 1, not {knobs["fsr_sharpen"]!r}.')
        return value
    sharpen=float(knobs.get('sharpen','0'))
    if upscale_ratio==1.0:
        return sharpen
    # Rounded to thousandths, as the launcher's port rounds it.
    return round(min(max(sharpen+0.4*(upscale_ratio-1.0),0.0),1.0),3)

def look_defines(knobs,upscale_ratio=1.0):
    """The shader_mod_defines of look knobs: NAME=VALUE with the hlsli names and numbers, and
    with taa the pass params TAA and TAA_SHARPEN (the look's sharpening, which then is 0), or at
    the scene_color point (taa_point=1) TAA_POINT with the look's sharpening kept, or with
    aa=fsr FSR at the scene_color point with the look's sharpening as FSR_SHARPNESS
    (fsr_sharpness: more while upscaling by upscale_ratio), or with aa=taau while upscaling TAA
    as the upscaler at that point with the same sharpening as TAA_SHARPEN (at ratio 1
    taa_point=1)."""
    defines=[]
    for name,value in knobs.items():
        if name in LOOK_RUNTIME_KNOBS or name in LOOK_TRACK_KNOBS or name in LOOK_PASS_KNOBS:
            continue
        if name not in LOOK_KNOBS:
            raise RuntimeError(f'Unknown look knob {name!r}; use '
                               f'{", ".join((*LOOK_KNOBS,*LOOK_RUNTIME_KNOBS,*LOOK_TRACK_KNOBS,*LOOK_PASS_KNOBS))}.')
        names=LOOK_NAMES.get(name,{})
        if value in names:
            value=names[value]
        elif names:
            raise RuntimeError(f'Unknown {name} {value!r}; use {", ".join(names)}.')
        defines.append(f'{LOOK_KNOBS[name]}={float(value)!r}')
    scene_point=taa_scene_point(knobs)
    aa=aa_method(knobs)
    fsr=aa=='fsr'
    if aa!='taa' and not taa_enabled(knobs):
        raise RuntimeError(f'aa={aa} needs taa above 0 (the look turns temporal anti-aliasing on).')
    if aa=='taau' and upscale_ratio!=1.0:
        # TAA as the upscaler at the scene_color point, jittered in every frame, with FSR's
        # sharpening for a like-for-like comparison.
        defines=[d for d in defines if not d.startswith('FM4_LOOK_SHARPEN=')]
        defines+=['FM4_LOOK_SHARPEN=0.0','TAA=1',
                  f'TAA_SHARPEN={fsr_sharpness(knobs,upscale_ratio)!r}','TAA_POINT=1',
                  'TAA_UPSCALE=1','TAA_JITTER_MOTION=0.0','TAA_JITTER_RATE=0.0']
    elif fsr:
        # FSR at the scene_color point, the look's sharpening as its RCAS.
        defines=[d for d in defines if not d.startswith('FM4_LOOK_SHARPEN=')]
        defines+=['FM4_LOOK_SHARPEN=0.0','TAA=1','TAA_SHARPEN=0.0','TAA_POINT=1','FSR=1',
                  f'FSR_SHARPNESS={fsr_sharpness(knobs,upscale_ratio)!r}']
    elif taa_enabled(knobs) and (scene_point or aa=='taau'):
        defines+=['TAA=1','TAA_SHARPEN=0.0','TAA_POINT=1']
    elif taa_enabled(knobs):
        sharpen=float(knobs.get('sharpen','0'))
        defines=[d for d in defines if not d.startswith('FM4_LOOK_SHARPEN=')]
        defines+=['FM4_LOOK_SHARPEN=0.0','TAA=1',f'TAA_SHARPEN={sharpen!r}']
    if windshield_strength(knobs)>0:
        defines.append(f'WINDSHIELD={windshield_strength(knobs)!r}')
    if haze_strength(knobs)>0:
        # The haze pass's strength; the mods' FM4_LOOK_HAZE mixes the scene by its amount.
        defines.append(f'HAZE={haze_strength(knobs)!r}')
    return defines

def look_track_settings(knobs):
    """FM4_TRACK_SETTINGS of the look knobs that edit the track lighting files ('' for none)."""
    items=[]
    for name,attributes in LOOK_TRACK_KNOBS.items():
        if name in knobs:
            value=float(knobs[name])
            if value<0:
                raise RuntimeError(f'{name} is a multiplier, 0 or more, not {knobs[name]!r}.')
            if value==1:
                continue
            items+=[f'{attribute}*={value!r}' for attribute in attributes]
    return ';'.join(items)

def look_cvars(root,look,shader_dev=False,upscale_ratio=1.0,stage=None):
    """Cvars of the look mods (work/fm4-rex/look/fm4-look.hlsli): copies them to BuildRoot/look
    (or <stage>/look: e2e.py gives each run its own, so two runs at once never rewrite the
    files the other's game reads)
    and passes the knobs of the look (look_knobs; knobs left out keep the original look). Only
    post pass knobs ('original': TAA): the post passes and their params without the look mods;
    none ('original,taa=0'): no mods at all.
    shader_dev (docs/analysis/shader-mod-iteration.md): the mods load from work/fm4-rex/look
    itself and hot reload when a file there changes, and every knob of the files is a live knob
    (the ones left out at their defaults), which shader_mod_set changes in the console (`).
    upscale_ratio: the scene's output lines over its input lines (scene_upscale_ratio)."""
    # The track lighting knobs are not shader knobs (look_track_settings).
    knobs={name:value for name,value in look_knobs(look).items() if name not in LOOK_TRACK_KNOBS}
    cvars=sun_cvars(knobs.get('sun'))
    mods=shader_dev or any(name not in LOOK_RUNTIME_KNOBS and name not in LOOK_PASS_KNOBS for name in knobs)
    defines=look_defines(knobs,upscale_ratio)
    if not mods:
        # The post pass params reach the passes through shader_mod_defines without the mods.
        defines=[define for define in defines if not define.startswith('FM4_LOOK_')]
        if defines:
            cvars['shader_mod_defines']=';'.join(defines)
        cvars.update(ao_cvars(root,taa_enabled(knobs) or windshield_strength(knobs)>0,stage=stage))
        return cvars
    if shader_dev:
        directory=LOOK_DIRECTORY
        given={define.split('=',1)[0] for define in defines}
        defines+=[f'{name}={float(value)!r}' for name,value in look_knob_defaults().items()
                  if name not in given]
    else:
        directory=(stage or root)/'look'
        if directory.is_dir():
            shutil.rmtree(directory)
        directory.mkdir()
        for path in look_files(LOOK_DIRECTORY):
            shutil.copyfile(path,directory/path.name)
    cvars.update({'shader_mod_path':directory,'shader_mod_defines':';'.join(defines)})
    if shader_dev:
        cvars.update(shader_mod_hot_reload='true',shader_mod_live_knobs='true')
    # The ao, taa, windshield and haze knobs need the runtime's post passes.
    cvars.update(ao_cvars(root,float(knobs.get('ao','0'))>0 or taa_enabled(knobs)
                          or windshield_strength(knobs)>0 or haze_strength(knobs)>0,shader_dev,stage))
    return cvars

def sun_cvars(strength):
    """Cvars of the sun knob (LOOK_RUNTIME_KNOBS): the guest pixels of the sun query times the
    strength (0 turns the glare off), through the soft limit. None without the knob."""
    if strength is None:
        return {}
    value=float(strength)
    if value<0:
        raise RuntimeError(f'The sun strength must be 0 or more, not {strength!r}.')
    return {'rtv_occlusion_awaited_scale':f'{value:g}','rtv_occlusion_awaited_limit':str(SUN_LIMIT)}

# Screens of -Display (16:9, as FM4's 1280x720); native is the primary monitor.
DISPLAYS={'720p':(1280,720),'1080p':(1920,1080),'1440p':(2560,1440),'4k':(3840,2160)}
DEFAULT_DISPLAY='native'
MAX_RESOLUTION_SCALE=4

def integrated_gpu():
    """Whether the computer has integrated GPUs only: every hardware adapter is UMA (it shares
    the system memory; D3D12_FEATURE_ARCHITECTURE of a Direct3D 12 device made on it), or has
    less than 1 GB of dedicated video memory where no device can be made. False when there is no
    hardware adapter or it cannot be told. The ROG Xbox Ally X's GPU is GPU bound at 2x
    (docs/analysis/d3d-hle-n4.md 17.21)."""
    import ctypes
    from ctypes import wintypes
    try:
        dxgi,d3d12=ctypes.WinDLL('dxgi'),ctypes.WinDLL('d3d12')
    except OSError:
        return False
    class GUID(ctypes.Structure):
        _fields_=[('a',wintypes.DWORD),('b',wintypes.WORD),('c',wintypes.WORD),('d',ctypes.c_ubyte*8)]
    def guid(a,b,c,d):
        return GUID(a,b,c,(ctypes.c_ubyte*8)(*d))
    class DESC1(ctypes.Structure):
        _fields_=[('description',wintypes.WCHAR*128),('vendor',wintypes.UINT),('device',wintypes.UINT),
                  ('subsys',wintypes.UINT),('revision',wintypes.UINT),('dedicated_video',ctypes.c_size_t),
                  ('dedicated_system',ctypes.c_size_t),('shared_system',ctypes.c_size_t),
                  ('luid_low',wintypes.DWORD),('luid_high',wintypes.LONG),('flags',wintypes.UINT)]
    class ARCHITECTURE(ctypes.Structure):
        _fields_=[('node',wintypes.UINT),('tile_based',wintypes.BOOL),('uma',wintypes.BOOL),
                  ('cache_coherent_uma',wintypes.BOOL)]
    def method(obj,index,*types):
        vtable=ctypes.cast(obj,ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
        return ctypes.WINFUNCTYPE(ctypes.c_long,ctypes.c_void_p,*types)(vtable[index])
    def release(obj):
        method(obj,2)(obj)
    factory=ctypes.c_void_p()
    iid_factory1=guid(0x770aae78,0xf26f,0x4dba,(0xa8,0x29,0x25,0x3c,0x83,0xd1,0xb3,0x87))
    iid_device=guid(0x189819f1,0x1db6,0x4b57,(0xbe,0x54,0x18,0x21,0x33,0x9b,0x85,0xf7))
    if dxgi.CreateDXGIFactory1(ctypes.byref(iid_factory1),ctypes.byref(factory)):
        return False
    hardware=integrated=0
    try:
        index=0
        while True:
            adapter=ctypes.c_void_p()
            # IDXGIFactory1::EnumAdapters1, IDXGIAdapter1::GetDesc1, ID3D12Device::CheckFeatureSupport.
            if method(factory,12,wintypes.UINT,ctypes.POINTER(ctypes.c_void_p))(factory,index,ctypes.byref(adapter)):
                break
            index+=1
            try:
                desc=DESC1()
                if method(adapter,10,ctypes.POINTER(DESC1))(adapter,ctypes.byref(desc)) or desc.flags&2:
                    continue
                hardware+=1
                uma=desc.dedicated_video<(1<<30)
                device=ctypes.c_void_p()
                if not d3d12.D3D12CreateDevice(adapter,0xb000,ctypes.byref(iid_device),ctypes.byref(device)):
                    architecture=ARCHITECTURE()
                    if not method(device,13,ctypes.c_int,ctypes.c_void_p,wintypes.UINT)(
                            device,1,ctypes.byref(architecture),ctypes.sizeof(architecture)):
                        uma=bool(architecture.uma)
                    release(device)
                integrated+=uma
            finally:
                release(adapter)
    finally:
        release(factory)
    return hardware>0 and integrated==hardware

def vrr_range(edid):
    """The variable refresh rate range (min Hz, max Hz) an EDID reports, or (0, 0): DisplayID's
    Adaptive-Sync data block (tag 0x2B) in a DisplayID extension, else EDID 1.4's range limits
    descriptor when its continuous frequency bit is set (as DisplayPort Adaptive-Sync panels
    report it)."""
    best=(0,0)
    for start in range(128,len(edid)-127,128):
        ext=edid[start:start+128]
        if ext[0]!=0x70:
            continue
        end=min(5+ext[2],127)
        i=5
        while i+3<=end:
            tag,size=ext[i],ext[i+2]
            if tag==0x2B:
                for d in range(i+3,min(i+3+size,end)-5,6):
                    low,high=ext[d+2],(ext[d+3]|((ext[d+4]&3)<<8))+1
                    if high>best[1]:
                        best=(low,high)
            i+=3+size
    if best[1]:
        return best
    if len(edid)>=128 and edid[19]>=4 and edid[0x18]&1:
        for d in range(54,126,18):
            block=edid[d:d+18]
            if block[0:3]==b'\0\0\0' and block[3]==0xFD:
                offsets=block[4]
                return (block[5]+(255 if offsets&1 else 0),block[6]+(255 if offsets&2 else 0))
    return (0,0)

def primary_display():
    """The primary monitor's refresh rate (Hz) and variable refresh rate range (vrr_range of its
    EDID in the registry), or 0 and (0, 0) when they cannot be read."""
    import ctypes
    import winreg
    from ctypes import wintypes
    class DISPLAY_DEVICE(ctypes.Structure):
        _fields_=[('cb',wintypes.DWORD),('DeviceName',wintypes.WCHAR*32),('DeviceString',wintypes.WCHAR*128),
                  ('StateFlags',wintypes.DWORD),('DeviceID',wintypes.WCHAR*128),('DeviceKey',wintypes.WCHAR*128)]
    class DEVMODE(ctypes.Structure):
        _fields_=[('dmDeviceName',wintypes.WCHAR*32),('dmSpecVersion',wintypes.WORD),('dmDriverVersion',wintypes.WORD),
                  ('dmSize',wintypes.WORD),('dmDriverExtra',wintypes.WORD),('dmFields',wintypes.DWORD),
                  ('dmPosition',ctypes.c_long*2),('dmDisplayOrientation',wintypes.DWORD),
                  ('dmDisplayFixedOutput',wintypes.DWORD),('dmColor',ctypes.c_short),('dmDuplex',ctypes.c_short),
                  ('dmYResolution',ctypes.c_short),('dmTTOption',ctypes.c_short),('dmCollate',ctypes.c_short),
                  ('dmFormName',wintypes.WCHAR*32),('dmLogPixels',wintypes.WORD),('dmBitsPerPel',wintypes.DWORD),
                  ('dmPelsWidth',wintypes.DWORD),('dmPelsHeight',wintypes.DWORD),('dmDisplayFlags',wintypes.DWORD),
                  ('dmDisplayFrequency',wintypes.DWORD),('dmICMMethod',wintypes.DWORD),('dmICMIntent',wintypes.DWORD),
                  ('dmMediaType',wintypes.DWORD),('dmDitherType',wintypes.DWORD),('dmReserved1',wintypes.DWORD),
                  ('dmReserved2',wintypes.DWORD),('dmPanningWidth',wintypes.DWORD),('dmPanningHeight',wintypes.DWORD)]
    user32=ctypes.windll.user32
    adapter=DISPLAY_DEVICE(cb=ctypes.sizeof(DISPLAY_DEVICE))
    index=0
    while user32.EnumDisplayDevicesW(None,index,ctypes.byref(adapter),0):
        index+=1
        if adapter.StateFlags&4:  # DISPLAY_DEVICE_PRIMARY_DEVICE
            break
    else:
        return 0,(0,0)
    mode=DEVMODE(dmSize=ctypes.sizeof(DEVMODE))
    refresh=mode.dmDisplayFrequency if user32.EnumDisplaySettingsW(adapter.DeviceName,-1,ctypes.byref(mode)) else 0
    monitor=DISPLAY_DEVICE(cb=ctypes.sizeof(DISPLAY_DEVICE))
    # EDD_GET_DEVICE_INTERFACE_NAME: \\?\DISPLAY#<model>#<instance>#{guid}.
    if not user32.EnumDisplayDevicesW(adapter.DeviceName,0,ctypes.byref(monitor),1):
        return refresh,(0,0)
    parts=monitor.DeviceID.split('#')
    if len(parts)<3:
        return refresh,(0,0)
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                            rf'SYSTEM\CurrentControlSet\Enum\DISPLAY\{parts[1]}\{parts[2]}\Device Parameters') as key:
            edid,_=winreg.QueryValueEx(key,'EDID')
    except OSError:
        return refresh,(0,0)
    return refresh,vrr_range(bytes(edid))

def primary_monitor():
    """Width and height in pixels and the DPI scale of the primary monitor, then its refresh rate
    and variable refresh rate range (primary_display), and whether the computer has integrated
    GPUs only (integrated_gpu)."""
    import ctypes
    from ctypes import wintypes
    user32,shcore=ctypes.windll.user32,ctypes.windll.shcore
    # Per monitor DPI aware, so the sizes are in pixels rather than scaled ones.
    shcore.SetProcessDpiAwareness(2)
    width,height=user32.GetSystemMetrics(0),user32.GetSystemMetrics(1)
    monitor=user32.MonitorFromPoint(wintypes.POINT(0,0),1)
    dpi_x,dpi_y=wintypes.UINT(),wintypes.UINT()
    refresh,(vrr_min,vrr_max)=primary_display()
    integrated=integrated_gpu()
    if shcore.GetDpiForMonitor(monitor,0,ctypes.byref(dpi_x),ctypes.byref(dpi_y)):
        return width,height,1.0,refresh,vrr_min,vrr_max,integrated
    return width,height,dpi_x.value/96,refresh,vrr_min,vrr_max,integrated

def screen_scale(width,height):
    """The scale of the largest 16:9 picture that fits a screen of width x height pixels, as a
    reduced fraction (numerator, denominator) of 1280x720: min(width / 1280, height / 720), at
    most 720 in the denominator (rounded down), at least 1 (docs/analysis/d3d-hle-n5.md 18.19)."""
    numerator,denominator=(width,1280) if width*720<=height*1280 else (height,720)
    if denominator>720:
        numerator,denominator=numerator*720//denominator,720
    if numerator<denominator:
        numerator,denominator=1,1
    divisor=math.gcd(numerator,denominator)
    return numerator//divisor,denominator//divisor

def logical_view(width,height):
    """The scene's logical size and scale for a screen of width x height pixels on the native
    path (docs/analysis/d3d-hle-n5.md 18.23): as wide as 16:9 or wider, Hor+ (720 lines, the
    width 720 times the aspect, rounded to a multiple of 8, at most 3840, and the scale
    height / 720); narrower, Vert+ (1280 wide, the height 1280 over the aspect, rounded to a
    multiple of 8, at most 1024, and the scale width / 1280). Returns (logical width, logical
    height, numerator, denominator) with the scale reduced, at most 720 in the denominator
    (rounded down) and at least 1."""
    if width*9>=height*16:
        logical_width=min(3840,max(1280,(1440*width+8*height)//(16*height)*8))
        logical_height=720
        numerator,denominator=height,720
    else:
        logical_width=1280
        logical_height=min(1024,max(720,(2560*height+8*width)//(16*width)*8))
        numerator,denominator=width,1280
    if denominator>720:
        numerator,denominator=numerator*720//denominator,720
    if numerator<denominator:
        numerator,denominator=1,1
    divisor=math.gcd(numerator,denominator)
    return logical_width,logical_height,numerator//divisor,denominator//divisor

def display_settings(display=DEFAULT_DISPLAY,fullscreen=False,supersample=False,
                     resolution_scale=None,monitor=None,scene_scale=False,upscale=False):
    """Cvars of -Display and a description. native: borderless fullscreen on the primary
    monitor; 720p, 1080p, 1440p, 4k: a window of that many pixels, or fullscreen with
    -Fullscreen or when it does not fit the monitor. The render scale is the smallest whole
    multiple of 1280x720 that covers the 16:9 picture of that screen (720p 1x, 1080p and 1440p
    2x, 4k 3x; a 2560x1600 monitor 2x), or 1x with integrated GPUs only (integrated_gpu), one
    more with -Supersample (up to 4x), or -ResolutionScale. With scene_scale (the native path in
    one pass with 2x MSAA, which renders the scene at a fractional scale, d3d-hle-n5.md 18.19;
    one pass with 4x is not tested at one yet) it is the screen's own scale instead, the largest 16:9 picture that fits (screen_scale: 1080p 3/2,
    1440p 2, a 2560x1600 monitor 2, letterboxed), except with integrated GPUs only (1x) or
    -Supersample (its whole part plus one); -ResolutionScale panel picks it on every GPU. That
    scale renders the screen's aspect too (logical_view: Hor+ wider than 16:9, Vert+ narrower,
    d3d-hle-n5.md 18.23); the other scales render 16:9, letterboxed or pillarboxed. The
    presenter fits the picture to the window, averaging the render over each screen pixel when
    it is larger (present_area_downscale). monitor: (width, height, DPI scale, refresh, VRR min,
    VRR max, integrated GPUs only) instead of the primary monitor's. upscale: -Upscale renders
    the scene below this scale (upscale_settings), so integrated GPUs take the screen's own
    scale too."""
    monitor_width,monitor_height,dpi,*rest=monitor or primary_monitor()
    integrated=len(rest)>3 and bool(rest[3])
    if display=='native':
        width,height,fullscreen=monitor_width,monitor_height,True
    else:
        width,height=DISPLAYS[display]
        # A window of the monitor's size or more does not fit beside its frame and the taskbar.
        fullscreen=fullscreen or width>=monitor_width or height>=monitor_height
    if resolution_scale=='panel' and not scene_scale:
        raise ValueError("-ResolutionScale panel needs the native path in one pass with 2x MSAA "
                         "(-Native without -SingleTile, -Msaa or -MsaaSamples 0 or 4).")
    own=screen_scale(width,height)
    logical_width,logical_height=1280,720
    integrated=integrated and not (scene_scale and upscale)
    if resolution_scale=='panel' or (scene_scale and not resolution_scale and not integrated
                                     and not supersample):
        logical_width,logical_height,numerator,denominator=logical_view(width,height)
    else:
        if scene_scale and supersample and not resolution_scale and not integrated:
            scale=min(MAX_RESOLUTION_SCALE,own[0]//own[1]+1)
        else:
            cover=1 if integrated else math.ceil(min(width/1280,height/720)-1e-6)
            scale=resolution_scale or min(MAX_RESOLUTION_SCALE,cover+int(supersample))
        numerator,denominator=scale,1
    whole=numerator//denominator
    cvars={'fullscreen':'true' if fullscreen else 'false'}
    if not fullscreen:
        # Window sizes are logical pixels: the runtime multiplies them by the DPI scale.
        cvars.update(window_width=str(round(width/dpi)),window_height=str(round(height/dpi)))
    if whole>1:
        # Render targets and resolved textures at N times 1280x720 on both axes;
        # CPU readbacks of resolves are downscaled.
        cvars['draw_resolution_scale_x']=cvars['draw_resolution_scale_y']=str(whole)
    logical=(logical_width,logical_height)!=(1280,720)
    if denominator>1 or logical:
        # The scene at the fraction (or, for a logical size, at the whole scale through the
        # scene draws' path), the other surfaces at its whole part; bit 32: the resolves write
        # the scene's textures at that scale (d3d-hle-n5.md 18.15).
        cvars.update({'native_draw_scale':f'{numerator}/{denominator}' if denominator>1
                      else str(numerator),'native_rt_surfaces':'58'})
        if denominator==1:
            cvars['native_draw_scale_variant']='true'
    if logical_width!=1280:
        cvars['native_logical_width']=str(logical_width)
    if logical_height!=720:
        cvars['native_logical_height']=str(logical_height)
    render_width=-(-logical_width*numerator//denominator)
    render_height=-(-logical_height*numerator//denominator)
    scale_text=f'{numerator}/{denominator}' if denominator>1 else f'{numerator}x'
    screen=(f'fullscreen {monitor_width}x{monitor_height}' if fullscreen
            else f'window {width}x{height}')
    gpu_note=' for an integrated GPU' if integrated and not resolution_scale else ''
    shown_width,shown_height=(monitor_width,monitor_height) if fullscreen else (width,height)
    view=''
    if logical:
        view=(f', logical {logical_width}x{logical_height} '
              +('Hor+' if logical_width!=1280 else 'Vert+'))
    # What the presenter adds around the picture: the screen's aspect against the render's.
    aspect_note=''
    if scene_scale and shown_width*render_height!=shown_height*render_width:
        bars=('letterboxed' if shown_width*render_height<shown_height*render_width
              else 'pillarboxed')
        aspect_note=f', {bars}' if logical else f', 16:9 {bars}'
    return cvars,(f'Display {display}: {screen}, render {scale_text} ({render_width}x{render_height})'
                  f'{view}{gpu_note}{aspect_note}')

# -Upscale (docs/running.md#-upscale, docs/analysis/fsr-plan.md 11): FSR 3.1 renders the scene
# at the output's lines divided by the mode's ratio and upscales it to the output scale.
UPSCALE_MODES={'native':1.0,'quality':1.5,'balanced':1.7,'performance':2.0}
DEFAULT_UPSCALE='native'

def scene_upscale_ratio(cvars):
    """The output lines over the input lines of a plan's cvars (upscale_settings), 1 without
    upscaling."""
    if 'native_scene_input_scale' not in cvars or 'native_draw_scale' not in cvars:
        return 1.0
    def lines(text):
        numerator,_,denominator=text.partition('/')
        numerator,denominator=int(numerator),int(denominator or 1)
        return (720*numerator+denominator//2)//denominator
    return lines(cvars['native_draw_scale'])/lines(cvars['native_scene_input_scale'])

def upscale_ratio(upscale):
    """The ratio of an -Upscale mode or number (1: native)."""
    upscale=upscale or DEFAULT_UPSCALE
    ratio=UPSCALE_MODES.get(upscale)
    if ratio is None:
        try:
            ratio=float(upscale)
        except ValueError:
            ratio=0.0
        if not 1.0<=ratio<=3.0:
            raise ValueError(f"Unknown -Upscale '{upscale}'; use {', '.join(UPSCALE_MODES)} or a "
                             "ratio from 1 to 3.")
    return ratio

def upscale_settings(upscale,cvars,aa,scene_scale):
    """The cvars of -Upscale over display_settings' cvars, and a description. native renders at
    the output scale; quality, balanced, performance or a ratio from 1 to 3 render the scene at
    round(output lines / ratio) lines (native_scene_input_scale, at least 720: lower is clamped
    to 720p), the other surfaces at its whole part, and FSR upscales the scene to the output scale
    (native_draw_scale). Needs aa=fsr and the native path in one pass with 2x MSAA."""
    upscale=upscale or DEFAULT_UPSCALE
    ratio=upscale_ratio(upscale)
    if ratio==1.0:
        return {},''
    if not scene_scale:
        raise ValueError("-Upscale needs the native path in one pass with 2x MSAA (-Native without "
                         "-SingleTile, -Msaa or -MsaaSamples 0 or 4).")
    if aa not in ('fsr','taau'):
        raise ValueError(f"-Upscale {upscale} needs aa=fsr or aa=taau in -Look: TAA does not "
                         "upscale.")
    numerator,_,denominator=cvars.get('native_draw_scale',
                                      cvars.get('draw_resolution_scale_x','1')).partition('/')
    numerator,denominator=int(numerator),int(denominator or 1)
    output_lines=(720*numerator+denominator//2)//denominator
    lines=int(output_lines/ratio+0.5)
    clamped=' (clamped to 720p)' if lines<720 else ''
    lines=max(720,lines)
    if lines>=output_lines:
        return {},f'; upscale {upscale}: none at {output_lines}p{clamped}'
    divisor=math.gcd(lines,720)
    input_numerator,input_denominator=lines//divisor,720//divisor
    whole=input_numerator//input_denominator
    return ({'native_draw_scale':f'{numerator}/{denominator}' if denominator>1 else str(numerator),
             'native_scene_input_scale':f'{input_numerator}/{input_denominator}'
                                        if input_denominator>1 else str(input_numerator),
             'draw_resolution_scale_x':str(whole),'draw_resolution_scale_y':str(whole),
             'native_rt_surfaces':'58'},
            f'; {"FSR" if aa=="fsr" else "TAAU"} {upscale} {lines}p -> {output_lines}p{clamped}')

# -Entry0: the menus and photo mode (the title's tiling entry 0) on the native path in one pass
# at the scene's scale (s, the default), or at the whole scale below it, scaled to the screen
# (k: the fallback for integrated GPUs if 4x MSAA at the screen's size costs too much there).
ENTRY0_SCALES=('s','k')
DEFAULT_ENTRY0='s'

def entry0_scale(entry0):
    """The -Entry0 value: s (the default) or k."""
    entry0=entry0 or DEFAULT_ENTRY0
    if entry0 not in ENTRY0_SCALES:
        raise ValueError(f"Unknown -Entry0 '{entry0}'; use {', '.join(ENTRY0_SCALES)}.")
    return entry0

VSYNC_MODES=('auto','on','off')
DEFAULT_VSYNC='auto'

# -Pad: the physical controller's options (docs/running.md#controller,
# docs/analysis/modern-gamepads.md), knob name to its runtime cvar, the launchers' default, and its
# choices or its range (bool knobs are 0 or 1). A knob whose launchers' default differs from the
# runtime's ('runtime_default') applies it when -Pad leaves the knob out: rumble 1.5, since the
# title's driving rumble is 0.05 to 0.2 of full and the owner preferred 1.5 on the ROG Xbox Ally X
# (25 October 2026). Only values that differ from the runtime's default become cvars. The GUI
# launcher reads this table from launcher.json.
PAD_BUTTONS=('a','b','x','y','lb','rb','back','start','ls','rs','up','down','left','right','guide',
             'none')
PAD_KNOBS={
    'driver':{'cvar':'sdl_joystick_driver','default':'default',
              'choices':['default','rawinput','wgi','gameinput']},
    'thread':{'cvar':'input_sdl_thread','default':'0','choices':['0','1']},
    'rumble':{'cvar':'pad_rumble_strength','default':'1.5','runtime_default':'1','min':0.0,
              'max':2.0},
    'trigger_rumble':{'cvar':'pad_trigger_rumble','default':'0','choices':['0','1']},
    'trigger_rumble_strength':{'cvar':'pad_trigger_rumble_strength','default':'1','min':0.0,'max':2.0},
    'stick_deadzone':{'cvar':'pad_stick_deadzone','default':'0','min':0.0,'max':0.5},
    'stick_outer':{'cvar':'pad_stick_outer','default':'1','min':0.5,'max':1.0},
    'stick_curve':{'cvar':'pad_stick_curve','default':'1','min':0.5,'max':3.0},
    'anti_deadzone':{'cvar':'pad_steering_anti_deadzone','default':'0','choices':['0','1']},
    'trigger_deadzone':{'cvar':'pad_trigger_deadzone','default':'0','min':0.0,'max':0.5},
    'trigger_outer':{'cvar':'pad_trigger_outer','default':'1','min':0.5,'max':1.0},
    'misc1':{'cvar':'pad_misc1','default':'guide','choices':list(PAD_BUTTONS)},
    'right_paddle1':{'cvar':'pad_right_paddle1','default':'y','choices':list(PAD_BUTTONS)},
    'left_paddle1':{'cvar':'pad_left_paddle1','default':'b','choices':list(PAD_BUTTONS)},
    'right_paddle2':{'cvar':'pad_right_paddle2','default':'x','choices':list(PAD_BUTTONS)},
    'left_paddle2':{'cvar':'pad_left_paddle2','default':'a','choices':list(PAD_BUTTONS)},
    'touchpad':{'cvar':'pad_touchpad','default':'guide','choices':list(PAD_BUTTONS)},
}

# -Keyboard: the keyboard's steering and pedal model (docs/running.md#keyboard,
# docs/analysis/keyboard-controls.md), as -Pad. The defaults are the launchers': launch_cvars
# turns the model on (mnk_smooth), and the rates are the runtime's defaults.
KEYBOARD_KNOBS={
    'smooth':{'cvar':'mnk_smooth','default':'1','choices':['0','1']},
    'steer_attack':{'cvar':'mnk_steer_attack','default':'0.3','min':0.0,'max':2.0},
    'steer_release':{'cvar':'mnk_steer_release','default':'0.2','min':0.0,'max':2.0},
    'throttle_attack':{'cvar':'mnk_throttle_attack','default':'0.25','min':0.0,'max':2.0},
    'throttle_release':{'cvar':'mnk_throttle_release','default':'0.1','min':0.0,'max':2.0},
    'brake_attack':{'cvar':'mnk_brake_attack','default':'0.15','min':0.0,'max':2.0},
    'brake_release':{'cvar':'mnk_brake_release','default':'0.1','min':0.0,'max':2.0},
    'precision':{'cvar':'mnk_precision','default':'0.5','min':0.1,'max':1.0},
    'title_deadzones':{'cvar':'mnk_title_deadzones','default':'1','choices':['0','1']},
    'mouse_steering':{'cvar':'mnk_mouse_steering','default':'0','choices':['0','1']},
    'mouse_steer_sensitivity':{'cvar':'mnk_mouse_steer_sensitivity','default':'1','min':0.1,
                               'max':10.0},
    'mouse_steer_centering':{'cvar':'mnk_mouse_steer_centering','default':'1','min':0.0,'max':5.0},
}

# -Wheel: the racing wheel (docs/running.md#racing-wheel, docs/analysis/racing-wheels.md), as -Pad.
# 'integer' knobs are whole numbers (axis and button numbers, -1 for none). The pedals' rest and
# pressed values are raw axis values (-32768 to 32767), set from the launcher's wheel test.
def wheel_pedal_knobs(pedal,axis):
    return {f'{pedal}_axis':{'cvar':f'wheel_{pedal}_axis','default':str(axis),'min':-1,'max':15,
                             'integer':True},
            f'{pedal}_rest':{'cvar':f'wheel_{pedal}_rest','default':'-32768','min':-32768,
                             'max':32767},
            f'{pedal}_pressed':{'cvar':f'wheel_{pedal}_pressed','default':'32767','min':-32768,
                                'max':32767},
            f'{pedal}_deadzone':{'cvar':f'wheel_{pedal}_deadzone','default':'0','min':0.0,
                                 'max':0.9},
            f'{pedal}_gamma':{'cvar':f'wheel_{pedal}_gamma','default':'1','min':0.2,'max':5.0}}

WHEEL_KNOBS={
    'on':{'cvar':'wheel','default':'0','choices':['0','1']},
    'input':{'cvar':'wheel_input','default':'1','choices':['0','1']},
    'steer_axis':{'cvar':'wheel_steer_axis','default':'0','min':0,'max':15,'integer':True},
    'steer_invert':{'cvar':'wheel_steer_invert','default':'0','choices':['0','1']},
    'steer_range':{'cvar':'wheel_steer_range','default':'900','min':90.0,'max':3600.0},
    'steer_lock':{'cvar':'wheel_steer_lock','default':'0','min':0.0,'max':3600.0},
    'car_lock':{'cvar':'wheel_car_lock','default':'900','min':90.0,'max':3600.0},
    **wheel_pedal_knobs('throttle',1),
    **wheel_pedal_knobs('brake',2),
    **wheel_pedal_knobs('clutch',-1),
    'shift_up':{'cvar':'wheel_shift_up_button','default':'-1','min':-1,'max':127,'integer':True},
    'shift_down':{'cvar':'wheel_shift_down_button','default':'-1','min':-1,'max':127,
                  'integer':True},
    'clutch_button':{'cvar':'wheel_clutch_button','default':'-1','min':-1,'max':127,
                     'integer':True},
    'handbrake_button':{'cvar':'wheel_handbrake_button','default':'-1','min':-1,'max':127,
                        'integer':True},
    'ffb_cap':{'cvar':'wheel_ffb_cap','default':'0.25','min':0.0,'max':1.0},
    'ffb_gain':{'cvar':'wheel_ffb_gain','default':'1','min':0.0,'max':4.0},
    'ffb_invert':{'cvar':'wheel_ffb_invert','default':'0','choices':['0','1']},
    'ffb_min_force':{'cvar':'wheel_ffb_min_force','default':'0','min':0.0,'max':0.5},
    'ffb_smoothing':{'cvar':'wheel_ffb_smoothing','default':'10','min':0.0,'max':200.0},
    'ffb_spring':{'cvar':'wheel_ffb_spring','default':'1','min':0.0,'max':2.0},
    'ffb_damper':{'cvar':'wheel_ffb_damper','default':'1','min':0.0,'max':2.0},
    'ffb_road':{'cvar':'wheel_ffb_road','default':'0.25','min':0.0,'max':1.0},
    'ffb_impact':{'cvar':'wheel_ffb_impact','default':'0.5','min':0.0,'max':1.0},
    'ffb_physics':{'cvar':'wheel_ffb_physics','default':'1','choices':['0','1']},
    'end_stop':{'cvar':'wheel_end_stop','default':'1','min':0.0,'max':1.0},
}

def option_knobs(text,table,option):
    """The knobs of a -Pad, -Keyboard or -Wheel value, 'name=value,...' (later items win)."""
    knobs={}
    for item in filter(None,(part.strip() for part in (text or '').split(','))):
        name,equals,value=item.partition('=')
        name,value=name.strip(),value.strip()
        if not equals or name not in table:
            raise ValueError(f"Unknown {option} knob {item!r}; use name=value with "
                             f"{', '.join(table)}.")
        knobs[name]=value
    return knobs

def option_cvars(text,table,option):
    """The runtime cvars of a -Pad or -Keyboard value over the launchers' defaults that differ
    from the runtime's (a knob's 'runtime_default'): the knobs whose value differs from the
    runtime's default ('runtime_default', else 'default')."""
    knobs={name:knob['default'] for name,knob in table.items() if 'runtime_default' in knob}
    knobs.update(option_knobs(text,table,option))
    cvars={}
    for name,value in knobs.items():
        knob=table[name]
        base=knob.get('runtime_default',knob['default'])
        if 'choices' in knob:
            if value not in knob['choices']:
                raise ValueError(f"{option} {name} is one of {', '.join(knob['choices'])}, "
                                 f"not {value!r}.")
            if value==base:
                continue
            text={'0':'false','1':'true'}.get(value,value) if knob['choices']==['0','1'] else value
        else:
            try:
                number=float(value)
            except ValueError:
                number=float('nan')
            if not knob['min']<=number<=knob['max'] or knob.get('integer') and number%1:
                raise ValueError(f"{option} {name} is {'a whole' if knob.get('integer') else 'a'} "
                                 f"number from {knob['min']:g} to {knob['max']:g}, not {value!r}.")
            if number==float(base):
                continue
            text=str(int(number)) if knob.get('integer') else repr(number)
        cvars[knob['cvar']]=text
    return cvars

def pad_knobs(pad):
    """The knobs of a -Pad value, 'driver=rawinput,trigger_rumble=1,rumble=1.5' (later items
    win)."""
    return option_knobs(pad,PAD_KNOBS,'-Pad')

def pad_cvars(pad):
    """The runtime cvars of a -Pad value over the launchers' defaults: the knobs whose value
    differs from the runtime's default."""
    return option_cvars(pad,PAD_KNOBS,'-Pad')

def keyboard_cvars(keyboard):
    """The runtime cvars of a -Keyboard value, 'steer_attack=0.4,precision=0.6': the knobs that
    differ from the launchers' defaults."""
    return option_cvars(keyboard,KEYBOARD_KNOBS,'-Keyboard')

def wheel_cvars(wheel):
    """The runtime cvars of a -Wheel value, 'on=1,throttle_rest=-30000,shift_up=4': the knobs that
    differ from the runtime's defaults."""
    return option_cvars(wheel,WHEEL_KNOBS,'-Wheel')

def present_settings(vsync,frame_rate,monitor):
    """Cvars, the race frame rate and a description of -VSync (docs/analysis/d3d-hle-n4.md 17.23).
    off: the host presents at once, tearing allowed (the runtime's default, and e2e's). on: on the
    display's refresh without tearing (present_vsync). auto: on a display with a variable
    refresh rate range up to its refresh (vrr_range), tearing presents held at least a refresh
    apart (present_min_interval_us) and races at most 3 fps under the refresh, inside the range;
    otherwise as on. monitor: (width, height, DPI scale, refresh Hz, VRR min Hz, VRR max Hz)."""
    refresh,vrr_min,vrr_max=(list(monitor[3:6])+[0,0,0])[:3]
    if vsync not in VSYNC_MODES:
        raise ValueError(f"Unknown VSync mode '{vsync}'; use {', '.join(VSYNC_MODES)}.")
    if vsync=='off':
        return {},frame_rate,'VSync off'
    vrr=refresh>3 and vrr_min<vrr_max and vrr_max>=refresh-1
    if vsync=='on' or not vrr:
        return {'present_vsync':'true'},frame_rate,'VSync on'+('' if vsync=='on' else ' (no VRR range)')
    cap=refresh-3
    description=f'VSync auto: VRR {vrr_min}-{vrr_max} Hz, presents at most {refresh} a second'
    if (frame_rate or 60)>cap:
        frame_rate=cap
        description+=f', races at {cap} fps'
    return {'present_min_interval_us':str(1000000//refresh)},frame_rate,description

def info_overlay(cvars,env,look,quality,display=None):
    """FM4_INFO, the lines of the settings overlay (work/fm4-rex/src/fm4-info-overlay.h): what
    the cvars and environment of this run set up. display: the -Display description."""
    scale=int(cvars.get('draw_resolution_scale_x','1'))
    downscale='area' if cvars.get('present_area_downscale','true')=='true' else 'bilinear'
    lines=[(display or f'Render {scale}x ({1280*scale}x{720*scale})')+f', {downscale} downscale']
    tiling=(', single tile'+(' with FXAA' if cvars.get('swap_post_effect')=='fxaa' else '')
            if env.get('FM4_TILING_SCENARIO')=='2' else '')
    quality=quality_preset(quality)
    samples=env.get('FM4_ONE_PASS_MSAA')
    msaa=(f', {samples}x MSAA'+(f' ({QUALITY_MSAA[quality]}x needs a whole scale at 16:9 without '
                                 '-Upscale)' if int(samples)<QUALITY_MSAA[quality] else '')
          if quality in QUALITY_MSAA and samples else '')
    lines.append(f'Quality: {QUALITY_NAMES[quality]}'+
                 (" (the game's own with soft particles)" if quality=='medium' else '')+tiling+msaa)
    knobs={value:name for name,value in LOOK_KNOBS.items()}
    tonemaps={str(float(number)):name for name,number in LOOK_NAMES['tonemap'].items()}
    defines=[part.split('=',1) for part in filter(None,cvars.get('shader_mod_defines','').split(';'))]
    settings=', '.join(f'{knobs.get(name,name)} '+(tonemaps.get(value,value) if name=='FM4_LOOK_TONEMAP'
                                                    else f'{float(value):g}') for name,value in defines)
    if 'shader_mod_path' not in cvars:
        lines.append('Look: original (no shader mods)')
    elif Path(cvars['shader_mod_path']).name=='look':
        lines.append(f'Look: {look}'+(f' ({settings})' if settings else ''))
    else:
        lines.append(f'Shader mods: {cvars["shader_mod_path"]}'+(f' ({settings})' if settings else ''))
    if cvars.get('shader_mod_hot_reload')=='true':
        lines.append(f'Shader dev: hot reload of {cvars["shader_mod_path"]}'+
                     (', live knobs (shader_mod_set in the console)'
                      if cvars.get('shader_mod_live_knobs')=='true' else ''))
    if env.get('FM4_TRACK_SETTINGS'):
        track={name:value for name,value in look_knobs(look).items() if name in LOOK_TRACK_KNOBS}
        lines.append('Track lighting: '+(', '.join(f'{name.replace("_"," ")} x{float(track[name]):g}'
                                                   for name in LOOK_TRACK_KNOBS if name in track)
                                         or env['FM4_TRACK_SETTINGS']))
    shadows=('cockpit only (workaround)' if env.get('FM4_SHADOW_WORKAROUND')=='1'
             else 'shadow map on every camera')
    anisotropy={'5':'16x','4':'8x','3':'4x','2':'2x'}.get(cvars.get('anisotropic_override',''),'game')
    lines.append(f'Shadows: {shadows}; textures: {anisotropy} anisotropic')
    rate=env.get('FM4_FRAME_RATE','60')
    pacing='VRR pacing' if cvars.get('vblank_vrr')=='true' else 'fixed vblank grid'
    mirror='half rate' if env.get('FM4_HALF_RATE_MIRROR')=='1' else 'every frame'
    path=cvars.get('render_target_path_d3d12','rtv').upper()
    if cvars.get('host_render_thread')=='true':
        path+=' (host render thread)'
    lines.append(f'Races: {rate} fps, {pacing}; mirror {mirror}; {path} render targets')
    full=[name for name in ('replay','countdown') if name in env.get('FM4_FULL_RATE','')]
    if full:
        lines.append(f"{' and '.join(full).capitalize()}: {rate} fps (post-race and photo mode 30)")
    if 'post_pass_path' in cvars:
        strength=dict(defines).get('FM4_LOOK_AO')
        applied=f'strength {float(strength):g}' if strength else 'pass only (no look applies it)'
        gtao=passes_settings(cvars['post_pass_path'],dict(defines))
        quality={0:'low',1:'medium',2:'high',3:'ultra'}.get(int(gtao['GTAO_QUALITY']),'high')
        lines.append(f"Ambient occlusion: GTAO, {applied}, radius {gtao['GTAO_RADIUS']:g} m, "
                     f'{quality}')
    if dict(defines).get('FSR')=='1':
        lines.append(f"Anti-aliasing: FSR 3.1 before the post chain (scene_color), sharpen {float(dict(defines)['FSR_SHARPNESS']):g}, scene jittered every frame (TAA there in a runtime without FSR)")
    elif dict(defines).get('TAA_UPSCALE')=='1':
        lines.append(f"Anti-aliasing: TAA as the upscaler before the post chain (scene_color), sharpen {float(dict(defines)['TAA_SHARPEN']):g}, scene jittered every frame")
    elif dict(defines).get('TAA')=='1' and dict(defines).get('TAA_POINT')=='1':
        lines.append("Anti-aliasing: TAA before the post chain (scene_color), the look's sharpening, "
                     'scene jittered while the camera moves, a still one accumulated')
    elif dict(defines).get('TAA')=='1':
        lines.append(f"Anti-aliasing: TAA before the HUD, sharpen {float(dict(defines)['TAA_SHARPEN']):g}, "
                     'scene jittered while the camera moves, a still one accumulated')
    if 'WINDSHIELD' in dict(defines):
        lines.append(f"Cockpit glass: strength {float(dict(defines)['WINDSHIELD']):g}")
    if 'rtv_occlusion_awaited_scale' in cvars:
        strength=float(cvars['rtv_occlusion_awaited_scale'])
        lines.append(f'Sun glare: {strength:g}x per pixel, soft limit {cvars["rtv_occlusion_awaited_limit"]}'
                     ' pixels'+(' (RTV only)' if path=='ROV' else ''))
    if cvars.get('shader_override_ab_ms','0') not in ('','0'):
        lines.append(f'Shader A/B: game shaders every other {cvars["shader_override_ab_ms"]} ms')
    return '|'.join(lines)

# Screen-space ambient occlusion of the scene depth: the GTAO post pass of work/fm4-rex/passes
# (docs/analysis/gtao-design.md), whose passes.toml names the vertex shaders that carry the
# scene camera and holds the radius picked with the user from a 2x variant grid. The look knob
# ao applies it in the final combine shaders (xe_post_ao); --ao alone only runs the pass.
def ao_cvars(root,enabled,shader_dev=False,stage=None):
    """Cvars of the post passes: copies work/fm4-rex/passes to BuildRoot/passes (or
    <stage>/passes, as look_cvars), or with shader_dev uses it in place (the look's hot reload
    then reloads saved pass files too)."""
    if not enabled:
        return {}
    if shader_dev:
        return {'post_pass_path':PASSES_DIRECTORY}
    directory=(stage or root)/'passes'
    if directory.is_dir():
        shutil.rmtree(directory)
    directory.mkdir()
    for path in PASSES_DIRECTORY.iterdir():
        if path.suffix in ('.toml','.hlsl','.hlsli'):
            shutil.copyfile(path,directory/path.name)
    return {'post_pass_path':directory}

def passes_settings(directory,defines):
    """GTAO_RADIUS and GTAO_QUALITY of the passes.toml in directory, after defines (as
    shader_mod_defines overrides them in the runtime), for the settings overlay."""
    import tomllib
    manifest=tomllib.loads((Path(directory)/'passes.toml').read_text(encoding='utf-8'))
    radius=manifest.get('params',{}).get('GTAO_RADIUS',1.0)
    radius=radius.get('value',1.0) if isinstance(radius,dict) else radius
    quality=manifest.get('defines',{}).get('GTAO_QUALITY',2)
    return {'GTAO_RADIUS':float(defines.get('GTAO_RADIUS',radius)),
            'GTAO_QUALITY':float(defines.get('GTAO_QUALITY',quality))}

def save_content(root,content):
    """run -Content: replaces the saved content paths without generating again."""
    state=json.loads((root/'local.json').read_text())
    validate_content(state['disc2'],content)
    state['content']=[str(Path(path).resolve()) for path in content]
    (root/'local.json').write_text(json.dumps(state,indent=2),encoding='utf-8')

def single_tile_settings(look):
    """One tile (-SingleTile, auto_single_tile): races render 720p in one tile without MSAA (the
    title's UI tiling scenario), without predicated tiling, and FXAA on the swap unless the look
    has TAA, which anti-aliases the tile itself (FXAA after it would blur the HUD and fight it).
    Environment and cvars."""
    return ({'FM4_TILING_SCENARIO':'2'},
            {} if taa_enabled(look_knobs(look)) else {'swap_post_effect':'fxaa'})

def auto_single_tile(look,rov=False,msaa=False):
    """Races render one tile without MSAA whenever the look has TAA (every preset, since 21
    October 2026), at every -Quality: TAA anti-aliases the tile, and its input is then one sample
    per pixel, with depth, motion and mask of the same sample as the color (docs/analysis/taa.md,
    section 5). -Rov, which runs no post passes, and -Msaa keep the tiles and MSAA of -Quality,
    and so does a look without TAA."""
    return taa_enabled(look_knobs(look)) and not rov and not msaa

def native_cvars(single_tile,rov=False):
    """Cvars of -Native: stage N1 of the native path, the host render thread fed by a record
    queue, with the converted PM4 writers leaving records (docs/analysis/d3d-hle.md, section
    14), the resolve readbacks on demand and the resolves from the host render targets of
    stage N3 (docs/analysis/d3d-hle-n3.md, sections 16.5 and 16.11), and stage N4's draw back
    end on the replay of the deferred command list, the texture bindings kept for rewritten
    fetch constants and the render target cache's update skipped where it would repeat
    (docs/analysis/d3d-hle-n4.md, sections 17.8 to 17.13). It covers races in one tile or in one
    pass with MSAA (16.17); the tiles of -Quality (-Msaa) stay on the PM4 RTV path, and -Rov,
    the uncorrected reference, is never combined with it."""
    if rov:
        raise ValueError('-Native runs on the host render target path (RTV); it cannot be combined with -Rov.')
    if not single_tile:
        print('-Native: races keep the tiles of -Quality here, so the PM4 RTV path runs instead '
              '(the native path covers one tile and one pass).')
        return {}
    return {'host_render_thread':'true','host_render_native':'true',
            'readback_resolve_on_demand':'true','native_rt_surfaces':'2',
            'd3d12_draw_back_end':'2','texture_bindings_keep_same_dwords':'true',
            'render_target_update_guard':'true'}

def launch(root,single_tile=False,fixed_vblank=False,frame_rate=None,rov=False,
           resolution_scale=None,full_rate_mirror=False,look=None,shadow_workaround=False,
           quality=None,display=DEFAULT_DISPLAY,fullscreen=False,supersample=False,info=False,
           ao=False,shader_dev=False,texture_dump=None,texture_replace=None,msaa=False,
           native=None,msaa_samples=None,vsync=DEFAULT_VSYNC,pad=None,upscale=DEFAULT_UPSCALE,
           keyboard=None,entry0=None,wheel=None):
    state = json.loads((root/'local.json').read_text())
    disc1,disc2 = validate_discs(state['disc1'],state['disc2'])
    report=json.loads((root/'build-result.json').read_text())
    for name,expected in report['artifacts'].items():
        if digest(root/'game-build'/name)!=expected:
            raise RuntimeError(f'Build artifact changed: {name}. Rebuild first.')
    # Disc 2 and the -Content packages: imported into user-data once, then kept there.
    packages=validate_content(disc2,state.get('content'))
    manifest=write_content_manifest(root,packages) if packages else None
    if packages:
        print(f'Content packages: {len(packages)}')
    cvars,plan_env,description=launch_plan(
        root,disc1,single_tile=single_tile,fixed_vblank=fixed_vblank,frame_rate=frame_rate,rov=rov,
        resolution_scale=resolution_scale,full_rate_mirror=full_rate_mirror,look=look,
        shadow_workaround=shadow_workaround,quality=quality,display=display,fullscreen=fullscreen,
        supersample=supersample,info=info,ao=ao,shader_dev=shader_dev,texture_dump=texture_dump,
        texture_replace=texture_replace,msaa=msaa,content_manifest=manifest,native=native,
        msaa_samples=msaa_samples,vsync=vsync,pad=pad,upscale=upscale,keyboard=keyboard,
        entry0=entry0,wheel=wheel)
    print(description)
    if texture_dump:
        print(f'Texture dump: {cvars["texture_dump_path"]}')
    if texture_replace:
        print(f'Texture replacements: {cvars["texture_replace_path"]}')
    env={k:v for k,v in os.environ.items() if not k.startswith('FM4_')}
    env.update(plan_env)
    args=[root/'game-build/fm4_rex.exe']+[f'--{k}={v}' for k,v in cvars.items()]
    run(args,cwd=root,env=env,log=root/'logs/play.log')

def launch_plan(root,disc1,single_tile=False,fixed_vblank=False,frame_rate=None,rov=False,
                resolution_scale=None,full_rate_mirror=False,look=None,shadow_workaround=False,
                quality=None,display=DEFAULT_DISPLAY,fullscreen=False,supersample=False,info=False,
                ao=False,shader_dev=False,texture_dump=None,texture_replace=None,msaa=False,
                content_manifest=None,monitor=None,native=None,msaa_samples=None,
                vsync=DEFAULT_VSYNC,pad=None,upscale=DEFAULT_UPSCALE,keyboard=None,entry0=None,
                wheel=None):
    """The cvars, the FM4_ environment and the display description of a run of the options of
    launch. The GUI launcher (work/fm4-launcher, plan.cpp) ports this function; launcher_tests
    compares the two. monitor: the primary monitor's (width, height, DPI scale, refresh Hz, VRR
    min Hz, VRR max Hz, integrated GPUs only) instead of the real one. native: True for -Native,
    False for -Rtv, None for the default: the native path unless -Rov or -Msaa (with integrated
    GPUs only since 24 October 2026, d3d-hle-n4.md 17.25; on every computer by the main
    session's decision of 26 October 2026, docs/analysis/native-default.md). pad:
    the -Pad value (PAD_KNOBS). upscale: the -Upscale mode (upscale_settings). keyboard: the
    -Keyboard value (KEYBOARD_KNOBS). wheel: the -Wheel value (WHEEL_KNOBS)."""
    monitor=monitor or primary_monitor()
    if native is None:
        native=not rov and not msaa
    env={}
    env.update(launch_env())
    if full_rate_mirror:
        env['FM4_HALF_RATE_MIRROR']='0'
    if shadow_workaround:
        # Skips the shadow map pass outside the cockpit in races, as split screen does: the
        # xenia-canary/game-patches#398 workaround, no longer needed on this runtime.
        env['FM4_SHADOW_WORKAROUND']='1'
    look=DEFAULT_LOOK if look is None else look
    # The quality tier (an alias resolved), and ultra's 4x MSAA where it keeps the picture.
    quality=quality_preset(quality)
    msaa_samples,quality_note=quality_msaa_samples(quality,native,msaa_samples,single_tile,msaa,rov,
                                                   display,fullscreen,supersample,resolution_scale,
                                                   monitor,upscale)
    # One pass with 2x MSAA on the native path (d3d-hle.md 16.17): the title's 2x scenario made
    # one 1280x720 tile, and the runtime's private scene depth. -Native runs it unless
    # -MsaaSamples 4 (one pass with 4x), 0 (one tile without MSAA), -SingleTile or -Msaa says
    # otherwise.
    if msaa_samples is not None and rov:
        raise ValueError("-MsaaSamples is the native path's one-pass MSAA; -Rov cannot run it.")
    if msaa_samples is not None and not native:
        raise ValueError("-MsaaSamples is the native path's one-pass MSAA; -Rtv and -Msaa run RTV.")
    if native and msaa_samples is None and not single_tile and not msaa and not rov:
        msaa_samples=2
    one_pass=bool(msaa_samples)
    single_tile=not one_pass and (single_tile or msaa_samples==0 or auto_single_tile(look,rov,msaa))
    settings=race_settings(quality,single_tile or one_pass)
    if settings:
        env['FM4_RACE_SETTINGS']=settings
    cvars=launch_cvars(root,disc1,root/'logs/runtime.log')
    if single_tile:
        tiling_env,tiling_cvars=single_tile_settings(look)
        env.update(tiling_env)
        cvars.update(tiling_cvars)
    if one_pass:
        # 2x: the title's 2x scenario (1); 4x: scenario 3, Small2xMSAA, which the title patch makes
        # one 4x tile (entry 0's 4x also serves the user interface; d3d-hle-n3.md 16.17).
        env.update({'FM4_TILING_SCENARIO':'1' if msaa_samples==2 else '3',
                    'FM4_ONE_PASS_MSAA':str(msaa_samples)})
    if fixed_vblank:
        cvars['vblank_vrr']='false'
    if rov:
        # The earlier pixel shader interlock path: GPU about 7 ms a race frame
        # instead of 2.5, for comparisons when the host render target path misbehaves.
        cvars.pop('edram_single_rt_no_wrap')
        # The uncorrected reference keeps the title's ring index wait and frame loop as they are.
        cvars.update({'render_target_path_d3d12':'rov','rov_occlusion_async':'all',
                      'guest_ring_wait_sleep':'false','fm4_render_thread_idle_wait':'false'})
    if native:
        cvars.update(native_cvars(single_tile or one_pass,rov))
        if one_pass:
            # Bit 8: the host render targets sized to the tiles the title uses (16.15); bit 16:
            # every 1280-wide race surface private (d3d-hle-n5.md 18.11).
            cvars.update({'native_one_pass_msaa':str(msaa_samples),'native_rt_surfaces':'26'})
    screen,description=display_settings(display,fullscreen,supersample,resolution_scale,monitor,
                                        scene_scale=bool(native and msaa_samples==2),
                                        upscale=upscale_ratio(upscale)>1.0)
    cvars.update(screen)
    upscaled,upscale_description=upscale_settings(upscale,cvars,aa_method(look_knobs(look)),
                                                  bool(native and msaa_samples==2))
    cvars.update(upscaled)
    description+=upscale_description+quality_note
    if (native and msaa_samples==2 and 'native_draw_scale' not in cvars and
            int(cvars.get('draw_resolution_scale_x','1'))>1):
        # A whole scale of 2 or more at 16:9 through the scene draws' path too, with bit 32: the
        # resolves write the scene's textures from the render target, 0.15 ms less GPU a race
        # frame at 2x on a discrete GPU; 1x keeps its loads (d3d-hle-n6.md 19.7.6).
        cvars.update({'native_draw_scale':cvars['draw_resolution_scale_x'],
                      'native_draw_scale_variant':'true','native_rt_surfaces':'58'})
    if native and msaa_samples==2 and entry0_scale(entry0)=='s':
        # The title's tiling entry 0 (the boot, the menus, photo mode) at the scene's scale: one
        # pass with 4x MSAA in a private 4x pair, photo mode in its three tiles there
        # (d3d-hle-n5.md 18.26). 'k' keeps it at the whole scale, scaled to the screen.
        env['FM4_ONE_PASS_ENTRY0']='4'
        cvars['native_rt_surfaces']=str(int(cvars['native_rt_surfaces'])|64)
    present,frame_rate,present_description=present_settings(vsync,frame_rate,monitor)
    cvars.update(present)
    description+=f'; {present_description}'
    if frame_rate:
        # The display refresh the title sees and the FrameRate of its race scenarios
        # (60). The guest vblank runs at 60 Hz, and at this rate only while a race
        # scenario sets the frame rate (fm4-vblank-rate.cpp): menus, countdown,
        # post-race, replays and photo mode keep their 60 Hz timing.
        # The title scales its time step, so the race clock stays real time.
        cvars['video_mode_refresh_rate']=str(frame_rate)
        env['FM4_FRAME_RATE']=str(frame_rate)
    # Shader mods on the game's post-processing (tone curve, sharpening, bloom, exposure,
    # contrast, saturation, vignette, motion blur): the aces look unless -Look says otherwise.
    cvars.update(look_cvars(root,look,shader_dev,scene_upscale_ratio(cvars)))
    track_settings=look_track_settings(look_knobs(look))
    if track_settings:
        env['FM4_TRACK_SETTINGS']=track_settings
    cvars.update(ao_cvars(root,ao,shader_dev))
    cvars.update(quality_cvars(quality,cvars))
    # Texture dumps and replacements by content hash (docs/analysis/texture-replacement.md).
    if texture_dump:
        cvars['texture_dump_path']=Path(texture_dump).resolve()
    if texture_replace:
        if not Path(texture_replace).is_dir():
            raise ValueError(f'-TextureReplace is not a directory: {texture_replace}')
        cvars['texture_replace_path']=Path(texture_replace).resolve()
    if content_manifest:
        env['FM4_CONTENT_MANIFEST']=str(content_manifest)
    # The physical controller's options, the keyboard's and the racing wheel's.
    cvars.update(pad_cvars(pad))
    cvars.update(keyboard_cvars(keyboard))
    cvars.update(wheel_cvars(wheel))
    # The settings overlay: F2 in game, shown at start with -Info.
    env['FM4_INFO']=info_overlay(cvars,env,look,quality,description)
    if info:
        env['FM4_INFO_SHOW']='1'
    return cvars,env,description

# The GUI launcher (work/fm4-launcher, docs/running.md#gui-launcher): MotorsportDX.exe beside
# fm4_rex.exe, and launcher.json, the launcher's tables, beside game-build. The tables come from
# this file, so the launcher has no rules of its own beyond launch_plan's. The program was
# fm4_launcher.exe until 26 October 2026; build and export remove that old file, so a shortcut
# to it fails instead of starting a launcher older than its game (docs/running.md).
LAUNCHER_DIRECTORY=REPO/'work/fm4-launcher'
LAUNCHER_EXE='MotorsportDX.exe'
OLD_LAUNCHER_EXE='fm4_launcher.exe'
LAUNCHER_ROOT,LAUNCHER_DISC1='<ROOT>','<DISC1>'

def launcher_data():
    """launcher.json: the tables of launch_plan, with the placeholders <ROOT> (the BuildRoot or
    export folder) and <DISC1> in the cvars that hold paths."""
    root=Path(LAUNCHER_ROOT)
    cvars=launch_cvars(root,LAUNCHER_DISC1,root/'logs/runtime.log')
    defaults={knob:value for knob,define in LOOK_KNOBS.items()
              for name,value in look_knob_defaults().items() if name==define}
    return {'cvars':{name:str(value) for name,value in cvars.items()},'env':launch_env(),
            'native_cvars':native_cvars(True),
            'look':{'knobs':LOOK_KNOBS,'runtime_knobs':list(LOOK_RUNTIME_KNOBS),
                    'pass_knobs':list(LOOK_PASS_KNOBS),'track_knobs':LOOK_TRACK_KNOBS,
                    'names':LOOK_NAMES,'presets':LOOK_PRESETS,'aliases':LOOK_ALIASES,
                    'default':DEFAULT_LOOK,'defaults':defaults},
            'sun_limit':SUN_LIMIT,
            'quality':{'presets':QUALITY_PRESETS,'aliases':QUALITY_ALIASES,'names':QUALITY_NAMES,
                       'gtao':QUALITY_GTAO,'msaa':QUALITY_MSAA,'default':DEFAULT_QUALITY},
            'displays':{name:list(size) for name,size in DISPLAYS.items()},
            'default_display':DEFAULT_DISPLAY,'max_resolution_scale':MAX_RESOLUTION_SCALE,
            'vsync':{'modes':list(VSYNC_MODES),'default':DEFAULT_VSYNC},
            'upscale':{'modes':UPSCALE_MODES,'default':DEFAULT_UPSCALE},
            'pad':{'knobs':PAD_KNOBS},'keyboard':{'knobs':KEYBOARD_KNOBS},
            'wheel':{'knobs':WHEEL_KNOBS},
            'gtao':passes_settings(PASSES_DIRECTORY,{}),
            'releases':{name:{'media_id':info['media_id'],'executables':info['executables']}
                        for name,info in RELEASES.items()},
            'content':{'title_id':TITLE_ID,'content_type':MARKETPLACE_CONTENT,
                       'magics':[magic.decode() for magic in STFS_MAGICS],
                       'content_type_offset':CONTENT_TYPE_OFFSET,'title_id_offset':TITLE_ID_OFFSET}}

def write_launcher_data(directory):
    """directory/launcher.json (launcher_data): pure project data, so the launcher-data action
    writes it without a build, for the builder package (docs/analysis/builder-installer.md)."""
    path=Path(directory)/'launcher.json'
    path.write_text(json.dumps(launcher_data(),indent=1),encoding='utf-8')
    return path

def look_files(directory):
    """The files of a look directory that a run reads: those of LOOK_SUFFIXES, and the PNG
    textures that the entries of its shaders.toml read (srv = ["file:<name>"], <name>.png), so
    other images in the folder stay out of runs and exports."""
    import tomllib
    directory=Path(directory)
    files=[path for path in sorted(directory.iterdir()) if path.suffix in LOOK_SUFFIXES]
    manifest=directory/'shaders.toml'
    if manifest.is_file():
        names=set()
        for entry in tomllib.loads(manifest.read_text(encoding='utf-8')).get('pixel',[]):
            resources=entry.get('srv',[])
            for resource in [resources] if isinstance(resources,str) else resources:
                if isinstance(resource,str) and resource.startswith('file:'):
                    names.add(resource[len('file:'):])
        files+=[directory/f'{name}.png' for name in sorted(names)
                if (directory/f'{name}.png').is_file()]
    return files

def stage_launcher_files(directory):
    """launcher.json, and the look mods and post passes that launch copies before each run, into
    directory (the BuildRoot or an export folder): the launcher runs them from there. Also the
    credits and the licenses of the third-party code in the binaries, which the launcher's About
    page opens, and the project's own license."""
    directory=Path(directory)
    write_launcher_data(directory)
    shutil.copyfile(CREDITS,directory/'CREDITS.txt')
    shutil.copyfile(LICENSE,directory/'LICENSE.txt')
    shutil.copytree(NOTICES,directory/'third-party-notices',dirs_exist_ok=True)
    for name,files in (('look',look_files(LOOK_DIRECTORY)),
                       ('passes',[path for path in PASSES_DIRECTORY.iterdir()
                                  if path.suffix in ('.toml','.hlsl','.hlsli')])):
        shutil.rmtree(directory/name,ignore_errors=True)
        (directory/name).mkdir()
        for path in files:
            shutil.copyfile(path,directory/name/path.name)

def launcher_build_directory(root):
    """root/launcher-build, unless its CMake cache belongs to another checkout of the repository
    (the main checkout and a worktree build into the same BuildRoot): then
    root/launcher-build-<this checkout's folder name>, so neither reconfigures the other's."""
    build=root/'launcher-build'
    cache=build/'CMakeCache.txt'
    if cache.exists():
        match=re.search(r'^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$',
                        cache.read_text(encoding='utf-8',errors='replace'),re.M)
        if match and Path(match.group(1).strip()).resolve()!=LAUNCHER_DIRECTORY.resolve():
            return root/f'launcher-build-{REPO.name}'
    return build

def build_launcher(root,jobs):
    """MotorsportDX.exe (work/fm4-launcher) against the installed runtime's SDL3 and the runtime
    source's Dear ImGui and JSON header, into game-build; its files into the BuildRoot."""
    build=launcher_build_directory(root)
    run(['cmake','-S',LAUNCHER_DIRECTORY,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
         *compilers('CXX','C'),
         '-DCMAKE_CXX_FLAGS=-march=x86-64-v2','-DCMAKE_C_FLAGS=-march=x86-64-v2',
         f'-DCMAKE_PREFIX_PATH={root/"runtime-install"}',
         f'-DFM4_THIRDPARTY={root/"runtime-source/thirdparty"}'],log=root/'logs/launcher-configure.log')
    run(['cmake','--build',build,'--parallel',jobs],log=root/'logs/launcher-build.log')
    shutil.copyfile(build/LAUNCHER_EXE,root/'game-build'/LAUNCHER_EXE)
    (root/'game-build'/OLD_LAUNCHER_EXE).unlink(missing_ok=True)
    stage_launcher_files(root)

# Options of the launcher tests: each with each monitor (width, height, DPI scale, refresh Hz,
# VRR min Hz, VRR max Hz, integrated GPUs only; the main PC's panel, a fixed 60 Hz screen, a
# 144 Hz VRR screen, a fixed 1280x800 screen, the ROG Xbox Ally X's panel and GPU, a 1440p
# screen, a 2880x1800 one, a 1920x1200 one and a 21:9 3440x1440 one).
LAUNCHER_TEST_MONITORS=((2560,1600,1.5,240,60,240,False),(1920,1080,1.0,60,0,0,False),
                        (3840,2160,2.0,144,48,144,False),(1280,800,1.25,60,0,0,False),
                        (1920,1080,1.5,120,48,120,True),(2560,1440,1.0,165,48,165,False),
                        (2880,1800,2.0,120,48,120,False),(1920,1200,1.0,60,0,0,False),
                        (3440,1440,1.0,144,48,144,False))
def launcher_test_options(root):
    return [{},
            {'look':'gt','quality':'ultra','frame_rate':120,'display':'1080p'},
            {'look':'original,taa=0','single_tile':True},
            {'look':'original','msaa':True,'display':'720p','fullscreen':True},
            {'rov':True,'fixed_vblank':True,'full_rate_mirror':True,'shadow_workaround':True,'info':True},
            {'rov':True},
            {'native':True},
            {'native':True,'msaa_samples':0},
            {'native':True,'msaa_samples':4},
            {'native':True,'single_tile':True,'look':'original,taa=0'},
            {'native':True,'msaa':True},
            {'native':True,'look':'gt','frame_rate':240,'fixed_vblank':True},
            {'native':True,'look':'gt7','display':'1440p'},
            {'native':True,'look':'fm2023,haze=0.8,temperature=-0.2'},
            {'look':'aces,haze=0.3,ao=0','info':True},
            {'native':True,'look':'pc,aa=fsr,sharpen=0.6','info':True},
            {'frame_rate':120,'vsync':'on'},
            {'pad':'driver=rawinput,trigger_rumble=1,rumble=1.5'},
            {'pad':'driver=gameinput,trigger_rumble=1','frame_rate':120},
            {'pad':'driver=wgi,thread=1,stick_deadzone=0.08,anti_deadzone=1,stick_curve=1.4,'
                   'stick_outer=0.95,trigger_deadzone=0.05,trigger_outer=0.9,'
                   'trigger_rumble_strength=2,misc1=back,right_paddle1=none,touchpad=start'},
            {'pad':'driver=default,rumble=1.0,misc1=guide,thread=0','frame_rate':240},
            {'keyboard':'smooth=0'},
            {'keyboard':'steer_attack=0.5,steer_release=0.25,throttle_attack=0,brake_release=0.3,'
                        'precision=0.6,title_deadzones=0','frame_rate':240},
            {'keyboard':'mouse_steering=1,mouse_steer_sensitivity=2.5,mouse_steer_centering=0,'
                        'smooth=1,steer_attack=0.3','pad':'rumble=1.5'},
            {'wheel':'on=1'},
            {'wheel':'on=1,steer_axis=0,steer_range=1080,steer_lock=540,steer_invert=1,'
                     'throttle_axis=2,throttle_rest=32767,throttle_pressed=-31000,'
                     'throttle_deadzone=0.03,throttle_gamma=1.5,brake_axis=-1,clutch_axis=3,'
                     'shift_up=4,shift_down=5,clutch_button=-1,handbrake_button=12,ffb_cap=0.4,'
                     'ffb_gain=1.25,ffb_invert=1,ffb_min_force=0.05,ffb_smoothing=0,'
                     'ffb_spring=0,ffb_damper=0.5,ffb_road=0.4,ffb_impact=0,ffb_physics=0,'
                     'car_lock=720,end_stop=0.5',
             'frame_rate':240},
            {'wheel':'on=0,input=0,brake_axis=2,ffb_cap=0.25','pad':'rumble=1.5'},
            {'native':True,'frame_rate':240,'vsync':'off'},
            {'native':False},
            {'msaa':True},
            {'look':'aces,foliage_hue=-60,sky=1.2,road_gloss=0.5,sun=0,tonemap=filmic,taa_point=1',
             'supersample':True},
            {'look':'neutral,windshield=2,ao=0','resolution_scale':3,'display':'4k'},
            # texture_replace must name a directory that exists: game-build holds the launcher
            # the tests run, while cache appears only with the first run of the game.
            {'look':'original,taa=0,sharpen=0.5','ao':True,'texture_dump':str(root/'logs'),
             'texture_replace':str(root/'game-build')},
            {'look':'pc,clearcoat=0,sky=1,exposure=1e-05','quality':'xbox','display':'1440p'},
            {'look':'original,taa=0,sun=2.5','msaa':True,'frame_rate':240,'supersample':True},
            {'look':'aces,adaptation=0','frame_rate':240},
            {'native':True,'resolution_scale':'panel'},
            {'native':True,'supersample':True},
            {'native':True,'display':'1080p'},
            {'native':True,'resolution_scale':2,'display':'1440p'},
            {'resolution_scale':'panel','msaa_samples':2,'native':True,'display':'720p'},
            {'native':True,'look':'pc,aa=fsr','upscale':'quality','display':'4k'},
            {'native':True,'look':'pc,aa=fsr','upscale':'balanced','display':'1440p'},
            {'native':True,'look':'pc,aa=fsr','upscale':'performance'},
            {'native':True,'look':'pc,aa=fsr','upscale':'1.25','display':'1080p'},
            {'native':True,'look':'pc,aa=fsr,fsr_sharpen=0.5','upscale':'quality','display':'1080p'},
            {'native':True,'look':'pc,aa=taau','upscale':'quality','display':'1080p','info':True},
            {'native':True,'look':'pc,aa=taau','upscale':'1.25','display':'1080p'},
            {'native':True,'look':'pc,aa=taau'},
            {'native':True,'entry0':'k','display':'1080p'},
            {'native':True,'look':'pc,aa=fsr','upscale':'quality','display':'1440p','entry0':'s'},
            # The quality tiers: their race settings and GTAO quality, ultra's 4x MSAA where the
            # native plan keeps the picture (whole scales at 16:9) and 2x elsewhere.
            {'native':True,'quality':'low','info':True},
            {'native':True,'quality':'medium'},
            {'native':True,'quality':'extrahigh','display':'1440p'},
            {'native':True,'quality':'ultra','info':True},
            {'native':True,'quality':'ultra','display':'4k'},
            {'native':True,'quality':'ultra','supersample':True},
            {'native':True,'quality':'ultra','msaa_samples':2},
            {'native':True,'quality':'ultra','look':'pc,aa=taau','upscale':'quality','display':'1440p'},
            {'quality':'ultra','look':'original,taa=0','single_tile':True},
            {'rov':True,'quality':'low'},
            {'quality':'low','look':'original,taa=0'}]

def launcher_tests(root):
    """Compares MotorsportDX.exe's port of launch_plan, disc check and content search with this
    file's: the plans of launcher_test_options on each test monitor, and the discs and content
    of local.json."""
    cases=[]
    disc1=r'C:\FM4 test\Disc 1'
    manifest=root/'content-packages.txt'
    for options in launcher_test_options(root):
        for monitor in LAUNCHER_TEST_MONITORS:
            cvars,env,description=launch_plan(root,disc1,content_manifest=manifest,monitor=monitor,
                                              **options)
            cases.append({'options':options,'monitor':list(monitor),
                          'expected':{'cvars':{k:str(v) for k,v in cvars.items()},'env':env,
                                      'description':description}})
    state=json.loads((root/'local.json').read_text())
    discs={'disc1':state['disc1'],'disc2':state['disc2'],'content':state.get('content') or [],
           'release':disc_release(Path(state['disc1'])),
           'packages':[str(path) for path in validate_content(Path(state['disc2']) if state['disc2'] else None,
                                                              state.get('content'))]}
    work=root/'launcher-build/tests'
    work.mkdir(parents=True,exist_ok=True)
    # A synthetic disc image for the port of disc_image_entries and unpack_disc_image.
    files={'default.xex':b'synthetic executable','Media/Cars/a.bin':bytes(range(256))*20,
           'Media/Cars/b.bin':b'','Media/Tracks/c.bin':b'track'*1000,'Media/Empty':None,
           'Database.xmplr':b'database','content/0000000000000000/x':b'package'}
    write_disc_image(work/'disc-image.iso',files)
    image={'path':str(work/'disc-image.iso'),'out':str(work/'disc-image'),'other':str(work/'cases.json'),
           'entries':[[path,offset,size] for path,offset,size in disc_image_entries(work/'disc-image.iso')],
           'files':{path:hashlib.sha256(data).hexdigest() for path,data in files.items() if data is not None}}
    # Synthetic Disc images for the launcher's UnpackDisc: a Disc 2 with four packages, and a
    # Disc 1 whose executables are of no release.
    header=bytearray(TITLE_ID_OFFSET+4)
    header[:4]=b'LIVE'
    header[CONTENT_TYPE_OFFSET:CONTENT_TYPE_OFFSET+4]=MARKETPLACE_CONTENT.to_bytes(4,'big')
    header[TITLE_ID_OFFSET:TITLE_ID_OFFSET+4]=TITLE_ID.to_bytes(4,'big')
    write_disc_image(work/'disc2-image.iso',{**{package.as_posix():bytes(header) for package in disc2_packages(Path())},
                                             'default.xex':b'installer'})
    write_disc_image(work/'disc1-image.iso',{**{name:b'no release' for name in EXECUTABLES},'Database.xmplr':b''})
    image['disc2']=str(work/'disc2-image.iso')
    image['disc1']=str(work/'disc1-image.iso')
    image['discs']=str(work/'discs')
    for path in (work/'disc-image',work/'discs'):
        if path.exists(): shutil.rmtree(path)
    (work/'cases.json').write_text(json.dumps({'root':str(root),'disc1':disc1,
                                               'manifest':str(manifest),'plans':cases,
                                               'discs':discs,'image':image},indent=1),encoding='utf-8')
    (work/'result.json').unlink(missing_ok=True)
    run([root/'game-build'/LAUNCHER_EXE,'--test',work/'cases.json',work/'result.json'],
        cwd=root,log=root/'logs/launcher-tests.log')
    result=json.loads((work/'result.json').read_text(encoding='utf-8'))
    if result.get('failures'):
        raise RuntimeError('Launcher tests failed:\n'+'\n'.join(result['failures'][:20]))
    print(f'Launcher tests passed: {result["plans"]} plans, discs, {len(discs["packages"])} content packages '
          'and a disc image.')

def export(root,out,jobs):
    """A folder that runs the game with MotorsportDX.exe alone (docs/running.md#export-to-another-computer): the
    launcher, game-build's executable, modules and runtime, the look mods, the post passes, the
    shader cache, launcher.json, CREDITS.txt, the third-party notices, and the launcher's
    backdrop when the BuildRoot has one and the folder does not. Not user-data: the
    profile, saves and imported content stay, and the launcher of the folder imports Disc 2 and
    the content packages on its first run.
    The Disc and content paths of local.json become the launcher's first settings."""
    report=json.loads((root/'build-result.json').read_text())
    game=root/'game-build'
    for name,expected in report['artifacts'].items():
        if digest(game/name)!=expected:
            raise RuntimeError(f'Build artifact changed: {name}. Rebuild first.')
    if not (game/LAUNCHER_EXE).is_file():
        raise RuntimeError(f'Run build first: game-build has no {LAUNCHER_EXE}.')
    out=Path(out).resolve()
    if out==root or root in out.parents or out in root.parents or out==REPO or REPO in out.parents \
            or out in REPO.parents:
        raise ValueError('The export folder must be outside the BuildRoot and the repository.')
    if out.exists() and any(out.iterdir()) and not (out/'launcher.json').is_file():
        raise ValueError(f'{out} is not empty and is not an earlier export.')
    (out/'game-build').mkdir(parents=True,exist_ok=True)
    for name in report['artifacts']:
        shutil.copyfile(game/name,out/'game-build'/name)
    shutil.copyfile(game/LAUNCHER_EXE,out/LAUNCHER_EXE)
    if (out/OLD_LAUNCHER_EXE).is_file():
        (out/OLD_LAUNCHER_EXE).unlink()
        print(f'Removed the old {OLD_LAUNCHER_EXE} of this export: start {LAUNCHER_EXE} instead '
              '(its settings stay).')
    shutil.copyfile(root/'build-result.json',out/'build-result.json')
    # With CREDITS.txt and third-party-notices, the licenses of the third-party code in the binaries.
    stage_launcher_files(out)
    # The launcher's backdrop is a local picture, not part of the source (docs/running.md#gui-launcher):
    # copied when the BuildRoot has one, never over the export's own.
    for name in ('launcher-backdrop.jpg','launcher-backdrop.png'):
        if (root/name).is_file() and not any((out/other).is_file()
                                             for other in ('launcher-backdrop.jpg','launcher-backdrop.png')):
            shutil.copyfile(root/name,out/name)
    if (root/'cache').is_dir():
        shutil.copytree(root/'cache',out/'cache',dirs_exist_ok=True)
    settings=out/'launcher-settings.json'
    if not settings.is_file():
        state=json.loads((root/'local.json').read_text())
        settings.write_text(json.dumps({'disc1':state['disc1'],'disc2':state['disc2'] or '',
                                        'content':state.get('content') or []},indent=1),
                            encoding='utf-8')
    print(f'Exported to {out}. Run {LAUNCHER_EXE} there; it imports Disc 2 and the content '
          'packages on its first run.')

def warm_cache(root):
    """Fills BuildRoot/cache with the shaders and pipelines of the menus and a Quick Race by
    driving the e2e fixed timeline (no network). Later runs then avoid compiling them."""
    out=root/'e2e/warmcache'
    run([sys.executable,HERE/'e2e/e2e.py','--build-root',root,'--out',out,'--scenario','quickrace',
         '--tail','30'],log=root/'logs/warmcache.log')
    print(f'Shader cache warmed; run details in {out}.')

def launch_env():
    """Launcher environment, shared with e2e/e2e.py: the rear-view mirror rendered every other
    frame in every race (the title does that for 12 and 16 cars), and replays and the 3-2-1
    countdown at the race frame rate instead of 30 (FM4_FULL_RATE, fm4-render-overrides.cpp and
    fm4-vblank-rate.cpp; post-race and photo mode keep 30)."""
    return {'FM4_HALF_RATE_MIRROR':'1','FM4_FULL_RATE':'replay,countdown'}

def launch_cvars(root,disc1,log_file):
    """Launcher cvars, shared with e2e/e2e.py. New performance defaults go here once they pass e2e."""
    return {'game_data_root':disc1,'user_data_root':root/'user-data','cache_root':root/'cache',
            'allow_game_relative_writes':'false','headless':'false','fullscreen':'false',
            'render_target_path_d3d12':'rtv','edram_single_rt_no_wrap':'true','readback_resolve':'fast',
            'gpu_allow_invalid_fetch_constants':'true','async_shader_compilation':'false',
            # The compiled look mods (DXBC), reused while their source, defines and includes
            # stay the same, so a run with a look seen before skips FXC.
            'shader_override_cache_path':root/'cache/shader-overrides',
            'rov_occlusion_async':'previous','vblank_vrr':'true',
            # The title's ring index wait sleeps until the read pointer is published again
            # instead of spinning on a core (docs/analysis/d3d-hle-n4.md 17.16); -Rov turns it off.
            'guest_ring_wait_sleep':'true',
            # The title render thread waits for its next queued frame, at most 1 ms, instead of
            # turning its frame loop and recomputing the gamma ramp between frames
            # (docs/analysis/career-season-soak.md 12); -Rov turns it off.
            'fm4_render_thread_idle_wait':'true',
            # 16x anisotropic filtering for every mipmapped linear texture: the title asks for
            # none on most and 2x-16x on a few, and the runtime default forces 4x.
            'anisotropic_override':'5',
            # FM4 writes guest memory only through watched pages (REX_STALE_PAGE_TRACE finds no
            # stale data), so CPU-uploaded pages need not be re-uploaded every frame; command lists
            # are recorded and executed, and resolve readbacks copied into guest memory, off the
            # command processor thread, and the swap does not wait for that thread to drain.
            'clear_memory_page_state':'false','d3d12_submit_thread':'true',
            'd3d12_submit_thread_async_swap':'true','d3d12_readback_publish_thread':'true',
            # The keyboard: the racing layout by FM4's DEFAULT LAYOUT 1, with the steering and
            # pedal model on (docs/analysis/keyboard-controls.md, docs/running.md#keyboard). The
            # left stick's up and down stay unbound: the title's steering deadzone is radial.
            'input_backend':'sdl','mnk_mode':'true','mnk_mouse':'false','mnk_smooth':'true',
            'keybind_right_trigger':'W','keybind_left_trigger':'S',
            'keybind_lstick_left':'A','keybind_lstick_right':'D',
            'keybind_lstick_up':'','keybind_lstick_down':'','keybind_precision':'Control',
            'keybind_a':'Space','keybind_b':'E,Backspace','keybind_x':'Q','keybind_y':'R',
            'keybind_left_shoulder':'Z','keybind_right_shoulder':'V',
            'keybind_start':'Escape,Return','keybind_back':'C,Tab',
            'keybind_dpad_up':'Up','keybind_dpad_down':'Down',
            'keybind_dpad_left':'Left','keybind_dpad_right':'Right',
            'keybind_rstick_up':'I','keybind_rstick_down':'K','keybind_rstick_left':'J',
            'keybind_rstick_right':'L','keybind_lstick_press':'F','keybind_rstick_press':'H',
            'log_file':log_file}

def main():
    global PROGRESS,FETCH,ABI
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['doctor','setup','generate','build','all','run','warmcache',
                                          'profbuild','export','preflight','launcher-data','snapshot',
                                          'toolchain'])
    parser.add_argument('--build-root',type=Path,default=REPO/'work/portable-fm4')
    parser.add_argument('--disc1',type=Path)
    parser.add_argument('--disc2',type=Path)
    parser.add_argument('--content',type=Path,action='append',
                        help='generate, all, run: an FM4 content package (DLC) or a directory searched '
                             'for them; repeatable. Saved in local.json and imported at the next run')
    parser.add_argument('--jobs',type=int,default=4)
    parser.add_argument('--out',type=Path,
                        help='export: the folder to export the game and its GUI launcher to; '
                             'launcher-data: the folder of launcher.json (default the BuildRoot); '
                             'snapshot: the new folder of the source snapshot')
    parser.add_argument('--revision',default='HEAD',
                        help='snapshot: the commit to snapshot (default HEAD)')
    parser.add_argument('--progress',
                        help='setup, generate, build, all: append a JSON line for each stage and step '
                             "to this file, or to the output with '-' (docs/setup.md#progress-lines)")
    parser.add_argument('--tests',choices=TEST_SETS,default='developer',
                        help='build, all: developer runs every test (default); player leaves out the '
                             'shader tests on WARP and with FXC (docs/setup.md#3-build)')
    parser.add_argument('--single-tile',action='store_true',
                        help='run: races without MSAA and predicated tiling also with a look without '
                             'TAA, then with FXAA (a look with TAA, every preset, has one tile anyway)')
    parser.add_argument('--msaa',action='store_true',
                        help='run: races keep the tiles and MSAA of --quality with a look that has TAA')
    parser.add_argument('--msaa-samples',type=int,choices=[0,2,4],
                        help='run with --native: races in one pass with this MSAA (2x, the default '
                             'of --native, or 4x), or 0 for one tile without MSAA '
                             '(docs/analysis/d3d-hle-n3.md 16.17)')
    parser.add_argument('--fixed-vblank',action='store_true',
                        help='run: hold every swap to the fixed 60 Hz vblank grid (vblank_vrr=false)')
    parser.add_argument('--frame-rate',type=int,choices=range(30,241),metavar='30-240',
                        help='run: race frame rate above 60 (for example 120 or 240); menus, '
                             'replays and photo mode keep their own')
    parser.add_argument('--display',choices=['native',*DISPLAYS],default=DEFAULT_DISPLAY,
                        help='run: screen (default native: fullscreen on the primary monitor; '
                             'the others a window of that size) and the render scale that covers it')
    parser.add_argument('--fullscreen',action='store_true',
                        help='run: fullscreen with a --display size (its render scale)')
    parser.add_argument('--supersample',action='store_true',
                        help='run: render one scale above the screen (up to 4x) and average it down')
    parser.add_argument('--info',action='store_true',
                        help='run: show the settings overlay (display, quality, look, ...) at start; '
                             'F2 toggles it in any run')
    parser.add_argument('--pad',
                        help="run: the controller's options as knobs, for example "
                             "'driver=rawinput,trigger_rumble=1,rumble=1.5' (PAD_KNOBS)")
    parser.add_argument('--upscale',default=DEFAULT_UPSCALE,
                        help='run: native (default), quality, balanced, performance or a ratio from '
                             '1 to 3: FSR 3.1 renders the scene at fewer lines and upscales it to '
                             'the output scale; needs -Native and aa=fsr or aa=taau in -Look')
    parser.add_argument('--entry0',choices=ENTRY0_SCALES,
                        help="run: with -Native, the menus and photo mode at the scene's scale (s, "
                             'the default) or at the whole scale below it (k)')
    parser.add_argument('--keyboard',
                        help="run: the keyboard's steering and pedal model as knobs, for example "
                             "'steer_attack=0.4,precision=0.6' (KEYBOARD_KNOBS)")
    parser.add_argument('--wheel',
                        help="run: the racing wheel as knobs, for example "
                             "'on=1,throttle_rest=-30000,shift_up=4' (WHEEL_KNOBS)")
    parser.add_argument('--vsync',choices=VSYNC_MODES,default=DEFAULT_VSYNC,
                        help='run: auto (default: on a VRR display, tearing presents held a refresh '
                             'apart and races under the refresh; else on), on (the refresh, no '
                             'tearing) or off (at once, tearing allowed)')
    parser.add_argument('--resolution-scale',
                        type=lambda text:text if text=='panel' else int(text),
                        choices=[*range(1,MAX_RESOLUTION_SCALE+1),'panel'],
                        metavar=f'1-{MAX_RESOLUTION_SCALE}|panel',
                        help='run: render at this multiple of 1280x720 instead of the --display '
                             "one, or panel: the screen's own resolution (the largest 16:9 "
                             'picture that fits it) on the native path in one pass')
    parser.add_argument('--full-rate-mirror',action='store_true',
                        help='run: render the rear-view mirror every frame in every race')
    parser.add_argument('--quality',choices=[*QUALITY_PRESETS,*QUALITY_ALIASES],
                        help='run: render quality of races, '+', '.join(QUALITY_PRESETS)+
                             ' (default '+DEFAULT_QUALITY+"; medium, also xbox: the title's own "
                             'with soft particles)')
    parser.add_argument('--shadow-workaround',action='store_true',
                        help='run: no shadow map outside the cockpit in races (the old Xenia workaround)')
    parser.add_argument('--rov',action='store_true',
                        help='run: the slower pixel shader interlock render target path instead of RTV')
    parser.add_argument('--native',action='store_const',const=True,default=None,
                        help='run: the native path, the host render thread instead of the command '
                             'processor thread (the default unless --rov or --msaa; '
                             'docs/analysis/d3d-hle.md)')
    parser.add_argument('--rtv',action='store_const',const=False,dest='native',
                        help='run: the PM4 path on host render targets (RTV) instead of the native '
                             'path, the default')
    parser.add_argument('--look',
                        help='run: post-processing look, a preset ('+', '.join(LOOK_PRESETS)+
                             '; pc is aces; default '+DEFAULT_LOOK+') and/or knobs, for example '
                             '"aces,sharpen=0.6" (knobs: '+
                             ', '.join((*LOOK_KNOBS,*LOOK_RUNTIME_KNOBS,*LOOK_TRACK_KNOBS,*LOOK_PASS_KNOBS))+
                             '; work/fm4-rex/look/fm4-look.hlsli)')
    parser.add_argument('--ao',action='store_true',
                        help="run: the runtime's screen-space ambient occlusion pass (the GTAO post pass) "
                             'even without the ao look knob, which also applies it (on in every '
                             'preset but original)')
    parser.add_argument('--shader-dev',action='store_true',
                        help='run: shader mod development: the look mods load from work/fm4-rex/look '
                             'and hot reload when a file there is saved, and every look knob is a '
                             'live knob that shader_mod_set changes in the console (`)')
    parser.add_argument('--texture-dump',type=Path,
                        help='run: write each new game texture once as <hash>.dds to this directory')
    parser.add_argument('--texture-replace',type=Path,
                        help='run: use <hash>.dds files from this directory instead of the game textures')
    parser.add_argument('--ffx-mirror',
                        help='setup, build, all: the FidelityFX SDK mirror, a Git URL or a local '
                             '.bundle file (FFX_MIRROR; docs/setup.md)')
    parser.add_argument('--fsr',choices=['on','off'],
                        help='setup, build, all: link AMD FSR 3.1, built from the FidelityFX SDK '
                             'source, into the runtime (REX_FSR; fetches the pinned SDK). Kept for '
                             'later builds; off by default')
    parser.add_argument('--fetch',choices=FETCHES,
                        help='setup, build, all: how the third-party sources are fetched: git clones '
                             "(the default in a Git checkout), archive takes GitHub's archives of the "
                             'pinned commits without Git (the default in a source snapshot; '
                             'docs/setup.md#fetching-without-git)')
    parser.add_argument('--abi',choices=ABIS,
                        help='setup, generate, build, all, profbuild: mingw builds with llvm-mingw '
                             '(default); msvc with LLVM and Microsoft\'s libraries from the Visual '
                             'Studio x64 environment, only for the A/B against the mingw build while '
                             'the switch is verified (docs/setup.md#toolchain). run, export and '
                             "warmcache default to the BuildRoot's own")
    parser.add_argument('--toolchain',type=Path,
                        help='toolchain: the folder to put the portable tools in; any other action: '
                             "put that folder's tools (its toolchain-state.json) first on PATH "
                             '(docs/setup.md#portable-tools)')
    parser.add_argument('--downloads',type=Path,
                        help="toolchain: the folder of the tools' archives (default "
                             '<toolchain>/downloads), kept for later runs')
    parser.add_argument('--portable',action='store_true',
                        help='toolchain: fetch and unpack every tool, also where an installed copy '
                             'would do')
    parser.add_argument('--flavor',default='',help='opt-in compiler experiment for build: comma list of '+','.join(FLAVORS))
    args=parser.parse_args()
    root=args.build_root.resolve()
    # The actions a builder runs before any toolchain exists: no doctor.
    if args.action=='snapshot':
        if not args.out:
            raise ValueError('snapshot needs --out, the new folder of the snapshot.')
        make_snapshot(args.out,args.revision)
        return
    if args.action=='preflight':
        report=preflight(root)
        print(json.dumps(report,indent=1))
        if not report['ok']:
            sys.exit(1)
        return
    if args.action=='launcher-data' and args.out:
        out=args.out.resolve()
        if out==REPO or REPO in out.parents:
            raise ValueError('The launcher-data folder must be outside the source.')
        out.mkdir(parents=True,exist_ok=True)
        print(f'Wrote {write_launcher_data(out)}.')
        return
    # Never write private outputs into the tracked deliverables/source directories.
    if build_root_error(root):
        raise ValueError(build_root_error(root))
    if args.action=='launcher-data':
        root.mkdir(parents=True,exist_ok=True)
        print(f'Wrote {write_launcher_data(root)}.')
        return
    if args.jobs<1 or args.jobs>64:
        raise ValueError('jobs must be between 1 and 64')
    flavor=tuple(sorted({f for f in args.flavor.split(',') if f}))
    if set(flavor)-set(FLAVORS) or {'pgo-gen','pgo-use'}<=set(flavor):
        raise ValueError(f'flavor must be a comma list of {FLAVORS}, not both pgo-gen and pgo-use')
    if args.ffx_mirror: os.environ['FFX_MIRROR']=args.ffx_mirror
    if args.fetch: FETCH=args.fetch
    # The actions that build nothing keep the BuildRoot's ABI unless told otherwise.
    ABI=args.abi or (args.action in ('run','export','warmcache') and build_root_abi(root)) or 'mingw'
    if args.progress:
        if args.progress=='-':
            PROGRESS=sys.stdout
        else:
            Path(args.progress).parent.mkdir(parents=True,exist_ok=True)
            PROGRESS=open(args.progress,'a',encoding='utf-8')
    if args.action=='toolchain':
        if not args.toolchain:
            raise ValueError('toolchain needs --toolchain, the folder of the portable tools.')
        with progress('stage','toolchain'):
            report=toolchain(args.toolchain,args.downloads,args.portable)
        print(json.dumps(report,indent=1))
        return
    if args.toolchain:
        print('Toolchain:',', '.join(use_toolchain(args.toolchain,('llvm-mingw',) if ABI=='msvc' else ())))
    doctor(fetch_mode())
    print(ffx_status(root))
    print(gameinput_status(root))
    print(dxc_status(root))
    if args.action=='doctor': return
    discs=None
    if args.content and args.action not in ('generate','all','run'):
        raise ValueError('--content is read by generate, all and run only.')
    if args.action in ('generate','all'):
        if args.disc1 and any(disc and disc.is_file() for disc in (args.disc1,args.disc2)):
            root.mkdir(parents=True,exist_ok=True)
            with progress('stage','discs'):
                args.disc1=disc_input(root,args.disc1,'disc1')
                args.disc2=disc_input(root,args.disc2,'disc2')
        discs=validate_discs(args.disc1,args.disc2)
        validate_content(discs[1],args.content)
    root.mkdir(parents=True,exist_ok=True)
    check_layout(root)
    if args.action in ('setup','generate','build','all','profbuild'):
        check_abi(root)
    fsr=None if args.fsr is None else args.fsr=='on'
    if args.action in ('setup','all'):
        with progress('stage','setup'): setup(root,args.jobs,fsr)
    if args.action in ('generate','all'):
        with progress('stage','generate'): generate(root,*discs,args.content,args.jobs)
    if args.action=='run' and args.content: save_content(root,args.content)
    if args.action in ('build','all'):
        with progress('stage','build'):
            build_game(root,args.jobs,flavor,None if args.action=='all' else fsr,args.tests)
    if args.action=='run': launch(root,args.single_tile,args.fixed_vblank,args.frame_rate,args.rov,
                                     args.resolution_scale,args.full_rate_mirror,args.look,
                                     args.shadow_workaround,args.quality,args.display,
                                     args.fullscreen,args.supersample,args.info,args.ao,
                                     args.shader_dev,args.texture_dump,args.texture_replace,args.msaa,
                                     args.native,args.msaa_samples,args.vsync,args.pad,
                                     args.upscale,args.keyboard,args.entry0,args.wheel)
    if args.action=='warmcache': warm_cache(root)
    if args.action=='profbuild': build_profiling(root,args.jobs)
    if args.action=='export':
        if not args.out:
            raise ValueError('export needs --out (Build.ps1 -Out), the folder to export to.')
        export(root,args.out,args.jobs)

if __name__=='__main__':
    try: main()
    except (OSError,ValueError,RuntimeError,subprocess.CalledProcessError) as error:
        sys.exit(f'ERROR: {error}')
