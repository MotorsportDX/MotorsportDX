"""Offline tests of the frozen-frame tools: frame_diff.py, parity.py and the summary fields of
e2e.py they read. Run: python outputs/FM4-build/e2e/test_e2e_tools.py"""
import contextlib, io, json, os, sys, tempfile, time, unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import compare  # noqa: E402
import e2e  # noqa: E402
import frame_diff  # noqa: E402
import parity  # noqa: E402

W, H = 96, 64


def write_ppm(path, pixels):
    path.write_bytes(b'P6\n%d %d\n255\n' % (W, H) + bytes(pixels))


def image(base=100, patch=None):
    """A gray frame; patch (x0, y0, x1, y1, value) paints a rectangle."""
    pixels = bytearray([base] * (W * H * 3))
    if patch:
        x0, y0, x1, y1, value = patch
        for y in range(y0, y1):
            pixels[(y * W + x0) * 3:(y * W + x1) * 3] = bytes([value]) * ((x1 - x0) * 3)
    return pixels


def make_run(root, name, frames, draws=1000.0, predicated=(40.0, 10.0), errors=None):
    """A run directory with captures of a freeze phase and its summary."""
    run = root / name
    (run / 'capture').mkdir(parents=True)
    names = []
    for index, pixels in enumerate(frames):
        stem = f'frame-{index + 1:03d}'
        write_ppm(run / 'capture' / f'{stem}.ppm', pixels)
        names.append(stem)
    phase = {'fps_mean': 60.0, 'draws_per_frame_mean': draws,
             'counts_per_frame_mean': {'predicated_run': predicated[0],
                                         'predicated_skipped': predicated[1]}}
    summary = {'crashed': None, 'goal_reached': True, 'phases': {'freeze': phase},
               'phase_frames': {'freeze': names}, 'errors': errors or {}}
    (run / 'summary.json').write_text(json.dumps(summary))
    return run


def gate(baseline, candidate, *extra):
    with contextlib.redirect_stdout(io.StringIO()) as out:
        code = parity.main([str(baseline), str(candidate), '--json', *extra])
    return code, json.loads(out.getvalue())


class FrameDiffTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_identical_frames(self):
        write_ppm(self.root / 'a.ppm', image())
        write_ppm(self.root / 'b.ppm', image())
        stats, _ = frame_diff.compare_images(self.root / 'a.ppm', self.root / 'b.ppm')
        self.assertEqual((stats['above'], stats['max'], stats['mean']), (0, 0, 0))

    def test_share_level_and_crop(self):
        write_ppm(self.root / 'a.ppm', image())
        # 16 x 8 pixels differ by 50, and the whole frame by 3 (below the level of 4).
        write_ppm(self.root / 'b.ppm', image(103, (64, 40, 80, 48, 150)))
        report = frame_diff.frame_diff(self.root / 'a.ppm', self.root / 'b.ppm',
                                       out=self.root / 'out', crops=2, crop_size=32)
        self.assertEqual(report['above'], 16 * 8)
        self.assertAlmostEqual(report['above_pct'], 100 * 128 / (W * H), places=3)
        self.assertEqual(report['max'], 50)
        self.assertAlmostEqual(report['above_pct_by_level']['2'], 100.0)
        self.assertEqual(len(report['crops']), 1)
        crop = report['crops'][0]
        self.assertTrue(crop['x'] <= 64 and crop['x'] + 32 >= 80 and crop['y'] <= 40)
        self.assertTrue(Path(crop['png']).read_bytes().startswith(b'\x89PNG'))
        self.assertTrue((self.root / 'out' / 'frame-diff.json').is_file())

    def test_ignore_region(self):
        write_ppm(self.root / 'a.ppm', image())
        write_ppm(self.root / 'b.ppm', image(100, (0, 56, W, H, 200)))
        stats, _ = frame_diff.compare_images(self.root / 'a.ppm', self.root / 'b.ppm',
                                             regions=[(0, 0.85, 1, 1)])
        self.assertEqual(stats['above'], 0)

    def test_bad_input(self):
        with self.assertRaises(Exception):
            frame_diff.parse_region('0.5,0,0.2,1')
        write_ppm(self.root / 'a.ppm', image())
        (self.root / 'b.ppm').write_bytes(b'P6\n%d %d\n255\n' % (W, H) + bytes(image())[:100])
        with self.assertRaises(SystemExit):
            frame_diff.compare_images(self.root / 'a.ppm', self.root / 'b.ppm')
        run = make_run(self.root, 'r', [image(), image()])
        with self.assertRaises(SystemExit):
            frame_diff.frame_diff(run, run, phase='race')

    def test_crops_at_high_level(self):
        write_ppm(self.root / 'a.ppm', image())
        write_ppm(self.root / 'b.ppm', image(140, (0, 0, 16, 16, 200)))
        report = frame_diff.frame_diff(self.root / 'a.ppm', self.root / 'b.ppm', level=64,
                                       out=self.root / 'out', crops=1, crop_size=32)
        self.assertEqual(len(report['crops']), 1)

    def test_run_directory_uses_last_capture_and_stability(self):
        a = make_run(self.root, 'a', [image(90), image(), image()])
        b = make_run(self.root, 'b', [image(), image(), image(100, (0, 0, 8, 8, 0))])
        report = frame_diff.frame_diff(a, b)
        self.assertEqual(report['above'], 64)
        self.assertEqual(report['stability']['a']['above_pct'], 0)
        self.assertGreater(report['stability']['b']['above_pct'], 0)


class ParityTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.base = make_run(self.root, 'base', [image(), image()])

    def tearDown(self):
        self.tmp.cleanup()

    def test_pass(self):
        code, report = gate(self.base, make_run(self.root, 'c', [image(), image()], draws=1015))
        self.assertEqual((code, report['verdict']), (0, 'PASS'))

    def test_draws_and_predication_fail(self):
        code, report = gate(self.base, make_run(self.root, 'c', [image(), image()], draws=950,
                                                predicated=(40.0, 0.0)))
        self.assertEqual(code, 1)
        self.assertTrue(any('draws per frame' in f for f in report['fail']))
        self.assertTrue(any('predicated_skipped' in f for f in report['fail']))

    def test_image_fail_and_limit(self):
        changed = image(100, (0, 0, 48, 32, 180))  # a quarter of the frame
        candidate = make_run(self.root, 'c', [changed, changed])
        code, report = gate(self.base, candidate)
        self.assertEqual(code, 1)
        self.assertTrue(any('frozen frame differs' in f for f in report['fail']))
        self.assertEqual(gate(self.base, candidate, '--image-pct', '30')[0], 0)

    def test_freeze_did_not_hold(self):
        code, report = gate(self.base, make_run(self.root, 'c', [image(180), image(), image()]))
        self.assertEqual(code, 0)
        code, report = gate(self.base, make_run(self.root, 'd', [image(), image(180), image()]))
        self.assertEqual(code, 1)
        self.assertTrue(any('did not hold' in f for f in report['fail']))

    def test_new_error_key(self):
        code, report = gate(self.base, make_run(self.root, 'c', [image(), image()],
                                                errors={'rejected draw #': 3}))
        self.assertEqual(code, 1)
        self.assertTrue(any('new error key' in f for f in report['fail']))

    def test_repeated_runs(self):
        for name in ('rb', 'rc'):
            for index in (1, 2):
                make_run(self.root / name, f'run{index}', [image(), image()])
        code, report = gate(self.root / 'rb', self.root / 'rc')
        self.assertEqual(code, 0)
        self.assertTrue(any(r['metric'].startswith('run2: ') for r in report['rows']))


class SummaryTests(unittest.TestCase):
    def window(self, **extra):
        return {'window_ms': 1000, 'fps': 50, 'frame_ms': [20.0] * 50, 'draws': 5000,
                'readbacks': 0, 'wait_ms': {'guest_idle': 600.0, 'other': 100.0}, **extra}

    def test_draws_per_frame_and_cp_render_thread(self):
        result = e2e.summarize([self.window()] * 3, 0)
        self.assertEqual(result['draws_per_frame_mean'], 100)
        self.assertEqual(result['render_thread_source'], 'cp')
        self.assertEqual(result['render_thread_busy_ms_per_frame_median'], 6)
        self.assertNotIn('guest_render_thread_ms_per_frame_median', result)

    def test_native_fields(self):
        result = e2e.summarize([self.window(render_thread_busy_ms=100.0, guest_render_ms=250.0,
                                            queue_depth_mean=0.5, queue_depth_max=2)] * 3, 0)
        self.assertEqual(result['render_thread_source'], 'render_thread')
        self.assertEqual(result['render_thread_busy_ms_per_frame_median'], 2)
        self.assertEqual(result['guest_render_thread_ms_per_frame_median'], 5)
        self.assertEqual((result['queue_depth_median'], result['queue_depth_max']), (0.5, 2))

    def test_aggregate_keeps_new_fields(self):
        run = e2e.summarize([self.window()] * 3, 0)
        run.update(crashed=None, goal_reached=True, goal=None, scenario='freeze', args=[],
                   final_capture=None, phases={'freeze': dict(run)})
        summary = e2e.aggregate([run, run])
        self.assertEqual(summary['draws_per_frame_mean'], 100)
        self.assertEqual(summary['phases']['freeze']['draws_per_frame_mean'], 100)
        self.assertEqual(summary['render_thread_source'], 'cp')

    def test_frames_by_phase(self):
        with tempfile.TemporaryDirectory() as tmp:
            frames = []
            for index, at in enumerate((10, 20, 30)):
                path = Path(tmp) / f'frame-{index + 1:03d}.ppm'
                path.write_bytes(b'')
                os.utime(path, (at, at))
                frames.append(path)
            marks = [('race', 5), ('end', 15), ('freeze', 18)]
            self.assertEqual(e2e.frames_by_phase(frames, marks, 40),
                             {'race': ['frame-001'], 'freeze': ['frame-002', 'frame-003']})

    def test_windows_by_phase_and_resummarize(self):
        self.assertEqual(e2e.windows_by_phase([('race', 2), ('end', 5), ('freeze', 7)], 9),
                         {'race': [2, 5], 'freeze': [7, 9]})
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp)
            perf = [dict(self.window(), t=float(t), draws=1000 * t) for t in range(1, 11)]
            (run / 'perf.jsonl').write_text(''.join(json.dumps(p) + '\n' for p in perf))
            # A run with phase windows gets its phases recomputed.
            (run / 'summary.json').write_text(json.dumps({'phase_windows': {'freeze': [4, 8]}}))
            summary = e2e.resummarize(run, 0)
            self.assertEqual(summary['phases']['freeze']['draws_per_frame_mean'], 130)
            # An older run keeps its stored phases: its timeline and the windows' t have
            # different origins.
            stored = {'freeze': {'fps_mean': 1.0}}
            (run / 'summary.json').write_text(json.dumps(
                {'timeline': [[4.5, 'phase:freeze'], [8.5, 'phase:end']], 'phases': stored}))
            summary = e2e.resummarize(run, 0)
            self.assertEqual(summary['phases'], stored)
            self.assertNotIn('phase_windows', summary)

    def test_guest_render_from_n0_trace(self):
        self.assertEqual(e2e.steady_by_phase([('race', 1000.0), ('freeze', 5000.0)], 9000.0),
                         {'race': [1000.0, 5000.0], 'freeze': [5000.0, 9000.0]})
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'n0.jsonl'
            # One line per second; the race seconds take 4 ms per frame, the frozen ones 2 ms.
            lines = [{'steady_ms': 1000.0 * s, 'render': {'calls': 60, 'ms': 240.0 if s <= 5 else 120.0}}
                     for s in range(2, 10)]
            lines.append({'steady_ms': 10000.0, 'render': {'calls': 0, 'ms': 0.0}})
            path.write_text(''.join(json.dumps(l) + '\n' for l in lines))
            summary = {'n0_trace': str(path), 'phases': {'race': {}, 'freeze': {}},
                       'phase_steady_ms': {'race': [1000.0, 5000.0], 'freeze': [5000.0, 9000.0]}}
            e2e.guest_render_from_n0(summary)
            self.assertEqual(summary['phases']['race']['guest_render_thread_ms_per_frame_median'], 4)
            self.assertEqual(summary['phases']['freeze']['guest_render_thread_ms_per_frame_median'], 2)
            self.assertEqual(summary['guest_render_thread_source'], 'n0_trace')
            # The title appends: a later run reads only its own lines.
            offset = path.stat().st_size
            with open(path, 'a') as file:
                file.write(json.dumps({'steady_ms': 20000.0, 'render': {'calls': 10, 'ms': 90.0}}) + '\n')
            summary = {'n0_trace': str(path), 'n0_trace_offset': offset}
            e2e.guest_render_from_n0(summary)
            self.assertEqual(summary['guest_render_thread_ms_per_frame_median'], 9)
            # A value from the perf trace wins.
            summary = {'n0_trace': str(path), 'guest_render_thread_ms_per_frame_median': 7}
            e2e.guest_render_from_n0(summary)
            self.assertEqual(summary['guest_render_thread_ms_per_frame_median'], 7)

    def test_compare_ignores_new_fields(self):
        b = compare.normalize({'fps_mean': 60, 'errors': {}, 'draws_per_frame_mean': 100})
        c = compare.normalize({'fps_mean': 60, 'errors': {}, 'draws_per_frame_mean': 10})
        self.assertEqual(compare.check_fail(b, c), [])


if __name__ == '__main__':
    unittest.main()
