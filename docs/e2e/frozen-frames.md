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
python outputs\FM4-build\e2e\e2e.py --build-root <BuildRoot> --out <BuildRoot>\e2e\<name> --scenario freeze --profile copy --env FM4_GAME_CMDLINE=engageaionstart
```

Steps: Quick Race with the default car and track (as `quickrace`, driven by the title's AI); as soon as the race HUD shows (`LAPS` or `PLACE`), Y until the rewind menu shows (`RESUME FROM HERE`, `REWIND FURTHER`; the first press is often ignored), then Y four more times, which rewinds to the oldest frame the rewind keeps (REWIND FURTHER greys out there; further presses do nothing); then the phase `freeze`: 20 s of the frozen frame, about 10 captures. The title keeps rendering the frozen frame every frame under the rewind hint bar.

Unless `--look` or `--arg` says otherwise, the scenario pins:

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

`summary.json` maps each phase to its captures (`phase_frames`, by the file times of the captures) and to its perf windows (`phase_windows`); `--resummarize` recomputes the phases from them (for older runs from the phase marks of `timeline`).

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
- in the frozen phase, the draws per frame differ from the baseline by more than `--draws` (default 2%);
- the predicated run or skipped packets per frame (`counts.predicated_run`, `counts.predicated_skipped`) differ by more than `--predicated` (default 2%);
- the last frozen captures differ in more than `--image-pct` (default 1%) of the pixels above `--level` (default 4);
- either run's own freeze did not hold: its captures differ in more than `--stable-pct` (default 0.05%).

The draws and the predicated packets are averaged over all frames of the phase (`draws_per_frame_mean`, `counts_per_frame_mean`). Per one-second window they alternate by about 0.5% with the number of frames in the window, because the half-rate mirror draws every other frame.

Performance stays with `compare.py`: a stage of the native port runs both. Repeated runs (`run1..runN`) are compared run by run. The defaults are provisional until the baseline variance below is measured on a deterministic scene.

The `parity` tier of `matrix.py` runs `freeze` twice at 60 Hz and, with `--baseline`, appends the `parity.py` verdict to the cell's line (its reports and crops go to `<cell>\parity`):

```
python outputs\FM4-build\e2e\matrix.py --build-root <BuildRoot> --name <name> --tier parity --baseline <baseline name>
```

## Backend-neutral fields

The native path has no command-processor thread, so `cp_busy` does not describe it. `summary.json` (and each phase) has these fields when the perf trace provides them:

| Field | From the perf window | On the PM4 path |
| --- | --- | --- |
| `render_thread_busy_ms_per_frame_median` | `render_thread_busy_ms` | the command processor's busy time per frame; `render_thread_source` says `cp` (else `render_thread`) |
| `guest_render_thread_ms_per_frame_median` | `guest_render_ms` (the guest render thread's zone) | the same, once the trace has it |
| `queue_depth_median`, `queue_depth_max` | `queue_depth_mean`, `queue_depth_max` | missing |
| `draws_per_frame_mean`, `counts_per_frame_mean` | `draws`, `counts` | the same |

`parity.py` lists them; they never fail it. `compare.py` ignores them.

## Baseline variance

Measured on 21 October 2026 on `I:\FM4-build-usa`, the build of 21 October 2026 (`snapshots\n0val`, `fm4_rex.exe` SHA-256 `7e50a9fc...`), at 60 Hz with the pins above.

| Pair | Changed pixels above 4 | Mean difference | Draws per frame | Predicated run / skipped per frame | Own freeze |
| --- | --- | --- | --- | --- | --- |
| RTV against RTV (`ffz-rtv-a`, `ffz-rtv-b`) | 54.92% | 46.5 | 3,241.08 against 3,005.03 (-7.3%) | 9,281.96 against 8,198.68 (-11.7%) / 10,464 against 8,758 (-16.3%) | 0.00% in both |

- Within a run the frozen frame is byte-identical from capture to capture, on RTV (`ffz-rtv-a`, `ffz-rtv-b`) and on `-Rov` (`ffz-rov-a`: 2,459.42 draws, 6,477.94 predicated run and 6,825 skipped packets per frame, 59.66 fps and 16.46 ms of GPU time per frame at 2x): the rendering of a fixed scene is deterministic on both paths.
- Between runs the scene is not the same. The rewind's oldest frame is the start, but Quick Race picks other opponent cars and puts the player in another grid slot each time, and the camera sits elsewhere. So these numbers measure the scene, not the rendering, and give no threshold yet.
- A fixed seed for the Quick Race setup is in progress in a separate session; with it the variance is measured again (RTV against RTV and `-Rov` against `-Rov`), and the threshold follows from it.
