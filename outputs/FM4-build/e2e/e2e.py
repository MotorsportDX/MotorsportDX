"""End-to-end run of the FM4 build. Drives the diagnostic controller either from a fixed
timeline or toward a goal with Jev (TypeSafe System One) judging OCR'd screen text, then
collects GPU frame captures, the per-second perf trace and log error counts."""
import argparse, json, os, re, shutil, statistics, struct, subprocess, sys, time, zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
BUTTONS = {'up': 0x1, 'down': 0x2, 'left': 0x4, 'right': 0x8, 'start': 0x10, 'back': 0x20,
           'lb': 0x100, 'rb': 0x200, 'a': 0x1000, 'b': 0x2000, 'x': 0x4000, 'y': 0x8000}

# Each step: [seconds to wait before, action, hold seconds]. Actions are button names joined
# with '+', or 'rt=<0..255>', 'lt=<0..255>', 'lx/ly/rx/ry=<-32768..32767>', or 'none'; a
# comma-separated sequence taps each in turn (released between). A button name after '~'
# pulses it: the diagnostic controller presses it on every other poll of the title (about once
# per frame in a race) instead of holding it, so an edge-triggered button acts on the first
# frame the title accepts it; '~y@<n>' starts the pulse on race frame n (the n-th session
# update after the race starts; it waits outside a race) instead of at once. A plain step
# with hold 0 pulses until the next input; a checked step or a comma sequence releases it
# after the hold, as any press. Actions are checked before the launch. These change no input:
#   'wait'             only waits (keeps a held throttle);
#   'phase:<name>'     starts a measured phase ('phase:end' closes it) for summary.json phases;
#   'select:<label>'   moves the menu highlight to the OCR'd label, for up to <seconds>;
#   'defines:<NAME=VALUE ...>'  writes shader_mod_defines.txt in the shader_mod_path, which the
#                      runtime applies with hot reload (--shader-dev): live knobs at once, other
#                      defines through a recompile. The file is restored after the run.
# 'key=<name>' holds a keyboard key (a keybind name such as D, Control or Space) through the
# keyboard driver's bindings and its steering and pedal model (docs/analysis/keyboard-controls.md);
# 'key=Control+key=D' holds two, and the next action without them releases them.
# [timeout, action, hold, text(, retry)] checks the screen: the action (comma-separated
# presses, or 'wait') is repeated every retry (default RETRY_S) seconds until one of the
# '|'-separated texts appears in a capture taken after it. The run fails (goal_reached false)
# when it does not within the timeout. Some FM4 menus ignore the first press after they open,
# so checked presses are the reliable way; give screens that load a retry longer than the load.
# A '?' before the action skips it when the text already shows (in a capture taken after the
# step starts). A text starting with '=' must be a whole OCR line; '!' checks that none shows.
# A text starting with '@' is a regular expression on the title's screen events instead of OCR
# (FM4_TEST_EVENTS_FILE, fm4-test-events.cpp: 'scene <id> <name>', 'push <name>', 'pop <name>',
# 'countdown placed', 'countdown start <n>'): the step passes when a matching event is written
# after its press, or for 'wait' after the run's last press; '?' skips the action when one was.

# Boot to the main menu. A skips the Microsoft Studios and Dolby splash videos (the main menu
# shows after about 30 s instead of 47), one press per screen read: presses at a fixed rate
# also skip the short title screen and select PLAY CAREER in the main menu. The last step
# presses A on the title with a retry longer than the main menu load.
_TITLE = 'start the game|SAVING CONTENT|FORZA MOTORSPORT'
_TO_MAIN_MENU = [
    # Whole lines: the legal notice before the videos also names Microsoft and Studios.
    [45, 'wait', 0, '=Microsoft|=Studios|DOLBY|' + _TITLE], [0.5, 'a', 0.12],
    [20, 'wait', 0, 'DOLBY|' + _TITLE], [0.5, 'a', 0.12],
    [30, '?a', 0.12, 'PLAY CAREER', 8],
]
SCENARIOS = {
    'boot': [[90, 'none', 0]],
    # Main menu -> Free Play -> Quick Race with the default Abarth 131 on Bernese Alps
    # Festival Circuit. The pre-race menu ignores A until its focus has moved once.
    'quickrace': _TO_MAIN_MENU + [
        [4, 'right', 0.12], [1.2, 'right', 0.12], [1.2, 'right', 0.12],
        [2, 'a', 0.12], [6, 'a', 0.12], [8, 'a', 0.12], [5, 'a', 0.12], [6, 'a', 0.12],
        [6, 'a', 0.12], [6, 'a', 0.12], [5, 'a', 0.12], [40, 'a', 0.12],
        [5, 'down', 0.12], [1.5, 'up', 0.12], [1.5, 'a', 0.12], [8, 'rt=255', 0],
    ],
}
# Title screen to the main menu, then into the career home menu (PLAY CAREER). The career menu
# hides after a few idle seconds; the first press shows it again, and Up never wraps.
_TO_CAREER = _TO_MAIN_MENU + [
    [20, 'select:PLAY CAREER', 0],
    [2, 'a', 0.12], [40, 'up', 0.12, 'MY PROFILE'],
]
# Career home menu > GO RACE > WORLD TOUR event screen. Both menus hide after a few idle
# seconds and the first press only shows them again; Up on their top item does nothing else,
# so 'up,a' activates the top item whether the menu shows or not.
_TO_WORLD_TOUR = [
    [30, 'up,a', 0.12, 'WORLD TOUR|EVENT LIST', 10], [60, 'up,a', 0.12, 'DRIVER LEVEL BONUS', 15],
]
SCENARIOS.update({
    # Quick Race, 10 s of driving, then 30 s measured (the race phase that the gates use
    # instead of the quickrace scenario), then pause > PHOTO MODE, take a photo and save it as
    # a new file (the harness answers the name prompt with its default, "Forza"). Use
    # --profile copy.
    'photo': SCENARIOS['quickrace'] + [
        [10, 'wait', 0], [1, 'phase:race', 0], [30, 'wait', 0], [12, 'start', 0.15, 'PHOTO MODE'],
        [1, 'phase:end', 0],
        # OCR often reads nothing in photo mode (a bright scene behind small hints), so entering
        # it is checked by the pause menu closing, and the photo is taken with one A and a fixed
        # wait: it renders for up to about 20 s (LOADING... PLEASE WAIT; no captures at 120 Hz),
        # and a repeated A would open the save dialog. The dark save dialog reads reliably.
        [45, 'select:PHOTO MODE', 0], [20, 'a', 0.12, '!PHOTO MODE|RESUME', 8],
        [2, 'phase:photo', 0], [8, 'wait', 0], [0.2, 'a', 0.12], [25, 'wait', 0],
        [60, '?a', 0.12, 'SAVE PHOTO', 20],
        [30, 'a', 0.12, 'SAVING|SAVE SUCCESSFUL', 10], [30, 'wait', 0, 'SAVE SUCCESSFUL'],
        [1, 'phase:end', 0],
    ],
    # The race phase of 'photo' without photo mode, for the step gates of the native port
    # (docs/analysis/d3d-hle.md, section 10): about 90 s shorter.
    'race': SCENARIOS['quickrace'] + [
        [10, 'wait', 0], [1, 'phase:race', 0], [30, 'wait', 0], [1, 'phase:end', 0],
    ],
    # A new process loads the photo saved by 'photo': --profile <photo run>\user-data. Career >
    # MY PROFILE > MEDIA > MY PHOTOS > LOAD shows it full screen. OCR reads its watermark only
    # sometimes, so the check is that no menu, loading text or error follows (and final.png).
    'photo_view': _TO_CAREER + [
        # The career menu hides again within one capture or two at 120 Hz (sooner than at 60 Hz),
        # too fast for a select: Up shows it (or does nothing on GO RACE, where it opens), then
        # four Downs reach MY PROFILE.
        [20, 'up,down,down,down,down,a', 0.12, 'EDIT PLAYERCARD', 20],
        [30, 'select:MEDIA', 0], [15, 'a', 0.12, 'MY PHOTOS'], [30, 'a', 0.12, 'VIEW PHOTOS', 10],
        [15, 'a', 0.12, 'FILE OPTIONS'], [20, 'a', 0.12, '!FILE OPTIONS', 8], [5, 'wait', 0],
        [20, 'wait', 0, '!VIEW PHOTOS|FILE OPTIONS|FAILED|LOADING'], [5, 'wait', 0],
    ],
    # Career > GO RACE > WORLD TOUR: the profile's next event (Tsukuba Short Circuit with the
    # Ford Ka for the BuildRoot profile), 40 s of driving, QUIT RACE, 30 s of its replay, then
    # CONTINUE back to the event. Use --profile copy: quitting changes no progress, but the
    # BuildRoot profile stays untouched either way.
    'career': _TO_CAREER + _TO_WORLD_TOUR + [
        [20, 'a', 0.12, 'START RACE', 10], [20, 'select:START RACE', 0],
        # A skips the intro; a later A may already start the race from the pre-race menu.
        [150, 'a', 0.12, 'STARTING GRID|LAPS', 15], [40, '?down,up,a', 0.12, 'LAPS', 10],
        [1, 'rt=255', 0],
        [1, 'phase:race', 0], [40, 'wait', 0], [12, 'start', 0.15, 'QUIT RACE'],
        [1, 'phase:end', 0],
        # The pause menu sways with the camera (more at 120 Hz), so its highlight can be judged
        # only in some captures: its selects (also PHOTO MODE above) get 45 s.
        [45, 'select:QUIT RACE', 0], [10, 'a', 0.12, 'ARE YOU SURE'],
        # The post-race menu starts the race replay by itself when left idle (sooner at 120 Hz),
        # so it gets fixed presses as soon as it shows: CONTINUE is highlighted, VIEW REPLAY two
        # below. B shows the replay's HUD (REWIND, CAMERA), a second B its menu again.
        [30, 'a', 0.12, 'VIEW REPLAY', 10], [0.2, 'down,down,a', 0.12],
        [5, 'phase:replay', 0], [30, 'wait', 0], [1, 'phase:end', 0],
        [20, 'b', 0.12, 'REWIND|CAMERA', 10], [20, 'b', 0.12, 'CONTINUE'], [0.2, 'a', 0.12],
        [60, 'wait', 0, 'START RACE'],
    ],
})
# 30 s measured on the main menu (phase menu; the main menu renders the selected car in 3D), then
# the race of 'race': the menus' frame rate, host presents and power next to the race's
# (docs/analysis/d3d-hle-n4.md, section 17.22).
_MENU_PHASE = [[5, 'wait', 0], [1, 'phase:menu', 0], [30, 'wait', 0], [1, 'phase:end', 0]]
SCENARIOS['menu_race'] = _TO_MAIN_MENU + _MENU_PHASE + SCENARIOS['race'][len(_TO_MAIN_MENU):]
SCENARIOS.update({
    # The career race to its rewards; needs the AI driver (--env FM4_GAME_CMDLINE=engageaionstart,
    # the matrix.py default): the Ford Ka wins the 3 laps of Tsukuba Short Circuit in about 3
    # minutes. CONTINUE on the post-race menu at once (it starts the replay when left idle), then
    # A every 6 s through YOU PLACED (Total), the driver and affinity level rewards (a gift car:
    # A keeps the default) up to EVENT COMPLETE, which follows the save. Use --profile copy.
    'career_finish': SCENARIOS['career'][:SCENARIOS['career'].index([1, 'rt=255', 0]) + 1] + [
        [1, 'phase:race', 0], [40, 'wait', 0], [1, 'phase:end', 0],
        [300, 'wait', 0, 'YOU PLACED|RACE RESULTS'], [0.5, 'a', 0.12],
        [20, 'wait', 0, 'Total'], [180, 'a', 0.12, 'EVENT COMPLETE', 6], [10, 'wait', 0],
    ],
    # A new process on the profile of career_finish (--profile <run>\user-data): WORLD TOUR shows
    # the next event (the TopGear challenge) with the driver at level 2 and the new credits. About
    # 2 s after the event's car screen the title opens a CHALLENGE EVENT box over it, which takes A
    # only once its CONTINUE shows (about 12 s later); behind it the event screen shows the level
    # again. The box is waited for and closed whether the level was read on the car screen first
    # or not (an earlier version failed when it was: native-default.md 2.4).
    'career_progress': _TO_CAREER + [
        [30, 'up,a', 0.12, 'WORLD TOUR|EVENT LIST', 10],
        [90, 'up,a', 0.12, 'LVL2|LVL 2|LVL 1|CHALLENGE EVENT', 15], [10, 'wait', 0, 'CHALLENGE EVENT'],
        [40, 'a', 0.12, '!CHALLENGE EVENT', 8],
        [10, 'wait', 0, 'LVL2|LVL 2'], [5, 'wait', 0, '!8,610 CR'],
    ],
})
SCENARIOS.update({
    # Downloadable content: Quick Race with a car of the Porsche Expansion Pack (Porsche is not in
    # the base game; its first car is the 1970 914/6) on the Speedway of Test Track Benchmark (the
    # Launch Track Pack's PhysicsTrack environment), then 20 s of racing. Needs both packs
    # imported into the BuildRoot profile (Build.ps1 -Content); use --profile copy. The
    # manufacturer and environment carousels are alphabetical and do not wrap. RB moves about
    # four entries, so it stops on one of the names just before the target and Right or Left
    # finishes; retries leave a capture (2 s) between presses.
    'dlc': _TO_MAIN_MENU + [
        [4, 'right', 0.12], [1.2, 'right', 0.12], [1.2, 'right', 0.12], [2, 'a', 0.12],
        [10, 'a', 0.12, 'AVAILABLE', 6],
        [60, 'rb', 0.12, '=Pagani|=Peugeot|=Plymouth|=Pontiac|=Porsche', 3],
        [15, '?right', 0.12, '=Porsche', 2.5],
        # Car select (its footer has TOGGLE), then the paint and the assists screens.
        [15, 'a', 0.12, 'TOGGLE', 4], [5, 'wait', 0, 'Porsche'], [15, 'a', 0.12, '!TOGGLE', 4],
        [6, 'wait', 0], [20, 'a', 0.12, 'GAME DIFFICULTY', 6], [3, 'wait', 0],
        [20, 'a', 0.12, '!GAME DIFFICULTY', 6], [20, 'wait', 0, 'TRACKS'],
        [50, 'rb', 0.12, '=Test Track Benchmark|=TopGear|=Tsukuba Circuit|=Twin Ring Motegi', 3],
        [15, '?left', 0.12, '=Test Track Benchmark', 2.5],
        # Track select (LAPS, the first layout is Speedway), then the race loads.
        [15, 'a', 0.12, 'LAPS', 4], [5, 'wait', 0, 'Speedway'], [3, 'wait', 0],
        [20, 'a', 0.12, 'STAGE|CLASS', 8],
        [150, 'a', 0.12, 'STARTING GRID|LAPS', 15], [40, '?down,up,a', 0.12, 'LAPS', 10],
        [1, 'rt=255', 0], [1, 'phase:race', 0], [20, 'wait', 0], [1, 'phase:end', 0],
    ],
})
SCENARIOS.update({
    # A frozen frame for image parity (docs/e2e/frozen-frames.md): Quick Race; Y (rewind)
    # pulsed from race frame 720 (12 s at 60 fps; '~y@720') until the rewind shows and for 6 s
    # more, which rewinds to the oldest frame the rewind keeps (REWIND FURTHER greys out
    # there); then 20 s of the frozen frame (phase freeze), which the title keeps rendering
    # under the rewind hint bar. The rewind keeps a window of about the last 10 s and takes Y
    # only after about 10 s of race (a held Y does not start it). The first press lands on race
    # frame 720 (session updates) in every run, but the race advances by wall time unless
    # --env FM4_FIXED_FRAME_TIME=1, and does not repeat exactly even then, so the frozen moment
    # still differs between runs (docs/e2e/frozen-frames.md, "Baseline variance").
    'freeze': SCENARIOS['quickrace'][:-1] + [
        [0.5, '~y@720', 0], [90, 'wait', 0, 'REWINDING|REWIND FURTHER|RESUME FROM HERE', 1],
        [6, 'none', 0], [6, 'wait', 0],
        [1, 'phase:freeze', 0], [20, 'wait', 0], [1, 'phase:end', 0],
    ],
})
# The fast route (--fast-route): the same races, reached through the title's screen events
# (FM4_TEST_EVENTS_FILE, '@' checks) instead of fixed delays. Every screen of the Quick Race
# menus creates its UI scene as it takes the A, so each A waits only until the screen before
# has settled and is repeated when the next scene does not follow.
# Boot: A until the title scene exists (A skips the Microsoft Studios part of the splash video;
# the Turn 10 part and the legal notice before it ignore A, so the boot is not shorter), then A
# on the title once it has faded in, until the main menu's render scenario is pushed. An A that
# lands as the title scene is created (up to 0.06 s before it) can open the main menu by itself,
# about 0.9 s later and so during the wait: '?' then skips the title's A, which would otherwise
# go to PLAY CAREER (25 October 2026, six runs).
_FAST_TO_MAIN_MENU = [
    [60, 'a', 0.12, '@scene \\d+ 120_P_PRE_SPLASH', 2.5],
    [2.5, 'wait', 0], [20, '?a', 0.12, '@push UI_Homespace', 3],
]
_FAST_QUICKRACE_MENUS = [
    # The main menu: FREE PLAY is three Rights from PLAY CAREER; the select checks it.
    [30, 'wait', 0, 'PLAY CAREER'], [1, 'right,right,right', 0.12], [20, 'select:FREE PLAY', 0],
    [15, 'a', 0.12, 'QUICK RACE', 6],
    # QUICK RACE, then the defaults of manufacturer, car, paint color, difficulty, location
    # and track; the track's A is GO RACE (the race event controller, scene 95).
    [1, 'wait', 0], [15, 'a', 0.12, '@scene \\d+ 115_C_BUY_MFRSELECT', 3],
    [1.5, 'wait', 0], [15, 'a', 0.12, '@scene \\d+ 108_C_BUY_CARSELECT', 3],
    [1.5, 'wait', 0], [15, 'a', 0.12, '@scene \\d+ 114_C_BUY_BUYCAR_COLOR', 3],
    [1.5, 'wait', 0], [15, 'a', 0.12, '@scene \\d+ 838_I_PRO_OPTIONS_SET_DIFFICULTY', 3],
    [1.5, 'wait', 0], [15, 'a', 0.12, '@scene \\d+ 122_F_TRK_LOCSELECT', 3],
    [1.5, 'wait', 0], [15, 'a', 0.12, '@scene \\d+ 123_F_TRK_TRACKSELECT', 3],
    [1.5, 'wait', 0], [15, 'a', 0.12, '@scene \\d+ 17_DYNAMIC_LOADING_FREEPLAY', 3],
    # Loading, then the pre-race menu (its scene; it slides in for about 2 s). It ignores A
    # until its focus has moved once; START RACE creates the HUD at once and places the cars
    # for the countdown about 1 s later.
    [90, 'wait', 0, '@scene \\d+ 4_C_INI_PRERACE'], [3, 'wait', 0],
    [30, 'down,up,a', 0.12, '@scene \\d+ 947_HUD|countdown placed', 5],
]
# The race phase starts 9.2 s after the race scenario is pushed (the end of the 3-2-1), where
# the quickrace route's 8 s of throttle and 10 s of driving after START RACE put it (22 October
# 2026).
_FAST_TO_RACE = [
    [0.1, 'rt=255', 0], [60, 'wait', 0, '@push \\S+_Race$'], [8.2, 'wait', 0],
]
# Career: PLAY CAREER is the main menu's default (scene 102_H_CAR_HOME), GO RACE > WORLD TOUR
# (scene 200_WORLD_TOUR, a globe that zooms to the next event for about 10 s), A until the
# event's screen (scene 204_C_RACE_INFO), START RACE (the event's race event controller, scene
# 92), then the pre-race menu as in a Quick Race. The menu route's race phase starts 2 s after
# OCR reads LAPS, 5.1 s after the race scenario is pushed (22 October 2026).
_FAST_CAREER_TO_RACE = [
    [30, 'wait', 0, 'PLAY CAREER'], [1, 'wait', 0], [15, 'a', 0.12, '@scene \\d+ 102_H_CAR_HOME', 4],
    [40, 'up', 0.12, 'MY PROFILE'], [30, 'up,a', 0.12, 'WORLD TOUR|EVENT LIST', 10],
    [60, 'up,a', 0.12, '@scene \\d+ 200_WORLD_TOUR', 10],
    [60, 'a', 0.12, '@scene \\d+ 204_C_RACE_INFO', 3],
    [20, 'wait', 0, 'START RACE'], [20, 'select:START RACE', 0],
    [15, 'a', 0.12, '@scene \\d+ 19_DYNAMIC_LOADING_WORLDTOUR', 5],
    [90, 'wait', 0, '@scene \\d+ 4_C_INI_PRERACE'], [3, 'wait', 0],
    [30, 'down,up,a', 0.12, '@scene \\d+ 947_HUD|countdown placed', 5],
    [0.1, 'rt=255', 0], [60, 'wait', 0, '@push \\S+_Race$'], [4.1, 'wait', 0],
]
FAST_SCENARIOS = {
    'quickrace': _FAST_TO_MAIN_MENU + _FAST_QUICKRACE_MENUS + _FAST_TO_RACE,
    'career': _FAST_TO_MAIN_MENU + _FAST_CAREER_TO_RACE
    + SCENARIOS['career'][SCENARIOS['career'].index([1, 'phase:race', 0]):],
}
FAST_SCENARIOS.update({
    'race': FAST_SCENARIOS['quickrace'] + SCENARIOS['race'][len(SCENARIOS['quickrace']) + 1:],
    'photo': FAST_SCENARIOS['quickrace'] + SCENARIOS['photo'][len(SCENARIOS['quickrace']) + 1:],
    'freeze': _FAST_TO_MAIN_MENU + _FAST_QUICKRACE_MENUS + SCENARIOS['freeze'][len(SCENARIOS['quickrace']) - 1:],
})
FAST_SCENARIOS['menu_race'] = (_FAST_TO_MAIN_MENU + [[30, 'wait', 0, 'PLAY CAREER']] + _MENU_PHASE
                               + FAST_SCENARIOS['race'][len(_FAST_TO_MAIN_MENU):])
RETRY_S = 5
# What the freeze scenario pins unless --look or --arg says otherwise: the title's own picture
# (no mods, no TAA, so the tiles and MSAA of --quality), the occlusion reports (awaited ones
# report a fixed count instead of host query results, which depend on timing), and 2x.
FREEZE_LOOK = 'original,taa=0'
FREEZE_ARGS = {'rtv_occlusion_debug': 'fake', 'query_occlusion_fake_sample_count': '1000',
               'rtv_occlusion_awaited_scale': '-1', 'rtv_occlusion_awaited_limit': '0',
               'draw_resolution_scale_x': '2', 'draw_resolution_scale_y': '2'}

# Steps after a goal (--steps <name>): hold the throttle and press Back every 6 s, cycling
# the race cameras (cockpit, hood, bumper, chase near, chase far, back to cockpit).
_CAMERA = [[4, 'rt=255+back', 0.15], [0.2, 'rt=255', 0]] + [[6, 'rt=255+back', 0.15], [0.2, 'rt=255', 0]] * 5
STEP_PRESETS = {'camera': _CAMERA}
# A Quick Race whose measured race phase cycles the race cameras as the camera preset does: the
# edges of every camera at a logical size (docs/analysis/d3d-hle-n5.md 18.24).
SCENARIOS['camera_race'] = (SCENARIOS['quickrace'] + [[10, 'wait', 0], [1, 'phase:race', 0]]
                            + _CAMERA + [[2, 'wait', 0], [1, 'phase:end', 0]])
FAST_SCENARIOS['camera_race'] = (FAST_SCENARIOS['quickrace']
                                 + SCENARIOS['camera_race'][len(SCENARIOS['quickrace']) + 1:])
# The race check (docs/e2e/README.md, "Race check"): the scenarios (and --goal goals) that drive
# into a race reach their goal only when the title pushed a race render scenario ('push
# <cars>_Race', at the end of the 3-2-1). The menu route presses through the Quick Race menus
# with fixed delays, so without the check a run stopped at a prompt (INSERT DISC 2 on a
# BuildRoot without Disc 2, 25 October 2026) passed. A run whose timeline ends before the push
# waits up to RACE_WAIT_S for it (a cold shader cache slows the countdown down).
RACE_SCENARIOS = {'quickrace', 'race', 'photo', 'menu_race', 'camera_race', 'freeze', 'career',
                  'career_finish', 'dlc'}
RACE_EVENT = re.compile(r'^push \S+_Race$')
RACE_WAIT_S = 60
# Screens that explain a run stopped short (scene events): the title's Disc 2 prompt, and the
# first-run screens of a new profile (the gamma adjustment, then the loading of the intro race),
# which no timeline drives.
DISC_PROMPT_SCENE = re.compile(r'^scene \d+ (\S*DISC\S*)$')
FIRST_RUN_SCENE = re.compile(r'^scene \d+ (830_GAMMA_ADJUST|1_C_INI_IE_LOADING)$')


def race_needed(options):
    """Whether the run's goal includes a race (the race check)."""
    if options.goal:
        return GOALS[options.goal].get('race', False)
    return not options.steps and options.scenario in RACE_SCENARIOS


def race_started(events, started):
    """Seconds from the launch to the first race push in the screen events, or None."""
    return next((round(t - started, 2) for t, text in events if t >= started and RACE_EVENT.match(text)), None)


def race_verdict(goal_reached, failed_step, crashed, needs_race, events, started):
    """(goal_reached, failed_step, race_started_s) after the race check: a run whose goal includes
    a race and that reached its other checks fails without a race push, and a failed run's
    failed_step gets what the screen events say about where it stopped."""
    events = [e for e in events if e[0] >= started]
    race_s = race_started(events, started)
    if goal_reached and needs_race and race_s is None and not crashed:
        if events:
            goal_reached = False
            failed_step = 'no race: the title never pushed a race render scenario (no "push *_Race" screen event)'
        else:
            # A binary from before the screen events (fm4-test-events.cpp) writes none.
            print('Warning: no screen events, so the race check could not run (a binary without '
                  'FM4_TEST_EVENTS_FILE support?)', flush=True)
    if goal_reached is False:
        reason = stop_reason(events)
        if reason:
            failed_step = f'{failed_step}; {reason}' if failed_step else reason
    return goal_reached, failed_step, race_s


def stop_reason(events):
    """What the screen events say about a run that stopped short of its goal, or None."""
    for _, text in events:
        if match := DISC_PROMPT_SCENE.match(text):
            return (f'the title asks for Disc 2 (scene {match[1]}): Disc 2 is not imported into the '
                    'BuildRoot profile. When local.json names no Disc 2, generate again with -Disc2 '
                    '<the extracted Disc 2 folder> (Build.ps1 -Action generate); the next e2e run '
                    'imports it before the launch')
    for _, text in events:
        if match := FIRST_RUN_SCENE.match(text):
            return (f'a new profile (scene {match[1]}): its first start plays the intro video, the '
                    'gamma adjustment and the intro race, which the timelines do not drive. Play once '
                    'with Build.ps1 -Action run (or the GUI launcher) up to the main menu first')
    return None


# Exact FM4 clear color: where it covers >= 1% of a race capture as a peak among the neutral grays
# around it, geometry such as the road surface was not drawn (seen on the RTV path).
CLEAR_GRAY = bytes([102, 102, 102])


def is_clear_gray(rgb, width, height):
    """The capture shows the clear color where geometry is missing: (102, 102, 102) covers at
    least 1% of it more than the mean of the neutral grays 97-101 and 103-107 do. Asphalt
    smoothed by TAA hits 102 by chance in 1% of a capture and more, but as part of an even spread
    of neutral grays; a surface left undrawn is a peak of that one value. Runs of a color count
    once per pixel; misaligned matches are negligible."""
    clear = rgb.count(CLEAR_GRAY)
    if clear < 0.01 * width * height:
        return False
    around = [rgb.count(bytes([v, v, v])) for v in (*range(97, 102), *range(103, 108))]
    return clear - sum(around) / len(around) >= 0.01 * width * height


def washed_out_bands(rgb, width, height, bands=4):
    """The horizontal bands (quarters of the height, from the top) of a capture that are washed
    out to white: a luma mean of 240 or more with a standard deviation under 20, on a grid of
    about 320x180 samples. A race capture has detail in every band (sky, scenery, road, the
    HUD); a build that lost the scene showed its upper half white (d3d-hle-n4.md 17.15)."""
    step_x, step_y = max(1, width // 320), max(1, height // 180)
    result = []
    for band in range(bands):
        values = []
        for y in range(band * height // bands, (band + 1) * height // bands, step_y):
            row = rgb[y * width * 3:(y + 1) * width * 3]
            values += [(299 * r + 587 * g + 114 * b) // 1000
                       for r, g, b in zip(row[0::3 * step_x], row[1::3 * step_x], row[2::3 * step_x])]
        mean = sum(values) / len(values)
        if mean >= 240 and sum((v - mean) ** 2 for v in values) / len(values) < 20 ** 2:
            result.append(band)
    return result

# Goals for Jev-driven navigation. The route is guidance for the judgment, not a script.
GOALS = {
    'quickrace': {
        'goal': 'Start a Quick Race from Free Play with the default car and the default track, '
                'then be driving in that race.',
        'route': ['Startup logos and videos: wait.', 'Title screen: start the game.',
                  'Main menu: open FREE PLAY.', 'Free Play menu: open QUICK RACE.',
                  'Manufacturer, car, paint color, location and track screens: accept the '
                  'highlighted default each time.',
                  'Assists screen: confirm the highlighted setting without changing anything.',
                  'Track screen: GO RACE.', 'Loading and pre-race camera: continue.',
                  'Pre-race menu: START RACE.'],
        # Screens this goal never needs; code backs out of them.
        'off_route': {'autovista', 'community_or_marketplace', 'career_menu', 'pause_menu'},
        # The race check (RACE_SCENARIOS) applies: Jev's race judgment needs a race push too.
        'race': True,
    },
}

SCREENS = {
    'logo_or_video': 'Startup logos, legal or audio logos, or an intro video',
    'title': 'Title screen asking to press A or START to start the game',
    'main_menu': 'Menu listing PLAY CAREER, COMMUNITY, AUTOVISTA, FREE PLAY and MARKETPLACE',
    'free_play_menu': 'Menu listing QUICK RACE, SPLIT SCREEN and HOT LAP',
    'career_menu': 'Career menus such as GO RACE, WORLD TOUR, EVENT LIST or season screens',
    'autovista': 'Autovista car showroom or its tutorial: move around the car, look around, '
                 'open the doors or hood, listen to the host',
    'community_or_marketplace': 'Community, storefront, marketplace, Xbox LIVE or online screens',
    'manufacturer_or_car_select': 'Choosing a manufacturer, a car model or a paint color',
    'assists_menu': 'Assists and difficulty list: braking, steering, traction control, shifting',
    'environment_or_track_select': 'Choosing a location or a track layout with laps and length',
    'loading': 'Loading screen showing the chosen car or track while the race loads',
    'pre_race_intro': 'Pre-race camera showing the car or track with no menu',
    'pre_race_menu': 'Pre-race menu listing START RACE, STARTING GRID, TUNE SETUP and QUIT RACE',
    'racing': 'Race in progress: HUD with place, lap counter and race timer, no menu',
    'pause_menu': 'Pause menu during a race, such as RESUME, RESTART and QUIT',
    'results': 'Race results or rewards',
    'dialog': 'Message box such as FEATURE UNAVAILABLE or a saving notice with OK',
    'other': None,
}
KEEP = 'the highlighted default'
NOTHING = 'nothing'
TEXTLESS_PROCEED = {'pre_race_intro', 'title'}
FOOTER_Y = 640


def write_png(path, width, height, rgb, scale):
    w, h = width // scale, height // scale
    rows = bytearray()
    for y in range(h):
        rows.append(0)
        start = y * scale * width * 3
        line = rgb[start:start + width * 3]
        for x in range(w):
            rows += line[x * scale * 3:x * scale * 3 + 3]
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(rows), 6)) + chunk(b'IEND', b'')
    path.write_bytes(png)


def read_ppm(ppm):
    parts = ppm.read_bytes().split(b'\n', 3)
    width, height = map(int, parts[1].split())
    return width, height, parts[3]


def ppm_to_png(ppm, png, scale=2):
    write_png(png, *read_ppm(ppm), scale)


def capture_stats(ppm):
    """Luminance statistics of a capture: brightness, black and saturated share, histogram."""
    _, _, rgb = read_ppm(ppm)
    # Rec. 601 luma in integer form; every other pixel is enough for these statistics.
    histogram = [0] * 256
    for r, g, b in zip(rgb[0::6], rgb[1::6], rgb[2::6]):
        histogram[(299 * r + 587 * g + 114 * b) // 1000] += 1
    pixels = sum(histogram) or 1
    return {
        'luma_mean': round(sum(i * n for i, n in enumerate(histogram)) / pixels, 2),
        'black_pct': round(100 * sum(histogram[:8]) / pixels, 2),
        'saturated_pct': round(100 * sum(histogram[248:]) / pixels, 2),
        'luma_hist16': [round(100 * sum(histogram[i:i + 16]) / pixels, 2) for i in range(0, 256, 16)],
    }


def percentile(values, fraction):
    values = sorted(values)
    if not values:
        return None
    return values[min(len(values) - 1, int(round(fraction * (len(values) - 1))))]


# When the harness last pressed anything (time.time()): event checks of 'wait' steps take the
# events written since then.
LAST_PRESS = [0.0]


def set_input(path, buttons=0, lt=0, rt=0, lx=0, ly=0, rx=0, ry=0, pulse=0, pulse_from=None,
              keys=()):
    if buttons or lt or rt or lx or ly or rx or ry or pulse or keys:
        LAST_PRESS[0] = time.time()
    tmp = path.with_suffix('.tmp')
    # The pulsed buttons and their race frame (FM4TestInput) are written only when set.
    extra = (f' {pulse}' + ('' if pulse_from is None else f' {pulse_from}')) if pulse else ''
    # Held keyboard keys, on a line of their own, go to the keyboard driver (FM4TestInput).
    key_line = f'keys {" ".join(keys)}\n' if keys else ''
    tmp.write_text(f'{buttons} {lt} {rt} {lx} {ly} {rx} {ry}{extra}\n{key_line}')
    # The game may be reading the file at this moment; Windows then refuses the replace.
    for attempt in range(50):
        try:
            os.replace(tmp, path)
            return
        except PermissionError:
            time.sleep(0.01)
    os.replace(tmp, path)


def parse_action(action):
    """The input state of one press ('+'-joined parts); ValueError names a bad part."""
    state = dict(buttons=0, lt=0, rt=0, lx=0, ly=0, rx=0, ry=0, pulse=0, pulse_from=None, keys=())
    if action == 'none':
        return state
    for part in action.split('+'):
        if part.startswith('key='):
            name = part[4:]
            if not name.isalnum():
                raise ValueError(f'bad key {part!r} in {action!r} (key=<keybind name>)')
            state['keys'] += (name,)
        elif part.startswith('~'):
            button, at, start = part[1:].partition('@')
            if button not in BUTTONS or (at and not start.isdigit()):
                raise ValueError(f'bad pulse {part!r} in {action!r} (~<button> or ~<button>@<frame>)')
            state['pulse'] |= BUTTONS[button]
            if at:
                state['pulse_from'] = int(start)
        elif '=' in part:
            key, value = part.split('=', 1)
            if key not in ('lt', 'rt', 'lx', 'ly', 'rx', 'ry'):
                raise ValueError(f'bad axis {part!r} in {action!r}')
            state[key] = int(value)
        elif part in BUTTONS:
            state['buttons'] |= BUTTONS[part]
        else:
            raise ValueError(f'unknown button {part!r} in {action!r}')
    return state


def check_actions(steps):
    """Parses every input action of the steps, so a bad one fails before the launch."""
    for index, (wait, action, hold, *text) in enumerate(steps):
        action = action[1:] if text and action.startswith('?') else action
        if action == 'wait' or action.startswith(('phase:', 'select:', 'defines:')):
            continue
        for press in action.split(','):
            try:
                parse_action(press)
            except ValueError as error:
                raise ValueError(f'step {index}: {error}') from None


# The centred part of a capture that holds the 1280x720 HUD, as fractions of its width and
# height: less than 1 when the scene renders at a logical size other than 16:9
# (native_logical_width and _height, docs/analysis/d3d-hle-n5.md 18.22). main sets it.
OCR_CROP = (1.0, 1.0)


def logical_crop(args):
    """OCR_CROP of the run's cvars."""
    width, height = 1280, 720
    for item in args:
        name, _, value = item.lstrip('-').partition('=')
        if name == 'native_logical_width' and value.isdigit():
            width = int(value)
        elif name == 'native_logical_height' and value.isdigit():
            height = int(value)
    return (1280 / max(width, 1280), 720 / max(height, 720))


class Ocr:
    """Persistent Windows OCR process (ocr-server.ps1)."""
    def __init__(self):
        self.crop = OCR_CROP
        self.process = subprocess.Popen(
            ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
             str(HERE / 'ocr-server.ps1')], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            text=True, encoding='utf-8', errors='replace')
        if 'ready' not in self.process.stdout.readline():
            raise RuntimeError('OCR server failed to start')

    def read(self, ppm):
        # Windows OCR sometimes reads nothing in an image at 2x that it reads at 1x (a frozen
        # rewind frame with its hint bar every time), so an empty read is repeated at 1x.
        # Boxes are in 720p pixels at either scale.
        return self.read_scaled(ppm, 2) or self.read_scaled(ppm, 1)

    def read_scaled(self, ppm, scale):
        crop = f'|{self.crop[0]:.6f},{self.crop[1]:.6f}' if self.crop != (1.0, 1.0) else ''
        self.process.stdin.write(f'{ppm}|{scale}{crop}\n')
        self.process.stdin.flush()
        # OCR text may contain control characters such as tabs.
        result = json.loads(self.process.stdout.readline(), strict=False)
        if 'error' in result:
            raise RuntimeError('OCR: ' + result['error'])
        return result['lines']

    def close(self):
        self.process.stdin.close()
        self.process.wait(10)


def annotate(lines):
    """Marks the highlighted line: FM4 draws its selection in red near the item."""
    items = [l for l in lines if l['text'].strip()]
    # The red FORZA MOTORSPORT logo heads several menus; selected items may have red text.
    candidates = [l for l in items if 'FORZA' not in l['text'].upper() and l['y'] < FOOTER_Y]
    score = lambda l: max(l['red'], l['red_text'])
    ranked = sorted(candidates, key=lambda l: -score(l))
    if ranked and score(ranked[0]) >= 15 and (len(ranked) == 1 or score(ranked[0]) >= 1.3 * score(ranked[1])):
        ranked[0]['selected'] = True
    return sorted(items, key=lambda l: (l['y'] // 20, l['x']))


def complete_ppm(path):
    """Whether a capture has been written completely: OCR of a partly written one sees
    a black image and no text."""
    try:
        with open(path, 'rb') as file:
            head = file.read(32).split(b'\n', 3)
        width, height = map(int, head[1].split())
        return path.stat().st_size >= len(b'\n'.join(head[:3])) + 1 + width * height * 3
    except (OSError, ValueError, IndexError):
        return False


def newest_capture(capture, after=0.0):
    frames = [p for p in capture.glob('frame-*.ppm') if p.stat().st_mtime > after]
    frames += [p for p in capture.glob('live-latest.ppm') if p.stat().st_mtime > after]
    frames = [p for p in frames if complete_ppm(p)]
    return max(frames, key=lambda p: p.stat().st_mtime) if frames else None


def jev_step(goal, lines, history):
    """One Jev request with independent judgments over the same screen state."""
    import jev
    texts = []
    for line in lines:
        if line['text'] not in texts:
            texts.append(line['text'])
    screen_text = [dict(text=l['text'], **({'highlighted': True} if l.get('selected') else {}),
                        **({'button_hint': True} if l['y'] >= FOOTER_Y else {})) for l in lines]
    state = {'goal': goal['goal'], 'route': goal['route'], 'screen_text': screen_text,
             'recent_steps': [{k: h[k] for k in ('screen', 'target', 'action')} for h in history[-6:]]}
    target_criteria = {text: None for text in texts[:250]}
    target_criteria[KEEP] = ('Keep the currently highlighted choice: the route says to accept the '
                             'default here, or the needed item is highlighted but its label is '
                             'not in `screen_text`')
    target_criteria[NOTHING] = ('Nothing to activate now: the screen is loading, a logo, video or '
                                'camera intro is playing, or no text is readable')
    questions = {
        'screen': {'type': 'choice', 'criteria': SCREENS,
                   'instructions': 'Which game screen is shown by `screen_text` (text read from the '
                                   'screen; empty when nothing is readable)? Use `recent_steps` for '
                                   'context about the previous screens.'},
        'target': {'type': 'choice', 'criteria': target_criteria,
                   'instructions': 'Following `route`, which item must the player activate on this '
                                   'screen to progress toward `goal`? Pick the own label of that item even '
                                   'when a different item is highlighted now. Entries marked '
                                   'button_hint are controller hints such as "A SELECT" or "GO RACE".'},
        'racing': {'type': 'noul',
                   'instructions': "Is the player's race in progress right now? The in-race HUD shows "
                                   "a lap counter such as '1/2', a race timer such as '00:28.686', a "
                                   "place such as '4/12' or the words PLACE and LAPS, with no menu list. "
                                   "Text is read by OCR and may be partial or garbled."},
    }
    answers = jev.system_one(state, questions)['answers']
    return answers


def choose_action(goal, answers, lines, history):
    """Code owns execution: turn the judgments into one controller action."""
    screen, target = answers['screen']['choice'], answers['target']['choice']
    signature = '|'.join(l['text'] for l in lines)
    same = 0
    for h in reversed(history):
        if h['signature'] != signature or h['action'] == 'wait':
            break
        same += 1
    if same >= 5:
        # Inputs no longer change this screen: assume a dead end off the route.
        return 'b', 'escape'
    if screen in goal['off_route']:
        return 'b', 'off route'
    last = history[-2:]
    if len(last) == 2 and all(h['action'] == 'a' and h['signature'] == signature for h in last):
        # Some FM4 menus ignore A until focus has moved once since they opened.
        return 'down,up,a', 'nudge'
    if screen == 'racing':
        return 'wait', 'racing'
    if target == NOTHING:
        if screen in TEXTLESS_PROCEED:
            return 'a', 'proceed'
        stalled = sum(1 for h in history[-4:] if h['action'] == 'wait' and h['signature'] == signature)
        return ('a', 'stalled') if stalled >= 3 and signature else ('wait', 'wait')
    if target == KEEP:
        return 'a', 'keep'
    line = next(l for l in lines if l['text'] == target)
    if line['y'] >= FOOTER_Y:
        return ('b' if re.search('BACK|CANCEL', target.upper()) else 'a'), 'hint'
    selected = next((l for l in lines if l.get('selected')), None)
    if not selected or selected is line:
        return 'a', 'activate'
    dx = (line['x'] + line['w'] / 2) - (selected['x'] + selected['w'] / 2)
    dy = (line['y'] + line['h'] / 2) - (selected['y'] + selected['h'] / 2)
    if abs(dx) > abs(dy):
        return ('right' if dx > 0 else 'left'), 'move'
    return ('down' if dy > 0 else 'up'), 'move'


def navigate(goal_name, process, input_file, capture, out, deadline, started):
    goal = GOALS[goal_name]
    ocr = Ocr()
    history, log = [], open(out / 'nav.jsonl', 'w', encoding='utf-8')
    seen_after = 0.0
    race_votes = 0
    try:
        while time.time() < deadline:
            if process.poll() is not None:
                raise RuntimeError(f'exited with {process.returncode}')
            frame = newest_capture(capture, seen_after)
            if not frame:
                time.sleep(0.2)
                continue
            lines = annotate(ocr.read(frame))
            answers = jev_step(goal, lines, history)
            screen, target = answers['screen'], answers['target']
            racing = answers['racing']['noul']
            # OCR reads only fragments of the race HUD, so also accept two consecutive
            # confident race-screen judgments backed by a moderate race probability.
            race_screen = screen['choice'] == 'racing' and screen['confidence'] >= 0.8 and racing >= 0.4
            race_votes = race_votes + 1 if race_screen else 0
            if racing >= 0.8 or race_votes >= 2:
                log.write(json.dumps({'t': round(time.time() - started, 1), 'screen': screen['choice'],
                                      'racing': racing, 'action': 'goal reached'}) + '\n')
                return True
            action, reason = choose_action(goal, answers, lines, history)
            pressed = time.time()
            for button in action.split(','):
                if button != 'wait':
                    set_input(input_file, **parse_action(button))
                    time.sleep(0.12)
                    set_input(input_file)
                    time.sleep(0.5)
            signature = '|'.join(l['text'] for l in lines)
            history.append({'screen': screen['choice'], 'target': target['choice'], 'action': action,
                            'signature': signature})
            log.write(json.dumps({
                't': round(pressed - started, 1), 'frame': frame.name,
                'screen': screen['choice'], 'screen_conf': screen['confidence'],
                'target': target['choice'], 'target_conf': target['confidence'],
                'selected': next((l['text'] for l in lines if l.get('selected')), None),
                'racing': round(racing, 3),
                'action': action, 'reason': reason,
                'text': [l['text'] for l in lines][:24]}) + '\n')
            log.flush()
            # Judge only frames captured after the input had time to take effect.
            seen_after = time.time() + 0.3
        return False
    finally:
        log.close()
        ocr.close()


def press(input_file, sequence, hold):
    """Taps each comma-separated action: press, hold, release, short pause."""
    for action in sequence.split(','):
        set_input(input_file, **parse_action(action))
        time.sleep(hold or 0.12)
        set_input(input_file)
        time.sleep(0.5)


def read_events(path):
    """The title's screen events (FM4_TEST_EVENTS_FILE): [(unix seconds, '<kind> <detail>')]."""
    events = []
    try:
        text = path.read_text(encoding='utf-8', errors='replace')
    except OSError:
        return events
    for line in text.splitlines():
        parts = line.split(' ', 2)
        if len(parts) == 3 and parts[0].isdigit():
            events.append((int(parts[0]) / 1000, parts[2]))
    return events


class Screen:
    """OCR of the newest capture for checked steps; starts the OCR server on first use.
    steps.jsonl records what each check saw. Event checks read the title's events file."""
    def __init__(self, capture, process, log_path, events_path=None):
        self.capture, self.process, self.ocr = capture, process, None
        self.log_path, self.log_file = log_path, None
        self.events_path = events_path
        self.frame = None

    def events(self, after):
        """The screen events written after `after` (time.time()); raises when the game exits."""
        if self.process.poll() is not None:
            raise RuntimeError(f'exited with {self.process.returncode}')
        return [e for e in read_events(self.events_path) if e[0] > after] if self.events_path else []

    def log(self, **record):
        if not self.log_file:
            self.log_file = open(self.log_path, 'w', encoding='utf-8')
        self.log_file.write(json.dumps({'frame': self.frame, **record}) + '\n')
        self.log_file.flush()

    def read(self, after, until):
        """(annotated lines, capture time) of the newest capture written after `after`, waiting
        for one until `until` ((None, after) then); raises when the game exits."""
        while time.time() < until:
            if self.process.poll() is not None:
                raise RuntimeError(f'exited with {self.process.returncode}')
            frame = newest_capture(self.capture, after)
            if frame:
                if not self.ocr:
                    self.ocr = Ocr()
                try:
                    self.frame = frame.name
                    return annotate(self.ocr.read(frame)), frame.stat().st_mtime
                except (RuntimeError, ValueError, OSError) as error:
                    # Usually a capture still being written; a dead server is started again.
                    self.log(ocr_error=repr(error)[:200])
                    if self.ocr.process.poll() is not None:
                        self.ocr = None
            time.sleep(0.2)
        return None, after

    def close(self):
        if self.ocr:
            self.ocr.close()
        if self.log_file:
            self.log_file.close()


def shows(lines, text):
    """Whether any OCR line contains one of the '|'-separated texts (case-insensitive); a text
    starting with '=' must be the whole line. A leading '!' inverts: none of them shows."""
    if text.startswith('!'):
        return not shows(lines, text[1:])
    wanted = [t.strip().upper() for t in text.split('|')]
    return any(w[1:] == l['text'].strip().upper() if w.startswith('=') else w in l['text'].upper()
               for l in lines for w in wanted)


def event_step(screen, input_file, timeout, action, hold, pattern, retry):
    """Repeats the action every `retry` seconds until a screen event matching the regular
    expression is written after it ('wait': after the run's last press)."""
    deadline = time.time() + timeout
    def seen(after, until):
        while True:
            match = next((e for e in screen.events(after) if re.search(pattern, e[1])), None)
            if match or time.time() >= until:
                return match
            time.sleep(0.05)
    if action.startswith('?'):
        action = action[1:]
        match = seen(LAST_PRESS[0], 0)
        if match:
            screen.log(event=pattern, seen=match[1], skipped=action)
            return True
    while time.time() < deadline:
        after = LAST_PRESS[0] if action == 'wait' else time.time()
        if action != 'wait':
            press(input_file, action, hold)
        match = seen(after, deadline if action == 'wait' else min(deadline, after + retry))
        if match:
            screen.log(event=pattern, seen=match[1], after_s=round(match[0] - after, 2))
            return True
    screen.log(event=pattern, seen=None)
    return False


def checked_step(screen, input_file, timeout, action, hold, text, retry=RETRY_S):
    """Repeats the action every `retry` seconds until text shows in a capture taken after it."""
    if text.startswith('@'):
        return event_step(screen, input_file, timeout, action, hold, text[1:], retry)
    deadline = time.time() + timeout
    if action.startswith('?'):
        action = action[1:]
        lines, _ = screen.read(time.time(), deadline)
        if lines and shows(lines, text):
            screen.log(check=text, seen=True, skipped=action)
            return True
    while time.time() < deadline:
        if action != 'wait':
            press(input_file, action, hold)
        after = time.time() + 0.3
        retry_until = deadline if action == 'wait' else min(deadline, after + retry)
        while time.time() < retry_until:
            lines, after = screen.read(after, retry_until)
            if lines is not None:
                screen.log(check=text, seen=shows(lines, text), text=[l['text'] for l in lines][:12])
                if shows(lines, text):
                    return True
    return False


def menu_peers(lines, target):
    """Entries of the menu holding target, the ones that may be highlighted, and how to score
    their highlight. In a column the highlighted entry is a dark band (bg about 65 against 110
    and more) with red lines above and below it; the line between two entries belongs to both,
    and a swaying menu can put a line next to a light entry, so only dark entries count (a dark
    submenu header has no red lines). In a row (horizontal menu) tiles have red frames."""
    lines = [l for l in lines if l['y'] < FOOTER_Y]
    column = [l for l in lines if abs(l['x'] - target['x']) <= 40]
    if len(column) >= 2:
        median_bg = sorted(l['bg'] for l in column)[len(column) // 2]
        dark = [l for l in column if l['bg'] <= 0.8 * median_bg]
        return column, dark, lambda l: min(l.get('red_above', 0), l.get('red_below', 0))
    row = [l for l in lines if abs(l['y'] - target['y']) <= 12]
    return row, row, lambda l: max(l['red'], l['red_text'])


def select_label(screen, input_file, timeout, label):
    """Moves the highlight to label. Highlight detection as in annotate(), but only among the
    target's menu, so red elsewhere (a logo, the player's row) does not count. It acts only
    when two clear readings in a row (captures of a swaying menu in between give none) find
    the same entry highlighted. Not for menus that hide when idle: a press would only show
    them again."""
    deadline = time.time() + timeout
    after = time.time()
    previous = None
    while time.time() < deadline:
        lines, after = screen.read(after, deadline)
        if not lines:
            continue
        target = next((l for l in lines if l['text'].strip().upper() == label.upper()), None) or \
            next((l for l in lines if label.upper() in l['text'].upper() and l['y'] < FOOTER_Y), None)
        if not target:
            screen.log(select=label, target=None, text=[l['text'] for l in lines][:12])
            continue
        peers, candidates, score = menu_peers(lines, target)
        ranked = sorted(candidates, key=lambda l: -score(l))
        screen.log(select=label, target=[target['x'], target['y']],
                   peers=[[l['text'], l['x'], l['y'], l['bg'], score(l), l in candidates] for l in peers])
        if not ranked or score(ranked[0]) < 15 or \
                (len(ranked) > 1 and score(ranked[0]) < 1.3 * score(ranked[1])):
            continue
        current = ranked[0]
        # While a menu slides in, its red lines can sit next to another entry for a capture.
        steady, previous = previous == current['text'], current['text']
        if not steady:
            continue
        if current is target:
            return True
        axis, (back, forward) = ('x', ('left', 'right')) if abs(current['y'] - target['y']) <= 12 \
            else ('y', ('up', 'down'))
        low, high = sorted((current[axis], target[axis]))
        steps = 1 + sum(1 for l in peers if low < l[axis] < high)
        press(input_file, ','.join([forward if target[axis] > current[axis] else back] * steps), 0.12)
        after = time.time() + 0.3
        previous = None
    return False


def perf_lines(path):
    """Windows written to the perf trace so far: marks where a phase starts."""
    try:
        return path.read_bytes().count(b'\n')
    except OSError:
        return 0


def frames_by_phase(frames, marks, end):
    """The captures written during each phase ({name: [frame stems]}), by their file times
    against the times of the phase marks; a phase ends at the next mark (or at end)."""
    bounds = marks + [('end', end)]
    result = {}
    for (name, start), (_, stop) in zip(bounds, bounds[1:]):
        if name != 'end':
            result.setdefault(name, []).extend(
                f.stem for f in frames if start <= f.stat().st_mtime < stop)
    return result


def prepare_profile(source, build_root, out):
    """Copies a user-data root to <out>/user-data, so saves and progress of the run stay out of
    the BuildRoot profile. The installed content packages (0000000000000000, gigabytes, never
    written by the game) are linked from the BuildRoot instead of copied."""
    content = '0000000000000000'
    target = out / 'user-data'
    if target.exists():
        # rmdir removes only the link and refuses a real non-empty directory, so the BuildRoot
        # packages are never deleted through it.
        if os.path.lexists(target / content):
            os.rmdir(target / content)
        shutil.rmtree(target)
    shutil.copytree(source, target, ignore=lambda directory, names:
                    [content] if Path(directory) == Path(source) else [])
    packages = build_root / 'user-data' / content
    if packages.is_dir():
        subprocess.run(['cmd', '/c', 'mklink', '/J', str(target / content), str(packages)],
                       check=True, capture_output=True)
    return target


def summarize(perf, measure):
    """Per-run statistics over the steady part of the perf trace."""
    steady = perf[-measure:] if measure else (perf[len(perf) // 3:] if len(perf) > 6 else perf)
    if not steady:
        return {}
    per_s = lambda p, value: value / p['window_ms'] * 1000
    median = lambda values: round(statistics.median(values), 2)
    frames = [f for p in steady for f in p.get('frame_ms', [])]
    minutes = sum(p['window_ms'] for p in steady) / 60000
    result = {
        'fps_median': round(statistics.median([p['fps'] for p in steady]), 1),
        'fps_min': round(min(p['fps'] for p in steady), 1),
        'wait_ms_per_s_median': {k: round(statistics.median([per_s(p, p['wait_ms'][k]) for p in steady]), 1)
                                 for k in steady[0]['wait_ms']},
        # Wall time of the command processor thread not spent waiting.
        'cp_busy_ms_per_s_median': round(statistics.median(
            [per_s(p, p['window_ms'] - sum(p['wait_ms'].values())) for p in steady]), 1),
        'readbacks_per_s_median': round(statistics.median([p['readbacks'] for p in steady]), 1),
        'draws_per_s_median': round(statistics.median([p['draws'] for p in steady]), 1),
    }
    if frames:
        result.update({
            # Frames over time across all measured windows; steadier than the median of
            # per-second FPS, which jumps between 20, 24 and 30 as frames snap to vblanks.
            'fps_mean': round(1000 * len(frames) / sum(frames), 2),
            'frame_ms_p50': round(percentile(frames, 0.50), 2),
            'frame_ms_p95': round(percentile(frames, 0.95), 2),
            'frame_ms_p99': round(percentile(frames, 0.99), 2),
            'hitch_50ms_per_min': round(sum(f > 50 for f in frames) / minutes, 2),
            'hitch_100ms_per_min': round(sum(f > 100 for f in frames) / minutes, 2),
        })
    if 'cpu_ms' in steady[0]:
        # Command-processor wall time per work kind, inclusive (draw contains draw_*).
        result['cpu_ms_per_s_median'] = {k: median([per_s(p, p['cpu_ms'][k]) for p in steady])
                                         for k in steady[0]['cpu_ms']}
    if 'occlusion_samples' in steady[0]:
        # Mean passing samples per occlusion interval as reported to the game.
        result['occlusion_samples_median'] = {k: median([p['occlusion_samples'][k] for p in steady])
                                              for k in steady[0]['occlusion_samples']}
    if 'submit_thread' in steady[0]:
        # Submit thread wall time (busy = record + execute + list reset + callbacks; callbacks
        # are the presenter's signal and the present with d3d12_submit_thread_async_swap) and
        # the command processor's drain of it before the swap submission (flush_wait, inside
        # cpu_ms submit). Older traces lack the callback fields.
        submit = lambda p, k: p['submit_thread'].get(k, 0)
        result['submit_thread_ms_per_s_median'] = {
            k: median([per_s(p, submit(p, k)) for p in steady])
            for k in ('busy_ms', 'record_ms', 'execute_ms', 'callback_ms', 'flush_wait_ms')}
        result['submit_thread_counts_per_s_median'] = {
            k: median([per_s(p, submit(p, k)) for p in steady])
            for k in ('jobs', 'callbacks', 'flushes', 'flushes_blocked', 'flush_jobs')}
        result['submit_flush_wait_ms_per_frame_median'] = median(
            [submit(p, 'flush_wait_ms') / max(1, len(p.get('frame_ms', []))) for p in steady])
    gpu = [p['gpu'] for p in steady if p.get('gpu', {}).get('frames')]
    if gpu:
        # GPU time per frame from timestamps: busy (submissions), resolves, first-to-last span.
        result['gpu_frame_ms_median'] = median([g['frame_ms'] for g in gpu])
        result['gpu_resolve_ms_median'] = median([g['resolve_ms'] for g in gpu])
        result['gpu_span_ms_median'] = median([g['span_ms'] for g in gpu])
        if 'passes_ms' in gpu[0]:
            # Per pass (docs/analysis/d3d-hle-n4.md 17.24): the post pass groups, the swap's
            # passes and the rest of the frame (the scene).
            result['gpu_passes_ms_median'] = {k: median([g['passes_ms'][k] for g in gpu])
                                              for k in gpu[0]['passes_ms']}
            result['gpu_passes_ms_median'].update(
                swap=median([g['swap_ms'] for g in gpu]), scene=median([g['scene_ms'] for g in gpu]))
    frames_in = lambda p: max(1, len(p.get('frame_ms', [])))
    # Over all measured frames: per-window values alternate with the frame count of a window
    # when frames alternate (the half-rate mirror), by about 0.5% in a frozen frame.
    all_frames = max(1, sum(len(p.get('frame_ms', [])) for p in steady))
    result['draws_per_frame_mean'] = round(sum(p['draws'] for p in steady) / all_frames, 2)
    if 'host_presents' in steady[0]:
        # The host's presents (docs/analysis/d3d-hle-n4.md 17.22): of a guest output frame no
        # earlier present showed, of the same frame again, and those the UI thread made, per
        # second over all measured windows; and all presents per guest frame.
        seconds = sum(p['window_ms'] for p in steady) / 1000
        presents = {k: sum(p['host_presents'][k] for p in steady) for k in steady[0]['host_presents']
                    if k != 'paint_gpu_ms'}
        result['host_presents_per_s'] = {k: round(v / seconds, 1) for k, v in presents.items()}
        if 'paint_gpu_ms' in steady[0]['host_presents']:
            # The GPU time of a presenter paint (17.24), a mean per window.
            result['host_paint_gpu_ms_median'] = round(statistics.median(
                [p['host_presents']['paint_gpu_ms'] for p in steady]), 3)
        result['host_presents_per_frame'] = round(
            (presents['new_frames'] + presents['repeats']) / all_frames, 2)
    # Backend-neutral names for the native path (docs/e2e/frozen-frames.md): the thread that
    # turns the guest's commands into host work, the guest thread that records them, and the
    # queue between the two. Each comes only from a trace that has it, except the render
    # thread, which on the PM4 path is the command processor (render_thread_source).
    if 'render_thread_busy_ms' in steady[0]:
        result['render_thread_busy_ms_per_frame_median'] = median(
            [p['render_thread_busy_ms'] / frames_in(p) for p in steady])
        result['render_thread_source'] = 'render_thread'
    elif 'wait_ms' in steady[0]:
        result['render_thread_busy_ms_per_frame_median'] = median(
            [(p['window_ms'] - sum(p['wait_ms'].values())) / frames_in(p) for p in steady])
        result['render_thread_source'] = 'cp'
    if 'guest_render_ms' in steady[0]:
        result['guest_render_thread_ms_per_frame_median'] = median(
            [p['guest_render_ms'] / frames_in(p) for p in steady])
    if 'queue_depth_mean' in steady[0]:
        result['queue_depth_median'] = median([p['queue_depth_mean'] for p in steady])
    if 'queue_depth_max' in steady[0]:
        result['queue_depth_max'] = max(p['queue_depth_max'] for p in steady)
    if 'counts' in steady[0]:
        result['counts_per_s_median'] = {k: median([per_s(p, p['counts'][k]) for p in steady])
                                         for k in steady[0]['counts']}
        result['counts_per_frame_median'] = {k: median([p['counts'][k] / frames_in(p) for p in steady])
                                             for k in steady[0]['counts']}
        result['counts_per_frame_mean'] = {k: round(sum(p['counts'][k] for p in steady) / all_frames, 2)
                                           for k in steady[0]['counts']}
    return result


DRIVING_INPUT = ('rt=', 'lt=', 'lx=', 'ly=')


class ModDefines:
    """The 'defines:' steps: shader_mod_defines.txt in the shader_mod_path, restored to what it
    was (or removed) after the run, so a later run does not start with these values."""

    def __init__(self, directory):
        self.path = Path(directory) / 'shader_mod_defines.txt' if directory else None
        self.original = None
        self.written = False

    def write(self, items):
        if self.path is None:
            raise RuntimeError("a 'defines:' step needs shader_mod_path (a look with mods)")
        if not self.written:
            self.original = self.path.read_bytes() if self.path.is_file() else None
            self.written = True
        text = ''.join(f'{item}\n' for item in items.replace(';', ' ').split())
        temporary = self.path.with_suffix('.tmp')
        temporary.write_text(text)
        temporary.replace(self.path)

    def restore(self):
        if not self.written:
            return
        if self.original is None:
            self.path.unlink(missing_ok=True)
        else:
            self.path.write_bytes(self.original)


def without_driving_input(steps):
    """Steps without throttle, brake and steering: 'rt=255' becomes 'wait', 'rt=255+back'
    becomes 'back'. Menu presses and checks stay."""
    result = []
    for step in steps:
        action = step[1]
        parts = action.split('+')
        kept = [p for p in parts if not p.startswith(DRIVING_INPUT)]
        if ',' not in action and len(kept) != len(parts):
            step = [step[0], '+'.join(kept) or 'wait', *step[2:]]
        result.append(step)
    return result


def run_once(options, out):
    root = Path(options.build_root)
    out.mkdir(parents=True, exist_ok=True)
    capture = out / 'capture'
    capture.mkdir(exist_ok=True)
    events_file = out / 'events.txt'
    for old in list(capture.glob('*')) + [out / 'perf.jsonl', out / 'runtime.log', out / 'nav.jsonl',
                                          out / 'steps.jsonl', events_file]:
        if old.is_file():
            old.unlink()
    state = json.loads((root / 'local.json').read_text())
    input_file = out / 'input.txt'
    set_input(input_file)
    LAST_PRESS[0] = 0.0

    # Same cvars as Build.ps1 -Action run.
    sys.path[:0] = [str(HERE), str(HERE.parent)]
    import cpu_power, pipeline
    args = {k: str(v) for k, v in pipeline.launch_cvars(root, state['disc1'], out / 'runtime.log').items()}
    # Nobody can type into the host keyboard dialog: XAM prompts answer with their defaults.
    args['headless'] = 'true'
    if options.profile:
        source = root / 'user-data' if options.profile == 'copy' else Path(options.profile)
        args['user_data_root'] = str(prepare_profile(source, root, out))
    freeze = options.scenario == 'freeze' and not options.steps
    look = (FREEZE_LOOK if freeze else pipeline.DEFAULT_LOOK) if options.look is None else options.look
    # The upscale ratio of --arg native_scene_input_scale (FSR's sharpening follows it).
    scale_args = dict(item.lstrip('-').split('=', 1) for item in options.arg
                      if item.lstrip('-').startswith(('native_draw_scale=', 'native_scene_input_scale=')))
    look_args = {k: str(v) for k, v in pipeline.look_cvars(
        root, look, options.shader_dev, pipeline.scene_upscale_ratio(scale_args)).items()}
    args.update(look_args)
    args.update({k: str(v) for k, v in pipeline.ao_cvars(root, options.ao, options.shader_dev).items()})
    if freeze:
        # Before the --arg items; a --look given for the run keeps what it sets (its sun knob
        # sets the awaited scale and limit).
        args.update({k: v for k, v in FREEZE_ARGS.items()
                     if options.look is None or k not in look_args})
    # The earlier deferred present skip, so the guest throughput stays comparable with the gates
    # before 24 October 2026 where the host presents slower than the guest renders (about 120
    # presents a second on the main PC at 240 Hz); --arg present_skip_superseded=false runs the
    # players' present path (docs/analysis/d3d-hle-n4.md 17.22).
    args['present_skip_superseded'] = 'true'
    global OCR_CROP
    OCR_CROP = logical_crop(options.arg)
    for item in options.arg:
        key, value = item.lstrip('-').split('=', 1)
        args[key] = value
    # With the skip, the UI thread presents, as in those gates (where the settings overlay kept
    # it): a present on the submit thread blocks it where the host presents slower than the guest
    # renders, and RTV's resolve readbacks then wait (readback_publish 25 to 55 ms/s instead of
    # about 1 on the main PC; docs/analysis/d3d-hle-n4.md 17.22).
    if args.get('present_skip_superseded') == 'true':
        args.setdefault('host_present_from_non_ui_thread', 'false')
    # The render scale the run uses, in its summary and on the console: the freeze scenario pins
    # 2x unless --arg says otherwise, so a run named "1x" without it renders at 2x.
    draw_scale = f"{args.get('draw_resolution_scale_x', '1')}x{args.get('draw_resolution_scale_y', '1')}"
    print(f'Render scale {draw_scale}' + (' (the freeze scenario pins 2x)' if freeze and
          not any(item.lstrip('-').startswith('draw_resolution_scale') for item in options.arg) else ''))
    env = {k: v for k, v in os.environ.items() if not k.startswith(('FM4_', 'REX_'))}
    env.update(pipeline.launch_env())
    env.update(FM4_CAPTURE_DIR=str(capture),
               FM4_TEST_INPUT_FILE=str(input_file), REX_PERF_TRACE_FILE=str(out / 'perf.jsonl'),
               FM4_TEST_EVENTS_FILE=str(events_file))
    # The launcher's race render quality and tiling: one tile when the look has TAA on RTV
    # (--msaa keeps the tiles); --env FM4_TILING_SCENARIO keeps its tiling scenario.
    tiling_given = any(item.startswith('FM4_TILING_SCENARIO=') for item in options.env)
    # --native without a tiling scenario, FM4_ONE_PASS_MSAA or --msaa runs the races in one pass
    # with 2x MSAA, like Build.ps1 -Native (d3d-hle.md 16.17); --env FM4_TILING_SCENARIO=2 keeps
    # one tile without MSAA (-MsaaSamples 0).
    # --msaa-samples, as Build.ps1 -MsaaSamples: 2 or 4 for one pass with that MSAA, 0 for one tile.
    if options.msaa_samples is not None:
        if not options.native or args.get('render_target_path_d3d12') == 'rov':
            raise SystemExit("--msaa-samples is the native path's one-pass MSAA: use it with --native")
        options.env = ({0: ['FM4_TILING_SCENARIO=2'], 2: ['FM4_TILING_SCENARIO=1', 'FM4_ONE_PASS_MSAA=2'],
                        4: ['FM4_TILING_SCENARIO=3', 'FM4_ONE_PASS_MSAA=4']}[options.msaa_samples] +
                       options.env)
        tiling_given = True
    one_pass_default = (options.native and not tiling_given and not options.msaa and
                        args.get('render_target_path_d3d12') != 'rov' and
                        not any(item.startswith('FM4_ONE_PASS_MSAA=') for item in options.env))
    if one_pass_default:
        options.env = ['FM4_TILING_SCENARIO=1', 'FM4_ONE_PASS_MSAA=2'] + options.env
        tiling_given = True
    single_tile = not tiling_given and pipeline.auto_single_tile(
        look, args.get('render_target_path_d3d12') == 'rov', options.msaa)
    if single_tile:
        tiling_env, tiling_cvars = pipeline.single_tile_settings(look)
        env.update(tiling_env)
        args.update({k: v for k, v in tiling_cvars.items() if k not in args})
    # One-pass MSAA is a probe of the native path (d3d-hle.md 16.17): -Rov, the uncorrected
    # reference, never runs it.
    if (args.get('render_target_path_d3d12') == 'rov' and
            any(item.startswith('FM4_ONE_PASS_MSAA=') for item in options.env)):
        raise SystemExit("FM4_ONE_PASS_MSAA is the native path's one-pass MSAA; -Rov cannot run it")
    # -Rov keeps the title's ring index wait as it is, like Build.ps1 -Rov (d3d-hle-n4.md 17.16).
    if args.get('render_target_path_d3d12') == 'rov':
        args['guest_ring_wait_sleep'] = 'false'
    if options.native:
        # One tile only, like Build.ps1 -Native, which runs the PM4 RTV path in tiles; here a
        # tiled run is refused instead, so a --native result is always the native path. The
        # last --env FM4_TILING_SCENARIO wins, as in the environment below.
        if args.get('render_target_path_d3d12') == 'rov':
            raise SystemExit('--native runs on RTV; it cannot be combined with -Rov')
        tiling = [item.split('=', 1)[1] for item in options.env
                  if item.startswith('FM4_TILING_SCENARIO=')]
        # One pass with MSAA (d3d-hle.md 16.17): the 2x scenario made one tile by the
        # title's FM4_ONE_PASS_MSAA=2, with the runtime's private scene depth, or scenario 3
        # made one 4x tile by FM4_ONE_PASS_MSAA=4, with a private depth and colour.
        one_pass = [item.split('=', 1)[1] for item in options.env
                    if item.startswith('FM4_ONE_PASS_MSAA=')]
        one_pass_samples = (one_pass[-1] if one_pass and tiling and
                            (one_pass[-1], tiling[-1]) in (('2', '1'), ('4', '3')) else None)
        if one_pass_samples:
            # Bit 8: the host render targets sized to the tiles the title uses (16.15); bit 16:
            # every 1280-wide race surface private (d3d-hle-n5.md 18.11).
            args.setdefault('native_one_pass_msaa', one_pass_samples)
            args.setdefault('native_rt_surfaces', '26')
        elif not (single_tile if not tiling else tiling[-1] == '2'):
            cause = ('--msaa' if options.msaa else f'--env FM4_TILING_SCENARIO={tiling[-1]}'
                     if tiling else f'the look {look!r} (no TAA)')
            raise SystemExit(f'--native covers one tile, but {cause} renders races in tiles: '
                             'use a look with TAA or --env FM4_TILING_SCENARIO=2')
        # An --arg given for the run keeps its value (host_render_native=false for an A/B).
        for key, value in pipeline.native_cvars(True).items():
            args.setdefault(key, value)
    settings = pipeline.race_settings(options.quality or pipeline.DEFAULT_QUALITY, tiling_given or single_tile)
    if settings:
        env['FM4_RACE_SETTINGS'] = settings
    command = [str(root / options.game_build / 'fm4_rex.exe')] + [f'--{k}={v}' for k, v in args.items()]
    # The look's track lighting knobs (road_gloss, sky).
    track_settings = pipeline.look_track_settings(pipeline.look_knobs(look))
    if track_settings:
        env['FM4_TRACK_SETTINGS'] = track_settings
    # Diagnostic variables, e.g. REX_RESOLVE_PUBLICATION_TRACE=<path>.
    env.update(item.split('=', 1) for item in options.env)
    # The settings overlay (the presenter draws it: never in the captures), at start with --info.
    # Off by default as in the launcher: while it shows, the UI thread presents every frame,
    # which is not the players' present path (docs/analysis/d3d-hle-n4.md 17.22).
    env['FM4_INFO'] = pipeline.info_overlay(args, env, look, options.quality or pipeline.DEFAULT_QUALITY)
    env.setdefault('FM4_INFO_SHOW', '1' if options.info else '0')
    # With --goal, --steps run after the goal is reached (e.g. looking around in the race).
    if options.steps in STEP_PRESETS:
        steps = STEP_PRESETS[options.steps]
    elif options.steps:
        steps = json.loads(options.steps)
    elif options.goal:
        steps = []
    else:
        steps = (FAST_SCENARIOS if options.fast_route else SCENARIOS)[options.scenario]
    # With the title's AI driving the player's car (engageaionstart) the harness sends no
    # driving input, so a race does not depend on whether pad input reaches that car.
    ai_driving = 'engageaionstart' in env.get('FM4_GAME_CMDLINE', '')
    if ai_driving:
        steps = without_driving_input(steps)
    check_actions(steps)

    n0_trace = env.get('FM4_N0_TRACE') and str(root / env['FM4_N0_TRACE'])
    # The title appends to the trace, so this run's lines start at its present size.
    n0_offset = os.path.getsize(n0_trace) if n0_trace and os.path.isfile(n0_trace) else 0
    stderr = open(out / 'stderr.log', 'w', encoding='utf-8', errors='replace')
    started = time.time()
    process = subprocess.Popen(command, cwd=root, env=env, stdout=stderr, stderr=subprocess.STDOUT)
    # The process's CPU time and the CPU package's power each second, the threads' CPU time per phase.
    sampler = cpu_power.Sampler(process, out / 'cpu_power.jsonl',
                                int(options.affinity, 0) if options.affinity else None)
    timeline = []
    crashed = None
    goal_reached = None
    failed_step = None
    phases = []
    phase_times = []
    phase_steady = []
    mod_defines = ModDefines(args.get('shader_mod_path'))
    screen = Screen(capture, process, out / 'steps.jsonl', events_file)
    needs_race = race_needed(options)
    try:
        if options.goal:
            goal_reached = navigate(options.goal, process, input_file, capture, out,
                                    started + options.goal_timeout, started)
            if goal_reached and not ai_driving:
                set_input(input_file, rt=255)
        for index, (wait, action, hold, *text) in enumerate(steps):
            if text:
                done = checked_step(screen, input_file, wait, action, hold, *text)
                timeline.append([round(time.time() - started, 1), action, text[0], done])
            elif action.startswith('select:'):
                done = select_label(screen, input_file, wait, action[7:])
                timeline.append([round(time.time() - started, 1), action, done])
            else:
                deadline = time.time() + wait
                while time.time() < deadline:
                    if process.poll() is not None:
                        raise RuntimeError(f'exited with {process.returncode}')
                    time.sleep(0.1)
                done = True
                if action.startswith('phase:'):
                    phases.append((action[6:], perf_lines(out / 'perf.jsonl')))
                    phase_times.append((action[6:], time.time()))
                    phase_steady.append((action[6:], time.perf_counter() * 1000))
                    sampler.mark(action[6:])
                elif action.startswith('defines:'):
                    mod_defines.write(action[8:])
                elif ',' in action:
                    press(input_file, action, hold)
                elif action != 'wait':
                    set_input(input_file, **parse_action(action))
                    if hold:
                        time.sleep(hold)
                        set_input(input_file)
                timeline.append([round(time.time() - started, 1), action])
            if not done:
                failed_step = f'step {index}: {action}' + (f' -> {text[0]!r}' if text else '') + ' timed out'
                break
            if text or action.startswith('select:'):
                goal_reached = True if goal_reached is None else goal_reached
        if failed_step:
            goal_reached = False
        deadline = time.time() + (0 if failed_step else options.tail)
        while time.time() < deadline and process.poll() is None:
            time.sleep(0.1)
        if process.poll() is not None:
            raise RuntimeError(f'exited with {process.returncode}')
        # The race check: a timeline that ended before the race push waits for it, unless the
        # title shows its Disc 2 prompt.
        deadline = time.time() + RACE_WAIT_S
        while goal_reached and needs_race and time.time() < deadline:
            events = read_events(events_file)
            if race_started(events, started) is not None or any(DISC_PROMPT_SCENE.match(e) for _, e in events):
                break
            if process.poll() is not None:
                raise RuntimeError(f'exited with {process.returncode}')
            time.sleep(0.2)
    except RuntimeError as error:
        crashed = str(error)
    except Exception as error:
        crashed = f'harness error: {error!r}'
    finally:
        screen.close()
        mod_defines.restore()
        sampler.stop()
        if process.poll() is None:
            process.kill()
            process.wait()
        stderr.close()

    goal_reached, failed_step, race_s = race_verdict(goal_reached, failed_step, crashed, needs_race,
                                                     read_events(events_file), started)

    frames = sorted(capture.glob('frame-*.ppm'))
    phase_frames = frames_by_phase(frames, phase_times, time.time())
    phase_steady_ms = steady_by_phase(phase_steady, time.perf_counter() * 1000)
    final_stats = None
    if frames:
        # Keep the final capture under a stable name for checkpoints.
        ppm_to_png(frames[-1], out / 'final.png')
        final_stats = capture_stats(frames[-1])
    clear_gray_frames = []
    # The race's captures with a band washed out to white: {frame stem: [bands]}.
    washed_out_frames = {}
    race_frames = set(phase_frames.get('race', []))
    for frame in frames:
        width, height, rgb = read_ppm(frame)
        if is_clear_gray(rgb, width, height):
            clear_gray_frames.append(frame.stem)
        if frame.stem in race_frames and (bands := washed_out_bands(rgb, width, height)):
            washed_out_frames[frame.stem] = bands
        ppm_to_png(frame, frame.with_suffix('.png'))
        if not options.keep_ppm and not freeze:
            frame.unlink()

    perf = [json.loads(line) for line in (out / 'perf.jsonl').read_text().splitlines()] \
        if (out / 'perf.jsonl').is_file() else []
    log_text = (out / 'runtime.log').read_text(encoding='utf-8', errors='replace') \
        if (out / 'runtime.log').is_file() else ''
    errors = {}
    for line in log_text.splitlines():
        if re.search(r'\b(E|error|ERROR)\b|\[error\]|\[E\]', line):
            key = re.sub(r'^\S+\s+\S+\s+', '', line)[:90]
            key = re.sub(r'0x[0-9A-Fa-f]+|\d+', '#', key)
            errors[key] = errors.get(key, 0) + 1
    # Each phase covers the perf windows from its mark to the next mark.
    phase_windows = windows_by_phase(phases, len(perf))
    phase_stats = {name: summarize(perf[start:end], end - start) for name, (start, end) in phase_windows.items()}
    exe = root / options.game_build / 'fm4_rex.exe'
    # The source the binary was built from, when its build result is next to it (a snapshot)
    # or it is the BuildRoot's game-build.
    build_result = root / options.game_build / 'build-result.json'
    if not build_result.is_file() and Path(options.game_build) == Path('game-build'):
        build_result = root / 'build-result.json'
    source = json.loads(build_result.read_text()).get('source') if build_result.is_file() else None
    summary = {
        'crashed': crashed, 'goal': options.goal, 'scenario': None if options.steps else options.scenario,
        'goal_reached': goal_reached, 'failed_step': failed_step, 'args': options.arg, 'profile': options.profile,
        # The race check: seconds from the launch to the first race push (None: no push or no check).
        'race_check': needs_race, 'race_started_s': race_s,
        'look': look, 'draw_scale': draw_scale,
        # The binary the run used: a baseline variance or a parity verdict holds for one binary.
        'binary': {'path': str(exe), 'sha256': pipeline.digest(exe),
                   'mtime': time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(exe.stat().st_mtime)),
                   'source': source},
        'seconds': round(time.time() - started, 1), 'timeline': timeline,
        'route': 'fast' if options.fast_route and not options.steps and not options.goal else 'menus',
        # The title's screen events, in seconds from the launch as the timeline.
        'events': [[round(t - started, 2), text] for t, text in read_events(events_file)],
        'frames_captured': len(frames), 'perf_windows': len(perf),
        **summarize(perf, options.measure),
        'phases': phase_stats,
        'phase_windows': phase_windows,
        'phase_frames': phase_frames,
        'phase_steady_ms': phase_steady_ms,
        'n0_trace': n0_trace, 'n0_trace_offset': n0_offset,
        'final_capture': final_stats,
        'clear_gray_frames': clear_gray_frames,
        'washed_out_frames': washed_out_frames,
        'cpu_power': sampler.summary(),
        'affinity': {'mask': options.affinity, 'sets': sampler.affinity_sets} if options.affinity else None,
        'errors': errors,
        'top_errors': sorted(errors.items(), key=lambda kv: -kv[1])[:10],
    }
    guest_render_from_n0(summary, options.measure)
    (out / 'summary.json').write_text(json.dumps(summary, indent=2))
    return summary


PHASE_KEYS = ('fps_mean', 'fps_median', 'frame_ms_p50', 'frame_ms_p95', 'frame_ms_p99', 'hitch_50ms_per_min',
              'cp_busy_ms_per_s_median', 'draws_per_s_median', 'gpu_frame_ms_median',
              'submit_flush_wait_ms_per_frame_median', 'draws_per_frame_mean',
              'render_thread_busy_ms_per_frame_median', 'guest_render_thread_ms_per_frame_median',
              'queue_depth_median', 'queue_depth_max')
PHASE_NESTED_KEYS = ('submit_thread_ms_per_s_median', 'submit_thread_counts_per_s_median',
                     'counts_per_frame_median', 'counts_per_frame_mean')


def aggregate(runs):
    """Combines repeated runs: median of medians, minimum of minimums, union of errors."""
    ok = [r for r in runs if 'fps_median' in r]
    median = lambda values: round(statistics.median(values), 2) if values else None
    pick = lambda key: [r[key] for r in ok if r.get(key) is not None]
    nested = lambda key: {k: median([r[key][k] for r in ok if key in r]) for k in ok[0].get(key, {})} if ok else {}
    errors = {}
    for run in runs:
        for key, count in run.get('errors', {}).items():
            errors[key] = errors.get(key, 0) + count
    summary = {
        'repeat': len(runs),
        'runs': [dict(crashed=r['crashed'], goal_reached=r['goal_reached'], fps_median=r.get('fps_median'),
                      race_started_s=r.get('race_started_s')) for r in runs],
        'crashed': next((r['crashed'] for r in runs if r['crashed']), None),
        'goal': runs[0]['goal'], 'scenario': runs[0].get('scenario'), 'route': runs[0].get('route'),
        'args': runs[0]['args'],
        'look': runs[0].get('look'), 'draw_scale': runs[0].get('draw_scale'),
        'binary': runs[0].get('binary'),
        'failed_steps': [r.get('failed_step') for r in runs],
        'goal_reached': None if runs[0]['goal_reached'] is None else all(r['goal_reached'] for r in runs),
        'fps_median': median(pick('fps_median')),
        'fps_min': min(pick('fps_min'), default=None),
        'cp_busy_ms_per_s_median': median(pick('cp_busy_ms_per_s_median')),
        'readbacks_per_s_median': median(pick('readbacks_per_s_median')),
        'draws_per_s_median': median(pick('draws_per_s_median')),
        'wait_ms_per_s_median': nested('wait_ms_per_s_median'),
        'counts_per_s_median': nested('counts_per_s_median'),
        'counts_per_frame_median': nested('counts_per_frame_median'),
        'counts_per_frame_mean': nested('counts_per_frame_mean'),
        'cpu_ms_per_s_median': nested('cpu_ms_per_s_median'),
        'occlusion_samples_median': nested('occlusion_samples_median'),
        'submit_thread_ms_per_s_median': nested('submit_thread_ms_per_s_median'),
        'submit_thread_counts_per_s_median': nested('submit_thread_counts_per_s_median'),
        'final_captures': [r['final_capture'] for r in runs],
        'cpu_power': [r.get('cpu_power') for r in runs],
        'clear_gray_frames': sum(len(r.get('clear_gray_frames', [])) for r in runs),
        'washed_out_frames': sum(len(r.get('washed_out_frames', {})) for r in runs),
        'errors': errors,
        'top_errors': sorted(errors.items(), key=lambda kv: -kv[1])[:10],
    }
    for key in ('fps_mean', 'frame_ms_p50', 'frame_ms_p95', 'frame_ms_p99', 'hitch_50ms_per_min', 'hitch_100ms_per_min',
                'gpu_frame_ms_median', 'gpu_resolve_ms_median', 'gpu_span_ms_median',
                'submit_flush_wait_ms_per_frame_median', 'draws_per_frame_mean',
                'render_thread_busy_ms_per_frame_median', 'guest_render_thread_ms_per_frame_median',
                'queue_depth_median', 'queue_depth_max'):
        summary[key] = median(pick(key))
    sources = pick('render_thread_source')
    if sources:
        summary['render_thread_source'] = sources[0]
    phase_names = list(dict.fromkeys(name for r in ok for name in (r.get('phases') or {})))
    summary['phases'] = {name: {key: median([r['phases'][name][key] for r in ok
                                             if r['phases'].get(name, {}).get(key) is not None])
                                for key in PHASE_KEYS}
                         for name in phase_names}
    for name, phase in summary['phases'].items():
        runs_in = [r['phases'][name] for r in ok if name in (r.get('phases') or {})]
        for key in PHASE_NESTED_KEYS:
            values = [p[key] for p in runs_in if key in p]
            if values:
                phase[key] = {k: median([v[k] for v in values]) for k in values[0]}
    return summary


def windows_by_phase(marks, total):
    """{phase: [first, end)} perf windows of each phase from its marks (name, windows written
    when it started); a phase ends at the next mark or at total."""
    bounds = marks + [('end', total)]
    return {name: [start, end] for (name, start), (_, end) in zip(bounds, bounds[1:])
            if name != 'end' and end > start}


def steady_by_phase(marks, end):
    """{phase: [start, end]} on the steady clock (QueryPerformanceCounter, ms) from the phase
    marks; the clock of the title's and the runtime's traces (n0.jsonl, REX_PM4_STATS)."""
    bounds = marks + [('end', end)]
    return {name: [round(start, 1), round(stop, 1)]
            for (name, start), (_, stop) in zip(bounds, bounds[1:]) if name != 'end'}


def n0_render_ms(lines):
    """Median over n0.jsonl seconds of render.ms / render.calls: the guest render thread's
    time per call of its frame function that presented a frame (calls without a Swap left out)."""
    values = [l['render']['ms'] / l['render']['calls'] for l in lines
              if (l.get('render') or {}).get('calls')]
    return round(statistics.median(values), 2) if values else None


def guest_render_from_n0(summary, measure=0):
    """guest_render_thread_ms_per_frame_median of the run and its phases from the D3D layer
    trace (FM4_N0_TRACE, docs/e2e/traces.md) when the perf trace has none. Only the run's own
    lines count (from n0_trace_offset; the title appends). A line covers the second before its
    steady_ms; a phase takes the lines inside it, the run the steady part as summarize does."""
    path = summary.get('n0_trace')
    if not path or not Path(path).is_file():
        return
    with open(path, 'rb') as file:
        file.seek(summary.get('n0_trace_offset', 0))
        lines = [json.loads(l) for l in file.read().decode().splitlines() if l.strip()]
    def put(target, value):
        key = 'guest_render_thread_ms_per_frame_median'
        if value is not None and (key not in target or target.get('guest_render_thread_source') == 'n0_trace'):
            target[key] = value
            target['guest_render_thread_source'] = 'n0_trace'
    put(summary, n0_render_ms(lines[-measure:] if measure else
                              (lines[len(lines) // 3:] if len(lines) > 6 else lines)))
    for name, (start, end) in (summary.get('phase_steady_ms') or {}).items():
        if name in (summary.get('phases') or {}):
            put(summary['phases'][name], n0_render_ms(
                [l for l in lines if start <= l.get('steady_ms', 0) - 1000 and l.get('steady_ms', 0) <= end]))


def resummarize(out, measure):
    """Refreshes perf statistics of finished runs, e.g. after new summary fields were added.
    Phases are recomputed from phase_windows; runs written before phase_windows keep their
    stored phases (their timeline counts from the launch, the perf windows' t from the perf
    trace's start, a few seconds later)."""
    def refresh(directory):
        summary = json.loads((directory / 'summary.json').read_text())
        perf = [json.loads(line) for line in (directory / 'perf.jsonl').read_text().splitlines()]
        summary.update(summarize(perf, measure))
        windows = summary.get('phase_windows')
        if windows is not None:
            summary['phases'] = {name: summarize(perf[start:end], end - start)
                                 for name, (start, end) in windows.items()}
        guest_render_from_n0(summary, measure)
        (directory / 'summary.json').write_text(json.dumps(summary, indent=2))
        return summary
    runs = sorted(out.glob('run[0-9]*'), key=lambda d: int(d.name[3:]))
    if not runs:
        return refresh(out)
    summary = aggregate([refresh(run) for run in runs])
    (out / 'summary.json').write_text(json.dumps(summary, indent=2))
    return summary


# A run writes its logs, perf trace, captures (2560x1440 PPMs for the freeze scenario) and a copy
# of the profile; on a full drive every write fails and a gate's cells fail one after another.
MIN_FREE_GB = 15.0


def check_free_space(out, min_free_gb):
    """Exits with a message when the drive that holds out has less than min_free_gb free."""
    probe = out
    while not probe.exists() and probe != probe.parent:
        probe = probe.parent
    free_gb = shutil.disk_usage(probe).free / 2**30
    if free_gb < min_free_gb:
        raise SystemExit(f'Only {free_gb:.1f} GB free on the drive of {out} (at least {min_free_gb:g} GB '
                         'needed for a run): free space first, or pass --min-free-gb.')


BUILD_PROCESSES = ('cl.exe', 'link.exe', 'lld-link.exe', 'clang-cl.exe', 'clang.exe', 'clang++.exe',
                   'ninja.exe', 'cmake.exe', 'msbuild.exe')


def warn_if_building():
    """Warns when a build runs beside the run: its CPU load moves the perf numbers."""
    try:
        listing = subprocess.run(['tasklist', '/fo', 'csv', '/nh'], capture_output=True, text=True,
                                 timeout=20).stdout.lower()
    except (OSError, subprocess.SubprocessError):
        return
    running = sorted({name for name in BUILD_PROCESSES if f'"{name}"' in listing})
    if running:
        print(f'Warning: a build runs beside this run ({", ".join(running)}); its CPU load moves '
              'the perf numbers.', flush=True)


def check_content(build_root):
    """Exits with a message when a content package that an import marker in the BuildRoot
    profile names (<user-data>/<package>.import-complete) is missing or empty in
    <user-data>/0000000000000000/4D530910/00000002: the races would run without it."""
    user_data = build_root / 'user-data'
    installed = user_data / '0000000000000000' / '4D530910' / '00000002'
    missing = []
    for marker in sorted(user_data.glob('*.import-complete')):
        package = installed / marker.name[:-len('.import-complete')]
        if not package.is_dir() or not any(f.is_file() for f in package.rglob('*')):
            missing.append(package.name)
    if missing:
        raise SystemExit(f'{len(missing)} imported content package(s) missing or empty in {installed} '
                         f'({", ".join(missing[:4])}{", ..." if len(missing) > 4 else ""}): restore '
                         'them or import them again (Build.ps1 -Content) first.')


def package_imported(user_data, package):
    """Whether the game's importer (fm4-content-installer.cpp) would skip the package: its marker
    <user-data>/<name>.import-complete records the package's size, and the package's directory
    exists under <user-data>/0000000000000000/4D530910/00000002."""
    marker = user_data / f'{package.name}.import-complete'
    try:
        size = int(marker.read_text().split()[0])
    except (OSError, ValueError, IndexError):
        return False
    installed = user_data / '0000000000000000' / '4D530910' / '00000002' / package.name
    return size == package.stat().st_size and installed.is_dir()


def import_content(build_root, game_build='game-build'):
    """Imports Disc 2 and the -Content packages that local.json names into the BuildRoot profile
    before the first launch, as Build.ps1 -Action run and the GUI launcher do at theirs: the game's
    import mode (FM4_CONTENT_MANIFEST with FM4_CONTENT_INSTALL_ONLY) installs them and exits before
    the title starts. Runs only when a package is not imported yet; a --profile copy links the
    imported packages from the BuildRoot profile. Exits with a message when the import fails."""
    sys.path[:0] = [str(HERE), str(HERE.parent)]
    import pipeline
    state = json.loads((build_root / 'local.json').read_text())
    if not state.get('disc2'):
        print('Note: local.json names no Disc 2, so races on its tracks stop at the INSERT DISC 2 '
              'prompt (the race check fails them): generate again with -Disc2 to import it.', flush=True)
    try:
        packages = pipeline.validate_content(state.get('disc2'), state.get('content'))
    except (OSError, ValueError) as error:
        raise SystemExit(f'Content packages of local.json: {error}')
    user_data = build_root / 'user-data'
    pending = [package for package in packages if not package_imported(user_data, package)]
    if not pending:
        return
    exe = build_root / game_build / 'fm4_rex.exe'
    log = build_root / 'logs' / 'e2e-import.log'
    log.parent.mkdir(parents=True, exist_ok=True)
    print(f'Importing {len(pending)} content package(s) into {user_data} before the first launch '
          f'(log: {log})', flush=True)
    manifest = pipeline.write_content_manifest(build_root, packages)
    args = {k: str(v) for k, v in pipeline.launch_cvars(build_root, state['disc1'], log).items()}
    args['headless'] = 'true'
    env = {k: v for k, v in os.environ.items() if not k.startswith(('FM4_', 'REX_'))}
    env.update(FM4_CONTENT_MANIFEST=str(manifest), FM4_CONTENT_INSTALL_ONLY='1')
    with open(build_root / 'logs' / 'e2e-import-stderr.log', 'w', encoding='utf-8', errors='replace') as stderr:
        code = subprocess.call([str(exe)] + [f'--{k}={v}' for k, v in args.items()], cwd=build_root,
                               env=env, stdout=stderr, stderr=subprocess.STDOUT)
    missing = [package.name for package in pending if not package_imported(user_data, package)]
    if code or missing:
        raise SystemExit(f'The content import failed (exit code {code}; not imported: '
                         f'{", ".join(missing) or "none"}). See {build_root / "logs" / "e2e-import-stderr.log"}, '
                         'or import with Build.ps1 -Action run once, then run e2e again.')
    print(f'Imported {len(pending)} content package(s).', flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-root', required=True, help='BuildRoot used by Build.ps1')
    parser.add_argument('--game-build', default='game-build',
                        help='build directory under BuildRoot, e.g. game-build-thinlto (pipeline.py --flavor)')
    parser.add_argument('--out', required=True, help='result directory, outside the repository')
    parser.add_argument('--scenario', default='boot', choices=sorted(SCENARIOS),
                        help='fixed timeline (docs/e2e/README.md, "Gameplay scenarios")')
    parser.add_argument('--profile', help="copy this user-data root to <out>/user-data for the run "
                                          "('copy': the BuildRoot one); the BuildRoot profile stays as it is")
    parser.add_argument('--fast-route', action='store_true',
                        help='reach the race of the scenario through the title\'s screen events instead of '
                             'fixed delays (docs/e2e/README.md, "Fast route"): ' + ', '.join(sorted(FAST_SCENARIOS)))
    parser.add_argument('--goal', choices=sorted(GOALS), help='navigate with Jev instead of a timeline')
    parser.add_argument('--goal-timeout', type=float, default=360)
    parser.add_argument('--env', action='append', default=[], help='extra NAME=value for the game process')
    parser.add_argument('--steps', help='JSON list of steps or a preset name (camera), overrides --scenario')
    parser.add_argument('--arg', action='append', default=[], help='extra/override --cvar=value')
    parser.add_argument('--look', help='look like Build.ps1 -Look (default: the launcher\'s, aces), '
                                       'e.g. "original" or "aces,sharpen=0.6"')
    parser.add_argument('--quality', help='race render quality like Build.ps1 -Quality '
                                          '(default: the launcher\'s), e.g. xbox')
    parser.add_argument('--native', action='store_true',
                        help='stage N1 of the native path (host_render_thread) like Build.ps1 -Native')
    parser.add_argument('--msaa-samples', type=int, choices=[0, 2, 4],
                        help='with --native, like Build.ps1 -MsaaSamples: races in one pass with 2x '
                             '(the default) or 4x MSAA, or 0 for one tile without MSAA')
    parser.add_argument('--msaa', action='store_true',
                        help='keep the tiles and MSAA of --quality with a look that has TAA, like '
                             'Build.ps1 -Msaa (default: one tile, as the launcher)')
    parser.add_argument('--ao', action='store_true',
                        help="the runtime's ambient occlusion pass (the GTAO post pass) like pipeline.py --ao, "
                             'also without the ao look knob (which turns it on and applies it)')
    parser.add_argument('--shader-dev', action='store_true',
                        help='shader mod development like pipeline.py --shader-dev: hot reload and '
                             'every look knob live (--arg shader_mod_path=<dir> for another directory)')
    parser.add_argument('--tail', type=float, default=10, help='seconds to keep running after the last step')
    parser.add_argument('--keep-ppm', action='store_true', help='keep full-resolution captures')
    parser.add_argument('--info', action='store_true',
                        help='show the settings overlay from the start (F2 toggles it); it moves the '
                             'host presents to the UI thread')
    parser.add_argument('--measure', type=int, default=0, help='summarize only the last N perf seconds')
    parser.add_argument('--repeat', type=int, default=1,
                        help='run N times into <out>/run1..runN and aggregate into <out>/summary.json')
    parser.add_argument('--resummarize', action='store_true',
                        help='recompute summary.json of existing runs in --out from their perf.jsonl')
    parser.add_argument('--affinity', help='confine the game to these logical processors, a mask such as '
                        '0xC3, as a proxy for a smaller CPU (cpu_power.py)')
    parser.add_argument('--min-free-gb', type=float, default=MIN_FREE_GB,
                        help='refuse to run with less free space on the drive of --out')
    options = parser.parse_args()

    out = Path(options.out).resolve()
    # Captures and logs show game content; they must never land in the repository.
    if out == REPO or REPO in out.parents:
        parser.error('--out must be outside the repository, for example <BuildRoot>/e2e/<name>')
    if options.fast_route and not options.steps and not options.goal and options.scenario not in FAST_SCENARIOS:
        parser.error(f'--fast-route covers {", ".join(sorted(FAST_SCENARIOS))}, not {options.scenario}')
    if not options.resummarize:
        check_free_space(out, options.min_free_gb)
        import_content(Path(options.build_root).resolve(), options.game_build)
        check_content(Path(options.build_root).resolve())
        warn_if_building()
    if options.resummarize:
        summary = resummarize(out, options.measure)
    elif options.repeat <= 1:
        summary = run_once(options, out)
    else:
        runs = []
        for index in range(1, options.repeat + 1):
            runs.append(run_once(options, out / f'run{index}'))
            print(f'run{index}: fps_median={runs[-1].get("fps_median")} crashed={runs[-1]["crashed"]}',
                  flush=True)
        summary = aggregate(runs)
        (out / 'summary.json').write_text(json.dumps(summary, indent=2))
    print(json.dumps({k: v for k, v in summary.items() if k != 'errors'}, indent=2))
    washed = summary.get('washed_out_frames')
    if washed:
        print(f'WARN: {washed if isinstance(washed, int) else len(washed)} race captures have a band '
              f'washed out to white (washed_out_frames); look at the captures', flush=True)
    if summary['crashed'] or summary['goal_reached'] is False:
        print(f"FAIL: {summary.get('crashed') or summary.get('failed_step') or summary.get('failed_steps')}",
              flush=True)
        sys.exit(1)


if __name__ == '__main__':
    main()
