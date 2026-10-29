"""Offline tests of the career_season scenario (career_season.py) and the soak analysis
(soak_report.py): the OCR parsers on lines read from real captures, the driver against a
simulated World Tour that answers its presses with screen events and OCR lines, and the soak
verdict on synthetic traces.
Run: python outputs/FM4-build/e2e/test_career_season.py"""
import json, sys, tempfile, time, types, unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import career_season  # noqa: E402
import e2e  # noqa: E402
import season_perf  # noqa: E402
import soak_report  # noqa: E402


def line(text, x, y, **extra):
    return {'text': text, 'x': x, 'y': y, 'w': 8 * len(text), 'h': 16, 'bg': 120, 'red': 0,
            'red_text': 0, **extra}


class ParserTests(unittest.TestCase):
    def test_footer_of_the_row_and_the_event_screen(self):
        # OCR of the World Tour row and the event screen (cs-explore1, 29 October 2026).
        row = [line('Ford Ka', 125, 631), line('I-VI-2', 296, 631), line('1 5,980 CR', 362, 631),
               line('SELECT', 731, 667)]
        self.assertEqual(career_season.footer(row), ('Ford Ka', 2, 15980))
        event = [line('Ford Ka', 377, 631), line('LVI- 1', 548, 631), line('8,610 CR', 613, 631)]
        self.assertEqual(career_season.footer(event), ('Ford Ka', 1, 8610))
        self.assertEqual(career_season.footer([line('START RACE', 123, 64)]), (None, None, None))

    def test_row_event(self):
        lines = [line('TRACK', 1197, 159), line('Short Circuit', 1197, 179),
                 line('Class F Asian Tour', 693, 495), line('DRIVER LEVEL BONUS', 693, 589),
                 line('+25%', 837, 586), line('Ford Ka', 125, 631)]
        self.assertEqual(career_season.row_event(lines),
                         {'name': 'Class F Asian Tour', 'bonus': 'DRIVER LEVEL BONUS +25%',
                          'track': 'Short Circuit'})
        self.assertIsNone(career_season.row_event([line('Ford Ka', 125, 631)]))

    def test_season_status(self):
        lines = [line('AMATEUR DIVISION', 305, 169), line('DESTINATION', 535, 253),
                 line('EVENT STATUS', 655, 252), line('COMPLETED EVENT', 655, 319),
                 line('Ladera Short', 550, 320), line('Road Atlanta Club', 521, 354),
                 line('COMPLETED EVENT', 655, 353), line('Tsukuba Short', 541, 388),
                 line('CURRENT EVENT', 655, 387), line('TopGear Loop', 544, 422),
                 line('NOT YET STARTED', 655, 421), line('Hockenheim Short', 519, 456),
                 line('NOT YET STARTED', 655, 455)]
        status = career_season.season_status(lines)
        self.assertEqual(status['name'], 'AMATEUR DIVISION')
        self.assertEqual(status['events'], [
            ['Ladera Short', 'COMPLETED EVENT'], ['Road Atlanta Club', 'COMPLETED EVENT'],
            ['Tsukuba Short', 'CURRENT EVENT'], ['TopGear Loop', 'NOT YET STARTED'],
            ['Hockenheim Short', 'NOT YET STARTED']])

    def test_restrictions_and_car_fits(self):
        box = [line('EVENT RESTRICTIONS', 306, 175), line('W 275', 306, 242),
               line('BODY FAMILY must be Economy', 305, 335), line('CANCEL', 923, 391)]
        rules = career_season.restrictions(box)
        self.assertEqual(rules, ['W 275', 'BODY FAMILY must be Economy'])
        self.assertTrue(career_season.car_fits(rules, 'Ford Ka'))
        self.assertFalse(career_season.car_fits(['MAKE must be Mazda'], 'Ford Ka'))
        self.assertTrue(career_season.car_fits(['MAKE must be Ford'], 'Ford Ka'))


class FakeWorldTour:
    """A World Tour that answers presses as the title does, with screen events and OCR lines:
    events (name, races, challenge, gift); the season's last event is the last one listed."""

    def __init__(self, events, credits=8610, earnings=5370, affinity=2000):
        self.events, self.credits = events, credits
        self.earnings, self.affinity = earnings, affinity
        self.log = []  # (unix time, text)
        self.index = 0  # the event being played
        self.heat = 0
        self.race_polls = None
        self.selected = 0  # the event screen's menu entry
        self.row = 0
        self.presses = []
        self.emit('scene 245 200_WORLD_TOUR')
        self.enter_stop()

    def emit(self, *texts):
        # The title writes an event a moment after the press that caused it, each at its own
        # time; time.time() steps by about 16 ms on Windows.
        for text in texts:
            time.sleep(0.02)
            self.log.append((time.time(), text))

    def enter_stop(self):
        event = self.events[self.index]
        self.state = 'globe'
        self.globe_reads = 2
        self.challenge = event.get('challenge', False)

    # The screen's side.
    def poll_events(self):
        if self.state == 'race':
            self.race_polls -= 1
            if self.race_polls <= 0:
                self.emit('pop 8-Car_Race', 'push 8-Car_321_and_PostRace', 'scene 207 924_POST_RACE',
                          'scene 187 037_I_D_POP_DIALOG')
                self.state = 'post'
        return self.log

    def footer(self, x=125):
        return [line('Ford Ka', x, 631), line('LVL 1', x + 170, 631), line(f'{self.credits:,} CR', x + 240, 631)]

    def lines(self):
        event = self.events[self.index]
        if self.state == 'globe':
            self.globe_reads -= 1
            if self.globe_reads <= 0:
                if self.challenge:
                    self.emit('scene 188 201_SPECIAL_EVENT_DIALOG')
                    self.state = 'challenge'
                else:
                    self.state = 'row'
            return []
        if self.state == 'challenge':
            return [line('CHALLENGE EVENT', 207, 175), line('CONTINUE', 1010, 531)]
        if self.state == 'row':
            names = event['row']
            return [line(names[self.row], 693, 495), line('CREDITS BONUS', 693, 589)] + self.footer() + \
                [line('SELECT', 731, 667), line('RESTRICTIONS', 913, 668), line('CHANGE CAR', 1054, 667)]
        if self.state == 'restrictions':
            return [line('EVENT RESTRICTIONS', 306, 175), line('BODY FAMILY must be Economy', 305, 335),
                    line('CANCEL', 923, 391)]
        if self.state == 'event':
            entries = ['START RACE', 'CHANGE CAR', 'SEASON STATUS', 'SET DIFFICULTY']
            return [line(text, 123, 64 + 40 * i, **({'bg': 60, 'red_above': 50, 'red_below': 50}
                                                     if i == self.selected else {}))
                    for i, text in enumerate(entries)] + self.footer(377)
        if self.state == 'status':
            rows = []
            for i, other in enumerate(self.events):
                status = 'COMPLETED EVENT' if i < self.index else 'CURRENT EVENT' if i == self.index \
                    else 'NOT YET STARTED'
                rows += [line(other['track'], 520, 320 + 34 * i), line(status, 655, 319 + 34 * i)]
            return [line('AMATEUR DIVISION', 305, 169), line('EVENT STATUS', 655, 252)] + rows
        if self.state == 'prerace':
            return [line('START RACE', 121, 65), line(f'HEAT {"I" * (self.heat + 1)}', 1005, 77)]
        if self.state == 'race':
            return [line('LAPS', 1050, 12), line('PLACE', 1016, 109)]
        if self.state == 'post':
            return [line('CONTINUE', 121, 65), line('YOU PLACED', 1124, 64), line('1st', 1130, 89)]
        if self.state == 'earnings':
            return [line('RACE WINNINGS', 252, 205), line('Total', 1151, 545),
                    line(f'+{self.earnings:,} CR', 954, 565), line('CONTINUE', 1133, 667)]
        if self.state == 'level':
            return [line('DRIVER LEVEL', 252, 206), line('CONTINUE', 1133, 667)]
        if self.state == 'reward1':
            return [line('Level 2 Driver Reward', 493, 451), line('CONTINUE', 1133, 667)]
        if self.state == 'reward2':
            return [line('Ford has sent you a', 498, 522), line(f'{self.affinity:,} CR', 665, 522),
                    line('bonus.', 747, 522), line('CONTINUE', 1133, 667)]
        if self.state == 'gift':
            return [line('1994 Mazda', 297, 77), line('MX-5 Miata', 296, 115), line('SELECT', 1149, 667)]
        if self.state == 'paint':
            return [line('SELECT B BACK Q FREE LOOK', 941, 662)]
        if self.state == 'sure':
            return [line('SELECT CAR', 306, 249), line('Are you sure you want this car?', 305, 361),
                    line('YES', 849, 452)]
        return []

    # The pad's side.
    def press(self, action):
        self.presses.append((self.state, action))
        event = self.events[self.index]
        if self.state == 'challenge' and action == 'a':
            self.state = 'row'
        elif self.state == 'row':
            if action == 'right':
                self.row = min(self.row + 1, len(event['row']) - 1)
            elif action == 'left':
                self.row = max(self.row - 1, 0)
            elif action == 'x':
                self.emit('scene 215 044_D_POP_UP')
                self.state = 'restrictions'
            elif action == 'a':
                self.emit('scene 17 EMPTY_SCENE', 'scene 209 204_C_RACE_INFO')
                self.state, self.selected = 'event', 0
        elif self.state == 'restrictions' and action == 'b':
            self.state = 'row'
        elif self.state == 'event':
            if action in ('up', 'down'):
                self.selected = max(0, min(3, self.selected + (1 if action == 'down' else -1)))
            elif action == 'a' and self.selected == 2:
                self.emit('scene 248 203_SEASON_STATUS')
                self.state = 'status'
            elif action == 'a' and self.selected == 0:
                self.emit('scene 92 19_DYNAMIC_LOADING_WORLDTOUR', 'pop UI_NoCar', 'push 8-Car_PreRace',
                          'scene 44 NotImplemented', 'scene 208 4_C_INI_PRERACE')
                self.state, self.heat, self.moved = 'prerace', 0, False
        elif self.state == 'status' and action == 'a':
            self.state = 'event'
        elif self.state == 'prerace':
            if action in ('up', 'down'):
                self.moved = True
            elif action == 'a' and self.moved:
                self.emit('scene 57 947_HUD', 'pop 8-Car_PreRace', 'push 8-Car_321_and_PostRace',
                          'countdown placed', 'pop 8-Car_321_and_PostRace', 'push 8-Car_Race')
                self.state, self.race_polls = 'race', 20
        elif self.state == 'post' and action == 'a':
            self.emit('scene 46 920_POST_RACE_EARNINGS')
            self.state = 'earnings'
        elif self.state == 'earnings' and action == 'a':
            self.credits += self.earnings
            self.emit('scene 68 909_LEVEL_PROGRESS')
            self.state = 'level'
        elif self.state == 'level' and action == 'a':
            self.emit('scene 69 910_POST_RACE_LEVEL_REWARDS')
            self.state = 'reward1'
        elif self.state == 'reward1' and action == 'a':
            self.state = 'reward2'
        elif self.state == 'reward2' and action == 'a':
            self.credits += self.affinity
            self.heat += 1
            if self.heat < event['races']:
                self.emit('scene 236 6_C_INI_UNLOADING', 'push 8-Car_PreRace', 'scene 208 4_C_INI_PRERACE')
                self.state, self.moved = 'prerace', False
            elif event.get('gift'):
                self.emit('scene 236 6_C_INI_UNLOADING', 'pop 8-Car_321_and_PostRace',
                          'scene 14 100_P_PRE_BACKGROUNDMAIN', 'scene 34 108_C_BUY_CARSELECT')
                self.state = 'gift'
            else:
                self.next_stop()
        elif self.state == 'gift' and action == 'a':
            self.emit('scene 19 114_C_BUY_BUYCAR_COLOR')
            self.state = 'paint'
        elif self.state == 'paint' and action == 'a':
            self.emit('scene 174 037_I_D_POP_DIALOG')
            self.state = 'sure'
        elif self.state == 'sure' and action == 'a':
            self.next_stop()

    def next_stop(self):
        self.emit('scene 216 EMPTY_SCENE', 'scene 184 035_D_LOADING', 'scene 245 200_WORLD_TOUR')
        self.index = min(self.index + 1, len(self.events) - 1)
        self.row = 0
        self.enter_stop()


class FakeScreen:
    def __init__(self, game):
        self.game = game

    def events(self, after):
        return [e for e in self.game.poll_events() if e[0] > after]

    def read(self, after, until):
        return self.game.lines(), time.time()


def fake_e2e(game):
    """The e2e functions the driver calls, wired to the fake game."""
    def press(input_file, sequence, hold):
        for action in sequence.split(','):
            game.press(action)

    def select_label(screen, input_file, timeout, label):
        entries = ['START RACE', 'CHANGE CAR', 'SEASON STATUS', 'SET DIFFICULTY']
        if game.state != 'event' or label not in entries:
            return False
        while game.selected != entries.index(label):
            game.press('down' if entries.index(label) > game.selected else 'up')
        return True
    return types.SimpleNamespace(press=press, select_label=select_label, shows=e2e.shows,
                                 newest_capture=lambda *a: None, ppm_to_png=lambda *a: None)


class DriverTests(unittest.TestCase):
    def drive(self, events, **options):
        game = FakeWorldTour(events)
        with tempfile.TemporaryDirectory() as tmp:
            season = career_season.Season(fake_e2e(game), FakeScreen(game), Path(tmp) / 'input.txt', tmp,
                                          time.time() + options.pop('cap_s', 120), **options)
            ok = season.run()
            log = [json.loads(l) for l in (Path(tmp) / 'season.jsonl').read_text().splitlines()]
            state = json.loads((Path(tmp) / 'season-state.json').read_text())
            return ok, season, log, state, game

    def test_a_season_of_three_events(self):
        events = [
            {'row': ['Class F Asian Tour', 'Inline Four Showcase'], 'track': 'Tsukuba Short', 'races': 1,
             'gift': True},
            {'row': ['Class F TopGear Challenge'], 'track': 'TopGear Loop', 'races': 1, 'challenge': True},
            {'row': ['Class F World Championship'], 'track': 'Hockenheim Short', 'races': 3, 'challenge': True},
        ]
        ok, season, log, state, game = self.drive(events)
        self.assertTrue(ok, state)
        self.assertEqual(len(log), 3)
        self.assertEqual([r['name'] for r in log], ['Class F Asian Tour', 'Class F TopGear Challenge',
                                                    'Class F World Championship'])
        self.assertEqual([r['places'] for r in log], [[1], [1], [1, 1, 1]])
        self.assertEqual(log[0]['gift_cars'], ['1994 Mazda MX-5 Miata'])
        self.assertEqual(log[2]['heats'], ['HEAT I', 'HEAT II', 'HEAT III'])
        self.assertEqual(log[0]['credits_before'], 8610)
        self.assertEqual(log[0]['credits_after'], 8610 + 5370 + 2000)
        self.assertEqual(log[1]['credits_after'], log[2]['credits_before'])
        self.assertEqual(log[0]['reward_credits'], [2000])
        self.assertEqual(log[2]['season']['events'][-1], ['Hockenheim Short', 'CURRENT EVENT'])
        self.assertEqual(state['seasons_done'][0]['season'], 'AMATEUR DIVISION')
        self.assertEqual(log[0]['stop_events'][1]['restrictions'], ['BODY FAMILY must be Economy'])
        summary = season.summary()
        self.assertEqual(summary['events'], 3)
        self.assertEqual(summary['credits_check']['match'], 2)
        self.assertEqual(summary['credits_check']['differ'], 0)
        # The pop-up the post-race menu opens at once got no press of its own: one A per screen.
        posts = [a for s, a in game.presses if s == 'post']
        self.assertEqual(posts, ['a'] * 5)

    def test_the_cap_stops_between_events(self):
        events = [{'row': ['A'], 'track': 'One', 'races': 1}, {'row': ['B'], 'track': 'Two', 'races': 1},
                  {'row': ['C'], 'track': 'Three', 'races': 1}]
        ok, season, log, state, game = self.drive(events, seasons=1, min_hours=10, cap_s=20)
        self.assertTrue(ok)
        self.assertTrue(season.capped)
        self.assertIsNone(season.failure)
        self.assertGreaterEqual(len(log), 1)


class SoakReportTests(unittest.TestCase):
    def write_run(self, root, private_per_race, gpu_per_race=0, races=6, frame_ms=4.0, frame_rise=0.0):
        run = Path(root)
        start = 1_800_000_000.0
        events, perf, soak = [], [], []
        t = start + 60
        windows = []
        for race in range(races):
            events += [(t, 'push 8-Car_Race'), (t + 240, 'pop 8-Car_Race')]
            windows.append((t, t + 240))
            t += 600
        for i in range(int((t - start) // 30)):
            now = start + 30 * i
            done = sum(1 for _, stop in windows if stop <= now)
            soak.append({'t': now, 'uptime_s': 30 * i, 'label': None,
                         'private_bytes': (2000 + done * private_per_race) * soak_report.MB + (i % 3) * soak_report.MB,
                         'working_set': 1500 * soak_report.MB, 'pagefile': 0, 'handles': 900 + i % 5,
                         'threads': 120, 'gdi': 30, 'user': 40,
                         'gpu_dedicated': (3000 + done * gpu_per_race) * soak_report.MB,
                         'gpu_shared': 100 * soak_report.MB, 'gpu_committed': 3200 * soak_report.MB})
        for second in range(int(t - start)):
            race = next((i for i, (a, b) in enumerate(windows) if a <= start + second <= b), None)
            ms = frame_ms * (1 + frame_rise * (race or 0) / races)
            perf.append({'t': second, 'window_ms': 1000, 'fps': 1000 / ms, 'frame_ms': [ms] * 10})
        (run / 'soak.jsonl').write_text(''.join(json.dumps(s) + '\n' for s in soak))
        (run / 'perf.jsonl').write_text(''.join(json.dumps(p) + '\n' for p in perf))
        (run / 'events.txt').write_text(''.join(f'{int(a * 1000)} 0 {b}\n' for a, b in events))
        return run

    def test_flat_run_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            report = soak_report.analyse(self.write_run(tmp, private_per_race=0))
            self.assertTrue(report['pass'], report['verdict'])
            self.assertEqual(report['races'], 6)
            self.assertLess(abs(report['metrics']['private_bytes']['slope_per_hour']), 5)

    def test_memory_that_rises_each_race_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            report = soak_report.analyse(self.write_run(tmp, private_per_race=20, gpu_per_race=10))
            self.assertFalse(report['verdict']['private_bytes']['pass'])
            self.assertFalse(report['verdict']['gpu_dedicated']['pass'])
            metric = report['metrics']['private_bytes']
            self.assertGreaterEqual(metric['floor_rises_in_a_row'], 3)
            self.assertAlmostEqual(metric['floor_slope_per_race'], 20, delta=1)

    def test_stubs(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / 'runtime.log'
            log.write_text('\n'.join([
                '[2026-10-29 20:57:06.240] [warning] [krnl] [t5796] Stub XFileFsDeviceInformation!',
                '[2026-10-29 20:57:06.240] [warning] [krnl] [t5796] IoDismountVolumeByFileHandle(0xf8000064) - stub',
                '[2026-10-29 20:58:00.000] [warning] [krnl] [t4648] __imp__XamXStudioRequest STUB',
                '[2026-10-29 20:59:00.000] [warning] [krnl] [t4648] __imp__XamXStudioRequest STUB',
                '[2026-10-29 21:00:00.000] [error] [krnl] [t1] Unimplemented XAM message app=000000FE, '
                'msg=0002B003, arg1=7057FA50, arg2=00000000',
                '[2026-10-29 21:00:01.000] [info] [gpu] [t2] nothing to see']) + '\n')
            found = soak_report.stubs(log)
            self.assertEqual([(e['name'], e['count']) for e in found],
                             [('XamXStudioRequest', 2), ('IoDismountVolumeByFileHandle', 1),
                              ('XAM message app=000000FE, msg=0002B003', 1), ('XFileFsDeviceInformation', 1)])
            self.assertEqual(found[0]['first'], '2026-10-29 20:58:00.000')
            self.assertEqual(found[0]['last'], '2026-10-29 20:59:00.000')

    def test_rising_frame_times_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            report = soak_report.analyse(self.write_run(tmp, private_per_race=0, frame_rise=0.5))
            self.assertFalse(report['verdict']['race_frame_p50']['pass'])
            self.assertTrue(report['verdict']['private_bytes']['pass'])


class SeasonPerfTests(unittest.TestCase):
    def test_phases_and_events(self):
        launch = 1_800_000_000.0
        # One event: menus 0-20 s, loading 20-30, pre-race 30-35, countdown 35-40, race 40-100,
        # rewards 100-120, loading 120-125, then the next World Tour screen.
        timeline = [(20, 'scene 92 19_DYNAMIC_LOADING_WORLDTOUR'), (30, 'scene 208 4_C_INI_PRERACE'),
                    (34, 'scene 57 947_HUD'), (35, 'countdown placed'), (40, 'push 8-Car_Race'),
                    (100, 'pop 8-Car_Race'), (100.1, 'scene 207 924_POST_RACE'),
                    (100.2, 'scene 187 037_I_D_POP_DIALOG'), (105, 'scene 46 920_POST_RACE_EARNINGS'),
                    (120, 'scene 236 6_C_INI_UNLOADING'), (125, 'scene 245 200_WORLD_TOUR')]
        spans = season_perf.phase_spans([(launch + t, text) for t, text in timeline], launch)
        self.assertEqual([(a, b if b != float('inf') else None, p) for a, b, p in spans], [
            (0.0, 20, 'menus'), (20, 30, 'loading'), (30, 35, 'pre_race'), (35, 40, 'countdown'),
            (40, 100, 'race'), (100, 120, 'rewards'), (120, 125, 'loading'), (125, None, 'menus')])
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp)
            (run / 'events.txt').write_text(''.join(f'{int((launch + t) * 1000)} 0 {text}\n' for t, text in timeline))
            perf = []
            for second in range(1, 131):
                racing = 40 < second <= 100
                frames = [4.0] * 240 if racing else [16.0] * 60
                if second == 70:
                    frames[10] = 80.0
                perf.append({'t': second, 'window_ms': 1000, 'fps': len(frames), 'frame_ms': frames,
                             'gpu': {'frames': len(frames), 'frame_ms': 3.0 if racing else 1.0,
                                     'passes_ms': {'gtao': 0.5 if racing else 0.1}, 'swap_ms': 0.1,
                                     'scene_ms': 2.4 if racing else 0.8},
                             'render_thread_busy_ms': 600, 'submit_thread': {'busy_ms': 300},
                             'shader_work': {'pipelines': [2, 5.0, 3.0, 0]} if second == 50 else {},
                             'long_frames': [[80.0, 0, 0, 0]] if second == 70 else []})
            (run / 'perf.jsonl').write_text(''.join(json.dumps(p) + '\n' for p in perf))
            (run / 'season.jsonl').write_text(json.dumps({'event': 1, 'name': 'Class F Asian Tour', 'start_s': 0,
                                                          'end_s': 125, 'season': {'name': 'AMATEUR DIVISION', 'events': [['Tsukuba Short', 'CURRENT EVENT']]}}) + '\n')
            (run / 'summary.json').write_text(json.dumps({'launch_time': launch}))
            report, records = season_perf.survey(run)
            race = report['events'][0]['phases']['race']
            self.assertEqual(report['events'][0]['track'], 'Tsukuba Short')
            self.assertEqual(race['over_50ms'], 1)
            self.assertAlmostEqual(race['gpu_ms_per_frame'], 3.0, places=2)
            self.assertEqual(race['gpu_parts_ms']['gtao'], 0.5)
            self.assertEqual(race['shader_work'], {'pipelines': [2, 5.0]})
            self.assertEqual(report['long_frames'][0]['phase'], 'race')
            self.assertEqual(report['long_frames'][0]['event'], 1)
            self.assertIn('loading', report['events'][0]['phases'])
            self.assertEqual(report['slowest_p99'][0]['phase'], 'menus')
            self.assertIn('perf', records[0])


if __name__ == '__main__':
    unittest.main()
