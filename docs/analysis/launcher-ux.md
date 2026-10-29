# GUI launcher UX: the racing theme

This note describes the look and the controls of the GUI launcher `MotorsportDX.exe`
([work/fm4-launcher](../../work/fm4-launcher), [GUI launcher](../running.md#gui-launcher)) and the
increments that bring every page into them. It replaces the first proposal of 29 October 2026
(a Home page of setup cards in the old flat style), which the project owner rejected the same day
as too plain: it did not feel like the game, and the launcher has to be easy and comfortable with
a gamepad.

The two machines the launcher has to serve:
- **The laptop:** a 2560x1600 SDR panel at 150%, mouse and keyboard, sometimes a pad.
- **The ROG Xbox Ally X:** a 7-inch 1920x1080 touch screen at 150% (about 315 pixels an inch,
  12.4 pixels a millimetre) and its built-in pad. The owner tests it through exports.

## 1. The owner's decisions (29 October 2026)

- **Direction:** the launcher looks like a racing game's menus. The visual reference is the menus
  of Forza Motorsport (2023): a photo behind translucent panels, a menu with a detail panel,
  settings as tabs of option rows, and the pad's buttons named at the bottom of the screen.
- **The concept** of the prototype (branch `claude/launcher-ux-prototype`, not for main) is
  approved: a blurred backdrop with translucent panels; a Home menu with a detail panel; settings
  as LB/RB tabs of option rows, where left and right change the value; the focused option's help
  in a right panel; a prompt bar with Menu Play, Y Profile, B Back and A Select.
- **The focus colour is red**, the wordmark's red.
- **The race frame rate is a set of steps** (30, 60, 90, 120, 144 and 240 fps), not a slider of
  single frames.
- **The backdrop's default is a capture of the game**, kept as `launcher-backdrop.jpg` beside the
  launcher, and players can replace it with a picture of their own.
- **The emblem** is the project's own drawn emblem beside the MOTORSPORT DX wordmark, with DX in
  red (the main session's decision, reported to the owner). The project is not affiliated with
  Microsoft or Turn 10, so no emblem or logo of the Forza series is used. An original emblem from
  the owner would replace the drawn one.

## 2. The design

**The frame**, on every page:
- the backdrop: the picture scaled to cover the window and blurred, darker at the top and the
  bottom where the bars sit; without a picture, a dark gradient with faint light streaks;
- the top bar: the emblem and the wordmark, the page's name in a rounded badge, and at the right
  a player card with the active profile and the Disc 1 release with the build date;
- the page in a centred column of at most 1,500 pixels times the scale;
- the prompt bar at the bottom right: Menu **Play** (only while Play can start), Y **Profile**,
  B **Back** (not on Home) and A **Select**, each a white circle with the button's letter and a
  word in Bahnschrift. The mouse and touch press a prompt as the pad presses its button. While
  Play cannot start, the bar's left end says why, in the warning colour.

**The controls:**
- On the themed pages the launcher keeps its own focus, as a game's menu does: always on one
  entry, moved by the D-pad, the left stick, the arrow keys or the mouse. A or Enter selects.
  Dear ImGui's navigation stays off there and serves the pages not yet themed and the popups.
- An option row is a translucent bar with the label at the left and the value at the right. The
  focused row has a lighter fill, a red frame with a notch in its top right corner, and chevrons
  around its value, which turns red. Left and right step the value; A, or a click on the row,
  steps it on; a click on the left chevron steps it back.
- A slider row (a look knob) has a bar between the label and the value, which the mouse or a
  finger drags; left and right step it by a round number near a fortieth of its range (0.05 for
  most knobs). A on a slider does nothing.
- A changed row has a red edge and a red label; X, Delete, Backspace or the X **Reset** prompt,
  which shows while such a row has the focus, resets it.
- LB and RB change the page's tab, B goes back to Home, Start plays, Y opens the profiles.

**The detail panel** at the right of a list shows the focused entry: its title in capitals in
Bahnschrift, its help, and below it what the options give (the render scale, the upscaler's
lines, the setup state). Red slashes fill its bottom right corner.

**Fonts and icons:** Bahnschrift for the wordmark, tabs, values and titles; Segoe UI for the text;
Segoe Fluent Icons for the icons. All are Windows' own, read at run time; the launcher carries no
font or picture.

**Developer options** stay behind **Show developer options** on Advanced (saved in
`launcher-settings.json`, never part of a plan): the raw `-Look`, `-Pad`, `-Keyboard` and `-Wheel`
values, Display's race rendering and the plan's description, and Advanced's render paths, test
switches, performance trace, extra cvars, texture dump and command line.

**The window** opens at 980x700 times the screen's scale, smaller when the monitor's work area is
smaller, and centred in the work area, so the taskbar never covers the prompt bar on the Ally X
at 150% (1,280x720 points, of which the taskbar takes about 48).

**The backdrop file:** `launcher-backdrop.jpg` (or `.png`) beside `launcher.json`. It is not part of
the source and the repository carries none ([distribution](../sources-and-licensing.md#distribution)).
The development computer's is a capture of a race at 2560x1440, saved at 1920x1080;
`export` copies it into an export that has none. The launcher reads it through WIC at 640 pixels
wide and blurs it with four box passes before the GPU scales it up.

## 3. Review screenshots

`MotorsportDX.exe --screenshot` ([launcher screenshots](../running.md#launcher-screenshots))
renders one page with WARP, without a window, so a review never takes focus from a game run.
Every increment is captured before and after at:
- 980x700 at scale 1: the default window on a 100% screen;
- 1470x960 at scale 1.5: the default window on the Ally X, inside its work area;

with `--backdrop` for the backdrop, `--pad` for the focus and the tabs, `--state developer` for the
developer rows and `--touch` for the Ally X's layout. The screenshots stay outside the repository.

## 4. Increments

Each lands on its own branch from local main, with the launcher built, the launcher tests passing
with the plans unchanged (531 plans), before and after screenshots at scales 1 and 1.5 shown to
the owner, and the [GUI launcher](../running.md#gui-launcher) section updated. The efforts are AI
calendar time.

| Increment | Contents | Effort |
| --- | --- | --- |
| T0 | The headless screenshot mode (`--screenshot`, `--state`, `--pad`) | Done |
| T1 | The frame (backdrop, top bar, prompt bar pressed by the pad, the mouse and touch, the window inside the work area), Home, Display (Screen, Performance and Extras tabs; the frame rate in steps; **Use FSR 3.1** for the upscaler), the profiles on Y, the developer switch, Controls in three tabs, the touch layout, the crash banner in words | Done |
| T2 | Look: the preset as a row, the knob groups (picture, lighting, car, sky and foliage, road) as tabs, each knob a slider row whose value steps by about a fortieth of its range and whose bar the mouse drags; a changed knob marked, Reset on X; **Save as look** and the saved looks, a delete after a confirmation | Done |
| T3 | Controls: Controller, Racing wheel and Keyboard as LB/RB tabs of rows; the key bindings as rows that wait for a key on A; the wheel test as its own panel. The racing wheel's controls and plan keys stay as the wheel's stages define them | 2.5 h |
| T4 | Game data: Disc 1, Disc 2 and the content folders as rows that open the folder picker on A, their checks in the detail panel; the content packages as a list with their import state; the texture pack | 1.5 h |
| T5 | Shaders and Advanced: the shader pass's progress as a panel with Skip and Compile on the prompts; Advanced as rows, the developer options as a tab of their own | 1.5 h |
| T6 | About, and the profile popup in the theme: the credits in the detail panel style; the profiles as rows | 1 h |

T3 shares the Racing wheel tab with the racing wheel's own stages (another session): each
increment rebases on local main before it lands, and keeps the wheel's options, its test and its
`-Wheel` knobs working.

## 5. Open questions for the owner

- A setting in the launcher to pick the backdrop picture, instead of replacing the file.
- Names: the look presets (ACES, GT, Neutral, Original), the pad drivers, and whether "Game data"
  should read "Game files".
