"""Offline tests of the frozen-frame tools: frame_diff.py, parity.py, the summary fields of
e2e.py they read, the input file of the freeze scenario's pulses and the screen events of the
fast route.
Run: python outputs/FM4-build/e2e/test_e2e_tools.py"""
import contextlib, io, json, os, random, sys, tempfile, time, unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import compare  # noqa: E402
import cpu_power  # noqa: E402
import e2e  # noqa: E402
import frame_diff  # noqa: E402
import parity  # noqa: E402
import perf_ab  # noqa: E402

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
        changed = image(100, (0, 0, W, 48, 180))  # three quarters of the frame
        candidate = make_run(self.root, 'c', [changed, changed])
        code, report = gate(self.base, candidate)
        self.assertEqual(code, 1)
        self.assertTrue(any('frozen frame differs' in f for f in report['fail']))
        self.assertEqual(gate(self.base, candidate, '--image-pct', '80')[0], 0)
        # A quarter of the frame is within the moment's variance of the default limit.
        quarter = make_run(self.root, 'd', [image(100, (0, 0, 48, 32, 180))] * 2)
        self.assertEqual(gate(self.base, quarter)[0], 0)
        self.assertEqual(gate(self.base, quarter, '--image-pct', '20')[0], 1)

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


class InputTests(unittest.TestCase):
    """The diagnostic controller's input file (docs/e2e/architecture.md)."""
    def test_pulse_actions(self):
        self.assertEqual(e2e.parse_action('~y')['pulse'], e2e.BUTTONS['y'])
        self.assertIsNone(e2e.parse_action('~y')['pulse_from'])
        state = e2e.parse_action('a+~y@720')
        self.assertEqual((state['buttons'], state['pulse'], state['pulse_from']),
                         (e2e.BUTTONS['a'], e2e.BUTTONS['y'], 720))
        self.assertEqual(e2e.parse_action('none')['pulse'], 0)

    def test_bad_actions(self):
        for action in ('~y@-5', '~y@x', '~y@', '~q', 'q', 'foo=1', 'rt=x'):
            with self.assertRaises(ValueError, msg=action):
                e2e.parse_action(action)
        e2e.check_actions(e2e.SCENARIOS['freeze'])
        with self.assertRaisesRegex(ValueError, 'step 1: .*~y@-5'):
            e2e.check_actions([[1, 'wait', 0], [1, 'a,~y@-5', 0.12]])
        with self.assertRaisesRegex(ValueError, 'step 0'):
            e2e.check_actions([[5, '?~q', 0.12, 'TEXT']])
        e2e.check_actions([[1, 'phase:x', 0], [1, 'select:A B', 0], [1, 'defines:X=1', 0],
                           [5, 'wait', 0, 'TEXT'], [1, 'none', 0], [1, 'rt=255+back', 0]])

    def test_input_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'input.txt'
            e2e.set_input(path, rt=255)
            self.assertEqual(path.read_text().split(), '0 0 255 0 0 0 0'.split())
            e2e.set_input(path, **e2e.parse_action('~y'))
            self.assertEqual(path.read_text().split(), '0 0 0 0 0 0 0 32768'.split())
            e2e.set_input(path, **e2e.parse_action('~y@720'))
            self.assertEqual(path.read_text().split(), '0 0 0 0 0 0 0 32768 720'.split())
            # Keyboard keys on a second line, for the keyboard driver (docs/analysis/keyboard-controls.md).
            e2e.set_input(path, **e2e.parse_action('key=Control+key=D+a'))
            self.assertEqual(path.read_text().splitlines(), ['4096 0 0 0 0 0 0', 'keys Control D'])
            e2e.set_input(path, **e2e.parse_action('none'))
            self.assertEqual(path.read_text().splitlines(), ['0 0 0 0 0 0 0'])

    def test_key_actions(self):
        self.assertEqual(e2e.parse_action('key=W')['keys'], ('W',))
        for action in ('key=', 'key=Shift+Up', 'key=W,'):
            with self.assertRaises(ValueError, msg=action):
                e2e.parse_action(action)
        e2e.check_actions([[1, 'key=A', 0], [1, 'key=W+key=Control', 0.5], [1, 'key=D,key=A', 0.12]])


class WholeScalePlanTests(unittest.TestCase):
    """The launchers' plan at a whole 16:9 scale of 2 or more (e2e.whole_scale_plan)."""

    def test_plan(self):
        two = {'draw_resolution_scale_x': '2', 'draw_resolution_scale_y': '2'}
        self.assertEqual(e2e.whole_scale_plan(two, True),
                         {'native_draw_scale': '2', 'native_draw_scale_variant': 'true',
                          'native_rt_surfaces': '122'})
        self.assertEqual(e2e.whole_scale_plan({**two, 'draw_resolution_scale_x': '3',
                                               'draw_resolution_scale_y': '3'}, False),
                         {'native_draw_scale': '3', 'native_draw_scale_variant': 'true',
                          'native_rt_surfaces': '58'})

    def test_no_plan(self):
        self.assertEqual(e2e.whole_scale_plan({}, True), {})
        self.assertEqual(e2e.whole_scale_plan({'draw_resolution_scale_x': '1',
                                               'draw_resolution_scale_y': '1'}, True), {})
        self.assertEqual(e2e.whole_scale_plan({'draw_resolution_scale_x': '2',
                                               'draw_resolution_scale_y': '1'}, True), {})
        two = {'draw_resolution_scale_x': '2', 'draw_resolution_scale_y': '2'}
        for key in e2e.WHOLE_SCALE_PLAN_KEYS:
            self.assertEqual(e2e.whole_scale_plan({**two, key: '90'}, True), {}, key)


class EventTests(unittest.TestCase):
    """Screen events (FM4_TEST_EVENTS_FILE), '@' checks and the fast route's scenarios."""
    class Process:
        def poll(self):
            return None

    def write(self, path, lines):
        path.write_text(''.join(f'{int(t * 1000)} 123 {text}\n' for t, text in lines))

    def test_read_events(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'events.txt'
            self.assertEqual(e2e.read_events(path), [])
            path.write_text('1000 5 scene 95 17_DYNAMIC_LOADING_FREEPLAY\npartial\n2500 6 push 12-Car_Race\n')
            self.assertEqual(e2e.read_events(path),
                             [(1.0, 'scene 95 17_DYNAMIC_LOADING_FREEPLAY'), (2.5, 'push 12-Car_Race')])

    def test_event_step(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            events, input_file = root / 'events.txt', root / 'input.txt'
            screen = e2e.Screen(root, self.Process(), root / 'steps.jsonl', events)
            now = time.time()
            e2e.LAST_PRESS[0] = now - 10
            # 'wait' takes the events since the last press; an older one does not count.
            self.write(events, [(now - 20, 'push 12-Car_Race'), (now - 5, 'scene 208 4_C_INI_PRERACE')])
            self.assertTrue(e2e.checked_step(screen, input_file, 1, 'wait', 0, '@scene \\d+ 4_C_INI_PRERACE'))
            self.assertFalse(e2e.checked_step(screen, input_file, 0.2, 'wait', 0, '@push \\S+_Race$'))
            # '?' skips the press when a matching event follows the last press.
            pressed = e2e.LAST_PRESS[0]
            self.assertTrue(e2e.checked_step(screen, input_file, 1, '?a', 0.12, '@4_C_INI_PRERACE'))
            self.assertEqual(e2e.LAST_PRESS[0], pressed)
            # A press counts only the events written after it, and is repeated every retry.
            started = time.time()
            self.assertFalse(e2e.checked_step(screen, input_file, 1.6, 'a', 0.05, '@4_C_INI_PRERACE', 0.7))
            self.assertGreater(e2e.LAST_PRESS[0], started + 0.5)
            screen.close()

    def test_fast_boot_main_menu_during_wait(self):
        # The title took the boot's last A and pushed the main menu during the wait (run
        # gp2s-240-photo, 25 October 2026): the title's A is skipped, not pressed on PLAY CAREER.
        _, action, hold, text, retry = e2e._FAST_TO_MAIN_MENU[2]
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            events, input_file = root / 'events.txt', root / 'input.txt'
            screen = e2e.Screen(root, self.Process(), root / 'steps.jsonl', events)
            try:
                pressed = time.time() - 2.6
                e2e.LAST_PRESS[0] = pressed
                self.write(events, [(pressed + 0.04, 'scene 221 120_P_PRE_SPLASH')])
                # Without the push, the title gets its A (and the step times out here).
                self.assertFalse(e2e.checked_step(screen, input_file, 0.2, action, hold, text, retry))
                self.assertGreater(e2e.LAST_PRESS[0], pressed)
                pressed = e2e.LAST_PRESS[0] = time.time() - 2.6
                self.write(events, [(pressed + 0.04, 'scene 221 120_P_PRE_SPLASH'),
                                    (pressed + 0.88, 'pop UI_NoCar'), (pressed + 0.88, 'push UI_Homespace')])
                self.assertTrue(e2e.checked_step(screen, input_file, 1, action, hold, text, retry))
                self.assertEqual(e2e.LAST_PRESS[0], pressed)
            finally:
                screen.close()

    def test_fast_scenarios(self):
        fast, menus = e2e.FAST_SCENARIOS, e2e.SCENARIOS
        for name, steps in fast.items():
            e2e.check_actions(steps)
            if name not in ('career', 'menu_race'):
                self.assertEqual(steps[:len(fast['quickrace']) - 3], fast['quickrace'][:-3], name)
        start = menus['career'].index([1, 'phase:race', 0])
        self.assertEqual(fast['career'][-(len(menus['career']) - start):], menus['career'][start:])
        self.assertEqual(fast['career'][:len(e2e._FAST_TO_MAIN_MENU)], e2e._FAST_TO_MAIN_MENU)
        # After the race starts, the steps are those of the menu route.
        for name in ('race', 'photo'):
            self.assertEqual(fast[name][len(fast['quickrace']):], menus[name][len(menus['quickrace']) + 1:])
            self.assertEqual(fast[name][len(fast['quickrace'])], [1, 'phase:race', 0])
        # menu_race: the menu phase on the main menu, then the steps of race.
        for scenarios, boot in ((fast, e2e._FAST_TO_MAIN_MENU), (menus, e2e._TO_MAIN_MENU)):
            steps = scenarios['menu_race']
            self.assertEqual(steps[:len(boot)], boot)
            menu = steps.index([1, 'phase:menu', 0])
            self.assertEqual(steps[menu:menu + 3], [[1, 'phase:menu', 0], [30, 'wait', 0], [1, 'phase:end', 0]])
            self.assertEqual(steps[menu + 3:], scenarios['race'][len(boot):])
        self.assertEqual(fast['freeze'][len(fast['quickrace']) - 3], [0.5, '~y@720', 0])
        self.assertEqual(fast['freeze'][len(fast['quickrace']) - 3:],
                         menus['freeze'][len(menus['quickrace']) - 1:])


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

    def test_gpu_passes(self):
        gpu = {'frames': 50, 'frame_ms': 8.0, 'resolve_ms': 1.0, 'span_ms': 9.0,
               'passes_ms': {'gtao': 1.0, 'taa': 0.5, 'motion': 0.2, 'windshield': 0.1, 'other': 0.0},
               'swap_ms': 0.2, 'scene_ms': 6.0}
        result = e2e.summarize([self.window(gpu=gpu)] * 3, 0)
        self.assertEqual(result['gpu_passes_ms_median'],
                         {'gtao': 1.0, 'taa': 0.5, 'motion': 0.2, 'windshield': 0.1, 'other': 0.0,
                          'swap': 0.2, 'scene': 6.0})
        self.assertEqual(result['gpu_detail_ms_median'], {})
        parts = {'transfers': 0.01, 'dump': 0.0, 'guest_layout': 0.2, 'host_textures': 0.1, 'clear': 0.02}
        writes = {'scene_sampled': [0.1, 4.0], 'scene_unsampled': [0.05, 2.0],
                  'whole_sampled': [0.02, 13.0], 'whole_unsampled': [0.0, 0.0]}
        result = e2e.summarize([self.window(gpu=dict(gpu, resolve_full_ms=0.4, load_full_ms=0.3,
                                                     resolve_full_parts_ms=parts,
                                                     resolve_host_writes=writes))] * 3, 0)
        self.assertEqual(result['gpu_detail_ms_median'], {'resolve_full_ms': 0.4, 'load_full_ms': 0.3})
        self.assertEqual(result['gpu_resolve_full_parts_ms_median'], parts)
        self.assertEqual(result['gpu_resolve_host_writes_median'], writes)

    def test_host_presents(self):
        result = e2e.summarize([self.window(host_presents={'new_frames': 50, 'repeats': 150, 'ui_thread': 200,
                                                           'paint_gpu_ms': 0.05}),
                                self.window(host_presents={'new_frames': 50, 'repeats': 0, 'ui_thread': 0,
                                                           'paint_gpu_ms': 0.07})], 0)
        self.assertEqual(result['host_presents_per_s'], {'new_frames': 50, 'repeats': 75, 'ui_thread': 100})
        self.assertEqual(result['host_paint_gpu_ms_median'], 0.06)
        self.assertEqual(result['host_presents_per_frame'], 2.5)
        self.assertNotIn('host_presents_per_s', e2e.summarize([self.window()] * 3, 0))

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

    def test_compare_fails_photo_mode_clear_color(self):
        # Photo mode's captures or the final one showing the clear color fail; as many as the
        # baseline's, or a summary without the fields, do not.
        base = {'fps_mean': 60, 'errors': {}}
        b = compare.normalize(dict(base, clear_gray_photo_frames=[], clear_gray_final=False))
        lost = compare.normalize(dict(base, clear_gray_photo_frames=['frame-070'],
                                      clear_gray_final=True))
        self.assertEqual(len(compare.check_fail(b, lost)), 2)
        self.assertEqual(compare.check_fail(lost, lost), [])
        self.assertEqual(compare.check_fail(compare.normalize(dict(base)), b), [])
        # The repeat summaries' counts.
        self.assertEqual(len(compare.check_fail(
            compare.normalize(dict(base, clear_gray_photo_frames=0, clear_gray_final=0)),
            compare.normalize(dict(base, clear_gray_photo_frames=3, clear_gray_final=1)))), 2)


class ShaderWorkTests(unittest.TestCase):
    """The shader work of the perf trace by segment and the cold cache rules (d3d-hle-n6.md 19.3.4)."""

    @staticmethod
    def window(mods=(0, 0.0, 0.0, 0), worker=(0, 0.0, 0.0, 0), frames=(20.0,) * 50, long=()):
        work = {kind: [0, 0.0, 0.0, 0] for kind in compare.SHADER_WORK_KINDS}
        work.update(mod_compiles=list(mods), mod_compiles_worker=list(worker), mods_pending=0)
        return {'window_ms': 1000, 'frame_ms': list(frames), 'shader_work': work,
                'long_frames': [list(f) for f in long]}

    def test_segments(self):
        stall = self.window(mods=(2, 900.0, 600.0, 2), frames=[20.0] * 10 + [700.0],
                            long=[(700.0, 650.0, 600.0, 0.0)])
        quiet = self.window()
        # start: windows 0-1, race 2-3, after_race 4, photo 5, after_photo 6.
        perf = [stall, quiet, quiet, stall, quiet, quiet, quiet]
        marks = [('race', 2), ('end', 4), ('photo', 5), ('end', 6)]
        summary = e2e.shader_work_summary(perf, marks)
        self.assertEqual(list(summary['segments']), ['start', 'race', 'after_race', 'photo', 'after_photo'])
        run = summary['run']
        self.assertEqual(run['work']['mod_compiles'], {'count': 4, 'ms': 1800.0, 'max_ms': 600.0, 'over_4ms': 4})
        self.assertEqual(run['frames_over_ms'], {'50': 2, '100': 2, '250': 2})
        self.assertEqual((run['frames_over_50ms_mod_compile'], run['longest_frame_ms']), (2, 700.0))
        self.assertEqual(summary['segments']['race']['frames_over_50ms_mod_compile'], 1)
        self.assertEqual(summary['segments']['after_race']['frames_over_ms']['50'], 0)
        self.assertIn('4 mod compiles on the draw path (1.8 s, longest 600 ms)', e2e.shader_work_line(run))
        # Phases back to back leave no gap; a trace without the counters has no summary.
        summary = e2e.shader_work_summary([quiet] * 4, [('race', 1), ('replay', 2), ('end', 3)])
        self.assertEqual(list(summary['segments']), ['start', 'race', 'replay', 'after_replay'])
        self.assertIsNone(e2e.shader_work_summary([{'window_ms': 1000}], []))

    def test_compare_cold_cache_rules(self):
        stall = self.window(mods=(3, 900.0, 600.0, 3), frames=[700.0], long=[(700.0, 650.0, 600.0, 0.0)])
        moved = self.window(worker=(3, 900.0, 600.0, 0), frames=[20.0])
        run = lambda w, cold=True: compare.normalize({
            'fps_mean': 60, 'errors': {}, 'cache': {'cold': cold},
            'shader_work': e2e.shader_work_summary([w], [])})
        fail = compare.check_fail(run(moved), run(stall))
        self.assertEqual(len(fail), 2)
        self.assertIn('3 look mod compiles on the draw path', fail[0])
        self.assertEqual(compare.check_fail(run(stall), run(moved)), [])
        # Only cold cache cells: a warm cell compiling a new look's mods does not fail.
        self.assertEqual(compare.check_fail(run(moved, False), run(stall, False)), [])
        rows = {r['metric']: r for r in compare.build_rows(run(stall), run(moved))}
        self.assertEqual((rows['shader_work.mod_compiles.count']['baseline'],
                          rows['shader_work.mod_compiles.count']['candidate']), (3, 0))
        self.assertEqual(rows['shader_work.frames_over_250ms']['delta'], -1)
        # The repeat summaries list each run's work.
        repeat = compare.normalize({'fps_mean': 60, 'errors': {}, 'cache': {'cold': True},
                                    'shader_work_runs': [run(moved)['shader_work']['run'],
                                                         run(stall)['shader_work']['run']]})
        self.assertEqual(len(compare.check_fail(run(moved), repeat)), 2)

    def test_early_pipelines(self):
        # Early pipelines (19.3.2 part 5) are off the draw path; the replay's waits for them count
        # with the pipelines created on it.
        def run(created, waits):
            w = self.window()
            w['shader_work'].update(pipelines=list(created), pipelines_early=[400, 900.0, 60.0, 50],
                                    pipeline_waits=list(waits))
            return compare.normalize({'fps_mean': 60, 'errors': {}, 'cache': {'cold': True},
                                      'shader_work': e2e.shader_work_summary([w], [])})
        line = e2e.shader_work_line(run((10, 30.0, 6.0, 2), (5, 40.0, 20.0, 3))['shader_work']['run'])
        self.assertIn('pipelines 10 on the draw path (2 over 4 ms), 400 early, 5 waits (40 ms, 3 over 4 ms)', line)
        off, on = run((500, 2800.0, 45.0, 227), (0, 0.0, 0.0, 0)), run((0, 0.0, 0.0, 0), (59, 463.0, 37.0, 31))
        self.assertFalse(any('over 4 ms' in w for w in compare.check_warn(off, on)))
        warn = compare.check_warn(run((10, 30.0, 6.0, 2), (0, 0.0, 0.0, 0)), run((0, 0.0, 0.0, 0), (5, 40.0, 20.0, 3)))
        self.assertTrue(any('pipelines over 4 ms on the draw path 2 -> 3' in w for w in warn))
        self.assertFalse(any('pipelines_early' in w for w in warn))

    def test_matrix_cold_cells(self):
        import matrix
        root = Path('B:/root')
        cold = ['career', 'photo']
        career = matrix.cell_args(root, 'n6', 240, 'career', 1, 'snap', [], True, cold)
        photo = matrix.cell_args(root, 'n6', 240, 'photo', 1, 'snap', [], True, cold)
        self.assertIn('--cold-cache', career)
        self.assertEqual(photo[photo.index('--cache-from') + 1], root / 'e2e' / 'n6-240-career' / '')
        self.assertNotIn('--cold-cache', photo)
        # A photo cell without the career cell starts empty; warm gates pass neither.
        alone = matrix.cell_args(root, 'n6', 240, 'photo', 1, 'snap', [], True, ['photo'])
        self.assertIn('--cold-cache', alone)
        warm = matrix.cell_args(root, 'n6', 240, 'photo', 1, 'snap', [], True)
        self.assertFalse({'--cold-cache', '--cache-from'} & set(map(str, warm)))

    def test_prepare_cache(self):
        with tempfile.TemporaryDirectory() as tmp:
            first, second = Path(tmp) / 'first', Path(tmp) / 'second'
            options = lambda **kw: type('Options', (), {'cold_cache': False, 'cache_from': None,
                                                        'cache_copy': False, **kw})
            self.assertIsNone(e2e.prepare_cache(options(), first))
            cache = e2e.prepare_cache(options(cold_cache=True), first)
            (cache / 'shader-overrides').mkdir()
            (cache / 'shader-overrides' / 'a.dxbc').write_bytes(b'1')
            copied = e2e.prepare_cache(options(cache_from=str(first)), second)
            self.assertTrue((copied / 'shader-overrides' / 'a.dxbc').is_file())
            # A repeated run starts empty again.
            self.assertEqual(list(e2e.prepare_cache(options(cold_cache=True), first).iterdir()), [])
            with self.assertRaises(SystemExit):
                e2e.prepare_cache(options(cache_from=str(Path(tmp) / 'none')), second)
            # --cache-copy: a private copy of the BuildRoot's warm cache.
            root = Path(tmp) / 'root'
            (root / 'cache' / 'shaders').mkdir(parents=True)
            (root / 'cache' / 'shaders' / 'p.bin').write_bytes(b'2')
            private = e2e.prepare_cache(options(cache_copy=True), second, root)
            self.assertEqual(private, second / 'cache')
            (private / 'shaders' / 'p.bin').write_bytes(b'3')
            self.assertEqual((root / 'cache' / 'shaders' / 'p.bin').read_bytes(), b'2')
            with self.assertRaises(SystemExit):
                e2e.prepare_cache(options(cache_copy=True), second, Path(tmp) / 'no-root')


class CoverageTests(unittest.TestCase):
    """coverage.py: the writers of each phase from the n0 trace's alloc table and the checks."""

    def test_phases_and_menu(self):
        import coverage
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'fm4_rex_funcs.h').write_text(
                'DECLARE_REX_FUNC(sub_82000000);\nDECLARE_REX_FUNC(sub_82000100);\n')
            # The run's clock: menu steps at 10 to 20 s, the race from 30 s; steady = run * 1000 + 5000.
            lines = [{'steady_ms': 5000 + 1000 * s, 'alloc': {'AllocateRingSpace:82000010': [2, 8]}}
                     for s in range(12, 21)]
            lines += [{'steady_ms': 5000 + 1000 * s, 'alloc': {'overflow:82000120': [1, 0]}}
                      for s in range(32, 40)]
            trace = root / 'n0.jsonl'
            trace.write_text('\n'.join(json.dumps(l) for l in lines) + '\n')
            perf = [{'t': float(s), 'frame_ms': [16.0] * 60,
                     'host_render': {'checks': {'82000100': 5}}} for s in range(1, 45)]
            (root / 'perf.jsonl').write_text('\n'.join(json.dumps(p) for p in perf) + '\n')
            summary = {'n0_trace': str(trace), 'n0_trace_offset': 0,
                       'timeline': [[10.0, 'wait', 'PLAY CAREER', True], [20.0, 'select:START RACE', True],
                                    [30.0, 'phase:race'], [40.0, 'phase:end']],
                       'phase_steady_ms': {'race': [35000.0, 45000.0]},
                       'phase_windows': {'race': [29, 39]}}
            (root / 'summary.json').write_text(json.dumps(summary))
            starts = coverage.functions(root)
            self.assertEqual(starts, [0x82000000, 0x82000100])
            n0 = lines
            result = {name: coverage.tally(l, p, starts)
                      for name, (l, p) in coverage.windows(summary, perf, n0).items()}
            self.assertEqual(list(result), ['menu', 'race'])
            self.assertEqual(result['menu']['allocations']['sub_82000000']['AllocateRingSpace'],
                             [18, 72])
            self.assertEqual(result['race']['allocations']['sub_82000100']['overflow'], [8, 0])
            self.assertEqual(result['race']['checks'], {'sub_82000100': 50})


class CpuPowerTest(unittest.TestCase):
    def test_summary(self):
        # A phase from its mark to the next: the process's CPU per second from the samples inside
        # it, the mean power in W, and each thread name's CPU per second over the phase.
        sampler = cpu_power.Sampler.__new__(cpu_power.Sampler)
        sampler.samples = [(10.0, 1000.0, 40000.0, 30000.0), (11.0, 1600.0, 42000.0, 32000.0),
                           (12.0, 2200.0, 44000.0, None), (13.0, 2500.0, 20000.0, 10000.0)]
        sampler.marks = [('race', 9.9, {'GPU Host Render': [100.0, 1], 'XThread0001': [50.0, 1]}),
                         ('end', 12.1, {'GPU Host Render': [1200.0, 1], 'XThread0001': [600.0, 1],
                                        'D3D12 Pipelines': [10.0, 4]})]
        race = sampler.summary()['race']
        self.assertEqual(race['seconds'], 2.0)
        self.assertEqual(race['process_cpu_ms_per_s'], 600.0)
        self.assertEqual(race['package_w'], 42.0)
        self.assertEqual(race['cores_w'], 31.0)
        self.assertEqual(list(race['threads']), ['GPU Host Render', 'XThread0001', 'D3D12 Pipelines'])
        self.assertEqual(race['threads']['GPU Host Render'], [500.0, 1])
        self.assertEqual(race['threads']['D3D12 Pipelines'], [4.5, 4])


class RaceCheckTests(unittest.TestCase):
    """The race check: race scenarios and goals need a race push (screen events)."""
    # The events of a Quick Race stopped at the Disc 2 prompt on a BuildRoot without Disc 2
    # (clean-quickrace, 25 October 2026), in seconds from the launch.
    DISC = [(25.28, 'push UI_Homespace'), (43.46, 'push UI_NoCar'), (51.59, 'scene 34 108_C_BUY_CARSELECT'),
            (62.89, 'scene 122 031_LOAD_DISC_2')]
    RACE = [(130.02, 'push 12-Car_321_and_PostRace'), (130.02, 'countdown placed'),
            (139.8, 'push 12-Car_Race')]
    # A new profile (warmcache on that BuildRoot): the gamma adjustment, then the intro race.
    FIRST_RUN = [(29.49, 'scene 243 830_GAMMA_ADJUST'), (37.09, 'scene 0 1_C_INI_IE_LOADING')]

    def options(self, scenario='quickrace', goal=None, steps=None):
        return type('Options', (), {'scenario': scenario, 'goal': goal, 'steps': steps})()

    def at(self, events, started=1000.0):
        return [(started + t, text) for t, text in events]

    def test_race_needed(self):
        self.assertTrue(e2e.race_needed(self.options()))
        self.assertTrue(e2e.race_needed(self.options(goal='quickrace', steps='camera')))
        for scenario in ('boot', 'photo_view', 'career_progress'):
            self.assertFalse(e2e.race_needed(self.options(scenario)), scenario)
        self.assertFalse(e2e.race_needed(self.options(steps='[[1, "wait", 0]]')))
        # Every scenario that drives (throttle, a race-frame pulse or the season driver) is a race
        # scenario.
        for scenarios in (e2e.SCENARIOS, e2e.FAST_SCENARIOS):
            for name, steps in scenarios.items():
                drives = any(step[1].startswith(('rt=', '~y@')) or step[1] in ('season', 'qrloop') for step in steps)
                self.assertEqual(name in e2e.RACE_SCENARIOS, drives, name)

    def test_disc_prompt_fails(self):
        with contextlib.redirect_stdout(io.StringIO()):
            reached, failed, race_s = e2e.race_verdict(True, None, None, True, self.at(self.DISC), 1000.0)
        self.assertFalse(reached)
        self.assertIsNone(race_s)
        self.assertIn('no race', failed)
        self.assertIn('031_LOAD_DISC_2', failed)
        self.assertIn('-Disc2', failed)

    def test_race_passes(self):
        events = self.at(self.DISC[:3] + self.RACE)
        self.assertEqual(e2e.race_verdict(True, None, None, True, events, 1000.0), (True, None, 139.8))
        # Without the race check, or after a crash, the verdict stays as it was.
        self.assertEqual(e2e.race_verdict(True, None, None, False, self.at(self.DISC), 1000.0),
                         (True, None, None))
        self.assertEqual(e2e.race_verdict(True, None, 'exited with 1', True, self.at(self.DISC[:3]), 1000.0),
                         (True, None, None))
        # Events of an earlier run do not count.
        with contextlib.redirect_stdout(io.StringIO()):
            reached, _, _ = e2e.race_verdict(True, None, None, True, self.at(self.RACE, 900.0), 1000.0)
        self.assertTrue(reached)  # no events of this run: the check cannot run

    def test_no_events_warns(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(e2e.race_verdict(True, None, None, True, [], 1000.0), (True, None, None))
        self.assertIn('race check could not run', output.getvalue())

    def test_failed_step_gets_reason(self):
        failed = "step 4: ?a -> 'PLAY CAREER' timed out"
        reached, reason, _ = e2e.race_verdict(False, failed, None, True, self.at(self.FIRST_RUN), 1000.0)
        self.assertFalse(reached)
        self.assertTrue(reason.startswith(failed + '; a new profile (scene 830_GAMMA_ADJUST)'))
        self.assertEqual(e2e.race_verdict(False, failed, None, True, self.at(self.DISC[:3]), 1000.0)[1], failed)


class ContentImportTests(unittest.TestCase):
    """import_content: Disc 2 and the -Content packages imported before the first launch."""

    def package(self, path):
        path.parent.mkdir(parents=True, exist_ok=True)
        header = bytearray(0x364)
        header[:4] = b'CON '
        header[0x344:0x348] = (2).to_bytes(4, 'big')
        header[0x360:0x364] = (0x4D530910).to_bytes(4, 'big')
        path.write_bytes(bytes(header))
        return path

    def build_root(self, root, disc2=True):
        disc2_dir = root / 'Disc 2'
        packages = ([self.package(disc2_dir / 'content/0000000000000000/4d530910/00000002' /
                                  f'4d5309100000000{index}') for index in range(1, 5)] if disc2 else [])
        (root / 'local.json').write_text(json.dumps({'disc1': str(root / 'Disc 1'),
                                                     'disc2': str(disc2_dir) if disc2 else None,
                                                     'content': []}))
        return packages

    def install(self, user_data, package):
        (user_data / '0000000000000000/4D530910/00000002' / package.name).mkdir(parents=True, exist_ok=True)
        (user_data / f'{package.name}.import-complete').write_text(f'{package.stat().st_size}\n1\n1\n')

    def test_package_imported(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            package, user_data = self.build_root(root)[0], root / 'user-data'
            self.assertFalse(e2e.package_imported(user_data, package))
            self.install(user_data, package)
            self.assertTrue(e2e.package_imported(user_data, package))
            # A changed package (another size) is imported again, as the game's importer does.
            package.write_bytes(package.read_bytes() + b'x')
            self.assertFalse(e2e.package_imported(user_data, package))

    def test_import_runs_once(self):
        from unittest import mock
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            packages, user_data = self.build_root(root), root / 'user-data'
            self.install(user_data, packages[0])
            calls = []

            def game(command, env, **kwargs):
                calls.append(env)
                for package in packages:
                    self.install(user_data, package)
                return 0
            with mock.patch.object(e2e.subprocess, 'call', game), contextlib.redirect_stdout(io.StringIO()):
                e2e.import_content(root)
                self.assertEqual(len(calls), 1)
                self.assertEqual(calls[0]['FM4_CONTENT_INSTALL_ONLY'], '1')
                self.assertEqual(Path(calls[0]['FM4_CONTENT_MANIFEST']).read_text(encoding='utf-8').splitlines(),
                                 [str(package) for package in packages])
                # Everything imported: no launch.
                e2e.import_content(root)
                self.assertEqual(len(calls), 1)

    def test_import_failure_and_no_disc2(self):
        from unittest import mock
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.build_root(root)
            with mock.patch.object(e2e.subprocess, 'call', lambda *a, **k: 1), \
                    contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises(SystemExit) as raised:
                    e2e.import_content(root)
            self.assertIn('content import failed', str(raised.exception))
            self.assertIn('4d53091000000001', str(raised.exception))
            self.build_root(root, disc2=False)
            output = io.StringIO()
            with mock.patch.object(e2e.subprocess, 'call', lambda *a, **k: self.fail('launched')), \
                    contextlib.redirect_stdout(output):
                e2e.import_content(root)
            self.assertIn('names no Disc 2', output.getvalue())


class PerfAbTests(unittest.TestCase):
    """perf_ab.py on a synthetic A/B trace: side b costs 0.1 GPU ms a frame more, under a drift
    of the scene; and e2e.py --race-seconds."""

    def window(self, period, offset, side, gpu):
        return {'window_ms': 250, 'draws': 2000 * 60, 'frame_ms': [4.0] * 60,
                'ab': {'side': side, 'value': ['0', '3'][side], 'period': period, 'offset_ms': offset},
                'gpu': {'frames': 60, 'frame_ms': gpu}, 'render_thread_busy_ms': 180.0,
                'cpu_ms': {'draw_textures': 24.0 - 0.6 * side}}

    def test_paired_difference(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp)
            lines = [{'window_ms': 1000, 'draws': 1, 'frame_ms': [9.0]}]  # before the race
            noise = random.Random(1)
            for period in range(100, 140):
                side = period % 2
                for step in range(8):
                    drift = 0.002 * (period * 8 + step - 800) + noise.gauss(0, 0.03)
                    # The first window after a flip carries frames of the other side.
                    stale = 0.5 if step == 0 else 0.0
                    lines.append(self.window(period, step * 250, side, 3.0 + drift + 0.1 * side + stale))
            (run / 'perf.jsonl').write_text('\n'.join(json.dumps(l) for l in lines) + '\n')
            (run / 'summary.json').write_text(json.dumps({'phase_windows': {'race': [1, len(lines)]}}))
            result = perf_ab.in_run(run, 'race', 250)
        gpu = result['metrics']['gpu_ms_per_frame']
        self.assertEqual(result['periods'], [20, 20])
        self.assertEqual(result['windows_dropped'], 40)
        self.assertEqual(gpu['pairs'], 39)
        self.assertAlmostEqual(gpu['median_diff'], 0.1, delta=0.02)
        self.assertLess(gpu['ci95'][0], 0.1 + 1e-9)
        self.assertGreater(gpu['ci95'][1], 0.1 - 1e-9)
        self.assertAlmostEqual(result['metrics']['textures_ms_per_frame']['median_diff'], -0.01, places=6)
        self.assertLess(gpu['ci95'][1] - gpu['ci95'][0], 0.1)
        self.assertAlmostEqual(result['metrics']['draws_per_frame']['median_diff'], 0.0)

    def test_unlabelled_run_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp)
            (run / 'perf.jsonl').write_text(json.dumps({'window_ms': 1000, 'draws': 1}) + '\n')
            with self.assertRaises(SystemExit):
                perf_ab.in_run(run, '', 250)

    def test_race_seconds(self):
        steps = e2e.with_race_seconds(e2e.FAST_SCENARIOS['race'], 90)
        index = [s[1] for s in steps].index('phase:race')
        self.assertEqual(steps[index + 1], [90, 'wait', 0])
        self.assertEqual(e2e.FAST_SCENARIOS['race'][index + 1][0], 30)
        with self.assertRaises(SystemExit):
            e2e.with_race_seconds(e2e.SCENARIOS['boot'], 90)


if __name__ == '__main__':
    unittest.main()
