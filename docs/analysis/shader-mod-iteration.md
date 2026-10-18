# Shader mod iteration

This note describes how the shader override and shader mod path ([B4 research](b4-shader-research.md)) is made fast to iterate on: a disk cache of the compiled DXBC, a hot reload for development, and look knobs that change while the game runs. The gap review of 17 October 2026 found that slow iteration limits how far material mods can grow beyond the six shader hashes of the look: every knob change needed a restart, and FXC compiled every mod on every launch.

## Starting point

- The look knobs reach the mods as preprocessor defines (`shader_mod_defines`, written by `pipeline.py` from `-Look`).
- Every `shader_override_*` and `shader_mod_*` cvar is read once, when the pipeline cache is created, and needs a restart.
- No compiled override is kept: FXC (`D3DCompile`, `ps_5_1`) compiles each mod when its shader is first translated, in every run. The look shaders take 0.15 to 1.4 s each, and 2.5 to 3.0 s for each of the two car paint modifications. With them, translating the shader storage at the start takes about 5.4 s (see [Measurements](#measurements)).

## What is added

All of it is off by default in the runtime.

| Switch | Default | Effect |
| --- | --- | --- |
| `shader_override_cache_path=<dir>` | empty (off) | Keeps the compiled DXBC of every override and mod in `<dir>`. The launcher and `e2e.py` set `<BuildRoot>\cache\shader-overrides`. |
| `shader_mod_hot_reload=true` | `false` | Watches the files the overrides were compiled from, recompiles what changed and recreates only the pipelines of those shaders. |
| `shader_mod_live_knobs=true` | `false` | Compiles the numeric `shader_mod_defines` as constants that the runtime writes every frame, so they change without a recompile. |
| `shader_mod_set NAME=VALUE ...` | (console command) | Changes defines while running: live knobs at once, other defines through a hot reload recompile. Without arguments it lists them. |
| `<shader_mod_path>\shader_mod_defines.txt` | (no file) | With hot reload: `NAME=VALUE` lines (`#` starts a comment) applied over `shader_mod_defines` at the start and whenever the file is saved, like `shader_mod_set`. |
| `Build.ps1 -ShaderDev`, `pipeline.py --shader-dev`, `e2e.py --shader-dev` | off | The look mods load from `work/fm4-rex/look` itself, with hot reload and every knob of the look files live (knobs not given keep their defaults). |

Shader overrides need bindless resources and host render targets, so they run on the RTV path only. With `-Rov` they are off by design (ROV is the reference without mods; see [AGENTS.md](../../AGENTS.md)).

The code is in [`shader_override.cpp`](../../work/runtime/src/graphics/d3d12/shader_override.cpp), [`pipeline_cache.cpp`](../../work/runtime/src/graphics/d3d12/pipeline_cache.cpp) (`ReloadShaderOverrides`) and [`command_processor.cpp`](../../work/runtime/src/graphics/d3d12/command_processor.cpp) (the knob upload).

## DXBC cache

**Key.** The key is the XXH3-64 hash of:

- a cache format version;
- the compiler identity: the size and modification time of `D3DCompiler_47.dll`;
- the hash of the embedded library (`shader_override.hlsli`);
- the FXC target and flags;
- the whole source FXC gets: the contract (which names the ucode hash and the modification, and lists the constants, descriptor slots, interpolators and outputs of the DXBC translation), the include lines of the library and the mod, and the override file or the generated HLSL;
- the defines as FXC gets them.

**Included files.** A mod and the files it includes are not in the key. The cache entry lists every file the include handler served, with the XXH3 hash of its content. On a lookup, each name is resolved again the way the include handler resolves it (the mod directory first, then the override directory) and its content is hashed. If any file differs or is missing, FXC compiles again and the entry is overwritten. The signature check against the DXBC translation runs on cache hits as well.

**Files.** Entries are `<dir>\shader_<hash>_<modification>_<key>.dxbc`. They are written to a temporary file and renamed, so a reader never sees a partial entry. Editing a mod keeps the same key, so its entry is replaced. Each combination of knob values in `#define` mode leaves its own entry, which makes a look that was used before load without FXC. The directory can be deleted at any time.

**Not covered.** The generator (`HlslShaderTranslator`) is covered through its output, which is part of the source. A change to the DXBC translator that leaves the contract and the generated HLSL unchanged does not change the key. This is the same assumption the override path already makes when it compares signatures.

## Hot reload

**What is watched.**

- Every file a compile of an override read: the override file, the mod, and its includes. This also covers a compile that failed.
- Files `shader_<hash>[_<modification>].ps.hlsl` and `shader_<hash>.mod.hlsli` that appear in or disappear from `shader_override_path` and `shader_mod_path`.
- `shader_mod_defines.txt` in `shader_mod_path`.

The check runs every 250 ms, at the end of a submission (`PipelineCache::EndSubmission`), after the pipeline creation threads have finished their queue, so no shader is being translated and no pipeline is being created at that moment. A file whose modification time or size changed is hashed again. Saving it with the same content does not trigger a reload.

**What is rebuilt.**

- The translations whose files changed are rebuilt, together with those whose hash gained or lost an override, and all overridden translations when a define that is not a live knob changed.
- For every overridden translation the runtime keeps its DXBC translation. The DXBC translation is used for the signature check and is restored when the mod is deleted.
- If a recompile fails, the last binary that worked stays and the FXC errors are logged. The next save of a fixed file reloads it.
- Only the pipelines whose pixel shader binary changed are recreated, with `CreateGraphicsPipelineState` on the command processor thread. The pipeline handle stays the same and its state is swapped atomically. Draws already recorded in the open submission take the new state, because the deferred command list resolves the handle when it executes. The old state is released after the GPU has completed the submission.

**Limits.**

- Vertex shaders are not reloaded. The library is embedded in `rexgpu-xenos.dll`, so a library change needs a runtime build.
- With `d3d12_dxbc_disasm`, the disassembly keeps the text of the first binary.
- A new mod for a shader that the game has not drawn yet applies when that shader is first translated.
- With hot reload off, the only cost is one flag check per submission.

## Live knobs

The knobs must reach the mods without changing the bindless root signature that every guest pipeline shares, and without changing the override contract for runs that do not use them. Three placements were considered:

1. **A new root constant buffer.** This changes the root signature of every pipeline. Rejected.
2. **A small texture in a system bindless view**, like the GTAO occlusion (`xe_post_ao`). The root signature stays the same, but every change needs an upload and barriers on the command list, and every knob read is a texture load.
3. **Floats after the system constants in `b0`** (chosen). `DxbcShaderTranslator::SystemConstants` (592 bytes) stays the same, and so does every DXBC translation. The system constant buffer is a root descriptor, which has no size. The command processor writes 32 knob floats (128 bytes) at offset 592 when live knobs are on. The 720 bytes still fit the 768 bytes that the 256-byte placement alignment already reserves for 592.

**How the mods see them.**

- The contract adds `XE_SYS_MOD_KNOBS` and a larger `XE_SYSTEM_CONSTANT_VECTORS`. The library adds `xe_mod_knob(index)`.
- Each numeric define compiles as `NAME=xe_mod_knob(<slot>u)`. The mods do not change, because they use their knobs only as values, never in `#if`.
- `shader_mod_set` and `shader_mod_defines.txt` write the values under a mutex and advance a generation counter. The command processor compares the counter once per draw in `UpdateSystemConstantValues` and uploads the system constants again when it changed.
- Without live knobs, the contract, the defines and the preprocessed library are the same as before.

### Trade-offs

- **Shader size and work.** With live knobs, FXC cannot remove the code of a feature that is off: the occlusion read, the other tone curves, the clear coat and the flakes stay in the shader behind branches decided at run time. Live shaders are larger and do work that the `#define` build leaves out (see [Measurements](#measurements)).
- **Use.** Live knobs are a tool for finding values. The values found go into `-Look` (defines) for normal runs.
- **Compiler warning.** An early `if (knob) return` becomes a run-time branch, and FXC reports it as a potentially uninitialized return value (X4000). The library disables that warning only when `XE_SYS_MOD_KNOBS` is defined.
- **Precision.** At equal values, the result of a live knob can differ from the `#define` result in the last bits, where FXC would otherwise fold constants at compile time.
- **Scope.** Only numeric defines become live, up to 32 of them. A define that was not given at the start, or one with a value that is not a number, needs a recompile (hot reload), so `-ShaderDev` gives every knob of the look files from the start.

## Workflow

```powershell
.\outputs\FM4-build\Build.ps1 -Action run @common -ShaderDev -Look pc
```

- Save a file in `work/fm4-rex/look`: the shaders that use it are recompiled and their pipelines recreated without a restart. FXC errors appear in `BuildRoot\logs\runtime.log`, and the last working shader stays on screen.
- Open the console with the backtick key and type, for example, `shader_mod_set FM4_LOOK_SHARPEN=0.6 FM4_LOOK_BLOOM=0.8`. Live knobs change in the next frame. `shader_mod_set` without arguments lists the values.
- Alternatively, keep `NAME=VALUE` lines in `work/fm4-rex/look/shader_mod_defines.txt` (not tracked by Git) and save it.
- Put the values found into the look, for example `-Look "pc,sharpen=0.6,bloom=0.8"`.

## Measurements

Measured on 17 October 2026 with the `pc` look at 1x, on the development computer (see [current status](../status/current.md#test-environment)).

**FXC at the start.** The shader storage translates the look shaders on several threads when the game starts. FXC takes:

| Shader | FXC time |
| --- | --- |
| `E97AA211B556A9B3`, `9CDA139F232A8A73` (final combine) | 148 to 248 ms |
| `4746CAD32EF168CF` (motion blur) | 669 to 722 ms |
| `30B67D8B86C37587`, `AE3B2E36FCA4958D` (final combine with depth of field) | 1.14 to 1.38 s |
| `B85EF04779ABD438` (car paint), two modifications on one thread | 2.5 to 3.0 s each |

**Cache.**

- `Translated N shaders from the storage` takes 5,421 to 5,471 ms when the mods are compiled, and 16 to 19 ms when the seven entries load from the cache.
- The first run of a look fills the cache (seven entries, 30 to 98 KB each). Every later run of the same look and render scale loads from it.

**Hot reload.** One `e2e.py --shader-dev` photo run on a copy of the look, with files saved during the race phase:

| Change | Result |
| --- | --- |
| `shader_mod_defines.txt` with `FM4_LOOK_SATURATION=0.0`, then `1.1` | Live, no compile: the next capture is gray, then in color again. |
| A red tint in `fm4_grade` (`fm4-look.hlsli`) | 5 translations rebuilt in parallel, 4 binaries changed (motion blur does not use `fm4_grade`), 8 pipelines recreated in 1,673 ms. The next captures are red. |
| A syntax error in the `E97AA211B556A9B3` mod | FXC error X3000 logged in 4.0 ms, the red shader stays, no pipeline recreated. |
| The mod fixed | Recompiled in 193 ms. The binary is the same as before the error, so no pipeline is recreated. |
| `fm4-look.hlsli` restored | 5 translations rebuilt, 8 pipelines recreated in 1,301 ms. |

The run reached its goal (the photo saved), with no clear-gray frames. A reload takes as long as its slowest FXC compile, and the command processor waits for it.

**Live knobs, shader size.** FXC instruction slots, from `fm4_shader_mod_tests` (its synthetic override, so only the difference counts):

| Mod | `pc` look as defines | All knobs live |
| --- | --- | --- |
| Final combine (four shaders) | 1,835 | 2,163 to 2,171 |
| Motion blur | 1,283 | 1,396 |
| Car paint | 2,511 | 2,595 |

## Verification

- `fm4_shader_mod_tests` ([`shader-mod-tests.cpp`](../../work/fm4-rex/tests/shader-mod-tests.cpp)) compiles every mod with every knob of the look files live (`xe_mod_knob`) and checks that the knobs are read from the constant buffer. The test also compiles every mod with the defines of the default look and prints an FNV-1a fingerprint of each DXBC. On 17 October 2026, all 18 fingerprints of the compiles without live knobs were identical between the previous library and the new one, so the output without live knobs is byte-identical.
- `test_pipeline.py` checks the defines that `look_cvars` writes, with and without `shader_dev`.
- A smoke gate and the `perf` tier of the new runtime against the build before it passed every cell and run, with no regression beyond the variance of the AI's race; see [current status](../status/current.md#automated-gates-17-october-2026).
