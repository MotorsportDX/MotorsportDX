"""The career_season scenario: World Tour event after event in one game process.

e2e.py runs the boot and the career menus up to the World Tour screen, then hands the run to
Season.run (the step action 'season'). The driver reacts to the title's screen events
(FM4_TEST_EVENTS_FILE: 'scene <id> <name>', 'push'/'pop' of the render scenarios) and reads the
screens it must judge by OCR:

- 200_WORLD_TOUR: the globe zooms to the next stop, the title may open a CHALLENGE EVENT box
  (201_SPECIAL_EVENT_DIALOG), then the stop's events show in a row. The driver reads each of
  them (Right), takes the first one the player's car may enter (car_fits, from the RESTRICTIONS
  box) and opens it (204_C_RACE_INFO).
- 204_C_RACE_INFO: the car, driver level and credits from the footer, SEASON STATUS (the
  season's name, its events and their state), then START RACE.
- the races: the pre-race menu (4_C_INI_PRERACE), the race (push/pop '<cars>_Race'; the title's
  AI drives with engageaionstart), the post-race menu (924_POST_RACE, CONTINUE at once: it starts
  the replay when left idle), the earnings (920), the level progress (909), the level and
  affinity rewards (910), a gift car (108 car select, 114 paint, its YES dialog), until the next
  World Tour screen. An event of several races (a championship's heats) runs its races in turn.

Per event it appends a record to season.jsonl: the event, the track, the car, the place of every
race, the earnings shown, the credits before (this event's screen) and after (the next one), and
the times; season-state.json holds the run's state after every step, so a stopped run can be
read afterwards. Screens the driver does not know are logged with their OCR text and a capture
(events/) and pressed through with A only where they show a CONTINUE, OK or YES.

The car stays the one the player drives: the driver changes or buys no car yet. A stop where the
car fits no event therefore ends at the title's own refusal (a screen noted, then a stall).
"""
import json
import re
import shutil
import time
from pathlib import Path

RACE_PUSH = re.compile(r'^push (\S+)_Race$')
RACE_POP = re.compile(r'^pop (\S+)_Race$')
SCENE = re.compile(r'^scene \d+ (\S+)$')
CREDITS = re.compile(r'^(\d[\d ,.]*\d)\s*CR$')
LEVEL = re.compile(r'^[LI1][\s\-]*[VI][\s\-]*[LI1]?[\s\-]*(\d+)$', re.I)
PLACE = re.compile(r'^(\d{1,2})\s*(st|nd|rd|th)$', re.I)
SIGNED_CREDITS = re.compile(r'^\+\s*(\d[\d ,.]*)\s*CR$')
# Screens that change by themselves: wait for the next event.
PASSIVE = {'EMPTY_SCENE', 'NotImplemented', '035_D_LOADING', '6_C_INI_UNLOADING',
           '100_P_PRE_BACKGROUNDMAIN', '19_DYNAMIC_LOADING_WORLDTOUR', '947_HUD', '968_TELEMETRY'}
# Pop-ups the title opens over a screen: one that follows another screen within POPUP_S is part
# of it (the post-race menu opens one at once) and gets no press of its own.
DIALOGS = {'037_I_D_POP_DIALOG', '032_D_POP_MESSAGE', '044_D_POP_UP'}
POPUP_S = 2.0
# The World Tour row of events: its footer's hint (with an event's name above it, row_event).
ROW_TEXT = 'CHANGE CAR'
STALL_S = 180
RACE_TIMEOUT_S = 25 * 60


def credits_of(lines):
    """The player's credits in the footer line ('8,610 CR'), or None."""
    for line in lines:
        match = CREDITS.match(line['text'].strip())
        if match and line['y'] > 600:
            return int(re.sub(r'[^\d]', '', match.group(1)))
    return None


def footer(lines):
    """(car, level, credits) of the footer of the World Tour row and the event screen: the line
    left of the level and the credits at the same height."""
    credit_line = next((l for l in lines if l['y'] > 600 and CREDITS.match(l['text'].strip())), None)
    if not credit_line:
        return None, None, None
    row = sorted((l for l in lines if abs(l['y'] - credit_line['y']) <= 10), key=lambda l: l['x'])
    level = next((int(LEVEL.match(l['text'].strip()).group(1)) for l in row
                  if LEVEL.match(l['text'].strip())), None)
    car = row[0]['text'].strip() if row and row[0] is not credit_line and not LEVEL.match(row[0]['text'].strip()) else None
    return car, level, credits_of([credit_line])


def row_event(lines):
    """The highlighted event of the World Tour row: its name (the title line above the
    description, about y 495) and its bonus line, or None."""
    names = [l for l in lines if 470 <= l['y'] <= 510 and l['x'] > 450]
    if not names:
        return None
    bonus = ' '.join(l['text'].strip() for l in lines if 575 <= l['y'] <= 600 and l['x'] > 600)
    track = None
    for index, line in enumerate(sorted(lines, key=lambda l: l['y'])):
        if line['text'].strip().upper() == 'TRACK':
            below = [l for l in lines if 0 < l['y'] - line['y'] <= 30 and abs(l['x'] - line['x']) <= 40]
            track = below[0]['text'].strip() if below else None
    return {'name': names[0]['text'].strip(), 'bonus': bonus or None, 'track': track}


def season_status(lines):
    """The SEASON STATUS box: {'name': the season's title, 'events': [[destination, status]]}."""
    header = next((l for l in lines if l['y'] < 220 and 'DIVISION' in l['text'].upper()), None)
    header = header or next((l for l in sorted(lines, key=lambda l: l['y'])
                             if l['y'] < 220 and l['text'].strip()), None)
    statuses = [l for l in lines if re.search(r'EVENT|STARTED', l['text'].upper()) and l['y'] > 280]
    events = []
    for status in sorted(statuses, key=lambda l: l['y']):
        place = [l for l in lines if abs(l['y'] - status['y']) <= 10 and l['x'] < status['x'] - 40]
        events.append([place[-1]['text'].strip() if place else None, status['text'].strip().upper()])
    return {'name': header['text'].strip() if header else None, 'events': events}


def restrictions(lines):
    """The lines of the EVENT RESTRICTIONS box (without its title and CANCEL)."""
    return [l['text'].strip() for l in lines
            if l['text'].strip() and l['y'] < 380 and 'RESTRICTIONS' not in l['text'].upper()]


def car_fits(rules, car):
    """Whether a car seems to fit the event's restriction lines: only the rules the OCR text can
    judge (the make and model in the car's name); a rule it cannot judge (performance index,
    body family, engine) is left to the title, which refuses a car that does not fit."""
    for rule in rules:
        match = re.search(r'(?:MAKE|MANUFACTURER|MODEL) must be (.+)$', rule, re.I)
        if match and car and match.group(1).strip().upper() not in car.upper():
            return False
    return True


class Season:
    """Drives World Tour events until the stop condition, the cap or a failure."""

    def __init__(self, e2e, screen, input_file, out, deadline, seasons=1, min_hours=0.0,
                 sampler=None, started=None):
        self.e2e, self.screen, self.input_file, self.out = e2e, screen, input_file, Path(out)
        self.deadline, self.seasons, self.min_hours = deadline, seasons, min_hours
        self.sampler, self.started = sampler, started or time.time()
        self.cursor = 0.0  # time of the last screen event read
        self.scene, self.scene_t = None, 0.0
        self.queue = []  # (time, scene) not handled yet, oldest first
        self.scene_log = []  # (time, scene) of every scene event
        self.handled_t = 0.0  # time of the scene handled last
        self.racing = False
        self.race_log = []  # [push time, pop time or None] of every race seen
        self.events_dir = self.out / 'events'
        self.events_dir.mkdir(exist_ok=True)
        self.records = []
        self.event = None  # the record of the event being driven
        self.seasons_done = []
        self.unknown = []
        self.failure = None
        self.capped = False
        self.state_path = self.out / 'season-state.json'
        self.log_path = self.out / 'season.jsonl'
        self.log_path.write_text('')

    # Plumbing.
    def now_s(self):
        return round(time.time() - self.started, 1)

    def label(self, text):
        if self.sampler:
            self.sampler.set_label(text)
        self.save_state(phase=text)

    def save_state(self, **extra):
        state = {'t_s': self.now_s(), 'scene': self.scene, 'racing': self.racing,
                 'events_done': len(self.records), 'seasons_done': self.seasons_done,
                 'event': self.event, 'unknown_screens': self.unknown[-20:], 'failure': self.failure,
                 **extra}
        tmp = self.state_path.with_suffix('.tmp')
        tmp.write_text(json.dumps(state, indent=2))
        tmp.replace(self.state_path)

    def poll(self):
        """Reads the screen events written since the last poll: the current scene, the queue of
        scenes to handle, and whether a race runs."""
        new = [e for e in self.screen.events(0) if e[0] > self.cursor]
        for t, text in new:
            self.cursor = t
            if (match := SCENE.match(text)):
                self.scene, self.scene_t = match.group(1), t
                self.queue.append((t, self.scene))
                self.scene_log.append((t, self.scene))
            elif RACE_PUSH.match(text):
                self.racing = True
                self.race_log.append([t, None])
            elif RACE_POP.match(text):
                self.racing = False
                if self.race_log and self.race_log[-1][1] is None:
                    self.race_log[-1][1] = t
        return new

    def wait_event(self, pattern, timeout, after=None):
        """Waits for a screen event matching the regular expression after `after` (default: now);
        returns its text or None."""
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

    def press_until_event(self, action, pattern, timeout, retry=5):
        """Presses the action every `retry` s until a matching screen event follows it."""
        until = min(self.deadline, time.time() + timeout)
        while time.time() < until:
            after = time.time()
            self.e2e.press(self.input_file, action, 0.12)
            if self.wait_event(pattern, max(0.5, min(retry, until - time.time())), after):
                return True
        return False

    def read(self, timeout=8):
        """The OCR lines of a capture taken from now on, or []."""
        lines, _ = self.screen.read(time.time() + 0.3, time.time() + timeout)
        return lines or []

    def read_until(self, text, timeout, action=None, retry=5):
        """Reads captures (pressing the action every `retry` s when given) until one shows the
        text (e2e.shows syntax); returns its lines or None."""
        until = min(self.deadline, time.time() + timeout)
        next_press = time.time()
        while time.time() < until:
            if action and time.time() >= next_press:
                self.e2e.press(self.input_file, action, 0.12)
                next_press = time.time() + retry
            lines = self.read(max(0.5, min(retry, until - time.time())))
            if lines and self.e2e.shows(lines, text):
                return lines
        return None

    def keep_capture(self, name):
        """Converts the newest capture to events/<name>.png (the run keeps only a few captures)."""
        frame = self.e2e.newest_capture(self.out / 'capture')
        if not frame:
            return None
        tmp = self.events_dir / 'capture.ppm'
        try:
            shutil.copyfile(frame, tmp)
            target = self.events_dir / f'{name}.png'
            self.e2e.ppm_to_png(tmp, target, 2)
            tmp.unlink()
            return target.name
        except (OSError, ValueError):
            return None

    def fail(self, reason):
        """Ends the run: a failure, or the cap when the deadline has passed (a wait it cut short
        fails too, so the cap is told apart by the time)."""
        if time.time() >= self.deadline:
            self.capped = True
            self.keep_capture(f'cap-{len(self.records) + 1:02d}')
            self.save_state(phase=f'cap reached ({reason})')
            return False
        self.failure = reason
        self.keep_capture(f'failure-{len(self.records) + 1:02d}')
        self.save_state(phase='failed')
        return False

    def stop_reached(self):
        hours = (time.time() - self.started) / 3600
        return len(self.seasons_done) >= self.seasons and hours >= self.min_hours

    def next_scene(self):
        """The oldest scene not handled yet, or None; a pop-up that followed the scene handled
        before it within POPUP_S belongs to that screen and is passed over."""
        while self.queue:
            t, scene = self.queue.pop(0)
            if scene in PASSIVE:
                continue
            if scene in DIALOGS and t - self.handled_t < POPUP_S:
                continue
            self.handled_t = t
            return t, scene
        return None

    def newer(self, t):
        """Whether another screen followed the scene at t: a scene event after it, not counting
        a pop-up within POPUP_S (part of the same screen)."""
        self.poll()
        return any(t2 > t and not (scene in DIALOGS and t2 - t < POPUP_S)
                   for t2, scene in reversed(self.scene_log[-20:]))

    # The run.
    def run(self):
        """True when the stop condition was reached, False on a failure; the cap (the deadline)
        counts as reached when at least one event finished."""
        self.poll()
        while True:
            if self.stop_reached():
                self.save_state(phase='done')
                return True
            if time.time() >= self.deadline:
                self.save_state(phase='cap reached')
                return bool(self.records)
            if not self.world_tour() or not self.event_screen() or not self.play_event():
                return self.capped and bool(self.records)

    def world_tour(self):
        """From the World Tour screen (or on the way to it) to the chosen event's screen; screens
        on the way (a challenge box, a season's end) are handled as they come."""
        number = len(self.records) + 1
        self.event = {'event': number, 'start_s': self.now_s()}
        self.label(f'event {number}: world tour')
        until = time.time() + 240
        while time.time() < until:
            if time.time() >= self.deadline:
                return self.fail('between events')
            self.poll()
            item = self.next_scene()
            if item and item[1] not in ('200_WORLD_TOUR', '102_H_CAR_HOME'):
                if item[1] == '201_SPECIAL_EVENT_DIALOG':
                    lines = self.read_until('CONTINUE', 30)
                    self.event['challenge'] = [l['text'] for l in lines or []][:6]
                    if not self.read_until('!CHALLENGE EVENT', 40, action='a', retry=6):
                        return self.fail(f'event {number}: the CHALLENGE EVENT box stays')
                    # Closing the box writes no scene event: the World Tour screen is back.
                    self.scene = '200_WORLD_TOUR'
                elif not self.handle(item[1], None, item[0]):
                    return False
                until = time.time() + 240
                continue
            if self.scene != '200_WORLD_TOUR':
                time.sleep(0.3)
                continue
            lines = self.read(6)
            if lines and self.e2e.shows(lines, ROW_TEXT) and row_event(lines):
                return self.choose_event(lines)
        return self.fail(f'event {number}: the World Tour row of events did not show (scene {self.scene})')

    def choose_event(self, lines):
        """Reads the stop's events (Right until the first comes round or nothing changes), takes
        the first one whose restrictions the car may meet, and opens it."""
        car, level, credit = footer(lines)
        row = []
        current = row_event(lines)
        for _ in range(6):
            if not current or any(r['name'] == current['name'] for r in row):
                break
            self.e2e.press(self.input_file, 'x', 0.12)
            box = self.read_until('EVENT RESTRICTIONS', 10)
            current['restrictions'] = restrictions(box) if box else None
            if box:
                # B closes the box; on the row itself it would leave the World Tour.
                self.e2e.press(self.input_file, 'b', 0.12)
                self.read_until('!EVENT RESTRICTIONS', 10)
            row.append(current)
            self.e2e.press(self.input_file, 'right', 0.12)
            current = row_event(self.read_until(ROW_TEXT, 10) or [])
        self.event.update({'stop_events': row, 'car': car, 'level_before': level, 'credits_row': credit})
        choice = next((i for i, r in enumerate(row) if car_fits(r.get('restrictions') or [], car)), 0)
        if row:
            self.event.update({'name': row[choice]['name'], 'track': row[choice].get('track'),
                               'bonus': row[choice].get('bonus')})
        # Back to the first event (whether the row wraps or not), then Right to the choice.
        moves = ['left'] * len(row) + ['right'] * choice
        if moves:
            self.e2e.press(self.input_file, ','.join(moves), 0.12)
        time.sleep(1.5)
        self.save_state(phase='event chosen')
        if not self.press_until_event('a', r'^scene \d+ 204_C_RACE_INFO$', 40, retry=5):
            return self.fail(f"event {self.event['event']}: its screen did not open")
        return True

    def event_screen(self):
        """The event's screen: the footer, SEASON STATUS, then START RACE."""
        number = self.event['event']
        self.label(f'event {number}: event screen')
        lines = self.read_until('START RACE', 30)
        if not lines:
            return self.fail(f'event {number}: no START RACE')
        car, level, credit = footer(lines)
        self.event.update({'car': car or self.event.get('car'),
                           'level_before': level or self.event.get('level_before'),
                           'credits_before': credit if credit is not None else self.event.get('credits_row')})
        if self.records and self.records[-1].get('credits_after') is None:
            self.records[-1]['credits_after'] = self.event['credits_before']
            self.write_log()
        if self.e2e.select_label(self.screen, self.input_file, 30, 'SEASON STATUS') and \
                self.press_until_event('a', r'^scene \d+ 203_SEASON_STATUS$', 20, retry=8):
            box = self.read_until('EVENT STATUS', 15)
            if box:
                self.event['season'] = season_status(box)
                self.keep_capture(f'{number:02d}-season-status')
            self.read_until('!EVENT STATUS', 20, action='a', retry=5)
        if not self.e2e.select_label(self.screen, self.input_file, 30, 'START RACE'):
            return self.fail(f'event {number}: START RACE could not be selected')
        if not self.press_until_event('a', r'^scene \d+ 19_DYNAMIC_LOADING_WORLDTOUR$', 30, retry=5):
            return self.fail(f'event {number}: the event did not load')
        self.event['races'] = []
        # The event's screens so far are done with; the loading's and those after it are not.
        loads = [i for i, (_, scene) in enumerate(self.queue) if scene == '19_DYNAMIC_LOADING_WORLDTOUR']
        self.queue = self.queue[loads[-1] + 1:] if loads else []
        return True

    def play_event(self):
        """The event's races and the screens after them, until the next World Tour screen."""
        number = self.event['event']
        race = None
        first_race = len(self.race_log)
        last_change = time.time()
        while True:
            if time.time() >= self.deadline:
                return self.fail(f'event {number}: during the event')
            if self.poll():
                last_change = time.time()
            if len(self.race_log) - first_race > len(self.event['races']):
                # A race started (it may have ended by now too).
                push, pop = self.race_log[first_race + len(self.event['races'])]
                race = {'race': len(self.event['races']) + 1, 'start_s': round(push - self.started, 1)}
                self.event['races'].append(race)
                self.label(f'event {number}: race {race["race"]}')
                if pop is None and not self.wait_event(r'^pop \S+_Race$', RACE_TIMEOUT_S, after=self.cursor):
                    return self.fail(f'event {number}: race {race["race"]} did not end')
                pop = self.race_log[first_race + race['race'] - 1][1] or time.time()
                race['seconds'] = round(pop - push, 1)
                self.label(f'event {number}: after race {race["race"]}')
                last_change = time.time()
                continue
            item = self.next_scene()
            if not item:
                if time.time() - last_change > STALL_S:
                    return self.fail(f'event {number}: stalled on {self.scene}')
                time.sleep(0.3)
                continue
            t, scene = item
            if scene == '200_WORLD_TOUR' and self.event['races']:
                # The next stop's screen: put it back for world_tour.
                self.queue.insert(0, item)
                return self.finish_event()
            if not self.handle(scene, race, t):
                return False
            last_change = time.time()

    def handle(self, scene, race, t):
        """One screen of the event (or one met on the way to the next)."""
        number = self.event['event']
        e = self.e2e
        if scene == '4_C_INI_PRERACE':
            lines = self.read(6)
            heat = next((l['text'].strip() for l in lines if re.match(r'^HEAT\b', l['text'].strip(), re.I)), None)
            if heat:
                self.event.setdefault('heats', []).append(heat)
            time.sleep(2)
            # The menu ignores A until its focus moved once; START RACE creates the HUD.
            if not self.press_until_event('down,up,a', r'^scene \d+ 947_HUD$|^countdown placed$', 60, retry=8):
                return self.fail(f'event {number}: the pre-race menu did not start the race')
            started = len(self.race_log)
            if not self.racing and not self.wait_event(r'^push \S+_Race$', 90, after=self.cursor - 0.001)                     and len(self.race_log) == started:
                return self.fail(f'event {number}: the race did not start after the countdown')
            return True
        if scene == '924_POST_RACE':
            # CONTINUE at once: the menu starts the replay when left idle.
            lines = self.read(4)
            text = [l['text'].strip() for l in lines]
            if race is not None:
                place = next((int(PLACE.match(x).group(1)) for x in text if PLACE.match(x)), None)
                race.update({'place': place, 'post_race': text[:12],
                             'capture': self.keep_capture(f'{number:02d}-race{race["race"]}-post')})
            return self.press_on(scene, t, 30)
        if scene == '920_POST_RACE_EARNINGS':
            lines = self.read_until('Total', 15) or []
            total = None
            totals = [l for l in lines if l['text'].strip().upper() == 'TOTAL']
            if totals:
                below = [l for l in lines if 0 < l['y'] - totals[0]['y'] <= 40
                         and SIGNED_CREDITS.match(l['text'].strip())]
                if below:
                    total = int(re.sub(r'[^\d]', '', below[0]['text']))
            if race is not None:
                race.update({'earnings_total': total, 'earnings': [l['text'].strip() for l in lines][:24],
                             'earnings_capture': self.keep_capture(f'{number:02d}-race{race["race"]}-earnings')})
            return self.press_on(scene, t, 60, retry=6)
        if scene == '909_LEVEL_PROGRESS':
            self.event['level_progress'] = [l['text'].strip() for l in self.read(6)][:16]
            return self.press_on(scene, t, 60, retry=6)
        if scene == '910_POST_RACE_LEVEL_REWARDS':
            return self.reward_pages(t)
        if scene == '108_C_BUY_CARSELECT':
            # A gift car (a level reward): A keeps the default.
            lines = self.read(6)
            name = ' '.join(l['text'].strip() for l in sorted(lines, key=lambda l: l['y']) if l['y'] < 140)
            self.event.setdefault('gift_cars', []).append(name or None)
            return self.press_on(scene, t, 30)
        if scene == '114_C_BUY_BUYCAR_COLOR':
            return self.press_on(scene, t, 30)
        lines = self.read(5)
        if e.shows(lines, 'Are you sure|=YES|CONTINUE|=OK|A OK'):
            e.press(self.input_file, 'a', 0.12)
            self.note(scene, lines, 'a')
        else:
            self.note(scene, lines, None)
        return True

    def reward_pages(self, t):
        """The level and affinity rewards: one scene, a page per reward; A turns the page."""
        until = time.time() + 120
        seen = []
        while time.time() < until and not self.newer(t):
            lines = self.read(6)
            text = [l['text'].strip() for l in lines]
            if text and text not in seen:
                seen.append(text)
                self.event.setdefault('rewards', []).append(text[:10])
                self.event.setdefault('reward_credits', []).extend(
                    int(re.sub(r'[^\d]', '', m.group(1))) for m in map(CREDITS.match, text) if m)
            self.e2e.press(self.input_file, 'a', 0.12)
            until_page = time.time() + 6
            while time.time() < until_page and not self.newer(t):
                time.sleep(0.2)
                if self.e2e.shows(self.read(1), '!' + '|'.join(text[:1]) if text else 'CONTINUE'):
                    break
        if not self.newer(t):
            return self.fail(f"event {self.event['event']}: the rewards did not move on")
        return True

    def press_on(self, scene, t, timeout, retry=5):
        """A until a scene event follows the screen's (scene at t)."""
        until = time.time() + timeout
        while time.time() < until:
            if self.newer(t):
                return True
            self.e2e.press(self.input_file, 'a', 0.12)
            until_retry = time.time() + retry
            while time.time() < until_retry and not self.newer(t):
                time.sleep(0.2)
        if self.newer(t):
            return True
        return self.fail(f"event {self.event['event']}: {scene} did not move on")

    def note(self, scene, lines, action):
        record = {'t_s': self.now_s(), 'event': self.event['event'], 'scene': scene, 'action': action,
                  'text': [l['text'] for l in lines][:12]}
        if scene not in {u['scene'] for u in self.unknown}:
            record['capture'] = self.keep_capture(f"screen-{len(self.unknown) + 1:02d}-{scene}")
        self.unknown.append(record)
        self.save_state(phase=f'screen {scene}')

    def finish_event(self):
        event = self.event
        event['end_s'] = self.now_s()
        event['seconds'] = round(event['end_s'] - event['start_s'], 1)
        event['places'] = [r.get('place') for r in event['races']]
        event['earnings_total'] = sum(r.get('earnings_total') or 0 for r in event['races'])
        event['credits_after'] = None  # read on the next event's screen
        rows = (event.get('season') or {}).get('events') or []
        # The season is complete when this event was its last one.
        if rows and rows[-1][1].startswith('CURRENT'):
            name = event['season'].get('name')
            event['season_complete'] = name
            self.seasons_done.append({'season': name, 'events': len(rows), 'ended_s': event['end_s'],
                                      'event': event['event']})
        self.records.append(event)
        self.write_log()
        self.save_state(phase='event done')
        return True

    def write_log(self):
        """season.jsonl: one line per finished event, rewritten as credits_after fills in."""
        self.log_path.write_text(''.join(json.dumps(r) + '\n' for r in self.records))

    def summary(self):
        """The season part of the run's summary.json. The credits check per event: the credits
        after it less those before it are the earnings shown plus the rewards' credits."""
        checks = []
        for r in self.records:
            before, after = r.get('credits_before'), r.get('credits_after')
            paid = r.get('earnings_total', 0) + sum(r.get('reward_credits') or [])
            checks.append(None if before is None or after is None else after - before == paid)
        return {'events': len(self.records), 'seasons_done': self.seasons_done, 'failure': self.failure,
                'capped': self.capped,
                'screens_noted': len(self.unknown),
                'credits_check': {'events': len(checks), 'match': checks.count(True),
                                  'differ': checks.count(False), 'unread': checks.count(None)},
                'places': [r.get('places') for r in self.records]}
