"""The llvm-mingw prototype (docs/analysis/mingw-toolchain.md, section 8): pipeline.py's setup,
generate and build stages with every CMake project configured by x86_64-w64-mingw32.cmake
instead of clang for Microsoft's libraries. pipeline.py itself is unchanged: this script wraps its
run() to rewrite the CMake configure commands, and runs nothing else of its own.

    python prototype.py <setup|generate|build|all> --build-root <new folder> --mingw <llvm-mingw>
        [--disc1 <folder> --disc2 <folder>] [--jobs N] [--keep-going] [--tests player]

PATH is set to the llvm-mingw bin folder, the portable CMake and Ninja (--toolchain), System32
and Windows; INCLUDE and LIB are removed, so nothing of Visual Studio or the Windows SDK is read.
--keep-going builds with ninja -k 0 and records every error instead of stopping at the first.
"""
import argparse
import os
from pathlib import Path
import shutil
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import pipeline  # noqa: E402

TOOLCHAIN_FILE = HERE/'x86_64-w64-mingw32.cmake'
# The compiler arguments pipeline.py passes; the toolchain file names the compilers instead.
DROPPED = ('-DCMAKE_C_COMPILER=', '-DCMAKE_CXX_COMPILER=')
KEEP_GOING = False
# llvm-mingw's C++ runtime DLLs (mingw-toolchain.md 4).
RUNTIME_DLLS = ('libc++.dll', 'libunwind.dll')
_run = pipeline.run


def mingw_run(args, cwd=None, log=None, env=None):
    args = [str(x) for x in args]
    if args and Path(args[0]).stem.lower() == 'cmake':
        if '-S' in args:
            args = [a for a in args if not a.startswith(DROPPED)]
            args.insert(1, f'-DCMAKE_TOOLCHAIN_FILE={TOOLCHAIN_FILE.as_posix()}')
        elif '--build' in args and KEEP_GOING:
            args += ['--', '-k', '0']
    return _run(args, cwd=cwd, log=log, env=env)


def environment(mingw, toolchain):
    os.environ['FM4_MINGW_ROOT'] = str(mingw)
    for variable in ('INCLUDE', 'LIB', 'LIBPATH', 'CPATH', 'LIBRARY_PATH'):
        os.environ.pop(variable, None)
    windows = os.environ.get('SystemRoot', r'C:\Windows')
    os.environ['PATH'] = os.pathsep.join([str(mingw/'bin'), rf'{windows}\System32', windows])
    if toolchain:
        pipeline.use_toolchain(toolchain)
    # The mingw bin folder before the portable tools: its clang, not LLVM's.
    os.environ['PATH'] = os.pathsep.join([str(mingw/'bin'), os.environ['PATH']])


def stage_runtime_dlls(mingw, root):
    """libc++ and libunwind, which every module links as DLLs, into runtime-install/bin: build
    copies that folder's DLLs beside the game, as it does rexruntime.dll."""
    for name in RUNTIME_DLLS:
        shutil.copyfile(mingw/'x86_64-w64-mingw32/bin'/name, root/'runtime-install/bin'/name)


def main():
    global KEEP_GOING
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['setup', 'generate', 'build', 'all'])
    parser.add_argument('--build-root', type=Path, required=True)
    parser.add_argument('--mingw', type=Path, required=True)
    parser.add_argument('--toolchain', type=Path)
    parser.add_argument('--disc1', type=Path)
    parser.add_argument('--disc2', type=Path)
    parser.add_argument('--jobs', type=int, default=8)
    parser.add_argument('--keep-going', action='store_true')
    parser.add_argument('--tests', choices=pipeline.TEST_SETS, default='player')
    parser.add_argument('--progress')
    args = parser.parse_args()
    KEEP_GOING = args.keep_going
    root = args.build_root.resolve()
    environment(args.mingw.resolve(), args.toolchain)
    pipeline.run = mingw_run
    pipeline.FETCH = 'archive'
    if args.progress:
        pipeline.PROGRESS = open(args.progress, 'a', encoding='utf-8')
    root.mkdir(parents=True, exist_ok=True)
    if args.action in ('setup', 'all'):
        with pipeline.progress('stage', 'setup'):
            pipeline.setup(root, args.jobs)
            stage_runtime_dlls(args.mingw.resolve(), root)
    if args.action in ('generate', 'all'):
        discs = pipeline.validate_discs(args.disc1, args.disc2)
        with pipeline.progress('stage', 'generate'):
            pipeline.generate(root, *discs, None, args.jobs)
    if args.action in ('build', 'all'):
        with pipeline.progress('stage', 'build'):
            pipeline.build_game(root, args.jobs, (), None, args.tests)


if __name__ == '__main__':
    main()
