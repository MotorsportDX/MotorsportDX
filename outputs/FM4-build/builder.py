"""The builder installer's side of the pipeline (docs/analysis/builder-installer.md): the builder's
downloads that launcher.json lists, the download action, the package action and its allowlist, and
the launcher tests of the launcher's builder mode. pipeline.py imports it; it reads pipeline.py's
pins and helpers through the running pipeline module."""
import contextlib
import io
import os
from pathlib import Path
import shutil
import struct
import sys
import urllib.parse
import zipfile

import pipeline as p

# The builder installer (docs/analysis/builder-installer.md 5 and 6): the folders below the install
# root that the package's launcher builds in, and every file it fetches on the player's computer.
# launcher.json carries both (launcher_data), so the launcher lists each download with its host,
# size and licence before anything is fetched. Paths are relative to the install root. The
# launcher fetches Python itself; the download action fetches the others into the places where
# the toolchain action and setup find them, so that neither fetches anything afterwards.
BUILDER_LAYOUT={'source':'source','build':'build','toolchain':'toolchain','downloads':'downloads',
                'logs':'logs'}
BUILDER_ITEMS=('python','cmake','ninja','llvm-mingw','sources','gameinput','dxc')
# The launcher's build steps in order (python is its own unpack; the others are actions of
# pipeline.py), with the seconds each took on the development laptop (20 threads, -Jobs 8, every
# download local), which the launcher shows as its estimates.
BUILDER_STEPS=(('python',5),('preflight',2),('toolchain',10),('setup',210),('generate',36),('build',173))
TOOL_NAMES={'python':'Python','cmake':'CMake','ninja':'Ninja','llvm-mingw':'llvm-mingw'}
TOOL_PURPOSES={'python':'Runs the build steps (the embeddable package: unpacked, nothing installed).',
               'cmake':'Configures the builds.',
               'ninja':'Runs the compiles of each build.',
               'llvm-mingw':'The compiler and linker (Clang 20.1.8 and LLD), with the C and C++ '
                             'libraries and the Windows headers the game is built against.'}
# A licence page on the web, or a file of the package (relative to the install root).
TOOL_LICENSE_LINKS={'python':'https://docs.python.org/3/license.html',
                    'cmake':'https://cmake.org/licensing/',
                    'ninja':'https://github.com/ninja-build/ninja/blob/master/COPYING',
                    'llvm-mingw':'third-party-notices/LLVM-LICENSE.txt'}
# The 15 archives of thirdparty_archives held 95,687,405 bytes on 31 October 2026: their size is
# not pinned (their unpacked files are, docs/setup.md#fetching-without-git).
THIRDPARTY_ARCHIVE_BYTES=95_687_405
GAMEINPUT_SIZE=2_143_539
DXC_SIZE=32_287_592

def builder_downloads(path=p.TOOLCHAIN):
    """The builder's downloads in BUILDER_ITEMS' order: id, name, what it is for, host, url, size
    (exact, or the last measured), sha256 (None where the unpacked files are pinned), licence and
    a link to its text, the files it leaves (files), the folder they are unpacked into, and for a
    tool the marker of install_tool, which the launcher writes for the Python it unpacks."""
    layout,tools=BUILDER_LAYOUT,p.load_toolchain(path)
    items=[]
    for name,tool in tools.items():
        items.append({'id':name,'name':f'{TOOL_NAMES[name]} {tool["version"]}','what':TOOL_PURPOSES[name],
                      'host':urllib.parse.urlsplit(tool['url']).hostname,'url':tool['url'],
                      'size':tool['size'],'exact':True,'sha256':tool['sha256'],'license':tool['license'],
                      'license_link':TOOL_LICENSE_LINKS[name],
                      'files':[f'{layout["downloads"]}/{tool["url"].rsplit("/",1)[1]}'],
                      'folder':f'{layout["toolchain"]}/{name}-{tool["version"]}',
                      'marker':{'sha256':tool['sha256'],'top':tool['top'],'keep':tool['keep']}})
    build=layout['build']
    items.append({'id':'sources','name':"The runtime's third-party sources",
                  'what':"The ReXGlue SDK's vendored libraries and the 13 submodules the runtime is built "
                         'from (SDL, FFmpeg, Dear ImGui, fmt and others): 15 GitHub archives of their '
                         'pinned commits.',
                  'host':urllib.parse.urlsplit(p.ARCHIVE_URL).hostname,
                  'url':p.ARCHIVE_URL.format(repository=p.SDK_REPOSITORY,commit=p.SDK_REV),
                  'size':THIRDPARTY_ARCHIVE_BYTES,'exact':False,'sha256':None,
                  'license':'BSD-3-Clause, MIT, zlib, BSL-1.0, BSD-2-Clause, LGPL-2.1-or-later (FFmpeg, '
                            'libmspack), GPL-2.0-or-later (the disassembler, in rexglue.exe only)',
                  'license_link':'third-party-notices/README.txt',
                  'files':[f'{build}/downloads/{p.archive_name(repository,commit)}'
                           for _,repository,commit,_,_ in p.thirdparty_archives()],
                  'folder':f'{build}/runtime-source/thirdparty','marker':None})
    package=f'microsoft.gameinput.{p.GAMEINPUT_VERSION}.nupkg'
    items.append({'id':'gameinput','name':f'Microsoft.GameInput {p.GAMEINPUT_VERSION}',
                  'what':'Two MIT files of the package (the GameInput API v3 header and its loader) for '
                         "the controllers' GameInput driver; the rest of the package is not used.",
                  'host':urllib.parse.urlsplit(p.GAMEINPUT_URL).hostname,'url':p.GAMEINPUT_URL,
                  'size':GAMEINPUT_SIZE,'exact':True,'sha256':None,
                  'license':"MIT (the two files used); the rest of the package under Microsoft's terms, "
                            'not extracted',
                  'license_link':'third-party-notices/Microsoft-GameInput-LICENSE.txt',
                  'files':[f'{build}/gameinput-sdk/{p.GAMEINPUT_VERSION}/{package}'],
                  'folder':f'{build}/gameinput-sdk/{p.GAMEINPUT_VERSION}','marker':None})
    items.append({'id':'dxc','name':f'DirectX Shader Compiler {p.DXC_VERSION}',
                  'what':"Microsoft's shader compiler for the native path's shaders: its two DLLs go "
                         'beside the game, on this computer only.',
                  'host':urllib.parse.urlsplit(p.DXC_URL).hostname,'url':p.DXC_URL,'size':DXC_SIZE,
                  'exact':True,'sha256':p.DXC_SHA256,
                  'license':"Microsoft's DirectX Shader Compiler terms, NCSA (LLVM), MIT",
                  'license_link':'third-party-notices/DirectXShaderCompiler-LICENSE-MS.txt',
                  'files':[f'{build}/dxc-download/{p.DXC_VERSION}/{p.DXC_URL.rsplit("/",1)[1]}'],
                  'folder':f'{build}/dxc-download/{p.DXC_VERSION}','marker':None})
    return items

def download(root,items,downloads):
    """The download action: the builder's downloads of items (BUILDER_ITEMS, or all), each a
    --progress step download-<id>: a tool's archive into downloads (fetch_tool, TOOLCHAIN_MIRROR
    first), the sources' archives into <BuildRoot>/downloads (ARCHIVE_MIRROR first), GameInput and
    DXC into the BuildRoot (GAMEINPUT_MIRROR, DXC_MIRROR). Files already there with their pins
    are kept."""
    unknown=set(items)-{'all',*BUILDER_ITEMS}
    if unknown:
        raise ValueError(f'download: unknown item {", ".join(sorted(unknown))}; one of '
                         f'{", ".join(BUILDER_ITEMS)} or all.')
    tools=p.load_toolchain()
    wanted=[item for item in BUILDER_ITEMS if item in items or 'all' in items]
    if downloads is None and set(wanted)&set(tools):
        raise ValueError('download needs --downloads or --toolchain for the tools.')
    log=root/'logs/downloads.log'
    log.parent.mkdir(parents=True,exist_ok=True)
    for item in wanted:
        with p.progress('step',f'download-{item}',log):
            if item in tools:
                p.fetch_tool(item,tools[item],downloads,log,os.environ.get('TOOLCHAIN_MIRROR'))
            elif item=='sources':
                p.download_archives(root,log)
            elif item=='gameinput':
                p.fetch_gameinput(root)
            elif item=='dxc':
                p.fetch_dxc(root)
            else:
                raise ValueError(f'download: {item} is not a tool of toolchain.json.')

# The package action (docs/analysis/builder-installer.md 3.1, increment B6): one zip of the
# builder package at a commit, MotorsportDX-<describe>.zip with one top folder: the launcher
# built at the commit, launcher.json, toolchain.json, the source snapshot, the licences and
# notices and the player's README. package_problems is its allowlist: nothing of a BuildRoot,
# no program but the launcher (which must import nothing but Windows' own DLLs), no picture, no
# DirectX Shader Compiler and nothing else of Microsoft's, and a size limit.
PACKAGE_FILES=(p.LAUNCHER_EXE,'launcher.json','toolchain.json','LICENSE.txt','CREDITS.txt','README.txt')
PACKAGE_LIMIT=40*2**20
PACKAGE_PICTURES=('.png','.jpg','.jpeg','.bmp','.gif','.tga','.dds','.webp','.ico','.tif','.tiff')
PACKAGE_BINARIES=('.exe','.dll','.pdb','.lib','.a','.obj','.o','.sys','.msi','.cab','.nupkg','.zip',
                  '.7z','.gz','.xz','.tar','.iso','.xex','.bin','.cso','.dxil','.spv')
# Names that only a BuildRoot, an install, DXC or Microsoft's packages hold.
PACKAGE_NAMES=('build-result.json','local.json','setup-complete.json','launcher-settings.json',
               'toolchain-state.json','builder-state.json','content-packages.txt','dxcompiler.dll',
               'dxil.dll','dxc.exe','d3dcompiler_47.dll','vcruntime140.dll','vcruntime140_1.dll',
               'msvcp140.dll','gameinputredist.msi','libc++.dll','libunwind.dll')
# The launcher's imports that are not Windows' own: llvm-mingw's C++ runtime and Microsoft's.
LAUNCHER_FOREIGN_IMPORTS=('libc++.dll','libunwind.dll','libwinpthread-1.dll','vcruntime140.dll',
                          'vcruntime140_1.dll','msvcp140.dll','ucrtbased.dll')

def pe_imports(data):
    """The DLL names a PE image imports, delay-loaded ones included."""
    pe=struct.unpack_from('<I',data,0x3C)[0]
    if data[pe:pe+4]!=b'PE\0\0':
        raise ValueError('not a PE image')
    sections,optional_size=struct.unpack_from('<H',data,pe+6)[0],struct.unpack_from('<H',data,pe+20)[0]
    optional=pe+24
    directories=optional+(112 if struct.unpack_from('<H',data,optional)[0]==0x20B else 96)
    table=[struct.unpack_from('<8xIIII',data,optional+optional_size+40*i) for i in range(sections)]
    def offset(rva):
        for size,address,raw_size,raw in table:
            if address<=rva<address+max(size,raw_size):
                return rva-address+raw
        raise ValueError(f'RVA {rva:#x} is in no section')
    def name(rva):
        at=offset(rva)
        return data[at:data.index(b'\0',at)].decode('ascii')
    names=[]
    # The import directory (1): 20-byte descriptors, the name at 12; the delay-load directory
    # (13): 32-byte descriptors, the name at 4.
    for index,size,field in ((1,20,12),(13,32,4)):
        rva=struct.unpack_from('<I',data,directories+8*index)[0]
        if not rva:
            continue
        at=offset(rva)
        while any(data[at:at+size]):
            names.append(name(struct.unpack_from('<I',data,at+field)[0]))
            at+=size
    return names

def package_problems(entries,tracked,total):
    """What package_problems' allowlist refuses in a package, as sentences: entries are (path below
    the top folder, its first bytes), tracked the files of the commit (source/ must hold them and
    source-stamp.json only), total the zip's size. The launcher's imports are checked by the
    caller (pe_imports)."""
    problems=[]
    paths={path for path,_ in entries}
    for path,head in entries:
        top,_,rest=path.partition('/')
        suffix=Path(path).suffix.lower()
        if top=='source' and rest:
            if rest!=p.STAMP_NAME and rest not in tracked:
                problems.append(f'{path} is not a file of the commit.')
        elif top=='third-party-notices' and rest and '/' not in rest:
            if suffix!='.txt':
                problems.append(f'{path} is not a notice.')
        elif path not in PACKAGE_FILES:
            problems.append(f'{path} is not part of a package.')
        if suffix in PACKAGE_PICTURES:
            problems.append(f'{path} is a picture.')
        if path!=p.LAUNCHER_EXE and (suffix in PACKAGE_BINARIES or head[:2]==b'MZ'):
            problems.append(f'{path} is a program, a library or an archive.')
        if Path(path).name.lower() in PACKAGE_NAMES:
            problems.append(f'{path} belongs to a BuildRoot, an install, DXC or Microsoft.')
    missing=[name for name in PACKAGE_FILES if name not in paths]
    if missing or not any(path.startswith('source/') for path in paths):
        problems.append(f'The package has no {", ".join(missing) or "source"}.')
    if total>PACKAGE_LIMIT:
        problems.append(f'The package holds {total:,} bytes, over the limit of {PACKAGE_LIMIT:,}.')
    return problems

def check_package(path,commit):
    """package_problems over a package zip, and the launcher's imports: only Windows' own DLLs
    (in System32, or an API set). Returns the number of files and the zip's size."""
    tracked=set(p.git_output('ls-tree','-r','--name-only',commit).splitlines())
    system=Path(os.environ.get('SystemRoot',r'C:\Windows'))/'System32'
    with zipfile.ZipFile(path) as package:
        infos=[info for info in package.infolist() if not info.is_dir()]
        tops={info.filename.split('/',1)[0] for info in infos}
        if len(tops)!=1:
            raise RuntimeError(f'{path} must hold one top folder, not {sorted(tops)}.')
        top=tops.pop()+'/'
        entries=[]
        for info in infos:
            with package.open(info) as stream:
                entries.append((info.filename[len(top):],stream.read(2)))
        problems=package_problems(entries,tracked,Path(path).stat().st_size)
        if not problems:
            imports=pe_imports(package.read(top+p.LAUNCHER_EXE))
            foreign=[name for name in imports if name.lower() in LAUNCHER_FOREIGN_IMPORTS or
                     not (name.lower().startswith(('api-ms-win-','ext-ms-')) or (system/name).is_file())]
            if foreign:
                problems.append(f'{p.LAUNCHER_EXE} imports {", ".join(foreign)}, which Windows does not carry.')
    if problems:
        raise RuntimeError(f'{path} fails the package allowlist:\n'+'\n'.join(problems))
    return len(entries),Path(path).stat().st_size

def package(root,out,revision,jobs,toolchain_directory=None):
    """The package action: the snapshot of revision into <out>/package-stage/source; that
    snapshot's own pipeline then runs setup and the launcher action in this BuildRoot (the
    launcher needs no discs) and writes launcher.json; with the commit's toolchain.json, licence,
    credits, third-party notices and package-readme.txt as README.txt, the stage is zipped into
    out/MotorsportDX-<describe>.zip, checked (check_package) and removed. Returns the zip's
    path."""
    if not p.is_git_checkout():
        raise RuntimeError(f'package needs a Git checkout; {p.REPO} is not one.')
    out=Path(out).resolve()
    if out==root or root in out.parents or out==p.REPO or p.REPO in out.parents:
        raise ValueError('The package folder must be outside the BuildRoot and the repository.')
    commit=p.git_output('rev-parse','--verify',f'{revision}^{{commit}}')
    if not commit:
        raise ValueError(f'{revision} is not a commit of {p.REPO}.')
    name=f'MotorsportDX-{p.git_output("describe","--tags","--always",commit)}'
    target=out/f'{name}.zip'
    if target.exists():
        raise ValueError(f'{target} exists already.')
    # The stage lies outside the BuildRoot, which may not hold the snapshot's source. Its pipeline
    # runs with -B: Python's bytecode caches in the source would fail the allowlist.
    stage=out/'package-stage'
    shutil.rmtree(stage,ignore_errors=True)
    p.make_snapshot(stage/'source',commit)
    source=stage/'source'
    pipeline=source/'outputs/FM4-build/pipeline.py'
    common=['--build-root',root,'--jobs',jobs,*(['--toolchain',toolchain_directory] if toolchain_directory else [])]
    p.run([sys.executable,'-I','-B',pipeline,'setup',*common],log=root/'logs/package-setup.log')
    p.run([sys.executable,'-I','-B',pipeline,'launcher',*common,'--out',stage],log=root/'logs/package-launcher.log')
    p.run([sys.executable,'-I','-B',pipeline,'launcher-data','--out',stage],log=root/'logs/package-launcher-data.log')
    for relative,destination in (('outputs/FM4-build/toolchain.json','toolchain.json'),('LICENSE','LICENSE.txt'),
                                 ('outputs/FM4-build/CREDITS.txt','CREDITS.txt'),
                                 ('outputs/FM4-build/package-readme.txt','README.txt')):
        shutil.copyfile(source/relative,stage/destination)
    shutil.copytree(source/'outputs/FM4-source-control/third-party-notices',stage/'third-party-notices')
    out.mkdir(parents=True,exist_ok=True)
    partial=target.with_name(target.name+'.partial')
    with zipfile.ZipFile(partial,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
        for path in sorted(stage.rglob('*')):
            if path.is_file():
                archive.write(path,f'{name}/{path.relative_to(stage).as_posix()}')
    try:
        files,size=check_package(partial,commit)
    except RuntimeError:
        partial.unlink()
        raise
    partial.replace(target)
    shutil.rmtree(stage)
    print(f'Package of {commit[:7]}: {target}, {files} files, {size:,} bytes.')
    return target

def builder_test_cases(state,work):
    """The launcher's builder mode against this file (builder.h): its downloads and steps, the
    progress lines that progress() writes, and errors that the pipeline raises, with the screen
    of the launcher's plain words for each (Explain)."""
    lines=io.StringIO()
    saved,p.PROGRESS=p.PROGRESS,lines
    try:
        with p.progress('stage','setup'):
            with p.progress('step','runtime-build',work/'runtime-build.log'):
                pass
        with contextlib.suppress(RuntimeError):
            with p.progress('stage','build'):
                with p.progress('step','game-build',work/'game-build.log'):
                    raise RuntimeError('Exit 1; log: game-build.log')
    finally:
        p.PROGRESS=saved
    errors=[]
    def error_of(call,step,screen):
        try:
            call()
        except (OSError,ValueError,RuntimeError) as error:
            errors.append([str(error),step,screen])
            return
        raise RuntimeError(f'builder test: {call} raised nothing')
    empty=work/'builder-empty'
    empty.mkdir(exist_ok=True)
    error_of(lambda:p.disc_release(empty),'generate','discs')
    error_of(lambda:p.validate_discs(state['disc1'],empty),'generate','discs')
    bogus={'version':'1','url':(work/'no-such-archive.zip').as_uri(),'size':1,'sha256':'0'*64}
    error_of(lambda:p.fetch_tool('cmake',bogus,work/'builder-downloads',work/'builder-downloads.log'),'toolchain',
             'downloads')
    error_of(lambda:p.run([sys.executable,'-c','raise SystemExit(3)'],log=work/'builder-exit.log'),'setup','')
    return {'items':list(BUILDER_ITEMS),'steps':[step for step,_ in BUILDER_STEPS],
            'progress':lines.getvalue().splitlines(),
            'expected':[['stage','setup','running',False],['step','runtime-build','running',False],
                        ['step','runtime-build','done',False],['stage','setup','done',False],
                        ['stage','build','running',False],['step','game-build','running',False],
                        ['step','game-build','failed',True],['stage','build','failed',True]],
            'errors':errors}

