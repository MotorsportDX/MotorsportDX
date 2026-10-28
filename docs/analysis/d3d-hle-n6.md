# Native port, stage N6: a renderer that behaves like a PC game

Section 19 of the [D3D-level HLE design](d3d-hle.md). Sections 1 to 15 are in [d3d-hle.md](d3d-hle.md), section 16 (stage N3) in [d3d-hle-n3.md](d3d-hle-n3.md), section 17 (stage N4) in [d3d-hle-n4.md](d3d-hle-n4.md) and section 18 (stage N5) in [d3d-hle-n5.md](d3d-hle-n5.md).

## 19. Stage N6 design, 27 October 2026

Status: the project owner approved the plan on 27 October 2026 (19.10). Increments 1.0 and 1.1 of item 1 are built (19.3.7); the rest is a plan. The measurements of 19.2 ran on local main's build of 27 October 2026 without code changes.

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

- A pipeline creation with a cold driver cache, on this PC and the Ally X. Clearing the driver's cache is the user's system state; a build whose translator emits one changed instruction per shader (a different bytecode, so a driver miss) measures it without touching the cache. Since 28 October 2026 `dxbc_driver_cache_salt=N` does that on any build (19.3.8); this PC's result is in 19.3.8, the Ally X's is open.
- The vertex shaders' translations in play, once the counters of increment 1.0 time them.

#### 19.3.7 Increments 1.0 and 1.1, 27 October 2026

Built on N5's shader contract, increment 1 (`99e3be6`). The step gates ran on `I:\FM4-build-usa` with `matrix.py --tier cold --native --fast-route` at 240 Hz and 1x, the launcher's look `aces`, one run a cell.

**1.0, the counters and the cold-cache cell.**

- The perf trace has `shader_work`: per kind its count, time, longest piece and pieces over 4 ms. The kinds on the draw path are the vertex and pixel shaders' DXBC translations, the look mods' FXC compiles and cache loads where a draw is issued, the pipelines created for a draw and the post passes' compiles. The compile workers' compiles and pipelines are counted off it. `long_frames` lists every frame over 50 ms with the draw path's shader work inside it, and of that the FXC time of look mods and of post passes ([traces](../e2e/traces.md#performance-trace-rex_perf_trace_file)). The start-up's storage (`InitializeShaderStorage`) and hot reload are not counted.
- `e2e.py --cold-cache` runs on empty caches (`cache_root` and the override cache in `<out>\cache`), `--cache-from` on a copy of an earlier run's, and `summary.json` splits the shader work by segment: `start` (the menus and loads), each phase, and `after_<phase>`. The `matrix.py` tier `cold` runs `career` on empty caches, then `photo` on the caches `career` left: the first two sessions of a fresh install, as in 19.2.1.
- `compare.py` fails a cold candidate that compiled a look mod on the draw path or has a frame over 50 ms with one. With both sides cold, more translations, pipelines or cache loads over 4 ms, or more frames over 100 ms, only warn. The plan's FAIL on any piece over 4 ms became this WARN. Pipelines over 4 ms are part 5's subject (1.5), and in these cells their time follows the driver's own cache (below).

**1.1, compiles off the render thread** (part 1 of 19.3.2).

- `ShaderCompileQueue` ([shader_compile_queue.cpp](../../work/runtime/src/graphics/d3d12/shader_compile_queue.cpp)), behind `shader_override_async` (on by default). A mod that a draw meets and the cache does not hold compiles on one to four workers: a quarter of the logical processors, at below normal priority. `ShaderOverrides::Compile` hands over a job (the source, the defines with a contract entry's params in their knob slots, the DXBC for the signature check, the cache file) instead of running FXC, so classic mods and the contract's entries take the same path.
- No variant is added. The plain look is the modded translation itself, which keeps the DXBC it was translated to until the swap. The plan drew with a plain modification. With the same translation there is no second translation or pipeline to create. The contract's `native_plain` and the A/B's `hlsl_override_disabled` stay what they were, and the pipeline storage keeps the mod's own description.
- `PipelineCache::SwapCompiledMods` runs on the thread that creates pipelines: the replay before each submission with the draw back end, `EndSubmission` otherwise. It puts a compiled binary into its translation, so pipelines created from then on take the mod. The workers then recreate the pipelines that existed, ahead of the other compiles. Their states are exchanged at the next call, and the old ones are retired after their submission, as hot reload does.
- The start-up's translations still compile before the title runs. Hot reload and `async_shader_compilation` keep the compile in place.

The cold cells by segment: 1.0 (`n6c10`, the build of `1b34af0` on `6c2dfa0`), 1.1 (`n6c11`, `6a31bfa`) and the final 1.1 (`n6c12`, `9b13d63`, the code of `c5b7511`), and RTV's career on 1.0 (`n6cr10`, `3125cfa`, previous main's code) and 1.1 (`n6cr11`, `6a31bfa`), run before RTV left the gates on 27 October 2026 (`d2efb01`). Each cell has a row for the whole run, the menus and loads before its first phase, and the race; `photo` also has the way into photo mode after the race.

| Cell | Segment | Mod compiles on the draw path | On the workers | Frames over 50, 100, 250 ms | Over 50 ms with a mod compile | Time in frames over 50 ms | Longest frame | Pipelines created in play (count, time, over 4 ms) |
|---|---|---|---|---|---|---|---|---|
| `n6c10-240-career` | run | 63, 23.7 s | 0, 0.0 s | 0, 0 ms | 23, 19, 13 | 15 | 27.0 s | 9,706 ms | 533, 2,030 ms, 177 |
| `n6c10-240-career` | menus and the load | 62, 23.6 s | 0, 0.0 s | 0, 0 ms | 20, 17, 13 | 14 | 26.7 s | 9,706 ms | 488, 1,983 ms, 173 |
| `n6c10-240-career` | race | 0, 0.0 s | 0, 0.0 s | 0, 0 ms | 0, 0, 0 | 0 | 0.0 s | 35 ms | 30, 22 ms, 2 |
| `n6c11-240-career` | run | 0, 0.0 s | 62, 27.7 s | 81, 658 ms | 18, 9, 3 | 0 | 2.9 s | 710 ms | 531, 1,872 ms, 177 |
| `n6c11-240-career` | menus and the load | 0, 0.0 s | 60, 26.9 s | 79, 644 ms | 17, 8, 3 | 0 | 2.8 s | 710 ms | 473, 1,805 ms, 171 |
| `n6c11-240-career` | race | 0, 0.0 s | 1, 0.7 s | 1, 8 ms | 0, 0, 0 | 0 | 0.0 s | 19 ms | 38, 48 ms, 5 |
| `n6c12-240-career` | run | 0, 0.0 s | 63, 26.7 s | 73, 798 ms | 17, 10, 3 | 0 | 3.2 s | 744 ms | 538, 1,960 ms, 184 |
| `n6c12-240-career` | menus and the load | 0, 0.0 s | 60, 25.9 s | 70, 780 ms | 15, 9, 3 | 0 | 3.0 s | 744 ms | 473, 1,885 ms, 178 |
| `n6c12-240-career` | race | 0, 0.0 s | 1, 0.7 s | 1, 9 ms | 0, 0, 0 | 0 | 0.0 s | 19 ms | 42, 47 ms, 4 |
| `n6c10-240-photo` | run | 15, 4.8 s | 0, 0.0 s | 0, 0 ms | 20, 9, 8 | 6 | 7.3 s | 1,457 ms | 332, 567 ms, 44 |
| `n6c10-240-photo` | menus and the load | 12, 3.8 s | 0, 0.0 s | 0, 0 ms | 11, 6, 5 | 4 | 5.6 s | 1,457 ms | 156, 483 ms, 38 |
| `n6c10-240-photo` | race | 1, 0.7 s | 0, 0.0 s | 0, 0 ms | 1, 1, 1 | 1 | 0.7 s | 707 ms | 29, 47 ms, 3 |
| `n6c10-240-photo` | after the race | 2, 0.3 s | 0, 0.0 s | 0, 0 ms | 6, 2, 2 | 1 | 0.9 s | 350 ms | 144, 34 ms, 3 |
| `n6c11-240-photo` | run | 0, 0.0 s | 15, 6.2 s | 27, 142 ms | 17, 5, 2 | 0 | 2.3 s | 724 ms | 318, 507 ms, 46 |
| `n6c11-240-photo` | menus and the load | 0, 0.0 s | 12, 4.8 s | 24, 117 ms | 11, 4, 1 | 0 | 1.7 s | 724 ms | 150, 429 ms, 38 |
| `n6c11-240-photo` | race | 0, 0.0 s | 1, 0.9 s | 1, 16 ms | 0, 0, 0 | 0 | 0.0 s | 12 ms | 27, 39 ms, 5 |
| `n6c11-240-photo` | after the race | 0, 0.0 s | 2, 0.4 s | 2, 9 ms | 4, 1, 1 | 0 | 0.5 s | 266 ms | 138, 37 ms, 3 |
| `n6c12-240-photo` | run | 0, 0.0 s | 16, 9.2 s | 28, 267 ms | 16, 5, 2 | 0 | 2.2 s | 689 ms | 335, 607 ms, 51 |
| `n6c12-240-photo` | menus and the load | 0, 0.0 s | 13, 7.3 s | 25, 207 ms | 12, 4, 1 | 0 | 1.7 s | 689 ms | 160, 525 ms, 44 |
| `n6c12-240-photo` | race | 0, 0.0 s | 1, 1.6 s | 1, 19 ms | 0, 0, 0 | 0 | 0.0 s | 25 ms | 27, 46 ms, 4 |
| `n6c12-240-photo` | after the race | 0, 0.0 s | 2, 0.3 s | 2, 41 ms | 2, 1, 1 | 0 | 0.4 s | 283 ms | 145, 35 ms, 3 |
| `n6cr10-240-career` | run | 77, 28.3 s | 0, 0.0 s | 0, 0 ms | 23, 17, 12 | 16 | 31.4 s | 8,700 ms | 536, 785 ms, 30 |
| `n6cr10-240-career` | menus and the load | 74, 27.8 s | 0, 0.0 s | 0, 0 ms | 19, 15, 11 | 13 | 30.6 s | 8,700 ms | 474, 722 ms, 27 |
| `n6cr10-240-career` | race | 1, 0.4 s | 0, 0.0 s | 0, 0 ms | 1, 1, 1 | 1 | 0.5 s | 469 ms | 40, 40 ms, 2 |
| `n6cr11-240-career` | run | 0, 0.0 s | 77, 38.6 s | 101, 142 ms | 12, 7, 1 | 0 | 2.0 s | 703 ms | 539, 391 ms, 8 |
| `n6cr11-240-career` | menus and the load | 0, 0.0 s | 74, 37.9 s | 98, 139 ms | 11, 6, 1 | 0 | 1.8 s | 703 ms | 472, 328 ms, 5 |
| `n6cr11-240-career` | race | 0, 0.0 s | 1, 0.6 s | 1, 1 ms | 0, 0, 0 | 0 | 0.0 s | 28 ms | 47, 42 ms, 2 |

- **No compile is left on the draw path.** The first career compiled 63 mods there (23.7 s, the longest 2,475 ms), and 15 of its frames over 50 ms had one, the longest 9.7 s. With the final 1.1 the workers compile them and no frame has a compile. The frames over 50, 100 and 250 ms went from 23, 19 and 13 to 17, 10 and 3, and the time inside them from 27.0 to 3.2 s. The longest frame is the boot's first frame of 744 ms, which every run has, warm or cold. The new track's race frame of 707 ms (19.2.1's 716 ms) is gone: that race's longest frame is 25 ms, and the way into photo mode went from 350 to 283 ms.
- **What is left** is the title's own frames (the boot's first frame of 710 to 745 ms in every run, warm or cold, and loading screens of about 100 ms) and the pipelines created in play. In `n6c12`'s career 538 pipelines took 2.0 s, 184 of them over 4 ms, and a second with 62 of them (577 ms) holds a frame of 432 ms. Their time follows the driver's cache. `n6k11` ran right after `n6c11`, on the same shaders with `native_shader_contract=true`, and created 536 pipelines in 226 ms, 2 of them over 4 ms (`n6k12` after `n6c12`: 530 in 427 ms). That is 19.3.6's cold-driver measurement, and part 5 (1.5) moves those creations ahead of the replay.
- **The swap.** In `6a31bfa` the replay recreated the existing pipelines itself, about 1 ms of creation a call: 81 in 658 ms in `n6c11`'s career, up to 59 ms each. In `n6k11` one took 619 ms and held a frame of 588 ms in the load. `9b13d63` moved the recreation to the workers, so the replay only exchanges states: in `n6c12`'s career the workers recreated 73 pipelines in 798 ms, up to 63 ms each, off the draw path.
- **The game's look, and for how long.** The mods queued in a load wait in bursts: 3 to 4 s with up to 17 pending at 1x, and 6 s with up to 25 at 2x in the race's load. The workers' compiles take a median of 167 ms and at most 3.3 s at below normal priority (FXC on the render thread: at most 2.5 s). The contract's entry `E62CFDF09D758B6C` (the glass, with its params) compiled on a worker in 553 ms in `n6k11`.
- **Translations** (19.3.6): the vertex shaders' DXBC translations in play are not stalls, 138 in 8 to 9 ms in the first career with the longest 0.5 ms. The pixel shaders' are 230 in 19 ms with the longest 0.4 ms. The post passes' 25 compiles (0.87 s) run before the title's first frame.

**The game's look in 1:1 crops at 2x** (the owner's decision 2 of 19.10). The race scenario at 2x with the AI driver ran on empty caches and on the BuildRoot's, on `6a31bfa`, with captures every 2.1 s (`D:\FM4-n6
uns
6x11-race-cold2x` and `-warm2x`, crops in `D:\FM4-n6\crops`).

- Capture 36 is the intro's shot behind the Abarth in both runs. In the cold run, five mods were still compiling at that moment (the two cockpit interiors, the scene's motion blur, the chrome rims and one more), and the pipelines of those finished in the second before were still being recreated. The car paint and the chrome trim had been swapped in 0.5 and 1.1 s before. Beside the warm run, the cold crop of the car's rear shows the badges and the exhaust tips lighter; the paint, the lights and the rest of the frame match.
- Capture 38, two seconds later, is the countdown in the cockpit. Every mod had been swapped in, and the dashboard, its gauges and the car ahead match the warm run.

**Close gate** (`I:\FM4-build-usa`, 240 Hz and 1x unless noted, the look `aces`; the owner's gate policy of 27 October 2026, `d2efb01`: native baselines on the previous main, `-Rov`'s proof the 1x freeze parity, RTV out of the gates). The baseline `n6b10` is `3125cfa`, the code of the previous main `b09759f` (1.0 on the shader contract's increment 1). The candidates are `n6c11` (`6a31bfa`) and the final `n6c12` (`9b13d63`). The final commit changes only where the swap recreates pipelines, which a warm cache never reaches.

| Check | Result |
|---|---|
| Cold tier, final (`n6c12` against `n6c10`) | `career`: no compile on the draw path, no frame over 50 ms with one, the race's p99 5.16 against 6.02 ms; `compare.py`'s FAIL is the whole run's p99 (33.24 against 16.99 ms), which one 30 fps menu window among about 100 decides (`n6c11` on the same code read 17.15 ms), so the cell passes by the per-phase rule. `photo`: WARN on capture statistics and on pipelines over 4 ms (44 to 51) |
| Contract cell, final (`n6k12` against `n6k11`) | the entry `E62CFDF09D758B6C` compiled on a worker, 88 pipelines recreated there in 71 ms, the longest frame the boot's 695 ms; WARN on pipelines over 4 ms (2 to 12) |
| Smoke, native (`n6s12` and `n6s11` against `n6s10`) | 3 of 3 each. `career` and `photo_view` PASS; `photo` WARN on the whole run's waits and on capture statistics. Races 235.7 to 237.4 fps against 236.5 to 238.8, p99 5.0 to 5.5 ms on both sides |
| Perf, native, three runs (`n6p11` against `n6p10`) | the race 239.0 against 238.5 fps, p99 4.88 against 4.86 ms, GPU 2.19 against 2.21 ms a frame; photo mode 29.95 fps on both; no shader work in play on either side and the same frames over 50, 100 and 250 ms (10, 4 and 2); WARN on capture statistics |
| Freeze parity, native, 2x (`n6z12` against `n6z10`) | PASS, draws −0.0% and +0.7%. `n6z11`'s second run timed out reading the rewind menu that its capture shows (an OCR miss); its first run passes against both baseline runs |
| Freeze parity, native, 1x (`n6y11` against `n6y10`) | PASS, draws −0.6% and 0.0% |
| `-Rov`, freeze parity at 1x (`n6v12` against `n6v10`) | PASS, draws −0.0% and +0.1%; `-Rov` creates no shader overrides (no such line in its log) |
| `-Rov` and RTV smokes with `original,taa=0` (`n6o11` against `n6o10`, `n6r11` against `n6r10`, run before `d2efb01`) | 3 of 3 each; `career` PASS on both; `photo` WARN, its race fps following the AI race's draws (RTV 2,053 against 3,248 draws a frame) |
| Error keys | the same three as main in every cell |
| Tests | `test_e2e_tools.py` (44) and `test_pipeline.py` (28) pass; the builds' shader mod, post pass and shader contract tests pass. `launcher_tests` is not touched |

Not covered: the ROG Xbox Ally X (an export ships the build computer's cache, so its first sessions need an export without one), 60 and 120 Hz, and a cold driver cache on purpose (19.3.6).

#### 19.3.8 Increment 1.5, 28 October 2026

Built on local main `c4607b8` (item 4) as `f76f8e6` and `b4b6de7`, snapshot `n6f1`; rebased onto main `1803b36` as `0d6072e` and `da03738` (snapshot `n6f2`), with the active window below as `09578aa` (snapshot `n6f3`).

**What it does** (part 5 of 19.3.2).

- `PipelineCache::QueueEarlyPipeline`, on the render thread after `TranslateShadersForDraw`: the description of the pipeline the replay will look up for the draw (`GetCurrentStateDescription` without a root signature or a geometry shader, the function the draw back end's check compares on both sides). It runs only in an active window (below). When it differs from the previous draw's and its hash is new (the render thread's own set, seeded with the storage's pipelines), the pipeline goes to the creation threads (`d3d12_pipeline_creation_threads`, three quarters of the cores) in the order of the draws, after their own queue and outside what `EndSubmission` awaits.
- The replay's `ConfigurePipelineDescribed`, on a miss, takes the early pipeline with the same description: created (no wait), being created (it waits for the rest), or still queued (it creates it itself, ahead of the queue). A description that the render thread did not see creates on the replay as before.
- Look mods compiled on the workers (1.1): `SwapCompiledMods` replaces a binary only while no creation thread creates an early pipeline (otherwise it tries again at the next submission) and also recreates the early pipelines created but not taken yet; the queued ones take the compiled binary.
- `d3d12_pipeline_early_creation` (on by default) turns it off for an A/B. It needs bindless resources and no hot reload or `async_shader_compilation`, as the draw back end does. `-Rov` and the PM4 RTV path create pipelines where the draw is issued and are not changed.
- Not done: the skip switch of 19.3.2 (a draw whose pipeline is missing after 2 ms dropped) and moving the translations to the creation threads. The waits left are below, and the translations in play are no stalls (19.3.7: 0.5 ms the longest).
- `dxbc_driver_cache_salt=N` (0 by default, no change) starts every DXBC translation with a dead write of N, so the driver's cache of earlier runs misses: the cold-driver measurement of 19.3.6 without touching the system's cache.
- The perf trace adds `shader_work.pipelines_early` (off the draw path), `shader_work.pipeline_waits` (on it) and `back_end.early_queued`, `early_ms` and `early` ([traces](../e2e/traces.md#the-draw-back-end-d3d12_draw_back_end)); `compare.py`'s cold rule counts the waits over 4 ms with the pipelines created on the draw path.

**Step gate** (`matrix.py --tier cold --native --fast-route` at 240 Hz and 1x, the look `aces`, one run a cell, each side with a salt no earlier run used: a cold driver cache).

| Cell | Segment | Pipelines created on the draw path (count, ms, over 4 ms) | Early pipelines (count, ms on the creation threads) | Replay's waits (count, ms, longest, over 4 ms) | Frames over 50, 100, 250 ms | Longest frame |
|---|---|---|---|---|---|---|
| `n6f1off-240-career` (off) | run | 533, 2,827 ms, 227 | 0 | 0 | 18, 8, 5 | 709 ms |
| `n6f1off-240-career` (off) | menus and the load | 475, 2,721 ms, 214 | 0 | 0 | 17, 7, 5 | 709 ms |
| `n6f1off-240-career` (off) | race | 37, 66 ms, 7 | 0 | 0 | 0, 0, 0 | 22 ms |
| `n6f1-240-career` (on) | run | 0 | 535, 5,093 ms | 59, 463 ms, 37 ms, 31 | 8, 3, 1 | 677 ms |
| `n6f1-240-career` (on) | menus and the load | 0 | 475, 4,969 ms | 48, 395 ms, 37 ms, 23 | 7, 2, 1 | 677 ms |
| `n6f1-240-career` (on) | race | 0 | 39, 69 ms | 6, 45 ms, 15 ms, 5 | 0, 0, 0 | 17 ms |
| `n6f1off-240-photo` (off) | run | 333, 807 ms, 72 | 0 | 0 | 16, 7, 3 | 660 ms |
| `n6f1-240-photo` (on) | run | 0 | 339, 1,051 ms | 24, 125 ms, 17 ms, 12 | 11, 6, 2 | 678 ms |

- **With a cold driver cache** a pipeline took 5.3 ms on average on the replay (n6c12's career, 27 October 2026: 3.6 ms, 184 over 4 ms). Early creation takes all of them off the draw path: of 535 in the first career the replay found 476 created and waited for 59, 463 ms in all (from 2,827 ms), the longest 37 ms (from 45 ms). The frames over 100 ms went from 8 to 3 and over 250 ms from 5 to 1, the boot's first frame (677 ms) being the one left. On the creation threads the pipelines took 5.1 s, 9.5 ms each: the driver's compiles in parallel run slower each, off the draw path.
- **The back end's check**, on a cold career with early creation (`n6f1-check`, salt 1503): 37,878,861 draws checked, no mismatch, no dropped draw; the replay took 531 early pipelines (473 created, 58 waited for).
- **The render thread** paid for the description at every draw before the active window: 0.042 to 0.045 µs a draw (0.07 to 0.09 ms a race frame of 1,700 to 2,200 draws). A warm race at 3/2 (`n6f1-r32`, one run) ran 234.7 fps with the render thread at 3.17 ms and the GPU at 3.49 ms a frame, against main's runtime on 28 October 2026 (`n6e-d0-32`, three runs: 232.7 to 235.2 fps, 3.18 to 3.30 ms, 3.48 to 3.53 ms). With a warm cache it queued nothing: the storage's pipelines are known.
- `compare.py` on the career: FAIL on the whole run's p99 (33.04 against 17.08 ms), which one 30 fps menu window decides as in 19.3.7 (the race's p99 5.26 against 5.11 ms); WARN on `wait_reg_mem`. The photo cell WARNs on capture statistics (the races differ). The captures of photo mode show the look's mods on both sides; parity at 1:1 is the close gate's.

**Close gate** (`I:\FM4-build-usa`, snapshot `n6f1`, the look `aces`; the lean close gate of the owner's decisions of 28 October 2026: baselines of the same day on main's runtime, `n6d0`; no 2x freeze parity, one race, and no `-Rov` or RTV cells; image-only cells two at once).

| Check | Result |
|---|---|
| Smoke, native, 3/2 (`n6fs32` against `n6es32`) | 3 of 3; `career` PASS (race 232.97 fps, p99 5.33 ms); `photo` and `photo_view` WARN on capture statistics only (the races differ) |
| Smoke, native, 2x, the launchers' plan (`n6fs2x` against `n6es2x`) | 3 of 3; `photo_view` PASS; `career` WARN on `wait_reg_mem` (race 191.04 fps, p99 7.95 ms), `photo` WARN on capture statistics |
| Freeze parity, native, 3/2 (`n6fp32` against `n6ep32`) | PASS. Run on its own copy of the BuildRoot's cache (`matrix.py --cache-from`) beside the perturbation run; the native capture at 1:1 beside main's shows the same look (`n6f-crops`) |
| The back end's check, perturbed (`n6f2-perturb`, a cold career with salt 1504, `d3d12_draw_back_end_check_perturb=1000`) | 37,113 pipeline mismatches, equal to the 37,113 comparisons perturbed; no other mismatch, no dropped draw. With a cold driver cache again: 533 early pipelines, 465 taken created and 68 waited for (606 ms) |
| Race at 3/2, one run (`n6f-32` against `n6e-d0-32`, three runs) | 234.1 fps, render thread 3.17 ms and GPU 3.46 ms a frame, textures zone 0.206 µs a draw; main 232.7 to 235.2 fps, 3.18 to 3.30 ms, 3.48 to 3.53 ms, 0.199 to 0.205 µs. The early description cost 0.046 µs a draw (0.084 ms a frame) before the active window |
| Tests | `test_e2e_tools` (47) and `test_pipeline` (29) pass |

The two image-only cells ran at once for the first time: the 3/2 parity (7.3 min) and the perturbation run (3.7 min) took 7 min of wall time together, about 4 min saved, with nothing shared but the snapshot (each its own run folder, profile copy and cache). The parity passed beside the other game, so the pair is usable for image cells; speed cells stay alone.

**The active window** (`09578aa`). After the rebase onto `1803b36`, smoke at 3/2 passed 3 of 3 (`n6gs32`, `career` PASS), and main's in-run A/B (`--arg ab_cvar=d3d12_pipeline_early_creation`, 90 s of race at 3/2, `perf_ab.py`) measured the description on every draw at 0.090 ms of render thread a race frame (95% interval 0.063 to 0.118 ms; `n6g-ab32`), with GPU and frame time unchanged. A warm race queues nothing, so the render thread now describes draws only for 1,000,000 draws (a few seconds) after a shader translated for a draw, a queued early pipeline or a pipeline the replay created itself (a miss it counts for the render thread). Each restarts the window; the first draws of a load always translate shaders or miss.

| Check (snapshot `n6f3`) | Result |
|---|---|
| In-run A/B at 3/2 (`n6f3-ab32`) | render thread 0.002 ms a race frame (−0.019 to 0.023), GPU 0.000 ms (−0.017 to 0.017), frame time 0.001 ms (−0.006 to 0.007): no cost left. One early pipeline queued in the run |
| Cold career, salt 1505 (`n6f3c-240-career` against `n6f1-240-career`) | 536 early pipelines; the replay took 454 created and waited for 73 (556 ms, 41 over 4 ms, the longest 39 ms; `n6f1` 59, 463 ms, 31; the always-on runs 58 to 68, 463 to 606 ms); 10 created on the draw path in 0.7 ms (none over 0.2 ms: missed by the window, their bytecode in the driver's cache of the run); frames over 50, 100, 250 ms 12, 5, 1 (8, 3, 1), the longest the boot's first (675 ms). `compare.py` WARN on the waits over 4 ms and the frames over 100 ms. The window adds 0.022 µs a capture while open |
| Cold photo cell (`n6f3c-240-photo` against `n6f1-240-photo`) | 334 early, 23 waits (116 ms, 9 over 4 ms; `n6f1` 24, 125 ms, 12); 62 on the draw path in 2.0 ms (none over 0.2 ms); frames over 50, 100, 250 ms 11, 5, 2 (11, 6, 2). WARN on `wait_reg_mem` only |

The cold cells differ between runs by the order the threads finish in: the career waited more than `n6f1` and the photo cell less. The pipelines the window misses are all fast, so it keeps what early creation is for. Not run on `n6f3`: smoke and parity, as the window changes only when the render thread describes a draw, not what the replay creates or draws.

**Rebased onto main `fbe6511`** (the shader contract's increment 6, whose vertex shaders are swapped in as the pixel shaders' mods are; `SwapCompiledMods` recreates early pipelines by both), snapshot `n6f4`: smoke at 3/2 passed 3 of 3 (`n6hs32` against `n6fs32`, `career` PASS, the race 237.38 fps, p99 5.06 ms). A cold career at 2x with `native_shader_contract=true`, `native_shader_contract_vertex_all=true`, a fresh salt and the back end's check (`n6f4-vcold`, beside main's `n5sc17-cold-240-career`): 35,064,814 draws checked, no mismatch, no dropped draw; 539 early pipelines, 475 taken created and 57 waited for (452 ms, 29 over 4 ms); 7 created on the draw path in 0.3 ms; frames over 50, 100 and 250 ms 10, 4 and 1 (main 10, 5 and 1, with 539 pipelines on the draw path, 54 over 4 ms). `compare.py` WARN: one vertex translation over 4 ms on the draw path (0 before).

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
- **Bindings reset often.** `BeginFrame` resets every binding each frame (329), and any texture whose watch fires sets `texture_became_outdated_`, which resets every binding at the next draw (543, 938): a resolve into a range a texture was loaded from, or a CPU write. The keys are recomputed about 1.5 times a draw (2,327 a race frame, 17.11); step 1's measurement (19.6.5) found that 98% of them follow the title's own fetch constant writes, not these resets.
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

Close gates as in 19.3.4. Expected: the textures zone from about 0.40 to about 0.15 ms a race frame on this PC (the keys 0.17 ms and most of the binding updates); on the Ally X about 0.3 to 0.5 ms of the render thread's 4.4 to 6.1 ms, CPU headroom rather than fps on a device that waits for its GPU. No stutter gain: the warm races have no frame over 25 ms. Effort: step 2 1 to 2 person-weeks, step 3 3 to 5. Step 1 (19.6.5) corrected this estimate: the resets are 1.8% of the keys, and 98% come from the title's own fetch constant writes, so step 2 saves about 0.01 ms a race frame and only step 3 could reach the keys' 0.17 ms.

#### 19.6.5 Step 1: the resets by cause, 28 October 2026

**The counters.** With `REX_N4_TEXTURE_KEYS=1` the perf trace's `texture_keys` object counts each binding reset by its cause ([traces](../e2e/traces.md)): the frame start (`BeginFrame`), a texture outdated by a GPU write (a resolve's `MarkRangeAsResolved`, memexport), by a CPU write (the shared memory's invalidation callback), by both since the last draw, and textures destroyed. For each it counts the valid bindings reset, the keys computed again for them, how many of those keys found the textures the binding had before, how many queued a load, and the valid bindings that held an outdated or missing texture at the reset.

**The measurement**, the build of `cd54c03` (`texture_binding_resets=0`, today's resets), the race and the career at 240 Hz with the fast route, one run a cell, with `REX_N4_TEXTURE_KEYS` and `REX_CP_SPLIT` (both make the keys zone slower). Per race frame at 3/2:

| | a frame | share of the keys |
|---|---|---|
| Keys computed again for used bindings out of sync | 3,018.5 | 100% |
| of these, the fetch constant had the binding's dwords (a compare, no key) | 1,401.3 | 46.4% |
| of these, the key changed | 1,610.2 | 53.3% |
| of the key changes, one of the binding's last four keys | 674.7 | 22.4% |
| Keys after a reset for a GPU write (28.8 resets, 54.9 valid bindings) | 54.9 | 1.8% |
| Keys after a reset for a CPU write (0.1 resets) | 0.5 | 0.02% |
| Keys after the frame start's reset (no valid binding at that point) | 0.0 | 0% |
| Keys after textures were destroyed | 0.0 | 0% |

Of the 54.9 bindings a resolve's reset reset each frame, 10.2 held the outdated texture, 9.8 found their old textures again and 23.8 queued a load check. 1x (3,048.0 keys, 55.5 from resets), 2x with the launchers' plan (2,998.4, 55.4), the career's race (3,347.9, 56.3) and its replay (3,307.5, 60.1) give the same picture. The race's uploads are 66 to 75 KB a frame with 1.4 to 1.7 invalidations a frame, the worst race frame 10.0 to 11.8 ms (17.9 ms in the career's race), as in 19.2.3.

**The open questions of the handoff, answered:**

1. **The share of the keys.** The resets cost 1.8% of the race's keys, nearly all of them after resolves; the frame start's reset finds no valid binding, and CPU writes and destroyed textures are rare. The other 98% follow the title's own fetch constant writes: 1,401 a frame rewrite the binding's dwords, which `texture_bindings_keep_same_dwords` already turns into a compare, and 1,610 change the key, which no reset policy saves. Step 3 targets these, not step 2. Item 5 (b)'s deferred guest layout writes fire no watch: the watches fire at the resolve, in `MarkRangeAsResolved`, as before (b), so the texture cache sees the resolve when it did before.
2. **Validity without a key.** A binding kept across a reset is valid when its textures are those a lookup of its key gives (`FindTexture` of the key after `ResolveTextureKey`, which applies the scaled and scene resolve state and the host size limits) and are up to date (the texture's outdated mask under the global lock). The key changes only with the fetch constant's dwords, which the in-sync mask already tracks; the lookup changes only when a texture is destroyed (which still resets every binding) or when a resolve changes the range's state, and that resolve's watch makes the texture in that range outdated. The binding check compares exactly that (19.6.6). A generation count is not needed for step 2.
3. **A resolve's reset by range.** Yes, by the textures' state instead of the range: once the watches have fired, the bindings that hold an outdated texture are the ones the resolve hit. That set is what a range test would give, since a watch fires only for textures that overlap the range, and needs no range passed through. It resets 13.3 bindings a race frame instead of 54.9.
4. **Measuring the saving.** By the textures zone a draw and the render thread's busy time a frame on capped races, three runs each on one binary (19.6.6). The Ally X's trace is not needed for this step: the saving is about 0.01 ms a race frame on this PC.

#### 19.6.6 Step 2: resets of stale bindings only, 28 October 2026

**The design**, in `cd54c03`, off by default. `texture_binding_resets` is a bit mask:

- **Bit 1**: a texture that became outdated (`texture_became_outdated_`, now with the cause's bits) resets only the valid bindings whose texture or signed texture is outdated, or missing where the binding's signs need it (`GetBindingsToReload`). The other bindings keep their keys and textures.
- **Bit 2**: the frame start resets only those same bindings.
- Textures destroyed by the memory limits still reset every binding.

**The proof.** `d3d12_reuse_draw_bindings_check` now also checks, after the loads of every draw, each used valid binding: its textures must be those a lookup of its key gives and must not be outdated. A binding met while another texture has just become outdated counts as racing, not as a mismatch: the next draw's reset covers it. A mismatch is put right (the binding reset, or the texture loaded), so each is counted once. `texture_binding_resets_perturb=N` leaves every Nth binding out of the reset for an outdated texture, which the check must catch. The checks, the build of `cd54c03` at 240 Hz, the career at 3/2 unless named:

| Cell | Checks | Key mismatches | Other texture | Outdated | Racing |
|---|---|---|---|---|---|
| `texture_binding_resets=0` | 36,930,778 | 0 | 0 | 0 | 3 |
| `texture_binding_resets=3` | 38,252,216 | 0 | 0 | 0 | 3 |
| 3, photo mode at 3/2 | 30,715,909 | 0 | 0 | 0 | 4 |
| 3, the career at 2x with the launchers' plan | 29,941,051 | 0 | 0 | 0 | 6 |
| 3, both perturbations every 1,000th | 36,060,923 | 36,060 of 36,060 perturbed | 0 | 138 of 306 left out | 2 |

The perturbed run catches every changed key and 138 of the 306 bindings left out; the other 168 never reached a draw with their outdated texture: a fetch constant write with other dwords rebuilt the binding first, the frame start's reset (bit 2) covered it, or another binding's load brought the shared texture up to date. The check does not count these three apart.

**The saving**, the race at 240 Hz with the fast route, three runs each on one binary, without the key trace, medians:

| Scale | Mode | Textures zone, ms a frame (3 runs) | median | µs a draw | Render thread, ms a frame | GPU frame, ms |
|---|---|---|---|---|---|---|
| 3/2 | 0 | 0.374, 0.355, 0.372 | 0.372 | 0.195 | 2.99 | 3.44 |
| 3/2 | 3 | 0.368, 0.363, 0.367 | 0.367 | 0.191 | 2.95 | 3.44 |
| 1x | 0 | 0.389, 0.378, 0.379 | 0.379 | 0.193 | 3.01 | 2.33 |
| 1x | 3 | 0.360, 0.356, 0.364 | 0.360 | 0.192 | 2.85 | 2.25 |

The textures zone falls by 0.005 ms a race frame at 3/2 and 0.019 ms at 1x (0.004 and 0.001 µs a draw), against the 0.25 ms that 19.6.4 expected: the 42 keys a frame that the mode saves are 1.4% of the keys. The render thread's 0.04 and 0.16 ms are within its runs' spread (1x mode 0's first run had 2,062 draws a frame against 1,882 to 1,968).

**Alternatives** considered:

- A reset by the resolve's range (passing the range from `MarkRangeAsResolved` to the texture cache): the same bindings as the outdated state, with more plumbing.
- A generation count a texture: needed for step 3's object-keyed bindings, not here.
- A per-binding cache of the last four dword sets and their textures, which would skip the key and the lookup for the 675 key changes a frame that return to a recent key: at most about 0.05 ms a race frame on this PC, below the 0.1 ms bar of 19.7.3.

**Risks.** The frame start's reset also retried a texture descriptor that could not be created; with bit 2 a binding whose descriptor failed keeps the failure until its fetch constant changes or its texture becomes outdated. This was not seen in the runs above.

**The main session's decision of 28 October 2026**, taken while the project owner was away: `texture_binding_resets` stays off by default as an experiment switch, with the counters and the extended binding check; steps 1 and 2 close without a step 2 gate. Step 3 (object-keyed bindings) moves to a later stage unless a trace of the ROG Xbox Ally X shows its render thread limiting frames. The alternatives were mode 3 as the native path's default (about 0.01 ms a race frame, after a step 2 gate, and no retry of a failed descriptor at each frame start) and a per-binding cache of the last four dword sets (at most about 0.05 ms, below the 0.1 ms bar).

**The default against the previous main.** The counters, the cause mapping and the split of `FindOrCreateTexture` change code that `-Rov` shares, so the build of `cd54c03` at its default (`texture_binding_resets=0`, no trace) was gated against the build of `1612fb1`, the previous main's runtime, on 28 October 2026:

| Check | Steps 1 and 2 (`cd54c03`) | Previous main (`1612fb1`) | Result |
| --- | --- | --- | --- |
| Races at 3/2, three runs each: textures zone a draw | 0.205, 0.199, 0.204 µs | 0.212, 0.200, 0.205 µs | same |
| Same runs: render thread a frame | 3.18, 3.30, 3.20 ms | 3.06, 3.26, 3.20 ms | same |
| Smoke at 3/2 | 3 of 3 cells | baseline | `career` WARN on `wait_reg_mem` only, `photo` and `photo_view` WARN on capture statistics of frames at other places on the track |
| Smoke at 2x (the launchers' plan) | 3 of 3 cells | baseline of the same plan | `career` and `photo_view` PASS, `photo` WARN on `guest_idle` only |
| Freeze parity at 3/2 | PASS | baseline | PASS |
| `-Rov` smoke at 1x | 3 of 3 cells | run in the same gate | `photo` and `career` PASS, `photo_view` WARN on capture statistics |
| `-Rov` freeze parity at 1x | PASS | baseline | PASS |

The freeze frame at 3/2 beside `-Rov`'s at 1x, cropped at 1:1 (the car ahead with the mirror, and the wall at the left), shows the same picture, sharper at 3/2.

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

#### 19.7.3 Step 1: the resolves' GPU time, 27 October 2026

The perf trace now times the parts of each full-screen resolve (`resolve_full_parts_ms`, [traces](../e2e/traces.md#performance-trace-rex_perf_trace_file)):

- the deferred transfers of the tiles it copies;
- the EDRAM dump;
- the write in the guest's layout;
- the host texture writes;
- the clears.

The runs used the build of `f99cf19` on `I:\FM4-build-usa`. Each was one `race` run on the native path with the fast route, the AI driver and `FM4_FIXED_SEED=1`, at 240 Hz, with `REX_FRAME_GRAPH`. The values are medians over the race phase's windows, in ms of GPU a frame.

| Part | 1x (`native_rt_surfaces=90`, the launchers' 720p) | 1x with bit 32 (122) | 3/2 (122, the ROG Xbox Ally X's 1080p panel) | 2x (90, a 1440p 16:9 screen) |
|---|---|---|---|---|
| GPU frame | 2.18 | 2.26 | 3.43 | 4.98 |
| Full-screen resolves (3 a frame) | 0.082 (3.8%) | 0.182 (8.0%) | 0.495 (14.4%) | 0.258 (5.2%) |
| of which the write in the guest's layout | 0.074 | 0.074 | 0.132 | 0.235 |
| of which the host texture writes | 0 | 0.099 | 0.340 | 0 |
| of which the clears | 0.008 | 0.008 | 0.022 | 0.023 |
| of which the transfers and the EDRAM dump | 0 | 0 | 0 | 0 |
| Full-screen texture loads (from the guest's layout) | 0.068 (3.1%) | 0.033 | 0 | 0.318 (6.4%) |
| The round trip (full-screen resolves and loads) | 0.150 (6.9%) | 0.215 (9.5%) | 0.495 (14.4%) | 0.576 (11.6%) |
| Other resolves (mid, small) | 0.166 | 0.243 | 0.294 | 0.335 |
| All texture loads | 0.179 | 0.102 | 0.069 | 0.539 |
| Transfers (color, depth) | 0.058, 0.046 | 0.056, 0.044 | 0.098, 0.079 | 0.114, 0.094 |
| Post passes (GTAO, TAA, motion, windshield) | 0.177, 0.097, 0.029, 0.088 | 0.178, 0.097, 0.029, 0.088 | 0.394, 0.208, 0.031, 0.174 | 0.702, 0.372, 0.117, 0.324 |

- **The guest's layout** costs 0.074 ms a frame at 1x, 0.132 ms at 3/2 and 0.235 ms at 2x for the three full-screen resolves: 3.4 to 4.7% of the frame. Step 3 would remove it.
- **The loads back** cost 0.068 ms at 1x and 0.318 ms at 2x. With bit 32 they go to 0.033 ms at 1x and to none at 3/2.
- **The host texture writes are dearer than the loads they replace.** At 1x, bit 32 writes 16 textures a frame for 16 resolves, the smaller private surfaces' too. Its full-screen writes take 0.099 ms against loads of 0.068 ms. The smaller resolves rise from 0.124 to 0.200 ms, and the frame from 2.18 to 2.26 ms. At 3/2 the full-screen host writes are 0.340 ms (21 textures a frame), 69% of the full-screen resolves' time, against 27% for the guest's layout. This is N3's finding (16.8) again: writing the textures from the render target moves the work rather than removing it.
- **On the ROG Xbox Ally X** the same parts weigh about 9 to 12 times more (19.2.3: 0.75 ms of full-screen resolves and 0.86 ms of loads at 1x, 4.53 ms of resolves at 3/2). By this PC's shares, step 3 would remove about 1.2 ms of the 15.9 ms frame at 3/2 (about 8%) and about 0.7 ms at 1x before its loads. The host texture writes, about 3.1 ms at 3/2, are the larger lever: step 4 (copies or views in place of compute encodes) or writing fewer textures (only the keys a pass samples).
- **The frame graph** gives a resolve the GPU time until the next mark, so the post passes injected right after it count as its own. The scene depth's resolve shows 0.237 ms at 1x and 0.876 ms at 2x, mostly GTAO. The parts above are measured inside the resolve, without them.

**The scope, decided on 27 October 2026** from these numbers. Item 5 goes in this order instead of 19.7.1's, because bit 32 makes 1x slower and at 3/2, the Ally X's case, the host texture writes are the larger cost:

1. **(a) Cheaper host texture writes**: only the textures a frame samples are written, and the smaller private surfaces' textures are left to their loads where a load costs less than the write. It is sized before it is built: at 3/2, how many of the host texture writes a frame are sampled before they are overwritten, and how many are textures whose load would be cheaper. Below a bound of about 0.1 ms a frame at 3/2 it is skipped.
2. **(b) Step 3, the guest layout on demand**, where bit 32 is on. It covers every reader of the guest layout: texture fetches through guest memory, CPU readbacks, memexport and photo mode's save. A check mode counts the layouts produced on demand and any read that found none, with a perturbation proof (one producer dropped and the check catching it), on `career`, `photo` and `photo_view`, since mirrors, photo mode's tiles and the pre-race final point all go through resolves.
3. **(c) Step 2, bit 32 at every scale**, is decided after (a) and (b) land, from new numbers at 1x and 2x.

A skipped or deferred write must not change the image. The gate of (a) and of (b) is freeze parity at 3/2, 1x and 2x against the previous local main with byte-identical captures, and 1:1 crops at 3/2, beside the step gate. The full `-Rov` smoke is needed only if the shared resolve code changes for every path rather than behind the native path's switches.

#### 19.7.4 Step (a): cheaper host texture writes, 27 October 2026

**Sizing.** The perf trace's `gpu.resolve_host_writes` ([traces](../e2e/traces.md#performance-trace-rex_perf_trace_file)) times every host texture write of a resolve by class: at the scene's scale or at the whole scale, and whether anything sampled the texture since its last write (a guest draw's binding, the swap or a post pass). A race frame at 3/2 (the build of `e568a04`, the runs of 19.7.3) writes:

| Host texture writes a frame at 3/2 | Count | GPU ms |
|---|---|---|
| At the scene's scale, sampled since their last write | 5 | 0.259 |
| At the scene's scale, nothing sampled them since their last write | 3 | 0.081 |
| At the whole scale (the post chain's private surfaces), sampled | 13 | 0.094 |
| At the whole scale, nothing sampled them | 0 | 0 |

The bound was about 0.13 ms a frame: the three unsampled writes at the scene's scale, and about 0.05 ms for the 13 at the whole scale net of the loads that replace them. That is above 19.7.3's bar of 0.1 ms, so (a) was built.

**The small resolves.** Of the about 20 smaller resolves of a race frame at 3/2 (19.7.3's "other resolves"):

- 13 come from private surfaces at the whole scale: the post chain's bloom and glare targets in `2_10_10_10`, from 320x192 down to 32x32, and a 32x32 `32_FLOAT` target. With bit 32 each had a host texture write, 0.094 ms for the 13 (a dispatch with its barriers and descriptors for a few kilobytes), more than the loads that replace them. (a) applies to them: bit 1 below leaves them to their loads.
- The others are resolves without host texture writes, about 0.09 ms in all: the shadow maps (512x512), a 256x256 blur, an `8_8` mip chain and 64x64 faces. (a) does not apply to them. They write the guest's layout and are loaded back from it, so they are (b)'s and (c)'s question.

**Design.** `native_rt_resolve_skip` (default 3, [experiments](../e2e/experiments.md)) leaves out host texture writes of the resolves from private surfaces:

- **Bit 1**, the textures at the whole scale. The resolve leaves them outdated, as without bit 32, and their next use loads them from the guest's layout, which the resolve still writes.
- **Bit 2**, the textures at the scene's scale that nothing sampled since their last write, when their `packed_mips` sibling holds the resolve's data: a sibling that the same resolve wrote before them, or one it writes after them. Some resolves have textures under both keys of the same range, with and without packed mips, and in the race only one key of each pair is sampled. The skipped texture is marked loaded with the skip recorded. When something samples it, it is copied from the sibling (one `CopyResource`, the same size and format) if the sibling still holds that resolve's data. The keys skipped in the race, three a frame, are the packed-mips keys of 1280x720 resolves in the guest's terms: the `2_10_10_10` scene targets at `1B479000`, `1B0E1000` and `1B811000`, and the scene depth's resolve at `1C0C1000` as `8_8_8_8` and as `24_8_FLOAT`. A partial resolve writes all its textures at the scene's scale, and first copies the writes skipped earlier from their siblings, before it writes any of them, so the rows it leaves hold the earlier resolve's data.
- **Bit 4**, the check. A skipped texture gets wrong bytes (its write with the endianness flipped) instead of keeping its old content, so its load or copy must replace them, and a use the skip misses shows in the image.
- **Bit 8**, the perturbation. The skipped textures at the whole scale are marked loaded, so they keep what they held; at the scene's scale the sampled textures are skipped instead and the copy is dropped.
- `native_rt_resolve_skip_ab_ms` turns the skips off in every other period of the steady clock, so one frozen scene compares both on one binary (`override_ab.py --phase freeze`, [experiments](../e2e/experiments.md)).

Only the native path writes host textures from resolves (bit 32), so `-Rov` and the PM4 RTV path do not change. The first build (`693afc8`) skipped no texture for a sibling: it took a sibling that the same resolve had just written for a skipped one, since a write marks its texture unsampled. `8caf655` asks whether the resolve wrote the sibling.

**The image.** The in-run A/B on the build of `8caf655`: the `freeze` scenario (the rewind freeze at the start of a Quick Race, the occlusion pinned) with `-Look "original,taa=0"`, `native_rt_surfaces=122`, `native_rt_resolve_skip=7` (the skips with the check) and `native_rt_resolve_skip_ab_ms=2000`. Every pair of frozen captures at least 100 ms from a phase change, byte compared (`override_ab.py --phase freeze` compares the consecutive ones):

| Run | Scale | On/off pairs byte-identical | Same-phase pairs byte-identical |
|---|---|---|---|
| `n6a4-ab32o` | 3/2 | 12 of 12 | 9 of 9 |
| `n6a4-ab1x` | 1x (`native_draw_scale_variant`, as the launchers' logical sizes) | 25 of 25 | 20 of 20 |
| `n6a4-ab2x` | 2x (the same) | 15 of 15 | 13 of 13 |
| `n6a4-pt32o`, the perturbation (`native_rt_resolve_skip=15`) | 3/2 | 0 of 9: 6.21 million of 6.22 million bytes differ | |

The counters (`native_rt_resolve`) agree: in the race at 3/2 (`n6a4-32`), a median window of 240 frames has 3,120 `skips_load` and 720 `skips_sibling` (13 and 3 a frame), no copy back (`fallbacks` 0), no `fallbacks_missing` and no `mismatches`. Over whole runs the copies back happen outside the race (241 in `n6a4-ab32o`, 1,036 in the race run `n6a4-32`), `fallbacks_missing` stays 0 and the perturbation counts 10,751. `misses` is 11 a run, as on the previous main (`n6m-32`).

The look `pc` is not byte-stable in the freeze without any skip, so it cannot prove identity. With `adaptation=0`, the control without skips (`native_rt_resolve_skip=0`) differs by 59,320 to 84,089 bytes (one step of 255) between captures. With the skips and a period of 6 s, which separates the phases from the captures' 2.06 s rhythm, same-phase pairs differ by 39,448 to 64,821 bytes and on/off pairs by 78 to 64,110: the look's own noise, not the skip.

**Against the previous main** (snapshot `n6m`, the build of `f99cf19`). Two runs never freeze the same moment, so the byte identity is shown in-run above, and the cross-build gates catch a broken frame:

- The `parity` tier at 3/2 (`n6a4p32` against `n6mp32`): PASS in both runs, 2,233 against 2,236 draws a frame, the frozen frames differing in 23.9 and 31.6% of the pixels above 4 of 255, within the freeze's run-to-run variance ([frozen frames](../e2e/frozen-frames.md)). `-Rov` at 1x (`n6a4rov` against `n6mrov`): PASS, 30.1 and 31.8%.
- The `smoke` tier at 3/2 (`n6a4s32` against `n6ms32`), and again with the check (`native_rt_resolve_skip=7`, `n6a4c32`): every scenario reached its goal. `photo` failed on a new error, twice in each run as photo mode started: `Resolve texture write skipped, but its sibling no longer holds the data` for the scene depth's two packed-mips keys. A partial resolve wrote the sibling before the skipped texture's copy, so the copy found the sibling holding the new resolve's data and the skipped texture kept older rows outside the band. `076ce9c` makes a partial resolve copy all the skipped writes before it writes any texture. On its build (snapshot `n6a5`) the smoke at 3/2 (`n6a5s32`) passes `career` (the replay at 117.6 fps) and warns on `photo` and `photo_view` only for capture statistics of other race moments; `photo` with the check (`n6a5c32`) warns the same way; `fallbacks_missing` is 0 in every cell. The freeze's in-run A/B at 3/2 (`n6a5-ab32o`) stays byte-identical (25 on/off and 20 same-phase pairs), and an in-run A/B in photo mode (`n6a5-abph`, `photo` with `--keep-ppm`) has its ten still captures byte-identical on and off; its other captures change between the scenario's steps, as much within one phase as across. The same cells on the branch rebased onto local main `b445ace` (snapshot `n6a6`, with N5's shader contract increments 3 and 4) give the same results: smoke 3 of 3 (`career` PASS, `photo` and `photo_view` WARN on capture statistics; the saved photo is the photo mode frame, darker in that run's shade), `photo` with the check WARN only, `fallbacks_missing` 0, the freeze's A/B 25 and 20 pairs identical, and photo mode's still captures identical on and off. The `career` cell failed on its replay's rate in one run (119.35 to 100.39 fps, with the replay's GPU frame unchanged at 4.60 and 4.58 ms); the check run on the same binary passed it (116.58 fps). `photo_view` warned on the capture's luma, which races do not keep.

**The saving.** Three race runs at 3/2 and 240 Hz with the skips off (`native_rt_resolve_skip=0`, `n6a4-off32`) and three with the default (`n6a4-on32`), on the same binary, medians over the race phase's windows:

| ms of GPU a frame | Skips off (3 runs) | Skips on (3 runs) | Change (median) |
|---|---|---|---|
| GPU frame | 3.601, 3.629, 3.660 | 3.278, 3.294, 3.304 | -0.335 (-9.2%) |
| Resolves | 0.849, 0.853, 0.877 | 0.605, 0.606, 0.606 | -0.247 |
| of which the host texture writes | 0.440 (21 a frame) | 0.259 (5 a frame) | -0.181 |
| of which the smaller resolves | 0.296 to 0.314 | 0.147 to 0.148 | -0.149 |
| Texture loads | 0.075 to 0.077 | 0.110 to 0.111 | +0.035 |
| Race rate (fps, cap 240) | 231.0, 232.7, 234.0 | 236.5, 236.8, 236.7 | +4.1 |

Against the bound of about 0.13 ms, the resolves and loads together save 0.21 ms a frame and the GPU frame 0.34 ms. The timed parts counted only the writes' dispatches; each write also took its barriers (the render target to a shader resource and the texture to unordered access), which the smaller resolves' times and the frame hold. The scene's three full-screen resolves are unchanged: the guest's layout (0.13 ms) and the five sampled writes (0.26 ms) stay for (b).

#### 19.7.5 Step (b): the guest layout on demand, 28 October 2026

**The producer.** A deferred layout must be written later, when its render target holds other pixels. A copy of the render target costs about what the write it saves does, so the only source left is the host texture at the scene's scale that the resolve writes from the render target (18.12). Its pixels are the render target's one to one, and its words are the guest's words with the texture's endianness. Where `resolve_host_32bpp` takes the nearest render target pixel for a guest pixel (depth, and every destination at a whole scale), the texture's word is the same word, so the layout from the texture is the render target's byte for byte. At a fractional scale a destination from a float color source is the mean of a box of render target pixels (18.17), taken before the encoding, so the texture's encoded words cannot give the same bytes.

**The main session's decision of 28 October 2026**, taken while the project owner was away, chose between:

- **F** (taken): the guest's layout of a full-screen destination at the scene's scale is always written from the texture, at the resolve or deferred alike, so deferring it never changes a byte. At a fractional scale the box filter then averages the texture's encoded values instead of the render target's: the layout's bytes change in those destinations, for their rare readers (the CPU readback, a texture key without a host texture), not in the picture, which the host textures make.
- **G** (the alternative): the box-filtered color destinations keep the render target's write at the resolve at a fractional scale, and only depth and whole scales are deferred. No byte changes, but at 3/2 only about a third of the saving.

The conditions: an opt-out bit that restores the render target's write for the box-filtered color destinations at a fractional scale (the A/B and the fallback); the byte change measured once at 3/2, old against new on the same frames, with a fall back to G for color if any difference exceeds one encoded step or a capture shows it; screen captures (race, freeze with `original,taa=0`, `photo_view`) byte-identical against main, the saved photo identical or within that one step, and whole scales byte-identical everywhere, the guest layout included; a counter on the invariant that a texture at the scene's scale is written only by resolves to its own range and never loaded, 0 in every gate cell with the check.

**Design.** `native_rt_resolve_layout` (default 7, [experiments](../e2e/experiments.md)):

- **Bit 1**, the layout from the texture. A full-screen resolve of a private surface at the scene's scale (not a tile's band of photo mode, which keeps the render target's write) writes its textures first, then the guest's layout from the first colour texture it wrote with `resolve_host_32bpp` kind 3: the texture's words with its endianness undone, the nearest pixel as the render target's resolve takes it, or at a fractional scale from a float color source the mean of the same box over the words decoded in their layout (7e3 or unorm for a bitwise copy, encoded again as the dump encodes; the destination's format otherwise, packed again as the resolve packs). Where no texture took the resolve's data (none exists yet), the layout is written from the render target, which still holds it.
- **Bit 2**, deferred, where the readback on demand is on (the native path's default, `-Native`): the write is kept with its texture's resource and written before something reads the range. The readers are hooked where they meet guest memory: a texture's load (`TextureCache::LoadTextureData`), every other GPU request of shared memory (`SharedMemory::RequestRanges`: vertex and index buffers, memexport), the readback on demand's service before its copy, and a resolve whose destination overlaps (the same resolve again replaces the deferred write instead). A CPU write is counted and leaves it pending: the readback on demand serves the CPU's first access to the range first (16.5), and the pages the CPU wrote upload over the layout's write when requested, as over the resolve's.
- **Bit 4**, the opt-out: the box-filtered color destinations at a fractional scale keep the render target's write at the resolve. On by default since the measurement below (G).
- **Bit 8**, the check: a deferred layout is written with wrong bytes at the resolve, which the write for its reader replaces, so a reader the hooks miss shows in the image. **Bit 16**, its perturbation: the readers' writes are dropped.
- `native_rt_resolve_layout_check=N`, every N-th frame: a deferred layout is read at once through the readers' path (the probe, which the perturbation drops, so the check's wrong bytes stay and show), a layout written at the resolve is taken as it is, and either is compared with the render target's, written into scratch beside it; `native_rt_resolve_layout_ab_ms` turns the switch off in every other period. The perf trace's `guest_layout` ([traces](../e2e/traces.md#performance-trace-rex_perf_trace_file)) counts the writes, the deferred ones and their ends, the source textures' loads, the invariant's faults, unordered GPU writes, and the check by class.

Only the native path's resolves from private surfaces into host textures (bit 32) take this path, so `-Rov` and the PM4 RTV path do not change.

**The measurement (condition 2) and the fall back to G.** With F (`native_rt_resolve_layout=11` and the check every 30th frame) on the smoke's cells at 3/2, every layout from the nearest pixel was the render target's byte for byte (`8_8_8_8` at 3/2; `8_8_8_8` and `2_10_10_10` at 1x and 2x), and the box-filtered `2_10_10_10` destinations (the three 1280x720 scene colour copies at `1B0E1000`, `1B479000` and `1B811000`) were not: 11.5 to 12.6% of their words and 4.0 to 4.7% of their bytes changed, with a largest difference of 676 to 767 steps of 1023. The differences larger than one step are about 1.3% of the color channels, and 99.5% of them have the texture's value below the render target's: the texture holds the destination's unorm values, clamped to [0, 1] before the box, while the render target's resolve averages the float values and clamps after, so a box that mixes values above 1 with darker ones (bright edges, lights) is darker from the texture. This is the format, not the sampling, and no texture of the destination's format can give those bytes. As the conditions set, G is the default (`native_rt_resolve_layout=7`): those destinations keep the render target's write at a fractional scale.

**The gate**, BuildRoot `I:\FM4-build-usa`, the build of `4b97062` (G by default) and the diagnostic builds after it, at 3/2 unless stated:

- **The same picture.** The freeze's in-run A/B on one binary with `native_rt_resolve_layout=15` (deferred layouts given wrong bytes that their readers' writes must replace) and the check every 10th frame is byte-identical in every pair of frozen captures at 3/2, 1x and 2x (25 and 20, 25 and 20, 15 and 13 pairs on and off and within a phase), and every layout the check compared is the render target's (3/2: 384 `8_8_8_8`; 1x and 2x: about 370 `8_8_8_8` and 964 `2_10_10_10`), so the whole scales' guest layouts are unchanged too. With G no layout is deferred or written from a texture in photo mode (the phase's counters are 0), so its captures and its saved photo are made as before.
- **The readers.** In every cell of the smoke (with and without the check), the races and the A/Bs, a deferred layout ended only replaced by the same resolve again (`superseded`), written for an overlapping resolve or for the check's probe: no texture load, other GPU request or CPU readback read one, and `cpu_writes`, `gpu_writes`, `foreign_thread` and `invariant` stay 0 (condition 4). The first builds counted a deferred layout's source texture that loads as the invariant's fault; that load reads no memory and keeps the texture's data, so it is counted apart as `source_loads`, and a GPU write over a pending layout that no hook ordered is counted as `gpu_writes`.
- **The perturbation** (`native_rt_resolve_layout=31`, the check's wrong bytes kept): the smoke's three cells fail on the new error `Guest layout from the texture differs from the render target's` (918, 566 and 5 times).
- **Against the previous main** (the build of `0509e97`): smoke 3 of 3 cells, `career` PASS, `photo` and `photo_view` WARN on capture statistics only, as in (a); with the check the same; freeze parity at 3/2 PASS. The 1:1 crops of the A/B's captures at 3/2 and 2x and of the parity capture beside the previous main's show no difference.
- **The saving**, race runs at 240 Hz on one binary (three each at 3/2, one each at 1x and 2x): at 3/2 the GPU frame is 3.303 ms with the layout from the render target, 3.299 ms with G and 3.197 ms with F (the full-screen resolves' parts 0.414, 0.400 and 0.291 ms); at 1x 2.232 against 2.187 ms (the resolves 0.379 against 0.316 ms) and at 2x 5.075 against 4.870 ms (0.980 against 0.884 ms). At 3/2 G saves nothing: the box-filtered destinations are written at the resolve, and the one layout it defers (`1C0C1000`, the nearest pixel) is written every frame for a 256x256 resolve to `1C1C1000` inside its range. At 1x and 2x two of three layouts are replaced before anything reads them.
- **The readback on demand** (condition 5) is on by default on the native path (`readback_resolve_on_demand` in the launchers' and the harness's native settings), so the deferral acts by default there; without it bit 2 does nothing and the layouts are written at the resolve from the texture.
- **After the rebase** onto local main `bb922c6` (increment 5 of N5's shader contract, whose shared `shader_override` code is on every path), the build of `1612fb1`: smoke at 3/2 3 of 3 cells against (b)'s (`photo` and `photo_view` PASS, `career` WARN on the guest's idle wait only), freeze parity at 3/2 PASS against the previous main of (a), and `-Rov` freeze parity at 1x PASS against increment 5's close gate. The 1:1 crops of the frozen captures beside both references show whole frames.
- Not covered: the ROG Xbox Ally X, 60 and 120 Hz, and RTV and `-Rov`, which this path does not touch.

**The main session's decisions after the gate**, 28 October 2026, taken while the project owner was away:

1. G stays the default. F (`native_rt_resolve_layout=3`) stays opt-in, for measurement, and can be reviewed from the ROG Xbox Ally X's numbers. The memory the guest sees must match the reference: 0.105 ms a frame at 3/2 on this PC (about 3%) does not outweigh bytes that differ by up to 767 steps for a reader that may appear later, and the Ally X's figure is not measured.
2. The exclusion list that would keep `1C0C1000`'s layout deferred past the 256x256 resolve inside its range (the later write skipping the ranges written since) is not built: it is worth a few hundredths of a ms a frame.

#### 19.7.6 Step (c): bit 32 at the whole scales, 28 October 2026

**What bit 32 needs.** `native_rt_surfaces` bit 32 writes the scene's textures from the host render target only where the scene has textures at its own scale (`SetSceneTextures`, [command_processor.cpp](../../work/runtime/src/graphics/d3d12/command_processor.cpp)): at a fractional `native_draw_scale`, while upscaling, or with `native_draw_scale_variant` at a whole one (the scene draws' path, 18.13, which the launchers' logical sizes already take). At a whole scale without the variant, `native_rt_surfaces=122` writes no host texture and runs as 90. Step 1's column "1x with bit 32" (19.7.3) ran with the variant. So step (c) at a whole 16:9 scale means the scene draws' path with bit 32, as the logical sizes run.

**The measurement**, the build of `1612fb1` ((a) and (b) on by default), race runs at 240 Hz on one binary, three runs each, medians over the race phase's windows in ms of GPU a frame:

| Settings | 1x: GPU frame (3 runs) | median | resolves | loads | 2x: GPU frame (3 runs) | median | resolves | loads |
|---|---|---|---|---|---|---|---|---|
| 90, the launchers' plain whole scale | 2.313, 2.173, 2.210 | 2.210 | 0.248 | 0.175 | 4.952, 4.999, 4.966 | 4.966 | 0.570 | 0.511 |
| 122 without the variant | 2.219, 2.176, 2.184 | 2.184 | 0.248 | 0.175 | 4.958, 4.958, 5.146 | 4.958 | 0.573 | 0.509 |
| 90 with the variant | 2.200, 2.221, 2.223 | 2.221 | 0.247 | 0.175 | 4.865, 5.012, 5.051 | 5.012 | 0.565 | 0.522 |
| 122 with the variant | 2.261, 2.191, 2.141 | 2.191 | 0.317 | 0.109 | 4.815, 4.864, 4.799 | 4.815 | 0.866 | 0.204 |

- **1x**: bit 32 with the variant against the plain 90 is -0.019 ms, inside the runs' spread of 0.14 ms: the resolves rise by 0.069 ms (five host texture writes, 0.135 ms, the guest layout deferred) and the loads fall by 0.066 ms. Nothing to gain.
- **2x**: -0.151 ms (-3.0%), all three runs below every other 2x run, the race from 197.8 to 203.8 fps: the resolves rise by 0.296 ms (host texture writes 0.43 ms), the loads fall by 0.307 ms and the post passes by 0.068 ms. The variant alone costs nothing measurable (+0.046 ms, within the spread).
- **3/2** on the same binary (`native_rt_surfaces=122`, the settings of 19.7.5's runs): 3.307, 3.327 and 3.327 ms, median 3.327 against 3.299 on the build of `4b97062`, +0.028 ms.
- **The resolves without host texture writes** (the shadow maps, the 256x256 blur, the `8_8` mip chain, the 64x64 faces; 19.7.4): about 0.09 ms at 3/2 for their layouts and loads together. Host writes and a deferred layout could save at most that, below 19.7.3's bar of 0.1 ms, and (a) found host writes of small surfaces dearer than their loads. They keep their loads.

**The main session's decision of 28 October 2026**, taken while the project owner was away, between keeping every plan as it was and the recommendation, which it took:

1. The launchers (`pipeline.py`'s `launch_plan` and the GUI launcher's `LaunchPlan`) give a whole 16:9 scale of 2 or more on the native path in one pass with 2x MSAA (2560x1440 at 2x, 4K at 3x, and the whole scales of `-Supersample` and `-ResolutionScale`) `native_draw_scale` at that scale, `native_draw_scale_variant` and bit 32 (`native_rt_surfaces=58`, 122 with entry 0 at the scene's scale). One pass with 4x MSAA (`ultra` at a whole 16:9 scale), RTV and `-Rov` do not change.
2. 1x keeps 90, the integrated GPUs and the ROG Xbox Ally X at 1x included, until the Ally X's numbers exist.
3. The resolves without host texture writes get none.

The e2e harness first kept its own settings (`native_rt_surfaces=90` at every whole scale). The main session's second decision of 28 October 2026: the harness follows the launchers' plan, so a gate runs what the players run. Since then `e2e.py --native` in one pass with 2x MSAA at a whole scale of 2 or more (the freeze scenario's pinned 2x included) gives `native_draw_scale` at that scale, `native_draw_scale_variant=true` and `native_rt_surfaces=122` (58 with `--entry0 k`), unless `--arg` gives any of `native_draw_scale`, `native_draw_scale_variant`, `native_rt_surfaces`, `native_logical_width`, `native_logical_height` or `native_scene_input_scale`. A comparison with a 2x baseline from before this change passes `--arg native_rt_surfaces=90` (and no variant) to run the earlier plan.

**The gate**, BuildRoot `I:\FM4-build-usa`. The runtime does not change, so the cells ran the build of `1612fb1` with the launchers' new settings passed as `--arg` (`native_draw_scale=2`, `native_draw_scale_variant=true`, `native_rt_surfaces=122` at 2x) against the earlier plan (90):

- **The plans**: `test_pipeline.py` passes, and the GUI launcher built from the branch gives the plans of `pipeline.py` in all 504 cases of its tests, 97 of them a whole 16:9 scale on the scene draws' path. One pass with 4x MSAA (`ultra` at a whole 16:9 scale), RTV and `-Rov` keep their plans.
- **The byte checks** on `career` at 2x (`native_rt_resolve_check=30`, `native_rt_surfaces_check=30`, `native_rt_resolve_skip=7`, `native_rt_resolve_layout=15` with its check every 30th frame): 7,200 texture checks, 16,510 resolve checks and 162 guest layouts compared, 0 mismatches; 5,395 skipped textures copied back from their siblings, none missing; 12 misses, as before.
- **The perturbation** (`native_rt_resolve_layout=31`, `native_rt_resolve_skip=15`): every layout the check compared differs (1,885; the error `Guest layout from the texture differs from the render target's` 1,888 times), and 16,328 skipped textures found their sibling overwritten; the broken picture stops the career before its grid.
- **Smoke at 2x**, the new plan against the earlier one on the same binary: 3 of 3 cells reached their goal, `career` PASS, `photo` and `photo_view` WARN on capture statistics only (another moment), no new error key. Photo mode's GPU frame rose from 7.56 to 8.53 ms in its single run (the resolves of the whole run from 0.80 to 1.28 ms), at its cap of 30 fps; the replay's fell from 7.45 to 7.21 ms.
- **Freeze parity at 2x**, the new plan against the earlier one: PASS. The 1:1 crops of the frozen frames show whole frames.
- The smoke at 3/2 was not run again: its runtime and plan are those of the combined gate after the rebase (19.7.5).
- **Photo mode at 2x, three runs each** (the perf tier's `photo` cell on the build of `1612fb1`, 28 October 2026, with item 4's measurement): photo mode's GPU frame is 7.55, 8.04 and 8.36 ms with 90 (median 8.04) and 8.06, 8.08 and 8.20 ms with the new plan (median 8.08), +0.04 ms, while its draws vary from 2,900 to 4,283 a frame between runs. The single smoke run's 8.53 against 7.56 ms was the runs' spread, not the plan. One run with 90 timed out waiting for the saved photo's message (`SAVE SUCCESSFUL`); the new plan's three runs reached their goal.

### 19.8 The proposed order

The owner's order is stutter first, then CPU headroom on the ROG Xbox Ally X. The Ally X waits for its GPU in every mode (17.21, 17.25), so CPU time it saves reaches the frame only through the shared power budget (1 to 3%), while the resolves of 19.7.1 hold 19 to 28% of its GPU frame. The proposal therefore puts the resolves second, as the Ally X's headroom, and the CPU items after them:

1. **Item 1, increments 1.0 and 1.1** (counters and the cold-cache cell, compiles off the render thread): the stalls players meet first, gone in about 1 to 2 person-weeks, without waiting for N5. Together with step 3 it lets a public preview start without a shipped cache.
2. **Item 5's resolves (19.7.1), steps 1 to 3**: the Ally X's largest lever, at its panel's resolution most of all, about 3 to 6 person-weeks; step 1's measurement can run beside item 1.
3. **Item 1, increments 1.2 to 1.5** (the offline pass to DXIL with DXC and its cache, N5's increment 7, on N5's increments 1 and 2; the disc's pixel shaders; the pipeline manifest and library; early creation of missed pipelines): the mods on time and no pipeline created in play, about 8.5 to 15 person-weeks. 1.2 starts when N5's increments 1 and 2 have landed, 1.3 and 1.5 can go before it; 1.5 first if the cold-driver measurement (19.3.6) shows pipeline stalls of more than a few milliseconds.
4. **Item 4, steps 1 and 2** (the binding resets): the cheapest CPU saving, on both devices, about 1 to 2 person-weeks. Step 1 found the resets 1.8% of the keys (19.6.5), so step 2 saves about 0.01 ms a race frame and stays off by default (19.6.6).
5. **Item 4, step 3, with 19.7.1's step 4**, if step 2 leaves most of the keys or the resolved textures' copies are still worth removing. Step 2 leaves 98% of the keys; step 3 moved to a later stage unless the Ally X's render thread limits its frames (19.6.6).
6. **Item 2**, starting with its measurement (19.4.4 step 1), only if the Ally X's battery runs (17 W) show the CPU time still worth fps after the items above, or this PC's headroom is wanted for 4K and high draw loads; otherwise it moves to a later stage.

N6 by this order is about 17 to 30 person-weeks without item 2 and 27 to 46 with it, besides N5's increments 1 and 2. In calendar time, by the calibration of 28 October 2026 ([D3D-level HLE, section 9](d3d-hle.md#9-stages)), what is left after item 4 is:
- increments 1.2 to 1.5 (about 8.5 to 15 person-weeks; 1.5 reached its close gate on 28 October): about 1 to 4 days, about 1.5 to 2 days at the median rate;
- item 2, if it is taken up (about 10 to 16 person-weeks): about 1 to 5 days more, about 2 days at the median rate.

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

The owner's answers of 28 October 2026 on 4 (the review is in [sources and licensing](../sources-and-licensing.md#third-party-code-directx-shader-compiler)):

- **The release**: DXC v1.9.2609 of 29 September 2026 (`dxc_2026_09_29.zip`, 32,287,592 bytes), of which only the x64 `dxcompiler.dll` and `dxil.dll` are used.
- **The route**: setup fetches the release into the BuildRoot for builds, as answer 4 has it. The GUI launcher fetches the same release, from the same URL with the same SHA-256, before the offline pass when `dxcompiler.dll` and `dxil.dll` are not beside it; without a connection the shaders compile with FXC as today. An export carries no DXC, so the Ally X runs the path that a launcher on another computer would take.
- **Why**: the release's Microsoft terms allow the distribution only of the files on a distributables list, which the release does not carry, and ask for end-user terms that protect it as much and for an indemnity. Fetched on the user's computer, DXC is never distributed by the project, which keeps a launcher given to others possible. Whether the launcher is given to others is not decided: [distribution](../sources-and-licensing.md#distribution) allows no binaries today, and the project has no license yet.

**The native port's end point**, set by the project owner on 28 October 2026 from three levels the main session described:

- **Level 1, the target:** a native renderer with no stutter. That is the rest of item 1 (increments 1.2 to 1.5), item 2 only if the Ally X's battery runs show CPU-bound frames, and N5's open items, with HDR10 waiting for an HDR display. By the calibration of [section 9](d3d-hle.md#9-stages), it is about 2 days of calendar time (1 to 4) without item 2.
- **Level 2, not planned:** the game's data in PC formats, with textures, vertex data and XMA audio converted once at install instead of at run time. It is about 1.5 to 2 days (1 to 3.5), a guess without a design. It would be taken up only if a measurement shows the run-time conversion costing loading time or CPU.
- **Level 3, not planned:** no Xbox 360 shape left (no big-endian guest memory, no Xbox kernel API, no recompiled XDK D3D library). It needs a decompilation of the game's 49,616 functions (plus 9,274 in its two other modules), whose equivalence cannot be proven without the original compiler. There is no honest estimate, and it is not recommended.
