# Direct race launch

Research for a switch that starts a Quick Race without the menus, to shorten e2e runs. The research took place on 21 October 2026. **The switch has not been built.** The menus write the race into three places that have no known entry point yet (see [What a direct launch needs](#what-a-direct-launch-needs)), so a hook that skips them would be only partial. This note records what was found, what is missing and an estimate.

## The menu route

The quickrace scenario was run on 21 October 2026 in I:\FM4-build-pass with fm4_rex.exe sha256 `1011e073...`, `engageaionstart` and `FM4_FIXED_SEED=1`. The times are harness seconds; the game's own trace runs about 0.3 s earlier.

| Time | Event |
| --- | --- |
| 23.9 s | Title screen ("Press A to start the game") |
| 28.1 s | Main menu |
| 32.2 s to 80.0 s | 3 Rights and 8 A presses, 5 s to 8 s apart: FREE PLAY, QUICK RACE, MANUFACTURER SELECT (Abarth), CAR SELECT (1980 131 Abarth), ENVIRONMENT SELECT (Bernese Alps), TRACK SELECT (Festival Circuit), GO RACE |
| 80.0 s | GO RACE. The race is built and the session starts within 0.3 s |
| about 95 s | Loading ends: the controller goes from state 4 to state 5 at 95.2 s, and the ENVIRONMENT card shows |
| by 99 s | Pre-race menu (START RACE, STARTING GRID, ...) |
| 120.2 s | The harness's next A press. It comes after a fixed 40 s wait, so it lands about 20 s after the pre-race menu shows |
| 128.6 s | START RACE |
| (142.5 s) | LAPS on the HUD. Not from this run: it comes from the freeze scenario's seed-a run of the same day |

The menus, from the main menu to GO RACE, take about 52 s. Launch to LAPS took 142.5 s in the freeze scenario's seed-a run, not the run in the table. A direct launch from the main menu could save at most the 52 s; the boot to the title (about 24 s) and the title plus main menu (about 4 s more) are not in that bound.

## What GO RACE does

- **The UI creates scene 95, `17_DYNAMIC_LOADING_FREEPLAY`.**
  - `sub_824E2790` builds the frontend's scene registry: 249 entries of (factory, name), starting at +4.
  - `sub_824B8EE0(registry, id, parent)` calls the factory of entry `id` and stores the id at +12 of the scene.
  - It is called by the UI scene manager (`sub_82987198`, from a scene description whose id is at +34). The scene manager in turn is called from the UI update. The return addresses are `82987254 8239B11C 8239AF60 8298C614 826CE66C` (the main loop).
  - The navigation comes from the scene data, not from code that names scene 95.
- **The scene is the race event controller.**
  - Factory `sub_824E0B70`: 420 bytes, vtable `0x8202E8DC`, base constructor `sub_824E0608`, arguments (parent, name, a description on the stack).
  - The base class updates it through the state machine `sub_82588708`, with the state at +352. It is created in state 1.
  - State 1:
    1. `sub_82542A28` builds the race: a switch on the mode of the current event, `*(0x82DC36E8)+16` (`sub_82516590`). Quick Race reaches `sub_824D35D8`, which runs the opponent cars query. The switch sends modes 1, 2, 5, 6, 26, 28, 33 and 34 there; which mode Quick Race uses was not recorded.
    2. `sub_82542B90` sets session type 1 (`sub_826CB8D0`, from `0x82542C24`) and requests the session (`sub_826CB990`).
  - The states then run 2, 3, 4 (within 10 ms), 5 (loading done, 15 s later), 7 and 8.

## What a direct launch needs

1. **The current event**, `*(0x82DC36E8)`, set up as the Quick Race the menus build.
   - Its layout is read from the code only, not from a dump: the mode accessor `sub_82516590` returns `*(0x82DC36E8)+16`, and `sub_824D35D8` reads +16 as the mode and +256 to +276 for the track (it compares the word at +276 with the track kinds). The trace dumped `*(0x82DC36D8)` instead, another object.
   - The free play screens fill it. No setter for it is known yet.
2. **The player's car.**
   - The builder takes it from the game object: `sub_822CA4A0(*(0x82DC36FC))`, then virtual +104.
   - The car select screen puts it there, as a car instance it creates from the database.
   - No setter for it is known yet.
3. **A jump of the UI to scene 95**, with a parent and a scene stack that the pre-race menu, the pause menu, QUIT RACE and the return to the menus accept.
   - The UI scene manager takes a scene description, not an id.
   - A request that starts from the main menu (or earlier) has not been found.

## The recorded race settings

`FM4_TRACE_RACE_SETUP=<file>` records 4 KB of race settings (`sub_822DEC50`, +444 of `*(0x82DC36FC)`). They do not carry the opponents or the grid:

- They are per-slot settings: the controller slots `:1` to `:4`, assists and options.
- The race build changes only their bytes 0x0 to 0x25F. They match the copy written at the session start.
- The car setups that the driver factory receives (`sub_82A93880`) are heap objects outside the 4 KB, for example `0x4047F2F4` for the player and `0x41CDB414` for an opponent.

Replaying the 4 KB therefore does not reproduce a race. A direct launch would still run the race build, so the opponent cars query would still run, and [`FM4_FIXED_SEED`](../e2e/experiments.md#fixed-seed-for-the-race-setup) would still be needed for the same opponents and grid.

## Estimate

Finding the three entry points needs trace runs that watch writes to the event object and to the game object's car during the menus, and that find the UI's transition request. That is about 5 to 8 runs of about 2.5 minutes each, plus the analysis between them.

After that come the implementation and the checks of the UI flow after the race: pause, photo mode, QUIT RACE and the replay.

The total is about 2 to 3 days. The risk is that the scene stack rejects a jump from the main menu, or that the race needs state left behind by the car select screen, such as the loaded car model.

The saving is at most about 52 s per run: the menu span only, not the boot or the title.

## Cheaper options

- **Harness side** (not done here; the validation session owns `e2e.py`): the quickrace steps use fixed delays.
  - Waiting for each screen's text instead would remove most of the 20 s idle at the pre-race menu, and some of the menu time, with no game change. The texts are MANUFACTURER SELECT, CAR SELECT, ENVIRONMENT SELECT, TRACK SELECT or GO RACE, and START RACE.
  - The pre-race menu shows about 19 s after GO RACE, but the harness waits 40 s.
- **Splash videos and title**: tests on 16 October 2026 found that `disablesplashintro` and `noattractvideo` do not change the boot. Skipping the videos would need a hook on the movie player and would save at most about 10 s to 15 s. Not done.
