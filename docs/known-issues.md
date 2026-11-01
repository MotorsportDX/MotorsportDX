# Known issues

Motorsport DX is an experimental build. This page lists, in plain terms, what is known not to work,
what has not been tried, and the hardware it has run on. The technical state and its evidence
are in [status/current.md](status/current.md). To report a problem, see
[bug reports](bug-reports.md).

## Tested hardware

| Computer | Processor and GPU | Screen | How it ran |
| --- | --- | --- | --- |
| Development laptop | Intel Core Ultra 7 255HX, NVIDIA GeForce RTX 5070 Ti Laptop GPU (the internal panel is wired to the Intel graphics, the game renders on the RTX) | 2560x1600 at 240 Hz | built here; every automated test |
| ROG Xbox Ally X | AMD Ryzen AI Z2 Extreme, Radeon 890M (integrated) | 1920x1080 at 120 Hz | an [export](running.md#export-to-another-computer) from the laptop, run by the project owner |

Both run Windows 11. No other GPU (other NVIDIA cards, AMD discrete cards, Intel Arc), no
desktop, no Windows 10 computer and no lower-end processor has been tried. A second computer
has not yet built the game from a clean clone; on 25 October 2026 a clean clone built and ran
on the development laptop. The ROG Xbox Ally X last ran exports of 24 and 25 October 2026: the
latest build, with the launcher's new look and its shader pass, has not run there yet.

Since 1 November 2026 the game is built with llvm-mingw instead of Microsoft's Visual Studio
libraries ([setup, toolchain](setup.md#toolchain)). Building needs no Visual Studio and no Windows
SDK, and the game no longer needs the Visual C++ Redistributable: its C++ runtime, `libc++.dll` and
`libunwind.dll`, lies beside it in `game-build`. On the development laptop the new build plays as
the old one did (the same frame rates and the same picture where a screen does not depend on the
race); on the ROG Xbox Ally X it has not run yet.

Racing wheels: **none tested yet**. The planned test set is the project owner's MOZA Racing R9
base (direct drive) with the ESX steering wheel and the SR-P pedals (two pedals, no clutch
pedal), run from an export with the [wheel checklist](running-controls.md#racing-wheel-test-checklist).
Every wheel result so far comes from a virtual wheel and recorded forces on the development
laptop.

## What works

On the latest build the automated tests reach the intro and the menus, races at 240 Hz
(60 and 120 Hz were last checked on an earlier build), photo mode (take, save and view a photo), career races through to
their rewards with replays, and one downloadable content car on a downloadable content track.
A 2-hour career session played 19 World Tour events in a row without a stop, through the end
of two divisions into the third.
The project owner has played Quick Race, career, photo mode and replays on both computers
above.

## Known problems

- **Wide and tall screens** (21:9, 16:10) fill the screen in the menus, photo mode, races and
  replays, with the HUD and the menus' panels in the centred 16:9; a photo saved there is the
  centred 16:9 of the picture. `-Rtv` (the earlier default path) shows a 16:9 picture with bars.
- **The native path is the default render path on every computer** (before, only with
  integrated GPUs). It has run on two GPUs only (see [Tested hardware](#tested-hardware)): if
  a picture looks wrong or the game stops on your GPU, try the launcher's render path "RTV"
  (`-Rtv`) and say in your [bug report](bug-reports.md) whether it changes anything. RTV is no
  longer tested since 27 October 2026, so it can have problems of its own.
- **The cockpit view in the game's own look is very dark on sunny tracks.** This is the game's own
  interior lighting. The default look fills the interior with the game's own sky light since
  26 October 2026 (`cockpit_fill`), which keeps the gauges readable; `-Look original` shows the
  game's dark interior.
- **AMD FSR 3.1** (`aa=fsr` and `-Upscale`) is new: it needs a build with `-Fsr on`, and it is
  not the default. On the ROG Xbox Ally X it costs about four times as much GPU time as TAA.
- **Controllers:** the ROG Xbox Ally X's built-in pad works and rumbles (the owner's runs; it
  has no trigger rumble). The other pad options were checked with a virtual pad only; an Xbox
  Series controller and PlayStation pads have not been tested, and the
  `gameinput` pad driver starts (also in the llvm-mingw build) but has not run a real pad yet. The game's STEERING AXIS DEADZONE
  INSIDE cannot go below about 24 in practice: to steer from the first stick movement, use the
  runtime's steering anti-deadzone (`-Pad 'anti_deadzone=1,stick_deadzone=0.05'`, see
  [the game's steering deadzone](running-controls.md#the-games-steering-deadzone)) and leave the game's
  setting at 24.
- **Racing wheels** are new and off by default (`-Wheel 'on=1'`, or the launcher's Controls
  page, [racing wheel](running-controls.md#racing-wheel)): the steering, pedals and shift buttons drive
  the car with each car's own steering lock and an end stop past it, and the game's steering
  torque drives the force feedback, with kerb, grass and impact vibrations. They were checked
  with a virtual wheel and recorded forces only; no real wheel has been tested yet (see
  [Tested hardware](#tested-hardware)), so the force's direction, feel and strength, the end
  stop's firmness and whether it oscillates on a direct-drive base are untested. Keep the
  force cap at its default 0.25 until then. The menus still need the controller or the
  keyboard. The cockpit's drawn steering wheel follows the wheel up to the car's lock, but
  past about 90 degrees the driver's hands keep one pose. Only cars with the most common
  steering lock have been driven in the checks; GT racing cars and prototypes have a smaller
  lock, covered by unit tests only.
- **Keyboard:** the smooth steering and pedals are new and tested with injected keys only;
  the game's D-PAD STEERING and mouse steering are untested.
- **The first press in some menus is ignored** by the game after several screens open; press
  again.
- **The first start after an install or an update compiles the shaders.** The launcher opens
  its Shaders page and runs the shader pass by itself. On a fast PC it takes about a minute, and
  longer on a slower one such as the ROG Xbox Ally X (not measured yet). The first time, the
  launcher also downloads Microsoft's DirectX Shader Compiler (32 MB) from GitHub. Without an
  internet connection the shaders compile with the older compiler (FXC) instead, as before.
  Skip is allowed: what is left then compiles while you play.
- **The game's own look for a moment on the first visit to a track or screen**: shaders the pass
  could not know about (mostly those of a car's or a track's first draw) compile in the
  background, and until each is ready (usually within a few seconds of a load) some surfaces, or
  the whole picture, show the game's original colours. Short pauses can still come in the loads
  while new graphics states are created. Both go away on later visits (the shader cache).
- **Sun glare** looks different from the Xbox and between quality settings; this is a known
  limit of the emulated GPU, and the default look evens it out.
- **16-car races at the Low and Medium race quality** (and `xbox`, Medium's earlier name) use
  the game's cheaper 16-car car shader. Forced onto a 12-car race, it painted the player's red
  car pink to white in the cockpit view on every render path; a real 16-car race has not been
  checked, nor whether the Xbox shows the same. High and above use the full car shader. Before 26 October 2026 the Xbox race quality also ran races
  without the look's ambient occlusion, anti-aliasing and cockpit glass; the five quality
  presets fixed that.
- **Memory use grows while new tracks and cars load, then levels off.** The game keeps what it
  loaded for each track and car: in a 3-hour test over the same 11 tracks and cars, its memory
  rose by about 3.4 GB and its video memory by about 1.7 GB on the first round, and stayed level
  on the third. A small number of system handles is not given back (about 15 a race); that is
  harmless at this rate and will be fixed. The frame rate did not drop in either test.
- **Short pauses on the screens after a race:** the gift car's paint screen and the level
  screens can pause for 0.15 to 0.4 s. The game itself does this work there (saving the
  profile, drawing a new car for the first time). Races themselves had no pause over 50 ms.
- **Saves:** each build starts with its own profile in the BuildRoot. Saves from other
  builds, other computers or an Xbox are not imported, and moving a profile between builds is
  not guaranteed to work.
- **Only the Japan/Asia and USA releases** with the exact executables of their discs are
  accepted. The European release and title updates are not supported.

## Not tested yet

"Not tested" does not mean broken: these parts have simply not been checked on the current
build, and reports about them are welcome.

- Audio as a whole: music, engine sounds, 5.1 output, and whether speech and the intro video
  stay in sync.
- Split screen, Rivals, Hot Lap, multiplayer, online play and Kinect (Kinect and online
  services are not expected to work).
- The career past the third division, buying a car in the career, most cars and tracks, and
  most cars of the downloadable content packs.
- Autovista, photo overwrite, sessions longer than two hours, and long sessions outside the
  career.
- Lap times and handling at 120 or 240 fps against 60 fps.
- Other GPUs, desktops and processors (see [Tested hardware](#tested-hardware)), HDR displays.
- Disc images (ISOs) of the Japan/Asia release: the launcher unpacks them as it unpacks the
  USA ones, which were checked file by file; the Japan/Asia images were not at hand.
