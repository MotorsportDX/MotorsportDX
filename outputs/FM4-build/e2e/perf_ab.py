"""Reads an in-run perf A/B: a run with ab_cvar, ab_values and ab_period_ms, where the runtime
sets one cvar to the first value in even periods of the steady clock and to the second in odd
periods, and labels each perf.jsonl window with its side (docs/e2e/experiments.md, "In-run perf
A/B"). Also compares separate runs the same way, to set an in-run result against N+N runs.

  e2e.py ... --scenario race --arg ab_cvar=<cvar> --arg ab_values=<a>,<b> --arg ab_period_ms=2000
  perf_ab.py <run> [--phase race] [--margin-ms 250] [--json <file>]
  perf_ab.py --runs <run or --repeat dir>... --vs <run or --repeat dir>... [--phase race]

In-run: the windows of the phase (summary.json phase_windows) are grouped by period of the
steady clock; windows that start within --margin-ms of their period's flip are dropped (frames
drawn before the flip whose GPU times arrive after it, transients of the switch). Each metric is
a ratio of sums over a period's windows (per frame or per draw). The paired estimate takes every
two consecutive periods (one per side) and their difference b - a; drifts of the scene cancel,
since the pairs alternate between a-then-b and b-then-a. It reports the median of those
differences, their interquartile range, and a 95% interval of the median from a block bootstrap.

Separate runs: each run's value is the ratio of sums over its phase's windows; the difference is
b - a of the means, with a 95% interval from Welch's t (the spread of a 3+3 perf claim).

Metrics: GPU ms a frame (gpu frame_ms), the render thread's busy ms a frame (render_thread_busy_ms,
or the command processor's window time not waiting without the host render thread), the textures
zone (cpu_ms draw_textures) in microseconds a draw and in ms a frame, draws a frame, and the
mean frame time. Standard library only."""
import argparse
import json
import math
import pathlib
import random
import statistics


def load_run(run, phase):
    """The perf windows of the run's phase (all windows when the run has no such phase)."""
    run = pathlib.Path(run)
    perf = [json.loads(line) for line in (run / 'perf.jsonl').read_text().splitlines() if line.strip()]
    summary_path = run / 'summary.json'
    if phase and summary_path.is_file():
        windows = json.loads(summary_path.read_text()).get('phase_windows') or {}
        if phase in windows:
            start, end = windows[phase]
            return perf[start:end]
        raise SystemExit(f'{run}: no phase {phase!r} in summary.json phase_windows '
                         f'({", ".join(windows) or "none"})')
    return perf


def frames(window):
    return len(window.get('frame_ms', []))


def busy_ms(window):
    if 'render_thread_busy_ms' in window:
        return window['render_thread_busy_ms']
    return max(0.0, window['window_ms'] - sum(window.get('wait_ms', {}).values()))


# name: (numerator, denominator, scale, unit) of a ratio of sums over windows.
METRICS = {
    'gpu_ms_per_frame': (lambda w: w.get('gpu', {}).get('frame_ms', 0) * w.get('gpu', {}).get('frames', 0),
                         lambda w: w.get('gpu', {}).get('frames', 0), 1, 'ms'),
    'busy_ms_per_frame': (busy_ms, frames, 1, 'ms'),
    'textures_us_per_draw': (lambda w: w.get('cpu_ms', {}).get('draw_textures', 0),
                             lambda w: w['draws'], 1000, 'us'),
    'textures_ms_per_frame': (lambda w: w.get('cpu_ms', {}).get('draw_textures', 0), frames, 1, 'ms'),
    'draws_per_frame': (lambda w: w['draws'], frames, 1, ''),
    'frame_ms': (lambda w: sum(w.get('frame_ms', [])), frames, 1, 'ms'),
}


def ratio(windows, metric):
    numerator, denominator, scale, _ = METRICS[metric]
    total = sum(denominator(w) for w in windows)
    return scale * sum(numerator(w) for w in windows) / total if total else None


def block_bootstrap_median(values, block=4, resamples=4000, seed=1):
    """95% interval of the median of values by a moving block bootstrap (consecutive paired
    differences share a period, so they are not independent)."""
    if len(values) < 2:
        return None, None
    block = max(1, min(block, len(values)))
    starts = len(values) - block + 1
    rng = random.Random(seed)
    medians = []
    for _ in range(resamples):
        sample = []
        while len(sample) < len(values):
            first = rng.randrange(starts)
            sample.extend(values[first:first + block])
        medians.append(statistics.median(sample[:len(values)]))
    medians.sort()
    return medians[int(0.025 * resamples)], medians[int(0.975 * resamples) - 1]


def in_run(run, phase, margin_ms):
    windows = load_run(run, phase)
    labelled = [w for w in windows if 'ab' in w]
    if not labelled:
        raise SystemExit(f'{run}: no windows with an "ab" label in the phase; was ab_cvar set and '
                         f'accepted? (runtime.log lists ab_cvar errors)')
    values = {w['ab']['side']: w['ab']['value'] for w in labelled}
    periods = {}
    dropped = 0
    for w in labelled:
        if w['ab']['offset_ms'] < margin_ms:
            dropped += 1
            continue
        periods.setdefault(w['ab']['period'], []).append(w)
    order = sorted(periods)
    result = {'run': str(run), 'phase': phase, 'margin_ms': margin_ms,
              'values': [values.get(0), values.get(1)],
              'windows': len(labelled), 'windows_dropped': dropped,
              'periods': [sum(1 for p in order if p % 2 == side) for side in (0, 1)],
              'metrics': {}}
    for metric in METRICS:
        per_period = {p: ratio(periods[p], metric) for p in order}
        sides = [ratio([w for p in order if p % 2 == side for w in periods[p]], metric)
                 for side in (0, 1)]
        differences = []
        for previous, current in zip(order, order[1:]):
            a, b = per_period[previous], per_period[current]
            if current != previous + 1 or a is None or b is None:
                continue
            differences.append(b - a if current % 2 else a - b)
        entry = {'a': sides[0], 'b': sides[1], 'pairs': len(differences)}
        if differences:
            low, high = block_bootstrap_median(differences)
            quartiles = statistics.quantiles(differences, n=4) if len(differences) >= 2 else [0, 0, 0]
            entry.update(median_diff=statistics.median(differences), ci95=[low, high],
                         iqr=quartiles[2] - quartiles[0])
        result['metrics'][metric] = entry
    return result


def expand(paths):
    runs = []
    for path in map(pathlib.Path, paths):
        nested = sorted(p for p in path.glob('run*') if (p / 'perf.jsonl').is_file())
        runs.extend(nested if nested and not (path / 'perf.jsonl').is_file() else [path])
    return runs


# Two-sided 95% quantiles of Student's t by degrees of freedom (1 to 10, then the normal).
T95 = [12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228]


def separate(runs_a, runs_b, phase):
    runs_a, runs_b = expand(runs_a), expand(runs_b)
    if len(runs_a) < 2 or len(runs_b) < 2:
        raise SystemExit('--runs and --vs need two or more runs each')
    phases_a = [load_run(r, phase) for r in runs_a]
    phases_b = [load_run(r, phase) for r in runs_b]
    result = {'runs': [str(r) for r in runs_a], 'vs': [str(r) for r in runs_b], 'phase': phase,
              'metrics': {}}
    for metric in METRICS:
        a = [v for v in (ratio(w, metric) for w in phases_a) if v is not None]
        b = [v for v in (ratio(w, metric) for w in phases_b) if v is not None]
        entry = {'a': statistics.mean(a) if a else None, 'b': statistics.mean(b) if b else None,
                 'runs_a': a, 'runs_b': b}
        if len(a) >= 2 and len(b) >= 2:
            va, vb = statistics.variance(a) / len(a), statistics.variance(b) / len(b)
            se = math.sqrt(va + vb)
            # Welch-Satterthwaite degrees of freedom.
            df = (va + vb) ** 2 / ((va ** 2 / (len(a) - 1)) + (vb ** 2 / (len(b) - 1))) if se else 1
            t = T95[min(len(T95), max(1, int(df))) - 1] if df < 11 else 1.96
            diff = entry['b'] - entry['a']
            entry.update(mean_diff=diff, ci95=[diff - t * se, diff + t * se], se=se)
        result['metrics'][metric] = entry
    return result


def number(value, digits=4):
    return '-' if value is None else f'{value:.{digits}f}'


def show(result):
    if 'values' in result:
        print(f"{result['run']}  phase {result['phase']}  a={result['values'][0]} "
              f"b={result['values'][1]}  periods a/b {result['periods'][0]}/{result['periods'][1]}  "
              f"windows {result['windows']} ({result['windows_dropped']} in the margin of "
              f"{result['margin_ms']} ms)")
        print(f"{'metric':24} {'a':>9} {'b':>9} {'median b-a':>11} {'95% interval':>21} "
              f"{'IQR':>8} {'pairs':>5}")
        for metric, entry in result['metrics'].items():
            low, high = entry.get('ci95', (None, None))
            print(f"{metric:24} {number(entry['a']):>9} {number(entry['b']):>9} "
                  f"{number(entry.get('median_diff')):>11} "
                  f"{'[' + number(low) + ', ' + number(high) + ']':>21} "
                  f"{number(entry.get('iqr')):>8} {entry['pairs']:>5}")
    else:
        print(f"{len(result['runs'])} runs against {len(result['vs'])}  phase {result['phase']}")
        print(f"{'metric':24} {'a':>9} {'b':>9} {'mean b-a':>11} {'95% interval':>21} {'se':>8}")
        for metric, entry in result['metrics'].items():
            low, high = entry.get('ci95', (None, None))
            print(f"{metric:24} {number(entry['a']):>9} {number(entry['b']):>9} "
                  f"{number(entry.get('mean_diff')):>11} "
                  f"{'[' + number(low) + ', ' + number(high) + ']':>21} {number(entry.get('se')):>8}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('run', nargs='?', help='e2e.py --out directory of a run with ab_cvar')
    parser.add_argument('--runs', nargs='+', help='separate runs of side a (or --repeat directories)')
    parser.add_argument('--vs', nargs='+', help='separate runs of side b (or --repeat directories)')
    parser.add_argument('--phase', default='race', help="summary.json phase to read ('' for all windows)")
    parser.add_argument('--margin-ms', type=float, default=250,
                        help='drop windows that start this soon after their flip')
    parser.add_argument('--json', help='also write the result to this file')
    options = parser.parse_args()
    if options.runs or options.vs:
        if not (options.runs and options.vs) or options.run:
            parser.error('separate runs need --runs and --vs and no run')
        result = separate(options.runs, options.vs, options.phase)
    elif options.run:
        result = in_run(options.run, options.phase, options.margin_ms)
    else:
        parser.error('give a run, or --runs and --vs')
    show(result)
    if options.json:
        pathlib.Path(options.json).write_text(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
