# Motion vectors: sources and design

Research note on where the runtime can get per-pixel motion vectors for FM4, as groundwork for TAA, temporal SSR and the FSR or DLSS upscalers. Nothing here is implemented. Today the runtime injects one screen pass, GTAO ([GTAO design](gtao-design.md)), and keeps no previous-frame buffers or motion vectors.

This note contains no game code, shaders or ucode. Addresses, hashes, registers, constant values and file element names are observations only.

Sources (all offline, no new game run):

- the local ucode disassembly of the motion blur pixel shader `4746CAD32EF168CF` (a `dump_shaders` run in `I:\FM4-build2`), and the constant table and literal block of its container in default.xex (read through `<BuildRoot>\loaded-image.bin` with the container parser of `material_atlas.py`);
- the `REX_GPU_INVENTORY` of a Quick Race on the Bernese Alps (`I:\FM4-build2`, 16,200 frames): render state per shader pair, including `RB_DEPTHCONTROL`;
- the `REX_FRAME_GRAPH` traces of GTAO M3 (`I:\FM4-build-gtao`: `m3-1x`, `m3-2x`, `m3b-2x`, every fifth frame, 4x MSAA, three tiles). The q-high traces named in the GTAO design are no longer on disk; the M3 traces show the same pattern;
- the GTAO M1 run `m1b` (`I:\FM4-build-gtao`), which logged the motion blur constant `c69` with `post_ao_log_constants`, and its screen captures;
- the material atlas (`I:\FM4-build2\atlas`) for the constant tables of the car and track vertex shaders;
- the user's disc: `Media\Camera`, `Media\RenderScenarios`, `Media\tracks\<track>\TrackSettings*.xml` and class names in default.xex;
- the runtime and integration code: [post_ao_camera.h](../../work/runtime/include/rex/graphics/d3d12/post_ao_camera.h), [post_ao.cpp](../../work/runtime/src/graphics/d3d12/post_ao.cpp), [command_processor.cpp](../../work/runtime/src/graphics/d3d12/command_processor.cpp), [fm4-render-overrides.cpp](../../work/fm4-rex/src/fm4-render-overrides.cpp) and [fm4-track-settings.h](../../work/fm4-rex/src/fm4-track-settings.h).

## Summary

- **The game already computes per-object reprojection.** Motion blur `4746CAD32EF168CF` reconstructs each pixel's clip position from the scene depth, picks one of 17 matrices by the pixel's **stencil value**, and projects the pixel into the previous frame. By all signs slot 0 is the static world (camera motion only) and slots 1 to 16 are the cars. FM4 races have at most 16 cars, so 17 slots cover every car.
- **The object ID is the scene stencil.** Almost every opaque scene draw (track, cars, glass, rims, foliage) writes its stencil reference with `ALWAYS` / `REPLACE`, in every frame, races included. The motion blur reads the stencil byte and turns it into a slot as round(stencil / 7); values of 126 and above, such as those the car shadow pass inverts, count as slot 0.
- **The game runs motion blur only with cinematic cameras.** In the 12-car Quick Race runs it drew in about 265 frames, the car shots of the start sequence and the 3-2-1, and never while driving. Each camera definition carries its own `MotionBlurAmount` (0.15 for the start, replay and car reveal cameras); the driving cameras have none, and `motionBlurAmount` is 0 in all 38 `TrackSettings` files. No RenderScenario sets the title's `MotionBlur` render control.
- **Recommendation: build the vectors on the host, the same way the game does, every frame.** The runtime reads the depth and stencil the game already resolves in every race frame, captures the camera and each car's world matrix from the vertex shader constants (grouped by the draw's stencil reference), keeps the previous frame's matrices, and runs one compute pass that writes the motion vectors. This needs no change to the game's behavior and costs an estimated 0.1 ms of GPU time at 1x and 0.2 to 0.3 ms at 2x. Where the game's own motion blur runs (pre-race, replays), its 17 matrices serve as a reference to check the host matrices.
- **Not verified yet**: that race frames give each car its own stencil reference, and that slot 0 is the camera-only matrix. Both need one short traced run (section 7), which needs the owner's go-ahead.
- **Second choice**: make the game run its motion blur in races through the existing `FM4_RACE_SETTINGS` or `FM4_TRACK_SETTINGS` overrides and read its 17 matrices at the draw. Untested, and the game's blur costs 0.65 ms at 1x and about 1.9 ms at 2x unless the runtime skips it.
- **Camera-only vectors are not enough** for a racing game: in the chase and hood cameras the player's car would get the world's motion and smear under TAA.

## 1. What the game's motion blur does

### Inputs

From the container's constant table and the ucode (the pixel shader runs with vertex shader `7A476A9766579BFD`, whose only constant is `invViewport` c0):

| Input | Content |
|---|---|
| fetch 0 (`s0`) | the resolved scene color |
| fetch 1 (`s1`) | the resolved scene depth, read as `k_24_8_FLOAT` (the depth float) |
| fetch 2 (`s2`) | the same resolve, read as `k_8_8_8_8`; the shader uses only its blue channel, which is the stencil byte (bits 0-7 of the D24FS8 word) |
| interpolator 0 | xy: the texture coordinate of the pixel; zw: the pixel position in guest pixels |
| interpolator 1 | xy: the pixel's normalized device coordinates |
| `invCurrentVPThenPrevVP` c0, 68 registers | 17 matrices of 4 registers |
| `viewport` c68 | x scale, x offset, y scale, y offset of the guest viewport, in pixels |
| `QAndNegNearQAndScale` c69 | x = Q and y = -Q*near, the same values as the DOF `combineQVals` (GTAO M1). z and w are not read by this shader. The `m1b` log shows (1.00004, -0.300012, 15.6 to 32.0, 11.25) |
| literals c250-c255 | 126/255 (0.494118), 4, 255/7 (36.4286), the twelve tap fractions (k/12), 0.5, 1, 1/720 and 1/1280 |

The `REX_GPU_INVENTORY` agrees: the pair draws once per frame, as a rect list, into a 1x `2_10_10_10_FLOAT` target with depth and stencil off, and fetches three textures (`k_8_8_8_8`, `k_24_8_FLOAT` and the scene as `k_2_10_10_10_AS_16_16_16_16`). The photo variants `8B1A70675335075E` and `1CA7A58B97F95E21` hold the same matrices one register higher (c1-c68, after `equalWeights`); none of the photo variants has drawn in any run ([look mod coverage](look-mod-coverage.md)).

### Per pixel

1. Linear depth: W = -Q*near / ((1 - Q) - d), with d the stored depth (Z is inverted through the viewport, as in the GTAO design).
2. Current clip position: (ndc.x * W, ndc.y * W, (1 - d) * W, W), with ndc from interpolator 1.
3. Slot: s = the stencil byte. If s < 126, slot = round(s / 7) (the shader multiplies by 255/7, adds 0.5 and floors); otherwise slot 0. The address register is a0 = 4 * slot.
4. Previous clip position: rows 0, 1 and 3 of the slot's matrix times the current clip position (row 2, the depth, is not needed).
5. Previous pixel: the previous clip xy divided by its w, through `viewport` c68. The motion is (previous pixel - current pixel) in guest pixels; the shader divides by 1280 and 720 to get texture coordinates.
6. Blur: 12 taps along the segment from the current pixel minus half the motion to the current pixel plus half the motion, plus the center. A tap is rejected when its slot is higher than the center's slot, so the static world (slot 0) never gathers car colors, while a car can gather the background behind it.

So the matrix of a slot maps the **current** clip position to the **previous** clip position: for the static world PrevVP * CurVP^-1, for a car PrevVP * PrevWorld * CurWorld^-1 * CurVP^-1 (column-vector notation), exactly what a motion vector pass needs. The shader has no blur-amount term, so the camera's `MotionBlurAmount` (or the photo shutter speed) must be folded into the matrices or into the "previous" camera the game passes. How is not known (section 7, check 2).

### Who writes the stencil

The inventory lists `RB_DEPTHCONTROL` for every shader pair. In the Alps run, 236 pair states have the stencil enabled. The scene draws use stencil function `ALWAYS` with `REPLACE` on depth pass, for example:

- track: road `C81F8D92FF6529EB` / `5E33CD06E3AF5B04`, objects `98BCD9067E26A4A2`, `1F07CE0FD94A46C6`, trees `7B8B9FF06041DCE5` / `9C8D86C330784DD7`; the road lines `49E644093DA8F52F` use `EQUAL` with `REPLACE` and no depth write, so they draw only where the stencil holds the track's value;
- cars: body `8D05A46C3584CA9E` / `B85EF04779ABD438`, the `42CB1356564CF9FA` family, chrome `AF6AA26032BC613D`, rims, spinning-wheel blur `10BE203F234535A7`, glass `E62CFDF09D758B6C` (depth write off, stencil still replaced), lamps.

Each of these pairs has a single depth control value across the whole run (each draws in 8,748 to 11,453 of the 16,200 frames, races included), so the stencil is written in every frame, not only when motion blur runs. The value written is the draw's stencil reference (`RB_STENCILREFMASK`), which no existing trace records per draw: the occlusion draw trace records it only for the draws that follow occlusion queries (track geometry, all with reference 0).

Two shadow passes use the stencil afterwards:

- the car shadow on the track (`CC292E3F829D24CC` / `248CBDF02EEBD66F`, constants `TrackGeoShadow*`): stencil `EQUAL`, `INVERT` on pass;
- the drop shadow (`D98372B7CDF34753`, `DropShadow.fxobj`): stencil `EQUAL`, keep.

An inverted small value lands at 255 - s, at least 143 for s up to 112, so the threshold of 126 sends the track pixels under a car shadow back to slot 0. This fits the reading that the track writes 0 and car n writes 7n (0, 7, ..., 112): 17 values, all below 126. The multiples of 7 are inferred from the shader, not seen in a trace.

### Answers to the slot questions

- **How the index is derived**: from all 8 bits of the scene stencil, as round(s / 7) below 126, else 0, times 4 registers per matrix.
- **Camera slot**: slot 0, inferred from the threshold (everything that is not an object ID falls to 0), from the shadow inversion, and from the track being drawn with the same `REPLACE` state. To be confirmed by comparing c0-c3 with the camera captured by `post_ao_camera` (section 7).
- **Cars**: slots 1 to 16, one per car; which car gets which slot is not known (grid order is the obvious guess).
- **Beyond 16 cars**: FM4 races have at most 16 cars (the `16-Car_*` render scenarios). A stencil value of 119 to 125 would give slot 17 or 18, which reads `viewport` and `QAndNegNearQAndScale` as a matrix; presumably the game never writes such a value.

## 2. When the game runs motion blur

### Observed

- **Frame graph** (M3 at 1x, every fifth frame): the second depth resolve per tile, to the same address, appears in frames 4800 to 6140 (the pre-race menu with its track flyover, then the start sequence). The motion blur pass itself (one draw, 1x, reading the depth resolve and the scene, then resolved back over the scene) appears only in frames 5880 to 6140: 53 sampled frames, about 265 frames in all. From frame 6145, when the race starts, to the end of the run (17,625) there is neither. The 2x trace matches: the second resolve in frames 4730 to 6065, the pass in frames 5805 to 6065.
- **Inventory**: `4746CAD32EF168CF` drew 264 times in 264 of 16,200 frames in the Alps run; the look mod coverage runs found 258 to 270 in races without a replay, and 2,809 frames in a career run with a replay.
- **`m1b` log and captures**: `c69` was logged from frame 5879 to 6119; the captures at those times show the start sequence (the car seen from behind with the vignette, the start lights) and end with "GO!" in the cockpit view, after which no further `c69` line appears.

So the game's motion blur is **off while driving**, in every camera. It runs in the start sequence, in replays and (by its technique names) in photo mode.

### What controls it

- **Cameras**: each cinematic camera has a `PostEffectsAnim` element with `MotionBlurAmount`. `321Cams.xml` (the 3-2-1, 4 cameras) and `carrevealcams.xml` use 0.15; `CarRelativeCams.xml` (138 car-relative cameras, presumably those of replays) uses 0.095 to 0.181, mostly 0.15; `ColorSelectCams.xml` mixes 0 and 0.15; the HomeSpace cameras use 0. The driving cameras (`CameraPhysics.xml`) have no such element.
- **Tracks**: every `TrackSettings*.xml` (38 files) sets `motionBlurAmount` to 0, next to the DOF defaults (`dofFocalDistance`, `dofFocalLength`, `dofFocalMaxBlur`, also 0). These look like the defaults for cameras without their own post effects, that is the driving cameras.
- **Render scenarios**: default.xex has a `CDynamicRenderMotionBlurControl` next to the controls the scenario files use (`DepthOfField`, `TilingScenario` and 33 others), so `<MotionBlur .../>` is a valid `DynamicRenderSettings` element. None of the 33 scenario files sets it. The render thread has an enable message and a mode message (the modes match the four techniques: replay, photo, photo fast, photo complete).
- The second depth resolve already runs in the pre-race menu when the pass does not, so the resolve and the pass have different conditions.

### Could the overrides enable it every frame?

Two existing switches could, with no code change:

- `FM4_RACE_SETTINGS="MotionBlur=1"` adds the element to the race scenarios ([fm4-render-overrides.cpp](../../work/fm4-rex/src/fm4-render-overrides.cpp)). If the title's default is already on, this changes nothing.
- `FM4_TRACK_SETTINGS="motionBlurAmount.value=<a>"` ([fm4-track-settings.h](../../work/fm4-rex/src/fm4-track-settings.h)) gives the driving cameras a nonzero amount.

Neither has been tried. Even if one works:

- If the amount is folded into the matrices (the shader has no amount term), the runtime has to scale the motion back by 1/a. A small a hides the blur but loses precision; a large a needs the blur hidden by a mod. The existing look knob `motion_blur=0` already returns the unblurred sample, but the shader still runs all its taps.
- It costs GPU time (M3 frame graphs, median per frame): three extra depth resolves (0.064 ms each at 1x, 0.14 ms at 2x), the pass (0.29 ms at 1x, 1.16 ms at 2x) and the resolve of its output over the scene (0.16 ms at 1x, 0.28 ms at 2x), so about 0.65 ms at 1x and 1.9 ms at 2x. The runtime could drop the pass and its resolve, but that is a new kind of draw surgery.
- The pass runs after all scene tiles and before bloom, so its matrices arrive in time for a motion vector pass at that point.

## 3. Host-built motion vectors

### 3.1 Camera only

- **Current camera**: `post_ao_camera` already reads the scene camera vertex shaders' constants on the first scene draw of each frame. It derives only P today, but the same constants give the view-projection: the road VS `C81F8D92FF6529EB` has `wvp` c128, `world` c140 and `viewInv` c243 (column vectors); the car body VS `8D05A46C3584CA9E` has `matWVP` c0 and `matW` c8 (row vectors), so VP = W^-1 * WVP.
- **Previous camera**: keep last frame's VP. The reprojection matrix PrevVP * CurVP^-1 is formed on the CPU in double, so the large world coordinates of the tracks (several kilometers) cancel, as the game's own matrix does.
- **Pass**: one compute pass after the last scene depth resolve, next to GTAO: read the depth (the texture GTAO already uses), rebuild the clip position as in section 1, multiply, write the motion in render pixels (RG16F).
- **Cost**: CPU none beyond 16 floats a frame; GPU one depth fetch and a 4x4 product per pixel, an estimated 0.03 to 0.05 ms at 1x and 0.1 ms at 2x (by analogy with the GTAO prefilter, not measured).
- **Limit**: every car gets the motion of the static world. In the chase, hood and bumper cameras the player's car is almost still on screen while the world streams past, so TAA would smear it. Treating all stencil-marked car pixels as "moves with the camera" fixes the player's car in those cameras but leaves every other car wrong.

### 3.2 Per car (recommended)

The car vertex shaders in the material atlas all have `matWVP` at c0 and `matW` (the world matrix) at c8 ( body `8D05A46C3584CA9E`, the `42CB1356564CF9FA` family, chrome `A42BEA3C0A6E5603`, rims `C00542278E7CCC25`, glass `F936B2AD861F53BE`, lamps `EFE6E73205BB1F64` and `A7B129D19544EFED`, tires `8317040277B5F496`, the car shadow `CC292E3F829D24CC`). So:

1. **Capture** (in `IssueDraw`, as `post_ao` does for the camera): for each scene draw whose vertex shader is in a configured list (a `post_mv_objects`-style cvar, like `post_ao_camera`), read the stencil reference from `RB_STENCILREFMASK` and `matW` c8-c11. The body's matrix (the paint VS) wins for each reference: the wheels are drawn by their own vertex shaders and probably carry their own `matW`, which is not the car's.
2. **Matrices**: per reference r, M_r = PrevVP * PrevW_r * CurW_r^-1 * CurVP^-1, in double, with the previous frame's W_r looked up by r; M_0 = PrevVP * CurVP^-1 for the track. A reference with no previous entry (a car coming into view, a camera cut) falls back to M_0 and is flagged.
3. **Pass**: as in 3.1, plus the stencil byte from the same resolved memory, read as `k_8_8_8_8` the way the game's motion blur reads it (a second texture-cache load of the 1280x720 resolve), and the slot rule of section 1. Output: the motion in render pixels, and optionally the slot for debugging.
4. **Check**: in frames where the game's motion blur runs, log the game's c0-c67 and compare each slot with the host matrix of the same stencil value.

- **CPU**: about 600 car draws per frame with cars on screen in the Alps run (counted over all tiles); each costs one register and 16 floats. Negligible.
- **GPU**: the pass of 3.1 plus one stencil fetch and a constant-buffer lookup, plus the stencil texture load (about the cost of one depth resolve: 0.06 ms at 1x, 0.14 ms at 2x). About 0.1 ms at 1x and 0.2 to 0.3 ms at 2x in total, estimated.
- **Depends on**: distinct stencil references per car in race frames (section 7, check 1). If the race writes one shared reference for all cars, the runtime can give each car its own value on the host only (it already sets the D3D12 stencil reference from the guest register per draw), choosing values that keep the shadow passes' `EQUAL` tests against the track's value intact. That is a deeper change and needs its own check.

### 3.3 Cost comparison

| Route | Coverage | CPU per frame | GPU at 1x | GPU at 2x | Engineering | Main risk |
|---|---|---|---|---|---|---|
| Game blur every frame (section 2) | world and 16 cars, the game's own IDs | read 70 constants at one draw | about 0.65 ms (resolves, blur, resolve) + 0.03 ms for vectors | about 1.9 ms + 0.1 ms | low in the runtime, plus hiding or skipping the blur | the switches may not work in races; the amount is folded in |
| Host per car (3.2) | world and every car with a stencil ID | about 600 draws x 17 values | about 0.1 ms (estimate) | 0.2 to 0.3 ms (estimate) | medium: capture, matrix table, one pass | per-car stencil references in races not verified |
| Camera only (3.1) | static world | 16 floats | 0.03 to 0.05 ms (estimate) | about 0.1 ms (estimate) | low | all cars wrong |

## 4. Recommendation

1. Build the host per-car route (3.2) as the motion vector source of the generic pass framework. It works in races, replays, photo mode and the start sequence alike, and it is cheap.
2. Use the game's own motion blur frames (start sequence, replays) as the reference for the host matrices, through the constant log.
3. Keep the "game blur every frame" switches as an experiment only: if section 7 shows that race frames have no per-car stencil IDs, check 3 tells whether turning the game's blur on brings them.
4. Do not ship camera-only vectors for TAA.

## 5. Gaps

The game's reprojection and the host route share these, because both use one matrix per stencil ID and the depth of the opaque surface:

- **Wheels**: rims, tires and the spinning-wheel blur write the car's stencil, so they get the body's motion and no spin. Spokes will smear or flicker under TAA. The tire VS has `tireRotation` c123 and the rim VS its own `matW`, but there is no per-pixel wheel ID. Options: a reactive mask on the wheel draws, or less history weight on high-contrast moving detail.
- **Particles, smoke, sparks, dust**: blended, no depth write. Their pixels get the motion of the surface behind. They need a reactive or transparency mask. FSR 2 and later can derive one from the opaque-only color and the final color, which the runtime could take as a copy of the scene before the first blended draw of each tile (the blend state of each draw tells which ones blend).
- **Glass**: car windows write the car's stencil but not depth, so the track seen through a car's windows gets that car's matrix with the track's depth. Small for the player's car in the chase camera (it moves with the camera), visible on fast cars passing.
- **Alpha-tested foliage and fences**: they write depth and the track's stencil, so their static motion is correct, which is what TAA needs against their shimmer. Any wind sway done in the tree vertex shaders is not in the vectors; with small sway this is acceptable.
- **Animated track objects** (flags, cable cars, crowd): drawn with the track's ID, so treated as static.
- **Cockpit interior**: drawn with a compressed depth range (GTAO M3), so the depth reconstruction is wrong there. The interior carries the player's car ID and moves with the camera, so its reprojection is close to identity whatever the depth; the error stays small. The interior mirror shows a moving image on a surface that does not move, which needs the reactive mask.
- **Sky**: at the far plane, it gets the camera's rotation, which is correct for a distant dome.
- **MSAA edges**: the depth resolve takes one sample while the color resolve averages, so the ID and depth at an edge belong to sample 0.
- **Camera cuts**: replays and the start sequence cut between cameras. The pass framework must reset the history on a cut. A cut can be detected on the host (a jump in camera position or orientation beyond what one frame allows, or a change of FOV).
- **Split screen**: two views per frame, not handled (as for GTAO).

## 6. Requirements for TAA and the upscalers

### Jitter

- The translated vertex shaders already apply the system constant `ndc_offset` after the guest's own transform ([command_processor.cpp](../../work/runtime/src/graphics/d3d12/command_processor.cpp) fills it from `GetHostViewportInfo`). A sub-pixel jitter is an addition to `ndc_offset.xy`, 2 * jitter / viewport size in host pixels, with the sign of the guest viewport scale. No vertex shader changes.
- Jitter only the scene draws: those whose depth buffer is the scene depth EDRAM base (the same test `post_ao_camera` uses), on every tile of the frame. Shadow maps, the environment cube, the mirrors, the post chain and the HUD stay unjittered.
- Use a Halton (2, 3) sequence in render pixels. The motion vectors are built from the unjittered matrices, and the pass undoes the jitter of the current pixel center, so the vectors contain no jitter; the jitter is passed to the upscaler separately.
- Other depth readers (DOF, soft particles, GTAO, the game's motion blur) see the jittered depth of the same frame, which is consistent.

### No-MSAA tiling

- DLSS and FSR expect aliased, jittered input. The game's 2x or 4x MSAA averages colors while the depth resolve takes one sample, and the emulated EDRAM tiles cost time.
- `FM4_TILING_SCENARIO=2` (`-SingleTile`) already selects the title's UI tiling for races: one 720p tile without MSAA. On ROV it raised fps from 54.3 to 58.6 (14 October 2026), which pays for part of a temporal pass.
- Not verified: that the game still resolves the scene depth in races with tiling 2. GTAO and the motion vectors both need that resolve. The GTAO design rejected tiling 2 only for losing MSAA.

### Injection point

- **TAA** belongs after the last scene color tile resolve and before the game's motion blur, bloom and FinalCombine: the same frame position as GTAO, but one resolve later (the color resolve follows the depth resolve of the last tile). Its output must replace what the game reads at the scene texture address, either by writing the resolve buffer in its tiled layout or by having the texture cache substitute the host texture for that frame. This is a requirement on the generic pass framework.
- **Upscalers before the HUD**: the HUD and the HUD mirror are drawn onto the FinalCombine output (about 40 draws on the same surface) before the front-buffer resolve. An upscaler must therefore run on the scene, before bloom and FinalCombine, and everything after it (bloom, FinalCombine, HUD) must run at the output resolution. The runtime has one draw resolution scale for all surfaces today; upscaling needs a separate scale for the scene tiles and for the post chain and HUD, including the texture cache serving the upscaled scene to the bloom and FinalCombine fetches. This is the largest piece of work.
- A first stage without that work: TAA, or DLSS or FSR in native anti-aliasing mode, at the existing render scale.

### Inputs the upscalers need

- **Color**: the scene, rendered as 7e3 and resolved as `2_10_10_10`, stored in gamma 2 (FinalCombine squares it). Square it to linear before accumulation.
- **Depth**: the resolved depth, with Z inverted through the viewport (stored 1 at the near plane, 0 at the far plane), so it is passed as inverted depth.
- **Motion**: render pixels, previous minus current, without jitter (section 3).
- **Exposure**: the game computes its exposure from a 32x32 luminance target after the scene. Pass the previous frame's value, or use the upscaler's own auto-exposure.
- **Reactive mask**: for particles, glass and the interior mirror (section 5).
- **History reset** on camera cuts (section 5).
- **Licensing**: the FSR SDK and the DLSS SDK need a review in [sources and licensing](../sources-and-licensing.md) before either is added.

## 7. Validation steps

These need a game run. Ask the owner before starting one.

1. **Stencil IDs per draw**: add the stencil reference to `REX_DRAW_LIST_TRACE` (or log `RB_STENCILREFMASK` for the car and track vertex shaders), and trace one race frame and one start sequence frame. Expected: the track at 0 and each car at its own multiple of 7. If all cars share one value in the race, see section 3.2.
2. **The game's matrices**: `post_ao_log_constants` with `4746CAD32EF168CF` c0-c11 (slots 0 to 2) during the start sequence, against the host camera of the same frames: slot 0 should equal PrevVP * CurVP^-1, possibly scaled by the camera's `MotionBlurAmount`.
3. **Switches in a race**: one run each with `FM4_RACE_SETTINGS="MotionBlur=1"` and with `FM4_TRACK_SETTINGS="motionBlurAmount.value=0.15"`, with `REX_GPU_INVENTORY` and `REX_FRAME_GRAPH`: does `4746CAD32EF168CF` draw in race frames, and does the second depth resolve appear?
4. **Single tile**: `-SingleTile` with `REX_FRAME_GRAPH`: is the scene depth still resolved in race frames?
