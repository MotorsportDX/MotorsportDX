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

Steps: Quick Race with the default car and track (as `quickrace`, driven by the title's AI); from START RACE on, the input file asks for Y pulsed from race frame 720 (`~y@720`, 12 s at 60 fps): the diagnostic controller presses Y on that frame of the race and on every second frame after it ([diagnostic controller](architecture.md#diagnostic-controller-fm4_test_input_file)). The rewind starts on the first press, and the following presses rewind further, to the oldest frame the rewind keeps (REWIND FURTHER greys out there; further presses do nothing). The pulse stops 6 s after the rewind shows (`REWINDING`, `REWIND FURTHER`, `RESUME FROM HERE`); then the phase `freeze`: 20 s of the frozen frame, about 10 captures. The title keeps rendering the frozen frame every frame under the rewind hint bar. The rewind keeps a window of about the last 10 s of the race and takes Y only after about 10 s of race (the presses before do nothing, and a held Y does not start it), so the oldest frame lies in the first seconds of the race. Quick Race also picks other opponents and grid slots each run; `--env FM4_FIXED_SEED=<n>` (the fixed seed of the race setup, [experiments](experiments.md)) pins them. The race frame pins the press to the guest frame. It does not pin the moment: the race advances by wall time, not by frames, so the same race frame shows another moment in each run, and the oldest frame of the rewind moves by a fraction of a second (see [Baseline variance](#baseline-variance)).

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
- in the frozen phase, the draws per frame (`phases.<phase>.draws_per_frame_mean`) differ from the baseline by more than `--draws` (default 3%);
- the predicated run or skipped packets per frame (`phases.<phase>.counts_per_frame_mean.predicated_run` and `.predicated_skipped`) differ by more than `--predicated` (default 3%);
- the last frozen captures differ in more than `--image-pct` (default 60%) of the pixels above `--level` (default 4);
- either run's own freeze did not hold: its captures differ in more than `--stable-pct` (default 0.05%).

The draws and the predicated packets are averaged over all frames of the phase (`draws_per_frame_mean`, `counts_per_frame_mean`). Per one-second window they alternate by about 0.5% with the number of frames in the window, because the half-rate mirror draws every other frame.

Performance stays with `compare.py`: a stage of the native port runs both. Repeated runs (`run1..runN`) are compared run by run. The limits sit above the largest differences between seeded baseline runs, which come from the frozen moment, not the rendering ([Baseline variance](#baseline-variance)): the image rule catches a broken frame, and a wrong detail shows only in the image share and the crops read against that variance.

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

Measured on 21 October 2026 on `I:\FM4-build-usa` (the USA data), with the build of the pinned press (`snapshots\fzpin`, `fm4_rex.exe` SHA-256 `b206aa2ae5548149ff7462c54f448040d7f976ae3614803e4bcc6abc670477cd`), at 60 Hz with the pins above, `FM4_FIXED_SEED=1` and `FM4_VBLANK_TRACE=1`: two RTV runs with the launcher defaults otherwise (`fzpin-rtv-a`, `-b`) and two `-Rov` runs (`fzpin-rov-a`, `-b`). In all four the first Y press fell on race frame 720.

| Pair | Changed pixels above 4 | Mean difference | Draws per frame | Predicated run per frame | Predicated skipped per frame | Own freeze |
| --- | --- | --- | --- | --- | --- | --- |
| RTV against RTV | 33.44% | 19.8 | 3,311.42 against 3,274.44 (-1.1%) | 9,274.07 against 9,153.09 (-1.3%) | 10,096 against 9,940 (-1.5%) | 0.00% in both |
| `-Rov` against `-Rov` | 21.76% | 8.9 | 3,273.93 against 3,273.93 (0.0%) | 9,148.45 against 9,148.45 (0.0%) | 9,932 against 9,932 (0.0%) | 0.00% in both |
| RTV against `-Rov` (`-a`) | 33.66% | 20.1 | 3,311.42 against 3,273.93 (-1.1%) | 9,274.07 against 9,148.45 (-1.4%) | 10,096 against 9,932 (-1.6%) | 0.00% in both |
| RTV against `-Rov` (`-b`) | 19.12% | 6.5 | 3,274.44 against 3,273.93 (0.0%) | 9,153.09 against 9,148.45 (-0.1%) | 9,940 against 9,932 (-0.1%) | 0.00% in both |

- Within a run the frozen frame is byte-identical from capture to capture on both paths: the rendering of a fixed scene is deterministic.
- The press is pinned, the moment is not. The race render scenario (`12-Car_Race`) was pushed after 8,035 to 8,233 pad polls, and the first Y press came exactly 720 polls later in every run. In a race the title polls the pad once per frame, on one thread (1,055 polls against 1,055 swaps in the traced run `fzp-diag-b`; twice per presented frame in the countdown). Yet the oldest frame of the rewind still lies a fraction of a second apart: the 1:1 crops show the same cars and the same place, a little further along or behind. The race advances by the elapsed wall time, not by a fixed step per frame (it runs at the right speed at any `FM4_FRAME_RATE`), so race frame 720 is a different moment of the race in each run, and so is the oldest frame 10 s before it. The image numbers above measure that moment, not the rendering.
- The draws and the predicated packets of the frozen frame follow the moment too: up to 1.6% apart. Where the counts match (`fzpin-rtv-b` and the `-Rov` pair) they agree within 0.1%, also between RTV and `-Rov`, though those frames still differ in 19 to 22% of the pixels.
- The limits of `parity.py` follow from this measurement and the earlier one: 3% for draws and predicated packets (about twice the 1.6% above), 60% for the image (above the 47.82% of the earlier `seed-a` and `seed-b`, whose rewinds started about 2 s apart in race time, 12.4 s and 10.3 s; 33.66% at most with the pinned press). At 60% the image rule catches a broken frame (black, the clear gray, most geometry missing), not a wrong detail; read the image share and the crops against this table. The 1% image target needs a frozen moment pinned to the race's time ([below](#what-pinning-the-moment-needs)).
- The earlier measurement with presses on the wall clock (21 October 2026, Japan/Asia data: 24 to 48% of the pixels, up to 0.83% in draws) is in [history](../status/history.md#21-october-2026-frozen-frame-variance-with-presses-on-the-wall-clock).

### What pinning the moment needs

Checked on 21 October 2026 on `I:\FM4-build-usa`:

- Y pulsed on every second poll from the wall-clock start of the pulse (`fzp-rtv-a`, `-b`): 28.57% of the pixels above 4. Y anchored to race frame 720 (`fzr-rtv-a`, `-b`, and the runs above): 32.57%, though the race push and the press fell on the same polls in both runs (push at poll 8,217, press at 8,937). Pressing on a fixed frame does not help, because the frames are not fixed in race time.
- The title's QueryPerformanceCounter (`sub_822D92D8`, the timebase into `*r3`) runs about 730 times per frame on its main thread (run `fzprobe-a`, with a probe of the timebase readers that is not committed), and 35 functions read the timebase directly (`mftb`, `REX_QUERY_TIMEBASE` in the generated code). A trial that made that thread's QueryPerformanceCounter advance by exactly 1/60 s per race frame (`fzc-rtv-a`, `-b`) did not pin the moment (38.58%), so it is not the race's only clock, and it changed the pacing: two swaps per pad poll, and the rewind no longer reached its oldest frame within the scenario. The trial is not kept.
- A pinned moment therefore needs the title's own race clock: the code that advances the race time per frame (fixed to 1/60 s in a test mode), or a hook that starts the rewind at a fixed race time, which pins the moment if the physics runs in fixed steps. Neither is found yet.
