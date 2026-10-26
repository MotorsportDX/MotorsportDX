# The native path as the default with a discrete GPU

Status: readiness report, 26 October 2026, for the main session's decision. Today the launchers run the native path ([D3D-level HLE](d3d-hle.md), stages N1 to N5) by default only on a computer with integrated GPUs only (the main session's decision of 24 October 2026, [17.25](d3d-hle-n4.md#1725-the-second-rog-xbox-ally-x-run-24-october-2026)); with a discrete GPU the default is RTV, the PM4 path on host render targets. This note gathers what flipping that default would change, the evidence on the development laptop, and a recommendation. RTV (`-Rtv`) and `-Rov` stay selectable as the references either way (AGENTS.md, render target paths).

## 1. What the flip changes, setting by setting

The flip is one line in each launcher: the automatic choice of `launch_plan` (`pipeline.py`) and its port (`plan.cpp`) becomes "the native path unless `-Rov` or `-Msaa`" on every computer instead of only with integrated GPUs. Everything below follows from the rules that already exist for `-Native`.

### 1.1 The defaults side by side

| Setting | RTV, today's default with a discrete GPU | Native path, the default after the flip |
|---|---|---|
| Render path | the command processor thread parses the guest's PM4 ring | the host render thread consumes records (`host_render_thread`, `host_render_native`), readbacks on demand, resolves from the host render targets, the draw back end (`native_cvars`) |
| Race tiling and anti-aliasing | one 720p tile without MSAA (the title's UI scenario, `FM4_TILING_SCENARIO=2`), TAA | one pass with 2x MSAA (the title's 2x scenario made one 1280x720 tile, `FM4_TILING_SCENARIO=1`, `FM4_ONE_PASS_MSAA=2`), TAA on top |
| Race quality (`-Quality high`) | the quality's race settings without its `TilingScenario` | the same settings, the same way |
| Render scale | the smallest whole multiple of 1280x720 that covers the screen, a 16:9 picture | the screen's own scale, fractional where needed, and its aspect (Hor+ or Vert+) |
| Front-end menus, pre-race screens, photo mode | at the render scale | entry 0 at the whole part k of the screen's scale, 16:9 (18.20, 18.25) |
| Look (`aces`) | mods, GTAO, TAA, the windshield | the same (look mods and GTAO target RTV and the native path) |
| `-Upscale` (FSR 3.1, TAAU) | refused | available |
| Frame pacing, memory, shader compilation | `launch_cvars` | the same `launch_cvars` |

What the screen's own scale gives per screen, against today (both launchers compute these; `launcher_tests` checks them on these monitors):

| Screen | RTV today | Native path |
|---|---|---|
| 1920x1080 | 2x, 2560x1440 averaged down | 3/2, 1920x1080 |
| 1920x1200 | 2x, 2560x1440, letterboxed | 3/2, 1920x1200, Vert+ 1280x800 |
| 2560x1440 | 2x, 2560x1440 | 2, 2560x1440 |
| 2560x1600 (this laptop) | 2x, 2560x1440, letterboxed | 2, 2560x1600, Vert+ 1280x800 |
| 2880x1800 | 3x, 3840x2160 averaged down, letterboxed | 9/4, 2880x1800, Vert+ 1280x800 |
| 3440x1440 | 2x, 2560x1440, pillarboxed | 2, 3440x1440, Hor+ 1720x720 |
| 3840x2160 | 3x, 3840x2160 | 3, 3840x2160 |
| 1280x800 | 1x, 1280x720, letterboxed | 1, 1280x800, Vert+ |

Integrated GPUs do not change: they already run the native path at 1x.

### 1.2 `-Quality` on the native path

`-Quality` sets the race render scenarios (`FM4_RACE_SETTINGS`). On the native path, as on RTV with TAA, its `TilingScenario` is left out and the other settings stay:

| `-Quality` | The title's tiling | RTV today (TAA) | Native path |
|---|---|---|---|
| `xbox` | 2x MSAA in two tiles (scenario 1) | one tile, no MSAA | one pass with 2x MSAA: the title's own sample count without the tiles |
| `high` (default) | 4x MSAA in three tiles (scenario 0) | one tile, no MSAA | one pass with 2x MSAA |
| `ultra` | as `high`, with every car at LOD 0 and the mirror every frame | one tile, no MSAA | one pass with 2x MSAA, with the same LOD and mirror settings |

No `-Quality` picks 4x on the native path: one pass with 4x MSAA is `-MsaaSamples 4`, chosen explicitly (d3d-hle-n3.md 16.17), and it keeps the whole multiple of 1280x720 and 16:9, since it has not run at a fractional scale.

### 1.3 Settings the native path does not cover

| Option | After the flip |
|---|---|
| `-Msaa` (the tiles and MSAA of `-Quality`) | the automatic choice stays RTV, as with integrated GPUs today; `-Native -Msaa` prints that the PM4 RTV path runs |
| `-Rov` | ROV; `-Native -Rov` is refused |
| `-Rtv` (the GUI's render path "RTV") | RTV, with today's whole scale and 16:9 |
| `-SingleTile`, `-MsaaSamples 0` | the native path in one tile without MSAA, at the whole multiple, 16:9 |
| `-MsaaSamples 4` | the native path in one pass with 4x MSAA, at the whole multiple, 16:9 |
| `-Supersample` | the native path at the whole part of the screen's scale plus one, 16:9 |
| `-ResolutionScale 1` to `4` | the native path at that whole scale, 16:9 |
| A look without TAA (`-Look "original,taa=0"`) | the native path in one pass with 2x MSAA, without TAA, where RTV renders the tiles and 4x MSAA of `high`. The reference comparison of AGENTS.md (RTV against `-Rov` with `original,taa=0`) then needs `-Rtv` in the launchers. `e2e.py` and `matrix.py` do not use the launchers' automatic choice (RTV unless `--native`), so the gates and their baselines are not affected |

## 2. Evidence on the development laptop

### 2.1 Runs

All runs are on `I:\FM4-build-usa` (USA release), one binary for both paths: main `46309c6` (TAAU), snapshot `snapshots\ndef`, at 240 Hz with the AI driver and the e2e harness's settings (a 1280x720 window, the presents of the UI thread). Each side runs the render cvars and the race tiling that the launchers plan for this laptop's panel (2560x1600, RTX 5070 Ti Laptop GPU):

- **RTV default** (today): render scale 2x (2560x1440, a 16:9 picture letterboxed on the panel), one tile without MSAA, TAA.
- **Native default** (after the flip): the native path's cvars, one pass with 2x MSAA, scale 2 through the scene draws' path with the logical size 1280x800 (Vert+, `native_draw_scale=2`, `native_draw_scale_variant=true`, `native_logical_height=800`, `native_rt_surfaces=58`): 2560x1600.
- **Native 2x** (for the GPU time at the same pixels as RTV): `-Native -ResolutionScale 2`, 2560x1440, 16:9.

| Runs | What |
|---|---|
| `nd-img-{rtv,nat}-*` | single runs with the 2x captures kept: `camera_race` (the race cameras: cockpit, hood, bumper, chase near and far), `photo` (photo mode and the save), `career` (the career menus, a race, its replay and the post-race screens), and Autovista (the main menu's AUTOVISTA, the first car, the showroom; steps given to `e2e.py --steps`, no scenario exists) |
| `ndsmk-rtv`, `ndsmk-nat` | the smoke tier |
| `ndprf-rtv`, `ndprf-nat`, `ndprf-nat2x` | the perf tier: `photo`, 3 runs each |
| `ndful-nat` | the full tier on the native default |
| `ndcar-rtv`, `ndcar-nat` | the career tier |
| `ndsmk60-rtv` | the smoke tier at 60 Hz on RTV, the baseline of the full tier's 60 Hz cells |

### 2.2 Images

Captures at render size (RTV 2560x1440, native 2560x1600; the frames of the two paths are different race moments, since races are not deterministic), judged full size and in 1:1 crops (kept outside the repository with the run folders):

| Scene | RTV default | Native default |
|---|---|---|
| Race, every camera | 16:9 | fills 16:10 (Vert+: more sky above and more of the bonnet and dashboard below), the HUD in the centred 16:9; nothing stretched |
| Cockpit (1:1: the gauges, the steering wheel, the A-pillar) | sharp | as sharp; the gauges' numerals and the wheel's stitching a little cleaner (2x MSAA under TAA) |
| Chase camera (1:1: the car's rear, the road lines) | sharp | the same detail |
| Replay (1:1: the pit building, the power lines, the cars) | sharp, the thin power lines unbroken | the same picture; the power lines unbroken |
| Photo mode (1:1: the car's front) | 16:9, at 2x, with the look's scene passes | the same detail at 2x (k = s on this panel), the frame 16:10 with the photo HUD in the centred 16:9; **no scene passes** (no GTAO, TAA, windshield): "a scene depth of 2560x1440, not at the scene's scale 2/1 and logical size 1280x800 (photo mode), runs no scene passes" |
| The saved photo (`photo_view`) | 1280x720 | 1280x720, correct |
| Front-end menus, the career home | 2560x1440 | 2560x1440 (entry 0 at k = 2), presented 16:9 letterboxed while the races fill the panel; the career home's car **without GTAO** (the AO under the bumper and in the fog lamp recesses is missing, as in photo mode) |
| Autovista (1:1: the Ferrari 458) | 2560x1440 with GTAO (the lower body and the wheel arch shaded) | 2560x1440, the same detail, **without GTAO** |
| Boot, logos, intro | 1280x720 legal screen, then 2560x1440 | 1280x720 legal screen, then 2560x1600 |

On this panel the whole part k of the scale is the scale itself (2), so the entry 0 scenes keep their sharpness; what they lose on the native path is the look's scene passes, because their scene depth is not at the logical size. On a 1080p screen (3/2, k = 1) and a 2880x1800 one (9/4, k = 2) the same scenes also render below the screen's scale and are scaled up (d3d-hle-n5.md 18.20), where RTV renders them at 2x and 3x and averages them down.

### 2.3 Performance

The perf tier, the race of `photo` at 240 Hz, 3 runs a side:

| Plan | Render | Race fps (runs) | Race p99 | GPU ms a race frame | Draws a frame | Render thread or CP busy a frame | Photo mode GPU ms |
|---|---|---|---|---|---|---|---|
| RTV default | 2560x1440 | 165.5, 165.9, 162.7 | 8.82, 9.55, 9.01 ms | 5.90, 5.81, 6.01 | 1,544, 1,480, 1,794 | 3.39 ms | 9.58, 9.61, 10.10 |
| Native default | 2560x1600 | 175.1, 173.0, 171.6 | 8.15, 8.14, 8.21 ms | 5.60, 5.69, 5.70 | 1,647, 1,783, 1,955 | 3.19 ms | 6.01, 6.18, 6.14 |
| Native 2x | 2560x1440 | 183.2, 183.0, 181.3 | 7.96, 7.67, 7.83 ms | 5.28, 5.30, 5.37 | 2,299, 2,196, 2,327 | 3.83 ms | 7.98, 7.93, 7.43 |

- At the same pixels (2560x1440) the native path takes 10% less GPU time a race frame than RTV (5.32 against 5.91 ms, means of the runs) with 2x MSAA, and races 11% faster (182.5 against 164.7 fps) with a p99 14% lower (7.82 against 9.13 ms).
- The native default renders 11% more pixels than RTV's letterboxed 2x and still takes 4% less GPU time (5.66 against 5.91 ms), races 5% faster (173.2 against 164.7 fps) and has a p99 10% lower (8.17 against 9.13 ms).
- Photo mode at 30 fps takes 6.1 ms on the native default against 9.8 ms on RTV, partly because it runs no scene passes there (2.2); native 2x, which runs them, takes 7.8 ms.
- All three sit below 240 fps: at 2x the GPU sets the frame rate. RTV's GPU time a race frame is in the range of N5's close gate (5.59 to 5.80 ms, d3d-hle-n5.md 18.18); native 2x is about 0.25 ms above that gate's 5.02 to 5.11 ms, with the looks of 26 October 2026 (the cockpit fill, the adaptation).
- `compare.py`: no FAIL; WARN on capture statistics, and on the guest's `wait_reg_mem` (193 to 233 ms/s), which the GPU-bound frame explains.
- Render target memory, from the earlier gates: the native path at this panel's plan 780.8 MB in a race (d3d-hle-n5.md 18.23), RTV at 2x 2,102.2 MB (18.18).

### 2.4 Coverage: the matrix's tiers with the native default

| Tier | Cells | Result |
|---|---|---|
| smoke, 240 Hz (`ndsmk-nat` against `ndsmk-rtv`) | `photo`, `photo_view`, `career` | 3 of 3 reached their goal on both paths. `career` PASS; `photo` and `photo_view` WARN on capture statistics only. Races 166.6 and 167.3 fps (p99 8.53 and 8.20 ms) against 160.1 and 154.7 (9.36 and 9.28 ms); the replay 114.4 against 106.8 fps. No new error key: both paths log the same three keys (`update:\media.zip`, an unimplemented XAM message, `BaseHeap::Release`) |
| full, 60 and 240 Hz, 3 runs (`ndful-nat`; 60 Hz against `ndsmk60-rtv`) | `photo`, `photo_view`, `career` | 6 of 6 cells and 18 of 18 runs reached their goal. At 60 Hz the race and the replay hold 60.0 fps (p99 17.2 to 17.3 ms, as RTV's 17.2 to 17.3), `career` PASS, `photo` and `photo_view` WARN on capture statistics only. At 240 Hz races 168.9 and 169.2 fps (p99 8.47 and 8.59 ms). No clear-gray or washed-out frames, no hitch over 50 ms in a race phase, the same error keys |
| career, 240 Hz (`ndcar-nat` against `ndcar-rtv`) | `career_finish`, `career_progress` | `career_finish` reached its goal on both (race 179.1 fps, p99 8.06 ms, against 153.2 fps and 11.48 ms); WARN on the frame latency wait (296.6 to 404.4 ms/s). `career_progress`: RTV reached its goal; the native default timed out at its last check in the gate's run and in two reruns on the same profile (`ndcar-nat-240-career_progress-r`). The progress is there: on the car screen OCR reads "Ford Ka, LVL2, 15,950 CR" in all three. The failure is the scenario's timing: its step 9 presses Up and A until LVL2 or LVL 1 shows, and step 10 waits 10 s for LVL2 again. The title then opens a CHALLENGE EVENT box (TopGear Bowling) over the car screen on both paths. On RTV, OCR misread the level at the first car screen ("197"), so step 9 kept pressing, closed the box and read LVL2 behind it. On the native path step 9 reads LVL2 at once, and step 10 sees only the box. Nothing points at the render path. Fixed in the scenario on 26 October 2026 (it waits for the box, closes it with A and reads the level on the event screen behind it): the career tier with 3 runs a side passes 2 of 2 cells on RTV (`ndcp3-rtv`) and on the native default (`ndcp3-nat`), `career_progress` PASS against RTV and `career_finish` WARN on the guest's waits only (race 173.0 against 156.3 fps, p99 8.29 against 9.93 ms) |

### 2.5 Known gaps of the native path

What the native path does not do yet that RTV does, or what has not run on it:

1. **The entry 0 scenes** (the boot screens, the front-end menus, the career home and the garage's car views, Autovista and photo mode; the title's tiling entry 0, 4x MSAA in three tiles; d3d-hle-n5.md 18.20 and 18.25):
   - At a logical size or a fractional scale they run **no scene passes** of the look: no GTAO, no TAA, no windshield. Seen here in photo mode, the career home and Autovista at 1280x800 (2.2). On a 16:9 screen at a whole scale (1440p, 4K) the scene depth matches the scene's scale and the passes run.
   - They render at the whole part k of the screen's scale and in 16:9. At 2 on this panel that keeps their sharpness, with a letterbox that comes and goes between the menus and the races. On a 1080p screen (3/2, k = 1) and a 2880x1800 one (9/4, k = 2) they render below the screen's scale and are scaled up, where RTV renders them at 2x and 3x and averages them down: softer than today.
   - The fix is N1's "entry 0 at s", in progress since 26 October 2026 (the main session routed this gap to it).
2. **One pass with 4x MSAA** (`-MsaaSamples 4`) keeps the whole scale and 16:9; it has not run at a fractional scale.
3. **Not run on the native path:** split screen, Autovista beyond its first screen (the tutorial's MOVE panel; the camera was not moved), DLC content (`dlc` scenario), 120 Hz, the soak test. Neither path has run on another GPU than the RTX 5070 Ti Laptop and the Radeon 890M: after the flip, an untested discrete GPU starts on the native path, with `-Rtv` as the way back.
4. **The reference recipe** of AGENTS.md (RTV against `-Rov` with `-Look "original,taa=0"`) needs `-Rtv` in the launchers after the flip (1.3). The e2e gates are not affected.

## 3. Recommendation

**Flip after "entry 0 at s"** (N1, in progress), with the launcher change prepared now so that it lands with or right after it.

For the flip:

- On this laptop the native default fills the panel (16:10 Vert+) and still costs less than today's letterboxed RTV: 4% less GPU time a race frame, 5% more fps and a 10% lower p99, with 2x MSAA under TAA; at the same pixels 10% less GPU time (2.3).
- Races, the cockpit, the chase cameras and replays look at least as good in 1:1 crops (2.2).
- The smoke and full tiers pass at 60 and 240 Hz with the same error keys as RTV, and `career_finish` reaches its rewards (2.4).
- RTV stays one switch away (`-Rtv`, the GUI's render path "RTV"), and `-Rov` is unchanged.

Against flipping now:

- The entry 0 scenes lose the look's scene passes at a logical size or a fractional scale: photo mode's live view, Autovista and the garage's car views lose GTAO and TAA on every screen that is not 16:9 at a whole scale (2.5, item 1). That is a visible step back from today's default on this laptop's own panel.
- On 1080p screens, the most common ones, the same scenes render at 1x and are scaled up, where RTV renders them at 2x.

Blockers, in order:

1. **Entry 0 at s** (N1): the menus, the garage, Autovista and photo mode at the screen's scale, with their scene passes at a logical size. Gate for the flip: these scenes at 1:1 against RTV at 1080p (3/2) and on this panel (2, 1280x800).
2. **The `career_progress` scenario** (2.4): done on 26 October 2026; the career tier passes on both paths.

Not blockers, listed in the known issues after the flip: one pass with 4x MSAA at the whole scale only, and split screen, DLC content and other GPUs not run on the native path.

If the main session prefers to flip now, the evidence supports it for races and replays, and the gaps of 2.5 go into [known issues](../known-issues.md) as a regression for photo mode, Autovista and the garage's car views until entry 0 at s lands.

## 4. The change after the decision

- `launch_plan` (`pipeline.py`) and `LaunchPlan` (`plan.cpp`): the automatic choice becomes the native path on every computer unless `-Rov` or `-Msaa`. The description's "native path for an integrated GPU" goes, since the native path is no longer special to them; `-Rtv` stays and so does `-Native`.
- The GUI's Advanced page: "Automatic (the native path)" on every computer.
- `launcher_tests` and `test_pipeline.py`: the expected plans of the automatic choice on the discrete GPU monitors.
- `running.md`, `known-issues.md`, `status/current.md`, and AGENTS.md's reference recipe if the main session wants `-Rtv` named there.
- Gates: the smoke tier on the native default, and the no-change proofs of RTV (`-Rtv`) and `-Rov` against this report's runs.
