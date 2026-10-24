# Native port, stage N5: native resolution and aspect

Section 18 of the [D3D-level HLE design](d3d-hle.md). Sections 1 to 15 are in [d3d-hle.md](d3d-hle.md), section 16 (stage N3) in [d3d-hle-n3.md](d3d-hle-n3.md) and section 17 (stage N4) in [d3d-hle-n4.md](d3d-hle-n4.md).

## 18. Native resolution and aspect, design, 24 October 2026

### 18.1 The requirement

The project owner's requirement of 24 October 2026: the game renders at the panel's own resolution, from 1920x1080 (the ROG Xbox Ally X's panel) through 2K up to 4K, and a fullscreen that truly fills the panel, widescreen included: 16:10 (this laptop's 2560x1600) and 21:9. It becomes N5's first priority.

Today the render scale is a whole multiple of 1280x720, the smallest that covers the picture (`plan.cpp`: `ceil(min(w / 1280, h / 720))`), and the picture is always 16:9: a 1080p panel gets 2x, rendered at 2560x1440 and downscaled at presentation, and a 2560x1600 panel gets 2560x1440 with bars of 80 lines ([history](../status/history.md)). The title renders 1280x720 in guest terms, as on the console, where the scaler made 1080p.

The Ally X's numbers from the user (run 7 of the [Ally X run set](../running.md#ally-x-run-set), the native path with one pass 2x): at render scale 2x a race runs at 39 fps on AC power with 24.2 ms of GPU time a frame, and at 34 fps on battery with 27.6 ms; at 1x at 109 and 89 fps with 7.9 and 9.9 ms.

### 18.2 The approved design

The project owner approved a hybrid on 24 October 2026:

- **Resolution** comes from one uniform host draw scale on the native path, a rational number: the panel's height over 720. That is 3/2 for 1080 lines, 2 for 1440, 20/9 (2.222) for 1600 and 3 for 2160.
- **Aspect** comes from the title's logical width at 720 lines: 1280 for 16:9, 1152 for 16:10, about 1720 for 21:9 (3440x1440 at scale 2). A title patch widens the scene's back buffer, the one-pass rectangle, the viewports and scissors, and hooks the projection (Hor+, the vertical field of view kept), the culling and the HUD.
- RTV and `-Rov` stay at integer scales and 16:9: they are the references.
- Rendering the full pixel size in the title (option B, 18.6) is a fallback that was evaluated, not the default. An upscale from a smaller render (option C, 18.7) is not native; it stays N5's upscaler item.

| Panel | Scale | Logical width | Host scene |
|---|---|---|---|
| 1920x1080 (16:9, the Ally X) | 3/2 | 1280 | 1920x1080 |
| 2560x1440 (16:9) | 2 | 1280 | 2560x1440 |
| 2560x1600 (16:10, this laptop) | 20/9 | 1152 | 2560x1600 |
| 3840x2160 (16:9) | 3 | 1280 | 3840x2160 |
| 2560x1080 (21:9) | 3/2 | 1707 | 2560x1080 |
| 3440x1440 (21:9) | 2 | 1720 | 3440x1440 |

### 18.3 Where the integer scale lives

The probe of 24 October 2026 read the runtime for every use of `draw_resolution_scale_x` and `draw_resolution_scale_y`. The scale is an integer end to end. The render target cache's header says so on purpose (`render_target/cache.h:129`): fractional scales are left out because of pixel coverage, with 1.5 as its example (18.4).

| Area | The integer assumption | On the native path with a rational scale |
|---|---|---|
| Configuration | `int32` cvars of range 1 to 8, clamped to 7 (`texture/cache.cpp:67`, `:241`); `uint32_t` members; 3-bit scale fields in the resolve constants (`draw.h:375`); a device clamp that steps the scale down by 1 (`d3d12/texture_cache.cpp:1135`) | a numerator and denominator, uniform in x and y; the integer path keeps its types and code, so integer scales stay bit-identical |
| Render target cache, EDRAM | host target = pitch times scale (`d3d12/render_target_cache.cpp:1954`); tiles of 80 by 16 samples times the scale (`:2873`); the 4x pixel size as a shift (`render_target/cache.cpp:1647`); scissor scales cast to `int32_t` in the overwrite cutout and the lazy transfers (`:909`, `:966`), which would make 1.5 act as 1 | a tile is whole at 3/2 (120x24) and at any k/16, but not at 20/9 (177.8x35.6). The scene's colour and depth become private host targets at every MSAA level, as the 4x scene's are (16.17), so they need no tile grid; the other surfaces keep an integer scale (18.4) |
| Texture cache, scaled resolves | the scaled resolve memory places each guest byte at its address times sx times sy (`d3d12/texture_cache.cpp:1179`, `resolve.xesli:217`), which has no fractional layout | resolves of the scene write host textures of the scaled size directly, from the host render target (16.8's texture write, 16.11's host resolve); the readback on demand gets a downscale to the guest size |
| Resolve shaders | the half-pixel fill exists for 2x and 3x only (`resolve_full_*bpp.xesli`, `resolve_host_32bpp.xesli`); `texture_address.xesli:189` and `resolve_downscale.cs.hlsl:77` group host pixels per guest pixel in integers | new variants for the private scene: host target to host texture at the same scale, and a downscale to the guest's layout for readbacks |
| Shader translator | the pixel position and the fetch offsets are already float (`dxbc_translator.cpp:661`, `dxbc_translator_fetch.cpp:1046`); memexport picks the host pixel with an integer divide (`dxbc_translator.cpp:727`) | the float paths take the rational scale; memexport picks the host pixel that contains the guest pixel's centre. The ROV output merger (`dxbc_translator_om.cpp`) is not on the native path |
| Viewport and scissor | `ViewportInfo` offsets and extents are host pixels in `uint32_t`, from guest pixels truncated and multiplied (`draw.cpp:337`, `:380`); scissors in `uint32_t` (`draw.h:282`) | D3D12 viewports are float; scissors round both edges by the top-left rule from the scaled guest edges |
| Occlusion queries | samples normalized by `(samples + scale / 2) / scale` (`command_processor.cpp:7640`) | divided by the scale's square as a float, so the sun's glare query keeps its guest sample count |
| Post passes | sizes come from the host textures, fraction-safe (`post_pass.cpp:1498`, `:1958`); the final size is the guest width times the integer scale (`post_pass.cpp:2138`) | the final size from the rational scale. TAA passes the frame through without a message when the final size differs from the scene's (`taa.hlsl:198`); that becomes a logged error before the scale work, so it cannot hide a mismatch |
| TAA, GTAO, motion vectors | the jitter (±0.5 px), TAA's thresholds (`TAA_STILL_PIXELS` 0.05, `TAA_MOTION_PIXELS` 16) and the motion vectors are in host pixels; GTAO's radius is computed per frame from the scene's width (`post_pass.cpp:2098`) | GTAO and the motion vectors need nothing; TAA's thresholds are retuned or expressed in guest pixels, so 1.5x does not behave between 1x and 2x by accident |
| Look mods | `XE_RESOLUTION_SCALE_X` and `_Y` are integer macros (`shader_override.cpp:641`), used for the offset reversal and the parameter generation (`shader_override.hlsli:293`, `:669`); no look file has a 1280 or 720 constant | float macros. The sharpen kernel steps one guest texel, 1.5 host pixels at 1.5x |
| Swap and presentation | the swap texture has the scaled size, and the one resample is at presentation (`presenter.cpp:549`, letterboxed by the guest mode's aspect at `:910`) | at the panel's own scale the presentation copies 1:1; with a logical width other than 1280 the guest mode reports the logical size, so the letterbox does not add bars |
| Launcher | the integer scale of `plan.cpp:481` and integer cvars (`:500`) | the panel's scale and logical width (increment 3) |

### 18.4 The coverage risk and the scale per resolve destination

The reason the header gives: at 1.5x, a one-pixel quad (the last step of a reduction such as the average luminance) covers host pixels 0 to 1.5 or 0.75 to 2.25, so by the top-left rule it covers only the first or only the second host pixel. Xenia's fix for the half-pixel offset at 2x and 3x, stretching the second column into the first, would erase such a pixel at 1.5x, and within one pass the emulator cannot tell which draws need it.

The design avoids the problem instead of fixing it: only the scene-sized surfaces render at the rational scale. Everything else renders at the integer scale k = floor(s): 1 at 3/2, 2 at 20/9, 3 at 3. The approved note drew that line per surface; the frame graph below moved it to the resolve destinations, because the reductions and the bloom chain are drawn into the scene's own surfaces. A pass that reads a scene-sized texture into a small target samples it in normalized coordinates, so the scale of its source does not matter to it. The inventory of a race frame by size sets the threshold (the post passes already use a guest width of at least 1024 for the scene, `post_pass.cpp:1252`). The list includes the title's full-screen resolves that feed its post chain (bloom, depth of field, motion blur), so that none of them drops to k without notice and comes back upscaled.

**The frame graph of a race in one pass (24 October 2026)** changed the rule from a scale per surface to a scale per resolve destination (`n5a-fg-nat1x`, `-Native` with one pass 2x at 1x, frame 15000: 26 resolves). The title draws its reductions and its bloom chain into small viewports of the scene's own 1280-wide surfaces and resolves those regions:

| Surfaces (guest pitch, MSAA) | What they hold | Resolves |
|---|---|---|
| 1280, 2x, colour at 0 and the private depth at 1024 | the scene | the depth and the colour, 1280x720 |
| 1280, 1x, colour at 0 (the post chain) and the final colour | the post passes, the bloom chain and the luminance reduction in small viewports, the final picture | 320x192, 160x192, 160x96, 96x96, 96x64, 64x64, 64x32 and 32x32 regions; the final 1280x720 |
| 640 and 520 with 4x, 560 | the shadow maps | 512x512 (`8_8_8_8`, `16_16`, `8_8`) |
| 320 | the reflections and their mip chain | 256x256 down to 32x32 |
| 80 | the luminance (`32_FLOAT`) | 32x32 |

So the rule is per resolve destination, decided by the main session on 24 October 2026:

- The scene-sized surfaces (the 1280-wide ones) render at s, including the post chain's draws into their small regions, with viewports and scissors through `DrawScale::ScaleEdge`.
- A resolve to a full-screen destination keeps s.
- A resolve to a smaller destination is resolved down to the integer scale k, so the texture the next pass samples has whole sizes (at 20/9 a 320x192 region would be 711.1 host pixels wide) and the reductions end on whole host pixels.
- Separate small surfaces (the shadow maps, the reflections, the luminance) render at k.

What follows from it:

- The scene's colour and depth are private host targets at every MSAA level, outside the tile map, as the 4x scene is (16.17). Their aliases in EDRAM, such as the shadow map's rows under the 2x scene colour (16.17), become explicit copies between the two scales.
- A resolve from the scene into a texture keeps the scene's scale in the texture cache's key (a scale value instead of today's scaled bit); a resolve from a small surface keeps its integer scale.
- The resolve byte check compares bytes at the guest's layout, which a fractional scale does not have. At 3/2 it compares the readback's downscale against the same resolve of a 1x run where the scene is frozen, and otherwise it is exempt for the scene's resolves, which the run's log says; the surfaces at the integer scale k keep the check as it is.
- A texture read with texel offsets or unnormalized coordinates from a scene-scaled texture is the remaining risk. The translator's offset reversal is float already, and the check mode compares the 1.5x frame with the 1x and 2x frames at 1:1.

### 18.5 Aspect: the logical width

The title renders 1280 guest pixels wide. The probe found nothing in the title hooks that sets the projection, the field of view, the culling or the HUD's layout; all of them are new work:

- **The scene's size.** The one-pass rectangle of the tiling table (`0x82D883C8`, already patched for one pass) becomes the logical width, and the scene's pitch is rounded up to whole tiles (1152 to 1200, 1720 to 1760). The title's viewports and scissors (observed at `SetViewport` `sub_822FEAA8` and `SetScissorRect` `sub_822FF050`) and its full-screen resolves of 1280 are widened, and so are the guest textures that receive them (the post chain); where the title takes those sizes from is to be found (the back buffer's description, or `Media\RenderScenarios`).
- **The guest video mode.** `VdQueryVideoMode` (`xboxkrnl_video.cpp:231`) and the GPU registers `0x194C` and `0x1961` report the mode's size; the title's readers of them are not known. The mode can report the logical size, so the title sees a wider screen where it asks.
- **The projection (Hor+).** The cameras' fields of view are in `Media\Camera\CameraSettings.ini` (for example the hood camera 66, the driver camera 62); the measured projections show a horizontal field of view of 93 to 104 degrees in the chase and cockpit cameras ([GTAO design](gtao-design.md)). Hor+ keeps the vertical field of view and widens the horizontal one by the logical width over 1280, where the title builds its projection (address to be found). A vertex shader override of the projection constants (`c128` to `c131` of the road shader and their equivalents, [section 8.2](d3d-hle.md#82-shaders-on-the-native-path)) is only the fallback: it touches every shader that reads those registers, while the mirror, the shadows and the cube maps keep their own projections.
- **The culling.** The title culls against its own frustum, so objects at the widened edges would pop in. The culling frustum is built from the same camera; the hook is the same function or one near it, and it is required.
- **The HUD.** The HUD's scenes (`947_hud`, `204_c_race_info`) are binary layouts in `Media\UI.zip`, so whether their elements are anchored or absolute in 1280x720 is not known yet. The cheapest correct step draws the HUD and the menus into the centre 1280x720 (a pillarboxed HUD over the full-width scene: the HUD's draws start at the post passes' final point, with a 1280x720 viewport). Anchoring the left and right elements to the panel's edges needs the UI's layout code and comes as a later step.

The scene at the logical width needs more EDRAM than exists (21:9 with 2x MSAA: 22 tiles across, 1,980 tiles each for colour and depth), which the private scene targets of 18.4 already cover.

### 18.6 Option B: the title renders the full pixel size

Rendering 3840x2160 in the title means guest surfaces of 6,480 tiles at 32bpp without MSAA against EDRAM's 2,048 and its 12-bit tile bases, guest resolve textures of about 33 MB each in the title's own heaps, readbacks four times as large, and every size the title derives from 1280x720 changed through hooks, the HUD's coordinates included. The host draw scale of the hybrid reaches the same pixels without any guest-side cost, so B stays a fallback for a case the hybrid cannot handle, and none is known.

### 18.7 Option C: upscaling

A render at 1x or an intermediate scale, upscaled to the panel by TAA upsampling or FSR, is not native rendering. It stays N5's upscaler item ([FSR plan](fsr-plan.md)), for a GPU that cannot afford the panel's scale.

### 18.8 GPU time estimates

Assumption, not measured: the GPU time of a race frame is a fixed part plus a part proportional to the scene's pixels, fitted to the two measured scales of each device.

| Device and scale | Fit (fixed + per 1280x720 of pixels) | Estimate |
|---|---|---|
| Ally X, AC power, 3/2 (1920x1080) | 2.47 + 5.43 ms, from 7.9 ms at 1x and 24.2 ms at 2x | about 14.7 ms (68 fps if GPU bound) |
| Ally X, battery, 3/2 | 4.0 + 5.9 ms, from 9.9 and 27.6 ms | about 17.3 ms (58 fps) |
| This PC, 20/9 at 16:10 (2560x1600) | 1.26 + 0.97 ms, from the close gate of N3 (2.23 ms at 1x, 5.14 ms at 2x, one pass 2x) | about 5.6 ms |
| This PC, 20/9 at 16:9 (2844x1600) | the same | about 6.0 ms |
| This PC, 3 (3840x2160) | the same | about 10.0 ms |
| This PC, 2 at 21:9 (3440x1440) | the same | about 6.5 ms |

The surfaces kept at the integer scale k (18.4) are part of the fixed time at each scale. With one pass 4x instead of 2x, the Ally X at 3/2 costs a little more (16.17: about 0.1 to 0.3 ms at 1x here); without MSAA, under TAA, a little less.

### 18.9 Increments and gates

The main session approved this note on 24 October 2026, with the scale per surface (18.4, refined the same day to a scale per resolve destination), the resolves into host textures with the downscale for readbacks, the projection hook before the shader override, the required culling hook and the centred HUD as the first aspect step (18.5). Its order, with step gates per step, a close gate per increment and the 1:1 image check every time:

1. **Resolution.** The rational scale on the native path: host targets, host resolves, scene-scaled textures, the swap, the post passes, TAA's jitter and thresholds, GTAO and the look mods. 3/2 first, then 20/9 and 3. Steps: TAA's size mismatch logged as an error; the rational type with the integer path unchanged, proven by a no-change gate at 1x and 2x; the scale per resolve destination (18.4); the private scene colour at 2x with its alias copies; resolves into host textures at the scene's scale with the downscale for readbacks; viewports, scissors, occlusion queries and memexport; the post passes, the look mods and the swap. Gate: 1:1 images at 3/2 against 1x and 2x; the resolve byte check and the identity check; no change at integer scales; perf on this PC at 3/2, 20/9 and 3. Effort, inferred: 3 to 5 person-weeks.
2. **Aspect.** The logical width with the projection, culling and HUD hooks, 16:10 first (2560x1600), then 21:9. Gate: no stretch (round wheels and objects), correct culling at the edges, the HUD placed, and the 16:9 result unchanged. Effort, inferred: 3 to 6 person-weeks; most of it finds the title's projection, culling and UI code.
3. **The launcher.** The display choice renders at the panel's own resolution and aspect in a borderless fullscreen that fills it; RTV and `-Rov` fall back to their integer 16:9 behaviour. `Build.ps1`, `pipeline.py`, `e2e.py` and `launcher_tests` get the options. Effort, inferred: under 1 person-week.
4. **The Ally X export** at 1080p native, for the user's runs.

### 18.10 Increment 1, step 1: the rational type, 24 October 2026

- **TAA's size mismatch.** TAA leaves the frame as it is when the final size is not the scene's (`taa.hlsl:198`). The post pass framework now logs that as an error, once per pair of sizes, so the scale work cannot hide a mismatch (`post_pass.cpp`, at the final point).
- **The type.** `rex/graphics/util/draw_scale.h` holds `DrawScale`, a uniform scale as a numerator over a denominator: `Parse` (`3/2`, `1.5`, `1080/720`), `Floor`, `ScaleDown`, `ScaleUp` and `ScaleEdge` (a guest pixel edge to the nearest host edge, halves up, so neighbouring rectangles share it). It is header only; `fm4_draw_scale_tests` checks it on the CPU, in `Build.ps1`'s tests: whole scales give the integer results, 3/2 and 20/9 give the panel sizes, and the edges of fractional scales leave no gap.
- **The cvar.** `native_draw_scale` (empty by default) sets the scene's scale on the native path. The runtime refuses it with an error and keeps the whole scale unless the path is RTV with `host_render_native` and `draw_resolution_scale_x` and `_y` are both its whole part, the scale of the other surfaces. `TextureCache::scene_draw_scale()` returns it; nothing reads it yet.

The no-change gate on `I:\FM4-build-usa` at 240 Hz (snapshot `n5a-nat1x`, local main `fc39156` with step 1), against the close gate of N3 (`n3z`):

| Check | Result |
|---|---|
| smoke, native one pass 2x at 1x and 2x, RTV at 1x and 2x (`n5a-nat1x`, `n5a-nat2x`, `n5a-rtv1x`, `n5a-rtv2x`) | 12 of 12 cells reached their goal; races 239.0 and 237.8 fps (1x native), 192.6 and 192.7 (2x native), 231.2 and 222.4 (1x RTV), 176.7 and 167.7 (2x RTV), within the noise of `n3z`. No "final size" error. The one FAIL (RTV 2x `photo`) is the known `Unimplemented XAM message` key, absent from that baseline cell; the WARNs are capture statistics and the guest's waits |
| `freeze`, native, against `n3z-freeze-*-nat` (both at the scenario's 2x) | PASS: draws -0.0% and +0.1%, 24.7% and 28.1% of the image above 4 |
| `native_draw_scale=3/2` at 2x (`n5a-freeze-1x-nat-s32`, which ran at the scenario's 2x) | refused: "native_draw_scale 3/2 needs draw_resolution_scale_x and _y of 1, not 2x2"; the run is otherwise the same |
| `native_draw_scale=3/2` at 1x (`n5b-freeze-1x-nat-s32` against `n5b-freeze-1x-nat`, both with the scale passed explicitly) | accepted ("Native draw scale: 3/2 for the scene, 1 for the other surfaces"); parity PASS, 25.2% of the image above 4 |
| The first real 1x freeze parity (`n5b-freeze-1x-*`, 1280x720 captures, `original,taa=0`, `FM4_TILING_SCENARIO=1`) | native one pass against RTV: draws -14.3%, predicated packets -25.8%, FAIL by design, 20.6% of the image above 4; the 1:1 crops of the old tile seam (line 512) and of the top 208 lines show no artefact |

