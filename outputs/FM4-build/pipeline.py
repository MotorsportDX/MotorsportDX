"""Windows source-only bootstrap. Private outputs stay below build-root."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import time

from prepare import write_switches

HERE = Path(__file__).resolve().parent
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
NOTICES = REPO/'outputs/FM4-source-control/third-party-notices'
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

def source_state(root):
    """What a build compiled: the HEAD of the checkout that ran it, whether its work/runtime or
    work/fm4-rex differ from HEAD, and a SHA-256 over the paths and contents of the BuildRoot's
    runtime-source (its mirror listing) and the project's title sources (src, tests,
    CMakeLists.txt), which another checkout may have left there."""
    def git(*args):
        result=subprocess.run(['git','-C',REPO,*args],capture_output=True,text=True)
        return result.stdout.strip() if result.returncode==0 else None
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
    status=git('status','--porcelain','--','work/runtime','work/fm4-rex')
    return {'head':git('rev-parse','HEAD'),'dirty':bool(status),'tree':tree.hexdigest()}

def run(args, cwd=None, log=None, env=None):
    args = [str(x) for x in args]
    print('Running:', subprocess.list2cmdline(args), flush=True)
    if log:
        log.parent.mkdir(parents=True, exist_ok=True)
        with log.open('w', encoding='utf-8') as output:
            result = subprocess.run(args, cwd=cwd, stdout=output, stderr=subprocess.STDOUT, env=env)
        if result.returncode:
            print(log.read_text(errors='replace')[-6000:])
            raise RuntimeError(f'Exit {result.returncode}; log: {log}')
    else:
        subprocess.run(args, cwd=cwd, env=env, check=True)

def doctor():
    if os.name != 'nt' or sys.maxsize <= 2**32 or sys.version_info < (3,11):
        raise RuntimeError('Requires Windows x64 and Python 3.11 or newer (64-bit).')
    for name in ('git', 'cmake', 'ninja', 'clang', 'clang++', 'link'):
        if not shutil.which(name):
            raise RuntimeError(f'Missing {name}. See docs/setup.md and use Build.ps1 from a VS C++ environment.')
    version = subprocess.check_output(['clang++', '--version'], text=True)
    if not re.search(r'clang version 20\.', version):
        raise RuntimeError('Use LLVM 20.x (validated: 20.1.8); pass -LLVMRoot to Build.ps1.')
    cmake_version = subprocess.check_output(['cmake','--version'],text=True)
    match = re.search(r'version (\d+)\.(\d+)',cmake_version)
    if not match or tuple(map(int, match.groups())) < (3,25):
        raise RuntimeError('CMake 3.25+ is required.')
    sdk_dir = os.environ.get('WindowsSdkDir', '')
    sdk_version = os.environ.get('WindowsSDKVersion', '').strip('\\/')
    fxc = Path(sdk_dir)/'bin'/sdk_version/'x64/fxc.exe'
    if not sdk_dir or not sdk_version or not fxc.is_file():
        raise RuntimeError('Windows SDK x64 fxc.exe not found in the VS development environment.')
    print('Preflight passed. LLVM:', version.splitlines()[0], 'FXC:', fxc)
    return fxc

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
        raise ValueError('Provide -Disc1 with the extracted Play Disc directory, not an ISO.')
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

def fetch_thirdparty(root):
    """Third-party libraries from the upstream SDK revision, outside the repository."""
    upstream, destination = root/'runtime-upstream', root/'runtime-source/thirdparty'
    marker = destination/'.fm4-thirdparty.json'
    wanted = {'revision':SDK_REV,'vendored':THIRDPARTY_VENDORED,'submodules':THIRDPARTY_SUBMODULES}
    if marker.is_file() and json.loads(marker.read_text()) == json.loads(json.dumps(wanted)):
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

def compile_resolve_shaders(root, fxc):
    """FM4 resolve shaders, the scaled readback downscale shader and the presenter's area
    downscale shader -> DXBC headers; only when their sources changed."""
    source = root/'runtime-source'
    shaders = source/'src/graphics/shaders'
    shader_source = shaders/'fm4-resolve'
    downscale = shaders/'resolve_downscale.cs.hlsl'
    area = source/'src/ui/shaders/guest_output_area.ps.hlsl'
    inputs = hashlib.sha256(b''.join(path.read_bytes() for path in
                                     sorted(shader_source.glob('*.xesl*'))+[downscale,area]))
    marker = source/'.fm4-resolve-shaders'
    bytecode = shaders/'bytecode/d3d12_5_1'
    area_output = source/'src/ui/shaders/bytecode/d3d12_5_1/guest_output_area_ps.h'
    outputs = [bytecode/f'{name}_cs.h' for name in RESOLVE_SHADERS]+[bytecode/'resolve_downscale_cs.h']
    host_outputs = [bytecode/f'{name}_cs.h' for name in RESOLVE_HOST_SHADERS]
    if (marker.is_file() and marker.read_text() == inputs.hexdigest() and
            all(path.is_file() for path in outputs+host_outputs+[area_output])):
        return
    strip = ['/O3','/Qstrip_reflect','/Qstrip_debug','/Qstrip_priv','/Gfp','/nologo']
    flags = ['/T','cs_5_1',*strip]
    for name,output in zip(RESOLVE_SHADERS,outputs):
        run([fxc,'/D','SHADING_LANGUAGE_HLSL_XE=1',*flags,
             '/Fh',output,'/Vn',name+'_cs',shader_source/(name+'.cs.xesl')],log=root/f'logs/{name}.log')
    for (name,defines),output in zip(RESOLVE_HOST_SHADERS.items(),host_outputs):
        run([fxc,'/D','SHADING_LANGUAGE_HLSL_XE=1',*[arg for define in defines for arg in ('/D',define)],
             *flags,'/Fh',output,'/Vn',name+'_cs',shader_source/'resolve_host_32bpp.xesli'],
            log=root/f'logs/{name}.log')
    run([fxc,*flags,'/Fh',outputs[-1],'/Vn','resolve_downscale_cs',downscale],
        log=root/'logs/resolve_downscale.log')
    run([fxc,'/T','ps_5_1',*strip,'/Fh',area_output,'/Vn','guest_output_area_ps',area],
        log=root/'logs/guest_output_area.log')
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

def fetch_fidelityfx(root):
    """<BuildRoot>/fidelityfx-sdk at FFX_REV, sparse (FFX_PATHS), from GitHub or else from
    FFX_MIRROR (ffx_mirror). Stops when the checked-out commit differs from the pinned one."""
    sdk=root/'fidelityfx-sdk'
    log=root/'logs/fidelityfx-fetch.log'
    mirror=ffx_mirror()
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
    head=ffx_head(sdk) if (sdk/'.git').is_dir() else None
    state=('not fetched yet (setup fetches it)' if head is None else
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

def build_runtime(root, fxc, jobs, fsr=None, build=None):
    """fsr: True or False sets REX_FSR (AMD FSR 3.1 from the FidelityFX SDK source, fetched
    by fetch_fidelityfx); None keeps the build directory's setting (off when it is new). build:
    another build directory, which is built but not installed."""
    source, install = root/'runtime-source', root/'runtime-install'
    default_build = build is None
    build = root/'runtime-build' if default_build else Path(build)
    sync_runtime(root)
    fetch_thirdparty(root)
    compile_resolve_shaders(root, fxc)
    options=[]
    if fsr is not None:
        options=['-DREX_FSR=ON',f'-DREX_FSR_SDK_DIR={fetch_fidelityfx(root).as_posix()}'] if fsr \
            else ['-DREX_FSR=OFF']
    if not (build/'build.ninja').is_file() or options:
        run(['cmake','-S',source,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
             '-DCMAKE_C_COMPILER=clang','-DCMAKE_CXX_COMPILER=clang++',
             '-DCMAKE_C_FLAGS=-march=x86-64-v2','-DCMAKE_CXX_FLAGS=-march=x86-64-v2',
             '-DREXGLUE_ENABLE_TRACY=OFF','-DREXGLUE_BUILD_TESTS=OFF','-DREXGLUE_USE_VULKAN=OFF',
             *options,f'-DCMAKE_INSTALL_PREFIX={install}'],
            log=root/('logs/runtime-configure.log' if default_build else f'logs/{build.name}-configure.log'))
    run(['cmake','--build',build,'--parallel',jobs],log=root/f'logs/{build.name}.log')
    if default_build:
        run(['cmake','--install',build],log=root/'logs/runtime-install.log')

def setup(root, fxc, jobs, fsr=None):
    fetch_fidelityfx(root)
    build_runtime(root, fxc, jobs, fsr)
    (root/'setup-complete.json').write_text(json.dumps({'thirdparty_revision':SDK_REV,
                                                        'fidelityfx_revision':FFX_REV,
                                                        'runtime':'work/runtime'}))

def generate(root, disc1, disc2, content, jobs):
    install, project = root/'runtime-install', root/'project'
    cli = install/'bin/rexglue.exe'
    if not cli.is_file() or not (root/'setup-complete.json').is_file():
        raise RuntimeError('Run setup first.')
    project.mkdir(exist_ok=True)
    # Only source files from the source-only checkout are copied. No developer cache.
    inventory=json.loads((REPO/'outputs/FM4-source-control/tracked-files.json').read_text())
    for entry in inventory['files']:
        name=entry['path']
        if name.startswith(('work/fm4-rex/src/','work/fm4-rex/tests/')):
            destination=project/Path(name).relative_to('work/fm4-rex')
            destination.parent.mkdir(parents=True,exist_ok=True)
            copy_source(REPO/name,destination)
    copy_source(REPO/'work/fm4-rex/CMakeLists.txt',project/'CMakeLists.txt')
    (project/'generated').mkdir(exist_ok=True)
    copy_source(HERE/'rexglue.cmake',project/'generated/rexglue.cmake')
    copy_source(HERE/'guard-generated.py',project/'guard-generated.py')
    for name in ('fm4_bounds.toml','fm4_initializers.toml','fm4_callbacks.toml','fm4_prior_cfg.toml','speech_bounds.toml','xmedia_bounds.toml'):
        copy_source(HERE/name,project/name)
    loader = root/'loader-build'
    run(['cmake','-S',HERE/'loader','-B',loader,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
         '-DCMAKE_CXX_COMPILER=clang++',f'-DCMAKE_PREFIX_PATH={install}'],log=root/'logs/loader-configure.log')
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

def build_game(root,fxc,jobs,flavor=(),fsr=None):
    project, install, build = root/'project',root/'runtime-install',game_build_dir(root,flavor)
    if not (root/'local.json').is_file():
        raise RuntimeError('Run generate first.')
    state=json.loads((root/'local.json').read_text())
    validate_discs(state['disc1'],state['disc2'])
    # Edits in work/runtime are picked up here: sync, incremental build, install.
    build_runtime(root,fxc,jobs,fsr)
    suffix=build.name[len('game-build'):]
    # clang++ drives lld-link, so linker options go through -Xlinker.
    lto_jobs=f'-Xlinker /opt:lldltojobs={jobs}'
    linker=[f'-DCMAKE_EXE_LINKER_FLAGS={lto_jobs}',f'-DCMAKE_SHARED_LINKER_FLAGS={lto_jobs}'] \
        if 'thinlto' in flavor else []
    run(['cmake','-S',project,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
         '-DCMAKE_CXX_COMPILER=clang++',f'-DCMAKE_CXX_FLAGS={flavor_flags(root,flavor)}',*linker,
         f'-DCMAKE_PREFIX_PATH={install}',f'-DPython3_EXECUTABLE={sys.executable}',
         f'-DFM4_LOADED_IMAGE={root / "loaded-image.bin"}'],log=root/f'logs/game-configure{suffix}.log')
    started=time.time()
    run(['cmake','--build',build,'--parallel',jobs],log=root/f'logs/game-build{suffix}.log')
    print(f'Game build took {time.time()-started:.0f} s.')
    # Always stage the matching runtime/plugin, including incremental builds.
    for dll in (install/'bin').glob('*.dll'):
        shutil.copyfile(dll,build/dll.name)
    run([build/'fm4_instruction_tests.exe'],cwd=build,log=root/f'logs/instruction-tests{suffix}.log')
    post_pass_tests(root,jobs)
    artifacts = ['fm4_rex.exe','fm4_rex_XMediaFacade_default.dll','fm4_rex_SpeechFacade_default.dll','rexruntime.dll','rexgpu-xenos.dll']
    report = {'artifacts':{name:digest(build/name) for name in artifacts},'gameplay_verified':False,
              'source':source_state(root)}
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

def post_pass_tests(root,jobs):
    """CPU tests of the runtime's post passes (docs/analysis/post-pass-framework.md) and of the
    shader mods, no GPU: the scene camera math, the GTAO passes of work/fm4-rex/passes on WARP
    (Direct3D 12 on the CPU) as the framework runs them, FXC compiles of the override library and
    the look mods with and without XE_POST_VIEWS (the library's post pass code is only compiled
    with it), the track lighting file edits of the look's track knobs (fm4-track-settings.h), and
    the texture replacement content hash and DDS files (docs/analysis/texture-replacement.md),
    the draw resolution scale as a fraction (docs/analysis/d3d-hle-n5.md), the physical pads'
    stick and trigger shaping (docs/analysis/modern-gamepads.md), and the keyboard's steering and
    pedal model (docs/analysis/keyboard-controls.md).
    work/fm4-rex/tests, synced here like the runtime, built against the installed runtime headers. The WARP scene's checks were set with a GTAO radius of 1 m."""
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
         '-DCMAKE_CXX_COMPILER=clang++',f'-DREX_HEADER_ROOT={root/"runtime-install/include"}',*fsr],
        log=root/'logs/tests-configure.log')
    run(['cmake','--build',build,'--parallel',jobs,'--target','fm4_post_pass_camera_tests',
         'fm4_post_pass_shader_tests','fm4_shader_mod_tests','fm4_track_settings_tests',
         'fm4_texture_replacement_tests','fm4_draw_scale_tests','fm4_pad_shaping_tests',
         'fm4_keyboard_model_tests'],
        log=root/'logs/tests-build.log')
    run([build/'fm4_track_settings_tests.exe'],cwd=build,log=root/'logs/track-settings-tests.log')
    run([build/'fm4_texture_replacement_tests.exe'],cwd=build,
        log=root/'logs/texture-replacement-tests.log')
    run([build/'fm4_post_pass_camera_tests.exe'],cwd=build,log=root/'logs/post-pass-camera-tests.log')
    run([build/'fm4_draw_scale_tests.exe'],cwd=build,log=root/'logs/draw-scale-tests.log')
    run([build/'fm4_pad_shaping_tests.exe'],cwd=build,log=root/'logs/pad-shaping-tests.log')
    run([build/'fm4_keyboard_model_tests.exe'],cwd=build,log=root/'logs/keyboard-model-tests.log')
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

def build_profiling(root,fxc,jobs):
    """game-build-prof: game-build with a runtime that has line tables and PDBs, for the
    command processor sampling profiler (REX_CP_PROFILE, docs/e2e/traces.md). The runtime writes its
    outputs into <source>/out, so this compiles a mirror of runtime-source (runtime-source-prof)
    and leaves the default build alone."""
    game=root/'game-build'
    if not (game/'fm4_rex.exe').is_file():
        raise RuntimeError('Run build first.')
    sync_runtime(root)
    fetch_thirdparty(root)
    compile_resolve_shaders(root,fxc)
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
             '-DCMAKE_C_COMPILER=clang','-DCMAKE_CXX_COMPILER=clang++',
             f'-DCMAKE_C_FLAGS={flags}',f'-DCMAKE_CXX_FLAGS={flags}',
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
            # Cockpit interior and driver (work/fm4-rex/look/fm4-cockpit.hlsli): the least share
            # of the sun's direct light that the interior keeps in its own shadow.
            'cockpit_light':'FM4_LOOK_COCKPIT_LIGHT',
            # Ambient occlusion of the GTAO post pass on the scene; above 0 it also turns
            # the post passes on (ao_cvars).
            'ao':'FM4_LOOK_AO'}
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
# aa=taa, the default, is TAA.
# windshield above 0: the glass of the cockpit camera's windshield and side windows, which the
# title does not draw there (work/fm4-rex/passes/windshield.hlsl, docs/analysis/windshield.md):
# the strength of its tint, dashboard reflection, glare and dust (WINDSHIELD), up to 2.
LOOK_PASS_KNOBS=('taa','taa_point','aa','windshield')
# Knobs that take a name instead of a number.
LOOK_NAMES={'tonemap':{'filmic':0,'gt':1,'aces':2,'neutral':3}}
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
# sharpening after the resolve), the cockpit interior's shadow floor (cockpit_light), the
# cockpit glass (windshield), the road's grained sheen instead of its sun highlight
# (road_specular=0,road_sheen=0.4) and the glass, chrome and rim mods. pc is aces under its
# earlier name (LOOK_ALIASES). original is the game's own picture with TAA and no shader mod;
# 'original,taa=0' is the game's picture as the title draws it, the reference against -Rov.
# No preset has the sky_* or foliage_* knobs: their A/B at 2x of 18 October 2026 showed only a
# slightly deeper sky (docs/analysis/look-mod-coverage.md). Every preset has TAA, so races
# render one tile without MSAA with them (auto_single_tile).
_LOOK_MODS=('saturation=1.1,bloom=0.7,sharpen=0.4,clearcoat=1,clearcoat_glint=1,ao=0.6,sun=2,sky=0.7,'
            'taa=1,cockpit_light=0.5,windshield=1,road_specular=0,road_sheen=0.4,glass=1,chrome=1,rims=1')
LOOK_PRESETS={'aces':'tonemap=aces,'+_LOOK_MODS,'gt':'tonemap=gt,'+_LOOK_MODS,
              'neutral':'tonemap=neutral,'+_LOOK_MODS,'original':'taa=1'}
LOOK_ALIASES={'pc':'aces'}
DEFAULT_LOOK='aces'

# Render quality of races (FM4_RACE_SETTINGS, fm4-render-overrides.cpp: controls of the race
# render scenarios). xbox keeps the title's own. high gives races what its replays and photo
# mode have: 4x MSAA (TilingScenario 0 instead of 1, 2x), soft particles, shadows of
# collidable objects, full detail wheels, car LOD transitions twice as far, and undoes the
# 16 car cuts (a cheaper car shader, no damage, environment map never updated, half rate bloom,
# no collidables in the mirror). ultra also keeps every car at its top LOD and renders the
# mirror every frame with every car.
_HIGH=['TilingScenario=0','SoftParticles=1','CollidableShadows=1','CarMinWheelLOD=0',
       'CarLODTransDist=near:30.0,far:50.0,veryfar:100.0','CarShader=-','CarDamage=1',
       'CarDamageTextures=1','EnvMapFrequencyScale=1.0','HalfRateBloom=-','CollidablesInMirror=-']
QUALITY_PRESETS={'xbox':[],'high':_HIGH,
                 'ultra':_HIGH+['CarKeepLOD0=1','HalfRateMirror=0','MaxCarsInMirror=11']}
DEFAULT_QUALITY='high'

def race_settings(quality,single_tile=False):
    """FM4_RACE_SETTINGS of a quality preset; one tile (-SingleTile, auto_single_tile) keeps its
    own tiling scenario."""
    if quality not in QUALITY_PRESETS:
        raise RuntimeError(f'Unknown quality {quality!r}; use {", ".join(QUALITY_PRESETS)}.')
    return ';'.join(s for s in QUALITY_PRESETS[quality]
                    if not (single_tile and s.startswith('TilingScenario=')))

LOOK_DIRECTORY=REPO/'work/fm4-rex/look'
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
    """The aa knob: taa (the default) or fsr."""
    value=knobs.get('aa','taa')
    if value not in ('taa','fsr'):
        raise RuntimeError(f'aa is taa or fsr, not {value!r}.')
    return value

def windshield_strength(knobs):
    """The windshield knob: 0 (off, the default) to 2."""
    value=float(knobs.get('windshield','0'))
    if not 0<=value<=2:
        raise RuntimeError(f'windshield is a strength from 0 (off) to 2, not {knobs["windshield"]!r}.')
    return value

def look_defines(knobs):
    """The shader_mod_defines of look knobs: NAME=VALUE with the hlsli names and numbers, and
    with taa the pass params TAA and TAA_SHARPEN (the look's sharpening, which then is 0), or at
    the scene_color point (taa_point=1) TAA_POINT with the look's sharpening kept, or with
    aa=fsr FSR at the scene_color point with the look's sharpening as FSR_SHARPNESS."""
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
    fsr=aa_method(knobs)=='fsr'
    if fsr and not taa_enabled(knobs):
        raise RuntimeError('aa=fsr needs taa above 0 (the look turns temporal anti-aliasing on).')
    if fsr:
        # FSR at the scene_color point, the look's sharpening as its RCAS.
        sharpen=float(knobs.get('sharpen','0'))
        defines=[d for d in defines if not d.startswith('FM4_LOOK_SHARPEN=')]
        defines+=['FM4_LOOK_SHARPEN=0.0','TAA=1','TAA_SHARPEN=0.0','TAA_POINT=1','FSR=1',
                  f'FSR_SHARPNESS={sharpen!r}']
    elif taa_enabled(knobs) and scene_point:
        defines+=['TAA=1','TAA_SHARPEN=0.0','TAA_POINT=1']
    elif taa_enabled(knobs):
        sharpen=float(knobs.get('sharpen','0'))
        defines=[d for d in defines if not d.startswith('FM4_LOOK_SHARPEN=')]
        defines+=['FM4_LOOK_SHARPEN=0.0','TAA=1',f'TAA_SHARPEN={sharpen!r}']
    if windshield_strength(knobs)>0:
        defines.append(f'WINDSHIELD={windshield_strength(knobs)!r}')
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

def look_cvars(root,look,shader_dev=False):
    """Cvars of the look mods (work/fm4-rex/look/fm4-look.hlsli): copies them to BuildRoot/look
    and passes the knobs of the look (look_knobs; knobs left out keep the original look). Only
    post pass knobs ('original': TAA): the post passes and their params without the look mods;
    none ('original,taa=0'): no mods at all.
    shader_dev (docs/analysis/shader-mod-iteration.md): the mods load from work/fm4-rex/look
    itself and hot reload when a file there changes, and every knob of the files is a live knob
    (the ones left out at their defaults), which shader_mod_set changes in the console (`)."""
    # The track lighting knobs are not shader knobs (look_track_settings).
    knobs={name:value for name,value in look_knobs(look).items() if name not in LOOK_TRACK_KNOBS}
    cvars=sun_cvars(knobs.get('sun'))
    mods=shader_dev or any(name not in LOOK_RUNTIME_KNOBS and name not in LOOK_PASS_KNOBS for name in knobs)
    defines=look_defines(knobs)
    if not mods:
        # The post pass params reach the passes through shader_mod_defines without the mods.
        defines=[define for define in defines if not define.startswith('FM4_LOOK_')]
        if defines:
            cvars['shader_mod_defines']=';'.join(defines)
        cvars.update(ao_cvars(root,taa_enabled(knobs) or windshield_strength(knobs)>0))
        return cvars
    if shader_dev:
        directory=LOOK_DIRECTORY
        given={define.split('=',1)[0] for define in defines}
        defines+=[f'{name}={float(value)!r}' for name,value in look_knob_defaults().items()
                  if name not in given]
    else:
        directory=root/'look'
        if directory.is_dir():
            shutil.rmtree(directory)
        directory.mkdir()
        for path in LOOK_DIRECTORY.glob('*.hlsli'):
            shutil.copyfile(path,directory/path.name)
    cvars.update({'shader_mod_path':directory,'shader_mod_defines':';'.join(defines)})
    if shader_dev:
        cvars.update(shader_mod_hot_reload='true',shader_mod_live_knobs='true')
    # The ao, taa and windshield knobs need the runtime's post passes.
    cvars.update(ao_cvars(root,float(knobs.get('ao','0'))>0 or taa_enabled(knobs)
                          or windshield_strength(knobs)>0,shader_dev))
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

# -Upscale (docs/running.md#upscaling, docs/analysis/fsr-plan.md 11): FSR 3.1 renders the scene
# at the output's lines divided by the mode's ratio and upscales it to the output scale.
UPSCALE_MODES={'native':1.0,'quality':1.5,'balanced':1.7,'performance':2.0}
DEFAULT_UPSCALE='native'

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
    if aa!='fsr':
        raise ValueError(f"-Upscale {upscale} needs aa=fsr in -Look: TAA does not upscale.")
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
            f'; FSR {upscale} {lines}p -> {output_lines}p{clamped}')

VSYNC_MODES=('auto','on','off')
DEFAULT_VSYNC='auto'

# -Pad: the physical controller's options (docs/running.md#controller,
# docs/analysis/modern-gamepads.md), knob name to its runtime cvar, its default, and its choices
# or its range (bool knobs are 0 or 1). Only knobs that differ from their default become cvars,
# so the default plan sets none. The GUI launcher reads this table from launcher.json.
PAD_BUTTONS=('a','b','x','y','lb','rb','back','start','ls','rs','up','down','left','right','guide',
             'none')
PAD_KNOBS={
    'driver':{'cvar':'sdl_joystick_driver','default':'default',
              'choices':['default','rawinput','wgi']},
    'thread':{'cvar':'input_sdl_thread','default':'0','choices':['0','1']},
    'rumble':{'cvar':'pad_rumble_strength','default':'1','min':0.0,'max':2.0},
    'trigger_rumble':{'cvar':'pad_trigger_rumble','default':'0','choices':['0','1']},
    'trigger_rumble_strength':{'cvar':'pad_trigger_rumble_strength','default':'1','min':0.0,'max':2.0},
    'stick_deadzone':{'cvar':'pad_stick_deadzone','default':'0','min':0.0,'max':0.5},
    'stick_outer':{'cvar':'pad_stick_outer','default':'1','min':0.5,'max':1.0},
    'stick_curve':{'cvar':'pad_stick_curve','default':'1','min':0.5,'max':3.0},
    'anti_deadzone':{'cvar':'pad_steering_anti_deadzone','default':'0','min':0.0,'max':0.5},
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

def option_knobs(text,table,option):
    """The knobs of a -Pad or -Keyboard value, 'name=value,...' (later items win)."""
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
    """The runtime cvars of a -Pad or -Keyboard value: the knobs that differ from their
    defaults."""
    cvars={}
    for name,value in option_knobs(text,table,option).items():
        knob=table[name]
        if 'choices' in knob:
            if value not in knob['choices']:
                raise ValueError(f"{option} {name} is one of {', '.join(knob['choices'])}, "
                                 f"not {value!r}.")
            if value==knob['default']:
                cvars.pop(knob['cvar'],None)
                continue
            text={'0':'false','1':'true'}.get(value,value) if knob['choices']==['0','1'] else value
        else:
            try:
                number=float(value)
            except ValueError:
                number=float('nan')
            if not knob['min']<=number<=knob['max']:
                raise ValueError(f"{option} {name} is a number from {knob['min']:g} to "
                                 f"{knob['max']:g}, not {value!r}.")
            if number==float(knob['default']):
                cvars.pop(knob['cvar'],None)
                continue
            text=repr(number)
        cvars[knob['cvar']]=text
    return cvars

def pad_knobs(pad):
    """The knobs of a -Pad value, 'driver=rawinput,trigger_rumble=1,rumble=1.5' (later items
    win)."""
    return option_knobs(pad,PAD_KNOBS,'-Pad')

def pad_cvars(pad):
    """The runtime cvars of a -Pad value: the knobs that differ from their defaults."""
    return option_cvars(pad,PAD_KNOBS,'-Pad')

def keyboard_cvars(keyboard):
    """The runtime cvars of a -Keyboard value, 'steer_attack=0.4,precision=0.6': the knobs that
    differ from the launchers' defaults."""
    return option_cvars(keyboard,KEYBOARD_KNOBS,'-Keyboard')

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
    lines.append(f'Quality: {quality}'+(" (the game's own)" if quality=='xbox' else '')+tiling)
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
def ao_cvars(root,enabled,shader_dev=False):
    """Cvars of the post passes: copies work/fm4-rex/passes to BuildRoot/passes, or with
    shader_dev uses it in place (the look's hot reload then reloads saved pass files too)."""
    if not enabled:
        return {}
    if shader_dev:
        return {'post_pass_path':PASSES_DIRECTORY}
    directory=root/'passes'
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
           keyboard=None):
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
        msaa_samples=msaa_samples,vsync=vsync,pad=pad,upscale=upscale,keyboard=keyboard)
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
                vsync=DEFAULT_VSYNC,pad=None,upscale=DEFAULT_UPSCALE,keyboard=None):
    """The cvars, the FM4_ environment and the display description of a run of the options of
    launch. The GUI launcher (work/fm4-launcher, plan.cpp) ports this function; launcher_tests
    compares the two. monitor: the primary monitor's (width, height, DPI scale, refresh Hz, VRR
    min Hz, VRR max Hz, integrated GPUs only) instead of the real one. native: True for -Native,
    False for -Rtv, None for the default: the native path with integrated GPUs only, unless
    -Rov or -Msaa (the main session's decision of 24 October 2026, d3d-hle-n4.md 17.25). pad:
    the -Pad value (PAD_KNOBS). upscale: the -Upscale mode (upscale_settings). keyboard: the
    -Keyboard value (KEYBOARD_KNOBS)."""
    monitor=monitor or primary_monitor()
    native_auto=native is None
    if native_auto:
        native=len(monitor)>6 and bool(monitor[6]) and not rov and not msaa
    env={}
    env.update(launch_env())
    if full_rate_mirror:
        env['FM4_HALF_RATE_MIRROR']='0'
    if shadow_workaround:
        # Skips the shadow map pass outside the cockpit in races, as split screen does: the
        # xenia-canary/game-patches#398 workaround, no longer needed on this runtime.
        env['FM4_SHADOW_WORKAROUND']='1'
    look=DEFAULT_LOOK if look is None else look
    # One pass with 2x MSAA on the native path (d3d-hle.md 16.17): the title's 2x scenario made
    # one 1280x720 tile, and the runtime's private scene depth. -Native runs it unless
    # -MsaaSamples 4 (one pass with 4x), 0 (one tile without MSAA), -SingleTile or -Msaa says
    # otherwise.
    if msaa_samples is not None and rov:
        raise ValueError("-MsaaSamples is the native path's one-pass MSAA; -Rov cannot run it.")
    if msaa_samples is not None and not native:
        raise ValueError("-MsaaSamples is the native path's one-pass MSAA: use it with -Native.")
    if native and msaa_samples is None and not single_tile and not msaa and not rov:
        msaa_samples=2
    one_pass=bool(msaa_samples)
    single_tile=not one_pass and (single_tile or msaa_samples==0 or auto_single_tile(look,rov,msaa))
    settings=race_settings(DEFAULT_QUALITY if quality is None else quality,single_tile or one_pass)
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
        # The uncorrected reference keeps the title's ring index wait as it is.
        cvars.update({'render_target_path_d3d12':'rov','rov_occlusion_async':'all',
                      'guest_ring_wait_sleep':'false'})
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
    description+=upscale_description
    present,frame_rate,present_description=present_settings(vsync,frame_rate,monitor)
    cvars.update(present)
    description+=f'; {present_description}'
    if native_auto and native:
        description+='; native path for an integrated GPU'
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
    cvars.update(look_cvars(root,look,shader_dev))
    track_settings=look_track_settings(look_knobs(look))
    if track_settings:
        env['FM4_TRACK_SETTINGS']=track_settings
    cvars.update(ao_cvars(root,ao,shader_dev))
    # Texture dumps and replacements by content hash (docs/analysis/texture-replacement.md).
    if texture_dump:
        cvars['texture_dump_path']=Path(texture_dump).resolve()
    if texture_replace:
        if not Path(texture_replace).is_dir():
            raise ValueError(f'-TextureReplace is not a directory: {texture_replace}')
        cvars['texture_replace_path']=Path(texture_replace).resolve()
    if content_manifest:
        env['FM4_CONTENT_MANIFEST']=str(content_manifest)
    # The physical controller's options, and the keyboard's.
    cvars.update(pad_cvars(pad))
    cvars.update(keyboard_cvars(keyboard))
    # The settings overlay: F2 in game, shown at start with -Info.
    env['FM4_INFO']=info_overlay(cvars,env,look,DEFAULT_QUALITY if quality is None else quality,
                                 description)
    if info:
        env['FM4_INFO_SHOW']='1'
    return cvars,env,description

# The GUI launcher (work/fm4-launcher, docs/running.md#gui-launcher): fm4_launcher.exe beside
# fm4_rex.exe, and launcher.json, the launcher's tables, beside game-build. The tables come from
# this file, so the launcher has no rules of its own beyond launch_plan's.
LAUNCHER_DIRECTORY=REPO/'work/fm4-launcher'
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
            'quality':{'presets':QUALITY_PRESETS,'default':DEFAULT_QUALITY},
            'displays':{name:list(size) for name,size in DISPLAYS.items()},
            'default_display':DEFAULT_DISPLAY,'max_resolution_scale':MAX_RESOLUTION_SCALE,
            'vsync':{'modes':list(VSYNC_MODES),'default':DEFAULT_VSYNC},
            'upscale':{'modes':UPSCALE_MODES,'default':DEFAULT_UPSCALE},
            'pad':{'knobs':PAD_KNOBS},'keyboard':{'knobs':KEYBOARD_KNOBS},
            'gtao':passes_settings(PASSES_DIRECTORY,{}),
            'releases':{name:{'media_id':info['media_id'],'executables':info['executables']}
                        for name,info in RELEASES.items()},
            'content':{'title_id':TITLE_ID,'content_type':MARKETPLACE_CONTENT,
                       'magics':[magic.decode() for magic in STFS_MAGICS],
                       'content_type_offset':CONTENT_TYPE_OFFSET,'title_id_offset':TITLE_ID_OFFSET}}

def stage_launcher_files(directory):
    """launcher.json, and the look mods and post passes that launch copies before each run, into
    directory (the BuildRoot or an export folder): the launcher runs them from there."""
    directory=Path(directory)
    (directory/'launcher.json').write_text(json.dumps(launcher_data(),indent=1),encoding='utf-8')
    for name,source,suffixes in (('look',LOOK_DIRECTORY,('.hlsli',)),
                                 ('passes',PASSES_DIRECTORY,('.toml','.hlsl','.hlsli'))):
        shutil.rmtree(directory/name,ignore_errors=True)
        (directory/name).mkdir()
        for path in source.iterdir():
            if path.suffix in suffixes:
                shutil.copyfile(path,directory/name/path.name)

def build_launcher(root,jobs):
    """fm4_launcher.exe (work/fm4-launcher) against the installed runtime's SDL3 and the runtime
    source's Dear ImGui and JSON header, into game-build; its files into the BuildRoot."""
    build=root/'launcher-build'
    run(['cmake','-S',LAUNCHER_DIRECTORY,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
         '-DCMAKE_CXX_COMPILER=clang++','-DCMAKE_C_COMPILER=clang',
         '-DCMAKE_CXX_FLAGS=-march=x86-64-v2','-DCMAKE_C_FLAGS=-march=x86-64-v2',
         f'-DCMAKE_PREFIX_PATH={root/"runtime-install"}',
         f'-DFM4_THIRDPARTY={root/"runtime-source/thirdparty"}'],log=root/'logs/launcher-configure.log')
    run(['cmake','--build',build,'--parallel',jobs],log=root/'logs/launcher-build.log')
    shutil.copyfile(build/'fm4_launcher.exe',root/'game-build/fm4_launcher.exe')
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
            {'native':True,'look':'pc,aa=fsr,sharpen=0.6','info':True},
            {'frame_rate':120,'vsync':'on'},
            {'pad':'driver=rawinput,trigger_rumble=1,rumble=1.5'},
            {'pad':'driver=wgi,thread=1,stick_deadzone=0.08,anti_deadzone=0.24,stick_curve=1.4,'
                   'stick_outer=0.95,trigger_deadzone=0.05,trigger_outer=0.9,'
                   'trigger_rumble_strength=2,misc1=back,right_paddle1=none,touchpad=start'},
            {'pad':'driver=default,rumble=1.0,misc1=guide,thread=0','frame_rate':240},
            {'keyboard':'smooth=0'},
            {'keyboard':'steer_attack=0.5,steer_release=0.25,throttle_attack=0,brake_release=0.3,'
                        'precision=0.6,title_deadzones=0','frame_rate':240},
            {'keyboard':'mouse_steering=1,mouse_steer_sensitivity=2.5,mouse_steer_centering=0,'
                        'smooth=1,steer_attack=0.3','pad':'rumble=1.5'},
            {'native':True,'frame_rate':240,'vsync':'off'},
            {'native':False},
            {'msaa':True},
            {'look':'aces,foliage_hue=-60,sky=1.2,road_gloss=0.5,sun=0,tonemap=filmic,taa_point=1',
             'supersample':True},
            {'look':'neutral,windshield=2,ao=0','resolution_scale':3,'display':'4k'},
            {'look':'original,taa=0,sharpen=0.5','ao':True,'texture_dump':str(root/'logs'),
             'texture_replace':str(root/'cache')},
            {'look':'pc,clearcoat=0,sky=1,exposure=1e-05','quality':'xbox','display':'1440p'},
            {'look':'original,taa=0,sun=2.5','msaa':True,'frame_rate':240,'supersample':True},
            {'native':True,'resolution_scale':'panel'},
            {'native':True,'supersample':True},
            {'native':True,'display':'1080p'},
            {'native':True,'resolution_scale':2,'display':'1440p'},
            {'resolution_scale':'panel','msaa_samples':2,'native':True,'display':'720p'},
            {'native':True,'look':'pc,aa=fsr','upscale':'quality','display':'4k'},
            {'native':True,'look':'pc,aa=fsr','upscale':'balanced','display':'1440p'},
            {'native':True,'look':'pc,aa=fsr','upscale':'performance'},
            {'native':True,'look':'pc,aa=fsr','upscale':'1.25','display':'1080p'}]

def launcher_tests(root):
    """Compares fm4_launcher.exe's port of launch_plan, disc check and content search with this
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
    (work/'cases.json').write_text(json.dumps({'root':str(root),'disc1':disc1,
                                               'manifest':str(manifest),'plans':cases,
                                               'discs':discs},indent=1),encoding='utf-8')
    (work/'result.json').unlink(missing_ok=True)
    run([root/'game-build/fm4_launcher.exe','--test',work/'cases.json',work/'result.json'],
        cwd=root,log=root/'logs/launcher-tests.log')
    result=json.loads((work/'result.json').read_text(encoding='utf-8'))
    if result.get('failures'):
        raise RuntimeError('Launcher tests failed:\n'+'\n'.join(result['failures'][:20]))
    print(f'Launcher tests passed: {result["plans"]} plans, discs and {len(discs["packages"])} content packages.')

def export(root,out,jobs):
    """A folder that runs the game with fm4_launcher.exe alone (docs/running.md#export): the
    launcher, game-build's executable, modules and runtime, the look mods, the post passes, the
    shader cache and launcher.json. Not user-data: the profile, saves and imported content stay,
    and the launcher of the folder imports Disc 2 and the content packages on its first run.
    The Disc and content paths of local.json become the launcher's first settings."""
    report=json.loads((root/'build-result.json').read_text())
    game=root/'game-build'
    for name,expected in report['artifacts'].items():
        if digest(game/name)!=expected:
            raise RuntimeError(f'Build artifact changed: {name}. Rebuild first.')
    if not (game/'fm4_launcher.exe').is_file():
        raise RuntimeError('Run build first: game-build has no fm4_launcher.exe.')
    out=Path(out).resolve()
    if out==root or root in out.parents or out in root.parents or out==REPO or REPO in out.parents \
            or out in REPO.parents:
        raise ValueError('The export folder must be outside the BuildRoot and the repository.')
    if out.exists() and any(out.iterdir()) and not (out/'launcher.json').is_file():
        raise ValueError(f'{out} is not empty and is not an earlier export.')
    (out/'game-build').mkdir(parents=True,exist_ok=True)
    for name in report['artifacts']:
        shutil.copyfile(game/name,out/'game-build'/name)
    shutil.copyfile(game/'fm4_launcher.exe',out/'fm4_launcher.exe')
    shutil.copyfile(root/'build-result.json',out/'build-result.json')
    # The licenses of the third-party code in the runtime (ReXGlue/Xenia, and AMD FidelityFX
    # and Microsoft's d3dx12.h when it is built with REX_FSR).
    shutil.copytree(NOTICES,out/'third-party-notices',dirs_exist_ok=True)
    stage_launcher_files(out)
    if (root/'cache').is_dir():
        shutil.copytree(root/'cache',out/'cache',dirs_exist_ok=True)
    settings=out/'launcher-settings.json'
    if not settings.is_file():
        state=json.loads((root/'local.json').read_text())
        settings.write_text(json.dumps({'disc1':state['disc1'],'disc2':state['disc2'] or '',
                                        'content':state.get('content') or []},indent=1),
                            encoding='utf-8')
    print(f'Exported to {out}. Run fm4_launcher.exe there; it imports Disc 2 and the content '
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
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['doctor','setup','generate','build','all','run','warmcache',
                                          'profbuild','export'])
    parser.add_argument('--build-root',type=Path,default=REPO/'work/portable-fm4')
    parser.add_argument('--disc1',type=Path)
    parser.add_argument('--disc2',type=Path)
    parser.add_argument('--content',type=Path,action='append',
                        help='generate, all, run: an FM4 content package (DLC) or a directory searched '
                             'for them; repeatable. Saved in local.json and imported at the next run')
    parser.add_argument('--jobs',type=int,default=4)
    parser.add_argument('--out',type=Path,
                        help='export: the folder to export the game and its GUI launcher to')
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
                             'the output scale; needs -Native and aa=fsr in -Look')
    parser.add_argument('--keyboard',
                        help="run: the keyboard's steering and pedal model as knobs, for example "
                             "'steer_attack=0.4,precision=0.6' (KEYBOARD_KNOBS)")
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
    parser.add_argument('--quality',choices=list(QUALITY_PRESETS),
                        help='run: render quality of races (default '+DEFAULT_QUALITY+'; xbox: the '
                             "title's own)")
    parser.add_argument('--shadow-workaround',action='store_true',
                        help='run: no shadow map outside the cockpit in races (the old Xenia workaround)')
    parser.add_argument('--rov',action='store_true',
                        help='run: the slower pixel shader interlock render target path instead of RTV')
    parser.add_argument('--native',action='store_const',const=True,default=None,
                        help='run: the native path, the host render thread instead of the command '
                             'processor thread (the default with integrated GPUs only; '
                             'docs/analysis/d3d-hle.md)')
    parser.add_argument('--rtv',action='store_const',const=False,dest='native',
                        help='run: the PM4 path on host render targets (RTV) even with integrated GPUs '
                             'only, where --native is the default')
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
    parser.add_argument('--flavor',default='',help='opt-in compiler experiment for build: comma list of '+','.join(FLAVORS))
    args=parser.parse_args()
    root=args.build_root.resolve()
    # Never write private outputs into the tracked deliverables/source directories.
    if root in (REPO,*REPO.parents) or any(root==REPO/d or REPO/d in root.parents for d in ('outputs','work/fm4-rex','work/runtime')):
        raise ValueError('BuildRoot must be a dedicated private directory, e.g. work/portable-fm4 or D:/FM4-build.')
    if args.jobs<1 or args.jobs>64:
        raise ValueError('jobs must be between 1 and 64')
    flavor=tuple(sorted({f for f in args.flavor.split(',') if f}))
    if set(flavor)-set(FLAVORS) or {'pgo-gen','pgo-use'}<=set(flavor):
        raise ValueError(f'flavor must be a comma list of {FLAVORS}, not both pgo-gen and pgo-use')
    if args.ffx_mirror: os.environ['FFX_MIRROR']=args.ffx_mirror
    fxc=doctor()
    print(ffx_status(root))
    if args.action=='doctor': return
    discs=None
    if args.content and args.action not in ('generate','all','run'):
        raise ValueError('--content is read by generate, all and run only.')
    if args.action in ('generate','all'):
        discs=validate_discs(args.disc1,args.disc2)
        validate_content(discs[1],args.content)
    root.mkdir(parents=True,exist_ok=True)
    check_layout(root)
    fsr=None if args.fsr is None else args.fsr=='on'
    if args.action in ('setup','all'): setup(root,fxc,args.jobs,fsr)
    if args.action in ('generate','all'): generate(root,*discs,args.content,args.jobs)
    if args.action=='run' and args.content: save_content(root,args.content)
    if args.action in ('build','all'): build_game(root,fxc,args.jobs,flavor,None if args.action=='all' else fsr)
    if args.action=='run': launch(root,args.single_tile,args.fixed_vblank,args.frame_rate,args.rov,
                                     args.resolution_scale,args.full_rate_mirror,args.look,
                                     args.shadow_workaround,args.quality,args.display,
                                     args.fullscreen,args.supersample,args.info,args.ao,
                                     args.shader_dev,args.texture_dump,args.texture_replace,args.msaa,
                                     args.native,args.msaa_samples,args.vsync,args.pad,
                                     args.upscale,args.keyboard)
    if args.action=='warmcache': warm_cache(root)
    if args.action=='profbuild': build_profiling(root,fxc,args.jobs)
    if args.action=='export':
        if not args.out:
            raise ValueError('export needs --out (Build.ps1 -Out), the folder to export to.')
        export(root,args.out,args.jobs)

if __name__=='__main__':
    try: main()
    except (OSError,ValueError,RuntimeError,subprocess.CalledProcessError) as error:
        sys.exit(f'ERROR: {error}')
