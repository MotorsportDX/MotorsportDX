# Builder installer (31 October 2026)

A design for a **builder installer**: a free download that carries none of the game and guides a
player from their own discs to a built, running Motorsport DX on their own Windows computer. It was
written as a design, with no code changed, nothing downloaded and no game or GPU run. Sizes and
licence terms that only a download could confirm are marked "not checked". The owner decided its
first questions on 31 October 2026 (section [9](#9-decisions-for-the-project-owner)), and
increments B1 to B3 are done (section [7](#7-increments)). The same day the owner chose route (c)
for the compiler's libraries: the build moved to llvm-mingw, which replaces increment B4 and every
Microsoft package of this note ([4.7](#47-the-msvc-libraries-and-the-windows-sdk)). Sections 4.1,
4.2 and 4.10 describe the design before that choice.

Where it starts from: releases are source only ([distribution](../sources-and-licensing.md#distribution),
[preview plan](preview.md)). Each player clones the repository, installs the toolchain by hand and
runs `Build.ps1` ([setup](../setup.md)). The [preview checklist](preview.md#source-and-build) lists
"a step-by-step install of the toolchain" as not done and strongly recommended for a public preview;
this note is that step, made into a program.

**Do players install the build dependencies themselves? No.** In the recommended design the player
installs nothing by hand: they consent to each download in the launcher's window, and every tool is
unpacked into the installer's own folder without administrator rights. The fallback, if the licence
reading of one route rules it out, is a single Microsoft installer ([4.1](#41-does-the-player-install-anything-by-hand)).

## 1. Goal and hard limits

The goal: a player who owns the game downloads one small package, points it at their discs, agrees
to each download it asks for, waits, and plays. No command line, no PowerShell execution policy, no
clone.

Hard limits, which every option below keeps:

- **Nothing game-derived is ever shipped.** No game code (`fm4_rex.exe` and the two module DLLs are
  the game's code recompiled from the player's discs), no game data, no `loaded-image.bin`, no
  generated source, no shader cache (`cache` holds shaders translated from the game), no export and
  no BuildRoot. Everything game-derived is generated and compiled on the player's computer from
  their own discs, as `generate` and `build` do today.
- **No launcher backdrop.** The package carries no picture; the launcher draws its dark gradient
  ([GUI launcher](../running.md#gui-launcher)).
- **No DirectX Shader Compiler.** Its `LICENSE-MS.txt` does not allow passing it on
  ([DXC](../sources-and-licensing.md#third-party-code-directx-shader-compiler)); the player's
  computer fetches it, as setup and the launcher's Shaders page do today.
- **No Forza names or logos** in the package's name, icon or pages. The product is Motorsport DX,
  with the non-affiliation line of the About page.
- **No silent installs and no system changes.** Every download is listed with its source, size and
  licence and needs the player's consent in the window. The installer never accepts a licence for
  the player, never changes a security setting (no antivirus exclusion, no execution policy), and
  installs nothing for all users without Windows' own elevation prompt in front of the player.
- **One commit.** The launcher, the source and the build come from the same tag, because the
  launcher's launch plan must match the runtime's cvars (the launcher tests of every `build` check
  that, [GUI launcher](../running.md#gui-launcher)).

## 2. What the player brings

| Input | Required | Checked by | Notes |
| --- | --- | --- | --- |
| Disc 1 (Play Disc), extracted | Yes | `validate_discs` and `disc_release` in [pipeline.py](../../outputs/FM4-build/pipeline.py); the launcher's port in [discs.cpp](../../work/fm4-launcher/src/discs.cpp) | The three executables (`default.xex`, `XMediaFacade_default.xex`, `SpeechFacade_default.xex`) must match the SHA-256 hashes of one release in `RELEASES`: **USA** (media ID `26FF1101`) or **Japan/Asia** (media ID `633772A0`). A mix, another revision, a title update or a renamed file is refused. `Media` and `Database.xmplr` must be there too |
| Disc 2, extracted | No (needed for its cars) | `disc2_packages`, `content_error` | The four STFS packages under `content\0000000000000000\4d530910\00000002` |
| DLC packages | No | `validate_content` | STFS (`CON`, `LIVE`, `PIRS`) with title ID `4D530910` and content type `00000002`; title updates and other titles are skipped ([downloadable content](../setup.md#downloadable-content)) |
| Disc 1 or Disc 2 as an ISO | No (either form) | `disc_input` in [pipeline.py](../../outputs/FM4-build/pipeline.py), `UnpackDisc` in the launcher | Unpacked once into `discs\disc1` and `discs\disc2`, then checked as the folders are (B9, done 1 November 2026; [Disc images](../setup.md#disc-images)) |

The releases are the same game: one build serves both ([releases](releases.md)). Title updates are
not supported ([title updates](title-updates.md)).

**When the check runs.** First, before any download: the launcher already holds the release hashes
and the content rules in `launcher.json` and checks the discs as `generate` does
([Game data page](../running.md#gui-launcher)). A player whose discs fail learns it before the
toolchain's gigabytes are fetched. `generate` checks them again.

**The discs stay where they are.** The game reads Disc 1 in place (`game_data_root`), `build`
validates the Disc paths of `local.json` again on every rebuild, and the first Play imports Disc 2
and the DLC into `user-data` (about their own size). The installer says so before it starts.
Whether Disc 2 and the DLC folders may move after their import is not checked.

## 3. What the installer ships: two options

### 3.1 Option A: the launcher and a source snapshot at a tag

The package is a zip (no admin needed to unpack it) with:

- `MotorsportDX.exe`, the GUI launcher, built at the tag, with a builder mode ([6](#6-the-launcher-ux));
- `launcher.json` for the tag, the launcher's tables (pure project data: releases, content rules,
  presets, cvars), written without a build ([7](#7-increments), B1);
- `source\`, a `git archive` of the tag (on 31 October 2026, `627e914` gave 1,256 files, 18.5 MB,
  4.6 MB as a zip), with a version stamp in place of the Git metadata;
- `toolchain.json`, the pinned list of downloads (section [4](#4-the-toolchain-on-the-players-computer));
- `LICENSE.txt`, `CREDITS.txt` and `third-party-notices`.

Everything else, the runtime and `rexglue.exe` included, is fetched as source and built on the
player's computer, exactly as `setup`, `generate` and `build` do today.

Licences of what option A ships:

| Component | Licence | Notice condition | In the package |
| --- | --- | --- | --- |
| The project's own code (launcher, pipeline, integration, docs) | BSD-3-Clause ([LICENSE](../../LICENSE)) | keep the notice | `LICENSE.txt`, source |
| Dear ImGui 1.92.5 | MIT | notice in all copies | `MotorsportDX.exe` |
| SDL 3 (`8bf3b72`, static), altered by the GameInput patch | zlib | none for binaries; the patch marks the altered source | `MotorsportDX.exe`; the patch in the source |
| Microsoft.GameInput 3.5.283, the API v3 header and loader only | MIT (those two files) | notice in all copies | `MotorsportDX.exe`, inside SDL |
| JSON for Modern C++ 3.11.3 | MIT | notice in all copies | `MotorsportDX.exe` |
| `wheel_host.cpp` of the runtime fork | BSD-3-Clause | as the project | `MotorsportDX.exe` |
| The C++ runtime of the toolchain that builds the launcher, if linked statically (B5) | Microsoft's Visual Studio terms | not checked | `MotorsportDX.exe` |
| The runtime fork `work/runtime` as source (ReXGlue SDK, Xenia-derived portions) | BSD-3-Clause | notice in source | source |
| XenonRecomp-based files of the fork (`include/rex/ppc/context.h`, `function.h`, `intrinsics.h`, `src/codegen/config.cpp`) | MIT, with the open Unleashed Recompiled question ([open items](../sources-and-licensing.md#open-items-before-a-public-release)) | notice in all copies (`XenonRecomp-LICENSE.txt`) | source only; compiled on the player's computer |
| Xenia's resolve shaders, adapted | BSD-3-Clause (`LICENSE.xenia`) | notice in source | source |
| The look's GT7 tone mapping port | MIT (Polyphony Digital) | notice in all copies | source and `look` |

The launcher links nothing else: no game code or data, no FFmpeg or libmspack (LGPL, in
`rexruntime.dll` only), no DXC, none of the four XenonRecomp-based files (checked on 31 October 2026
against [work/fm4-launcher/CMakeLists.txt](../../work/fm4-launcher/CMakeLists.txt): its sources,
`wheel_host.cpp`, Dear ImGui and `SDL3::SDL3-static`, and Windows' own system libraries). It uses
Windows' `urlmon` and `bcrypt` to fetch and check DXC, and Windows' `tar.exe` to unpack it
([shaders.cpp](../../work/fm4-launcher/src/shaders.cpp)); the builder reuses the same three.

What option A fetches on the player's computer (each with consent, [4](#4-the-toolchain-on-the-players-computer)):

| Component | Licence | Used for |
| --- | --- | --- |
| ReXGlue SDK `f5337cd`, its 13 submodules and 11 vendored folders | BSD-3-Clause, MIT, zlib, Boost 1.0, BSD-2-Clause, LGPL 2.1+ (FFmpeg, libmspack), GPL 2.0+ (the binutils disassembler, in `rexglue.exe` only), two files without a stated licence ([third-party components](../sources-and-licensing.md#third-party-components)) | built into the runtime, `rexglue.exe` and the game on the player's computer; nothing is passed on, so the LGPL and GPL terms are met as today ([LGPL](../sources-and-licensing.md#lgpl-ffmpeg-and-libmspack)) |
| AMD FidelityFX SDK v1.1.4 (sparse) | MIT; its `binary_store` holds a DXC 1.8.2403 under Microsoft's terms, run at build time with `-Fsr on` only | FSR 3.1 when built in; `setup` fetches it on every BuildRoot |
| Microsoft.GameInput 3.5.283 (NuGet) | the two used files MIT; the rest of the package under Microsoft's terms, not extracted | SDL's GameInput driver |
| DirectX Shader Compiler v1.9.2609 | Microsoft's DXC terms, LLVM's NCSA, MIT | the native path's DXIL shaders |
| The toolchain (section [4](#4-the-toolchain-on-the-players-computer)): Python, CMake, Ninja, LLVM, Microsoft's C and C++ libraries and Windows SDK | per tool ([4.2](#42-each-prerequisite)) | building |

Trade-offs of option A:

- **Download:** the package is about 10 MB (the launcher 4.5 MB on the development BuildRoot, more
  with a static C++ runtime, not checked; the source 4.6 MB). The player's computer then fetches the
  SDK sources (201 MB of files as archives, against a clone of 1,086 MB today), GameInput (2 MB),
  DXC (32 MB zip), the FidelityFX SDK only for a build with FSR, and the toolchain (section 4).
- **Time:** the measured clean `all`, about 16 minutes at `-Jobs 8` on the 20-thread development
  laptop, of which the game's own compilation took 141 s
  ([clean setup](../status/current.md#clean-setup-from-a-fresh-clone-25-october-2026)), plus the
  toolchain; about 25 to 45 minutes estimated on the ROG Xbox Ally X ([4.11](#411-the-first-build-on-a-slower-computer)).
- **Failure points:** every fetch (GitHub, NuGet, the toolchain hosts), the runtime build, the
  game build, the tests at the end of `build`. All are the paths the project already runs on every
  BuildRoot, with logs per stage.
- **Licences:** nothing new is passed on but the launcher; the LGPL, GPL and Unleashed questions
  stay where they are today.

### 3.2 Option B: the launcher with a prebuilt runtime and `rexglue.exe`

The package adds `runtime-install` (the development BuildRoot's holds 59 MB: `rexruntime.dll`
10.5 MB, `rexgpu-xenos.dll` 9.5 MB, `rexglue.exe` 3.5 MB, the headers, import libraries and CMake
files the game builds against). Only the game is generated and compiled on the player's computer.
The upstream SDK clone, the FidelityFX SDK and GameInput are then not needed; DXC still is.

Licences that option B adds, because these binaries would be passed on:

| Component | Licence | What passing it on requires |
| --- | --- | --- |
| FFmpeg (libavcodec, libavutil) and libmspack (`lzxd.c`), static in `rexruntime.dll` | LGPL 2.1 or later | the LGPL text, an offer of the libraries' source as built, and relinking with a changed library ([LGPL](../sources-and-licensing.md#lgpl-ffmpeg-and-libmspack)): the package would need the runtime's object files or a way to relink, beyond the source snapshot |
| GNU binutils PowerPC disassembler, in `rexglue.exe` | GPL 2.0 or later | the complete corresponding source of `rexglue.exe` under the GPL, and the licence text; the project's own code and every library in it must be GPL-compatible (BSD, MIT, zlib and Boost are) |
| The four XenonRecomp-based files, in the runtime, the game's code and `rexglue.exe` | MIT as marked; possibly GPL 3.0 if they hold Unleashed Recompiled code | the open question must be answered by the ReXGlue SDK first ([projects](../sources-and-licensing.md#projects-this-work-builds-on)) |
| `pe_image.h` and disruptorplus (vendored, in `rexruntime.dll` and `rexgpu-xenos.dll`) | no stated licence in the SDK's copy | a licence must be found first ([open items](../sources-and-licensing.md#open-items-before-a-public-release)) |
| The rest of the runtime's libraries (fmt, spdlog, toml++, xxHash, SIMDe, CLI11, inja, UTF8-CPP, o1heap, Dear ImGui, SDL, GameInput, FidelityFX with `-Fsr on`, the vendored notices) | permissive | the notices, which `third-party-notices` already holds |

Trade-offs of option B:

- **Download:** about 70 MB for the package, against about 10 MB; the player's computer skips the
  1.1 GB upstream clone and the FidelityFX and GameInput fetches.
- **Time:** saves the runtime's build (not measured on its own; the 16-minute `all` includes it and
  the fetches) but not the game's 141 s nor the toolchain, which the game's build needs in full:
  clang 20, the MSVC libraries, the Windows SDK, CMake, Ninja and Python stay.
- **Failure points:** fewer fetches, but a new one: the prebuilt runtime must match the source and
  the compiler exactly (ABI, `-march=x86-64-v2`, the cvars), and `build` today always rebuilds the
  runtime from source (`build_runtime` in `build_game`), so the pipeline needs a mode that trusts a
  shipped `runtime-install` and checks its hashes.
- **Licences:** blocked until the LGPL relinking, the GPL source of `rexglue.exe`, the Unleashed
  Recompiled question and the two vendored files are resolved.

### 3.3 Recommendation

**Option A.** It passes on only the launcher, whose licences are all permissive and already in
`third-party-notices`; it keeps every LGPL and GPL component on the source-only footing it has
today; and it runs the same `setup`, `generate` and `build` that every gate and the clean setup of
25 October 2026 ran. Option B saves less than it looks (the toolchain stays, and the game's own
compile stays) and needs four licence items answered first. It can follow later as an increment if
those items are closed and download size proves to matter.

## 4. The toolchain on the player's computer

### 4.1 Does the player install anything by hand?

**No, in the recommended design.** Every build tool is fetched into the installer's own folder
(`<InstallRoot>\toolchain`), unpacked rather than installed, with no administrator rights, each
pinned by version and SHA-256, and each after the player's consent in the window. Nothing goes onto
the system `PATH` or into the registry. Three changes make that possible: Git is replaced by archive
downloads ([4.3](#43-git-avoided)); the Microsoft libraries come as packages from Microsoft's own
servers after the player accepts Microsoft's licence in the window (route (b),
[4.7](#47-the-msvc-libraries-and-the-windows-sdk)); and `fxc.exe` is replaced by Windows' own
`d3dcompiler_47.dll` ([4.8](#48-fxc-replaced-by-d3dcompile)).

**The fallback is one Microsoft installer.** If the licence reading of route (b) rules it out, or
its downloads fail, the builder runs Microsoft's Build Tools bootstrapper (route (a)) with the
components named: one installer window and one elevation prompt, nothing else by hand.

What the player still needs and the builder cannot fetch: Windows 10 or 11 x64 and a GPU driver with
Direct3D 12. The preflight checks both ([5](#5-the-flow)).

### 4.2 Each prerequisite

What [setup](../setup.md#prerequisites) requires today, and what the builder uses instead. Versions
read on the development computer on 31 October 2026: clang 20.1.8, Visual Studio Community 2022 with
MSVC 14.44.35207, its bundled CMake 3.31.6 and Ninja 1.12.1, Windows SDK 10.0.26100.0, Python 3.11.9,
Git 2.55.0. "Installed" sizes are folder sizes there; a download size needs the download and is "not
checked".

| Prerequisite | The builder's route | Portable, no admin, pinned | Licence: may the installer fetch it? ship it? | Download | Disk |
| --- | --- | --- | --- | --- | --- |
| Git | **Avoided:** the SDK and its submodules as archives at their pinned commits ([4.3](#43-git-avoided)); MinGit as the fallback | Archives: yes, pinned by a hash over the unpacked files. MinGit: a zip, yes; its hash is not pinned yet | Archives: the sources' own licences, fetched as today. MinGit: GPL 2.0 (fetch: yes; ship: only with its source offer, so not shipped) | not checked (archives, MinGit) | needed files 201 MB (the development BuildRoot's `runtime-source\thirdparty`), against today's clone of 1,086 MB |
| Python 3.11 x64 | python.org's Windows embeddable package ([4.4](#44-python)) | Yes (a zip), pinned in `toolchain.json` (B3) | PSF License 2.0: fetch yes; shipping is allowed with its notices and those of the libraries it bundles (its `LICENSE.txt` lists them), but it is fetched to keep the package small | 11.2 MB | the full install 125 MB; the embeddable package 21.7 MB |
| CMake 3.25+ | Kitware's Windows x64 zip ([4.5](#45-cmake-and-ninja)) | Yes; 3.31.6 pinned (B3) | BSD-3-Clause: fetch and ship allowed | 46.5 MB | Visual Studio's copy with Ninja: 58 MB; the builder keeps 20.9 MB of the zip |
| Ninja | the Ninja project's Windows zip | Yes; one executable, 1.12.1 pinned (B3) | Apache-2.0: fetch and ship allowed | 0.28 MB | one executable, 0.57 MB |
| LLVM 20 (clang, clang++, lld-link, llvm-ar, llvm-rc) | the LLVM release's Windows archive, unpacked ([4.6](#46-llvm-20)) | Yes: 20.1.8's `.tar.xz`, pinned (B3) | Apache-2.0 with LLVM exceptions: fetch and ship allowed | 939.5 MB | full install 2,427 MB; the members the build uses 304 MB |
| MSVC x64 tools (the C and C++ runtime and library headers and libraries) | route (b): Microsoft's packages from Microsoft's servers after the player accepts the licence ([4.7](#47-the-msvc-libraries-and-the-windows-sdk)); route (a) as the fallback | (b): yes, pinned by the SHA-256 that Microsoft's manifest lists per payload. (a): no, admin | Microsoft's Visual Studio terms. **Ship: no.** Fetch: (a) yes, through Microsoft's installer; (b) **not checked** whether the terms cover files fetched without the installer | not checked ((b) smaller; (a) several GB) | MSVC tools 1,411 MB installed, with parts the build does not use |
| `link` | **lld-link** from LLVM, which the builds already use: `CMAKE_LINKER` is LLVM's `lld-link.exe` in the development BuildRoot's runtime and game build caches. Only `doctor`'s check of `link` goes | - | - | - | - |
| Windows SDK (headers, libraries) | with the MSVC libraries: route (b) or (a) | as above | Microsoft's SDK terms: ship no; fetch as above | not checked | the Windows Kits folder 1,708 MB with five SDK versions |
| `fxc.exe` (Windows SDK) | **Replaced** by Windows' own `d3dcompiler_47.dll` (B3, [4.8](#48-fxc-replaced-by-d3dcompile)) | nothing to fetch | part of Windows | none | none |
| `rc.exe` (Windows SDK, found by CMake) | `llvm-rc` from LLVM, which CMake finds instead; no build compiles a resource script (no `.rc` in the development BuildRoot's `build.ninja` files) | - | - | - | - |
| The Visual Studio developer environment (`vswhere`, `Enter-VsDevShell` in `Build.ps1`) | **Avoided:** the builder sets `INCLUDE`, `LIB` and `PATH` to its own folders and runs `pipeline.py` without `Build.ps1` | - | - | - | - |
| VC++ 2015-2022 x64 runtime (to run the game) | app-local: the runtime DLLs beside the game for that computer only ([4.10](#410-what-runs-on-the-players-computer)) | yes | Microsoft's terms (the runtime DLLs are the redistributable part); fetching them by route (b): not checked | not checked | not checked |

The project's own pinned downloads stay as they are today:

| Component | Source | Pin | Licence | Size |
| --- | --- | --- | --- | --- |
| ReXGlue SDK `f5337cd` and 13 submodules | GitHub; archives in the builder ([4.3](#43-git-avoided)) | the commit and the submodules' commits | per [third-party components](../sources-and-licensing.md#third-party-components) | 1,086 MB as a clone (730 MB of it Git history); 201 MB of needed files |
| AMD FidelityFX SDK v1.1.4 | GitHub, sparse clone; `FFX_MIRROR` | commit `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55` | MIT | about 39 MB of files and 36 MB of Git objects ([setup](../setup.md#1-setup)) |
| Microsoft.GameInput 3.5.283 | NuGet; `GAMEINPUT_MIRROR` | SHA-512 `59083faf...fe77705` | MIT for the two used files | 2,143,539 bytes |
| DirectX Shader Compiler v1.9.2609 | GitHub release; `DXC_MIRROR`, `FM4_DXC_MIRROR` | SHA-256 `ad31b1fc...a4a7e7f1`, and one per DLL | Microsoft's DXC terms (fetch yes, ship no), NCSA, MIT | 32,287,592 bytes |

**Reuse before download.** The builder first looks for tools of the right versions already on the
computer (`vswhere`, `py -0p`, LLVM's default folder) and offers a download only for what is
missing. A player with Visual Studio and LLVM 20 downloads only the project's pins.

### 4.3 Git, avoided

The pipeline uses Git in five places today: `fetch_thirdparty` (a clone of the upstream SDK, a
checkout and `submodule update --recursive` of 13 submodules), `fetch_fidelityfx` (a sparse clone,
and the bundle mirror), `apply_patch` (`git apply` of the SDL patch), `source_state` (the head and the
dirty flag) and `doctor`. The builder does without each:

- **Archives at pinned commits.** GitHub serves an archive of any commit of a public repository. The
  upstream SDK's archive at `f5337cd` holds the 11 vendored folders; each of the 13 submodules is one
  more archive at the commit the SDK pins (read on the development BuildRoot: for example FFmpeg
  `0604b46` on Xenia's fork, SDL `8bf3b72`, Dear ImGui `6d910d5`; SIMDe brings one nested submodule,
  `test/munit`). GitHub does not promise byte-identical archives over time (its archive compression
  changed once, in January 2023), so the pin is a SHA-256 over the unpacked files' paths and
  contents, as `source_state`'s tree hash is, not over the archive.
- **FidelityFX.** `setup` fetches it into every BuildRoot today, while the default build (`-Fsr` off)
  compiles none of it. The builder fetches it only for a build with FSR, as an archive of the pinned
  commit of which only `FFX_PATHS` is unpacked (the archive holds the whole SDK: its size is not
  checked).
- **The SDL patch** is applied by a small exact-match unified diff applier in Python, which refuses
  any fuzz as `git apply` does.
- **`source_state`** reads the snapshot's version stamp when there is no Git checkout.
- **MinGit** (Git for Windows' portable zip) stays the fallback for a mirror that is a Git URL.

Git's own share of today's download goes with it: the clone holds 1,086 MB in the development
BuildRoot, 730 MB of it Git history, for 201 MB of files that the build reads.

### 4.4 Python

python.org's **embeddable package** is a zip that runs where it is unpacked, with no installer and no
registry. The pipeline uses only the standard library (`hashlib`, `json`, `tomllib`, `urllib` with
HTTPS, `zipfile`, `subprocess`), and the game's CMake asks only for the interpreter
(`find_package(Python3 REQUIRED COMPONENTS Interpreter)` in
[work/fm4-rex/CMakeLists.txt](../../work/fm4-rex/CMakeLists.txt)). To check when it is pinned:

- the embeddable package's `._pth` file fixes `sys.path`, and `pipeline.py` imports `prepare.py` from
  its own folder: since B3 `pipeline.py` puts its own folder on `sys.path`, and the `._pth` file
  stays as shipped;
- the modules it carries: `_ssl` (OpenSSL 3.0.13, with the Windows certificate store), `_lzma`,
  `_ctypes` and `_msi` are in the package (B3); HTTPS through it was not exercised (every fetch of
  the B3 run came from local copies);
- the version: 3.11.9 runs the pipeline and is pinned; it was, as far as known here, the last 3.11
  release with Windows binaries (later 3.11 releases are source-only security releases; not
  re-checked online), and a later Python would need its own run of the pipeline's tests.

### 4.5 CMake and Ninja

Both have official zip releases that run unpacked: Kitware's `cmake-<version>-windows-x86_64.zip` and
Ninja's `ninja-win.zip`. Pin CMake 3.31.6 and Ninja 1.12.1, the versions the development builds use
from Visual Studio, or newer ones checked once. Both licences (BSD-3-Clause, Apache-2.0) would also
allow shipping them; fetching keeps the package small.

### 4.6 LLVM 20

The build needs clang, clang++, lld-link, llvm-ar and llvm-rc, with clang's own headers and builtins
(`lib\clang\20`). The LLVM project's GitHub release of 20.1.8 has a Windows archive,
`clang+llvm-20.1.8-x86_64-pc-windows-msvc.tar.xz` (939.5 MB; 5,010 members, 4,189 MB unpacked, no
links). The builder unpacks it with Python's `lzma` and keeps only the members the build uses: 301
files, 304 MB (B3).

### 4.7 The MSVC libraries and the Windows SDK

This is the hard part. clang compiles, but it targets the MSVC ABI: it needs MSVC's C runtime and C++
library (headers and import libraries), the Universal CRT, and the Windows SDK's headers and libraries
(`d3d12`, `dxgi`, `d3dcompiler` and the rest). The Visual Studio and Windows SDK terms do not allow the
project to ship them. Three routes:

| | (a) Microsoft's Build Tools installer | (b) Microsoft's packages, unpacked | (c) MinGW-w64 or llvm-mingw, no MSVC |
| --- | --- | --- | --- |
| What runs | `vs_BuildTools.exe --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.26100 --wait`, interactive so that the player sees Microsoft's terms (the component names to confirm against Microsoft's list) | The builder reads Microsoft's Visual Studio channel manifest, picks the C runtime and C++ library packages of MSVC 14.44 for x64 and the Windows SDK 10.0.26100 packages, downloads them from Microsoft's servers and unpacks them (VSIX files are zips; the SDK's MSI and CAB files need an MSI reader, for example Python 3.11's `msilib`, and Windows' `expand.exe`): the approach of tools such as `xwin` | Another compiler target: mingw-w64's headers and libraries and LLVM's libc++ instead of MSVC's |
| Admin | yes (one elevation prompt) | no | no |
| Pinned | the bootstrapper installs the current 17.x release; a fixed-version bootstrapper: not checked | yes: the manifest lists a SHA-256 per payload, and the builder pins the package versions and hashes in `toolchain.json`. How long Microsoft keeps older payloads on its servers: not checked | yes (a release zip) |
| Licence | the player accepts Microsoft's terms in Microsoft's window; whether the Build Tools terms allow this use without a Visual Studio licence: **not checked** (Visual Studio Community is free for individuals) | the player accepts the Visual Studio and Windows SDK terms in the builder's window, from the manifest's licence links; whether those terms cover files fetched without Microsoft's installer: **not checked**, to read before B4. Nothing of Microsoft's passes through the project | permissive (mingw-w64's runtime and headers, libc++): fetch and even ship allowed |
| Size | several GB of download (not checked); MSVC 1,411 MB and the SDK installed here | only the x64 headers and libraries: not checked (less than (a)) | not checked |
| Linker | lld-link, as today | lld-link, as today | lld |
| Version drift | a later MSVC library may require a newer clang than 20 | none: MSVC 14.44, the version the builds use today | none |
| Cost | the smallest (the arguments, the detection, the wait), inside B4 | B4: the manifest reader, the package list, the unpackers, the environment (`INCLUDE`, `LIB`); about 7 h | a port of the runtime and the title code to another C++ library and ABI (the Xenia-derived runtime's MSVC-specific code, mingw-w64's D3D12 and DXGI headers, not checked how current, the GameInput and DXC headers, the FFmpeg and SDL builds), then every gate again because every binary changes: an estimate of 4 to 8 person-weeks, 14 to 28 h, uncertain |

**Recommendation: (b), with (a) as the fallback; not (c).** (b) is the only route with no admin,
pinned versions and a download of only what the build reads; its open question is the licence text,
which a reading (no download) can answer before B4 starts. (a) stays as the fallback in the same
window when (b) is ruled out or fails. (c) costs a port and a full re-validation for what (b) already
gives.

**The licence reading (31 October 2026) reopens this recommendation.** The main session read the
terms with no download:

- **Visual Studio 2022 Build Tools** (the terms the catalog links, LinkId 2179911: Microsoft's text
  of March 2024, read in Microsoft's online document viewer):
  - 1.a: the software is a supplement to Visual Studio Community, Professional or Enterprise, and
    its use needs a valid licence to one of them;
  - 1.d: without such a licence, it may be used only to compile and build C++ components that a
    third party released under an OSI-approved licence and that are reasonably required to build
    the user's own applications ("Open Source Dependencies"); 1.e: not to develop them further;
  - 2.b: files may be copied onto build devices solely for those purposes;
  - 4: no working around technical limitations, no sharing or publishing of the software, and no
    offering it stand-alone or combined with an application for others to use.
- **Visual Studio Community 2022** (LinkId 2179811):
  - 1.a: an individual may use it to develop and test their own applications, for any purpose;
  - 1.b: an organisation may use it, among other cases, for applications released under
    OSI-approved licences.
- **The Windows SDK** (`sdk_license.rtf` of 10.0.26100, `EULAID:WIN10SDK.RTM.AUG_2018`):
  - 1.a: any number of copies on the user's devices to design, develop and test their programs,
    and each copy must be complete;
  - 2.a: distributable code only as its REDIST.TXT list names it;
  - 7: no working around technical limitations, and no publishing of the software for others to
    copy.
- `Redist.txt` in this computer's Visual Studio only points to `https://aka.ms/vs/17/redist.txt`
  (not read).

What follows from the text:

1. A developer of the project is covered: Community 1.a, or Build Tools with a Community licence.
2. A player who builds the game only to play it fits none of these clauses clearly. Build Tools 1.d
   is about the dependencies of the user's own applications, Community 1.a about the user's own
   applications, and the SDK about the user's own programs. This holds for routes (a) and (b)
   alike: the installer changes how the files arrive, not the terms.
3. Route (b) adds two questions of its own:
   - the SDK's condition that each copy is complete, which an x64 selection of its MSIs does not
     meet;
   - whether unpacking Microsoft's packages without its installer counts as working around a
     technical limitation, which the text does not answer.
4. Route (c) depends on no Microsoft terms.

This note is not legal advice. The route is the owner's decision again (section 9, item 4).

**Route (c) researched (31 October 2026):** [mingw-toolchain.md](mingw-toolchain.md) counts the
MSVC-specific code (no blocker found; the D3D12 struct returns, setjmp and the exports need proof),
names what mingw-w64 lacks (`gameinput.h`), pins llvm-mingw 20250709 (LLVM 20.1.8, 171 MB), estimates
14 to 28 h, and recommends a prototype of 2 to 3 h with that one download before the route is chosen.

**Route (c) chosen (31 October 2026).** The owner decided to move the build to llvm-mingw ("Lanjut
pindah"). Its first increment (M1) makes llvm-mingw the default of every action, for players and
developers ([setup, toolchain](../setup.md#toolchain)): `toolchain.json` pins llvm-mingw 20250709 in
place of LLVM 20.1.8's archive, `doctor` asks for nothing of Visual Studio, and `libc++.dll` and
`libunwind.dll` go beside the game instead of the VC++ runtime DLLs. **Increment B4 is replaced by
this route:** the builder fetches no Microsoft package, needs no licence page for Microsoft's terms
and no `INCLUDE` or `LIB`; route (a) is no longer the fallback. The build with Microsoft's libraries
stays selectable as `-Abi msvc` only for the A/B of the switch's verification (M2), then goes. The
switch's state is in [mingw-toolchain.md, section 11](mingw-toolchain.md#11-the-switch-m1-31-october-2026).

**Route (c) prototyped (31 October 2026):** with that one download the whole player build passes
with llvm-mingw, its tests included, the generated code is the MSVC build's byte for byte, and a
D3D12 probe on WARP returns the same structs; 16 small shims, no rewrite, and about 9 to 17 h left,
most of it proof ([mingw-toolchain.md, section 10](mingw-toolchain.md#10-prototype-results-31-october-2026)).
On the GPU the smoke A/B passes after the setjmp fix of section 10.7; the frozen-frame parity still
fails on draws, because the mingw build freezes another moment of the same race.

### 4.8 FXC, replaced by D3DCompile

`fxc.exe` is used in one place: `compile_resolve_shaders` in
[pipeline.py](../../outputs/FM4-build/pipeline.py) compiles the resolve shaders, the readback
downscale shader and the presenter's area downscale shader to DXBC headers (`/T cs_5_1` or `ps_5_1`,
`/O3`, `/Gfp`, `/Qstrip_reflect`, `/Qstrip_debug`, `/Qstrip_priv`, `/D` defines, `/Fh` with `/Vn`).
`doctor` checks for it.

The same compiler is `d3dcompiler_47.dll`, which is part of Windows 10 and 11 (in `System32`);
`fxc.exe` is a front end to it, and the runtime already compiles the look mods through `D3DCompile`
at run time ([shader_override.cpp](../../work/runtime/src/graphics/d3d12/shader_override.cpp)). A
replacement calls `D3DCompile` (from Python through `ctypes`, so nothing needs building) with
`D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_PREFER_FLOW_CONTROL`, the defines and the standard file
include handler, strips the reflection, debug and private data with `D3DStripShader`, and writes the
header with the array name of `/Vn`. Windows' copy of the DLL may be another build than the SDK's, so
the bytecode could differ. **Done in B3:** on the development computer all 33 headers are byte for
byte those of `fxc.exe`, with `System32`'s DLL (10.0.26100.9711) and with the SDK's (10.0.26100.7705)
([B3 results](#b3-results-31-october-2026)); the runtime therefore embeds the same shader bytecode as
before, and the resolve paths needed no run of their own.

### 4.9 Other pins and mirrors

The project's pins (the SDK commit, FidelityFX, GameInput, DXC) stay. LLVM, Python, CMake, Ninja and
the Microsoft packages need an asset, a size and a SHA-256 in `toolchain.json`; finding them means
downloading each once on the development computer, with the owner's approval (B3, B4). The upstream
SDK and its submodules have no mirror today; a copy on the owner's GitHub would cover a repository
that disappears.

### 4.10 What runs on the player's computer

- **The game** links the C and C++ runtime dynamically (the game, its two modules and the runtime DLLs
  share it), so it needs the VC++ 2015-2022 x64 runtime DLLs. Today the player installs Microsoft's
  redistributable ([troubleshooting](../setup.md#troubleshooting)). The builder puts the runtime DLLs
  beside the game instead (app-local, a deployment model Microsoft supports), from the packages of
  route (b), or from the Build Tools' `VC\Redist\MSVC` folder (103 MB on the development computer)
  with route (a).
- **The shipped launcher** has to start before any of that exists, so it links the C++ runtime
  statically (it is one module, with SDL linked statically too).
- **GameInput's runtime** is part of Windows or its redistributable; without it SDL's other pad
  drivers take the pads ([GameInput](../sources-and-licensing.md#third-party-code-microsoftgameinput)).
  The builder installs nothing for it.

### 4.11 The first build on a slower computer

The clean setup of 25 October 2026 (`-Jobs 8` on the development laptop, an Intel Core Ultra 7 255HX
with 20 threads and 32 GB), split by the creation times of its stage logs in `BuildRoot\logs`:

| Part | Time |
| --- | --- |
| Fetches (FidelityFX, the SDK clone and its submodules, the copy into `runtime-source`) | about 2.6 min |
| The resolve shaders, the runtime build and its install | about 2.6 min |
| `generate` (the loader, the image, `codegen`, the guards) | about 0.8 min |
| The game's compilation | 141 s |
| The tests at the end of `build`, of which the shader-mod tests (FXC compiles of the look mods on WARP) took about 6.9 min | about 7.5 min |
| The launcher and its tests | under 1 min |
| `all` | about 16 min |

**The ROG Xbox Ally X, an estimate.** Its Ryzen Z2 Extreme has 8 cores and 16 threads in a handheld's
power budget, and 24 GB of memory shared with the GPU; nothing has been built on it. Taking the CPU
parts (about 13.5 min here) at 1.5 to 2.5 times as long, at `-Jobs 6` to `8`, gives about 20 to 35
minutes of build on AC power, more on battery, plus the downloads, which depend on the connection.
**Estimate: about 25 to 45 minutes in all for a first build; not measured.**

Two levers for every computer:

- **A player build without the developer tests.** The WARP shader-mod and post pass tests (about
  7 min here) check the project's HLSL, which is the same on every computer. The instruction tests
  (295,650 checks of the player's compiler on the title's arithmetic) and the launcher tests stay.
  That would cut the Ally X estimate by about a third. The owner approved it on 31 October 2026
  (section 9); B1 made it `--tests player` ([setup](../setup.md#3-build)).
- **Fewer fetches.** The archives of 4.3 replace a 1,086 MB clone with the files the build reads.

## 5. The flow

```text
Preflight -> Disc check -> Plan and consent -> Toolchain fetch -> Setup -> Generate -> Build
          -> Content import (first Play) -> Offline shader pass -> First run
```

| Step | What happens | Time (measured, development laptop) | Typical failures |
| --- | --- | --- | --- |
| 1. Preflight | Windows 10 or 11 x64 (Windows 10 is not tested); a CPU with x86-64-v2 (SSE3, SSSE3, SSE4.1, SSE4.2, POPCNT, CMPXCHG16B, LAHF/SAHF, by CPUID: the default build uses `-march=x86-64-v2`); a Direct3D 12 GPU (`D3D12CreateDevice` at feature level 11_0); RAM, which sets `-Jobs` (`-Jobs 8` ran on 32 GB; the memory per job is not measured); free disk on the install drive; the install path (short, ASCII, outside any synced folder) | seconds | old CPU or GPU; too little disk |
| 2. Disc check | Disc 1, Disc 2 and the DLC folders, as `generate` checks them (section [2](#2-what-the-player-brings)) | seconds (three SHA-256 hashes) | wrong release, missing files, a title update, an ISO |
| 3. Plan and consent | One page lists what is missing: each download with its host, size, licence (a button opens it) and where it goes. (Obsolete since route (c): Microsoft's terms for its C and C++ libraries and Windows SDK, route (b), and the Build Tools' elevation prompt, route (a).) | the player's | the player declines |
| 4. Toolchain fetch | Each download to `downloads\`, checked against its pin, unpacked into `toolchain\` (obsolete since route (c): route (a)'s installer window) | not measured | network, a hash mismatch |
| 5. Setup | `setup`: the runtime source, the SDK's third-party libraries, GameInput, DXC, the resolve shaders, the runtime build | about 5.2 min with its fetches ([4.11](#411-the-first-build-on-a-slower-computer)) | GitHub unreachable, a compile error |
| 6. Generate | `generate`: the loader reads `default.xex`, the switch tables, `rexglue.exe codegen`, the guards; writes `local.json` | about 0.8 min | disc moved since step 2 |
| 7. Build | `build`: the game (141 s), the DLLs staged, DXC and the C++ runtime DLLs staged, the instruction, post pass (WARP), CPU and launcher tests, `build-result.json`, the launcher | about 10.5 min, 7.5 of it tests | a compile error, a test failure |
| 8. Content import | The first Play imports Disc 2 (2.8 GB) and the DLC into `user-data` | 15 s for Disc 2 and eight DLC packages on 25 October 2026 | disk full |
| 9. Offline shader pass | The Shaders page's pass, which the launcher starts by itself once per build ([Shaders page](../running.md#shaders-page)) | not part of this design's measurements | GPU or driver problems |
| 10. First run | Play. The new profile plays the game's intro video, gamma adjustment and intro race | minutes | crashes: the error banner and the logs |

Disk: the BuildRoot held 2.6 GB after `build` in the clean setup; Disc 2's import adds 2.8 GB and
the DLC about their own size; the installed toolchain on the development computer is about 5.6 GB
(LLVM 2.4 GB, MSVC 1.4 GB, the Windows Kits 1.7 GB with five SDK versions, Python, Git). The
builder's portable Python, CMake, Ninja and LLVM take 347 MB unpacked, beside 998 MB of their
archives while those are kept (B3); Microsoft's libraries are B4's to measure. A provisional
preflight floor of 20 GB free, to be set from the measurements of B3 and B4.

**How the builder drives the pipeline.** It runs `pipeline.py` directly with the toolchain's Python,
not `Build.ps1`, so the PowerShell execution policy never matters. It builds the environment
itself: `PATH` with its own llvm-mingw, CMake, Ninja and Python. (Obsolete since route (c): `INCLUDE`
and `LIB` with the unpacked Microsoft libraries of route (b), and route (a)'s capture of the Build
Tools' `VsDevCmd.bat`; the builder reads nothing of Microsoft's. B5 gives each step an environment
of Windows' folders and the `--toolchain` tools only, [B5 results](#b5-results-1-november-2026).) It starts each action as a child process with the logs in
`BuildRoot\logs`. The BuildRoot is `<InstallRoot>\build`, outside `source`, which keeps
`pipeline.py`'s BuildRoot rule.

**Resume after a failure.** Every stage already leaves a marker: `setup-complete.json`, `local.json`
(generate), `build-result.json` (build); fetches keep a valid file and resume with no network
(`fetch_gameinput`, `fetch_dxc`, an existing FidelityFX checkout); the builds are incremental. The
builder keeps `builder-state.json` (the tag, the step reached, the toolchain found) and starts at the
first step without its marker; **Retry** reruns the failed step only. A partial download keeps its
`.partial` name and starts again.

**Logs.** `BuildRoot\logs` per stage, as today, plus `logs\builder.log` (what the builder ran, with
the times, the tool versions and the hashes checked). The failure page opens the log of the failed
stage and shows its last lines.

**Updates.** A new tag is a new package. The player unpacks it over the old one or points it at the
existing install (B7): the builder replaces `source\` (unpacked beside it first, then swapped), keeps
`build\`, `toolchain\` and `downloads\`, fetches only what a changed `toolchain.json` asks for, and
runs `generate` and `build` again in the same BuildRoot. Kept: `user-data` (profiles, saves, imported
content), `launcher-settings.json` (profiles, looks, bindings) and `cache` (the shader pass runs
again by itself because the executable changed). Before the rebuild the builder copies `user-data`'s
save files to `backups\<date>`: saves are not checked between builds
([known issues](../known-issues.md)).

## 6. The launcher UX

The launcher already has the pieces: the racing-theme frame, pages driven by pad, mouse, keyboard or
touch, the Game data page's disc checks, the Shaders page's fetch with a pinned hash and its
progress, and the error banner ([GUI launcher](../running.md#gui-launcher),
[launcher UX](launcher-ux.md)). What it lacks is a start without a game: today it shows "No
game-build\fm4_rex.exe beside the launcher" and stops.

**Builder mode.** When the launcher finds no built game beside it, Home's Play becomes **Build the
game** and a **Build** page leads, as a first-run wizard of four screens:

1. **Welcome:** what will happen, the time and disk it takes, that the discs stay in place, and
   that nothing of the game is downloaded.
2. **Your discs:** the Game data page's rows (Disc 1, Disc 2, the DLC), checked at once.
3. **Downloads:** the plan of step 3, each row with its host, size and licence, **Download all** or
   one by one. (Obsolete since route (c): the Microsoft row with Microsoft's terms, **Accept** and
   **Decline**, and route (a)'s installer with administrator rights. B5 has no Microsoft row.)
4. **Build:** the steps of section 5 as rows (waiting, running, done, failed) with the elapsed time
   and an estimate from the measured times; a bar for the compile from Ninja's `[n/total]` lines in
   the stage's log; **Pause** (ends the running stage, resumes from its marker), **Open log**.

While it builds the launcher keeps the computer awake for itself (`SetThreadExecutionState`, no
setting changed), warns on battery, and stays usable.

**Plain errors.** Each known failure has a sentence and an action, from the messages the pipeline
already prints:

| Pipeline message | The builder says | Action |
| --- | --- | --- |
| `Unsupported executables in ...` | "These discs are not a supported release (USA or Japan/Asia)." | Choose Disc 1 again |
| `Missing Disc 2 content package` | "Disc 2 is incomplete: four packages are missing from its content folder." | Choose Disc 2 again, or build without it |
| `... cannot be fetched with its pinned SHA-...` | "The download from <host> did not arrive whole or has changed." | Retry; mirrors |
| `cannot be reached and FFX_MIRROR names no mirror` | "GitHub cannot be reached." | Retry later |
| `Use LLVM 20.x` (obsolete: the builder always uses its own llvm-mingw) | "The installed LLVM is not version 20." | Use the builder's own |
| A Microsoft payload refused or missing (obsolete since route (c)) | "Microsoft's server no longer has this package, or it changed." | Retry; Microsoft's installer instead (route (a)) |
| `Exit <n>; log: ...` in a build | "The build stopped in <stage>." | Open log, Retry, report |
| `Launcher tests failed` | "The launcher and the game of this build disagree." | Report: a project bug |
| Windows' disk full | "The drive is full: <n> GB more are needed." | Free space, Retry |

**Bug reports.** The failure page has **Copy details**: the tag, the step, the tool versions, the
error and the paths of the logs to attach, following [bug reports](../bug-reports.md)
([which files](../bug-reports.md#which-files-to-send), [privacy](../bug-reports.md#privacy): the logs
show the install paths, which may hold the Windows user name). It copies text only; nothing is sent.

**After the build.** `build` writes its own `MotorsportDX.exe` into `build\game-build`, from the same
tag. The shipped launcher hands over to it (starts it and exits; a file in use cannot be replaced),
and later starts it directly when `build-result.json`'s version stamp names its own tag; otherwise it
offers **Rebuild**. The shortcut points at the shipped one.

## 7. Increments

Estimates in AI calendar time (1 person-week ≈ 3.5 h, range 2 to 7 h). B0 is the owner's.

| Increment | Contents | Estimate | Verification |
| --- | --- | --- | --- |
| B0 | The owner's decisions of section 9; `main` pushed to GitHub; a first tag | owner | - |
| B1. Pipeline for a builder (**done** on 31 October 2026, [results](#b1-results-31-october-2026)) | A `preflight` action with JSON output (CPU features, GPU, RAM, disk, path); `--progress` JSON lines per step; a `launcher-data` action that writes `launcher.json` without a build; a version stamp file in the snapshot, read by `source_state` when the source is not a Git checkout; running from a `git archive` snapshot without `Build.ps1`; the player build without the developer tests if the owner says so | 3.5 h | `test_pipeline.py` cases; `all` from a `git archive` snapshot into a new BuildRoot on the development computer (build only, no GPU run) |
| B2. No Git (**done** on 31 October 2026, [results](#b2-results-31-october-2026)) | The SDK and its submodules as archives at their pinned commits with a hash over the unpacked files; FidelityFX only for a build with FSR; the SDL patch applied in Python; `doctor` without `git` | 3.5 h | the unpacked files' hash equals that of the development BuildRoot's `runtime-source\thirdparty`; `all` with Git off the `PATH` |
| B3. The portable tools (**done** on 31 October 2026, [results](#b3-results-31-october-2026)) | `toolchain.json` (asset, size, SHA-256, licence, unpack rule) for Python (embeddable), CMake, Ninja and LLVM, each downloaded once with the owner's approval to pin it; the unpackers; detection of installed tools; the resolve shaders through `D3DCompile` instead of `fxc.exe`; `doctor` without `link`, `fxc.exe` and `vswhere` | 5 h | the `D3DCompile` headers against the `fxc.exe` ones on the development computer; `all` with only the portable tools and Visual Studio's libraries |
| B4. Microsoft's libraries (**replaced** on 31 October 2026 by route (c), llvm-mingw: [4.7](#47-the-msvc-libraries-and-the-windows-sdk)) | Route (b) after the licence reading: the manifest reader, the pinned package list (MSVC 14.44 x64, Windows SDK 10.0.26100), the licence page, the VSIX, MSI and CAB unpacking, `INCLUDE` and `LIB`, the app-local C++ runtime DLLs; route (a) as the fallback | 7 h | `all` in a Windows account or sandbox without Visual Studio, LLVM, Python or Git (Windows Sandbox on Windows 11 Pro is an optional feature the owner turns on, or a VM), then the instruction tests; a game run there is the main session's call under the GPU rules |
| B5. Builder mode in the launcher (**done** on 1 November 2026, [results](#b5-results-1-november-2026)) | The Build page and wizard, consent, progress, plain errors, resume, logs, keep-awake; the launcher with a static C++ runtime; `--screenshot build` and `--state builder` for reviews | 7 h | the launcher tests; screenshots at scale 1 and 1.5 (WARP); a full build through the launcher into a new install root on the development computer |
| B6. The package | A `package` action at a tag: the launcher built at the tag (needs only `setup`, no discs), `launcher.json`, `git archive` source, `toolchain.json`, licences and notices, into one zip; an allowlist check of the zip (no BuildRoot file, no executable but `MotorsportDX.exe`, no picture, no DXC, nothing of Microsoft's, a size limit) | 3.5 h | the zip's listing against the allowlist; the zip in the clean account or sandbox of B4 up to a built game |
| B7. Updates | Pointing a new package at an install; the source swap; `generate` and `build` again; the `user-data` backup | 2 h | an update from one tag to the next on the development computer: the hashes of `user-data` before and after, the shader pass runs again |
| B8. Second computer | The package on a second Windows computer (the ROG Xbox Ally X or another), run by the owner, from download to a Quick Race, with the build time measured against the estimate of 4.11 | 1 h of notes; the owner's run | the owner's report, as [bug reports](../bug-reports.md) asks |
| B9 (optional). ISO input | **Done on 1 November 2026**: Disc 1 and Disc 2 from Xbox 360 disc images, unpacked once into a folder by a GDFX reader of its own in `pipeline.py` and its port in the launcher ([Disc images](../setup.md#disc-images)) | 5 h | the USA images unpack byte-identical to their extracted folders ([status](../status/current.md#disc-images-iso-1-november-2026)); the Japan/Asia images are not checked |
| B10 (optional). Option B | The shipped runtime, after the licence items of 3.2 | 3.5 h, plus the licence work | a game built against the shipped runtime has the hashes of one built from source |

B1 to B7: about 31.5 h (18 to 63 h). No increment needs a game run on the GPU until B8, whose run is
the owner's. Route (c) of 4.7 replaced B4 on 31 October 2026; its own increments (M1, the pipeline
and the code; M2, the verification) are in [mingw-toolchain.md](mingw-toolchain.md).

### B1 results (31 October 2026)

What B1 added to `pipeline.py` is documented in [setup](../setup.md#builder-interface): the
`preflight`, `launcher-data` and `snapshot` actions, `--progress`, `--tests player`, the version
stamp that `source_state` reads in a snapshot, the rule that a snapshot's BuildRoot lies outside
it, and local mirrors that are used before the network (with `THIRDPARTY_MIRROR` for developers).

- **CPU features.** `preflight` reads all seven x86-64-v2 features with the CPUID instruction, run
  from a small routine in executable memory through `ctypes`. Where that cannot run it falls back
  to `IsProcessorFeaturePresent`, which has no flag for POPCNT or LAHF/SAHF: those are then reported
  `unchecked`, never passed. On the development laptop: CPUID, all seven present; the RTX 5070 Ti
  Laptop GPU and the Intel graphics pass at feature level 11_0; `suggested_jobs` 8 from 31.4 GiB.
- **`launcher-data`** wrote a `launcher.json` equal, as JSON, to the one the development BuildRoot's
  build of `627e914` wrote.
- **A snapshot build, no network.** `snapshot` of `5089673` (1,257 files and the stamp, 3.7 s) into
  `I:\FM4-src-b1`; then `pipeline.py all --tests player --jobs 8` run with Python directly (the
  Visual Studio x64 environment and LLVM 20 set up by the caller, no `Build.ps1`) into the new
  BuildRoot `I:\FM4-build-b1`, with USA Disc 1 and Disc 2. The fetches came from local copies only:
  the FidelityFX bundle, the GameInput and DXC files of the development BuildRoot, and its
  `runtime-upstream` through `THIRDPARTY_MIRROR`, with `GIT_NO_LAZY_FETCH=1`. Times from the
  progress lines:

  | Stage | Time | Largest steps |
  | --- | --- | --- |
  | `setup` | 219.6 s | copying the third-party libraries 58.9 s, the runtime's configure 58.4 s and build 55.5 s |
  | `generate` | 38.8 s | `codegen` 27.4 s |
  | `build` with `--tests player` | 177.8 s | the game 131.5 s; the tests' configure and build 3.6 s; running the instruction, CPU and launcher tests 8.9 s (the wheel host test 8.6 s) |
  | `all` | 436.5 s | |

  The instruction tests, the ten CPU tests and the launcher tests (558 plans, the discs and Disc 2's
  four packages) passed. `build-result.json` names the stamp's commit, `dirty` false. Against the
  clean setup of 25 October 2026 (about 16 minutes with the fetches over the network and every
  test), the player build here took about 7.3 minutes without network fetches.
- **The developer path is unchanged.** An incremental `build` in the same BuildRoot with the default
  tests ran every test, the three shader tests included, and passed in 523.5 s: the shader-mod
  tests 451.4 s, the post pass tests on WARP 30.4 s.
- The BuildRoot held 1.68 GB afterwards (no `runtime-upstream` clone, no `user-data` yet).

Not verified in B1: the fetches over the network in a new BuildRoot (the code paths are unchanged
but for the order of the local copies); `preflight` on another computer, on Windows 10, or with the
fallback of `IsProcessorFeaturePresent` (covered by tests with faked inputs only); a BuildRoot with
spaces or non-ASCII characters; a game run of the snapshot's build (not part of B1).

### B2 results (31 October 2026)

What B2 added to `pipeline.py` is documented in [setup](../setup.md#fetching-without-git): `--fetch
archive` (the default in a source snapshot) takes GitHub's archives of the pinned commits, kept in
`BuildRoot\downloads`; the FidelityFX SDK is fetched only for a build with `-Fsr on`; the SDL patch
is applied in Python; `doctor` asks for `git` only with `--fetch git`. The developer path (`--fetch
git`, the default in a checkout) keeps the clones and `THIRDPARTY_MIRROR`: it is unchanged but for
the SDL patch and FidelityFX's fetch, which follow the same rules on both paths. One change in the
runtime fork: its `thirdparty/CMakeLists.txt` checked each submodule for a `.git` file; it now asks
for any file in the folder, since an archive has no `.git`.

- **The hash rule.** A SHA-256 over each file's path, a zero byte and the SHA-256 of its contents
  with every CRLF read as LF, in path order; a submodule's `.git` file and the pipeline's `.fm4-*`
  markers are left out. Reading CRLF as LF makes a Git for Windows checkout (CRLF) and the archive
  (LF) compare; the files keep the archive's endings. Symbolic links (15 in libmspack, one in
  o1heap) are written as files holding their target, as the development checkout has them
  (`core.symlinks=false`).
- **The downloads** (approved by the owner on 31 October 2026, made once, 21 seconds in all):

  | Archive | Commit | Bytes | Files kept | SHA-256 of the archive (not pinned) |
  | --- | --- | --- | --- | --- |
  | rexglue/rexglue-sdk | `f5337cd` | 51,283,324 | 59 (the 11 vendored folders) | `58aa7ea03ea79db428ed7d52c96bdad0780d5680459948c0517c1de81570113c` |
  | wmarti/FFmpeg | `0604b46` | 13,842,847 | 7,501 | `cb05b8b2a0ea051150bd70bc98091cd1e27f92f5000b48bbfe3828b0e89d4991` |
  | libsdl-org/SDL | `8bf3b72` | 15,977,394 | 2,200 | `bb4f83f5d6041c3a22186cdac4e15ed71e4127a1a8532dbb382f4b730a334b61` |
  | simd-everywhere/simde | `71fd833` | 7,481,259 | 1,332 | `72b2c14a487560b7eb203795f2c2fead5c7499662e639944cca2a9bb19f09029` |
  | ocornut/imgui | `6d910d5` | 2,012,931 | 268 | `b715a3226320aaebaea79dc6af6405ae37cdcb479817fddaef12a17d8bd15a3a` |
  | marzer/tomlplusplus | `3017243` | 1,295,179 | 212 | `291254ffe7f2433f90deef878d0d9335534a350a958ea23ecf511b7b2277bf7f` |
  | Cyan4973/xxHash | `e626a72` | 1,147,772 | 87 | `34e71c085636fbc1726482996d92719d0a16e6a6097970ecf2990a27b3821162` |
  | fmtlib/fmt | `407c905` | 711,737 | 128 | `2bc1fe4a5b6d5d6a614239b4ca1d520e66e152a02d3d262684d26dfd6ab3438a` |
  | kyz/libmspack | `3059077` | 409,099 | 319 | `342427b589ec04ba50ac5662119400955cbb0d9c52259ad1c8188dae505ab0f2` |
  | pavel-kirienko/o1heap | `388a73f` | 401,202 | 29 | `b9c6c40d1eda938028c8a9e3d1de3a4e0762bd6253bcaef37169e8962e8a5ec9` |
  | CLIUtils/CLI11 | `bfffd37` | 392,159 | 296 | `03c9b7921b8f99ca39ae660b03ebf9bd5a3f4280201f7d04bc5639b1e0496401` |
  | pantor/inja | `7d1b460` | 387,554 | 102 | `e86aee0376a9ae726bff9d9309e1319bc6e9ddce3d3204df21953aa6f0d96899` |
  | gabime/spdlog | `79524dd` | 281,845 | 177 | `c6dee844e35a54cc80e3ab76aae2310763e0b49c7397549dfa4d4657d9737138` |
  | nemtrif/utfcpp | `63d64de` | 35,376 | 29 | `f11186e50579ba4404ad531be74f30b31fa8986abe54f4742cfef52009a4f614` |
  | nemequ/munit (SIMDe's `test/munit`) | `da8f734` | 27,727 | 11 | `835a9ee03e3b9560af5a2e4465394e00337398f5baba9d686083b8e1d93f0c30` |
  | 15 archives | | 95,687,405 | 12,750 | |

  Each archive named its commit in its pax comment. The archives' own SHA-256 are recorded here
  only, to tell later whether GitHub's bytes changed; the pins are the hashes of the unpacked files.
- **Against the development BuildRoot.** The 24 folders unpacked from the archives (12,750 files)
  give the same hash as `I:\FM4-build-usa\runtime-upstream\thirdparty`, the Git checkout of the same
  commits, folder by folder and in all. After the SDL patch, 23 folders equal those of its
  `runtime-source\thirdparty`; in SDL the three patched files differ only in line endings: the
  development copy has CR CR LF on every line, the Python patch LF. The old `git apply` in a
  scratch copy wrote CRLF there under `core.autocrlf=true`, and the copy back turned each LF into
  CRLF once more; `I:\FM4-build-b1` has the same. The compiler read those lines as blank lines in
  between, harmless in these files (no line continuations). The Python patch reversed gives the
  pinned SDL tree again, and applied to the CRLF checkout it keeps CRLF.
- **FidelityFX** was not downloaded (not approved). A `git archive` of `FFX_REV` made from the
  bundle `D:\FM4-archive\third-party\fidelityfx-sdk-v1.1.4.bundle` (14,672,438 bytes, `FFX_PATHS`
  only, LF endings as GitHub's server writes them) unpacked to 493 files with the hash of the
  development BuildRoot's sparse checkout; that hash is `FFX_TREE`. The size of GitHub's full
  archive of the SDK is not known.
- **A snapshot build without Git.** `snapshot` of `7dfb1bf` (1,257 files and the stamp, 5.7 s) into
  `I:\FM4-src-b2`; then `pipeline.py all --tests player --jobs 8` with Python directly into the new
  BuildRoot `I:\FM4-build-b2` (the archives already in its `downloads`), USA Disc 1 and Disc 2, with
  the two folders that hold `git.exe` removed from `PATH` (no `git` left), the GameInput and DXC
  files of the development BuildRoot as local copies, and no `THIRDPARTY_MIRROR` or `FFX_MIRROR`.
  `doctor` passed with `Fetch: archive`. A first run stopped at the runtime's configure on the
  `.git` check above; it was moved to `D:\FM4-archive\FM4-build-b2-attempt1` and run again after
  the fix. Times from the progress lines:

  | Stage | Time | Largest steps |
  | --- | --- | --- |
  | `setup` | 211.9 s | unpacking and checking the 15 archives 56.2 s, the runtime's configure 55.1 s and build 59.9 s; the SDL patch's check and patch under 0.1 s each |
  | `generate` | 38.8 s | `codegen` 26.9 s |
  | `build` with `--tests player` | 192.6 s | the game 142.7 s; the wheel host test 8.5 s; the launcher 6.1 s |
  | `all` | 443.6 s | |

  The instruction, CPU and launcher tests passed; `build-result.json` names `7dfb1bf`, `dirty`
  false, `tests: player`. The BuildRoot held 1,719,425,921 bytes afterwards (1.72 GB, the 95.7 MB
  of archives included, no `runtime-upstream`).
- **FSR without Git.** In the same BuildRoot, `build --fsr on --tests player` with `FFX_MIRROR`
  naming the local archive: 66.5 s (copying and unpacking the archive 2.6 s, the runtime with the
  FSR 3 upscaler and its DX12 backend 20.4 s, 115 steps, the game 10.5 s); the runtime's cache has
  `REX_FSR=ON` with the unpacked SDK. The BuildRoot then held 1.80 GB.
- **Tests.** `test_pipeline.py`: 43 pass (7 new: the hash rule, the unpacker's selection and
  refusals, the archive fetch with its pins and `.rejected`, the pins' consistency, the patch
  applier exact, refusing and with CRLF, `apply_patch` writing nothing on a failure, the fetch mode
  and `doctor` without Git).

Not verified in B2: the FidelityFX archive from GitHub (its bytes, size and whether its files give
`FFX_TREE`; the pin comes from a local `git archive`); a download that fails or is cut off midway (the
code renames only a finished download into place); a second computer; a game run of the build. The
runtime's version falls back to `0.10.0.0-dev.unknown` in a snapshot (a CMake warning: its version
comes from `git describe`), as in B1; the stamp could give it, before B6.

### B3 results (31 October 2026)

What B3 added to `pipeline.py` is documented in [setup](../setup.md#portable-tools): the tracked
[`toolchain.json`](../../outputs/FM4-build/toolchain.json), the `toolchain` action with the
detection of installed tools, the unpackers, and `--toolchain` for the other actions; the resolve
shaders through `D3DCompile` ([setup, 1](../setup.md#1-setup)); `doctor` without `link`, `fxc.exe`
and `vswhere`, which checks `INCLUDE` and `LIB` instead. `fxc.exe` is no longer used anywhere in
the pipeline, on the developer path too. `patch_text` now reads CR CR LF as one line ending and
writes it back, so a later change of the SDL patch reverses the old one in the development
BuildRoots, whose three patched SDL files have it from the old `git apply` (their files were not
touched).

- **The downloads** (approved by the owner on 31 October 2026, made once with `curl` into
  `I:\FM4-toolchain-b3\downloads`, 77 seconds in all, 71 of them LLVM's): each file has the size
  the release page gives, and LLVM's archive has GitHub's digest; the SHA-256 of the other three
  are the downloaded files' (their publishers' checksum files were not fetched). The pins are in
  `toolchain.json` and in [sources and licensing](../sources-and-licensing.md#third-party-tools-the-builders-portable-toolchain).

  | Tool | Archive bytes | Members | Kept files | Kept bytes |
  | --- | --- | --- | --- | --- |
  | Python 3.11.9 embeddable | 11,249,023 | 34 | 34 | 21,665,567 |
  | CMake 3.31.6 | 46,473,549 | 8,146 | 3,767 | 20,927,837 |
  | Ninja 1.12.1 | 275,425 | 1 | 1 | 569,856 |
  | LLVM 20.1.8 (`.tar.xz`) | 939,541,912 | 5,010 (4,730 files, 280 folders, no links; 4,189,252,381 bytes) | 301 | 304,219,141 |
  | Together | 997,539,909 | | 4,103 | 347,382,401 |

  LLVM's selection is what the builds run: `clang`, `clang++`, `lld-link` (`CMAKE_LINKER`, and
  `-fuse-ld=lld-link`, which CMake adds), `llvm-ar` (`CMAKE_AR`), `llvm-rc` (`CMAKE_RC_COMPILER`,
  which CMake takes when there is no `rc.exe`; no build compiles a resource script), Clang's headers
  and its builtins and profile libraries. `clang.exe` and `clang++.exe` are two copies of the same
  101.9 MB program in the archive, and both are kept. CMake keeps `cmake.exe`, its modules and its
  copyright file, not `cmake-gui`, `ctest`, `cpack`, `cmcldeps` or the documentation. The LLVM, CMake
  and Ninja executables import only Windows' own DLLs (read with `llvm-objdump`): they need no
  Visual C++ redistributable. The `toolchain --portable` action unpacked the four from the kept
  archives in 44.3 s, 42.9 s of it LLVM's `.tar.xz` (its SHA-256 check included).
- **Detection.** The same run reported the installed copies on the development computer: Python
  3.11.9 (`C:\Program Files\Python311`), the CMake 3.31.6 and Ninja 1.12.1 that Visual Studio
  bundles, and LLVM 20.1.8 in `C:\Program Files\LLVM\bin`, each accepted; without `--portable` the
  action would fetch nothing here.
- **`D3DCompile` against `fxc.exe`.** The 33 headers of the resolve shaders, the readback downscale
  shader and the presenter's area downscale shader, compiled by `fxc.exe` of the Windows SDK
  10.0.26100.0 (with its `d3dcompiler_47.dll` 10.0.26100.7705) and by `D3DCompile` through `ctypes`,
  once with `System32`'s `d3dcompiler_47.dll` (10.0.26100.9711) and once with the SDK's: equal bytes,
  all 33 in both cases, disassembly listing included; they also equal the development BuildRoot's
  headers of 28 October 2026. The headers that the B3 build below compiled are the same bytes again.
  The 33 compiles took 33.0 s in setup.
- **The embeddable Python.** It ran the whole pipeline below (`python311._pth` unchanged; `pipeline.py`
  puts its own folder on `sys.path`). It carries `_lzma`, `_ssl` (OpenSSL 3.0.13; the default
  context loads 31 CA certificates from the Windows store), `_ctypes` and `_msi`. CMake's
  `find_package(Python3 ... Interpreter)` in the game's project found it ("Found Python3:
  ...\python-3.11.9\python.exe (found version 3.11.9)"), and the guards of `generate` and the
  game's build ran under it. HTTPS was not exercised: every fetch of the run came from local copies.
- **A snapshot build with only the portable tools.** `snapshot` of `cdcfca9` (1,258 files and the
  stamp) into `I:\FM4-src-b3`; the 15 archives of `I:\FM4-build-b2\downloads` copied into the new
  BuildRoot `I:\FM4-build-b3`, and the GameInput and DXC files of the development BuildRoot as local
  copies (`GAMEINPUT_MIRROR`, `DXC_MIRROR`); `INCLUDE` and `LIB` from Visual Studio's
  `vcvars64.bat`, and no other variable of it; `PATH` set to `C:\Windows\System32;C:\Windows`
  only, so no `git`, `fxc`, `link`, `rc`, `mt`, `vswhere`, `cl`, `python`, `cmake` or `clang` was on
  it; then the embeddable Python ran `pipeline.py all --toolchain I:\FM4-toolchain-b3 --tests player
  --jobs 8` with USA Disc 1 and Disc 2. `doctor` passed with `Fetch: archive`, and every CMake cache
  names the portable `cmake.exe`, `ninja.exe`, `clang++.exe`, `lld-link.exe`, `llvm-ar.exe` and
  `llvm-rc.exe`. Times from the progress lines:

  | Stage | Time | Largest steps |
  | --- | --- | --- |
  | `setup` | 210.5 s | unpacking and checking the 15 archives 59.7 s, the runtime's configure 55.1 s and build 61.2 s, the 33 shaders 33.0 s |
  | `generate` | 40.3 s | `codegen` 28.0 s |
  | `build` with `--tests player` | 191.5 s | the game 141.8 s; the wheel host test 8.5 s; the launcher 6.2 s |
  | `all` | 443 s | |

  The instruction, CPU and launcher tests (558 plans, the discs and Disc 2's four packages)
  passed; `build-result.json` names `cdcfca9`, `dirty` false, `tests: player`. The BuildRoot held
  1,719,104,970 bytes afterwards (1.72 GB, the 95.7 MB of archives included), as B2's. The times
  match B2's with the installed tools (443.6 s).
- **Tests.** `test_pipeline.py`: 49 pass (6 new: `toolchain.json`'s schema and pins, the unpackers'
  selection and refusals for zip and tar.xz, the fetch with its pins, `.rejected` and the
  installed-or-portable choice, the detection, the `D3DCompile` call and header, CR CR LF; and the
  `doctor` test rewritten for `lld-link`, `INCLUDE` and `LIB`).

Not verified in B3: a computer without Visual Studio (clang also finds an installed Visual Studio
by itself when `INCLUDE` is unset, so this run shows that the portable tools suffice beside
Microsoft's libraries, not that nothing else of Visual Studio is read; B4's sandbox run shows that);
the downloads through the pipeline's own `fetch_tool` over HTTPS (the pins were made with `curl`;
the run took the kept files); `TOOLCHAIN_MIRROR`; the embeddable Python's HTTPS; a game run of the
build (not part of B3); Windows 10.

### B5 results (1 November 2026)

What B5 added is documented in [running, builder mode](../running.md#builder-mode) and
[setup, builder downloads](../setup.md#builder-downloads): the launcher's builder mode
([builder.h](../../work/fm4-launcher/src/builder.h), the Build page in
[app.cpp](../../work/fm4-launcher/src/app.cpp)), `launcher.json`'s `builder` table, the
`download` action and `ARCHIVE_MIRROR` ([builder.py](../../outputs/FM4-build/builder.py), which
also holds B6's package action: `pipeline.py` reached the 256 KiB limit of
`check-staged-content.py`, so the builder's code moved into a module of its own). The Shaders
page's pinned fetch became `FetchPinned` in [shaders.cpp](../../work/fm4-launcher/src/shaders.cpp),
which the builder's Python fetch shares.

Decisions taken in B5 (the main session may revise them):

- **The pinned tools always** (`toolchain --portable`): the builder never takes an installed CMake,
  Ninja or llvm-mingw, unlike 4.2's "reuse before download". A player's build then uses exactly
  the tools this project tested, for 48 MB more of CMake and Ninja; llvm-mingw has no installer to
  detect anyway.
- **Python, then the preflight.** The preflight is `pipeline.py preflight` and needs Python, so it
  runs after the 11 MB Python fetch and before the other 350 MB. The discs are checked before any
  download, on the Your discs screen, by the launcher's own port of the disc check.
- **The settings live in the BuildRoot** (`build\launcher-settings.json`, with the discs unpacked
  into `build\discs`), so the launcher of the build, which the hand-over starts, keeps the Discs and
  profiles chosen before the build.
- **The steps run separately** (`preflight`, `toolchain`, `setup`, `generate`, `build`), not as
  `all`, so a Retry restarts the failed step with the earlier ones kept.
- **No Microsoft row and no consent page** (route (c)); the download rows are the four tools, the
  sources, GameInput and DXC.

Verification:

- **Tests.** `test_pipeline.py`: 55 pass (3 new: the downloads table against the fetches' own
  paths and install_tool's marker, the download action from its mirrors with the network blocked,
  the package allowlist and `pe_imports`). The launcher tests now also compare the launcher's view
  of the builder: the downloads and steps of `launcher.json`, eight progress lines written by
  `progress()`, Ninja's `[n/total]`, and the plain words of four errors the pipeline raises
  (`disc_release` and `validate_discs` on wrong discs, `fetch_tool` without a source, a step's
  exit); they passed in the build below (558 plans).
- **Screenshots** at scale 1 (1280x720) and 1.5 (1920x1080), WARP: the four screens, the Downloads
  screen with a row focused, the made-up running and failed builds, and Home. Read at 1:1; the
  sizes below 1 MB now show in KB, the paths with backslashes, the bar in the theme's red, and the
  main action above the steps on the Build screen.
- **A full build through the launcher.** `snapshot` of `c40467f` into the new install root
  `I:\FM4-b5-install\source`, `launcher-data` beside it, the launcher of the same source, and
  `build\launcher-settings.json` with the USA Disc 1 and Disc 2; `MotorsportDX.exe --build` with
  `TOOLCHAIN_MIRROR`, `ARCHIVE_MIRROR`, `GAMEINPUT_MIRROR` and `DXC_MIRROR` naming the local copies
  of the handoff, in one exclusive slot of the GPU queue. Every file came from a mirror
  (`build\logs\downloads.log`, `toolchain\logs\toolchain.log`). From the builder's log:

  | Step | Time |
  | --- | --- |
  | Python and the six other downloads, from the mirrors | 3 s |
  | `preflight` (all checks pass, 8 jobs) | under 1 s |
  | `toolchain --portable` (llvm-mingw's 5,406 files in 2.2 s) | 3 s |
  | `setup` | 210 s |
  | `generate` | 36 s |
  | `build --tests player` | 173 s |
  | From the start to the hand-over | 431 s |

  `build-result.json` names `c40467f`, `dirty` false, `tests: player`, `abi: mingw`; the
  instruction, CPU and launcher tests passed. The shipped launcher then started
  `build\game-build\MotorsportDX.exe` and exited.
- **A bug found:** the launcher of the build started its first shader pass, which stopped at once:
  `Unable to read ...\content-packages.txt`. The plan names the content manifest whenever Disc 2
  is set, but only Play wrote it, so the first pass of every new build or export with Disc 2 failed
  before Play (what is left then compiles in play). Fixed: the pass writes the manifest too
  (`WriteContentManifest`); B6's run covers it.
- **The shipped launcher** imports Windows' own DLLs only (30 imports, the Universal CRT's API sets
  among them; read by `pe_imports`): it needs nothing beside it, as M1 linked it.
- **The compiler's search paths**: with nothing but `System32` and `Windows` on `PATH`, llvm-mingw's
  `x86_64-w64-mingw32-clang++ -v` searches its own `include\c++\v1`, `lib\clang\20\include` and
  `include` only, and links from its own `x86_64-w64-mingw32\lib`, `mingw\lib`, `lib` and clang's
  `lib\windows`; with `INCLUDE` and `LIB` set to other folders it reads neither. Unlike the MSVC
  target, it does not look for an installed Visual Studio.

Not verified in B5: a download over the network (every file came from a mirror; the urlmon fetch
of Python and the pipeline's HTTPS were not exercised), the download rows pressed one by one,
Pause and Retry in a real run (the code paths are the job object and the markers), the battery
warning, a slow computer, and a game run of the build (not part of B5).

## 8. Risks

- **Microsoft's libraries.** Route (b) depends on terms not yet read for files fetched without
  Microsoft's installer, and on Microsoft keeping the pinned payloads on its servers; Microsoft could
  also object to a program that automates it. Route (a), the fallback, is the largest download and
  the only admin step, its terms for a player without a Visual Studio licence are not checked, and a
  newer MSVC library may require a newer clang than 20, which would break a build on a computer that
  installs the Build Tools after the tag. Mitigation: route (b) pins MSVC 14.44; route (a) reports
  the MSVC version it finds.
- **Upstream availability.** GitHub, NuGet, Microsoft and the toolchain hosts. FidelityFX,
  GameInput and DXC have mirrors; the ReXGlue SDK and its submodules have none. GitHub's archives
  are pinned by their unpacked files, so a changed compression does not break them, but a deleted
  commit (FFmpeg's sits on a branch of Xenia's fork) would.
- **Antivirus and SmartScreen.** An unsigned program that downloads compilers and runs them is what
  heuristics flag; Windows SmartScreen warns on an unsigned download. Signing costs money (owner).
  Real-time scanning also slows the build; the builder may say so but never adds an exclusion.
- **Paths.** Spaces, non-ASCII Windows user names, synced folders (OneDrive) and the 260-character
  path limit with the generated tree are not checked. Mitigation: the default install root is a
  short folder on the chosen drive (for example `D:\MotorsportDX`), checked by the preflight.
- **Slow and small computers.** The build time and the memory per compile job are not measured
  below the development laptop (4.11 estimates the Ally X); the preflight sets `-Jobs` from the RAM.
- **Support load.** About ten downloads and three build stages: every failure lands on the
  project. The plain errors and **Copy details** keep reports useful.
- **Visibility.** The loader decrypts the player's `default.xex` with the runtime's XEX code; that is
  true of the source today, but a one-click download makes the project more visible. This note is
  not legal advice; whether to seek some is the owner's call.
- **The distribution rule.** Shipping the launcher changes what [distribution](../sources-and-licensing.md#distribution)
  allows today. A launcher passed around outside the package (without its source and tag) cannot
  build anything, but it must still carry `LICENSE.txt` and the notices.
- **Discs moved after the build.** The game and every rebuild read the Disc paths of `local.json`;
  the Game data page must update them (and `generate` run again) when the player moves a disc.

## 9. Decisions for the project owner

The owner decided items 1, 2, 4, 5 and 6 on 31 October 2026 ("setuju semua, mulai B1"). Items 3
and 7 to 11 stay open; they are settled before B6.

1. **Whether the launcher may be distributed on its own**, inside the builder package. Today
   [distribution](../sources-and-licensing.md#distribution) says the runtime DLLs and the launcher are
   not distributed on their own; this note does not change that rule. The launcher alone is
   licence-clean (section 3.1) and useless without a build.
   **Decided on 31 October 2026: yes**, the launcher may be distributed inside the builder package.
2. **Option A or B.** Recommended: A. B only after the LGPL relinking, the GPL source of
   `rexglue.exe`, the Unleashed Recompiled question and the two vendored files are resolved.
   **Decided on 31 October 2026: option A.**
3. **Publication:** push `main` to GitHub (about 220 commits behind local `main`), the first tag and
   its name, where the package is hosted (for example the repository's releases), and whether the
   first package goes to a closed or a public preview ([preview plan](preview.md)).
4. **The route for Microsoft's libraries** (4.7): (b), Microsoft's packages unpacked after the
   player accepts the terms in the builder (recommended, once the terms are read and allow it), with
   (a), Microsoft's Build Tools installer, as the fallback; not (c).
   **Decided on 31 October 2026: route (b)**, once the main session has read Microsoft's terms;
   route (a) is the fallback. **Superseded the same day: route (c)** (below). **Reopened the same day:** the terms do not clearly cover a player
   who builds only to play, by either route, and route (b) has two questions of its own
   ([the licence reading](#47-the-msvc-libraries-and-the-windows-sdk)). **The owner's decision of
   31 October 2026: research route (c), llvm-mingw, first**: a design note with the port's scope,
   cost and risks, and a prototype plan, before any download or port. After the prototype, **the
   owner's decision of 31 October 2026: move to llvm-mingw** ("Lanjut pindah"), for players and
   developers; B4 is replaced.
5. **The downloads the package may make on a player's computer**, and the approval to download each
   new one (LLVM 20.1.8, the Python embeddable package, CMake, Ninja, MinGit, the pinned Microsoft
   packages and the Build Tools bootstrapper) once on the development computer to pin its size and
   hash (B3, B4).
   **Decided on 31 October 2026: approved in principle**; the main session asks the owner again
   before each stage's downloads (B2, B3, B4).
6. **A player build without the developer tests** (4.11): the instruction and launcher tests stay,
   the WARP shader-mod and post pass tests go; about 7 minutes less here.
   **Decided on 31 October 2026: yes**, the player build skips the developer tests (`--tests
   player`, B1).
7. **ISO input** (B9): yes or no, and unpacked to a folder or read in place.
   **Decided on 1 November 2026: yes, unpacked to a folder** ("Bongkar ke folder"); done the same
   day.
8. **Code signing** of the package's launcher, or the SmartScreen warning.
9. **Updates:** a new package by hand (recommended first), or a check of the repository's releases
   from the launcher, which is a network request the player would consent to.
10. **The name** of the package (for example "Motorsport DX Builder") and its page; no Forza names or
   logos.
11. **Legal advice** on the visibility of a one-click builder (section 8), if the owner wants it.
