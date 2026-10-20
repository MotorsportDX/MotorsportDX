# FM4 Recompilation: Current Status

As of 20 October 2026. Latest code change: TAA's jitter only while the camera moves, so still pictures no longer shimmer ([TAA jitter gate](#taa-jitter-gate-20-october-2026)); before it `cockpit_light=0.5` and `windshield=1` in the `pc` preset ([pc with the cockpit knobs](#pc-with-the-cockpit-knobs-20-october-2026)); before it the [cockpit glass](../analysis/windshield.md) post pass and the [`cockpit_light` look knob](../analysis/cockpit-brightness.md#the-cockpit_light-knob-19-october-2026), a shadow floor for the cockpit interior and the driver; before it the [motion vector gaps](../analysis/motion-vectors.md#9-the-gaps-in-the-game-19-october-2026) (the cockpit interior, the world through car glass, the mirror panel, and a motion mask) and four columns of `REX_DRAW_LIST_TRACE` (viewport Z, blend, color mask); before it the per-car [motion vectors](../analysis/motion-vectors.md#8-implementation-18-october-2026) on the runtime's [post pass framework](../analysis/post-pass-framework.md), the [grain of the road sheen](../analysis/look-mod-coverage.md#the-sheen-on-top-gear-and-its-grain-18-october-2026) (`road_sheen_grain`), the [road surface look mods](../analysis/look-mod-coverage.md#road-surface-18-october-2026) and 64 live knob slots, the stencil and shader constant columns of `REX_DRAW_LIST_TRACE` (diagnostics only, for the motion vector checks), and the post pass framework with GTAO on it. The dated chronology and earlier results are in [history](history.md).

This page describes the state now. Results from earlier builds came from different binaries, profiles and settings, so a past pass does not show that a feature was retested on the newer binary. Retested results are listed under [Verified on the latest build](#verified-on-the-latest-build).

## Summary

The project is an experimental build. On the latest binary, the e2e harness checks that Quick Race, photo mode and career races run through to rewards. It is not yet a full port: the full feature set, visuals and stability have not been verified.

- The default launcher (`look pc` with clearcoat and ambient occlusion) passed the smoke gate on 17 October 2026, and again on 18 October 2026 with GTAO on the post pass framework.
- Screen passes run on the runtime's post pass framework since 18 October 2026 (HLSL files and a manifest; scene depth, scene color and final points; history). GTAO moved onto it with identical output, texel by texel in the game, and the same GPU time.
- Per-car motion vectors were added to the framework on 18 October 2026, from the scene stencil's car IDs and the cars' world matrices. In the game they match the game's own motion blur reprojection within one pixel. Since 19 October 2026 the cockpit interior, the world seen through car glass and the rear-view mirror panel are handled, with a mask of the pixels without known motion and the reactive ones; the passes cost 0.02 ms of GPU time at 1x and 0.1 ms at 2x and pass a smoke gate against main. No pass uses them yet.
- The USA release (media ID `26FF1101`) is supported beside the Japan/Asia release since 18 October 2026. A clean setup, generate and build from the USA discs and a smoke gate with the `dlc` cell passed.
- The latest full matrix (240, 120 and 60 Hz) ran on 16 October 2026. The 17 October changes were checked with smoke gates, plus a three-run race-phase measurement for ambient occlusion.
- The clean build was verified on 13 October 2026 (commit `d1fa29a`). The project-owned runtime was verified on 14 October 2026.
- The clean build test covered startup and the intro only. It was not a race test, and it did not cover full mode coverage or performance.
- All verification ran on the same Windows computer used for development. No second computer has been tested.
- No original FM4 code or assets, recompiled code, game binaries, saves or captures are in Git. The game content is read from the user's local data.

### Status by area

| Area | State | Evidence |
|---|---|---|
| Boot, logos, intro video, main menu | Works | Clean build test (13 October 2026); menu reached about 30 seconds after start in the e2e boot |
| USA release (Disc 1, Disc 2, DLC) | Verified on the latest binary | Clean build, content import and smoke gate with `dlc`, 18 October 2026 |
| Quick Race at 60, 120 and 240 Hz | Verified on the latest binary | Matrix, 16 October 2026 |
| Photo mode: take, save, load in a new process | Verified on the latest binary | `photo` and `photo_view`, 16 October 2026 |
| Photo mode: overwrite, and photo with an empty shader cache | Not verified on the latest build | See [Current gaps](#current-gaps) |
| Career: World Tour, quit race, replay, continue | Verified on the latest binary | `career`, 16 October 2026 |
| Career race to rewards (AI driver) | Verified at 240 Hz | `career_finish` and `career_progress`, 16 October 2026 |
| Autovista | Worked on an earlier development build | Not covered by e2e |
| BMW M1 1981 (Disc 2) | Finished and continued on an earlier development build | Not covered by e2e |
| Look presets, clearcoat, ambient occlusion, display options | Checked with smoke gates | 17 October 2026 |
| Shader mod cache, hot reload, live knobs | Checked with a smoke gate, the `perf` tier and one hot reload run | [Shader mod iteration](#automated-gates-17-october-2026), 17 October 2026 |
| Motion vector sources (per-car stencil IDs, the game's motion blur matrices, scene depth with `-SingleTile`) | Verified in the game by traces | [Motion vector sources](#motion-vector-sources-18-october-2026), 18 October 2026 |
| Motion vectors (per car, on the post pass framework) and their mask | Verified in the game: matrices against the game's, debug views at 2x (Japan/Asia and USA), GPU time, smoke gate against main; no reactive mask for particles | [Motion vectors](#motion-vectors-18-and-19-october-2026), 18 and 19 October 2026 |
| Cockpit windshield glass (`windshield` look knob, 1 in `pc` since 20 October 2026) | Checked in the game at 2x: only the cockpit camera's window pixels change, other cameras, mirrors and HUD byte-identical; +0.28 ms GPU at 2x; smoke gate of the new `pc` against the earlier one | [Cockpit windshield](#cockpit-windshield-19-and-20-october-2026), 19 and 20 October 2026 |
| Post pass framework (GTAO on it, history, final point) | GTAO identical in the game, `perf` tier and smoke gate against main; history, the final point and hot reload checked with test manifests | [Post pass framework](#post-pass-framework-18-october-2026), 18 October 2026 |
| Downloadable content: import, mount, a DLC car on a DLC track | Verified on the latest binary (8 DLC packages; `dlc` scenario) | [Content import](#content-import-17-october-2026), 17 October 2026 |
| Split screen, Rivals, Hot Lap, Kinect, multiplayer/online, audio, DLC cars and tracks in game | Not validated | See [Current gaps](#current-gaps) |
| All cars and tracks, a full career season | Not validated | Status not verified; this does not mean they are broken |

## Verified on the latest build

### Test environment

- Same Windows computer as development. The GPU is an RTX 5070 Ti Laptop. The 240 Hz internal panel is driven through the iGPU (hybrid mode), and rendering runs on the RTX 5070 Ti Laptop.
- Gate tiers come from [outputs/FM4-build/e2e/matrix.py](../../outputs/FM4-build/e2e/matrix.py). Each run uses a snapshot of the build (`<BuildRoot>\snapshots\<name>`), so the game build can be rebuilt while a gate runs.

| Tier | Contents | Approximate time |
|---|---|---|
| `smoke` | `photo`, `photo_view` and `career` at 240 Hz, one run each | about 10 minutes |
| `perf` | `photo` at 240 Hz, three runs (race phase and photo mode) | - |
| `full` | `photo`, `photo_view` and `career` at 60 and 240 Hz, three runs each | about 1 hour |
| `career` | `career_finish` and `career_progress` at 240 Hz, one run | - |

The race phase of `photo` is 30 seconds of racing after 10 seconds of driving, with 12 cars. `career` is a World Tour race with quit, replay and continue.

### Cockpit light look knob, 19 October 2026

- Built: `-Look "...,cockpit_light=<0 to 1>"` ([cockpit brightness](../analysis/cockpit-brightness.md#the-cockpit_light-knob-19-october-2026), [running](../running.md#look)); 0 by default, 0.5 in `pc` since 20 October 2026. 44 shader mods: the floor constant (`MinCockpitShadow`, `MinShadowValue`) of 38 interior shaders of every car shader set, and the shadow map and lightmap texels of the driver's body and suit (six hashes). RTV only.
- Offline: `override_fxc.py --stub` compiles the 63 real contracts of the 44 hashes of a Bernese Alps dump with and without the knob; `fm4_shader_mod_tests` passes (its synthetic shader now reads the floor constants c82 to c91).
- In the game (`I:\FM4-build-pass`, USA discs, 2x, `-Look original` plus the knob, AI driver, frozen frames, live `defines:` 0, 0.15, 0.3, 0.5, 0; runs `cockpit-light` and `cockpit-light2`): at 0.5, with the sun behind or beside the car, the gauges go from 2 of 255 to 41 to 60, the gloves from 6 to 10 to 30 to 45 (white with their red logo), and at 0.3 both are readable. In the hood camera no pixel changes at 0.5, in the chase cameras only the driver seen through the rear window (0.04 to 0.06% of the pixels), and in the cockpit the road and sky patches change by at most 0.3 of 255. No artifacts in the 1:1 crops. With the sun ahead of the car the faces toward the driver get no N·L and nothing changes; the dashboard top and the wheel rim stay near black because their albedo is black (25 to 28 of 255 in direct sun).
- A bit-coded trace (`cockpit-trace3`) found the shaders on screen: the gloves are the suit shaders WhiteFelt and ApendagesSuede (not the body shader `30712480E0AB46E4`), the dashboard top `327942867CAF7123`, the wheel rim `C7B414C46424E22F`.
- Not run: other tracks and times of day, the split-screen and 16-car sets in the game. The A/B on top of `pc` and the smoke gate followed on 20 October 2026 ([pc with the cockpit knobs](#pc-with-the-cockpit-knobs-20-october-2026)).

### TAA jitter gate, 20 October 2026

Details in [TAA, the jitter gate](../analysis/taa.md#the-jitter-gate-20-october-2026). With `pc`, the image shook outside the moving race (menu scenes, the countdown, pause, rewind, photo mode, replays): the residue of TAA's eight jitter phases on still pictures. The jitter now runs only while the scene camera moves by at least `TAA_JITTER_MOTION` host pixels per frame (0.5, the static world at 10 m, held for 8 frames) and the frames come at `TAA_JITTER_RATE` fps or more (45); both params at 0 are the jitter of every frame. BuildRoot `I:\FM4-build-pass`, regenerated and rebuilt from this change.

- Offline: `fm4_post_pass_camera_tests` (the camera's motion against the matrices, the gate's rules) and `fm4_post_pass_shader_tests` (once the jitter stops, the history settles and matches one that never saw the jitter, at both points) pass; `test_pipeline.py` passes (15 tests).
- In the game at 2x (USA Disc 1 and profile, `--shader-dev`, the old behavior switched live on the same binary; runs `jg-freeze`, Quick Race at 60 Hz, and `jg-career`, the career race at 240 Hz): consecutive captures of two rewind freezes and the photo mode live view are byte-identical with the gate, and identical to `TAA_JITTER=0`, where the jitter of every frame changes 8.7 to 21.6% of the pixels by more than 4 of 255 (maximum 90 to 196). The jitter stays on in every logged frame of moving races at 60 and 240 Hz, and is off in menus with a 3D scene, the intro, the countdown, pause, rewind, photo mode, post-race and replays. 1:1 crops of moving races show the same continuous wires as before.
- Smoke gate at 240 Hz and 1x, `jgnew` (the gate) against `jgold` (the same binary with the manifest of local main, the jitter of every frame): 3 of 3 cells in both. `career` PASS (race 194.7 against 165.2 fps, p99 8.72 against 9.83 ms; replay 29.98 fps), `photo_view` PASS, `photo` WARN on capture statistics only (race 182.1 against 185.6 fps; photo mode 29.95 fps). The same three error keys, no clear-gray frames.
- Not checked: 120 Hz and the scene_color point (`taa_point=1`) with the gate. The thresholds await the project owner's review in play.

### pc with the cockpit knobs, 20 October 2026

`pc` gained `cockpit_light=0.5` and `windshield=1` at the project owner's request (`-Look "pc,cockpit_light=0,windshield=0"` is the earlier `pc`). BuildRoot `I:\FM4-build-pass` (game build of 19 October 2026, 22:45, with `cockpit_light`; the glass is a pass file).

- A/B at 2x on top of `pc` (`pc2-cockpit-ab`: Quick Race on the Bernese Alps with the USA Disc 1 and profile, AI driver, nine cockpit freezes, the defines switched live): in four freezes the gauges go from 1 to 4 of 255 to 37 to 62 and the gloves from 2 to 8 to 7 to 48; in the other five the shaded faces turn away from the sun and change by less than 4. The glass lowers the road and sky patches by 1 to 5. No artifacts in the 1:1 crops.
- Smoke gate at 240 Hz and 1x, `pcnew2` (the new `pc`) against `pcold` (`--look "pc,cockpit_light=0,windshield=0"`), same binary: 3 of 3 cells. `career` PASS (race 192.4 against 204.3 fps, p99 7.69 against 7.70 ms, GPU 3.28 against 3.14 ms; replay 30.0 fps); `photo` WARN on capture statistics only (race 223.6 against 205.4 fps, photo mode 29.95 fps); `photo_view` WARN on capture statistics. The first gate of the new `pc` (`pcnew`) failed on fps (race 162.4 and 178.5 fps): its new defines compiled every mod with FXC during the races, which the warm DXBC cache of `pcnew2` no longer does.
- `test_pipeline.py` passes (15 tests).

### Cockpit windshield, 19 and 20 October 2026

Details in [windshield](../analysis/windshield.md). The title draws no window glass for the player's car in the race cockpit camera; a post pass at the final point (`work/fm4-rex/passes/windshield.hlsl`, the look knob `windshield`, 0 by default, 1 in `pc` since 20 October 2026) adds a tint, a dashboard reflection, glare and dust over the pixels that see the world through the windows. BuildRoot `I:\FM4-build-pass` with the USA Disc 1 and profile, `e2e.py` at 2x, Quick Race on the Bernese Alps with the AI driver, frozen frames.

- The pass finds the cockpit frames (the interior shell drew and covers at least 10% of the frame; 51% in the cockpit camera) and the window pixels from the scene depth and stencil; the debug view marks the windows, and the intro, 3-2-1, hood, bumper and chase cameras as no cockpit.
- With TAA off, `WINDSHIELD` 1 against 0 in a cockpit freeze changes 99.2% of the window pixels and 0.51% of the interior pixels (the one-pixel soft rim); the HUD, the interior mirror and the side mirror do not change. Hood and far chase freezes are byte-identical with it on and off.
- GPU time at 2x: +0.28 ms per frame in the cockpit camera.
- Open: the project owner's review of the look; other cars and tracks, rain and night.

### Motion vectors, 18 and 19 October 2026

Details in [motion vectors](../analysis/motion-vectors.md#in-the-game-18-and-19-october-2026) and, for the interior, glass and mirror panel, [section 9](../analysis/motion-vectors.md#9-the-gaps-in-the-game-19-october-2026). BuildRoot `I:\FM4-build-pass` (Japan/Asia; the USA Disc 1 through `game_data_root` for one run), launcher defaults (look `pc`, quality `high`).

- **Build and offline tests** passed: the instruction tests, the track settings tests, `fm4_post_pass_camera_tests` (a car point 3 km from the origin reprojected within 1.5e-12 m), `fm4_post_pass_shader_tests` on WARP (the synthetic scene's track, shadowed track, cars and sky within 0.41 of the allowed error; an interior under a mask, bare masks and ground through glass within 0.2, with their mask), `fm4_shader_mod_tests` (0 failures) and `test_pipeline.py` (11 tests).
- **Against the game's motion blur matrices** (the start sequence, `post_pass_motion_check`): at 1x and 60 Hz, 260 frames, slot 0 k 0.337 and the car slots (10 cars, 1,369 slot frames) k 0.337; turned back into motion, the game's car matrices and the host's differ by 0.027 px in the median and 0.48 px at most at 1280x720. At 2x and 240 Hz the same check gives k 0.337 and 0.333, and 0.051 px in the median and 0.98 px at most at 2560x1440.
- **Debug views at 2x** (19 October 2026; views 1, 3, 4, 5 and 6 in the cockpit, hood, bumper, chase near and chase far cameras, cars passing): the player's car is still in the chase and hood cameras and has its own color in the slot view; the world moves outward from the direction of travel; other cars carry their own motion. The cockpit interior moves with its own matrix: 0.06 to 3.2 px at 2x (0.3 to 5.5 px before, radial). The world seen through car side windows is marked reactive. The rear-view mirror panel of the hood and bumper cameras has no motion and is marked unknown in the mask. The same with the USA discs.
- **GPU time** (`REX_FRAME_GRAPH` every frame, the passes switched on and off every second in one race, at 240 Hz): the frame's last scene depth resolve, which runs the passes, takes 0.260 against 0.236 ms at 1x (+0.024 ms) and 0.939 against 0.828 ms at 2x (+0.111 ms), below the estimate of 0.1 ms and 0.2 to 0.3 ms.
- **Smoke gate** `mvgapsmoke2` against `mvmainsmoke`, main's build (the `mvsmoke` snapshot with main's passes) run just before it:

  | Cell | Main | This branch | `compare` |
  |---|---|---|---|
  | `photo` | race 221.3 fps, p99 5.9 ms (1,981 draws per frame); photo mode 29.95 fps | race 216.1 fps, p99 6.7 ms (2,050 draws per frame); photo mode 29.97 fps | WARN on capture statistics |
  | `photo_view` | 56.03 fps | 56.99 fps | WARN on capture statistics |
  | `career` | race 195.2 fps, p99 7.2 ms; replay 30.0 fps | race 206.2 fps, p99 7.2 ms; replay 30.0 fps | PASS |

  3 of 3 cells passed in both, with the same error keys and no clear-gray frames; the draw time per draw is 1.23 against 1.21 microseconds.

### Motion vector sources, 18 October 2026

Details in [motion vectors](../analysis/motion-vectors.md#7-validation-in-the-game-18-october-2026). BuildRoot `I:\FM4-build-usa` (USA), main `3f7fc10` plus the new `REX_DRAW_LIST_TRACE` columns, four `photo` runs (12-car Quick Race on the Bernese Alps, AI driver) at 60 Hz and 1x with the launcher defaults; all four reached their goal (race phase 60.0 fps).

- **Stencil IDs**: the static scene writes 0 and every draw of car n writes 7n (7 to 84), in the start sequence and in race frames, in all four races; the rear-view mirror carries the same values. The body and the `42CB1356564CF9FA` draws of a car share one world matrix; the rims have one per wheel.
- **The game's motion blur matrices** (start sequence, 265 frame pairs): slot 0 equals the host camera reprojection PrevVP * CurVP^-1 and each car slot round(ref / 7) the host's per-car reprojection, both scaled as I + k (M - I) with k about 0.34 (median 0.337 for slot 0, 0.335 for the cars), not by the cameras' `MotionBlurAmount` of 0.15 directly.
- **Switches**: `FM4_RACE_SETTINGS="MotionBlur=1"` and `FM4_TRACK_SETTINGS="motionBlurAmount.value=0.15"` change nothing: the motion blur still draws only in the start sequence (267 and 269 frames against 267), and race frames resolve the depth once per tile.
- **`-SingleTile`**: race frames still resolve the scene depth, once per frame as one 1280x720 tile.

### Post pass framework, 18 October 2026

Details in [post pass framework](../analysis/post-pass-framework.md#verification) and [GTAO design](../analysis/gtao-design.md) (M5). BuildRoot `I:\FM4-build-pass` (Japan/Asia), main `ad2797a` (`pfbase`) against the framework (`pfcand`), 240 Hz, launcher defaults.

- **GTAO output identical**: the same FXC instructions, a byte-identical occlusion on WARP, and in the game a temporary build that ran the old and the new pass side by side and compared them on the GPU at every swap: no differing texel in 22,321 frames at 2x with the debug views cycled and 18,241 frames with the `pc` look. Frozen frames that showed the old and the new occlusion in turn were byte-identical at 2x. The look mods compile to the same DXBC (204 fingerprints).
- **`perf` tier**: 3 of 3 runs each, WARN (guest idle wait, final capture statistics), no FAIL. Race GPU per frame 3.02 / 3.24 / 2.91 ms on main and 3.04 / 3.10 / 3.09 ms with the framework, at 2,347 / 3,454 / 1,970 and 2,392 / 2,920 / 2,763 draws per frame; race fps 206.8 against 187.5, following the draws per frame.
- **Smoke gate** `pfcsmoke` against `pfbsmoke` (main, run just before it):

  | Cell | Main | Framework | `compare` |
  |---|---|---|---|
  | `photo` | race 187.1 fps, p99 7.7 ms, GPU 3.08 ms (2,766 draws per frame); photo mode 29.95 fps | race 217.0 fps, p99 6.7 ms, GPU 2.93 ms (2,055 draws per frame); photo mode 29.95 fps | WARN on capture statistics |
  | `photo_view` | 56.99 fps | 56.87 fps; the saved photo loads in color | WARN on capture statistics |
  | `career` | race 193.4 fps, p99 7.5 ms, GPU 3.11 ms; replay 30.0 fps | race 181.1 fps, p99 7.9 ms, GPU 3.17 ms; replay 30.0 fps | PASS |

  3 of 3 cells passed in both, with the same error keys and no clear-gray frames.
- **New points, history and hot reload** (test manifests, not in the repository, 2x): the history is valid from the second scene frame on; reprojected through the depth, the previous frame's color matches the world where the plain difference shows every moving edge. Passes drawn into the final color show under the HUD. Two bugs found by these runs were fixed before the gates above (the pixel passes and the sun occlusion query; the views of the framework's own descriptor heaps).

### USA against Japan/Asia performance, 18 October 2026

One binary for both releases: the `I:\FM4-build-usa` game build, with the launcher defaults (look `pc`, quality `high`, 1x), the `perf` tier (`photo` at 240 Hz, three runs a gate) and the Disc 1 chosen with `--arg game_data_root=...`. The gates ran in the order USA, Japan/Asia, Japan/Asia, USA (`relusa1`, `reljp1`, `reljp2`, `relusa2`) on copies of the same profile.

- 12 of 12 runs passed. Race phase: USA 214.2 fps (standard deviation 14.2), p99 6.46 ms; Japan/Asia 202.1 fps (13.1), p99 7.04 ms. Photo mode ran at 29.9 fps in every run.
- The race fps of a run follows its draws per frame (correlation -0.98 over the 12 runs), because the AI races differ from run to run: the USA runs averaged 2,189 draws per frame and the Japan/Asia runs 2,494. With that workload taken out, the difference is -0.2 fps (permutation test, p = 0.92). The raw difference (+6.0%, p = 0.16) came from lighter races, not from the release.
- This agrees with the code: the images are identical, and the game's two `XamGetExecutionId` calls read only the title ID, which both releases share.
- The USA BuildRoot is the development base, and Japan/Asia is checked on it through `game_data_root`. After a rebuild at commit `8c7d3e1` (build and CPU tests passed), the smoke gate `jpsmoke` on the Japan/Asia disc passed 3 of 3 cells against the USA gate `usa`. Its `photo` race ran at 146.5 fps (FAIL), but that race drew 3,516 draws per frame, more than any earlier run; the rerun `jpsmoke2` ran at 179.6 fps at 2,843 draws per frame (WARN on capture statistics only), in line with the workload curve above. `career`: 202.7 fps, PASS.

### USA release, 18 October 2026

BuildRoot `I:\FM4-build-usa`, a new BuildRoot set up from the USA Disc 1 and Disc 2 and the eight DLC packages. The two releases differ only in the executable headers; see the [release comparison](../analysis/releases.md).

- **Setup, generate and build**: passed in the new BuildRoot in about 8 minutes with 16 jobs. The upstream SDK clone was copied from another BuildRoot instead of downloaded. `generate` printed `Disc 1: USA release, media ID 26FF1101`. The loaded image, the 271 switch tables, the guard audits and every generated source file are identical to those generated from the Japan/Asia discs; only the codegen stamp files differ, because they name the Disc paths. The instruction tests (295,650 checks, 0 failures) and the post_ao, shader mod and track settings tests pass.
- **Content import**: the four USA Disc 2 packages and the eight DLC packages were imported into a new profile (the installer's developer import mode, `FM4_CONTENT_INSTALL_ONLY`).
- **Boot with the new profile**: the logos and the intro car video render (e2e `boot`, 90 seconds).
- **Smoke gate `usa` with the `dlc` cell** (240 Hz, 1x, launcher defaults), on a profile copied from the Japan/Asia BuildRoot `I:\FM4-build2`, so the saves and career progress were made by the Japan/Asia executable: 4 of 4 cells passed in 13 minutes. Against the Japan/Asia smoke gate `pcsky07` (same commit and settings) there is no FAIL and no new runtime error.

| Cell | USA | Japan/Asia (`pcsky07`) | Verdict |
|---|---|---|---|
| `photo` | race 195.9 fps, p99 7.55 ms; photo mode 29.9 fps | race 204.6 fps, p99 7.59 ms; photo mode 29.9 fps | WARN: capture statistics, guest idle wait 7.0 to 32.5 ms/s |
| `photo_view` | 55.8 fps; the saved photo loads in color | 57.0 fps | WARN: capture statistics |
| `career` | race 185.9 fps, p99 7.99 ms; replay 30.0 fps | race 192.0 fps, p99 8.54 ms; replay 30.0 fps | PASS |
| `dlc` | race 179.4 fps, p99 7.47 ms; the DLC car on the DLC track | no baseline at this commit | - |

Each cell is one run, so the fps differences (within the 10-20% noise of single runs) show no performance difference. Not covered: 60 and 120 Hz, a new USA profile through a career race, races with Disc 2 cars, and title updates (not supported for either release).

### Glass, chrome and rim look mods, 18 October 2026

Details in [look mod coverage](../analysis/look-mod-coverage.md#glass-chrome-and-rims-18-october-2026). BuildRoot `I:\FM4-build-iter`.

- Offline: `fm4_shader_mod_tests` passes 1,021 checks for the 53 look mods, including a new check that every mod's DXBC changes when its knobs move; the 22 mods that existed before keep their DXBC fingerprints with the `pc` defines. `override_fxc.py --stub` compiles 55 real contracts of 37 modded hashes with every mod, with and without the knobs. `test_pipeline.py` passes.
- In the game: the runtime compiles 27 of the 31 new mods without error. False color in a Quick Race and in HomeSpace shows which car parts each hash draws. An A/B at 2x (five frozen race cameras, HomeSpace, the 16-car set) shows stronger reflections on glass at grazing angles (clearly on the HomeSpace windshield), brighter chrome exhaust tips, small highlights on lamp lenses, almost no change on rims, no change on the cockpit gauge glass, and no artifacts.
- Smoke gate `gcr` (`pc` plus `glass`, `chrome` and `rims` at 1) against `gcrbase` (`pc`), run one after the other at 240 Hz and 1x: 3 of 3 cells passed, no FAIL. Race GPU per frame 3.09 to 3.19 ms (`photo`) and 3.24 to 3.24 ms (`career`); replay 4.42 to 4.73 ms; photo mode 3.82 to 4.49 ms. The race fps moved both ways with the draws per frame (185.3 to 170.7 at 2,575 to 3,021 draws; 169.7 to 182.7 at 2,767 to 2,482 draws).

### Road surface look mods, 18 October 2026

Details in [look mod coverage](../analysis/look-mod-coverage.md#road-surface-18-october-2026). BuildRoot `I:\FM4-build-usa`.

- Build: `Build.ps1 -Action build` of the branch (runtime with 64 live knob slots) into `I:\FM4-build-usa` passed, with the instruction, post pass, track settings and shader mod tests. `-ShaderDev` on it reports 93 mods and 34 live knobs, with no overflow warning, and compiles 24 of the 25 road mods live without an FXC error.
- Offline: `fm4_shader_mod_tests` passes 1,781 checks for the 93 look mods (34 knobs of the 64 live knob slots, raised from 32); at the `pc` defines the 25 new mods compile to the DXBC of the library without a mod, byte for byte on the 29 real contracts of 24 of them. A mod file still moves its hash from the DXBC translation to the generated HLSL, so the default look runs these 25 road shaders on that path; it was not compared in the game with and without the files. `override_fxc.py --stub` compiles every mod against 99 real contracts of 76 hashes (24 of the 25 new ones), with and without the road knobs, and each road mod's DXBC changes with them.
- In the game, at 2x: debug views of `ROAD_BLND_2` (switched live in one frozen frame by the new `defines:` e2e step) show that the bright sheet towards the sun is the game's sun highlight; `CameraExposureRange` does not change it. False color on the Bernese Alps, Tsukuba, Sebring and Top Gear shows which road, line, edge and shoulder hashes draw where. A/B at 2x on the Bernese Alps grid and five race cameras: see the Display item under [Current gaps](#current-gaps).
- Smoke gate `roadc` (`pc` plus `road_specular=0,road_sheen=1`) against `roadbase` (`pc`) at 240 Hz and 1x: 3 of 3 cells passed, the same error keys, no clear-gray frames. `career` FAIL on race fps (213.7 to 189.5), with 29% more draws per frame in that race, which is limited by the command processor; race GPU per frame 3.01 to 3.08 ms (`photo`) and 3.08 to 3.20 ms (`career`). This gate ran before the grain below.
- The sheen's grain (`road_sheen_grain`, default 1; the Top Gear road `E2DECBB50FE1F549` weighted by its decoded specular amount; `road_debug` 6 and 7): offline, `fm4_shader_mod_tests` passes 1,781 checks (35 knobs of 64), at the `pc` defines the 29 real road contracts compile byte-identical to the library without a mod, and `override_fxc.py --stub` compiles all 99 contracts with the sheen and grain on. In the game at 2x (`--shader-dev`, frozen cameras, live `defines:`): the Bernese Alps grid toward the sun, Top Gear with five race cameras (cockpit and hood facing the sun) and the Bernese Alps race with five; results in the Display item under [Current gaps](#current-gaps). The first grain brightened the road by a factor of about 3, because the mod's own texture fetch reads darker than the shader's; the grain now takes all its texels from the mod's fetches. Not run: a smoke gate and the GPU cost of the grain's five extra fetches.

### Sky and vegetation look mods, 18 October 2026

Details in [look mod coverage](../analysis/look-mod-coverage.md#sky-and-vegetation-18-october-2026). BuildRoot `I:\FM4-build2`.

- Offline: `fm4_shader_mod_tests` passes 1,306 checks for the 68 look mods; at the `pc` defines the 15 new mods compile to the DXBC of the library without a mod. `override_fxc.py --stub`, whose stub now fetches every 2D texture through the fetch hook, compiles the real contracts of all 15 hashes with and without the knobs. `test_pipeline.py` passes.
- In the game, at 2x: false color on the Bernese Alps and Maple Valley shows the sky dome (also in the mirror and the car's environment cube; Y of interpolator 0 is up) and every tree, tree card and grass tuft on screen drawn by a modded hash, after `2FABDDDB86C5AA15` (`DIFF_OPAC_2_2SD`) was added for the dark tree lines of the Alps. An A/B with `sky_saturation=1.15`, `sky_contrast=0.3`, `sky_horizon=0.5` and `foliage_saturation=1.15` shows a slightly deeper blue sky and barely visible foliage changes; `foliage_hue=-45` turns the green trees of Maple Valley rusty orange and leaves its red and yellow trees as they are. No artifacts. No preset changed.
- Smoke gate `natc1` (`pc` plus that set) against `pcsky07` at 240 Hz and 1x: 3 of 3 cells passed, no FAIL, no clear-gray frames. Race GPU per frame 3.01 to 2.93 ms (`photo`) and 3.14 to 3.13 ms (`career`); the race fps followed the draws per frame (204.6 to 213.7 at 2,406 to 2,143 draws; 192.0 to 180.8 at 2,458 to 2,751 draws).

### Automated gates, 17 October 2026

- **Default launcher** (`look pc` with clearcoat and ambient occlusion, commit `48ed669`): smoke gate passed.
- **Shader mod iteration** ([shader-mod-iteration.md](../analysis/shader-mod-iteration.md): DXBC cache on in the launcher, hot reload and live knobs off). Smoke gate at 240 Hz with the `pc` look, against a baseline smoke gate of the build before the change, run just before it in the same BuildRoot and profile:

  | Scenario | Baseline | With the change | `compare` |
  |---|---|---|---|
  | `photo` | race 212.7 fps, p99 7.2 ms, GPU 2.92 ms; photo mode 29.95 fps | race 152.4 fps, p99 9.7 ms, GPU 3.29 ms; photo mode 29.94 fps | FAIL on fps |
  | `photo_view` | 56.8 fps | 57.0 fps | WARN on capture statistics |
  | `career` | race 169.5 fps, p99 9.8 ms, GPU 3.17 ms; replay 30.0 fps | race 172.3 fps, p99 9.7 ms, GPU 3.17 ms; replay 30.0 fps | WARN on guest idle wait |

  - 3 of 3 cells passed in both gates, with no clear-gray frames.
  - The `photo` FAIL comes from the race, not the runtime. The AI driver led alone in the baseline run and ran sixth among the cars in the new run. The scene had 3,204 draws per frame instead of 2,071, and the race was limited by the command processor. The `career` race, the same event in both runs, shows the same GPU time and fps within 2%. The `perf` tier below confirms it.
  - **`perf` tier** (`photo` at 240 Hz, three runs per build, the baseline first). The place is the player's in the pause menu after the race phase, or on the race HUD where OCR did not read the menu:

    | Run | Place | Race fps | Race p99 | Race GPU | Draws per frame | Photo mode |
    |---|---|---|---|---|---|---|
    | Baseline 1 | 5th (HUD) | 166.0 | 9.06 ms | 3.21 ms | 2,897 | 29.95 fps, GPU 4.98 ms |
    | Baseline 2 | 1st | 189.4 | 8.78 ms | 3.06 ms | 2,475 | 29.97 fps, GPU 4.39 ms |
    | Baseline 3 | 2nd | 189.2 | 8.26 ms | 3.08 ms | 2,418 | 29.95 fps, GPU 4.45 ms |
    | Change 1 | 1st | 203.2 | 7.70 ms | 3.01 ms | 2,133 | 29.95 fps, GPU 4.03 ms |
    | Change 2 | 4th (HUD) | 187.8 | 7.74 ms | 3.11 ms | 2,721 | 29.95 fps, GPU 4.46 ms |
    | Change 3 | 1st | 219.8 | 6.62 ms | 2.93 ms | 2,070 | 29.97 fps, GPU 4.19 ms |

    All six runs passed, with no clear-gray frames. `compare` on the medians: WARN on capture statistics only. The race median is 203.2 against 189.2 fps (p99 7.70 against 8.78 ms, GPU 3.01 against 3.08 ms). The fps follows the number of cars in view: a run behind other cars draws 2,700 to 2,900 draws per frame and runs at 166 to 188 fps on either build. The command processor time per draw is 1.81 to 2.02 µs with the change and 1.99 to 2.05 µs without it.
  - The first cell compiled the seven look shaders and stored them. In the next two cells they loaded from the cache, and translating the shader storage at the start took 16 to 19 ms instead of 5,421 to 5,471 ms.
  - Without live knobs, the DXBC of every look mod is byte-identical to the previous build (`fm4_shader_mod_tests` fingerprints).
- **Look mods for every car shader set** (the 16 mods of [look mod coverage](../analysis/look-mod-coverage.md) on top of the shader mod iteration, with the decode of encoded environment cubes; BuildRoot `I:\FM4-build-iter`). Smoke gate at 240 Hz with the `pc` look against the shader mod iteration gate above:

  | Scenario | Result | `compare` |
  |---|---|---|
  | `photo` | race 200.7 fps, p99 8.8 ms; photo mode 29.97 fps | WARN on capture statistics |
  | `photo_view` | 56.95 fps | WARN on capture statistics (another photo is loaded) |
  | `career` | race 170.9 fps, p99 11.5 ms; replay 30.0 fps | PASS |

  3 of 3 cells passed, with no new error keys. A gate run before the decode fix had one `photo` FAIL on overall p99 (43.0 ms): the pre-race intro ran at 23 to 24 fps for 16 seconds with late vblanks while its GPU time stayed at 4.2 ms. A repeat of that cell passed, so it is counted as a pacing flake. The in-game checks of the mods themselves are in [look mod coverage](../analysis/look-mod-coverage.md#in-game-checks-17-october-2026).
- **Hot reload and live knobs** (one `e2e.py --shader-dev` photo run at 60 Hz, files of a copy of the look saved during the race): a live saturation change showed in the next capture without a compile. A tint in `fm4-look.hlsli` recompiled 5 shaders and recreated 8 pipelines in 1.7 s, and showed in the next capture. A syntax error kept the previous shader and logged the FXC error, and the fixed and restored files reloaded. The run reached its goal (the photo saved), with no clear-gray frames.
- **Clearcoat** (smoke, 240 Hz, compared with a `qhigh` baseline): 3 of 3 runs. Race fps 214 in the `photo` race phase and 213 in `career`. Race-phase GPU 2.72-2.79 ms (2.83-2.88 ms for the baseline). Replay and photo mode 30 fps. No clear-gray frames. `compare` reports WARN on capture statistics only.
- **Ambient occlusion** (smoke 1x, 240 Hz, compared with a `look pc` baseline without AO): 3 of 3 runs. `photo_view` and `career` PASS, `photo` WARN on capture statistics. Replay and photo mode 30 fps. No clear-gray frames.
- **Ambient occlusion performance** (`perf`, race phase, three runs):

  | Render scale | Before AO | With AO | Limit |
  |---|---|---|---|
  | 1x | GPU 2.82 ms, 196 fps | GPU 3.01 ms, 200 fps | command processor |
  | 2x | GPU 5.32 ms, 174 fps | GPU 6.09 ms, 162 fps | GPU |

  At 2x with the launcher at 60 Hz: 60.0 fps.

- **Display and render scale** (smoke, high quality with clearcoat, 240 Hz): all cells pass. Replay and photo mode 30 fps. No clear-gray frames. `compare` reports FAIL on fps only, because this banding uses the `xbox` quality setting.

  | Render scale | Race fps | GPU per frame | Notes |
  |---|---|---|---|
  | 1x (1280x720) | 213 | 2.7 ms | |
  | 2x (2560x1440 or 2560x1600 display) | 177 | 5.4 ms | |
  | 3x (3840x2160) | 109 | 8.9 ms | |
  | 4x | 63-65 | 15 ms (p99 21 ms) | Replay GPU 25.6 ms. Only suitable for 60 fps with more GPU headroom than this computer has. |

- **Manual checks**:
  - Alt+Enter, tested with real key presses in Quick Race at a 1920x1080 window with 2x render: 1920x1080 to 2560x1600 to 1920x1080. OCR afterwards saw only the race HUD (timer running, no pause menu).
  - Presenter downscale: a 1920x1080 window capture of a frozen rewind scene at 2x matched the area-averaging simulation (mean difference 0.12-0.13 of 255), not bilinear (0.71-4.02). Bridge railings stay continuous.
  - The settings overlay is drawn by the presenter, so it is not in e2e captures.

### Content import, 17 October 2026

The generic content installer (`-Content`, [setup](../setup.md#downloadable-content)) on the latest build, with a separate test profile under `BuildRoot`:

- **Header check:** the four Disc 2 packages (`PIRS`) and eight DLC packages (`LIVE`: Launch Track Pack, Limited Collector's Edition Pack, March Pirelli, May TopGear, Meguiar's, November Speed, Porsche Expansion and September Pennzoil) pass. In the Disc 2 root, the `$SystemUpdate` packages and `nxeart` are skipped.
- **Import:** all 12 packages imported in 7.2 seconds. A second run skipped all 12 through their markers (2.2 seconds).
- **Game:** with the 12 packages installed, the title mounts `DLCRoot0` to `DLCRoot11`; with Disc 2 only it mounts `DLCRoot0` to `DLCRoot3`. `quickrace` (one run, launcher default frame rate) passed with the same error keys as the Disc 2 only baseline: fps mean 41.3 against 41.4, p99 34.2 ms against 33.9 ms.
- **DLC race** (`dlc` scenario, `--profile copy`, launcher default frame rate, 3 of 3 runs): Porsche shows in the Quick Race manufacturer list with 30 cars (the Porsche Expansion Pack, not locked), the 1970 914/6 is selected, and the Launch Track Pack's Test Track Benchmark (Speedway) loads with 12 cars. Race phase 60.0 fps, p99 17.2-17.5 ms. No clear-gray frames, and the same error keys as `quickrace`.
- **Not covered:** the cars of the other six packs and Disc 2 in a race (their makes show in the list, for example Wiesmann), Autovista with DLC cars, TopGear Soccer Field (Car Soccer), and the license state the title reads for each package.

### Full matrix, 16 October 2026

Binary after the dynamic refresh-rate change. Values are fps and p99 frame time. 240 and 120 Hz have three runs per cell; the 60 Hz column has one run (smoke).

| Scenario | 240 Hz | 120 Hz | 60 Hz |
|---|---|---|---|
| `quickrace` | 3/3, 224-235 fps, p99 5.3-6.2 ms | 3/3, 118-120 fps, p99 8.8-9.8 ms | 60.0 fps, p99 17.1 ms |
| `photo` | 3/3; race 220-228 fps, p99 5.4-5.8 ms; photo mode 30.0 fps | 3/3; race 117-118 fps, p99 9.6-10.1 ms; photo mode 30.0 fps | race 58.8 fps; photo mode 30.0 fps |
| `photo_view` | 3/3, menu 57 fps | 3/3, menu 57 fps | passed |
| `career` | 3/3; race 232 fps, p99 5.4 ms; replay 30.0 fps | 3/3; race 118-120 fps; replay 30.0 fps | race 60.0 fps; replay 30.0 fps |

Notes:

- No crashes and no clear-gray frames. The error keys match the baseline.
- `compare.py` reports FAIL only on total fps. Photo mode and replay now run at their original 30 fps; before this change they followed the global frame rate.
- The race phase of `photo` starts at the start of the race, so its p99 includes the start hitch.

### Race outcomes and features, 16 October 2026

- **Quick Race** at 60, 120 and 240 Hz: 60.0, 118-120 and 224-235 fps, as in the matrix above.
- **Photo mode**: a photo saved from photo mode loads in a new process. The `photo_view` run reads the photo back from `MY PHOTOS` and shows it in color.
- **Career**: World Tour, `QUIT RACE`, replay and `CONTINUE` back to the event all run.
- **Career race to rewards** (`career_finish`, 240 Hz): the AI driver wins Tsukuba Short Circuit (Ford Ka). The race ran at 219 fps, p99 6.9 ms. The `YOU PLACED` screen showed a total of +5,170 CR. Driver and Ford affinity reached level 2. The car reward was a 1994 Mazda MX-5 Miata, with a 2,000 CR bonus. `EVENT COMPLETE` appeared.
- **Progress after restart** (`career_progress`, a new process): the next event is TopGear Bowling Challenge. The Ford Ka is at LVL 2 with 15,740 CR.
- **AI driver without input** (developer test option): the AI driver wins Tsukuba Short Circuit (Ford Ka, 8 cars, 3 laps) with laps of 1:02.1 and 0:56.5. It finishes first, 22.6 seconds ahead, and the `YOU PLACED` screen appears. Pause, photo mode, photo save, `QUIT RACE`, replay and `CONTINUE` still work with this driver (smoke at 240 Hz, 3 of 3, with the first version of the option).

### Clean build, 13-14 October 2026

Source: [outputs/FM4-build/verification.json](../../outputs/FM4-build/verification.json) (date 13 October 2026), with additions from the runtime work on 14 October 2026.

- **Toolchain**: LLVM 20.1.8, Windows SDK 10.0.26100.0.
- **Setup**: passed. It takes the SDK at a pinned commit, applies the patch, builds the eight SDK resolve shaders, then builds and installs the SDK locally.
- **Generate and build**: passed from a clean export of the Git index, with no pre-existing generated game source. A regenerate and rebuild also passed. The main executable, the XMediaFacade and SpeechFacade DLLs, the runtime and the GPU plugin built as local output in BuildRoot.
- **Switch tables**: 271 switch tables were regenerated from the user's executable. They match the previous build.
- **Function registration sets**: match the previous build. Default 50,233, XMedia 3,212, Speech 6,715. This does not show that each function has run or that its behavior is correct.
- **Script unit tests**: 5 tests, 0 failures, with synthetic inputs.
- **Instruction tests**: 295,650 checks, 0 failures. These are 68,121 float16 checks and 227,529 vector checks.
- **Startup**: the Disc 2 packages were imported into a new isolated profile (4 packages). The logos and the intro car video rendered. The process stayed alive for 60 seconds and was closed at the tester's request with exit code 0.
- **Not covered**: `full_gameplay_verified` is `false`. No race was run in this test.
- **Repository contents**: no game code or assets, recompiled code, binaries, saves or captures. License files and integration metadata for the licensed SDK source are included.
- **Project-owned runtime (14 October 2026)**: `work/runtime`, a ReXGlue SDK fork with D3D12 only, replaces `sdk.patch` and `sdk-extra`. Full setup, generate and build pass in a new BuildRoot. The runtime source matches the old BuildRoot except for line endings. Setup rejects an old BuildRoot that has `sdk-source`.

## Current launcher defaults

These are the defaults of `Build.ps1 -Action run`. The shared cvars are in [outputs/FM4-build/pipeline.py](../../outputs/FM4-build/pipeline.py) (`launch_cvars`) and are also used by `e2e.py` and `matrix.py`. Full option reference: [running](../running.md).

| Setting | Default | Change with |
|---|---|---|
| Look | `pc` preset: `tonemap=aces`, `saturation=1.1`, `bloom=0.7`, `sharpen=0.4`, clearcoat 1, `ao=0.6`, `sun=2` (sun glare per pixel, soft limit 1,200), `sky=0.7` (track lighting: sky brightness), `taa=1` (TAA before the HUD, the scene jittered while the camera moves, the look's sharpening after it), `cockpit_light=0.5` (the cockpit interior's shadow floor), `windshield=1` (cockpit window glass) | `-Look <preset or knobs>`; `original` turns off all mods |
| Clearcoat | Setting A: `clearcoat=1`, `clearcoat_roughness` 0.3, `clearcoat_glint` 4. Chosen from three race A/B runs on 17 October 2026. Flakes are not used by any preset. | `-Look clearcoat=..,clearcoat_roughness=..,clearcoat_glint=..` |
| Ambient occlusion | GTAO on, strength 0.6, radius 0.75 m (presets `pc`, `aces`, `gt`, `neutral`) | `-Look ao=<strength>` |
| Glass, chrome and rims | Off in every preset (A/B at 2x on 18 October 2026; the sun highlights are not yet checked) | `-Look glass=..,chrome=..,rims=..` (and `glass_roughness`, `chrome_roughness`, `rims_roughness`) |
| Sky and vegetation | Off in every preset (A/B at 2x on 18 October 2026: only a slightly deeper sky) | `-Look sky_saturation=..,sky_contrast=..,sky_warmth=..,sky_horizon=..,foliage_hue=..,foliage_saturation=..,foliage_brightness=..` |
| Render target path | RTV: `render_target_path_d3d12=rtv`, `edram_single_rt_no_wrap=true` | `-Rov` (ROV, with `rov_occlusion_async=all`) |
| Occlusion query | `rov_occlusion_async=previous`; no sample correction (`rov_occlusion_awaited_report_divisor` at its default 1 since 18 October 2026) | `-Rov` |
| Vblank pacing | Variable (`vblank_vrr=true`) | `-FixedVblank` (fixed 60 Hz grid) |
| Frame rate | 60 in races; 30 for countdown, post-race, replay and photo mode | `-FrameRate <30-240>` (race only) |
| Resolve readback | `readback_resolve=fast`; readback publish on its own thread (`d3d12_readback_publish_thread=true`) | - |
| Submit thread | `d3d12_submit_thread=true` | - |
| Guest memory page state | `clear_memory_page_state=false` | - |
| Shader compilation | Synchronous (`async_shader_compilation=false`) | - |
| Anisotropic filtering | 16x (`anisotropic_override=5`) | - |
| Shadow workaround | Off | `-ShadowWorkaround` |
| Rear-view mirror | Every other frame (`FM4_HALF_RATE_MIRROR=1`) | `-FullRateMirror` (every frame) |
| Race quality | `high` (4x MSAA scenario, three tiles, SoftParticles and related settings) | `-Quality xbox\|high\|ultra` |
| Display | `native`: borderless fullscreen on the primary monitor | `-Display native\|720p\|1080p\|1440p\|4k`, `-Fullscreen`, `-Supersample`, `-ResolutionScale 1-4` |
| Render scale | Smallest whole multiple of 1280x720 that covers the 16:9 image on the display (720p 1x, 1080p, 1440p and 2560x1600 2x, 4K 3x), one step higher with `-Supersample` | as above |
| Presenter downscale | Area averaging (`present_area_downscale`) | - |
| Settings overlay | Hidden | `-Info`; F2 toggles it |
| Window mode | Toggled with Alt+Enter | - |

Notes on the defaults:

- The shadow workaround (`SkipShadowMapUnlessCockpit`, from xenia-canary/game-patches#398) is no longer a default as of 16 October 2026. Without it, the shadow map is rendered on every race camera. The captures checked show no box shadows. Cars now have self-shadowing and crisp shadows on the asphalt. The flag is read when the scenario is pushed, so it cannot be A/B-tested in the middle of a race.
- `-SingleTile` (sets `FM4_TILING_SCENARIO=2`, with FXAA, which the launcher leaves out while TAA is on) is not a default. Its trade-off is in [Open issues](#open-issues).
- Look mods and AO run on the RTV path only, by design: `-Rov` turns the shader overrides off and shows the title's own picture. ROV is the reference for the EDRAM emulation, so mods and AO are not planned for it, and this is not counted as a gap.
- The launcher no longer passes `rov_occlusion_awaited_report_divisor=2` (18 October 2026), so `-Rov` reports the samples it counts and stays a reference without a title correction. The cvar remains in the runtime for experiments; it only affects ROV occlusion samples (`RetireROVOcclusionSnapshots` in [work/runtime/src/graphics/d3d12/command_processor.cpp](../../work/runtime/src/graphics/d3d12/command_processor.cpp)).

## Current performance

These numbers are baselines, not targets.

- **Quick Race**: 60.0 fps (p99 17.1 ms), 118-120 fps at 120 Hz (p99 8.8-9.8 ms), and 224-235 fps at 240 Hz (p99 5.3-6.2 ms). All three are from the 16 October 2026 matrix.
- **240 Hz**: the race is limited by the command processor thread at 1x. GPU time is about 2.8 ms per frame without AO and 3.01 ms with AO.
- **2x with AO**: about 162 fps, limited by the GPU (6.09 ms per frame).
- **Track driving at 120 Hz**: the last track fps measurement (15 October 2026, before the final command processor changes that day) was 100-106 fps with 2,800-3,000 draws per frame. After those changes, command processor time per frame fell from 8.6-8.9 ms to 4.6 ms at 2,400-2,800 draws, but no track fps figure was recorded. The 16 October matrix measures Quick Race only, so 120 fps on track is not yet shown.
- **Refresh-rate scaling**: the race clock runs at 1.001 times the wall clock at high refresh rates. The game scales its time step, so it does not run faster.
- **Shader cache**: with an empty cache, synchronous shaders add a little hitch on load (about 9 frames over 100 ms per run, worst 400-640 ms). Race fps is not reduced.

## Known limitations

- **Supported input**: the three XEX executables of the Japan/Asia release (media ID `633772A0`) or of the USA release (`26FF1101`). Their hashes are recorded in [outputs/FM4-build/pipeline.py](../../outputs/FM4-build/pipeline.py). Other regions or revisions, modified executables, a mix of both releases and title updates are rejected. The user must extract the ISO first. The two releases differ only in their executable headers ([release comparison](../analysis/releases.md)).
- **Platform**: Windows x64 only, with a matching toolchain and GPU. Setup checks the prerequisites and does not install Visual Studio, Python or LLVM. A clone alone is not a playable game package. See [setup](../setup.md).
- **Save data**: saves and progress from historical builds loaded. Compatibility of all saves and profile migration between builds or computers is not guaranteed. The workflow uses its own local profile and does not copy old saves automatically.
- **Profile state in e2e**: on 14 October 2026, three runs with a new, empty profile did not reach a race in the harness. The game then shows the default car and different pre-race screens, which the harness did not recognize. With a copied profile and shader cache, 6 of 6 runs reached the race. The e2e scenarios therefore use `--profile copy`. See [e2e overview](../e2e/README.md).
- **Harness menu navigation**: the game ignores the first press after several screens open. The career menu and `GO RACE` also hide about 9 seconds after the last press (a real-time timer, not a frame count). The e2e scenarios press until OCR reads the expected screen, instead of counting presses.
- **Developer AI driver**: `engageaionstart` (rebuilt on 16 October 2026) is a test tool, not a player feature. `matrix.py` uses it by default unless `--human` is given. Direct launch into a race is not available, because the code that reads the `track`, `car` and `launch` options is missing from the retail build.
- **Shadows**: with the workaround off (the default), the shadow map is rendered on all race cameras. Its parity with the original Xbox has not been compared.
- **Stubs**: some Xbox and runtime service functions are still stubs. The synthetic instruction tests do not prove that the kernel, audio, timing, synchronization or shaders are correct.
- **Synthetic tests**: the 295,650 instruction checks and 5 script tests use synthetic data. They do not prove that the game behaves correctly.

## Open issues

- **Sun glare strength depends on the MSAA level.** Matching the original Xbox glare is not a goal (decision of 18 October 2026): the glare is to look good and stay the same at every quality setting. Measured on 18 October 2026 (Quick Race with the camera cycle, `--quality xbox`, `REX_OCCLUSION_SEGMENT_TRACE`): the sun query (the awaited report) sees the sun for about 3 seconds at the same point of each run (about 82 to 86 s). On ROV the sun sprite's samples (passed plus depth-failed, at most) are 338 at 1x MSAA, 675 at 2x and 1,355 at 4x (`FM4_TILING_SCENARIO` 2, 1 and 0): the counter counts guest samples, exactly in proportion to the MSAA sample count. The empirical divisor of 2 (13 October, races at 2x MSAA) therefore equalled the sample count of that tiling and turned samples into pixels; since races at `high` quality use 4x MSAA, it would have left twice the pixel count. On RTV at 2x the host query passes 2 to 10 samples in 15 frames at the same point, so it does return samples when the sun is in view. A sweep of fixed counts on RTV (`rtv_occlusion_debug=fake`, same race, captures at the sun point) shows that the glare does not saturate at the sprite's full area: at 1x MSAA nothing shows at 85, a small star at 340, a larger star with haze at 680 and 1,360, and the screen washes out at 4,000. So the real counts at 1x (at most 338) can give only a small star, and those at 4x (at most 1,355) a large one. The same count gives a weaker glare at 4x than at 1x (340: none against a small star; 1,360: a star against a star with haze), but by much less than the factor 4 that a title expecting samples would show. The captures of different runs differ by up to 0.3 s, so this is a visual judgment, not a measured curve. The count is therefore not simply a fraction of the sprite, and the glare may also depend on the frame-wide queries, which are not faked and also scale with MSAA. With a fixed count the glare also shows where the sun is behind the cockpit roof, which a normal run (real counts, ROV or RTV) does not. The title code and the constants of the glare draw were not traced, because the goal is no longer parity. The `sun` look knob (18 October 2026, `sun=2` in every preset but `original`) answers this on the RTV path: the awaited reports become guest pixels times the strength, through a soft limit of 1,200 pixels (see [running](../running.md#sun-glare)). A/B on 18 October 2026 (RTV, the same race, fixed counts so that the sun shows at lap time about 39 s, `REX_OCCLUSION_SEGMENT_TRACE`): with `sun=2`, 340 samples at 1x and 1,360 at 4x both report 607 pixels and give the same medium star, where without the knob they gave a small star and a large one; 4,000 samples at 4x report 933, and 8,000 at 1x report 1,161, a large star with haze over a scene that stays readable, where 4,000 samples without the knob wash out the screen. The MSAA is taken from the draws of the query interval, because the surface at the report can already be another one (at 1x the first build read 4x there and reported 170). Races with real counts at `high` quality could not be compared with and without the knob, because the AI driver took different lines (fifth against first place).
- **Full-frame occlusion on RTV**: the full-frame query used for auto-exposure reports about 1.5 times the ROV sample count, because the host query counts every sample that passes. No visible effect was seen. The auto-exposure query was not changed.
- **RTV missing road pieces (mitigated, not fully explained)**: on some runs of the RTV path, road pieces (mostly on the start straight, 4-14 seconds after the start) were not drawn and showed the flat clear color (102,102,102). Kerbs and other objects were still drawn. The cause was found on 15 October 2026. The loader's sGPU heap could not place a 1.8 MB section because the heap was fragmented after a load spike. The loader then failed and discarded the request. The fix is a fallback: when the sGPU allocation fails, the section is retried from fGPU. Set `FM4_STREAMING_FALLBACK=0` to turn it off. See [work/fm4-rex/src/fm4-streaming-fixes.cpp](../../work/fm4-rex/src/fm4-streaming-fixes.cpp). After the fix, 20 of 20 runs with fixed vblank (the more frequent failure case) showed no missing road, and 5 of those runs used the fallback successfully. Three Quick Race runs with the current defaults also showed no clear-gray frames. The e2e harness detects the symptom through `clear_gray_frames`.
- **Look preset values**: the B4 step 3 note of 16 October 2026 says the preset values had not been chosen yet. The defaults in the table above were set later that day, and that note is superseded (see [history](history.md)).
- **Tiling quality versus speed**: on ROV (3 runs, 14 October 2026), `-SingleTile` raised fps from 54.3 to 58.6 and cut p95 from 32.9 ms to 19.5 ms. Without MSAA, edges are jagged. FXAA smooths some of them, but thin wires break up. With TAA (19 October 2026) the single tile looks close to `high` at 2x and costs about 1 ms less GPU time per frame; the project owner kept `high` as the default.
- **Ambient occlusion scope**: AO is missing in the cockpit interior (the depth range is compressed there) and in the garage and showroom (no camera vertex-shader constants). See [GTAO design](../analysis/gtao-design.md).

## Current gaps

Everything still open, grouped by area.

### Display

- The `sun` knob keeps the sun glare the same at every MSAA level, checked with fixed counts only (see [Open issues](#open-issues)). `-Look original` and `-Rov` keep the glare that changes with the MSAA level.
- AO is not in the cockpit interior, garage or showroom. See the "Later" section of [GTAO design](../analysis/gtao-design.md).
- The cockpit interior is near black on sunny parts of the Bernese Alps (dashboard top 0.1 to 3 of 255, gauges 1 to 6 at 2x). Measured on 19 October 2026 with frozen frames: RTV and `-Rov` match within a few levels with `-Look original`, there is no transient after a camera switch, and in tunnels the interior is readable. It is the game's interior lighting: its own shadow map shades it, `MinCockpitShadow` is 0, and the ambient term is small against an exposure set by the snow outside. The `pc` look darkens it by 15 to 35% more through ACES; AO does not reach it. The `cockpit_light` look knob (19 October 2026, 0.5 in `pc` since 20 October 2026) raises the shadow floor of the 38 interior shaders and the six driver shaders: at 0.5 the gauges go from 2 to 4 of 255 to 32 to 60 when the sun is behind or beside the car, and the windows, the exposure and the other cameras stay as they are; with the sun ahead, and on the black dashboard top and wheel rim, it changes little. A cockpit exposure of +2 stops blows out the windows. The windshield and side windows look open in the cockpit camera because the title draws no window glass for the player's car in that camera (only in the 3-2-1 camera); the `windshield` post pass (1 in `pc`) adds it. See [cockpit brightness](../analysis/cockpit-brightness.md).
- Look mod coverage is partly verified in the game (17 October 2026, [look mod coverage](../analysis/look-mod-coverage.md#in-game-checks-17-october-2026)): false-color runs show the body paint mods of races, replays, the cockpit, car menus, HomeSpace, Autovista (the showroom) and the 16-car and split-screen car shader sets on screen. The 16-car and split-screen sets were loaded in a Quick Race through `FM4_RACE_SETTINGS`. On 18 October 2026 the title's 16-car render scenarios were reproduced at `-Quality xbox` on a 12-car grid (Quick Race has no 16-car grid), and photos at shutter speed 0 and 100 (also one from a replay) were saved; `REX_GPU_INVENTORY` showed no new hash. A real split-screen race and a real 16-car grid were not run. The second body paint technique of each set, the four watermark final combines, the no-post-effects final combine and the three photo motion blur variants never showed in any run, so their mods are checked by compilation only. `E5B1A71249D0CA9A`, first thought to be the environment cube render, draws the car lights and trim, so it needs no clear coat.
- The glass, chrome and rim mods (`-Look glass`, `chrome`, `rims`; 31 hashes of the race, 16-car, split-screen and garage car shader sets) are in no preset. The in-game A/B at 2x covered one track and time of day: a sun highlight on glass or chrome, the split-screen set and the chrome rims on screen were not seen. The spinning-wheel blur shaders, the chrome with a normal map, the showroom set and the material shaders that load in the UI only have none. See [look mod coverage](../analysis/look-mod-coverage.md#glass-chrome-and-rims-18-october-2026).
- The sky and vegetation mods (`-Look sky_*` and `foliage_*`; 15 hashes: the race sky dome, trees, tree cards, bushes and grass of every race track) are in no preset. False color and an A/B at 2x covered the Bernese Alps and Maple Valley at one time of day. The HomeSpace skies, `DIFF_TREEBEND_1` (trunks and branches) and the terrain under the grass have none, and 5 of the 15 hashes did not draw on these two tracks. See [look mod coverage](../analysis/look-mod-coverage.md#sky-and-vegetation-18-october-2026).
- The road surface mods (`-Look road_*`; 25 hashes: the road shaders of 20 race locations, the painted lines, the road edges and the paved shoulders) are in no preset. The near-white sheet on sunlit asphalt toward the sun is the game's own sun highlight of `ROAD_BLND_2`, a Blinn-Phong lobe of power 25 with no Fresnel, whose specular amount comes mostly from the blend value of the rubber, which `road_gloss` does not scale: `road_specular=0` removes it, `CameraExposureRange` with `maxval` 1 leaves it as it is. The sheen in its place has a grain (`road_sheen_grain`, default 1) since 18 October 2026, after the sheen without it turned the sun-facing asphalt of Top Gear into an even, milky haze. In A/B runs at 2x on the Bernese Alps and Top Gear, `road_specular=0,road_sheen=0.4` takes the Alps sheet from 51.8% of the crop at 230 or more to 0.4% and keeps the aggregate and the skid marks of Top Gear visible through the glare; views away from the sun change by at most 1.5 of 255, with no seams at the lines and edges. Facing the sun on Top Gear, a hood view patch where the game's glint is weak is 24 of 255 brighter than `pc`, a cockpit view patch 6 darker. The candidate waits for the project owner's review of the crops. False color covered the Bernese Alps, Tsukuba, Sebring and Top Gear; the A/B ran at one time of day. Indianapolis and Infineon (`2AD313C93939CC4E`), the shoulders of Tsukuba and the other `SHLDR_*` hashes have no mod. See [look mod coverage](../analysis/look-mod-coverage.md#the-sheen-on-top-gear-and-its-grain-18-october-2026).
- The track lighting knobs `road_gloss` and `sky` (`FM4_TRACK_SETTINGS`, see [running](../running.md#track-lighting)) work in the game. The `pc` preset has `sky=0.7` since 18 October 2026; no preset sets `road_gloss`. Results of that day (BuildRoot `I:\FM4-build2`, `main` `6f8d04f` with the knobs; look `pc`; Bernese Alps Quick Race at 2x, 2560x1440):
  - The game mounted the edited copies of 30 files in 24 track folders.
  - For identical frames, the player's car stays on the grid with no AI driver and no throttle, and the camera looks around with the right stick. Quick Race puts the car in one of several grid slots at random, so only runs that landed in the same slot are compared.
  - Sky: a clear-sky patch measures RGB (147, 187, 216) at `sky=1`, (129, 173, 206) at 0.8 (another slot), (119, 163, 199) at 0.7 and (107, 152, 190) at 0.6. At 0.7 the sky is bluer and less washed out, and the snow and clouds keep their brightness. The project owner chose 0.7 for the `pc` preset.
  - Road: looking toward the sun, the sunlit asphalt next to the shaded grandstand is a near-white sheet (51.6% of a 640x300 crop at 230 or more), and `road_gloss` does not remove it. It is the road shader's own sun highlight; see the road surface item below. In the normal hood view, the sparkle at `road_gloss=1` reads better than the slightly milky 0.25, so the knob stays at 1.
  - Smoke gates at 240 Hz passed 3 of 3 cells with the knobs off (`rsoff`, which covers the runtime's file link lookup in every file open) and with `road_gloss=0.5` and `sky=0.8` (`rson`). `rson` against `rsoff`: `career` PASS; `photo` and `photo_view` WARN on capture statistics only. The error keys are the same, and there are no clear-gray frames.
  - The new `pc` preset (`sky=0.7`, gate `pcsky07`) against `pc` with the game's sky (`pcsky1`, the same binary, run just before it) at 240 Hz: 3 of 3 cells passed. `career` PASS (race 192.0 fps, p99 8.5 ms; replay 30.0 fps). `photo` WARN on capture statistics only (race 204.6 fps; photo mode 29.94 fps). `photo_view` WARN on capture statistics only (56.98 fps). There are no new error keys and no clear-gray frames, and only the `pcsky07` cells mounted the copies.
- Parity with the original Xbox (shadows, color, reflections, noise and object edges) has not been compared. The combinations of MSAA and render scale are not all verified.
- The post pass framework (18 October 2026) gives screen passes at three points of the frame, history textures, camera reprojection, per-car motion vectors, the jitter of the scene draws and a write-back into the game's scene, and has GTAO, the motion vectors and TAA ([post pass framework](../analysis/post-pass-framework.md)); rain screen effects, screen-space reflections and temporal upscalers are not written yet (see [post pass framework](../analysis/post-pass-framework.md#the-final-point-and-upscalers)). The motion vectors (host-built from the game's resolved depth, its stencil object IDs and the car and camera matrices of the vertex shader constants; [motion vectors](../analysis/motion-vectors.md#8-implementation-18-october-2026)) are verified in the game; the cockpit interior, the world through car glass and the rear-view mirror panel are handled with a mask. Wheel spin and the steering wheel and hands take the motion of their car or interior. Not traced: a 16-car grid, and what sets the scale k of the game's matrices.
- TAA (19 October 2026; [TAA, current state](../analysis/taa.md#current-state)): `pc` has TAA at the final point, before the HUD, with the `high` tiling and `TAA_SHARPEN` 0.4, and since 20 October 2026 the scene jittered only while the camera moves and at 45 fps or more; `-Look "pc,taa=0"` turns it off and `-Look "pc,taa_point=1"` runs it at the scene_color point instead, as an option. It costs about 0.2 ms of GPU time per race frame at 2x (0.47 ms at the scene_color point), passed the smoke and perf gates and the Japan/Asia check, and the scene camera behind it is known on all 26 environments of the TRACKS screen and in replays. There is no reactive mask for particles and the interior mirror; nothing ghosted in the checks so far.
- Texture replacement (`-TextureDump`, `-TextureReplace`; 19 October 2026) works on the Bernese Alps: a Quick Race dumps 969 textures, the sky panorama (`5de3b416781cfd97`, DXT1 8192x2048) keeps its hash across runs, and a test pattern in its place shows in 2x captures and in the car reflections. The smoke gate with both switches on passed 3 of 3 cells against the same binary with them off (`career` PASS, `photo` and `photo_view` WARN on capture statistics). See [texture replacement](../analysis/texture-replacement.md#verification). Open: no real replacement art yet; no other track, the HomeSpace or the garage dumped; replacements load on the command processor thread, which a large pack would make stutter; render targets, cube maps, arrays, 3D textures and textures that change all the time are not covered. The offline extractor (`texture_extract.py`, 19 October 2026) dumps the textures stored in the 32 track archives without running the game: 18,473 distinct hashes, among them a sky panorama for every race track (tiles on Road America and Silverstone). Its Bernese Alps sky is byte-identical to the runtime's dump, and 103 of its 493 Bernese Alps hashes are in the runtime dumps of that track; the textures streamed from the `.bix` files (8,300 records) and cube maps are not extracted ([offline extraction](../analysis/texture-replacement.md#offline-extraction)).
- Not done: post-process anti-aliasing other than TAA (SMAA, or FXAA without `-SingleTile`), HDR10 output, new lighting for tracks (beyond the `TrackSettings` knobs), car materials other than paint, glass, chrome and rims (tires, brakes and lamp lenses; the cockpit interior has only the `cockpit_light` shadow floor), track surfaces other than the road, its lines, edges and paved shoulders (terrain, kerbs, grass), and car flake paint. Flake cells in UV space are too small for race cameras and need an LOD model.
- Widescreen and ultrawide need a FOV and projection hook, culling and HUD changes. The image is always 16:9, with bars on other screens.

### Performance

- At 240 Hz, the race is limited by the command processor (pacing, swap synchronization with the submit thread, and cost per draw). At 2x with AO, it is limited by the GPU (about 162 fps).
- The race clock does not run faster above 60 Hz (ratio 1.001). But lap time and handling are not yet compared between 60 and 240 fps.

### Gameplay coverage

- Not covered by e2e: photo overwrite, photo with an empty shader cache, Autovista, a Disc 2 car, and direct launch into a race.
- Not validated: all cars and tracks, one full career season, Rivals, Hot Lap, split screen, audio (XMA, 5.1 output, intro sync), Kinect, multiplayer and online play, and most cars of downloadable content (one Porsche Expansion Pack car on the Launch Track Pack's Test Track Benchmark is covered by the `dlc` scenario).

### Settings and input

- The in-game PC settings overlay (saved to TOML), key rebinding and full controller support (rumble, triggers and wheel) are not done or verified.

### Stability and runtime

- A 2-hour soak test (memory, handles and VRAM) has never been run.
- The XAM and kernel stubs called across all scenarios have not been listed from `runtime.log`.
- Save compatibility between builds and profile migration between computers are not guaranteed.

### Platform and distribution

- The clone and the full workflow have not been run on a second Windows computer.
- AMD and Intel GPUs and lower-end CPUs have not been tested.
- Only the Japan/Asia and USA releases are supported. The European release and title updates are not. The European discs have not been compared with the other two. A title update relinks the executable, so supporting one needs a second integration profile and a runtime fix for the USA v4 module patches ([title updates](../analysis/title-updates.md)).
- There is no CI, no setup wizard, and no license and provenance review for a release. This includes the pending `XenonRecomp-FM4-fixes.patch`. See [sources and licensing](../sources-and-licensing.md).

## Next validation steps

The proposed order is:

1. **Run the 2-hour soak test and the 60 vs 240 fps handling test.** The soak test watches memory, handles and VRAM. The handling test compares lap times and handling at both rates.
2. **Run the clean setup on a second Windows computer.** Run `setup`, `generate`, `build` and `run` from a clean clone.
3. **Extend e2e coverage.** Add photo overwrite, photo with an empty shader cache, Autovista, a Disc 2 car, cars of the other DLC packs, and direct launch. Re-test photo overwrite and the empty shader cache on the latest build, because neither has been repeated since the 16 October changes. The photo, Autovista and replay steps are planned in [photo mode, Autovista and replays](../analysis/photo-autovista-replay.md#4-proposed-plan).
4. **Inventory the stubs.** List every XAM and kernel stub called in `runtime.log` across all scenarios.
5. **Choose the road look.** Review the crops of `road_specular=0,road_sheen=0.4` (the Bernese Alps and Top Gear, facing the sun) and decide whether `pc` gets it; then run a smoke gate and confirm the race GPU cost with the `perf` tier. See [look mod coverage](../analysis/look-mod-coverage.md#the-sheen-on-top-gear-and-its-grain-18-october-2026).
6. **Finish the glass, chrome and rim check.** A sun-facing scene (a replay camera or another track) for the highlights, and the split-screen set; then decide whether `pc` gets them. See [look mod coverage](../analysis/look-mod-coverage.md#in-game-checks-18-october-2026).
7. **Re-run the gate tiers after each change.** Use `smoke` for quick checks, `perf` for performance, `full` before committing a stage, and `career` for the rewards path. See the [e2e overview](../e2e/README.md) for the commands and gates.
8. **The default tiling.** The project owner kept `high` for `pc` with TAA on 19 October 2026. With TAA the single tile looks close to `high` in 1:1 crops at 2x and costs about 1 ms less GPU time per frame ([TAA, MSAA and tiling](../analysis/taa.md#5-msaa-and-tiling)); it is also the tiling the upscalers would need.
9. **Watch TAA for ghosting, then trace a 16-car grid.** TAA steps 1 to 7 are done ([TAA](../analysis/taa.md#current-state)). Not observed yet: tire smoke from the cockpit camera, sparks of a collision, the heavy smoke of a full grid at the start, and gravel dust; a case that ghosts gets step 8, a reactive mask written by a shader mod for that shader. The scene_color point could become the default after a smoke gate with `taa_point=1`, the write-back counter on more tracks, career, replays and photo mode, and evidence that its occlusion filter helps.
10. **Build a sky pack.** The offline extractor gives the skies of every track (`<BuildRoot>\texextract\panoramas.txt`); check one sky other than the Bernese Alps in game, look at the Road America and Silverstone tiles, and load replacements off the command processor thread before building a large pack. See [offline extraction](../analysis/texture-replacement.md#offline-extraction).

This page documents the status. It does not report that all further tests have been run, and it is not an instruction to run them automatically.

## Related documents

- [history](history.md): dated chronology and past results
- [setup](../setup.md), [running](../running.md)
- [e2e overview](../e2e/README.md), [traces](../e2e/traces.md), [experiments](../e2e/experiments.md)
- Analysis: [post pass framework](../analysis/post-pass-framework.md), [GTAO design](../analysis/gtao-design.md), [motion vectors](../analysis/motion-vectors.md), [B4 shader research](../analysis/b4-shader-research.md), [visual style](../analysis/visual-style.md), [material atlas](../analysis/material-atlas.md), [photo mode, Autovista and replays](../analysis/photo-autovista-replay.md)
- [sources and licensing](../sources-and-licensing.md)
- [Build script](../../outputs/FM4-build/Build.ps1), [verification record](../../outputs/FM4-build/verification.json), [e2e harness](../../outputs/FM4-build/e2e/e2e.py), [matrix](../../outputs/FM4-build/e2e/matrix.py)
- [Project overview](../../README.md), [agent guide](../../AGENTS.md)
