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
| Git | Git for Windows, with HTTPS access to GitHub (for the SDK, its pinned submodules and, with `-Fsr on`, the FidelityFX SDK). Setup uses partial and sparse clones; validated version: **2.55.0**. Needed only for `-Fetch git`, the default in a Git checkout; a [source snapshot](#source-snapshots) fetches GitHub's archives without Git ([fetching without Git](#fetching-without-git)) |
| Python | **Python 3.11+ x64**, for example from python.org. Use a real `python.exe`, not the Microsoft Store alias in `WindowsApps`. `py -0p` lists the installed versions with their paths: an install for all users is in `C:\Program Files\Python311`, one for the current user in `%LOCALAPPDATA%\Programs\Python\Python311`. Only the standard library is used |
| Visual Studio | Visual Studio 2022 or Build Tools, workload **Desktop development with C++**, MSVC x64 tools: the build uses its C and C++ libraries (headers and import libraries), not its compiler or linker |
| Windows SDK | Headers and libraries. Validated version: **10.0.26100.0**. Its `fxc.exe` is no longer used (since 31 October 2026): the resolve shaders compile through Windows' own `d3dcompiler_47.dll` |
| Build tools | CMake **3.25+** and Ninja. Both can be installed through the Visual Studio Installer, or fetched by the [`toolchain` action](#portable-tools) |
| LLVM | **LLVM 20.x x64**. Validated version: **20.1.8**. Installed, or fetched by the [`toolchain` action](#portable-tools) |

Disk space and time, measured on 25 October 2026 with a fresh clone and a new BuildRoot on
the development laptop (Intel Core Ultra 7 255HX, 20 threads, 32 GB, `-Jobs 8`): `all`
took about 16 minutes, of which the game's own compilation took 141 seconds, and the
BuildRoot held 2.6 GB after `build` (1.1 GB of it the upstream SDK clone with its
submodules). The first run then imports Disc 2 (2.8 GB) and the downloadable content
packages into the BuildRoot, about their own size. Setup downloads the SDK and its submodules
from GitHub once (the FidelityFX SDK only for a build with `-Fsr on`). The build directory can
be on another drive.
`-Jobs` can go up to the number of threads when the computer has the memory; the default
of 4 is slower.

The `doctor` action checks the prerequisites and reports what is missing: Windows
x64, 64-bit Python 3.11 or newer, `git` (with `-Fetch git` only), `cmake`, `ninja`, `clang`,
`clang++`, `lld-link` and `llvm-ar` on the path, LLVM 20.x (from `clang++ --version`), CMake 3.25
or newer, Windows' `d3dcompiler_47.dll`, and Microsoft's C and C++ libraries and the Windows SDK
in the folders of `INCLUDE` and `LIB` (`vcruntime.h`, `corecrt.h`, `windows.h` and `d3d12.h`;
`msvcrt.lib`, `ucrt.lib`, `kernel32.lib` and `d3d12.lib`). It asks for no tool of Visual Studio
(`link`, `fxc.exe`, `vswhere`): clang++ links with LLVM's `lld-link`. It also reports the BuildRoot's
FidelityFX SDK (fetched or not, at the pinned commit or not) and its mirror
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
| `preflight` | Checks whether this computer can build and run the game, before anything is fetched, and prints the result as JSON. Makes no changes. See [Preflight](#preflight). |
| `launcher-data` | Writes the GUI launcher's `launcher.json` into `-Out` (or the BuildRoot) without a build. See [Launcher data](#launcher-data). |
| `snapshot` | Writes a source snapshot of a commit (`-Revision`, default `HEAD`) with its version stamp into the new folder `-Out`. See [Source snapshots](#source-snapshots). |

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
| `-FfxMirror` | The FidelityFX SDK mirror for `setup`, `build` and `all`, in place of the `FFX_MIRROR` environment variable: another Git URL or a local `.bundle` file, or with `-Fetch archive` a local copy or another URL of the SDK's archive. See [the FidelityFX SDK mirror](#the-fidelityfx-sdk-mirror). |
| `-Fetch` | `git` or `archive`, for `setup`, `build` and `all`: how the third-party sources are fetched. `git` clones them (the default in a Git checkout); `archive` takes GitHub's archives of the pinned commits without Git (the default in a [source snapshot](#source-snapshots)). See [fetching without Git](#fetching-without-git). |
| `-Out` | The folder of `export`, outside the BuildRoot and the repository; of `launcher-data`, outside the source; or the new folder of `snapshot`, outside the repository. |
| `-Tests` | `developer` (the default) or `player`, for `build` and `all`: which tests run at the end of `build`. See [3. Build](#3-build). |
| `-Progress` | A file to which `setup`, `generate`, `build` and `all` append one JSON line for each stage and step, or `-` for the output. See [Progress lines](#progress-lines). |
| `-Revision` | The commit of `snapshot`. Default: `HEAD`. |

## Stages

### 1. Setup

`setup` builds the project's runtime, a D3D12-only fork of the ReXGlue SDK kept in
[work/runtime](../work/runtime). It:

- mirrors `work/runtime` to `BuildRoot\runtime-source`;
- takes the third-party libraries from `rexglue/rexglue-sdk` at commit
  `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`: the vendored directories and the 13
  submodules that the D3D12 build needs (with `-Fetch git` a clone in
  `BuildRoot\runtime-upstream`, with `-Fetch archive` [GitHub's archives](#fetching-without-git)).
  Vulkan, SPIR-V, Tracy and the tests are not fetched;
- with `-Fsr on` only (since 31 October 2026; before, every setup fetched it), takes the AMD
  FidelityFX SDK from `GPUOpen-LibrariesAndSDKs/FidelityFX-SDK`, tag
  `v1.1.4`, commit `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`: the files the FSR 3 upscaler and
  its DX12 backend build from (about 39 MB) in `BuildRoot\fidelityfx-sdk`, with `-Fetch git` as a
  sparse, blob-filtered Git clone (plus 36 MB of Git objects), with `-Fetch archive` from the
  commit's archive. Setup refuses any other commit, and takes it from the
  [mirror](#the-fidelityfx-sdk-mirror) when GitHub cannot be reached;
- takes the Microsoft.GameInput package, version 3.5.283, from NuGet (2.04 MB) into
  `BuildRoot\gameinput-sdk\3.5.283`, and extracts its two MIT files: the GameInput API v3
  header and its loader source ([the GameInput package](#the-gameinput-package));
- patches the fetched SDL source for GameInput API v3
  ([outputs/FM4-build/sdl3-gameinput-v3.patch](../outputs/FM4-build/sdl3-gameinput-v3.patch))
  in Python: every hunk must match exactly where it stands (no fuzz, no other offset), and each
  file keeps its own line endings (since 31 October 2026; before, `git apply` in a scratch copy,
  which gave every line of the three patched files a doubled carriage return, CR CR LF, on a
  checkout with Git for Windows' CRLF line endings);
- compiles the licensed Xenia/ReXGlue resolve shaders, the readback downscale shader and the
  presenter's area downscale shader to DXBC headers through `D3DCompile` in Windows' own
  `d3dcompiler_47.dll` (from Python by `ctypes`), with FXC's `/O3 /Gfp` and its strip options;
  the headers are byte for byte those of the Windows SDK's `fxc.exe` (33 headers compared on
  31 October 2026, with `System32`'s DLL 10.0.26100.9711 and the SDK's 10.0.26100.7705), so
  setup no longer needs `fxc.exe`;
- builds and installs the Release runtime locally.

#### Fetching without Git

With `-Fetch archive` (`--fetch archive`, the default in a [source snapshot](#source-snapshots)),
setup takes the third-party sources as GitHub's archives of the pinned commits
(`https://codeload.github.com/<owner>/<repository>/tar.gz/<commit>`), with no Git on the
computer: the upstream SDK's archive at `f5337cd`, of which only the 11 vendored folders are
unpacked, and one archive for each of the 13 submodules at the commit the SDK pins, plus SIMDe's
own submodule `test/munit` (`THIRDPARTY_ARCHIVES` in `pipeline.py`). The archives are kept in
`BuildRoot\downloads` and fetched only when missing, so a rerun needs no network. On 31 October
2026 the 15 archives held 95.7 MB (the SDK's 51.3 MB, SDL's 16.0 MB, FFmpeg's 13.8 MB) and
downloaded in 21 seconds.

GitHub does not promise the same archive bytes over time, so the pin of each archive is a hash
over its unpacked files, not over the archive: a SHA-256 over, in path order, each file's path
below its folder, a zero byte and the SHA-256 of its contents with every CRLF read as LF. Files
of Git (a submodule's `.git` file) and the pipeline's markers (`.fm4-*`) are not counted. Line
endings are read as LF because a Git for Windows checkout writes CRLF where the archive has LF;
the files keep the archive's own. A symbolic link in an archive (libmspack has 15, o1heap one) is
written as a file holding its target, as Git for Windows writes it without symbolic links. A
folder whose files differ from its pin stops setup, and its archive is renamed to `.rejected`,
so that the next run fetches it again. The archive must also name the pinned commit (its pax
comment, which `git archive` writes).

Unpacking takes only regular files and links: every member passes `tarfile`'s data filter
(nothing absolute, nothing outside the folder), and paths with `..` or `:` are refused. Nothing
from an archive is run but the build itself. A submodule's folder from an archive has no `.git`
file, so the runtime's `thirdparty/CMakeLists.txt` asks only that each one holds files.

The unpacked 24 folders (12,750 files) gave the same hash as the development BuildRoot's Git
checkout of the same commits under this rule on 31 October 2026. A pin that moves to another
commit gets its new hash from the first fetch's stop message, after its files have been compared
with a Git checkout of that commit.

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
  (about 15 MB), made from the checkout without network. When the file exists and the
  BuildRoot has no checkout yet, setup checks that the bundle holds the pinned commit and
  checks it out from there first, without network (since 31 October 2026; before, only when
  GitHub could not be reached). On the development computer it is
  `D:\FM4-archive\third-party\fidelityfx-sdk-v1.1.4.bundle`, written on 25 October 2026.
- **Another Git URL** that holds the tag `v1.1.4` at the pinned commit, used the same way as
  GitHub. The project owner may fork the SDK to their own GitHub account and name the fork
  here; check that the fork has the tag `v1.1.4` (fork every branch, not the main branch
  only, or push the tag to it). A mirror at another commit is refused like GitHub.
- **With `-Fetch archive`**, a local copy of the commit's archive (a `.tar.gz` file, used first,
  without network) or another URL of it (tried after GitHub). The archive holds the whole SDK;
  only the files of the sparse checkout above are unpacked, and they must give the pinned hash
  ([fetching without Git](#fetching-without-git)). A `.bundle` is ignored there. The SDK's
  archive was not downloaded from GitHub for this pin (its size is not known); the pin comes from
  a `git archive` of the pinned commit made from the bundle above, whose 493 unpacked files gave
  the same hash as the development BuildRoot's sparse checkout on 31 October 2026.

```powershell
$env:FFX_MIRROR = 'D:\FM4-archive\third-party\fidelityfx-sdk-v1.1.4.bundle'
.\outputs\FM4-build\Build.ps1 -Action setup @common
```

Without a mirror, setup stops when GitHub cannot be reached and the BuildRoot holds no
checkout yet. An existing checkout at the pinned commit needs no network.

#### The GameInput package

The pad driver choice `gameinput` ([running.md](running-controls.md#pad-drivers)) builds SDL's GameInput
driver against GameInput API v3. `setup` and `build` fetch the Microsoft.GameInput package,
version 3.5.283, from `https://api.nuget.org/v3-flatcontainer/microsoft.gameinput/3.5.283/microsoft.gameinput.3.5.283.nupkg`
into `BuildRoot\gameinput-sdk\3.5.283` (`fetch_gameinput` in `pipeline.py`) and refuse any file
whose SHA-512 is not the pinned one (`59083faf...fe77705`, NuGet's published hash). They extract
only the package's two MIT-licensed files, `include\gameinput.h` (the public header) and
`src\GameInput.cpp` (the loader); the header is used for SDL's sources only, and the loader is
built into SDL. `GAMEINPUT_MIRROR` (an environment variable) names another source: a local copy
of the `.nupkg` file, which is tried first, without network, or another URL of the same package,
tried after NuGet. Either is refused without the pinned hash. An existing package with the
pinned hash needs no network. Nothing of the package is committed or shipped, and nothing in it is run; the
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

`-Tests player` (`--tests player`), the build of the
[builder installer](analysis/builder-installer.md), leaves out the tests of the project's
HLSL, which is the same on every computer: the post pass tests on WARP, the shader-mod tests
(FXC compiles of the look mods) and the GT7 tone curve test on WARP, and the FSR checks on the
GPU of a runtime built with `-Fsr on`. It keeps the instruction tests, the launcher tests and the
CPU tests of C++ that the player's compiler builds into the game, the runtime and the launcher:
the scene camera math, the track lighting edits, the texture replacement hash and DDS files, the
draw scale fraction, the pads' shaping, the keyboard model, the native shader contract's
manifest parser and pass classes, and the racing wheel's force feedback, mapping and host. Each
of those runs in seconds and checks what this computer's compiler made. `developer`, the
default, runs every test. `build-result.json` records the set in `tests`.

### Rebuilding after edits

- After editing `work/runtime`, `work/fm4-rex` or `work/fm4-launcher`, `build` is enough.
  Before 26 October 2026 `build` did not copy `work/fm4-rex`: a game built that way after a
  title edit without `generate` lacks the edit (on 26 October 2026, main's build of `15f828c`
  ran photo mode without its hooks of entry 0 at s and lost the scene).
- Run `generate` again for a new disc release, new content packages, or a change of the
  generator's inputs (`outputs/FM4-build/*.toml`, `rexglue.cmake`).
- A `BuildRoot` created before the runtime fork (one that contains `sdk-source`) is
  rejected. Choose a new `BuildRoot`.

## Builder interface

The [builder installer](analysis/builder-installer.md) runs `pipeline.py` itself, without
`Build.ps1` and without a Git checkout. These actions and options serve it; developers can use
them too.

### Without Build.ps1

`pipeline.py` runs directly with Python 3.11+ x64 from a checkout or a
[source snapshot](#source-snapshots): `python outputs\FM4-build\pipeline.py <action>
--build-root <folder> ...`, with the options of [Parameters](#parameters) in lower case with
hyphens (`--disc1`, `--jobs`, `--tests`, `--progress`). The caller provides the environment:
CMake, Ninja and LLVM 20 on `PATH` (installed ones, or the [portable tools](#portable-tools)
with `--toolchain`), and Microsoft's C and C++ libraries and the Windows SDK in `INCLUDE` and
`LIB`, which Visual Studio's x64 developer environment sets (`Build.ps1` enters it). Nothing else
of Visual Studio is needed on `PATH`. `preflight`, `launcher-data`, `snapshot` and `toolchain`
need none of it: they run before any toolchain exists. In a snapshot no action needs Git: the fetches take GitHub's archives
([fetching without Git](#fetching-without-git)) and the SDL patch is applied in Python
(increment B2). `doctor` asks for `git` only with `--fetch git`.

### Preflight

`preflight` prints one JSON object and exits with 0, or with 1 when a check fails. It needs no
toolchain, writes nothing and fetches nothing. Schema 1:

```json
{
 "schema": 1,
 "ok": true,
 "build_root": "D:\\MotorsportDX\\build",
 "source": "D:\\MotorsportDX\\source",
 "suggested_jobs": 8,
 "checks": {
  "windows": {"state": "pass", "version": "10.0.26200", "machine": "AMD64", "name": "Windows 11"},
  "cpu": {"state": "pass", "level": "x86-64-v2", "method": "cpuid",
          "features": {"sse3": true, "ssse3": true, "cmpxchg16b": true, "sse4_1": true,
                       "sse4_2": true, "popcnt": true, "lahf_sahf": true},
          "missing": [], "unchecked": []},
  "gpu": {"state": "pass", "adapters": [{"name": "...", "vendor_id": "10DE",
          "dedicated_video_memory": 12524191744, "software": false, "d3d12_11_0": true}]},
  "memory": {"state": "pass", "total_bytes": 33731026944, "threads": 20, "suggested_jobs": 8},
  "disk": {"state": "pass", "path": "D:\\", "free_bytes": 160009805824, "required_bytes": 20000000000},
  "paths": {"state": "pass",
            "build_root": {"state": "pass", "path": "D:\\MotorsportDX\\build", "length": 21, "problems": []},
            "source": {"state": "pass", "path": "D:\\MotorsportDX\\source", "length": 22, "problems": []}}
 }
}
```

Each check has a `state`: `pass`; `warn`, which may work but is not tested; `fail`; or
`unchecked`, when the check could not run on this computer (its `detail` says why). An
`unchecked` check is never counted as a pass, and `ok` is false only when a check fails: the
caller decides what to do with `warn` and `unchecked`. A check may carry a `detail` sentence.

| Check | What it looks at | States |
| --- | --- | --- |
| `windows` | The Windows version (`sys.getwindowsversion`) and the machine Windows runs on (`IsWow64Process2`), and a 64-bit Python | `pass` on Windows 11 x64; `warn` on Windows 10 x64 (build 10240 or later), which is not tested; `fail` on any other version or machine (ARM64 included) |
| `cpu` | The x86-64-v2 features of the default build (`-march=x86-64-v2`): SSE3, SSSE3, CMPXCHG16B, SSE4.1, SSE4.2 and POPCNT from CPUID leaf 1, LAHF/SAHF in 64-bit mode from leaf `0x80000001`. `method` is `cpuid`: a 10-instruction routine in executable memory runs the CPUID instruction | `fail` when a feature is missing (`missing`). Where the routine cannot run, `method` is `IsProcessorFeaturePresent`, which has no flag for POPCNT or LAHF/SAHF and reports a flag that an older Windows does not know as absent: those features are `null` in `features`, listed in `unchecked`, and the state is `unchecked` |
| `gpu` | Every adapter DXGI lists, and whether `D3D12CreateDevice` accepts it at feature level 11_0. It is called without an output pointer, so no device is made | `pass` when a hardware adapter (not WARP) has Direct3D 12 at 11_0; `fail` otherwise; `unchecked` when `dxgi.dll` or `d3d12.dll` cannot be loaded |
| `memory` | The installed RAM (`GlobalMemoryStatusEx`) and the logical processors | `pass`; `warn` below 15 GiB (builds below 16 GB are not measured). `suggested_jobs` is one job per 4 GiB of RAM, rounded, at most one per logical processor (32 GB gives 8, the measured `-Jobs 8`; 24 GB gives 6). The memory per job is not measured, so the rule is provisional; `pipeline.py`'s own default stays 4 |
| `disk` | Free space on the drive of the BuildRoot (or of its nearest existing parent) | `fail` below 20 GB (`required_bytes`), a provisional floor for the BuildRoot, Disc 2's import, the downloadable content and the builder's toolchain |
| `paths` | The BuildRoot's and the source folder's paths | `fail`: longer than 70 characters for the BuildRoot (the deepest file measured below one is 187 characters, Disc 2's imported content) or 150 for the source; non-ASCII characters; inside a synced folder; or a BuildRoot that the [BuildRoot rule](#parameters) refuses. `warn`: a BuildRoot longer than 46 characters (the FSR build's objects hold its path twice and reach twice its length plus 166) or a source longer than 100; spaces (not tested). Synced folders are those of OneDrive's environment variables, the sync roots registered with Windows' Cloud Files API, and folders with the Cloud Files placeholder attributes; a sync tool that uses none of them is not detected |

### Progress lines

With `--progress <file>` (`-Progress`), `setup`, `generate`, `build` and `all` append one JSON
line to the file for each stage and step as it starts and as it ends, and flush it at once, so
a caller can read the file while the action runs. `--progress -` writes the lines to the output
instead, between the action's other output. Schema 1:

```json
{"schema": 1, "event": "stage", "stage": "setup", "step": null, "state": "running", "elapsed": 0.0, "log": null, "time": "2026-10-31T12:00:00Z"}
{"schema": 1, "event": "step", "stage": "setup", "step": "runtime-build", "state": "running", "elapsed": 0.0, "log": "D:\\MotorsportDX\\build\\logs\\runtime-build.log", "time": "2026-10-31T12:01:10Z"}
{"schema": 1, "event": "step", "stage": "setup", "step": "runtime-build", "state": "done", "elapsed": 95.3, "log": "D:\\MotorsportDX\\build\\logs\\runtime-build.log", "time": "2026-10-31T12:02:45Z"}
{"schema": 1, "event": "stage", "stage": "setup", "step": null, "state": "failed", "elapsed": 160.2, "log": null, "time": "2026-10-31T12:02:50Z", "error": "Exit 1; log: ..."}
```

| Field | Meaning |
| --- | --- |
| `schema` | 1 |
| `event` | `stage` (`setup`, `generate`, `build`) or `step` |
| `stage` | The stage the line belongs to, or `null` for a step outside a stage |
| `step` | For a step: the name of its log without `.log` (every process the pipeline starts with a log is a step: `runtime-configure`, `runtime-build`, `codegen`, `game-build`, `instruction-tests`, `patch-sdl3-gameinput-v3`, ...), or `gameinput-fetch`, `dxc-fetch`, `thirdparty-mirror`; with `--fetch archive` `thirdparty-download`, `thirdparty-unpack`, `fidelityfx-download` and `fidelityfx-unpack`, each with a log of its archives (size, source and seconds; files, hash and seconds); `null` for a stage. A name can come twice: the FidelityFX fetch's Git commands share `fidelityfx-fetch`, and `patch-sdl3-gameinput-v3` comes once for the check and once for the patch |
| `state` | `running`, then `done` or `failed` |
| `elapsed` | Seconds since the stage or step started, to a tenth; 0 on `running` |
| `log` | The step's log in `BuildRoot\logs`, or `null` |
| `time` | The time of the line, UTC |
| `error` | On `failed` only: the error message |

A failed step is followed by its stage's `failed` line, and the action then exits with an
error. Steps that find their work done (an unchanged fetch, an up-to-date build) still write
their two lines.

### Launcher data

`launcher-data` writes `launcher.json`, the tables of the [GUI launcher](running.md#gui-launcher)
(the cvars of a launch, the looks, the quality tiers, the displays, the pad, keyboard and wheel
knobs, the releases and the content rules), without a build: they are project data in
`pipeline.py` and the look and pass files. With `-Out` it writes into that folder, otherwise
into the BuildRoot. The file equals the one `build` writes (checked on 31 October 2026 against
the development BuildRoot's, built from `627e914`).

### Source snapshots

A source snapshot is the source as the builder package carries it: the files of one commit, as
`git archive` writes them, without Git metadata, and a version stamp. `snapshot -Out <new
folder> [-Revision <commit>]` writes one from a checkout (uncommitted changes are not in it).
The stamp is `source-stamp.json` at the snapshot's root:

```json
{
 "schema": 1,
 "commit": "508967345b7d3d38298c668838ddbe95329d108e",
 "date": "2026-10-31T13:53:40+07:00",
 "describe": "5089673",
 "files": 1257,
 "tree": "a276fde480a8dbfe95d013b09280ec960f5f00dce514ed0b7c76e3d2d3eab9b5"
}
```

`commit` is the full commit hash, `date` its committer date, `describe` the output of `git
describe --tags --always` (the tag once the project has one), `files` the number of files
besides the stamp, and `tree` a SHA-256 over the paths and contents of the files in
`work/runtime` and `work/fm4-rex` (each file's path relative to the snapshot, a zero byte and
its SHA-256, in path order).

In a snapshot (a source folder without `.git`), `build-result.json`'s `source` takes `head` from
the stamp's `commit`, and `dirty` is true when the files of `work/runtime` or `work/fm4-rex` no
longer give the stamp's `tree`; both are `null` without a stamp. The BuildRoot must lie outside
the snapshot altogether, so `--build-root` is required there (the default `work\portable-fm4`
would lie inside it).

### Portable tools

`pipeline.py toolchain --toolchain <folder>` gives a computer without the build tools their
portable copies, with no installer, no administrator rights and nothing on the system `PATH`.
[outputs/FM4-build/toolchain.json](../outputs/FM4-build/toolchain.json) pins each tool's
archive by its URL, size and SHA-256, with its licence, the versions taken from an installed
copy, and the members to keep:

| Tool | Archive | Bytes | Kept | Unpacked bytes | Licence |
| --- | --- | --- | --- | --- | --- |
| Python 3.11.9 | python.org's embeddable package `python-3.11.9-embed-amd64.zip` | 11,249,023 | all 34 files | 21,665,567 | PSF-2.0 (`LICENSE.txt`, with the notices of the libraries it bundles) |
| CMake 3.31.6 | Kitware's `cmake-3.31.6-windows-x86_64.zip` | 46,473,549 | `bin/cmake.exe`, `share/cmake-3.31/`, `doc/cmake/Copyright.txt` (3,767 files) | 20,927,837 | BSD-3-Clause |
| Ninja 1.12.1 | the Ninja project's `ninja-win.zip` | 275,425 | `ninja.exe` | 569,856 | Apache-2.0 |
| LLVM 20.1.8 | the LLVM release's `clang+llvm-20.1.8-x86_64-pc-windows-msvc.tar.xz` | 939,541,912 | `clang`, `clang++`, `lld-link`, `llvm-ar`, `llvm-rc`, clang's headers (`lib/clang/20/include`) and its builtins and profile libraries (301 files) | 304,219,141 | Apache-2.0 with LLVM exceptions |

The action first looks for installed copies (`detect_tools`): the tool on `PATH`, then LLVM's
default folder (`Program Files\LLVM\bin`), CMake's, and the CMake and Ninja that Visual Studio
bundles, and runs each one for its version. LLVM 20.x (with `clang++`, `lld-link` and `llvm-ar`
beside `clang`), CMake 3.25 or newer and Ninja 1.10 or newer are taken; Python is the
interpreter that runs the action. Only a tool without an accepted copy is fetched and unpacked
into `<folder>\<tool>-<version>`, or every tool with `--portable`. The archives are kept in
`--downloads` (default `<folder>\downloads`) and fetched only when missing, from the folder
`TOOLCHAIN_MIRROR` first; an archive with another size or SHA-256 is refused (a kept one is
renamed to `.rejected` and fetched again). `<folder>\toolchain-state.json` names each tool's
source (`installed` or `portable`), folder and version, and the action prints the same with every
installed copy it found, as JSON. With `--progress` its stage is `toolchain` and its steps
`toolchain-<tool>`.

Archives are untrusted data: they are unpacked only by the pipeline's own code (`zipfile`, and
`tarfile` with Python's `lzma` for `.tar.xz`), only members below the archive's top folder and in
the keep list, and only regular files: an absolute path, a drive, `..`, a member outside the top
folder and any link are refused, and every keep entry must match a member. A tool runs only from
a folder whose archive passed its size and SHA-256. The LLVM, CMake and Ninja executables link
the C runtime statically (they import only Windows' own DLLs), and the embeddable Python brings
its own `vcruntime140.dll`, so none of them needs the Visual C++ redistributable.

Any other action takes `--toolchain <folder>` to put the folders of its `toolchain-state.json`
first on `PATH`, for itself and the processes it starts. A build then needs only `INCLUDE` and
`LIB` from outside (Microsoft's libraries; increment B4 of the
[builder installer](analysis/builder-installer.md) fetches them too). For example, with
`INCLUDE` and `LIB` taken from Visual Studio's x64 environment and nothing else on `PATH` but
`C:\Windows\System32` and `C:\Windows`:

```powershell
I:\toolchain\python-3.11.9\python.exe <snapshot>\outputs\FM4-build\pipeline.py all `
    --toolchain I:\toolchain --build-root I:\FM4-build --disc1 <Disc 1> --disc2 <Disc 2> `
    --jobs 8 --tests player
```

The embeddable Python fixes `sys.path` in its `python311._pth` file, without the script's
folder; `pipeline.py` puts its own folder on `sys.path` itself, so the file stays as shipped. It
carries the modules the pipeline uses (`_lzma`, `_ssl` with the Windows certificate store,
`_ctypes`, `tomllib`), and the game's CMake takes it as its Python interpreter
(`Python3_EXECUTABLE`, for `guard-generated.py`).

### Local mirrors for developers

To build into a new BuildRoot without network, for example to check a snapshot, each fetch can
take a local copy:

| Variable | Local copy | Used |
| --- | --- | --- |
| `THIRDPARTY_MIRROR` | A checkout of the upstream SDK at the pinned commit with its 13 submodules, such as another BuildRoot's `runtime-upstream` | With `--fetch git` only: setup copies the third-party folders from it instead of cloning, and refuses it at another commit or with a submodule that is missing or differs from its pin. `--fetch archive` keeps its archives in `BuildRoot\downloads` instead; copying them into a new BuildRoot's `downloads` saves the network the same way |
| `FFX_MIRROR` | The FidelityFX SDK bundle, or with `--fetch archive` the SDK's archive | First, when the BuildRoot has no checkout ([the mirror](#the-fidelityfx-sdk-mirror)) |
| `GAMEINPUT_MIRROR` | The `.nupkg` file | First ([the GameInput package](#the-gameinput-package)) |
| `DXC_MIRROR` | The release zip | First ([the DirectX Shader Compiler](#the-directx-shader-compiler)) |
| `TOOLCHAIN_MIRROR` | A folder holding the archives of [`toolchain.json`](#portable-tools) under their release file names | First, by the `toolchain` action |

Every copy is checked against its pin as a download is. With `GIT_NO_LAZY_FETCH=1` Git also
refuses to fetch a missing object of the FidelityFX checkout instead of fetching it.

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
├── runtime-upstream/ # Upstream SDK clone (only for third-party libraries; -Fetch git)
├── downloads/        # GitHub's archives of the pinned commits (-Fetch archive)
├── fidelityfx-sdk/   # AMD FidelityFX SDK, sparse clone or the archive's files (only with -Fsr on)
├── gameinput-sdk/    # Microsoft.GameInput package and its two MIT files
├── runtime-build/    # Runtime build
├── runtime-install/  # Locally installed runtime
├── project/          # Integration source and private generated/
├── game-build/       # Executable, modules, runtime, GPU plugins
├── user-data/        # Profiles, saves, imported Disc 2 and DLC content
├── cache/            # Game and shader cache (shader-overrides/: compiled look mods)
├── logs/             # Logs for setup, generate, build and startup
├── launcher-build/   # GUI launcher build and its test cases (launcher-build-<folder> for a
│                     #   second checkout, such as a worktree, building into the same root)
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
