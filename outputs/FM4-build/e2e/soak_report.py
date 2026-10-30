"""Soak analysis of one long e2e run: python soak_report.py <run> [--json].

Reads the run's soak.jsonl (soak.py), perf.jsonl and events.txt, and writes soak-report.json:

- the warm-up: everything up to the end of the first race (the first 'pop <cars>_Race' screen
  event) is left out of the slopes;
- per metric (private bytes, working set, handles, threads, GDI and USER objects, the process's
  dedicated and committed GPU memory): the least-squares slope per hour after the warm-up, and
  the first, last, lowest and highest value;
- the step pattern per race: the lowest value of each race cycle (from the end of one race to
  the end of the next). Memory that rises each race and never comes back shows as floors that
  only climb: their slope per race and the number of rises in a row;
- the frame times: p50, p95 and p99 per minute of the run, and per race (its race windows), with
  the slope of the races' p50 and p99 per hour and the first third of the races against the last;
- the kernel and XAM stubs the run called (runtime.log): each stubbed function or unimplemented
  XAM message with its count and the times of its first and last call;
- for a run that revisits the same tracks and cars (quickrace_loop.py), the same metrics per pass
  and each pair's floor against the pass before (passes); and the process's handles by object
  type, first against last (handle_types).

The verdict uses the thresholds proposed for the season soak (29 October 2026): private bytes
grow at most 50 MB an hour, the process's dedicated GPU memory at most 25 MB an hour, handles at
most 100 and threads at most 5 an hour, and the races' p50 and p99 frame times of the last third
of the races are at most 10% above those of the first third.
"""
import argparse
import bisect
import json
import re
import statistics
import sys
from pathlib import Path

MB = 1 << 20
METRICS = ('private_bytes', 'working_set', 'pagefile', 'handles', 'threads', 'gdi', 'user',
           'gpu_dedicated', 'gpu_shared', 'gpu_committed')
BYTES = {'private_bytes', 'working_set', 'pagefile', 'gpu_dedicated', 'gpu_shared', 'gpu_committed'}
# The verdict's limits per hour after the warm-up (bytes for the memory metrics).
THRESHOLDS = {'private_bytes': 50 * MB, 'gpu_dedicated': 25 * MB, 'handles': 100, 'threads': 5}
FRAME_RISE = 0.10
RACE_PUSH = re.compile(r'^push \S+_Race$')
RACE_POP = re.compile(r'^pop \S+_Race$')
# The runtime's stub lines: '__imp__<name> STUB', 'Stub <name>!', '<name>(<args>) - stub' and
# 'Unimplemented XAM message app=<app>, msg=<msg>, ...'.
LOG_LINE = re.compile(r'^\[([\d\- :.]+)\] \[\w+\] \[(\w+)\] \[t\w+\] (.*)$')
STUBS = (re.compile(r'^__imp__(\w+) STUB'), re.compile(r'^Stub (\w+)!'), re.compile(r'^(\w+)\(.*\) - stub'),
         re.compile(r'^Unimplemented (XAM message app=\w+, msg=\w+)'))


def read_jsonl(path):
    if not path.is_file():
        return []
    rows = []
    for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
        try:
            rows.append(json.loads(line))
        except ValueError:
            pass  # a line cut off by a stopped run
    return rows


def read_events(path):
    events = []
    if path.is_file():
        for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
            parts = line.split(' ', 2)
            if len(parts) == 3 and parts[0].isdigit():
                events.append((int(parts[0]) / 1000, parts[2]))
    return events


def slope(points):
    """Least-squares slope of (x, y) points, or None with fewer than two distinct x."""
    if len(points) < 2:
        return None
    mean_x = sum(x for x, _ in points) / len(points)
    mean_y = sum(y for _, y in points) / len(points)
    var = sum((x - mean_x) ** 2 for x, _ in points)
    if not var:
        return None
    return sum((x - mean_x) * (y - mean_y) for x, y in points) / var


def percentile(values, fraction):
    values = sorted(values)
    if not values:
        return None
    index = min(len(values) - 1, max(0, round(fraction * (len(values) - 1))))
    return values[index]


def frame_stats(frames):
    if not frames:
        return None
    return {'frames': len(frames), 'p50': round(percentile(frames, 0.5), 3),
            'p95': round(percentile(frames, 0.95), 3), 'p99': round(percentile(frames, 0.99), 3)}


def races(events):
    """(push time, pop time) of every race, in unix seconds."""
    result, start = [], None
    for t, text in events:
        if RACE_PUSH.match(text):
            start = t
        elif RACE_POP.match(text) and start is not None:
            result.append((start, t))
            start = None
    return result


def stubs(path):
    """[{name, module, count, first, last}] of the stubs called in a runtime log, most called first.
    The runtime rotates its log at 5 MB and keeps four (runtime.1.log, the newest, to
    runtime.4.log): those are read too, oldest first; a long run's oldest lines are gone."""
    found = {}
    rotated = sorted(path.parent.glob(f'{path.stem}.[0-9]*{path.suffix}'),
                     key=lambda p: -int(p.suffixes[0][1:]) if p.suffixes[0][1:].isdigit() else 0)
    for part in rotated + ([path] if path.is_file() else []):
        read_stubs(part, found)
    return sorted(found.values(), key=lambda e: (-e['count'], e['name']))


def read_stubs(path, found):
    with open(path, encoding='utf-8', errors='replace') as log:
        for text in log:
            line = LOG_LINE.match(text.rstrip('\n'))
            if not line:
                continue
            for pattern in STUBS:
                match = pattern.match(line.group(3))
                if match:
                    entry = found.setdefault(match.group(1), {'name': match.group(1), 'module': line.group(2),
                                                              'count': 0, 'first': line.group(1)})
                    entry['count'] += 1
                    entry['last'] = line.group(1)
                    break


def show(metric, value):
    if value is None:
        return None
    return round(value / MB, 1) if metric in BYTES else round(value, 2)


PASS_LABEL = re.compile(r'^pass (\d+) pair (\d+)')


def passes(samples):
    """A run that revisits the same tracks and cars (quickrace_loop.py: labels 'pass <p> pair <i>:
    ...'): per pass and metric, the slope per hour inside the pass, each pair's floor (its lowest
    sample) and the floors' rise over the pass; and per pair the floor against the same pair's in
    the pass before. A cache stays level once a pass repeats what an earlier one loaded; a leak
    keeps rising on the repeat."""
    by_pass = {}
    for s in samples:
        match = PASS_LABEL.match(s.get('label') or '')
        if match:
            by_pass.setdefault(int(match.group(1)), []).append((int(match.group(2)), s))
    if not by_pass:
        return None
    result = {}
    floors = {}
    for number, items in sorted(by_pass.items()):
        entry = {'samples': len(items), 'hours': round((items[-1][1]['t'] - items[0][1]['t']) / 3600, 3),
                 'pairs': len({pair for pair, _ in items}), 'metrics': {}}
        for metric in METRICS:
            values = [(s['t'], s[metric]) for _, s in items if s.get(metric) is not None]
            if len(values) < 2:
                continue
            pair_floor = {}
            for pair, s in items:
                if s.get(metric) is not None:
                    pair_floor[pair] = min(pair_floor.get(pair, s[metric]), s[metric])
            floors[(number, metric)] = pair_floor
            ordered = [pair_floor[p] for p in sorted(pair_floor)]
            t0 = values[0][0]
            metric_entry = {
                'slope_per_hour': show(metric, slope([((t - t0) / 3600, v) for t, v in values])),
                'first_floor': show(metric, ordered[0]), 'last_floor': show(metric, ordered[-1]),
                'floor_rise': show(metric, ordered[-1] - ordered[0]),
                'floor_slope_per_pair': show(metric, slope(list(enumerate(ordered)))),
            }
            before = floors.get((number - 1, metric))
            if before:
                deltas = [pair_floor[p] - before[p] for p in sorted(pair_floor) if p in before]
                if deltas:
                    metric_entry['against_previous_pass'] = {
                        'median': show(metric, statistics.median(deltas)),
                        'first_pair': show(metric, deltas[0]), 'last_pair': show(metric, deltas[-1])}
            entry['metrics'][metric] = metric_entry
        result[str(number)] = entry
    return result


def handle_type_trend(samples):
    """The handles by object type at the first sample after the warm-up's end of each pass, or of
    the run's first and last samples: the types whose count changed most."""
    with_types = [s for s in samples if s.get('handle_types')]
    if len(with_types) < 2:
        return None
    first, last = with_types[0]['handle_types'], with_types[-1]['handle_types']
    change = {name: last.get(name, 0) - first.get(name, 0) for name in set(first) | set(last)}
    points = []
    for s in with_types:
        match = PASS_LABEL.match(s.get('label') or '')
        key = (match.group(1), match.group(2)) if match else None
        if key and (not points or points[-1]['pass_pair'] != list(key)):
            points.append({'pass_pair': list(key), 'uptime_s': s['uptime_s'], 'types': s['handle_types']})
    return {'first': first, 'last': last,
            'change': dict(sorted(((k, v) for k, v in change.items() if v), key=lambda kv: -abs(kv[1]))),
            'per_pair': points}


def guest_functions(run):
    """Sorted guest function starts and names from the game's linker map next to its
    executable (summary.json's binary): the recompiler names each function sub_<address>."""
    summary = run / 'summary.json'
    try:
        exe = Path(json.loads(summary.read_text())['binary']['path'])
    except (OSError, ValueError, KeyError, TypeError):
        return [], []
    starts = set()
    try:
        with open(exe.with_suffix('.map'), encoding='utf-8', errors='replace') as handle:
            for line in handle:
                match = re.search(r'\ssub_([0-9A-F]{8})\s', line)
                if match:
                    starts.add(int(match.group(1), 16))
    except OSError:
        return [], []
    ordered = sorted(starts)
    return ordered, [f'sub_{s:08X}' for s in ordered]


def physical_events(run, events):
    """The trace's physical allocations of 1 MB or more and its physical frees, with the guest
    functions they returned to (the call's and its caller's), and the allocations by caller."""
    if not events:
        return None
    starts, names = guest_functions(run)

    def function(address):
        if not address or not starts:
            return None
        index = bisect.bisect_right(starts, address) - 1
        return names[index] if index >= 0 else None
    mb = lambda v: round(v / MB, 2)
    listed = [{'event': e['event'], 'uptime_s': e['uptime_s'], 'address': f"{e['address']:08X}",
               'size_mb': mb(e['size']), 'page_size': e['page_size'],
               'caller': function(e['caller']), 'caller2': function(e['caller2'])} for e in events]
    by_caller = {}
    for e in listed:
        if e['event'] == 'alloc':
            key = f"{e['caller']} <- {e['caller2']}"
            by_caller[key] = round(by_caller.get(key, 0) + e['size_mb'], 2)
    allocations = [e for e in listed if e['event'] == 'alloc']
    return {'allocations': len(allocations), 'allocated_mb': round(sum(e['size_mb'] for e in allocations), 2),
            'frees': sum(e['event'] == 'free' for e in listed),
            'last_allocation_s': max((e['uptime_s'] for e in allocations), default=None),
            'by_caller_mb': dict(sorted(by_caller.items(), key=lambda kv: -kv[1])), 'events': listed}


def guest_memory(run, samples):
    """guest-mem.jsonl (REX_GUEST_MEM_TRACE): the guest's physical memory (of 512 MB) and virtual
    heaps: the peak committed physical bytes, when and where it was (the soak label of the
    nearest sample, and the pass's pair from qrloop.jsonl), the last values, and per pass the
    first and last committed physical bytes."""
    raw = read_jsonl(run / 'guest-mem.jsonl')
    lines = [l for l in raw if 'physical' in l]
    events = [l for l in raw if l.get('event')]
    if not lines:
        return None
    labelled = sorted((s['t'], s.get('label')) for s in samples)
    times = [t for t, _ in labelled]

    def label_at(unix):
        if not labelled:
            return None
        i = max(0, bisect.bisect_right(times, unix) - 1)
        return labelled[i][1]

    pairs = {}
    for cycle in read_jsonl(run / 'qrloop.jsonl'):
        pairs[(cycle['pass'], cycle['pair'])] = {k: cycle.get(k) for k in ('manufacturer', 'car', 'location', 'track')}
    peak = max(lines, key=lambda l: l['physical']['used'])
    where = label_at(peak['unix'])
    match = PASS_LABEL.match(where or '')
    mb = lambda v: round(v / MB, 1)
    result = {
        'samples': len(lines), 'physical_total_mb': 512,
        'peak_physical_used_mb': mb(peak['physical']['used']),
        'peak_uptime_s': peak['uptime_s'], 'peak_label': where,
        'peak_pair': pairs.get((int(match.group(1)), int(match.group(2)))) if match else None,
        'last': {name: {k: mb(v) for k, v in lines[-1][name].items()}
                 for name in ('physical', 'vA0000000', 'vC0000000', 'vE0000000', 'v00000000', 'v40000000')
                 if name in lines[-1]},
        'peak_virtual_used_mb': {name: mb(max(l[name]['used'] for l in lines if name in l))
                                 for name in ('v00000000', 'v40000000') if name in lines[0]},
        'available_pages_last': lines[-1].get('available_pages'),
    }
    result['physical_events'] = physical_events(run, events)
    per_pass = {}
    for line in lines:
        match = PASS_LABEL.match(label_at(line['unix']) or '')
        if match:
            per_pass.setdefault(match.group(1), []).append(line['physical']['used'])
    if per_pass:
        result['physical_used_per_pass_mb'] = {
            p: {'first': mb(v[0]), 'last': mb(v[-1]), 'min': mb(min(v)), 'max': mb(max(v))}
            for p, v in per_pass.items()}
    return result


def analyse(run):
    samples = read_jsonl(run / 'soak.jsonl')
    events = read_events(run / 'events.txt')
    perf = read_jsonl(run / 'perf.jsonl')
    race_windows = races(events)
    if not samples:
        raise SystemExit(f'{run}: no soak.jsonl samples')
    # The perf trace's 't' counts seconds from the process's start, as the sampler's uptime does
    # from its own start a moment later; the first sample ties the two clocks together.
    origin = samples[0]['t'] - samples[0]['uptime_s']
    warm_end = race_windows[0][1] if race_windows else samples[0]['t']
    after = [s for s in samples if s['t'] >= warm_end]
    report = {'run': str(run), 'samples': len(samples), 'hours': round((samples[-1]['t'] - samples[0]['t']) / 3600, 3),
              'warm_up_end_s': round(warm_end - origin, 1), 'races': len(race_windows),
              'units': 'MB for memory, counts otherwise; slopes per hour', 'metrics': {}}
    cycles = [(race_windows[i][1], race_windows[i + 1][1]) for i in range(len(race_windows) - 1)]
    for metric in METRICS:
        values = [(s['t'], s[metric]) for s in after if s.get(metric) is not None]
        if not values:
            continue
        per_hour = slope([((t - warm_end) / 3600, v) for t, v in values])
        floors = []
        for start, stop in cycles:
            inside = [v for t, v in values if start <= t < stop]
            if inside:
                floors.append(min(inside))
        rises = longest = 0
        for a, b in zip(floors, floors[1:]):
            rises = rises + 1 if b > a else 0
            longest = max(longest, rises)
        report['metrics'][metric] = {
            'slope_per_hour': show(metric, per_hour), 'first': show(metric, values[0][1]),
            'last': show(metric, values[-1][1]), 'min': show(metric, min(v for _, v in values)),
            'max': show(metric, max(v for _, v in values)),
            'race_floors': [show(metric, v) for v in floors],
            'floor_slope_per_race': show(metric, slope(list(enumerate(floors)))),
            'floor_rises_in_a_row': longest,
        }
    # Frame times per minute of the process's clock, and per race.
    minutes = {}
    for window in perf:
        minutes.setdefault(int(window['t'] // 60), []).extend(window.get('frame_ms', []))
    report['frame_ms_per_minute'] = [{'minute': m, **frame_stats(f)} for m, f in sorted(minutes.items()) if f]
    per_race = []
    for index, (start, stop) in enumerate(race_windows):
        frames = [f for w in perf if start - origin <= w['t'] <= stop - origin for f in w.get('frame_ms', [])]
        stats = frame_stats(frames)
        if stats:
            per_race.append({'race': index + 1, 'start_s': round(start - origin, 1),
                             'seconds': round(stop - start, 1), **stats})
    report['frame_ms_per_race'] = per_race
    trend = {}
    if len(per_race) >= 3:
        third = max(1, len(per_race) // 3)
        for key in ('p50', 'p99'):
            first = statistics.median(r[key] for r in per_race[:third])
            last = statistics.median(r[key] for r in per_race[-third:])
            trend[key] = {'first_third': round(first, 3), 'last_third': round(last, 3),
                          'rise': round(last / first - 1, 4) if first else None,
                          'slope_ms_per_hour': round(slope([(r['start_s'] / 3600, r[key]) for r in per_race]), 4)}
    report['frame_trend'] = trend
    # The verdict against the proposed thresholds.
    verdict = {}
    for metric, limit in THRESHOLDS.items():
        value = report['metrics'].get(metric, {}).get('slope_per_hour')
        limit_shown = show(metric, limit)
        verdict[metric] = {'slope_per_hour': value, 'limit': limit_shown,
                           'pass': None if value is None else value <= limit_shown}
    for key, values in trend.items():
        verdict[f'race_frame_{key}'] = {'rise': values['rise'], 'limit': FRAME_RISE,
                                        'pass': None if values['rise'] is None else values['rise'] <= FRAME_RISE}
    report['verdict'] = verdict
    report['passes'] = passes(samples)
    report['handle_types'] = handle_type_trend(samples)
    report['guest_memory'] = guest_memory(run, samples)
    report['stubs'] = stubs(run / 'runtime.log')
    report['pass'] = all(v['pass'] is not False for v in verdict.values())
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('run', type=Path)
    parser.add_argument('--json', action='store_true', help='print the whole report')
    options = parser.parse_args()
    report = analyse(options.run)
    (options.run / 'soak-report.json').write_text(json.dumps(report, indent=2))
    if options.json:
        print(json.dumps(report, indent=2))
        return
    print(f"{report['run']}: {report['hours']} h, {report['samples']} samples, {report['races']} races, "
          f"warm-up to {report['warm_up_end_s']} s")
    for metric, m in report['metrics'].items():
        print(f"  {metric}: {m['first']} -> {m['last']} (min {m['min']}, max {m['max']}), "
              f"{m['slope_per_hour']}/h, floors {m['floor_slope_per_race']}/race, "
              f"{m['floor_rises_in_a_row']} rises in a row")
    for key, t in report['frame_trend'].items():
        print(f"  race frame {key}: {t['first_third']} -> {t['last_third']} ms ({t['rise']:+.1%}), "
              f"{t['slope_ms_per_hour']} ms/h")
    for name, v in report['verdict'].items():
        print(f"  {name}: {'PASS' if v['pass'] else 'FAIL' if v['pass'] is False else 'n/a'} ({v})")
    for number, entry in (report.get('passes') or {}).items():
        print(f"  pass {number}: {entry['hours']} h, {entry['pairs']} pairs")
        for metric in ('private_bytes', 'gpu_dedicated', 'handles'):
            m = entry['metrics'].get(metric)
            if m:
                print(f"    {metric}: {m['slope_per_hour']}/h, floors {m['first_floor']} -> {m['last_floor']} "
                      f"({m['floor_rise']:+}), against the pass before {m.get('against_previous_pass')}")
    if report.get('handle_types'):
        print(f"  handle types changed: {report['handle_types']['change']}")
    guest = report.get('guest_memory')
    if guest:
        print(f"  guest physical memory: peak {guest['peak_physical_used_mb']} of 512 MB at "
              f"{guest['peak_uptime_s']} s ({guest['peak_label']}, {guest['peak_pair']}); "
              f"virtual peaks {guest['peak_virtual_used_mb']}; per pass {guest.get('physical_used_per_pass_mb')}")
        events = guest.get('physical_events')
        if events:
            print(f"  physical allocations of 1 MB or more: {events['allocations']} ({events['allocated_mb']} MB, "
                  f"the last at {events['last_allocation_s']} s), frees: {events['frees']}; "
                  f"by caller {events['by_caller_mb']}")
    print(f"  stubs called: {len(report['stubs'])}")
    for entry in report['stubs']:
        print(f"    {entry['count']:>8} {entry['module']} {entry['name']} ({entry['first']} to {entry['last']})")
    print('PASS' if report['pass'] else 'FAIL')
    sys.exit(0 if report['pass'] else 1)


if __name__ == '__main__':
    main()
