# GUI launcher UX: audit and proposal

This note audits the GUI launcher `MotorsportDX.exe` ([work/fm4-launcher](../../work/fm4-launcher),
[GUI launcher](../running.md#gui-launcher)) as a player meets it, and proposes a layout and a
set of small increments. It was written on 29 October 2026 from the launcher of local main
`c2d515c` and a prototype, without running the game.

The two machines the launcher has to serve:
- **The laptop:** a 2560x1600 SDR panel at 150%, mouse and keyboard, sometimes a pad.
- **The ROG Xbox Ally X:** a 7-inch 1920x1080 touch screen at 150% (about 315 pixels an inch,
  12.4 pixels a millimetre) and its built-in pad. The owner tests it through exports.

## 1. Method

`MotorsportDX.exe --screenshot` ([GUI launcher](../running.md#gui-launcher)) renders one page
without a window, with WARP, at a given size and DPI scale, so the audit never opened a window
that could take focus from a game run. `--state` shows the first run and the error banner, and
`--pad` presses gamepad buttons before the capture, which is how the pad navigation below was
checked. Every page was captured at:
- 980x700 at scale 1 and 1470x1050 at scale 1.5: the launcher's default window on a 100% and a
  150% screen;
- 1920x1080 at scales 1 and 1.5: a maximized window;
- 1280x3600 at scale 1: whole pages, to see everything below the fold.

The mockups come from the prototype on branch `claude/launcher-ux-prototype` (commit `9a4a995`,
not for main), captured the same way, with `--touch` for the Ally X's layout. The screenshots
stay outside the repository; the same commands on that branch make them again.

## 2. Problems, by player impact

| # | Problem | Where | Who |
| --- | --- | --- | --- |
| 1 | **Upscale Quality, Balanced or Performance on the default look makes Play fail.** The upscaler is the look's anti-aliasing method (`aa=fsr` or `aa=taau`, Look page); with the default look the plan refuses it, the Display page shows `-Upscale quality needs aa=fsr or aa=taau in -Look: TAA does not upscale.` under the options, and Play shows the same error. The render scale also falls back to "Auto (1x, 1280x720)" in its label. | Display | Every player who tries the upscaler |
| 2 | **The first run has no path.** The launcher opens on Display, or on Shaders when the build has not had its shader pass: that pass starts at once, also before Disc 1 is set. Play is enabled; pressing it jumps to Game data with a red banner. Nothing says that Disc 1 is the one step that is required, that Disc 2 and the content are optional, or that the shader pass follows. | All | Every new player |
| 3 | **The pad cannot move between the panels directly.** The navigation and the page are child windows: the D-pad first lands on the profile box in the footer, A enters a panel and B leaves it. There is no page switch on the shoulder buttons, Start does nothing, and ImGui's focus ring is 2 pixels at every scale (0.16 mm on the Ally X). | All | Ally X, any pad |
| 4 | **Touch targets are small and there is no finger scrolling.** At 150% a button is 43 pixels high (29 effective pixels, 3.5 mm) and a small button (Reset, Set rest, Remove import, Assign) 26 pixels (2.1 mm); Windows asks for 40 effective pixels. ImGui does not scroll a page by dragging it, and the scrollbar is 18 pixels wide. The Controls page is about 2,000 pixels long at scale 1. | All | Ally X |
| 5 | **The default window is taller than the Ally X's screen.** 980x700 at 150% is 1470x1050 pixels, plus the title bar, on a 1080-pixel screen whose taskbar leaves about 1008: the footer with Play is likely under the taskbar. Not seen on the device yet. | Window | Ally X |
| 6 | **Developer knobs sit among the player's options.** The raw `-Look`, `-Pad`, `-Keyboard` and `-Wheel` values named after `Build.ps1`; "Menus at the render scale", "One tile with TAA / MSAA tiles"; the plan's description line ("logical 1280x800 Vert+"); on Advanced the render paths, ROV, vblank and shadow switches, extra cvars and the texture dump. | Display, Look, Controls, Advanced | Every player |
| 7 | **One long Controls page.** Controller, racing wheel, wheel test, keyboard and 26 key bindings in one scroll; the racing wheel's test sits between the wheel's options and the keyboard. | Controls | Pad, wheel and keyboard players |
| 8 | **Readability.** Slider values are drawn over the grab and hidden by it at many values; the race quality's hint runs off the right edge at 150% and in the default window; the status lines under the paths start 22 pixels right of the field above them; section titles ("Controller", "Racing wheel") are the size and colour of the labels. | Display, Look, Controls, Game data | Every player |
| 9 | **Error states speak in codes.** A crash shows "The game exited with code 3221225477" (0xC0000005, a memory access violation). | Banner | Players who hit a crash |
| 10 | **Deleting asks nothing.** The profile's Delete and Delete look remove at once, beside Save as. | Footer, Look | Every player |
| 11 | **Wording.** Preset names in lower case (`aces (default)`, `gt`) that also name tone curves; "60 fps exposure speed"; the pad drivers "Raw Input", "WGI", "GameInput" and "Input thread" without plain words; long help paragraphs that quote the game's internal deadzone percentages. | Look, Controls | Every player |

What works and stays: the dark theme and its colours, the label column, DPI scaling of fonts and
sizes (`FontScaleDpi` and `ScaleAllSizes` keep text sharp at 150%), profiles, the content
package table with its import state, the per-knob Reset, and the shader pass running once per
build.

## 3. Proposed layout and flow

```
+--------------------------------------------------------------------------+
| Motorsport DX   USA release . build 29 October 2026         Disc 1 found  |
+------------+-------------------------------------------------------------+
| Home     * | Ready to race / Set up Motorsport DX                        |
| Display    |  [o] Disc 1 (Play Disc)          USA release, verified [..] |
| Look       |  [o] Disc 2 and content          4 packages, imported  [..] |
| Controls   |  [o] Shaders                     compiled for this build    |
| Game data .|  [o] Controls                    Xbox controller            |
| Shaders   .|  Profile: Default  render 2x . High . 60 fps . look ACES    |
| Advanced   |                                                             |
| About      |                                                             |
| LB/RB pages|                                                             |
+------------+-------------------------------------------------------------+
| Profile [Default v] [Save as]   Choose Disc 1 first             [ Play ] |
+--------------------------------------------------------------------------+
```

- **Home first.** The launcher opens on a Home page with four setup steps as cards, each with its
  state, one line in plain words and one button: Disc 1 (required: "Choose folder"), Disc 2 and
  content (optional: "Add"), the shaders (compiled, compiling with its progress and Skip, or
  "Compile now"), and the controls (the connected pad, or "the keyboard drives"; the racing
  wheel when it is on). Below them, the profile's main settings in one line with links to
  Display and Look. A navigation dot marks Game data while Disc 1 is missing and Shaders while
  the pass runs.
- **Play says why it waits.** Play is disabled until Disc 1 checks out, and the footer says why
  ("Choose Disc 1 first") where it said "Saved on Play". The automatic shader pass starts only
  once Disc 1 is set, and shows on Home instead of switching to the Shaders page.
- **Player options first, developer options behind one switch.** Advanced starts with the user
  data folder and the log and user-data buttons, then "Show developer options" (saved in
  `launcher-settings.json`, never part of a plan). It shows the raw option values on every
  page, Display's menus scale and race rendering, the plan's description line, and Advanced's
  render paths, test switches, performance trace, extra cvars, texture dump and command line.
  The [Ally X run set](../running.md#ally-x-run-set) then starts with turning it on.
- **Controls in tabs:** Controller, Racing wheel (the wheel's options and its test, unchanged),
  Keyboard (its options and the key bindings).
- **The upscaler fixes its own requirement:** with Upscale on and a TAA look, Display says
  "Needs FSR 3.1 or TAAU" with a **Use FSR 3.1** button that sets `aa=fsr` (and `taa=1`) in the
  profile's look. The plan only changes when the player presses it.
- **Pad:** the navigation, the page and the footer are flattened (`ImGuiChildFlags_NavFlattened`),
  so the D-pad moves between them without A and B; LB and RB change the page, Start presses
  Play when it is enabled, and the navigation shows "LB / RB pages, Start Play" while a pad is
  connected. A focus ring of 2.5 pixels times the scale in the accent colour is drawn over
  ImGui's own (the 2-pixel ring is fixed in Dear ImGui, which the runtime shares and this
  project does not patch).
- **Touch** (on when Windows reports a touch screen): frame padding 14x12 and item spacing 12
  (buttons 61 pixels high at 150%, 41 effective pixels), small buttons become full buttons, a
  22-pixel scrollbar, and a finger dragged over the page scrolls it.
- **Window:** the first window fits the display's usable area (maximized when 980x700 times the
  scale does not fit).
- **Readability:** slider values beside the slider; the race quality's hint on its own line; the
  status lines aligned with their fields; a heading font for page and section titles.
- **Crash banner in words:** "The game crashed after 2 min 14 s (a memory access violation,
  0xC0000005). The game log says more." Other NTSTATUS codes print in hex.
- **Confirm before deleting** a profile or a saved look.

### Mockups (prototype, `claude/launcher-ux-prototype`)

| Mockup | Command (`MotorsportDX.exe --screenshot ...`) | Shows |
| --- | --- | --- |
| Home, first run, Ally X | `home 1920x1010 1.5 <out> --touch --state first-run` | The setup steps, Disc 1 highlighted, Play disabled with "Choose Disc 1 first" |
| Home, ready, laptop | `home 980x700 1` | All steps done but the shaders, the profile's line |
| Display with Upscale Quality | `display 980x700 1 <out> --settings <upscale quality, default look>` | "Needs FSR 3.1 or TAAU" and **Use FSR 3.1** instead of a failing plan |
| Controls, Ally X, pad | `controls 1920x1010 1.5 <out> --touch --pad down,down,down,right,a` | The tabs, touch sizes, the thick focus ring, the LB/RB hint |
| Look, Ally X | `look 1920x1010 1.5 <out> --touch` | Slider values beside the sliders |
| Error banner | `home 980x700 1 <out> --state error-banner` | The crash in words |

## 4. Increments

Each lands on its own branch from local main, with the launcher built, the launcher tests
passing with the plans unchanged (531 plans on 29 October 2026), before and after screenshots at
scales 1 and 1.5, and the [GUI launcher](../running.md#gui-launcher) section updated. The
efforts are AI calendar time.

| Increment | Contents | Problems | Effort |
| --- | --- | --- | --- |
| U0 | The headless screenshot mode (`--screenshot`, `--state`, `--pad`) | (method) | Done |
| U1 | The upscaler's requirement on Display, with **Use FSR 3.1** | 1 | 0.5 h |
| U2 | Pad and touch: flattened navigation, LB/RB, Start, the focus ring, the touch layout, finger scrolling, the first window inside the usable area | 3, 4, 5 | 1.5 h |
| U3 | Readability: slider values, the quality hint, the status line alignment, headings | 8 | 1 h |
| U4 | Home page and Play's reason; the shader pass waits for Disc 1 and shows on Home; the crash banner in words | 2, 9 | 2 h |
| U5 | Developer options behind one switch on Advanced; docs, including the Ally X run set | 6 | 1 h |
| U6 | Controls in three tabs; the racing wheel's section moves whole, with its controls and keys | 7 | 0.5 h |
| U7 | Wording: preset and option names, shorter help, confirmations before deleting | 10, 11 | 1.5 h |

U1 and U3 change no layout and can land first. U2 matters most for the Ally X. U6 moves the
racing wheel's section into a tab without changing its contents, so the racing wheel's stage 3
options (gain, minimum force, smoothing, torque cap) land in it as they would today. U7 needs the
owner's choices of names.

## 5. Open questions for the owner

- The direction: Home as the first page, and the developer options behind one switch.
- Which increments, and in what order.
- Whether the Ally X's launcher window reaches below the taskbar today (problem 5).
- Names for U7: the look presets (ACES, GT, Neutral, Original), the pad drivers, and whether
  "Game data" should read "Game files".
