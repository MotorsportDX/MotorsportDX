# FSR: plan

A plan to add AMD FSR 3.1 to the runtime's post pass framework, first in its native anti-aliasing mode (no upscaling) beside the project's own [TAA](taa.md), and later, as a separate project, as an upscaler. Nothing of it is built. Written on 20 October 2026; the license review is in [sources and licensing](../sources-and-licensing.md#candidate-third-party-code-amd-fsr).

## Summary

- **Version**: FSR 3.1 (the FidelityFX SDK 1.1.x upscaler), built from its MIT source. FSR 4 is out: it ships as signed AMD DLLs only, on undocumented redistribution terms, and runs on Radeon RX 9000 GPUs only. FSR 2.2 is the fallback if the 3.1 build does not fit (also MIT, with an older and simpler code base).
- **Mode**: native AA first (render size = output size, ratio 1.0), as an alternative to TAA, chosen with a look knob. TAA stays the default on every GPU until an A/B decides otherwise.
- **Point**: the scene_color point ([TAA, section 2.2](taa.md#22-the-scene_color-point-option)), where the color, depth, motion vectors and masks share the scene layout, so nothing has to be rotated, and the output goes back into the game's scene through the existing write-back.
- **Needs**: the single tile without MSAA (the default with TAA since 20 October 2026), the jitter on every frame of a scene (FSR accumulates still pictures itself), a way to run FSR's D3D12 backend inside the runtime's deferred command list, DXC for its Shader Model 6.2 shaders, and the license steps below.
- **Not in the first stage**: upscaling. The runtime has one draw resolution scale for every surface, and an upscaler needs the scene tiles at one scale and the post chain and HUD at another ([motion vectors, section 6](motion-vectors.md#injection-point)), the largest piece of work in that list.

## 1. Why, and what it can gain

- Our TAA is a first version: a 3x3 Gaussian reconstruction, a variance clip, a fixed blend and a simple sharpening ([TAA, section 7](taa.md#7-the-resolve)). FSR 3.1's upscaler adds locks for thin features, a luma-stability history, a reactive and a transparency mask, and RCAS sharpening. Where it should help most: wires, fences and lattices in motion on the single tile, and still pictures.
- GPU cost: FSR 3.1's native AA at 2560x1440 is expected to cost about 0.5 to 1 ms on this GPU, against 0.15 to 0.21 ms for our TAA at the final point and 0.47 ms at the scene_color point; to be measured (section 6). At 1x and 240 Hz the command processor sets the frame rate, so the cost shows at 2x and above only.
- It runs on every vendor (NVIDIA, AMD, Intel) with Shader Model 6.2, unlike DLAA.

## 2. Licensing and sources (stage 0)

1. Read the `LICENSE.txt` of the FidelityFX SDK at the tag to be used (1.1.4 or the last 1.1.x); the SDK's README states MIT, but the file was not read yet.
2. Do not vendor the SDK into the repository: fetch it during `setup` at a pinned tag and commit hash, as the runtime's upstream SDK clone is pinned, and verify the hash. Only integration code of this project goes into Git.
3. Add AMD's notice under `outputs/FM4-source-control/third-party-notices`, whitelist new files in `.gitignore`, add review entries to `tracked-files.json`, and run `check-staged-content.py`.
4. Check that MIT code linked into the runtime fits the runtime's license ([work/runtime/LICENSE](../../work/runtime/LICENSE)); MIT is permissive and normally does.
5. The SDK's prebuilt `amd_fidelityfx_dx12.dll` (the FidelityFX API) is not used: build the effect from source into a static library.

## 3. Build (stage 0)

- The FSR 3.1 upscaler is C++ host code (`ffx_fsr3upscaler`) plus HLSL shaders compiled into permutation headers, and a D3D12 backend (`ffx_backend_dx12`).
- The shaders need DXC (Shader Model 6.2, 16-bit types where supported); the runtime compiles its own shaders with FXC. Check that the Windows SDK 10.0.26100 `dxc.exe` builds them, or fetch a pinned DXC release in `setup`, and add it to `doctor`.
- Build the two libraries with the project's toolchain (LLVM 20, CMake, Ninja) into BuildRoot and link them into `rexgpu`. A build switch keeps the runtime buildable without them (`REX_FSR=OFF` by default until stage 2 passes).
- Risk: the SDK's CMake expects MSVC and its own shader compiler wrapper; adapting it to clang-cl may take a patch, applied at setup like the runtime's own.

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

- The runtime's single draw resolution scale would become two: one for the scene tiles (and the depth and color resolves that feed the post passes) and one for the post chain, the HUD and the swap; the texture cache serves the upscaled scene to the bloom and final combine fetches.
- The render scales are whole multiples of 1280x720, so the ratios available are 2.0 (1x to 2x, 2x to 4x), 1.5 (2x to 3x) and 1.33 (3x to 4x).
- What it can gain: at 2x the GPU sets the frame rate above about 160 fps, and 3x costs 8.9 ms a frame against 5.4 ms at 2x ([current status](../status/current.md)); a 2x scene upscaled to 4K would take most of that back. At 1x and 240 Hz the command processor sets the frame rate, so upscaling gains nothing there.
- Its own design note first: the per-surface scale touches the render target cache, the resolves and the texture cache.

## 6. Validation

- **Offline**: an FSR test beside `fm4_post_pass_shader_tests` on WARP with the synthetic scene: a still edge converges and holds still, a moving box leaves no trail, a cut resets.
- **In the game, 2x** (the launcher's scale on the development panel), the same frozen and moving frames for TAA and FSR, switched live in one run: 1:1 crops (the cable car wires, fences, the grandstand lattice, distant road lines); consecutive captures of rewind freezes, pause and photo mode (the share of pixels that change by more than 4 of 255); ghosting cases (tire smoke, sparks, the start's smoke, the interior mirror).
- **GPU time**: `REX_FRAME_GRAPH` every frame with TAA and FSR switched every second, at 1x and 2x.
- **Gates**: `smoke` against TAA, `perf`, and the Japan/Asia check on the same binary.

## 7. Stages and effort

| Stage | Work | Estimate |
|---|---|---|
| 0 | License file read, pinned fetch in `setup`, DXC, the two libraries built with the project's toolchain, notices | 1 to 2 days |
| 1 | The deferred list callback and state invalidation, the FSR context, inputs and output at the scene_color point, the jitter rule, the knob | 2 to 4 days |
| 2 | WARP test, in-game A/B at 2x, GPU time, gates, the decision TAA or FSR as the default | 1 to 2 days |
| 3 | Upscaling with a separate scene scale (its own design note first) | weeks |

## 8. Prerequisites and open questions

- The single tile without MSAA as the default with TAA: done on 20 October 2026; FSR cannot take MSAA input, so `-Msaa` and `-Rov` would leave it out.
- Whether the SDK's shader permutation build runs under the project's toolchain without large patches (stage 0 decides; FSR 2.2 is the fallback).
- Whether the callback in the deferred list (4.1 a) can restore the command processor's descriptor heap state cheaply.
- Exposure: auto exposure or 1.0 (A/B).
- DLAA (NVIDIA) through the same inputs needs its own license review; not planned yet.

## Related

- [TAA](taa.md), [motion vectors](motion-vectors.md#6-requirements-for-taa-and-the-upscalers), [post pass framework](post-pass-framework.md)
- [Sources and licensing](../sources-and-licensing.md#candidate-third-party-code-amd-fsr)
