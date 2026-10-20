# Experiments, A/B switches and shader overrides

This page covers the switches that exist for experiments: A/B comparisons of one binary, workarounds, shader overrides and the look-mod diagnostics. None of them is needed for a normal run. Run results with a switch on are for comparison against a baseline gate, not for a release. Derived game shaders are kept in the BuildRoot, never in the repository.

Diagnostic output (files that record what happened) is on the [traces page](traces.md). The commands that run the harness are on the [harness overview](README.md). Launcher options and look knobs for players are in [running.md](../running.md).

## A/B switches and workarounds

Pass them with `--arg NAME=value` (runtime cvars) or `--env NAME=value` (environment variables).

| Switch | Effect |
| --- | --- |
| `--arg=--readback_resolve=fast` | A/B of the resolve readback cvar on the same binary. The launcher uses `fast`. |
| `--arg=--rtv_uniform_draws=false` | Turns off the clear handling of the RTV path (transfer cutouts and 4x MSAA clear remapping), for comparisons. Default `true`. |
| `--env FM4_HALF_RATE_MIRROR=0` | Renders the rear-view mirror every frame in every race, also the 12 car Quick Race. The launcher's `1` renders it every other frame only in the 12 and 16 car races. |
| `--env FM4_SHADOW_WORKAROUND=1` | Skips the shadow-map pass outside the cockpit in races (`Build.ps1 -ShadowWorkaround`). The workaround is off by default. |
| `--env FM4_TILING_SCENARIO=<n>` | Replaces the TilingScenario value 0 or 1 of the race scenarios. The title uses 1 (two predicated tiles with 2x MSAA) for 8 to 16 car races and 0 (more tiles) for pre-race, replays and 1 car races. 2 is the title's UI value: one 720p tile without MSAA. Split screen (4) and photo mode keep their own values. |
| `--env FM4_STREAMING_FALLBACK=0` | Turns off the streaming fix, which is always on by default, for comparisons. |
| `--arg d3d12_submit_draw_interval=<n>` | With the submit thread, ends the submission before a draw once it holds `n` draws, so the submit thread replays the frame while it is recorded and the swap waits only for the last part. Default `0` (off). Measured in [status/current.md](../status/current.md#current-performance). |
| `--arg rtv_occlusion_debug=noqueries` | RTV occlusion snapshots report 0 samples without host queries. |
| `--arg rtv_occlusion_debug=fake` | Host queries still run, but awaited intervals report N samples, with N from `query_occlusion_fake_sample_count`. |

Source: [`fm4-render-overrides.cpp`](../../work/fm4-rex/src/fm4-render-overrides.cpp), [`command_processor.cpp`](../../work/runtime/src/graphics/d3d12/command_processor.cpp) (the RTV cvars) and [`fm4-streaming-fixes.cpp`](../../work/fm4-rex/src/fm4-streaming-fixes.cpp).

### Frame rate and vblank experiments

These go with the frame rate options on the [harness overview](README.md#frame-rate-and-launcher-settings) (`--arg=--video_mode_refresh_rate=<n>`, `--env FM4_FRAME_RATE=<n>`).

| Switch | Effect |
| --- | --- |
| `--env FM4_VBLANK_SWITCH=0` | Keeps the guest vblank at the reported refresh rate, even in race scenarios. |
| `--env FM4_VBLANK_TRACE=1` | Prints each render scenario push and pop with the vblank rate. |
| `--env FM4_VRR_HALF_RATE=1` | Keeps `vblank_vrr` in the 30 fps scenarios. By default they are held to the fixed grid. |
| `--arg=--vblank_rate=<hz>` | Sets a fixed startup vblank rate, without `FM4_FRAME_RATE`. |

Source: [`fm4-vblank-rate.cpp`](../../work/fm4-rex/src/fm4-vblank-rate.cpp).

## Developer command line options

FM4 keeps its developer command line parameters, but the retail build never fills them (see [status/current.md](../status/current.md)). `--env FM4_GAME_CMDLINE="<option> <option>=<value>"` feeds them through the title's own parser ([`fm4-command-line.cpp`](../../work/fm4-rex/src/fm4-command-line.cpp)) after its defaults. A bare name means `name=1`.

Options whose code the retail build no longer has are rebuilt in [`fm4-dev-features.cpp`](../../work/fm4-rex/src/fm4-dev-features.cpp):

- `engageaionstart`: an AI driver drives the player's car from the start. Pause, photo mode, QUIT RACE, replays and CONTINUE work as usual. `e2e.py` then sends no driving input: throttle, brake and steering steps become a wait (`rt=255`), or the remaining button (`rt=255+back` becomes `back`), and goal mode does not hold the throttle. The gates use it by default (see [the harness overview](README.md#gates-and-tiers)).

Options that the retail build still reads work without a rebuild, for example `nohud` (no HUD at all; do not use it with scenarios that read the HUD, such as career waiting for LAPS). `camoverride` and its fixed cameras are gone from the retail build and are not rebuilt.

Race setup trace:

- `--env FM4_TRACE_RACE_SETUP=1` prints the guest return addresses of each race's player driver creation (the main loop state that loads a race, for finding the code behind a game mode), every car's driver setup and the pending session type.
- `--env FM4_TRACE_RACE_SETUP=<file>` also appends 4 KB of the race settings (`sub_822DEC50`) to that file when a session starts.

## Shader overrides (phase B4)

Shader overrides replace the DXBC translation of a guest pixel shader with HLSL that you write. Research notes are in [b4-shader-research.md](../analysis/b4-shader-research.md).

### Override files

`--arg shader_override_path=<dir>` replaces the DXBC translation of a guest pixel shader with `<dir>\shader_<ucode hash>[_<modification>].ps.hlsl` (the hash and modification as in the `dump_shaders` file names). The file defines:

```hlsl
void xe_pixel_shader(inout XePixel p)
```

It reads `p.r[]` (`r#` with the interpolators loaded) and writes `p.color[i]` (`oC#`) and `p.depth`. The runtime compiles it with FXC `ps_5_1` after the contract of the translation that it replaces:

- `XE_C(n)` for `c<n>`;
- `XE_TFETCH2D`, `XE_TFETCH3D` and `XE_TFETCHCUBE` (`fetch, coord`) for the default tfetch;
- the `XE_TEXTURE*` and `XE_SAMPLER*` slots.

It also uses the library [`work/runtime/src/graphics/d3d12/shader_override.hlsli`](../../work/runtime/src/graphics/d3d12/shader_override.hlsli), which mirrors the DXBC translator: tfetch; Shader Model 3 multiplication (`xe_mul`, `xe_mad`, `xe_dp4`, `xe_mad_identical` where the ucode multiplies identical operands); PWL gamma; and the epilogue (alpha test, alpha to coverage, exponent bias).

`runtime.log` says `Shader override ... compiled`, or why it kept the DXBC translation: FXC errors, a signature that differs from the translation's, float24 depth, or memexport.

### Contracts

`--arg dump_shaders=<dir>` writes the contract of every translated pixel shader, with its ucode, as `shader_<hash>_<modification>.ps.contract.hlsli`. Each override is compiled against its contract.

### Generated starting points

`--arg shader_override_generate_path=<dir>` writes HLSL for every pixel shader that has no override file. The HLSL does the operations of its DXBC translation, with each block after the guest instruction in a comment. The generator is [`hlsl_translator.cpp`](../../work/runtime/src/graphics/pipeline/shader/hlsl_translator.cpp). Copy one file to the override directory and edit it.

`--arg shader_override_generated=true` uses the generated HLSL for all of them, to test the generator against the DXBC translations with the A/B below.

### FXC notes

[`override_fxc.py`](../../outputs/FM4-build/e2e/override_fxc.py) compiles overrides the way the runtime does, without starting the game, against the contracts of a `dump_shaders` run (about 8 s for 418 shaders):

```
python outputs\FM4-build\e2e\override_fxc.py <override file or dir> <dump_shaders dir> [--mods work\fm4-rex\look --define FM4_LOOK_SHARPEN=0.5]
```

Options: `--asm <dir>` writes the FXC disassembly of each override there, `--mods` gives a `shader_mod_path` directory, `--define NAME=VALUE` (repeatable) sets the defines of the mods, and `--workers` (default 8) sets the parallel compiles.

`--stub` compiles the mods themselves against the contracts, without override files: every `shader_<hash>.mod.hlsli` of `--mods` on every contract of its hash, around a stub override that writes interpolator 0 plus every 2D texture the contract binds, each fetched through `XE_HOOK_TFETCH` as a generated override fetches it (so the fetch hooks of the sky, foliage and road mods run too):

```
python outputs\FM4-build\e2e\override_fxc.py --stub <dump_shaders dir> --mods work\fm4-rex\look --define FM4_LOOK_GLASS=1.0 ...
```

A contract defines only the fetches and float constants its shader reads, so this finds a mod that reads a constant its shader does not (`error X3004: undeclared identifier 'XE_FLOAT_CONSTANT_42'`), which the synthetic contract of `fm4_shader_mod_tests` cannot. With `--define`, each contract is also compiled without the defines, and a mod whose DXBC the defines leave unchanged counts as a failure: its code never reached that translation (for example an `#if` on a fetch the shader does not bind). Give defines that move every knob, or the mods of the knobs left at their defaults fail this check. It lists the mods without a contract in the directory.

The runtime compiles with FXC `ps_5_1`, IEEE strictness and unbounded descriptor tables.

### Shader mods

Shader mods change a generated override without copying it. With `--arg shader_mod_path=<dir>`, a pixel shader with `<dir>\shader_<hash>.mod.hlsli` gets its generated HLSL with that file included after the library. The mod file redefines the hooks that the generator leaves in the code:

- `XE_HOOK_TFETCH`: the result of each texture fetch;
- `XE_HOOK_CONSTANT`: each float constant (`c<n>`);
- `XE_HOOK_INPUT`: the registers after the prologue;
- `XE_HOOK_OUTPUT`: the outputs.

`--arg shader_mod_defines=NAME=VALUE;...` sets its defines. The defaults change nothing.

The look mods (`work/fm4-rex/look`, see below) are shader mods of this kind.

### Iterating on overrides and mods

These switches make shader work faster. They are described in [shader-mod-iteration.md](../analysis/shader-mod-iteration.md), and all of them are off by default in the runtime.

- `--arg shader_override_cache_path=<dir>` keeps the compiled DXBC of every override and mod. An entry is used while the source FXC gets, the defines and the content of every included file stay the same. The launcher and `e2e.py` set `<BuildRoot>\cache\shader-overrides`; `runtime.log` says `loaded from the cache` for each hit.
- `--arg shader_mod_hot_reload=true` recompiles an override or mod when a file it was compiled from is saved, or when a `shader_<hash>` file appears or disappears in the two directories. Only the pipelines of the changed shaders are recreated. A failed compile keeps the last binary that worked.
- `--arg shader_mod_live_knobs=true` compiles the numeric defines as constants after the system constants (`xe_mod_knob`). The console command `shader_mod_set NAME=VALUE ...` (backtick opens the console) then changes them in the next frame. With hot reload, `<shader_mod_path>\shader_mod_defines.txt` does the same whenever it is saved.
- `e2e.py --shader-dev` (like `Build.ps1 -ShaderDev`) turns on both, with every knob of the look files live. Add `--arg shader_mod_path=<dir>` to edit a copy of the look instead of `work/fm4-rex/look`.

With live knobs the mods are larger and branch at run time, so measure performance without them.

## Shader A/B (same scene, two shaders)

`--arg shader_override_ab_ms=<N>` draws with the DXBC translations instead of the overrides in every other period of N ms of the steady clock, so one static scene shows both. [`override_ab.py`](../../outputs/FM4-build/e2e/override_ab.py) lists consecutive captures of a `--keep-ppm` run with their phase. In a static scene, same-phase pairs are identical, and on/off pairs are identical if the override renders exactly like the translation.

```
python outputs\FM4-build\e2e\e2e.py --build-root <BuildRoot> --out <dir> --scenario photo --profile copy --keep-ppm --env FM4_GAME_CMDLINE=engageaionstart --arg shader_override_path=<overrides> --arg shader_override_ab_ms=5000
python outputs\FM4-build\e2e\override_ab.py <dir> --period-ms 5000 --diff <dir>\ab-diff.png
```

With 5000 ms and captures every 2 s, each phase has same-phase pairs. Photo mode after the photo is taken (the SAVE/EXIT screen) is static for about 30 s, but it shows the scene that it rendered once, so only post-processing changes show there.

For the scene shaders (car paint, track), freeze a race with Y (rewind) on the camera to compare. The title keeps rendering the frozen scene every frame (GPU 2.4 ms a frame at 60 fps) under only a small hint bar, until A resumes; same-phase captures are identical. The pause menu (Start) also keeps rendering, but blurs and covers the scene.

## Look mods and the GTAO pass

The look mods are the post-processing of FM4 (original code, see `fm4-look.hlsli`) in [`work/fm4-rex/look/`](../../work/fm4-rex/look/). `Build.ps1 -Action run -Look "sharpen=0.5,bloom=0.8,contrast=0.2,saturation=1.05,vignette=0.5,motion_blur=0.5,exposure=0,tonemap=aces"` (and `pipeline.py` and `e2e.py --look`) copies them to `<BuildRoot>\look` and sets the knobs. Knobs left out keep the original look. The knobs themselves are described in [running.md](../running.md).

Details that matter for experiments:

- `tonemap=gt|aces|neutral` replaces the game's filmic curve before its color grading LUT, with middle gray where the game puts it. `filmic` is the original.
- The car body paint shader (`B85EF04779ABD438`, see [material-atlas.md](../analysis/material-atlas.md)) gets a clear coat from `fm4-paint.hlsli`. `clearcoat=1` adds a GGX highlight of the sun, soft limited by `clearcoat_glint` (in units of the sun's diffuse light; default 4, 1 in `pc` since 20 October 2026; roughness 0.3), and the environment cube at grazing angles, in the paint's own shadow.
- `flakes=0...1` adds metallic flakes (`flake_scale` cells per unit of the paint UV). No preset uses them, as in races they do not show (see `fm4-paint.hlsli`).
- `ao=<strength>` applies the runtime's GTAO pass (see [gtao-design.md](../analysis/gtao-design.md)) in the final combine shaders. Above 0 it also turns the post passes on (`post_pass_path`, `pipeline.py` `ao_cvars`), whose `passes.toml` names the camera vertex shaders and the 0.75 m radius. `--ao` (on `pipeline.py` and `e2e.py`) runs the pass alone.
- Presets name sets of knobs and mix with them, later items winning: `pc` (the full list is in [running](../running.md#look)); `aces`, `gt` and `neutral` (the tone curve, `clearcoat=1`, `ao=0.6` and `sun=2`); and `original`, which runs without mods.
- `road_debug=1...5` (`FM4_LOOK_ROAD_DEBUG`) shows the terms of the lit road `ROAD_BLND_2` instead of its result: the share of the sun highlight, the result without it, the highlight alone, which light the diffuse takes, and the parts of the specular amount (see [look mod coverage](../analysis/look-mod-coverage.md#the-bright-sheet)). With `--shader-dev`, `defines:` steps ([README](README.md#scenario-steps)) switch the views and the other road knobs in one frozen frame.
- The launcher and `e2e.py` use `pc` unless `-Look`/`--look` says otherwise, so the gates run with it. `--look original` compares with the game's own look.

### Post pass and AO diagnostics

The [post pass framework](../analysis/post-pass-framework.md) reads its params from `shader_mod_defines` by name, so a GTAO param is set by adding `NAME=VALUE` to the look's defines (or with `shader_mod_set` in the console, or in `shader_mod_defines.txt` with `--shader-dev`).

- `FM4_LOOK_AO_DEBUG=1` in `shader_mod_defines`: the final combine shaders show the GTAO texture as it is.
- `GTAO_DEBUG=1|2|3|4` (a param of `passes.toml`, formerly `--arg post_ao_debug`) makes that texture view:
  - `1`: W over 0-100 units, for checking the depth and its alignment;
  - `2`: the occlusion before denoising;
  - `3`: the depth discontinuities (1 across a depth discontinuity on any side, the edges of the denoise);
  - `4`: view W over 0-2 units, for close surfaces.
- `--arg post_pass_ab_ms=<N>` skips the passes in every other period of N ms.
- `--arg post_pass_variants="GTAO_RADIUS=0.5,GTAO_SCALE=0.6;GTAO_DEBUG=2;...;off"` with `--arg post_pass_variant_ms=<N>` cycles param values, to compare them in one frozen frame. Freeze a race with Y (rewind) on the camera first, as above.
- `--arg post_pass_log_every=<N>` logs the scene depth, the camera (whether its view is known and from which source), the history, the jitter (with the camera's motion and the frame rate that arm it), the write-back counts and the params of every N-th scene; `--arg post_pass_path=` (empty) runs without the passes.
- TAA ([TAA](../analysis/taa.md#10-validation)): `defines:` steps with `--shader-dev` switch `TAA`, `TAA_SHARPEN`, `TAA_DEBUG` and `TAA_POINT` on a frozen frame; `--arg post_pass_variants="TAA=1;TAA=0"` with `--arg post_pass_variant_ms=1000` and `--env REX_FRAME_GRAPH_EVERY=1` measures its GPU time in one run; `SCENE_PATTERN=1` writes a test pattern into the game's scene through the write-back. A frozen frame is not jittered since 20 October 2026 (the jitter runs only while the camera moves and at 45 fps or more); add `TAA_JITTER_MOTION=0 TAA_JITTER_RATE=0` to judge TAA with the jitter on a frozen frame, as a moving race has it ([TAA, arming](../analysis/taa.md#35-arming-and-switching)).
- `--arg post_pass_jitter=<phases>` jitters the scene draws in every frame after a scene, without the pass that would undo it and whatever the camera does (`auto`, the default, follows the manifest's `[jitter]`; `off` never jitters), and `--arg post_pass_jitter_offset=x,y` uses a fixed offset in host pixels instead of the sequence: to check that the motion vectors stay unjittered and that no seam opens at the tile boundaries ([post pass framework](../analysis/post-pass-framework.md#jitter)).

Source: [`post_pass.cpp`](../../work/runtime/src/graphics/d3d12/post_pass.cpp), [`passes.toml`](../../work/fm4-rex/passes/passes.toml), [`gtao.hlsl`](../../work/fm4-rex/passes/gtao.hlsl), and the GTAO notes in [gtao-design.md](../analysis/gtao-design.md).

## Mod and post pass tests

The tests for the shader mods and the post passes are in [`work/fm4-rex/tests/`](../../work/fm4-rex/tests/): `shader-mod-tests.cpp`, `post-pass-shader-tests.cpp` (the scene_depth passes of `passes.toml` on WARP) and `post-pass-camera-tests.cpp`.
