# Agent instructions

These rules apply to every AI agent working in this repository.

## Language

- **Chat replies** to the user are written in **Indonesian** (Bahasa Indonesia).
- **Everything written into the repository** is written in **English**: code, identifiers, code comments, log and error messages, commit messages, pull request titles and bodies, and all documentation (`README.md`, `docs/`, and any other `.md` or `.txt` file).
- Quoted game output, file contents, and user-supplied data keep their original language.

## Documentation layout

- `README.md` is the landing page: what the project is, supported versions, quick start, and an index of the other docs. Keep it short.
- Detailed docs live under `docs/`:
  - `docs/setup.md`: prerequisites, setup, generate, build, troubleshooting.
  - `docs/running.md`: launcher usage, options, look knobs, controls.
  - `docs/status/current.md`: the present state, verified results on the latest build, open gaps, next validation steps.
  - `docs/status/history.md`: dated results and past measurements.
  - `docs/known-issues.md` and `docs/bug-reports.md`: for players, the known problems and tested hardware in plain terms, and how to report a bug. Keep them in step with `docs/status/current.md`.
  - `docs/e2e/`: e2e harness guide, trace variables, experiment switches.
  - `docs/analysis/`: research and design notes.
  - `docs/sources-and-licensing.md`: source inventory, third-party notices, review rules.
- When verification results change, update `docs/status/current.md` and move the superseded result into `docs/status/history.md`. Do not leave two conflicting "current" statements.
- Do not link to `PLAN.txt`. It is a local planning file and is not part of the repository.
- Write dates in full (`16 October 2026`). Write decimals with a point (`0.6`) and thousands with a comma (`50,233`).
- Keep docs factual and in the present tense unless they describe history.

## Render target paths

- Shader mods (`work/fm4-rex/look`, the shader override path) and the GTAO ambient occlusion target the host render target paths with bindless resources only: RTV, and the native path (the D3D-level HLE with host render targets planned in [docs/analysis/d3d-hle.md](docs/analysis/d3d-hle.md); the project owner's decision of 21 October 2026). Do not port them to the ROV path (`-Rov`, `render_target_path_d3d12=rov`), and do not add title-specific corrections to it (such as the removed occlusion sample divisor).
- ROV is the uncorrected reference for the EDRAM emulation: it renders the title's own output. That `-Rov` runs without mods and AO is by design, not an implementation gap. Compare RTV against `-Rov` with `-Look "original,taa=0"` (no mods, no TAA, and the tiles and MSAA of `-Quality`; `-Look original` has TAA and one tile).
- The native path is compared the same way, against `-Rov` and against RTV with `-Look "original,taa=0"`. It never replaces `-Rov` as the reference, and the PM4 RTV path stays selectable beside it.

## Repository rules

- `.gitignore` denies everything by default. A new source or doc file is ignored until it is whitelisted in `.gitignore`. Source files also need a review entry in `outputs/FM4-source-control/tracked-files.json`.
- Run `outputs/FM4-source-control/check-staged-content.py` before committing.
- Do not commit game data, executables, DLLs, `BuildRoot` contents, captures, saves, or logs.
- Do not commit or push unless the user asks for it.
