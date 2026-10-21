"""Backend-neutral parity gate of two frozen-frame runs (the freeze scenario, docs/e2e/frozen-frames.md).

  parity.py <baseline run> <candidate run> [--phase freeze] [--out DIR] [--json]

FAIL when the candidate crashed, missed its goal or a phase, has an error key the baseline does
not have, or, in the frozen phase, when the draws per frame, the predicated run or skipped
packets per frame, or the share of pixels that differ from the baseline's frozen frame exceed
their limits, or when either run's own freeze did not hold. Performance stays with compare.py:
a native stage runs both. The backend-neutral metrics (render thread, guest render thread,
queue depth) are listed when the summaries have them and never fail.
Exit 1 on FAIL, 2 on bad input, else 0. Repeated runs (run1..runN) are compared run by run.
"""
import argparse, json, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import compare  # noqa: E402
import frame_diff  # noqa: E402

# Limits of the frozen phase (docs/e2e/frozen-frames.md, "Baseline variance"). Two seeded
# baseline runs of 21 October 2026 differed by at most 0.14% in draws and predicated packets
# per frame; their frozen frames differed in 24-28% of the pixels, because the rewind's oldest
# frame moves with the time of the Y press, so the image limit is a target for a frozen moment
# pinned to the frame, not a measured one.
IMAGE_LEVEL = 4           # a pixel differs above 4 of 255 (frame_diff.LEVEL)
IMAGE_FAIL_PCT = 1.0      # FAIL when more than 1% of the pixels differ above the level
STABLE_FAIL_PCT = 0.05    # a run's freeze held when its own captures differ in at most 0.05%
DRAWS_FAIL_REL = 0.01     # draws per frame more than 1% away from the baseline
PREDICATED_FAIL_REL = 0.01  # predicated run or skipped packets per frame more than 1% away
PREDICATED_KEYS = ('predicated_run', 'predicated_skipped')
NEUTRAL_KEYS = ('render_thread_busy_ms_per_frame_median', 'guest_render_thread_ms_per_frame_median',
                'queue_depth_median', 'queue_depth_max', 'draws_per_frame_mean')


def runs_of(path):
    """The single-run directories of a run (run1..runN of a --repeat run, else itself)."""
    path = Path(path)
    runs = sorted(path.glob('run[0-9]*'), key=lambda d: int(d.name[3:]))
    return [r for r in runs if (r / 'summary.json').is_file()] or [path]


def phase_of(summary, phase):
    return (summary.get('phases') or {}).get(phase) or {}


def relative(b, c):
    return compare.rel_change(b, c) if compare.num(b) and compare.num(c) else None


def check_run(b, c, phase, b_dir, c_dir, out, limits):
    """FAIL reasons and rows of one baseline and candidate run."""
    fail, rows = [], []
    if c.get('crashed'):
        fail.append(f"candidate crashed: {c['crashed']}")
    if c.get('goal_reached') is False:
        fail.append(f"candidate goal_reached is False; {c.get('failed_step')}")
    for name in sorted(set(b.get('phases') or {}) - set(c.get('phases') or {})):
        fail.append(f'phase {name} missing in candidate')
    for k in sorted(set(c['errors']) - set(b['errors'])):
        fail.append(f"new error key in candidate: {k} (x{c['errors'][k]})")
    bp, cp = phase_of(b, phase), phase_of(c, phase)
    if not bp or not cp:
        fail.append(f'phase {phase} missing in the baseline or the candidate')
        return fail, rows
    rel = relative(bp.get('draws_per_frame_mean'), cp.get('draws_per_frame_mean'))
    rows.append(compare.row(f'{phase}.draws_per_frame', bp.get('draws_per_frame_mean'),
                            cp.get('draws_per_frame_mean')))
    if rel is None:
        fail.append(f'{phase} draws per frame missing')
    elif rel > limits['draws']:
        fail.append(f"{phase} draws per frame {cp['draws_per_frame_mean']} is more than "
                    f"{limits['draws']:.0%} away from the baseline {bp['draws_per_frame_mean']}")
    bc, cc = bp.get('counts_per_frame_mean') or {}, cp.get('counts_per_frame_mean') or {}
    for key in PREDICATED_KEYS:
        rows.append(compare.row(f'{phase}.{key}_per_frame', bc.get(key), cc.get(key)))
        rel = relative(bc.get(key), cc.get(key))
        if rel is None:
            fail.append(f'{phase} {key} per frame missing')
        elif rel > limits['predicated']:
            fail.append(f'{phase} {key} per frame {cc[key]} is more than {limits["predicated"]:.0%} '
                        f'away from the baseline {bc[key]}')
    for key in NEUTRAL_KEYS[:-1]:
        if key in bp or key in cp:
            rows.append(compare.row(f'{phase}.{key}', bp.get(key), cp.get(key)))
    try:
        image = frame_diff.frame_diff(b_dir, c_dir, phase, limits['level'], limits['ignore'],
                                      out / c_dir.name if out else None)
    except SystemExit as error:
        fail.append(f'frozen frames: {error}')
        return fail, rows
    rows.append({'metric': f'{phase}.image above {limits["level"]} (%)', 'baseline': None,
                 'candidate': image['above_pct'], 'delta': None, 'delta_pct': None})
    if image['above_pct'] > limits['image']:
        fail.append(f"{phase} frozen frame differs in {image['above_pct']:.3f}% of the pixels above "
                    f"{limits['level']} (limit {limits['image']}%)" +
                    (f"; crops in {out / c_dir.name}" if out else ''))
    for name, s in image['stability'].items():
        if s['above_pct'] > limits['stable']:
            who = 'baseline' if name == 'a' else 'candidate'
            fail.append(f"{phase} freeze of the {who} did not hold: its own captures differ in "
                        f"{s['above_pct']:.3f}% (limit {limits['stable']}%)")
    return fail, rows


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('baseline')
    parser.add_argument('candidate')
    parser.add_argument('--phase', default='freeze')
    parser.add_argument('--out', help='directory for the frame difference reports and crops')
    parser.add_argument('--image-pct', type=float, default=IMAGE_FAIL_PCT)
    parser.add_argument('--level', type=int, default=IMAGE_LEVEL)
    parser.add_argument('--draws', type=float, default=DRAWS_FAIL_REL)
    parser.add_argument('--predicated', type=float, default=PREDICATED_FAIL_REL)
    parser.add_argument('--stable-pct', type=float, default=STABLE_FAIL_PCT)
    parser.add_argument('--ignore', type=frame_diff.parse_region, action='append', default=[],
                        help='x0,y0,x1,y1 fractions of the frame left out of the image check')
    parser.add_argument('--json', action='store_true')
    options = parser.parse_args(argv)
    limits = {'image': options.image_pct, 'level': options.level, 'draws': options.draws,
              'predicated': options.predicated, 'stable': options.stable_pct, 'ignore': options.ignore}
    out = Path(options.out) if options.out else None
    if out and frame_diff.e2e.REPO in out.resolve().parents:
        parser.error('--out must be outside the repository')
    base_runs, cand_runs = runs_of(options.baseline), runs_of(options.candidate)
    fail, rows = [], []
    for index, (b_dir, c_dir) in enumerate(zip(base_runs, cand_runs)):
        b = compare.normalize(compare.load(str(b_dir)))
        c = compare.normalize(compare.load(str(c_dir)))
        f, r = check_run(b, c, options.phase, b_dir, c_dir, out, limits)
        prefix = f'run{index + 1}: ' if len(base_runs) > 1 else ''
        fail += [prefix + item for item in f]
        rows += [dict(row, metric=prefix + row['metric']) for row in r]
    if len(base_runs) != len(cand_runs):
        fail.append(f'{len(base_runs)} baseline runs against {len(cand_runs)} candidate runs')
    verdict = 'FAIL' if fail else 'PASS'
    if options.json:
        print(json.dumps({'rows': rows, 'fail': fail, 'verdict': verdict}, indent=2))
    else:
        compare.print_table(rows)
        compare.print_list('FAIL', fail)
        print(f'VERDICT: {verdict}')
    return 1 if fail else 0


if __name__ == '__main__':
    sys.exit(main())
