# Builder installer (31 October 2026)

A design for a **builder installer**: a free download that carries none of the game and guides a
player from their own discs to a built, running Motorsport DX on their own Windows computer. It was
written as a design, with no code changed, nothing downloaded and no game or GPU run. Sizes and
licence terms that only a download could confirm are marked "not checked". The owner decided its
first questions on 31 October 2026 (section [9](#9-decisions-for-the-project-owner)), and
increment B1 is done (section [7](#7-increments)).

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
| Disc 1 or Disc 2 as an ISO | Not today | `validate_discs` says "not an ISO" | An optional increment ([7](#7-increments)) and an owner question ([9](#9-decisions-for-the-project-owner)) |

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
| Python 3.11 x64 | python.org's Windows embeddable package ([4.4](#44-python)) | Yes (a zip); pinned once the asset is hashed | PSF License 2.0: fetch yes; shipping is allowed with its notices and those of the libraries it bundles (not checked which), but it is fetched to keep the package small | not checked | the full install 125 MB; the embeddable package not checked |
| CMake 3.25+ | Kitware's Windows x64 zip ([4.5](#45-cmake-and-ninja)) | Yes; version and hash to pin | BSD-3-Clause: fetch and ship allowed | not checked | Visual Studio's copy with Ninja: 58 MB |
| Ninja | the Ninja project's Windows zip | Yes; one executable | Apache-2.0: fetch and ship allowed | not checked | one executable |
| LLVM 20 (clang, clang++, lld-link, llvm-ar, llvm-rc) | the LLVM release's Windows archive, unpacked ([4.6](#46-llvm-20)) | Yes if 20.1.8 has a Windows archive (not checked); else the installer's files unpacked without running it (not checked) | Apache-2.0 with LLVM exceptions: fetch and ship allowed | not checked | full install 2,427 MB; the members the build uses: not checked |
| MSVC x64 tools (the C and C++ runtime and library headers and libraries) | route (b): Microsoft's packages from Microsoft's servers after the player accepts the licence ([4.7](#47-the-msvc-libraries-and-the-windows-sdk)); route (a) as the fallback | (b): yes, pinned by the SHA-256 that Microsoft's manifest lists per payload. (a): no, admin | Microsoft's Visual Studio terms. **Ship: no.** Fetch: (a) yes, through Microsoft's installer; (b) **not checked** whether the terms cover files fetched without the installer | not checked ((b) smaller; (a) several GB) | MSVC tools 1,411 MB installed, with parts the build does not use |
| `link` | **lld-link** from LLVM, which the builds already use: `CMAKE_LINKER` is LLVM's `lld-link.exe` in the development BuildRoot's runtime and game build caches. Only `doctor`'s check of `link` goes | - | - | - | - |
| Windows SDK (headers, libraries) | with the MSVC libraries: route (b) or (a) | as above | Microsoft's SDK terms: ship no; fetch as above | not checked | the Windows Kits folder 1,708 MB with five SDK versions |
| `fxc.exe` (Windows SDK) | **Avoided:** Windows' own `d3dcompiler_47.dll` ([4.8](#48-fxc-replaced-by-d3dcompile)) | nothing to fetch | part of Windows | none | none |
| `rc.exe` (Windows SDK, found by CMake) | `llvm-rc` from LLVM; `work/` has no resource script (the third-party sources: not checked) | - | - | - | - |
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
  its own folder: the builder adds that folder to the `._pth` file, or `pipeline.py` puts its own
  folder on `sys.path` (one line);
- the modules it carries (`_ssl` for HTTPS, `_lzma` to unpack a `.tar.xz`): not checked;
- the version: 3.11.9 runs the pipeline today; whether 3.11 has a later Windows binary release is not
  checked, and a later Python would need its own run of the pipeline's tests.

### 4.5 CMake and Ninja

Both have official zip releases that run unpacked: Kitware's `cmake-<version>-windows-x86_64.zip` and
Ninja's `ninja-win.zip`. Pin CMake 3.31.6 and Ninja 1.12.1, the versions the development builds use
from Visual Studio, or newer ones checked once. Both licences (BSD-3-Clause, Apache-2.0) would also
allow shipping them; fetching keeps the package small.

### 4.6 LLVM 20

The build needs clang, clang++, lld-link, llvm-ar and llvm-rc, with clang's own headers and builtins
(`lib\clang\20`). The LLVM project's GitHub release of 20.1.8 has a Windows installer; whether it also
has a Windows archive (`clang+llvm-20.1.8-x86_64-pc-windows-msvc.tar.xz`) is not checked. Either way
the builder unpacks rather than installs, and can keep only the members the build uses: the full
install is 2,427 MB on the development computer, mostly what the build never runs (the size of the
selection is not checked). Unpacking a `.tar.xz` needs Python's `lzma` or Windows' `tar.exe` with xz
support (neither checked).

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
the bytecode may differ: B3 compares the headers of both on the development computer and runs the
resolve paths' checks. About 1.5 h, inside B3.

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
| 3. Plan and consent | One page lists what is missing: each download with its host, size, licence (a button opens it) and where it goes, and Microsoft's terms for its C and C++ libraries and Windows SDK, which the player accepts there (route (b)); with route (a), the Build Tools' elevation prompt is announced | the player's | the player declines |
| 4. Toolchain fetch | Each download to `downloads\`, checked against its pin, unpacked into `toolchain\`; with route (a) the Build Tools' installer runs in its own window | not measured | network, a hash mismatch, a payload Microsoft has removed, a declined elevation |
| 5. Setup | `setup`: the runtime source, the SDK's third-party libraries, GameInput, DXC, the resolve shaders, the runtime build | about 5.2 min with its fetches ([4.11](#411-the-first-build-on-a-slower-computer)) | GitHub unreachable, a compile error |
| 6. Generate | `generate`: the loader reads `default.xex`, the switch tables, `rexglue.exe codegen`, the guards; writes `local.json` | about 0.8 min | disc moved since step 2 |
| 7. Build | `build`: the game (141 s), the DLLs staged, DXC and the C++ runtime DLLs staged, the instruction, post pass (WARP), CPU and launcher tests, `build-result.json`, the launcher | about 10.5 min, 7.5 of it tests | a compile error, a test failure |
| 8. Content import | The first Play imports Disc 2 (2.8 GB) and the DLC into `user-data` | 15 s for Disc 2 and eight DLC packages on 25 October 2026 | disk full |
| 9. Offline shader pass | The Shaders page's pass, which the launcher starts by itself once per build ([Shaders page](../running.md#shaders-page)) | not part of this design's measurements | GPU or driver problems |
| 10. First run | Play. The new profile plays the game's intro video, gamma adjustment and intro race | minutes | crashes: the error banner and the logs |

Disk: the BuildRoot held 2.6 GB after `build` in the clean setup; Disc 2's import adds 2.8 GB and
the DLC about their own size; the installed toolchain on the development computer is about 5.6 GB
(LLVM 2.4 GB, MSVC 1.4 GB, the Windows Kits 1.7 GB with five SDK versions, Python, Git), and the
builder's portable selection is smaller (not checked). A provisional preflight floor of 20 GB free,
to be set from the measurements of B3 and B4.

**How the builder drives the pipeline.** It runs `pipeline.py` directly with the toolchain's Python,
not `Build.ps1`, so the PowerShell execution policy never matters. It builds the environment
itself: `PATH` with its own LLVM, CMake, Ninja and Python, and `INCLUDE` and `LIB` with the unpacked
Microsoft libraries of route (b); with route (a) it captures the Build Tools' `VsDevCmd.bat -arch=x64
-host_arch=x64` once instead. It starts each action as a child process with the logs in
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
   one by one. The Microsoft row shows Microsoft's terms with **Accept** and **Decline**; with route
   (a) it says that Microsoft's installer opens and asks for administrator rights.
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
| `Use LLVM 20.x` | "The installed LLVM is not version 20." | Use the builder's own |
| A Microsoft payload refused or missing | "Microsoft's server no longer has this package, or it changed." | Retry; Microsoft's installer instead (route (a)) |
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
| B3. The portable tools | `toolchain.json` (asset, size, SHA-256, licence, unpack rule) for Python (embeddable), CMake, Ninja and LLVM, each downloaded once with the owner's approval to pin it; the unpackers; detection of installed tools; the resolve shaders through `D3DCompile` instead of `fxc.exe`; `doctor` without `link`, `fxc.exe` and `vswhere` | 5 h | the `D3DCompile` headers against the `fxc.exe` ones on the development computer; `all` with only the portable tools and Visual Studio's libraries |
| B4. Microsoft's libraries | Route (b) after the licence reading: the manifest reader, the pinned package list (MSVC 14.44 x64, Windows SDK 10.0.26100), the licence page, the VSIX, MSI and CAB unpacking, `INCLUDE` and `LIB`, the app-local C++ runtime DLLs; route (a) as the fallback | 7 h | `all` in a Windows account or sandbox without Visual Studio, LLVM, Python or Git (Windows Sandbox on Windows 11 Pro is an optional feature the owner turns on, or a VM), then the instruction tests; a game run there is the main session's call under the GPU rules |
| B5. Builder mode in the launcher | The Build page and wizard, consent, progress, plain errors, resume, logs, keep-awake; the launcher with a static C++ runtime; `--screenshot build` and `--state builder` for reviews | 7 h | the launcher tests; screenshots at scale 1 and 1.5 (WARP); a full build through the launcher into a new install root on the development computer |
| B6. The package | A `package` action at a tag: the launcher built at the tag (needs only `setup`, no discs), `launcher.json`, `git archive` source, `toolchain.json`, licences and notices, into one zip; an allowlist check of the zip (no BuildRoot file, no executable but `MotorsportDX.exe`, no picture, no DXC, nothing of Microsoft's, a size limit) | 3.5 h | the zip's listing against the allowlist; the zip in the clean account or sandbox of B4 up to a built game |
| B7. Updates | Pointing a new package at an install; the source swap; `generate` and `build` again; the `user-data` backup | 2 h | an update from one tag to the next on the development computer: the hashes of `user-data` before and after, the shader pass runs again |
| B8. Second computer | The package on a second Windows computer (the ROG Xbox Ally X or another), run by the owner, from download to a Quick Race, with the build time measured against the estimate of 4.11 | 1 h of notes; the owner's run | the owner's report, as [bug reports](../bug-reports.md) asks |
| B9 (optional). ISO input | Disc 1 and Disc 2 from Xbox 360 disc images: either unpacked into a folder by a small tool on the runtime's GDFX reader ([disc_image_device.h](../../work/runtime/include/rex/filesystem/devices/disc_image_device.h)), or read in place (the three XEX for `generate`, the runtime mounting the image); needs the owner's yes | 5 h | both releases' images against their extracted folders: the same hashes, the same `generate` output |
| B10 (optional). Option B | The shipped runtime, after the licence items of 3.2 | 3.5 h, plus the licence work | a game built against the shipped runtime has the hashes of one built from source |

B1 to B7: about 31.5 h (18 to 63 h). No increment needs a game run on the GPU until B8, whose run is
the owner's. Route (c) of 4.7 is not an increment: it is not recommended.

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
   route (a) is the fallback.
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
8. **Code signing** of the package's launcher, or the SmartScreen warning.
9. **Updates:** a new package by hand (recommended first), or a check of the repository's releases
   from the launcher, which is a network request the player would consent to.
10. **The name** of the package (for example "Motorsport DX Builder") and its page; no Forza names or
   logos.
11. **Legal advice** on the visibility of a one-click builder (section 8), if the owner wants it.
