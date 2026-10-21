"""Pixel difference of two frozen frames: the share of pixels above a threshold and 1:1 crops of
the largest differences (docs/e2e/frozen-frames.md).

  frame_diff.py <a> <b> [--out DIR] [--level 4] [--phase freeze] [--crops 6] [--json]

<a> and <b> are PPM captures (--keep-ppm) or run directories. For a run directory the frame is
the last capture of --phase (summary.json "phase_frames"), and the run's own stability is
measured too: its first settled capture of the phase against that last one.

A pixel differs by the largest of its three channel differences (0 to 255); it counts as
changed above --level. --out writes frame-diff.json and the crops: a PNG per area with a, b
and the difference side by side (gray: difference times 8 below the level; red to yellow above
it). Standard library only.
"""
import argparse, json, operator, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import e2e  # noqa: E402

LEVEL = 4             # changed above 4 of 255, as the TAA still-picture checks count
LEVELS = (1, 2, 4, 8, 16, 32, 64)
BLOCK = 32            # crops are ranked by changed pixels per block of BLOCK x BLOCK pixels
SETTLE = 1            # captures of the phase skipped before its stability is measured


def pixel_diff(a, b):
    """Per pixel the largest channel difference, as bytes."""
    channels = bytes(map(abs, map(operator.sub, a, b)))
    return bytes(map(max, channels[0::3], channels[1::3], channels[2::3]))


def mask_regions(diff, width, height, regions):
    """Zero the difference inside regions given as fractions x0,y0,x1,y1 of the frame."""
    if not regions:
        return diff
    diff = bytearray(diff)
    for x0, y0, x1, y1 in regions:
        left, right = int(x0 * width), int(x1 * width)
        for y in range(int(y0 * height), int(y1 * height)):
            diff[y * width + left:y * width + right] = bytes(right - left)
    return bytes(diff)


def above_mask(diff, level):
    """1 where the difference is above level, else 0."""
    return diff.translate(bytes(int(v > level) for v in range(256)))


def block_counts(mask, diff, width, height, block=BLOCK):
    """Changed pixels and the sum of the differences per block, as {(bx, by): (count, sum)}
    for the blocks with any changed pixel."""
    counts = {}
    for y in range(height):
        row = y * width
        by = y // block
        for bx in range(0, (width + block - 1) // block):
            start, end = row + bx * block, row + min(width, (bx + 1) * block)
            n = mask.count(1, start, end)
            if n:
                c, total = counts.get((bx, by), (0, 0))
                counts[(bx, by)] = (c + n, total + sum(diff[start:end]))
    return counts


def pick_crops(counts, width, height, count, size, block=BLOCK):
    """Crop rectangles (x, y, w, h, changed pixels in the block) around the blocks with the
    most changed pixels (ties: the larger differences), without overlapping each other."""
    size_w, size_h = min(size, width), min(size, height)
    picked = []
    for (bx, by), (n, _) in sorted(counts.items(), key=lambda kv: (-kv[1][0], -kv[1][1])):
        if len(picked) >= count:
            break
        cx, cy = bx * block + block // 2, by * block + block // 2
        x = max(0, min(width - size_w, cx - size_w // 2))
        y = max(0, min(height - size_h, cy - size_h // 2))
        if any(x < px + pw and px < x + size_w and y < py + ph and py < y + size_h
               for px, py, pw, ph, _ in picked):
            continue
        picked.append((x, y, size_w, size_h, n))
    return picked


def crop_panel(a, b, diff, width, rect, level, gap=4):
    """One PNG row: a, b and the difference of rect, 1:1, with a white gap between them."""
    x, y, w, h, _ = rect
    rows = bytearray()
    white = b'\xff' * (gap * 3)
    for line in range(y, y + h):
        start = line * width + x
        rows += a[start * 3:(start + w) * 3] + white + b[start * 3:(start + w) * 3] + white
        for v in diff[start:start + w]:
            rows += bytes((255, min(255, 128 + v * 2), 0)) if v > level else bytes((v * 8,) * 3)
    return 3 * w + 2 * gap, h, bytes(rows)


def compare_images(a_path, b_path, level=LEVEL, regions=()):
    """Statistics of the difference of two PPM captures, and the data for crops."""
    wa, ha, a = e2e.read_ppm(Path(a_path))
    wb, hb, b = e2e.read_ppm(Path(b_path))
    if (wa, ha) != (wb, hb):
        raise SystemExit(f'size differs: {a_path} {wa}x{ha}, {b_path} {wb}x{hb}')
    a, b = a[:wa * ha * 3], b[:wb * hb * 3]
    diff = mask_regions(pixel_diff(a, b), wa, ha, regions)
    pixels = wa * ha
    mask = above_mask(diff, level)
    above = mask.count(1)
    stats = {
        'a': str(a_path), 'b': str(b_path), 'width': wa, 'height': ha, 'level': level,
        'above': above, 'above_pct': round(100 * above / pixels, 4),
        'mean': round(sum(diff) / pixels, 4), 'max': max(diff),
        'above_pct_by_level': {str(v): round(100 * above_mask(diff, v).count(1) / pixels, 4)
                               for v in LEVELS},
    }
    return stats, (a, b, diff, mask)


def write_crops(out, stem, data, width, height, level, count, size):
    a, b, diff, mask = data
    crops = []
    for index, rect in enumerate(pick_crops(block_counts(mask, diff, width, height), width, height,
                                            count, size)):
        w, h, rgb = crop_panel(a, b, diff, width, rect, level)
        path = out / f'{stem}-crop{index + 1}-x{rect[0]}-y{rect[1]}.png'
        e2e.write_png(path, w, h, rgb, 1)
        crops.append({'x': rect[0], 'y': rect[1], 'w': rect[2], 'h': rect[3],
                      'block_above': rect[4], 'png': str(path)})
    return crops


def phase_ppms(run, phase):
    """The PPM captures of a phase of a run, in order (all captures without phase_frames)."""
    run = Path(run)
    capture = run / 'capture'
    names = None
    summary = run / 'summary.json'
    if summary.is_file():
        names = (json.loads(summary.read_text()).get('phase_frames') or {}).get(phase)
    frames = [capture / f'{n}.ppm' for n in names] if names is not None else sorted(capture.glob('frame-*.ppm'))
    frames = [f for f in frames if f.is_file()]
    if not frames:
        raise SystemExit(f'no PPM captures of phase {phase!r} in {capture} (run e2e.py with --keep-ppm)')
    return frames


def resolve(path, phase):
    """(frame to compare, stability frames or None) of a PPM path or a run directory."""
    path = Path(path)
    if path.is_dir():
        frames = phase_ppms(path, phase)
        settled = frames[min(SETTLE, len(frames) - 1)]
        return frames[-1], (settled, frames[-1]) if settled != frames[-1] else None
    return path, None


def parse_region(text):
    values = [float(v) for v in text.split(',')]
    if len(values) != 4 or not all(0 <= v <= 1 for v in values):
        raise argparse.ArgumentTypeError('a region is x0,y0,x1,y1 as fractions of the frame')
    return tuple(values)


def frame_diff(a, b, phase='freeze', level=LEVEL, regions=(), out=None, crops=6, crop_size=256):
    """The report of two frames or runs: the difference, each run's stability, the crops."""
    frame_a, stable_a = resolve(a, phase)
    frame_b, stable_b = resolve(b, phase)
    stats, data = compare_images(frame_a, frame_b, level, regions)
    report = {'phase': phase, **stats, 'stability': {}}
    for name, pair in (('a', stable_a), ('b', stable_b)):
        if pair:
            s, _ = compare_images(*pair, level, regions)
            report['stability'][name] = {'first': str(pair[0]), 'last': str(pair[1]),
                                         'above_pct': s['above_pct'], 'max': s['max']}
    if out:
        out = Path(out)
        out.mkdir(parents=True, exist_ok=True)
        report['crops'] = write_crops(out, 'diff', data, stats['width'], stats['height'], level,
                                      crops, crop_size)
        (out / 'frame-diff.json').write_text(json.dumps(report, indent=2))
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('a', help='PPM capture or run directory (the baseline)')
    parser.add_argument('b', help='PPM capture or run directory (the candidate)')
    parser.add_argument('--out', help='directory for frame-diff.json and the crops (outside the repository)')
    parser.add_argument('--phase', default='freeze', help='phase whose last capture a run directory gives')
    parser.add_argument('--level', type=int, default=LEVEL, help='a pixel changed above this difference')
    parser.add_argument('--ignore', type=parse_region, action='append', default=[],
                        help='x0,y0,x1,y1 fractions of the frame left out; repeatable')
    parser.add_argument('--crops', type=int, default=6, help='number of crops')
    parser.add_argument('--crop-size', type=int, default=256, help='crop width and height in pixels')
    parser.add_argument('--json', action='store_true', help='print the report as JSON')
    options = parser.parse_args(argv)
    if options.out and e2e.REPO in Path(options.out).resolve().parents:
        parser.error('--out must be outside the repository')
    report = frame_diff(options.a, options.b, options.phase, options.level, options.ignore,
                        options.out, options.crops, options.crop_size)
    if options.json:
        print(json.dumps(report, indent=2))
    else:
        print(f"{report['width']}x{report['height']}: {report['above_pct']:.4f}% of the pixels above "
              f"{report['level']} (mean {report['mean']:.3f}, max {report['max']})")
        print('  by level: ' + ', '.join(f'>{k} {v:.4f}%' for k, v in report['above_pct_by_level'].items()))
        for name, s in report['stability'].items():
            print(f"  stability {name}: {s['above_pct']:.4f}% above {report['level']} (max {s['max']})")
        for crop in report.get('crops', []):
            print(f"  crop {crop['png']}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
