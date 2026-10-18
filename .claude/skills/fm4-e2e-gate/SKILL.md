---
name: fm4-e2e-gate
description: Run and read FM4-PC e2e gates (matrix.py smoke, perf, full and career tiers, single e2e.py runs, compare.py verdicts). Use when a change to the runtime, the integration code, a look or a launcher setting needs verification on the built game, when asked to "run smoke", "run a gate", "measure fps", "compare with the baseline" or "check that it still works", and when reading existing gate results under <BuildRoot>\e2e.
---

# FM4 e2e gates

Use this skill to pick, run and interpret an e2e gate. The harness reference is [docs/e2e/README.md](../../../docs/e2e/README.md). Read it when this page does not answer a question; do not guess harness behavior.

## 1. Pick the tier by what the change can break

| Change | Tier | Time |
|---|---|---|
| Any change, first check | `smoke` (`photo`, `photo_view`, `career` at 240 Hz, 1 run) | about 10 min |
| Performance work (CP, GPU, pacing, draws) | `perf` (`photo` at 240 Hz, 3 runs), against a `perf` baseline | about 13 min |
| Before committing a stage | `full` (3 scenarios at 60 and 240 Hz, 3 runs); `--hz 60,120,240` adds 120 Hz | about 1 h |
| Career, saves, rewards, progress | `career` (`career_finish` and `career_progress` at 240 Hz) | about 8 min |
| DLC import or mounting | `--scenarios dlc` (needs the packs imported with `Build.ps1 -Content`) | about 4 min |
| How it looks | a single `e2e.py` run at 2x with `--keep-ppm` (section 5), not a gate | a few min |

Run `smoke` before measuring anything. Do not start with `full`.

## 2. Pre-flight

1. **BuildRoot.** There can be several (for example `I:\FM4-build2`, `I:\FM4-build-iter`). Use the one the user named or the one just built. If unsure, pick the BuildRoot with the newest `build-result.json` and say which one you used. Never run against a BuildRoot without `build-result.json`.
2. **The build is finished.** `matrix.py` snapshots `game-build` and refuses it when the artifacts differ from `build-result.json` ("finish the build first"). Rebuild with `Build.ps1 -Action build` instead of working around it. `--no-snapshot` is only for a quick check that never feeds a comparison.
3. **The machine is free.** No other `fm4_rex.exe` and no other gate may run: performance numbers need the machine to themselves. Check with `Get-Process fm4_rex -ErrorAction SilentlyContinue`. Run cells one at a time; never start two gates in parallel.
4. **A baseline exists.** A baseline is a gate of the same tier, Hz and scenarios, on the same BuildRoot and settings, without the change under test. List gate logs with `Get-ChildItem <BuildRoot>\e2e\*.log`; a gate log is `<name>.log`, a cell log is `<name>-<hz>-<scenario>.log`. If no suitable baseline exists, run one first: on the previous build, or on the same binary with the change switched off through `--arg` or `--env`.
5. **Settings match.** `matrix.py` has no `--look`, `--quality`, `--ao` or `--keep-ppm`. Every cell runs the launcher defaults: look `pc` (which turns AO and clearcoat on) and quality `high`, at 1x in a 1280x720 window. To change them for a whole gate, pass cvars with `--arg` and environment with `--env`. To repeat the settings of an older gate, read the `args` list in one of its `summary.json` files, for example a no-AO baseline used `--arg post_ao=false --arg "shader_mod_defines=..."` (since 18 October 2026 the pass is off with `--arg post_pass_path=`).

## 3. Run

```powershell
python outputs\FM4-build\e2e\matrix.py --build-root <BuildRoot> --name <name> --tier smoke --baseline <baseline name>
```

- Names are short and describe the change: `m4ao1x`, `disp2x`, `vrr30`. Add `2x` or `p` (perf) to the name when the gate uses them, so baselines stay easy to match. Reusing a name overwrites its cells and its snapshot.
- At 2x add `--arg draw_resolution_scale_x=2 --arg draw_resolution_scale_y=2`, and compare only with a 2x baseline.
- Every race is driven by the title's AI (`FM4_GAME_CMDLINE=engageaionstart`). `--human` drops it; do not use it for comparisons.
- Run gates in the background (Bash `run_in_background`) and wait for the completion notification. Do not poll and do not sleep in a loop. Tell the user the expected duration.
- Cells go to `<BuildRoot>\e2e\<name>-<hz>-<scenario>`. Never put results inside the repository and never commit them.

A single scenario without a gate (for a quick check or a visual run):

```powershell
python outputs\FM4-build\e2e\e2e.py --build-root <BuildRoot> --out <BuildRoot>\e2e\<name> --scenario photo --profile copy --env FM4_GAME_CMDLINE=engageaionstart --arg=--video_mode_refresh_rate=240 --env FM4_FRAME_RATE=240
```

`photo_view` needs the profile of a `photo` run (`--profile <BuildRoot>\e2e\<photo run>\user-data`), and `career_progress` the profile of a `career_finish` run. Run every other scenario on `--profile copy`, so the BuildRoot's own profile is never written.

## 4. Read the results

`<BuildRoot>\e2e\<name>.log` has one line per cell:

```
lookpc-240-photo: exit 0, 4.2 min, 1/1 fps 91.13 p99 33.71, race 227.16 fps p99 5.63, photo 29.96 fps p99 33.85 WARN vs vrr30-240-photo
```

That is: exit code, duration, runs that reached their goal out of runs, overall `fps_mean` and `frame_ms_p99`, then each phase, then the `compare.py` verdict against the baseline cell. For the full table run:

```powershell
python outputs\FM4-build\e2e\compare.py <BuildRoot>\e2e\<baseline cell> <BuildRoot>\e2e\<candidate cell>
```

How to judge a cell:

| Signal | Meaning | Action |
|---|---|---|
| `exit 1`, `0/1`, `goal_reached` false | the run crashed or a step timed out | read `failed_step` in `summary.json`, `steps.jsonl` and `final.png`, then `runtime.log` and `stderr.log`. Re-run the cell once to tell a menu-timing flake from a regression before reporting either |
| new error key (FAIL) | the runtime logged an error the baseline did not have | find the line in `runtime.log`; this is a real finding until explained |
| `clear_gray_frames` rose (WARN) | captures show the clear color (102,102,102): geometry such as the road was not drawn | treat as a regression; look at the frames it lists. See the RTV missing-road issue in [docs/status/current.md](../../../docs/status/current.md) |
| phase `race` fps down more than 10% or p99 up (FAIL/WARN) | a real performance change if it repeats | confirm with the `perf` tier (3 runs) and a baseline run bracketed before and after |
| overall fps FAIL only | the overall fps mixes the race with the 30 fps photo mode, replay or menus, so phase lengths move it | judge performance by the phases, not by the overall value |
| capture statistics WARN only (luma, black or saturated share, histogram) | races are not deterministic | look at the captures; on its own this is not a regression. `photo_view` often warns here |

Expected values on the current build: photo mode and replay run at 30 fps by design, the `photo_view` menu at about 57 fps, and the 240 Hz race phase at 1x at a little over 200 fps with a p99 of about 5-7 ms. The authoritative numbers are under "Current performance" in [docs/status/current.md](../../../docs/status/current.md); check them there before calling a value normal.

Noise: single runs vary by 10-20% in fps, and a hot laptop drifts (53.4 then 48.8 fps half an hour apart on the same binary). A performance claim needs `--repeat 3` (or the `perf` tier) and a baseline measured close in time. One smoke run proves that the scenarios still work, not that performance changed.

## 5. Visual checks

The PNG files in a cell's `capture\` directory are point-sampled previews at half the capture size. Never use them to judge how the game looks.

- Run `e2e.py` directly at 2x (`--arg draw_resolution_scale_x=2 --arg draw_resolution_scale_y=2`) with `--keep-ppm`, so the 2560x1440 PPM captures stay next to the previews.
- Convert the frames to judge at full size:

  ```powershell
  python -I -c "import sys; from pathlib import Path; sys.path[:0] = [r'outputs\FM4-build\e2e', r'outputs\FM4-build']; import e2e; [e2e.ppm_to_png(p, p.with_name(p.stem + '-full.png'), 1) for p in Path(sys.argv[1]).glob('frame-*.ppm')]" <BuildRoot>\e2e\<run>\capture
  ```

- Show 1:1 crops of the judged areas next to the full frame, and state each image's render resolution. Use 1x only for correctness, GPU timing and comparisons with a 1x baseline.
- Races are not deterministic: compare frames from the same phase and camera, or freeze a frame with rewind (`Y`) when the exact scene matters.

## 6. Report

Report to the user (in Indonesian, per [AGENTS.md](../../../AGENTS.md)):

- the BuildRoot, the gate name, the tier, the build commit, the settings that differ from the launcher defaults, and the baseline it was compared with;
- one row per cell: runs passed, the race and photo or replay phase fps and p99, the verdict, and the reason for every FAIL or WARN in your own reading (not only the `compare.py` text);
- what the tier does not cover (for example: `smoke` is one run, so it shows no performance change; nothing at 60 or 120 Hz; no visual check).

Do not update `docs/status/current.md` unless the user asks for it or the task is to record a verification. When you do, follow the documentation rules in [AGENTS.md](../../../AGENTS.md): move the superseded result into `docs/status/history.md` and keep one current statement.
