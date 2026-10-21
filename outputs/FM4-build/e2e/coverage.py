"""The PM4 writers of each phase of a run (docs/analysis/d3d-hle.md, section 14.7).

  coverage.py <run> --functions <BuildRoot>/project/generated/default [--json]

Reads the run's FM4_N0_TRACE lines (the "alloc" table of each second: ring allocations and
segment overflows by allocator and return address) and its perf trace (the identity check's
"checks" by writer, with host_render_check), and sums them for each phase of summary.json and
for the menus, which the career scenario does not mark as a phase: from the step that waits
for PLAY CAREER to the selection of START RACE. Each return address is taken to the function
that contains it, the greatest sub_ address of the generated code at or below it.
"""
import argparse, bisect, json, re, sys
from pathlib import Path

ALLOCATORS = ('BeginCommand', 'AllocateRingSpace', 'overflow')


def functions(directory):
    """Sorted guest addresses of the functions of the generated code."""
    found = set()
    for path in Path(directory).glob('fm4_rex_funcs.*.h'):
        found.update(int(m, 16) for m in re.findall(r'DECLARE_REX_FUNC\(sub_([0-9A-F]{8})\)',
                                                    path.read_text(errors='replace')))
    return sorted(found)


def containing(starts, address):
    i = bisect.bisect_right(starts, address) - 1
    return starts[i] if i >= 0 else None


def menu_bounds(summary):
    """The menus on the run's clock (seconds from the launch): from the wait for PLAY CAREER
    to the selection of START RACE, or None."""
    start = end = None
    for step in summary.get('timeline', []):
        action = step[1] if len(step) > 1 else ''
        pattern = step[2] if len(step) > 2 and isinstance(step[2], str) else ''
        if start is None and 'PLAY CAREER' in pattern:
            start = step[0]
        if start is not None and action == 'select:START RACE':
            end = step[0]
            break
    return (start, end) if start is not None and end is not None else None


def phase_mark(summary, name):
    for step in summary.get('timeline', []):
        if len(step) > 1 and step[1] == f'phase:{name}':
            return step[0]
    return None


def windows(summary, perf, n0):
    """{phase: (n0 lines, perf windows)}."""
    result = {}
    steady = summary.get('phase_steady_ms') or {}
    marks = summary.get('phase_windows') or {}
    for name, (start, end) in steady.items():
        lines = [l for l in n0 if start <= l.get('steady_ms', 0) - 1000 and l.get('steady_ms', 0) <= end]
        a, b = marks.get(name, (0, 0))
        result[name] = (lines, perf[a:b])
    menu = menu_bounds(summary)
    anchor = next(((name, phase_mark(summary, name)) for name in steady if phase_mark(summary, name)), None)
    if menu and anchor and anchor[0] in marks:
        name, mark = anchor
        # Offsets from the run's clock to the steady clock and to the perf trace's clock.
        to_steady = steady[name][0] - mark * 1000
        to_perf = perf[marks[name][0]]['t'] - mark if marks[name][0] < len(perf) else None
        start, end = menu[0] * 1000 + to_steady, menu[1] * 1000 + to_steady
        lines = [l for l in n0 if start <= l.get('steady_ms', 0) - 1000 and l.get('steady_ms', 0) <= end]
        perf_menu = ([p for p in perf if menu[0] <= p['t'] - to_perf <= menu[1]]
                     if to_perf is not None else [])
        result = {'menu': (lines, perf_menu), **result}
    return result


def tally(lines, perf, starts):
    writers = {}
    for line in lines:
        for key, (calls, dwords) in (line.get('alloc') or {}).items():
            allocator, address = key.split(':')
            function = containing(starts, int(address, 16)) if starts else None
            name = f'sub_{function:08X}' if function else f'lr {address}'
            entry = writers.setdefault(name, {a: [0, 0] for a in ALLOCATORS})
            entry[allocator][0] += calls
            entry[allocator][1] += dwords
    checks = {}
    for window in perf:
        for writer, count in ((window.get('host_render') or {}).get('checks') or {}).items():
            checks[f'sub_{writer}'] = checks.get(f'sub_{writer}', 0) + count
    frames = sum(len(p.get('frame_ms', [])) for p in perf)
    seconds = len(lines)
    return {'seconds': seconds, 'frames': frames, 'allocations': writers, 'checks': checks}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('run', type=Path)
    parser.add_argument('--functions', type=Path, help='the generated code with fm4_rex_funcs.*.h')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    summary = json.loads((args.run / 'summary.json').read_text())
    path = summary.get('n0_trace')
    if not path or not Path(path).is_file():
        sys.exit('the run has no FM4_N0_TRACE file (summary.json n0_trace)')
    with open(path, 'rb') as file:
        file.seek(summary.get('n0_trace_offset', 0))
        n0 = [json.loads(l) for l in file.read().decode().splitlines() if l.strip()]
    perf = [json.loads(l) for l in (args.run / 'perf.jsonl').read_text().splitlines() if l.strip()]
    starts = functions(args.functions) if args.functions else []
    result = {name: tally(lines, windows_, starts)
              for name, (lines, windows_) in windows(summary, perf, n0).items()}
    if args.json:
        print(json.dumps(result, indent=1))
        return
    for name, phase in result.items():
        print(f"## {name}: {phase['seconds']} s of the D3D layer trace, {phase['frames']} frames")
        print('writer                BeginCommand        AllocateRingSpace   overflow')
        for writer, entry in sorted(phase['allocations'].items()):
            cells = ''.join(f"{entry[a][0]:>9} {entry[a][1]:>9} " if entry[a][0] else f"{'':>20}"
                            for a in ALLOCATORS)
            print(f'{writer:<20}  {cells}')
        if phase['checks']:
            print('checked ranges by writer (outermost hooked writer):')
            for writer, count in sorted(phase['checks'].items()):
                print(f'  {writer}  {count}')
        print()


if __name__ == '__main__':
    main()
