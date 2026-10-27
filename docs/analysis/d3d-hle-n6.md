# Native port, stage N6: a renderer that behaves like a PC game

Section 19 of the [D3D-level HLE design](d3d-hle.md). Sections 1 to 15 are in [d3d-hle.md](d3d-hle.md), section 16 (stage N3) in [d3d-hle-n3.md](d3d-hle-n3.md), section 17 (stage N4) in [d3d-hle-n4.md](d3d-hle-n4.md) and section 18 (stage N5) in [d3d-hle-n5.md](d3d-hle-n5.md).

## 19. Stage N6 design, 27 October 2026

Status: a plan for the project owner's approval. Nothing in it is built; the measurements of 19.2 ran on local main's build of 27 October 2026 without code changes.

### 19.1 The goal and the order of priorities

N1 to N4 replaced the ring and the command processor thread with a host render thread, moved the render targets out of the EDRAM model and moved the draw back end onto the submit thread; N5 added the panel's own resolution, the logical size and photo mode at the screen's scale. What is left of the emulator is under the native path, not beside it: the title's PM4 is still parsed, the Xenos register file is still the state between the title and D3D12, shaders are still translated when the title first draws with them, and textures are still found by fetch constants and kept valid by page watches.

N6 is the step from "D3D-level HLE on the emulator's backend" to a renderer that behaves like a native PC game. The owner's priority (27 October 2026): what players feel most first, which is stutter, then CPU headroom on the ROG Xbox Ally X. The candidates were five: shader precompilation, direct D3D12 draws, multithreaded command recording, a native texture cache, and whatever else the profile shows. Each is sized below by measurement and by the code (19.3 to 19.7), then ordered (19.8); 19.9 names what N6 leaves out. Two findings set the order: with an empty shader cache the look mods' compiles stall the first sessions for seconds at a time (19.2.1), and on the ROG Xbox Ally X the scene's resolves, still written in the guest's memory layout, take a fifth to more than a quarter of the GPU frame (19.2.3).

### 19.2 Measurements, 27 October 2026

On `I:\FM4-build-usa` (the USA release), local main's build `595fd04` (`game-build`, built 27 October 2026 07:36), `e2e.py --native --fast-route` at 240 Hz and 1x with the launcher's defaults otherwise (look `aces`, one pass with 2x MSAA, the AI driving), one run each:

| Run | Scenario | Shader cache |
|---|---|---|
| `n6c-career-cold` | `career` (menus, Tsukuba with the Ford Ka, the replay) | an empty `cache_root` and `shader_override_cache_path` (`D:\FM4-n6\cacheA`): a fresh install |
| `n6c-photo-newtrack` | `photo` (Quick Race on the Bernese Alps with the Abarth, photo mode) | `cacheA` after `n6c-career-cold`: the second session, on a new track and car |
| `n6c-career-cold2` | `career` | `cacheA` after both: the career's race and replay for the first time |
| `n6c-career-warm`, `n6c-photo-warm` | `career`, `photo` | the BuildRoot's cache, filled by every e2e run since 18 October 2026 |

The NVIDIA driver's own shader cache was warm in every run (it is the user's system state and was not cleared), so a pipeline creation here is the driver's cache hit; the cold driver case is an open measurement (19.3.6).

#### 19.2.1 Compiles in play

| Run | Phase | FXC compiles of look mods | Pipelines created in play | Frames over 50, 100, 250 ms | Time in frames over 50 ms | Longest frame |
|---|---|---|---|---|---|---|
| `n6c-career-cold` | menus and the load to the pre-race (the run stopped there: the step that starts the race timed out after 30 s) | 53, 20.3 s | 368, 303 ms | 16, 14, 9 | 22.2 s | 8,517 ms |
| `n6c-photo-newtrack` | menus and the load | 23, 7.9 s | 255, 194 ms | 11, 7, 6 | 9.4 s | 4,819 ms |
| | race | 1, 0.7 s | 36, 27 ms | 1, 1, 1 | 0.7 s | 716 ms |
| | the way into photo mode | 0 | 146, 25 ms | 3, 2, 2 | 0.8 s | 433 ms |
| | photo mode | 2, 0.4 s | 3, 1 ms | 1, 0, 0 | 0.1 s | 84 ms |
| `n6c-career-cold2` | menus and the load | 1, 0.04 s | 33, 24 ms | 5, 2, 1 | 1.1 s | 725 ms |
| | race and replay | 0 | 24, 5 ms | 0 | 0 | 21 ms |
| `n6c-career-warm` | menus and the load | 0 | 0 | 5, 3, 1 | 1.1 s | 713 ms |
| | race and replay | 0 | 0 | 0 | 0 | 25 ms |
| `n6c-photo-warm` | menus and the load | 0 | 0 | 6, 2, 1 | 1.2 s | 732 ms |
| | race | 0 | 0 | 0 | 0 | 11 ms |
| | the way into photo mode, photo mode | 0 | 0 | 6, 1, 1 | 0.6 s | 283 ms |

The warm runs' long frames are the title's own (the boot's first frames and its loading screens); what the cold runs have beyond them is the compiles.

- **The stalls of a fresh install are the look mods' FXC compiles.** `ShaderOverrides::Compile` ([shader_override.cpp:943](../../work/runtime/src/graphics/d3d12/shader_override.cpp)) compiles a modded pixel shader with FXC on the host render thread, where the draw meets it: 53 compiles in `n6c-career-cold`, 20.3 s in all, median 132 ms, 90th percentile 1,095 ms, longest 2,397 ms. Its longest frames are made of them: the frame of 7.6 s at the career home falls in the seconds of the log that hold 8.2 s of compiles, and the frames of 1.7 and 8.5 s in the load to the pre-race in seconds holding 10.5 s (the log's clock and the trace's are matched to about a second).
- **A new track in a later session** (`n6c-photo-newtrack`: 52 overrides from the cache, 26 compiled, 9.0 s) stalls in the load (4.8 s), in the race itself (one compile, a frame of 716 ms) and on the way into photo mode (433 ms).
- **The third session** (`n6c-career-cold2`, the career's race and replay seen for the first time) compiles one mod and has no long frame in the race: the first career's pre-race had already drawn the track and the car. The stalls of a fresh install are concentrated in its first sessions' menus and loads, and come back with every new track, car or mode.
- **The DXBC translations and the pipelines cost little with the driver's cache warm**: 368 pipelines created on the replay in `n6c-career-cold` took 303 ms in all, 0.8 ms each, spread over the menus; the translations of shaders without a mod are not counted separately and do not show as stalls. The way into photo mode on a new track (433 ms against 283 ms warm) is the one place where translations and 146 pipelines show beside the title's own stall.
- **A warm cache has none of it**: no compile and no pipeline creation in play, no race frame over 25 ms, and 304 mods loaded from the override cache.
- **Start-up**: with a warm cache the runtime translates the stored shaders and creates the stored pipelines before the title runs (`PipelineCache::InitializeShaderStorage`, [pipeline_cache.cpp:245](../../work/runtime/src/graphics/d3d12/pipeline_cache.cpp)): 930 shaders in 191 ms and 3,448 pipelines in 126 to 208 ms. The ROG Xbox Ally X's logs of 24 October 2026 show the same step at 26 to 160 ms for 884 shaders and 89 to 148 ms for 2,306 to 2,367 pipelines.

What it means for players: the export ships the build computer's cache ([running](../running.md#export-to-another-computer), `pipeline.py` copies `BuildRoot\cache`), so the owner's Ally X never saw these stalls. The cache holds shaders translated from the game, so a public preview cannot ship it ([distribution](../sources-and-licensing.md#distribution)): every player's first sessions would have them, and every change of the look's knobs or of the render scale compiles the mods again, since the override cache is keyed by the source and the defines (one entry per shader, look and render scale, [running](../running.md#shader-cache)).

#### 19.2.2 How much of the game the cache covers

The BuildRoot's shader storage (`4D530910.xsh`, every shader its runs have loaded since 18 October 2026) holds 930 shaders: 354 vertex and 576 pixel shaders. The offline inventory of the game's shader containers in [look mod coverage](look-mod-coverage.md) (171 `.fxobj` files, the 32 track archives, default.xex and the DLC archives) has 1,076 unique pixel shaders, so even this cache holds about half as many pixel shaders as the game has. 138 pixel shaders have a look mod; `cacheA` after the career's menus and the Bernese Alps race had 803 pipelines, 295 pixel shader translations, and 78 of them modded (78 FXC compiles).

#### 19.2.3 Textures and resolves

This PC, the warm runs, per phase (the textures zone is the render thread's `RequestTextures` with its keys; the GPU's texture loads are the untiling and decoding compute passes with their copies, `gpu.load_ms`):

| Phase | Uploads | Invalidations | GPU texture loads | Textures zone (CPU) | Shared memory upload and protect |
|---|---|---|---|---|---|
| Menus and loading (`career`, `photo`) | 19.7 to 20.3 MB/s | 176 to 193 a second | 0.18 to 0.21 ms a frame | 0.30 to 0.33 ms a frame | 4.6 to 5.0 and 2.1 to 2.5 ms/s |
| Race | 15.5 to 16.8 MB/s (54 KB a frame) | 394 to 414 a second (1.5 to 1.7 a frame) | 0.19 to 0.20 ms a frame | 0.37 to 0.40 ms a frame | 7.9 and 6.3 to 6.4 ms/s |
| Replay | 14.4 MB/s | 168 a second | 0.25 ms a frame | 0.52 ms a frame | 4.5 and 3.2 ms/s |
| Photo mode (30 fps, about 2,600 draws a frame) | 0.6 MB/s | 55 a second | 0.29 ms a frame | 0.81 ms a frame | 0.8 and 0.7 ms/s |

No race frame of a warm run was over 16 ms, so the uploads of the track's streaming do not stall a race on this PC. On the CPU the textures cost about 0.4 ms of the render thread's race frame, mostly keys (17.11), and the uploads about 14 ms a second; the GPU loads take 0.19 ms of a 2.3 ms race frame (8%).

The ROG Xbox Ally X's traces (24 and 25 October 2026, `-Native`; my medians of the race windows, which [FSR plan, 12.4](fsr-plan.md#124-the-owners-rog-xbox-ally-x-result-for-upscaling) also breaks down, with medians over other windows 0.5 to 1.2 ms higher) show the same work weighing far more on the Radeon 890M, and the full-screen resolves with it:

| Race frame, GPU | This PC, 1x (`n6c-photo-warm`) | This PC, 3/2 (`pf-c32-240-career`, a side session's build of 27 October 2026) | Ally X, 1x, 35 W (17.25) | Ally X, 1x (25 October 2026, `log_ally_5` run 1) | Ally X, 3/2 (run 2) |
|---|---|---|---|---|---|
| Frame | 2.30 ms | 3.50 ms | 7.48 ms | 8.27 ms | 15.91 ms (60.4 fps) |
| Full-screen resolves (three a frame) | 0.08 ms | 0.49 ms | 0.68 ms | 0.75 ms | 4.53 ms |
| All resolves | 0.27 ms | 0.79 ms | 0.90 ms | 1.23 ms | 5.17 ms |
| Full-screen texture loads | 0.07 ms | 0 | 0.86 ms | 0.86 ms | 0 |
| All texture loads | 0.19 ms | 0.07 ms | 1.03 ms | 1.06 ms | 0.17 ms |
| Transfers between host targets | 0.06 ms | 0.10 ms | 0.12 ms | 0.13 ms | 0.97 ms |

At 1x the scene's three full-screen resolves are written into the guest's memory layout (the scaled resolve buffer) and the textures that sample them are loaded back from it every frame; at a fractional scale `native_rt_surfaces` bit 32 writes those textures straight from the host render target instead (21 a frame, 18.12), but the guest layout is still written for the readbacks. On the 890M that round trip is 1.6 ms of an 8.3 ms frame at 1x (19%) and the resolves 4.5 ms of a 15.9 ms frame at 3/2, the panel's resolution (28%), where the race runs at 60 fps; on this PC it is 0.15 and 0.49 ms. The transfers at 3/2 (1.0 ms on the 890M) were cut after these traces, on 26 October 2026 (0.32 to 0.10 ms on this PC, [FSR plan, 13.1](fsr-plan.md#131-the-uniform-draws)); the Ally X has not measured that build yet.

#### 19.2.4 The threads, from N4

- **This PC** (17.13 to 17.16): the host render thread is busy 2.72 ms a race frame at 1,800 draws, the submit thread 1.5 to 1.7 ms, and the race runs at the 240 Hz cap; the busiest guest thread uses 788 ms a second with the ring wait's sleep.
- **ROG Xbox Ally X** (17.21, 17.25): GPU bound in every mode (the GPU busy 85 to 87% of the frame at 1x); the host render thread 4.36 ms a frame at 35 W and 6.11 ms at 17 W, the submit thread 274 to 333 ms a second, the busiest guest thread 735 ms a second; no thread is saturated. CPU time the native path saves reaches the GPU through the shared power budget: the ring wait's sleep saved 216 to 275 ms of CPU a second and gave 2.0 to 3.1% more frames.
- **The guest render thread's own work** (the sampling profile `n4z-race-prof2` of 23 October 2026): of its busy samples, the title's track presentation (`CTrackPresentation::DrawObjectCommandBuffer` `sub_82311DD0`, `DrawObject` `sub_82314BC8`) and the vector helpers of the recompiled code (`FM4VectorFMA`, `FM4VectorDot`) take the largest shares; `RunCommandBuffer` itself about 8%, and the native records' copies a few percent.

### 19.3 Item 1: no shader or pipeline compiles in play

#### 19.3.1 Today

| Step | Where | When | Thread |
|---|---|---|---|
| Stored shaders translated (DXBC, with their mods from the override cache) | `InitializeShaderStorage`, [pipeline_cache.cpp:245](../../work/runtime/src/graphics/d3d12/pipeline_cache.cpp) to 549 | start-up, blocking | translation threads, all cores but one |
| Stored pipelines created | the same, 551 to 728 | start-up, blocking | the creation threads (`d3d12_pipeline_creation_threads`, three quarters of the cores) |
| A shader not in the storage translated | `ConfigurePipeline` and `TranslateShadersForDraw`, pipeline_cache.cpp:1040 to 1180 | the first draw with it | host render thread |
| Its look mod compiled with FXC (or loaded from the override cache) | `ShaderOverrides::Compile`, [shader_override.cpp:943](../../work/runtime/src/graphics/d3d12/shader_override.cpp) | inside that translation | host render thread |
| A new pipeline created | `ConfigurePipelineDescribed` with `use_async` false, [draw_back_end.cpp:1595](../../work/runtime/src/graphics/d3d12/draw_back_end.cpp) | the replay of the first draw with it | submit thread |
| Post passes compiled | `post_pass.cpp` | start-up (25 passes, 873 ms in `n6c-career-cold`), from the override cache's `pass_*` entries when warm | render thread |

The launcher turns asynchronous compilation off (`async_shader_compilation=false`, `pipeline.py` `launch_cvars`), and the draw back end needs it off ([17.8](d3d-hle-n4.md#178-step-2-increment-2-results-23-october-2026)): a draw waits for its pipeline rather than being dropped (the asynchronous mode of the PM4 path skips draws whose pipeline is not ready, [deferred_command_list.cpp](../../work/runtime/src/graphics/d3d12/deferred_command_list.cpp) `Execute`).

#### 19.3.2 Design

Item 1 is built on N5's native shader contract (8.2; the main session approved its design on 27 October 2026, `n5-shader-contract`), not on a path of its own:

- **What N5 provides.** Its increment 1: `shaders.toml` entries keyed by the ucode hash, the pass classes, and the look's knobs as `params` in live knob slots, so one binary serves an entry under every look and a knob change compiles nothing. Its increment 2: no DXBC translation for entries: `HlslShaderTranslator` assigns the bindings, the contract and the expected signature come from the ucode analysis, and an entry's translation is its compiled HLSL alone.
- **What moves from N5 to N6.** Its increment 7, the offline translation of every shader to HLSL and DXIL at the first start, becomes part 2 below.
- **What N6 adds**: when and where shaders and pipelines compile. The compile off the render thread (part 1), the offline pass over the shader storage with DXC and Shader Model 6 (part 2), filling the storage from the user's disc (part 3), the pipeline state precompile (part 4), the early creation of what is still missed (part 5), and the cache with its invalidation (part 6). N5 keeps what a shader declares and how it binds.

1. **No compile on the render thread.** A pixel shader whose modded translation is not cached draws with the plain translation first: a modification that keeps the DXBC translation, as N5's contract already has two (its increment 1, [shader contract](shader-contract.md#22-pass-classes)): `native_plain` for the draws outside an entry's `passes`, whose pipelines are stored, and `hlsl_override_disabled` for the shader override A/B (`shader_override_ab_ms`), whose pipelines are not; the contract's own A/B takes the classic mod with `native_classic`, not stored either. The compile goes to a worker pool; when it finishes, the pipelines of that translation swap their state in place, as the shader hot reload does today (`PipelineCache::ReloadShaderOverrides`, pipeline_cache.cpp:832: the new state is exchanged atomically, the old one retired after its submission's fence), and the mod shows from the next frame on. It works for today's classic mods and FXC, so it does not wait for N5, and for contract entries alike. Cost to the player: a shader's first frames without its mod (up to 2.4 s by 19.2.1), which parts 2 and 3 make rare.
2. **The offline pass over the shader storage** (N5's increment 7). Before the title starts, the runtime takes every shader of the user's shader storage (`.xsh`) with the modifications that the pipeline storage and the manifest of part 4 name, generates its HLSL with `HlslShaderTranslator` (increment 2's path; vertex shaders once N5's increment 6 generates them, until then their DXBC translation, about a millisecond each), compiles it with DXC to DXIL (Shader Model 6.0 or higher, with the precision of FXC's `/Gis`) on all cores but one, and caches the result per ucode hash and modification (part 6). Classic mods move onto the same path as their entries move to `shaders.toml`; the plain translations follow once increment 2's bindings serve every shader, not only entries. A progress screen shows the pass; it runs at the first start, after a build or driver change and after a change of what keys the cache (a new entry, a new render scale), and only for what is missing.
3. **The storage filled from the user's disc.** The offline inventory that `look_coverage.py` runs in Python (the containers of `Media\Shaders`, the track archives with their LZX entries, default.xex, the DLC packages) moves into the runtime or the launcher (players have no Python) and adds the game's 1,076 pixel shaders to the storage at the first start, so part 2 compiles a fresh install's pixel shaders before any is drawn. Their keys are the runtime's (no rewrite of pixel shader microcode, 2.6). Vertex shaders stay with what the storage has seen: the XDK rewrites their fetch instructions at draw time (2.6), so their keys exist only once the title has drawn with them.
4. **The pipeline state precompile.** A committed pipeline manifest (pipeline descriptions: shader hashes, modifications and fixed function state, no microcode) is expanded at the first start for every description whose shaders the storage holds, and the created pipelines are kept in a D3D12 pipeline library (`ID3D12PipelineLibrary`) under `cache\shaders\local`, so a start-up does not depend on the driver's own cache, and a driver update recompiles before the title starts instead of in play.
5. **Early creation of what is still missed.** When the render thread meets a new shader pair or a new state, it queues the pipeline the back end will need on the creation threads at once (the description follows from the draw's capture), so the replay of the list a few milliseconds later usually finds it created and otherwise waits only for the rest. Translations still missing move to the same threads. A switch for an A/B lets the replay skip a draw whose pipeline is still missing after 2 ms, only for kinds that write no depth and feed no resolve; it is off by default.
6. **The cache and its invalidation.** One cache of compiled translations under `cache\shaders\local` (where the comment in `InitializeShaderStorage` places host blobs), replacing the override cache for what moves onto part 2: an entry per ucode hash and modification, keyed by the generated HLSL's hash with those of the library (`shader_override.hlsli`), the hooks and the contract (today's `LoadCached` already checks the included files' hashes), the translator's version, the compiler's version and the render scale where it enters the code; no longer the look's knobs, which N5's `params` take out of the binary. The pipeline library is dropped as a whole when D3D12 reports another driver or adapter; the storages keep their own versions as today. The cache is pruned by last use: the BuildRoot's override cache holds 16,041 entries (568 MB) from every look and scale it ever compiled.

#### 19.3.3 Risks

- **Part 1 shows the game's look for a moment** on a fresh shader. The 1:1 crops of the gate decide whether that is acceptable; parts 2 and 3 make it rare.
- **DXC is not shipped today.** It is a build-time tool of the FidelityFX SDK only ([sources and licensing](../sources-and-licensing.md)); part 2 ships `dxcompiler.dll` and `dxil.dll` to players, which needs the license review and its notices first. DXIL can differ from today's FXC DXBC in the last bits, so N5's byte-identical A/B is the gate where it holds, and frozen-frame parity with 1:1 crops where the compiler differs; the precision flags are checked on the same shaders.
- **The manifest carries hashes and render state of game shaders.** It holds no microcode and no game code, like the look mods' hashes in `passes.toml`, but it is derived from runs of the game: the review rules of [sources and licensing](../sources-and-licensing.md) decide before it is committed. The microcode, the generated HLSL, the translations and the pipelines stay on the user's machine, as N5's design and today's caches keep them.
- **Part 3's inventory must match the runtime's keys**, also for the DLC and Disc 2; a mismatch costs only the precompile's coverage, not correctness. Its LZX reader is new runtime or launcher code.
- **Part 5's skip switch can change the picture**: it is off by default, and the gates count skipped draws (FAIL above zero). The asynchronous mode tried on 13 to 15 October 2026 dropped about 2,000 draws a run without fewer hitches, and about 40 with a wait of 2 ms ([performance history](../status/history-performance.md#shader-cache-13-15-october)); part 5 creates early and waits instead of dropping.
- **The vertex shaders' rewrite** (2.6) keeps part 3 to pixel shaders. Re-running the XDK's rewrite (`sub_826EC0C0`) offline from recorded declarations would close the gap; it is not planned without a measurement showing vertex shader stalls.
- **The dependency on N5.** Parts 2 and 6 need the contract's increments 1 and 2; parts 1, 3, 4 and 5 do not.
- `-Rov` runs no look mods and no contract, so parts 1, 2 and 6 do not touch it; parts 3 to 5 change when pipelines are created on every path, and the no-change proofs of RTV and `-Rov` cover them.

#### 19.3.4 Increments and gates

A new gate cell for stutter: a run on an empty `cache_root` and override cache (`e2e.py --cold-cache`, which the first increment adds), with FAIL rules on the frames over 100 ms per phase and on any compile, translation or pipeline creation timed on the render thread or the replay over 4 ms. The perf trace adds the counts and times of translations and compiles per window beside `back_end.pipelines`.

| Increment | Needs | Step gate (about 30 minutes) | Close gate |
|---|---|---|---|
| 1.0 Counters and the cold-cache cell | | the cold `career` and `photo` cells reproduce 19.2.1 | smoke on the native path, RTV and `-Rov` (the counters run on every path) |
| 1.1 Compiles off the render thread (part 1) | | cold `career`: no frame over 100 ms from a compile; the 1:1 crops of the first race frames at 2x beside a warm run | smoke on the native path, perf with three runs, `parity.py` on the freeze at 1x and 2x with a warm cache, the RTV and `-Rov` smokes with `-Look "original,taa=0"` (no change), a native capture at 1:1 beside RTV |
| 1.2 The offline pass with DXC, and the cache (parts 2 and 6) | N5's increments 1 and 2 | a warm run with no compile in play; the A/B of DXIL against the DXBC path on a frozen frame (byte-identical where N5's gate holds, else parity and 1:1 crops); the pass's time on this PC; a change of a hook's source or of the scale recompiles only what it keys | the same as 1.1, and the pass on the Ally X (the owner runs the export) |
| 1.3 The game's pixel shaders from the disc (part 3) | | the inventory's keys against the BuildRoot's storage (every stored pixel shader found); cold `photo` on a new track | the same as 1.1 |
| 1.4 The pipeline manifest and library (part 4) | 1.2 for the translations it creates from | cold cells: no pipeline created in play for a manifest description; start-up time | the same, and a start-up on the Ally X |
| 1.5 Early creation of missed pipelines (part 5) | | the back end's check run with no skipped draw; the replay's wait for pipelines and the frames over 100 ms in the cold cells | the same with the perturbation proof of the back end's check |

#### 19.3.5 Expected gains and effort

- **This PC**: a fresh install's first career and race without the stalls of 19.2.1 (in `n6c-career-cold` 16 frames over 50 ms, 22.2 s inside them, nine over 250 ms); a new track without its 0.7 s hitch in the race; a look change without a compile at all (N5's `params`), a scale change compiled before the title starts. Races and fps at a warm cache do not change. Increment 1.1 alone removes the stalls (the mods arrive late instead); 1.2 to 1.4 make them arrive on time.
- **ROG Xbox Ally X**: the same stalls, longer by its slower cores (not measured: the owner's exports ship the cache); the offline pass's time grows with them, about 4 to 8 times this PC's by its thread count and clock (inferred), which 1.2 measures. DXC is expected to compile the same HLSL faster than FXC, whose longest compiles took 1.1 to 2.4 s here; 1.2's timing confirms it.
- **Effort**: 1.0 and 1.1 about 1 to 2 person-weeks (they fix the stalls), 1.2 3 to 5 (N5's estimate for its increment 7), 1.3 2 to 4, 1.4 2 to 3, 1.5 1.5 to 3: 10 to 17 for the item, after N5's increments 1 and 2 (2.5 to 4 person-weeks in N5) for 1.2.

#### 19.3.6 Open measurements

- A pipeline creation with a cold driver cache, on this PC and the Ally X. Clearing the driver's cache is the user's system state; a build whose translator emits one changed instruction per shader (a different bytecode, so a driver miss) measures it without touching the cache.
- The vertex shaders' translations in play, once the counters of increment 1.0 time them.

### 19.4 Item 2: direct D3D12 draws

#### 19.4.1 Today

On the native path the title still writes PM4 for nearly everything. Only the three dirty-state writers are records (`WriteDirtyRegisters` `sub_823114F8`, `WriteDirtyFloatConstants` `sub_82310E78`, `WriteDirtyFetchConstants` `sub_8230FA40`; [fm4-native-records.cpp](../../work/fm4-rex/src/fm4-native-records.cpp)); the host render thread executes the rest with `ExecutePacket` ([command_processor.cpp:941](../../work/runtime/src/graphics/command_processor.cpp)): the direct draws, the clears, resolves and events, and the replays of recorded command buffers, which carry about 82% of the draws (section 3). Every draw goes through the Xenos register file: `IssueDraw` ([d3d12/command_processor.cpp:3839](../../work/runtime/src/graphics/d3d12/command_processor.cpp)) reads the registers for the shaders, the primitive processing, the render targets and the textures, and the back end on the submit thread derives the pipeline, the fixed function state, the system constants and the constant buffers from a copy of them ([draw_back_end.cpp](../../work/runtime/src/graphics/d3d12/draw_back_end.cpp) `ReplayDrawBackEndDraw`).

What the register model costs on the hot path, per race frame at 1,800 draws on this PC (17.7 to 17.13, without the split's timers where known):

| Part | Thread | ms a race frame |
|---|---|---|
| PM4 dispatch and parsing (about 28,000 packets, 519 replays) | render | about 0.4 |
| Register packets and the converted writers' records | render | about 0.15 and 0.20 |
| Shader loads (`IM_LOAD`: hash and lookup) | render | about 0.12 |
| The capture of the registers for the back end | render | about 0.23 |
| The texture keys from fetch constants | render | about 0.17 |
| The back end's decode of the registers into D3D12 state | submit | about 0.85 (470 ns a draw) |

About 1.3 ms of the render thread's 2.72 ms and most of the back end's work exist because the state between the title and D3D12 is Xenos registers.

#### 19.4.2 Design

The title's draws become host draw records with D3D12-shaped state, made where the state is known:

- **The draw flush** (`sub_823436B8` and the dirty writers it calls) emits one draw record per direct draw: the pipeline key (the shader pair and the fixed function state, hashed once per change of the dirty groups), the constant buffer contents by reference, the texture bindings by object (with item 4) and the draw parameters. The host render thread no longer parses the direct draws' PM4 or keeps a register file for them.
- **Recorded command buffers** are decoded once into host draw lists keyed by the replayed buffer, with the state they inherit applied at each replay and the `LOAD_ALU_CONSTANT` blocks read at replay (13.3). This is N2's op list idea with a different validity: the 0.02% of replays whose PM4 changed (13.2) and the CPU's rewrites of recorded blocks are caught by a hash of the buffer at replay (`host_render_oplists_verify` measured that as cheap) instead of page watches, whose access violations made N2's op lists slower (15.8). Half of the replayed buffers come from the assets, not from `EndCommandBuffer`, so the decode keys on the buffer, as N2 planned.
- **Resolves, clears, events and fences** become records of their own; the guest-visible writes stay on the host render thread in order, as today.
- The PM4 path stays for everything not converted, executed in order as N1 does, and the PM4 RTV path and `-Rov` stay as they are.

#### 19.4.3 Risks

- The largest of N6: the predicated tiles of `-Quality`'s photo mode and split screen, the inherited state of replays, the vertex fetch rewrite at the flush and the late writes of 17.20 all have to be carried by records that the identity check can compare.
- N2's op lists saved nothing (15.8); a second attempt has to show the parse saving on its first step before the draw records are built.
- The checks multiply: a record path beside PM4 for every draw kind.

#### 19.4.4 Increments and gates

1. Measure first: the render thread's time by packet kind inside and outside replays on today's build (`REX_CP_SPLIT`), and the share of the replays' buffers whose hash changes between replays.
2. Replays as host draw lists with the hash check, the draws still through `IssueDraw` from the decoded register runs. Gate: the identity check per replay with its perturbation proof; the render thread's time per race frame against step 1.
3. Direct draws as records from the flush. Gate: the identity check of the records against the PM4 they replace, with its perturbation proof.
4. The register file off the hot path: `IssueDraw` and the back end take the records' state. Gate: the back end's check against the register path, field by field.

Every close gate: smoke, perf with three runs, `parity.py` on the freeze at 1x and 2x, the perturbation proof as one `career` run, the RTV and `-Rov` smokes, a native capture at 1:1 beside RTV.

#### 19.4.5 Expected gains and effort

- **This PC**: the render thread from 2.72 ms to about 1.5 to 2.0 ms a race frame at 1,800 draws and the submit thread about 0.5 ms lighter, by the table of 19.4.1; the race already runs at the 240 Hz cap at 1x, so the gain is headroom (2x, 4K, 2,400 draws a frame) and power.
- **ROG Xbox Ally X**: GPU bound, so no fps directly; the CPU time saved (inferred about 1 to 2 ms of the host render thread's 4.4 to 6.1 ms a frame, and part of the submit thread) goes to the GPU's power budget as the ring wait's sleep did: about 1 to 3% more frames on battery by that comparison.
- **Effort**: 10 to 16 person-weeks.

### 19.5 Item 3: multithreaded command recording

#### 19.5.1 Today

One D3D12 command list per submission, recorded by one thread at a time: the host render thread writes the deferred command list, the submit thread replays it into the D3D12 list with the draw back end and executes it (`D3D12CommandProcessor::SubmitThreadMain`, [d3d12/command_processor.cpp:7390](../../work/runtime/src/graphics/d3d12/command_processor.cpp)), and the present runs on a thread of its own (`PresentThreadMain`, 7515). No bundles, no second list per submission, no parallel recording. The work is already split across three threads: the front end, the back end with the recording, and the present.

#### 19.5.2 Design, risks and gains

Splitting a frame's deferred commands into ranges (the scene, the shadow cascades, the mirror, the post passes) replayed into several command lists on worker threads and executed in order would shorten the submit thread's part of the frame. It gains nothing while no thread limits the frame: the submit thread takes 1.5 to 1.7 ms of a 4.17 ms frame at 240 Hz on this PC and 274 to 333 ms a second on the Ally X, neither critical (17.21). Its risks are the replay's state (the back end's register copy and D3D12 state per list, 17.6), which each range would have to start from, and the ordering of the caches' own commands. Effort 3 to 5 person-weeks.

N6 leaves it out (19.9). The trigger to take it up: the submit thread above about 60% of the frame time in a perf gate, on a smaller CPU, at 4K, or after item 2 has moved more work onto it.

### 19.6 Item 4: a native texture cache

#### 19.6.1 Today

- **Keys from fetch constants.** `TextureCache::RequestTextures` ([cache.cpp:540](../../work/runtime/src/graphics/pipeline/texture/cache.cpp)) builds a texture's key from the binding's six fetch constant dwords (`BindingInfoFromFetchConstant`, 1168) whenever the binding is out of sync, then finds or creates the texture.
- **Bindings reset often.** `BeginFrame` resets every binding each frame (329), and any texture whose watch fires sets `texture_became_outdated_`, which resets every binding at the next draw (543, 938): a resolve into a range a texture was loaded from, or a CPU write. That is why the keys are recomputed about 1.5 times a draw (2,327 a race frame, 17.11).
- **Uploads and untiling.** A texture's guest memory is copied into the 512 MB shared memory buffer on the render thread (`SharedMemory::RequestRanges`, [shared_memory.cpp:465](../../work/runtime/src/graphics/shared_memory.cpp); `D3D12SharedMemory::UploadRanges`, [d3d12/shared_memory.cpp:307](../../work/runtime/src/graphics/d3d12/shared_memory.cpp)), and a compute shader untiles and decodes it on the GPU (`LoadTextureDataFromResidentMemoryImpl`, [d3d12/texture_cache.cpp:2050](../../work/runtime/src/graphics/d3d12/texture_cache.cpp)).
- **Validity by page watches.** A loaded texture watches its pages (`MakeUpToDateAndWatch`); a CPU write faults, the shared memory's invalidation callback (shared_memory.cpp:712) clears the pages and fires the watches.

The measured cost (19.2.3) is small in the race and in the menus: about 0.4 ms of the render thread's race frame on this PC (0.30 to 0.33 in the menus, 0.8 in photo mode at 2,600 draws a frame), uploads of 15 to 20 MB a second at 14 ms a second of CPU, and no race frame over 16 ms from the track's streaming. The GPU's loads are larger on the Ally X (1.0 ms of a race frame), but 0.86 ms of that is the full-screen textures loaded back from the scene's resolves, which 19.7.1 removes; the other loads take 0.12 ms on this PC and 0.17 to 0.2 ms on the Ally X.

#### 19.6.2 Design

Textures keyed by the title's texture objects: hooks on the XDK's texture creation and on `SetTexture` (`sub_8233A9A8`) give each binding the object's address and the fetch constant it holds; the object's identity and a generation count (bumped by the texture lock `sub_826DEF78` and its unlock, by a resolve into its range, and by a page watch as the safety net) replace the key computation and the per-frame reset. A binding stays valid across frames and across resolves into other textures. Uploads keep the shared memory and the GPU untile; loads that the track streaming brings in during a race move to a copy queue ahead of their first use where the loader's completion is known.

#### 19.6.3 Risks

- The title also writes texture memory without the lock (the loader's streaming writes into its heaps, video frames, game code that keeps a pointer from an earlier lock): the watches stay as the safety net, so the gain is in the keys and the resets, not in removing the watches.
- 63.4 million `SetTexture` calls in one run (13.7): the hook must cost less than the key it saves.
- The texture replacement keys (`-TextureReplace`, by content hash) must not change.

#### 19.6.4 Increments, gains and effort

1. Measure: the binding resets per frame by cause (the frame start, a resolve's watch, a CPU write's watch) and the keys they cost; the uploads in the race's streaming against the frame times (19.2.3).
2. No reset by resolves into ranges that no bound texture reads, and no reset at the frame start for bindings whose texture is valid: the binding check of N4 (`d3d12_reuse_draw_bindings_check`) with its perturbation proof. This is the cheap half and may give most of the gain.
3. Object-keyed bindings with the generation count, behind the same check.

Close gates as in 19.3.4. Expected: the textures zone from about 0.40 to about 0.15 ms a race frame on this PC (the keys 0.17 ms and most of the binding updates); on the Ally X about 0.3 to 0.5 ms of the render thread's 4.4 to 6.1 ms, CPU headroom rather than fps on a device that waits for its GPU. No stutter gain: the warm races have no frame over 25 ms. Effort: step 2 1 to 2 person-weeks, step 3 3 to 5.

### 19.7 Item 5: what else the profile shows

#### 19.7.1 Resolves without the guest's memory layout

The largest item the profile shows is not on the CPU. Every resolve still writes its destination in the guest's memory layout, tiled and scaled (`D3D12RenderTargetCache::Resolve`, [render_target_cache.cpp:1481](../../work/runtime/src/graphics/d3d12/render_target_cache.cpp), into the scaled resolve buffer), because the CPU might read it (the readback on demand serves it from there, 16.5) and because the texture cache loads the textures that sample it from there (`MarkRangeAsResolved`, [cache.cpp:336](../../work/runtime/src/graphics/pipeline/texture/cache.cpp)). At a fractional scale `native_rt_surfaces` bit 32 also writes the sampling textures straight from the host render target (`ResolveHostIntoTextures`, render_target_cache.cpp:6945, 18.12), so their loads go, but the guest layout is still written. N3 measured that moving the work (16.8, step A) saves nothing on this PC; the saving needs a write removed. Section 8's resolves row planned that step: "later a resolved range is written only when the CPU touches it". It is also the open half of item (e) of the integrated GPU proposal behind [FSR plan, 13](fsr-plan.md#13-uniform-draws-off-the-whole-scales-and-rcas-with-the-ratio-26-october-2026), whose transfers half (13.1) is done.

**Design.**

1. Measure first: the GPU time of a full-screen resolve split into the guest-layout write, the texture writes and the transfers around them, at 1x and 3/2, on this PC; the owner runs the same trace on the Ally X.
2. The textures that sample a resolved destination are always written from the host render target (bit 32 at every scale, 1x included), so the full-screen loads of 19.2.3 go.
3. The guest layout of a destination is written only on demand: when the readback on demand finds the CPU touching the range (the page protection that serves readbacks today), when a texture samples it through a key that has no host texture (5 to 9% of the resolves from private surfaces found none, 18.12), or when a later resolve or transfer reads it. Until then the destination is marked stale in the guest layout, as the readback on demand marks the guest memory today.
4. Where the destination texture has the host render target's own format at the same scale (the scene colour as `8_8_8_8`), the write becomes a copy or a view instead of a compute encode; with the native texture cache (19.6) a texture can view the resolved surface through a component swizzle instead of a byte swap.

**Risks.** A reader of the guest layout that the on-demand write does not see: the 77 texture-lock sites (2.7), GPU readers through another format or size of the same range, and memexport (none in the scenes measured). The texture byte check and the resolve byte check of 18.12 (`native_rt_resolve_check`, `native_rt_surfaces_check`) compare the skipped writes' bytes, computed on sampled frames, with what the on-demand write produces; the photo save and `photo_view` cover the CPU readback. `-Rov` and the PM4 RTV path keep writing the guest layout (the change is behind the native path's switches).

**Increments and gates.** Step 1 is a measurement. Steps 2 and 3 each: a step gate with the byte checks of 18.12 and a perturbation proof added to them, a perf run at 1x and at 3/2 on this PC with the GPU time per pass, the freeze's `parity.py` and a 1:1 capture beside RTV; a close gate with smoke (photo save and load included), perf with three runs, `parity.py` at 1x and 2x, the RTV and `-Rov` smokes with `-Look "original,taa=0"`, and the owner's Ally X run at 1x and 3/2. Step 4 the same, after 19.6's step 3.

**Expected.** By 19.2.3 and N3's finding that the texture writes cost about what the loads did: on the Ally X at 3/2, about 1.5 to 3 ms of the 15.9 ms GPU frame (the guest-layout half of the full-screen resolves, and more where step 4 turns encodes into copies), from about 60 fps to about 66 to 78 fps in the race at the panel's resolution beside what 13.1's transfers already gave; at 1x about 0.8 to 1.2 ms of 8.3 ms (+10 to 15%). On this PC about 0.25 to 0.4 ms of 3.5 ms at 3/2 and 0.3 to 0.5 ms of about 5 ms at 2x, where the race is GPU bound (+5 to 10%). Inferred until step 1's split is measured. Effort: 3 to 6 person-weeks, step 4 with item 4.

#### 19.7.2 The rest

- **The guest's own CPU time.** The busiest guest thread is the title's render thread; its profile is the title's presentation code and the recompiled vector helpers, with the XDK's `RunCommandBuffer` at about 8% (19.2.4). An HLE of the XDK setters saves little; the vector helpers are the recompiler's code generation, outside the GPU layer (19.9).
- **Readbacks on the CPU path.** None in the race or the replay on the native path (readback on demand, 16.5; `demand_readback.requests` 0 in every run of 19.2); the photo save reads its picture back once. Nothing to do.
- **Memory.** The native path's render targets take about 200 to 245 MB at 1x, 443 MB at 3/2 and 580.5 MB at 2x on this PC (`rt_memory`, 17.15 and the runs of 19.2; 338 MB at 3/2 on the Ally X), the texture cache is capped at 384 MB soft and 768 MB hard (`texture_cache_memory_limit_*`, cache.cpp:30 to 47) and the shared memory buffer is 512 MB, on every GPU alike. A budget from the adapter's `QueryVideoMemoryInfo` that sets the texture cache's limits and warns before a 4K render target set would not fit is a small item (about 1 person-week), worth doing with N5's 4K work, not a player-facing gain on the two test devices (12 GB of video memory on this PC, 24 GB shared on the Ally X).
- **The GPU.** Not N6's subject, but the Ally X's lever (17.25): GTAO and the windshield take 1.3 to 1.6 ms of its 7.5 to 8.3 ms a race frame. An "Ally X performance" look preset is already a candidate (current status, open issues).

### 19.8 The proposed order

The owner's order is stutter first, then CPU headroom on the ROG Xbox Ally X. The Ally X waits for its GPU in every mode (17.21, 17.25), so CPU time it saves reaches the frame only through the shared power budget (1 to 3%), while the resolves of 19.7.1 hold 19 to 28% of its GPU frame. The proposal therefore puts the resolves second, as the Ally X's headroom, and the CPU items after them:

1. **Item 1, increments 1.0 and 1.1** (counters and the cold-cache cell, compiles off the render thread): the stalls players meet first, gone in about 1 to 2 person-weeks, without waiting for N5. Together with step 3 it lets a public preview start without a shipped cache.
2. **Item 5's resolves (19.7.1), steps 1 to 3**: the Ally X's largest lever, at its panel's resolution most of all, about 3 to 6 person-weeks; step 1's measurement can run beside item 1.
3. **Item 1, increments 1.2 to 1.5** (the offline pass to DXIL with DXC and its cache, N5's increment 7, on N5's increments 1 and 2; the disc's pixel shaders; the pipeline manifest and library; early creation of missed pipelines): the mods on time and no pipeline created in play, about 8.5 to 15 person-weeks. 1.2 starts when N5's increments 1 and 2 have landed, 1.3 and 1.5 can go before it; 1.5 first if the cold-driver measurement (19.3.6) shows pipeline stalls of more than a few milliseconds.
4. **Item 4, steps 1 and 2** (the binding resets): the cheapest CPU saving, on both devices, about 1 to 2 person-weeks.
5. **Item 4, step 3, with 19.7.1's step 4**, if step 2 leaves most of the keys or the resolved textures' copies are still worth removing.
6. **Item 2**, starting with its measurement (19.4.4 step 1), only if the Ally X's battery runs (17 W) show the CPU time still worth fps after the items above, or this PC's headroom is wanted for 4K and high draw loads; otherwise it moves to a later stage.

N6 by this order is about 17 to 30 person-weeks without item 2 and 27 to 46 with it, besides N5's increments 1 and 2.

### 19.9 What N6 leaves out

- **Multithreaded command recording** (19.5): no thread limits a frame on either device; it has a trigger condition instead.
- **The guest's CPU time outside the D3D layer**: the recompiled vector helpers and the title's presentation code belong to the recompiler and the codegen, not to the GPU layer.
- **The vertex shaders' keys before the title draws with them** (19.3.3): their offline pass covers what the storage has seen; the XDK's rewrite is not re-run offline unless a measurement shows their stalls.
- **ROV and the PM4 RTV path**: no N6 change targets them; `-Rov` stays the uncorrected reference and the PM4 RTV path stays selectable (AGENTS.md). Their no-change proofs stay in every close gate.
- **GPU-side rendering changes** (the look, GTAO, the windshield's cost, upscalers, HDR10): N5's items and the look presets. The resolves of 19.7.1 are in N6 because they remove the guest's memory layout from the frame, not because they change the picture.
- **Vulkan or another API**: the fork removed the Vulkan backend (11), and N6 does not bring one back.
- **Ubershaders** as the answer to compiles: the Xenos microcode's flow control and fetch rules make one shader for all of them impractical; precompilation and early creation are the route.

### 19.10 Decisions for the project owner

1. The scope and the order of 19.8: the resolves of 19.7.1 second, as the Ally X's headroom, ahead of the CPU items; and whether item 2 is in N6 or a later stage.
2. Whether part 1 of item 1 (the game's own look for a shader's first frames, until its mod is compiled) is acceptable, judged on the 1:1 crops of increment 1.1's gate.
3. Whether item 1's pipeline manifest (hashes, modification bits and fixed function state of game shaders, no microcode) may be committed, after the review of [sources and licensing](../sources-and-licensing.md).
4. Whether DXC (`dxcompiler.dll`, `dxil.dll`) may ship to players for item 1's offline pass, after the same review.
5. Where the offline pass runs: in the GUI launcher with its own progress page, or in the runtime's start-up behind a splash screen.

The owner's answers of 27 October 2026:

1. The order of 19.8 as proposed: item 1's increments 1.0 and 1.1 first, then the resolves of 19.7.1, then the rest of item 1 and item 4. Item 2 moves to a later stage unless the Ally X's battery runs still show CPU-bound frames after them.
2. The game's own look for a shader's first frames, until its mod is compiled, is acceptable (still judged on increment 1.1's 1:1 crops).
3. The manifests of item 1 may be committed: hashes, modification bits and fixed function state of game shaders, with no microcode and no translation of a game shader, reviewed like the other metadata in [sources and licensing](../sources-and-licensing.md).
4. DXC may ship to players: setup fetches `dxcompiler.dll` and `dxil.dll` from Microsoft's official DirectX Shader Compiler release on GitHub, pinned like the FidelityFX SDK and the GameInput package, and the build puts them beside the game (chosen over FXC, which needs nothing shipped but stops at Shader Model 5.1). Before increment 1.2 lands: the owner's approval of the exact release (version, size, license files), the license review in [sources and licensing](../sources-and-licensing.md), and the notices (`LICENSE-LLVM`, `LICENSE-MS` or whatever that release carries) in the third-party notices and the exports.
5. The offline pass runs in the GUI launcher, on a page of its own with progress, once after an install or an update; it can be skipped, and what is left continues in the background.
