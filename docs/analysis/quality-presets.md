# Five quality presets

Status: measured and implemented, 26 October 2026. The launchers' race quality (`-Quality`, the
GUI launcher's "Race quality") has five tiers of detail, `low`, `medium`, `high`, `extrahigh`
and `ultra`, in place of `xbox`, `high` and `ultra`; `xbox` is an alias of `medium`. The
project owner asked for the five tiers on 26 October 2026; the main session decided that the
tiers cover detail only, so the render scale, `-Upscale` and the look stay separate options.
Usage: [running.md](../running.md#-quality).

## 1. The tiers

| Tier | Race render scenarios (`FM4_RACE_SETTINGS`) | GTAO_QUALITY | Anisotropic | MSAA on the native path |
|---|---|---|---|---|
| `low` | `SoftParticles=1`, `MaxCarsInMirror=3`, `CollidablesInMirror=0` | 0 (2x2) | 16x | 2x |
| `medium` (`xbox`) | `SoftParticles=1` | 1 (2x4) | 16x | 2x |
| `high` (default) | `high` as before: `TilingScenario=0`, soft particles, collidable shadows, `CarMinWheelLOD=0`, car LOD distances 30/50/100 m, the 16-car cuts undone | 2 (3x6, passes.toml's own) | 16x | 2x |
| `extrahigh` | `high` and `CarKeepLOD0=1` | 3 (4x8) | 16x | 2x |
| `ultra` | `extrahigh` and `HalfRateMirror=0`, `MaxCarsInMirror=11` | 3 (4x8) | 16x | 4x where the native plan is a whole scale at 16:9 without `-Upscale`, otherwise 2x |

- Every tier has `SoftParticles=1`, because without it the title resolves no scene depth in races
  (section 3.1).
- The launchers add `GTAO_QUALITY` to `shader_mod_defines` where the post passes run and the
  tier's value differs from passes.toml's 2 (`quality_cvars`), so `high` runs exactly as before.
- `ultra`'s 4x: one pass with 4x MSAA has no fractional scale, logical size or upscaling yet
  ([d3d-hle-n3.md 16.17](d3d-hle-n3.md)). The launchers take 4x only where the display plan of
  2x (the screen's own scale) and that of 4x (the whole scale) are the same, with `-Upscale
  native` (`quality_msaa_samples`): 1440p and 4K screens, `-ResolutionScale 1` to `4`, and
  integrated GPUs at 1x. 1080p (3/2), 16:10 and 21:9 screens (Vert+ and Hor+),
  `-Supersample` at 1080p, `-ResolutionScale panel` and every `-Upscale` mode keep 2x.
  `-MsaaSamples`, `-SingleTile`, `-Msaa` and `-Rov` keep their own. The plan description and the
  settings overlay name the choice ("Ultra: 4x MSAA", or "2x MSAA (4x needs a whole scale at
  16:9 without -Upscale)").
- Anisotropic filtering stays at 16x in every tier (the launchers' `anisotropic_override=5`).
- On RTV with TAA races render one tile without MSAA at every tier; with `-Msaa`, `-Rov` or a look
  without TAA `low` and `medium` keep the title's two tiles with 2x MSAA and `high` and up the
  three with 4x.

## 2. Method

- `I:\FM4-build-usa`, the binary snapshot `snapshots\qpre` (head 6c1b239, whose runtime is that
  of main a570a9b), `e2e.py` of main 15f828c with `--entry0 k` (the snapshot predates entry 0 at
  s; races are not entry 0).
- The `race` scenario (the race phase of `photo`: a 12-car Quick Race on the Bernese Alps, 30 s
  measured) on the fast route, the title's AI driving, `FM4_FIXED_SEED=1`, 240 Hz, the look
  `aces`.
- The native path in one pass with 2x MSAA unless noted, at 2x (2560x1440, 16:9) and 1x.
- GPU time a race frame is the race phase's `gpu_frame_ms_median`, the pass times its
  `gpu_passes_ms_median`, the render thread its `render_thread_busy_ms_per_frame_median`.
- Race configurations ran 3 to 5 times in rounds, single knobs once and three times where the
  difference was small.
- Pictures: the `freeze` scenario at 2x (the cockpit camera rewound to a frozen race frame, 60
  Hz, the look `aces`), judged in 1:1 crops of the 2560x1440 captures. The frozen moment
  differs a little between runs.
- Run folders: `I:\FM4-build-usa\e2e\qp-*` (races), `qpv-*` (frozen frames), `qpr-*` (RTV).

## 3. Findings

### 3.1 `xbox` races ran without the look's scene passes

The 12-car race scenario (`12-Car_Race.xml`) has no `SoftParticles`, and neither do the other
N-Car race scenarios. Without soft particles the title resolves no scene depth in a race, so
the post passes never find their scene depth (`scene_depth` point) and none of the look's
scene passes run: no GTAO, TAA, motion vectors or windshield. The countdown scenario
(`12-Car_321_and_PostRace.xml`) has `SoftParticles=1`: the per-second trace of the `xbox` run
shows GTAO at 0.61 to 0.75 ms during the countdown and 0 from the race scenario's push on.

| Run (2x, 240 Hz) | GTAO in the race | GPU ms a race frame | fps |
|---|---|---|---|
| native, `xbox` (`qp-xbox-2x-r1`) | 0 | 3.36 | 231.9 |
| RTV, `xbox` (`qpr-xbox-2x-r1`) | 0 | 3.94 | 207.3 |
| native, `xbox` with `SoftParticles=1` (3 runs) | 0.71 | 4.97 | 196.6 |

In a 1:1 crop of the frozen frame the cockpit's view through the windshield is stair-stepped
at `xbox` (no TAA, the pylon's lattice) and smooth with soft particles. `high` and `ultra` have
always had soft particles. Every tier now has them, so `medium` is the title's own settings
plus soft particles.

### 3.2 The 16-car car shader paints the player's car white

`CarShader=shaders_16car` (the title's setting for 16-car races, whose body paint shader
`4A15688ECE9E3657` reflects a static cube, [look mod coverage](look-mod-coverage.md)) turns
the red hood of the player's car, seen from the cockpit, pink to white, and darkens the wagon
ahead. It does so on the native path without look mods (`original`), on RTV with `aces` and
on `-Rov` with `original,taa=0`, in the same frozen frame. `-Rov` shares the shader
translation with the other paths, so this does not tell whether the Xbox 360 shows it too.
The title uses this shader in 16-car races at `low` and `medium` (their own settings), as it
did at `xbox`; `high` and up replace it. Not fixed here; it is listed in the
[known issues](../known-issues.md).

The `-Rov` frozen frame of that run (2x, the 16-car shader) also lost the road surface; the
`-Rov` frozen frames of other sessions at 1x draw it. Not investigated.

## 4. Races at 2x on the native path

| Configuration | Runs | GPU ms | fps | p99 ms | Draws a frame | Render thread ms |
|---|---|---|---|---|---|---|
| `xbox` (no scene passes, 3.1) | 1 | 3.36 | 231.9 | 5.31 | 1,902 | 3.28 |
| `medium` race settings | 3 | 4.97 | 196.6 | 7.08 | 1,932 | 3.28 |
| `high` | 5 | 5.10 (median 5.04) | 191.7 | 7.42 | 1,966 | 3.47 |
| `high` and `CarKeepLOD0` | 3 | 5.07 | 193.5 | 7.34 | 1,958 | 3.41 |
| `ultra` race settings | 3 | 5.39 | 181.2 | 7.35 | 2,913 | 4.80 |
| `low` race settings | 3 | 4.84 | 202.6 | 6.55 | 1,566 | 2.83 |
| first `low` candidate with the 16-car shader (3.2) | 3 | 4.65 | 210.4 | 6.23 | 1,567 | 2.65 |

The first `low` candidate also had the title's other cuts: car LOD distances of 10/17/35 m,
crowd LOD counts and sprite distance 0.5, no crowd shadows, a particle rate of 0.5 and
half-rate bloom. Left out one at a time (one run each, GPU ms / draws / render thread ms),
only the mirror cuts saved anything:

| Without | GPU ms | Draws | Render thread ms |
|---|---|---|---|
| (all of them) | 4.84 | 1,566 | 2.83 |
| the mirror cuts | 4.98 | 1,802 | 3.26 |
| the crowd cuts | 4.85 | 1,618 | 2.95 |
| the LOD distances | 4.83 | 1,591 | 2.87 |
| half-rate bloom | 4.81 | 1,547 | 2.80 |
| the particle rate | 4.82 | 1,573 | 2.87 |

So `low` keeps the mirror cuts only. In 1:1 crops its mirror shows 3 cars where `medium`'s
shows 4, and the crowd looks the same.

## 5. Knobs on top of `high` at 2x

| Knob | Runs | GPU ms | Pass ms | Frozen frame GPU ms |
|---|---|---|---|---|
| `GTAO_QUALITY` 0 | 1 | 4.60 | 0.31 | 4.83 |
| `GTAO_QUALITY` 1 | 1 | 4.69 | 0.40 | 4.84 |
| `GTAO_QUALITY` 2 (`high`) | 5 | 5.10 (median 5.04) | 0.71 | 5.23 |
| `GTAO_QUALITY` 3 | 1 | 5.37 | 1.04 | 5.52 |
| one tile without MSAA | 1 | 5.04 | | 5.07 |
| one pass with 4x MSAA | 1 | 5.29 | | 5.43 |
| anisotropic: the game's own | 3 | 4.93 | | 5.05 |
| anisotropic 4x | 3 | 4.95 | | 5.17 |
| anisotropic 8x | 3 | 4.97 | | |
| anisotropic 16x (`high`) | 5 | 5.10 (median 5.04) | | 5.23 |

- GTAO's own passes take 0.31 to 1.04 ms; in 1:1 crops of the frozen frame the four
  qualities look nearly alike, quality 0 a little darker and blotchier in the folds of the
  driver's gloves.
- 4x MSAA costs 0.2 to 0.25 ms against 2x, and one tile without MSAA saves nothing measurable.
  In the frozen frame the three look the same (TAA accumulates a still camera's phases);
  motion was not judged. On the native path one tile and 4x MSAA lose the fractional scale,
  the logical size and `-Upscale`, so the tiers keep 2x and only `ultra` takes 4x where it
  keeps the picture.
- 16x anisotropic filtering costs at most about 0.1 ms against 4x, within the run-to-run
  range of `high` (4.97 to 5.31 ms). The game's own filtering (mostly none) blurs the asphalt
  at grazing angles. All tiers keep 16x.
- `CarKeepLOD0` costs nothing measurable here (draws and GPU time as `high`). Its cost is
  geometry, which this GPU hides at 2x; it starts at `extrahigh` until the ROG Xbox Ally X
  measures it.

## 6. Races at 1x and on RTV

At 1x on the native path the race runs close to the 240 fps cap and its frame is set by the
render thread more than by the GPU (one run each):

| Configuration | fps | GPU ms | Draws a frame | Render thread ms |
|---|---|---|---|---|
| first `low` candidate (with the 16-car shader) | 239.4 | 2.09 | 1,586 | 2.40 |
| `medium` race settings | 235.6 | 2.29 | 1,843 | 2.91 |
| `high` | 231.6 | 2.31 | 1,856 | 3.24 |
| `high` and `CarKeepLOD0` | 232.6 | 2.31 | 1,895 | 3.23 |
| `ultra`, the whole tier (GTAO 3, 4x MSAA) | 219.6 | 2.74 | 2,931 | 4.38 |
| `high` with `GTAO_QUALITY` 0 | 236.2 | 2.19 (GTAO 0.08, at 2: 0.18) | 1,947 | 3.06 |
| `high`, one tile without MSAA | 236.5 | 2.24 | 1,880 | 2.95 |
| `high`, one pass with 4x MSAA | 236.5 | 2.38 | 1,868 | 2.91 |

RTV at 2x (one tile without MSAA with TAA, one run each; the render thread is the command
processor):

| Configuration | fps | GPU ms | Draws a frame | Render thread ms |
|---|---|---|---|---|
| `xbox` (no scene passes, 3.1) | 207.3 | 3.94 | 1,903 | 4.01 |
| `medium` race settings | 163.9 | 6.00 | 1,864 | 4.26 |
| `high` | 163.1 | 5.99 | 1,856 | 4.19 |
| first `low` candidate (with the 16-car shader) | 172.4 | 5.68 | 1,579 | 3.52 |
| `ultra` race settings with `GTAO_QUALITY` 3 | 139.3 | 6.92 | 2,912 | 6.39 |

On RTV `ultra`'s mirror costs the command processor thread more than the native path's render
thread (6.39 against 4.80 ms).

## 7. The tiers measured whole

On the game build of main `15f828c` (snapshot `qp15`), `e2e.py --quality <tier>` with the same
settings as section 2 and entry 0 at s, three runs a tier at 2x and one at 1x:

| Tier | GPU ms at 2x (runs) | fps at 2x | p99 ms | Draws a frame | Render thread ms | GPU ms at 1x | fps at 1x |
|---|---|---|---|---|---|---|---|
| `low` | 4.49 (4.55, 4.46, 4.45) | 218.3 | 6.02 | 1,640 | 2.80 | 2.04 | 239.4 |
| `medium` | 4.65 (4.63, 4.68, 4.65) | 210.1 | 6.42 | 1,927 | 3.24 | 2.11 | 238.4 |
| `high` | 4.98 (4.98, 5.00, 4.95) | 197.0 | 6.94 | 1,917 | 3.25 | 2.18 | 238.0 |
| `extrahigh` | 5.30 (5.31, 5.30, 5.29) | 184.6 | 7.52 | 1,961 | 3.30 | 2.26 | 238.7 |
| `ultra` (4x MSAA) | 6.05 (6.02, 6.00, 6.12) | 162.0 | 7.70 | 2,933 | 4.79 | 2.65 | 230.5 |

On a discrete GPU the tiers differ by 1.56 ms a race frame at 2x (35%), and at 1x, where the
race sits near the 240 fps cap, by 0.6 ms of GPU and 9 fps. The render scale and `-Upscale`
change the GPU time far more ([FSR plan](fsr-plan.md)). The sums of section 5's knobs on the
race settings of section 4 (4.44, 4.66, 5.04, 5.40 and 5.97 ms) came within 0.1 ms of these.

## 8. Not covered

- The ROG Xbox Ally X (the Radeon 890M): the export for the owner's run measures the tiers
  there, `extrahigh` against `high` for `CarKeepLOD0` and the anisotropic filtering's cost.
- 60 and 120 Hz, other tracks, 8- and 16-car races, split screen.
- Motion: the pictures are of a frozen frame.
