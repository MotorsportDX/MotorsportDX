# FM4 Recompilation: Current Status

As of 24 October 2026. Latest change, also the latest code change: the native path by default with integrated GPUs only (stage N4; [draw back end](#draw-back-end-stage-n4-23-october-2026)); before it a present thread for the asynchronous swap, and the second ROG Xbox Ally X run (stage N4; [draw back end](#draw-back-end-stage-n4-23-october-2026)); before it render scale 1x with integrated GPUs only in the launchers (stage N4; [draw back end](#draw-back-end-stage-n4-23-october-2026)); before it the GPU time per pass in the perf trace (stage N4; [draw back end](#draw-back-end-stage-n4-23-october-2026)); before it step 1 of N5's native resolution, a fractional draw scale type and the `native_draw_scale` cvar with no consumer yet, TAA's size mismatch logged as an error, and the render scale passed explicitly in every e2e matrix cell ([D3D-level HLE, 18.10](../analysis/d3d-hle-n5.md#1810-increment-1-step-1-the-rational-type-24-october-2026)); before it the launchers' VSync option (stage N4; [draw back end](#draw-back-end-stage-n4-23-october-2026)); before it host presents once a guest frame, also from the submit thread, and e2e without the settings overlay (stage N4; [draw back end](#draw-back-end-stage-n4-23-october-2026)). Latest result: the ROG Xbox Ally X runs of stage N4 ([draw back end](#draw-back-end-stage-n4-23-october-2026)). Before it: the close gate of stage N3 of the native port, which closes the stage ([one pass with 2x MSAA](#one-pass-with-2x-msaa-stage-n3-23-october-2026)); before it `-MsaaSamples 4`, races in one pass with 4x MSAA under `-Native` in `Build.ps1`, `e2e.py` and the GUI launcher ([one pass with 2x MSAA](#one-pass-with-2x-msaa-stage-n3-23-october-2026)); before it photo mode's 4x MSAA depth copies at 1x from the host render target under `-Native` ([one pass with 2x MSAA](#one-pass-with-2x-msaa-stage-n3-23-october-2026)); before it fM4-PC's own fast copy of 4x MSAA surfaces, which no longer reads the vertical sample in four pixel columns of every tile and changes `-Rov`'s output with the project owner's approval ([one pass with 2x MSAA](#one-pass-with-2x-msaa-stage-n3-23-october-2026)); before it one pass with 4x MSAA as a development setting of stage N3, off by default, with a private scene colour and depth ([one pass with 2x MSAA](#one-pass-with-2x-msaa-stage-n3-23-october-2026)); before it the last items of stage N4 before the ROG Xbox Ally X runs: the GUI launcher's native path, performance trace and extra cvars, 1 ms timer resolution without power throttling, the process's CPU time in the perf trace, and the host render queue's wait (stage N4; [draw back end](#draw-back-end-stage-n4-23-october-2026)); before it the title's ring index wait sleeping until the read pointer moves, on in the launcher for RTV and `-Native` (stage N4; [draw back end](#draw-back-end-stage-n4-23-october-2026)); before it the close gate of stage N4 of the native port, which turns its three switches on in `-Native`, with the source of each build in `build-result.json` and a WARN for race captures washed out to white ([draw back end](#draw-back-end-stage-n4-23-october-2026)); before it races in one pass with 2x MSAA under `-Native`, stage N3 of the native port, with `-MsaaSamples 0` for one tile ([one pass with 2x MSAA](#one-pass-with-2x-msaa-stage-n3-23-october-2026)); before it the render target cache's update skipped at draws that would repeat it, for stage N4 of the native port, off by default ([draw back end](#draw-back-end-stage-n4-23-october-2026)); before it texture bindings kept when their fetch constant is written again with the same dwords, off by default; before it the cheaper capture of the draw back end (increment 3: written registers marked per range, the fetch constants the shaders read), off by default; before it the draw back end on the thread that replays the deferred command list, with its capture, off by default; before it the e2e fast route, which reaches the race through the title's screen events instead of fixed delays and saves about 57 s per `photo` run ([e2e fast route](#e2e-fast-route-22-october-2026)); before it the deferred constant buffers of stage N4 of the native port, off by default ([deferred constant buffers](#deferred-constant-buffers-stage-n4-22-october-2026)); before it the resolves from the host render target of stage N3 of the native port, on with `-Native` and off by default otherwise ([resolves from the host render target](#resolves-from-the-host-render-target-stage-n3-22-october-2026)); before it the resolve readbacks on demand of stage N3, on with `-Native` and off by default otherwise ([readback on demand](#readback-on-demand-stage-n3-22-october-2026)), with the read trace that measured them; before it stage N2 of the native port, closed, with its op lists and binding reuse off by default ([decoded replays and binding reuse](#decoded-replays-and-binding-reuse-stage-n2-22-october-2026)); before it the third increment of stage N1 of the native port, the dirty state writers as records applied at markers, on with `-Native` ([dirty state writers as records](#dirty-state-writers-as-records-stage-n1-22-october-2026)); before it the second increment of stage N1 of the native port, the coverage of the PM4 writers by phase and the identity check of the host render thread, off by default ([identity check](#identity-check-stage-n1-22-october-2026)); before it the first increment of stage N1 of the native port, a host render thread fed by a record queue in place of the command processor thread, behind `-Native` and off by default ([host render thread](#host-render-thread-stage-n1-21-october-2026)); before it the measurements of stage N0 of the [native port](../analysis/d3d-hle.md#13-stage-n0-measurements-21-october-2026), with their counters (off by default) and one registry for the hooks of the D3D layer ([D3D layer counters](#d3d-layer-counters-and-stage-n0-21-october-2026)); before it TAA compares each pixel of a still camera with the same jitter phase a cycle ago, so still pictures keep thin details and colored edges still and what moves blends as in motion ([TAA's phases on a still camera](#taas-phases-on-a-still-camera-20-october-2026)); before it the [GUI launcher and export](#gui-launcher-and-export-20-october-2026), a window with the options of `Build.ps1 -Action run` that runs the game without Python or Visual Studio, also from a folder copied to another computer; before it races in one tile without MSAA whenever the look has TAA, the presets `aces` (the default, the earlier `pc`, now its alias), `gt` and `neutral` with the same mods and `original` with TAA only, a still camera's jitter accumulated into a still picture, and the fix of TAA's history, which neither point had had since 19 October 2026 ([one tile with TAA](#one-tile-with-taa-the-presets-and-taas-history-20-october-2026)); before it the asynchronous swap, on in the launcher, which removes the command processor's wait for the submit thread at every swap ([asynchronous swap](#asynchronous-swap-20-october-2026)); before it `pc` with the clear coat's sun highlight limited to `clearcoat_glint=1`, the road sheen and the glass, chrome and rim mods, after an A/B toward the sun ([pc facing the sun](#pc-facing-the-sun-20-october-2026)), and AO found running in the garage, Autovista and the career home ([AO outside races](#ao-in-the-garage-autovista-and-the-career-home-20-october-2026)); before it replays and the 3-2-1 countdown at the race frame rate instead of 30 ([replay and countdown frame rate](#replay-and-countdown-frame-rate-20-october-2026)); before it TAA's jitter only while the camera moves, so still pictures no longer shimmer ([TAA jitter gate](history.md#20-october-2026-taa-jitter-on-still-pictures)); before it `cockpit_light=0.5` and `windshield=1` in the `pc` preset ([pc with the cockpit knobs](#pc-with-the-cockpit-knobs-20-october-2026)); before it the [cockpit glass](../analysis/windshield.md) post pass and the [`cockpit_light` look knob](../analysis/cockpit-brightness.md#the-cockpit_light-knob-19-october-2026), a shadow floor for the cockpit interior and the driver; before it the [motion vector gaps](../analysis/motion-vectors.md#9-the-gaps-in-the-game-19-october-2026) (the cockpit interior, the world through car glass, the mirror panel, and a motion mask) and four columns of `REX_DRAW_LIST_TRACE` (viewport Z, blend, color mask); before it the per-car [motion vectors](../analysis/motion-vectors.md#8-implementation-18-october-2026) on the runtime's [post pass framework](../analysis/post-pass-framework.md), the [grain of the road sheen](../analysis/look-mod-coverage.md#the-sheen-on-top-gear-and-its-grain-18-october-2026) (`road_sheen_grain`), the [road surface look mods](../analysis/look-mod-coverage.md#road-surface-18-october-2026) and 64 live knob slots, the stencil and shader constant columns of `REX_DRAW_LIST_TRACE` (diagnostics only, for the motion vector checks), and the post pass framework with GTAO on it. The dated chronology and earlier results are in [history](history.md).

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
| One tile with TAA at every quality, the presets with TAA, a still camera's accumulation, TAA's history | Still pictures checked in the game at 2x; smoke gates at 240 Hz against the tiles of `high` and with the asynchronous swap | [one tile with TAA](#one-tile-with-taa-the-presets-and-taas-history-20-october-2026), 20 October 2026 |
| `pc` facing the sun (`clearcoat_glint=1`, road sheen, glass, chrome and rims) | A/B at 2x on the Bernese Alps grid toward the sun; smoke gate of the new `pc` against the earlier one | [pc facing the sun](#pc-facing-the-sun-20-october-2026), 20 October 2026 |
| AO in the garage, Autovista and the career home | Verified in the game at 2x (debug view, A/B in Autovista) | [AO outside races](#ao-in-the-garage-autovista-and-the-career-home-20-october-2026), 20 October 2026 |
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

BuildRoots on 20 October 2026: `I:\FM4-build-usa` (USA discs, the development base) and `I:\FM4-build-pass` (Japan/Asia discs, for side builds). `I:\FM4-build2`, `I:\FM4-build-iter` and `I:\FM4-build-gtao` were removed that day; the runs, shader dumps, inventories, gate logs, material atlas and shader storage that the docs cite were moved first to `I:\FM4-archive\<BuildRoot>` with the same relative paths, and the docs point there. Results named with a bare BuildRoot of those three describe where a run was made.

### Draw back end, stage N4, 23 October 2026

Step 2 of stage N4 of the [native port](../analysis/d3d-hle-n4.md#178-step-2-increment-2-results-23-october-2026): with `d3d12_draw_back_end=2` (off by default; it needs bindless resources, host render targets and `async_shader_compilation` off, as `-Native` sets them) the render thread captures each draw's back-end inputs (484 bytes a draw in the race) into the deferred command list, and the submit thread, as it replays the list, configures the pipeline, the fixed function state, the system constants, samplers, constant buffers and root parameters and records the draw. With its check (`d3d12_draw_back_end_check`, every 1,000th comparison perturbed) a `career` run compared 41,252,704 draws: only the 41,252 perturbed comparisons differed, no draw was dropped. In the race at 240 Hz and 1x (two runs each way, 1,800 draws a frame): render thread busy 3.38 to 2.99 ms (−0.39 ms, of which the capture costs 0.25 ms), the submit thread's recording 0.72 to 1.57 ms, p99 not higher at the same draw load (6.40 to 5.26 ms pooled). The step gate asked for −0.4 ms on the render thread and was accepted at −0.39. Increment 3 ([results](../analysis/d3d-hle-n4.md#179-step-2-increment-3-results-23-october-2026)) marks written registers per range and captures only the fetch constants the draw's shaders read (409 to 432 bytes a draw): the render thread's busy time is 2.94 ms at 1,800 draws, 0.50 to 0.59 ms under off in the same session, the marking costs nothing measurable, the capture about 0.23 ms; the check stays exact (41,874,077 draws). `texture_bindings_keep_same_dwords` (off by default) keeps a texture binding whose fetch constant was written again with the same dwords (44% of the keys computed again in the race): the binding check stays exact (34,992,998 bindings), the textures zone gains 0.02 ms ([texture keys](../analysis/d3d-hle-n4.md#1711-the-texture-keys-23-october-2026)). `render_target_update_guard` (off by default) skips the render target cache's update at the 85 to 88% of the race's draws that would repeat it: its check stays exact on `-Native` and the one pass in 2x (36,302,177 and 38,094,707 skipped updates), the render thread is busy 2.72 ms at 1,800 draws (2.95 without it), both threads 4.40 ms. With these switches the 240 Hz race runs at 237.8 fps on average with p99 5.02 ms pooled, 4.93 to 4.97 ms up to 2,000 draws a frame and 5.29 to 5.33 ms above ([update guard](../analysis/d3d-hle-n4.md#1713-the-render-target-caches-update-at-the-draw-23-october-2026)). Since the close gate ([17.15](../analysis/d3d-hle-n4.md#1715-the-close-gate-23-october-2026)), `-Native` turns on the back end on the replay, `texture_bindings_keep_same_dwords` and `render_target_update_guard`; `d3d12_deferred_constants` stays off. The gate on `n4be12` passed: the combined checks are exact in one tile and in one pass with 2x MSAA, the frozen frames of RTV and `-Native` pass at 1x and 2x, and 1x smokes with `-Look "original,taa=0"` on RTV and `-Rov` pass (6 of 6 cells). In the 240 Hz race, three runs each in one tile, `-Native` runs at 237.7 fps at 1x (RTV 218.3) and 198.3 at 2x (RTV 165.6), with 4.89 to 5.00 ms of GPU time a frame at 2x against RTV's 5.80 to 6.15. With the switches off, RTV's command processor is 1.1% busier than before step 2 at 1,800 draws (3.647 against 3.608 ms, a bisect), at the edge of the noise, and the main session accepted it. The step gate on `n4be14` (f766d0f's code) ran with e2e's `-Native` default, one pass with 2x MSAA, against `-Native` without N4's switches (N1's `n4g1xpn` and `n4g2xpn`): 238.2 fps at 1x with 2.25 ms of GPU time and 1.61 µs of render thread time per draw, against 215.1 to 236.0 fps, 2.17 to 2.32 ms and 1.85 to 1.94 µs; 181.5 fps at 2x with 5.36 ms and 1.74 µs, against 177.8 to 181.9 fps, 5.36 to 5.50 ms and 2.04 to 2.09 µs. The combined checks stay exact, and the 2x captures at 1:1 beside RTV's show the whole scene with the same detail. An earlier build, `n4be13`, had linked another checkout's title objects and rendered the scene broken under `-Native`; its numbers are void (17.15). `guest_ring_wait_sleep` (on in the launcher for RTV and `-Native`, off for `-Rov` and by default) lets the title's ring index wait sleep until the command processor publishes the read pointer again instead of polling it on a whole core: in the 1x race the process uses 0.22 to 0.29 cores less (6 to 8%) and the CPU package 2.4 to 3.3 W less on `-Native`, and 0.31 to 0.39 cores (9 to 11%) and 2.6 to 2.8 W less on RTV, with the frame rate, p99, the render or command processor thread's time per draw and the GPU time unchanged and the 2x image the same at 1:1 ([17.16](../analysis/d3d-hle-n4.md#1716-event-waits-in-place-of-the-remaining-spins-23-october-2026)). e2e records the process's CPU time, the package's power and each thread's CPU time per phase (`cpu_power`), and the perf trace the process's and its threads' CPU time per window (`process_cpu`). The last items of stage N4 ([17.17 to 17.19](../analysis/d3d-hle-n4.md#1719-a-smaller-cpu-and-the-timer-resolution-23-october-2026)):
- **Not needed.** The vblank interrupt and the readback copies stay where they are: no readback copy runs in the race or the replay.
- **The queue.** A record waits 0.8 ms on average and 3 to 3.75 ms at p99 in the host render queue, under a frame, so nothing is tuned.
- **Fewer cores.** Four performance cores hold `-Native` at the refresh rate (p99 5.0 to 5.4 ms at 1,600 to 2,000 draws a frame) with the ring index wait's sleep, and 6.5 to 6.9 ms without it. Masks of efficient cores are not a valid proxy: Windows parks cores inside them.
- **The timer.** The runtime asks for 1 ms timer resolution and opts out of power throttling (`high_resolution_timing`, every path, no measurable cost).
- **The GUI launcher** runs the native path, writes the perf trace and applies extra cvars, for the [Ally X run set](../running.md#ally-x-run-set).
- **Host presents** ([17.22](../analysis/d3d-hle-n4.md#1722-host-presents-24-october-2026)): the menus present once a guest frame instead of twice (the always-created achievement toast kept the ImGui drawer in the presenter, which repainted at every UI tick), for 133 ms/s less process CPU, 1.7 W less package power and about 2 W less GPU power; a present on the submit thread no longer drops its frame; a frame limiter at 30 gives 29.8 guest frames and 29.8 presents. On the main PC the host presents at most about 120 times a second at 240 Hz (the hybrid laptop's display path), so e2e keeps the earlier skip (`present_skip_superseded=true`) for comparable guest throughput, and shows the settings overlay only with `--info`.
- **The native path with integrated GPUs only** ([17.25](../analysis/d3d-hle-n4.md#1725-the-second-rog-xbox-ally-x-run-24-october-2026), the main session's decision): the launchers run `-Native` by default when every GPU is integrated (the rule of the 1x render scale), unless `-Rov` or `-Msaa`; `-Rtv` keeps RTV. RTV stays the default elsewhere.
- **The second ROG Xbox Ally X run** ([17.25](../analysis/d3d-hle-n4.md#1725-the-second-rog-xbox-ally-x-run-24-october-2026), export of `96da8da`, 35 W and 17 W): VSync Auto found a VRR range (races at 117), every guest frame is presented once (the Ally X does not cap the presents), and against the first run `-Native` gains 5 to 9% in fps with p99 down by up to a fifth (35 W: 118.2 fps with VSync off, p99 11.38 ms; 17 W: 110.5 fps with Auto, p99 15.30 ms). The passes the runtime adds take 29 to 30% of the 890M's frame (GTAO 0.7 to 0.9 ms, the windshield 0.6 ms). RTV's readbacks waited under VSync Auto (56.5 ms/s), so a frame's present now runs on a thread of its own (`GPU Present`), with one frame in flight.
- **Integrated GPUs** ([17.21](../analysis/d3d-hle-n4.md#1721-the-rog-xbox-ally-x-runs-24-october-2026), the main session's decision): with integrated GPUs only (a UMA adapter, or under 1 GB of dedicated video memory), the launchers' automatic render scale is 1x on every screen, so the ROG Xbox Ally X no longer starts at 2x; the description says "for an integrated GPU", and `-ResolutionScale` or `-Supersample` still choose another.
- **GPU time per pass** ([17.24](../analysis/d3d-hle-n4.md#1724-gpu-time-per-pass-24-october-2026)): the perf trace splits the frame's GPU time into GTAO, TAA, motion vectors, the windshield, the swap and the rest (the scene with the look mods), and times the presenter's paint. At 1x on the main PC the added passes take 0.40 of 2.08 ms a race frame, at 2x 1.48 of 5.05 ms, without a measurable cost.
- **VSync** ([17.23](../analysis/d3d-hle-n4.md#1723-vsync-24-october-2026)): the launchers' `-VSync auto|on|off` (default auto: on a screen with a VRR range, tearing presents at most one a refresh and races 3 fps under the refresh; else on). On the main PC every mode presents 120 a second at 240 Hz (its display cap); auto lowers the race p99 from 13.2 to 9.8 ms there, and at 120 fps the modes do not differ. e2e keeps off.
- **The ROG Xbox Ally X** (export of `de4e9bd`, Quick Races at 120 fps, one run a side, [17.21](../analysis/d3d-hle-n4.md#1721-the-rog-xbox-ally-x-runs-24-october-2026)) is GPU bound in every mode. At 1x the native path races at 112.5 fps with p99 14.34 ms on AC power (35 W) and at 93.3 fps with p99 19.52 ms on battery (17 W), against RTV's 106.5 and 91.6 fps with p99 18.22 and 24.45 ms. At the launcher's 2x for the 1080p panel the GPU takes 23.9 to 27.2 ms a frame (39.1 and 34.6 fps), so the device target of 60 fps at the panel's resolution is not met; at 1x it holds on AC power, and on battery the frame rate holds but p99 does not. The ring wait's sleep gives 2 to 3% more fps and a quarter of a core less; the timer switch changes nothing measurable; one pass with 4x MSAA costs 8%. The render thread is busy 4.36 ms (35 W) and 6.11 ms (17 W) a race frame, above section 10's 2.5 ms, but waits for the GPU. No new error key. Proposed Ally X default: render scale 1x with the native path and the other defaults.
- **The identity check's late write** (two sightings in N3's careers, a tiling bracket draw's closing dword) did not come back in 266,281,085 ranges; a late write trace now runs with `FM4_NATIVE_CHECK` and names the function that rewrote the dword if it does ([17.20](../analysis/d3d-hle-n4.md#1720-the-identity-checks-late-write-23-october-2026)). `d3d12_draw_back_end=1` captures without moving anything (increment 1: 120 bytes and 0.24 ms a race frame on each thread; the check passed over 34,778,359 draws and a 1x smoke with `-Look "original,taa=0"` passed on RTV and `-Rov`). The earlier RTV comparison with the cvar off at increment 3 is in [history](history.md#23-october-2026-rtv-with-n4s-switches-off-at-increment-3).

### E2E fast route, 22 October 2026

`e2e.py --fast-route` (and `matrix.py --fast-route`) reaches the race of `quickrace`, `race`, `photo`, `freeze` and `career` through the title's screen events (`FM4_TEST_EVENTS_FILE`, the UI scenes, render scenarios and countdown of [`fm4-test-events.cpp`](../../work/fm4-rex/src/fm4-test-events.cpp)) instead of fixed delays; the menu route stays the default ([fast route](../e2e/README.md#fast-route)). On `I:\FM4-build-usa` (`game-build-route`, sha256 `e4f78753...`) at 1x and 240 Hz with `FM4_FIXED_SEED=1`:

| Check | Menu route | Fast route |
| --- | --- | --- |
| `photo`, five runs | 249.9 s to 252.5 s (runs of 22 October 2026) | 191.6 s to 193.9 s, 5 of 5 reached their goal |
| GO RACE, race start, race phase start (medians) | 79.5 s, 138.0 s, 147.2 s | 49.7 s, 81.1 s, 90.4 s |
| Race phase, draws per frame | 1,902 to 1,956 (three `race` runs) | 1,914 to 1,968 |
| Race phase, fps (p99) | 211.7 to 219.6 (6.3 ms to 7.1 ms) | 212.8 to 220.7 (6.0 ms to 6.8 ms) |
| `freeze` at 60 Hz, one run each | 207.6 s | 150.5 s; `parity.py` against the menu route passes (draws -0.6%, 32.8% of the image above 4) |
| `career` without the seed, three runs | 206.5 s to 215.1 s (runs of 21 to 23 October 2026) | 203.3 s to 209.9 s, 3 of 3 reached their goal |

The error keys are the same three in both routes. The boot to the main menu (about 24 s) and the loading (17 s) do not change. Details in [direct race launch](../analysis/direct-race-launch.md#the-fast-route-22-october-2026).

### Deferred constant buffers, stage N4, 22 October 2026

Step 1 of stage N4 of the [native port](../analysis/d3d-hle-n4.md#175-step-1-results-22-october-2026): with `d3d12_deferred_constants` (off by default, not set by `-Native`) the float, bool and loop, and fetch constant buffers of the draws are filled by the thread that replays the deferred command list (the submit thread), from the constant registers captured at the draw, instead of on the render thread. The check (`d3d12_deferred_constants_check`) compared 97,058,424 buffers with the inline path's bytes in a `career` run (race, replay, menus) with every 1,000th comparison perturbed: 36,145 mismatches, exactly the perturbed ones.

| `race` at 240 Hz and 1x with `-Native`'s cvars, at 1,800 draws a race frame | Off (3 runs) | On (2 runs) |
|---|---|---|
| Render thread busy | 3.307 ms | 3.207 ms |
| Submit thread recording the lists | 0.722 ms | 1.074 ms |

The render thread saves 0.06 to 0.10 ms a race frame (2 to 3%), the submit thread pays 0.35 to 0.38 ms, and p99 follows the draw load of the AI races (4.99 to 5.83 ms) without a difference. The section 10 target of 2.5 ms needs step 2, the draw back end on the replay side ([design](../analysis/d3d-hle-n4.md#176-step-2-design-the-draw-back-end-on-the-replay-side)).

### One pass with 2x MSAA, stage N3, 23 October 2026

Increment 4 of stage N3 of the [native port](../analysis/d3d-hle-n3.md#1617-increment-4-step-0-the-one-pass-probe-23-october-2026): `-Native` renders races in one pass with 2x MSAA instead of the title's tiles. The title's 2x scenario becomes one 1280x720 tile (`FM4_ONE_PASS_MSAA=2`), the scene depth gets a private host render target (`native_one_pass_msaa=2`), and the host render targets are sized to the tiles the title uses (`native_rt_surfaces=10`). `-MsaaSamples 0` keeps one tile without MSAA. RTV and `-Rov` keep the title's tiles, and `-Rov` refuses the option.

| Close gate of stage N3, 240 Hz, 24 October 2026 | RTV's defaults (one tile) | Native one pass 2x | Native one pass 4x |
|---|---|---|---|
| perf 1x, race (`n3z-rtv1xp`, `n3z-nat1xp`, `n3z-nat4x1xp`) | 225.4 fps, GPU 2.79 ms a frame | 239.3 fps (the 240 Hz cap), GPU 2.23 ms | 236.4 fps, GPU 2.49 ms |
| perf 2x, race (`n3z-rtv2xp`, `n3z-nat2xp`) | 167.0 fps, GPU 5.81 ms | 190.1 fps, GPU 5.14 ms | |
| smoke 1x and 2x, races of `photo` and `career` | 225.9 and 223.6 fps; 172.5 and 168.2 fps | 239.4 and 238.0; 190.2 and 192.9 fps | 236.1 and 238.1 fps (1x) |
| `parity.py` on `freeze`, two runs at 2x (the run named 1x also rendered at 2x) | `-Rov` against RTV passes: 30.5% and 13.0% of the image above 4 | FAIL by design on draws (-14.4%) and predicated packets (-26%); 23.9% and 24.6% of the image above 4 against RTV | |
| combined checks with perturbation, `career` | | identity, bindings and guard: mismatches only the perturbed; 12,317 resolve byte checks, 0 mismatches | the same; 11,733 resolve byte checks, 0 mismatches |

Every cell reached its goal with no new error key, `-Rov`'s 1x smoke included; the frozen frames, checks and inventories ran on `13393dc`, the smokes and perf on local main `de4e9bd` with the same runtime and title source ([d3d-hle-n3.md 16.18](../analysis/d3d-hle-n3.md#1618-the-close-gate-of-stage-n3-24-october-2026)). The 1:1 crops of the 2x frozen frames (the old tile seam, the top 416 lines) show no artefact. The earlier close gate of the 2x increment is in [history](history.md#23-october-2026-the-close-gate-of-one-pass-2x).

The race in one pass has 20.5 ownership transfers and 2,800 tiles a frame (0.35 ms of GPU time), against 19.5 and 2,384 in one tile. The one addition is a copy from the shadow map into the bottom 208 lines of the scene colour, where the two alias in EDRAM ([d3d-hle-n3.md 16.17](../analysis/d3d-hle-n3.md#1617-increment-4-step-0-the-one-pass-probe-23-october-2026)).

One pass with 4x MSAA is `-Native -MsaaSamples 4` (`e2e.py --native --msaa-samples 4`, the GUI launcher's "Native MSAA" choice): the title's scenario 3 becomes one 4x tile, and `native_one_pass_msaa=4` gives the scene's colour and depth private host targets, since together they need 5,760 tiles. At 1x it costs about 0.1 to 0.3 ms of GPU time a race frame more than one pass 2x, and 2x at 2x costs about 2.2 times the GPU time of 4x at 1x; the main session chose 4x at 1x as the candidate for the ROG Xbox Ally X, to be confirmed on the device ([d3d-hle-n3.md 16.17](../analysis/d3d-hle-n3.md#1617-increment-4-step-0-the-one-pass-probe-23-october-2026)). Render target memory is 231.5 MB against 577.8 MB for RTV with the title's 4x tiles. A race frame has the 19.5 transfers and 2,384 tiles of one tile. Resolves from the private 4x surfaces have no byte check. Open item:

- The identity check counted one unperturbed range twice during the 4x work, in the menus or the load: writer `82311080` rewrote a draw range's last dword after it returned. It did not come back in the close gate's two careers (28.6 million ranges); the late write trace of `FM4_NATIVE_CHECK` names the writer if it does.

The shared fast copy of 4x MSAA surfaces, on every path including `-Rov`, read the vertical sample (guest sample 1) instead of sample 0 in pixel columns 20 to 23 of every 40-pixel tile at 1x. A copy group of 8 pixels straddled the depth tile's half swap, which the precompiled shader applied once per group. FM4-PC's own fast copy computes every pixel's address. With the project owner's approval of 23 October 2026, this changes `-Rov`'s output, in photo mode's 4x depth copies at 1x. Proof: RTV's byte check against the host resolve shows 158,835 checks without a mismatch, `-Rov`'s band test shows 0 rows (2,242 before), and the frozen-frame parity of `-Rov` against RTV passes ([d3d-hle-n3.md 16.17](../analysis/d3d-hle-n3.md)). Under `-Native`, these copies come from the host render target. A `-Native` photo run checked 190,899 resolves without a mismatch.


### Resolves from the host render target, stage N3, 22 October 2026

Increment 3 of stage N3 of the [native port](../analysis/d3d-hle-n3.md#1611-increment-3-step-3-resolves-from-the-host-render-target-22-october-2026): with `native_rt_surfaces=2` (set by `-Native`, off by default otherwise, never on `-Rov`) a 32bpp resolve whose source tiles one host render target owns reads that render target with `resolve_host_32bpp` instead of its dump into the EDRAM buffer. A byte check (`native_rt_surfaces_check`) compared 65,099 resolves with the dump and copy at 1x and 2x without a mismatch. The unscaled fast copies of multisampled sources (photo mode's 4x MSAA depth at 1x) keep the dump: they differ in the byte check, and which side is wrong is open.

| Gate, 240 Hz, commit `d9a0c9f` | RTV | `-Native`'s cvars |
|---|---|---|
| perf 2x (`n3c2xpr`, `n3c2xpn`), race | 158.4 fps, p99 9.29 ms, GPU 6.13 ms a frame | 190.1 fps, p99 7.68 ms, GPU 5.16 ms |
| smoke 2x (`n3c2x`, `n3c2xn`), races | `photo` 166.0, `career` 157.6 fps | 188.4, 183.3 fps |
| smoke 1x (`n3c1x`, `n3c1xn`), races | `photo` 233.0, `career` 218.9 fps | 234.8, 224.1 fps |
| `parity.py` on `freeze`, two runs at 2x (the run named 1x also rendered at 2x) | | pass, 22.3% and 31.1% of the image above 4 |
| identity check with perturbation | | 13,525 ranges perturbed, 13,525 reported |

The race's resolves take 0.47 ms of GPU time a frame at 2x against 0.68 ms on RTV; 26.1 of 29.9 resolves a frame come from the host render target. One of the 14 RTV runs crashed while the race loaded, on RTV's defaults; it did not recur. The fault is in guest code (`sub_82956DA8` stores through a null buffer pointer of a game object, so it writes to guest address 0), not in the GPU path ([d3d-hle-n3.md 16.11](../analysis/d3d-hle-n3.md)).

### Readback on demand, stage N3, 22 October 2026

Increment 2 of stage N3 of the [native port](../analysis/d3d-hle-n3.md#165-increment-2-readback-on-demand-22-october-2026): with `readback_resolve_on_demand` (set by `-Native`, off by default otherwise, never on `-Rov`) resolves skip their readback to guest memory and trap their pages; the first access by the CPU waits for the readback of the latest resolve. A read trace measured first that no thread reads any of the 24 readbacks (13.4 MB) of a race frame at 2x (`readback_read_trace`, [16.4](../analysis/d3d-hle-n3.md#164-who-reads-the-readbacks-22-october-2026)). The gate against RTV of the same binary on `I:\FM4-build-usa` at 240 Hz passed with no new error key:

| Gate | RTV | Readback on demand |
|---|---|---|
| perf 2x (`n3d2xp`, `n3d2xpd`), race | 155.5 fps, p99 9.50 ms, GPU 6.25 ms a frame | 176.7 fps, p99 8.31 ms, GPU 5.51 ms |
| perf 1x (`n3d1xp`, `n3d1xpd`), race | 216.2 fps, p99 6.44 ms, GPU 3.01 ms | 220.8 fps, p99 5.62 ms, GPU 2.37 ms |
| smoke 2x (`n3d2x`, `n3d2xd`), races | `photo` 160.5, `career` 154.9 fps | 176.5, 174.5 fps |
| smoke 1x (`n3d1x`, `n3d1xd`), races | `photo` 213.1, `career` 225.0 fps | 210.9, 229.6 fps |
| `parity.py` on `freeze`, two runs at 2x (the run named 1x also rendered at 2x) | | pass, 19.9% and 17.4% of the image above 4 |
| smoke 1x with `-Native`'s cvars (`n3d1xn`) | | pass; races 237.5 and 224.9 fps |

The races, replays and frozen frames read nothing back on demand (no stall); photo mode reads the photo save's two destinations (at most 8.9 ms), the menus and loading 3,156 destinations in all runs (at most 68 ms each, no timeout). The saved photos are intact at 1x and 2x, checked by eye on their thumbnails and in `photo_view` (the races differ, so the files cannot be compared with RTV's). Details in [16.6](../analysis/d3d-hle-n3.md#166-increment-2-results-22-october-2026).

### Decoded replays and binding reuse, stage N2, 22 October 2026

Stage N2 of the [native port](../analysis/d3d-hle.md#15-stage-n2-design-22-october-2026), closed: the replays of recorded command buffers are measured (72% of the render thread's time in the race, most of it in the draw path), the draw path is profiled, and the two reuses built behind switches, off by default, save nothing measurable on this backend: op lists for the replays (`host_render_oplists`, 2.11 against 1.92 us per draw) and the texture bindings of fetch constants rewritten with their values (`d3d12_reuse_draw_bindings`, 1.830 against 1.828 us per draw). Their checks hold: no stale op list with the exact watch range, and a binding check of 60.9 million bindings with exactly the perturbed mismatches. The closing gate (smoke `n2zrtv` and `n2znat`, `parity.py` on the one-tile `freeze`) passed with the error keys of RTV. The CPU targets of section 10 move to N4. Details in [15.7 to 15.12](../analysis/d3d-hle.md#1512-step-2-results-and-the-end-of-n2-22-october-2026).

### Dirty state writers as records, stage N1, 22 October 2026

The third increment of stage N1 of the [native port](../analysis/d3d-hle.md#149-increment-3-design-the-dirty-writers-as-records): the title's three dirty state writers leave a marker and a record of their register writes instead of their PM4, and the host render thread applies the record at the marker (`host_render_native`, on with `-Native`).

- **Runs** on `I:\FM4-build-usa`, 240 Hz, 1x (commit `f4241ae`, one binary for RTV and the native path): smoke `n3brtv` and `n3bnat`, perf `n3brtvp` and `n3bnatp`, the perturbed identity check `n3bprt-career`, the packet counts `n3bpm4-off` and `n3bpm4-on`, and the one-tile `freeze` pair `n3bfz-rtv` and `n3bfz-nat`. Every run reached its goal with the error keys of RTV.
- **Results**: 33.3 million ranges compared with the records, no mismatch beyond the 31,617 perturbed ones and no marker without a record; smoke PASS or WARN on capture statistics; perf WARN on capture statistics, with the race at 214.2 against 233.7 fps and p99 5.93 against 5.14 ms (medians of three runs, inside the `compare.py` limits) and 9.9% more render thread time per draw; `parity.py` PASS (image 17.14%). The records remove only 3.8% of the race's type-0 packets: most of its register writes come from replayed command buffers. Details in [14.10](../analysis/d3d-hle.md#1410-increment-3-results-22-october-2026).
- Not covered: 60 and 120 Hz, 2x, photo mode, split screen and the intro videos with the records.

### Identity check, stage N1, 22 October 2026

The second increment of stage N1 of the [native port](../analysis/d3d-hle.md#147-increment-2-design-coverage-by-phase-and-the-identity-check): the coverage of the PM4 writers by phase and the identity check, both off by default (`FM4_N0_TRACE`, `FM4_NATIVE_CHECK=1` with `host_render_check`, [traces](../e2e/traces.md#identity-check-of-the-native-path-fm4_native_check-host_render_check)).

- **Runs** on `I:\FM4-build-usa`, 240 Hz, 1x, the host render thread (commit `1bf8233`): `career` with the check (`n2chk5-career`) and with it perturbed (`n2prt-career`), and the perf gates `n2natp` (check off) and `n2chkp` (check on). Every run reached its goal, with no new error key except the perturbed run's own reports.
- **Results**: 16,863,547 ranges of the menus, the one-tile race and the replay compared without a mismatch; the perturbed run reported exactly its 16,095 perturbed ranges. With the check off the race ran at 227.2 fps (p99 5.57 ms) against 219.6 fps for increment 1 (PASS); with it on at 94.1 fps, about 3 us more per draw. The writers of each scene are measured ([14.4](../analysis/d3d-hle.md#144-writers-by-scene)): the menus run the tiling bracket, the one-tile race and the replay do not. Details in [14.8](../analysis/d3d-hle.md#148-increment-2-results-21-and-22-october-2026).
- Not covered: 60 and 120 Hz, 2x, photo mode, split screen and the intro videos with the check.

### Host render thread, stage N1, 21 October 2026

The first increment of stage N1 of the [native port](../analysis/d3d-hle.md#14-stage-n1-design-21-october-2026): `host_render_thread` (`-Native`, one tile only) replaces the command processor thread with a host render thread that executes the guest's kicks from a bounded record queue, with every PM4 writer still on the fallback.

- **Runs** on `I:\FM4-build-usa`, one binary for both paths (commit `e720054`): smoke `n1rtv2` and `n1nat2` and perf `n1rtvp2` and `n1natp2` at 240 Hz and 1x, and the one-tile `freeze` pair `n1fz2-rtv` and `n1fz2-nat` at 60 Hz with the scenario's pins (the 2x draw scale, so the frozen frames are captured at 2560x1440, as in the tiled `freeze` runs). Every cell reached its goal, with the same error keys on both paths.
- **Results**: smoke PASS, or WARN on capture statistics only (`career` race 225.2 against 224.1 fps, replay 117.1 against 118.1 fps); perf WARN on capture statistics only, with the race at 229.4 against 219.6 fps and p99 5.48 against 6.08 ms (medians of three runs, inside the `compare.py` limits), 14% more draws per frame in the native runs and 2.6% more busy time per draw; `parity.py` PASS (image 23.03%, draws -0.6%). The table is in [section 14.6](../analysis/d3d-hle.md#146-increment-1-results-21-october-2026).
- Not covered: 60 and 120 Hz for the smoke and perf gates, a 2x swap, the ROG Xbox Ally X, split screen, the intro videos and the identity check (increment 2).

### D3D layer counters and stage N0, 21 October 2026

The measurement part of stage N0 of the [native port](../analysis/d3d-hle.md#9-stages); the numbers and their reading are in [section 13](../analysis/d3d-hle.md#13-stage-n0-measurements-21-october-2026) of that analysis.

- **What changed**: every hook of `fm4-d3d-trace.cpp` and `fm4-jpeg-diagnostics.cpp` moved into one registry, `fm4-guest-hooks.cpp`, with one definition per guest address, and the counters were added, all off by default: `FM4_N0_TRACE` and `FM4_N0_KEYCHECK` in the title, `REX_PM4_STATS` and `REX_CP_SPLIT` in the runtime ([traces](../e2e/traces.md#d3d-layer-counters-fm4_n0_trace)).
- **Runs** on `I:\FM4-build-usa`, 240 Hz: `career` with the counters at 1x with one tile and with `-Msaa` (menu, race and replay, all goals reached), with `FM4_N0_KEYCHECK` and `REX_LATE_WRITE_TRACE`, and the `photo` race three times each without counters (`n0-p1x`), with `REX_CP_SPLIT` (`n0-p1x-split`) and with it at 2x (`n0-p2x-split`). All runs reached their goals; one earlier tiled run crashed in the counters' summary writer, which read freed guest memory, and was repeated after the fix.
- **The moved diagnostics**: a `photo` run with `FM4_D3D_TRACE` and `FM4_PHOTO_ENCODER_TRACE` (`n0-registry`) reached its goal with the usual three error keys, and wrote the call counts of the PM4 writers (50 of the 71 were called) and the photo encoder's snapshots as before the move.
- **Without counters** (`n0-p1x`, the launcher defaults): three `photo` runs on the N0 build; they are no longer the current 240 Hz baseline, which is the RTV perf gate of the [host render thread](#host-render-thread-stage-n1-21-october-2026) ([history](history.md#21-october-2026-the-n0-baseline-at-240-hz)).
- **Main findings**: every replay writes its `INDIRECT_BUFFER` inline into the segment (`sub_826E4A28` is not used); about half of the race replays are byte copies of recordings and the rest come from buffers no `EndCommandBuffer` of the run produced, each replayed about twice a frame; `IM_LOAD` keys of the ring are stable from writing to execution, those of recordings change in 1.63% (race) to 16.6% (replay) of executions; no late write to command buffers, constants or shaders; the render thread issues every replay and every draw that reaches the frame directly, a second thread records at load, the tile stream worker has its own thread.
- Not covered: 60 and 120 Hz, the frozen-frame harness (another N0 part), split screen, photo mode's own frames, the video, Kinect and facade paths, and the overhead of the hooks with the counters off (no A/B against the previous binary).

### TAA's phases on a still camera, 20 October 2026

At the project owner's request: the shimmer that the still camera's accumulation left, mostly in the cockpit. Details in [TAA, step 12](../analysis/taa.md#72-per-pixel-taa_resolve) and [the phases of a still camera](../analysis/taa.md#the-phases-of-a-still-camera-20-october-2026).

- **What changed**: on a still camera TAA keeps, per pixel, the luma of each of the 8 jitter phases and the frames since the pixel last changed (the history textures `taa_phases` and `taa_unchanged`: 18 bytes per pixel with the previous frame's copies, 66 MB at 2x and 17 MB at 1x). The jitter repeats every 8 frames, so a pixel compared with the same phase a cycle ago saw the same samples. A pixel within `TAA_STILL_CHANGE` (2 of 255) for a whole cycle keeps its history unclipped, its luma held within that of its phases and of the 5x5; a pixel that changed in the last cycle blends as on a moving camera. The runtime gives the passes the jitter's phase and the still frames in a row (`xe_scene_jitter_phase`, `xe_scene_jitter_phases`, `xe_scene_still_frames`; the scene constants grew to 2,320 bytes). `TAA_STILL_PHASES=0` is the earlier behavior, and debug view 7 shows the test.
- **The cause**: TAA's clip itself. The clip moves the history along all of YCoCg at once, so a color that one phase's 5x5 lacks (the red seams of the gloves, the dashboard's trim, the hood's lit edge) and a detail thinner than a pixel were cut back in that phase and returned in the others. Widening only the luma of the box changed nothing (0.05%); leaving those pixels unclipped took the freeze to 0.00%. The earlier reading, features thinner than a pixel and the HUD prompt, is corrected in [history](history.md#6-corrections-and-qualifications-of-earlier-entries).
- **Offline**: `fm4_post_pass_shader_tests` passes with two new checks. A white and a red line a quarter of a pixel wide on a still camera change by 0.7 of 255 between frames, against 3.2 without the phases, and keep their light (0.177 per pixel along them, 0.003 without). Stripes moving 0.7 pixels per frame on a still camera, without motion of their own as a flag has none, stay within 0.064 of the unjittered stripes, against 0.233 smeared without the phases. `fm4_post_pass_camera_tests` and the build tests pass.
- **In the game** (`I:\FM4-build-pass`, USA Disc 1, 2x, `--shader-dev`, Bernese Alps with the AI driver, the defaults with the cockpit glass, `TAA_STILL_PHASES` switched live, consecutive captures 2.1 s apart; runs `ck-ph3`, `st-ph` and `st-ph2`): the share of pixels that change by more than 4 of 255.

  | Rewind freeze | Without the phases | With them |
  |---|---|---|
  | Cockpit camera | 0.03 to 0.04%, at most 125 to 165 of 255 | 0.00%, at most 3 |
  | Cockpit camera, another moment | 0.03 to 0.05%, at most 101 to 169 | 0.00%, at most 3 to 4 |
  | Hood camera | 0.31 to 0.57%, at most 103 to 159 (the asphalt's grain) | 0.00%, at most 3 |

  In 1:1 crops the dashboard's thin trim lines and the hood's lit edge are whole with the phases and broken into dashes and specks without them; the asphalt keeps the same grain without the specks, and nothing is softer. After a switch the picture settles in about 6 s. Debug view 7 marks 84 to 90% of the frame unchanged in the last cycle and 0.013% changed (an accent line of the HUD bar); the rest is the HUD and the cockpit glass, drawn after TAA.
- **Smoke gate** `stph` at 240 Hz and 1x on `I:\FM4-build-pass` against `stws` (the cockpit glass's accumulation): 3 of 3 cells, `career` PASS, `photo` and `photo_view` WARN on capture statistics only, the same three error keys, no clear-gray frames; races at 217.5 and 220.7 fps (225.9 and 217.9 before), GPU 2.89 and 2.95 ms per frame (2.84 and 2.92); the photo loads in color.
- Not covered: something that moves in a freeze (no waving flag was in view; the blend of a changed pixel is checked on WARP only), the GPU time of the phases (on a still camera only), the pause menu, photo mode and replays with the phases, 60 and 120 Hz, and the scene_color point, which has no phases.

### GUI launcher and export, 20 October 2026

`build` builds `fm4_launcher.exe` and `export` copies it with the game into a folder of its own ([GUI launcher](../running.md#gui-launcher), [export](../running.md#export-to-another-computer)). Checked on the development computer with `I:\FM4-build-usa` (USA discs):

- `Build.ps1 -Action build` built the launcher and passed the launcher tests: the launch plans of ten sets of options on four monitor sizes (40 plans; cvars, environment including the settings overlay text, and the display description) are identical to `pipeline.py`'s, and so are the Disc 1 release, the Disc 2 check and the four Disc 2 packages. Three deliberate differences in the test cases were each reported.
- `Build.ps1 -Action export -Out I:\FM4-export-test` wrote a 186 MB folder without `user-data`.
- `fm4_launcher.exe --play` from that folder, with the default profile, imported the four Disc 2 packages into the folder's own `user-data`, loaded 884 shaders from the exported shader cache and ran the game for about two minutes; closing the game window ended it with exit code 0, and the launcher showed again without an error banner. `runtime.log` had no errors other than the `update:\media.zip` line that every run has.
- The pages were checked in screenshots at 2560x1600 with a 1.5 DPI scale: the Disc checks, the import state of the packages, the look sliders (a changed knob is marked and the `-Look` value follows) and the key bindings.

Not checked yet: a run on a second computer, a race started from the launcher, downloadable content beyond Disc 2 through the launcher, a non-zero exit banner from a real crash, and other DPI scales.

### One tile with TAA, the presets and TAA's history, 20 October 2026

At the project owner's request: every look preset has TAA, and races render one tile without MSAA with it at every quality; `-Rov` keeps the tiles and 4x MSAA. Details in [TAA](../analysis/taa.md#current-state) and [running](../running.md#-singletile-and--msaa).

- **Presets** (`pipeline.py`): `aces` (the default) has the mods of the earlier `pc`, which is now its alias; `gt` and `neutral` have the same mods with their tone curve; `original` is the game's look with TAA only (no shader mod; `original,taa=0` is the game's picture, the reference against `-Rov` in AGENTS.md).
- **Tiling**: one tile whenever the look has TAA, on the RTV path, at every `-Quality` (`auto_single_tile`); `-Msaa` keeps the tiling of `-Quality`, and so do `-Rov` and a look without TAA.
- **A still camera** (`[jitter] still`, `TAA_STILL` 1): once the camera has been still for 8 frames it is jittered again at any frame rate, and TAA's still pixels accumulate the phases (the mean of up to `TAA_STILL_FRAMES` 256 frames, the 5x5's minimum and maximum as the clip box, also on reactive pixels and pixels without known motion, no disocclusion test). Draws after the frame's final combine are never jittered: the pause menu tests the scene's depth buffer and shook with it. The post pass params now hold 40 values (28 before; the manifest has 31).
- **TAA's history**: from step 7 (19 October 2026, 20:25) until this change neither point had one in the game. The framework required two history textures that only the scene_color point writes; it now counts only those an enabled pass reads ([TAA, the history defect](../analysis/taa.md#the-history-defect-19-to-20-october-2026)). Found with debug view 4 (the whole frame without a valid history in a rewind freeze) and a new log of the history textures in the `post_pass_log_every` line.
- **Offline**: `test_pipeline.py` passes (17 tests); `fm4_post_pass_camera_tests` (the still gate, no jitter after the final combine) and `fm4_post_pass_shader_tests` pass. On WARP a still edge under the jitter changes by 0.2 of 255 between frames after 300 accumulated frames, against 21 with the usual blend.
- **Still pictures in the game** (BuildRoot `I:\FM4-build-pass`, USA Disc 1, 2x, `--shader-dev`, Quick Race on the Bernese Alps with the AI driver, consecutive captures 2.1 s apart; runs `st-still` to `st-still4`): the share of pixels that change by more than 4 of 255.

  | Scene | Before the history fix | Final |
  |---|---|---|
  | Rewind freeze, cockpit camera | 12 to 16% | 0.11 to 0.18%, 0.06% with the cockpit glass accumulated, 0.00% with TAA's phases; with `TAA_STILL=0` (no jitter) 0.01% |
  | Rewind freeze, chase camera | 25 to 31% | 0.01 to 0.04% once settled |
  | Pause menu | 1.9 to 2.5% | 0.00% (at most 1 or 2 of 255) |

  The cockpit glass, drawn after TAA from the jittered depth, accumulates the light it adds on a still camera since a later change the same day ([windshield](../analysis/windshield.md#results)): the cockpit freeze then changes in 0.06% of the pixels with the glass, the same as without it, against 0.09 to 0.17% before. What still changed then was TAA's clip of thin details and colored edges, which [TAA's phases](#taas-phases-on-a-still-camera-20-october-2026) removed later the same day. In 1:1 crops the accumulated still frame has smooth edges where the frame without the jitter shows the steps of one tile without MSAA. A flag that moved in the freeze, which has no motion vectors, was averaged into a smear; with the phases a changed pixel blends as in motion (checked on WARP). `TAA_STILL_FRAMES` 64 and 1,024 changed little against 256.
- **Smoke gates** at 240 Hz and 1x on `I:\FM4-build-pass` (Japan/Asia, launcher defaults): `stnew` (one tile) against `stmsaa` (`-Msaa`), the same binary without the asynchronous swap, then `stmerge`, the branch on top of the asynchronous swap, against `stnew`. 3 of 3 cells in each, the same three error keys, no clear-gray frames; the photo saves and loads in color.

  | Cell | `stmsaa` (tiles) | `stnew` (one tile) | `stmerge` (one tile, asynchronous swap) |
  |---|---|---|---|
  | `photo` race | 218.1 fps, p99 6.18 ms, GPU 3.17 ms, 1,963 draws per frame, command processor 818 ms/s | 236.9 fps, 5.16 ms, 2.69 ms, 1,589, 689 ms/s | 230.7 fps, 5.78 ms, 2.85 ms, 1,893, 774 ms/s |
  | `career` race | 200.5 fps, 7.53 ms, 3.33 ms, 2,308, 948 ms/s | 232.0 fps, 5.91 ms, 2.76 ms, 1,712, 790 ms/s | 233.2 fps, 5.59 ms, 2.87 ms, 1,711, 745 ms/s |
  | `career` replay | 104.9 fps, GPU 4.19 ms | 117.7 fps, 3.64 ms | 115.9 fps |
  | `compare` | - | WARN: capture statistics; `wait_reg_mem` 376 to 458 ms/s (the command processor waits for the vblank) | `career` PASS; `photo` and `photo_view` WARN on capture statistics |

  One tile takes 60 to 68% fewer predicated packets per frame (4,519 to 1,792 and 6,262 to 1,984). With it the races come close to the 240 Hz limit, so the asynchronous swap shows no further gain there.
- Not covered: TAA in motion with its history at 2x (thin wires and fences without MSAA), the countdown, a replay with a still camera, the photo mode live view, 60 and 120 Hz, the scene_color point with the fixed history.

### Asynchronous swap, 20 October 2026

`d3d12_submit_thread_async_swap` (runtime default `false`; `true` in the launcher since 20 October 2026, see [experiments](../e2e/experiments.md)) removes the command processor's wait for the submit thread at the swap ([submit thread at the swap](#submit-thread-at-the-swap-20-october-2026)). The swap submission becomes a submit thread job like the others. The D3D12 presenter reserves the value of its guest output fence when the swap writes the image, and the submit thread signals it after that job; a paint or capture of the image waits on the CPU until that signal is on the queue, so its commands follow the frame's. The presentation of the frame (the UI thread paint request, or the paint itself when the guest output thread paints) runs on the submit thread after the swap job. In FM4 the UI thread always paints, because the achievement toast keeps an ImGui dialog registered; the paint on the submit thread is implemented (a paint there skips an image whose signal that thread has not enqueued yet, and the next presentation shows it) but does not occur.

BuildRoot `I:\FM4-build-usa` (USA), runtime of `23b0f52` with the change. Launcher defaults (look `pc`), 1x, AI driver, the cvar set per gate on one binary. Every cell reached its goal (3 of 3 runs where three ran); every cell has the same three error keys and no clear-gray frames.

| Gate | Async swap | Race fps per run | Draws per frame | `swap` zone per frame | Swap wait (`flush_wait`) per frame | CP busy per draw | Race p99 |
|---|---|---|---|---|---|---|---|
| `asyoffp` (`photo`, 240 Hz, 3 runs) | off | 130.6, 183.4, 202.4 | 3,439, 2,252, 2,001 | 0.46-0.87 ms | 0.32-0.60 ms | 2.18-2.28 us | 7.5-11.2 ms |
| `asyonp` (`photo`, 240 Hz, 3 runs) | on | 188.5, 203.1, 184.7 | 2,594, 2,456, 2,953 | 0.034-0.043 ms | 0 | 1.76-1.92 us | 7.0-8.5 ms |
| `asyoffc` (`career`, 240 Hz, 3 runs) | off | 171.0, 206.4, 177.7 | 2,898, 2,290, 2,758 | 0.38-0.61 ms | 0.24-0.47 ms | 1.94-1.99 us | 7.4-8.0 ms |
| `asyonc` (`career`, 240 Hz, 3 runs) | on | 200.2, 201.7, 197.3 | 2,467, 2,508, 2,560 | 0.033-0.034 ms | 0 | 1.86-1.90 us | 6.8-7.4 ms |

- The race stays command processor bound (938-977 ms/s busy). At matched draws the command processor frame is 0.3-1.3 ms shorter: `photo` 4.98, 4.31 and 5.19 ms against 5.74, 5.45 and 6.50 ms from the baseline's fit (0.26 ms plus 2.11 us per draw), `career` 4.65-4.78 ms at 2,470-2,560 draws against about 4.9 ms at 2,500. The swap accounts for 0.35-0.85 ms of it; the rest is within the spread of the races. `compare.py`: `career` PASS (race 177.7 to 200.2 fps), `photo` WARN on capture statistics only (race 183.4 to 188.5 fps with 11% more draws per frame).
- The submit thread is busy 244-292 ms/s in the race (212-276 without the change). It runs two callbacks per frame (the presenter's signal and the presentation request), 0.8-0.9 ms/s in all.
- Replay (`career`): 100.8-103.5 fps against 100.9-102.0, not command processor bound (540 ms/s); the `swap` zone falls from 0.15 to 0.04 ms per frame.
- 60 Hz (`asyoff60`, `asyon60`, `photo`, 1 run): race 60.0 fps in both, p99 17.1 and 17.3 ms, PASS.
- 2x at 240 Hz (`asyoff2x`, `asyon2x`, `photo`, 1 run, with `--keep-ppm`): the swap waited 1.51 ms per frame without the change; the `swap` zone falls from 1.89 to 0.04 ms per frame and the command processor from 935 to 765 ms/s busy, so the race is now GPU bound (139.8 against 136.4 fps, GPU 7.0 ms per frame), PASS. The 2560x1440 race captures are whole (HUD, mirror, road), with no partly written image.
- Smoke gate `asyonn` with the change and `FM4_INFO_SHOW=0`, against `pcsun`: 3 of 3 cells; `photo` and `career` PASS, `photo_view` WARN on capture statistics only.
- Window test (`asywin`, `photo` with the change): during the race the window was resized three times, switched to fullscreen with Alt+Enter and back, then closed with `WM_CLOSE`. The process exited with code 0 after 1.4 s; no new errors. The application hard-exits once the window closes, so the runtime's shutdown order (the submit thread drained before the presenter is destroyed) is not reached by this test.
- Not covered: 120 Hz, other tracks, the paint on the submit thread (it does not occur in FM4), a guest output size change during a run, and the runtime's shutdown path.

### Submit thread at the swap, 20 October 2026

BuildRoot `I:\FM4-build-usa` (USA), runtime of `cacb57c` with the submit thread trace (`submit_thread` in `perf.jsonl`, see [traces.md](../e2e/traces.md)). Launcher defaults (look `pc`), 240 Hz, 1x, AI driver. Gates `sflp` (perf, 3 runs) and `sflc` (`career`, 1 run); then `sdi0p`, `sdi400p` and `sdi200p` (perf, 3 runs each) on one binary with `d3d12_submit_draw_interval` 0, 400 and 200. Every cell passed 3 of 3 (1 of 1 for `career`).

- Without `d3d12_submit_thread_async_swap` (the only behavior before it), at the swap the command processor drains the submit thread before it records the swap list itself, so the presenter's commands follow the frame on the queue. In the race this wait (`flush_wait`) is 87-96 ms/s, 0.58-0.61 ms per frame (`sflp` at about 3,050 draws per frame, 155.8 fps; `sflc` at 165.2 fps), and 0.35-0.38 ms per frame at about 2,100-2,700 draws (`sdi0p`). It is most of the `submit` and `swap` zones and is counted as busy. In the replay it is 2.1 ms/s.
- The submit thread is not saturated: it is busy 230-280 ms/s in the race (replay of the deferred lists 175-235, `Close` and `ExecuteCommandLists` 36-47) at 5-6 submissions per frame. Almost every swap finds exactly one job in flight (`flush_jobs` equals `flushes_blocked`): the frame body submitted at the end of the primary buffer just before the swap. The wait is a serialization, not a backlog.
- `d3d12_submit_draw_interval` (default 0) ends the submission every n draws so that the thread replays the frame while it is recorded:

  | Interval | Race fps (3 runs) | Draws per frame | Swap wait per frame | `swap` zone per frame | CP busy per draw | Submissions per frame | GPU per frame |
  |---|---|---|---|---|---|---|---|
  | 0 | 194.4, 193.9, 192.7 | 2,109, 2,098, 2,682 | 0.35-0.38 ms | 0.48-0.53 ms | 1.87-2.22 us | 5.9-6.4 | 3.24-3.27 ms |
  | 400 | 194.1, 223.2, 190.8 | 2,699, 2,020, 2,768 | 0.09-0.16 ms | 0.23-0.31 ms | 1.81-1.84 us | 8.4-10.0 | 3.13-3.35 ms |
  | 200 | 148.8, 186.5, 209.2 | 3,353, 2,721, 2,169 | 0.07-0.17 ms | 0.22-0.36 ms | 1.88-1.96 us | 13.5-19.4 | 3.30-3.71 ms |

  At the same load (about 2,700 draws per frame) 400 lowers the command processor time per draw by 2-3% (5.02 to 4.94-5.02 ms per frame): the swap saves about 0.2 ms, the extra submissions cost back part of it in the draw zone. 200 costs more than it saves (draw zone +6% per draw, GPU +0.3 ms per frame). The race fps differences are within the spread of the races (2,000 to 3,350 draws per frame). The interval stays 0.
- Conclusion: the swap wait is removable, but splitting submissions removes only part of it at a cost. The [asynchronous swap](#asynchronous-swap-20-october-2026) removes all of it.
- Not covered: 60 and 120 Hz, 2x (where the GPU limits and extra submissions would cost GPU time), other tracks.

### pc facing the sun, 20 October 2026

Details in [look mod coverage, facing the sun](../analysis/look-mod-coverage.md#facing-the-sun-and-the-pc-preset-20-october-2026). BuildRoot `I:\FM4-build-usa` (USA, game build of 20 October 2026, 12:14). `e2e.py` at 2x with `--shader-dev`, a Quick Race on the Bernese Alps whose car stays on the grid without the AI driver, the near chase camera turned with the right stick in seven directions, the knobs switched live (runs `sun-alps`, `sun-hood`).

- Road, side view with the sunlit asphalt: `road_specular=0,road_sheen=0.4` turns the white sheet into asphalt with its aggregate (crop mean luma 127.2 to 95.6, pixels at 230 or more 4.2 to 0.9%).
- `glass=1,chrome=1,rims=1`, front left of the car with the sun reflected on the hood: a little more reflection on the windows and the rim edges (5.4% of the car crop by more than 4 of 255, against 1.4 to 1.6% between captures of one state); no highlight blob on glass or chrome. From the front 0.04% changes.
- The clear coat of `pc` turned the hood and roof into a white blob in that view (8.8% of the hood crop at 250 or more with `clearcoat_glint` 4; 0.0% without the clear coat). `clearcoat_glint=1` keeps a shaped highlight (1.5%); 2 gives 4.5%.
- The project owner put `clearcoat_glint=1`, `road_specular=0,road_sheen=0.4` and `glass=1,chrome=1,rims=1` into `pc`; `-Look "pc,clearcoat_glint=4,road_specular=1,road_sheen=0,glass=0,chrome=0,rims=0"` is the earlier `pc`. `test_pipeline.py` passes (16 tests).
- Smoke gate at 240 Hz and 1x on the same binary, `pcsun` (the new `pc`) against `pcsunold` (the earlier `pc`, given as its knobs), each after a warm-up run of its look that filled the DXBC cache: 3 of 3 cells in both, the same three error keys, no clear-gray frames.

  | Cell | Earlier `pc` | New `pc` | `compare` |
  |---|---|---|---|
  | `photo` | race 172.5 fps, p99 9.8 ms, GPU 3.27 ms (6,197 predicated draws per frame); photo mode 29.95 fps | race 204.9 fps, p99 7.6 ms, GPU 3.16 ms (4,498); photo mode 29.95 fps | WARN on capture statistics |
  | `photo_view` | 56.9 fps | 56.8 fps | WARN on capture statistics |
  | `career` | race 177.0 fps, p99 8.9 ms, GPU 3.27 ms (6,000); replay 95.3 fps | race 149.4 fps, p99 10.0 ms, GPU 3.33 ms (8,071); replay 98.8 fps | FAIL on race fps |

  The race fps follows the draws per frame of each AI race on a saturated command processor (962.9 against 976.3 ms/s busy in `career`; 421,000 against 418,000 draws per second): the `career` race of the new `pc` drew 34% more per frame, the `photo` race 27% less. The race GPU time per frame moved by +0.06 and -0.11 ms. One run per cell, so the GPU cost of the new knobs is not measured; see the next validation steps.
- Not covered: other tracks and times of day, the split-screen set, the cockpit camera toward the sun with the new knobs, 60 and 120 Hz.

### AO in the garage, Autovista and the career home, 20 October 2026

Details in [GTAO design](../analysis/gtao-design.md#6-stages-and-verification). Since the post pass framework's camera fallback of 19 October 2026, these scenes have a scene camera, so GTAO runs there and the final combine `30B67D8B86C37587` applies it. BuildRoot `I:\FM4-build-usa`, `e2e.py` at 2x with `--shader-dev`, look `pc`, `FM4_LOOK_AO_DEBUG` and `FM4_LOOK_AO` switched live (runs `sun-alps`, `ao-ui`, `ao-autovista`, `ao-home`).

- The debug view shows an occlusion in the garage's full-screen car view of Quick Race, the pre-race intro, Autovista (the Ferrari 458) and the career home (the Ford Ka). The small car view of the car select screen has none: its scene depth is narrower than `min_width`.
- In Autovista, whose camera rests (captures of one state byte-identical), AO 0.6 against 0 changes 8.5% of the frame (at most 67 of 255), 1 against 0 11.0%: the wheel wells, the brake disc and caliper behind the spokes, the front intakes and the contact with the floor get darker, and the brake disc turns almost black at 0.6.
- Not covered: the GPU cost in these scenes, other cars in Autovista, the garage's other scenes.

### Cockpit light look knob, 19 October 2026

- Built: `-Look "...,cockpit_light=<0 to 1>"` ([cockpit brightness](../analysis/cockpit-brightness.md#the-cockpit_light-knob-19-october-2026), [running](../running.md#look)); 0 by default, 0.5 in `pc` since 20 October 2026. 44 shader mods: the floor constant (`MinCockpitShadow`, `MinShadowValue`) of 38 interior shaders of every car shader set, and the shadow map and lightmap texels of the driver's body and suit (six hashes). RTV only.
- Offline: `override_fxc.py --stub` compiles the 63 real contracts of the 44 hashes of a Bernese Alps dump with and without the knob; `fm4_shader_mod_tests` passes (its synthetic shader now reads the floor constants c82 to c91).
- In the game (`I:\FM4-build-pass`, USA discs, 2x, `-Look original` plus the knob, AI driver, frozen frames, live `defines:` 0, 0.15, 0.3, 0.5, 0; runs `cockpit-light` and `cockpit-light2`): at 0.5, with the sun behind or beside the car, the gauges go from 2 of 255 to 41 to 60, the gloves from 6 to 10 to 30 to 45 (white with their red logo), and at 0.3 both are readable. In the hood camera no pixel changes at 0.5, in the chase cameras only the driver seen through the rear window (0.04 to 0.06% of the pixels), and in the cockpit the road and sky patches change by at most 0.3 of 255. No artifacts in the 1:1 crops. With the sun ahead of the car the faces toward the driver get no N·L and nothing changes; the dashboard top and the wheel rim stay near black because their albedo is black (25 to 28 of 255 in direct sun).
- A bit-coded trace (`cockpit-trace3`) found the shaders on screen: the gloves are the suit shaders WhiteFelt and ApendagesSuede (not the body shader `30712480E0AB46E4`), the dashboard top `327942867CAF7123`, the wheel rim `C7B414C46424E22F`.
- Not run: other tracks and times of day, the split-screen and 16-car sets in the game. The A/B on top of `pc` and the smoke gate followed on 20 October 2026 ([pc with the cockpit knobs](#pc-with-the-cockpit-knobs-20-october-2026)).

### Replay and countdown frame rate, 20 October 2026

At the project owner's request, replays and the 3-2-1 countdown run at the race frame rate instead of the title's 30; the post-race screens and photo mode keep 30 ([running](../running.md#-framerate)). `FM4_FULL_RATE=replay,countdown` (launcher and e2e default): the replay scenarios get the race rate in their file (`fm4-render-overrides.cpp`); the countdown, which shares its scenario with the post-race, gets it when it is pushed after the pre-race (`fm4-vblank-rate.cpp`); and while either is on top the guest vblank runs at twice the race rate (at most 240 Hz), because the title presents them every second vblank. BuildRoot `I:\FM4-build-pass` (Japan/Asia Disc 1, game build of 20 October 2026, 10:37), `career` scenario on Tsukuba with the AI driver.

- 1x, default rate (`fr2-career-60`): countdown 60 fps, replay 59.7 fps (p99 18.4 ms), post-race 30; 240 Hz (`fr2-career-240`): countdown 90 to 115 fps, replay 94.7 fps, post-race 30. Before: 30 for all three. The vblank trace shows the post-race push at frame rate 30.
- The replay clock against wall time is 1.00 at 30, 60 and 240 fps (32.5 s of replay over 32.5 s); the race clock 16.3 s after the countdown starts differs by 0.08 s between the default and 240 Hz, so the countdown follows wall time too.
- 2x (`fr2x-career-60`, `fr2x-career-240`): replay 59.2 fps (p99 22.7 ms) and 94.4 fps; after a 165 ms hitch the replay went straight back to 60.
- Gates at 240 Hz and 1x on the same binary: `frnew` against `frold` (`--env FM4_FULL_RATE=`): `career` PASS (replay 101.6 against 29.8 fps, race 165.5 against 166.4 fps); `photo_view` WARN on capture statistics; `photo` FAIL on its race phase (167.4 against 209.4 fps), where the AI race drew 31% more per frame (7,292 against 5,549 predicated draws) on a saturated command processor; the race scenario is not changed. `frcareer` (`career` tier): `career_finish` reaches EVENT COMPLETE through the reward screens, and `career_progress` passes.
- Not covered: photo mode (keeps 30), a race restarted from the pause menu (its countdown may keep 30), split screen, 120 Hz.

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
- Glare and dust are stronger since 20 October 2026 (`WINDSHIELD_GLARE` 0.3, threshold 0.3, radius 0.25, `WINDSHIELD_DUST` 1.0): with the first values they were hardly visible at strength 1. The project owner chose this set from an A/B of three in four frozen cockpit frames at 2x (`windshield-glare`).
- Open: other cars and tracks, rain and night.

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
| Look | `aces` preset (the earlier `pc`, now its alias; `gt` and `neutral` are the same with their tone curve): `tonemap=aces`, `saturation=1.1`, `bloom=0.7`, `sharpen=0.4`, clearcoat 1 with `clearcoat_glint=1`, `ao=0.6`, `sun=2` (sun glare per pixel, soft limit 1,200), `sky=0.7` (track lighting: sky brightness), `taa=1` (TAA before the HUD, the scene jittered while the camera moves and on a still camera, whose still pixels accumulate the phases, the look's sharpening after it), `cockpit_light=0.5` (the cockpit interior's shadow floor), `windshield=1` (cockpit window glass), `road_specular=0,road_sheen=0.4` (the road's grained sheen instead of its sun highlight), `glass=1,chrome=1,rims=1` | `-Look <preset or knobs>`; `original` is the game's look with TAA only, `original,taa=0` turns off all mods |
| Clearcoat | Setting A: `clearcoat=1`, `clearcoat_roughness` 0.3, chosen from three race A/B runs on 17 October 2026; `clearcoat_glint` 1 in `pc` since 20 October 2026 (4 in `aces`, `gt` and `neutral`, where the hood turns into a white blob toward the sun). Flakes are not used by any preset. | `-Look clearcoat=..,clearcoat_roughness=..,clearcoat_glint=..` |
| Ambient occlusion | GTAO on, strength 0.6, radius 0.75 m (presets `pc`, `aces`, `gt`, `neutral`): races, replays, photo mode, the garage's full-screen car views, Autovista and the career home | `-Look ao=<strength>` |
| Glass, chrome and rims | 1 in `pc` since 20 October 2026 (A/B at 2x toward the sun); off in the other presets | `-Look glass=..,chrome=..,rims=..` (and `glass_roughness`, `chrome_roughness`, `rims_roughness`) |
| Road surface | `road_specular=0,road_sheen=0.4` in `pc` since 20 October 2026; the game's road in the other presets | `-Look road_specular=..,road_sheen=..,road_sheen_grain=..` |
| Sky and vegetation | Off in every preset (A/B at 2x on 18 October 2026: only a slightly deeper sky) | `-Look sky_saturation=..,sky_contrast=..,sky_warmth=..,sky_horizon=..,foliage_hue=..,foliage_saturation=..,foliage_brightness=..` |
| Render target path | RTV: `render_target_path_d3d12=rtv`, `edram_single_rt_no_wrap=true` | `-Rov` (ROV, with `rov_occlusion_async=all`) |
| Occlusion query | `rov_occlusion_async=previous`; no sample correction (`rov_occlusion_awaited_report_divisor` at its default 1 since 18 October 2026) | `-Rov` |
| Vblank pacing | Variable (`vblank_vrr=true`) | `-FixedVblank` (fixed 60 Hz grid) |
| Frame rate | 60 in races, replays and the 3-2-1 countdown (since 20 October 2026); 30 for post-race and photo mode | `-FrameRate <30-240>` (races; replays and the countdown up to 120 fps) |
| Resolve readback | `readback_resolve=fast`; readback publish on its own thread (`d3d12_readback_publish_thread=true`) | - |
| Submit thread | `d3d12_submit_thread=true`; the swap does not wait for it (`d3d12_submit_thread_async_swap=true`, since 20 October 2026) | - |
| Guest memory page state | `clear_memory_page_state=false` | - |
| Shader compilation | Synchronous (`async_shader_compilation=false`) | - |
| Anisotropic filtering | 16x (`anisotropic_override=5`) | - |
| Shadow workaround | Off | `-ShadowWorkaround` |
| Rear-view mirror | Every other frame (`FM4_HALF_RATE_MIRROR=1`) | `-FullRateMirror` (every frame) |
| Race quality | `high` (SoftParticles and related settings; its tiling is the 4x MSAA scenario in three tiles) | `-Quality xbox\|high\|ultra` |
| Race tiling | One 720p tile without MSAA whenever the look has TAA (every preset, since 20 October 2026), at every quality; TAA anti-aliases it | `-Msaa` (the tiling of `-Quality`); `-Rov` and a look without TAA keep it too; `-SingleTile` one tile also without TAA, with FXAA |
| Display | `native`: borderless fullscreen on the primary monitor | `-Display native\|720p\|1080p\|1440p\|4k`, `-Fullscreen`, `-Supersample`, `-ResolutionScale 1-4` |
| Render scale | Smallest whole multiple of 1280x720 that covers the 16:9 image on the display (720p 1x, 1080p, 1440p and 2560x1600 2x, 4K 3x), one step higher with `-Supersample` | as above |
| Presenter downscale | Area averaging (`present_area_downscale`) | - |
| Settings overlay | Hidden | `-Info`; F2 toggles it |
| Window mode | Toggled with Alt+Enter | - |

Notes on the defaults:

- The shadow workaround (`SkipShadowMapUnlessCockpit`, from xenia-canary/game-patches#398) is no longer a default as of 16 October 2026. Without it, the shadow map is rendered on every race camera. The captures checked show no box shadows. Cars now have self-shadowing and crisp shadows on the asphalt. The flag is read when the scenario is pushed, so it cannot be A/B-tested in the middle of a race.
- One tile (`FM4_TILING_SCENARIO=2`) is the default with TAA since 20 October 2026; the launcher then leaves out the swap's FXAA. `-SingleTile` gives it also to a look without TAA, with FXAA. The trade-off is in [Open issues](#open-issues).
- Look mods and AO run on the RTV path only, by design: `-Rov` turns the shader overrides off and shows the title's own picture. ROV is the reference for the EDRAM emulation, so mods and AO are not planned for it, and this is not counted as a gap.
- The launcher no longer passes `rov_occlusion_awaited_report_divisor=2` (18 October 2026), so `-Rov` reports the samples it counts and stays a reference without a title correction. The cvar remains in the runtime for experiments; it only affects ROV occlusion samples (`RetireROVOcclusionSnapshots` in [work/runtime/src/graphics/d3d12/command_processor.cpp](../../work/runtime/src/graphics/d3d12/command_processor.cpp)).

## Current performance

These numbers are baselines, not targets.

- **Quick Race**: 60.0 fps (p99 17.1 ms), 118-120 fps at 120 Hz (p99 8.8-9.8 ms), and 224-235 fps at 240 Hz (p99 5.3-6.2 ms). All three are from the 16 October 2026 matrix.
- **240 Hz**: three `photo` runs of 21 October 2026 with the launcher defaults (`n1rtvp2`, RTV on the build of the host render thread with it off) ran the race at 226.5 to 237.5 fps (median of the runs 229.4, p99 5.48 ms, 1,517 to 1,981 draws per frame, GPU 2.82 ms; [host render thread](#host-render-thread-stage-n1-21-october-2026)). With one tile and TAA (the default since 20 October 2026) the race at 1x ran at 230.7 fps in `photo` (1,893 draws per frame) and 233.2 fps in `career` (1,711) with the asynchronous swap, GPU 2.85 to 2.87 ms per frame; the command processor was busy 745 to 774 ms/s and waited for the vblank most of the rest, so these races are close to the 240 Hz limit ([one tile with TAA](#one-tile-with-taa-the-presets-and-taas-history-20-october-2026)). With the tiles of `high` (`-Msaa`) the same binary without the asynchronous swap ran at 200.5 to 218.1 fps on a saturated command processor (818 to 948 ms/s). GPU time with the tiles is about 2.8 ms per frame without AO and 3.01 ms with AO. With the asynchronous swap and the tiles the race ran at 184.7-203.1 fps in `photo` (2,456-2,953 draws per frame) and 197.3-201.7 fps in `career` (2,467-2,560); the swap takes 0.03-0.04 ms of the command processor frame instead of 0.4-0.9 ms ([asynchronous swap](#asynchronous-swap-20-october-2026)). The race fps follows the draws per frame of each AI race.
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
- **Fixed seed for the race setup**: `FM4_FIXED_SEED=<n>` (21 October 2026, off by default; see [experiments](../e2e/experiments.md#fixed-seed-for-the-race-setup)) keys SQLite's random generator at the opponent cars query and pins the AI drivers' seeds. Verified the same day on one binary (sha256 `27f90c57...`, the `freeze` scenario at 2x with `engageaionstart`): two runs with `FM4_FIXED_SEED=1` showed the same opponent cars and the same grid slot (the same station wagon ahead); two runs without it showed other cars and other slots, draws per frame 3,320.2 and 3,195.6 (3.8% apart), 56.0% of the pixels above 4. Seeded freeze runs still differ in their frozen frame, not because of the race setup: with the rewind's first press pinned to race frame 720 (counted in pad polls then), four seeded runs differ in 19 to 34% of the pixels, because the race advances by wall time ([frozen frames](../e2e/frozen-frames.md#baseline-variance); the earlier seeded pair is in [history](history.md#21-october-2026-frozen-frame-variance-with-presses-on-the-wall-clock)).
- **Fixed frame time for races**: `FM4_FIXED_FRAME_TIME=1` (21 October 2026, off by default; see [experiments](../e2e/experiments.md#fixed-frame-time-for-races)) turns on the title's `lockupdatetimeto60fps` (every main loop pass one step of 0.01668335 s instead of the measured time) and starts the 3-2-1 60 session updates after the countdown placed the cars; with `FM4_FIXED_SEED` the cars' random states are reset at the 3-2-1. The diagnostic controller now counts race frames in session updates, not pad polls. Verified the same day on one binary (sha256 `90179f8d...`): two `freeze` runs with both switches started the 3-2-1 and pressed Y on the same session updates and agreed within 0.2% in draws and predicated packets, but their frozen frames still differ in 25.64% of the pixels: the race diverges from a last-bit difference while the AI revs before the start, whose input is not found, and the crowd differs ([what pinning the moment needs](../e2e/frozen-frames.md#what-pinning-the-moment-needs)). `parity.py` keeps its 60% image limit.
- **Shadows**: with the workaround off (the default), the shadow map is rendered on all race cameras. Its parity with the original Xbox has not been compared.
- **Stubs**: some Xbox and runtime service functions are still stubs. The synthetic instruction tests do not prove that the kernel, audio, timing, synchronization or shaders are correct.
- **Synthetic tests**: the 295,650 instruction checks and 5 script tests use synthetic data. They do not prove that the game behaves correctly.

## Open issues

- **Sun glare strength depends on the MSAA level.** Matching the original Xbox glare is not a goal (decision of 18 October 2026): the glare is to look good and stay the same at every quality setting. Measured on 18 October 2026 (Quick Race with the camera cycle, `--quality xbox`, `REX_OCCLUSION_SEGMENT_TRACE`): the sun query (the awaited report) sees the sun for about 3 seconds at the same point of each run (about 82 to 86 s). On ROV the sun sprite's samples (passed plus depth-failed, at most) are 338 at 1x MSAA, 675 at 2x and 1,355 at 4x (`FM4_TILING_SCENARIO` 2, 1 and 0): the counter counts guest samples, exactly in proportion to the MSAA sample count. The empirical divisor of 2 (13 October, races at 2x MSAA) therefore equalled the sample count of that tiling and turned samples into pixels; since races at `high` quality use 4x MSAA, it would have left twice the pixel count. On RTV at 2x the host query passes 2 to 10 samples in 15 frames at the same point, so it does return samples when the sun is in view. A sweep of fixed counts on RTV (`rtv_occlusion_debug=fake`, same race, captures at the sun point) shows that the glare does not saturate at the sprite's full area: at 1x MSAA nothing shows at 85, a small star at 340, a larger star with haze at 680 and 1,360, and the screen washes out at 4,000. So the real counts at 1x (at most 338) can give only a small star, and those at 4x (at most 1,355) a large one. The same count gives a weaker glare at 4x than at 1x (340: none against a small star; 1,360: a star against a star with haze), but by much less than the factor 4 that a title expecting samples would show. The captures of different runs differ by up to 0.3 s, so this is a visual judgment, not a measured curve. The count is therefore not simply a fraction of the sprite, and the glare may also depend on the frame-wide queries, which are not faked and also scale with MSAA. With a fixed count the glare also shows where the sun is behind the cockpit roof, which a normal run (real counts, ROV or RTV) does not. The title code and the constants of the glare draw were not traced, because the goal is no longer parity. The `sun` look knob (18 October 2026, `sun=2` in every preset but `original`) answers this on the RTV path: the awaited reports become guest pixels times the strength, through a soft limit of 1,200 pixels (see [running](../running.md#sun-glare)). A/B on 18 October 2026 (RTV, the same race, fixed counts so that the sun shows at lap time about 39 s, `REX_OCCLUSION_SEGMENT_TRACE`): with `sun=2`, 340 samples at 1x and 1,360 at 4x both report 607 pixels and give the same medium star, where without the knob they gave a small star and a large one; 4,000 samples at 4x report 933, and 8,000 at 1x report 1,161, a large star with haze over a scene that stays readable, where 4,000 samples without the knob wash out the screen. The MSAA is taken from the draws of the query interval, because the surface at the report can already be another one (at 1x the first build read 4x there and reported 170). Races with real counts at `high` quality could not be compared with and without the knob, because the AI driver took different lines (fifth against first place).
- **Full-frame occlusion on RTV**: the full-frame query used for auto-exposure reports about 1.5 times the ROV sample count, because the host query counts every sample that passes. No visible effect was seen. The auto-exposure query was not changed.
- **RTV missing road pieces (mitigated, not fully explained)**: on some runs of the RTV path, road pieces (mostly on the start straight, 4-14 seconds after the start) were not drawn and showed the flat clear color (102,102,102). Kerbs and other objects were still drawn. The cause was found on 15 October 2026. The loader's sGPU heap could not place a 1.8 MB section because the heap was fragmented after a load spike. The loader then failed and discarded the request. The fix is a fallback: when the sGPU allocation fails, the section is retried from fGPU. Set `FM4_STREAMING_FALLBACK=0` to turn it off. See [work/fm4-rex/src/fm4-streaming-fixes.cpp](../../work/fm4-rex/src/fm4-streaming-fixes.cpp). After the fix, 20 of 20 runs with fixed vblank (the more frequent failure case) showed no missing road, and 5 of those runs used the fallback successfully. Three Quick Race runs with the current defaults also showed no clear-gray frames. The e2e harness detects the symptom through `clear_gray_frames`.
- **Look preset values**: the B4 step 3 note of 16 October 2026 says the preset values had not been chosen yet. The defaults in the table above were set later that day, and that note is superseded (see [history](history.md)).
- **Tiling quality versus speed**: one tile without MSAA is the default with TAA since 20 October 2026 (the project owner's choice). It takes 19 to 26% fewer draws, 60 to 68% fewer predicated packets and 0.5 to 0.6 ms less GPU time per race frame at 1x than the tiles of `high`, and at 2x about 1 ms less GPU time (19 October 2026, [TAA, MSAA and tiling](../analysis/taa.md#5-msaa-and-tiling)). Without MSAA a still picture has no anti-aliasing unless the jitter runs, so a still camera is jittered and TAA accumulates its phases ([one tile with TAA](#one-tile-with-taa-the-presets-and-taas-history-20-october-2026)). Not yet checked with the fixed history: thin wires and fences in motion at 2x, which TAA alone must hold without MSAA. On ROV (3 runs, 14 October 2026) `-SingleTile` raised fps from 54.3 to 58.6; without TAA (FXAA) thin wires break up.
- **Ambient occlusion scope**: AO is missing in the cockpit interior (the depth range is compressed there) and in the small car view of the car select screen (narrower than the scene depth's minimum width). The garage's full-screen car views, Autovista and the career home have it since the camera fallback of 19 October 2026. See [GTAO design](../analysis/gtao-design.md).
- **An "Ally X performance" look preset (candidate)**: on the ROG Xbox Ally X's 890M, GTAO takes 0.7 to 0.9 ms and the windshield pass 0.6 ms of a 7.5 to 8.3 ms race frame at 1x ([17.25](../analysis/d3d-hle-n4.md#1725-the-second-rog-xbox-ally-x-run-24-october-2026)); a preset without them, or a cheaper windshield, would give that back on battery. The windshield costing almost as much as GTAO is worth a look.

## Current gaps

Everything still open, grouped by area.

### Display

- The `sun` knob keeps the sun glare the same at every MSAA level, checked with fixed counts only (see [Open issues](#open-issues)). `-Look original` and `-Rov` keep the glare that changes with the MSAA level.
- AO is not in the cockpit interior. It runs in the garage's full-screen car views, Autovista and the career home (checked on 20 October 2026, [AO outside races](#ao-in-the-garage-autovista-and-the-career-home-20-october-2026)); at 0.6 it turns the brake disc behind the spokes almost black in Autovista. See the "Later" section of [GTAO design](../analysis/gtao-design.md).
- The cockpit interior is near black on sunny parts of the Bernese Alps (dashboard top 0.1 to 3 of 255, gauges 1 to 6 at 2x). Measured on 19 October 2026 with frozen frames: RTV and `-Rov` match within a few levels with `-Look original`, there is no transient after a camera switch, and in tunnels the interior is readable. It is the game's interior lighting: its own shadow map shades it, `MinCockpitShadow` is 0, and the ambient term is small against an exposure set by the snow outside. The `pc` look darkens it by 15 to 35% more through ACES; AO does not reach it. The `cockpit_light` look knob (19 October 2026, 0.5 in `pc` since 20 October 2026) raises the shadow floor of the 38 interior shaders and the six driver shaders: at 0.5 the gauges go from 2 to 4 of 255 to 32 to 60 when the sun is behind or beside the car, and the windows, the exposure and the other cameras stay as they are; with the sun ahead, and on the black dashboard top and wheel rim, it changes little. A cockpit exposure of +2 stops blows out the windows. The windshield and side windows look open in the cockpit camera because the title draws no window glass for the player's car in that camera (only in the 3-2-1 camera); the `windshield` post pass (1 in `pc`) adds it. See [cockpit brightness](../analysis/cockpit-brightness.md).
- Look mod coverage is partly verified in the game (17 October 2026, [look mod coverage](../analysis/look-mod-coverage.md#in-game-checks-17-october-2026)): false-color runs show the body paint mods of races, replays, the cockpit, car menus, HomeSpace, Autovista (the showroom) and the 16-car and split-screen car shader sets on screen. The 16-car and split-screen sets were loaded in a Quick Race through `FM4_RACE_SETTINGS`. On 18 October 2026 the title's 16-car render scenarios were reproduced at `-Quality xbox` on a 12-car grid (Quick Race has no 16-car grid), and photos at shutter speed 0 and 100 (also one from a replay) were saved; `REX_GPU_INVENTORY` showed no new hash. A real split-screen race and a real 16-car grid were not run. The second body paint technique of each set, the four watermark final combines, the no-post-effects final combine and the three photo motion blur variants never showed in any run, so their mods are checked by compilation only. `E5B1A71249D0CA9A`, first thought to be the environment cube render, draws the car lights and trim, so it needs no clear coat.
- The glass, chrome and rim mods (`-Look glass`, `chrome`, `rims`; 31 hashes of the race, 16-car, split-screen and garage car shader sets) are in `pc` since 20 October 2026. The A/B at 2x on the Bernese Alps grid with the camera turned toward the sun showed a little more reflection on the windows and no highlight blob on glass or chrome ([facing the sun](../analysis/look-mod-coverage.md#facing-the-sun-and-the-pc-preset-20-october-2026)); the split-screen set, the chrome rims on screen and other tracks and times of day were not seen. The spinning-wheel blur shaders, the chrome with a normal map, the showroom set and the material shaders that load in the UI only have none. See [look mod coverage](../analysis/look-mod-coverage.md#glass-chrome-and-rims-18-october-2026).
- The sky and vegetation mods (`-Look sky_*` and `foliage_*`; 15 hashes: the race sky dome, trees, tree cards, bushes and grass of every race track) are in no preset. False color and an A/B at 2x covered the Bernese Alps and Maple Valley at one time of day. The HomeSpace skies, `DIFF_TREEBEND_1` (trunks and branches) and the terrain under the grass have none, and 5 of the 15 hashes did not draw on these two tracks. See [look mod coverage](../analysis/look-mod-coverage.md#sky-and-vegetation-18-october-2026).
- The road surface mods (`-Look road_*`; 25 hashes: the road shaders of 20 race locations, the painted lines, the road edges and the paved shoulders) are in `pc` since 20 October 2026 as `road_specular=0,road_sheen=0.4`, chosen by the project owner from the crops below and from an A/B toward the sun on the Bernese Alps grid on that day ([facing the sun](../analysis/look-mod-coverage.md#facing-the-sun-and-the-pc-preset-20-october-2026)). The roads of Amalfi, Fujimi Kaido and Maple Valley are drawn by terrain, shoulder and track object shaders, so they keep the game's highlight. The near-white sheet on sunlit asphalt toward the sun is the game's own sun highlight of `ROAD_BLND_2`, a Blinn-Phong lobe of power 25 with no Fresnel, whose specular amount comes mostly from the blend value of the rubber, which `road_gloss` does not scale: `road_specular=0` removes it, `CameraExposureRange` with `maxval` 1 leaves it as it is. The sheen in its place has a grain (`road_sheen_grain`, default 1) since 18 October 2026, after the sheen without it turned the sun-facing asphalt of Top Gear into an even, milky haze. In A/B runs at 2x on the Bernese Alps and Top Gear, `road_specular=0,road_sheen=0.4` takes the Alps sheet from 51.8% of the crop at 230 or more to 0.4% and keeps the aggregate and the skid marks of Top Gear visible through the glare; views away from the sun change by at most 1.5 of 255, with no seams at the lines and edges. Facing the sun on Top Gear, a hood view patch where the game's glint is weak is 24 of 255 brighter than the earlier `pc`, a cockpit view patch 6 darker. False color covered the Bernese Alps, Tsukuba, Sebring and Top Gear; the A/B ran at one time of day. Indianapolis and Infineon (`2AD313C93939CC4E`), the shoulders of Tsukuba and the other `SHLDR_*` hashes have no mod. See [look mod coverage](../analysis/look-mod-coverage.md#the-sheen-on-top-gear-and-its-grain-18-october-2026).
- The track lighting knobs `road_gloss` and `sky` (`FM4_TRACK_SETTINGS`, see [running](../running.md#track-lighting)) work in the game. The `pc` preset has `sky=0.7` since 18 October 2026; no preset sets `road_gloss`. Results of that day (BuildRoot `I:\FM4-build2`, `main` `6f8d04f` with the knobs; look `pc`; Bernese Alps Quick Race at 2x, 2560x1440):
  - The game mounted the edited copies of 30 files in 24 track folders.
  - For identical frames, the player's car stays on the grid with no AI driver and no throttle, and the camera looks around with the right stick. Quick Race puts the car in one of several grid slots at random, so only runs that landed in the same slot are compared.
  - Sky: a clear-sky patch measures RGB (147, 187, 216) at `sky=1`, (129, 173, 206) at 0.8 (another slot), (119, 163, 199) at 0.7 and (107, 152, 190) at 0.6. At 0.7 the sky is bluer and less washed out, and the snow and clouds keep their brightness. The project owner chose 0.7 for the `pc` preset.
  - Road: looking toward the sun, the sunlit asphalt next to the shaded grandstand is a near-white sheet (51.6% of a 640x300 crop at 230 or more), and `road_gloss` does not remove it. It is the road shader's own sun highlight; see the road surface item below. In the normal hood view, the sparkle at `road_gloss=1` reads better than the slightly milky 0.25, so the knob stays at 1.
  - Smoke gates at 240 Hz passed 3 of 3 cells with the knobs off (`rsoff`, which covers the runtime's file link lookup in every file open) and with `road_gloss=0.5` and `sky=0.8` (`rson`). `rson` against `rsoff`: `career` PASS; `photo` and `photo_view` WARN on capture statistics only. The error keys are the same, and there are no clear-gray frames.
  - The new `pc` preset (`sky=0.7`, gate `pcsky07`) against `pc` with the game's sky (`pcsky1`, the same binary, run just before it) at 240 Hz: 3 of 3 cells passed. `career` PASS (race 192.0 fps, p99 8.5 ms; replay 30.0 fps). `photo` WARN on capture statistics only (race 204.6 fps; photo mode 29.94 fps). `photo_view` WARN on capture statistics only (56.98 fps). There are no new error keys and no clear-gray frames, and only the `pcsky07` cells mounted the copies.
- Parity with the original Xbox (shadows, color, reflections, noise and object edges) has not been compared. The combinations of MSAA and render scale are not all verified.
- The post pass framework (18 October 2026) gives screen passes at three points of the frame, history textures, camera reprojection, per-car motion vectors, the jitter of the scene draws and a write-back into the game's scene, and has GTAO, the motion vectors and TAA ([post pass framework](../analysis/post-pass-framework.md)); rain screen effects, screen-space reflections and temporal upscalers are not written yet (see [post pass framework](../analysis/post-pass-framework.md#the-final-point-and-upscalers)). The motion vectors (host-built from the game's resolved depth, its stencil object IDs and the car and camera matrices of the vertex shader constants; [motion vectors](../analysis/motion-vectors.md#8-implementation-18-october-2026)) are verified in the game; the cockpit interior, the world through car glass and the rear-view mirror panel are handled with a mask. Wheel spin and the steering wheel and hands take the motion of their car or interior. Not traced: a 16-car grid, and what sets the scale k of the game's matrices.
- TAA (19 October 2026; [TAA, current state](../analysis/taa.md#current-state)): every preset has TAA at the final point, before the HUD, with `TAA_SHARPEN` 0.4 (none in `original`) and one tile without MSAA; the scene is jittered while the camera moves at 45 fps or more and on a still camera, whose still pixels accumulate the phases ([one tile with TAA](#one-tile-with-taa-the-presets-and-taas-history-20-october-2026)); `-Look "aces,taa=0"` turns it off and `-Look "aces,taa_point=1"` runs it at the scene_color point instead, as an option. From step 7 (19 October 2026, 20:25) until 20 October 2026 neither point had a history in the game, so TAA's own anti-aliasing was not in effect then ([TAA, the history defect](../analysis/taa.md#the-history-defect-19-to-20-october-2026)). It costs about 0.2 ms of GPU time per race frame at 2x (0.47 ms at the scene_color point), passed the smoke and perf gates and the Japan/Asia check, and the scene camera behind it is known on all 26 environments of the TRACKS screen and in replays. There is no reactive mask for particles and the interior mirror; nothing ghosted in the checks so far.
- Texture replacement (`-TextureDump`, `-TextureReplace`; 19 October 2026) works on the Bernese Alps: a Quick Race dumps 969 textures, the sky panorama (`5de3b416781cfd97`, DXT1 8192x2048) keeps its hash across runs, and a test pattern in its place shows in 2x captures and in the car reflections. The smoke gate with both switches on passed 3 of 3 cells against the same binary with them off (`career` PASS, `photo` and `photo_view` WARN on capture statistics). See [texture replacement](../analysis/texture-replacement.md#verification). Open: no real replacement art yet; no other track, the HomeSpace or the garage dumped; replacements load on the command processor thread, which a large pack would make stutter; render targets, cube maps, arrays, 3D textures and textures that change all the time are not covered. The offline extractor (`texture_extract.py`, 19 October 2026) dumps the textures stored in the 32 track archives without running the game: 18,473 distinct hashes, among them a sky panorama for every race track (tiles on Road America and Silverstone). Its Bernese Alps sky is byte-identical to the runtime's dump, and 103 of its 493 Bernese Alps hashes are in the runtime dumps of that track; the textures streamed from the `.bix` files (8,300 records) and cube maps are not extracted ([offline extraction](../analysis/texture-replacement.md#offline-extraction)).
- Not done: post-process anti-aliasing other than TAA (SMAA, or FXAA without `-SingleTile`), HDR10 output (design: [HDR output](../analysis/hdr-output.md)), new lighting for tracks (beyond the `TrackSettings` knobs), car materials other than paint, glass, chrome and rims (tires, brakes and lamp lenses; the cockpit interior has only the `cockpit_light` shadow floor), track surfaces other than the road, its lines, edges and paved shoulders (terrain, kerbs, grass), and car flake paint. Flake cells in UV space are too small for race cameras and need an LOD model.
- Widescreen and ultrawide need a FOV and projection hook, culling and HUD changes. The image is always 16:9, with bars on other screens.

### Performance

- At 240 Hz, the race is limited by the command processor (pacing and cost per draw, 1.8-1.9 us per draw at 2,500 draws per frame). The swap no longer waits for the submit thread ([asynchronous swap](#asynchronous-swap-20-october-2026)). At 2x it is limited by the GPU (about 140 fps in the `photo` race at 7.0 ms per frame on 20 October 2026; about 162 fps at 6.09 ms in earlier measurements).
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

- The clone and the full workflow have not been run on a second Windows computer, and neither has an [export](../running.md#export-to-another-computer) of the GUI launcher (checked on the development computer only).
- AMD and Intel GPUs and lower-end CPUs have not been tested.
- Only the Japan/Asia and USA releases are supported. The European release and title updates are not. The European discs have not been compared with the other two. A title update relinks the executable, so supporting one needs a second integration profile and a runtime fix for the USA v4 module patches ([title updates](../analysis/title-updates.md)).
- There is no CI, no setup wizard, and no license and provenance review for a release. This includes the pending `XenonRecomp-FM4-fixes.patch`. See [sources and licensing](../sources-and-licensing.md).

## Next validation steps

The proposed order is:

1. **Run the 2-hour soak test and the 60 vs 240 fps handling test.** The soak test watches memory, handles and VRAM. The handling test compares lap times and handling at both rates.
2. **Run on a second Windows computer.** First an export: install the Visual C++ redistributable, copy the export folder and the discs, and play from `fm4_launcher.exe`. Then the clean setup: `setup`, `generate`, `build` and `run` from a clean clone.
3. **Extend e2e coverage.** Add photo overwrite, photo with an empty shader cache, Autovista, a Disc 2 car, cars of the other DLC packs, and direct launch (a launch without the menus is not built; what it needs is in [direct race launch](../analysis/direct-race-launch.md); the [fast route](../e2e/README.md#fast-route) takes the menus at the title's pace instead and leaves at most 25 s of menus for it to save). Re-test photo overwrite and the empty shader cache on the latest build, because neither has been repeated since the 16 October changes. The photo, Autovista and replay steps are planned in [photo mode, Autovista and replays](../analysis/photo-autovista-replay.md#4-proposed-plan).
4. **Inventory the stubs.** List every XAM and kernel stub called in `runtime.log` across all scenarios.
5. **Finish the checks of `pc` facing the sun.** The race GPU cost of the road sheen and the glass, chrome and rim mods with the `perf` tier, the split-screen set, another track and time of day, and the cockpit camera toward the sun. See [look mod coverage](../analysis/look-mod-coverage.md#facing-the-sun-and-the-pc-preset-20-october-2026).
6. **Re-run the gate tiers after each change.** Use `smoke` for quick checks, `perf` for performance, `full` before committing a stage, and `career` for the rewards path. See the [e2e overview](../e2e/README.md) for the commands and gates.
7. **Check TAA in motion with one tile and its history.** One tile is the default since 20 October 2026, and TAA has its history again since the same day. Compare moving races at 2x against `-Msaa` in 1:1 crops and consecutive frames: thin wires, fences, lattices and distant road lines, which TAA alone must now hold. Then the countdown and a replay with a still camera, the photo mode live view, and 60 and 120 Hz.
8. **Watch TAA for ghosting, then trace a 16-car grid.** TAA steps 1 to 7 are done ([TAA](../analysis/taa.md#current-state)). Not observed yet: tire smoke from the cockpit camera, sparks of a collision, the heavy smoke of a full grid at the start, and gravel dust; a case that ghosts gets step 8, a reactive mask written by a shader mod for that shader. The scene_color point could become the default after a smoke gate with `taa_point=1`, the write-back counter on more tracks, career, replays and photo mode, and evidence that its occlusion filter helps.
9. **Build a sky pack.** The offline extractor gives the skies of every track (`<BuildRoot>\texextract\panoramas.txt`); check one sky other than the Bernese Alps in game, look at the Road America and Silverstone tiles, and load replacements off the command processor thread before building a large pack. See [offline extraction](../analysis/texture-replacement.md#offline-extraction).

This page documents the status. It does not report that all further tests have been run, and it is not an instruction to run them automatically.

## Related documents

- [history](history.md): dated chronology and past results
- [setup](../setup.md), [running](../running.md)
- [e2e overview](../e2e/README.md), [traces](../e2e/traces.md), [experiments](../e2e/experiments.md)
- Analysis: [post pass framework](../analysis/post-pass-framework.md), [GTAO design](../analysis/gtao-design.md), [motion vectors](../analysis/motion-vectors.md), [B4 shader research](../analysis/b4-shader-research.md), [visual style](../analysis/visual-style.md), [material atlas](../analysis/material-atlas.md), [photo mode, Autovista and replays](../analysis/photo-autovista-replay.md), [direct race launch](../analysis/direct-race-launch.md)
- [sources and licensing](../sources-and-licensing.md)
- [Build script](../../outputs/FM4-build/Build.ps1), [verification record](../../outputs/FM4-build/verification.json), [e2e harness](../../outputs/FM4-build/e2e/e2e.py), [matrix](../../outputs/FM4-build/e2e/matrix.py)
- [Project overview](../../README.md), [agent guide](../../AGENTS.md)
