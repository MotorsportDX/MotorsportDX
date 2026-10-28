"""Run an e2e gate on a snapshot of the game build and summarize it against a baseline.

The snapshot (<BuildRoot>/snapshots/<name>) is the build that build-result.json describes, so
Build.ps1 -Action build can rebuild game-build while the gate runs. Cells run one at a time
(performance numbers need the machine to themselves) into <BuildRoot>/e2e/<name>-<hz>-<scenario>.

Tiers (docs/e2e/README.md, "Gates"):
  smoke  one run per scenario at 240 Hz: does it still work (about 10 minutes)
  perf   three runs of photo at 240 Hz: its race phase (12 cars, 30 s) and photo mode
  full   three runs of every scenario at 60 and 240 Hz: before committing a stage (about 1 hour)
  career career_finish (the AI wins a career race, rewards, EVENT COMPLETE) and career_progress
         (a new process sees level 2 and the new credits) at 240 Hz, one run (about 8 minutes)
  parity two runs of freeze (a rewind freeze at the start of a Quick Race, 2x, the title's own
         picture, pinned occlusion) at 60 Hz: image, draw and predication parity with parity.py
         against a baseline (docs/e2e/frozen-frames.md, about 8 minutes)
  cold   a fresh install's shader caches at 240 Hz, one run: career on empty caches, then photo
         on the caches career left (a new track in the second session); the stalls of shader
         compiles in play (docs/analysis/d3d-hle-n6.md 19.3.4, about 9 minutes)
An AI driver drives the player's car in every race (FM4_GAME_CMDLINE=engageaionstart; --human
for the harness throttle).

  python outputs\\FM4-build\\e2e\\matrix.py --build-root D:\\FM4-build --name vb --tier smoke --baseline reg
"""
import argparse, json, shutil, subprocess, sys, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import pipeline  # noqa: E402
from e2e import (FAST_SCENARIOS, MIN_FREE_GB, check_content, check_free_space, import_content,  # noqa: E402
                 shader_work_line)

TIERS = {
    'smoke': (('photo', 'photo_view', 'career'), (240,), 1),
    'perf': (('photo',), (240,), 3),
    'full': (('photo', 'photo_view', 'career'), (60, 240), 3),
    'career': (('career_finish', 'career_progress'), (240,), 1),
    'parity': (('freeze',), (60,), 2),
    'cold': (('career', 'photo'), (240,), 1),
}
# Cells that run on the profile a previous cell of the gate saved.
PROFILE_FROM = {'photo_view': 'photo', 'career_progress': 'career_finish'}
# With cold caches (the cold tier, --cold-cache): cells that start with the caches a previous cell
# of the gate left; the others start empty.
CACHE_FROM = {'photo': 'career'}


def snapshot(root, source, name):
    """Copy the game build to <root>/snapshots/<name> after checking it against build-result.json."""
    report = json.loads((root / 'build-result.json').read_text())
    for artifact, expected in report['artifacts'].items():
        if pipeline.digest(root / source / artifact) != expected:
            raise SystemExit(f'{source}/{artifact} differs from build-result.json; finish the build first')
    target = root / 'snapshots' / name
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    for item in (root / source).iterdir():
        if item.is_file() and item.suffix.lower() in ('.exe', '.dll', '.pdb', '.map'):
            shutil.copy2(item, target / item.name)
    # The snapshot keeps what it was built from (the source's HEAD, dirty flag and tree hash).
    shutil.copy2(root / 'build-result.json', target / 'build-result.json')
    return target.relative_to(root)


def cell_args(root, name, hz, scenario, repeat, build, extra, fast_route=False, cold=None):
    """The e2e.py command of a cell; cold: the gate's scenarios when it runs on cold caches."""
    args = [sys.executable, HERE / 'e2e.py', '--build-root', root, '--game-build', build,
            '--out', root / 'e2e' / f'{name}-{hz}-{scenario}', '--scenario', scenario,
            '--repeat', str(repeat)]
    if fast_route and scenario in FAST_SCENARIOS:
        args += ['--fast-route']
    if hz != 60:
        args += [f'--arg=--video_mode_refresh_rate={hz}', '--env', f'FM4_FRAME_RATE={hz}']
    if scenario == 'quickrace':
        args += ['--tail', '40', '--measure', '30']
    if scenario in ('photo', 'career', 'career_finish', 'dlc', 'freeze'):
        args += ['--profile', 'copy']
    if scenario == 'freeze':
        # The same opponents and grid every run (an --env given to matrix.py comes later and wins).
        args += ['--env', 'FM4_FIXED_SEED=1']
    # The render scale, explicit in every cell, so that a gate's name says what ran: 1 unless an
    # --arg given to matrix.py sets it (it comes later and wins); the freeze scenario's pin, 2.
    scale = '2' if scenario == 'freeze' else '1'
    args += [f'--arg=draw_resolution_scale_x={scale}', f'--arg=draw_resolution_scale_y={scale}']
    if scenario in PROFILE_FROM:
        # The profile saved by the earlier cell of the same gate (its photo, its race result).
        source = f'{name}-{hz}-{PROFILE_FROM[scenario]}'
        args += ['--profile', root / 'e2e' / source / ('run1' if repeat > 1 else '') / 'user-data']
    if cold is not None:
        if CACHE_FROM.get(scenario) in cold:
            source = f'{name}-{hz}-{CACHE_FROM[scenario]}'
            args += ['--cache-from', root / 'e2e' / source / ('run1' if repeat > 1 else '')]
        else:
            args += ['--cold-cache']
    return args + extra


def describe(out):
    """One line per cell: runs that reached their goal, overall and phase fps / p99."""
    runs = sorted(out.glob('run[0-9]*')) or [out]
    passed, parts = 0, []
    for run in runs:
        path = run / 'summary.json'
        if not path.exists():
            continue
        s = json.loads(path.read_text())
        passed += not s.get('crashed') and s.get('goal_reached') is not False
    path = out / 'summary.json'
    if path.exists():
        s = json.loads(path.read_text())
        parts.append(f"fps {s.get('fps_mean')} p99 {s.get('frame_ms_p99')}")
        for phase, stats in (s.get('phases') or {}).items():
            parts.append(f"{phase} {stats.get('fps_mean')} fps p99 {stats.get('frame_ms_p99')}")
        if s.get('failed_steps'):
            parts.append(f"failed: {s['failed_steps']}")
        work = [w for w in s.get('shader_work_runs') or [(s.get('shader_work') or {}).get('run')] if w]
        if work and (s.get('cache') or {}).get('cold'):
            parts.append(shader_work_line(work[0]))
    return f'{passed}/{len(runs)} ' + ', '.join(parts)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build-root', type=Path, required=True)
    parser.add_argument('--name', required=True, help='cells go to <BuildRoot>/e2e/<name>-<hz>-<scenario>')
    parser.add_argument('--tier', choices=sorted(TIERS), default='smoke')
    parser.add_argument('--scenarios', help='comma list instead of the tier scenarios')
    parser.add_argument('--hz', help='comma list of refresh rates instead of the tier ones')
    parser.add_argument('--repeat', type=int, help='runs per cell instead of the tier count')
    parser.add_argument('--baseline', help='compare each cell with <BuildRoot>/e2e/<baseline>-<hz>-<scenario>')
    parser.add_argument('--game-build', default='game-build', help='build directory to snapshot')
    parser.add_argument('--no-snapshot', action='store_true', help='run the build directory itself')
    parser.add_argument('--human', action='store_true',
                        help='drive with the harness input instead of the AI driver (engageaionstart)')
    parser.add_argument('--env', action='append', default=[], help='extra NAME=value for every cell')
    parser.add_argument('--arg', action='append', default=[], help='extra --cvar=value for every cell')
    parser.add_argument('--look', help="look of every cell as Build.ps1 -Look (default: the launcher's, aces), "
                                       'for example "aces,taa=0"')
    parser.add_argument('--msaa', action='store_true',
                        help='every cell keeps the tiles and MSAA of the quality with a look that has TAA '
                             '(Build.ps1 -Msaa)')
    parser.add_argument('--quality', help="race quality of every cell as Build.ps1 -Quality (default: the "
                                          "launcher's, high), for example extrahigh")
    parser.add_argument('--native', action='store_true',
                        help='every cell on the native path (e2e.py --native)')
    parser.add_argument('--cache-copy', action='store_true',
                        help="each cell on a private copy of the BuildRoot's warm caches (e2e.py "
                             '--cache-copy): for an image-only tier beside another game')
    parser.add_argument('--offline-pass', action='store_true',
                        help='every cell runs the offline shader pass first (e2e.py --offline-pass)')
    parser.add_argument('--cold-cache', action='store_true',
                        help='every cell on cold shader caches as the cold tier: empty, or those its '
                             'CACHE_FROM cell left (e2e.py --cold-cache, --cache-from)')
    parser.add_argument('--fast-route', action='store_true',
                        help='cells with a fast route reach their race through the title\'s screen events '
                             '(e2e.py --fast-route); the others keep their menus')
    parser.add_argument('--offline-pass', action='store_true',
                        help='every cell runs the offline shader pass on its caches before the title '
                             '(e2e.py --offline-pass)')
    options = parser.parse_args()
    root = options.build_root.resolve()
    scenarios, rates, repeat = TIERS[options.tier]
    scenarios = options.scenarios.split(',') if options.scenarios else scenarios
    rates = [int(r) for r in options.hz.split(',')] if options.hz else rates
    repeat = options.repeat or repeat
    for later, earlier in PROFILE_FROM.items():
        if later in scenarios and earlier not in scenarios:
            parser.error(f'{later} runs on the profile of the {earlier} cell; run {earlier} too')
    scenarios = sorted(scenarios, key=lambda s: s not in PROFILE_FROM.values() and
                       s not in CACHE_FROM.values())
    cold = scenarios if options.cold_cache or options.tier == 'cold' else None
    if cold and options.cache_copy:
        parser.error('--cache-copy starts from the warm caches; the cold tier starts from empty ones')
    # A full drive or missing content packages fail here once instead of in every cell; Disc 2 and
    # the content packages of local.json are imported before the first cell when they are not yet.
    check_free_space(root / 'e2e', MIN_FREE_GB)
    import_content(root, options.game_build)
    check_content(root)
    build = Path(options.game_build) if options.no_snapshot else snapshot(root, options.game_build, options.name)
    # An AI driver drives the player's car, so every race follows the track and the load per
    # frame no longer depends on where a car held at full throttle ends up.
    env = options.env if options.human or any(e.startswith('FM4_GAME_CMDLINE=') for e in options.env) \
        else options.env + ['FM4_GAME_CMDLINE=engageaionstart']
    extra = [f'--env={e}' for e in env] + [f'--arg={a}' for a in options.arg]
    if options.look:
        extra += ['--look', options.look]
    if options.msaa:
        extra += ['--msaa']
    if options.quality:
        extra += ['--quality', options.quality]
    if options.native:
        extra += ['--native']
    if options.offline_pass:
        extra += ['--offline-pass']
    if options.cache_copy:
        extra += ['--cache-copy']
    if options.offline_pass:
        extra += ['--offline-pass']
    log = root / 'e2e' / f'{options.name}.log'
    log.parent.mkdir(parents=True, exist_ok=True)
    started, results = time.time(), []
    for hz in rates:
        for scenario in scenarios:
            cell = f'{options.name}-{hz}-{scenario}'
            out = root / 'e2e' / cell
            t = time.time()
            with open(root / 'e2e' / f'{cell}.log', 'w', encoding='utf-8', errors='replace') as output:
                code = subprocess.call([str(a) for a in cell_args(root, options.name, hz, scenario, repeat, build,
                                                                  extra, options.fast_route, cold)],
                                       stdout=output, stderr=subprocess.STDOUT)
            verdict = ''
            if options.baseline:
                base = root / 'e2e' / f'{options.baseline}-{hz}-{scenario}'
                if (base / 'summary.json').exists() and (out / 'summary.json').exists():
                    report = subprocess.run([sys.executable, HERE / 'compare.py', base, out, '--json'],
                                            capture_output=True, text=True)
                    try:
                        verdict = ' ' + json.loads(report.stdout)['verdict'] + f' vs {base.name}'
                    except (ValueError, KeyError):
                        verdict = ' compare failed'
                    if scenario == 'freeze':
                        # Image, draw and predication parity of the frozen frame.
                        report = subprocess.run([sys.executable, HERE / 'parity.py', base, out, '--json',
                                                 '--out', out / 'parity'], capture_output=True, text=True)
                        try:
                            verdict += ', parity ' + json.loads(report.stdout)['verdict']
                        except (ValueError, KeyError):
                            verdict += ', parity failed'
                else:
                    verdict = f' (no baseline {base.name})'
            line = f'{cell}: exit {code}, {(time.time() - t) / 60:.1f} min, {describe(out)}{verdict}'
            results.append(code)
            print(line, flush=True)
            with open(log, 'a', encoding='utf-8') as f:
                f.write(line + '\n')
    print(f'{options.tier} gate {options.name}: {sum(c == 0 for c in results)}/{len(results)} cells passed '
          f'in {(time.time() - started) / 60:.0f} min')
    sys.exit(1 if any(results) else 0)


if __name__ == '__main__':
    main()
