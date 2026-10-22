# Direct race launch

Research for a switch that starts a Quick Race without the menus, to shorten e2e runs. The research took place on 21 October 2026. **The switch has not been built.** The menus write the race into three places that have no known entry point yet (see [What a direct launch needs](#what-a-direct-launch-needs)), so a hook that skips them would be only partial. This note records what was found, what is missing and an estimate.

Instead, since 22 October 2026 the e2e harness takes the menus at the title's own pace: [the fast route](#the-fast-route-22-october-2026) waits for the title's screen events rather than fixed delays and saves about 57 s per run. Against it, a direct launch would save at most the 25 s from the main menu to GO RACE.

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

## The fast route (22 October 2026)

`e2e.py --fast-route` ([fast route](../e2e/README.md#fast-route)) drives the same menus with the same defaults, but each press waits for an event of the title instead of a fixed delay. The title writes the events with `FM4_TEST_EVENTS_FILE` ([`fm4-test-events.cpp`](../../work/fm4-rex/src/fm4-test-events.cpp)): every UI scene the frontend creates (`sub_824B8EE0`, with its name from the scene registry), every render scenario push and pop, and the countdown's placing of the cars and its 3-2-1.

The runs: `I:\FM4-build-usa`, `game-build-route` (fm4_rex.exe sha256 `e4f78753...`), 240 Hz, `engageaionstart`, `FM4_FIXED_SEED=1`, `--profile copy`. Seconds from the launch, medians of 5 `photo` runs of the fast route and 3 `race` runs of the menu route:

| Event | Menu route | Fast route |
| --- | --- | --- |
| Title scene (`120_P_PRE_SPLASH`) | 20.9 | 20.9 |
| Main menu (`push UI_Homespace`) | 24.9 | 24.4 |
| GO RACE (scene 95, `17_DYNAMIC_LOADING_FREEPLAY`) | 79.5 | 49.7 |
| Pre-race menu (`4_C_INI_PRERACE`) | 96.8 | 66.9 |
| START RACE (`947_HUD`) | 128.0 | 71.3 |
| The cars placed (`countdown placed`) | 129.0 | 72.3 |
| Race (`push 12-Car_Race`) | 138.0 | 81.1 |
| Race phase starts | 147.2 | 90.4 |

Every milestone varied by at most 0.4 s between the runs of a route. The `photo` runs of the fast route took 191.6 s to 193.9 s; the menu route's `photo` runs at 1x and 240 Hz took 249.9 s to 252.5 s on 22 October 2026 (the same steps after the race phase). One `race` run of each route without the seed took 137.1 s and 193.4 s.

What the events show:

- Every screen from QUICK RACE to the track creates its scene as it takes the A that opens it, within 0.1 s. In the fast route each screen took the first A, 1.5 s after the scene before it, in every run.
- The track screen's A is GO RACE: it creates scene 95, the race event controller. The pre-race menu's scene (`4_C_INI_PRERACE`) follows 17.1 s to 17.3 s later in both routes; that is the loading, and the route does not change it. The menu slides in for about 2 s.
- START RACE creates the HUD (`947_HUD`) at once and places the cars about 1 s later; the race scenario (`12-Car_Race`) follows 8.5 s after the 3-2-1 starts. The fast route starts the race phase 9.3 s after that push, where the menu route's fixed delays put it (9.1 s to 9.2 s after it).
- The splash videos do not get shorter. With A every 2.5 s from the launch, the title scene (`120_P_PRE_SPLASH`) came at 20.9 s to 22.4 s, as in the menu route (20.9 s to 21.8 s): the Turn 10 part of `T10_MS_Combined.wmv` does not take A, only its Microsoft Studios part does.
- With the seed, the race is the same race: in the race phase the fast route drew 1,914 to 1,968 draws per frame (median 1,947) at 212.8 to 220.7 fps (median 218.6, p99 6.0 ms to 6.8 ms), the menu route 1,902 to 1,956 (median 1,913) at 211.7 to 219.6 fps (median 216.7, p99 6.3 ms to 7.1 ms), with the same GPU time per frame (2.93 ms to 2.98 ms) and the same three error keys. Without the seed the menu route's race phase spreads over 1,462 to 2,441 draws per frame and 200 to 238 fps (27 `photo` and `race` runs on RTV at 1x and 240 Hz of 21 and 22 October 2026, several binaries).

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

The saving was at most about 52 s per run against the menu route of 21 October 2026: the menu span only, not the boot or the title. Against the fast route it is at most the 25 s from the main menu's render scenario to GO RACE, of which the main menu's OCR checks and `select:FREE PLAY` take about 13 s.

## Cheaper options

- **Harness side**: done on 22 October 2026 as [the fast route](#the-fast-route-22-october-2026), with the title's screen events in place of the screen texts that this note proposed (OCR sees a screen only every 2 s).
- **Splash videos and title**: tests on 16 October 2026 found that `disablesplashintro` and `noattractvideo` do not change the boot. Skipping the videos would need a hook on the movie player and would save at most about 10 s to 15 s. Not done. A pressed every 2.5 s from the launch does not skip them sooner (see [the fast route](#the-fast-route-22-october-2026)).
