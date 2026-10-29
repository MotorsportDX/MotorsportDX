# Running Motorsport DX

This guide covers launching a finished build with `Build.ps1 -Action run`: what
the launcher checks and sets up, its options, the look knobs, the controls and the
data directories. The same options are in the [GUI launcher](#gui-launcher), which
also runs the game on a computer without the build tools after an
[export](#export-to-another-computer). Build the game first; see [setup.md](setup.md).

Performance numbers in this file describe the launcher defaults, and the option
details keep the measurements that explain each option. Measurements that depend on
a specific build are dated in the status documents: [status/current.md](status/current.md)
and [status/history.md](status/history.md).

## Launching

```powershell
.\outputs\FM4-build\Build.ps1 -Action run -BuildRoot 'D:\FM4-build' -LLVMRoot 'C:\Program Files\LLVM' -Python 'C:\Python311\python.exe'
```

If you open a new PowerShell session, define `$common` again as shown in
[setup.md](setup.md#commands). `run` uses the Disc and content locations saved by
`generate`; it does not read `-Disc1` or `-Disc2`. `-Content` on `run` replaces the
saved content locations (see [Downloadable content](setup.md#downloadable-content)).

The launcher then:

1. Checks the build artifacts and the game revision. It compares the artifact
   hashes with `build-result.json`, validates the Disc and content paths in
   `local.json` and checks the three executables.
2. Starts with a new local profile in `BuildRoot\user-data` (see
   [Data directories](#data-directories)).
3. Mounts Disc 1 and imports the content packages: Disc 2's four, if Disc 2 was
   given, and those found in the `-Content` paths. It lists them in
   `BuildRoot\content-packages.txt`. A package is imported once, into
   `BuildRoot\user-data`; later runs skip it while its size is unchanged. The first
   import needs extra disk space, about the size of the packages.
4. Prints the display and render scale that it chose and starts the game.

The game window runs fullscreen on the primary monitor by default. Use Alt+Enter to
switch to a window (see [Keyboard](#keys)).

## GUI launcher

`build` builds `MotorsportDX.exe` into `BuildRoot\game-build`, beside the game. It is
a window with the options of `Build.ps1 -Action run` that starts the game itself: it
needs neither Python nor Visual Studio nor the repository, so it also runs from an
[export](#export-to-another-computer) on another computer. Start it by double-clicking
`MotorsportDX.exe`. `MotorsportDX.exe --play` presses Play at once with the active
profile, for a desktop shortcut.

The launcher looks like a racing game's menus and works with a gamepad, a mouse, a keyboard or
touch ([launcher UX](analysis/launcher-ux.md)):
- **The frame.** A backdrop picture, blurred, behind translucent panels; the MOTORSPORT DX
  wordmark and the page's name at the top left; the active profile and the Disc 1 release with
  the build date at the top right; and a bar of pad prompts at the bottom right: Menu
  **Play**, Y **Profile**, B **Back** and A **Select**. The mouse and touch press a prompt as
  the pad presses its button. While Play cannot start, the bar's left end says why ("Choose
  Disc 1 first").
- **The focus** is a red frame. On every page and in the profiles the D-pad, the left stick or
  the arrow keys move it, A or Enter selects, and on an option row left and right change the
  value (a click on the row steps it on, a click on a chevron steps it back or on). A slider row
  has a bar that the mouse or a finger drags. A changed row has a red edge and a red label, and
  X, Delete or the X **Reset** prompt resets it. The focused option explains itself in the panel
  at the right. The confirmations of the Look and Game data pages use Dear ImGui's own
  navigation with a thicker focus ring.
- **The buttons.** LB and RB change the page's tab, B goes back to Home, Start plays from any
  page, and Y opens the profiles: pick one, save the options as a new profile (under the name
  typed in its field, or `Profile <n>`), or delete the active one after a confirmation. B or
  Escape steps back.
- **The window** opens at 980x700 times the screen's scale, made smaller when that does not fit
  the monitor's work area, and centred in it: on the ROG Xbox Ally X at 150% the taskbar never
  covers the prompts. On a touch screen the buttons are larger and a finger dragged over a page
  scrolls it.

**The backdrop** is `launcher-backdrop.jpg` (or `.png`) beside `launcher.json`, in the BuildRoot
or the export folder. It is a local file, not part of the source: the repository carries no
picture. Without it the launcher draws a dark gradient. Any picture of your own replaces it;
the launcher scales and blurs it. `export` copies the BuildRoot's backdrop into an export folder
that has none ([distribution](sources-and-licensing.md#distribution)).

| Page | Contents |
| --- | --- |
| Home | The first page. A menu (Play, then the pages) and a panel with the focused entry's detail: whether Disc 1 checks out, Disc 2 and the content, the shaders, the connected controller, and the profile's render scale, quality and frame rate. While Disc 1 is missing, Play reads "Choose Disc 1" and opens the folder picker; Game data and Shaders carry a mark while they need attention or work. |
| Display | Three tabs of option rows. **Screen:** the screen (`-Display`), fullscreen, the render scale and supersample. **Performance:** the upscaler (`-Upscale`), the race frame rate in steps of 30, 60, 90, 120, 144 and 240 fps (a rate saved before keeps showing until it is changed), VSync and the race quality. With the upscaler on and a look whose anti-aliasing is TAA, a row "Use FSR 3.1 for the upscaler" sets `aa=fsr` in the profile's look. **Extras:** the rear-view mirror every frame, the settings overlay at start and the menus at the render scale. The right panel shows the render scale and the upscaler's lines that the options give. |
| Look | Six tabs of option rows. **Preset:** the preset (`aces`, `gt`, `neutral`, `original`, `original` without TAA, or a saved look; changing it drops the knob changes), the tone curve, temporal anti-aliasing and its method (TAA, FSR 3.1 or TAAU, `aa`), the exposure speed of 60 fps (`adaptation`), **Save as look**, which keeps the current value under a name, **Delete this look** for a saved look, after a confirmation, and **Reset all changes**. **Picture, Lighting, Car, Sky & foliage and Road:** a slider row for each [look knob](#look) of that group, which left and right step by about a fortieth of its range; the right panel says what the knob does and the preset's value. |
| Controls | Five tabs of option rows. **Controller:** the options of `-Pad` (the pad driver, rumble strength, trigger rumble and its strength, the steering anti-deadzone, the input thread, then the stick and trigger shaping and the extra buttons); see [Controller](#controller). **Racing wheel:** the wheel in the game (`-Wheel`: steering, the three pedals, the buttons and force feedback), with the steering's or the focused pedal's value at the right while the wheel test runs; see [Racing wheel in the game](#racing-wheel-in-the-game). **Wheel test:** the [wheel test](#racing-wheel-test), with the device and its axes, buttons and force at the right. **Keyboard:** the options of `-Keyboard` (smooth steering and pedals, their rates and the precision share, then the mouse and the deadzones); see [Keyboard](#keyboard). **Key bindings:** the keyboard bindings, by their function in the game's DEFAULT LAYOUT 1 (the runtime's `keybind_*` settings). A waits for a key, which replaces the keys; several keys are typed in the field at the right, separated by commas, with modifiers written as `Shift+Up`. Unknown key names are marked and block Play. A connected controller needs no setup. |
| Game data | Two tabs. **Discs:** Disc 1 and Disc 2, checked as `generate` checks them, and the texture pack (the `-TextureReplace` folder). A opens the folder picker, X clears Disc 2 or the texture pack, and the right panel shows the check's result and a field to type the folder. **Content:** rows that add downloadable content folders and files, the folders and files searched (A removes one from the list), and the content packages found, with their import state; A on an imported package removes the import from the user data after a confirmation (see [Data directories](#data-directories)). The right panel shows the package's path and size and what the next Play imports. |
| Shaders | Rows for the shader pass's state, **Compile shaders**, **Skip** and the DirectX Shader Compiler (A fetches it when it is not beside the game), with the pass's progress at the right; see [Shaders page](#shaders-page). |
| Advanced | The **General** tab: the user data folder (A opens the folder picker, X goes back to the default), rows that open the logs and the user data, and **Show developer options**, which adds a **Developer** tab. |
| About | Motorsport DX and its build date, with the non-affiliation line at the right; a row for each project that made it possible (the ReXGlue SDK, Xenia, XenonRecomp and the others of [CREDITS.txt](../outputs/FM4-build/CREDITS.txt)), which says at the right what it gave; and rows that open `CREDITS.txt` and the `third-party-notices` folder beside the launcher. |

**Show developer options** (Advanced, saved in `launcher-settings.json`, never part of a launch
plan) adds the options for comparisons and bug reports:
- on Display's Extras tab, the race rendering (one tile with TAA, or the MSAA tiles of `-Msaa`),
  and the launch plan's description in the right panel;
- on Look and Controls, the `-Look`, `-Pad`, `-Keyboard` and `-Wheel` values, shown and editable;
- on Advanced's Developer tab: fixed vblank, the shadow workaround, `-SingleTile`, `--ao`, `-Rov`, the render
  path (automatic, which is the native path; RTV, `-Rtv`; or the native path, `-Native`), the
  native path's MSAA (one pass with 2x, the default, one pass with 4x as `-MsaaSamples 4`, or one
  tile without MSAA as `-MsaaSamples 0`), the performance trace, extra cvars, the texture dump
  folder, and a button that copies the command line.

A **profile** is one set of run options; the Discs, content, saved looks and key
bindings are shared by all profiles. The launcher saves its settings in
`launcher-settings.json` beside `launcher.json` when Play is pressed and when it closes.
In a BuildRoot it starts with the Disc and content paths of `local.json`.

Two developer options of the Advanced page apply at Play, outside the launch plan:
- **Performance trace** writes the perf trace (one JSON line a second, [traces](e2e/traces.md#performance-trace-rex_perf_trace_file)) to `logs\perf-<profile>-<time>.jsonl`. It carries the process's CPU time and its threads' (`process_cpu`), so a run without the e2e harness still shows where the CPU goes.
- **Extra cvars** are runtime settings written as `--name=value`, separated by spaces, and applied after all the others: for A/B runs such as `--guest_ring_wait_sleep=false`.

When Play is pressed, the launcher checks the Discs and content, the key bindings and
the options, compares the build artifacts with `build-result.json` once per session,
writes `content-packages.txt`, and starts `game-build\fm4_rex.exe` with the logs in
`logs\play.log` and `logs\runtime.log`, as `run` does. It hides while the game runs
and shows again when the game exits. If the game exits with an error, a banner says what
happened in words ("The game crashed after 2 min 14 s (a memory access violation,
0xC0000005)"), with buttons that open both logs.

Not in the launcher: `-ShaderDev`, `warmcache`, the build flavors and the e2e
harness, which need the repository. The look mods and post passes that the launcher
uses are the copies in `BuildRoot\look` and `BuildRoot\passes` that `build` makes;
after editing `work/fm4-rex/look` or `work/fm4-rex/passes`, run `build` or use
`Build.ps1 -Action run`.

The launch rules stay in [pipeline.py](../outputs/FM4-build/pipeline.py): `build`
writes their tables (the runtime settings, the look presets and knobs, the quality
presets, the releases and the content package checks) into `BuildRoot\launcher.json`,
and the launcher's code ([work/fm4-launcher](../work/fm4-launcher)) is a port of
`launch_plan`. Every `build` compares the two: the launcher tests compute the cvars,
the environment and the display description of 59 sets of options (RTV, `-Rov` and
`-Native`) on nine monitors with both (531 plans), and check the Discs and content packages
of `local.json` with both. A difference fails the build.

The launcher was `fm4_launcher.exe` until 26 October 2026. `build` and `export` now remove that
old file from `game-build` and from an earlier export, so a shortcut to it stops working
instead of starting a launcher older than its game: point the shortcut at `MotorsportDX.exe`
in the same folder. The settings stay, because the launcher finds `launcher.json` and
`launcher-settings.json` by its folder, not by its name.

### Launcher screenshots

For reviews of the launcher's layout, `MotorsportDX.exe --screenshot <page> <width>x<height>
<scale> <out.png>` renders one page without a window and exits. The pages are `home`,
`display`, `look`, `controls`, `game-data`, `shaders`, `advanced` and `about`; the scale is the
DPI scale (1 for a 100% screen, 1.5 for the ROG Xbox Ally X's default). It renders with WARP,
the CPU's Direct3D 11, so it never touches the GPU, and it neither reads nor writes
`launcher-settings.json`: it starts from the default settings and the Discs of `local.json`.
It draws no backdrop unless one is given. Options:
- `--settings <file>` reads that `launcher-settings.json` instead.
- `--state first-run` starts without Discs and content; `--state error-banner` shows the banner
  of a game that exited with an error; `--state load-error` shows the error of a launcher
  without its game; `--state developer` turns on the developer options.
- `--pad <button>,<button>...` presses gamepad buttons one after another before the capture
  (`up`, `down`, `left`, `right`, `a`, `b`, `x`, `y`, `lb`, `rb`, `start`, `back`), to check
  the pad navigation.
- `--touch` uses the touch screen's layout.
- `--backdrop <picture>` draws that picture behind the pages.

### Shaders page

On the native path the game's shaders compile with Microsoft's DirectX Shader Compiler (DXC)
when its two DLLs are beside the game (`native_shader_dxil`, on by default), and with FXC
otherwise. The launcher's Shaders page runs the offline shader pass
([D3D-level HLE, 19.3.9](analysis/d3d-hle-n6.md#1939-increment-12-design-28-october-2026) and
[19.3.10](analysis/d3d-hle-n6.md#19310-increment-13-the-games-pixel-shaders-from-the-disc-28-october-2026)).
The game's executable starts with the active profile's settings and `shader_offline_pass=true`.
It reads the game's pixel shaders from the discs and the installed content, translates and
compiles the shader storage, and exits before the title runs, so the shaders are not compiled
during a race.

- **When it runs:** the launcher starts the pass by itself once for each build: when the game's
  executable, or whether the DirectX Shader Compiler is beside it, differs from the last pass that
  finished (`logs\shader-pass.done`).
- **Skip** ends it. What is left compiles on worker threads while the game plays, with the game's
  own look until each shader's mod is ready.
- **Progress:** in `logs\shader-pass.jsonl`; the pass's log is in `logs\shader-pass.log`.
- **The DirectX Shader Compiler:** when `dxcompiler.dll` and `dxil.dll` are not beside the game,
  A on the page's **DirectX Shader Compiler** row fetches Microsoft's release v1.9.2609 from GitHub (`FM4_DXC_MIRROR`: a local copy of
  the zip, or another URL) and checks its pinned SHA-256. It keeps the zip in `downloads\dxc` and
  puts the two DLLs and the release's license files beside the game. Without them the shaders
  compile with FXC.

### Ally X run set

The runs of stage N4 on the ROG Xbox Ally X ([D3D-level HLE, 17.19](analysis/d3d-hle-n4.md)), from an export, with the GUI launcher. Turn on **Show developer options** on the Advanced page first: the performance trace, extra cvars, the render path and the native path's MSAA are among them. Make one profile per run, all with **Performance trace** on, the default look, the race frame rate the screen allows (120 on the Ally X's panel), and otherwise the defaults.

Runs 1 to 6 use render scale 1x (Display page). On the Ally X's 1080p panel the launcher of `de4e9bd` picked 2x, and its GPU then takes about 21 ms a race frame (about 47 fps, N1's estimate), which would hide what the CPU A/B runs measure; later launchers pick 1x there (integrated GPUs only, [-Display](#-display-and-render-scale)):

| Run | Profile settings |
| --- | --- |
| 1 | RTV, render scale 1x |
| 2 | Native path, render scale 1x |
| 3 | Native path, render scale 1x, extra cvars `--guest_ring_wait_sleep=false` |
| 4 | Native path, render scale 1x, extra cvars `--high_resolution_timing=false` |
| 5 | RTV, render scale 1x, extra cvars `--guest_ring_wait_sleep=false` (if time allows) |
| 6 | Native path, Native MSAA "One pass with 4x MSAA" (`-MsaaSamples 4`), render scale 1x |
| 7 | Native path at the default render scale, 2x (optional: the GPU-bound frame rate) |

- Run each as a Quick Race with the default car and track: let the race run for at least three minutes, then quit to the menu and close the game, so the trace covers the race and its end.
- Run the set once on AC power and once on battery: a handheld changes its power plan on battery, and that is where Windows parks cores and throttles processes.
- Send back only these files from the export's `logs` folder: the `perf-<profile>-<time>.jsonl` files (timings, draw counts and the CPU time per thread), `play.log` and `runtime.log` (the game's log). Note for each trace whether it ran on AC or battery. The logs show the paths of the export and of the Discs, which may include the Windows user name; nothing from `user-data` (the profile, the saves and the imported content) is needed or included.

### Export to another computer

`export` copies what the launcher needs into a folder outside the BuildRoot and the
repository:

```powershell
.\outputs\FM4-build\Build.ps1 -Action export -BuildRoot 'D:\FM4-build' -LLVMRoot 'C:\Program Files\LLVM' -Python 'C:\Python311\python.exe' -Out 'E:\FM4-portable'
```

The folder holds `MotorsportDX.exe`, `game-build` (the executable, the two modules and
the runtime DLLs of `build-result.json`), `look`, `passes`, the shader cache (`cache`),
`launcher.json`, `build-result.json`, `CREDITS.txt` (the projects that made it possible), the
license notices of the third-party code in the binaries (`third-party-notices`), the
launcher's backdrop when the BuildRoot has one and the folder has none (a local picture, see
[GUI launcher](#gui-launcher)), and a first `launcher-settings.json` with the
Disc and content paths of `local.json`. It does not hold `user-data`: profiles, saves
and imported content stay on the build computer. `export` checks the artifact hashes
first, and refuses a folder that is not empty unless it is an earlier export, which it
updates and whose `launcher-settings.json` it keeps.

On the other computer:

1. Install the Microsoft Visual C++ 2015-2022 Redistributable (x64), recent enough
   to have `MSVCP140_ATOMIC_WAIT.dll`. The GPU must support Direct3D 12.
2. Copy the export folder and your extracted discs to it.
3. Start `MotorsportDX.exe`, set Disc 1, Disc 2 and the content on the Game data page,
   and press Play. The first Play imports Disc 2 and the content packages into
   `user-data` in the export folder; it needs free space of about the size of the
   packages.

The export contains executables built from your discs, and its shader cache holds shaders
translated from the game. Keep it for your own computers; do not publish it or give it to
others ([distribution](sources-and-licensing.md#distribution)). Its `third-party-notices`
folder carries the license texts of the third-party code in the binaries.

## Launcher defaults

Running with no options gives this configuration:

- **Shader compilation** is synchronous (`async_shader_compilation=false`).
- **Anisotropic filtering** is 16x for every mipmapped linear texture
  (`anisotropic_override=5`). The title asks for none on most textures and 2x to 16x
  on a few; the runtime default is 4x.
- **Rear-view mirror** is rendered every other frame in every race, as the title does
  for 12- and 16-car races. One- and eight-car races render it every frame. See
  [-FullRateMirror](#-fullratemirror).
- **Shadows** use the title's shadow map for every camera. See
  [-ShadowWorkaround](#-shadowworkaround).
- **Race quality** is `high`, rendered in one pass with 2x MSAA on the native path, or in one
  tile without MSAA on RTV because the look has TAA. See [-Quality](#-quality),
  [-SingleTile and -Msaa](#-singletile-and--msaa) and [-Native](#-native).
- **Look** is the `aces` preset (`pc` is its earlier name), with screen-space ambient occlusion,
  temporal anti-aliasing, the cockpit interior's sky fill, the cockpit glass, the road sheen
  and the glass, chrome and rim mods. See [Look](#look).
- **Display** is `native`: borderless fullscreen on the primary monitor, rendered at the
  screen's own resolution and aspect (1080p at 3/2, 1440p at 2, 4K at 3, a 2560x1600 screen
  at 2 with more above and below). On a computer with integrated GPUs only the render scale
  is 1x and the picture 16:9. See [-Display](#-display-and-render-scale).
- **Render path** is the native path ([-Native](#-native)), with the frame pacing and memory
  settings described in [Render path and frame pacing](#render-path-and-frame-pacing).
  `-Rtv` runs RTV, the PM4 path on host render targets, and `-Rov` the uncorrected reference;
  `-Msaa` runs RTV too.

Performance on the defaults: Quick Race holds 60 fps on the development computer.
The GPU time per race frame with `high` quality in its three 4x MSAA tiles and the `pc` look
with ambient occlusion was about 3.0 ms at 1x (1280x720) and about 6.1 ms at 2x, measured
before `pc` had TAA, which adds about 0.1 ms at 1x and 0.15 ms at 2x. With TAA the one tile
cost about 1 ms less GPU time at 2x (19 October 2026). At 1x and 240 Hz the one tile takes
0.5 to 0.6 ms less GPU time and a fifth to a quarter fewer draws per race frame than the
tiles: races ran at 230.7 to 236.9 fps against 200.5 to 218.1 fps with `-Msaa` (20 October
2026, [current status](status/current.md#one-tile-with-taa-the-presets-and-taas-history-20-october-2026)).
On the native default at 240 Hz (26 October 2026, [native default](analysis/native-default.md#23-performance)),
a 2560x1600 screen's race (2560x1600 with 2x MSAA) ran at 171.6 to 175.1 fps with 5.60 to
5.70 ms of GPU time a frame, against 162.7 to 165.9 fps and 5.81 to 6.01 ms on RTV at 2x
(2560x1440, letterboxed).

### Render path and frame pacing

The launcher uses these runtime settings (set in `launch_cvars` in
[pipeline.py](../outputs/FM4-build/pipeline.py)):

- Rendering uses the host render target path `render_target_path_d3d12=rtv` with
  `edram_single_rt_no_wrap=true`. Earlier measurements put the GPU time at about 2.5 ms
  per race frame on this path, against about 7 ms on the pixel shader interlock path
  (`-Rov`). Quick Race runs at a stable 60 fps.
- Guest memory that the CPU wrote is uploaded to the GPU once, not every frame
  (`clear_memory_page_state=false`).
- Command lists are recorded and executed on a thread of their own
  (`d3d12_submit_thread=true`), and the swap does not wait for that thread
  (`d3d12_submit_thread_async_swap=true`). The copies of resolve readbacks into guest memory run
  on their own thread too (`d3d12_readback_publish_thread=true`).
- Occlusion reports are published at once from the previous frame's samples
  (`rov_occlusion_async=previous`). Resolve readbacks do not wait for the GPU
  (`readback_resolve=fast`).
- A frame that misses a guest vblank gets it as soon as it is ready
  (`vblank_vrr=true`). See [-FixedVblank](#-fixedvblank).
- The title's ring index wait sleeps until the command processor publishes the read pointer
  again instead of polling it on a whole core (`guest_ring_wait_sleep=true`, RTV and `-Native`;
  [section 17.16](analysis/d3d-hle-n4.md)). `-Rov`, the unchanged reference, keeps the title's
  wait.
- The runtime asks for 1 ms timer resolution and opts the process out of power throttling
  (`high_resolution_timing=true`, a runtime default on every path, `-Rov` included, since it is a
  host setting and not a title correction; [section 17.19](analysis/d3d-hle-n4.md)): the guest's
  1 ms sleeps keep their length, also where Windows would throttle the process.
- The road sections that this path used to leave undrawn came from the title's track
  streaming. They are fixed for every render path.

Known limits of this path: the sun glare has not been calibrated on it (see
[status/current.md](status/current.md#open-issues)). Interactive Autovista has not
been tested on it. If rendering misbehaves, compare with `-Rov`. Which scenarios have passed on this path is recorded
in [status/current.md](status/current.md).

## Options

Pass the options to `Build.ps1 -Action run`. Each option is a switch unless it
takes a value.

| Option | Default | Description |
| --- | --- | --- |
| `-FrameRate 30-240` | 60 | Race frame rate, also for replays and the 3-2-1 countdown. Values above 60 lift the race limit; menus, post-race and photo mode keep their own timing. |
| `-VSync auto\|on\|off` | auto | How the host presents: see [-VSync](#-vsync). |
| `-Display native\|720p\|1080p\|1440p\|4k` | `native` | Screen and render size. `native` is borderless fullscreen on the primary monitor. Other values are a window of that size. |
| `-Fullscreen` | off | Fullscreen with the `-Display` size and its render scale. |
| `-Supersample` | off | Render one scale above the screen (up to 4x), then average the result down to the screen. |
| `-ResolutionScale 1-4\|panel` | from `-Display` | Render at this multiple of 1280x720, or `panel`: the screen's own resolution (the native path in one pass only). Overrides the render scale of `-Display`. |
| `-Upscale native\|quality\|balanced\|performance\|<ratio>` | `native` | With `-Native` and `aa=fsr` or `aa=taau`: FSR 3.1 or TAAU renders the scene at fewer lines and upscales it to the render scale. See [-Upscale](#-upscale). |
| `-Entry0 s\|k` | `s` | With `-Native` in one pass with 2x MSAA: the front-end menus, Autovista, the career home and photo mode (the title's tiling entry 0) at the render scale with 4x MSAA (`s`), so they fill the screen at its resolution, and the menus, Autovista and the career home run the scene passes (GTAO) at every scale; the saved photo stays 1280x720. `k` renders them at the whole scale below and scales them to the screen, as before 26 October 2026: the fallback for integrated GPUs (at 3/2 the menus take about 0.7 to 1.5 ms of GPU time more with `s` on this PC). The GUI launcher's "Menus at the render scale". See [D3D-level HLE, 18.26](analysis/d3d-hle-n5.md#1826-entry-0-at-s-with-photo-mode-in-its-three-tiles-26-october-2026). |
| `-Quality low\|medium\|high\|extrahigh\|ultra` | `high` | Detail of races in five tiers (`xbox` is `medium`). See [-Quality](#-quality). |
| `-Look <preset/knobs>` | `aces` | Post-processing look and car paint. See [Look](#look). |
| `-Pad <knobs>` | `rumble=1.5` | The physical controller: the SDL pad driver, rumble strength, trigger rumble, the input thread, stick and trigger shaping and the extra buttons. See [Controller](#controller). |
| `-Keyboard <knobs>` | none | The keyboard's steering and pedal model: its rates, the precision share and mouse steering. See [Keyboard](#keyboard). |
| `-ShaderDev` | off | Shader mod development: the look mods reload when a file in `work/fm4-rex/look` is saved, and the knobs change live. See [Shader mod development](#shader-mod-development). |
| `-TextureDump <dir>` | off | Write each new game texture once as `<hash>.dds` to this directory. See [Texture replacement](#texture-replacement). |
| `-TextureReplace <dir>` | off | Use `<hash>.dds` files from this directory instead of the game textures with that hash. See [Texture replacement](#texture-replacement). |
| `-Info` | off | Show the settings overlay at start. F2 toggles it in any run. |
| `-Rov` | off | Use the earlier pixel shader interlock render target path, for comparisons. |
| `-Native` | on, unless `-Rtv`, `-Rov` or `-Msaa` | The native path: a host render thread consumes the guest's commands from a record queue instead of the command processor thread, and races render in one pass with 2x MSAA. See [-Native](#-native). |
| `-Rtv` | off | RTV, the PM4 path on host render targets, instead of the native path: the reference beside `-Rov`. |
| `-SingleTile` | with TAA | Render races as one 720p tile without MSAA also with a look without TAA, then with FXAA on the swap. A look with TAA (every preset) renders one tile anyway. |
| `-Msaa` | off | Keep the tiles and MSAA of `-Quality` with a look that has TAA. |
| `-MsaaSamples 4` or `0` | 2 on the native path | On the native path: 4 renders races in one pass with 4x MSAA, 0 in one tile without MSAA, instead of one pass with 2x MSAA. See [-Native](#-native). |
| `-FixedVblank` | off | Hold every swap to the fixed 60 Hz vblank grid. |
| `-FullRateMirror` | off | Render the rear-view mirror in every frame of every race. |
| `-ShadowWorkaround` | off | Render the shadow map only with the cockpit camera, as the old Xenia workaround did. |

`-Action warmcache` is not a run option; it is described in
[Shader cache](#shader-cache). The `--ao` flag of `pipeline.py` and `e2e.py` has no
switch in `Build.ps1`. It runs the GTAO pass without a look applying it; see
[Look](#look).

### Option details

#### -FrameRate

`-FrameRate 120` lifts the 60 fps limit of races. The title sees a 120 Hz display,
the `FrameRate` of its race render scenarios becomes 120, and the title scales its
time step (race clock against wall time 1.001). The guest vblank stays at 60 Hz in
menus, post-race and photo mode (30 fps, as on the Xbox 360). It runs at 120 Hz only
while a race scenario sets the frame rate (`fm4-vblank-rate.cpp`).

Since 20 October 2026 replays and the 3-2-1 countdown run at the race frame rate too
(`FM4_FULL_RATE=replay,countdown` in the launcher environment), instead of the title's
30: 60 fps without `-FrameRate`, 240 at `-FrameRate 240`. The replay scenarios get the
race rate in their file; the countdown shares its scenario with the post-race, so its
rate is set when the countdown is pushed after the pre-race, and put back for the
post-race. The title presents both every second vblank whatever their frame rate, so
while one of them runs the guest vblank goes to twice the race rate, at most 240 Hz:
60 fps at the default (vblank 120 Hz), up to 120 fps at `-FrameRate 120` or more. The
replay and countdown clocks follow wall time at every rate (replay clock against wall
time 1.00 at 30, 60 and 240), so only the motion gets smoother. The post-race screens
and photo mode keep 30.

Measured on 20 October 2026 (`career` scenario, Tsukuba, AI driver): at 1x the replay ran
at 59.7 fps by default and 94.7 to 101.6 fps at 240 Hz (30 before), the countdown at 60 and
90 to 115 fps (30 before); at 2x the replay ran at 59.2 and 94.4 fps.

Measured with Quick Race, three runs: 118 to 120 fps, frame p99 8.8 to 9.8 ms, and
every frame fully rendered. At 240, races run at 220 to 235 fps (p99 5.3 to 6.2 ms).
At 240 the command processor sets the limit: it needs about 4.3 ms per race frame,
against about 2.2 ms of GPU time. Values from 30 to 240 are accepted.

The title's auto exposure adapts by a fixed share per frame, so above 60 fps it settles
faster than on the Xbox 360; the presets' look knob `adaptation=1` keeps the game's speed
([Exposure adaptation](#exposure-adaptation)).

#### -VSync

`auto` (the default): on a screen with a variable refresh rate range up to its refresh (read
from its EDID: DisplayID's Adaptive-Sync block, or EDID 1.4's range limits with continuous
frequency), frames are presented as they come with tearing allowed, but at most one a refresh
(`present_min_interval_us`), and races run 3 fps under the refresh (117 at 120 Hz, 237 at 240
Hz), inside the range. On another screen it is `on`. The launcher prints which ("VSync auto:
VRR 48-120 Hz, ...", or "VSync on (no VRR range)").

`on` presents on the screen's refresh without tearing (`present_vsync`). `off` presents at
once with tearing allowed, the runtime's default, which `e2e.py` keeps. Below the range's
floor the presenter still presents each frame once; low frame rate compensation is the
driver's. Measured on 24 October 2026: [D3D-level HLE, 17.23](analysis/d3d-hle-n4.md#1723-vsync-24-october-2026).

#### -Display and render scale

`native` (the default) is borderless fullscreen on the primary monitor. The other
sizes (`720p`, `1080p`, `1440p`, `4k`) are a window of that many pixels. They go
fullscreen with `-Fullscreen`, or when they do not fit the monitor. Window sizes take
the Windows display scale into account.

On RTV (`-Rtv`), `-Rov` and the native path's modes other than one pass with 2x MSAA, the
render scale is the smallest whole multiple of 1280x720 that covers the 16:9 picture of the
screen: 720p 1x, 1080p and 1440p 2x, 2560x1600 2x, and 4k 3x. `-Display
720p` was the earlier default (1x in a 1280x720 window). On a computer with integrated GPUs only
(every GPU shares the system memory, as on the ROG Xbox Ally X) it is 1x on every screen: the
Ally X's GPU takes 23.9 to 27.2 ms a race frame at 2x and races at 35 to 39 fps
([D3D-level HLE, 17.21](analysis/d3d-hle-n4.md#1721-the-rog-xbox-ally-x-runs-24-october-2026)).
The launcher prints "for an integrated GPU" after the render scale then; `-ResolutionScale`
and `-Supersample` (the render scale and supersample on the GUI launcher's Display page)
still choose another. If the render is larger than
the window, the presenter averages it over each screen pixel in linear light
(`work/runtime/src/ui/shaders/guest_output_area.ps.hlsl`; `present_area_downscale=false`
selects the earlier single bilinear tap). So 2x on a 1080p screen is supersampling,
not a shrink that skips texels.

On the native path in one pass with 2x MSAA (the default), the scene renders at the screen's
own resolution instead: the largest 16:9
picture that fits the screen, at a fractional scale of 1280x720 where it needs one
(1080p at 3/2, 1920x1080; 1440p at 2; 2160p at 3; 2560x1600 at 2, letterboxed; 2880x1800
at 9/4, 2880x1620, letterboxed). The launcher sets `native_draw_scale` and the
`native_rt_surfaces` the fraction needs together ([D3D-level HLE,
18.19](analysis/d3d-hle-n5.md#1819-increment-3-the-screens-own-resolution-in-the-launcher-25-october-2026)).
A whole scale of 2 or more takes the same path (`native_draw_scale` at that scale with
`native_draw_scale_variant`), so the resolves write the scene's textures straight from the
render target; 1x keeps loading them from the guest's memory
([D3D-level HLE, 19.7.6](analysis/d3d-hle-n6.md#1976-step-c-bit-32-at-the-whole-scales-28-october-2026)).
A screen of another aspect than 16:9 renders its own aspect: wider than 16:9 with more on
each side (Hor+, 720 lines and a wider logical width: 3440x1440 renders 1720x720 at 2),
narrower with more above and below (Vert+, 1280 wide and a taller logical height: 2560x1600
renders 1280x800 at 2, 1920x1200 at 3/2); the HUD and the menus stay in the centred 16:9
([D3D-level HLE, 18.23](analysis/d3d-hle-n5.md#1823-increment-2-step-3-the-launchers-logical-size-25-october-2026)).
Where the logical size, rounded to a multiple of 8, misses the screen by a few pixels
(2560x1080 renders 2556x1080), the presenter pillarboxes or letterboxes them, and the
description says so. Integrated GPUs keep
1x automatically; `-ResolutionScale panel` (the GUI's "Screen's own resolution") picks
the screen's own resolution on them. `-Supersample` on the native path renders at the
whole part of the screen's scale plus one. Photo mode stays at the whole scale and is
scaled to the screen. One pass with 4x MSAA keeps the whole multiple until it is tested at
a fractional scale.

`-Supersample` renders one scale above the screen, up to 4x (5120x2880), and averages
it down. Edges are smoother and thin objects are more stable, at the GPU cost of that
scale.

`-ResolutionScale 2` renders at 2560x1440 and `-ResolutionScale 3` at 3840x2160,
instead of the size from `-Display` (`draw_resolution_scale_x` and
`draw_resolution_scale_y`; CPU readbacks of resolves are downscaled). Measured with
Quick Race: 59.7 fps with about 3.7 ms of GPU time per frame at 2x, and 59.9 fps with
about 6.8 ms at 3x. The HUD and the scene are sharp. The smoke gate at 2x (240 Hz)
passes: races run at 202 to 206 fps, photo mode at 30 fps (GPU 7.5 ms), a photo can be
saved and loaded, and a career race with its replay runs at 30 fps (GPU 8.2 ms). The
replay used to drop to 23.5 fps after a long frame; see `-FixedVblank`. These numbers
predate `-Quality high` and the clear coat.

Alt+Enter switches between borderless fullscreen and the window in any run
(`bind_fullscreen`). The render scale stays the same.

#### -Upscale

`-Upscale quality`, `balanced` or `performance` renders the race scene at fewer lines and
lets AMD FSR 3.1 upscale it to the render scale ([FSR plan, 11](analysis/fsr-plan.md#11-stage-3-upscaling-on-n5s-scales-25-october-2026)).
The scene renders at the output's lines divided by 1.5, 1.7 or 2 (a number such as
`-Upscale 1.25` picks another ratio from 1 to 3), at least 720 lines; the shadow maps,
the rear-view mirror and the other surfaces render at the whole part of that input scale,
and the HUD, the title's post-processing and the menus' text stay at the render scale:

| Output | `native` | `quality` | `balanced` | `performance` |
| --- | --- | --- | --- | --- |
| 1080p | 1080 | 720 | 720 (clamped to 720p) | 720 (clamped to 720p) |
| 1440p | 1440 | 960 | 847 | 720 |
| 2160p | 2160 | 1440 | 1271 | 1080 |

The launch description names the lines ("FSR quality 960p -> 1440p", with "(clamped to
720p)" where the input was raised to 720), and the GUI launcher shows them beside its
Upscale choice. At a 720p output every mode renders natively. On a computer with
integrated GPUs only, an upscale mode takes the screen's own resolution as its output, so
the ROG Xbox Ally X renders 720p and upscales it to 1080p, where it otherwise renders 1x.
`-Upscale` needs the native path in one pass with 2x MSAA (the default) and an upscaler as the anti-aliasing method: FSR 3.1
(`-Look "aces,aa=fsr"`, a runtime built with `-Fsr on`) or the project's TAA as the
upscaler (`-Look "aces,aa=taau"`, the description then says "TAAU quality 720p -> 1080p");
both launchers refuse it with `aa=taa` ("-Upscale quality needs aa=fsr or aa=taau in -Look:
TAA does not upscale"). TAAU costs about half of FSR's GPU time (0.25 against 0.43 ms a
race frame at 1080p from 720p on this PC) and keeps the still picture as sharp; in motion
its textures are a little softer than FSR's at `quality`, and at 1.25 (864 to 1080 lines)
it is close to native in motion and in still pictures ([FSR plan, 14](analysis/fsr-plan.md#14-taau-the-projects-taa-as-the-upscaler-26-october-2026)). `native`, the default, renders at the render
scale as before. Photo mode and the menus render at the whole part of the input scale and
are scaled to the screen. FSR sharpens an upscaled picture more than a native one: the
look's `sharpen` plus 0.4 times the ratio above 1 (0.4 becomes 0.5 at 1.25, 0.6 at
`quality`, 0.68 at `balanced`, 0.8 at `performance`), which brings it close to native
rendering's crispness; the look knob `fsr_sharpen` sets it instead, and the `-Info` overlay
shows the strength in use. The scene's texture mips are those of the output (a LOD bias of
log2 of the input over the output scale).

#### -Quality

Sets the detail of races in five tiers: `low`, `medium`, `high` (the default), `extrahigh`
and `ultra` ("Low" to "Ultra" in the GUI launcher). `xbox`, the name of the title's own
settings until 26 October 2026, is an alias of `medium`. The tiers set the race render
scenarios (`FM4_RACE_SETTINGS`, `fm4-render-overrides.cpp`; the N-Car, Autocross and Bowling
scenarios, not split screen), the quality of the look's ambient occlusion (GTAO) and, for
`ultra`, the MSAA of the native path. Anisotropic filtering is 16x in every tier. The render
scale, `-Upscale` and the look are separate options.

| Tier | Race render scenarios | GTAO | MSAA on the native path |
|---|---|---|---|
| `low` | The title's own with soft particles, at most 3 cars in the rear-view mirror and no collidable objects there | low (2x2) | 2x |
| `medium` (`xbox`) | The title's own with soft particles | medium (2x4) | 2x |
| `high` | What the title gives its replays and photo mode: soft particles, shadows of collidable objects, full-detail wheels, car LOD transitions at twice the distance (30/50/100 m), and none of the cuts the title makes for 16-car races (the cheaper car shader, no damage, an environment map that is never updated, half-rate bloom, no collidables in the mirror) | high (3x6) | 2x |
| `extrahigh` | `high`, and every car at its top LOD (`CarKeepLOD0`, as 1-car races do) | ultra (4x8) | 2x |
| `ultra` | `extrahigh`, and the mirror every frame with up to 11 cars | ultra (4x8) | 4x where the native plan is a whole scale at 16:9 without `-Upscale` (for example 1440p and 4K), otherwise 2x |

Every tier has soft particles because without them the title resolves no scene depth in
races, so the look's scene passes (GTAO, TAA, motion vectors, the windshield) do not run
there: until 26 October 2026 `xbox` races ran without them, on RTV and on the native path.
`ultra`'s 4x MSAA has no fractional scale and no logical size yet, so where the plan has one
(1080p at 3/2, a 16:10 screen in Vert+, `-Supersample` at 1080p) or upscales, `ultra` keeps
2x; the plan description and the settings overlay say which one runs ("Ultra: 4x MSAA").
`-MsaaSamples` overrides it.

On a discrete GPU the tiers differ little in GPU time: at 2x (2560x1440) on the native path
and 240 Hz, on the development laptop's RTX 5070 Ti Laptop GPU, a 12-car race frame takes
4.5 ms on `low`, 4.7 on `medium`, 5.0 on `high`, 5.3 on `extrahigh` and 6.1 ms on `ultra`. The render scale and `-Upscale` change it far
more. Most of the difference is the GTAO quality (0.31 to 1.04 ms) and `ultra`'s mirror (half
again as many draws and 1.5 ms more time on the render thread). Details:
[quality presets](analysis/quality-presets.md).

With a look that has TAA (every preset, since 20 October 2026) races on RTV render one tile
without MSAA, and on the native path one pass with 2x MSAA (4x at `ultra` where it applies);
the other settings of the tier stay. `-Msaa` keeps the title's tiles: two with 2x MSAA at
`low` and `medium`, three with 4x MSAA (`TilingScenario` 0) from `high` up, and so do `-Rov`
and a look without TAA (see [-SingleTile and -Msaa](#-singletile-and--msaa)).

#### -Info

Shows the settings overlay at the top left from the start. It lists the display and
render scale, the race quality, the look and its knobs, the ambient occlusion, the
anti-aliasing (TAA and its point and sharpening), the shadows, the anisotropic filtering, the race frame rate and pacing, the mirror rate,
the render target path, and the window size and present rate, as the launcher set them
up (`info_overlay` in `pipeline.py`, `FM4_INFO`; `work/fm4-rex/src/fm4-info-overlay.h`).
The presenter draws the overlay over the guest output, so captures never show it.
F2 toggles it in any run.

#### -Rov

Uses the earlier pixel shader interlock render target path
(`render_target_path_d3d12=rov`, `rov_occlusion_async=all`) for comparisons. Measured
with Quick Race: 60.0 fps, with about 7.1 ms of GPU time per frame (2.5 ms on the
default path). Its occlusion reports are the counted guest samples, without a title
correction (since 18 October 2026 the launcher no longer passes
`rov_occlusion_awaited_report_divisor=2`). Use it as the reference when rendering
misbehaves on the default path.

Shader mods and ambient occlusion are RTV only, by design: with `-Rov` the runtime
turns off the shader overrides ("Shader overrides need bindless resources and host
render targets" in the log), so every look mod and the AO are gone and the picture is
the title's own. Compare the default path against `-Rov` with `-Look original`.

#### -Native

The native path is the default, unless `-Rov` or `-Msaa` is given; `-Rtv` (the GUI launcher's render path "RTV") runs RTV, which stays the reference beside `-Rov`. It became the default with integrated GPUs only on 24 October 2026, where on the ROG Xbox Ally X it races at 110.5 fps on battery against RTV's 100.1, with p99 15.3 against 19.8 ms ([D3D-level HLE, 17.25](analysis/d3d-hle-n4.md#1725-the-second-rog-xbox-ally-x-run-24-october-2026)), and on every computer after the readiness report of 26 October 2026 ([native default](analysis/native-default.md)): on the development laptop's 2560x1600 panel it fills the screen and takes 4% less GPU time a race frame than RTV's letterboxed 2x. The reference comparison (RTV against `-Rov` with `-Look "original,taa=0"`) needs `-Rtv`, since a look without TAA runs the native path in one pass with 2x MSAA.

Runs the first increment of stage N1 of the native port
([D3D-level HLE, section 14](analysis/d3d-hle.md#14-stage-n1-design-21-october-2026)):
`host_render_thread=true`. A host render thread ("GPU Host Render") takes the
place of the command processor thread ("GPU Commands"). The guest's kicks of the
ring write pointer become records in a bounded queue, and the host render thread
executes them in order, waiting on the queue instead of spinning. Every PM4 writer
of the title is still on the fallback, so the PM4 is the same as on the default
path, and the render targets stay on RTV.

- Races render in one pass with 2x MSAA (stage N3, increment 4,
  [section 16.17](analysis/d3d-hle-n3.md)). The title's 2x tiling scenario becomes one 1280x720
  tile (`FM4_ONE_PASS_MSAA=2`), the scene depth gets a private host render target
  (`native_one_pass_msaa=2`), and the host render targets are sized to the tiles the title
  uses, and every 1280-wide race surface gets a private host target (`native_rt_surfaces=26`,
  [D3D-level HLE, 18.11](analysis/d3d-hle-n5.md)). TAA and the post passes read the title's resolves of the
  multisampled scene. Photo mode, replays and split screen keep their own scenarios.
- `-Native -MsaaSamples 4` renders races in one pass with 4x MSAA: the title's scenario 3
  becomes one 1280x720 4x tile, and its colour and depth get private host targets
  (`native_one_pass_msaa=4`, [section 16.17](analysis/d3d-hle-n3.md)). It is the candidate
  for the ROG Xbox Ally X at 1x, where it costs about as much GPU time as 2x.
- `-Native -MsaaSamples 0` or `-Native -SingleTile` renders races in one tile without MSAA
  instead. With `-Native -Msaa` the tiles of `-Quality` stay on the PM4 RTV path, and the
  launcher says so. `-MsaaSamples` with `-Rtv`, `-Rov` or `-Msaa` is refused; RTV and `-Rov`
  keep the title's tiles.
- One pass uses 2x MSAA also with a look without TAA, such as the comparison look
  `original,taa=0`, where RTV and `-Rov` render the tiles and MSAA of `-Quality` (4x at
  `high`). Compare one pass 2x against RTV and `-Rov` in the title's 2x scenario
  (`FM4_TILING_SCENARIO=1`), as the gates of section 16.17 do; `-MsaaSamples 4` is chosen
  explicitly, never by `-Quality`.
- `-Native` with `-Rov` is refused: `-Rov` stays the uncorrected reference.
- `-Native` also sets `readback_resolve_on_demand=true` (stage N3,
  [section 16.5](analysis/d3d-hle-n3.md)): resolves skip their readback to guest
  memory, and the first access by the CPU to a resolved range waits for the
  readback of the latest resolve to it. On RTV without `-Native` it is off unless
  passed as a cvar; the ROV path never uses it.
- `-Native` also sets `native_rt_surfaces=2`, or 10 in one pass (stage N3,
  [section 16.11](analysis/d3d-hle-n3.md)): a resolve whose source one host render
  target holds reads it directly instead of its dump into the EDRAM buffer. Off on
  RTV without `-Native` unless passed as a cvar; never on the ROV path.
- `-Native` also sets `d3d12_draw_back_end=2`, `texture_bindings_keep_same_dwords=true` and
  `render_target_update_guard=true` (stage N4,
  [sections 17.8 to 17.13](analysis/d3d-hle-n4.md)): the draws' back half (pipeline, fixed
  function state, system constants, samplers, constant buffers, root parameters) is recorded by
  the thread that replays the deferred command list instead of the host render thread, a texture
  binding whose fetch constant is written again with the same dwords keeps its key, and the
  render target cache's update is skipped at draws that would repeat it. The draw back end needs
  bindless resources, host render targets and `async_shader_compilation` off (the launcher's
  setting) and turns itself off otherwise. Off on RTV without `-Native` unless passed as cvars;
  never on the ROV path.
- `host_render_queue_capacity` (default 256) is the queue length at which a new
  span of the ring merges into the last record when that is a span, so the guest
  never waits for the queue. Calls (`CallInThread`) are not bounded.
- The host render thread executes consecutive spans of the ring as one, as the
  command processor thread executes every kick since its last batch.
- `host_render_native=true` (the second increment of the native producers,
  [D3D-level HLE, 14.9](analysis/d3d-hle.md#149-increment-3-design-the-dirty-writers-as-records))
  converts the title's dirty state writers: they leave a three-dword marker and a
  record of their register writes instead of their PM4, and the host render
  thread applies the record at the marker.
- The perf trace adds `render_thread_busy_ms`, `queue_depth_mean`,
  `queue_depth_max` and `host_render` (records and merged spans, and with
  `host_render_native` the records applied, their registers and markers reached
  without a record) to each window.

#### -SingleTile and -Msaa

Races render one 720p tile without MSAA whenever the look has TAA (`taa=1`, every preset since
20 October 2026), on the RTV path, at every `-Quality`. This is the title's own UI tiling
scenario, selected through the render scenario override (`FM4_TILING_SCENARIO=2`; the pre-race,
countdown, race, post-race and replay scenarios of the N-Car, Autocross and Bowling races).
TAA anti-aliases the tile, and its input is then one sample per pixel, with the depth, motion
and mask of the same sample as the color ([TAA, MSAA and tiling](analysis/taa.md#5-msaa-and-tiling)).
Photo mode and split screen keep their own settings.

- `-Msaa` keeps the tiles and MSAA of `-Quality` with TAA (for comparisons).
- `-Rov` and a look without TAA (for example `-Look "original,taa=0"`) keep them too.
- `-SingleTile` renders one tile also with a look without TAA, and then the swap applies FXAA.
  Measured with Quick Race on `-Rov`: 58.6 fps instead of 54.3, frame p95 20 ms instead of
  33 ms, GPU 13 ms instead of 17.5 ms. Without TAA, edges are visibly more jagged than with
  MSAA (2x at `-Quality low` and `medium`, 4x from `high` up): thin cables break up.
- Measured with TAA at 2x and 240 Hz (19 October 2026, one run each): GPU 5.54 ms a race frame
  and 173.3 fps in one tile against 6.50 ms and 149.0 fps in the three 4x MSAA tiles of `high`;
  in 1:1 crops the two look nearly alike.

#### -FixedVblank

Holds every swap to the fixed 60 Hz guest vblank grid (`vblank_vrr=false`). By default
(`vblank_vrr=true`), a frame that misses a vblank gets it as soon as it is ready,
instead of waiting for the next one. Measured with Quick Race on `-Rov`, three runs
interleaved for each setting: the same average fps (52.5 against 52.7), frame p95 of
23 ms instead of 33 ms, frame p99 of 26 ms instead of 35 ms, and unchanged game speed.

Scenarios that render at 30 fps (post-race and photo mode) keep
the fixed grid either way. Their render thread waits for the vblanks itself. With
`vblank_vrr`, one long frame left every later vblank forced or late: the career replay
at `-ResolutionScale 2` stayed at 23 fps instead of 30. The countdown and replays, at the
race rate since 20 October 2026, keep `vblank_vrr`: at 2x the replay recovered at once from
a 165 ms hitch and held 59.2 fps by default.

#### -FullRateMirror

Renders the rear-view mirror in every frame of every race. By default every race renders
it every other frame, as the title does for 12- and 16-car races
(`HalfRateMirror` in the race render scenarios). One- and eight-car races render it in
every frame. Measured with Quick Race (12 cars) and the mirror in every frame, before the
upload change described in [Render path and frame pacing](#render-path-and-frame-pacing):
55.6 fps instead of 59.7, and GPU 9.5 ms instead of 8.1 ms a frame. At `-FrameRate 120`:
75 fps instead of 84.

#### -ShadowWorkaround

Adds `SkipShadowMapUnlessCockpit` to the race render scenarios (`fm4-render-overrides.cpp`),
as the title's split screen does. The shadow map, a depth pass of 70 to 160 draws
resolved to 512x512 plus a 16_16 4x MSAA pass, is then rendered with the cockpit camera
only. Without it, the hood, bumper and chase cameras also get the shadow map. Their car
shadows are sharp, with the wheels apart instead of soft, at a cost of about 0.2 to 0.4
ms of GPU time a frame. Xenia needed this workaround against square shadows. Captures of
every race camera on this runtime show none.

## Look

The look mods are original shader code hooked into the HLSL that the runtime
generates from the game's post-processing shaders, car shaders (body paint,
glass, chrome, rims and the cockpit interior) and track shaders (sky, trees, bushes, grass and the road surface;
`work/fm4-rex/look`; see [Shader overrides](e2e/experiments.md)). `-Look` takes a
preset, knobs, or both. A preset sets only its own knobs. Later items win, so
`-Look "aces,sharpen=0.6"` uses the `aces` preset with sharpening at 0.6.

| Preset | Settings |
| --- | --- |
| `aces` (default) | `tonemap=aces`, `saturation=1.1`, `bloom=0.7`, `sharpen=0.4`, `clearcoat=1`, `clearcoat_glint=1`, `ao=0.6`, `sun=2`, `sky=0.7`, `taa=1`, `cockpit_fill=1`, `windshield=1`, `road_specular=0`, `road_sheen=0.4`, `glass=1`, `chrome=1`, `rims=1`, `adaptation=1` |
| `gt` | the same with `tonemap=gt` (Hajime Uchimura's curve: a long linear middle, so midtone colors stay closest to the game's) |
| `neutral` | the same with `tonemap=neutral` (Khronos PBR Neutral: linear up to 0.76, then only the brightest channel compressed) |
| `gt7` | after Gran Turismo 7: the same with `tonemap=gt7` (GT7's own tone mapping: a darker toe, a long linear middle, a convergent shoulder, and the hues of bright colors kept), `sky_saturation=1.15` and `sky_contrast=0.3` for a deeper blue sky. Since 29 October 2026; see [look presets](analysis/look-presets-gt7-fm2023.md) |
| `fm2023` | after Forza Motorsport (2023): the `aces` mods with `saturation=1.05`, `ao=0.9`, `haze=0.3`, `temperature=0.35` and `bloom_width=0.5`: warmer, hazier and a little less saturated than `gt7`, with deeper occlusion and a wider bloom. Since 29 October 2026; see [look presets](analysis/look-presets-gt7-fm2023.md) |
| `original` | The game's own look with TAA (`taa=1`) and no shader mod. Clears the knobs given before it in the same value. `-Look "original,taa=0"` is the game's picture without any change. |
| `pc` | The earlier name of `aces`. |

Since 20 October 2026 `aces`, `gt` and `neutral` share the mods that `pc` had; before, they had
only the tone curve, `clearcoat=1`, `ao=0.6` and `sun=2`. Every preset has TAA, so races render
one tile without MSAA ([-SingleTile and -Msaa](#-singletile-and--msaa)).

Knobs that are not set keep the original look. Examples: `-Look "aces,clearcoat=0,ao=0"`
and `-Look original`.

| Knob | Values and default | Effect |
| --- | --- | --- |
| `tonemap` | `filmic`, `gt`, `aces`, `neutral`, `gt7` | Tone curve. `gt7` is a port of Polyphony Digital's GT7 tone mapping (SIGGRAPH 2025, MIT; [look presets](analysis/look-presets-gt7-fm2023.md#2-the-gt7-operator)) with an SDR paper white of 250 cd/m^2. |
| `sharpen` | number | Sharpening. |
| `bloom` | number | Bloom. |
| `bloom_width` | number, default 0 | A wide halo around the bloom (two rings of taps 3% and 6% of the screen's width out), in units of the bloom's own energy, as a low sun spreads its glow. |
| `temperature` | number, -1 to 1, default 0 | White balance of the final image: above 0 warmer, below 0 cooler; 1 raises red over blue by about 1.27, the luminance kept. |
| `haze` | number, 0 to 1, default 0 | Aerial haze: the post pass `haze_amount` (`work/fm4-rex/passes/haze.hlsl`) turns each pixel's view distance into an amount, 1 - exp(-(d - 30 m) / 800 m) times the knob, and 0.35 times the knob on the sky, and the final combine mixes the scene toward a pale blue-gray by it, so distant hills and buildings fade and the sky gets paler. Above 0 it turns the post passes on. The shape is `HAZE_DISTANCE`, `HAZE_START` and `HAZE_SKY` in [passes.toml](../work/fm4-rex/passes/passes.toml). |
| `haze_brightness` | number, default 1 | Brightness of the haze, in units of 2 stops above the game's middle gray after the exposure. |
| `exposure` | number | Exposure. |
| `adaptation` | 0 or 1, default 0 | 1: the auto exposure adapts as fast as the game's at 60 fps at every frame rate ([below](#exposure-adaptation)); 1 in `aces`, `gt` and `neutral` since 26 October 2026. |
| `contrast` | number | Contrast. |
| `saturation` | number | Saturation. |
| `vignette` | number | Vignette. |
| `motion_blur` | number | Motion blur, in all four motion blur shaders. Photo mode's blur, which the player sets, changes only when this is set. |
| `clearcoat` | number | Clear coat of the car body paint (`fm4-paint.hlsli`) in every car shader set: races, 16-car races, split screen, garage and showroom. In the showroom it adds only the reflection of the studio cube. |
| `clearcoat_roughness` | number, default 0.3 | Roughness of the clear coat. |
| `clearcoat_glint` | number, default 4; 1 in `pc` | Soft limit of the clear coat's sun highlight, in units of the sun's diffuse light; also the limit of the glass, chrome and rim highlights. At 4 the hood turns into a white blob towards the sun. |
| `flakes` | number | Flakes in the car paint. |
| `flake_scale` | number, default 1200 | Scale of the paint flakes. |
| `glass` | number | Windows and lamp covers (`fm4-car.hlsli`) in races, 16-car races, split screen and the garage: the environment's reflection at grazing angles and a sharp sun highlight, with the game's transparency kept. |
| `glass_roughness` | number, default 0.08 | Roughness of the glass highlight. |
| `chrome` | number | Chrome trim and chrome rims, in the same car shader sets: a sun highlight and the rise of the reflection at grazing angles. |
| `chrome_roughness` | number, default 0.1 | Roughness of the chrome highlight. |
| `rims` | number | Painted and alloy rims, in the same car shader sets: a clear lacquer over the game's metal. |
| `rims_roughness` | number, default 0.25 | Roughness of the rim lacquer. |
| `sky_saturation` | number | Saturation of the sky dome of the race tracks (`fm4-nature.hlsli`). |
| `sky_contrast` | number, 0 to 1 | Contrast of the sky dome: an S-curve on its brightness, so clouds stand out more against the sky. |
| `sky_warmth` | number, -1 to 1 | White balance of the sky dome: below 0 bluer, above 0 warmer. |
| `sky_horizon` | number, 0 to 1 | A gradient from the horizon to the zenith: a paler, brighter horizon and a deeper sky overhead. |
| `foliage_hue` | degrees | Hue of the green of trees, tree cards, bushes and grass on every track: below 0 towards yellow and orange (-30 dry, -60 autumn), above 0 towards blue green. Bark, flowers and gray chain-link fences keep their color. |
| `foliage_saturation` | number | Saturation of that green. |
| `foliage_brightness` | number | Brightness of that green. |
| `road_brightness` | number, default 1 | Brightness of the asphalt texture of the road and the road edges on every track (`fm4-road.hlsli`). |
| `road_contrast` | number, default 0 | Contrast of the asphalt aggregate against the texture's local mean: light stones lighter, the binder darker, the overall tone kept. |
| `road_specular` | number, default 1 | The game's sun highlight on the road, its lines, edges and paved shoulders. `0` removes it, and with it the bright sheet on sunlit asphalt toward the sun. |
| `road_sheen` | number, 0 to 1 | A GGX sun highlight with Fresnel on the same surfaces, in the sun's share of their diffuse light, glinting with the aggregate. With `road_specular=0` it replaces the game's highlight. |
| `road_roughness` | number, default 0.5 | Roughness of that highlight: lower is a smaller, brighter glare. |
| `road_sheen_grain` | number, default 1 | How much that highlight follows the aggregate and the skid marks: it scales the highlight by the asphalt texel's brightness against the texture's local mean, to this power (and on the Top Gear and Crown Jewel road by the darkening of the skid marks). `0` gives an even sheen. |
| `road_debug` | 0 to 7 | Debug views of the lit road `ROAD_BLND_2`: 1 the highlight's share of the light, 2 without the highlight, 3 the highlight alone, 4 which light the diffuse takes, 5 the parts of the specular amount. With `road_sheen` above 0, on every lit road shader: 6 the image times the sheen's grain, 7 the sheen alone. See [look mod coverage](analysis/look-mod-coverage.md#road-surface-18-october-2026). |
| `cockpit_fill` | number, 0 to 2; 1 in `aces`, `gt` and `neutral` | The sky's fill of the shaded cockpit interior (`fm4-cockpit.hlsli`), in the race, 16-car, split-screen and UI car shader sets: it scales the game's own hemispherical term of the interior by 1 + 20 times the knob, weighted by how much a surface faces up and lessened by 65% where the track lightmap shades the car (tunnels, rock walls, stands), and gives the driver a floor of 0.25 times the knob on the car's own shadow. The gauges, cluster and dashboard stay readable with the sun ahead, beside or behind, and the interior does not glow in tunnels. `0` is the game's interior. See [Cockpit light](#cockpit-light). |
| `cockpit_light` | number, 0 to 1; no preset since 26 October 2026 | The least share of the sun's direct light that the cockpit interior and the driver keep in the car's own shadow (`fm4-cockpit.hlsli`), in every car shader set: it raises the floor of their shadow term, which the game sets to 0. It follows the sun's direction against the car and also lights the interior where the world is shaded, so the interior swings between lit and black as the car turns and glows in tunnels. See [Cockpit light](#cockpit-light). |
| `ao` | number, `0` turns it off | Strength of the screen-space ambient occlusion. |
| `taa` | `1` on (in every preset), `0` off | Temporal anti-aliasing before the HUD, with the scene jittered while the camera moves and on a still camera, whose frames it accumulates; the look's `sharpen` moves after it, and races render one tile without MSAA. See [Temporal anti-aliasing](#temporal-anti-aliasing). |
| `taa_point` | `0` (default) or `1` | With `taa`: `1` runs TAA at the scene_color point, before the game's post chain, instead of before the HUD; the look's `sharpen` stays in the final combine. See [Temporal anti-aliasing](#temporal-anti-aliasing). |
| `aa` | `taa` (default), `fsr` or `taau` | With `taa`: `fsr` runs AMD FSR 3.1's native anti-aliasing at the scene_color point instead of TAA, with the look's `sharpen` as FSR's sharpening; needs a runtime built with `-Fsr on`, else TAA runs at that point. `taau` runs TAA at the scene_color point (as `taa_point=1`), and with [-Upscale](#-upscale) as the upscaler, with FSR's sharpening and the jitter of every frame. See [Temporal anti-aliasing](#temporal-anti-aliasing). |
| `fsr_sharpen` | number, 0 to 1; no preset | With `aa=fsr` (and `aa=taau` while upscaling): FSR's sharpening (RCAS; TAAU's own sharpening) in place of its default, the look's `sharpen` at ratio 1 and 0.4 times the [-Upscale](#-upscale) ratio above 1 more while upscaling (0.6 at `quality` with the presets' 0.4), at most 1. For A/B runs, for example `-Look "aces,aa=fsr,fsr_sharpen=0.4"`. |
| `windshield` | number, 0 (off) to 2; 1 in `aces`, `gt` and `neutral` | Glass on the windshield and side windows of the race cockpit camera, which the game leaves open: a light tint, a faint reflection of the dashboard, and glare and dust toward bright light. See [Cockpit glass](#cockpit-glass). |
| `sun` | number, `0` turns the glare off | Strength of the sun glare on the RTV path, the same at every MSAA level. Without it (`original`) the glare follows the counted samples. See [Sun glare](#sun-glare). |
| `road_gloss` | multiplier, default 1 | Specular of the road surface. See [Track lighting](#track-lighting). |
| `sky` | multiplier, default 1 | Brightness of the sky; the `sky_*` knobs above change its color. See [Track lighting](#track-lighting). |

`pc` sets `clearcoat_glint=1`, `road_specular=0`, `road_sheen=0.4`, `glass=1`, `chrome=1` and
`rims=1` since 20 October 2026, after an A/B at 2x with the camera turned toward the sun on the
Bernese Alps grid; `-Look "pc,clearcoat_glint=4,road_specular=1,road_sheen=0,glass=0,chrome=0,rims=0"`
is the `pc` look before them. At `clearcoat_glint=4` the hood and roof turned into a white blob
toward the sun, at 1 the highlight keeps its shape. The road knobs turn the bright sheet on sunlit
asphalt toward the sun into a glare with the aggregate showing through it (with the default
`road_sheen_grain=1`; without the grain the sheen is an even, milky haze on Top Gear), and views
away from the sun look the same. The glass, chrome and rim mods add a little reflection to the
windows at grazing angles and showed no bloom blob toward the sun (see
[look mod coverage](analysis/look-mod-coverage.md#facing-the-sun-and-the-pc-preset-20-october-2026)).
No preset sets the `sky_*` or `foliage_*` knobs: in an A/B at 2x on the Bernese Alps
and Maple Valley, `sky_saturation=1.15,sky_contrast=0.3,sky_horizon=0.5` gave a slightly deeper
blue sky, and `foliage_saturation=1.15` was barely visible (see
[look mod coverage](analysis/look-mod-coverage.md#in-game-checks-sky-and-vegetation-18-october-2026)).
For autumn colors try `-Look "pc,foliage_hue=-45"`.
### Cockpit light

In the sun the car's roof shades most of the cockpit interior, and FM4 draws the shaded part with a small
hemispherical term only, near black against an exposure set by the windows. Since 26 October 2026 the
presets `aces`, `gt` and `neutral` set `cockpit_fill=1`, a sky fill from that term; from 20 to 26 October
2026 they set `cockpit_light=0.5`, a floor of the sun's direct light, which the project owner saw swing
between much too bright and much too dark with the sun's position.

- Over a lap of the Bernese Alps with the AI driver (1x, the gauges' mean luma of 255): 1.2 to 92.6 with
  `cockpit_light=0.5`, 88.8 in the tunnel; 1.0 to 12.8 with neither knob, the same as `-Look original`
  and `-Rov`; 12.0 to 29.7 with `cockpit_fill=1`, 21.5 in the open and 24.6 in the tunnel.
- `-Look "aces,cockpit_fill=0"` is the game's interior; `-Look "aces,cockpit_fill=0,cockpit_light=0.5"`
  the look of 20 to 26 October 2026.
- The fill covers the 22 interior shaders that have the track lightmap and a hemispherical scale and the
  six driver shaders; the 16 others (the showroom set and the `cockpit_*` material shaders) keep the
  game's interior. It lights only the cockpit interiors and the driver, also where they show through a
  car's windows, not the windows themselves; a brighter interior can lower the auto exposure a little in
  dark scenes.
- `windshield=1` (since 20 October 2026; `-Look "aces,windshield=0"` turns it off) adds the glass
  ([Cockpit glass](#cockpit-glass)).

See [cockpit brightness](analysis/cockpit-brightness.md#the-cause-the-cockpit_light-floor-25-october-2026).

### Exposure adaptation

The game's auto exposure moves towards the scene's brightness by a fixed share of the
difference in every frame (0.045, the same at every frame rate), made for the Xbox 360's
60 fps. Above 60 fps it therefore settles faster than the game does: four times as fast at
`-FrameRate 240` (a time constant of about 0.09 s instead of 0.36 s), twice at 120, and twice
in the countdown and replays at the default rate, which run at 60 fps instead of the title's
30 ([-FrameRate](#-framerate)). After a change of the scene's brightness, such as a tunnel
exit, the picture reaches its new exposure in about a quarter of the game's time at 240 fps.

`adaptation=1` gives each frame the share that covers the same part of the difference in
that frame's time at the game's speed: `1 - (1 - share)^(design rate x frame interval)`. The
design rate is the frame rate the title is designed to render the scenario at (60 in races,
30 in its 30 fps screens and in the countdown and replays, which it presents every second
vblank), as `fm4-vblank-rate.cpp` reports it; the frame interval is the title's measured time
between its presented frames, averaged over about eight frames, each at most 50 ms. The
runtime passes both to the shader mods (`xe_guest_design_frame_rate`, `xe_guest_frame_interval`),
so a race below its target rate, such as on the ROG Xbox Ally X, keeps the game's speed too.
The power is at least `60 / vblank rate`, the most frames the title can present at that rate,
so a switch of the scenario, which the averaged interval follows a few frames late, never
slows it, and at most 1: below the title's own rate it adapts per frame, as the title does.
At a 60 Hz guest vblank (the menus, the 30 fps screens, and races without `-FrameRate`) it
changes nothing. It is a shader mod of the auto exposure shader
(`shader_CAF3881F7C4CDCAA.mod.hlsli`), so it works on the RTV path and the native path;
`-Rov` keeps the title's adaptation. The GUI launcher shows it as the checkbox
**60 fps exposure speed**. The presets `aces`, `gt` and `neutral` set it since 26 October 2026;
`-Look "aces,adaptation=0"` keeps the title's adaptation, as `original` does.

Measured on 26 October 2026 in a frozen race frame whose exposure input was stepped: the
exposure made 90% of its change 1.47 to 1.50 s after the step at 60 Hz, with and without the
knob; with it 1.41 s at 240 Hz on RTV held at 160 fps by its tiles, and 1.43 s on the native
path at 229 fps, against 0.59 and 0.34 s without it. Details in the
[current status](status/current.md#auto-exposure-adaptation-by-frame-time-26-october-2026).

### Sun glare

The title draws the sun glare from an occlusion query of the sun sprite. The host query
counts samples, so without a correction the same sun gives twice the glare at 2x MSAA
(`-Quality xbox` races) and four times at 4x (`high` races, replays, photo mode) as at 1x.
The `sun` knob makes the runtime report guest pixels instead, times the strength
(`rtv_occlusion_awaited_scale`), through a soft limit of 1,200 pixels
(`rtv_occlusion_awaited_limit`, `SUN_LIMIT` in `pipeline.py`): up to 400 pixels pass
unchanged, above that they approach the limit, so the glare cannot wash out the screen. A
fully visible sun is about 338 pixels at 720p. The knob is a launcher setting, not a
shader mod, so it also works with `-Look original,sun=1`. `-Rov` does not use it.

### Track lighting

`road_gloss` and `sky` do not change shaders. They edit the title's per-track lighting
files, `Media\tracks\<Track>\TrackSettings.xml` and `TrackSettings1.xml`, as multipliers of
each track's own values, so every track keeps its own character:

- `road_gloss` scales `RoadSpecular` and `RoadSpecular2` of the `RoadSpecular` element, the
  specular of the road surface. The glossiness (highlight size) is left as it is.
- `sky` scales `skyScale`, the brightness constant of the sky shaders.

The launcher passes them as `FM4_TRACK_SETTINGS` (`fm4-track-settings.h`). At start the game
copies those files of every race location on Disc 1 from your own data, edits the values,
writes the copies to `BuildRoot\cache\fm4-track-settings`, and opens the copies instead
of the disc files. The garage, showroom and car thumbnail scenes (`HomeSpace*`,
`UIThumb*`) and the tracks of downloadable content keep their files. A knob at 1 edits
nothing, and then no copies are made.

The `pc` preset sets `sky=0.7`; `aces`, `gt` and `neutral` keep the game's sky, and no preset
sets `road_gloss`. `-Look "pc,sky=1"` restores the sky of `pc` before 18 October 2026. In 2x
captures of that day on the Bernese Alps, `sky=0.7` gives a bluer, less washed out sky that
keeps the snow and clouds bright. Lowering `road_gloss` only removes the
sparkle of the asphalt aggregate. The bright sheet on sunlit asphalt toward the sun is the
road shader's sun highlight, but most of its specular amount comes from the blend value of
the rubber (`RoadSkid` and `RoadSkid2`), which `road_gloss` leaves as it is. `road_specular`
and `road_sheen` (see [Look](#look)) act on the whole highlight. See
[look mod coverage](analysis/look-mod-coverage.md#road-surface-18-october-2026).

### Ambient occlusion

`ao` above 0 turns on the runtime's post passes (`post_pass_path`, a copy of
`work/fm4-rex/passes` in `BuildRoot\passes`; see the
[post pass framework](analysis/post-pass-framework.md)), whose GTAO pass works on the
scene depth (see [GTAO design](analysis/gtao-design.md)) with a radius of 0.75 m. The
final combine shaders darken the scene by the occlusion, less on bright pixels. It covers
races, replays, photo mode, the full-screen car views of the garage, Autovista and the career
home; not the cockpit interior, and not the small car view of the car select screen.
`pipeline.py --ao` (and `e2e.py --ao`) runs the pass without a look applying it. With
`-ShaderDev` the passes load from `work/fm4-rex/passes` itself and reload when a file
there is saved; `shader_mod_set GTAO_RADIUS=1` in the console changes a pass param at
once.

The passes also write the scene's motion vectors (see
[motion vectors](analysis/motion-vectors.md#8-implementation-18-october-2026)), for later
temporal passes, and a mask of the pixels whose motion is unknown or mixed;
`shader_mod_set MOTION_VECTORS=0` skips them. `shader_mod_set MOTION_DEBUG=1` draws
them color coded over the frame, under the HUD (2: over the scene, 3: the car IDs, 4: the
cars' own motion, 5: the cockpit interior, the world through glass and the depth masks,
6: the mask; 0: off).

The GTAO pass costs about 0.2 ms of GPU time per race frame at 1x, and 0.7 to 0.8 ms at 2x
(2560x1440). Races stay at 60 fps. At 240 Hz, races at 2x drop from about 174 fps to
about 162 fps. Replays and photo mode have occlusion too. The cockpit interior, the
garage and the showroom get none. The first run of a look compiles the mod shaders with
FXC when they are first drawn. For the six measured, this took 0.15 to 1.4 s each and
2.5 to 3.0 s for the paint (about 5.4 s at the start). Later runs load them from
`BuildRoot\cache\shader-overrides` in milliseconds.

### Temporal anti-aliasing

`taa=1` turns on the TAA post pass ([TAA](analysis/taa.md)): the scene draws move by a
sub-pixel offset each frame, and the frames before the HUD are accumulated along the motion
vectors, so wires, fences, lattices, distant road lines and shading glints stop stepping and
shimmering. The HUD and the side mirrors are drawn after it and stay sharp. `pc` had it since
19 October 2026, and every preset since 20 October 2026; with it races render one tile
without MSAA ([-SingleTile and -Msaa](#-singletile-and--msaa)). `-Look "aces,taa=0"` is the look
without it (the look's sharpening, no jitter, the tiles and MSAA of `-Quality`).

- The scene draws are jittered while the camera moves and the frames come at 45 fps or more,
  and on a still camera at any frame rate (since 20 October 2026): there TAA accumulates the
  jitter's phases where nothing moves, so rewind, pause, photo mode and a still replay or
  countdown camera get a still, anti-aliased picture instead of a shimmering one. A camera
  that moves at under 45 fps (the 30 fps post-race) is not jittered. The pass params
  `TAA_JITTER_MOTION` (0.5 host pixels per frame) and `TAA_JITTER_RATE` (45) set the
  thresholds, `TAA_STILL` (1) the accumulation on a still camera and `TAA_STILL_FRAMES` (256)
  how many frames it averages; they change live with `-ShaderDev`. `TAA_JITTER_MOTION` and
  `TAA_JITTER_RATE` at 0 jitter every frame, `TAA_STILL=0` turns the still camera's jitter off
  ([TAA, arming](analysis/taa.md#35-arming-and-switching)). On a still camera TAA also
  compares each pixel with the same jitter phase a cycle ago (`TAA_STILL_PHASES`, 1): what
  did not change keeps its thin details and colored edges still, and what changed (a flag in
  the wind) blends as in motion instead of smearing; `TAA_STILL_CHANGE` (2 levels of 255) is
  the threshold ([TAA, step 12](analysis/taa.md#72-per-pixel-taa_resolve)).
- The look's `sharpen` (0.4 in the presets) is applied after TAA instead of before it (the pass param
  `TAA_SHARPEN`), since sharpening the jittered frame would sharpen the jitter. TAA without
  sharpening is visibly softer; 0.4 comes close to the look without TAA, without its stair
  steps. `original` has TAA without sharpening.
- In the single tile the swap's FXAA is left out: TAA anti-aliases the tile itself.
- It costs about 0.1 ms of GPU time per race frame at 1x and 0.15 to 0.2 ms at 2x.
- `shader_mod_set TAA=0` switches it off at once with `-ShaderDev` (the tiling stays until the
  next race); `TAA_DEBUG=1` to `7` shows the debug views
  ([TAA, debug views](analysis/taa.md#76-debug-views-taa_debug)).
- The `-Info` overlay shows "Anti-aliasing: TAA before the HUD, sharpen 0.4, scene jittered
  while the camera moves, a still one accumulated".
- RTV only, like the other post passes; not with `-Rov` or `-Look "original,taa=0"`.
- `aa=fsr` (`-Look "aces,aa=fsr"`; with [-Upscale](#-upscale) it also upscales) runs AMD FSR 3.1's native anti-aliasing (its upscaler at ratio 1) at the scene_color point instead of TAA, on a runtime built with `Build.ps1 -Fsr on` ([setup](setup.md#fsr-31-optional)): the scene is jittered in every frame, FSR accumulates still pictures itself, the look's `sharpen` becomes FSR's RCAS sharpening, and its output is written back into the game's scene. A runtime built without FSR keeps TAA at the scene_color point and logs a warning. The `-Info` overlay shows "Anti-aliasing: FSR 3.1 before the post chain (scene_color), sharpen 0.4, scene jittered every frame"; the GUI launcher offers TAA or FSR 3.1 under Temporal AA. `shader_mod_set FSR_DEBUG=1` shows FSR's debug view with `-ShaderDev`. It costs about three times TAA's GPU time (1.09 against 0.36 ms a race frame at 2x on this PC); the comparison and the A/B checklist are in [FSR plan, 9.9](analysis/fsr-plan.md#99-stage-2-the-checks-before-the-owners-ab-25-october-2026).
- `taa_point=1` (`-Look "aces,taa_point=1"`) moves TAA to the scene_color point: it runs on the scene before the game's motion blur, bloom, exposure and final combine, and its output is written back into the game's scene, which those then sample. The occlusion gets its own temporal filter there (`GTAO_TEMPORAL`), and the look's `sharpen` stays in the final combine, after TAA. It costs about 0.47 ms of GPU time per race frame at 2x, and the `-Info` overlay shows "Anti-aliasing: TAA before the post chain (scene_color), the look's sharpening, scene jittered while the camera moves". The presets keep the final point; see [TAA, the comparison](analysis/taa.md#23-comparison).

### Cockpit glass

In the race cockpit camera FM4 draws no glass for the player's car, so the windshield and side
windows look like open frames ([cockpit brightness](analysis/cockpit-brightness.md#windshield-and-side-windows)).
`windshield=1` (in `pc` and then `aces`, `gt` and `neutral` since 20 October 2026; `-Look "aces,windshield=0"` turns it off) adds a post pass that puts glass over the pixels that
see the world through the cockpit's windows ([windshield](analysis/windshield.md)): a light
green-gray tint, a reflection of the dashboard (faint, since FM4's dashboards are dark), and a
haze with dust and wiper arcs where bright sky or sun shows through. `windshield=2` doubles
every effect.

- Only frames of the cockpit camera change. The chase, hood and bumper cameras, the 3-2-1
  camera, the HUD, the side mirrors and the interior mirror stay as they are.
- It runs before the HUD and after TAA. The `-Info` overlay shows "Cockpit glass: strength 1".
- RTV only, like the other post passes; not with `-Rov`.
- With `-ShaderDev`, `shader_mod_set WINDSHIELD=0` switches it off at once, `WINDSHIELD_DEBUG=1`
  to `3` shows the debug views, and the `WINDSHIELD_*` defines of
  [passes.toml](../work/fm4-rex/passes/passes.toml) tune each effect.

### Shader mod development

`-ShaderDev` is for working on the look mods; see
[shader-mod-iteration.md](analysis/shader-mod-iteration.md).

- The mods load from `work/fm4-rex/look` itself instead of a copy. Saving a file there
  recompiles the shaders that use it, without a restart. FXC errors go to
  `BuildRoot\logs\runtime.log`, and the last shader that compiled stays on screen.
- Every knob of the look files is live. Open the console with the backtick key and type
  `shader_mod_set FM4_LOOK_SHARPEN=0.6 FM4_LOOK_BLOOM=0.8`: the image changes in the next
  frame. `shader_mod_set` alone lists the values. The names are the `FM4_LOOK_*` defines
  of `fm4-look.hlsli`, `fm4-paint.hlsli`, `fm4-car.hlsli`, `fm4-nature.hlsli` and `fm4-road.hlsli`; `FM4_LOOK_TONEMAP` takes 0 to 4 (`filmic`,
  `gt`, `aces`, `neutral`, `gt7`). The haze's strength is the pass param `HAZE` (with `FM4_LOOK_HAZE`
  above 0).
- `NAME=VALUE` lines in `work/fm4-rex/look/shader_mod_defines.txt` (not tracked by Git)
  apply in the same way whenever the file is saved.
- The GTAO pass only runs when `ao` is above 0 in `-Look` (or with `pipeline.py --ao`), so
  start with it on to change `FM4_LOOK_AO` live.
- Live knobs make the mods larger and slower. Put the values you found into `-Look` for
  normal runs, for example `-Look "pc,sharpen=0.6,bloom=0.8"`.

## Texture replacement

The runtime can write the game's textures to DDS files and use edited files in their
place, like a Dolphin texture pack. Each texture is named by a hash of its contents, so
a file keeps working across runs, builds and game releases. The design and its limits
are in [texture-replacement.md](analysis/texture-replacement.md).

1. Dump: run with `-TextureDump D:\fm4-dump` and drive where the texture is shown. Each
   new texture is written once as `<16 hex digits>.dds`, and a line about it (format,
   size, mip count, guest address) is added to `textures.txt`. Files that are already
   in the directory are not written again.
   Without the game, `python outputs/FM4-build/texture_extract.py --build-root <BuildRoot>`
   writes the same files for the textures stored in the track archives, the skies of
   every track among them, to `<BuildRoot>\texextract`
   ([offline extraction](analysis/texture-replacement.md#offline-extraction)).
2. Edit a dumped file, or make a new one with the same name, and put it in a pack
   directory (subdirectories are allowed).
3. Run with `-TextureReplace D:\fm4-pack`. `BuildRoot\logs\runtime.log` lists every
   file found, every texture that was replaced, and why a file was not used.

A replacement may have another size and format than the original: BC1 to BC7, RGBA8,
BGRA8, R8, RG8, R10G10B10A2 and RGBA16F, with or without mips. Give it a full mip chain;
a file without mips is sampled without them. sRGB files are read as their UNORM
equivalent, because the game's shaders decode gamma themselves. Block-compressed files
must be a multiple of 4 in width and height.

Both options are off by default and cost nothing then. With either one on, each
texture load is hashed on the CPU (an untiling copy of its base level). Render
targets, cube maps, arrays, 3D textures and textures that change all the time are never
dumped or replaced.

## Controller

A connected controller works without setup through the runtime's SDL backend. The game
knows only the Xbox 360 pad: two rumble motors, 8-bit triggers and no paddles. `-Pad`, and
the Controller section of the GUI launcher's Controls page, change how the runtime reads a
modern pad and how it rumbles. Every default keeps the pad as the game has always seen it,
except the rumble strength: the launchers default to 1.5, because the game's driving rumble is
weak (0.05 to 0.2 of full strength) and 1.5 felt best in the owner's test on the ROG Xbox Ally
X (25 October 2026). The runtime's own default stays 1, the game's own scale.
The design and the measurements behind the options are in
[modern gamepads](analysis/modern-gamepads.md).

```powershell
.\outputs\FM4-build\Build.ps1 -Action run @common -Pad 'driver=rawinput,trigger_rumble=1,rumble=1.5'
```

### The game's steering deadzone

The game applies deadzones of its own after the runtime, from the player's profile. The
steering stick's inner deadzone is 24% by default: the first quarter of the stick's travel
does nothing, and full lock comes at 95%. On a modern pad whose stick centres precisely, that
dead quarter is the main reason small corrections do not steer.

The game's own setting does not help: it keeps the inner edge at XInput's left stick
deadzone, 7,849 of 32,767 (24%), or above, so lowering STEERING AXIS DEADZONE INSIDE below 24
changes almost nothing (measured on 25 October 2026, see
[keyboard controls](analysis/keyboard-controls.md#verification)); it can only raise the
deadzone. Use the runtime's steering anti-deadzone instead:

```powershell
.\outputs\FM4-build\Build.ps1 -Action run @common -Pad 'anti_deadzone=1,stick_deadzone=0.05'
```

`anti_deadzone=1` maps the left stick into the deadzone the game reports for the player's
profile (read live, so it stays exact whatever the profile says), so the steering follows the
stick from the first movement and reaches full lock at full deflection. `stick_deadzone=0.05`
keeps a stick that does not rest exactly at 0 from steering on its own; raise it a step if the
car still drifts. Both are off by default for now. Leave the game's STEERING AXIS DEADZONE
INSIDE at 24.

The game's controller settings are at PLAY CAREER > MY PROFILE > OPTIONS > CONTROLLER (the
CONTROLLER LAYOUT screen), then X (ADVANCED) for the ADVANCED OPTIONS screen, saved with A. It
holds, in percent, STEERING AXIS DEADZONE INSIDE (24) and OUTSIDE (95: full lock), the
throttle range (ACCELERATION AXIS DEADZONE INSIDE and OUTSIDE, 15 and 90), the brake range
(DECELERATION AXIS DEADZONE, 15 and 95), the clutch and handbrake ranges, and VIBRATION SCALE,
the game's own rumble scale, at 100 with its slider full; `rumble` above 1 is the way to
stronger rumble. Y resets the screen to the defaults. The game's rumble switch is VIBRATION on
the CONTROLLER LAYOUT screen.

### -Pad knobs

Knobs are `name=value`, separated by commas; a later knob wins. A knob left out takes the
launchers' default; a value equal to the runtime's default sets nothing. Each knob is a runtime
setting, named in the last column.

| Knob | Values | Default | Description | Runtime setting |
| --- | --- | --- | --- | --- |
| `driver` | `default`, `rawinput`, `wgi`, `gameinput` | `default` | SDL's Windows pad drivers; see [Pad drivers](#pad-drivers). | `sdl_joystick_driver` |
| `rumble` | 0 to 2 | 1.5 (the runtime's: 1) | Multiplies the game's two motors, clamped at full strength. The game's rumble in normal driving is 0.05 to 0.2 of full strength, so the launchers default to 1.5; `rumble=1` gives the game's own strength. | `pad_rumble_strength` |
| `trigger_rumble` | 0, 1 | 0 | Trigger rumble derived from the game's motors; see [Trigger rumble](#trigger-rumble). | `pad_trigger_rumble` |
| `trigger_rumble_strength` | 0 to 2 | 1 | Multiplies the derived trigger rumble. | `pad_trigger_rumble_strength` |
| `thread` | 0, 1 | 0 | Updates SDL's pads every millisecond on a thread of their own; see [Input thread](#input-thread). | `input_sdl_thread` |
| `anti_deadzone` | 0, 1 | 0 | Left stick only: maps the stick into the game's own steering deadzone as the game reports it for the player's profile, so the steering follows the stick from the first movement and reaches full lock at full deflection; see [the game's steering deadzone](#the-games-steering-deadzone). Use it with `stick_deadzone`. | `pad_steering_anti_deadzone` |
| `stick_deadzone` | 0 to 0.5 | 0 | Both sticks: a radial deadzone, rescaled from its edge, for a stick that does not rest at 0; 0.05 with `anti_deadzone`. | `pad_stick_deadzone` |
| `stick_outer` | 0.5 to 1 | 1 | Both sticks: the deflection that gives full output. | `pad_stick_outer` |
| `stick_curve` | 0.5 to 3 | 1 | Both sticks: an exponent on the deflection; above 1 is finer near the centre. | `pad_stick_curve` |
| `trigger_deadzone` | 0 to 0.5 | 0 | Triggers read 0 up to this fraction. | `pad_trigger_deadzone` |
| `trigger_outer` | 0.5 to 1 | 1 | The trigger travel that gives full output, for triggers that do not reach the end. | `pad_trigger_outer` |
| `misc1` | `a`, `b`, `x`, `y`, `lb`, `rb`, `back`, `start`, `ls`, `rs`, `up`, `down`, `left`, `right`, `guide`, `none` | `guide` | The Xbox 360 button that SDL's Misc1 presses (the DualSense microphone button). | `pad_misc1` |
| `right_paddle1`, `left_paddle1`, `right_paddle2`, `left_paddle2` | as `misc1` | `y`, `b`, `x`, `a` | The buttons of paddles P1, P3, P2 and P4. | `pad_right_paddle1`, ... |
| `touchpad` | as `misc1` | `guide` | The button of the PlayStation touchpad click. | `pad_touchpad` |

- The stick and trigger knobs apply to physical pads (the SDL and XInput backends), never to
  the keyboard or the e2e harness's diagnostic controller. The order is: the deadzone and the
  outer edge on the stick's magnitude, then the curve, then the anti-deadzone. The
  anti-deadzone is applied per guest user, before the pad's stick and the keyboard's steering
  are merged; the keyboard maps its steering into the same range, so the device that steers
  further wins with its whole stick.
- A Guide target reaches the game only with the runtime's `guide_button` setting on.
- On Windows, Xbox pads report neither the Share button nor the Elite paddles in this build
  (no SDL driver here reads them); an Elite's own profile can map its paddles to other
  buttons. `misc1`, the paddles and `touchpad` apply to PlayStation and other pads that
  report them.

### Pad drivers

| `driver` | What SDL uses | Xbox One and Series pads | ROG Xbox Ally X built-in pad |
| --- | --- | --- | --- |
| `default` | SDL's defaults: XInput for Xbox class pads, HIDAPI for PlayStation pads | two motors, no trigger rumble | XInput, two motors |
| `rawinput` | Raw Input, with Windows.Gaming.Input (WGI) correlating the triggers and the trigger motors | two motors; trigger rumble once correlated, which needs a first button or stick input | Raw Input, two motors; no trigger rumble (an Xbox 360 class pad; checked on the owner's Ally X) |
| `wgi` | WGI, with XInput and DirectInput off | two motors and trigger rumble; the axes read 0 without window focus | WGI, if WGI lists the pad |
| `gameinput` | Microsoft's GameInput (API v3) before SDL's other drivers; without a GameInput v3 runtime, as `default` | two motors, trigger rumble and the Share button (Misc1) where GameInput reports them | GameInput, for its trigger motors, which XInput and Raw Input do not reach (the owner's test) |

PlayStation pads go through SDL's HIDAPI drivers with every choice (rumble, light bar,
sensors, touchpad). A driver that does not start leaves SDL's others running, so no choice
loses a pad. Raw Input is off in remote desktop sessions.

`gameinput` needs a GameInput runtime of API v3 or newer: the GameInput redistributable
(`GameInputRedist.dll`; the development PC has version 4.0.302 in System32). Windows' own
`GameInput.dll` there (0.2309.26100.9278) offers only the older API. The runtime is not
shipped with the export. At start the log says which applies, `SDL GameInput: a GameInput v3 runtime is available` or
`SDL GameInput: no GameInput v3 runtime (HRESULT ...)` with the reason (not installed, or older
than v3); without one, SDL's other drivers take the pads as with `default`. The pad report's
`driver=gameinput` shows a pad that GameInput took. SDL's GameInput driver is built with the
MIT header and loader of Microsoft's GameInput package and a patch to SDL
([setup](setup.md#the-gameinput-package)).

### Trigger rumble

With `trigger_rumble=1`, a pad with trigger motors (`trigger_rumble=1` in its
[pad report](#pad-report-and-latency-probe)) rumbles its triggers from the game's two motors:
the right trigger from the left motor, scaled by how far the throttle is pressed, and the
left trigger from the right motor, scaled by the brake. Wheelspin buzzes the throttle and a
locking front axle the brake; gear changes, impacts and kerbs also buzz a pressed trigger. A
released trigger stays still. Trigger rumble is zero while the motors are, while the window
has no focus, and while an overlay takes the input. The game sends no rumble while its AI
drives the player's car. On Xbox pads only `rawinput` and `wgi` give trigger rumble. With the
SDL backend (the default), closing the game window stops every pad's motors and trigger
motors, with or without these options; a game process that is killed cannot stop them.

### Input thread

SDL updates the pads in the UI thread's event loop, every millisecond while a pad is open.
In 240 fps races the UI thread is busy presenting, and on the main PC it updated the pads
only every 8.4 ms, so a new pad state waited about 4 ms longer before the game could read it.
`thread=1` updates the pads on a thread of their own every millisecond instead, and the UI
thread stops updating them. Measured with the latency probe on the main PC (25 October 2026):
in 240 fps races a new state was 2.8 ms old on average when the game read it (5.6 ms at the
99th percentile) instead of 6.6 ms (12.2 ms), with `default` and `rawinput` alike; at 60 and
120 fps, where the UI thread keeps up, it made no difference. On the ROG Xbox Ally X (races at
about 100 fps under its 117 fps limit) it brought no gain either: its 1 ms sleep updates the
pads every 1.55 ms, and the UI thread already did every 0.7 ms. It stays off by default.

### Pad report and latency probe

When a pad connects, `runtime.log` gets one line for it with what SDL exposes under the
chosen driver: the name, type, vendor and product ID, the SDL driver that opened it
(`xinput`, `rawinput`, `wgi`, `hidapi`, `dinput`, ...), the connection, the capabilities
(rumble, trigger rumble, LEDs, gyroscope and accelerometer, paddles, Misc1, touchpad) and
the battery. For example, the probe's virtual pad:

```text
SDL pad report: device=1 name="FM4 latency probe" type=standard vendor=0000 product=0000 driver=virtual connection=unknown rumble=1 trigger_rumble=1 rgb_led=0 mono_led=0 player_led=0 gyro=0 accel=0 paddles=0 misc1=0 touchpad_button=0 touchpads=0 power=unknown battery=-1 path="" guid=ff00bd8a464d34206c6174656e007601
```

`--input_latency_probe=true` (on the GUI launcher's Advanced page, Extra cvars) attaches a
virtual SDL pad whose right stick moves by 256, inside every deadzone, at random times, and
logs every 5 seconds how long a new state took to reach the game (`SDL latency probe (ms):`):
the gaps between SDL's pad updates, the wait from a new value to the update that sent it,
from there to the game's read, the whole age at the read, and the gaps between the game's
reads, then the rumble the virtual pad received. It is a measurement tool; leave it off for
normal play.

### Controller test checklist

For the owner's test of the options on real pads, from an
[export](#export-to-another-computer) with the GUI launcher. Leave the game's STEERING AXIS
DEADZONE INSIDE at 24, or set it back to 24: a lower value does nothing
([above](#the-games-steering-deadzone)). Play a Quick Race with the default car and track in
each profile, at least two minutes of driving with wheelspin, braking and a kerb or two.

Xbox Series controller:

| Profile | Controller section |
| --- | --- |
| A | the defaults (`default` driver) |
| B | Pad driver Raw Input, Trigger rumble on (Rumble strength 1.5, the default) |
| C | as B with Rumble strength 2 |
| D | the defaults with Steering anti-deadzone on and, under Sticks and triggers, Stick deadzone 0.05 |
| E | Pad driver GameInput, Trigger rumble on (Rumble strength 1.5, the default) |

- Note for each profile whether the pad works in the menus and in races, whether the first
  rumble comes at once or only after a first input, whether the trigger rumble feels right
  (throttle on wheelspin, brake on locking) and which rumble strength you prefer.
- For profile D: whether small corrections steer now, how the steering feels against A, and
  whether the car steers by itself with the stick at rest (then raise the stick deadzone a
  step and note the value that holds).
- For profile E: whether the pad works, whether the triggers rumble (against B), and whether
  the Share button does anything; send its `SDL GameInput:` line and pad report
  (`driver`, `trigger_rumble`, `misc1`).

ROG Xbox Ally X built-in pad, race frame rate 120 (its panel). Profiles A to D ran on
25 October 2026 ([results](analysis/modern-gamepads.md#the-owners-rog-xbox-ally-x-test)):

| Profile | Controller section | Extra cvars (Advanced page) |
| --- | --- | --- |
| A | the defaults | `--input_latency_probe=true` |
| B | Pad driver Raw Input | `--input_latency_probe=true` |
| C | the defaults, Input thread on | `--input_latency_probe=true` |
| D | Pad driver Raw Input, Input thread on | `--input_latency_probe=true` |
| E | the defaults with Steering anti-deadzone on and Stick deadzone 0.05 | none |
| F | Pad driver GameInput, Trigger rumble on (Rumble strength 1.5, the default) | none |

- The Ally X's pad reports no trigger rumble under XInput or Raw Input (`trigger_rumble=0`),
  so Trigger rumble does nothing with those drivers; GameInput is the route left to its
  trigger motors (profile F).
- For profile E: as profile D of the Series controller.
- For profile F: whether the pad works and whether the triggers rumble; send its
  `SDL GameInput:` line and pad report (`driver`, `trigger_rumble`, `misc1`). Also note the
  version of `C:\Windows\System32\GameInputRedist.dll` (Properties, Details) if the file
  exists.

What to send back, from the export folder's `logs\runtime.log` (the line types below,
nothing else is needed). The log keeps the earlier runs; each run starts at its own
`SDL joystick driver option:` line, so note which profile ran in which order:

```powershell
Select-String -Path logs\runtime.log -Pattern 'SDL pad report:','SDL joystick driver option:','SDL input','SDL GameInput','SDL latency probe' | ForEach-Object Line
```

- `SDL pad report:`: what each pad exposes under the driver of that run.
- `SDL joystick driver option:` and `SDL input thread`: which options the run used.
- `SDL GameInput:` (with `gameinput`): whether a GameInput v3 runtime was found, and why not.
- `SDL input: rumble stopped at exit`: the pads' rumble was stopped when the window closed.
- `SDL latency probe (ms):` (every 5 seconds) and `SDL latency probe at exit:`: the probe's
  timings and the rumble it received.

The lines contain the pads' names and device paths, not the Windows user name.

## Racing wheel

Native racing wheel support ([racing wheels](analysis/racing-wheels.md)) is complete up to the
owner's hardware test, and **off by default** until then: no real wheel has been tested yet.
The MOZA R9 base with the ESX wheel and the SR-P pedals is the planned test set
([known issues](known-issues.md#tested-hardware)).

- The runtime and the GUI launcher read the wheel through SDL's joystick API. Its steering,
  pedals and buttons drive the first player's car at the wheel's full resolution, beside the
  controller.
- Each car has its own steering lock, with an end stop past it, and the cockpit's drawn steering
  wheel turns 1:1 with the wheel.
- The game's own steering torque, at its physics rate, drives the force feedback through a
  safety governor with a hard force cap (0.25 of the wheel's full force by default), together
  with kerb, surface and impact vibrations.
- A wheel that SDL maps as a gamepad also reaches the game as a pad, as before.

A player meets the wheel in this order, all on the launcher's Controls page:

1. The **Wheel test** tab, without the game: find the wheel's axes and buttons and check the
   force's direction ([Racing wheel test](#racing-wheel-test)).
2. The **Racing wheel** tab: turn **Use a racing wheel** on, then set the steering, the pedals,
   the buttons and the force feedback, in that order
   ([Racing wheel in the game](#racing-wheel-in-the-game),
   [Racing wheel force feedback](#racing-wheel-force-feedback)).
3. Play.

From the command line, the same settings are knobs of `-Wheel`
([Racing wheel options](#racing-wheel-options)):

```powershell
.\outputs\FM4-build\Build.ps1 -Action run @common -Wheel 'on=1,shift_up=4,shift_down=5'
```

### Racing wheel test

The **Wheel test** tab runs without the game (`MotorsportDX.exe --wheel-test` opens it with the
test running). Start it with the wheel centred and your hands on the wheel. Its rows are
**Wheel test** (starts and stops it), **Force cap** (0.25 by default, at most 0.5 in the test;
set it before the test starts), **Invert force**, **Record forces** and **Test force**. The
panel at the right shows:

- the wheel SDL found (the first joystick of SDL's wheel type, else the first joystick that is
  not a gamepad), its vendor and product ID, whether SDL types it as a wheel (`SDL wheel`) and
  whether it has force feedback and a spring;
- every axis at the device's resolution (-32,768 to 32,767), the buttons held and the hats;
- the force sent, the ramp, the reason the force is zero, and how long each update to the
  device took.

The test forces, on the **Test force** row (left and right pick one, A sends it):

- **Direction pulse 5%** pushes towards positive for one second, to the right. If the wheel
  turns left, stop the test, turn **Invert force** on and start it again; turn **Invert
  force** on in the Racing wheel tab's force feedback section as well, for the game.
- **Spring 10%** and **25%**: a centring spring that the wheel's firmware runs.
- **Sine 10%** and **25%**: a 1 Hz back-and-forth sent as a constant force every millisecond.
- **Stop force** ends them.

Every force goes through the governor: it ramps in over 1.5 seconds, never exceeds **Force
cap**, is spread out when it would jump, and drops to zero at once when the launcher's window
loses focus or the test stops. Pressing Play stops the test. The launcher writes the test's
lines to `logs\wheel-test.log` (its `Wheel report:` line names the wheel's axes, buttons and
force feedback effects) and, with **Record forces**, every force sent to
`logs\wheel-test-forces.csv`.

While the test runs, the Racing wheel tab shows the steering and each pedal's value as the car
will get it, and its **Rest**, **Pressed** and button rows take their values from the wheel.

### Racing wheel in the game

**Use a racing wheel** opens the wheel in the game; **Drive with the wheel** (on by default)
lets it drive the car, off leaves only its force feedback. The tab's sections, in order:

- **Steering:** the axis, an inversion, and the wheel's rotation lock to lock (**Wheel
  rotation**: set it to Pit House's maximum angle, since the wheel reports that rotation over
  its whole axis).
  - **Car steering lock**, lock to lock, is each car's own. The game gives each car a
    road-wheel lock: 42 degrees for most cars, 36 for GT racing cars and 30 for prototypes.
    This row is the lock of a 42-degree car (900 by default); GT cars get 6/7 of it (771) and
    prototypes 5/7 (643).
  - **Fixed steering lock** replaces it with one lock for every car (0: each car's own).
  - The lock is never more than the wheel's rotation. The car steers 1:1 with the wheel and
    reaches full steering at half the lock either way. Past it, the **End stop** holds the
    wheel with force feedback ([below](#racing-wheel-force-feedback)).
  - The game's steering deadzones and the pad's steering rate limit do not apply: while the
    wheel steers, the car gets the game's own filters for a 900-degree wheel, which follow the
    wheel linearly.
  - The cockpit's drawn steering wheel turns 1:1 with the wheel up to the car's lock. Past
    about 90 degrees the driver's hands keep the game's full-turn pose.
- **Throttle, brake and clutch pedal:** each an axis (None: no pedal), its raw values at rest
  and fully pressed (A on **Rest** and **Pressed** takes them from the wheel test while it
  runs; the order may be either way round), a deadzone at the rest end and a response curve
  (gamma; above 1 is finer at the start of the travel). The values reach the car as floats,
  not as the pad's 8-bit triggers.
- **Buttons:** shift up and down (sequential shifts in the game's manual gearbox), the clutch
  and the handbrake while held. A on a button's row takes the next button pressed while the
  wheel test runs.
- **Force feedback:** [below](#racing-wheel-force-feedback).

The wheel merges with the controller as the game merges two bindings of one action: the
larger pedal, the steering of larger magnitude, a shift from either. The menus are still
driven with the controller or the keyboard. The launcher's **Wheel value** field shows the
profile's `-Wheel` value.

The game's anti-cheat rule withholds a race's credits and experience when a lap has fewer
than 10 frames with a fast steering change. Its threshold is a change per frame, so at a high
frame rate smooth steering could miss it; the threshold is scaled to the frame's time so that
it means what it means at 60 fps, for every device, pads included
([stage 2](analysis/racing-wheels.md#stage-2-done-29-october-2026)).

### Racing wheel force feedback

In a race you drive, the wheel gets the game's own force feedback
([stage 3](analysis/racing-wheels.md#stage-3-done-29-october-2026)), as the game made it for
the Xbox 360 wheel, but sent from a thread of its own at 1,000 Hz:

- a constant force from the game's steering torque. The game computes it once a frame; the
  runtime runs the game's own torque model after each of the physics steps (about 360 a
  second) and plays those values back about a frame behind, so the force has the same detail
  at 60 fps as at 240 fps ([stage 4](analysis/racing-wheels.md#stage-4-done-29-october-2026));
- a centring spring, which the wheel's firmware runs: the game fades it in between 1 and
  10 m/s and out when the rear slides (the rear slip between 0.25 and 0.5), so the wheel
  goes light when the car oversteers;
- a light damper, also in the firmware;
- the vibration of kerbs, rumble strips, grass and gravel, from the game's surface rumble, and
  a short jolt when the car hits a wall, another car or an object, from its collision rumble,
  both scaled by the game's wheel rumble table and run by the firmware as vibrations;
- the end stop of the car's steering lock ([stage 5](analysis/racing-wheels.md#stage-5-done-29-october-2026)):
  past the lock, a force back towards it that starts softly, reaches its strength 6 degrees
  past the lock and holds it, with a damper that takes the bounce out of the stop. It is part
  of the constant force, inside the cap, and only in a race you drive.

The launcher's rows (the End stop row is in the Steering section, the others in the Force
feedback section):

| Row | Knob | Default | What it does |
| --- | --- | --- | --- |
| **Force cap** | `ffb_cap` | 0.25 | The hard limit of everything together (the constant force, the spring, the damper, the vibrations and the end stop), a fraction of the wheel's full force, whatever the gain. The spring and the damper take at most half of it, the vibrations a quarter. |
| **Force gain** | `ffb_gain` | 1 | Multiplies the game's force, spring and damper before the cap. |
| **Minimum force** | `ffb_min_force` | 0 | Lifts small forces above a wheel's own friction; 0 for a direct-drive base. |
| **Smoothing** | `ffb_smoothing` | 10 ms | A low-pass on the game's force; 0: none. |
| **Centring spring**, **Damper** | `ffb_spring`, `ffb_damper` | 1 | 1: as the game scales them; 0: off. |
| **Kerbs and surface**, **Impacts** | `ffb_road`, `ffb_impact` | 0.25, 0.5 | The vibrations' strength at the game's strongest rumble, a fraction of the wheel's full force; 0: off. |
| **Physics-rate force** | `ffb_physics` | on | Off: the frame's torque instead of the physics steps'. |
| **Invert force** | `ffb_invert` | off | If the wheel test's direction pulse turned the wheel to the left. |
| **End stop** | `end_stop` | 1 | The end stop's strength, a fraction of the wheel's full force before the cap; 0: off, the wheel turns on to its own rotation. |

The force is zero at once outside a race you drive: in the menus, at the race's end, in
replays, when the AI drives your car, and when the window loses focus. When the game stops
updating the race (pause, photo mode, the rewind, loading), the force falls to zero within
about 0.1 seconds. After every such stop it ramps in again over 1.5 seconds. It also stops at
exit (closing the game window stops and releases the wheel before the process ends) and on a
crash (an unhandled exception filter and a SIGABRT handler stop the effects). A process that is
killed cannot stop them itself; what the wheelbase then does is part of the owner's test below.
Pit House's own settings (force feedback strength, road sensitivity, damper, friction, inertia)
apply on top of the game's.

### Racing wheel options

Every launcher row is a knob of `-Wheel`, and each knob sets a runtime cvar (also settable on
the Advanced page's **Extra cvars**). With the wheel on, the game logs a `Wheel report:` line, a
`Wheel: input` line with its mapping and a `Wheel: force feedback host started` line with the
force settings.

| Knob | Default | Runtime cvar |
| --- | --- | --- |
| `on` | 0 | `wheel`: opens the wheel; needs the SDL input backend (the default) |
| `input` | 1 | `wheel_input` |
| `steer_axis` | 0 | `wheel_steer_axis` |
| `steer_invert` | 0 | `wheel_steer_invert` |
| `steer_range` | 900 | `wheel_steer_range` (degrees) |
| `car_lock` | 900 | `wheel_car_lock` (degrees, the lock of a car with a 42-degree road-wheel lock) |
| `steer_lock` | 0 | `wheel_steer_lock` (degrees, one lock for every car; 0: each car's own) |
| `end_stop` | 1 | `wheel_end_stop` (0 to 1; 0: off) |
| `throttle_axis`, `brake_axis`, `clutch_axis` | 1, 2, -1 | `wheel_throttle_axis`, ... (-1: none) |
| `throttle_rest`, `brake_rest`, `clutch_rest` | -32768 | `wheel_throttle_rest`, ... |
| `throttle_pressed`, `brake_pressed`, `clutch_pressed` | 32767 | `wheel_throttle_pressed`, ... |
| `throttle_deadzone`, `brake_deadzone`, `clutch_deadzone` | 0 | `wheel_throttle_deadzone`, ... (0 to 0.9) |
| `throttle_gamma`, `brake_gamma`, `clutch_gamma` | 1 | `wheel_throttle_gamma`, ... (0.2 to 5) |
| `shift_up`, `shift_down`, `clutch_button`, `handbrake_button` | -1 | `wheel_shift_up_button`, ... (-1: none) |
| `ffb_cap` | 0.25 | `wheel_ffb_cap` |
| `ffb_gain` | 1 | `wheel_ffb_gain` |
| `ffb_min_force` | 0 | `wheel_ffb_min_force` (0 to 0.5) |
| `ffb_smoothing` | 10 | `wheel_ffb_smoothing` (milliseconds, 0 to 200) |
| `ffb_spring` | 1 | `wheel_ffb_spring` (0 to 2) |
| `ffb_damper` | 1 | `wheel_ffb_damper` (0 to 2) |
| `ffb_road` | 0.25 | `wheel_ffb_road` (0 to 1) |
| `ffb_impact` | 0.5 | `wheel_ffb_impact` (0 to 1) |
| `ffb_physics` | 1 | `wheel_ffb_physics` |
| `ffb_invert` | 0 | `wheel_ffb_invert` |

Cvars without a knob:

| Cvar | Default | Description |
| --- | --- | --- |
| `wheel_name` | empty | Only a joystick whose name contains this. |
| `wheel_ffb_sink` | `device` | `record` sends no force to any device and only records it (with `wheel_ffb_record`); the window's focus then does not stop the force, so a check without focus records it. |
| `wheel_ffb_record` | empty | A CSV file of every force sent: the time, the target and the force sent, the spring's and the damper's saturation and coefficient, the ramp, the block reasons and the governor's flags, the device update's cost, and the game's sample (whether you drive, its sequence number, the steering torque, the speed and the rear slip), the road's vibration and the impact sent, the impacts counted, the game's road and impact levels, the frame's force, whether the physics-rate force drives it, that force and its torque, the playback's delay and the points received, the wheel's angle, the lock and the end stop. |
| `wheel_virtual` | empty | For checks without hardware: an SDL virtual wheel driven by a CSV script (`time_s,steer,throttle,brake,buttons`, raw axis values interpolated between rows and a mask of buttons held; it repeats), and pads read without window focus. |

### Racing wheel test checklist

For the owner's MOZA R9 with the ESX wheel and the SR-P pedals, from an
[export](#export-to-another-computer). Keep one Pit House profile for every test and note its
settings (maximum angle, force feedback strength, road sensitivity, damper, friction,
inertia). Keep **Force cap** at 0.25 until step 30.

Stage 0, the device and the forces without the game:

1. Open Pit House with that profile, then the GUI launcher, Controls page, **Wheel test** tab.
   Leave **Force cap** at 0.25, turn **Record forces** on, then turn **Wheel test** on.
2. Check the device line: the name, `vendor 346e`, `SDL wheel`, `Force feedback: yes`.
3. Turn the wheel lock to lock and press each pedal fully: note which axis moves, and its
   values at both ends and at rest. Press each button and both paddles: note their numbers.
4. **Direction pulse 5%**: the wheel must turn to the right. If it turns left, note it,
   stop the test, turn **Invert force** on and repeat.
5. **Spring 10%**, then **Spring 25%**: the wheel must centre gently, ramping in over about
   1.5 seconds. **Stop force**.
6. **Sine 10%**, then **Sine 25%**: a slow back-and-forth. Note any roughness or clicks.
7. With a force running, Alt+Tab to another window: the force must stop at once. Return:
   it ramps in again.
8. With a force running, end `MotorsportDX.exe` in Task Manager: note whether the force stops,
   and how quickly.

Stage 2, driving with the wheel:

9. Start the launcher again, turn **Wheel test** on, and on the **Racing wheel** tab turn **Use
   a racing wheel** on. Set the steering axis and **Wheel rotation** to Pit House's maximum
   angle, leave **Car steering lock** at 900 and **Fixed steering lock** at 0. Turn the wheel
   with a steering row focused: **Steering now** at the right must read -1 at 450 degrees left
   (a road car's lock), 0 in the centre and 1 at 450 degrees right (else turn **Invert
   steering** on). If step 4 needed **Invert force**, turn it on in the Force feedback section
   too.
10. For the throttle and the brake: set each axis, then press A on **Rest** with the pedal
    released and on **Pressed** with it pressed fully; with a row of that pedal focused, **The
    pedal now** must go from 0 to 1 over the travel. Set the clutch pedal's axis to None (the
    SR-P has none).
11. Press A on shift up and shift down and then the paddle, the clutch on a button and the
    handbrake on another. Turn **Wheel test** off, then Play.
12. In the game's options, set the transmission to manual (and once to manual with clutch).
    Drive a lap of a known car and track in the cockpit view: the drawn steering wheel must
    follow the R9 1:1 at every angle up to the car's lock without lag; the car must respond to
    the smallest steering movement; the throttle and brake must be smooth from the first
    millimetre to the end of the travel; each paddle press must shift exactly one gear; the
    clutch button must disengage while held (manual with clutch), the handbrake button lock
    the rear wheels.
13. Finish a career race: the race result must show its credits and experience (the
    anti-cheat rule must not withhold them).
14. Note the steering feel: anything that looks rate limited, smoothed or exaggerated at
    speed (the game's STEERING assist setting changes it; try Simulation).

Stage 3, the game's force feedback:

15. In the launcher's **Force feedback** section, set **Force gain** to 0.25. Start a Quick
    Race and drive a lap: on the straight the wheel must centre; in corners it must pull
    towards the centre more the harder the car turns, and go light when the front or the
    rear slides. Note any knocks, buzzing or notches, and whether the wheel oscillates when
    you let go of it on a straight.
16. Repeat with **Force gain** 0.5, then try **Smoothing** 0 and 20 ms, and note which feels
    best.
17. Spin the car once: the force must not jump when the car comes to rest or drives off.
18. Pause during a corner: the force must stop within a fraction of a second, and ramp in
    again over about 1.5 seconds after RESUME. The same for photo mode and the rewind.
19. Alt+Tab during a corner: the force must stop at once.
20. Finish the race: at the finish line, when the AI takes over the car, and in the results
    and the replay the force must be zero.
21. Rate the centre feel (the game's spring: try **Centring spring** 0 and 1) and the loss of
    grip at the limit, and note the Pit House settings you used; say which **Force cap**
    still felt safe.

Stage 4, the physics-rate force and the effects:

22. Drive a lap at 60 fps and at 240 fps (Display page, **Race frame rate**) with **Force gain**
    0.5: the force must feel equally detailed at both, with no buzz on the straight. Then turn
    **Physics-rate force** off and on and note the difference.
23. Drive over kerbs and onto the grass: the wheel must vibrate on the kerbs and the grass, not
    on the track. Try **Kerbs and surface** 0, 0.25 and 0.5.
24. Brush a wall and hit one, and touch another car: each must give a short jolt; a scrape
    must not buzz continuously. Try **Impacts** 0, 0.5 and 1.
25. Pause on a kerb and during a scrape: the vibration must stop with the force.
26. In a replay and when the AI drives your car: no vibration and no force.
27. Note any knock when a vibration starts or ends, and which kerb and impact strengths feel
    right.

Stage 5, each car's steering lock and the end stop (Pit House at 900 degrees or more):

28. In a road car (the career's first cars), turn the wheel slowly to full lock both ways at
    a standstill and while driving: the car's steering must reach full lock at 450 degrees,
    and the wheel must stop there, softly at first and then firmly. Note how far past the lock
    you can push it, and whether the stop knocks, buzzes or bounces when you let go of the
    wheel against it.
29. Turn quickly into the stop, and let the wheel spin back from it: no oscillation at the
    stop, no jolt.
30. Try **End stop** 0.5 and 1, and a higher **Force cap** if 0.25 felt safe: note which holds
    best.
31. In a GT racing car (for example the Aston Martin DBR9 or the Corvette C6-R) and a prototype
    (for example the Audi R10 or the Peugeot 908): the stop must come at about 385 and 320
    degrees either way. In the cockpit view the drawn wheel must follow the R9 1:1 up to the
    stop; note how the hands look at large angles.
32. With **Fixed steering lock** 540, every car must stop at 270 degrees.

Before the races of steps 15 to 20, 22 to 26 and 28 to 32, add
`--wheel_ffb_record=<the launcher folder>\logs\wheel-forces.csv` (a full path) to the Advanced
page's **Extra cvars**, so the game records its forces. What to send back: the notes of steps
3 to 32 and the files that [bug reports](bug-reports.md#racing-wheel) list for a wheel.

## Keyboard

The game knows only the Xbox 360 pad; the runtime's keyboard driver turns keys into a
virtual pad. The launchers bind the keys in a racing layout by the function each pad input
has in the game's DEFAULT LAYOUT 1, and turn on a steering and pedal model: the steering,
throttle and brake move at limited rates instead of jumping to full, and their values are
mapped into the game's own deadzones, so a short press steers a little and a held key
steers more. The design and the measurements are in
[keyboard controls](analysis/keyboard-controls.md).

```powershell
.\outputs\FM4-build\Build.ps1 -Action run @common -Keyboard 'steer_attack=0.4,precision=0.6'
```

### Keys

| Function in a race | Keys | Pad input |
| --- | --- | --- |
| Gas | `W` | right trigger |
| Brake | `S` | left trigger |
| Steer left / right | `A` / `D` | left stick |
| Precision (hold): half lock and half pedal | `Ctrl` | |
| Shift up | `E`, `Backspace` | B |
| Shift down | `Q` | X |
| Handbrake | `Space` | A |
| Clutch | `Z` | LB |
| Look straight back | `V` | RB |
| Rewind | `R` | Y |
| Switch camera | `C`, `Tab` | Back |
| Pause | `Escape`, `Enter` | Start |
| Telemetry | arrow keys | D-pad |
| Free look | `I`, `J`, `K`, `L` | right stick |
| Toggle heat damage | `H` | right stick press |
| Switch between borderless fullscreen and the window | `Alt+Enter` | (`bind_fullscreen`) |
| Show or hide the settings overlay | `F2` | |

In the menus the same keys are the pad's buttons: the arrow keys move, `Space` selects
(A), `Backspace` or `E` goes back (B), `Q` and `R` are X and Y (for example ADVANCED and
RESET TO DEFAULT on the controller screen), `Z` and `V` are LB and RB, and `Enter` or
`Escape` is Start.

- The keyboard driver ignores keys pressed with Alt, so `Alt+Enter` does not also press
  Start. `F2`, `F3`, `F4`, `F7` and the backtick stay free for the overlays and the console.
- `Ctrl` works while the other keys are held: a modifier bound as a key of its own is left
  out when the other bindings compare their modifiers. `Ctrl+Escape` opens the Windows Start
  menu, so pause with `Enter` while holding precision.
- The keys press pad inputs, so the game's own controller options move the functions:
  with SWITCH GEAR UP/DOWN on, `E` shifts down. The game has 13 layouts (LAYOUT on the
  CONTROLLER LAYOUT screen); 8, 9, 12 and 13 move the gas and brake off the triggers, where
  the model does not reach them.
- The game's D-PAD STEERING (CONTROLLER LAYOUT screen) makes the D-pad, so the arrow keys,
  steer through the game's own handling instead of toggling the telemetry.
- The game's assists apply on top: STEERING (ASSISTED, NORMAL, SIMULATION), BRAKING,
  STABILITY CONTROL and TRACTION CONTROL on the ASSISTS & DIFFICULTY screen.
- The GUI launcher's Controls page edits the bindings on its **Key bindings** tab and keeps the
  ones you changed; X resets one to this layout.

### What changed for keyboard players (25 October 2026)

- The steering, throttle and brake ramp up and back instead of jumping to full;
  `-Keyboard smooth=0` (or Smooth steering and pedals off on the Controls page) gives the
  digital keys back with the same layout.
- `W` and `S` no longer push the left stick up and down, and the arrow keys no longer move
  the right stick. The menus still move with the arrow keys; free look is on `I`, `J`, `K`,
  `L` (or the mouse with "Mouse moves the right stick").
- Shift up and down are `E` and `Q` (`Backspace` still presses B). LB, RB and Y moved from
  `1`, `3` and `P` to `Z`, `V` and `R`; `Escape` pauses, `C` switches the camera, `Ctrl` is
  the new precision key and `H` toggles the heat damage.
- Bindings you changed in the GUI launcher stay as you set them. Outside the launchers the
  runtime keeps its own defaults and digital keys.

### -Keyboard knobs

Knobs are `name=value`, separated by commas; a later knob wins. A knob at its default sets
nothing. The GUI launcher's Controls page has them on its Keyboard tab.

| Knob | Values | Default | Description | Runtime setting |
| --- | --- | --- | --- | --- |
| `smooth` | 0, 1 | 1 | The steering and pedal model; 0: digital keys, full at once. | `mnk_smooth` |
| `steer_attack` | 0 to 2 s | 0.3 | Seconds from centre to full lock while a steering key is held. | `mnk_steer_attack` |
| `steer_release` | 0 to 2 s | 0.2 | Seconds from full lock back to centre after it is released. A reversal (the other key) runs back to centre at the faster of the attack and the release, then on at the attack. | `mnk_steer_release` |
| `throttle_attack`, `throttle_release` | 0 to 2 s | 0.25, 0.1 | The same for the throttle. | `mnk_throttle_attack`, `mnk_throttle_release` |
| `brake_attack`, `brake_release` | 0 to 2 s | 0.15, 0.1 | The same for the brake. | `mnk_brake_attack`, `mnk_brake_release` |
| `precision` | 0.1 to 1 | 0.5 | The share of full lock and full pedal while the precision key is held. | `mnk_precision` |
| `title_deadzones` | 0, 1 | 1 | Map the keys into the game's own deadzones (read live from the game, so a lowered in-game deadzone gives no jump); 0 for comparisons. | `mnk_title_deadzones` |
| `mouse_steering` | 0, 1 | 0 | The mouse's horizontal motion steers, with auto-centring; the right stick then gets no mouse motion. | `mnk_mouse_steering` |
| `mouse_steer_sensitivity` | 0.1 to 10 | 1 | Thousandths of full lock per count of mouse motion. | `mnk_mouse_steer_sensitivity` |
| `mouse_steer_centering` | 0 to 5 s | 1 | Time constant of the return to centre while the mouse rests; 0 turns it off. | `mnk_mouse_steer_centering` |

With both steering keys held, the one pressed last wins. The model runs on the clock, not
per frame, so it behaves the same at 60, 120 and 240 fps. A connected pad and the keyboard
work together on player 1: each stick comes from the device that deflects it further, the
triggers from the one pressed further.

### Keyboard test checklist

For the owner's test on the laptop, from an [export](#export-to-another-computer) with the
GUI launcher. Play a Quick Race with the default car and track in each profile, at least two
minutes with corners in both directions, braking into a hairpin and a kerb or two. Keep the
game's assists as they are (the export's profile), and note them.

| Profile | Keyboard section |
| --- | --- |
| A | the defaults |
| B | Steering attack 0.2, Steering release 0.15 |
| C | Steering attack 0.45, Steering release 0.3 |
| D | Smooth steering and pedals off (the earlier digital keys) |
| E | the defaults, with the game's D-PAD STEERING on, steering with the arrow keys |
| F | the defaults, Mouse steering on |

- For each profile: can you hold a steady line through a long corner, correct a slide
  without overcorrecting, and keep the car straight on a straight? Which steering attack
  feels right?
- Throttle and brake: can you feed in the throttle out of a corner without wheelspin, and
  brake hard enough? Do the throttle and brake rates need changing?
- Precision (`Ctrl`): useful in tight corners and on the throttle? Is 0.5 the right share?
- Menus: do the arrow keys, `Space`, `Backspace`, `Enter` and `Escape` work everywhere you
  went? Anything that does not respond, or presses twice?
- Profile E: how does the game's own D-pad steering compare with the defaults?
- Profile F: is mouse steering usable, and how does the auto-centring feel?
- Note the race frame rate (the laptop's panel) and the steering deadzone in the game's
  options, if you changed it.

Nothing needs to be sent from the logs for this test; the notes are the result.

## Shader cache

`-Action warmcache` runs the e2e Quick Race timeline once, without a network connection,
so that the shader and pipeline cache under `BuildRoot\cache` is filled before you play.
Later runs then skip those compilations. Without them the game does not wait for the look:
a shader whose look mod is not in the cache draws with the game's own look while the mod
compiles in the background, and takes the look a moment later, usually within a second or
two ([D3D-level HLE, 19.3.7](analysis/d3d-hle-n6.md#1937-increments-10-and-11-27-october-2026);
`--shader_override_async=false` compiles in place instead and pauses the game for it, as
before 27 October 2026). The compiled look mods are kept in
`BuildRoot\cache\shader-overrides`, one entry per shader, look and render scale; the
directory can be deleted at any time. The run details are written to
`BuildRoot\e2e\warmcache`, and the log to `BuildRoot\logs\warmcache.log`.

```powershell
.\outputs\FM4-build\Build.ps1 -Action warmcache @common
```

Play the game once with `run` (or the GUI launcher) before `warmcache`, up to the main menu.
The first start of a new profile plays the game's long intro video, the gamma adjustment and
the intro race, which the timeline does not drive: on a new BuildRoot `warmcache` stops at
"PLAY CAREER timed out" (seen on 25 October 2026), and its log says to play once first.
`warmcache` imports Disc 2 and the downloadable content of `local.json` before its launch when
they are not imported yet, as `run` does, and fails when the Quick Race does not reach the
race, for example at the INSERT DISC 2 prompt of a BuildRoot generated without `-Disc2`
([e2e race check](e2e/README.md#race-check)). The timeline also reads the menus with Windows OCR in English (United States), as the
e2e harness does ([e2e](e2e/README.md)); without that language and its optical character
recognition, `warmcache` cannot find the menus. Playing fills the same cache.

## Data directories

Everything the launcher writes is under `BuildRoot`, or under the export folder for
the GUI launcher of an [export](#export-to-another-computer), which has the same
`user-data`, `cache` and `logs` directories. The full layout is in
[BuildRoot layout](setup.md#buildroot-layout).

- **Profile:** each run uses a new local profile in `BuildRoot\user-data`. Saves from
  earlier builds are not copied into it automatically.
- **Disc 2 and downloadable content:** imported packages are stored in the profile
  under `BuildRoot\user-data\0000000000000000\4D530910\00000002`, one directory per
  package, with a `<package>.import-complete` marker in `BuildRoot\user-data`. They
  stay installed when a later run no longer lists them. To remove one, delete its
  directory, its marker and its `.header` file in `4D530910\Headers\00000002`.
- **Discs:** the discs are read in place. Writes relative to the game directory are
  disabled (`allow_game_relative_writes=false`).
- **Cache:** game and shader cache files are in `BuildRoot\cache`.
- **Logs:** the launcher log is `BuildRoot\logs\play.log`, and the runtime log is
  `BuildRoot\logs\runtime.log`.
- **e2e output:** captures, logs and `summary.json` of e2e runs are in
  `BuildRoot\e2e`. The captures show the game, so keep them out of the repository.
  See [e2e/README.md](e2e/README.md).

Nothing is uploaded by `run`. Never commit or publish `BuildRoot`.
