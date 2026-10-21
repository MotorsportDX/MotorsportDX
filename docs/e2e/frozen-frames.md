# Frozen-frame parity

`compare.py` judges performance: it fails on a crash, a missed goal, fps more than 10% below the baseline, a p99 more than 20% above it, a new error key or a missing phase. Image statistics and draw counts only warn, because races are not deterministic. A wrong image or a dropped draw therefore passes it. The frozen-frame harness closes that gap for the stages of the [native port](../analysis/d3d-hle.md#10-validation-and-success-criteria): one scene, frozen by the title's rewind, rendered by two builds or two paths and compared pixel by pixel, with the draws and the predicated packets of that frame.

Parts:

- the `freeze` scenario of [`e2e.py`](../../outputs/FM4-build/e2e/e2e.py), which reaches the frozen frame and pins what would otherwise change the picture;
- [`frame_diff.py`](../../outputs/FM4-build/e2e/frame_diff.py), the pixel difference and its 1:1 crops;
- [`parity.py`](../../outputs/FM4-build/e2e/parity.py), the FAIL rules;
- the `parity` tier of [`matrix.py`](../../outputs/FM4-build/e2e/matrix.py);
- backend-neutral fields in `summary.json`.

## The freeze scenario

```
python outputs\FM4-build\e2e\e2e.py --build-root <BuildRoot> --out <BuildRoot>\e2e\<name> --scenario freeze --profile copy --env FM4_GAME_CMDLINE=engageaionstart --env FM4_FIXED_SEED=1
```

Steps: Quick Race with the default car and track (as `quickrace`, driven by the title's AI); from START RACE on, Y every capture until the rewind shows (`REWINDING`, `REWIND FURTHER`, `RESUME FROM HERE`), then Y four more times, which rewinds to the oldest frame the rewind keeps (REWIND FURTHER greys out there; further presses do nothing); then the phase `freeze`: 20 s of the frozen frame, about 10 captures. The title keeps rendering the frozen frame every frame under the rewind hint bar. The rewind keeps a window of about the last 10 s of the race and takes Y only after about 10 s of race (the presses before do nothing, and a held Y does not start it), so the oldest frame lies in the first seconds of the race. Quick Race also picks other opponents and grid slots each run; `--env FM4_FIXED_SEED=<n>` (the fixed seed of the race setup, [experiments](experiments.md)) pins them. What it does not pin is the moment: the test input follows the wall clock, not the guest frames, so the press that starts the rewind, and with it the oldest frame, moves by a fraction of a second from run to run (see [Baseline variance](#baseline-variance)).

Unless `--look` or `--arg` says otherwise, the scenario pins the settings below. A `--look` given for the run keeps the cvars it sets itself (a `sun` knob sets the awaited scale and limit); `--arg` overrides any of them.

| Setting | Value | Why |
| --- | --- | --- |
| Look | `original,taa=0` | the title's own picture: no shader mods, no post passes, no TAA, so the tiles and 4x MSAA of `--quality` (`high`); the reference look against `-Rov` (AGENTS.md) |
| `rtv_occlusion_debug` | `fake` | awaited occlusion intervals (the sun glare) report a fixed count instead of host query results, which arrive at different times |
| `query_occlusion_fake_sample_count` | `1000` | the fixed count |
| `rtv_occlusion_awaited_scale`, `rtv_occlusion_awaited_limit` | `-1`, `0` | no `sun` knob scaling |
| `draw_resolution_scale_x`, `_y` | `2`, `2` | 2560x1440 captures in the 1280x720 window |
| PPM captures | kept | `frame_diff.py` reads them; `--keep-ppm` is implied |

Variants:

- `-Rov`: `--arg render_target_path_d3d12=rov --arg rov_occlusion_async=all` (as `Build.ps1 -Rov`). The RTV occlusion knobs do not apply to it; it reports its counted samples.
- TAA pinned: `--look original` (TAA on, one tile without MSAA, as the launcher). A frozen frame is not jittered (the jitter runs only while the camera moves), and with TAA's still-camera phases consecutive captures of a rewind freeze changed in 0.00% of the pixels by more than 4 ([TAA](../analysis/taa.md)); check the run's `stability` all the same.
- Another binary: `--game-build snapshots\<name>` (a `matrix.py` snapshot), so a rebuild of `game-build` during the runs does not change the binary. `summary.json` `binary` records the path, the SHA-256 and the file time of the `fm4_rex.exe` that ran: a variance or a verdict holds for one binary.

`summary.json` maps each phase to its captures (`phase_frames`, by the file times of the captures) and to its perf windows (`phase_windows`); `--resummarize` recomputes the phases from `phase_windows`. Runs written before `phase_windows` keep their stored phases: their `timeline` counts from the launch, the perf windows' `t` from the start of the perf trace, a few seconds later. A run directory whose `phase_frames` lacks the phase is an error for `frame_diff.py` and a FAIL for `parity.py`; only a run written before `phase_frames` falls back to all its captures.

## frame_diff.py

```
python outputs\FM4-build\e2e\frame_diff.py <a> <b> [--out DIR] [--level 4] [--phase freeze] [--ignore x0,y0,x1,y1] [--crops 6] [--crop-size 256] [--json]
```

`<a>` and `<b>` are PPM captures or run directories. For a run directory the frame is the last capture of the phase, and the run's own stability is measured: its second capture of the phase against its last (the first can still show the rewind settling).

- A pixel's difference is the largest of its three channel differences (0 to 255). It counts as changed above `--level` (default 4, as the TAA still-picture checks count).
- The report: `above_pct` (the share of changed pixels), the mean and maximum difference, `above_pct_by_level` (above 1, 2, 4, 8, 16, 32 and 64), and `stability` per run.
- `--out` writes `frame-diff.json` and one PNG per crop: a, b and the difference side by side at 1:1 (gray: the difference times 8 below the level; red to yellow above it). Crops are centered on the 32x32 blocks with the most changed pixels and do not overlap.
- `--ignore` leaves out a region given as fractions of the frame, for example something that moves with wall time.

Standard library only; a 2560x1440 pair takes a few seconds.

## parity.py

```
python outputs\FM4-build\e2e\parity.py <baseline run> <candidate run> [--phase freeze] [--out DIR] [--json]
```

FAIL when:

- the candidate crashed, missed its goal or a phase, or has an error key the baseline does not have (as `compare.py`);
- in the frozen phase, the draws per frame (`phases.<phase>.draws_per_frame_mean`) differ from the baseline by more than `--draws` (default 2%);
- the predicated run or skipped packets per frame (`phases.<phase>.counts_per_frame_mean.predicated_run` and `.predicated_skipped`) differ by more than `--predicated` (default 2%);
- the last frozen captures differ in more than `--image-pct` (default 1%) of the pixels above `--level` (default 4);
- either run's own freeze did not hold: its captures differ in more than `--stable-pct` (default 0.05%).

The draws and the predicated packets are averaged over all frames of the phase (`draws_per_frame_mean`, `counts_per_frame_mean`). Per one-second window they alternate by about 0.5% with the number of frames in the window, because the half-rate mirror draws every other frame.

Performance stays with `compare.py`: a stage of the native port runs both. Repeated runs (`run1..runN`) are compared run by run. The draw and predication limits are about 2.4 times the largest difference between seeded baseline runs; the image limit is a target for a frozen moment pinned to the frame, which the harness cannot reach yet (see below), so until then two runs fail the image rule on the moment alone and the image share is read against the measured variance.

The `parity` tier of `matrix.py` runs `freeze` twice at 60 Hz and, with `--baseline`, appends the `parity.py` verdict to the cell's line (its reports and crops go to `<cell>\parity`):

```
python outputs\FM4-build\e2e\matrix.py --build-root <BuildRoot> --name <name> --tier parity --baseline <baseline name>
```

## Backend-neutral fields

The native path has no command-processor thread, so `cp_busy` does not describe it. `summary.json` (and each phase) has these fields when the perf trace provides them:

| Field | From the perf window | On the PM4 path |
| --- | --- | --- |
| `render_thread_busy_ms_per_frame_median` | `render_thread_busy_ms` | the command processor's busy time per frame; `render_thread_source` says `cp` (else `render_thread`) |
| `guest_render_thread_ms_per_frame_median` | `guest_render_ms` (the guest render thread's zone), else the D3D layer trace | with `--env FM4_N0_TRACE=<out>\n0.jsonl` ([traces](traces.md#d3d-layer-counters-fm4_n0_trace)): the median over its seconds of `render.ms` / `render.calls`, the time per call of the render frame `sub_822DB318` that presented a frame (the calls without a `Swap` are left out); `guest_render_thread_source` says `n0_trace` |
| `queue_depth_median`, `queue_depth_max` | `queue_depth_mean`, `queue_depth_max` | missing |
| `draws_per_frame_mean`, `counts_per_frame_mean` | `draws`, `counts` | the same |

The n0.jsonl seconds map to the phases by the steady clock (`summary.json` `phase_steady_ms`, QueryPerformanceCounter in milliseconds, the clock of `steady_ms`); a phase takes the seconds that lie inside it, and the run the steady part (the last `--measure` seconds, else all but the first third). The title appends to the file, so `summary.json` records where the run's lines start (`n0_trace_offset`) and each run of `--repeat` reads only its own. `parity.py` lists these fields; they never fail it. `compare.py` ignores them.

## Baseline variance

Measured on 21 October 2026 on `I:\FM4-build-pass` (the Japan/Asia data), with the fixed seed build of 21 October 2026 (`snapshots\n0seed`, `fm4_rex.exe` SHA-256 `27f90c57c8...`), at 60 Hz with the pins above and `FM4_FIXED_SEED=1`, the launcher defaults otherwise. The `fz2` runs used the present scenario (Y from START RACE, about every 2 s); `seed-a` and `seed-b`, an hour earlier, the first one (Y once `LAPS` or `PLACE` showed, about every 4 s).

| Pair | Rewind started at race time | Changed pixels above 4 | Mean difference | Draws per frame | Predicated run per frame | Predicated skipped per frame | Own freeze |
| --- | --- | --- | --- | --- | --- | --- | --- |
| RTV against RTV (`fz2-rtv-a`, `fz2-rtv-b`) | about 11 s in both | 25.47% | 11.1 | 3,270.47 against 3,270.92 (+0.01%) | 9,140.85 against 9,141.44 (+0.01%) | 9,930 against 9,930 | 0.00% in both |
| `-Rov` against `-Rov` (`fz2-rov-a`, `fz2-rov-b`) | about 11 s in both | 28.23% | 14.1 | 3,267.93 against 3,272.45 (+0.14%) | 9,125.95 against 9,138.32 (+0.14%) | 9,911 against 9,918 (+0.07%) | 0.00% in both |
| RTV against RTV (`seed-a`, `seed-b`) | after 12.4 s, after 10.3 s | 47.82% | 33.3 | 3,298.92 against 3,271.40 (-0.83%) | 9,179.44 against 9,142.06 (-0.41%) | 9,938 against 9,930 (-0.08%) | 0.00% in both |
| RTV against `-Rov` (`fz2-rtv-a`, `fz2-rov-a`) | about 11 s in both | 24.13% | 11.2 | 3,270.47 against 3,267.93 (-0.08%) | 9,140.85 against 9,125.95 (-0.16%) | 9,930 against 9,911 (-0.19%) | 0.00% in both |

The race times are the last race clock read before the rewind showed.

- Within a run the frozen frame is byte-identical from capture to capture on both paths: the rendering of a fixed scene is deterministic.
- With the seed the scene is the same: the same opponents, the same grid slot, the same stretch of the straight. The frozen moment is not: the oldest frame of the rewind moves with the press that started it, and with the cockpit camera that moves the whole picture. The image numbers above measure that moment, not the rendering, and they grow with the spread of the presses: 24 to 28% for the `fz2` runs, 47.82% for `seed-a` and `seed-b`, whose presses were about 2 s apart.
- The draws and the predicated packets of the frozen frame move with the moment too: up to 0.83% in draws and 0.41% in predicated run packets between seeded runs. RTV and `-Rov` at about the same moment agree within 0.2%. The limits of `parity.py` (2%) are about 2.4 times the largest difference; with the freeze pinned to a guest frame they can be measured again and tightened.
- The image limit (1% above 4) stays a target until the freeze is pinned to a guest frame. That needs the test input to press Y at a guest frame (the `FM4TestInput` driver in `work/fm4-rex/src`), or a hook that starts the rewind at a fixed race tick; the e2e harness alone cannot do it.
- The same scene on the USA data (`I:\FM4-build-usa`, `snapshots\n0main`, local main of 21 October 2026, `34662eb1...`, run `fz-n0` with `FM4_N0_TRACE`): 3,270.94 draws and 9,141.46 predicated run packets per frame, 21.81% of the pixels against `fz2-rtv-a`. Its guest render thread took 16.0 ms per presented frame in the frozen phase at 60 fps, nearly the whole frame interval: at 60 Hz the zone measures the frame pacing more than the work.
- Without the seed (on the build of `snapshots\n0val` on `I:\FM4-build-usa`, `7e50a9fc...`) two RTV runs differed in 54.92% of the pixels and by 7.3% in draws per frame: other opponents and another grid slot.
