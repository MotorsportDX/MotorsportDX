# In-game PC settings overlay

Status: active. Written on 4 November 2026. S0 (the inventory) and S1 (persistence) are implemented
(4 November 2026); S2 to S4 follow.

A menu that the player opens during play to change the PC settings (look, display, controls) and
keep them for the next start. Today every PC setting is chosen in the GUI launcher before the game
starts, or given as launcher options; the gap is listed under "Settings and input" in
[current status](../status/current.md#settings-and-input).

## 1. Goals and non-goals

Goals:

- Change a setting without leaving the game, where the runtime can apply it while running, and see
  its effect at once in the same scene (a look A/B, the sharpening of TAA, rumble strength).
- Tune controls while driving: the pad's deadzones and curve, rumble, the keyboard's steering
  rates, the wheel's force feedback.
- Work on the ROG Xbox Ally X with the built-in pad alone: no mouse, no keyboard, readable at its
  7-inch 1080p screen.
- Keep one source of truth with the launcher: a value changed in the overlay is the value the
  launcher shows and uses at the next start.
- Say plainly which settings apply at once, which at the next race or screen load, and which need a
  restart.

Non-goals:

- Replacing the launcher. The launcher keeps the discs, the content, the builder, the shader pass,
  the profiles and every developer option.
- Developer options (traces, experiment switches, `FM4_*` test variables, extra cvars). The
  runtime's F4 dialog stays the developer tool (section 2).
- The RTV and ROV paths. RTV is frozen and ROV is the uncorrected reference
  ([AGENTS.md](../../AGENTS.md)); the overlay targets the native path. On RTV and ROV it shows the
  settings read-only (the owner's decision of 4 November 2026).
- Changes to the title's own menus. The overlay is the runtime's, drawn over the guest output.

## 2. What exists

| Piece | Where | What it does | What it lacks for this feature |
| --- | --- | --- | --- |
| Settings overlay (F2) | [fm4-info-overlay.h](../../work/fm4-rex/src/fm4-info-overlay.h), wired in [fm4_rex_app.h](../../work/fm4-rex/src/fm4_rex_app.h) | Read-only text: the lines of `FM4_INFO` (built by `info_overlay` in `pipeline.py` and by the launcher's plan), the window size and the game's frame rate. `IsIdle()` is always true, so it never makes the host present on its own ([D3D-level HLE, 17.22](d3d-hle-n4.md)). | No input, no editing. |
| Runtime settings dialog (F4) | [settings_overlay.cpp](../../work/runtime/src/ui/overlay/settings_overlay.cpp), bound in [rex_app.cpp](../../work/runtime/src/ui/rex_app.cpp) | A generic editor of every registered cvar, by category, with a search box, a lifecycle badge (live, restart, init only), key binding capture and "Save to config" into `fm4_rex.toml` beside the executable. | Mouse-driven; lists hundreds of developer cvars; knows nothing of the `FM4_*` variables or the launcher's options (look, quality, display); its save writes every value that differs from its default, including the ones the launcher passed on the command line (`SerializeToTOML` in [cvar.cpp](../../work/runtime/src/core/cvar.cpp)), so one save bakes the launcher's plan of that run into every later start. |
| Cvar lifecycle | [cvar.h](../../work/runtime/include/rex/cvar.h) | `kHotReload` (the default), `kRequiresRestart`, `kInitOnly`; `ListFlagsByLifecycle`; pending restart values. | Most of the project's cvars carry no tag and so claim `kHotReload`, whether or not the code reads them again after start. |
| Live shader knobs | `shader_mod_set`, `shader_mod_live_knobs` ([shader mod iteration](shader-mod-iteration.md)) | Post pass params (for example `TAA_SHARPEN`, `WINDSHIELD`, the GTAO params) change at once; with `shader_mod_live_knobs` the numeric look defines become constants written every frame. | `shader_mod_live_knobs` is a development mode: its binaries differ from the ones the shader pass compiles, so turning it on for players compiles in play (section 3.2). |
| Launcher settings | `launcher-settings.json` ([settings.h](../../work/fm4-launcher/src/settings.h)), `Options` in [plan.h](../../work/fm4-launcher/src/plan.h) | The profiles of run options, the saved looks, the key bindings; the plan turns them into `--name=value` cvars and `FM4_*` variables at Play. | Written only by the launcher. |

## 3. Inventory of the player's settings

The player's settings are the launcher's `Options` and the Controls page. The class says when a
change takes effect:

- **Live**: the code reads the value again while running (every frame, poll or draw).
- **Next load**: read when a race, a track or a screen loads.
- **Restart**: read once at start (a cached `getenv`, an init-only cvar, a device or swap chain
  parameter) or bound to the shader pass.

The evidence column names where the value is read. S0 verified every row in the code
(4 November 2026); the classes below are the verified ones, and the table of the player's settings
(`PLAYER_SETTINGS` in [player_settings.py](../../outputs/FM4-build/player_settings.py), launcher.json's
`settings`) carries them.

| Setting (launcher option) | Carried as | Class | Evidence |
| --- | --- | --- | --- |
| Pad stick and trigger deadzones, outer, curve, anti-deadzone (`-Pad`) | `pad_*` cvars | Live | `CurrentPadSettings()` in [pad_settings.cpp](../../work/runtime/src/input/pad_settings.cpp) reads the cvars at every poll of the XInput and SDL drivers |
| Rumble and trigger rumble strength (`-Pad`) | `pad_rumble_strength`, `pad_trigger_rumble*` | Live | the same, read when the rumble is set |
| Pad driver (`default`, `rawinput`, `gameinput`) | cvar | Restart | `ApplyDriverHints` in [sdl_input_driver.cpp](../../work/runtime/src/input/sdl/sdl_input_driver.cpp) sets SDL's hints once, before the pads start; the cvar was already `kRequiresRestart` |
| Keyboard steering and pedal rates (`-Keyboard`) | `mnk_*` cvars | Live | [mnk_input_driver.cpp](../../work/runtime/src/input/mnk/mnk_input_driver.cpp) reads every `mnk_*` cvar at each poll; nothing is cached |
| Key bindings (Controls page) | `keybind_*` cvars | Live | `RefreshBinds` compares each `keybind_*` text at every poll and parses the ones that changed |
| Racing wheel on or off, mode, drawn wheel (`-Wheel`) | `FM4_WHEEL_MODE`, `FM4_WHEEL_DRAWN` and cvars | Restart for the variables | cached in a `static` at first use in [fm4-wheel-input.cpp](../../work/fm4-rex/src/fm4-wheel-input.cpp) |
| Wheel force feedback strength and cap (`-Wheel`) | cvars | Restart | `StartWheel` copies every `wheel_*` cvar into the wheel host's options once; S0 tagged them `kRequiresRestart` |
| Look knobs that are post pass params (TAA sharpening, windshield, GTAO strength and radius) | `shader_mod_defines` read by name as params | Live | `shader_mod_set` applies params at once ([post pass framework](post-pass-framework.md)) |
| TAA on or off (`taa`) | param `TAA` | Live | `shader_mod_set TAA=0` switches TAA and its jitter together ([TAA](taa.md)); the tiling that goes with it stays until a restart |
| Look preset and the knobs that are shader mod defines (tone curve, grade, clear coat, road sheen, `sun`, `cockpit_fill`) | `shader_mod_path`, `shader_mod_defines` | Restart | each look compiles its own binaries; the shader pass compiles the active profile's look only ([running](../running.md#shaders-page)); another look in play would compile in play, against Level 1 |
| Track lighting (`sky`, `road_gloss`) | `FM4_TRACK_SETTINGS` | Restart | [fm4-track-settings.h](../../work/fm4-rex/src/fm4-track-settings.h); `FM4MountTrackSettings` reads the variable once, at start |
| Quality (MSAA, tiles), render path (`-Native`, `-Rtv`, `-Rov`) | cvars, `FM4_ONE_PASS_MSAA`, `FM4_TILING_SCENARIO` | Restart | render targets, pipelines and the shader pass depend on them |
| Display, resolution scale, upscaler (`-Display`, `-ResolutionScale`, `-Upscale`, FSR) | cvars | Restart | the swap chain and the render target sizes; FSR needs a runtime built with `-Fsr on` |
| Fullscreen | cvar, Alt+Enter | Live | the runtime's `bind_fullscreen` already toggles it |
| VSync, frame rate cap (`-VSync`, `-FrameRate`) | cvars, `FM4_FRAME_RATE` | Restart | `FM4_FRAME_RATE` is read at start ([fm4-vblank-rate.cpp](../../work/fm4-rex/src/fm4-vblank-rate.cpp)); `present_vsync` itself is read at every present, but the launcher's VSync also sets `present_min_interval_us` and the race frame rate, so the option is restart |
| Shadow workaround, anisotropy | `FM4_SHADOW_WORKAROUND`, `anisotropic_override` | Restart; anisotropy live | the variable is cached at start; `anisotropic_override` is read whenever the texture cache builds sampler parameters (it is not a player option: the launch plan fixes 16x) |
| Texture replacement | cvars | Restart | loaded on the command processor thread from a folder chosen at start |

What follows from the table:

1. The controls are almost all live already. They are the best first payload, and the one the
   ROG Xbox Ally X needs most.
2. Most of the look is restart-only, because looks are compiled binaries. Only the post pass
   params change at once.
3. A restart-only change still has value in the overlay: it is saved and the overlay says "on the
   next start" (or offers to restart, section 4.4).

### 3.1 Live look changes: three options

A live switch between presets needs every preset's binaries before play.

- **(a) Restart only.** The overlay saves the look and applies it at the next start. No new cost.
  The recommended first step.
- **(b) The shader pass compiles every preset.** Then a switch in play loads the binaries from the
  cache. Cost: the pass's time and the cache's size multiplied by the number of presets (six with
  `original`); this has to be measured on the ROG Xbox Ally X, whose pass is the slowest.
- **(c) Live knobs for players.** Compile the shared mods once with their numeric defines as
  constants (`shader_mod_live_knobs`), so presets that differ only in numbers switch at once. Cost:
  a constant read in place of a folded literal in every modded shader (to measure with the `perf`
  tier), and the presets that differ in more than numbers still need (a) or (b).

(b) and (c) are the owner's decision after S4 (section 7).

## 4. Design

### 4.1 Drawing and the present path

- An `ImGuiDialog` of the project, beside `FM4InfoOverlay`, drawn by the presenter over the guest
  output, so captures never contain it.
- While it is open it is not idle: the host presents at UI ticks, from the UI thread, as the `-Info`
  overlay did in the e2e runs before 24 October 2026 ([D3D-level HLE, 17.22](d3d-hle-n4.md)).
  That cost is accepted while the menu is open; closed, the dialog is destroyed and the present
  path is the players' own, as with F2 today.
- The look follows the launcher's racing theme ([launcher UX](launcher-ux.md)) where its code can
  be shared (colours, fonts, the option rows); the runtime's ImGui drawer has its own style, so S2
  checks what can be reused before copying.
- Scale follows the window's DPI and height, so the rows are readable on the Ally X's 1080p at
  7 inches and on a 4K desktop.

### 4.2 Input

- Opening with a pad: holding View and Menu (Back and Start) together for about one second (the
  owner's decision of 4 November 2026). It needs no button that Windows or the ROG Xbox Ally X
  takes for itself (Guide goes to the Game Bar, the Ally X's extra buttons to its own software).
  The title still sees the Start press before the chord is recognised, so its pause menu opens
  too, which pauses a race behind the overlay. S2 checks what the title does with Back alone and
  whether that press must be held back from the guest while the chord is pending.
- Opening with the keyboard: F2 opens the settings menu, whose last page is today's info text
  (the owner's decision of 4 November 2026). `-Info` and `FM4_INFO_SHOW=1` keep showing the
  read-only info panel at start, without input, so e2e runs that use `--info` do not change.
- While open, the guest sees a neutral pad and no keys, so steering a slider does not steer the
  car. The game is not paused by the overlay: the player opens the game's own pause menu first,
  or the overlay presses the title's pause on opening (section 7).
- Navigation with the pad (D-pad or stick, A to change, B to go back, the bumpers for pages),
  the keyboard and the mouse. ImGui's gamepad navigation is fed from the same input drivers.

### 4.3 Persistence: one source of truth

- The launcher stays the only writer of `launcher-settings.json`. The launcher may be open while
  the game runs, and two writers of one file lose changes.
- The overlay writes a small file of its changes, `overlay-changes.json` beside
  `launcher-settings.json` (the launcher passes its path in a cvar), with the profile name and
  each changed option in the launcher's own names (`pad`, `keyboard`, `wheel`, `look`, `quality`
  and so on), not cvars.
- At its next start, or when it gets focus, the launcher folds the file into that profile, saves,
  and deletes the file. A run started without the launcher (`fm4_rex.exe` directly, or `e2e.py`)
  does not get the cvar, so the overlay shows its values but cannot save them, and says so.
- The overlay never calls `rex::cvar::SaveConfig`, which writes every non-default value, including
  the plan's command-line values, into `fm4_rex.toml` (section 2). The F4 dialog keeps that
  behaviour for developers.
- Live values are applied with `rex::cvar::SetFlagByName` for cvars and through the
  `shader_mod_set` path for post pass params, so the overlay adds no second way of applying a value.

As built in S1 ([overlay.h](../../work/fm4-launcher/src/overlay.h)): at Play the launcher writes
`overlay-changes.json` with `profile`, the profile's `options` and an empty `changes`, and passes
its path in `fm4_settings_changes`. The overlay keeps the file's profile and options and writes the
file whole with every change of the run, so folding twice gives the same profile. `changes` maps a
knob option (`pad`, `keyboard`, `wheel`, `look`, whose `preset` key sets the look's preset) to its
changed knobs, and every other option to its new value, all as text in the launcher's own form
(`frame_rate` 0 for 60, `quality` empty for high). The launcher folds the file at its start, when
its window gets focus while no game runs, when the game exits and before Play: a knob set to the
launchers' default leaves the option's knob list, and a change of an unknown profile, option, knob
or value is dropped with a line in `logs\overlay-changes.log`. It removes the file unless the
overlay wrote it again meanwhile. The table reaches every run, `e2e.py` included, through
`fm4_settings_table` (launcher.json) in the launch plan.

### 4.4 Restart-only settings

- Shown with their class ("next start"), saved like the others.
- No "Restart now" for now (the owner's decision of 4 November 2026): the player quits and starts
  again from the launcher. A later version could exit with a code that tells the launcher to fold
  the changes and start the same profile again, the shader pass first when the look changed.

### 4.5 Pages

1. **Controls**: pad (deadzones, curve, anti-deadzone, rumble, trigger rumble; the driver as
   restart), keyboard (steering and pedal rates, mouse steering), wheel (on and off, force feedback
   gain and cap, all restart since S0 found the host takes them at start).
2. **Look**: preset (restart, or live after S5), TAA on or off, sharpening, windshield, AO strength.
3. **Display**: fullscreen (live); render scale, quality, upscaler, VSync, frame rate (restart).
4. **Info**: today's F2 text and the frame rate.

## 5. Performance, gates and tests

- Closed, the overlay costs nothing: no dialog exists. That is the state of every gate. `e2e.py`
  and `matrix.py` never open it, and the gates keep their meaning.
- Unit tests: the settings model (classes, ranges, the mapping between the launcher's option
  strings and cvars) in the launcher's test suite, which already holds the plan's cases; the
  folding of `overlay-changes.json` into a profile.
- A runtime test hook: a test event that applies one change through the overlay's code path
  without drawing it (for example `pad_rumble_strength`), so an e2e step checks that the value took
  effect and that the changes file is written.
- Screenshots of the overlay itself, for review: through the presenter's own output, not the
  guest captures; a `--fm4_settings_overlay_shot` style switch in S2, at scale 1 and 1.5.
- A short check of 15 to 20 minutes per stage ([tiered gates](d3d-hle.md#gate-tiers-the-main-sessions-decision-of-22-october-2026)):
  a Quick Race with the overlay closed against the previous main, and one with a live change.

## 6. Stages

Estimates are in AI calendar time (one person-week is about 3.5 hours).

| Stage | Work | Estimate | Check |
| --- | --- | --- | --- |
| S0. Inventory | Verify every "verify" row of section 3 in the code; tag the player cvars with their real lifecycle (`kRequiresRestart` where the value is cached); a table of the player's settings with class, range, default and the launcher option it maps to, shared by the launcher and the runtime (generated into `launcher.json` by `pipeline.py`, as the other tables are) | 2 h | the table's unit tests; the F4 dialog shows the corrected badges |
| S1. Persistence | `overlay-changes.json`, its path cvar from the launcher's plan, the launcher's folding on start and on focus | 2 h | launcher tests: a change folds into the right profile, an unknown profile or option is dropped with a log line |
| S2. The overlay shell | The dialog, pad chord and key, neutral guest input while open, pad navigation, DPI scale, the racing theme where shareable, the screenshot switch | 4 h | screenshots at scale 1 and 1.5; a Quick Race with the overlay opened and closed; the present path back to the guest's own once closed |
| S3. Live settings | The Controls page and the live look params (TAA, sharpening, windshield, AO) through `SetFlagByName` and the `shader_mod_set` path; fullscreen | 3 h | the test event changes rumble and a param; a short check with the overlay closed against the previous main |
| S4. Restart settings | The Look and Display pages' restart rows, saved and labelled "next start" | 2 h | a change of look and quality saved in the overlay is the launcher's at the next start; the shader pass runs when the look changed |
| S5 (optional). Live presets | Option (b) or (c) of section 3.1, after the owner's decision | 4 to 7 h | the shader pass's time and cache size per preset on this PC; the `perf` tier for (c); no compile in play when switching (the native path's compile counters) |
| S6. The ROG Xbox Ally X | Notes and a checklist for the owner's run: the View and Menu chord on the built-in pad (and that neither button is taken by the Ally X's software), readability, navigation, a live rumble change in a race | 0.5 h of notes; the owner's run | the owner's report |

S0 to S4 are about 13 hours; with S5, 17 to 20 hours.

## 7. Decisions and open questions

Decided by the owner on 4 November 2026:

- The pad opens the overlay with View and Menu held together (section 4.2).
- F2 opens the settings menu, with the info text as its last page (section 4.2).
- No "Restart now" for now (section 4.4).
- On RTV and ROV the overlay is read-only (section 1).

Open:

1. **Pausing.** The chord's Start press opens the title's pause menu in a race. Is that enough, or
   should F2 press the title's pause too? To settle in S2 with what the title does.
2. **Live presets (S5).** Option (a) only, or (b) or (c) after measuring their cost? Asked after S4.
