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
on the development laptop.

## What works

On the latest build the automated tests reach the intro and the menus, Quick
Race at 60, 120 and 240 Hz, photo mode (take, save and view a photo), career races through to
their rewards with replays, and one downloadable content car on a downloadable content track.
The project owner has played Quick Race, career, photo mode and replays on both computers
above.

## Known problems

- **The front-end menus, the pre-race screens and photo mode are less sharp than races** at
  a 1080p screen (and at other fractional scales) on the native path: they render at the
  whole render scale below the screen's and are scaled up. Races, their HUD, replays and the
  post-race screens render at the screen's resolution. A fix is in design.
- **Wide and tall screens** (21:9, 16:10) fill the screen only on the native path (`-Native`,
  the default with integrated GPUs only), with the HUD and menus in the centred 16:9. With a
  discrete GPU the default path (RTV) shows a 16:9 picture with black bars; add `-Native` for
  the full width.
- **The cockpit view in the game's own look is very dark on sunny tracks.** This is the game's own
  interior lighting. The default look fills the interior with the game's own sky light since
  26 October 2026 (`cockpit_fill`), which keeps the gauges readable; `-Look original` shows the
  game's dark interior.
- **AMD FSR 3.1** (`aa=fsr` and `-Upscale`) is new: it needs a build with `-Fsr on`, and it is
  not the default. On the ROG Xbox Ally X it costs about four times as much GPU time as TAA.
- **Controllers:** the ROG Xbox Ally X's built-in pad works and rumbles (the owner's runs; it
  has no trigger rumble). The other pad options were checked with a virtual pad only; an Xbox
  Series controller, PlayStation pads and racing wheels have not been tested, and the
  `gameinput` pad driver starts but has not run a pad yet. The game's STEERING AXIS DEADZONE
  INSIDE cannot go below about 24 in practice: to steer from the first stick movement, use the
  runtime's steering anti-deadzone (`-Pad 'anti_deadzone=1,stick_deadzone=0.05'`, see
  [the game's steering deadzone](running.md#the-games-steering-deadzone)) and leave the game's
  setting at 24.
- **Keyboard:** the smooth steering and pedals are new and tested with injected keys only;
  the game's D-PAD STEERING and mouse steering are untested.
- **The first press in some menus is ignored** by the game after several screens open; press
  again.
- **Short pauses on the first visit to a track or screen** while shaders compile. They go
  away on later visits (the shader cache).
- **Sun glare** looks different from the Xbox and between quality settings; this is a known
  limit of the emulated GPU, and the default look evens it out.
- **16-car races at the Low and Medium race quality** (and `xbox`, Medium's earlier name) use
  the game's cheaper 16-car car shader. Forced onto a 12-car race, it painted the player's red
  car pink to white in the cockpit view on every render path; a real 16-car race has not been
  checked, nor whether the Xbox shows the same. High and above use the full car shader. Before 26 October 2026 the Xbox race quality also ran races
  without the look's ambient occlusion, anti-aliasing and cockpit glass; the five quality
  presets fixed that.
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
- A full career season, most cars and tracks, and most cars of the downloadable content packs.
- Autovista, photo overwrite, and long sessions (two hours or more: memory and stability).
- Lap times and handling at 120 or 240 fps against 60 fps.
- Other GPUs, desktops and processors (see [Tested hardware](#tested-hardware)), HDR displays.
