# Preview plan (25 October 2026)

What a preview of Motorsport DX can be, what it must not contain, and what is done, not done or
blocking for a closed preview (a few testers who own the game) and for a public one. The
checklist comes from an audit on 25 October 2026: a clean setup from a fresh clone, a review of
every third-party license, a scan of the Git history, and the status documents. Whether and
when to publish is the project owner's decision.

## 1. The form of a preview

- **Source only.** The preview is this repository at a named commit, with the build
  instructions of [setup](../setup.md). Each tester builds the game on their own Windows
  computer from their own extracted discs: `generate` reads the game's code from their
  `default.xex`, `build` compiles it, and the run reads the discs in place.
- **No binaries.** The game executable and its two module DLLs are the game's code recompiled
  from the tester's discs. An export, a BuildRoot, the shader cache and the runtime and launcher
  binaries are not distributed by the project or by testers
  ([distribution](../sources-and-licensing.md#distribution)).
- **No game data.** Discs, extracted files, Disc 2 and downloadable content packages, title
  updates and saves stay with their owner.
- **A named commit.** Testers build one commit (a tag such as `preview-1` would do), so every
  report names the same source; [bug reports](../bug-reports.md) asks for it.
- **For testers:** [known issues](../known-issues.md) and [bug reports](../bug-reports.md).

## 2. Checklist

Done: finished and checked. Not done: open, but the preview can start without it. Blocker:
the preview cannot start without it.

### Source and build

| Item | Closed preview | Public preview |
| --- | --- | --- |
| A fresh clone follows README and [setup](../setup.md) to a built game on the development computer that runs a Quick Race (25 October 2026, [current status](../status/current.md#clean-setup-from-a-fresh-clone-25-october-2026)) | Done | Done |
| The bugs and gaps that clean setup found are fixed: the launcher test that needed `BuildRoot\cache`, the execution policy, the Python path, the `vswhere` message, disk space and time, and `warmcache`, which needs a profile played once and the English OCR language | Done | Done |
| `warmcache` and the e2e timelines on a new profile: the game's intro video and first-run screens are not driven, so a tester plays once first | Not done (documented) | Not done (documented) |
| Testers can clone the source: GitHub's `main` is about 184 commits behind local `main` (25 October 2026), and nothing is pushed without the owner | **Blocker** (push to a private repository with the testers invited, or another channel the owner chooses) | **Blocker** (push to a public repository) |
| A named preview commit or tag | Not done (recommended) | **Blocker** |
| A clean setup on a second computer (another user's Windows, other paths and versions) | Not done: the first testers are that test | **Blocker** |
| A step-by-step install of the toolchain (Visual Studio 2022 with the C++ workload and the Windows SDK, LLVM 20.1.8, Python 3.11, Git); `doctor` checks them but does not install them | Not done (the testers can be helped one by one) | Not done (strongly recommended) |
| CI that builds the repository | Not done | Not done (it cannot run without game data; a build of the runtime and the launcher only would catch most breakage) |

### Licensing and content

| Item | Closed preview | Public preview |
| --- | --- | --- |
| Every third-party component that the build compiles or an export ships is credited, with its license text in the notices, in [sources and licensing](../sources-and-licensing.md#third-party-components) and in README | Done | Done |
| The export carries the notices (`export` copies `third-party-notices`) | Done | Done |
| No game file, executable, capture or log in any commit of any ref (scan of 25 October 2026) | Done | Done |
| `.gitignore`, `tracked-files.json` and `check-staged-content.py` keep the game out | Done | Done |
| A license for the project's own code | Not done (testers build for themselves) | **Owner's decision**: without one the public may read the source but not reuse it |
| `pe_image.h` and disruptorplus without a stated license in the SDK's copy; the AMD CAS and FSR 1 years and the DirectX Shader Compiler's `LICENSE.TXT` not re-read from upstream ([open items](../sources-and-licensing.md#open-items-before-a-public-release)) | Not done (low risk: fetched from upstream, not stored) | Not done (confirm before a public release) |
| A disclaimer that the project is not affiliated with Microsoft or Turn 10 (README) | Done | Done |
| GameInput (the `gameinput` pad driver): only the package's two MIT files are used, reviewed with the change ([Microsoft.GameInput](../sources-and-licensing.md#third-party-code-microsoftgameinput)), and the SDL patch is marked | Done | Done |

### For testers

| Item | Closed preview | Public preview |
| --- | --- | --- |
| [Known issues](../known-issues.md): open gaps in plain terms, tested hardware | Done | Done |
| [Bug reports](../bug-reports.md): what to write, `play.log`, `runtime.log`, perf traces, how to collect them, privacy | Done | Done |
| The status documents agree with each other (README, [running](../running.md), [current status](../status/current.md)) | Done (25 October 2026) | Done |
| A place to report (issues on the repository, or a channel the owner names) | Not done (owner) | **Blocker** |
| Crash dumps for players: only the e2e harness writes them; players can copy the Event Viewer entry | Not done | Not done (recommended) |

### Stability and coverage

| Item | Closed preview | Public preview |
| --- | --- | --- |
| Quick Race, photo mode, career to the rewards, one DLC car on a DLC track: e2e on every change | Done | Done |
| The owner's tests in progress: keyboard on the laptop, FSR 3.1 A/B, an Xbox Series controller | Not done (not blocking) | Not done (finish first) |
| The front-end menus and photo mode at the screen's resolution on the native path ("entry 0 at s") | Not done (cosmetic) | Not done (cosmetic) |
| A two-hour soak test (memory, handles, video memory) | Not done | **Blocker** (a public preview should not leak or crash in a long session) |
| Save compatibility between builds | Not done (documented in known issues) | Not done (documented) |
| Coverage only more testers can close: audio (music, engine, 5.1, speech and video sync), split screen, Rivals, Hot Lap, a full career season, more cars, tracks and DLC packs, and more GPUs (other NVIDIA cards, AMD and Intel discrete GPUs, desktops, Windows 10) | Not done: the purpose of the closed preview | Not done: at least one AMD and one Intel GPU, and audio, should be reported by the closed preview first |

## 3. Order

1. The owner chooses the channel and pushes `main` (or a preview branch) where the testers can
   clone it, and names the commit.
2. A closed preview with a few testers who own the game, on their own hardware: their first
   task is the clean setup, then the coverage gaps above, reported as [bug reports](../bug-reports.md) asks.
3. Before a public preview: the soak test, the owner's open tests, the license of the project's
   own code, the confirmation of the open license items, and a reporting place.
