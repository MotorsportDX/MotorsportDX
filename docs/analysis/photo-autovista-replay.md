# Photo mode, Autovista and replays (M5)

Research for milestone M5 of the renderer work: photo mode, Autovista and replays on the RTV path, with the look mods and the [post pass framework](post-pass-framework.md). It collects how the title renders the three modes, what the e2e gates cover, what the runtime does with them, and a proposed plan. As of 18 October 2026. For the present state see [status/current.md](../status/current.md).

Sources: the title's render scenario and camera files (read from the user's USA Disc 1), the runtime and integration code, and existing runs: the `photo` runs `mvA` to `mvD` on `I:\FM4-build-usa` (frame graph, resolve inventory, draw list; see [motion vectors](motion-vectors.md#7-validation-in-the-game-18-october-2026)), the smoke gates of 17 and 18 October 2026, and the look mod coverage runs `cov-*-autovista`, `cov-*-home` and `cov-replay-photo*` on `I:\FM4-build-iter` (17 October 2026). No new run was made for this note.

## Summary

- **Photo mode** refines a still image over 120 frames into a 1280x720 half-float target, then saves a 1280x720 image and a 416x240 thumbnail that its JPEG encoder reads from guest memory. The saved photo has 1280x720 pixels at every render scale.
- **The photo save is correct under `readback_resolve=fast` because the final image's readback key is new**: keys unused for 60 frames are evicted, so the save waits for the GPU. No rule marks this resolve as read by the CPU, and nothing checks the photo's content.
- **Replays and photo mode are locked to 30 fps by their scenarios; Autovista and HomeSpace run at 30 fps without a FrameRate in their scenarios.** At 1x a replay frame costs about 4.6 ms of GPU time and the command processor is busy about 170 ms/s. Before the dynamic vblank change (16 October 2026), replay ran at 111 to 113 fps and photo mode at 90 to 112 fps at 240 Hz, so the renderer can draw these modes faster. Whether a replay then plays at the right speed was not measured.
- **Autovista has no gate.** The only runs on the RTV path are look coverage runs with blind button presses. It has no AO or post passes (no camera vertex shader in the manifest), and its glass, chrome and rim shaders have no mods.
- **The gates cover one photo per run, a photo load, and 30 s of replay without checks.** Overwrite, an empty shader cache, a second photo in one process, photos from replays or Autovista, and replay controls are not covered.

## 1. How the title renders the three modes

### Render scenarios

From `Media\RenderScenarios` (identical in both releases):

| Scenario | FrameRate | TilingScenario | Other settings |
|---|---|---|---|
| `PhotoMode` | 30 | 0 | DepthOfField 0, CollidableShadows 1, SoftParticles 1, CarMinWheelLOD 0 |
| `1-Car_Replay`, `8-`, `12-`, `16-Car_Replay` | 30 | 0 | DepthOfField 1, CarDrawDriver 1, SoftParticles 1; 1 car: CarKeepLOD0; 16 cars: no damage |
| `UI_Showroom` (Autovista) | none | 0 (UITilingScenario 0) | CarShader `shaders_slod`, no damage |
| `UI_Homespace`, `UI_Car`, `UI_NoCar` | none | 0 | CarShader `shaders_ui` |
| `*-Car_Race` (for comparison) | 60 | 1 | HalfRateMirror 1 |

TilingScenario 0 is the title's 4x MSAA scenario with three tiles. The launcher's `high` quality gives races the same tiling, so race, replay and photo mode render alike. `FM4_RACE_SETTINGS` and `FM4_TILING_SCENARIO` reach every file whose name contains `-Car_`, including the replays, but not `PhotoMode` or the UI scenarios ([fm4-render-overrides.cpp](../../work/fm4-rex/src/fm4-render-overrides.cpp)). The game reads all scenario files once at boot.

Photo mode is pushed on top of the race scenario; the race phases are arranged by `sub_826D9508` (0 PreRace, 1 and 3 321_and_PostRace, 2 Race, 4 Replay, 5 PhotoMode). [fm4-vblank-rate.cpp](../../work/fm4-rex/src/fm4-vblank-rate.cpp) mirrors this stack.

### Photo mode

Frame graph and resolve inventory of `mvA` (Bernese Alps Quick Race, 1x, launcher defaults), confirmed by the inventories of `mvB` to `mvD`:

- **Live view**: the frozen scene in three tiles at 4x MSAA, with the shadow cascades, the environment and reflection passes and the bloom chain, at 30 fps: about 3,300 draws per frame, GPU 4.3 ms per frame at 1x (`roadbase-240-photo`).
- **Accumulation**: 120 resolves to one 1280x720 `k_16_16_16_16_FLOAT` target (7,536,640 bytes, 1280x736 at 8 bytes per pixel), in about 15 s, exactly 120 in each of the four runs. Each comes after a full-screen 1x pass that reads two textures. The resolves start when photo mode opens, not at the shutter press: in `mvA` they run from 193.2 to 208.1 s, and the scenario presses A at 206.0 s. So the title refines the still image over 120 scene frames. Whether those frames are sub-pixel jitter, depth of field or shutter samples is not traced.
- **Save**: when SAVE PHOTO is chosen, the title resolves the final image once into a 1280x720 `k_8_8_8_8` buffer (3,768,320 bytes at `1C0C1000`) and once into a 416x240 thumbnail (425,984 bytes). Its JPEG encoder (`sub_82BB96E0`, traced by [fm4-jpeg-diagnostics.cpp](../../work/fm4-rex/src/fm4-jpeg-diagnostics.cpp)) reads them from guest memory, so they reach it only through the resolve readback. With a render scale above 1 the runtime downscales them to guest size first, so the photo always has 1280x720 pixels.
- **Saved files** (`user-data\...\photo\Forza` of the profile): `image@<timestamp>User` (0.25 to 0.52 MB; no JPEG marker and about 8 bits of entropy per byte, so it is encrypted or packed), `thumb` (a plain 416x240 JFIF), and `metadata` (100 bytes). Only the game opens the image, so `photo_view` is the check that it loads.
- **Effects**: Y opens EFFECTS MODE; SHUTTER SPEED (default 50) sets the photo's motion blur, which draws `4746CAD32EF168CF`. The three photo motion blur variants never drew at shutter speed 0, 63 or 100 ([look mod coverage](look-mod-coverage.md)).
- **Other entries**: TAKE PHOTO (the ninth icon of the replay toolbar) opens the same photo mode from a replay (`cov-replay-photo3` saved a photo). Autovista also has a photo mode (`Camera\ShowroomPhotomode.ini`: FOV 22.5 to 60 degrees, at most 5 m from the car); it was last opened on an earlier development build.
- `PhotoMode` sets DepthOfField 0, yet photo mode draws the depth of field final combines `30B67D8B86C37587` and `AE3B2E36FCA4958D`. Not reconciled.

### Replays

- 30 fps with about 2,800 draws per frame. GPU 4.5 to 4.6 ms per frame at 1x, command processor busy about 170 ms/s (`roadbase-240-career`, `usa-240-career`); 8.3 to 8.6 ms at 2x (16 October 2026); 25.6 ms at 4x.
- The title's motion blur (`4746CAD32EF168CF`) draws in replays: 2,809 frames in a career run with 30 s of replay, against 258 to 270 frames in a race with no replay. The replay cameras (`CarRelativeCams.xml`, 138 cameras) carry `MotionBlurAmount` 0.095 to 0.181.
- Replays cut between cameras. The post pass framework marks a frame whose camera moved more than 5 m, turned more than 30 degrees or changed its focal length by more than 10% as a cut, with no valid history. The thresholds are per frame, so at 30 fps a fast camera is more likely to count as a cut than at 60 fps.
- At 2x, replay once fell to 23.5 fps through a VRR trap; `SetVblankVrrPaused` now holds the 30 fps scenarios to the fixed grid ([history](../status/history.md)).

### Autovista

- AUTOVISTA on the main menu opens the `UI_Showroom` scene. Its car uses `shaders_slod_v16` (body paint `9ED0E8D2A0AC30E0`) and the final combine `30B67D8B86C37587`.
- The coverage runs show 30 fps with about 1,200 draws per frame. The command processor waits in `WAIT_REG_MEM` about 930 ms/s, so the title paces itself, although `UI_Showroom` sets no FrameRate. HomeSpace (`cov-ab-home`) is the same. Where this limit comes from is not traced. GPU about 2.7 ms per frame at 1x (`cov-fc-autovista`) and 5.5 ms at 2x (`cov-ab-autovista`), both with shader dumps on.
- Look and passes: the clear coat adds only the studio cube's reflection. None of the 17 glass, chrome and rim hashes of `shaders_slod_v16` has a mod; only two were ever translated, and neither appears in a draw inventory. The post pass framework captures the camera only from `8D05A46C3584CA9E` and `C81F8D92FF6529EB` ([passes.toml](../../work/fm4-rex/passes/passes.toml)); the garage and the showroom draw neither, so their scene points do not run and they have no AO.
- Last interactive test: an earlier development build (Ferrari 458 Italia: viewing the car, the panel, the cockpit, a paint change, photo mode and back). A static comparison on 14 October 2026 found RTV and ROV equivalent. The panel, the cockpit, paint changes and Autovista's photo mode have not been tested on the RTV path.
- `Camera\WalkaroundCamera.ini` is the Kinect walk-around; Kinect is not supported.

## 2. Coverage

| Case | Covered by | State on the latest build |
|---|---|---|
| Take and save one photo in a race | `photo` (smoke, perf, full) | Passes; confirmed by the OCR text SAVE SUCCESSFUL |
| Load the photo in a new process | `photo_view` (smoke, full) | Passes; the run checks that the viewer opens, and "in color" is judged by eye |
| Replay after a career race | `career` (smoke, full) | 30 s measured; nothing checks that the replay is playing |
| Overwrite a photo | none | Manual test on an earlier development build |
| Photo with an empty shader cache | none | Not run; the cache is shared by all runs of a BuildRoot |
| Second photo in one process | none | Not run |
| Photo from a replay | `cov-replay-photo3` (ad hoc) | 1 of 3 runs reached the save; the other two failed on OCR in the replay toolbar |
| Autovista | `cov-*-autovista` (ad hoc) | Entered through `select:AUTOVISTA`, then blind A presses every 25 s, no checks |
| Photo at 2x, 3x and 4x | smoke at each scale (17 October 2026) | Passes |
| Replay controls (rewind, cameras, speed) | none | Not run |

Harness limits that matter for new scenarios (from [e2e.py](../../outputs/FM4-build/e2e/e2e.py)):

- No step checks a saved file, and `capture_stats` measures luma only, so a photo in the wrong colors would pass.
- No scenario presses Y or X, and nothing moves a camera in a replay or in Autovista.
- `--arg cache_root=<empty folder>` should give an empty shader cache (the `--arg` values override the launch cvars), but no run has used it. The mod DXBC cache would stay warm unless `shader_override_cache_path` is also moved.
- `final.png` and `clear_gray_frames` read only the archived captures, and the runtime stops archiving after 450 captures (about 15 minutes at one capture every 2 s).

## 3. Findings

### 3.1 Readback of the photo under `readback_resolve=fast`

Readbacks are keyed by destination address and length, with four slots per key ([command_processor.cpp](../../work/runtime/src/graphics/d3d12/command_processor.cpp), `IssueCopy_ReadbackResolvePath`). In `fast` mode a key that already has a copy publishes the newest copy the GPU has finished, which is usually the previous one, and does not wait. Only a key with no copy waits for the GPU and is in guest memory before the command processor goes on. Keys unused for 60 frames are evicted (`kReadbackBufferEvictionAgeFrames`), and at most 256 are kept.

The final photo is a fresh key in every run: its address and length (`1C0C1000`, 3,768,320 bytes) were last resolved at 22 to 28 s, as a `k_2_10_10_10` image, long before the save. The save therefore waits and the encoder reads the right data. This is safe by circumstance, not by rule:

- A CPU-read resolve to an address and length that was resolved within the last 60 frames gets the previous copy. With `-SingleTile`, the race resolves its depth every frame to that same address and length (`mvD`: 6,163 resolves from 79.4 s); photo mode uses its own tiling, so the key was idle for 38 s when the photo was saved.
- With the publish thread, a guest read does not wait for a queued copy (only a key's first copy is awaited).
- No run has compared the saved photo with the rendered one.

### 3.2 Frame rate of the 30 fps modes

- The rate of the stack is its topmost FrameRate other than 60; values below 60 also hold VRR to the fixed grid. `FM4_FRAME_RATE` replaces only the value 60, so 30 fps scenarios keep 30.
- `FM4_RACE_SETTINGS="FrameRate=value:60"` reaches the four replay files, but a FrameRate of 60 counts as unset in the stack, so a replay pushed over a faster race would take the race's rate. An explicit value is needed. `PhotoMode` is reached by neither variable; it needs a new boot-time rule or a rewrite of the FrameRate control (`CDynamicRenderSettings` +48) at the push.
- `pipeline.py` removes every `FM4_*` variable from the environment and rebuilds them from its options, so an experiment needs a launcher option.
- `--frame-rate` 30 to 59 sets the reported refresh but does not install the dynamic vblank (it needs a rate above 60), so the guest vblank runs at that rate in every mode, contrary to the launcher's help text. Not checked in the game.
- Cost: a replay frame takes 4.6 ms of GPU time at 1x and 8.3 to 8.6 ms at 2x, so 60 fps fits at both scales and 120 fps at 1x only. The command processor would be busy about 340 ms/s at 60 fps.
- Not known: whether replay playback, the photo accumulation and Autovista's animations follow the frame rate or the clock. The race clock runs at 1.001 times the wall clock at high refresh rates, because the game scales its time step.

### 3.3 Autovista frame rate

Autovista and HomeSpace run at 30 fps although their scenarios set no FrameRate, so the vblank module does not see their limit. Raising it needs the title's own control to be found first (a swap interval or a frame skip in the UI code).

## 4. Proposed plan

Steps in order. Each ends with the gate named; images that judge the look are taken at 2x or more with full-resolution captures.

1. **Coverage, no runtime change** (3 to 4 days).
   - `autovista`: main menu > AUTOVISTA > a car, the panel and a hotspot, the cockpit and back, a paint change, Autovista's photo mode with a save, back to the menu; OCR checks at each screen and a measured phase.
   - `replay`: a career race into its replay, a measured phase that checks the replay HUD, camera changes, rewind and fast forward, then TAKE PHOTO and save (the steps of `cov-replay-photo3`, with the toolbar OCR made reliable).
   - `photo_overwrite`: a second `photo` run on the first run's profile that replaces the photo, and a `photo_view` of the result.
   - `photo_cold`: `photo` with `cache_root` and `shader_override_cache_path` in empty folders.
   - A color measure in `capture_stats` (mean chroma) and a content check for `photo_view`: the viewer's capture against the photo mode capture of the same run, downscaled.
   - Gate: each new cell 3 of 3 at 60 and 240 Hz on `I:\FM4-build-usa`, once on the Japan/Asia disc; then `autovista` and `replay` in the `full` tier.
2. **Photo correctness audit** (2 days). With `FM4_PHOTO_ENCODER_TRACE`, compare the encoder input with the rendered photo for: the first photo, a second photo in one process, an overwrite, a photo from a replay, a photo from Autovista, at 1x and 2x, and with `-SingleTile`. If any is stale, make the readback of the photo exact: `full` while `PhotoMode` or `UI_Showroom` is on the scenario stack, or a list of the CPU-read resolves (by length, format and size). Gate: the cases above match, then smoke.
3. **Frame graphs of replay and Autovista** (1 to 2 days). `REX_FRAME_GRAPH` and `REX_GPU_INVENTORY` in both modes (photo mode is in `mvA`): passes, tiling, resolves and GPU per frame at 1x, 2x and 3x. Find the vertex shaders that carry the showroom and garage camera.
4. **Look and passes in Autovista** (3 to 5 days). The showroom and garage cameras in the manifest's `[scene]` list, so AO and the scene points run there; mods for the showroom's glass, chrome and rims that draw; A/B at 2x. Gate: smoke plus the `autovista` cell.
5. **PC features**, each behind an option and each a decision for the project owner:
   - High-resolution photos (3 to 5 days): when the encoder runs, read the final image at the host resolution (2560x1440 at 2x, 5120x2880 at 4x) from the scaled resolve and write it as PNG beside the game's save, which stays 1280x720. Needs a runtime call that reads a scaled resolve range at full size.
   - 60 fps or more in replays, photo mode and Autovista (investigation 2 to 3 days): first measure the replay clock and the photo accumulation against the wall clock at 30, 60 and 120 fps (`race_clock.py` on the replay HUD); only then an explicit FrameRate option at the scenario push.
6. **Gates and docs**. Record the results in [status/current.md](../status/current.md) and move the superseded ones to [history](../status/history.md).

## 5. Still open

- What the 120 photo mode frames sample (jitter, depth of field or shutter), and whether more frames at a higher render scale would help.
- Where Autovista's and HomeSpace's 30 fps limit comes from.
- When the three photo motion blur variants and the four watermark final combines draw.
- Why photo mode draws the depth of field final combines with `DepthOfField 0`.
- Replay and photo mode motion vectors: the host per-car route of [motion vectors](motion-vectors.md) is meant to work there too, but replays were not traced.
- Autovista with downloadable content cars.
