# Motion vectors: sources and design

Research note on where the runtime can get per-pixel motion vectors for FM4, as groundwork for TAA, temporal SSR and the FSR or DLSS upscalers. Nothing here is implemented. Today the runtime injects one screen pass, GTAO ([GTAO design](gtao-design.md)), on the [post pass framework](post-pass-framework.md), which keeps the previous frame's scene color, depth and camera but no motion vectors.

This note contains no game code, shaders or ucode. Addresses, hashes, registers, constant values and file element names are observations only.

The offline research (sections 1 to 6) was checked in the game on 18 October 2026: [section 7](#7-validation-in-the-game-18-october-2026) has the runs and their results.

Offline sources:

- the local ucode disassembly of the motion blur pixel shader `4746CAD32EF168CF` (a `dump_shaders` run in `I:\FM4-build2`), and the constant table and literal block of its container in default.xex (read through `<BuildRoot>\loaded-image.bin` with the container parser of `material_atlas.py`);
- the `REX_GPU_INVENTORY` of a Quick Race on the Bernese Alps (`I:\FM4-build2`, 16,200 frames): render state per shader pair, including `RB_DEPTHCONTROL`;
- the `REX_FRAME_GRAPH` traces of GTAO M3 (`I:\FM4-build-gtao`: `m3-1x`, `m3-2x`, `m3b-2x`, every fifth frame, 4x MSAA, three tiles). The q-high traces named in the GTAO design are no longer on disk; the M3 traces show the same pattern;
- the GTAO M1 run `m1b` (`I:\FM4-build-gtao`), which logged the motion blur constant `c69` with `post_ao_log_constants`, and its screen captures;
- the material atlas (`I:\FM4-build2\atlas`) for the constant tables of the car and track vertex shaders;
- the user's disc: `Media\Camera`, `Media\RenderScenarios`, `Media\tracks\<track>\TrackSettings*.xml` and class names in default.xex;
- the runtime and integration code: `post_ao_camera.h` and `post_ao.cpp` (since 18 October 2026 [post_pass_camera.h](../../work/runtime/include/rex/graphics/d3d12/post_pass_camera.h) and [post_pass.cpp](../../work/runtime/src/graphics/d3d12/post_pass.cpp), the [post pass framework](post-pass-framework.md)), [command_processor.cpp](../../work/runtime/src/graphics/d3d12/command_processor.cpp), [fm4-render-overrides.cpp](../../work/fm4-rex/src/fm4-render-overrides.cpp) and [fm4-track-settings.h](../../work/fm4-rex/src/fm4-track-settings.h).

## Summary

- **The game already computes per-object reprojection.** Motion blur `4746CAD32EF168CF` reconstructs each pixel's clip position from the scene depth, picks one of 17 matrices by the pixel's **stencil value**, and moves the pixel toward its position in the previous frame. Slot 0 is the static world (camera motion only) and slots 1 to 16 are the cars, one per car; in a 12-car race the unused slots 13 to 16 hold the camera matrix of slot 0 (verified in the game, section 7).
- **The object ID is the scene stencil.** Every static scene draw (track, objects, trees, road) writes 0 and every draw of car n writes 7n (7 to 84 in a 12-car race), with `ALWAYS` / `REPLACE`, in every frame, races included (verified in the game, section 7). The motion blur turns the stencil byte into a slot as round(stencil / 7); the shadow passes mark track pixels with 128 and 129, which count as slot 0 because they are 126 or more.
- **The game runs motion blur only with cinematic cameras.** In the 12-car Quick Race runs it drew in about 265 frames, the car shots of the start sequence and the 3-2-1, and never while driving. Each camera definition carries its own `MotionBlurAmount` (0.15 for the start, replay and car reveal cameras); the driving cameras have none, and `motionBlurAmount` is 0 in all 38 `TrackSettings` files. No RenderScenario sets the title's `MotionBlur` render control, and neither setting it nor a nonzero track `motionBlurAmount` makes the blur run while driving (section 7).
- **The game's matrices are the host matrices, scaled.** In the start sequence each slot equals I + k (M - I), where M is the frame-to-frame reprojection the host builds from the camera and the car's world matrix, and k is about 0.34 for the world and every car alike. So the game folds a blur amount into the matrices, and the host route reproduces the game's own reprojection.
- **Recommendation: build the vectors on the host, the same way the game does, every frame.** The runtime reads the depth and stencil the game already resolves in every race frame, captures the camera and each car's world matrix from the vertex shader constants (grouped by the draw's stencil reference), keeps the previous frame's matrices, and runs one compute pass that writes the motion vectors. This needs no change to the game's behavior and costs an estimated 0.1 ms of GPU time at 1x and 0.2 to 0.3 ms at 2x. Where the game's own motion blur runs (pre-race, replays), its 17 matrices serve as a reference to check the host matrices.
- **Second choice ruled out**: the existing `FM4_RACE_SETTINGS="MotionBlur=1"` and `FM4_TRACK_SETTINGS="motionBlurAmount.value=0.15"` overrides do not make the game run its motion blur in races, so the game's matrices exist only in cinematic frames.
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

So the matrix of a slot maps the **current** clip position toward the **previous** clip position: for the static world PrevVP * CurVP^-1, for a car PrevVP * PrevWorld * CurWorld^-1 * CurVP^-1 (column-vector notation), exactly what a motion vector pass needs. The shader has no blur-amount term, so the camera's `MotionBlurAmount` (or the photo shutter speed) is folded into the matrices: in the game each slot is I + k (M - I) with M the full reprojection above and k about 0.34 in the start sequence (section 7, check 2).

### Who writes the stencil

The inventory lists `RB_DEPTHCONTROL` for every shader pair. In the Alps run, 236 pair states have the stencil enabled. The scene draws use stencil function `ALWAYS` with `REPLACE` on depth pass, for example:

- track: road `C81F8D92FF6529EB` / `5E33CD06E3AF5B04`, objects `98BCD9067E26A4A2`, `1F07CE0FD94A46C6`, trees `7B8B9FF06041DCE5` / `9C8D86C330784DD7`; the road lines `49E644093DA8F52F` use `EQUAL` with `REPLACE` and no depth write, so they draw only where the stencil holds the track's value;
- cars: body `8D05A46C3584CA9E` / `B85EF04779ABD438`, the `42CB1356564CF9FA` family, chrome `AF6AA26032BC613D`, rims, spinning-wheel blur `10BE203F234535A7`, glass `E62CFDF09D758B6C` (depth write off, stencil still replaced), lamps.

Each of these pairs has a single depth control value across the whole run (each draws in 8,748 to 11,453 of the 16,200 frames, races included), so the stencil is written in every frame, not only when motion blur runs. The value written is the draw's stencil reference (`RB_STENCILREFMASK`), which `REX_DRAW_LIST_TRACE` records per draw since 18 October 2026.

Measured in the game (section 7, check 1), with all masks `FF` unless named:

| Stencil value | Written by | Test and operation |
|---|---|---|
| 0 | every static scene draw: road, objects, trees; the road lines `49E644093DA8F52F` draw only over it | `ALWAYS` / `REPLACE`; the road lines `EQUAL` 0 / `REPLACE` |
| 7n | every draw of car n, n = 1 to 12 in a 12-car race: body `8D05A46C3584CA9E`, the `42CB1356564CF9FA` family, glass `F936B2AD861F53BE`, rims `C00542278E7CCC25`, chrome `A42BEA3C0A6E5603`, lamps `EFE6E73205BB1F64` and `A7B129D19544EFED`, tires `8317040277B5F496`, `7B69DF9819BBA422`, `D7B380C7A7F72EF5`, `2D98065BE6F7E073`; for the player's car also the cockpit interior (`4EE552137CA415E3`, `918AF0171F6F6F4A`, `BBD01C26B1C45733` and others) | `ALWAYS` / `REPLACE` |
| 128 | shadow draws over the track (vertex shaders `0A182E9DA8E45F73` and `16F25D856B5CA825` with the pixel shaders `248CBDF02EEBD66F`, `BB5743878E0E85BC` and `D98372B7CDF34753`) | `EQUAL` 0, `INVERT` with write mask `80` |
| 129 | the car shadow on the track (`CC292E3F829D24CC`, `E803527E29EC0BD8`, `F2E1966AFEA0615A` with `248CBDF02EEBD66F`, constants `TrackGeoShadow*`) | `EQUAL` 128, `INVERT` with write mask `01` |
| read only | the drop shadow (`A18507753B3C6731` / `D98372B7CDF34753`, `DropShadow.fxobj`) | `EQUAL` 128 with mask `FE` (128 or 129), keep |

So the shadow passes flip single bits of the track's 0, and the threshold of 126 sends the track pixels under a shadow (128, 129) back to slot 0. Car pixels keep 7n, because every shadow test needs 0 or 128.

### Answers to the slot questions

- **How the index is derived**: from all 8 bits of the scene stencil, as round(s / 7) below 126, else 0, times 4 registers per matrix.
- **Camera slot**: slot 0. Verified in the game: c0-c3 equal the scene camera's reprojection PrevVP * CurVP^-1, scaled by k (section 7, check 2).
- **Cars**: slots 1 to 16, one per car: car n writes 7n and gets slot n. The 12 cars of each measured race had the multiples of 7 from 7 to 84, and each car kept its value from the start sequence through the race. The player's car, the only car with an ID in the pre-race frames, had 35, 49, 42 and 42 in four races, so the slot is not tied to the player (the grid position is the likely order, not checked). The unused slots hold slot 0's matrix.
- **Beyond 16 cars**: FM4 races have at most 16 cars (the `16-Car_*` render scenarios). A stencil value of 119 to 125 would give slot 17 or 18, which reads `viewport` and `QAndNegNearQAndScale` as a matrix; presumably the game never writes such a value. A 16-car grid was not traced.

## 2. When the game runs motion blur

### Observed

- **Frame graph** (M3 at 1x, every fifth frame): the second depth resolve per tile, to the same address, appears in frames 4800 to 6140 (the pre-race menu with its track flyover, then the start sequence). The motion blur pass itself (one draw, 1x, reading the depth resolve and the scene, then resolved back over the scene) appears only in frames 5880 to 6140: 53 sampled frames, about 265 frames in all. From frame 6145, when the race starts, to the end of the run (17,625) there is neither. The 2x trace matches: the second resolve in frames 4730 to 6065, the pass in frames 5805 to 6065.
- **Inventory**: `4746CAD32EF168CF` drew 264 times in 264 of 16,200 frames in the Alps run; the look mod coverage runs found 258 to 270 in races without a replay, and 2,809 frames in a career run with a replay.
- **`m1b` log and captures**: `c69` was logged from frame 5879 to 6119; the captures at those times show the start sequence (the car seen from behind with the vignette, the start lights) and end with "GO!" in the cockpit view, after which no further `c69` line appears.
- **Runs of 18 October 2026** (section 7, USA release, 60 Hz): the same pattern in all four races. The pass drew in 267, 267, 269 and 256 frames (frames 5878 to 6144 in the first run), the second depth resolve per tile ran in the pre-race menu, the start sequence and photo mode, and race frames had neither. Photo mode resolves the depth twice per tile but drew no motion blur pass while the scenario's photo was taken.

So the game's motion blur is **off while driving**, in every camera. It runs in the start sequence, in replays and (by its technique names) in photo mode.

### What controls it

- **Cameras**: each cinematic camera has a `PostEffectsAnim` element with `MotionBlurAmount`. `321Cams.xml` (the 3-2-1, 4 cameras) and `carrevealcams.xml` use 0.15; `CarRelativeCams.xml` (138 car-relative cameras, presumably those of replays) uses 0.095 to 0.181, mostly 0.15; `ColorSelectCams.xml` mixes 0 and 0.15; the HomeSpace cameras use 0. The driving cameras (`CameraPhysics.xml`) have no such element.
- **Tracks**: every `TrackSettings*.xml` (38 files) sets `motionBlurAmount` to 0, next to the DOF defaults (`dofFocalDistance`, `dofFocalLength`, `dofFocalMaxBlur`, also 0). These look like the defaults for cameras without their own post effects, that is the driving cameras.
- **Render scenarios**: default.xex has a `CDynamicRenderMotionBlurControl` next to the controls the scenario files use (`DepthOfField`, `TilingScenario` and 33 others), so `<MotionBlur .../>` is a valid `DynamicRenderSettings` element. None of the 33 scenario files sets it. The render thread has an enable message and a mode message (the modes match the four techniques: replay, photo, photo fast, photo complete).
- The second depth resolve already runs in the pre-race menu when the pass does not, so the resolve and the pass have different conditions.

### Could the overrides enable it every frame?

No, not with the existing switches (section 7, check 3). Two switches looked able to, with no code change:

- `FM4_RACE_SETTINGS="MotionBlur=1"` adds `<MotionBlur value="1"/>` to the race scenarios ([fm4-render-overrides.cpp](../../work/fm4-rex/src/fm4-render-overrides.cpp)).
- `FM4_TRACK_SETTINGS="motionBlurAmount.value=<a>"` ([fm4-track-settings.h](../../work/fm4-rex/src/fm4-track-settings.h)) gives the driving cameras a nonzero amount.

With either, the pass still drew only in the start sequence (267 and 269 frames, against 267 without them), and race frames resolved the depth once per tile. Making the blur run while driving would need a change inside the title (the render thread's enable and mode messages), which is not worth it next to the host route. Had a switch worked, it would also have cost GPU time (M3 frame graphs, median per frame): three extra depth resolves (0.064 ms each at 1x, 0.14 ms at 2x), the pass (0.29 ms at 1x, 1.16 ms at 2x) and the resolve of its output over the scene (0.16 ms at 1x, 0.28 ms at 2x), so about 0.65 ms at 1x and 1.9 ms at 2x, and the runtime would have had to undo the folded amount (section 1).

## 3. Host-built motion vectors

### 3.1 Camera only

- **Current camera**: the scene camera (`post_ao_camera`, since 18 October 2026 `[scene] camera` of the [post pass framework](post-pass-framework.md)) already reads the scene camera vertex shaders' constants on the first scene draw of each frame. It derives P, and from the road's view inverse the view to world matrix and the reprojection of a view position into the previous frame's view (`xe_view_to_previous_view`); the same constants give the view-projection: the road VS `C81F8D92FF6529EB` has `wvp` c128, `world` c140 and `viewInv` c243 (column vectors); the car body VS `8D05A46C3584CA9E` has `matWVP` c0 and `matW` c8 (row vectors), so VP = W^-1 * WVP.
- **Previous camera**: keep last frame's VP (the post pass framework keeps the previous frame's camera, scene color and depth since 18 October 2026). The reprojection matrix PrevVP * CurVP^-1 is formed on the CPU in double, so the large world coordinates of the tracks (several kilometers) cancel, as the game's own matrix does.
- **Pass**: one compute pass after the last scene depth resolve, next to GTAO: read the depth (the texture GTAO already uses), rebuild the clip position as in section 1, multiply, write the motion in render pixels (RG16F).
- **Cost**: CPU none beyond 16 floats a frame; GPU one depth fetch and a 4x4 product per pixel, an estimated 0.03 to 0.05 ms at 1x and 0.1 ms at 2x (by analogy with the GTAO prefilter, not measured).
- **Limit**: every car gets the motion of the static world. In the chase, hood and bumper cameras the player's car is almost still on screen while the world streams past, so TAA would smear it. Treating all stencil-marked car pixels as "moves with the camera" fixes the player's car in those cameras but leaves every other car wrong.

### 3.2 Per car (recommended)

The car vertex shaders in the material atlas all have `matWVP` at c0 and `matW` (the world matrix) at c8 ( body `8D05A46C3584CA9E`, the `42CB1356564CF9FA` family, chrome `A42BEA3C0A6E5603`, rims `C00542278E7CCC25`, glass `F936B2AD861F53BE`, lamps `EFE6E73205BB1F64` and `A7B129D19544EFED`, tires `8317040277B5F496`, the car shadow `CC292E3F829D24CC`). So:

1. **Capture** (in `IssueDraw`, as the post passes do for the camera): for each draw on the scene depth buffer whose vertex shader is in a configured list (like the post passes' `[scene] camera`), read the stencil reference from `RB_STENCILREFMASK` and `matW` c8-c11. Verified in the game: the body and the `42CB1356564CF9FA` family carry the same `matW` for a car, while the rims carry one per wheel (four per car), so the body's matrix must win for each reference. The rear-view mirror is a second view on its own depth buffer with the same references; the scene depth test keeps it out.
2. **Matrices**: per reference r, M_r = PrevVP * PrevW_r * CurW_r^-1 * CurVP^-1, in double, with the previous frame's W_r looked up by r; M_0 = PrevVP * CurVP^-1 for the track. VP is the scene camera's for every car: in the cockpit camera the player's car body has a projection of its own (the interior's compressed depth range), and the game's matrix for that car uses the scene camera's VP, not the interior's. A reference with no previous entry (a car coming into view, a camera cut) falls back to M_0 and is flagged.
3. **Pass**: as in 3.1, plus the stencil byte from the same resolved memory, read as `k_8_8_8_8` the way the game's motion blur reads it (a second texture-cache load of the 1280x720 resolve), and the slot rule of section 1. Output: the motion in render pixels, and optionally the slot for debugging.
4. **Check**: in frames where the game's motion blur runs, log the game's c0-c67 and compare each slot with the host matrix of the same stencil value: the game's slot is I + k (M_r - I), with k about 0.34 and within about 0.02 of slot 0's value for every car slot of a frame (section 7, check 2).

- **CPU**: about 600 car draws per frame with cars on screen in the Alps run (counted over all tiles); each costs one register and 16 floats. Negligible.
- **GPU**: the pass of 3.1 plus one stencil fetch and a constant-buffer lookup, plus the stencil texture load (about the cost of one depth resolve: 0.06 ms at 1x, 0.14 ms at 2x). About 0.1 ms at 1x and 0.2 to 0.3 ms at 2x in total, estimated.
- **Depends on**: distinct stencil references per car in race frames, verified in four races (section 7, check 1).

### 3.3 Cost comparison

| Route | Coverage | CPU per frame | GPU at 1x | GPU at 2x | Engineering | Main risk |
|---|---|---|---|---|---|---|
| Game blur every frame (section 2) | world and 16 cars, the game's own IDs | read 70 constants at one draw | about 0.65 ms (resolves, blur, resolve) + 0.03 ms for vectors | about 1.9 ms + 0.1 ms | not available: the switches do not turn it on in races (section 7) | the amount is folded in |
| Host per car (3.2) | world and every car with a stencil ID | about 600 draws x 17 values | about 0.1 ms (estimate) | 0.2 to 0.3 ms (estimate) | medium: capture, matrix table, one pass | none known: per-car references in races and the game's matching matrices verified |
| Camera only (3.1) | static world | 16 floats | 0.03 to 0.05 ms (estimate) | about 0.1 ms (estimate) | low | all cars wrong |

## 4. Recommendation

1. Build the host per-car route (3.2) as the motion vector source of the generic pass framework. It works in races, replays, photo mode and the start sequence alike, and it is cheap. The checks of section 7 confirm its inputs: per-car stencil IDs in race frames, one body `matW` per car, the scene depth resolved in every race frame (also with `-SingleTile`), and the game's own matrices built the same way.
2. Use the game's own motion blur frames (start sequence, replays) as the reference for the host matrices: `REX_DRAW_LIST_CONSTANTS` with `REX_DRAW_LIST_AFTER_PS=4746CAD32EF168CF` gives both in one trace, and the slots must equal I + k (M_r - I) with k about 0.34.
3. Drop the "game blur every frame" route: neither switch makes the game's blur run while driving.
4. Do not ship camera-only vectors for TAA.

## 5. Gaps

The game's reprojection and the host route share these, because both use one matrix per stencil ID and the depth of the opaque surface:

- **Wheels**: rims, tires and the spinning-wheel blur write the car's stencil, so they get the body's motion and no spin, in the game's blur as on the host. Spokes will smear or flicker under TAA. The tire VS has `tireRotation` c123 and each rim draw its own wheel's `matW` (four per car, measured), so the host knows the wheel motion, but there is no per-pixel wheel ID. Options: a reactive mask on the wheel draws, a host-side ID for the wheel draws (for example a separate target written by those draws), or less history weight on high-contrast moving detail.
- **Particles, smoke, sparks, dust**: blended, no depth write. Their pixels get the motion of the surface behind. They need a reactive or transparency mask. FSR 2 and later can derive one from the opaque-only color and the final color, which the runtime could take as a copy of the scene before the first blended draw of each tile (the blend state of each draw tells which ones blend).
- **Glass**: car windows write the car's stencil but not depth, so the track seen through a car's windows gets that car's matrix with the track's depth. Small for the player's car in the chase camera (it moves with the camera), visible on fast cars passing.
- **Alpha-tested foliage and fences**: they write depth and the track's stencil, so their static motion is correct, which is what TAA needs against their shimmer. Any wind sway done in the tree vertex shaders is not in the vectors; with small sway this is acceptable.
- **Animated track objects** (flags, cable cars, crowd): drawn with the track's ID, so treated as static.
- **Cockpit interior**: drawn with a compressed depth range (GTAO M3; in the cockpit camera the player's car body has its own projection), so the depth reconstruction with the scene camera is wrong there. The interior carries the player's car ID and moves with the camera, so its reprojection is close to identity whatever the depth; the error stays small. The interior mirror shows a moving image on a surface that does not move, which needs the reactive mask.
- **Sky**: at the far plane, it gets the camera's rotation, which is correct for a distant dome.
- **MSAA edges**: the depth resolve takes one sample while the color resolve averages, so the ID and depth at an edge belong to sample 0.
- **Camera cuts**: replays and the start sequence cut between cameras. The pass framework must reset the history on a cut. A cut can be detected on the host (a jump in camera position or orientation beyond what one frame allows, or a change of FOV).
- **Split screen**: two views per frame, not handled (as for GTAO).

## 6. Requirements for TAA and the upscalers

### Jitter

- The translated vertex shaders already apply the system constant `ndc_offset` after the guest's own transform ([command_processor.cpp](../../work/runtime/src/graphics/d3d12/command_processor.cpp) fills it from `GetHostViewportInfo`). A sub-pixel jitter is an addition to `ndc_offset.xy`, 2 * jitter / viewport size in host pixels, with the sign of the guest viewport scale. No vertex shader changes.
- Jitter only the scene draws: those whose depth buffer is the scene depth EDRAM base (the same test the post passes' scene camera uses), on every tile of the frame. Shadow maps, the environment cube, the mirrors, the post chain and the HUD stay unjittered.
- Use a Halton (2, 3) sequence in render pixels. The motion vectors are built from the unjittered matrices, and the pass undoes the jitter of the current pixel center, so the vectors contain no jitter; the jitter is passed to the upscaler separately.
- Other depth readers (DOF, soft particles, GTAO, the game's motion blur) see the jittered depth of the same frame, which is consistent.

### No-MSAA tiling

- DLSS and FSR expect aliased, jittered input. The game's 2x or 4x MSAA averages colors while the depth resolve takes one sample, and the emulated EDRAM tiles cost time.
- `FM4_TILING_SCENARIO=2` (`-SingleTile`) already selects the title's UI tiling for races: one 720p tile without MSAA. On ROV it raised fps from 54.3 to 58.6 (14 October 2026), which pays for part of a temporal pass.
- With tiling 2 the game still resolves the scene depth in every race frame (section 7, check 4): once, as one 1280x720 tile without MSAA, to the same address as the first of the three tiles at `-Quality high`. GTAO and the motion vectors both need that resolve. The GTAO design rejected tiling 2 only for losing MSAA.

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

## 7. Validation in the game (18 October 2026)

Four Quick Races (`photo` scenario: 12 cars on the Bernese Alps, the title's AI driver, 10 s of driving and a 30 s race phase, then photo mode), on `I:\FM4-build-usa` (USA release), main `3f7fc10` plus the trace columns below, at 60 Hz and 1x with the launcher defaults (look `pc`, quality `high`: TilingScenario 0, 4x MSAA, three tiles). Every run reached its goal (race phase 60.0 fps, photo mode 29.9 fps). Results stay in `I:\FM4-build-usa\e2e\mvA` to `mvD`, outside the repository.

| Run | Change | Traces |
|---|---|---|
| `mvA` | none | `REX_DRAW_LIST_TRACE` every 300th frame plus every frame after a motion blur frame (`REX_DRAW_LIST_AFTER_PS=4746CAD32EF168CF`), with `REX_DRAW_LIST_CONSTANTS` for c0-c11 of the body, the `42CB1356564CF9FA` family and the rims, the road's camera constants and c0-c69 of the motion blur; `REX_FRAME_GRAPH` every 5th frame; `REX_GPU_INVENTORY` |
| `mvB` | `FM4_RACE_SETTINGS` of `high` plus `MotionBlur=1` | draw list every 300th frame (body and motion blur constants), frame graph, inventory |
| `mvC` | `FM4_TRACK_SETTINGS="skyScale.value*=0.7;motionBlurAmount.value=0.15"` (the `pc` sky plus the amount) | as `mvB` |
| `mvD` | `FM4_TILING_SCENARIO=2` and `swap_post_effect=fxaa` (`-SingleTile`) | as `mvB` |

`REX_DRAW_LIST_TRACE` gained `RB_DEPTHCONTROL` and `RB_STENCILREFMASK` per draw, `REX_DRAW_LIST_CONSTANTS` and `REX_DRAW_LIST_AFTER_PS` for these checks ([traces](../e2e/traces.md#draw-traces)).

### Check 1: stencil IDs per draw

Verified. In the start sequence and in race frames of all four races, the static scene draws write 0 and every draw of car n writes 7n, n = 1 to 12, with `ALWAYS` / `REPLACE` and masks `FF` (the table in section 1). For example, in the first run:

- start sequence frame 5900 (motion blur on): the track with 668 draws at 0, six cars at 14, 21, 35, 42, 70 and 77 with 204 to 372 draws each;
- race frame 6900 (motion blur off): the track at 0; cars 28, 35 and 84 in the scene view, cars 7, 14, 21, 42, 56 and 70 in the rear-view mirror (a second view on its own depth buffer, EDRAM base 80 against the scene's 1024), with the same values in both views.

Every part of a car carries the car's value: the body `8D05A46C3584CA9E`, the `42CB1356564CF9FA` family, glass, rims `C00542278E7CCC25`, chrome, lamps, tires and the cockpit interior of the player's car. The body and the `42CB1356564CF9FA` draws of a car share one `matW`; the rim draws have four, one per wheel. A car keeps its value for the whole race. The player's car had 35, 49, 42 and 42 in the four races. The shadow passes turn the track's 0 under a shadow into 128 and 129 (section 1).

### Check 2: the game's matrices

Verified, with a scale. In `mvA` the motion blur drew in frames 5878 to 6144, and the draw list holds every frame from 5879 to 6145: 265 pairs of consecutive frames with the blur in the second one. The host camera is VP = W^-1 * WVP from the body constants (row vectors, `matWVP` c0, `matW` c8); all body draws of the scene camera agree to about 1e-5. Results:

- **Slot 0** is the camera reprojection, scaled: c0-c3 equal I + k (PrevVP * CurVP^-1 - I) in the rows the shader uses (0, 1 and 3), with a relative residual of 2% or less in 228 of the 265 frame pairs, and k = 0.337 in the median (5th to 95th percentile 0.318 to 0.353). The other 37 are three camera cuts and frames of an almost still camera, where the residual is noise.
- **Cars**: slot round(r / 7) equals I + k (M_r - I) with the host's M_r = PrevVP * PrevW_r * CurW_r^-1 * CurVP^-1 from the body `matW` of both frames: 770 car slots with a residual of 5% or less, k = 0.335 in the median, within about 0.02 of the frame's slot 0 value.
- **The player's car in the cockpit camera**: its body draws use a projection of their own (the interior's compressed depth range), but the game's slot for that car matches the scene camera's VP (residual below 0.1%), not the interior's.
- **Unused slots** 13 to 16 hold slot 0's matrix. `viewport` c68 is (640, 640, -360, 360), and `QAndNegNearQAndScale` c69 is (1.0, -0.1 to -0.6, 15 to 16.7, 11.25), the near plane changing between the start sequence cameras.
- **The amount**: k is not the cameras' `MotionBlurAmount` of 0.15 itself (k / 0.15 is about 2.25). All start sequence cameras use 0.15 and the frames ran at 30 fps, so this run does not tell how k follows the amount or the frame time. The host route does not need k; to use the game's matrices as motion, (G - I) / k + I recovers M.

### Check 3: the switches in a race

Negative. With `MotionBlur=1` in the race scenarios (`mvB`, the element present in the mounted `12-Car_Race.xml`) and with `motionBlurAmount` 0.15 in every track (`mvC`, present in the mounted `TrackSettings.xml`), `4746CAD32EF168CF` drew 267 and 269 times in as many frames, all in the start sequence, against 267 in `mvA`. In race frames the frame graph shows three scene depth resolves per frame, one per tile, with no repeat and no motion blur pass, as without the switches. Photo mode resolves the depth twice per tile in all runs, but no motion blur pass drew there while the scenario took its photo.

### Check 4: single tile

Verified. With `-SingleTile` (`mvD`) race frames render one 1280x720 tile without MSAA (EDRAM depth base 720), and the game resolves its depth once per frame, to the same address as the first tile at `-Quality high` (`1C0C1000`). The start sequence also renders in one tile, with the second depth resolve and the motion blur (256 frames). Photo mode keeps its own tiling (three tiles, 4x MSAA). Stencil IDs per car are as in the other runs.

### Still open

- A 16-car grid (slots up to 16), replays and photo mode motion blur: not traced.
- What sets k, and whether it changes with `MotionBlurAmount` (replays use 0.095 to 0.181) or the frame rate.
- The host route itself: its capture, the matrix table and the pass are not written yet.
