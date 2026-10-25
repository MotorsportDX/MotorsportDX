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

The main session's decisions of 25 October 2026, after the probe of 18.21:

- **Hor+ and Vert+.** A screen of 16:9 or wider keeps the vertical field of view and widens the horizontal one: a logical width W at 720 lines (21:9: 1720). A screen narrower than 16:9 keeps 16:9's horizontal field of view and gains vertical: Vert+, 1280 wide and a logical height H (16:10: 800). The HUD and the menus stay in the centred 1280x720 either way.
- **The projection in the host.** The scene draws through the camera widen their viewports to the logical size and scale clip x (or y) by 1280/W (or 720/H) through the system constants' NDC scale, which the translator applies to every vertex shader's position: every camera shader, without overrides, while the mirror, the shadows and the cube maps (not scene draws) keep their projections. The post passes that reconstruct positions from the camera (GTAO, the motion vectors, TAA's reprojection, the windshield) take the same scale.
- **The culling** of the title (`CTrackPresentation::ComputeVisibility` `sub_82925860` and the cars' and objects') is widened by the same factor, after the host steps; until then the gaps at the edges are documented.
- **The order:** the logical width with the HUD centred at 21:9 on this PC behind a switch, then 16:10 Vert+ on this laptop's panel, then the culling. Each step's gate: no change at 16:9 (freeze parity at 1x and 2x), at the other aspects no stretch (round wheels and objects) and the HUD centred, the post passes checked at the edges (GTAO crops, a TAA crop in motion, the motion vectors' debug view).

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

1. **Resolution.** The rational scale on the native path: host targets, host resolves, scene-scaled textures, the swap, the post passes, TAA's jitter and thresholds, GTAO and the look mods. 3/2 first, then 20/9 and 3. Steps, in the order the main session approved on 24 October 2026: (1) TAA's size mismatch logged as an error, and the rational type with the integer path unchanged, proven by a no-change gate at 1x and 2x (18.10); (2) every 1280-wide surface of the races private, at integer scales, behind `native_rt_surfaces` bit 16, off by default (18.11); (3) resolves from the private surfaces write host textures, full-screen destinations at the surface's scale and smaller ones resolved down to k, the texture cache keyed by a scale value instead of the scaled bit, readbacks on demand downscaled from the host texture; at integer scales the host textures are byte-checked against the existing path, and bit 16 becomes part of `-Native` once that check passes on its surfaces; (4) the rational scale for the private surfaces: their sizes by `ScaleUp`, the viewports and scissors of draws into them by `ScaleEdge`, occlusion queries divided by the scale's square, memexport's pixel; the first image at 3/2; (5) the post passes' final size, TAA's thresholds in guest pixels, the look mods' scale macros as floats, the swap and the presentation at the panel's size; then 20/9 and 3, with perf. Gate: 1:1 images at 3/2 against 1x and 2x; the resolve byte check and the identity check; no change at integer scales; perf on this PC at 3/2, 20/9 and 3. Effort, inferred: 3 to 5 person-weeks.
2. **Aspect.** The logical width with the projection, culling and HUD hooks, 16:10 first (2560x1600), then 21:9. Gate: no stretch (round wheels and objects), correct culling at the edges, the HUD placed, and the 16:9 result unchanged. Effort, inferred: 3 to 6 person-weeks; most of it finds the title's projection, culling and UI code.
3. **The launcher.** The display choice renders at the panel's own resolution and aspect in a borderless fullscreen that fills it; RTV and `-Rov` fall back to their integer 16:9 behaviour. `Build.ps1`, `pipeline.py`, `e2e.py` and `launcher_tests` get the options. Effort, inferred: under 1 person-week.
4. **The Ally X export** at 1080p native, for the user's runs.

The project owner set the order after increment 1 on 25 October 2026: (1) finish increment 1 (step 5: the box filter, the TAA check, 20/9 and 3x); (2) increment 3, the launcher's resolution choice at the panel's own resolution, which sets the scale and the matching `native_rt_surfaces` together; (3) increment 2, widescreen (the logical width, Hor+ FOV, culling, the centred HUD first); (4) FSR 3.1 as [fsr-plan.md](fsr-plan.md) plans it, native AA first, then upscaling on top of this section's scales per resolve destination; (5) photo mode at native resolution (one pass with the private 4x pair, 18.17), with its own parity gate; on 25 October 2026 the main session widened item 5 to "entry 0 at s" (the front-end menus, the pre-race screens and photo mode, 18.20).

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
| The first real 1x freeze parity (`n5b-freeze-1x-*`, 1280x720 captures, `original,taa=0`, `FM4_TILING_SCENARIO=1`) | `-Rov` against RTV passes: draws +0.1%, predicated packets +0.1%, 18.2% of the image above 4 (`n5b-freeze-1x-rov2`; the first `-Rov` run timed out in the menu, at `UI_Homespace`). Native one pass against RTV and `-Rov`: draws -14.3%, predicated packets -25.8% and -25.9%, FAIL by design, 20.6% and 23.7% of the image above 4; the 1:1 crops of the old tile seam (line 512) and of the top 208 lines show no artefact |

### 18.11 Increment 1, step 2: every 1280-wide surface private, 24 October 2026

`native_rt_surfaces` bit 16 (off by default, not in `-Native`), with `native_one_pass_msaa`, gives every 1280-wide surface of the races a private host target outside the EDRAM tile map, as the one-pass scene depth and the 4x scene colour have (16.17): the 2x scene colour (base 0, 16 tiles of pitch, the float format), the post passes' one-sample colour at base 0 (the float format, and `2_10_10_10` for the final picture) and the final picture's depth (base 720, one sample). The other surfaces (the shadow maps, the reflections, the luminance) stay in the tile map. A resolve from a private surface other than a 4x one dumps that surface alone, so the resolve byte check covers these surfaces too. Bit 16 joins `-Native` once step 3's host textures are byte-checked against the existing path on these surfaces (the main session's condition of 24 October 2026).

The gate on `I:\FM4-build-usa` at 240 Hz (snapshot `n5c-nat1x`, local main `ea2a4ca` with step 2), `-Native` with one pass 2x and `native_rt_surfaces=26`, against step 1 (`n5a`, `n5b`) and N3's close gate (`n3z`, `n3w`):

| Check | Result |
|---|---|
| smoke at 1x and 2x (`n5c-nat1x`, `n5c-nat2x`) | 6 of 6 cells reached their goal; races 239.1 and 237.8 fps at 1x, 200.4 and 193.6 at 2x; no new error key. The FAIL is the overall p99 of the 1x `career` (33.26 ms against 17.11), which mixes the menus and the 30 fps screens; its race p99 is 5.15 ms against 5.13 |
| perf at 2x, 3 runs (`n5c-nat2xp` against `n3z-nat2xp`) | race 196.2 fps against 190.1, p99 7.17 ms against 7.24, GPU 4.83 ms a frame against 5.14 |
| transfer inventory, `race` at 2x (`n5c-inventory-2x`) | 15.25 transfers and 1,006 tiles a frame against 20.5 and 2,800: the transfers into and out of the 1280-wide surfaces are gone, the shadow map's rows under the 2x scene colour (16.17) among them |
| render target memory, `career` | 616.5 MB at 2x against 596.5 MB, 207.5 MB at 1x against 203.5 MB |
| combined checks with perturbation, `career` (`n5c-check-nat2x`, `n5c-check-nat4x`) | the resolve byte check: 12,726 and 12,140 resolves, 0 mismatches; 222,792 and 240,063 resolves from private surfaces against 16,154 before, 0 violations; identity, bindings and guard: mismatches only the perturbed |
| `parity.py` on `freeze` against step 1 | PASS at real 1x (`n5c-freeze-1x-nat` against `n5b-freeze-1x-nat`, 22.2% of the image above 4), at 2x (`n5c-freeze-2x-nat` against `n5a-freeze-2x-nat`, 30.3%) and at 2x with `--look aces` (`n5c-freeze-2x-aces` against `n3w-freeze-nat`, 18.5%); against RTV at 1x FAIL by design (draws -14.3%) |
| 1:1 crops, 2x with `aces` | the rewind menu's text and buttons, the dashboard and its gauges, the windshield over the sky: the same as without bit 16 |
| the menus at 2x, `photo_view` with full-size captures, bit 16 off and on (`n5c-menu-off`, `n5c-menu-on`) | both reached their goal; 27 of the 37 captures at 2560x1440 are identical (0.0% of the image above 4); the other 10 differ by 2.8% to 25.7% where the menus animate (the intro's arrow, the career home's car and sky at another moment), with the menus' text and panels the same |

### 18.12 Increment 1, step 3: resolves from the private surfaces into host textures, 24 October 2026

`native_rt_surfaces` bit 32 (off by default, not in `-Native`; it needs bit 2): after a resolve from a private surface, `ResolveHostIntoTextures` writes the host textures that sample the destination straight from the host render target. The shader is `resolve_host_32bpp` built with `XE_HOST_TEXTURE`: the samples are read and encoded as for the host resolve, then stored as `resolve_texture_32bpp` stores them (16.8), the destination's endianness and then the texture's applied, through a `UINT` view of the texture. Destinations: `8_8_8_8` and `2_10_10_10`, at any size and at offset 0, into the existing textures of their key (both `packed_mips` keys, every endianness, one level, created with unordered access under bit 32). Those textures are marked loaded, so the texture cache does not load them from the scaled resolve buffer. The scaled resolve buffer is still written, for the readbacks and every other reader, so at integer scales the step saves no GPU time; it is the path a fractional scale needs (18.9, step 4). With `native_rt_resolve_check=N`, every N-th frame writes scratch copies instead, and the textures' next loads are compared with them.

The gate on `I:\FM4-build-usa` at 240 Hz (snapshot `n5d-nat2x`, local main `9a67a9e` with step 3), `-Native` with one pass and `native_rt_surfaces=58`:

| Check | Result |
|---|---|
| texture byte check, `native_rt_resolve_check=30` with the resolve byte check (`native_rt_surfaces_check=30`) | `career` one pass 2x at 1x (`n5d-check-nat-1x`): 5,498 textures compared, 0 mismatches; at 2x (`n5d-check-nat-2x`): 8,244, 0; one pass 4x at 1x (`n5d-check-nat4x-1x`): 5,593, 0; `photo` at 2x (`n5d-check-photo-2x`): 6,148, 0. The resolve byte check: 20,072, 15,943, 18,887 and 11,564 resolves, 0 mismatches. All four runs reached their goal with no new error key |
| destinations | 279,346 to 361,861 textures written for 202,642 to 368,431 resolves from private surfaces; 5% to 9% of those resolves found no texture (a destination not sampled yet, or not at offset 0) and keep the load from the scaled resolve buffer |
| smoke at 2x (`n5d-nat2x`) | 3 of 3 cells reached their goal; races 198.8 and 190.9 fps |
| perf at 2x, 3 runs (`n5d-nat2xp` against step 2's `n5c-nat2xp`) | race 191.7 fps against 196.2, p99 7.27 ms against 7.17, GPU 4.89 ms a frame against 4.83: the texture writes cost about what the loads they replace did |
| `parity.py` on `freeze` against step 2 | PASS at real 1x (22.7% of the image above 4) and at 2x with `--look aces` (33.3%); the 1:1 crops of the windshield, the dashboard and the rewind menu are the same |

The main session's condition for `-Native` holds for bit 16 (turned on in `51cd88d`, the byte check covering the private surfaces); bit 32 stays off until the fractional scale needs it.

### 18.13 Step 4 design: draws at the scene's scale, 24 October 2026

Step 4 splits into three parts, each with a no-change gate at whole scales; the first image at 3/2 comes with the third.

**4a, draws into the private surfaces at s.** A draw whose bound targets are private (the registers say so before the render target update) is a scene draw; every other draw keeps k.

- **Targets.** A private target is created at `ScaleUp` of its guest size; the others at k, as today.
- **Viewport and scissor.** `GetHostViewportInfo` keeps its guest-pixel snapping. For a scene draw at a fractional s the host edges are `ScaleEdge` of the guest edges, and the NDC scale and offset come from those host edges divided by s, so the mapping is exact. Whole scales keep today's formula. The draw back end of N4, which computes the viewport on the replay thread, takes the same path.
- **The translated shaders.** The translator writes the scale into each translation as literals: the pixel position's 1/scale, memexport's pixel choice, and the reversal of texture fetch offsets for scaled textures. A scene draw at a fractional s uses a pixel shader translation with a new modification bit (`scene_scale`), translated with s. Every existing translation keeps its modification value and its DXBC, so the shader storage stays valid and whole scales do not change. At 3/2, k is 1, so the scaled textures a scene draw samples are the ones written at s. The main session approved the bit over moving the scale into the system constants on 24 October 2026. A fractional scale adds pixel shader translations and pipelines for the scene draws; step 4's perf report counts them per race at 3/2 and checks that the shader storage keeps them for the next run, since the first race otherwise compiles them (and the Ally X's driver compiles pipelines anyway).
- **Pipelines.** The slope-scaled depth bias and the point size use the draw's scale.
- **Occlusion queries.** A query whose draws went into the scene targets is normalized by s squared, others by k squared. The queries have several paths (on RTV and on `-Rov`), so this comes with 4c's check of the sun's glare.
- **Test at whole scales.** A diagnostic forces the scene variant when s equals k. Its images and byte checks must match the normal path.

**4b, resolves from s.** The scaled resolve buffer stays at k for everything, and so do the readbacks and the small destinations, which load from it as today. A host resolve from a private surface at s into the buffer at k takes the nearest sample where only readbacks read the destination; where a pass samples it for the picture (the bloom chain's inputs, the reflections), nearest at 3/2 would alias, so those take a box filter over the host pixels of each guest pixel at k (the main session's rule of 24 October 2026). The inventory of 18.4 says which destinations are which. Full-screen destinations get host textures at s (step 3's path). These textures have a key of their own (a `scene_scaled` bit, set for ranges a full-screen private resolve wrote), sized by `ScaleUp`.

**Open item of the 20/9 step.** At 20/9, k is 2: a texture at k sampled with fetch offsets in a scene draw has its offsets reversed with s instead of k. The error is the offset times (s - k) / s, 0.1 texel of the texture at s per texel of offset, so under one host texel for any offset below 10 texels. The main session deferred it to the 20/9 step on 24 October 2026.

**4c, the frame at s.** The post passes' final size, the swap and the presentation at the panel's size, TAA's thresholds in guest pixels, and the look mods' scale macros. Then the first image at 3/2, at 1:1 against 1x and 2x.

### 18.14 Increment 1, step 4a: scene draws, 24 October 2026

The parts of 18.13's 4a, behind `native_draw_scale`. A scene draw is one whose render target update bound private surfaces, while the scene's scale is fractional or the diagnostic `native_draw_scale_variant` is on, and that is not a uniform draw (a clear-like draw of `rtv_uniform_draws`, which stays off while the scene's scale is fractional).

- **Targets.** A private target at a fractional scale is created at `ScaleUp` of its guest width and height (`GetKeyScale`).
- **Viewport and scissor.** `draw_util::GetHostViewportInfo(regs, DrawScale, ...)` computes the viewport in guest pixels, maps its edges with `ScaleEdge`, and adjusts the NDC scale and offset to the host edges. `ScaleScissor` maps the scissor's edges the same way. The command processor and every viewport of the draw back end go through `draw_back_end::ComputeViewportAndScissor`: the capture's expected values, the check, and the replay that draws. The back end's capture carries the scene draw as `kFlagSceneScale`.
- **Shaders.** A scene draw's pixel shader has the `scene_scale` modification. The translator takes its scale for the pixel position, memexport's pixel (at a fractional scale the first host pixel of each guest pixel) and the scaled textures' coordinates and offsets from that bit. A translation without the bit keeps its literals.
- **Pipelines and system constants.** The slope-scaled depth bias of a pipeline with a `scene_scale` pixel shader, and the point size and polygon offset of a scene draw's system constants (`SystemConstantInputs::scene_draw`), use the scene's scale.

The no-change gate on `I:\FM4-build-usa` at 240 Hz, local main `96da8da` with step 4a, `-Native` one pass 2x with `native_rt_surfaces=26`, `native_draw_scale` equal to the whole scale and `native_draw_scale_variant=true`, so that every draw into the private surfaces takes the scene draws' path at a scale where it must give the same picture:

| Check | Result |
|---|---|
| combined checks with perturbation, `career` at 1x (`n5h-check-1x`) and 2x (`n5g-check-2x`) | the draw back end's check (the replay's pipeline, viewport, scissor and system constants against the draw's): mismatches only the perturbed; identity, bindings and guard the same; the resolve byte check 11,961 resolves at 2x, 0 mismatches. The first 1x run (`n5g-check-1x`) found 8 draws in the boot splash whose replayed viewport and system constants differed: the replay that draws still computed its viewport on the whole path. Fixed before `n5h-check-1x` |
| smoke at 2x (`n5g-nat2x`) | 3 of 3 cells reached their goal; races 199.6 and 193.9 fps |
| `parity.py` on `freeze` against step 3 | PASS at real 1x (15.7% of the image above 4) and at 2x with `--look aces` (26.7%); the 1:1 crops of the dashboard and the car ahead are the same; the still picture does not change from capture to capture |

Occlusion queries at the scene's scale come with 4c (18.13).

### 18.15 Increment 1, step 4b: resolves from the scene's scale, 24 October 2026

- **The source scale.** `HostResolveSource` carries the render target's scale: the scene's for a private surface. The host offset of a resolve's first pixel is the guest offset at that scale (`ScaleEdge`). The host resolve shaders take `xe_resolve_host_source_scale`, the source's scale over the destination's as n:d, and destination pixel p reads source pixel floor((2p + 1) * n / (2d)): the nearest sample, the identity at 1:1. The scaled resolve buffer (or the shared memory at 1x) stays at the whole scale k, so the readbacks and the small destinations load as before.
- **Textures at the scene's scale.** A full-screen destination (at least 1280x720) of a private surface, in a format the host texture shader writes (`8_8_8_8` or `2_10_10_10`, at offset 0, not an array), is marked scene-resolved (`MarkRangeAsSceneResolved`; the next resolve to the range clears it). A texture over such a range gets the `scene_scaled` key bit, is sized by `ScaleUp`, is written only by the resolves (`ResolveHostIntoTextures` with the destination's pixels one to one, bounded by `xe_resolve_host_texture_size`) and is never loaded. If no texture of the destination exists at the resolve, the resolve creates the two `packed_mips` variants under the destination's endianness, so the first frame is written too. The texture cache reports a `scene_scaled` texture as resolution-scaled, so the scene variant of the shaders scales its coordinates.
- **Counters** (perf trace, `native_rt_resolve`): `scene_created`, the textures a resolve created, and `scene_unwritten`, those fetched before any resolve wrote them. A texture keeps its data when the readback on demand writes the same bytes under it.
- It needs `native_rt_surfaces` bit 32 (the textures' unordered access) and runs at a fractional scene scale, or at any with `native_draw_scale_variant`.

The no-change gate on `I:\FM4-build-usa` at 240 Hz (step 4b on local main `8e70103`, then again on `9337939` after the rebase, `n5l`), with the scene path forced at whole scales (`native_draw_scale` equal to the whole scale, `native_draw_scale_variant=true`, `native_rt_surfaces=58`):

| Check | Result |
|---|---|
| combined checks with perturbation and both byte checks, `career` at 1x and 2x (`n5k-check-1x`, `n5k-check-2x`) | the resolve byte check 12,355 and 12,309 resolves, 0 mismatches; the texture byte check (the textures below 1280x720) 2,180 and 5,268, 0 mismatches; 8 and 10 scene textures created, none fetched before its first write; identity, bindings, guard and back end: mismatches only the perturbed, apart from one identity mismatch at 2x, register 044B (`CP_PROG_COUNTER`), a register mismatch the late write trace does not cover, which N4 recorded in 17.26 and logs since `02ff637`. On the rebased tree (`n5l-check-1x`, `n5l-check-2x`): the resolve byte check 12,535 and 11,566, the texture byte check 2,215 and 4,956, 0 mismatches; identity 12,147 and 11,156, only the perturbed |
| smoke at 2x (`n5k-nat2x`) | 3 of 3 cells reached their goal and PASS against step 4a; races 199.9 and 190.3 fps |
| `parity.py` on `freeze` against step 4a | PASS at 2x with `--look aces` (34.1% of the image above 4). At real 1x the first run froze at a later race moment (the car ahead much closer): draws -4.0%, FAIL on draws, with a correct picture. On the rebased tree both pass: real 1x draws -0.0% (11.1% of the image above 4), 2x with `aces` -0.0% (32.2%) |
| an earlier build of 4b (`n5i`, `n5j`) | marked destinations the resolve does not write as scene-resolved and looked for the existing textures under the wrong key at 2x: 74,015 fetches before a write and 59,124 creations a run. Fixed before `n5k` |


### 18.16 Increment 1, step 4c: the first image at 3/2, 24 October 2026

The parts of 18.13's 4c that the first image needs, behind `native_draw_scale` with `native_rt_surfaces` bit 32:

- **Textures at the scene's scale.** The host texture of a `scene_scaled` key is created at `ScaleUp` of its guest size (`D3D12TextureCache::CreateTexture`; before, it took the whole scale, so the first 3/2 images showed the top left of the scene, enlarged), and `RequestResolvedTexture` reports that size to the post passes.
- **The scene depth.** The title resolves the scene depth to a depth format (`k_24_8_FLOAT`). A full-screen depth resolve from a private surface is a scene destination too: it writes the destination's `8_8_8_8` textures (the same words; the post passes read the stencil through that view) and its depth-format textures, created by the resolve when missing. A depth texture at the scene's scale is `R32_TYPELESS`, written through an `R32_UINT` view with the depth as the depth texture load converts it (24-bit unorm or 20e4 float, `resolve_host_32bpp.xesli`). Without it the post passes took the 1x depth at 3/2, and TAA rejected its history at every edge: 17% to 22% of a frozen picture changed from capture to capture with `aces`.
- **The frame.** The post passes' final size at a fractional scale is `ScaleUp` of the guest size. The look mods' `XE_RESOLUTION_SCALE_X` and `_Y` of a `scene_scale` translation are floats (`(3.0 / 2.0)`), with both axes' texture offsets reverted. A resolve clear into a private surface at a fractional scale maps its rectangle with `ScaleEdge`. A private surface at a fractional scale is created at its full height (no sizing: the growth copies by tile transfers at the whole scale), and a resolve from it is never dumped into the EDRAM buffer, as for one pass 4x. Occlusion queries at a fractional scale are normalized by the scene's scale squared; the sun's glare at 3/2 has no separate check yet.
- **Counters.** The perf trace's `scene_variants` counts the pixel shader translations and the pipelines with the `scene_scale` modification made while the title runs, and the shader storage's load log counts the pipelines of scene draws it created. A log line per full-screen destination says whether it is a scene destination.
- **e2e.** `ocr-server.ps1` averages captures taller than 720 lines down to 720 lines at any ratio, so menu selection works on 1920x1080 captures (the 3/2 career first timed out on QUIT RACE: the red lines were looked for at 720p offsets in a 1080p image).

The gate on `I:\FM4-build-usa`, one pass 2x, `native_rt_surfaces=58`, the scene's scale passed explicitly (`native_draw_scale=3/2` with `draw_resolution_scale_x` and `_y` of 1; 1 and 2 at the whole scales, without `native_draw_scale_variant`):

| Check | Result |
|---|---|
| captures and presentation | 1920x1080 at 3/2 in `freeze`, `photo` and `career`; the swap's texture is 1920x1080 |
| 1:1 crops at 3/2 against 1x and 2x (`n5s-freeze-32-nat` against `n5l-freeze-1x-nat` with `original,taa=0`; `n5w-freeze-32-aces` against `n5l-freeze-2x-aces`) | the car ahead, the stands, the gauges, the rear-view mirror (a destination resolved down to k) and the cable car: the same picture, its detail between 1x and 2x; no offset, seam or zoom |
| still picture, `freeze` (`n5x-freeze-*`) | 0.0% of the image above 4 from capture to capture at 1x, 3/2 and 2x, with `original,taa=0` and with `aces` (TAA, GTAO, motion vectors, windshield) |
| combined checks with perturbation, `career` at 3/2 (`n5t-check-32`) | reached its goal; the resolve byte check 5,105 resolves and the texture byte check 2,190 textures (the destinations at k), 0 mismatches; identity, bindings and guard: mismatches only the perturbed; the error keys of the 2x baseline (`n5l-check-2x`) and one "final size" error (below) |
| scene textures | 12 created a `freeze` run, none fetched before its first write; 11 resolves without a texture against about 5,500 before the scene depth |
| translations and pipelines of scene draws | the storage holds 295 pipelines of scene draws at 3/2 (of 2,601) after the runs of this step. A run with a warm storage made none at runtime in `freeze` and the `photo` race; `photo` made 1 translation and 2 pipelines for photo mode in its first run, and 1 pipeline in the next |

GPU time a frame on this PC (RTX 5070 Ti Laptop), the same build, one run each (`n5x`):

| Scene | 1x (1280x720) | 3/2 (1920x1080) | 2x (2560x1440) | 3/2 from the fit of 1x and 2x |
|---|---|---|---|---|
| `freeze`, `original,taa=0` | 1.67 ms | 2.42 ms | 3.09 ms | 2.26 ms |
| `freeze`, `aces` | 2.21 ms | 3.52 ms | 4.93 ms | 3.35 ms |
| race of `photo` at 240 Hz, `aces` | 2.31 ms | 3.74 ms | 5.35 ms | 3.58 ms |
| race fps and p99 | 236.0, 5.03 ms | 236.1, 5.24 ms | 188.4, 7.49 ms | |

3/2 costs 0.75 to 1.43 ms more than 1x, and 4% to 7% more than the line through 1x and 2x (the surfaces kept at k are part of the fixed cost of every scale, 18.8). The passes of `aces` at 3/2: GTAO 0.38 ms, TAA 0.27, windshield 0.19, motion vectors 0.03.

RTV and `-Rov` on the 4c binary (`3213732`), against the 4b binary (`c122016`) built and run in the same session (`n5z`). Every part of 4c is gated on a fractional scene scale or the `scene_scale` modification, which RTV and `-Rov` never have:

| Check | Result |
|---|---|
| RTV `freeze` parity, real 1x with `original,taa=0` and 2x with `aces`, 4b against 4c | PASS: draws -0.2% and +0.5%, 21.9% and 30.6% of the image above 4. A second 4c run against the first (the same binary): +1.6% and -0.5%, 29.7% and 18.3%; against 4b: +1.4% and -0.0%, 32.2% and 32.2%. The freeze holds at a slightly different race moment from run to run (the car ahead and the cars in the mirror move), so the 4b against 4c values are within the run-to-run range |
| the look mods' DXBC at whole scales | unchanged: the RTV runs with `aces` on 4b and 4c load the same 221 overrides from the shader override cache and compile none |
| RTV smoke at 1x and 2x (`n5z-rtv1x`, `n5z-rtv2x`, against N4's `n5r`) | 6 of 6 cells reached their goal; races 215.6 and 212.7 fps at 1x, 167.9 and 158.2 at 2x; the WARNs are capture statistics and the 2x `career` p99 |
| `-Rov` smoke at 1x (`n5z-rov1x` against `n5r-rov1x`) | 3 of 3 cells reached their goal; `career` PASS. `photo` FAIL on the race's fps, 108.4 against 131.7 (GPU 9.73 ms against 7.56): N3's close gate measured 104.7 fps and 9.77 ms on the same cell (`n3z-rov1x`), and the captures differ by 37 points of black, so the baseline's race is the outlier. A repeat (`n5z-rov1x-photo2`) raced at 118.1 fps and timed out later at SAVE PHOTO |

Open items for step 5: in photo mode the post passes see a 1280x720 scene depth (photo mode's depth resolve is not a scene destination yet), so they log "final size 1920x1080 differs from the scene's 1280x720" and leave its frames without the look's passes, which photo mode's picture shows correctly otherwise; TAA's thresholds in guest pixels; the box filter of the small destinations sampled for the picture (18.13, 4b); then 20/9 and 3.

### 18.17 Increment 1, step 5: the box filter, photo mode, TAA, 20/9 and 3, 25 October 2026

- **The box filter.** A small destination of the scene's surfaces resolves down to the integer scale k (18.4). The nearest sample of 4b aliased what the title samples for the picture (the bloom chain's regions, the rear-view mirror). At a fractional ratio of the source's scale over k, `resolve_host_32bpp` (float colour) now takes the mean of the host pixels the rasterizer gave each destination pixel: those between its edges rounded half up as `DrawScale::ScaleEdge` rounds them. The shader needs the exact position of the resolve's first pixel, of which `xe_resolve_host_offset` is the rounded value, so `xe_resolve_host_source_phase` carries the difference in 1/256 of a source pixel. A box over the exact edges (the first version) took up to half a row of the next draw's pixels: a red line under the mirror's picture at 3/2 and 20/9. The rule of 18.13 applies the box where the picture samples the destination and the nearest sample where only readbacks read it; every float colour downscale gets the box, since the reductions are means as well. Uint colour and depth keep the nearest sample. Whole ratios (1:1 at every whole scale and for the textures at the scene's scale) do not change. Cost: the resolves take about 0.1 ms more a frame at 3/2 (0.71 ms against 0.61 in `freeze`).
- **Photo mode** (the main session's decision of 25 October 2026, option B). Photo mode renders its scene with 4x MSAA in the title's three tiles (the depth at base 1024, pitch 32, resolved as 1280x256, 1280x256 and 1280x208 into the range of the race's scene depth; 16.17). Those surfaces are not private, so at a fractional scale photo mode renders at k and is scaled up to the panel; it is not native resolution yet. The post passes take that scene depth at k, so at a fractional scale a scene depth not at the scene's scale leaves its frame without scene passes. The log says so once ("a scene depth of 1280x720, not at the scene's scale 3/2 (photo mode), runs no scene passes"), instead of 4c's "final size" error. Photo mode at native resolution (one pass with the private 4x pair) is item 5 of the order above, with its own parity gate.
- **TAA's thresholds stay in host pixels** (the main session's decision of 25 October 2026). `TAA_STILL_PIXELS` (0.05), `TAA_MOTION_PIXELS` (16) and the jitter gate's `TAA_JITTER_MOTION` are in host pixels, as the jitter is. In guest pixels they would change 1x or 2x, whichever they were not tuned at; in host pixels, 3/2 sits between 1x and 2x as 1x and 2x already differ. The check: the race's captures in motion at 1x, 3/2 and 2x with `aces` (`n6e-photo-*`) show no ghosting at the moving car, the cables or the poles at 3/2, and every still picture holds (0.0% from capture to capture at 1x, 3/2, 2x, 20/9 and 3).
- **20/9 and 3.** `native_draw_scale=20/9` with `draw_resolution_scale_x` and `_y` of 2 renders the scene at 2845x1600 (`ScaleUp` of 1280 is 2845, of 720 1600) and the other surfaces at 2x. 3 is a whole scale: the existing integer path at 3x (3840x2160). Both run with no new error; the 1:1 crops at 20/9 against 2x (the car ahead, the gauges, the mirror) show the same picture with more detail. The offsets of 18.13's open item (a texture at k sampled with fetch offsets by a scene draw at 20/9) show no visible error in these crops.

The gate on `I:\FM4-build-usa` (one pass 2x, `native_rt_surfaces=58`, the scales passed explicitly), against 4c's runs (`n5x`, `n5z`):

| Check | Result |
|---|---|
| no change at whole scales: `freeze` parity, native at real 1x with `original,taa=0` and at 2x with `aces` against `n5x`, RTV at 2x with `aces` against `n5z-freeze-2x-aces-rtv-4c` | PASS: draws +0.1%, +0.6% and +0.4%, 24.8%, 37.0% and 25.4% of the image above 4, within the range of two runs of the same binary (18.16) |
| combined checks with perturbation, `career` at 2x, 3/2 and 20/9 (`n6h-check-*`) | all reached their goal; the resolve byte check 11,639, 5,521 and 4,947 resolves and the texture byte check 6,072, 2,430 and 5,076 textures, 0 mismatches; identity, bindings and guard only the perturbed; the error keys of 4c's 2x baseline |
| the mirror's last row, `freeze` with `aces` | white as at 2x, at 3/2 and 20/9 (`n6g`) |
| photo mode at 3/2 and 20/9 | the photo saved, no error; one "runs no scene passes" line |
| scene variants with a warm storage | 1 pipeline a run at runtime in `photo`, 1 translation and 1 pipeline in `career` |

GPU time a frame on this PC, the race of `photo` at 240 Hz with `aces`, one run each (`n6e`, `n6h` with the final box):

| Scale | Scene | GPU ms | Race fps, p99 |
|---|---|---|---|
| 1x | 1280x720 | 2.25 | 239.0, 4.92 ms |
| 3/2 | 1920x1080 | 3.78 | 237.3, 5.14 ms |
| 2x | 2560x1440 | 5.29 | 186.4, 7.71 ms |
| 20/9 | 2845x1600 | 6.76 | 147.4, 9.12 ms |
| 3 | 3840x2160 | 9.36 | 103.9, 12.81 ms |

20/9 at 16:9 costs 6.8 ms against the 6.0 ms that 18.8 estimated; 3 costs 9.4 ms against 10.0. `freeze` with `aces`: 6.97 ms at 20/9 and 9.83 ms at 3.

Increment 1's step gates are done; its close gate is next, then increment 3 (the order above).

### 18.18 Increment 1's close gate, 25 October 2026

The full tier of the main session's list on `I:\FM4-build-usa`, main `bccfeb0` (snapshot `n6z-nat1x`), one pass 2x with `native_rt_surfaces=58` on the native path and the scales passed explicitly, at 240 Hz:

| Check | Result |
|---|---|
| smoke, RTV at 1x and 2x (`n6z-rtv1x`, `n6z-rtv2x`, against 4c's `n5z`) | 6 of 6 cells reached their goal; races 224.0 and 226.3 fps at 1x, 177.3 and 164.0 at 2x; WARNs are capture statistics |
| smoke, native at 1x, 3/2 and 2x (`n6z-nat1x`, `n6z-nat32`, `n6z-nat2x`) | 9 of 9 reached their goal. In the matrix the 1x and 2x `photo` cells timed out at the boot (`@push UI_Homespace`: the title pushed it before the step watched for it, a fast route timing flake) and their `photo_view` cells had no photo; the reruns (`-r`) passed. Races 234.6 and 238.4 fps at 1x, 237.7 and 234.8 at 3/2, 197.3 and 188.1 at 2x; no new error key |
| smoke, `-Rov` at 1x with `original,taa=0` (`n6z-rov1x`) | 3 of 3; races 128.5 and 90.4 fps; `career` PASS |
| combined checks with perturbation, `career` at 2x and 3/2 (`n6z-check-2x`, `n6z-check-32`) | both reached their goal; the resolve byte check 11,235 and 5,519 resolves and the texture byte check 5,803 and 2,395 textures, 0 mismatches; identity (with the late write trace) 10,636 and 13,149, bindings 16,102 and 17,388, guard 16,167 and 18,310, back end 18,683 and 21,346: mismatches only the perturbed; the error keys of every earlier check run |
| `freeze` parity against the previous build's runs | native at real 1x -0.0% draws (27.5% of the image above 4), native at 2x with `aces` -0.6% (37.4%), RTV at real 1x -1.5% (32.1%), RTV at 2x with `aces` -0.9% (33.0%): PASS, within the range of one binary (18.16: +1.6% and -0.5%); `-Rov` against RTV at real 1x +0.7% (30.2%) PASS; native against RTV -14.2% and -14.4% draws, FAIL by design as at every earlier gate |
| still pictures | 0.0% of the image above 4 from capture to capture in every `freeze` run |
| images at 1:1 | `freeze` with `aces` at 3/2 and 20/9 against 2x: the car ahead, the gauges, the mirror, the stands and the cable car the same picture, with the detail of each scale. `career` at 3/2 against 2x: the race HUD (the place, the gear and speed) sharp at 1920x1080; photo mode at k by design (18.17) |
| render target memory (`rt_memory` in a race) | native 203.5 MB at 1x, 331.2 MB at 3/2, 600.5 MB at 2x, 797.7 MB at 20/9; RTV 577.8 MB at 1x and 2,102.2 MB at 2x |

Perf, the race of `photo` with `aces`, 3 runs each (20/9 by `e2e.py` runs, since the drive had 14.8 GB free and the matrix needs 15):

| Path and scale | GPU ms a race frame | Race fps | Race p99 | Draw zone a draw |
|---|---|---|---|---|
| RTV 1x | 2.92, 2.83, 2.84 | 210.7, 229.0, 224.4 | 6.18, 5.43, 5.68 ms | 1.36 to 1.37 µs |
| RTV 2x | 5.59, 5.78, 5.80 | 174.6, 168.4, 166.7 | 8.46, 8.78, 8.83 ms | 2.21 to 2.90 µs |
| native 1x | 2.17, 2.28, 2.24 | 239.4, 238.4, 239.0 | 4.81, 5.01, 4.98 ms | 0.89 to 0.90 µs |
| native 3/2 | 3.69, 3.63, 3.77 | 237.2, 237.8, 234.9 | 5.18, 5.07, 5.30 ms | 0.84 to 0.89 µs |
| native 2x | 5.02, 5.07, 5.11 | 194.5, 191.5, 191.1 | 7.44, 7.45, 7.44 ms | 1.82 to 2.04 µs |
| native 20/9 | 6.59, 6.45, 6.58 | 148.2, 150.7, 147.8 | 9.16, 8.91, 9.04 ms | 2.87 to 3.43 µs |

The native path takes 21% less GPU time than RTV at 1x and 12% less at 2x. The draw zone grows with the scale where the GPU sets the frame rate (2x, 20/9): the command processor waits for the GPU inside it.

Two findings:

- **The front-end menus render at k.** In `career` at 3/2, 20 of 98 captures are 1280x720 (the career home, MY PROFILE, the event list): those menus draw into surfaces that are not private, so at 3/2 they render at 1x and are scaled up to the panel, where at 2x they render at 2x. The races, their HUD, the replays and the post-race screens are 1920x1080. The menus at the scene's scale need their surfaces private, as the races' are; an open item for the main session.
- **The mirror's last row** is red in this gate's 2x `freeze` too, without the box filter: it is the track behind at that race moment, so 18.17's "white as at 2x" compared different moments and shows nothing either way. The box of 18.17 stays as it is, correct by its construction (the rasterizer's edges).

Increment 1 is closed.

### 18.19 Increment 3: the screen's own resolution in the launcher, 25 October 2026

The main session approved the design on 25 October 2026. `display_settings` (`pipeline.py`) and its port `DisplaySettings` (`plan.cpp`) choose the render scale; `launcher_tests` compares the two.

- **The screen's own scale.** `screen_scale` (`ScreenScale`) is the largest 16:9 picture that fits the screen, min(width / 1280, height / 720), as a reduced fraction with at most 720 in the denominator (rounded down) and at least 1: 1920x1080 3/2, 2560x1440 2, 3840x2160 3, 2560x1600 2 (2560x1440, letterboxed), 2880x1800 9/4 (2880x1620, letterboxed), 1366x768 16/15, 1280x800 1.
- **When it applies.** On the native path in one pass with 2x MSAA (`-Native`, the default with integrated GPUs only), the automatic render scale is the screen's own instead of the whole multiple that covers the screen. A fraction sets `native_draw_scale` (`3/2`), its whole part `draw_resolution_scale_x` and `_y`, and `native_rt_surfaces=58` together, so a user never sets the cvars. A whole scale keeps `native_rt_surfaces=26`. One pass 4x (`-MsaaSamples 4`), one tile, the MSAA tiles, RTV and `-Rov` keep the whole multiple: one pass 4x has not run at a fractional scale.
- **Integrated GPUs** keep 1x automatically (the main session's decision of 25 October 2026); `-ResolutionScale panel`, the GUI's "Screen's own resolution", picks the screen's own scale on them. The default for integrated GPUs is pending the user's 3/2 preview run on the ROG Xbox Ally X.
- **Supersample** on the native path renders at the whole part of the screen's scale plus one (2x on a 1080p screen), a whole scale averaged down as before.
- **16:10 and other aspects.** The 16:9 picture is fitted, letterboxed or pillarboxed as the presenter does today, and the description says "16:9 letterboxed until the aspect work". Increment 2 fills 16:10 with a logical width of 1152 (2560x1600 at 20/9).
- **Options.** `Build.ps1 -ResolutionScale` takes `1` to `4` or `panel`; `pipeline.py --resolution-scale` the same; the GUI's Render scale has Auto (now showing its scale, "3/2, 1920x1080" on a 1080p screen on the native path), the screen's own resolution, and 1x to 4x. `panel` off the native path in one pass 2x is refused with the reason.
- **Tests.** `test_pipeline.py` `test_screen_scale` covers the scales, the iGPU, supersample, one pass 4x, RTV and `-Rov`. The launcher tests have two more monitors (a 2560x1440 one and a 2880x1800 one) and five more option sets (`panel`, supersample, a 1080p window, a 1440p window at 2, `panel` on a 720p window): 182 plans. Built outside the launcher's CMake project (the worktree cannot configure it), the port matched `pipeline.py` on the scale, the `native_*` cvars and the description in all 182 plans; its look cvars differed there because that build read other look data, so the launcher's own `launcher_tests` on main is the check.

### 18.20 The front-end menus at the scene's scale: the diagnosis, 25 October 2026

18.18 found the front-end menus at k at a fractional scale (20 of 98 captures of a `career` at 3/2 are 1280x720: the career home with the car, MY PROFILE, the event list). Diagnostic runs on main `ce5cfb8` at 3/2 (`n7a` to `n7d`, the code reverted after them):

- **The resolve trace** (`REX_RTV_RESOLVE_TRACE`, `n7a`) shows the menus' only full-screen resolve from a shared surface: a one-sample `8_8_8_8_GAMMA` surface at base 0, pitch 16, into `13BCA000`. Making that surface private at s (bit 16 extended to `8_8_8_8` and `8_8_8_8_GAMMA` at base 0, `n7b`) made `13BCA000` a scene destination, and the menus' captures stayed 1280x720.
- **The swap texture** (a log of its key, `n7c`) is the frontbuffer pair `1B0E1000` and `1B479000` (`2_10_10_10`, tiled), as in races; during the menus their range is not scene-resolved.
- **What clears it** (a log of every resolve over scene pages, `n7d`): the menus render with the title's tiling entry 0, 4x MSAA in three tiles (base 0, pitch 32, resolves of 1280x256, 1280x256 and 1280x208, in `2_10_10_10` and other formats) straight into the frontbuffers. Those 4x surfaces are shared, so they render at k, and each tile's resolve marks its range resolved at k, clearing the scene marking of the race's last frame. The same entry 0 serves the pre-race screens and photo mode (photo mode's three depth tiles of 18.17), as d3d-hle-n3.md found (16.17: "Entry 0 also serves the user interface").
- **What it needs.** Entry 0 in one pass with 4x MSAA and the private 4x pair (16.17) at the scene's scale. N3's attempt at entry 0 in one pass 4x did not boot (`n4p-onepass-4x`), which is why one pass 4x took entry 3 of its own. So the menus, the pre-race screens and photo mode at s are one item: find why entry 0 in one pass 4x does not boot, run the private 4x pair at a fractional scale (never run yet), then the menus, the pre-race screens and photo mode at s, with a parity gate of its own, since it changes `-Native` at whole scales too.

The main session's decision of 25 October 2026: that item, "entry 0 at s", takes the place of photo mode's item 5 in the order of 18.9, after FSR 3.1; increment 2 (the aspect) comes first. Until then, at a fractional scale the front-end menus, the pre-race screens and photo mode render at k and are scaled up to the screen; the races, their HUD, the replays and the post-race screens render at s.

### 18.21 Increment 2, step 1: the logical width (Hor+) in the host, 25 October 2026

**The probe** for the title's projection, before the main session chose the host route: the image's only 16/9 float (`0x8201A35C`, next to 1/1280 and 1/720) is read by `sub_82395630` alone, a frustum-like calculation after a tangent that stores into an object at +376 to +396; patched to 21:9 for a run (the read-only page opened for the store, `n8a`), the frozen picture did not change, so it is not the main projection. The tangent (`sub_823057D0`, a double-precision routine) has 32 callers; `sub_82300B20` is a zoom ratio, tan(f k) / tan(f0 k). Finding the projection statically would take many more rounds, so the host does it instead.

**The design** (`native_logical_width`, guest pixels at 720 lines, 1280 by default; it needs the scene draws' path, a fractional `native_draw_scale` or `native_draw_scale_variant`):

- **Surfaces.** A private 1280-wide surface is created at the logical width (`GetKeyLogicalWidth`), as are the textures at the scene's scale that a full-screen resolve writes (`SceneTextureHostWidth`); the resolve fills them across the width. A resolve from such a surface is never dumped into the EDRAM buffer, as for one pass 4x, and a full-width resolve clear covers the logical width. Uniform draws (`rtv_uniform_draws`) stay off.
- **Draws** (`draw_back_end::ApplyLogicalWidth`, in the command processor and the draw back end's replay). A scene draw whose guest viewport spans the full 1280 widens it to the logical width. A camera draw (depth or stencil on, and not at a constant depth: a viewport Z scale of 0 marks a screen-space draw such as the mask over the cockpit interior) also scales clip x by the old width over the new, so pixels keep their distance from the centre and the sides show more of the scene. Other full-width draws (the post chain's full-screen passes) stretch to fill. The HUD's draws, those after the post passes' final point (after the final combine itself), keep their viewports and scissors and move into the centred 1280. Full-width scissors widen the same way.
- **Post passes.** The final size, the scene's guest width (`GuestSceneWidth`, which the GTAO radius, the camera motion and the scene constants take instead of the depth's pitch) and the camera's unpack (its guest viewport shifted by half the added width, `LogicalViewport`) follow the logical width, so GTAO, the motion vectors and TAA's reprojection reconstruct positions across it.
- **Presentation.** The swap of a frame at the logical width presents it at its own aspect (the display width times W / 1280), so the presenter fits a 1720x720 picture to the window.

The step gate on `I:\FM4-build-usa` (one pass 2x, `native_rt_surfaces=58`), W = 1720 (21:9):

| Check | Result |
|---|---|
| no change at 16:9 (`native_logical_width` unset; every change is gated on W != 1280): `freeze` parity at real 1x with `original,taa=0` and at 2x with `aces` against the close gate's `n6z` | PASS: draws -0.0% and +0.6%, within the range of one binary (18.16) |
| 21:9 at real 1x (the scene draws' path forced) and at 3/2 with `aces` (`n8c-freeze-*`) | 1720x720 and 2580x1080 captures; the cockpit, the stands and the car ahead not stretched (round wheels), more on both sides; the rewind menu and the race HUD (place, laps, the map, the gear and speed) in the centred 1280 |
| the edges at 1:1 (3/2, `aces`) | GTAO under the railings and the wall at the left edge, the car and the mirror at the right edge, as in the centre |
| motion vectors (`MOTION_DEBUG` 1, 3 and 5 in a race at 3/2) | the object slots right across the width; with the mask first treated as a camera draw the cockpit interior was recognized only in the centre 1280 and its motion broke at the old edges (`n8e` to `n8g`); with constant-depth draws kept screen-space the interior covers the width and the motion field is continuous (`n8h`, `n8i`) |
| combined checks with perturbation, `career` at 3/2 with W = 1720 (`n8i-check-32-1720`) | reached its goal; the texture byte check 2,235 textures and the resolve byte check 5,170 resolves (the destinations at k), 0 mismatches; identity, bindings and guard only the perturbed; the error keys of every earlier check run |
| culling | no gap visible at the edges in the frozen and moving frames checked; the culling hooks (18.5) stay the later step and get their own check |

Next: 16:10 Vert+ (a logical height, 1280x800), then the culling.
