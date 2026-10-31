# Bug reports

How to report a problem with Motorsport DX so that it can be reproduced. Check
[known issues](known-issues.md) first: the problem may be known, or a part that has not been
tested yet, which a report is welcome for too.

Report problems on the repository's issue tracker, or in the channel that the preview's
invitation names. Use one report per problem.

## What to write

1. **What happened**, what you expected, and the steps that lead to it: the mode (Quick Race,
   career, photo mode, a replay), the car and the track, and how often it happens (every time,
   sometimes, once).
2. **When**, as the clock time of the run, so that it can be found in the logs.
3. **Your computer**: the processor, the GPU and its driver version, the memory, the screen's
   resolution and refresh rate, and the Windows version. `dxdiag` (Win+R, `dxdiag`, Save All
   Information) writes them all to a text file; send the parts named above.
4. **The game**: the release that `generate` printed (`Disc 1: USA release` or Japan/Asia),
   and whether Disc 2 and downloadable content are imported.
5. **The build**: the Motorsport DX commit (`git log -1 --format=%h` in the repository), whether it
   was built with `-Fsr on` or with `-Abi msvc` (`build-result.json` names the `abi`; the default
   since 1 November 2026 is llvm-mingw), and whether you run from the BuildRoot or from an export.
6. **The options**: the `Build.ps1 -Action run` command line, or in the GUI launcher the
   profile's settings (Advanced, **Show developer options**, then **Copy the command line** on the
   Developer tab). With `-Info` or F2 the settings overlay
   shows the render path, scale and look in the game.

## Which files to send

The logs are in `<BuildRoot>\logs`, or in the `logs` folder of an export. Each run of the
game replaces the two logs, so copy them right after the run that showed the problem.

| File | What it holds | When to send it |
| --- | --- | --- |
| `play.log` | the launcher's output and the game's console output | always |
| `runtime.log` | the runtime's log: the GPU, the display, the content mounts, warnings and errors | always |
| `perf-<profile>-<time>.jsonl` | the performance trace, one line a second: frame rate, frame times, draws, GPU time and CPU time per thread | for stutter, a low frame rate or a hang |
| `build-result.json` (in the BuildRoot or the export) | the hashes of the binaries and the source they were built from | when the build is not a plain commit, or on request |
| a screenshot or two | what is wrong on screen | for anything you can see |

The performance trace: in the GUI launcher, turn on **Show developer options** on the Advanced
page and then **Performance trace** on its Developer tab; each Play then writes its own `perf-<profile>-<time>.jsonl`. With `Build.ps1`, set the
file before the run:

```powershell
$env:REX_PERF_TRACE_FILE = 'D:\FM4-build\logs\perf.jsonl'
.\outputs\FM4-build\Build.ps1 -Action run @common
```

Let the problem happen at least once while the trace runs, then quit to the menu and close the
game, so the trace has the problem and its end.

**If the game crashes or closes**, also send what Windows recorded: open the Event Viewer
(Win+R, `eventvwr`), Windows Logs, Application, and copy the text of the `Application Error`
entry for `fm4_rex.exe` at that time (the faulting module and its offset). If the GUI launcher
shows an exit code, add it. If the game does not start at all and Windows names `libc++.dll` or
`libunwind.dll`, say whether the game's folder (`game-build`) holds them: `build` and `export`
put them beside the game.

Lines that every run writes and that are not problems on their own: in `runtime.log` the
error `ResolvePath(update:\media.zip) failed - device not found` (no title update is mounted), and
the `STUB` warnings of Xbox services the game calls but does not need; in the console of
`Build.ps1 -Abi msvc`, `'vswhere.exe' is not recognized`.

## Racing wheel

No wheel has been tested yet ([known issues](known-issues.md#tested-hardware)), so every wheel
report helps. Besides the files above, send:

| File or value | Where | What it holds |
| --- | --- | --- |
| `wheel-test.log` | the launcher folder's `logs` | the launcher's wheel test, with the device line `Wheel report:`: the wheel's name, vendor and product ID, axes, buttons, hats and force feedback effects |
| `wheel-test-forces.csv` | the launcher folder's `logs`, with **Record forces** on in the wheel test | every test force sent |
| the game's force recording | the file named by `--wheel_ffb_record=<full path>` in **Extra cvars** on the Advanced page's Developer tab, for example `<the launcher folder>\logs\wheel-forces.csv` | every force the game sent, with the game's torque, the wheel's angle, the lock and the end stop |
| the `Wheel` lines of `runtime.log` | `Select-String -Path logs\runtime.log -Pattern 'Wheel' \| ForEach-Object Line` | the device line, the mapping and the force settings the game ran with |
| the `-Wheel` value | the launcher's **Wheel value** field on the Racing wheel tab, shown with **Show developer options** on | every wheel setting |
| the wheel software's profile | Pit House (or the wheel's own software) | its maximum angle, force feedback strength, road sensitivity, damper, friction and inertia |

Name the wheel, the base and the pedals, and say which [checklist](running-controls.md#racing-wheel-test-checklist)
step shows the problem. The game replaces its recording at every run, like the logs, so copy it
right after the run.

## Privacy

- The logs name the folders of the BuildRoot, the export and your discs, which can contain
  your Windows user name. Replace it before you post a log publicly.
- Do not send `user-data` (your profile, saves and imported content), `cache` (the shader
  cache), game files, an export or a BuildRoot. None of them is needed, and they hold game
  data or code built from it.
- Screenshots: a few that show the problem are enough; do not attach captures of the game in
  bulk.
