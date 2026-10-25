# FSR: plan

A plan to add AMD FSR 3.1 to the runtime's post pass framework, first in its native anti-aliasing mode (no upscaling) beside the project's own [TAA](taa.md), and then as an upscaler on N5's scales. Stage 0 is done: the runtime builds and links the upscaler from the SDK source behind `REX_FSR` (section 3.1); nothing of it runs yet. Written on 20 October 2026 and updated against N5 on 25 October 2026 (sections 9 and 10); the license review is in [sources and licensing](../sources-and-licensing.md#third-party-code-amd-fidelityfx-sdk-fsr-31).

## Summary

- **Version**: FSR 3.1 (the FidelityFX SDK 1.1.x upscaler), built from its MIT source. FSR 4 is out: it ships as signed AMD DLLs only, on undocumented redistribution terms, and runs on Radeon RX 9000 GPUs only. FSR 2.2 is the fallback if the 3.1 build does not fit (also MIT, with an older and simpler code base).
- **Mode**: native AA first (render size = output size, ratio 1.0), as an alternative to TAA, chosen with a look knob. TAA stays the default on every GPU until an A/B decides otherwise.
- **Point**: the scene_color point ([TAA, section 2.2](taa.md#22-the-scene_color-point-option)), where the color, depth, motion vectors and masks share the scene layout, so nothing has to be rotated, and the output goes back into the game's scene through the existing write-back.
- **Needs**: the single tile without MSAA (the default with TAA since 20 October 2026), the jitter on every frame of a scene (FSR accumulates still pictures itself), a way to run FSR's D3D12 backend inside the runtime's deferred command list, DXC for its Shader Model 6.2 shaders, and the license steps below.
- **Not in the first stage**: upscaling. It builds on N5's scales: the native path renders the scene's private surfaces at a rational scale and every other surface at the integer scale, chosen per resolve destination, at the screen's logical size; upscaling splits the scene's scale into an input scale for the scene and an output scale for the post chain, the HUD and the swap (section 9).
- **The plan against N5** (25 October 2026) is section 9; the download stage 0 needs is section 10, approved by the project owner on 25 October 2026.

## 1. Why, and what it can gain

- Our TAA is a first version: a 3x3 Gaussian reconstruction, a variance clip, a fixed blend and a simple sharpening ([TAA, section 7](taa.md#7-the-resolve)). FSR 3.1's upscaler adds locks for thin features, a luma-stability history, a reactive and a transparency mask, and RCAS sharpening. Where it should help most: wires, fences and lattices in motion on the single tile, and still pictures.
- GPU cost: FSR 3.1's native AA at 2560x1440 is expected to cost about 0.5 to 1 ms on this GPU, against 0.15 to 0.21 ms for our TAA at the final point and 0.47 ms at the scene_color point; to be measured (section 6). At 1x and 240 Hz the command processor sets the frame rate, so the cost shows at 2x and above only.
- It runs on every vendor (NVIDIA, AMD, Intel) with Shader Model 6.2, unlike DLAA.

## 2. Licensing and sources (stage 0)

1. Read the `LICENSE.txt` of the FidelityFX SDK at the tag to be used (1.1.4 or the last 1.1.x). Done on 25 October 2026: MIT (section 10).
2. Do not vendor the SDK into the repository: fetch it during `setup` at a pinned tag and commit hash, as the runtime's upstream SDK clone is pinned, and verify the hash. Only integration code of this project goes into Git.
3. Add AMD's notice under `outputs/FM4-source-control/third-party-notices`, whitelist new files in `.gitignore`, add review entries to `tracked-files.json`, and run `check-staged-content.py`.
4. Check that MIT code linked into the runtime fits the runtime's license ([work/runtime/LICENSE](../../work/runtime/LICENSE)); MIT is permissive and normally does.
5. The SDK's prebuilt `amd_fidelityfx_dx12.dll` (the FidelityFX API) is not used: build the effect from source into a static library.

## 3. Build (stage 0)

- The FSR 3.1 upscaler is C++ host code (`ffx_fsr3upscaler`) plus HLSL shaders compiled into permutation headers, and a D3D12 backend (`ffx_backend_dx12`).
- The shaders need DXC (Shader Model 6.2, 16-bit types where supported); the runtime compiles its own shaders with FXC. The SDK's `sdk/tools/binary_store` carries its shader compiler with a DXC (section 10), so no separate DXC is fetched; `doctor` checks it.
- Build the two libraries with the project's toolchain (LLVM 20, CMake, Ninja) into BuildRoot and link them into `rexgpu`. A build switch keeps the runtime buildable without them (`REX_FSR=OFF` by default until stage 2 passes).
- Risk: the SDK's CMake expects MSVC and its own shader compiler wrapper; adapting it to clang-cl may take a patch, applied at setup like the runtime's own. Stage 0 needed no patch (3.1).

### 3.1 Stage 0's build (25 October 2026)

- `work/runtime/cmake/rex_fsr.cmake`, included by the graphics plugin's CMake file, runs when `REX_FSR` is on (`Build.ps1 -Fsr on`, kept in the runtime build folder for later builds; off by default). It sets the variables the SDK's top `CMakeLists.txt` sets for `DX12_X64` with only the FSR 3 upscaler and static libraries, and adds the SDK's own CMake files for the component (`ffx_fsr3upscaler_x64`) and the DX12 backend (`ffx_backend_dx12_x64`). The SDK's top file is not used: it adds every component and the PIX library, which the sparse checkout does not hold.
- The adaptations, all in that file, none in the SDK: the two libraries are compiled as C++17 (in C++20 and later `ffx_util.h` calls `std::popcount` without including `<bit>`) with the SDK's `UNICODE` defines and without warnings; an empty `pix/pix3.h` stands in for the PIX header (the backend loads the PIX functions it uses from `WinPixEventRuntime.dll` at run time); the backend's frame generation swap chain is taken out of its sources (it needs the AntiLag 2 header and the upscaler does not use it); and an empty file stands in for a dependency the SDK's shader function adds by mistake (the name of its output variable), which Visual Studio ignores and Ninja does not.
- The shaders: the backend's CMake runs `FidelityFX_SC.exe` (with the DXC of `sdk/tools/binary_store`) on the upscaler's 10 passes, each as wave32 and wave64, with and without 16-bit types, over the 64 permutations of its options: 240 headers in the build folder (40 permutation tables and 200 distinct shaders, 13.2 MB), embedded into the backend.
- The runtime: `rexgpu-xenos` links the two libraries, and `d3d12/fsr.cpp` logs the linked version at the command processor's setup (`FSR 3.1.4: linked`, with the backend's scratch memory for one context). No context is created and nothing is dispatched. The DLL grows from 6,716,928 to 9,148,416 bytes.
- Gate (25 October 2026): the runtime builds with `REX_FSR` on and off in separate build folders from the same source, under clang++ 20.1.8 and Ninja, and the shader permutations compile; see section 10 for the fetch.
## 4. Runtime integration (stage 1)

### 4.1 Running FSR inside the deferred command list

The runtime records its commands into a `DeferredCommandList` and replays them later on the submit thread; FSR's D3D12 backend dispatches into a real `ID3D12GraphicsCommandList` and binds its own root signatures, pipelines and descriptor heap. Options:

- **(a) A callback command in the deferred list** (recommended): `D3DCallback(fn)` stores a function that the replay calls with the real command list at that position. FSR's dispatch runs there, in order with the post passes around it. After it, the replay marks the root signature, pipeline and descriptor heaps as unknown, so the next post pass or draw binds its own again (the post passes already set their root signature per pass; the command processor's descriptor heap binding needs an invalidation hook).
- (b) Close the submission, run FSR on its own command list on the same queue, open a new submission. Simpler, but it adds a submission boundary and a flush of the submit thread in every frame, which the command processor cannot afford at 240 Hz.
- (c) A custom FFX backend that records into the deferred list. The cleanest separation, and the most code.

### 4.2 Inputs at the scene_color point

All in the scene layout (rotated 180 degrees against the screen, consistently, so FSR needs no rotation); [motion vectors, section 6](motion-vectors.md#inputs-the-upscalers-need) lists them:

| FSR input | Source in the framework | Conversion |
|---|---|---|
| Color | `scene_color` (the 7e3 scene resolved as `2_10_10_10`, gamma 2) | A pass squares it into an `R16G16B16A16_FLOAT` linear texture (HDR flag on) |
| Depth | the resolved scene depth (Z inverted through the viewport) | as is, with the inverted depth flag |
| Motion vectors | `motion_vectors` (host pixels, previous minus current, jitter-free) | as is, with the matching scale and sign |
| Reactive mask | `motion_mask` G (car glass) | as is; particles and the interior mirror are not in it ([TAA, section 6](taa.md#6-reactive-mask)) |
| Transparency and composition mask | `motion_mask` R (no motion known: the mirror panel, interior masks) | as is |
| Exposure | FSR's auto exposure, or 1.0 | to be chosen by A/B |
| Jitter | the framework's Halton (2, 3), 8 phases | FSR's own sequence at ratio 1.0 is the same length; pass the offset in the scene layout (negated against the screen) |
| Camera | the scene camera's near plane, far W and field of view | from the camera unpack |
| Reset | a camera cut or no history (`xe_history_valid()`) | the reset flag |
| Frame time | the swap interval | milliseconds |

### 4.3 Output

- FSR writes an `R16G16B16A16_FLOAT` texture; a pass encodes it back to gamma 2 into `taa_scene_out`'s place, and the existing write-back hands it to the game's motion blur, bloom, exposure and final combine.
- Sharpening: FSR's RCAS with the look's `sharpen` mapped to its sharpness; the final combine's sharpening is then 0.
- GTAO is applied in the final combine after FSR, so `gtao_temporal` stays on, as with TAA at this point ([TAA, section 8](taa.md#8-the-occlusion-at-the-scene_color-point)).

### 4.4 The jitter

FSR accumulates still pictures itself and expects the jitter in every frame. With FSR on, the post pass manifest arms the jitter on every frame of a scene (`TAA_JITTER_MOTION=0`, `TAA_JITTER_RATE=0`), not the gate that TAA uses ([TAA, section 3.5](taa.md#35-arming-and-switching)).

### 4.5 Switches

- A look knob `aa` with `taa` (default) and `fsr`, and a pass param `FSR`; with `fsr` the TAA passes are off and the FSR pass runs. RTV only, like every post pass; not with `-Rov`.
- The `-Info` overlay names the method; `-ShaderDev` switches it live for A/B.
- A runtime without FSR built logs a warning and keeps TAA.

## 5. Upscaling (stage 3, a separate project)

Since 25 October 2026, upscaling builds on the scales of N5's native resolution ([d3d-hle-n5.md, section 18](d3d-hle-n5.md#18-native-resolution-and-aspect-design-24-october-2026)): the native path already renders the scene's surfaces at a rational scale and every other surface at the integer scale, chosen per resolve destination, which removes the one-scale blocker below. The project owner placed FSR 3.1 after N5's widescreen step, with native AA first and upscaling on top of those scales.

Section 9.3 replaces this section's earlier outline (a second whole scale for the post chain): with N5 the input and output scales are rational and chosen per surface and resolve destination.

- What it can gain: on this PC the GPU sets the frame rate from 2x up (a race frame takes 5.1 ms at 2x, 6.6 ms at 20/9 and 9.4 ms at 3 on the native path, [D3D-level HLE, 18.17 and 18.18](d3d-hle-n5.md#1818-increment-1s-close-gate-25-october-2026)); on the ROG Xbox Ally X it does at every scale. At 1x and 240 Hz on this PC the command processor sets the frame rate, so upscaling gains nothing there.

## 6. Validation

- **Offline**: an FSR test beside `fm4_post_pass_shader_tests` on WARP with the synthetic scene: a still edge converges and holds still, a moving box leaves no trail, a cut resets.
- **In the game, 2x** (the launcher's scale on the development panel), the same frozen and moving frames for TAA and FSR, switched live in one run: 1:1 crops (the cable car wires, fences, the grandstand lattice, distant road lines); consecutive captures of rewind freezes, pause and photo mode (the share of pixels that change by more than 4 of 255); ghosting cases (tire smoke, sparks, the start's smoke, the interior mirror).
- **GPU time**: `REX_FRAME_GRAPH` every frame with TAA and FSR switched every second, at 1x and 2x.
- **Gates**: `smoke` against TAA, `perf`, and the Japan/Asia check on the same binary.

## 7. Stages and effort

Section 9.7 updates these stages and their gates against N5.

| Stage | Work | Estimate |
|---|---|---|
| 0 | License file read, pinned fetch in `setup`, DXC, the two libraries built with the project's toolchain, notices | 1 to 2 days |
| 1 | The deferred list callback and state invalidation, the FSR context, inputs and output at the scene_color point, the jitter rule, the knob | 2 to 4 days |
| 2 | WARP test, in-game A/B at 2x, GPU time, gates, the decision TAA or FSR as the default | 1 to 2 days |
| 3 | Upscaling with a separate scene scale (its own design note first) | weeks |

## 8. Prerequisites and open questions

- The single tile without MSAA as the default with TAA: done on 20 October 2026; FSR cannot take MSAA input, so `-Msaa` and `-Rov` would leave it out.
- Whether the SDK's shader permutation build runs under the project's toolchain without large patches: it does, with no patch of the SDK (3.1), so FSR 2.2 is not needed.
- Whether the callback in the deferred list (4.1 a) can restore the command processor's descriptor heap state cheaply.
- Exposure: auto exposure or 1.0 (A/B).
- DLAA (NVIDIA) through the same inputs needs its own license review; not planned yet.

## 9. The plan against N5 (25 October 2026)

For the main session's approval. N5 changed what FSR builds on: the native path in one pass with 2x MSAA renders the scene's private surfaces at a rational scale s, the other surfaces at the integer scale k, a full-screen resolve of a private surface into textures at s, small destinations down to k with a box filter, and the scene at the screen's logical size (Hor+ or Vert+) with the HUD in the centred 1280x720 ([D3D-level HLE, section 18](d3d-hle-n5.md#18-native-resolution-and-aspect-design-24-october-2026)).

### 9.1 Where FSR 3.1 sits

- **The point**: the post pass framework's scene_color point, as before (section 4.2): after the title's scene and before its post chain (motion blur, bloom, exposure, the final combine) and the HUD. On the native path every input there is a texture at the scene's scale s and logical size: the scene colour (the `2_10_10_10_FLOAT` destination `1B811000`), the scene depth (its depth-format textures, 18.16), the stencil (its `8_8_8_8` view), the motion vectors and the motion mask (`GuestSceneWidth` and the shifted unpack, 18.21).
- **The paths**: the native path in one pass with 2x MSAA, at any scale and logical size; the scene is single-sample by then (the 2x MSAA surface resolves into the scene colour). RTV keeps TAA only (its tiles and whole scales; FSR could run there at ratio 1 too, but the native path is the target), and `-Rov` neither, as with every post pass.
- **Photo mode, the menus and the pre-race screens** stay at k and 16:9 until "entry 0 at s" (18.20); FSR leaves them, as the scene passes do (18.17).

### 9.2 Native AA (ratio 1)

- A look knob `aa` (`taa`, the default, or `fsr`), mapped to a pass param `FSR`; with `fsr` the TAA passes are off and FSR runs at the scene_color point with render size = output size = the scene's host size (ScaleUp of the logical size). The knob switches live under `-ShaderDev` for A/B; the `-Info` overlay names the method.
- The output goes back into the scene colour's place through the existing write-back (section 4.3), so the title's post chain and the final combine see it as they see TAA's.
- TAA stays the default until an A/B at 3/2 and 2x decides (section 9.7).

### 9.3 Upscaling

The scene renders at an input scale s_in below the output scale s_out (the screen's own, 18.19), and FSR upscales the scene colour at the scene_color point; everything after it (the title's post chain, the HUD, the swap) is at s_out. Examples: a 1080p panel (s_out = 3/2) with s_in = 1 (ratio 1.5, FSR's Quality) or 6/5 (ratio 1.25); a 4K panel (s_out = 3) with s_in = 2 (1.5) or 3/2 (2.0, Performance).

How N5 expresses it:

- **Two scene scales.** Today one scene scale serves every private surface. Upscaling splits them: the scene's own surfaces (the 2x scene colour at base 0 and its depth at 1024, the private depth at 720) at s_in, the post chain's (the one-sample colour at base 0 and the final picture) at s_out. `GetKeyScale` picks by key, as it picks between s and k today; the scene draws' viewport, scissor, translation variant (`scene_scale`, translated per scale) and occlusion normalization take the draw's surface scale.
- **Two kinds of scene textures.** A full-screen resolve of a scene surface writes textures at s_in (the depth, the stencil view, the scene colour before FSR); FSR's output replaces the scene colour's texture with one at s_out, and the post chain's own full-screen resolves write textures at s_out. The texture cache's `scene_scaled` bit becomes a scale per scene-resolved range (s_in or s_out), so a fetch gets the texture of the size the range was resolved at.
- **The post chain samples the upscaled colour.** The title's post chain reads the scene colour in normalized coordinates, so a texture at s_out is read as the one at s_in would have been; small destinations of the post chain resolve down to k from s_out with the box filter (18.17).
- **The logical size** applies to both scales: s_in times the logical size in, s_out times it out; the HUD stays in the centred 1280x720 at s_out.
- **The jitter**: FSR's phase count for the ratio (`ffxFsr3UpscalerGetJitterPhaseCount`), on every frame of a scene (section 4.4), in host pixels at s_in.
- **The motion vectors and the masks** at s_in; FSR's display size is the output.

### 9.4 Running FSR in the deferred command list

Option (a) of section 4.1, a callback command: `DeferredCommandList::D3DCallback` records a function that the replay calls with the real command list at that position, on the submit thread. FSR's D3D12 backend dispatches there and binds its own root signature, pipelines and descriptor heap; afterwards the replay marks the root signature, the pipeline and the descriptor heaps as unknown, so the next post pass or draw binds its own again. The draw back end of N4 replays its draws on the same thread, after the callback in list order. FSR's backend needs its own descriptor heap (a CBV/SRV/UAV heap it creates) or a range of the runtime's; the runtime's shader-visible heap is rebound after the callback.

### 9.5 The jitter, the reactive and transparency masks

- **Jitter**: FSR's Halton sequence (or the framework's, of the same length at ratio 1) on every frame of a scene, not the TAA gate's (only while the camera moves); `TAA_JITTER_MOTION` and `TAA_JITTER_RATE` at 0 with `aa=fsr`. The jitter is in host pixels at s_in (18.21 adds no change to it at a logical size).
- **Reactive mask** (`motion_mask` G today, car glass): the windshield pass's glass pixels and the see-through pixels; particles (tire smoke, sparks, dust) are not marked yet, so FSR may ghost them: a particle flag from the particle draws' pixel shaders (a manifest list of their hashes) writing the mask is an item of stage 1.
- **Transparency and composition mask** (`motion_mask` R): the pixels with no motion known (the mirror panels, the interior mask).
- **The cockpit interior**: its motion is the camera's, as the motion vectors give it since 18.21's mask fix; the windshield's reflections ride on the glass pixels of the reactive mask.

### 9.6 The cost on the ROG Xbox Ally X (estimate)

Assumption, not measured: FSR 3.1's upscaler costs about 0.5 to 1 ms at a 1080p output on a mid-range desktop GPU, and the Ally X's Radeon 890M is two to three times slower, so 1.5 to 2.5 ms there. The Ally X took 7.9 ms a race frame at 1x and 24.2 ms at 2x (17.21), about 14.7 ms at 3/2 by 18.8's fit. A 1x scene upscaled to 1080p would take about 7.9 + 2 = 10 ms (about 100 fps if GPU-bound) against 14.7 ms for 3/2 native (68 fps); s_in = 6/5 about 10.4 + 2 = 12.4 ms. Native AA at 3/2 adds the same 1.5 to 2.5 ms to 14.7 ms. To be measured in stage 2 and stage 3 through the export.

### 9.7 Stages and gates

| Stage | Work | Gate |
|---|---|---|
| 0 | The download (section 10, after the owner's approval), the license file read, the notices, the two libraries (`ffx_fsr3upscaler`, `ffx_backend_dx12`) built with LLVM 20 and the SDK's shader compiler (`FidelityFX_SC.exe` with its DXC) into BuildRoot behind `REX_FSR` (off by default) | the runtime builds with and without it; the SDK's shader permutations compile; nothing runs yet. Passed on 25 October 2026 (3.1) |
| 1 | The callback (9.4), the FSR context, the inputs and output at the scene_color point, the jitter rule, the knob, the particle flag | a WARP test with the synthetic scene (a still edge converges and holds, a moving box leaves no trail, a cut resets) |
| 2 | Native AA in the game | 1:1 crops at 3/2 and 2x against TAA (the cable car wires, fences, the grandstand lattice, distant road lines), still pictures (the share of pixels that change), ghosting cases (tire smoke, sparks, the interior mirror), GPU time per frame on this PC, smoke and perf gates, the 16:9 parity unchanged with `aa=taa`; the owner's A/B decides the default |
| 3 | Upscaling (9.3): two scene scales, the texture cache's scale per range, the launcher's choice of the input scale | 1:1 crops of the upscaled frame against native s_out and against s_in, the byte checks (the destinations at k), GPU time at 1080p and 4K on this PC, and the Ally X through the export |

Effort, inferred: stage 0 one to two days if the SDK's CMake and shader compiler build under clang (FSR 2.2 is the fallback), stages 1 and 2 three to six days, stage 3 two to three weeks.

## 10. The download for stage 0 (approved on 25 October 2026)

The project owner approved the download on 25 October 2026 (relayed by the main session), on the condition that AMD's credit goes with the code: the notices in `outputs/FM4-source-control/third-party-notices`, the entry in [sources and licensing](../sources-and-licensing.md#third-party-code-amd-fidelityfx-sdk-fsr-31), a credits line in the README, and the notices in every export. It was fetched on the same day into `I:\FM4-build-usa\fidelityfx-sdk`. `fetch_fidelityfx` in `pipeline.py` fetches it pinned, when `setup`, `build` or `all` runs with `--fsr on`:

- **Repository**: `https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK`.
- **Tag and commit**: `v1.1.4`, commit `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`, the last 1.1.x release (the FSR 3.1 upscaler from source). The 2.x tags (`v2.0.0` to `v2.3.0`) are the line that ships FSR 4 as signed DLLs and are not used.
- **Commit check**: after the fetch, `HEAD` is `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55` (checked on 25 October 2026); `fetch_fidelityfx` stops when it differs.
- **License**: `LICENSE.txt` at the tag (1,115 bytes) is the MIT license, "Copyright (C) 2024 Advanced Micro Devices, Inc."; `sdk/LICENSE.txt` is the same file. Read in full on 25 October 2026 and copied into the notices. The DX12 backend also compiles Microsoft's `d3dx12.h` (MIT, its `license.txt` beside it), whose notice is there too.
- **What is fetched**: a sparse, blob-filtered clone (`git clone --filter=blob:none --no-checkout --sparse`, then `git sparse-checkout set --no-cone` with the paths of `FFX_SDK_PATHS`), checked against the commit: `LICENSE.txt`, `sdk/LICENSE.txt`, `sdk/CMakeLists.txt`, `sdk/toolchain.cmake`, `sdk/include`, `sdk/src/components/fsr3upscaler`, `sdk/src/backends/dx12`, `sdk/src/backends/shared`, `sdk/src/shared` and `sdk/tools/binary_store`. Against the list below, measured at stage 0: `sdk/src/backends/shared` (the shader blob accessors the backend compiles, 356 KB) is added, and `sdk/tools/ffx_shader_compiler` (the source of the compiler driver, whose build is in `binary_store`) and `sdk/libs` (PIX and AntiLag 2) are not fetched. The list as planned:
  - `sdk/include` (the host and GPU headers: `FidelityFX/host/ffx_fsr3upscaler.h`, the DX12 backend's header, the shaders' `gpu/fsr3upscaler`),
  - `sdk/src/components/fsr3upscaler` (`ffx_fsr3upscaler.cpp`, 91,833 bytes), `sdk/src/backends/dx12` and `sdk/src/shared`,
  - `sdk/tools/ffx_shader_compiler` and `sdk/tools/binary_store`, which holds the SDK's shader compiler `FidelityFX_SC.exe` (474,624 bytes) with `dxcompiler.dll` (17,802,672 bytes) and `dxil.dll` (1,463,736 bytes), so no separate DXC download is needed; the same folder holds `D3D12Core.dll`, `d3d12SDKLayers.dll`, `d3dconfig.exe` and `glslangValidator.exe`, which the build does not use,
  - `sdk/CMakeLists.txt` and `sdk/toolchain.cmake`.
- **Not fetched**: `samples`, `framework`, `docs`, `ffx-api`, `PrebuiltSignedDLL` (the SDK's own signed DLLs) and the samples' media (`UpdateMedia.bat`).
- **Size**: the whole repository is about 411 MB (GitHub's figure, with the samples' media); the sparse checkout measures 39 MB of files, of which 34 MB are the binaries of `sdk/tools/binary_store`, and 36 MB of Git objects.
- **Where**: outside the repository, into the BuildRoot (`<BuildRoot>\fidelityfx-sdk`), as the runtime's own upstream clone; only this project's integration code goes into Git, with AMD's notice under `outputs/FM4-source-control/third-party-notices`.

## Related

- [TAA](taa.md), [motion vectors](motion-vectors.md#6-requirements-for-taa-and-the-upscalers), [post pass framework](post-pass-framework.md)
- [Sources and licensing](../sources-and-licensing.md#third-party-code-amd-fidelityfx-sdk-fsr-31)
