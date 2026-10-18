"""Compiles HLSL shader overrides with fxc.exe the way the runtime does (shader_override.cpp),
without starting the game: each override after the contract of its translation and the library
work/runtime/src/graphics/d3d12/shader_override.hlsli, FXC ps_5_1 with /Gis, /O3 and unbounded
descriptor tables. Contracts come from a run with --arg dump_shaders=<dir>
(shader_<hash>_<modification>.ps.contract.hlsli); overrides named shader_<hash>.ps.hlsl use
every contract of that hash.

  override_fxc.py <override file or directory> <contracts dir> [--asm <dir>]
      [--mods <shader_mod_path dir> --define NAME=VALUE ...]
  override_fxc.py --stub <contracts dir> --mods <shader_mod_path dir> [--define NAME=VALUE ...]

With --mods, an override whose hash has shader_<hash>.mod.hlsli there is compiled with the mod
included after the library, as shader_mod_path does (with --define as shader_mod_defines).
Prints how many compiled and groups the failures by their first error.

--stub compiles every mod of --mods against every contract of its hash, on a stub override that
writes interpolator 0 plus every 2D texture the contract binds, each fetched at interpolator 0
through XE_HOOK_TFETCH (the hooks run around and inside it as in a generated one). A contract
defines only the fetches and constants its shader reads, so this finds a mod that reads others.
With --define, each is also compiled without the defines: a mod whose DXBC the defines do not
change never reached its code in that translation (an #if on a fetch it does not bind)."""
import argparse
import concurrent.futures
import pathlib
import re
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
LIBRARY = HERE.parents[2] / 'work/runtime/src/graphics/d3d12/shader_override.hlsli'


def find_fxc():
    kits = pathlib.Path(r'C:\Program Files (x86)\Windows Kits\10\bin')
    candidates = sorted(kits.glob('10.*/x64/fxc.exe'))
    if not candidates:
        sys.exit('fxc.exe not found in the Windows 10/11 SDK')
    return candidates[-1]


def stub_override(contract_text):
    """The override of --stub for a contract: interpolator 0 plus a fetch of every 2D texture the
    contract binds, each through XE_HOOK_TFETCH as a generated override fetches, so that the fetch
    hooks of a mod run too."""
    fetches = sorted(int(n) for n in re.findall(r'^#define XE_TEXTURE(\d+)_2D_U ', contract_text,
                                                re.M)
                     if f'#define XE_TEXTURE{n}_2D_S ' in contract_text)
    lines = ['void xe_pixel_shader(inout XePixel p) {', '  float4 sum = p.r[0];']
    lines += [f'  {{ float4 t = XE_HOOK_TFETCH({n}u, XE_TFETCH2D({n}, p.r[0].xy), p.r[0]); '
              'sum += t; }' for n in fetches]
    return '\n'.join(lines + ['  p.color[0] = sum;', '}', ''])


def compile_one(fxc, override, contract, asm_dir, mods, defines):
    """Compiles override (a file, or None for the stub) after contract: (contract file name, FXC exit
    code, error lines, DXBC or None)."""
    with tempfile.TemporaryDirectory() as work:
        work = pathlib.Path(work)
        (work / 'xenos_override.hlsli').write_bytes(LIBRARY.read_bytes())
        mod = ''
        if mods:
            for path in mods.glob('*.hlsli'):
                (work / path.name).write_bytes(path.read_bytes())
            name = f'{contract.name[:23]}.mod.hlsli'
            if (mods / name).is_file():
                mod = f'#include "{name}"\n'
        name, text = (('stub.ps.hlsl', stub_override(contract.read_text())) if override is None
                      else (override.as_posix(), override.read_text()))
        source = (contract.read_text() + '\n#include "xenos_override.hlsli"\n' + mod +
                  f'#line 1 "{name}"\n' + text +
                  '\n#define XE_OVERRIDE_MAIN\n#include "xenos_override.hlsli"\n')
        (work / 'main.hlsl').write_text(source)
        command = [str(fxc), '/nologo', '/T', 'ps_5_1', '/E', 'main', '/Gis', '/O3',
                   '/enable_unbounded_descriptor_tables', '/Fo', str(work / 'main.cso')]
        command += [f'/D{define}' for define in defines]
        if asm_dir:
            command += ['/Fc', str(asm_dir / contract.name.replace('.contract.hlsli', '.asm'))]
        result = subprocess.run(command + [str(work / 'main.hlsl')], capture_output=True,
                                text=True)
        errors = [line for line in (result.stdout + result.stderr).splitlines() if 'error' in line]
        dxbc = (work / 'main.cso').read_bytes() if (work / 'main.cso').is_file() else None
        return contract.name, result.returncode, errors, dxbc


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('overrides', nargs='?', help='an override file or a directory of them')
    parser.add_argument('contracts', nargs='?', help='the dump_shaders directory of a run')
    parser.add_argument('--asm', help='write the FXC disassembly of each to this directory')
    parser.add_argument('--mods', help='a shader_mod_path directory')
    parser.add_argument('--define', action='append', default=[], help='NAME=VALUE for the mods')
    parser.add_argument('--stub', action='store_true',
                        help='compile the mods of --mods on a stub override (the contracts '
                             'directory is then the only positional argument)')
    parser.add_argument('--workers', type=int, default=8)
    options = parser.parse_args()
    if options.stub and options.contracts is None:
        options.overrides, options.contracts = None, options.overrides
    if options.contracts is None or options.stub != (options.overrides is None):
        parser.error('give an override and a contracts directory, or --stub and a contracts '
                     'directory')
    if options.stub and not options.mods:
        parser.error('--stub needs --mods')
    mods = pathlib.Path(options.mods) if options.mods else None
    contracts = pathlib.Path(options.contracts)
    asm_dir = pathlib.Path(options.asm) if options.asm else None
    if asm_dir:
        asm_dir.mkdir(parents=True, exist_ok=True)
    if options.stub:
        stub(options, mods, contracts, asm_dir)
    overrides = pathlib.Path(options.overrides)
    files = [overrides] if overrides.is_file() else sorted(overrides.glob('shader_*.ps.hlsl'))
    jobs = []
    for override in files:
        stem = override.name[:-len('.ps.hlsl')]
        # shader_<hash>_<modification> has one contract, shader_<hash> all of its hash.
        pattern = f'{stem}.ps.contract.hlsli' if stem.count('_') == 2 else f'{stem}_*.ps.contract.hlsli'
        jobs += [(override, contract) for contract in sorted(contracts.glob(pattern))]
    if not jobs:
        sys.exit('no override with a contract')
    fxc = find_fxc()
    failures = {}
    with concurrent.futures.ThreadPoolExecutor(options.workers) as pool:
        for name, code, errors, _ in pool.map(
                lambda job: compile_one(fxc, *job, asm_dir, mods, options.define), jobs):
            if code:
                failures[name] = errors
    print(f'{len(jobs) - len(failures)}/{len(jobs)} compiled')
    report(failures)


def report(failures):
    """Prints the failures grouped by their first error and exits, with 1 if there are any."""
    by_error = {}
    for name, errors in failures.items():
        key = errors[0].split(': ', 1)[-1][:120] if errors else 'unknown'
        by_error.setdefault(key, []).append(name)
    for key, names in sorted(by_error.items(), key=lambda item: -len(item[1])):
        print(f'{len(names):4d}  {key}\n      for example {names[0]}')
    sys.exit(1 if failures else 0)


def stub(options, mods, contracts, asm_dir):
    """--stub: every mod of mods on every contract of its hash, with and without the defines."""
    jobs, without = [], []
    for mod in sorted(mods.glob('shader_*.mod.hlsli')):
        found = sorted(contracts.glob(f'{mod.name[:23]}_*.ps.contract.hlsli'))
        jobs += found
        if not found:
            without.append(mod.name[7:23])
    if not jobs:
        sys.exit('no mod with a contract')
    fxc = find_fxc()
    failures, unchanged = {}, []
    with concurrent.futures.ThreadPoolExecutor(options.workers) as pool:
        modded = list(pool.map(
            lambda contract: compile_one(fxc, None, contract, asm_dir, mods, options.define), jobs))
        plain = (list(pool.map(lambda contract: compile_one(fxc, None, contract, None, mods, []),
                               jobs)) if options.define else modded)
    for (name, code, errors, dxbc), (_, plain_code, plain_errors, plain_dxbc) in zip(modded, plain):
        if code or plain_code:
            failures[name] = errors if code else plain_errors
        elif options.define and dxbc == plain_dxbc:
            unchanged.append(name)
    hashes = len({contract.name[7:23] for contract in jobs})
    print(f'{len(jobs) - len(failures)}/{len(jobs)} contracts of {hashes} modded hashes compiled' +
          (f' with and without {";".join(options.define)}' if options.define else ''))
    if without:
        print(f'{len(without)} mods without a contract here: {" ".join(without)}')
    if options.define:
        print(f'{len(unchanged)} unchanged by the defines' +
              ''.join(f'\n      {name}' for name in unchanged))
    failures.update((name, ['the defines leave the DXBC unchanged']) for name in unchanged)
    report(failures)


if __name__ == '__main__':
    main()
