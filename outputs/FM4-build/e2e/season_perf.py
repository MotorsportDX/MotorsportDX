"""Performance survey of a career_season run: python season_perf.py <run> [--top N] [--json].

Splits the run by its screen events (events.txt) into phases and its events (season.jsonl), and
summarises the perf trace (perf.jsonl) of each event and phase:

- the phases: `menus` (the World Tour, the event's screen and every other menu), `loading`
  (the event's loading, the unloading after it, the loading screens between menus), `pre_race`
  (the pre-race menu), `countdown` (from the cars' placement to the race), `race` (the race
  scenario's push to its pop) and `rewards` (the post-race menu, the earnings, the level and
  affinity rewards and a gift car's screens); the driver presses CONTINUE at once, so a season run
  has no replay;
- per event and phase: seconds, frames, fps, the frame time p50, p95 and p99, the frames over 16,
  33 and 50 ms, the GPU time a frame and its parts (the post passes by group, the swap, the
  scene), the host render thread's and the submit thread's busy time a frame, and the shaders and
  pipelines made in play (`shader_work`: count and ms per kind);
- every frame over 50 ms with its time (seconds from the launch), event and phase, and the
  shader work inside it.

It writes season-perf.json, adds each event's phases to its season.jsonl record ('perf'), and
prints the event-phase pairs ranked by their p99 and by their frames over 33 ms a minute: the
candidates for optimisation.
"""
import argparse
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from soak_report import percentile, read_events, read_jsonl  # noqa: E402

PHASES = ('menus', 'loading', 'pre_race', 'countdown', 'race', 'rewards')
LOADING = {'19_DYNAMIC_LOADING_WORLDTOUR', '17_DYNAMIC_LOADING_FREEPLAY', '6_C_INI_UNLOADING', '035_D_LOADING'}
REWARDS = {'924_POST_RACE', '920_POST_RACE_EARNINGS', '909_LEVEL_PROGRESS', '910_POST_RACE_LEVEL_REWARDS',
           '108_C_BUY_CARSELECT', '114_C_BUY_BUYCAR_COLOR'}
# Scenes that do not change the phase: the race's HUD, transient scenes and pop-ups.
NEUTRAL = {'EMPTY_SCENE', 'NotImplemented', '100_P_PRE_BACKGROUNDMAIN', '947_HUD', '968_TELEMETRY',
           '037_I_D_POP_DIALOG', '032_D_POP_MESSAGE', '044_D_POP_UP'}
SCENE = re.compile(r'^scene \d+ (\S+)$')
SHADER_KINDS = ('translations_vs', 'translations_ps', 'mod_compiles', 'pipelines', 'pipeline_waits',
                'pass_compiles')
OVER = (16, 33, 50)


def phase_spans(events, launch):
    """[(start s, end s, phase)] from the launch, by the screen events; the boot is 'menus'."""
    spans, phase, start = [], 'menus', 0.0

    def switch(t, new):
        nonlocal phase, start
        if new != phase:
            spans.append((start, t, phase))
            phase, start = new, t
    for unix, text in events:
        t = unix - launch
        if re.match(r'^push \S+_Race$', text):
            switch(t, 'race')
        elif re.match(r'^pop \S+_Race$', text):
            switch(t, 'rewards')
        elif text == 'countdown placed':
            switch(t, 'countdown')
        elif (match := SCENE.match(text)):
            scene = match.group(1)
            if scene in NEUTRAL or phase in ('race', 'countdown'):
                continue
            if scene == '4_C_INI_PRERACE':
                switch(t, 'pre_race')
            elif scene in LOADING:
                switch(t, 'loading')
            elif scene in REWARDS:
                switch(t, 'rewards')
            else:
                switch(t, 'menus')
    spans.append((start, float('inf'), phase))
    return [s for s in spans if s[1] > s[0]]


def phase_at(spans, t):
    for start, end, phase in spans:
        if start <= t < end:
            return phase
    return spans[-1][2] if spans else 'menus'


def new_bucket():
    return {'seconds': 0.0, 'frames': [], 'gpu_frames': 0, 'gpu_ms': 0.0, 'passes': {}, 'swap_ms': 0.0,
            'scene_ms': 0.0, 'render_busy_ms': 0.0, 'submit_busy_ms': 0.0,
            'shader_work': {k: [0, 0.0] for k in SHADER_KINDS}}


def add_window(bucket, window):
    bucket['seconds'] += window.get('window_ms', 0) / 1000
    bucket['frames'].extend(window.get('frame_ms', []))
    gpu = window.get('gpu') or {}
    if gpu.get('frames'):
        n = gpu['frames']
        bucket['gpu_frames'] += n
        bucket['gpu_ms'] += gpu.get('frame_ms', 0) * n
        bucket['swap_ms'] += gpu.get('swap_ms', 0) * n
        bucket['scene_ms'] += gpu.get('scene_ms', 0) * n
        for name, ms in (gpu.get('passes_ms') or {}).items():
            bucket['passes'][name] = bucket['passes'].get(name, 0.0) + ms * n
    bucket['render_busy_ms'] += window.get('render_thread_busy_ms', 0) or 0
    bucket['submit_busy_ms'] += (window.get('submit_thread') or {}).get('busy_ms', 0) or 0
    for kind in SHADER_KINDS:
        values = (window.get('shader_work') or {}).get(kind)
        if values:
            bucket['shader_work'][kind][0] += values[0]
            bucket['shader_work'][kind][1] += values[1]


def finish(bucket):
    frames = bucket['frames']
    if not frames:
        return None
    n, g = len(frames), bucket['gpu_frames'] or 1
    minutes = sum(frames) / 60000
    result = {
        'seconds': round(bucket['seconds'], 1), 'frames': n,
        'fps': round(1000 * n / sum(frames), 2),
        'frame_ms_p50': round(percentile(frames, 0.50), 3),
        'frame_ms_p95': round(percentile(frames, 0.95), 3),
        'frame_ms_p99': round(percentile(frames, 0.99), 3),
        'frame_ms_max': round(max(frames), 1),
        **{f'over_{ms}ms': sum(f > ms for f in frames) for ms in OVER},
        'over_33ms_per_min': round(sum(f > 33 for f in frames) / minutes, 2) if minutes else None,
        'gpu_ms_per_frame': round(bucket['gpu_ms'] / g, 3) if bucket['gpu_frames'] else None,
        'gpu_parts_ms': {name: round(ms / g, 3) for name, ms in
                         sorted({**bucket['passes'], 'swap': bucket['swap_ms'], 'scene': bucket['scene_ms']}.items(),
                                key=lambda kv: -kv[1])} if bucket['gpu_frames'] else None,
        'render_thread_busy_ms_per_frame': round(bucket['render_busy_ms'] / n, 3),
        'submit_thread_busy_ms_per_frame': round(bucket['submit_busy_ms'] / n, 3),
        'shader_work': {k: [c, round(ms, 1)] for k, (c, ms) in bucket['shader_work'].items() if c},
    }
    return result


def survey(run):
    events = read_events(run / 'events.txt')
    perf = read_jsonl(run / 'perf.jsonl')
    records = read_jsonl(run / 'season.jsonl')
    summary = json.loads((run / 'summary.json').read_text()) if (run / 'summary.json').is_file() else {}
    state = json.loads((run / 'season-state.json').read_text()) if (run / 'season-state.json').is_file() else {}
    # The perf trace's 't' and the driver's seconds both count from the launch.
    launch = summary.get('launch_time') or state.get('launch_time')
    if launch is None:
        raise SystemExit(f'{run}: no launch_time in summary.json or season-state.json')
    spans = phase_spans(events, launch)

    def event_of(t):
        for record in records:
            if record['start_s'] <= t < record.get('end_s', float('inf')):
                return record['event']
        return None
    buckets, totals, long_frames = {}, {p: new_bucket() for p in PHASES}, []
    for window in perf:
        start = window['t'] - window.get('window_ms', 0) / 1000
        t = window['t'] - window.get('window_ms', 0) / 2000
        phase, event = phase_at(spans, t), event_of(t)
        add_window(buckets.setdefault((event, phase), new_bucket()), window)
        add_window(totals[phase], window)
        clock = start
        for ms in window.get('frame_ms', []):
            clock += ms / 1000
            if ms > 50:
                long_frames.append({'t_s': round(clock, 2), 'ms': round(ms, 1), 'event': event,
                                    'phase': phase_at(spans, clock)})
        # The window's own list of frames over 50 ms has their shader work.
        for entry in window.get('long_frames') or []:
            match = next((f for f in reversed(long_frames) if abs(f['ms'] - entry[0]) < 0.05 and
                          'shader_work_ms' not in f), None)
            if match:
                match['shader_work_ms'] = round(entry[1], 1)
    by_event = {}
    for (event, phase), bucket in buckets.items():
        stats = finish(bucket)
        if stats:
            by_event.setdefault(event, {})[phase] = stats
    report = {'run': str(run), 'phases': {p: finish(b) for p, b in totals.items() if finish(b)},
              'events': [], 'long_frames': long_frames}
    for record in records:
        phases = by_event.get(record['event'], {})
        record['perf'] = phases
        report['events'].append({'event': record['event'], 'name': record.get('name'),
                                 'track': record.get('track') or
                                 next((d for d, s in (record.get('season') or {}).get('events', [])
                                       if s.startswith('CURRENT')), None),
                                 'season': (record.get('season') or {}).get('name'), 'phases': phases})
    report['outside_events'] = by_event.get(None, {})
    candidates = []
    for entry in report['events']:
        for phase, stats in entry['phases'].items():
            if stats['seconds'] >= 5:
                candidates.append({'event': entry['event'], 'track': entry['track'], 'name': entry['name'],
                                   'phase': phase, **{k: stats[k] for k in (
                                       'seconds', 'fps', 'frame_ms_p99', 'over_33ms_per_min', 'over_50ms',
                                       'gpu_ms_per_frame')}})
    report['slowest_p99'] = sorted(candidates, key=lambda c: -c['frame_ms_p99'])
    report['most_hitches'] = sorted(candidates, key=lambda c: -(c['over_33ms_per_min'] or 0))
    return report, records


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('run', type=Path)
    parser.add_argument('--top', type=int, default=10)
    parser.add_argument('--json', action='store_true', help='print the whole survey')
    options = parser.parse_args()
    report, records = survey(options.run)
    (options.run / 'season-perf.json').write_text(json.dumps(report, indent=2))
    if records:
        (options.run / 'season.jsonl').write_text(''.join(json.dumps(r) + '\n' for r in records))
    if options.json:
        print(json.dumps(report, indent=2))
        return
    print(f"{report['run']}: {len(report['events'])} events, {len(report['long_frames'])} frames over 50 ms")
    for phase, stats in report['phases'].items():
        print(f"  {phase}: {stats['seconds']} s, {stats['fps']} fps, p50 {stats['frame_ms_p50']} "
              f"p99 {stats['frame_ms_p99']} ms, over 16/33/50 ms {stats['over_16ms']}/{stats['over_33ms']}/"
              f"{stats['over_50ms']}, GPU {stats['gpu_ms_per_frame']} ms {stats['gpu_parts_ms']}")
    print(f'  slowest p99 (event, phase):')
    for c in report['slowest_p99'][:options.top]:
        print(f"    {c['frame_ms_p99']} ms  event {c['event']} {c['track']} {c['phase']} ({c['seconds']} s, "
              f"{c['fps']} fps, GPU {c['gpu_ms_per_frame']} ms)")
    print(f'  most frames over 33 ms a minute:')
    for c in report['most_hitches'][:options.top]:
        print(f"    {c['over_33ms_per_min']}/min  event {c['event']} {c['track']} {c['phase']} "
              f"({c['over_50ms']} over 50 ms)")


if __name__ == '__main__':
    main()
