"""The quickrace_loop scenario: Quick Races on a fixed list of (manufacturer, location) pairs, pass
after pass, in one game process: the soak that revisits tracks and cars.

e2e.py runs the boot to the main menu, then hands the run to Loop.run (the step action 'qrloop').
A cycle, driven by the title's screen events and OCR:

- the Free Play menu's QUICK RACE, the manufacturer select (115_C_BUY_MFRSELECT: Right until OCR
  reads the pair's manufacturer, at about y 115), its first car (108_C_BUY_CARSELECT), the paint
  and the difficulty (114, 838), the location select (122_F_TRK_LOCSELECT: Right until the pair's
  location) and its first track layout (123_F_TRK_TRACKSELECT, A is GO RACE);
- the loading (17_DYNAMIC_LOADING_FREEPLAY), the pre-race menu, the race (the title's AI drives
  with engageaionstart) for --race-seconds after the race scenario's push;
- the pause menu (925_PAUSE_MENU): QUIT RACE, its dialog, the post-race menu's CONTINUE (the
  unloading, then the track select again), and B back to the Free Play menu (track, location,
  car, manufacturer select, Free Play).

Both selects keep the last choice, so a pair is found by its name, not by a count of presses. The
first pass takes the pairs from --qr-pairs or, without it, a manufacturer and a location
--qr-step (locations: LOCATION_STEP) presses on from the previous ones, and records their names; the later passes repeat the
same pairs in the same order. The run stops after the pass in which it is --min-hours old, at the
cap within a cycle, or on a failure.

Per cycle it appends to qrloop.jsonl: the pass, the pair's index, the manufacturer, car,
location and track read, and the times; qrloop-state.json holds the state after every step. The
soak sampler's label is 'pass <p> pair <i>: <phase>', which soak_report.py splits by pass.
"""
import json
import re
import time
from pathlib import Path

SCENE = re.compile(r'^scene \d+ (\S+)$')
RACE_PUSH = re.compile(r'^push \S+_Race$')
NAME_Y = (100, 130)  # the select screens' name line (the title line above it is the country)
# Locations of the first pass: every second one of the select's 26, so 11 pairs reach the large
# tracks too (the Quick Race grid is the title's full 12 cars on every track).
LOCATION_STEP = 2


def title_line(lines, y_range=NAME_Y, x_max=640):
    """The select screens' name: the line about y 115 on the left half."""
    for line in sorted(lines or [], key=lambda l: l['y']):
        if y_range[0] <= line['y'] <= y_range[1] and line['x'] < x_max and line['text'].strip():
            return line['text'].strip()
    return None


def same_name(a, b):
    """OCR may drop or change a character: names match when one holds the other, ignoring case
    and spaces, or when they differ in at most one character."""
    if not a or not b:
        return False
    a, b = re.sub(r'\s+', '', a.lower()), re.sub(r'\s+', '', b.lower())
    if a in b or b in a:
        return True
    return len(a) == len(b) and sum(x != y for x, y in zip(a, b)) <= 1


class Loop:
    def __init__(self, e2e, screen, input_file, out, deadline, pairs=None, count=11, step=3,
                 race_s=180.0, min_hours=2.0, max_passes=10, sampler=None, started=None):
        self.e2e, self.screen, self.input_file, self.out = e2e, screen, input_file, Path(out)
        self.deadline, self.race_s, self.min_hours = deadline, race_s, min_hours
        self.count, self.step, self.max_passes = count, step, max_passes
        self.pairs = [list(p) for p in pairs] if pairs else []
        self.sampler, self.started = sampler, started or time.time()
        self.cursor = 0.0
        self.scene = None
        self.cycles = []
        self.cycle = None
        self.failure = None
        self.capped = False
        self.state_path = self.out / 'qrloop-state.json'
        self.log_path = self.out / 'qrloop.jsonl'
        self.log_path.write_text('')
        (self.out / 'events').mkdir(exist_ok=True)

    # Plumbing (as in career_season.Season).
    def now_s(self):
        return round(time.time() - self.started, 1)

    def label(self, text):
        if self.sampler:
            self.sampler.set_label(text)
        self.save_state(phase=text)

    def save_state(self, **extra):
        state = {'t_s': self.now_s(), 'launch_time': round(self.started, 3), 'scene': self.scene,
                 'cycles_done': len(self.cycles), 'pairs': self.pairs, 'cycle': self.cycle,
                 'failure': self.failure, 'capped': self.capped, **extra}
        tmp = self.state_path.with_suffix('.tmp')
        tmp.write_text(json.dumps(state, indent=2))
        tmp.replace(self.state_path)

    def poll(self):
        new = [e for e in self.screen.events(0) if e[0] > self.cursor]
        for t, text in new:
            self.cursor = t
            if (match := SCENE.match(text)):
                self.scene = match.group(1)
        return new

    def wait_event(self, pattern, timeout, after=None):
        after = time.time() if after is None else after
        until = min(self.deadline, time.time() + timeout)
        while time.time() < until:
            match = next((e for e in self.screen.events(after) if re.search(pattern, e[1])), None)
            if match:
                self.poll()
                return match[1]
            time.sleep(0.2)
        self.poll()
        return None

    def press(self, action):
        self.e2e.press(self.input_file, action, 0.12)

    def press_until_event(self, action, pattern, timeout, retry=5):
        until = min(self.deadline, time.time() + timeout)
        while time.time() < until:
            after = time.time()
            self.press(action)
            if self.wait_event(pattern, max(0.5, min(retry, until - time.time())), after):
                return True
        return False

    def read(self, timeout=6):
        lines, _ = self.screen.read(time.time() + 0.3, time.time() + timeout)
        return lines or []

    def read_until(self, text, timeout, action=None, retry=5):
        until = min(self.deadline, time.time() + timeout)
        next_press = time.time()
        while time.time() < until:
            if action and time.time() >= next_press:
                self.press(action)
                next_press = time.time() + retry
            lines = self.read(max(0.5, min(retry, until - time.time())))
            if lines and self.e2e.shows(lines, text):
                return lines
        return None

    def keep_capture(self, name):
        frame = self.e2e.newest_capture(self.out / 'capture')
        if not frame:
            return None
        target = self.out / 'events' / f'{name}.png'
        try:
            self.e2e.ppm_to_png(frame, target, 2)
            return target.name
        except (OSError, ValueError):
            return None

    def fail(self, reason):
        if time.time() >= self.deadline:
            self.capped = True
            self.save_state(phase=f'cap reached ({reason})')
            return False
        self.failure = reason
        self.keep_capture(f'failure-{len(self.cycles) + 1:03d}')
        self.save_state(phase='failed')
        return False

    # The run.
    def run(self):
        """True when the passes ended after --min-hours (or the cap came after a whole pass)."""
        self.poll()
        passes = 0
        while passes < self.max_passes:
            passes += 1
            for index in range(self.count if passes == 1 and not self.pairs else len(self.pairs)):
                if time.time() >= self.deadline:
                    self.capped = True
                    self.save_state(phase='cap reached')
                    return passes > 2
                if not self.one(passes, index):
                    return self.capped and passes > 2
            if passes >= 2 and (time.time() - self.started) / 3600 >= self.min_hours:
                break
        self.save_state(phase='done')
        return True

    def one(self, pass_number, index):
        first = pass_number == 1 and index >= len(self.pairs)
        self.cycle = {'pass': pass_number, 'pair': index, 'start_s': self.now_s()}
        tag = f'pass {pass_number} pair {index}'
        self.label(f'{tag}: menus')
        if not self.to_quick_race():
            return self.fail(f'{tag}: the Free Play menu did not open QUICK RACE')
        # The manufacturer.
        if not self.wait_event(r'^scene \d+ 115_C_BUY_MFRSELECT$', 20, after=self.cursor - 30):
            return self.fail(f'{tag}: no manufacturer select')
        time.sleep(1.5)
        if first:
            mfr = self.step_and_read(self.step if index else 0)
        else:
            mfr = self.seek(self.pairs[index][0])
        if not mfr:
            return self.fail(f'{tag}: the manufacturer was not read')
        self.cycle['manufacturer'] = mfr
        if not self.press_until_event('a', r'^scene \d+ 108_C_BUY_CARSELECT$', 20):
            return self.fail(f'{tag}: no car select')
        time.sleep(1.5)
        lines = self.read()
        self.cycle['car'] = ' '.join(l['text'].strip() for l in sorted(lines, key=lambda l: l['y'])
                                     if 60 <= l['y'] <= 130 and l['x'] < 640) or None
        for scene in ('114_C_BUY_BUYCAR_COLOR', '838_I_PRO_OPTIONS_SET_DIFFICULTY', '122_F_TRK_LOCSELECT'):
            if not self.press_until_event('a', rf'^scene \d+ {scene}$', 20, retry=3):
                return self.fail(f'{tag}: {scene} did not open')
            time.sleep(1.5)
        # The location.
        if first:
            location = self.step_and_read(LOCATION_STEP if index else 0)
        else:
            location = self.seek(self.pairs[index][1])
        if not location:
            return self.fail(f'{tag}: the location was not read')
        self.cycle['location'] = location
        if not self.press_until_event('a', r'^scene \d+ 123_F_TRK_TRACKSELECT$', 20, retry=3):
            return self.fail(f'{tag}: no track select')
        time.sleep(1.5)
        self.cycle['track'] = title_line(self.read())
        if first:
            self.pairs.append([mfr, location])
        # The race.
        self.label(f'{tag}: loading')
        if not self.press_until_event('a', r'^scene \d+ 17_DYNAMIC_LOADING_FREEPLAY$', 20, retry=3):
            return self.fail(f'{tag}: the race did not load')
        if not self.wait_event(r'^scene \d+ 4_C_INI_PRERACE$', 120, after=self.cursor - 0.001):
            return self.fail(f'{tag}: no pre-race menu')
        time.sleep(3)
        self.label(f'{tag}: pre-race')
        if not self.press_until_event('down,up,a', r'^scene \d+ 947_HUD$|^countdown placed$', 40, retry=6):
            return self.fail(f'{tag}: the pre-race menu did not start the race')
        if not self.wait_event(r'^push \S+_Race$', 90, after=self.cursor - 0.001):
            return self.fail(f'{tag}: the race did not start')
        self.cycle['race_s'] = self.now_s()
        self.label(f'{tag}: race')
        until = min(self.deadline, time.time() + self.race_s)
        while time.time() < until:
            self.poll()
            if self.scene == '924_POST_RACE':  # the AI finished the race first
                break
            time.sleep(0.5)
        if time.time() >= self.deadline:
            return self.fail(f'{tag}: during the race')
        # Quit, then back to the Free Play menu.
        self.label(f'{tag}: quit')
        if self.scene != '924_POST_RACE':
            if not self.press_until_event('start', r'^scene \d+ 925_PAUSE_MENU$', 30, retry=5):
                return self.fail(f'{tag}: no pause menu')
            time.sleep(1)
            if not self.e2e.select_label(self.screen, self.input_file, 45, 'QUIT RACE'):
                return self.fail(f'{tag}: QUIT RACE could not be selected')
            if not self.press_until_event('a', r'^scene \d+ 037_I_D_POP_DIALOG$', 15, retry=5):
                return self.fail(f'{tag}: no quit dialog')
            time.sleep(1)
            if not self.press_until_event('a', r'^scene \d+ 924_POST_RACE$', 30, retry=5):
                return self.fail(f'{tag}: the race did not end')
        time.sleep(1.5)
        # The post-race menu ignores the first press at times; it starts the replay when idle.
        if not self.press_until_event('a', r'^scene \d+ 123_F_TRK_TRACKSELECT$', 60, retry=4):
            return self.fail(f'{tag}: the post-race menu did not go back to the track select')
        self.label(f'{tag}: menus')
        time.sleep(1.5)
        if not self.read_until('QUICK RACE', 60, action='b', retry=3):
            return self.fail(f'{tag}: B did not reach the Free Play menu')
        self.cycle['end_s'] = self.now_s()
        self.cycles.append(self.cycle)
        self.log_path.write_text(''.join(json.dumps(c) + '\n' for c in self.cycles))
        self.save_state(phase='cycle done')
        return True

    def to_quick_race(self):
        """From the main menu (PLAY CAREER) or the Free Play menu (QUICK RACE) to the manufacturer
        select."""
        lines = self.read()
        if self.e2e.shows(lines, 'PLAY CAREER') and not self.e2e.shows(lines, 'QUICK RACE'):
            self.press('right,right,right')
            time.sleep(1.5)
            if not self.e2e.select_label(self.screen, self.input_file, 20, 'FREE PLAY'):
                return False
            if not self.read_until('QUICK RACE', 15, action='a', retry=6):
                return False
        if not self.e2e.select_label(self.screen, self.input_file, 20, 'QUICK RACE'):
            return False
        return self.press_until_event('a', r'^scene \d+ 115_C_BUY_MFRSELECT$', 20, retry=5)

    def current_name(self, timeout=8):
        """The select screen's name, once OCR reads one."""
        until = time.time() + timeout
        while time.time() < until:
            name = title_line(self.read())
            if name:
                return name
        return None

    def step_and_read(self, presses):
        """Right `presses` times, each until the name read changes (a select can drop a press
        while it settles: the soak of 30 October 2026 kept one pair for five), then the name."""
        name = self.current_name()
        for _ in range(presses):
            for _attempt in range(4):
                self.press('right')
                until = time.time() + 4
                changed = None
                while time.time() < until:
                    now = title_line(self.read(2))
                    if now and not same_name(now, name):
                        changed = now
                        break
                if changed:
                    name = changed
                    break
            else:
                return None
        time.sleep(1)
        return self.current_name() or name

    def seek(self, name, limit=80):
        """Right until the select screen shows `name` (both selects wrap round)."""
        for _ in range(limit):
            if same_name(title_line(self.read()), name):
                return name
            self.press('right')
            time.sleep(0.9)
        return None

    def summary(self):
        by_pass = {}
        for c in self.cycles:
            by_pass.setdefault(c['pass'], 0)
            by_pass[c['pass']] += 1
        return {'cycles': len(self.cycles), 'cycles_by_pass': by_pass, 'pairs': self.pairs,
                'failure': self.failure, 'capped': self.capped}
