# Mods

Part of [running Motorsport DX](running.md): the cars of Forza Motorsport 3 and Forza
Motorsport 2 and the tracks of Forza Motorsport 3 that FM4 lacks, converted from the player's own
discs, and how several of them play together. The launcher's other options are in
[running.md](running.md#options).

## Contents

- [Converting a mod](#converting-a-mod)
- [Several mods together](#several-mods-together)
- [Tracks](#tracks)
- [Mods page](#mods-page)

## Converting a mod

The `mods` action converts the cars of FM3 or FM2, or the tracks of FM3, that FM4 and the
imported content packages lack into a mod, from the player's own discs, and `run -Mod` plays it.
The conversion is [fm4-content-converter](https://github.com/MotorsportDX/fm4-content-converter),
a separate program under GPL-3.0-only, which `mods.py` fetches and runs; this project ships none
of it ([licensing](sources-and-licensing.md#third-party-tools-the-mods)). The mods are
experimental: the cockpit view of some cars, the engine sound, the paint colours and the PI
class can differ from the original game, as the converter's
[FM3](https://github.com/MotorsportDX/fm4-content-converter/blob/main/docs/fm3.md) and
[FM2](https://github.com/MotorsportDX/fm4-content-converter/blob/main/docs/fm2.md) guides record,
and the tracks have their own limits ([tracks](#tracks)). The GUI launcher's **Mods** page offers
them in an install of the [builder package](running.md#builder-mode), which has the source, the
Python and the build tools that the conversion runs on ([Mods page](#mods-page)); elsewhere,
`Build.ps1` converts them.

```powershell
# After the game has run once with its content packages, so that the mods keep their cars and names:
.\outputs\FM4-build\Build.ps1 -Action mods -BuildRoot 'D:\FM4-build' -MingwRoot 'D:\Tools\llvm-mingw-20250709-ucrt-x86_64' `
    -Python 'C:\Python311\python.exe' -ModGame fm3 -ModDisc 'D:\Games\FM3\Disc 1.iso' -ModDisc2 'D:\Games\FM3\Disc 2.iso'
.\outputs\FM4-build\Build.ps1 -Action mods ... -ModGame fm2 -ModDisc 'D:\Games\FM2\Disc.iso' -ModDisc2 'D:\Games\FM2\Bonus.iso'
.\outputs\FM4-build\Build.ps1 -Action mods ... -ModGame fm3 -ModContent tracks -ModDisc '...\Disc 1.iso' -ModDisc2 '...\Disc 2.iso'
.\outputs\FM4-build\Build.ps1 -Action run -BuildRoot 'D:\FM4-build' -MingwRoot '...' -Python '...' -Mod fm3-cars,fm2-cars,fm3-tracks
.\outputs\FM4-build\Build.ps1 -Action run -BuildRoot 'D:\FM4-build' -MingwRoot '...' -Python '...' -Mod none
```

| Option | Meaning |
|---|---|
| `-ModGame` | `fm3` or `fm2`. |
| `-ModContent` | `cars` (the default; FM3's discs of the original release have 81 cars that FM4 lacks, the Ultimate Collection's 91) or `tracks` (FM3 only: FM2 has no track that FM4 or FM3 lacks, see [tracks](#tracks)). The mod is `<game>-<content>`: `fm3-cars`, `fm2-cars` or `fm3-tracks`. |
| `-ModDisc` | FM3 Disc 1 or the FM2 disc: an extracted folder, its `Media` folder, or the disc image (`.iso`). The converter reads the title ID and disc number from `default.xex` and refuses another disc. |
| `-ModDisc2` | Optional. FM3 Disc 2 (Content Install Disc) or the FM2 Bonus Disc. Without it, the cars or tracks whose files are only there are skipped: 26 of FM3's 81 cars (36 of the Ultimate Collection's 91), FM2's four downloadable cars, and FM3's Amalfi. |
| `-Mod` | `run`: the mods to play, in any order (a comma-separated list), or `none`. Saved in `local.json` for the next runs, like `-Content`. |

What the `mods` action does, with every file below `BuildRoot`:

1. Fetches GitHub's archive of the converter's pinned commit (`mods.CONVERTER_COMMIT`) into
   `downloads` and unpacks it into `mods-tools\converter`, checked by its tree hash, as the
   third-party archives are ([fetching without Git](setup.md#fetching-without-git); the
   `ARCHIVE_MIRROR` folder is read first).
2. Fetches the NumPy 2.3.5 and Pillow 12.0.0 wheels for the Python that runs `pipeline.py`
   (CPython 3.11 to 3.14), checked by size and SHA-256, into `mods-tools\site-cp3XX`.
3. Builds the converter's model helper `carbin-cpu.exe` into `mods-tools\carbin-cpu` from
   GitHub's archive of ForzaEditor at a pinned commit, with the C# compiler of the .NET
   Framework 4 that Windows ships (`csc.exe`); the converter fetches the pinned MonoGame
   package from NuGet for its vector arithmetic.
4. Builds the converter's LZX decoder `lzx_bridge.dll` into `mods-tools\lzx` with clang
   (`-MingwRoot` or `-Toolchain`) from libmspack's `lzxd.c` in `runtime-source`, which setup
   fetched.
5. Runs the conversion on the disc that Disc 1 of `local.json` and the mods converted before
   make ([several mods together](#several-mods-together)), with the content packages imported in
   `user-data`. Extracted disc files and FM2's models are kept in `mods-cache\<game>` (the
   tracks' in `mods-cache\fm3-tracks`), so a rerun skips them; delete the folder to free its
   space. The mod is written to `mods\<game>-<content>`, and replaces an earlier one only when it
   is complete. `logs\mods.log` holds the converter's log, and `-Progress` gets a step line for
   each car or track.

The mod is the converter's package: `mod.json` (format `fm4-mod` version 1: the files with
their SHA-256, the cars or tracks and their status, the mods it was built on in `base_packages`,
and the disc and content packages it was built against) and `files`, in Disc 1's layout. `run
-Mod` puts the mods in their install order and checks that each was built on the disc that Disc
1 and the mods before it make (the SHA-256 of `gamedb.slt` and `zipmanifest.xml`) with the
content packages imported now, and stops otherwise: a mod holds a merged copy of the database
and the name tables, so a changed Disc 1, a new content package or a mod converted again under it
needs the mod built again. It then plays `mod-disc`: every file of Disc 1 hard-linked (copied
when Disc 1 is on another volume than `BuildRoot`), and each file the mods write copied from
them in install order, so Disc 1 is never written. `mod-disc` is built again when Disc 1 or a
mod changes.

## Several mods together

FM4 reads one database (`Media/DB/gamedb.slt`), one archive list (`Media/zipmanifest.xml`) and
one name override (`Media/stringtables.zip`). The downloadable content packs add to them through
the title's own merge; a mod cannot, so the mods stack instead: each mod is converted on the disc
that Disc 1 and the mods converted before it make, as the converter's `--fm4` with
`--base-package` for each of them ([the converter's stacked installs](https://github.com/MotorsportDX/fm4-content-converter/blob/main/docs/mod-format.md#pemasangan-bertumpuk)).
Its database and archive list keep every row and archive of the mods under it, and its name
override, thumbnails and car audio archive keep their members, so the last mod's copies of these
files carry all of them. The FM3 and FM2 cars keep their own car IDs, which do not overlap; the
other keys of a mod start after those already taken.

- The order is the order of conversion: `mods` builds a new mod on top of the mods that play
  together now. The usual order is FM3's cars, FM2's cars, then FM3's tracks; any order works.
- `run -Mod` and the Mods page play a mod with the mods under it, or with none above it: the
  FM3 cars alone, the FM3 and FM2 cars, or all three. A mod above another cannot play without it.
- Converting a mod again (a new disc, a new content package) converts the mods above it again,
  from the discs their `mod.json` records. When those discs are no longer where they were, the
  action says so and the mod above stays out until it is converted again.
- Two mods converted each on Disc 1 alone (before stacking, or with a mod removed in between)
  cannot play together: `run -Mod` names the one to convert again.

## Tracks

`-ModContent tracks` converts the FM3 environments and layouts that FM4 lacks
(`fm3.track_env` of the converter, [its acceptance](https://github.com/MotorsportDX/fm4-content-converter/blob/main/docs/fm3-acceptance.md)):

| FM3 track | Layouts | Needs |
|---|---|---|
| New York Circuit (a new location, New York, USA) | Full Circuit, Old Circuit and their reverses (4) | FM3 Disc 1 |
| Rally di Positano (Amalfi, in FM4's Amalfi location) | Full Circuit and Stage A to D, each also reversed (10) | FM3 Disc 2 (the Road Racing Track Pack) |
| Bugatti Circuit (in FM4's Le Mans location) | 1 | FM3 Disc 1 |
| Sidewinder Proving Grounds (FM3's Test Track) | Layout A to I, each also reversed (18) | the Ultimate Collection's Disc 1 and Disc 2 (its database and the Forza Ultimate Track Pack); skipped with other discs |
| A reverse layout of a circuit FM4 has (CrownJewel 170, FM4's geometry) | 1 | FM3 Disc 1 |

With the FM3 discs of the original release, 16 layouts convert. FM4 relit every track, and its
lighting values do not map from FM3's, so each environment is lit with an FM4 track's
`TrackSettings.xml`. The track geometry, AI, audio, names, logos and mini maps are FM3's
(converted to FM4's file versions: PVS v27, RMB v5). The converter raced one layout of each
environment to the finish with the title's AI; the other layouts share their files but have not
each been raced.

FM2 has no track to convert: all its active layouts are in FM4 or in their FM3 versions above.
Its only other layout, TuningTrack (ID 71), is inactive in FM2's database (`Active=0`) and has no
files on the disc.

## Mods page

The launcher's Mods page, on the Home menu after Shaders, has three tabs:

- **In play:** one row a mod in install order, On or Off, with its cars or track layouts; Play
  races with the mods that are on, together (the launcher's own setting, `mods` in `launcher-settings.json`, which also
  reads the single `mod` that the launcher kept before 7 November 2026; `Build.ps1 -Mod` sets
  `local.json` instead). Turning a mod on turns on the mods it was converted on, and turning one
  off turns off the mods converted on it; a mod in play that was not converted with the new one
  is turned off, and the page says why. A mod that does not fit the current Disc 1, content
  packages or the mods under it lists "Convert again" and can only be turned off. A change
  prepares the mod Disc in the background (`mods.cpp`, the port of `mods.mod_disc` and
  `mods.order`: the same checks and marker, so a mod Disc that `run` prepared serves the
  launcher and the other way round); **Prepare the mods again** builds it anew. Play refuses
  mods whose Disc is not prepared for this Disc 1 and opens the page. The shader pass also reads
  the mod Disc.
- **Forza 3** and **Forza 2:** the source discs (a folder with A, a disc image with the row
  below it), the optional Disc 2 or Bonus Disc, **Convert the FM3 cars** (or the FM2 cars),
  on the Forza 3 tab **Convert the FM3 tracks**, and **Stop**. A conversion runs `pipeline.py
  mods` of the install's source with its Python and `--toolchain`, in a job object as the build
  steps run, its output in `logs\mods-run.log`; the panel shows the cars or tracks converted so
  far. Stop, and closing the launcher, end the conversion; the next one reuses what was fetched
  and extracted. When a mod in play was converted again, the mod Disc is prepared again.

Outside a builder install the page lists the mods and plays them, and says that converting needs
the builder's tools. For development, `FM4_MODS_SOURCE` (a checkout), `FM4_MODS_PYTHON` and
`FM4_MODS_TOOLCHAIN` name them. The launcher tests compare the install order and the mod Disc of
two stacked mods on both sides (`mods.launcher_test_case`).
