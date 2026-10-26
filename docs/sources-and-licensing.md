# Sources and licensing

This repository is source-only. It records reviewed integration code, diagnostic tools, synthetic tests, and the project's runtime, a D3D12-only fork of the ReXGlue SDK. It does not contain the original game, translated game function bodies, extracted content, game shaders, or build output.

## Contents

### Included

- `work/fm4-rex/src`: application integration, runtime adapters, diagnostics, and test features (developer options fed to the title, a rebuilt `engageaionstart`, and the guest vblank rate). Hook addresses identify externally supplied functions; their bodies are absent.
- `work/fm4-rex/tests`: synthetic arithmetic vectors and SDK texture-format tests. `vector-vectors.h` is generated from a mathematical oracle, not from a game dump.
- CMake files and two local diagnostic helper scripts: `work/fm4-rex/observe-frame.py` and `work/fm4-rex/send-input.py`.
- `work/runtime`: the ReXGlue SDK fork (BSD-3-Clause, with Xenia-derived portions; notices kept). Third-party libraries are not stored. Setup fetches them from the pinned upstream SDK revision. The inventory category is `runtime-fork`. Files may be up to 512 KiB. The recompiler emitter, `work/runtime/src/codegen/function_graph.cpp`, is marked in the inventory so that the translated-function pattern check skips it. Its graphics layer may diverge from upstream as far as the native path needs, while the recompiler, core and kernel stay close to it ([HLE plan, decisions](analysis/d3d-hle.md#11-decisions)).
- `work/fm4-launcher`: the GUI launcher, original code that ports the launch rules of `pipeline.py`. It is built against the runtime's third-party SDL3, Dear ImGui and JSON header, which are not stored.
- `outputs/FM4-build`: the setup, generate, build, and run pipeline (`Build.ps1`, `pipeline.py`, and the loader), reviewed integration metadata (function bounds and switch recipes), the e2e harness and gates (`e2e/`), and `verification.json`.
- `docs/`: project documentation, including the research notes in `docs/analysis/`.
- The third-party notices in `outputs/FM4-source-control/third-party-notices`: the license text of every third-party component that the build compiles into a binary of the game, the runtime or the GUI launcher (the ReXGlue/Xenia notice, the libraries of [Third-party components](#third-party-components), AMD's FidelityFX SDK license, Microsoft's `d3dx12.h` license and the Microsoft.GameInput package's MIT license), copied from their pinned upstream revisions, and a `README.txt` that says what each covers and which binary holds it.
- `outputs/FM4-build/sdl3-gameinput-v3.patch`: the change to the fetched SDL 3 source for GameInput API v3, applied by setup (it holds context lines of SDL's code, zlib, and marks what it alters; [Microsoft.GameInput](#third-party-code-microsoftgameinput)).
- `work/runtime/cmake/rex_fsr.cmake`, `work/runtime/cmake/fsr-include`, `work/runtime/include/rex/graphics/d3d12/fsr.h`, `work/runtime/src/graphics/d3d12/fsr.cpp` and `work/fm4-rex/passes/fsr.hlsl`: the integration that builds AMD FSR 3.1 from the FidelityFX SDK source when `REX_FSR` is on and runs it at the scene_color point (original code of this project; it calls the SDK's API and holds none of its code). The SDK itself is not stored ([AMD FidelityFX SDK](#third-party-code-amd-fidelityfx-sdk-fsr-31)).

### Excluded

Kept on the development computer and never staged for this checkpoint:

- Original discs, ISO/XEX files, extracted assets, loaded images, and memory dumps.
- `generated/`, translated or disassembled game code, `crash-function.cpp`, original extracted tables, and game shader analysis.
- SDK and third-party checkouts, executable, DLL, and library files, and build or cache directories.
- Captures, screenshots, logs, traces, saves, user data, and existing archives.
- `XenonRecomp-FM4-fixes.patch`, pending a separate provenance and license review. The file is not in the working copy, and `.gitignore` does not allow it.

## Licensing and attribution

No blanket license is assigned to the project by this checkpoint. Upstream SDK-derived portions remain subject to the upstream license notices included in the repository. No rights to FM4 game content are provided.

The SDK and the shipped shader sources keep their upstream attribution and license terms:

- [ReXGlue/Xenia notice](../outputs/FM4-source-control/third-party-notices/ReXGlue-LICENSE.txt)
- [AMD FidelityFX SDK license](../outputs/FM4-source-control/third-party-notices/AMD-FidelityFX-SDK-LICENSE.txt) and [Microsoft d3dx12.h license](../outputs/FM4-source-control/third-party-notices/Microsoft-d3dx12-LICENSE.txt), for a runtime built with `REX_FSR`
- [SDL license](../outputs/FM4-source-control/third-party-notices/SDL-LICENSE.txt) (zlib), for the SDL 3 built into every runtime and altered by the GameInput patch
- [Microsoft.GameInput license](../outputs/FM4-source-control/third-party-notices/Microsoft-GameInput-LICENSE.txt) (MIT), for the GameInput header and loader built into SDL
- [What each notice covers and which binary holds it](../outputs/FM4-source-control/third-party-notices/README.txt), including the DirectX Shader Compiler that the FSR build runs and does not ship
- [Runtime license (ReXGlue/Xenia)](../work/runtime/LICENSE)
- [Xenia shader license](../work/runtime/src/graphics/shaders/fm4-resolve/LICENSE.xenia)
- [Origin and adaptation of the resolve shader source](../work/runtime/src/graphics/shaders/fm4-resolve/PROVENANCE.txt)

The repository holds integration code, tooling, synthetic tests, function location and bounds metadata, switch recipes, and the runtime (the ReXGlue SDK fork in `work/runtime`). It does not hold the original game instructions, the contents of the switch tables, game assets, game shaders, recompilation output, binaries, saves, or captures. Game instructions and table contents are read from the user's own data during `generate`.

Research notes:

- [Visual style of FM4](analysis/visual-style.md): whether the look comes from shader code in the game or from parameters in the track assets.
- [B4 shader research](analysis/b4-shader-research.md): the editable shader path (HLSL compiled through FXC, with overrides per hash).

## Projects this work builds on

Read on 26 October 2026 from each project's own README and license (the ReXGlue SDK's README in the BuildRoot's `runtime-upstream`, the others on GitHub). The thanks to them are in README's [Acknowledgements](../README.md#acknowledgements) and in [CREDITS.txt](../outputs/FM4-build/CREDITS.txt).

| Project | License | How this work builds on it |
|---|---|---|
| [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk), Tom (crack) and contributors | BSD-3-Clause | The recompiler and the runtime that `work/runtime` forks. |
| [Xenia](https://github.com/xenia-project/xenia), Ben Vanik and the Xenia project contributors | BSD-3-Clause | ReXGlue's runtime derives from its kernel, GPU and audio emulation; the resolve shaders in `work/runtime/src/graphics/shaders/fm4-resolve` adapt Xenia's. |
| [Xenia Canary](https://github.com/xenia-canary/xenia-canary) | not reviewed: no code of it is copied | The optional shadow workaround follows its game patches (game-patches#398), and `fm4-vector-fixes.h` matches its dot product arithmetic. |
| [XenonRecomp](https://github.com/hedge-dev/XenonRecomp), hedge-dev and contributors | MIT | Through ReXGlue: its README says much of ReXGlue's code analysis and instruction translation is based on XenonRecomp, and four files of the runtime fork are marked as based on XenonRecomp and Unleashed Recompiled (below). `fm4-instruction-fixes.h` ports float16 semantics that XenonRecomp's regression suite checks. |
| [rexdex's recompiler](https://github.com/rexdex/recompiler) | MIT | Through ReXGlue, which names it as an inspiration; no code of it. |
| [XenosRecomp](https://github.com/hedge-dev/XenosRecomp) and [Unleashed Recompiled](https://github.com/hedge-dev/UnleashedRecomp), hedge-dev | MIT and GPL-3.0 | Read for design ideas: the editable shader path ([B4 research](analysis/b4-shader-research.md)) and the native port ([D3D-level HLE, 5](analysis/d3d-hle.md#5-prior-art-unleashed-recompiled)). No code of them in this project's own sources. |

**XenonRecomp's code in the runtime fork.** The ReXGlue SDK marks `include/rex/ppc/context.h` ("Based on XenonRecomp/UnleashedRecomp PPCContext infrastructure"), `include/rex/ppc/function.h` (translation wrappers), `include/rex/ppc/intrinsics.h` (SIMD patterns) and `src/codegen/config.cpp` (the TOML config) as based on XenonRecomp and Unleashed Recompiled. The three headers compile into the runtime and the game's generated code, `config.cpp` into `rexglue.exe`. The files carry only ReXGlue's BSD-3-Clause notice, so XenonRecomp's MIT notice is kept in [XenonRecomp-LICENSE.txt](../outputs/FM4-source-control/third-party-notices/XenonRecomp-LICENSE.txt). Unleashed Recompiled is under the GPL 3.0; whether those files hold code from it rather than from XenonRecomp, whose PPC context it shares, is a question for the ReXGlue SDK ([open items](#open-items-before-a-public-release)).

## Third-party components

Reviewed on 25 October 2026 against the build's CMake files, the link map of `fm4_rex.exe` and the license files of the pinned revisions. No third-party library is stored in the repository: `setup` fetches the ReXGlue SDK at `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2` with the 13 submodules of `THIRDPARTY_SUBMODULES` and the 11 vendored folders of `THIRDPARTY_VENDORED` in [pipeline.py](../outputs/FM4-build/pipeline.py), and the FidelityFX SDK at its pinned commit. Every license text is in [third-party-notices](../outputs/FM4-source-control/third-party-notices/README.txt), which `export` copies into each export.

| Component | Version | License | In | Notice condition |
|---|---|---|---|---|
| ReXGlue SDK, with Xenia-derived portions | `f5337cd` (the fork in `work/runtime`) | BSD-3-Clause | every binary but the launcher | keep the notice in source and in the documentation of binaries |
| XenonRecomp (the PPC context, function wrappers and SIMD patterns that ReXGlue bases on it) | in the fork's `include/rex/ppc` and `src/codegen/config.cpp` | MIT | the runtime, the game's code, `rexglue.exe` | the notice in all copies, in `XenonRecomp-LICENSE.txt` |
| SDL 3, altered by `outputs/FM4-build/sdl3-gameinput-v3.patch` | `8bf3b72` (3.5.0 development) | zlib | `rexruntime.dll`, `MotorsportDX.exe` | none for binaries; keep the notice in source and mark the altered source, which the patch does |
| Microsoft.GameInput (the API v3 header and its loader, in SDL's GameInput driver) | 3.5.283 | MIT (those two files only) | `rexruntime.dll`, `MotorsportDX.exe`, inside SDL | the notice in all copies; see [below](#third-party-code-microsoftgameinput) |
| Dear ImGui | 1.92.5 (`6d910d5`) | MIT | `rexruntime.dll`, `MotorsportDX.exe` | the notice in all copies |
| {fmt} | 12.1.0 | MIT with an object code exception | `rexruntime.dll`, `fm4_rex.exe` | none for object code |
| spdlog | `79524dd` | MIT | `rexruntime.dll`, `fm4_rex.exe` | the notice in all copies |
| FFmpeg (libavcodec, libavutil; Xenia's fork) | `0604b46` | LGPL 2.1 or later (configured without GPL parts) | `rexruntime.dll`, static | license text, source offer, relinking; see [LGPL](#lgpl-ffmpeg-and-libmspack) |
| libmspack (`lzxd.c` only) | 1.11 | LGPL 2.1 | `rexruntime.dll`, static | as FFmpeg |
| toml++ | 3.4.0 | MIT | `rexruntime.dll`, `rexgpu-xenos.dll`, `rexglue.exe` | the notice in all copies |
| xxHash | 0.8.3 | BSD-2-Clause | `rexruntime.dll`, `rexgpu-xenos.dll` | the notice in the documentation of binaries |
| SIMD Everywhere | 0.8.2 | MIT | `rexruntime.dll` and the game's generated code (header code) | the notice in all copies |
| CLI11 | `bfffd37` | BSD-3-Clause style | `rexruntime.dll`, `rexglue.exe` | the notice in the documentation of binaries |
| inja | 3.5.0 | MIT | `rexglue.exe` (build time only) | the notice in all copies |
| JSON for Modern C++ (nlohmann/json, from inja) | 3.11.3 | MIT | `MotorsportDX.exe` (the launcher), `rexglue.exe` | the notice in all copies |
| UTF8-CPP | `63d64de` | Boost Software License 1.0 | `rexruntime.dll` | none for object code |
| o1heap | 3.0.0 | MIT | `rexruntime.dll` | the notice in all copies |
| aes_128, DES, TinySHA1, SHA-256, Rijndael, `pe_image.h`, disruptorplus, `renderdoc_app.h`, the DXC API headers, stb_image, DXBCChecksum (vendored by the SDK, mostly from Xenia) | the SDK's commit | MIT, ISC style, zlib, public domain, NCSA, RSA MD5 notice; two without a stated license (see [open items](#open-items-before-a-public-release)) | `rexruntime.dll`, `rexgpu-xenos.dll` | the notices, quoted in `Vendored-code-NOTICES.txt` |
| AMD FidelityFX CAS and FSR 1 (the presenter's shader bytecode, from Xenia) | in `work/runtime/src/ui/shaders/bytecode` | MIT | `rexruntime.dll` | the notice in all copies |
| ProggyTiny font (Tristan Grimmer, embedded by Xenia's overlay code) | - | MIT | `rexruntime.dll` | the notice in all copies |
| AMD FidelityFX SDK (FSR 3.1) and Microsoft `d3dx12.h` | v1.1.4 | MIT | `rexgpu-xenos.dll` with `-Fsr on` | the notice in all copies; see [below](#third-party-code-amd-fidelityfx-sdk-fsr-31) |
| GNU binutils PowerPC disassembler (`thirdparty/disasm`) | the SDK's commit | GPL 2.0 or later | `rexglue.exe` only, never exported | source and license text if `rexglue.exe` were given to others |
| DirectX Shader Compiler of the FidelityFX SDK | 1.8.2403 | Microsoft's DXC terms | build time only, with `-Fsr on` | not shipped |

Not built: tiny-AES-c (compiled as a static library that no binary links), Vulkan, SPIR-V, glslang, Tracy, Catch2 and the upstream SDK's own FidelityFX option (`REXGLUE_ENABLE_FIDELITYFX`, off). The GUI launcher uses the Segoe UI fonts of Windows and ships no font. The project's look mods and post passes (`work/fm4-rex/look`, `work/fm4-rex/passes`) are the project's own HLSL; they implement published methods that their comments name: GTAO (Jimenez et al., 2016), interleaved gradient noise (Jimenez, 2014), the ACES fit of Krzysztof Narkowicz (2015), Uchimura's GT curve and the Khronos PBR Neutral curve.

### LGPL: FFmpeg and libmspack

`rexruntime.dll` links FFmpeg's libavcodec and libavutil and libmspack's LZX decoder statically. A binary that is passed to someone else must carry the LGPL text and this notice, come with an offer of those libraries' source as built, and allow relinking with a changed library (the runtime's source and build scripts). The project meets this by publishing source only: every user fetches the libraries at their pinned revisions and builds the binaries. The export carries the notices, and it is not distributed in any case ([Distribution](#distribution)).

## Distribution

What Motorsport DX can be given to others as, a preview included ([preview plan](analysis/preview.md)):

- **The source repository, with the build instructions.** It holds the integration code, the runtime fork, the tooling, the documentation and the third-party notices, and none of the game. Each player builds the game on their own computer from their own discs ([setup](setup.md)): `generate` reads the game's code from their `default.xex`, and `build` compiles it there.
- **Not an export, a BuildRoot or any binary of it.** `fm4_rex.exe` and the two module DLLs are the game's code recompiled from the player's discs, so they carry the game, and the export's shader cache (`cache`) holds shaders translated from the game. An export is for the builder's own computers ([export](running.md#export-to-another-computer)). The runtime DLLs and the GUI launcher are not distributed on their own either: they are built in the same BuildRoot, and the LGPL terms above would apply to the runtime.
- **Not game data.** Discs, extracted files, Disc 2 and DLC packages, title updates, saves, `loaded-image.bin`, the generated source, texture dumps, captures and screenshots of game assets in bulk, and logs and traces with the game's data are not distributed and are never committed. A few screenshots in a bug report are the player's own call.

The repository keeps the game out in three ways: `.gitignore` denies every path that it does not list, [`tracked-files.json`](../outputs/FM4-source-control/tracked-files.json) lists every reviewed file, and [`check-staged-content.py`](../outputs/FM4-source-control/check-staged-content.py) blocks unlisted, binary and oversized files, translated function bodies and FidelityFX sources. A BuildRoot inside the repository (the default `work\portable-fm4`) is ignored, and `pipeline.py` refuses a BuildRoot inside `outputs`, `work\fm4-rex` or `work\runtime` and an export folder inside the repository or the BuildRoot.

### History check, 25 October 2026

Every commit of every ref of the development repository was scanned (352 commits on 32 local branches and the remote's 3 refs, 3,244 distinct file versions): no file has a NUL byte or a binary, image, audio, video, archive, executable, disc, save, log or trace extension, no path names a game file, a generated tree or a BuildRoot, and the only matches of the translated function pattern are the recompiler's templates and emitter (`work/runtime/resources/templates`, `work/runtime/src/codegen/function_graph.cpp`). The largest file ever committed is the runtime's `command_processor.cpp` (378,733 bytes). The shader bytecode headers in `work/runtime/src/*/shaders/bytecode` and `vulkan_spirv` are the runtime's own shaders from the upstream SDK and Xenia (with AMD's CAS and FSR 1), not the game's. The check covers only what Git holds; the repository has no Git LFS.

## Open items before a public release

- **No project license.** No license is assigned to the project's own code (`work/fm4-rex`, `work/fm4-launcher`, `outputs`, the docs); without one, others may read the source but have no right to reuse it. `work/runtime` stays BSD-3-Clause. The choice is the project owner's.
- **Two vendored files without a stated license.** `pe_image.h` (a Microsoft copyright line only) and disruptorplus (MIT upstream; no license file in the SDK's copy), both from Xenia and fetched from the upstream SDK, not stored here.
- **Notices not re-read from upstream.** The copyright years of AMD's FidelityFX CAS and FSR 1 licenses, and the DirectX Shader Compiler's `LICENSE.TXT`, which the SDK's copy of the DXC headers does not include; the review did not download them.
- **Unleashed Recompiled in the ReXGlue SDK.** Four files of the runtime fork are marked as based on XenonRecomp and Unleashed Recompiled (GPL 3.0), with only ReXGlue's BSD-3-Clause notice ([above](#projects-this-work-builds-on)). Ask the ReXGlue SDK which parts, if any, come from Unleashed Recompiled.
- **`XenonRecomp-FM4-fixes.patch`** stays out of the repository until its provenance is reviewed.
- **DLSS** would need its own review of NVIDIA's SDK license; not planned.

## Third-party code: AMD FidelityFX SDK (FSR 3.1)

The project owner approved the download on 25 October 2026 ([FSR plan, section 10](analysis/fsr-plan.md#10-the-download-for-stage-0-approved-on-25-october-2026)). No FidelityFX source or binary is in the repository: setup fetches the SDK into the BuildRoot, and the runtime builds the upscaler from that source when `REX_FSR` is on.

| Item | Value |
|---|---|
| Source | [FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK), tag `v1.1.4`, commit `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`; a sparse, blob-filtered checkout in `<BuildRoot>\fidelityfx-sdk`, fetched by every `setup` (`fetch_fidelityfx` in `pipeline.py`, which stops when the checked-out commit differs), with a mirror for when GitHub cannot be reached: a local Git bundle of the pinned commit or another Git URL ([setup](setup.md#the-fidelityfx-sdk-mirror)); the bundle stays outside the repository |
| License | MIT, "Copyright (C) 2024 Advanced Micro Devices, Inc.", read in the SDK's `LICENSE.txt` on 25 October 2026 (`sdk/LICENSE.txt` is the same file); [notice](../outputs/FM4-source-control/third-party-notices/AMD-FidelityFX-SDK-LICENSE.txt) |
| What is used | The FSR 3 upscaler component (`ffx_fsr3upscaler`, FSR 3.1.4) and the SDK's DX12 backend (`ffx_backend_dx12`), as static libraries linked into `rexgpu-xenos`, with the upscaler's shader permutations compiled into headers at build time. The backend's frame generation swap chain and every other component are left out |
| How it is built | From source with LLVM 20 and Ninja, through the SDK's own CMake files for the component and the backend (`work/runtime/cmake/rex_fsr.cmake`), behind `REX_FSR` (off by default; `Build.ps1 -Fsr on`). No prebuilt AMD DLL is used: not the SDK's `amd_fidelityfx_dx12.dll` (the FidelityFX API) and not its `PrebuiltSignedDLL` folder |
| Also compiled in | Microsoft's `d3dx12.h` from the SDK's DX12 backend, MIT; [notice](../outputs/FM4-source-control/third-party-notices/Microsoft-d3dx12-LICENSE.txt) |
| Build-time tools | The SDK's `FidelityFX_SC.exe` with Microsoft's DirectX Shader Compiler (`dxcompiler.dll`, `dxil.dll`) from `sdk/tools/binary_store`: run at build time only, not stored and not shipped ([notices README](../outputs/FM4-source-control/third-party-notices/README.txt)) |
| Exports | `Build.ps1 -Action export` copies `third-party-notices` into the export folder |
| Fit with the runtime's license | MIT and the runtime's BSD-3-Clause are both permissive; each needs only its notice kept with the code and the binaries, which the notices folder and the export do |

The review of 20 October 2026, when FSR was a candidate for an anti-aliasing or upscaling post pass beside the project's own TAA ([TAA, open items](analysis/taa.md#11-open-items-and-later)):

| Version | Form | License | On the development GPU (RTX 5070 Ti Laptop) |
|---|---|---|---|
| FSR 2.x ([FidelityFX-FSR2](https://github.com/GPUOpen-Effects/FidelityFX-FSR2)) | Source (HLSL and C++) | MIT, checked in its `LICENSE.txt`; the only condition is that the notice "Copyright (c) 2022-2023 Advanced Micro Devices, Inc." and the permission text stay with the code | Runs; not vendor specific |
| FSR 3.1 ([FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) 1.1.x) | Source, released with SDK 1.1 | MIT, read in the `LICENSE.txt` of v1.1.4 on 25 October 2026 | Runs; needs Shader Model 6.2 |
| FSR 4.0.2 (FidelityFX SDK 2.0) | Pre-built, signed DLLs only (`amd_fidelityfx_loader.dll` and `amd_fidelityfx_upscaler.dll`), which AMD requires; no source of the neural model | The redistribution terms of the DLLs are not stated on [AMD's SDK 2.0 page](https://gpuopen.com/learn/amd-fidelityfx-sdk-2-0/); press reports of an MIT license do not clearly cover the DLLs or the model | Does not run: FSR 4 needs a Radeon RX 9000 or newer, and the API falls back to FSR 3.1.5 elsewhere |

Conclusions:

- FSR 3.1 built from source is the one used (the table above). The repository holds only the integration and the notices; `check-staged-content.py` blocks any file that carries the SDK's source header outside the notices folder.
- FSR 4 is not planned: it would mean redistributing AMD binaries on undocumented terms, and it does not run on the development computer.
- DLSS (DLAA) needs its own review of NVIDIA's SDK license; not reviewed yet.
- The plan for adding FSR 3.1 is in [FSR plan](analysis/fsr-plan.md).

## Third-party code: Microsoft.GameInput

The project owner approved, on 25 October 2026, the download of exactly one package, Microsoft.GameInput 3.5.283 from nuget.org, for its MIT files only ("pakai yang MIT"), and then option A of the review: GameInput API v3 with a minimal SDL patch. Nothing of the package is in the repository.

| Item | Value |
|---|---|
| Source | [Microsoft.GameInput](https://www.nuget.org/packages/Microsoft.GameInput/3.5.283) 3.5.283, published 28 September 2026, authors Microsoft, 2,143,539 bytes, SHA-512 `59083faf7e0fc669f6caf0c39642b8a4cb9825faf9b17acc4b030d0b8ebe2a7219d512397831e98321b281b3a5757a963ac5dcd61ba57f60ff2890fb7fe77705` (NuGet's catalog, base64 `WQg/r34P...f+d3BQ==`); fetched by `setup` and `build` into `<BuildRoot>\gameinput-sdk\3.5.283` (`fetch_gameinput` in `pipeline.py`, which refuses any other hash), with `GAMEINPUT_MIRROR` as a fallback ([setup](setup.md#the-gameinput-package)) |
| Used | Two files: `native/include/GameInput.h` (GameInput API v3, extracted as `include/gameinput.h`) and `native/src/GameInput.cpp` (the static library's loader source), which both say "This file is licensed under the MIT License" with "Copyright (c) Microsoft Corporation"; the package's README for 3.5: "Updated public header and static library (lib) license to MIT". [Notice](../outputs/FM4-source-control/third-party-notices/Microsoft-GameInput-LICENSE.txt) |
| Not used | The package's `LICENSE.txt` is Microsoft's software license terms for the GameInput redistributable, and its `v0`, `v1` and `v2` headers carry only "All rights reserved" under those terms; they are not extracted. Neither are its static libraries, its `GameInputRedist.msi` or its signature. Nothing in the package is run |
| How it is built | The header comes before the Windows SDK's `gameinput.h` for SDL's sources only (`REX_GAMEINPUT_DIR` in `work/runtime/thirdparty/CMakeLists.txt`), and the runtime's SDL driver includes it for its availability line; the loader is compiled into SDL. At run time the loader loads the newest GameInput runtime DLL that Windows or the GameInput redistributable installed (`GameInput.dll` or `GameInputRedist.dll`); without a v3-capable one SDL's GameInput driver does not start and SDL's other drivers take the pads |
| SDL | SDL 3 (zlib, "Copyright (C) 1997-2026 Sam Lantinga") knows GameInput API v1 and v2 only; `outputs/FM4-build/sdl3-gameinput-v3.patch` adds v3 (the namespace, creation through the loader's `GameInputInitialize`, the controller counts from `controllerInfo` with a null check). `patch_thirdparty` in `pipeline.py` applies it to the fetched source and refuses a mismatch. [Notice](../outputs/FM4-source-control/third-party-notices/SDL-LICENSE.txt) |
| Exports | `Build.ps1 -Action export` copies `third-party-notices` into the export folder; no file of the package is copied |
| Pre-commit check | `check-staged-content.py` blocks a file with the text of the package's header or loader |

## Inventory and the pre-commit check

- The root `.gitignore` denies everything by default and re-includes only individually reviewed paths.
- [`tracked-files.json`](../outputs/FM4-source-control/tracked-files.json) records the inventory and the provenance category of each file.
- Before adding a new path, review its origin, update both the inventory and `.gitignore`, and then run the check.
- [`check-staged-content.py`](../outputs/FM4-source-control/check-staged-content.py) reads the staged Git index and blocks entries that are not in the inventory, binary or oversized files, and text that looks like a translated guest function (except the entries marked as emitter). Run it with Python before committing:

```powershell
& 'C:\Python311\python.exe' .\outputs\FM4-source-control\check-staged-content.py
```

The check is an accident guard, not proof of licensing provenance. Even a listed source file needs review when it is edited.

Hooks are not installed by cloning. When this file was last checked (25 October 2026), the working copy had no pre-commit hook installed, so run the check by hand before each commit.

## Build and reproduction notes

- The portable setup, generate, build, and run workflow is documented in [setup](setup.md). It builds `work/runtime` with third-party libraries from a pinned open-source SDK revision (`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`) and regenerates private game source from the user's extracted discs.
- The tracked compiler-template transform, [`guard-generated.py`](../outputs/FM4-build/guard-generated.py), has been reviewed separately. It contains generic emitter transforms, not original game function bodies. Reviewed numeric function locations, bounds, and switch recipes are included as integration metadata. The original switch contents are not included.
- The repository does not contain a runnable game or a complete backup of the original development directory. Rebuild by following the portable workflow.
- No remote or upload is configured by this workflow.
- Full gameplay compatibility remains incomplete.
