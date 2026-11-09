"""Mods converted from the player's own Forza Motorsport 2 or 3 discs: their cars, and FM3's
tracks (docs/running-mods.md): the mods action and the mod Disc of run. pipeline.py imports it; it
reads pipeline.py's pins and helpers through the pipeline module, as builder.py does.

The conversion is fm4-content-converter (GPL-3.0-only, a separate program): GitHub's archive of
CONVERTER_COMMIT, checked by its tree like the third-party archives (pipeline.archive_tree). It
runs on this Python with NumPy and Pillow from pinned wheels, with carbin-cpu, the converter's
model helper, built here from GitHub's archive of ForzaEditor and the C# compiler of the .NET
Framework that Windows ships, and with its LZX decoder (tools/lzx_bridge) built by the build's
clang (llvm-mingw) from the SDK's libmspack. Every download is kept in <BuildRoot>/downloads.

A mod is a folder <BuildRoot>/mods/<id> with the converter's mod.json and files/ (FM4 Disc 1
layout; the converter's docs/mod-format.md). Mods stack like DLC packs: each is built on the disc
that Disc 1 and the mods before it make (the converter's --fm4 and --base-package), so its
database, names and archives hold theirs too, and its built_against records that disc. run plays
them from <BuildRoot>/mod-disc: Disc 1's files hard-linked (copied when Disc 1 is on another
volume), and each file the mods write copied from the last mod that writes it, so Disc 1 itself
is never written.
"""
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request
import zipfile

import pipeline as p

CONVERTER_REPOSITORY='MotorsportDX/fm4-content-converter'
CONVERTER_COMMIT='114e4655a2ae01f6ee199a049a22186781e90b93'
CONVERTER_TREE='74e9bc7869c18c64c0e459bcbb8fa9ebf82423c185c1f825969965d5f350d28e'
FORZA_EDITOR_REPOSITORY='FMdangaina/ForzaEditor'
FORZA_EDITOR_COMMIT='8807c7d137646dab8d9eecab7e8d5a6602ef58aa'
FORZA_EDITOR_TREE='9683da6d13127e2f35828e075c186e4ba68093c0d1b4a91431bdc46d76d42b94'
# The converter's optional dependencies ([project.optional-dependencies] full), one wheel per
# CPython of the pipeline's supported range: (file, bytes, SHA-256) under PYPI_URL.
PYPI_URL='https://files.pythonhosted.org/packages/{path}'
WHEELS={
    'cp311':(('aa/44/9fe81ae1dcc29c531843852e2874080dc441338574ccc4306b39e2ff6e59/numpy-2.3.5-cp311-cp311-win_amd64.whl',
              13078442,'a414504bef8945eae5f2d7cb7be2d4af77c5d1cb5e20b296c2c25b61dff2900c'),
             ('4d/42/aaca386de5cc8bd8a0254516957c1f265e3521c91515b16e286c662854c4/pillow-12.0.0-cp311-cp311-win_amd64.whl',
              6999256,'b583dc9070312190192631373c6c8ed277254aa6e6084b74bdd0a6d3b221608e')),
    'cp312':(('2d/57/8aeaf160312f7f489dea47ab61e430b5cb051f59a98ae68b7133ce8fa06a/numpy-2.3.5-cp312-cp312-win_amd64.whl',
              12782922,'86945f2ee6d10cdfd67bcb4069c1662dd711f7e2a4343db5cecec06b87cf31aa'),
             ('a2/0b/d87733741526541c909bbf159e338dcace4f982daac6e5a8d6be225ca32d/pillow-12.0.0-cp312-cp312-win_amd64.whl',
              7001107,'9fe611163f6303d1619bbcb653540a4d60f9e55e622d60a3108be0d5b441017a')),
    'cp313':(('0c/88/e2eaa6cffb115b85ed7c7c87775cb8bcf0816816bc98ca8dbfa2ee33fe6e/numpy-2.3.5-cp313-cp313-win_amd64.whl',
              12779090,'00dc4e846108a382c5869e77c6ed514394bdeb3403461d25a829711041217d5b'),
             ('6d/2a/dd43dcfd6dae9b6a49ee28a8eedb98c7d5ff2de94a5d834565164667b97b/pillow-12.0.0-cp313-cp313-win_amd64.whl',
              7007477,'4cf7fed4b4580601c4345ceb5d4cbf5a980d030fd5ad07c4d2ec589f95f09905')),
    'cp314':(('a3/2e/235b4d96619931192c91660805e5e49242389742a7a82c27665021db690c/numpy-2.3.5-cp314-cp314-win_amd64.whl',
              12919388,'e6a0bc88393d65807d751a614207b7129a310ca4fe76a74e5c7da5fa5671417e'),
             ('2b/24/b350c31543fb0107ab2599464d7e28e6f856027aadda995022e695313d94/pillow-12.0.0-cp314-cp314-win_amd64.whl',
              7142916,'8dc232e39d409036af549c86f24aed8273a40ffa459981146829a324e0848b4b')),
}
CSC=Path(os.environ.get('WINDIR','C:/Windows'))/'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
# libmspack of the SDK's third-party sources (setup), whose lzxd.c the LZX decoder links.
LIBMSPACK=Path('runtime-source/thirdparty/libmspack/libmspack/mspack')
GAMES={'fm3':'Forza Motorsport 3','fm2':'Forza Motorsport 2'}
# What the converter converts of each game: a mod <game>-<content> per pair.
CONTENTS={'cars':('fm3','fm2'),'tracks':('fm3',)}
# The files whose hashes chain stacked mods: built_against records the disc's, each mod writes its own.
CHAIN_FILES={'fm4_gamedb_sha256':'Media/DB/gamedb.slt','fm4_zipmanifest_sha256':'Media/zipmanifest.xml'}
# Where the game imports the content packages (Disc 2, -Content): the converter keeps their keys
# and string tables, so a mod is built against them.
CONTENT_ROOT=Path('user-data/0000000000000000/4D530910/00000002')
MODS,TOOLS,CACHE,MOD_DISC='mods','mods-tools','mods-cache','mod-disc'
MOD_DISC_MARKER='.fm4-mod-disc.json'
# Runs a module of the converter with its source and the wheels first on sys.path, also on the
# embeddable Python, whose ._pth file makes it ignore PYTHONPATH.
BOOTSTRAP=('import runpy,sys;sys.path[:0]=sys.argv[1:3];m=sys.argv[3];del sys.argv[1:4];'
           "runpy.run_module(m,run_name='__main__',alter_sys=True)")


def python_tag(version=sys.version_info):
    return f'cp{version[0]}{version[1]}'


def sha256(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()


def fetch(url,path,size,expected,log):
    """path, downloaded from url when missing, checked by size and SHA-256."""
    path=Path(path)
    if not path.is_file():
        path.parent.mkdir(parents=True,exist_ok=True)
        partial=path.with_name(path.name+'.partial')
        with urllib.request.urlopen(url,timeout=300) as response, partial.open('wb') as stream:
            shutil.copyfileobj(response,stream,1<<20)
        partial.replace(path)
        with Path(log).open('a',encoding='utf-8') as stream:
            stream.write(f'{path.name} {path.stat().st_size} bytes from {url}\n')
    if path.stat().st_size!=size or sha256(path)!=expected:
        path.replace(path.with_name(path.name+'.rejected'))
        raise RuntimeError(f'{path.name} is not the pinned file (size {size}, SHA-256 {expected}).')
    return path


def pinned_tree(root,repository,commit,tree,target,log):
    """GitHub's archive of a pinned commit unpacked into target, checked by its tree. A target
    whose marker names the commit is kept. With tree None (a new pin), the fetch stops and names
    the tree to pin."""
    marker=target/'.fm4-commit'
    if marker.is_file() and marker.read_text(encoding='utf-8').strip()==commit:
        return target
    # ARCHIVE_MIRROR, as for the third-party archives: a folder that may hold the archive.
    mirror=os.environ.get('ARCHIVE_MIRROR')
    local=Path(mirror)/p.archive_name(repository,commit) if mirror else None
    archive=p.download_archive(repository,commit,root/'downloads',log,
                               str(local) if local and local.is_file() else None)
    stage=target.with_name(target.name+'.stage')
    if tree is None:
        shutil.rmtree(stage,ignore_errors=True)
        p.unpack_archive(archive,stage,commit)
        raise RuntimeError(f'{repository} {commit} has no pinned tree; its tree is {p.archive_tree(stage)}.')
    p.unpack_checked(archive,stage,commit,tree,None,log)
    (stage/'.fm4-commit').write_text(commit,encoding='utf-8')
    p.place(stage,target)
    return target


def wheels(root,site,log):
    """NumPy and Pillow for this Python in site."""
    tag=python_tag()
    if tag not in WHEELS:
        raise RuntimeError(f'The mods need CPython 3.11 to 3.14; this is {sys.version.split()[0]}.')
    marker=site/'.fm4-wheels'
    names=[Path(path).name for path,_,_ in WHEELS[tag]]
    if marker.is_file() and marker.read_text(encoding='utf-8').split()==names:
        return site
    stage=site.with_name(site.name+'.stage')
    shutil.rmtree(stage,ignore_errors=True)
    stage.mkdir(parents=True)
    for path,size,expected in WHEELS[tag]:
        wheel=fetch(PYPI_URL.format(path=path),root/'downloads'/Path(path).name,size,expected,log)
        with zipfile.ZipFile(wheel) as archive:
            for member in archive.infolist():
                parts=member.filename.split('/')
                if member.filename.startswith('/') or '..' in parts or ':' in member.filename:
                    raise RuntimeError(f'{wheel.name}: refused member {member.filename!r}.')
            archive.extractall(stage)
    (stage/'.fm4-wheels').write_text('\n'.join(names),encoding='utf-8')
    p.place(stage,site)
    return site


def lzx_bridge(root,folder,log):
    """<mods-tools>/lzx/lzx_bridge.dll: the converter's LZX decoder, built with clang."""
    out=folder/'lzx'
    dll=out/'lzx_bridge.dll'
    marker=out/'.fm4-commit'
    if dll.is_file() and marker.is_file() and marker.read_text(encoding='utf-8').strip()==CONVERTER_COMMIT:
        return dll
    mspack=root/LIBMSPACK
    if not (mspack/'lzxd.c').is_file():
        raise RuntimeError(f'{mspack/"lzxd.c"} is missing; run setup first.')
    clang=shutil.which('clang')
    if clang is None:
        raise RuntimeError('The mods build their LZX decoder with clang: give Build.ps1 -MingwRoot '
                           'or -Toolchain, as for the build.')
    shutil.rmtree(out,ignore_errors=True)
    out.mkdir(parents=True)
    p.run([clang,'-shared','-O2','-I',mspack,folder/'converter/tools/lzx_bridge/lzx_bridge.c',
           mspack/'lzxd.c','-o',dll],log=log)
    marker.write_text(CONVERTER_COMMIT,encoding='utf-8')
    return dll


def converter_command(tools,module,*args):
    return [sys.executable,'-I','-c',BOOTSTRAP,str(tools/'converter/src'),str(tools/f'site-{python_tag()}'),
            module,*map(str,args)]


def tools(root,log):
    """The converter, its wheels and carbin-cpu below <BuildRoot>/mods-tools."""
    folder=root/TOOLS
    pinned_tree(root,CONVERTER_REPOSITORY,CONVERTER_COMMIT,CONVERTER_TREE,folder/'converter',log)
    wheels(root,folder/f'site-{python_tag()}',log)
    helper=folder/'carbin-cpu'
    marker=helper/'.fm4-commit'
    if not (marker.is_file() and marker.read_text(encoding='utf-8').strip()==CONVERTER_COMMIT):
        if not CSC.is_file():
            raise RuntimeError(f'The mods build their model helper with {CSC} (.NET Framework 4, '
                               'part of Windows), which is missing.')
        reference=pinned_tree(root,FORZA_EDITOR_REPOSITORY,FORZA_EDITOR_COMMIT,FORZA_EDITOR_TREE,
                              folder/'forza-editor',log)
        shutil.rmtree(helper,ignore_errors=True)
        p.run(converter_command(folder,'fm4_converter.fm3.build_carbin_cpu','--reference',reference,
                                '--out',helper),log=log)
        marker.write_text(CONVERTER_COMMIT,encoding='utf-8')
    lzx_bridge(root,folder,log)
    return folder


def mod_folders(root):
    return sorted(path.parent for path in (root/MODS).glob('*/mod.json'))


def load_mod(folder):
    mod=json.loads((Path(folder)/'mod.json').read_text(encoding='utf-8'))
    if mod.get('format')!='fm4-mod' or mod.get('format_version')!=1:
        raise RuntimeError(f'{folder} is not an fm4-mod package of version 1.')
    return mod


def content_packs(root):
    """The imported content packages as mod.json's built_against lists them."""
    content=root/CONTENT_ROOT
    return [{'path':path.relative_to(content).as_posix(),'size':path.stat().st_size}
            for path in sorted(content.glob('*/Media/DLCZips/*.zip'))] if content.is_dir() else []


def disc_state(disc1):
    """The hashes of Disc 1's files that a mod built on it records (CHAIN_FILES)."""
    return {key:p.digest(Path(disc1)/relative) for key,relative in CHAIN_FILES.items()}


def after(mod,state):
    """The CHAIN_FILES hashes of the disc once mod is installed over the disc of state."""
    files={relative.casefold():entry for relative,entry in mod['files'].items()}
    return {key:files.get(relative.casefold(),{}).get('sha256',state[key]) for key,relative in CHAIN_FILES.items()}


def fits(mod,state):
    built=mod['built_against']
    return all(built[key]==state[key] for key in CHAIN_FILES)


def base_ids(mod):
    """The mods that were installed when mod was built (the converter's base_packages)."""
    return [base['id'] for base in mod.get('base_packages') or ()]


def order(root,disc1,ids,lenient=False):
    """The mods of ids as (folder, mod.json) in install order: each was built on the disc that Disc
    1 and the mods before it make (its built_against), like DLC packs that each add to the one
    database (docs/running-mods.md). Raises RuntimeError on a mod that does not fit; lenient keeps
    the longest chain that fits and returns the others' ids with the reason as a second value."""
    found={folder.name:folder for folder in mod_folders(root)}
    remaining={}
    for mod_id in dict.fromkeys(ids):
        if mod_id not in found:
            raise RuntimeError(f'No mod {mod_id} in {root/MODS}; build it with -Action mods.')
        remaining[mod_id]=(found[mod_id],load_mod(found[mod_id]))
    packs=content_packs(root)
    state,start=disc_state(disc1),disc_state(disc1)
    ordered,dropped=[],{}
    while remaining:
        ready=sorted((mod_id for mod_id,(_,mod) in remaining.items() if fits(mod,state)),
                     key=lambda mod_id:remaining[mod_id][1].get('created',''))
        if len(ready)>1 and not lenient:
            raise RuntimeError(f'Mods {ready[0]} and {ready[1]} were each built on the same disc; build '
                               f'{ready[1]} again with {ready[0]} installed (-Action mods).')
        if not ready:
            mod_id,(_,mod)=min(remaining.items(),key=lambda item:item[1][1].get('created',''))
            missing=[base for base in base_ids(mod) if base not in remaining and
                     base not in {folder.name for folder,_ in ordered}]
            if missing:
                reason=f"Mod {mod_id} was built with {', '.join(missing)} installed; play it with them."
            elif ordered and fits(mod,start):
                placed=', '.join(folder.name for folder,_ in ordered)
                reason=(f'Mods {placed} and {mod_id} were each built on Disc 1 alone; build {mod_id} '
                        f'again with {placed} installed (-Action mods).')
            else:
                reason=(f'Mod {mod_id} was built for another Disc 1 or on other mods; build it again '
                        '(-Action mods).')
            if not lenient:
                raise RuntimeError(reason)
            dropped.update({other:reason if other==mod_id else 'it was built on mods that do not fit'
                            for other in remaining})
            break
        mod_id=ready[0]
        folder,mod=remaining.pop(mod_id)
        if mod['built_against']['content_packs']!=packs:
            reason=(f'Mod {mod_id} was built with other content packages than those imported now; '
                    'build it again (-Action mods).')
            if not lenient:
                raise RuntimeError(reason)
            dropped[mod_id]=reason
            continue
        ordered.append((folder,mod))
        state=after(mod,state)
    return (ordered,dropped) if lenient else ordered


def mod_error(root,mod,disc1):
    """Why mod, with the mods it was built on, cannot be played on this Disc 1 and these content
    packages, or None."""
    try:
        order(root,disc1,[*base_ids(mod),mod['id']])
    except RuntimeError as error:
        return str(error)
    return None


def convert(root,game,disc,disc2=None,progress_event=None,content='cars'):
    """Converts the cars (or the tracks) of FM3 (disc: Disc 1, disc2: Disc 2) or FM2 (disc, disc2:
    the Bonus Disc) that FM4 and the imported content packages lack into <BuildRoot>/mods/<game>-
    <content>. The mod is built on the disc that Disc 1 and the other mods make, so that they all
    play together; rebuilding a mod builds the ones that were built on it again, from the discs
    they record. An earlier mod of that id is replaced only when the new one is complete."""
    if game not in GAMES:
        raise ValueError(f'--mod-game takes {" or ".join(GAMES)}.')
    if game not in CONTENTS.get(content,()):
        raise ValueError(f'{GAMES[game]} has no {content} to convert; --mod-content takes '
                         f'{" or ".join(c for c,games in CONTENTS.items() if game in games)}.')
    if not (root/'local.json').is_file():
        raise RuntimeError('Run generate first: a mod is built against the Disc 1 of the BuildRoot.')
    state=json.loads((root/'local.json').read_text())
    disc1,_=p.validate_discs(state['disc1'],state['disc2'])
    packages=p.validate_content(state['disc2'],state.get('content'))
    if packages and not content_packs(root):
        raise RuntimeError('The content packages are imported at the first run; run the game once, '
                           'then build the mod, so that it keeps their cars and names.')
    log=root/'logs/mods.log'
    log.parent.mkdir(parents=True,exist_ok=True)
    folder=tools(root,log)
    mod_id=f'{game}-{content}'
    # The installed stack: a mod already in it keeps the mods under it; a new one goes on top.
    stack,stale=order(root,disc1,[path.name for path in mod_folders(root)],lenient=True)
    names=[path.name for path,_ in stack]
    base,above=(names[:names.index(mod_id)],stack[names.index(mod_id)+1:]) if mod_id in names else (names,[])
    for other,reason in stale.items():
        if other!=mod_id:
            print(f'Mods: {other} is not installed with the others: {reason}',flush=True)
    out=root/MODS/mod_id
    stage=out.with_name(mod_id+'.new')
    shutil.rmtree(stage,ignore_errors=True)
    stage.parent.mkdir(parents=True,exist_ok=True)
    base_disc=mod_disc(root,disc1,base)
    if base:
        print(f"Mods: building {mod_id} on {', '.join(base)}",flush=True)
    args=['convert','--game',game,'--disc',disc,'--fm4',base_disc,'--helper',folder/'carbin-cpu/carbin-cpu.exe',
          '--out',stage,'--cache',root/CACHE/(game if content=='cars' else f'{game}-{content}'),'--json']
    if content!='cars':
        args+=['--content',content]
    if disc2:
        args+=['--disc2' if game=='fm3' else '--bonus-disc',disc2]
    if (root/CONTENT_ROOT).is_dir():
        args+=['--content-root',root/CONTENT_ROOT]
    for name in base:
        args+=['--base-package',root/MODS/name]
    command=converter_command(folder,'fm4_converter.cli',*args)
    with log.open('a',encoding='utf-8') as stream:
        stream.write(subprocess.list2cmdline(command)+'\n')
        stream.flush()
        env={**os.environ,'FM4_LZX_BRIDGE':str(folder/'lzx/lzx_bridge.dll')}
        process=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=stream,text=True,encoding='utf-8',
                                 env=env)
        done=None
        for line in process.stdout:
            event=json.loads(line)
            kind=event['event']
            if kind=='stage':
                print(f"Mods: {event['message']}",flush=True)
            elif kind in ('car','track'):
                print(f"  [{event['index']}/{event['total']}] {event[kind]}: {event['status']}"
                      +(f" ({event['error'][:160]})" if event.get('error') else ''),flush=True)
                if progress_event:
                    progress_event('step',f"mods:{event[kind]}",
                                   'done' if event['status']=='packaged' else 'failed',
                                   error=event.get('error'))
            elif kind=='done':
                done=event
            elif kind=='error':
                process.wait()
                raise RuntimeError(f"Mod conversion failed: {event['message']} (see {log}).")
        if process.wait() or done is None:
            raise RuntimeError(f'Mod conversion failed (exit {process.returncode}); see {log}.')
    shutil.rmtree(out,ignore_errors=True)
    os.replace(stage,out)
    mod=load_mod(out)
    items=mod.get('tracks') if content=='tracks' else mod['cars']
    print(f"Mod {mod_id}: {mod['packaged']} {content} converted, {mod['failed']} failed, "
          f"{sum(c['status']=='skipped' for c in items or ())} skipped. Play it with -Mod {mod_id}.",flush=True)
    for above_folder,above_mod in above:
        rebuild(root,above_folder,above_mod,progress_event)
    return out


# The converter's disc roles (mod.json sources) and the arguments of convert they come from.
SOURCE_ROLES={'fm3-disc1':'disc','fm2-disc':'disc','fm3-disc2':'disc2','fm2-bonus':'disc2'}


def rebuild(root,folder,mod,progress_event=None):
    """Converts mod again from the discs its mod.json records, on the mods now under it."""
    game=mod.get('source_game','').lower()
    content='tracks' if mod.get('kind')=='track-overlay' else 'cars'
    discs={SOURCE_ROLES[source['role']]:source['path'] for source in mod.get('sources') or ()
           if source.get('role') in SOURCE_ROLES}
    if 'disc' not in discs or not all(Path(path).exists() for path in discs.values()):
        print(f'Mods: {folder.name} was built on the mod just replaced; its discs are not where they were '
              f'({", ".join(discs.values()) or "none recorded"}), so build it again (-Action mods).',flush=True)
        return None
    print(f'Mods: building {folder.name} again on the new mods under it',flush=True)
    return convert(root,game,Path(discs['disc']),Path(discs['disc2']) if 'disc2' in discs else None,
                   progress_event,content)


def verify(folder,mod):
    """mod.json's files: present, of their size and SHA-256, and nothing else in files/."""
    files=Path(folder)/mod['files_root']
    for relative,entry in mod['files'].items():
        parts=relative.split('/')
        if '..' in parts or ':' in relative or relative.startswith('/'):
            raise RuntimeError(f"Mod {mod['id']}: refused path {relative!r}.")
        path=files.joinpath(*parts)
        if not path.is_file() or path.stat().st_size!=entry['size'] or sha256(path)!=entry['sha256']:
            raise RuntimeError(f"Mod {mod['id']}: {relative} is missing or changed; build it again.")
    listed={relative.casefold() for relative in mod['files']}
    extra=[path for path in files.rglob('*') if path.is_file()
           and path.relative_to(files).as_posix().casefold() not in listed]
    if extra:
        raise RuntimeError(f"Mod {mod['id']}: {extra[0]} is not listed in mod.json.")


def mod_disc(root,disc1,ids):
    """The Disc 1 to play with the mods of ids: disc1 itself without mods, else <BuildRoot>/mod-disc,
    built again when Disc 1 or a mod changed. The mods go over Disc 1 in install order (order); a
    file that several write comes from the last, which was built on the others."""
    if not ids:
        return Path(disc1)
    disc1=Path(disc1)
    chosen=order(root,disc1,ids)
    target=root/MOD_DISC
    want={'disc1':str(disc1),'mods':{folder.name:sha256(folder/'mod.json') for folder,_ in chosen}}
    marker=target/MOD_DISC_MARKER
    if marker.is_file() and json.loads(marker.read_text(encoding='utf-8'))==want:
        return target
    for folder,mod in chosen:
        verify(folder,mod)
    replaced={path.casefold():(folder,mod,path) for folder,mod in chosen for path in mod['writes']}
    stage=target.with_name(target.name+'.stage')
    shutil.rmtree(stage,ignore_errors=True)
    copied=linked=0
    for path in sorted(disc1.rglob('*')):
        if not path.is_file():
            continue
        relative=path.relative_to(disc1).as_posix()
        if relative.casefold() in replaced:
            continue
        destination=stage/relative
        destination.parent.mkdir(parents=True,exist_ok=True)
        try:
            os.link(path,destination)
            linked+=1
        except OSError:
            shutil.copyfile(path,destination)
            copied+=1
    for folder,mod,relative in replaced.values():
        destination=stage/relative
        destination.parent.mkdir(parents=True,exist_ok=True)
        # A new file: never written through a link to Disc 1.
        shutil.copyfile(Path(folder)/mod['files_root']/relative,destination)
    (stage/MOD_DISC_MARKER).write_text(json.dumps(want,indent=1),encoding='utf-8')
    shutil.rmtree(target,ignore_errors=True)
    os.replace(stage,target)
    if copied:
        print(f'Mod Disc: {copied} files of Disc 1 copied (Disc 1 is on another volume than the BuildRoot).')
    print(f"Mod Disc: {', '.join(folder.name for folder,_ in chosen)} over Disc 1 ({linked} files linked).")
    return target


def launcher_test_case(work):
    """The case of the launcher's port (mods.cpp) in pipeline.launcher_tests: a synthetic Disc 1 and
    two stacked mods (fm2-cars built with fm3-cars installed) in two BuildRoots. This side prepares
    the mod Disc of python with both; the launcher checks that it accepts it, prepares cpp's, orders
    and lists the mods. launcher_test_check then checks that this side accepts cpp's without
    building it again."""
    work=Path(work)/'mods'
    shutil.rmtree(work,ignore_errors=True)
    disc1=work/'Disc 1'
    for relative,data in {'default.xex':b'xex','Media/db/gamedb.slt':b'database','Media/zipmanifest.xml':b'manifest',
                          'Media/Cars/A.zip':b'native car','Media/UI/Textures.zip':b'textures'}.items():
        (disc1/relative).parent.mkdir(parents=True,exist_ok=True)
        (disc1/relative).write_bytes(data)
    stack=[('fm3-cars','FM3',{'Media/DB/gamedb.slt':b'database with FM3 cars','Media/Cars/X.zip':b'converted car'},
            [{'media':'X','status':'packaged'},{'media':'Y','status':'skipped'}]),
           ('fm2-cars','FM2',{'Media/DB/gamedb.slt':b'database with FM3 and FM2 cars','Media/Cars/Z.zip':b'FM2 car'},
            [{'media':'Z','status':'packaged'}])]
    for root in (work/'python',work/'cpp'):
        pack=root/CONTENT_ROOT/'4d53091000000001/Media/DLCZips/0001_pri_65.zip'
        pack.parent.mkdir(parents=True,exist_ok=True)
        pack.write_bytes(b'downloadable content')
        state,base=disc_state(disc1),[]
        for number,(mod_id,game,files,cars) in enumerate(stack):
            folder=root/MODS/mod_id
            for relative,data in files.items():
                (folder/'files'/relative).parent.mkdir(parents=True,exist_ok=True)
                (folder/'files'/relative).write_bytes(data)
            mod={'format':'fm4-mod','format_version':1,'id':mod_id,'name':f'{game} cars','source_game':game,
                 'created':f'2026-11-0{5+number}T00:00:00+00:00','files_root':'files','packaged':1,'failed':0,
                 'cars':cars,'files':{r:{'sha256':hashlib.sha256(d).hexdigest(),'size':len(d)} for r,d in files.items()},
                 'writes':sorted(files),'base_packages':[{'id':b,'created':'2026-11-05T00:00:00+00:00'} for b in base],
                 'built_against':{**state,'content_packs':content_packs(root)}}
            (folder/'mod.json').write_text(json.dumps(mod,indent=1),encoding='utf-8')
            state,base=after(mod,state),[*base,mod_id]
    ids=['fm2-cars','fm3-cars']
    with contextlib.redirect_stdout(io.StringIO()):
        target=mod_disc(work/'python',disc1,ids)
    expected={path.relative_to(target).as_posix():sha256(path) for path in sorted(target.rglob('*'))
              if path.is_file() and path.name!=MOD_DISC_MARKER}
    return {'python':str(work/'python'),'cpp':str(work/'cpp'),'disc1':str(disc1),'ids':ids,
            'order':[folder.name for folder,_ in order(work/'python',disc1,ids)],'files':expected}


def launcher_test_check(case):
    """After the launcher's run: this side reuses the mod Disc that the launcher prepared."""
    root=Path(case['cpp'])
    marker=root/MOD_DISC/MOD_DISC_MARKER
    if not marker.is_file():
        raise RuntimeError('Launcher tests: the launcher prepared no mod Disc.')
    stamp=marker.stat().st_mtime_ns
    with contextlib.redirect_stdout(io.StringIO()):
        mod_disc(root,Path(case['disc1']),case['ids'])
    if marker.stat().st_mtime_ns!=stamp:
        raise RuntimeError("Launcher tests: pipeline.py built the launcher's mod Disc again.")
