# Setup and build

This guide covers building Motorsport DX from source on Windows x64 with your own
Forza Motorsport 4 discs: prerequisites, the `doctor` check, the setup, generate
and build stages, optional build flavors, the BuildRoot layout, what is kept out
of the repository, and troubleshooting. To launch the built game and its options,
see [running.md](running.md).

Motorsport DX is an experimental integration. Building it does not turn Forza Motorsport 4
into a complete or bug-free native port; existing rendering, mode-coverage and
performance limitations still apply. For what has been verified, the historical
gameplay results and the open gaps, see [status/current.md](status/current.md) and
[status/history.md](status/history.md). [verification.json](../outputs/FM4-build/verification.json)
records the latest clean build and startup test; it does not certify full gameplay
compatibility.

## Supported game version

- **Forza Motorsport 4 Japan/Asia** (Play Disc media ID `633772A0`) or **USA**
  (media ID `26FF1101`).
- The three executables are checked by SHA-256 hash (`RELEASES` in
  [pipeline.py](../outputs/FM4-build/pipeline.py)) before generation, and all
  three must come from the same release. Other releases, modified executables,
  a mix of executables from both releases, and title updates are rejected. Do not
  rename another release to bypass the check. `generate` prints the release it
  found and saves it in `local.json`.
- The two releases have the same game code and assets: only the headers of the
  executables differ, and the decrypted images are identical. One set of function
  bounds and switch tables serves both, and Disc 2 and the downloadable content
  are the same. See the [release comparison](analysis/releases.md).
- To change the release of a `BuildRoot`, run `generate` with the other Disc 1
  (and Disc 2), then `build`.
- **Disc 1 is required.** It must be the extracted root of the Play Disc with all
  of its assets: the three executables, `Database.xmplr` and the `Media` folder.
  A folder that contains only the three XEX files is rejected.
- **Disc 2 is optional** for a base-game launch. It is required to import its
  additional cars. Supply its extracted root, which must contain the four content
  packages listed below.
- **Downloadable content is optional.** Car and track packs are imported from the
  content packages you supply with `-Content`. See
  [Downloadable content](#downloadable-content).
- ISO extraction is outside this workflow. Extract the discs yourself. No game
  data is downloaded by any step.

Example layout of the input directories:

```text
D:\Games\FM4\
├── Disc 1\
│   ├── default.xex
│   ├── XMediaFacade_default.xex
│   ├── SpeechFacade_default.xex
│   ├── Database.xmplr
│   ├── Media\
│   └── ... all other Disc 1 files
└── Disc 2\
    └── content\0000000000000000\4d530910\00000002\
        ├── 4d53091000000001
        ├── 4d53091000000002
        ├── 4d53091000000003
        └── 4d53091000000004
```

Input discs are read in place; see [running.md](running.md#data-directories).

### Downloadable content

`-Content` takes a content package file or a directory, and it can be repeated.
A directory is searched recursively. Each package must be an STFS package (`CON`,
`LIVE` or `PIRS`) whose header names FM4 (title ID `4D530910`) and marketplace
content (content type `00000002`). Inside a directory, other files and the packages
of other titles or content types, such as system updates and title updates, are
skipped with a message. A file named directly that fails the check is rejected,
and so is a directory that contains no FM4 package. Package file names must be
unique, because each package is installed under its file name. Disc 2's four
packages count towards this check.

Example layout, the console's `Content` folder layout:

```text
D:\Games\FM4\DLC\
└── Car Pack\
    └── 4D530910\
        └── 00000002\
            └── <package file>
```

The paths are saved in `local.json` by `generate` and `all`. `run -Content` replaces
the saved list without generating again. The launcher imports a package into
`BuildRoot\user-data` on the first run that lists it, and keeps it there; see
[running.md](running.md#data-directories). The e2e harness and `warmcache` import
the same packages before their first launch ([content import](e2e/README.md#content-import)).

## Prerequisites

| Component | Requirement |
| --- | --- |
| System | Windows x64, a CPU compatible with x86-64-v2, a GPU that supports Direct3D 12 |
| Git | Git for Windows, with HTTPS access to GitHub (for the SDK, its pinned submodules and the FidelityFX SDK). Setup uses partial and sparse clones; validated version: **2.55.0** |
| Python | **Python 3.11+ x64**, for example from python.org. Use a real `python.exe`, not the Microsoft Store alias in `WindowsApps`. `py -0p` lists the installed versions with their paths: an install for all users is in `C:\Program Files\Python311`, one for the current user in `%LOCALAPPDATA%\Programs\Python\Python311`. Only the standard library is used |
| Visual Studio | Visual Studio 2022 or Build Tools, workload **Desktop development with C++**, MSVC x64 tools |
| Windows SDK | Includes `fxc.exe`. Validated version: **10.0.26100.0** |
| Build tools | CMake **3.25+** and Ninja. Both can be installed through the Visual Studio Installer |
| LLVM | **LLVM 20.x x64**. Validated version: **20.1.8** |

Disk space and time, measured on 25 October 2026 with a fresh clone and a new BuildRoot on
the development laptop (Intel Core Ultra 7 255HX, 20 threads, 32 GB, `-Jobs 8`): `all`
took about 16 minutes, of which the game's own compilation took 141 seconds, and the
BuildRoot held 2.6 GB after `build` (1.1 GB of it the upstream SDK clone with its
submodules). The first run then imports Disc 2 (2.8 GB) and the downloadable content
packages into the BuildRoot, about their own size. Setup downloads the SDK, its submodules
and the FidelityFX SDK from GitHub once. The build directory can be on another drive.
`-Jobs` can go up to the number of threads when the computer has the memory; the default
of 4 is slower.

The `doctor` action checks the prerequisites and reports what is missing: Windows
x64, 64-bit Python 3.11 or newer, `git`, `cmake`, `ninja`, `clang`, `clang++` and
`link` on the path, LLVM 20.x (from `clang++ --version`), CMake 3.25 or newer, and
`fxc.exe` in the Visual Studio developer environment. It also reports the BuildRoot's
FidelityFX SDK checkout (fetched or not, at the pinned commit or not) and its mirror
([the FidelityFX SDK mirror](#the-fidelityfx-sdk-mirror)), and the GameInput package
([the GameInput package](#the-gameinput-package)). `Build.ps1` sets up the
Visual Studio x64 environment itself. The scripts do not silently install system
software or change security settings.

## Commands

Run the commands in PowerShell from the root of the cloned repository. Replace the
example paths with your own. Every stage must use the same `BuildRoot`. Quote paths
that contain spaces.

Windows does not run PowerShell scripts by default (the execution policy `Restricted`). Allow
them for the current PowerShell window only, after you have reviewed `Build.ps1`; this
changes no system setting and ends when the window closes:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
```

```powershell
git clone https://github.com/MotorsportDX/MotorsportDX.git
Set-Location MotorsportDX

$common = @{
    BuildRoot = 'D:\FM4-build'
    LLVMRoot  = 'C:\Program Files\LLVM'
    Python    = 'C:\Python311\python.exe'
    Jobs      = 4
}

.\outputs\FM4-build\Build.ps1 -Action doctor @common

$discs = @{
    Disc1   = 'D:\Games\FM4\Disc 1'
    Disc2   = 'D:\Games\FM4\Disc 2'
    Content = 'D:\Games\FM4\DLC'
}

.\outputs\FM4-build\Build.ps1 -Action all @common @discs
```

Remove the `Disc2` or `Content` entry if you do not use that content. Instead of
`Set-ExecutionPolicy`, a single command can also run with a process-only policy; it takes
the parameters written out, not `@common`:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\outputs\FM4-build\Build.ps1 -Action doctor -LLVMRoot 'C:\Program Files\LLVM' -Python 'C:\Python311\python.exe'
```

### Actions

| `-Action` | Purpose |
| --- | --- |
| `doctor` | Checks the prerequisites. Makes no changes. |
| `setup` | Stage 1: builds and installs the runtime. |
| `generate` | Stage 2: generates the project from the discs. Requires `-Disc1`, optionally `-Disc2` and `-Content`. |
| `build` | Stage 3: builds the game executables, modules and runtime, and the GUI launcher. |
| `all` | `setup`, then `generate`, then `build`. Does not launch the game. |
| `run` | Launches the game. See [running.md](running.md). |
| `warmcache` | Fills the shader cache by running the e2e Quick Race timeline once, on a profile that has been played once to the main menu. See [running.md](running.md#shader-cache). |
| `profbuild` | Builds `game-build-prof`, a copy with line tables and PDBs for profiling. Run `build` first. |
| `export` | Copies the game and the GUI launcher into the `-Out` folder, to run on another computer. Run `build` first. See [running.md](running.md#export-to-another-computer). |

The stages can also run separately:

```powershell
.\outputs\FM4-build\Build.ps1 -Action setup @common
.\outputs\FM4-build\Build.ps1 -Action generate @common @discs
.\outputs\FM4-build\Build.ps1 -Action build @common
```

### Parameters

| Parameter | Meaning |
| --- | --- |
| `-BuildRoot` | Private build directory. Defaults to `work\portable-fm4` in the repository, which Git ignores. It must be a dedicated directory: not the repository root or one of its parents, and not inside `outputs`, `work\fm4-rex` or `work\runtime`. |
| `-LLVMRoot` | LLVM 20.x install directory that contains `bin`. Its `bin` folder is placed first on `PATH`. |
| `-Python` | Python 3.11+ x64 executable. Default: `python`. |
| `-Jobs` | Number of parallel jobs, 1 to 64. Default: 4. |
| `-Disc1` | Extracted Disc 1 root. Required by `generate` and `all`. |
| `-Disc2` | Extracted Disc 2 root. Optional. |
| `-Content` | Content package files or directories (DLC). Optional, one or more, read by `generate`, `all` and `run`. See [Downloadable content](#downloadable-content). |
| `-Flavor` | Compiler experiment for `build`, for example `thinlto,v3`. See [Build flavors](#build-flavors). |
| `-Fsr` | `on` or `off`, for `setup`, `build` and `all`: link AMD FSR 3.1 into the runtime, built from the FidelityFX SDK source (`REX_FSR`). `on` fetches the pinned SDK first. The runtime build folder keeps the choice for later builds; off by default. See [FSR 3.1](#fsr-31-optional). |
| `-FfxMirror` | The FidelityFX SDK mirror for `setup`, `build` and `all`, in place of the `FFX_MIRROR` environment variable: another Git URL or a local `.bundle` file. See [the FidelityFX SDK mirror](#the-fidelityfx-sdk-mirror). |
| `-Out` | The folder of `export`, outside the BuildRoot and the repository. |

## Stages

### 1. Setup

`setup` builds the project's runtime, a D3D12-only fork of the ReXGlue SDK kept in
[work/runtime](../work/runtime). It:

- mirrors `work/runtime` to `BuildRoot\runtime-source`;
- takes the third-party libraries from `rexglue/rexglue-sdk` at commit
  `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`: the vendored directories and the 13
  submodules that the D3D12 build needs. Vulkan, SPIR-V, Tracy and the tests are not
  fetched;
- takes the AMD FidelityFX SDK from `GPUOpen-LibrariesAndSDKs/FidelityFX-SDK`, tag
  `v1.1.4`, commit `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`: a sparse, blob-filtered Git
  clone of the files the FSR 3 upscaler and its DX12 backend build from (about 39 MB plus
  36 MB of Git objects) in `BuildRoot\fidelityfx-sdk`. Setup refuses any other commit, and
  takes it from the [mirror](#the-fidelityfx-sdk-mirror) when GitHub cannot be reached;
- takes the Microsoft.GameInput package, version 3.5.283, from NuGet (2.04 MB) into
  `BuildRoot\gameinput-sdk\3.5.283`, and extracts its two MIT files: the GameInput API v3
  header and its loader source ([the GameInput package](#the-gameinput-package));
- patches the fetched SDL source for GameInput API v3
  ([outputs/FM4-build/sdl3-gameinput-v3.patch](../outputs/FM4-build/sdl3-gameinput-v3.patch));
- compiles the licensed Xenia/ReXGlue resolve shaders with the Windows SDK FXC;
- builds and installs the Release runtime locally.

#### FSR 3.1 (optional)

With `-Fsr on`, `setup` (or `build`) also builds the FSR 3 upscaler and its DX12 backend
from the FidelityFX SDK checkout into the runtime, with the SDK's shader compiler and DXC
from the same checkout (used at build time only). Nothing of it runs yet
([FSR plan](analysis/fsr-plan.md#31-stage-0s-build-25-october-2026)). The license notices
are in [third-party-notices](../outputs/FM4-source-control/third-party-notices/README.txt).

#### The FidelityFX SDK mirror

The SDK is not in the repository, so a copy of the pinned commit keeps setup reproducible
when GitHub cannot be reached or the repository goes away. `FFX_MIRROR` (an environment
variable, or `Build.ps1 -FfxMirror`) names it:

- **A local `.bundle` file.** When the file does not exist, setup writes it after a fetch
  from GitHub: a Git bundle of the pinned commit with its trees and the sparse files' blobs
  (about 15 MB), made from the checkout without network. When GitHub cannot be reached,
  setup checks that the bundle holds the pinned commit and checks it out from there. On the
  development computer it is `D:\FM4-archive\third-party\fidelityfx-sdk-v1.1.4.bundle`,
  written on 25 October 2026.
- **Another Git URL** that holds the tag `v1.1.4` at the pinned commit, used the same way as
  GitHub. The project owner may fork the SDK to their own GitHub account and name the fork
  here; check that the fork has the tag `v1.1.4` (fork every branch, not the main branch
  only, or push the tag to it). A mirror at another commit is refused like GitHub.

```powershell
$env:FFX_MIRROR = 'D:\FM4-archive\third-party\fidelityfx-sdk-v1.1.4.bundle'
.\outputs\FM4-build\Build.ps1 -Action setup @common
```

Without a mirror, setup stops when GitHub cannot be reached and the BuildRoot holds no
checkout yet. An existing checkout at the pinned commit needs no network.

#### The GameInput package

The pad driver choice `gameinput` ([running.md](running.md#pad-drivers)) builds SDL's GameInput
driver against GameInput API v3. `setup` and `build` fetch the Microsoft.GameInput package,
version 3.5.283, from `https://api.nuget.org/v3-flatcontainer/microsoft.gameinput/3.5.283/microsoft.gameinput.3.5.283.nupkg`
into `BuildRoot\gameinput-sdk\3.5.283` (`fetch_gameinput` in `pipeline.py`) and refuse any file
whose SHA-512 is not the pinned one (`59083faf...fe77705`, NuGet's published hash). They extract
only the package's two MIT-licensed files, `include\gameinput.h` (the public header) and
`src\GameInput.cpp` (the loader); the header is used for SDL's sources only, and the loader is
built into SDL. `GAMEINPUT_MIRROR` (an environment variable) names a fallback: another URL of the
same package or a local copy of the `.nupkg` file. An existing package with the pinned hash needs
no network. Nothing of the package is committed or shipped, and nothing in it is run; the
license review is in [sources and licensing](sources-and-licensing.md#third-party-code-microsoftgameinput).

#### The DirectX Shader Compiler

The native path's DXIL shaders (`native_shader_dxil`, on by default where the DLLs are beside the game;
[D3D-level HLE, 19.3.9](analysis/d3d-hle-n6.md#1939-increment-12-design-28-october-2026)) are
compiled by Microsoft's DirectX Shader Compiler, release v1.9.2609. `setup` and `build` take its
zip, `dxc_2026_09_29.zip`, from `BuildRoot\dxc-download\v1.9.2609` (`fetch_dxc` in
`pipeline.py`). Only when the zip is missing do they fetch it, from `DXC_MIRROR` (a local file or
another URL) or else the release URL. A zip whose SHA-256 is not the pinned one is refused
(`ad31b1fc...a4a7e7f1`, GitHub's digest of the asset). `build` copies the x64 `dxcompiler.dll` and
`dxil.dll`, each checked against its own pinned SHA-256, and the release's three license files
beside the game. No export carries them: the GUI launcher fetches the same release on the other
computer ([running.md](running.md#shaders-page)). Without the DLLs the game compiles its shaders
with FXC, as before. The license review is in
[sources and licensing](sources-and-licensing.md#third-party-code-directx-shader-compiler).

### 2. Generate

`generate` validates the input executables and creates the private project:

- A small SDK loader reads your `default.xex` without running any guest code and
  creates a private image (`loaded-image.bin`). Bounded switch dispatches are
  evaluated from that image to regenerate the switch labels.
- ReXGlue generates the main executable and both auxiliary module sources
  (`XMediaFacade_default` and `SpeechFacade_default`).
- Compiler-template transforms add instruction helpers and reject unresolved direct
  branches and labels.

No existing generated tree is required. The generated sources stay in
`BuildRoot\project`. The integration sources are copied from the tracked files
listed in [tracked-files.json](../outputs/FM4-source-control/tracked-files.json)
(`work/fm4-rex/src` and `work/fm4-rex/tests`), and nothing else from a developer
cache. The Disc paths are saved in `BuildRoot\local.json`.

### 3. Build

`build` first mirrors `work/runtime` again and rebuilds the runtime incrementally, so
runtime edits need only this stage. It then:

- copies the title's sources (`work/fm4-rex/src`, `tests` and `CMakeLists.txt`) into the
  private project again, leaving unchanged files untouched, as `generate` does;
- compiles the application and both modules;
- stages the matching runtime and GPU DLLs;
- runs the arithmetic instruction tests, the post pass tests (the GTAO passes on WARP),
  the shader-mod CPU tests (no GPU needed; see the
  [post pass framework](analysis/post-pass-framework.md)) and the CPU tests of the pads, the
  keyboard and the racing wheel (its force feedback governor, and its host against an SDL
  virtual wheel with no force device, [racing wheels](analysis/racing-wheels.md));
- writes `build-result.json`, which lists the SHA-256 hashes of the artifacts, records
  `gameplay_verified: false`, and records in `source` what was compiled: the `head` of the
  checkout that ran the build, `dirty` when its `work/runtime` or `work/fm4-rex` differ from
  it, and `tree`, a SHA-256 over the BuildRoot's `runtime-source` and the project's title
  sources, which another checkout may have left there (a `matrix.py` snapshot keeps the file,
  and each e2e run's `summary.json` copies `source` into `binary`);
- builds the [GUI launcher](running.md#gui-launcher), `game-build\MotorsportDX.exe`,
  from [work/fm4-launcher](../work/fm4-launcher) with the runtime's SDL3, Dear ImGui
  and JSON header, writes its tables to `launcher.json`, copies the look mods and
  post passes into `look` and `passes`, and `CREDITS.txt` and `third-party-notices` beside them;
- runs the launcher tests, which compare the launcher's launch plans and its Disc and
  content checks with those of `pipeline.py`. A flavor build skips the launcher.

### Rebuilding after edits

- After editing `work/runtime`, `work/fm4-rex` or `work/fm4-launcher`, `build` is enough.
  Before 26 October 2026 `build` did not copy `work/fm4-rex`: a game built that way after a
  title edit without `generate` lacks the edit (on 26 October 2026, main's build of `15f828c`
  ran photo mode without its hooks of entry 0 at s and lost the scene).
- Run `generate` again for a new disc release, new content packages, or a change of the
  generator's inputs (`outputs/FM4-build/*.toml`, `rexglue.cmake`).
- A `BuildRoot` created before the runtime fork (one that contains `sdk-source`) is
  rejected. Choose a new `BuildRoot`.

## Build flavors

`build -Flavor` builds an opt-in compiler experiment into
`BuildRoot\game-build-<flavor>` without touching the default `game-build`. The
flavor names are sorted and joined with hyphens, and the build writes
`build-result-<flavor>.json`.

| Flavor | Effect |
| --- | --- |
| `thinlto` | ThinLTO (`-flto=thin`). |
| `v3` | `-march=x86-64-v3`. Newer CPUs only. |
| `pgo-gen` | PGO instrumentation (`-fprofile-generate`). |
| `pgo-use` | PGO use of `BuildRoot\pgo\fm4.profdata`. Merge the `pgo-gen` `*.profraw` files with `llvm-profdata` first. |

Example: `-Flavor thinlto,v3` builds `game-build-thinlto-v3`. `pgo-gen` and
`pgo-use` cannot be combined in one build; the launcher rejects the request.

Profiles stay in `BuildRoot`. Run a flavor build with
`e2e.py --game-build game-build-<flavor>` (see [e2e/README.md](e2e/README.md)). The
default build keeps `-march=x86-64-v2` without LTO or PGO.

## BuildRoot layout

Every private output is written under `BuildRoot`: `project/generated`,
`loaded-image.bin`, `game-build`, `local.json`, `user-data`, `cache` and `logs`.
The default location is `work\portable-fm4`, which the repository ignores. The
layout is:

```text
FM4-build/
├── runtime-source/   # Copy of work/runtime + third-party libraries
├── runtime-upstream/ # Upstream SDK clone (only for third-party libraries)
├── fidelityfx-sdk/   # AMD FidelityFX SDK, sparse clone (only with -Fsr on)
├── gameinput-sdk/    # Microsoft.GameInput package and its two MIT files
├── runtime-build/    # Runtime build
├── runtime-install/  # Locally installed runtime
├── project/          # Integration source and private generated/
├── game-build/       # Executable, modules, runtime, GPU plugins
├── user-data/        # Profiles, saves, imported Disc 2 and DLC content
├── cache/            # Game and shader cache (shader-overrides/: compiled look mods)
├── logs/             # Logs for setup, generate, build and startup
├── launcher-build/   # GUI launcher build and its test cases
├── look/  passes/    # Look mods and post passes the launchers run
├── e2e/              # e2e run output (captures, logs, summary.json)
├── snapshots/        # game-build copies used by the matrix.py gates
├── loaded-image.bin  # Private image of your executable
├── local.json        # Local Disc and content paths, and the release
├── content-packages.txt # Packages the last run imports, written by the launcher
├── launcher.json     # Tables of the GUI launcher, written by build
├── CREDITS.txt       # The projects that made it possible, copied by build
├── third-party-notices/ # The licenses of the third-party code, copied by build
├── launcher-settings.json # The GUI launcher's Discs, profiles, looks and bindings
└── build-result.json # Artifact hashes and build result
```

Nothing is uploaded by `setup`, `generate`, `build` or `run`. Never commit or
publish `BuildRoot`, or a package of its contents.

## What is and is not committed

The repository contains the integration code, the tooling, the synthetic tests,
the function location and boundary metadata, the switch recipes, and the runtime
fork in `work/runtime`. Specifically:

- The TOML function addresses and switch recipes are factual integration metadata
  (locations, lengths, registers and counts). Switch labels and instruction
  contents are read from your executable during `generate`.
- `guard-generated.py` transforms generic compiler output templates locally. It
  stores no original function bodies.
- `rexglue.cmake` is SDK-generated build boilerplate, covered by the SDK notice.
- `work/runtime` contains SDK code (BSD-3, with Xenia-derived portions), not game
  code.

The repository does not contain original FM4 instruction bodies, shader binaries,
game assets, generated game sources, recompilation output, binaries, saves or
captures. Licenses and notices are listed in
[sources-and-licensing.md](sources-and-licensing.md).

The root allowlist excludes newly created files by default. Before committing a
change, run the tracked content check:

```powershell
& 'C:\Python311\python.exe' .\outputs\FM4-source-control\check-staged-content.py
```

The check helps prevent accidental additions, but it does not replace reviewing
where a change came from. Adding source files requires updating
[tracked-files.json](../outputs/FM4-source-control/tracked-files.json) and reviewing
their contents. The existing local pre-commit hook is not installed automatically
when you clone the repository. No project-wide license is assigned by this
repository.

## Troubleshooting

- **Setup or build fails:** run `doctor` first. Check that `Python` points to a real
  Python executable and that `LLVMRoot` points to LLVM 20.x.
- **An executable is rejected** (`Unsupported executables in ...` or `Missing
  executable`): check the game version and that the extraction is complete. The
  three executables must all be those of one supported release, as extracted from
  its Play Disc. Renaming a file does not make another revision compatible.
- **Assets are not found:** `Disc1` must point to the complete extracted root, not
  to a folder that contains only the three XEX files.
- **Disc 2 package is missing** (`Missing Disc 2 content package`): check that the
  four packages are present under the `content` path shown in
  [Supported game version](#supported-game-version).
- **A content package is rejected** (`Content package is for title ...`, `... is not
  FM4 marketplace content`, `No FM4 content packages found`): `-Content` points to a
  package of another game, a title update, or a directory without FM4 packages. See
  [Downloadable content](#downloadable-content).
- **`Two content packages share the name`:** the same package file name appears in two
  places. Remove the duplicate copy or pass only one of the directories.
- **`Run setup first.` or `Run generate first.`:** the later stage needs the output
  of the earlier one in the same `BuildRoot`. Run `setup`, then `generate`, then
  `build`, or use `all`.
- **`BuildRoot must be a dedicated private directory`:** choose a directory outside
  the repository's `outputs`, `work\fm4-rex` and `work\runtime` folders.
- **Old layout rejected** (`old sdk-source/sdk.patch layout`): create a new
  `BuildRoot`.
- **Check the logs:** open the logs of the failed stage in `BuildRoot\logs`.
- **`Build artifact changed` at launch:** the artifacts no longer match
  `build-result.json`. Run `build` again. Do not mix runtime or GPU plugin files from
  different builds; `run` checks the artifact hashes.
- **`Launcher tests failed`:** the GUI launcher's port of the launch rules differs from
  `pipeline.py`. The log lists each difference (`logs\launcher-tests.log` and
  `launcher-build\tests\result.json`). Change `work/fm4-launcher/src/plan.cpp` to match.
- **The GUI launcher does not start on another computer:** install the Visual C++
  2015-2022 x64 redistributable. A missing `MSVCP140_ATOMIC_WAIT.dll` means an older
  one is installed.
- **PowerShell blocks the script** (`running scripts is disabled on this system`): allow
  scripts for the window with `Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass`, as
  in [Commands](#commands).
- **`The term 'C:\Python311\python.exe' is not recognized`:** the `Python` path of `$common`
  does not exist. `py -0p` prints the paths of the installed Pythons.
- **`'vswhere.exe' is not recognized as an internal or external command`** at the start of
  every action: printed by the Visual Studio developer environment that `Build.ps1` enters;
  it is harmless, the actions still find the tools (`doctor` says `Preflight passed`).
- **`Launcher tests failed`** or **`-TextureReplace is not a directory`** right after the
  launcher build in a new BuildRoot: a build of the repository before 25 October 2026 ran one
  launcher test against `BuildRoot\cache`, which exists only after the first run. Update the
  repository, or run the game once and then `build` again.
