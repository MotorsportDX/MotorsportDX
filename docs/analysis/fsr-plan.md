# FSR: plan

A plan to add AMD FSR 3.1 to the runtime's post pass framework, first in its native anti-aliasing mode (no upscaling) beside the project's own [TAA](taa.md), and then as an upscaler on N5's scales. Stages 0 to 2 are done: the runtime builds the upscaler from the SDK source behind `REX_FSR` (section 3.1), runs its native anti-aliasing at the scene_color point (9.8) and was checked in the game (9.9); stage 3, upscaling, is section 11. Written on 20 October 2026 and updated against N5 on 25 October 2026 (sections 9 to 11); the license review is in [sources and licensing](../sources-and-licensing.md#third-party-code-amd-fidelityfx-sdk-fsr-31).

## Summary

- **Version**: FSR 3.1 (the FidelityFX SDK 1.1.x upscaler), built from its MIT source. FSR 4 is out: it ships as signed AMD DLLs only, on undocumented redistribution terms, and runs on Radeon RX 9000 GPUs only. FSR 2.2 is the fallback if the 3.1 build does not fit (also MIT, with an older and simpler code base).
- **Mode**: native AA first (render size = output size, ratio 1.0), as an alternative to TAA, chosen with a look knob. TAA stays the default on every GPU until an A/B decides otherwise.
- **Point**: the scene_color point ([TAA, section 2.2](taa.md#22-the-scene_color-point-option)), where the color, depth, motion vectors and masks share the scene layout, so nothing has to be rotated, and the output goes back into the game's scene through the existing write-back.
- **Needs**: the single tile without MSAA (the default with TAA since 20 October 2026), the jitter on every frame of a scene (FSR accumulates still pictures itself), a way to run FSR's D3D12 backend inside the runtime's deferred command list, DXC for its Shader Model 6.2 shaders, and the license steps below.
- **Not in the first stage**: upscaling. It builds on N5's scales: the native path renders the scene's private surfaces at a rational scale and every other surface at the integer scale, chosen per resolve destination, at the screen's logical size; upscaling splits the scene's scale into an input scale for the scene and an output scale for the post chain, the HUD and the swap (section 9, built in stage 3: section 11).
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
- Whether the callback in the deferred list (4.1 a) can restore the command processor's descriptor heap state cheaply: it does, by binding the list's last heaps again (9.8).
- FSR's shading change pyramid pass on WARP (9.8, open item): the CPU tests run FSR's checks on the hardware adapter.
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

### 9.8 Stage 1 (25 October 2026)

- **The callback** (9.4): `DeferredCommandList::D3DCallback(function, arguments)` stores a function and a copy of its arguments; the replay calls it with the real command list in list order, binds the list's last descriptor heaps again after it (also the draw back end's sampler heap with `d3d12_draw_back_end=2`) and forgets the pipeline and the back end's root state. At record time `D3D12CommandProcessor::InvalidateCommandListState` makes the next draw bind the guest's pipeline, root signature and root parameters again.
- **The upscaler** (`work/runtime/include/rex/graphics/d3d12/fsr.h`, `fsr.cpp`): `FsrUpscaler` holds an FSR 3.1 context per render and output size, created on the command processor's thread at the first frame that needs it, with an inverted depth and an infinite far plane, and records its dispatch on the replay thread. FSR 3.1 needs the application to provide the three resources it shares with the effects after it (the dilated depth, the dilated motion vectors and the reconstructed previous nearest depth); the class creates them with the context. Without `REX_FSR` it is a stub (`Available()` false). A context replaced by a new size is released once the GPU is past the submission that last used it.
- **The passes** (`work/fm4-rex/passes/fsr.hlsl` and `passes.toml`): `fsr_prepare` turns the scene color (gamma 2) into linear color, the scene depth into FSR's depth (`FSR_DEPTH_NEAR / W`, 0 for the sky), `motion_mask` G (and the reactive mark) into the reactive mask (`FSR_REACTIVE`, 0.8) and `motion_mask` R into the transparency and composition mask (`FSR_COMPOSITION`, 1.0); `fsr_upscale`, a pass of the new type `fsr`, runs FSR on them and the motion vectors (host pixels, the scale 1 for FSR's pixel units); `fsr_output` encodes the output back to gamma 2 into `fsr_scene_out`, which the write-back hands to the game's post chain. FSR's reset: a new context, a frame without FSR before, a scene without a previous one, a camera cut. Exposure 1, no HDR flag (the scene is 0 to 1), RCAS from `FSR_SHARPNESS`, FSR's debug view with `FSR_DEBUG=1`, and FSR's 16-bit shaders where the GPU has 16-bit operations, FSR's own default (`FSR_FP16=0` keeps them to 32 bits, an A/B for stage 2, on this PC and later on the ROG Xbox Ally X through the owner's export).
- **The switch**: the look knob `aa=fsr` (with `taa` on) sets `FSR=1`, `TAA_POINT=1` and the look's sharpening as `FSR_SHARPNESS` (none in the final combine) in both launchers, and the `-Info` overlay names the method; the GUI launcher has a TAA or FSR 3.1 choice. The TAA passes have `FSR == 0` in their conditions, and `gtao_temporal` runs as with TAA at the scene_color point. A runtime without FSR, or one whose context cannot be created, keeps `FSR` at 0 with a warning, so TAA runs at that point instead. `REX_FSR` stays off by default until stage 2's A/B.
- **The jitter**: `fsr_upscale` is a consumer of `[jitter]`, and `every_frame_if = "FSR > 0"` jitters every frame after a consumer's run, without the gate's motion, frame rate and still tests. The offset FSR gets is the framework's jitter in host pixels (the content's shift in the scene layout), which the test confirms.
- **The particle flag** (option (c) of [motion vectors, section 9](motion-vectors.md#open), the main session's choice of 25 October 2026): `xe_post_mark_reactive(input.xe_position, value)` in the override library writes the manifest's `reactive_mark` texture, which `fsr_prepare` adds to the reactive mask. It is bound and cleared only while an override or mod file calls the function, so with no such mod (none yet) it costs nothing. The particle mods themselves come at stage 2, for the particles that visibly ghost in the A/B.
- **Gate**: `fm4_post_pass_shader_tests` runs the passes of the scene_color point over frames as the framework does, with FSR through `FsrUpscaler`: a still edge converges to its pixel coverage and holds, a moving box leaves no trail and keeps its color, a cut resets the accumulation (results in [current status](../status/current.md#fsr-31-native-anti-aliasing-stage-1-25-october-2026)). FSR's shading change pyramid pass crashes WARP itself (the open item below), so on WARP the six FSR checks are skipped with a line that says so and counted as skipped, not passed, and the build runs the test a second time on the first hardware adapter (`--hardware`, not while `fm4_rex.exe` runs; runs by hand take the shared GPU lock), where they run; everything else stays on WARP. Without a hardware adapter the second run skips them with "FSR checks skipped: no hardware adapter". The main session approved this on 25 October 2026 and rejected a patch of the SDK's shader at setup (it would break the pinned v1.1.4 source that `FFX_REV` verifies and change what is credited as AMD's code) and a debug layer (a download or a Windows feature, the owner's decision).
- **Open item, WARP**: from the second frame on, `FSR3-SHADING-CHANGE-PYRAMID` crashes WARP's shader execution (an access violation in the test process; `Close` and the dispatch return no error). Found by running FSR's jobs one by one through a counter in a local copy of the backend: up to `FSR3-LUMA-PYRAMID` every frame runs, adding the shading change pyramid crashes. WARP itself clears a 1x1 `R32G32B32A32_FLOAT` texture fine (the frame info clear first suspected), and clamping the two indices of `ComputeMinimumDifference` into its five-sample arrays (they can step past them) did not stop the crash. The same pass runs on the RTX 5070 Ti Laptop, where every check passes. Without the D3D12 debug layer (not installed; the SDK's Agility 1.614 is not newer than the inbox runtime) the cause is not known.

### 9.9 Stage 2, the checks before the owner's A/B (25 October 2026)

On `I:\FM4-build-usa`, one binary for both methods: `game-build-fsr`, the game of 40e0ee8 with the runtime built with `REX_FSR` on (the gate area `fsr-gate`), the look `aces` against `aces,aa=fsr` (TAA at the final point against FSR at the scene_color point, both with the look's sharpening 0.4 after them), the title's AI driving, a fixed seed.

**GPU time per frame of the race, the passes' groups (`-Native`, one pass with 2x MSAA, `photo`'s race phase):**

| Scale | Frame, TAA | TAA | Frame, FSR | FSR (`FSR_FP16=1`) | FSR (`FSR_FP16=0`) |
|---|---|---|---|---|---|
| 1x (1280x720) | 2.48 ms | 0.10 ms | 2.81 ms | 0.32 ms | 0.32 ms |
| 3/2 (1920x1080) | 3.74 ms | 0.21 ms | 4.11 ms | 0.61 ms | 0.66 ms |
| 2x (2560x1440) | 4.77 ms | 0.36 ms | 5.73 ms | 1.09 ms | 1.08 ms |

The `fsr` group is `fsr_prepare`, FSR's own passes and `fsr_output` (a new group of the perf trace, [traces](../e2e/traces.md)). FSR costs about three times TAA's time on this PC. RTV at 2x gives the same picture: the race 1.07 to 1.23 ms against 0.35 to 0.37 ms. In photo mode at 2x FSR takes 1.49 ms (1.32 ms with 32-bit shaders) against TAA's 0.51 ms; at 3/2 photo mode renders at the whole scale and runs no scene passes (18.17), with TAA and FSR alike. The first FSR context of a size took 743 ms to create with a cold driver shader cache, 21 ms later: a one-time hitch at the first race frame.

**FP16**: the RTX 5070 Ti Laptop shows no difference in time (0.32, 0.61 against 0.66, 1.09 against 1.08 ms) and none in the picture (the frozen frame at 2x differs between 16-bit and 32-bit shaders by 22.8% of the pixels above 4, inside the range of two runs of one setting, 18% to 41%). FSR's own default (16-bit where the GPU has native 16-bit operations) stays; the ROG Xbox Ally X's Radeon 890M runs 16-bit math at twice the 32-bit rate, which the owner's export measures.

**Pictures** (2x, full-resolution captures, 1:1 crops of TAA against FSR):
- The frozen frame (the cockpit at the start of Bernese Alps): FSR is sharper on the cable car's wires, the pylon's lattice and the grandstand roof; the gauges and the crowd are alike; nothing new aliases.
- The start of the race (the same moment in both runs, the gantry at 00:00:01): the gantry and the wires are crisper with FSR at 2x and at 3/2.
- The chase camera in motion: the car's paint, rear window and badges are sharper with FSR and leave no trail; the cockpit's A-pillar and the mirror edge in motion are clean in both. The runs do not reach the same moment (about 0.3 s apart), so these are side by side, not matched.
- Photo mode at 2x: FSR's picture is clean (the runs chose different photo angles).
- Tire smoke at the start, in the mirror and ahead: no trail behind the smoke with either method. No particle ghosted in these captures, so no particle mod writes the reactive mark yet; `reactive_mark` stays unbound (no "reactive mark bound" in the logs) and costs nothing.
- The HUD: identical in both (text and the rewind menu), as it is drawn after the scene_color point.

**No change with `aa=taa`**: the frozen frame with the FSR runtime and `original,taa=0` against stage 1's runs passes parity on RTV (draws +0.0%, 30.5% of the image above 4) and `-Rov` (27.5%). Stage 2 changes no shared code apart from the perf trace's `fsr` group.

**The owner's A/B** (an export built with `Build.ps1 -Action build -Fsr on`, then `-Action export`; it carries `third-party-notices` with AMD's and Microsoft's licenses):
1. In the GUI launcher, the look `aces` (the default), Temporal AA on, and Method TAA, then the same drive with Method FSR 3.1. On the ROG Xbox Ally X keep its defaults (the native path at the screen's scale) and turn Performance trace on; on this PC try the default and Render scale 2x.
2. Drive a Quick Race on Bernese Alps: the standing start (tire smoke ahead and in the mirrors), the first lap in the cockpit camera (dashboard, gauges, windshield and A-pillar edges, the rear-view mirror's picture), then the chase camera (car edges, fences, the cable car wires, the grandstand roof, distant road lines). Rewind with Y and hold a still frame for a few seconds (shimmer on a still picture). Open photo mode and take a photo. Watch a replay.
3. Look for: sharpness of thin lines and text, shimmer or crawling edges on still frames, trails or smears behind cars, smoke and sparks, the mirror's picture lagging, any flicker or black frame, the HUD.
4. Send back: the `-Info` overlay's "Anti-aliasing:" line (a photo of the screen is enough), the lines of `runtime.log` that contain "FSR" (the context's size and creation time, any warning that TAA runs instead, any FSR error), and with Performance trace the `perf-<profile>-<time>.jsonl` files, which hold the GPU time of the `fsr` and `taa` groups.

**The owner's first result on the ROG Xbox Ally X** (25 October 2026, relayed by the main session): the export of f4891c4 (`E:\FM4-Test-Ally-2026-10-25`, logs in `D:\log_ally_3`) on the Radeon 890M at 117 Hz with the launcher's defaults for integrated GPUs (`-Native` at 1x, 1280x720, one pass with 2x MSAA), the look `aces`, a Quick Race. The medians of the race windows (the two runs are not frame-matched):

| Method | fps | Frame p99 | GPU a frame | The AA pass |
|---|---|---|---|---|
| TAA (`perf-1`) | 91.0 | 26.8 ms | 10.42 ms | `taa` 0.61 ms |
| FSR (`perf-2`, `FSR_FP16=1`) | 80.7 | 33.3 ms | 11.47 ms | `fsr` 2.57 ms |

A third run had FSR's native AA at 1920x1080 (its context created in 717 ms, the cold driver cache's hitch), without a perf trace. FSR's native AA costs about four times TAA's time on the 890M, eight times this PC's 0.32 ms at 1x, so at 1x it is not worth it there. The main session's decision of 25 October 2026: TAA stays the default with integrated GPUs; FSR's value there is the upscaling of stage 3 (720 to 1080 lines, section 11), which the next export measures. The default with discrete GPUs waits for the owner's visual verdict, which has not come yet.

## 10. The download for stage 0 (approved on 25 October 2026)

The project owner approved the download on 25 October 2026 (relayed by the main session), on the condition that AMD's credit goes with the code: the notices in `outputs/FM4-source-control/third-party-notices`, the entry in [sources and licensing](../sources-and-licensing.md#third-party-code-amd-fidelityfx-sdk-fsr-31), a credits line in the README, and the notices in every export. It was fetched on the same day into `I:\FM4-build-usa\fidelityfx-sdk`. `fetch_fidelityfx` in `pipeline.py` fetches it pinned (the constants `FFX_URL`, `FFX_TAG`, `FFX_REV` and `FFX_PATHS`, as the ReXGlue SDK's `SDK_URL` and `SDK_REV`) at every `setup`, and at `build` with `--fsr on`; `doctor` reports the checkout. A mirror, `FFX_MIRROR`, takes over when GitHub cannot be reached: another Git URL (a fork on the project owner's GitHub, for example) or a local Git bundle of the pinned commit, which setup writes after a fetch (on the development computer `D:\FM4-archive\third-party\fidelityfx-sdk-v1.1.4.bundle`, 14,584,507 bytes, written again on 25 October 2026 with `.gitattributes`; [setup, the FidelityFX SDK mirror](../setup.md#the-fidelityfx-sdk-mirror)):

- **Repository**: `https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK`.
- **Tag and commit**: `v1.1.4`, commit `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`, the last 1.1.x release (the FSR 3.1 upscaler from source). The 2.x tags (`v2.0.0` to `v2.3.0`) are the line that ships FSR 4 as signed DLLs and are not used.
- **Commit check**: after the fetch, `HEAD` is `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55` (checked on 25 October 2026); `fetch_fidelityfx` stops when it differs, and refuses a bundle without that commit.
- **The mirror bundle**: Git neither bundles a partial clone with missing objects nor clones a filtered bundle, so setup clones the checkout locally as a shallow clone of the pinned commit with a `sparse:oid` filter of `FFX_PATHS` (no network), bundles that with the same filter (bundle format v3), and restores it by unbundling into a new repository marked shallow and partial. Checked on 25 October 2026 with GitHub unreachable: the checkout from the bundle (without network) and from another Git URL both have the index of the GitHub checkout, the bundle's checkout is byte-identical to it and `git status` runs on it without network, and the runtime built from the bundle's checkout with `REX_FSR` on.
- **License**: `LICENSE.txt` at the tag (1,115 bytes) is the MIT license, "Copyright (C) 2024 Advanced Micro Devices, Inc."; `sdk/LICENSE.txt` is the same file. Read in full on 25 October 2026 and copied into the notices. The DX12 backend also compiles Microsoft's `d3dx12.h` (MIT, its `license.txt` beside it), whose notice is there too.
- **What is fetched**: a sparse, blob-filtered clone (`git clone --filter=blob:none --no-checkout --sparse`, then `git sparse-checkout set --no-cone` with the paths of `FFX_PATHS`), checked against the commit: `.gitattributes` (the repository's line endings, added on 25 October 2026 so that a checkout from the mirror bundle is byte-identical to one from GitHub), `LICENSE.txt`, `sdk/LICENSE.txt`, `sdk/CMakeLists.txt`, `sdk/toolchain.cmake`, `sdk/include`, `sdk/src/components/fsr3upscaler`, `sdk/src/backends/dx12`, `sdk/src/backends/shared`, `sdk/src/shared` and `sdk/tools/binary_store`. Against the list below, measured at stage 0: `sdk/src/backends/shared` (the shader blob accessors the backend compiles, 356 KB) is added, and `sdk/tools/ffx_shader_compiler` (the source of the compiler driver, whose build is in `binary_store`) and `sdk/libs` (PIX and AntiLag 2) are not fetched. The list as planned:
  - `sdk/include` (the host and GPU headers: `FidelityFX/host/ffx_fsr3upscaler.h`, the DX12 backend's header, the shaders' `gpu/fsr3upscaler`),
  - `sdk/src/components/fsr3upscaler` (`ffx_fsr3upscaler.cpp`, 91,833 bytes), `sdk/src/backends/dx12` and `sdk/src/shared`,
  - `sdk/tools/ffx_shader_compiler` and `sdk/tools/binary_store`, which holds the SDK's shader compiler `FidelityFX_SC.exe` (474,624 bytes) with `dxcompiler.dll` (17,802,672 bytes) and `dxil.dll` (1,463,736 bytes), so no separate DXC download is needed; the same folder holds `D3D12Core.dll`, `d3d12SDKLayers.dll`, `d3dconfig.exe` and `glslangValidator.exe`, which the build does not use,
  - `sdk/CMakeLists.txt` and `sdk/toolchain.cmake`.
- **Not fetched**: `samples`, `framework`, `docs`, `ffx-api`, `PrebuiltSignedDLL` (the SDK's own signed DLLs) and the samples' media (`UpdateMedia.bat`).
- **Size**: the whole repository is about 411 MB (GitHub's figure, with the samples' media); the sparse checkout measures 39 MB of files, of which 34 MB are the binaries of `sdk/tools/binary_store`, and 36 MB of Git objects.
- **Where**: outside the repository, into the BuildRoot (`<BuildRoot>\fidelityfx-sdk`), as the runtime's own upstream clone; only this project's integration code goes into Git, with AMD's notice under `outputs/FM4-source-control/third-party-notices`.

## 11. Stage 3, upscaling on N5's scales (25 October 2026)

The scene renders at an input scale s_in below the output scale s_out, and FSR 3.1 upscales it at the scene_color point (section 9.3). The native path in one pass with 2x MSAA only (`-Native`, `native_rt_surfaces=58`); RTV keeps FSR's native AA and `-Rov` is untouched. The main session approved the design on 25 October 2026.

### 11.1 The two scene scales

- **The cvars**: s_out is `native_draw_scale` (the screen's own scale, 18.19); s_in is `native_scene_input_scale` (a rational from 1 to s_out). Unset, or equal to s_out, renders as before. The runtime refuses s_in above s_out, and s_in without `native_one_pass_msaa` and bit 16 of `native_rt_surfaces`.
- **`GetKeyScale`** picks by key: the MSAA private surfaces (the 2x scene colour at base 0 and its depth at 1024) at s_in, the one-sample private surfaces (the post chain's colour at base 0, the final picture and its depth at 720) at s_out, every other surface at k.
- **k = floor(s_in)**, the main session's decision of 25 October 2026: the shadow maps, the environment cube, the rear-view mirror and the small destinations cost what the input scale costs. The visible cost: at 4K performance (s_in 3/2, s_out 3) k is 1, so the mirror and the shadows are at 1x there. On the ROG Xbox Ally X (s_out 3/2 from s_in 1) k is 1, as at 3/2 today.
- **s_in is at least 1**, the main session's decision of 25 October 2026: N5 renders no scene below 1x, so a mode whose input falls below 720 lines is clamped to 720p, and the launchers say so ("clamped to 720p").
- **The scene draws** take the scale of their bound surfaces: the viewport, the scissor, the occlusion normalization (the occlusion queries' draws go into the MSAA scene, at s_in) and the translation. A pixel shader drawing into the MSAA surfaces while upscaling is translated with a second modification bit, `scene_input_scale`, beside `scene_scale`, so a shader is translated at most once per scale; without upscaling the bit stays 0 and every existing translation is unchanged. The draw back end carries it as `kFlagSceneInput`. Every draw into the private surfaces is a scene draw while upscaling, also at whole scales (s_out 3 with k 2), and the uniform draws (the clears of the render target cache) are off.
- **The places that took "a whole scale" for "the other surfaces' scale"** (the size of a new render target, a resolve clear's rectangle, a resolve source's offsets and scale, the sized targets, the EDRAM dump of a private surface) compare with k instead, which is the same for every scale without upscaling.

### 11.2 The texture cache's scale class

- The texture key's `scene_scaled` bit gets a second one, `scene_input`: a scene texture at s_in. `SceneTextureHostWidth` and `SceneTextureHostHeight` take it, and a second page map beside the scene-resolved pages says which ranges were resolved at s_in.
- A full-screen resolve of an MSAA private surface while upscaling marks its range at s_in and writes the s_in textures; one of a one-sample private surface (the post chain) marks it at s_out. Small destinations resolve down to k with the box filter from either.
- **The write-back** of FSR's output (`fsr_scene_out`, of the new manifest size `output`, the scene at s_out) marks the scene colour's range at s_out before the copy and drops the texture bindings, so the game's next fetches bind the s_out texture: the title's motion blur, bloom, exposure and final combine sample the upscaled scene. The next resolve of the range marks it at s_in again.

### 11.3 Reads of the scene range (the audit before FSR's two sizes)

The main session asked, before FSR's two sizes, for a list of the shaders that read the scene's range in unnormalized coordinates or through vertex fetches, since a read like that would need the s_in texture or a scaled variant. `native_scene_read_trace` (a diagnostic cvar) logs every fetch from a scene-scaled texture once per shader, fetch constant, opcode, coordinates, offsets, texture class and draw class, and every vertex fetch from a scene-resolved range. Run on 25 October 2026 with the scene upscaled from 720 to 1080 lines (s_in 1, s_out 3/2) in `photo` (the race, photo mode and its menus) and `career` (a career race, its replay and the post-race menus):

- **No unnormalized fetch and no vertex fetch** of the scene's range in either run. Every read of the scene colour (`1B811000`), of the post chain's full-screen destinations (`1B479000`, `1B0E1000`) and of the scene depth's views (`1C0C1000`) is a normalized `tfetch2D` or a `getGradients`, so a texture of either class is sampled at the same place.
- **Two fetches carry texel offsets**, which the translator divides by the draw's scale (`draw_resolution_scaled_texture_offsets`, on by default):
  - pixel shader `6686E1B8EC8D0195`, offset (0.5, 0.5), reads the scene depth from a scene draw into the MSAA surfaces: both at s_in, so it matches;
  - pixel shader `BCFAE662E7F38473`, offset (1, 1), reads the scene colour from a post chain draw at s_out. After FSR's write-back the colour is at s_out and it matches. In frames without the write-back (before the first scene with a camera) it reads the s_in texture with an offset meant for s_out: two thirds of a texel at 1080p from 720p, a slightly narrower filter tap. Accepted.
- Scene draws at s_in read the previous frame's upscaled scene colour (six pixel shaders, `1D1DBE92CED7E7D7`, `6975A8589117B443`, `B85EF04779ABD438`, `C5862561EE9EC6CB`, `D5B19764A94E4929`, `E62CFDF09D758B6C`), and draws at k (the small destinations, bloom) read both classes; all normalized.

So no shader needs the s_in texture or a scaled variant. The photo mode, replay and menu behaviour:
- **Races and replays** are upscaled: the career replay runs FSR at 1280x720 to 1920x1080 as the race does (0.42 ms a frame).
- **Photo mode** renders its tiles at k = floor(s_in) and runs no scene passes (18.17), so no FSR, then is scaled to the screen: at 1080p from 720p as before (k 1), at 4K quality at 2x instead of native's 3x.
- **The menus and the pre-race screens** render at k (entry 0 at s, 18.20, is still to come) without FSR.

### 11.4 FSR's two sizes and the jitter

- `fsr_prepare` runs at the scene's size (s_in), `fsr_upscale` with that render size and the output size (`fsr_output`'s texture, size `output`), `fsr_output` at the output size, taking the game's alpha from the scene pixel under each output pixel.
- The post passes take the scene depth at s_in while upscaling. The final point sizes its passes from the final picture at s_out, also when s_out is whole (1440p and 4K: a first build took the whole s_out for the other surfaces' k and ran the windshield pass in the top left 1280x720 of a 2560x1440 picture, which the final point's size check reported); the check compares the final picture with the output size.
- The windshield pass already maps each final pixel to the scene pixel by the ratio of the sizes (`windshield.hlsl`).
- **The jitter phases**: 8 ratio^2 while upscaling (FSR's `ffxFsr3UpscalerGetJitterPhaseCount`, from the exact ratio of the two scales: 18 at 1.5, 23 at 1.7, 32 at 2, at most 64), the manifest's 8 at s_in = s_out, so stages 1 and 2 stay as they were.

### 11.5 The launchers' knob

`-Upscale native|quality|balanced|performance|<ratio>` (`Build.ps1`, `pipeline.py --upscale`, the GUI launcher's Upscale choice on the Display page); native, the default, changes nothing. A mode renders `round(output lines / ratio)` lines, at least 720, the other surfaces at its whole part:

| Output | native | quality (1.5) | balanced (1.7) | performance (2) |
|---|---|---|---|---|
| 1080p (s_out 3/2) | 1080 | 720 (1) | 720 (clamped from 635) | 720 (clamped from 540) |
| 1440p (s_out 2) | 1440 | 960 (4/3) | 847 (847/720) | 720 (1) |
| 2160p (s_out 3) | 2160 | 1440 (2) | 1271 (1271/720) | 1080 (3/2) |

A ratio is the knob's number form: `-Upscale 1.25` renders 864 lines (6/5) for 1080p. The launch description adds "FSR quality 960p -> 1440p", with "(clamped to 720p)" where the input was raised to 720; at a 720p output every mode renders native ("upscale quality: none at 720p (clamped to 720p)"). The GUI shows the input and output lines beside the choice ("960p → 1440p"). With integrated GPUs only, an upscale mode takes the screen's own scale as its output (the Ally X's 1080p from 720p), where they otherwise render at 1x. Both launchers refuse an upscale mode with `aa=taa` ("-Upscale quality needs aa=fsr in -Look: TAA does not upscale") and off the native path in one pass with 2x MSAA.

### 11.6 Later: a dynamic s_in

Not in stage 3: an input scale that changes per frame (dynamic resolution). The s_in plumbing should not prevent it: the translation variant and the surfaces' sizes depend on s_in today, so a dynamic s_in would render into surfaces at the largest s_in with a per-frame viewport, and FSR takes a per-frame render size within its maximum.

### 11.7 The gate (25 October 2026)

On `I:\FM4-build-usa`, the game of stage 2's `game-build-fsr` with the runtime of the steps (built with `REX_FSR` on in `fsr-gate`), `-Native`, `native_rt_surfaces=58`, `aces,aa=fsr`, the title's AI driving, a fixed seed, at 60 Hz.

**No change at s_in = s_out** (the frozen frame, `parity.py` against stage 2's runtime):

| Case | Step 1 | Step 2 | Steps 1 to 3 |
|---|---|---|---|
| `-Native` 3/2, `aces,taa=0` | not run (the candidate timed out in the rewind menu) | PASS (draws +0.0%, 18.8% of the image above 4) | PASS (-0.1%, 28.6%) |
| `-Native` 3/2, `aces` (TAA) | | PASS (-0.7%, 22.1%) | PASS (-0.7%, 26.9%) |
| `-Native` 9/4 at 2, `aces,taa=0` | | | PASS (-0.0%, 26.2%) |
| RTV 2x, `aces` | PASS (-0.0%, 29.4%) | PASS (+0.6%, 22.0%) | PASS (+0.5%, 39.1%) |
| `-Rov` 1x, `original,taa=0` | PASS (+0.1%, 32.1%) | PASS (+0.1%, 31.2%) | PASS (+0.0%, 24.7%) |

FSR jitters every frame, so a frozen frame with `aa=fsr` does not hold still (its own captures differ in 0.3% of the pixels, above the 0.05% limit); the `-Native` checks run without temporal AA (`aces,taa=0`) and with TAA (`aces`). Draws are within 0.7% and the predicated packets within 1.8%; the image shares are within the variance of two runs of one binary (18% to 41%).

**GPU time a race frame** (the median of the race phase's windows; the `fsr` group is `fsr_prepare`, FSR and `fsr_output`):

| Output | Mode | Input (k) | Frame | FSR | Scene |
|---|---|---|---|---|---|
| 1080p | native | 1080 (1) | 4.18 ms | 0.68 ms | 2.84 ms |
| 1080p | quality | 720 (1) | 3.27 ms | 0.43 ms | 2.41 ms |
| 1440p | native | 1440 (2) | 5.80 ms | 1.09 ms | 3.49 ms |
| 1440p | quality | 960 (1) | 4.26 ms | 0.72 ms | 2.83 ms |
| 1440p | balanced | 847 (1) | 4.02 ms | 0.66 ms | 2.72 ms |
| 1440p | performance | 720 (1) | 3.69 ms | 0.61 ms | 2.52 ms |
| 2160p | native | 2160 (3) | 12.19 ms | 2.59 ms | 6.57 ms |
| 2160p | quality | 1440 (2) | 8.76 ms | 1.66 ms | 5.44 ms |
| 2160p | balanced | 1271 (1) | 6.76 ms | 1.51 ms | 3.84 ms |
| 2160p | performance | 1080 (1) | 6.22 ms | 1.37 ms | 3.56 ms |

FSR's own cost follows the output size more than the input (1.37 to 1.66 ms at 2160p); the savings come from the scene and, where k drops, from the shadow maps and the mirror (2160p balanced and performance at k 1).

**Pictures** (1:1 crops of the frozen frame from full-resolution captures, native against quality at each output; the crops stay outside the repository):
- 1080p from 720p, 1440p from 960p and 2160p from 1440p keep the cable car's wires and the pylon's lattice; the upscaled ones are a little softer, most at 1080p from 720p, where far wires break up in places. The grandstand's crowd is alike. The HUD is identical (drawn at s_out after the scene).
- The rear-view mirror renders at k, so it is softer where k drops (1440p quality at k 1 against native's 2; 2160p quality at k 2 against 3), the cost the main session accepted with k = floor(s_in).
- The clock in the instrument cluster looks hazy in every upscaled mode and not at ratio 1. The first guess here, FSR's reactive mask, was wrong; the cause is the texture mips (section 12.1), fixed after the gate.
- A whole upscaled race frame and photo mode at 1080p from 720p look complete, with the HUD sharp.

**Runs**: the upscale smoke (`s3-up32from1-photo`, `-career`: race, photo mode, the replay) reached its goal with the error keys of stage 2's runs; the scene read trace is section 11.3. The final point's warning "no single-sampled color render target" (logged once) is explained in section 12.2.

## 12. After stage 3 (25 October 2026)

### 12.1 The instrument cluster's haze: the texture mips

In every upscale mode the dials of the instrument cluster (the clock most, its numerals and ticks) looked hazy and doubled against native rendering, while the hands, which are geometry, stayed sharp. The causes the main session listed, checked:

- **Not FSR's reactive or transparency handling.** The cluster is the cockpit interior: the motion pass classifies it as the interior (its own motion, `motion_mask` 0 in both channels), so neither FSR's reactive mask (G marks only the world seen through car glass) nor its transparency and composition mask (R marks pixels without known motion) covers it.
- **Not a scene texture's class, nor the scene colour.** The dials are ordinary textures, not scene-resolved ranges (the scene read trace of 11.3 lists no read of them), and the cluster draws are scene draws into the MSAA surfaces like the rest of the interior.
- **The texture mips.** A scene draw at s_in has larger screen-space derivatives than at s_out, so the hardware picks a coarser mip of every texture, and FSR upscales that coarser picture. At ratio 1 the mips are those of the output. FSR's documentation asks for a negative mip bias while upscaling for this reason.

The fix: the computed-LOD fetches of the scene draws at the input scale (the `scene_input_scale` translations) take a LOD bias of log2(s_in / s_out), the mips the output scale would pick: -0.585 at 1080p from 720p and 1440p from 960p, -1 at performance. The DXBC translator adds it before the gradients' scale; the shader override library takes it as `XE_SCENE_LOD_BIAS`, defined only for those translations. Nothing changes at s_in = s_out or off the native path (the bias is 0 and no instruction or define is emitted). `native_scene_input_lod_bias` overrides it with a number of mip levels (0: none). FSR's documentation also suggests one level more (log2(render / display) - 1, -1.585 at quality); at 1440p from 960p that sharpened the ticks a little more in the frozen frame, but it would sharpen beyond native rendering and, at ratio 1, change stages 1 and 2, so the default stays log2(s_in / s_out).

Checked on the frozen frame (`aces,aa=fsr`, the build of 12.3), 1:1 crops of native, quality before and quality with the bias: at 1080p from 720p the clock's dial, numerals and ticks match native's again, and the other dials and the grandstand's crowd are sharper than before; at 1440p from 960p the dials' small print (VEGLIA, ELECTRONIC) is sharper. GPU time a race frame at 1440p quality: 4.51 ms against 4.26 ms before (one run each; native 5.80 ms).

### 12.2 The final point's skipped frames

The warning "no single-sampled color render target at the final point" came once per run, in runs before stage 3 too (stage 2's `-Native` 1x and 2x, RTV 2x). With the frame and the target logged: from the first frame with a scene camera the title's final combine shader draws, for 125 frames in a row, into a 4x target at EDRAM base 0 (`2_10_10_10`, the guest viewport 1280x720): the pre-race screens with the car, which use the title's tiling entry 0 (4x tiles). The final point's passes (the windshield, TAA at the final point) need the single-sampled final picture, so they skip those frames; the race's frames and photo mode run them. Harmless, and expected until entry 0 renders in one pass ("entry 0 at s"). The warning is now an information line once per stretch of such frames, with the target, and another when the final point runs again with the number of frames skipped.

### 12.3 The checks

On the build after 3f28619 with 12.1 and 12.2 (`game-build-s4`):

- No change at s_in = s_out: the frozen frame's parity against stage 2's runtime on `-Native` 3/2 without TAA (draws +0.2%, 39.0% of the image above 4) and on RTV 2x with `aces` (+0.1%, 33.1%): PASS.
- `photo` at 1080p from 720p reached its goal with no new error key; the final point logs one stretch of 123 skipped frames (the pre-race screens) and runs from frame 2,762 on.
- The CPU tests: `fm4_shader_mod_tests` compiles the override library with the bias define's `#ifdef`; the post pass shader tests pass with FSR's checks on the hardware adapter.


### 12.4 The owner's ROG Xbox Ally X result for upscaling

The export of 5c346c9 (logs in `D:\log_ally_5`) on the Radeon 890M at 117 Hz, the look `aces`, a Quick Race, the medians of the race windows (the owner's runs, relayed by the main session on 25 October 2026):

| Run | Setting | fps | GPU a frame | Scene | The AA pass | `gtao` | `windshield` |
|---|---|---|---|---|---|---|---|
| 1 | 720p with TAA (the default with integrated GPUs) | 103.0 | 8.82 ms | 6.39 ms | `taa` 0.54 ms | 0.90 ms | 0.53 ms |
| 2 | 1080p native with TAA | 56.9 | 17.14 ms | 11.93 ms | `taa` 1.14 ms | 1.76 ms | 1.01 ms |
| 3 | 720 to 1080 with FSR quality | 64.6 | 15.15 ms | 9.07 ms | `fsr` 3.00 ms | 1.08 ms | 1.06 ms |
| 4 | 720p with FSR's native AA | 83.5 | 11.44 ms | 6.70 ms | `fsr` 2.54 ms | 1.17 ms | 0.49 ms |

Upscaling gains 14% over native 1080p there (15.15 against 17.14 ms). Where run 3's time goes against run 1, from the same perf files (the windows of the race after the start, medians; `scene` is the frame less the passes and the swap, so it holds the guest's draws, resolves, ownership transfers and texture loads; the medians do not add up exactly):

| Part | Run 1 | Run 3 | Run 3 less run 1 |
|---|---|---|---|
| GPU a frame | 8.30 ms | 13.93 ms | +5.63 ms |
| `fsr` against `taa` | 0.53 ms | 2.95 ms | +2.42 ms |
| Full-screen resolves (3 a frame) | 0.75 ms (0.25 each) | 2.63 ms (0.88 each) | +1.88 ms |
| Smaller resolves | 0.33 ms | 0.65 ms | +0.32 ms |
| Ownership transfers | 0.13 ms | 1.01 ms | +0.88 ms |
| Texture loads | 1.07 ms | 0.16 ms | -0.91 ms |
| `windshield` (the final point, at s_out) | 0.64 ms | 1.10 ms | +0.46 ms |
| `gtao`, `motion` | 1.26 ms | 1.27 ms | +0.01 ms |
| The guest's draws (the scene less resolves, transfers and loads) | about 3.6 ms | about 3.9 ms | about +0.4 ms |

The guest's own post chain and HUD at 1080 cost little more than at 720; the extra time is FSR, the full-screen resolves and the transfers at the output scale, and the windshield pass. At 1x the full-screen resolves go into guest memory and the textures are loaded after (0.25 ms a resolve and 1.07 ms of loads); with the scene at a scale they write the host textures straight (0.88 ms a resolve at 1080 from 720, 1.51 ms at 1080 native, run 2).

## 13. Uniform draws off the whole scales, and RCAS with the ratio (26 October 2026)

Two items of the integrated GPU proposal after 12.4, approved by the main session on 26 October 2026: (e2) remove the ownership transfers that the fractional scales and upscaling cost, and (f) sharpen an upscaled picture more than a native one.

### 13.1 The uniform draws

`IssueDraw` turns clears and draws that overwrite their whole rectangle with uniform values into uniform draws: the render target cache takes the tiles they cover without transferring their old owner's data first, and a 4x clear that the next draw at the same place reads with fewer samples is drawn at that sample count (half the pitch, `GetUniformDrawNextMsaa`). Until now it did that only at a whole scene scale with the 16:9 logical size, and since stage 3 not while upscaling, because the private surfaces are at the scene's scale there and the others at its whole part. So at 3/2 (the 1080p default of the native path), at a logical size and in every upscale mode, every such clear pulled in the tiles' old owner first. The transfer inventory in `race` with TAA at 3/2: 37.5 transfers and 5,760 tiles a frame, where 2x has 15.5 and 1,006; 2x with `rtv_uniform_draws=false` has the kinds of 3/2.

The change: off the whole scales, at a logical size and while upscaling, uniform draws are taken for every draw that binds no private surface (`RenderTargetCache::DrawBindsPrivateSurface`, the keys `Update` would make private, checked before it runs). Where the 4x clear's lower sample count would name a private surface, the clear stays a uniform draw at its own 4x instead. Uniform draws into the private surfaces themselves stay off there: none of the remaining transfers is into one, so their bounds at the scene's scale are not needed. Nothing changes at the whole scales with 16:9, so RTV, `-Rov` and `-Native` at 1x and 2x run the same code as before.

The gate (`I:\FM4-build-usa`, `-Native` with `native_rt_surfaces=58`, `race` at 60 Hz with the transfer inventory, one run each; the baseline `game-build-e2base` is the runtime before the change, the candidate `game-build-quirky`):

| Run | Tiles transferred a frame | `transfer` GPU ms | GPU a frame |
|---|---|---|---|
| 3/2, `aces` | 5,760 → 2,382 | 0.32 → 0.10 | 3.66 → 3.48 |
| 9/4, `aces` (2880x1620) | 5,760 → 2,382 | 1.05 → 0.26 | 6.98 → 6.17 |
| 1440p quality (960 to 1440 lines), `aces,aa=fsr` | 5,760 → 2,382 | 0.31 → 0.10 | 4.25 → 4.04 |

The first version, which dropped the 4x clears whose lower sample count names a private surface, kept 4,766 tiles at 3/2 (0.22 ms). The 2,382 tiles left are mostly the cars' shadow map (the 4x colour at base 832, pitch 13) and the 4x depth at base 0, pitch 13 taking tiles of the scene's surfaces, which 2x does not transfer either way. On the 890M the transfers took 1.0 ms a frame at 1080p native and from 720 (12.4), so the saving there should be several times this PC's, at native 1080p as well as upscaled; the next export measures it.

The frozen frame against the baseline (`parity.py`): `-Native` 3/2 without TAA passes (draws +0.6%, 34.5% of the image above 4), and so do RTV 2x with `aces` (-0.1%, 29.0%) and `-Rov` 1x with `original,taa=0` (+1.2%, 32.6%). Under upscaling FSR keeps changing the frozen frame (its own captures differ in 0.4 to 0.8% of pixels on both sides, so `parity.py` reports FAIL on its freeze check alone), so the candidate is judged against the run-to-run range: at 1080p from 720p two baseline runs differ in 36.3% of the image above 4, the candidate against them in 23.5% and 36.3%, with the draws within 0.1%; at 1440p quality 25.9%, the draws +0.1%. In 1:1 crops at 1080p quality the gauges' print, the grandstand and the HUD are as sharp as the baseline's. The 240 Hz smoke at 3/2 (`photo`, `career`) reached its goals with the baseline's error keys: `photo` raced at 237.7 fps (p99 4.97 ms) with photo mode at 30.0, `career` at 236.0 fps (p99 5.28 ms) with the replay at 119.4. The CPU tests pass: `test_pipeline` and the launcher tests (351 plans).

### 13.2 RCAS with the ratio

FSR's RCAS sharpening was the look's `sharpen` at every ratio. In 1:1 crops of the frozen frame at 1080p from 720p against native 1080p, 0.6 brings the grandstand and the gauges' print close to native's crispness, 0.8 starts to over-sharpen the crowd. `FSR_SHARPNESS` is now the look's `sharpen` plus 0.4 times the ratio above 1, at most 1: with the presets' 0.4, 0.5 at 1.25, 0.6 at quality, 0.68 at balanced and 0.8 at performance, and nothing added at ratio 1 (FSR's native anti-aliasing is unchanged). The ratio is the output's lines over the input's (`scene_upscale_ratio` in `pipeline.py`, `SceneUpscaleRatio` in the launcher). The look knob `fsr_sharpen` (0 to 1) sets it instead, for the owner's A/B. The `-Info` overlay shows the strength in use.

## Related

- [TAA](taa.md), [motion vectors](motion-vectors.md#6-requirements-for-taa-and-the-upscalers), [post pass framework](post-pass-framework.md)
- [Sources and licensing](../sources-and-licensing.md#third-party-code-amd-fidelityfx-sdk-fsr-31)
