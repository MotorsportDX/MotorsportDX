# Sources and licensing

This repository is source-only. It records reviewed integration code, diagnostic tools, synthetic tests, and the project's runtime, a D3D12-only fork of the ReXGlue SDK. It does not contain the original game, translated game function bodies, extracted content, game shaders, or build output.

## Contents

### Included

- `work/fm4-rex/src`: application integration, runtime adapters, diagnostics, and test features (developer options fed to the title, a rebuilt `engageaionstart`, and the guest vblank rate). Hook addresses identify externally supplied functions; their bodies are absent.
- `work/fm4-rex/tests`: synthetic arithmetic vectors and SDK texture-format tests. `vector-vectors.h` is generated from a mathematical oracle, not from a game dump.
- CMake files and two local diagnostic helper scripts: `work/fm4-rex/observe-frame.py` and `work/fm4-rex/send-input.py`.
- `work/runtime`: the ReXGlue SDK fork (BSD-3-Clause, with Xenia-derived portions; notices kept). Third-party libraries are not stored. Setup fetches them from the pinned upstream SDK revision. The inventory category is `runtime-fork`. Files may be up to 512 KiB. The recompiler emitter, `work/runtime/src/codegen/function_graph.cpp`, is marked in the inventory so that the translated-function pattern check skips it.
- `work/fm4-launcher`: the GUI launcher, original code that ports the launch rules of `pipeline.py`. It is built against the runtime's third-party SDL3, Dear ImGui and JSON header, which are not stored.
- `outputs/FM4-build`: the setup, generate, build, and run pipeline (`Build.ps1`, `pipeline.py`, and the loader), reviewed integration metadata (function bounds and switch recipes), the e2e harness and gates (`e2e/`), and `verification.json`.
- `docs/`: project documentation, including the research notes in `docs/analysis/`.
- The ReXGlue/Xenia copyright and BSD license notice in `outputs/FM4-source-control/third-party-notices`.

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
- [Runtime license (ReXGlue/Xenia)](../work/runtime/LICENSE)
- [Xenia shader license](../work/runtime/src/graphics/shaders/fm4-resolve/LICENSE.xenia)
- [Origin and adaptation of the resolve shader source](../work/runtime/src/graphics/shaders/fm4-resolve/PROVENANCE.txt)

The repository holds integration code, tooling, synthetic tests, function location and bounds metadata, switch recipes, and the runtime (the ReXGlue SDK fork in `work/runtime`). It does not hold the original game instructions, the contents of the switch tables, game assets, game shaders, recompilation output, binaries, saves, or captures. Game instructions and table contents are read from the user's own data during `generate`.

Research notes:

- [Visual style of FM4](analysis/visual-style.md): whether the look comes from shader code in the game or from parameters in the track assets.
- [B4 shader research](analysis/b4-shader-research.md): the editable shader path (HLSL compiled through FXC, with overrides per hash).

## Candidate third-party code: AMD FSR

No FSR code or binary is in the repository. Reviewed on 20 October 2026 as a candidate for an anti-aliasing or upscaling post pass beside the project's own TAA ([TAA, open items](analysis/taa.md#11-open-items-and-later)):

| Version | Form | License | On the development GPU (RTX 5070 Ti Laptop) |
|---|---|---|---|
| FSR 2.x ([FidelityFX-FSR2](https://github.com/GPUOpen-Effects/FidelityFX-FSR2)) | Source (HLSL and C++) | MIT, checked in its `LICENSE.txt`; the only condition is that the notice "Copyright (c) 2022-2023 Advanced Micro Devices, Inc." and the permission text stay with the code | Runs; not vendor specific |
| FSR 3.1 ([FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) 1.1.x) | Source, released with SDK 1.1 | MIT according to the SDK's README; the `LICENSE.txt` of the SDK itself was not read yet | Runs; needs Shader Model 6.2 |
| FSR 4.0.2 (FidelityFX SDK 2.0) | Pre-built, signed DLLs only (`amd_fidelityfx_loader.dll` and `amd_fidelityfx_upscaler.dll`), which AMD requires; no source of the neural model | The redistribution terms of the DLLs are not stated on [AMD's SDK 2.0 page](https://gpuopen.com/learn/amd-fidelityfx-sdk-2-0/); press reports of an MIT license do not clearly cover the DLLs or the model | Does not run: FSR 4 needs a Radeon RX 9000 or newer, and the API falls back to FSR 3.1.5 elsewhere |

Conclusions:

- FSR 3.1 built from source is the usable candidate (for example its native AA mode on the single tile). Before any of it enters the repository: read the SDK's `LICENSE.txt`, add the AMD notice under `outputs/FM4-source-control/third-party-notices`, whitelist the files in `.gitignore` with review entries in `tracked-files.json`, and check that MIT code fits beside the runtime's license.
- FSR 4 is not planned: it would mean redistributing AMD binaries on undocumented terms, and it does not run on the development computer.
- DLSS (DLAA) needs its own review of NVIDIA's SDK license; not reviewed yet.
- The plan for adding FSR 3.1 is in [FSR plan](analysis/fsr-plan.md).

## Inventory and the pre-commit check

- The root `.gitignore` denies everything by default and re-includes only individually reviewed paths.
- [`tracked-files.json`](../outputs/FM4-source-control/tracked-files.json) records the inventory and the provenance category of each file.
- Before adding a new path, review its origin, update both the inventory and `.gitignore`, and then run the check.
- [`check-staged-content.py`](../outputs/FM4-source-control/check-staged-content.py) reads the staged Git index and blocks entries that are not in the inventory, binary or oversized files, and text that looks like a translated guest function (except the entries marked as emitter). Run it with Python before committing:

```powershell
& 'C:\Python311\python.exe' .\outputs\FM4-source-control\check-staged-content.py
```

The check is an accident guard, not proof of licensing provenance. Even a listed source file needs review when it is edited.

Hooks are not installed by cloning. When this file was last checked (17 October 2026), the working copy had no pre-commit hook installed, so run the check by hand before each commit.

## Build and reproduction notes

- The portable setup, generate, build, and run workflow is documented in [setup](setup.md). It builds `work/runtime` with third-party libraries from a pinned open-source SDK revision (`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`) and regenerates private game source from the user's extracted discs.
- The tracked compiler-template transform, [`guard-generated.py`](../outputs/FM4-build/guard-generated.py), has been reviewed separately. It contains generic emitter transforms, not original game function bodies. Reviewed numeric function locations, bounds, and switch recipes are included as integration metadata. The original switch contents are not included.
- The repository does not contain a runnable game or a complete backup of the original development directory. Rebuild by following the portable workflow.
- No remote or upload is configured by this workflow.
- Full gameplay compatibility remains incomplete.
