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
- The third-party notices in `outputs/FM4-source-control/third-party-notices`: the ReXGlue/Xenia copyright and BSD license notice, AMD's FidelityFX SDK license, Microsoft's `d3dx12.h` license, and a `README.txt` that says what each covers.
- `work/runtime/cmake/rex_fsr.cmake`, `work/runtime/cmake/fsr-include` and `work/runtime/src/graphics/d3d12/fsr.cpp`: the integration that builds AMD FSR 3.1 from the FidelityFX SDK source when `REX_FSR` is on. The SDK itself is not stored ([AMD FidelityFX SDK](#third-party-code-amd-fidelityfx-sdk-fsr-31)).

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
- [What each notice covers](../outputs/FM4-source-control/third-party-notices/README.txt), including the DirectX Shader Compiler that the FSR build runs and does not ship
- [Runtime license (ReXGlue/Xenia)](../work/runtime/LICENSE)
- [Xenia shader license](../work/runtime/src/graphics/shaders/fm4-resolve/LICENSE.xenia)
- [Origin and adaptation of the resolve shader source](../work/runtime/src/graphics/shaders/fm4-resolve/PROVENANCE.txt)

The repository holds integration code, tooling, synthetic tests, function location and bounds metadata, switch recipes, and the runtime (the ReXGlue SDK fork in `work/runtime`). It does not hold the original game instructions, the contents of the switch tables, game assets, game shaders, recompilation output, binaries, saves, or captures. Game instructions and table contents are read from the user's own data during `generate`.

Research notes:

- [Visual style of FM4](analysis/visual-style.md): whether the look comes from shader code in the game or from parameters in the track assets.
- [B4 shader research](analysis/b4-shader-research.md): the editable shader path (HLSL compiled through FXC, with overrides per hash).

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
