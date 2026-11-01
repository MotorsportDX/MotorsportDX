# Running Motorsport DX

This guide covers launching a finished build with `Build.ps1 -Action run`: what
the launcher checks and sets up, its options, the shader cache and the data
directories. The look presets and knobs are in [looks and textures](running-look.md),
and pads, racing wheels and the keyboard in [controls](running-controls.md). The same options are in the [GUI launcher](#gui-launcher), which
also runs the game on a computer without the build tools after an
[export](#export-to-another-computer). Build the game first; see [setup.md](setup.md).

Performance numbers in this file describe the launcher defaults, and the option
details keep the measurements that explain each option. Measurements that depend on
a specific build are dated in the status documents: [status/current.md](status/current.md)
and [status/history.md](status/history.md).

## Contents

- [Launching](#launching)
- [GUI launcher](#gui-launcher)
  - [Launcher screenshots](#launcher-screenshots)
  - [Shaders page](#shaders-page)
  - [Ally X run set](#ally-x-run-set)
  - [Export to another computer](#export-to-another-computer)
- [Launcher defaults](#launcher-defaults)
  - [Render path and frame pacing](#render-path-and-frame-pacing)
- [Options](#options)
  - [Option details](#option-details)
- [Looks and textures](running-look.md), a page of its own
  - [Look](running-look.md#look)
  - [Texture replacement](running-look.md#texture-replacement)
- [Controls](running-controls.md), a page of its own
  - [Controller](running-controls.md#controller)
  - [Racing wheel](running-controls.md#racing-wheel)
  - [Keyboard](running-controls.md#keyboard)
- [Shader cache](#shader-cache)
- [Data directories](#data-directories)

## Launching

```powershell
.\outputs\FM4-build\Build.ps1 -Action run -BuildRoot 'D:\FM4-build' -MingwRoot 'D:\Tools\llvm-mingw-20250709-ucrt-x86_64' -Python 'C:\Python311\python.exe'
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
switch to a window (see [Keyboard](running-controls.md#keys)).

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
| Look | Six tabs of option rows. **Preset:** the preset (`aces`, `gt`, `neutral`, GT7 style `gt7`, FM2023 style `fm2023`, `original`, `original` without TAA, or a saved look; changing it drops the knob changes), the tone curve, temporal anti-aliasing and its method (TAA, FSR 3.1 or TAAU, `aa`), the exposure speed of 60 fps (`adaptation`), **Save as look**, which keeps the current value under a name, **Delete this look** for a saved look, after a confirmation, and **Reset all changes**. **Picture, Lighting, Car, Sky & foliage and Road:** a slider row for each [look knob](running-look.md#look) of that group, which left and right step by about a fortieth of its range; the right panel says what the knob does and the preset's value. |
| Controls | Five tabs of option rows. **Controller:** the options of `-Pad` (the pad driver, rumble strength, trigger rumble and its strength, the steering anti-deadzone, the input thread, then the stick and trigger shaping and the extra buttons); see [Controller](running-controls.md#controller). **Racing wheel:** the wheel in the game (`-Wheel`: steering, the three pedals, the buttons and force feedback), with the steering's or the focused pedal's value at the right while the wheel test runs; see [Racing wheel in the game](running-controls.md#racing-wheel-in-the-game). **Wheel test:** the [wheel test](running-controls.md#racing-wheel-test), with the device and its axes, buttons and force at the right. **Keyboard:** the options of `-Keyboard` (smooth steering and pedals, their rates and the precision share, then the mouse and the deadzones); see [Keyboard](running-controls.md#keyboard). **Key bindings:** the keyboard bindings, by their function in the game's DEFAULT LAYOUT 1 (the runtime's `keybind_*` settings). A waits for a key, which replaces the keys; several keys are typed in the field at the right, separated by commas, with modifiers written as `Shift+Up`. Unknown key names are marked and block Play. A connected controller needs no setup. |
| Game data | Two tabs. **Discs:** Disc 1 and Disc 2, checked as `generate` checks them, **Unpack a disc image (ISO)**, and the texture pack (the `-TextureReplace` folder). A opens the folder picker, X clears Disc 2 or the texture pack, and the right panel shows the check's result and a field to type the folder. On **Unpack a disc image (ISO)**, A opens the file picker for one ISO or both: each is recognised as Disc 1 or Disc 2 by its files and unpacked in the background into `discs\disc1` or `discs\disc2` of the export folder (or the BuildRoot), as `generate` does ([Disc images](setup.md#disc-images)); the row shows the percentage, and when it is done the Disc is set to that folder. A wrong image or another release stops in seconds, and the same image is not unpacked twice. A Disc set to a file is refused with a pointer to this row. **Content:** rows that add downloadable content folders and files, the folders and files searched (A removes one from the list), and the content packages found, with their import state; A on an imported package removes the import from the user data after a confirmation (see [Data directories](#data-directories)). The right panel shows the package's path and size and what the next Play imports. |
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
the environment and the display description of 62 sets of options (RTV, `-Rov` and
`-Native`) on nine monitors with both (558 plans), check the Discs and content packages
of `local.json` with both, and read and unpack synthetic disc images with the launcher's
port of the ISO reader. A difference fails the build.

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


### Render thread idle wait A/B

The A/B of `fm4_render_thread_idle_wait` (30 October 2026, off by default; [career season soak, 12](analysis/career-season-soak.md#12-the-render-threads-idle-wait-30-october-2026)) with the GUI launcher of an export: **Show developer options** on, **Performance trace** on, render scale 1x, the default look and the race frame rate the screen allows. Two profiles, alike but for the extra cvars of the second, `--fm4_render_thread_idle_wait=false` (the wait is on by default since 31 October 2026). Run each as a Quick Race of at least three minutes, then quit to the menu, look at the post-race screens and close the game; once on battery and once on AC. Send back the `perf-<profile>-<time>.jsonl` files, `play.log` and `runtime.log`, and the battery's drain if the device shows it.
### Export to another computer

`export` copies what the launcher needs into a folder outside the BuildRoot and the
repository:

```powershell
.\outputs\FM4-build\Build.ps1 -Action export -BuildRoot 'D:\FM4-build' -MingwRoot 'D:\Tools\llvm-mingw-20250709-ucrt-x86_64' -Python 'C:\Python311\python.exe' -Out 'E:\FM4-portable'
```

The folder holds `MotorsportDX.exe`, `game-build` (the executable, the two modules, the
runtime DLLs and the C++ runtime `libc++.dll` and `libunwind.dll` of `build-result.json`), `look`, `passes`, the shader cache (`cache`),
`launcher.json`, `build-result.json`, `CREDITS.txt` (the projects that made it possible), the
project's own license (`LICENSE.txt`), the license notices of the third-party code in the
binaries (`third-party-notices`), the
launcher's backdrop when the BuildRoot has one and the folder has none (a local picture, see
[GUI launcher](#gui-launcher)), and a first `launcher-settings.json` with the
Disc and content paths of `local.json`. It does not hold `user-data`: profiles, saves
and imported content stay on the build computer. It does not hold the DirectX Shader
Compiler (`dxcompiler.dll` and `dxil.dll`) either: its license does not allow passing it on,
so the launcher's [Shaders page](#shaders-page) fetches it on the other computer, and without
it the shaders compile with FXC ([third-party code](sources-and-licensing.md#third-party-code-directx-shader-compiler)). `export` checks the artifact hashes
first, and refuses a folder that is not empty unless it is an earlier export, which it
updates and whose `launcher-settings.json` it keeps.

On the other computer:

1. The GPU must support Direct3D 12. Nothing needs installing for an export of the default
   build (llvm-mingw, since 1 November 2026): its C++ runtime is in `game-build`. An export of an
   `-Abi msvc` build, or of a build before 1 November 2026, needs the Microsoft Visual C++
   2015-2022 Redistributable (x64), recent enough to have `MSVCP140_ATOMIC_WAIT.dll`.
2. Copy the export folder and your discs to it, extracted or as ISOs.
3. Start `MotorsportDX.exe`, set Disc 1, Disc 2 and the content on the Game data page
   (for ISOs, **Unpack a disc image (ISO)** unpacks them into the export's `discs` folder,
   about 10.5 GB for both),
   let the Shaders page run the shader pass (it fetches the DirectX Shader Compiler first,
   32 MB, when the computer is online), and press Play. The first Play imports Disc 2 and the content packages into
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
  and the glass, chrome and rim mods. See [Look](running-look.md#look).
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
2026, [current status](status/history-results.md#one-tile-with-taa-the-presets-and-taas-history-20-october-2026)).
On the native default at 240 Hz (26 October 2026, [native default](analysis/native-default.md#23-performance)),
a 2560x1600 screen's race (2560x1600 with 2x MSAA) ran at 171.6 to 175.1 fps with 5.60 to
5.70 ms of GPU time a frame, against 162.7 to 165.9 fps and 5.81 to 6.01 ms on RTV at 2x
(2560x1440, letterboxed).

### Render path and frame pacing

The launcher uses these runtime settings (set in `launch_cvars` in
[pipeline.py](../outputs/FM4-build/pipeline.py)):

- Rendering uses the host render targets, `render_target_path_d3d12=rtv` with
  `edram_single_rt_no_wrap=true`, on the native path (the default) and on RTV. Earlier
  measurements put the GPU time at about 2.5 ms per race frame on RTV, against about 7 ms
  on the pixel shader interlock path (`-Rov`). Quick Race runs at a stable 60 fps.
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
- The title's render thread waits for its next queued frame, at most 1 ms, instead of turning its frame
  loop and recomputing the gamma ramp between frames (`fm4_render_thread_idle_wait=true`, since 31 October
  2026; [career season soak, 12](analysis/career-season-soak.md#12-the-render-threads-idle-wait-30-october-2026)).
  `-Rov` keeps the title's loop.
- The runtime asks for 1 ms timer resolution and opts the process out of power throttling
  (`high_resolution_timing=true`, a runtime default on every path, `-Rov` included, since it is a
  host setting and not a title correction; [section 17.19](analysis/d3d-hle-n4.md)): the guest's
  1 ms sleeps keep their length, also where Windows would throttle the process.
- The road sections that this path used to leave undrawn came from the title's track
  streaming. They are fixed for every render path.

Known limits of the host render targets: the sun glare has not been calibrated on them (see
[status/current.md](status/current.md#open-issues)). Interactive Autovista has not
been tested on them. If rendering misbehaves, compare with `-Rtv` and `-Rov`. Which scenarios have passed is recorded
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
| `-Look <preset/knobs>` | `aces` | Post-processing look and car paint. See [Look](running-look.md#look). |
| `-Pad <knobs>` | `rumble=1.5` | The physical controller: the SDL pad driver, rumble strength, trigger rumble, the input thread, stick and trigger shaping and the extra buttons. See [Controller](running-controls.md#controller). |
| `-Keyboard <knobs>` | none | The keyboard's steering and pedal model: its rates, the precision share and mouse steering. See [Keyboard](running-controls.md#keyboard). |
| `-ShaderDev` | off | Shader mod development: the look mods reload when a file in `work/fm4-rex/look` is saved, and the knobs change live. See [Shader mod development](running-look.md#shader-mod-development). |
| `-TextureDump <dir>` | off | Write each new game texture once as `<hash>.dds` to this directory. See [Texture replacement](running-look.md#texture-replacement). |
| `-TextureReplace <dir>` | off | Use `<hash>.dds` files from this directory instead of the game textures with that hash. See [Texture replacement](running-look.md#texture-replacement). |
| `-Info` | off | Show the settings overlay at start. F2 toggles it in any run. |
| `-Rov` | off | Use the earlier pixel shader interlock render target path, the uncorrected reference, for comparisons. |
| `-Native` | on, unless `-Rtv`, `-Rov` or `-Msaa` | The native path: a host render thread consumes the guest's commands from a record queue instead of the command processor thread, and races render in one pass with 2x MSAA. See [-Native](#-native). |
| `-Rtv` | off | RTV, the PM4 path on host render targets, instead of the native path: the players' fallback, frozen without new features since 27 October 2026. |
| `-SingleTile` | off | Render races as one 720p tile without MSAA instead of the native path's one pass with 2x MSAA; on RTV also with a look without TAA, then with FXAA on the swap (on RTV a look with TAA, every preset, renders one tile anyway). |
| `-Msaa` | off | Keep the tiles and MSAA of `-Quality` with a look that has TAA. |
| `-MsaaSamples 2`, `4` or `0` | 2 on the native path | On the native path: races in one pass with 2x MSAA (2), in one pass with 4x MSAA (4), or in one tile without MSAA (0). See [-Native](#-native). |
| `-FixedVblank` | off | Hold every swap to the fixed 60 Hz vblank grid. |
| `-FullRateMirror` | off | Render the rear-view mirror in every frame of every race. |
| `-ShadowWorkaround` | off | Render the shadow map only with the cockpit camera, as the old Xenia workaround did. |

`-Action warmcache` is not a run option; it is described in
[Shader cache](#shader-cache). The `--ao` flag of `pipeline.py` and `e2e.py` has no
switch in `Build.ps1`. It runs the GTAO pass without a look applying it; see
[Look](running-look.md#look).

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
([Exposure adaptation](running-look.md#exposure-adaptation)).

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
with Quick Race: 60.0 fps, with about 7.1 ms of GPU time per frame (2.5 ms on RTV, the
default then). Its occlusion reports are the counted guest samples, without a title
correction (since 18 October 2026 the launcher no longer passes
`rov_occlusion_awaited_report_divisor=2`). Use it as the reference for the title's own output when rendering misbehaves on the
default path. No gate runs it since 28 October 2026; its stored captures answer questions
about the title's own output.

Shader mods and ambient occlusion run on the host render target paths only (the native path
and RTV), by design: with `-Rov` the runtime
turns off the shader overrides ("Shader overrides need bindless resources and host
render targets" in the log), so every look mod and the AO are gone and the picture is
the title's own. Compare the default path against `-Rov` with `-Look "original,taa=0"`
(no mods and no TAA; `-Look original` keeps TAA).

#### -Native

The native path is the default, unless `-Rov` or `-Msaa` is given; `-Rtv` (the GUI launcher's render path "RTV") runs RTV, the players' fallback: it gets no new features and no gate runs since 27 October 2026. It became the default with integrated GPUs only on 24 October 2026, where on the ROG Xbox Ally X it races at 110.5 fps on battery against RTV's 100.1, with p99 15.3 against 19.8 ms ([D3D-level HLE, 17.25](analysis/d3d-hle-n4.md#1725-the-second-rog-xbox-ally-x-run-24-october-2026)), and on every computer after the readiness report of 26 October 2026 ([native default](analysis/native-default.md)): on the development laptop's 2560x1600 panel it fills the screen and takes 4% less GPU time a race frame than RTV's letterboxed 2x. The reference comparison (RTV against `-Rov` with `-Look "original,taa=0"`) needs `-Rtv`, since a look without TAA runs the native path in one pass with 2x MSAA.

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
- `-Native` also sets `native_rt_surfaces=2`, or 26 in one pass, 58 at a whole 16:9 scale of 2
  or more, and bit 64 with the menus at the screen's scale (stage N3,
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

On RTV (`-Rtv`), races render one 720p tile without MSAA whenever the look has TAA (`taa=1`, every
preset since 20 October 2026), at every `-Quality`; the native path, the default, renders them in
one pass with 2x MSAA instead ([-Native](#-native)). This is the title's own UI tiling
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

The look presets and knobs (`-Look`), the cockpit, exposure, sun, track lighting, ambient
occlusion, TAA and cockpit glass, shader mod development and texture replacement are in
[looks and textures](running-look.md).

## Controls

<a id="controller"></a><a id="racing-wheel"></a><a id="racing-wheel-test"></a><a id="keyboard"></a>
Pads (`-Pad`), racing wheels (`-Wheel`) and the keyboard (`-Keyboard`), with their keys and the
owner's test checklists, are in [controls](running-controls.md): [controller](running-controls.md#controller),
[racing wheel](running-controls.md#racing-wheel) and [keyboard](running-controls.md#keyboard).

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
  disabled (`allow_game_relative_writes=false`). Discs given as ISOs are unpacked once
  into `BuildRoot\discs` (or the export's `discs`), and that folder is read in place.
- **Cache:** game and shader cache files are in `BuildRoot\cache`.
- **Logs:** the launcher log is `BuildRoot\logs\play.log`, and the runtime log is
  `BuildRoot\logs\runtime.log`.
- **e2e output:** captures, logs and `summary.json` of e2e runs are in
  `BuildRoot\e2e`. The captures show the game, so keep them out of the repository.
  See [e2e/README.md](e2e/README.md).

Nothing is uploaded by `run`. Never commit or publish `BuildRoot`.
