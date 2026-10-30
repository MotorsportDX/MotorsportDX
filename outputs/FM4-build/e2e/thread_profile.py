"""What the threads do in a run's long frames: python thread_profile.py <run> <fm4_rex.map> [--phase rewards] [--min-ms 50] [--top N]

Reads <run>/thread-profile.txt (REX_THREAD_PROFILE), perf.jsonl and events.txt, finds every
frame over --min-ms (in --phase, the phases of season_perf.py; --phase any for all frames), and
counts the samples of each thread inside those frames by:

- the leaf module (where the time is spent: the game's code, the runtime, ntdll waits...);
- the innermost game function (fm4_rex.exe offsets resolved by the linker map);
- the innermost runtime frame and its game caller.

It prints the same counts for all samples outside long frames of the same phase as a baseline.
"""
import argparse
import bisect
import collections
import json
import re
from pathlib import Path

import season_perf
from soak_report import read_events, read_jsonl


def load_map(path):
    """[(rva, name)] sorted, from an MSVC/lld-link map: ' 0001:000f54d0  sub_822A0000  00000001400f64d0 ...'."""
    entries = []
    base = 0x140000000
    pattern = re.compile(r'^\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{16})\s', re.I)
    with open(path, encoding='utf-8', errors='replace') as handle:
        for line in handle:
            if 'Preferred load address is' in line:
                base = int(line.split()[-1], 16)
            match = pattern.match(line)
            if not match:
                continue
            name = match.group(1)
            # A hooked guest function's own body is __imp__sub_<address>; other __imp_ names are
            # import thunks.
            if name.startswith('__imp__sub_'):
                name = name[len('__imp__'):]
            elif name.startswith('__imp_'):
                continue
            entries.append((int(match.group(2), 16) - base, name))
    entries.sort()
    return [e[0] for e in entries], [e[1] for e in entries]


def load_profile(path):
    names, stacks, samples = {}, {}, []
    with open(path, encoding='utf-8', errors='replace') as handle:
        for line in handle:
            if line.startswith('T '):
                _, tid, name = line.rstrip('\n').split(' ', 2)
                names[int(tid)] = name
            elif line.startswith('S '):
                parts = line.split()
                stacks[int(parts[1])] = parts[2:]
            else:
                parts = line.split()
                if len(parts) == 3:
                    samples.append((float(parts[0]) / 1000, int(parts[1]), int(parts[2])))
    return names, stacks, samples


def long_frames(perf, min_ms):
    frames = []
    for window in perf:
        clock = window['t'] - window.get('window_ms', 0) / 1000
        for ms in window.get('frame_ms', []):
            clock += ms / 1000
            if ms > min_ms:
                frames.append((clock - ms / 1000, clock, ms))
    return frames


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('run', type=Path)
    parser.add_argument('map', type=Path)
    parser.add_argument('--phase', default='rewards')
    parser.add_argument('--min-ms', type=float, default=50)
    parser.add_argument('--top', type=int, default=15)
    parser.add_argument('--profile', default='thread-profile.txt')
    options = parser.parse_args()
    rvas, symbols = load_map(options.map)

    def game_name(offset):
        index = bisect.bisect_right(rvas, offset) - 1
        return symbols[index] if index >= 0 else f'?{offset:x}'

    names, stacks, samples = load_profile(options.run / options.profile)
    perf = read_jsonl(options.run / 'perf.jsonl')
    spans = None
    summary = options.run / 'summary.json'
    if summary.is_file():
        launch = json.loads(summary.read_text()).get('launch_time')
        if launch:
            spans = season_perf.phase_spans(read_events(options.run / 'events.txt'), launch)
    frames = long_frames(perf, options.min_ms)
    if spans and options.phase != 'any':
        frames = [f for f in frames if season_perf.phase_at(spans, f[1]) == options.phase]
    starts = [f[0] for f in frames]

    def in_long(t):
        index = bisect.bisect_right(starts, t) - 1
        return index >= 0 and t <= frames[index][1]

    def phase_of(t):
        return season_perf.phase_at(spans, t) if spans else 'any'

    def describe(stack_id):
        frames_ = stacks[stack_id]
        leaf = frames_[0].split('+')[0]
        game = next((game_name(int(f.split('+')[1], 16)) for f in frames_ if f.startswith('fm4_rex.exe+')), None)
        runtime = next((f for f in frames_ if f.split('+')[0] in ('rexruntime.dll', 'rexgpu-xenos.dll')), None)
        return leaf, game, runtime

    inside = collections.defaultdict(collections.Counter)
    outside = collections.defaultdict(collections.Counter)
    for t, tid, stack_id in samples:
        name = names.get(tid, str(tid))
        leaf, game, runtime = describe(stack_id)
        if in_long(t):
            target = inside
        elif phase_of(t) == options.phase or options.phase == 'any':
            target = outside
        else:
            continue
        target[name]['leaf ' + leaf] += 1
        target[name]['game ' + (game or '-')] += 1
        if runtime:
            target[name]['runtime ' + runtime + ' <- ' + (game or '-')] += 1
    print(f'{len(frames)} frames over {options.min_ms} ms in {options.phase}, '
          f'{sum(f[2] for f in frames):.0f} ms in all')
    for label, table in (('INSIDE long frames', inside), (f'OUTSIDE long frames ({options.phase})', outside)):
        print(f'\n== {label}')
        busiest = sorted(table.items(), key=lambda kv: -sum(v for k, v in kv[1].items() if k.startswith('leaf ')))
        for name, counter in busiest[:6]:
            total = sum(v for k, v in counter.items() if k.startswith('leaf '))
            print(f'-- {name}: {total} samples')
            for kind in ('leaf ', 'game ', 'runtime '):
                for key, count in sorted(((k, v) for k, v in counter.items() if k.startswith(kind)),
                                         key=lambda kv: -kv[1])[:options.top]:
                    print(f'   {count:6d} {100 * count / total:5.1f}%  {key}')


if __name__ == '__main__':
    main()
