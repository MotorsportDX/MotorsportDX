# Native port, stage N4: the render thread's CPU time

Section 17 of the [D3D-level HLE design](d3d-hle.md), moved here on 23 October 2026 so the design note stays within the repository's file size limit. Section numbers are unchanged; sections 1 to 15 are in [d3d-hle.md](d3d-hle.md) and section 16 in [d3d-hle-n3.md](d3d-hle-n3.md).

## 17. Stage N4 design, 22 October 2026

Stage N4 (section 9) brings the render thread to the CPU targets of section 10: busy at most 2.5 ms per race frame and p99 at most 5.0 ms at 240 Hz, on the main PC and on the ROG Xbox Ally X. The native path's render thread is busy 3.1 to 3.4 ms per race frame at 1x (15.7, 15.12), so N4 has to remove 0.6 to 0.9 ms. The main session's order: first the descriptor and constant buffer work leaves the render thread, which the profile of 15.10 points at ([CP draw offload](cp-draw-offload.md), options 1 and 2).

### 17.1 The bindings per draw, and what may leave the render thread

The bindings zone of `IssueDraw` took 0.39 ms per race frame in `n2d` and 0.559 ms in `n2f`, whose extra timers inflate it (15.10). Its parts in `n2f`, and what each reads:

| Part | ms per race frame (`n2f`) | Reads | Writes | Step 1 |
|---|---|---|---|---|
| System constant values (`UpdateSystemConstantValues`) | about 0.14 | about 25 registers, the viewport, the texture cache's signs, integer scale bits and resolution flags per texture, the render target cache's path and formats | `system_constants_` and its dirty flag | stays |
| Constant buffer data | 0.199 | the register file (the float constants each shader uses, the bool and loop constants, the fetch constants) and `system_constants_` | upload memory that the GPU reads after `ExecuteCommandLists` | the float, bool and loop, and fetch buffers move; the system constants stay |
| Descriptors (bindless, the default) | 0.156 | the fetch constants (the parameters of every sampler at every draw), the texture cache's bindings (SRV keys and indices) | 2 to 16 dwords of descriptor indices when a binding changed; a sampler descriptor only for a new sampler state | stay |
| Root parameters | 0.040 | the buffers' addresses | the deferred command list | already deferred |
| Root signature and common buffers | 0.020 | the shaders | the deferred command list | already deferred |

- The D3D12 command recording is already off the render thread: the render thread appends to the deferred command list, and the submit thread (`d3d12_submit_thread` with `d3d12_submit_thread_async_swap`, both set by the launcher since 20 October 2026) replays it into the D3D12 command list and executes it.
- Stay on the render thread for ordering: every read of the register file that decides something (which buffers are out of date, the system constant values, the sampler parameters), the texture cache's validity and bindings, and the render target cache's ownership. These read state that the next packets change, and the caches record their own work into the same deferred command list in stream order.
- The descriptor phase is decisions, not writes: in the bindless path the writes are a few dwords of indices when a binding changed, and a sampler descriptor is created only for a sampler state not seen before. Moving those writes saves nothing; moving the decisions needs their inputs captured per draw (17.4).

### 17.2 Step 1: deferred constant buffers

`d3d12_deferred_constants` (off by default, not set by `-Native`, read at start-up):

1. **At the draw (render thread).** `UpdateBindings` makes the same decisions as before: which of the float vertex, float pixel, bool and loop, and fetch constant buffers are out of date, their allocation from the constant buffer pool (the GPU address is needed now for the root parameter), and the root parameters. For each draw that refills any of them it writes one `kDeferredConstants` command into the deferred command list: the destinations and sizes, the two shaders' float constant maps, and the constant registers that the fills read and that were written since they were last captured into this list.
2. **The capture.** A pending mask of the constant registers in 16-byte slots (586 slots from `0x4000` to `0x4927`) is set by the hooked constant writers (`WriteRegister`, `WriteRegistersFromMem`, and the changed dwords in the path of `d3d12_reuse_draw_bindings`) and set whole when a submission opens. A draw captures the pending slots its fills read and clears them. The inline path copies every constant a shader uses whenever one of them changed; the capture copies only the used constants written since the last capture, and only the written fetch and bool and loop slots.
3. **At the replay (submit thread).** The thread that replays the list keeps a copy of the constant registers per list, applies the captured slots and fills the buffers from the copy, in the layout of the inline path.
4. **Hand-over and fence.** The command is part of the list, so the list order is the hand-over: no new thread, and no wait or signal per draw. The fill runs while the list is replayed, before its `ExecuteCommandLists`, so the GPU reads the buffers only after they are filled. The pool reuses a buffer only after its frame's fence, which the submit thread signals after executing the list. Each list starts with all slots pending, so it captures every register it reads and carries nothing over from another list. Without the submit thread (or for a swap without the asynchronous swap) the list is replayed on the render thread at the end of the submission: the result is the same, without a saving.
5. **The check.** `d3d12_deferred_constants_check` also writes the inline path's bytes of every filled buffer into the command; the replay compares them with its own, buffer by buffer and draw by draw, and logs an error for the first 16 that differ. `d3d12_deferred_constants_check_perturb=N` changes one byte of the comparison of every N-th command, so a perturbed run must report exactly the perturbed comparisons. A writer of the constant registers that bypasses the pending mask shows as a mismatch.
6. **Counters** (the perf trace's `constants` object, [traces.md](../e2e/traces.md)): the buffers filled and their slots on either path, the slots captured, the commands replayed and the replay's time, the checks, mismatches and perturbed comparisons, and, for 17.4, the sampler parameters computed and the descriptor index buffers written.

### 17.3 What step 1 can save

At most the constant buffer data phase of 15.10 without the system constants: 0.199 ms per race frame with the `n2f` timers, about 0.14 ms without them (scaled by the bindings zone, 0.39 against 0.559 ms). The render thread keeps the allocations, the capture and the command, so the expected saving is about 0.05 to 0.1 ms per race frame, and the replay adds about the same work to the submit thread. Step 1 cannot reach the section 10 target alone; what it adds is the mechanism: register state captured at the draw and consumed in list order on the submit thread, checked byte for byte against the inline path.

### 17.4 After step 1

By the profile, the rest of the 0.6 to 0.9 ms has to come from moving decisions with their inputs, not only writes:

- The system constant values and the sampler parameters on the replay, with their inputs captured at the draw: the registers they read, the viewport, and per texture the texture cache's signs, integer scale bits and resolution flags; the replay would own the bindless sampler heap. Ceiling by 15.10: about 0.14 ms for the system constants and a share of the 0.156 ms of descriptors.
- A draw back end on the replay side: the pipeline configuration, the fixed function state and the draw recording, with the front end keeping the primitive processor, the render target cache and the texture cache in stream order (option 2 of [CP draw offload](cp-draw-offload.md), split at the draw instead of at the PM4). Ceiling: the bindings, pipeline, state and call parts of 15.10, about 0.6 to 0.7 ms per race frame.
- The other items of the N4 row of section 9: event waits instead of the remaining spins, the vblank interrupt and the readback copies on the render thread, queue depth, the capped-CPU proxy and the Ally X runs.

### 17.5 Step 1 results, 22 October 2026

On `I:\FM4-build-usa` at 240 Hz and 1x with `-Native`'s cvars and the launcher defaults, one binary (`fm4_rex.exe` SHA-256 beginning `034c6215a4eac18e`, `rexgpu-xenos.dll` `199e45fe39d8b33f`, `rexruntime.dll` `b3d42a69266b2c9b`), the cvar switched per run:

- **The check** (`n4a-chk-career`: the `career` scenario with its race, replay and menus, `d3d12_deferred_constants_check` with `_perturb=1000`): 97,058,424 buffers compared with the inline path's bytes, 36,145 mismatches, exactly the 36,145 perturbed comparisons; no error logged for a real mismatch; the error keys of the other runs. The fills are byte-exact.
- **What moves.** In that run's race (2,023 draws a frame) the inline path fills 5,098 buffers a frame (2.5 a draw) holding 175,800 slots (87 a draw, 1.4 KB); the capture copies 44,286 slots (22 a draw, 25%).
- **Performance** (the `race` scenario, alternating: `n4a-race-off1`, `-on1`, `-off2`, `-on2`, then `n4b-race-on1`, `-off1`, `-on2` with the fills moved into one pass before the list's D3D12 calls). The draw load of the AI races varied from 1,402 to 2,217 draws a frame between runs, so the times are fitted per one-second window against the draws a frame and compared at 1,800 draws:

| At 1,800 draws a race frame | Off (3 runs) | On (2 runs) | On, fills in one pass first (2 runs) |
|---|---|---|---|
| Render thread busy | 3.307 ms | 3.207 ms | 3.250 ms |
| of which the bindings zone | 0.490 ms | 0.415 ms | 0.421 ms |
| Submit thread recording the lists | 0.722 ms | 1.074 ms | 1.105 ms |
| of which the fills (`replay_ms`) | | 0.146 ms | 0.124 ms |

- The render thread saves 0.06 to 0.10 ms a race frame (2 to 3%), in the bindings zone about 0.07 ms (0.28 against 0.25 us a draw), as 17.3 expected. Busy per draw over the whole race was 1.81 to 1.88 us off and 1.73 to 1.82 us on; p99 (4.99 to 5.83 ms) follows the draw load and shows no difference.
- The submit thread pays 0.35 to 0.38 ms a race frame, three to four times the saving: the fills themselves take 0.12 to 0.15 ms. Filling everything in one pass before the D3D12 calls changes neither, so the interleaving of the upload memory's write-combined stores with the driver's calls is not the cause. Inferred, not measured: the list grows by about 0.55 KB a draw (the 200-byte header and the captured values), which one core writes and another reads, and which displaces the driver's working set on the submit thread. The one-pass variant is not kept.
- The submit thread stays far from the frame budget (1.1 to 1.2 ms of its 4.17 ms at 240 Hz), and the render thread is the one that limits the race, so the trade lowers the frame's critical path. For step 2 it means that the volume handed over per draw costs the replay more than its own work: the command has to stay small.
- **The context registers** (`REX_N4_REGISTER_DELTA`, in the check run): 4.0 dwords in 2.6 granules of 16 bytes change from one draw to the next in `0x2000` to `0x23FF` in the race and in the replay.

By the main session's decision of 22 October 2026, step 1 lands off by default when it is byte-exact, as the capture-and-replay mechanism step 2 builds on.

### 17.6 Step 2 design: the draw back end on the replay side

The main session decided on 22 October 2026 that step 2 moves the back half of each draw to the thread that replays the deferred command list (option 2 of [CP draw offload](cp-draw-offload.md), split at the draw instead of at the PM4), with the system constants and the sampler parameters folded in: it is the only item of 17.4 whose ceiling reaches the target of section 10.

**The split.** `IssueDraw` keeps on the render thread (the front end) everything that reads or changes state in stream order, and writes one `kDrawBackEnd` command where it records the draw today:

| Front end (render thread, stream order) | Back end (replay of the list, list order) |
|---|---|
| shader analysis, modifications and translation (the used texture masks and binding layouts need it) | the pipeline description from the registers, the pipeline lookup and creation, the wait or drop of an asynchronous pipeline |
| post pass `OnDraw` and the TAA jitter | viewport, scissor, blend factor and stencil reference |
| primitive processor (index conversion, shared memory requests) | system constants (`UpdateSystemConstantValues`) |
| uniform draws and the render target cache's `Update` (transfers, render target binding) | sampler parameters, the bindless sampler heap and its map |
| texture cache `RequestTextures` (keys, loads, bindings) and the texture SRV indices | the constant buffers (step 1's fills and now also their decisions) and the descriptor index buffers |
| vertex buffer and memexport residency, shared memory state, barriers | root signature, root parameters, topology, index buffer, the draw |
| draw traces, statistics and dumps | |

**Captured per draw** (the command's arguments), sized by 17.5:

- the context registers `0x2000` to `0x23FF` (every register of the pipeline, fixed function and system constant state lies there) changed since the previous capture in the list: 2.6 granules of 16 bytes a draw on average (42 bytes and a mask), all 256 granules (4 KB) at a list's first draw;
- the constant registers as in step 1: 22 slots a draw (350 bytes), the largest part;
- the front end's results, about 250 bytes: the two shader translations and modifications (32 bytes), the primitive processing result (index buffer type and GPU address, vertex count, host vertex shader and primitive type, line loop closing index, index endianness; about 40), the normalized depth control and color mask (8), the bound render target bits and formats (24), flags of the draw and of the render target cache (memexport, uniform draw scale, polygonal, path, float24 conversion, gamma as UNORM16, fixed16 truncation, resolution scale; about 16) and the jitter (8);
- per texture the texture cache's swizzled signs, integer scale bits and resolution flag (6 bytes each), and per texture binding its bindless SRV index (4 bytes each), taken where the texture cache knows them.

That is about 0.6 to 0.7 KB a draw, against step 1's 0.55 KB, so the encoding has to be compact: only the filled buffers' fields, only the non-zero words of the masks, and the front end's results only where they changed since the previous draw of the list.

**The back end's state.** The replay keeps the copy of the context and constant registers per list, and tracks the D3D12 state the list sets: root signature and root parameters, pipeline, topology, viewport, scissor, blend factor, stencil reference and descriptor heaps, including what the front end's own commands set (render target transfers, texture loads, post passes, resolves) as they are replayed. That replaces the front end's caches of that state (`current_graphics_root_up_to_date_`, `ff_*`, `current_guest_pipeline_`, `primitive_topology_`) for guest draws. The back end owns the pipeline cache's descriptions, pipeline map and creation, the root signature map, a constant buffer pool of its own (post passes and the texture cache keep the front end's) and the bindless sampler heap with its map (set again at each list's start and on an overflow); `PipelineCache::EndSubmission` runs where the list is replayed. The functions that read registers (`GetCurrentStateDescription`, `UpdateSystemConstantValues`, `GetHostViewportInfo`, `GetScissor`, `GetSamplerParameters`) take the register file to read as a parameter.

**Order and fence.** As in step 1: the command is part of the list, the back end runs when the list is replayed and before `ExecuteCommandLists`, and every list starts with all registers pending. Without the submit thread the list is replayed on the render thread at the end of the submission.

**The check.** `d3d12_draw_back_end_check`: at every draw the front end also computes the back end's results from the live register file without recording anything (the pipeline description, the system constants, the viewport, scissor, blend factor and stencil reference, the sampler parameters, the constant buffer bytes and the descriptor indices) and writes them into the command; the replay compares them with its own, field by field. `d3d12_draw_back_end_check_perturb=N` changes one field of every N-th draw's comparison. A register the back end reads but the capture misses shows as a mismatch.

**Expected saving and risks.** By 15.10 and 17.5 the back end takes about 0.6 to 0.7 ms a race frame off the render thread (the bindings, pipeline, fixed function and call parts), and the capture adds about 0.1 ms. The replay's recording grows from 1.1 ms a race frame (with step 1) by the moved work and, by 17.5, a hand-over cost of the same order: about 2 to 2.5 ms at 1x, which fits the 4.17 ms of a 240 Hz frame on the main PC; on the Ally X the replay may become the critical thread, which N4's Ally X runs measure. With `async_shader_compilation` off (the launcher's setting) a pipeline creation stalls the replay instead of the render thread.

**Increments**, with the main session's decisions of 23 October 2026:

1. The register-reading functions take a register file, and RTV and `-Rov` run through them unchanged; the capture, the replay's register copy and the check run while the back end still runs at the draw. It measures the capture's size per draw, its cost on the render thread per race frame and the replay's cost per KB before anything moves, and profiles the render thread outside the draw zone. Gate: the check with its perturbation proof (perturbed equal to reported, no other mismatch), a 1x smoke on RTV and on `-Rov` with `-Look "original,taa=0"`.
2. The back end moves to the replay, off by default; `-Native` sets it only after its close gate, which includes the perf gate at 2x and the frozen-frame parity, since the recording of the pipeline and the state moves. Gate: the step gate of section 10 with the check, the render thread's busy time per race frame and per draw against step 1, the submit thread at most 2.5 ms per race frame at 1x on the main PC, and no regression of the race's p99 at the same draw load.

**The budget to 2.5 ms** (the main session's decision of 23 October 2026 asked where it comes from; the numbers are in 17.7). At 1,800 draws a race frame the render thread is busy 3.39 to 3.42 ms. The back end takes the bindings (0.51 ms), the pipeline (0.14) and the fixed function state and the call (about 0.12) off it, about 0.77 ms, and the capture costs 0.24 ms as built: about 2.9 ms after increment 2. The last 0.3 to 0.4 ms has to come from:

1. The capture itself, 0.24 ms (0.17 inside it, about 98 ns a draw, and 0.07 for the marking of written registers): one compact record built where the front half's results are made instead of copied and compared afterwards, the samplers' fetch masks kept per shader pair, and written registers marked once per range in `WriteRegistersFromMem`. Target about 0.1 ms.
2. The register write path outside the draws: register packets (0.51 ms with the split's timers) and the records of the converted writers (0.21 ms, 92,307 registers a race frame) write ranges outside the constants register by register through `WriteRegisterFast`; a bulk path (a copy of the range and only its few hooked registers through `WriteRegister`) has a ceiling of about 0.1 to 0.2 ms.
3. The texture keys (0.19 to 0.21 ms, 15.10, 17.7) of fetch constants that change at draws: the key computed again only when the dwords that make it change.

The other N4 row items do not show in the race's busy time: `WAIT_REG_MEM` is a wait (0.65 ms a race frame), not busy time, and the readbacks are on demand (none in the race, 16.6). For the submit thread the measured parts add up to about 2.1 ms a race frame at 1x (0.73 today, 0.35 for step 1's hand-over, 0.24 for the capture's, about 0.77 of moved work), under the 2.5 ms budget; the hand-over costs 0.1 to 0.2 us a command beyond the replay's own time (17.5, 17.7), which increment 2 measures by its parts first.

### 17.7 Step 2, increment 1 results, 23 October 2026

`d3d12_draw_back_end=1` (off by default; with it off, RTV and `-Rov` run the refactored functions only):

- **The refactor.** The back half's register-reading parts take the register file to read: `draw_back_end::ComputeSystemConstants` (`UpdateSystemConstantValues` with the render target cache's and the texture cache's answers passed in), `ComputeBlendFactorAndStencilReference`, `D3D12TextureCache::GetSamplerParameters(regs, binding)` and `PipelineCache::GetCurrentStateDescription(regs, ...)` with a description-only mode that creates nothing.
- **The capture** (`kDrawBackEnd`, written after the bindings): the context registers of the back end's set that were written since the list's previous capture and changed; the fetch constant slots of the draw's samplers, from step 1's pending mask, so both commands fill one copy of the registers; and the front half's groups that changed (shader translations, primitive state, the draw call, the bound render targets, flags with the jitter, the texture cache's answers for the used textures). The set is the 76 registers named in the back end's functions and the `draw_util` functions they call. The base command processor marks written registers in `WriteRegisterFast` and `WriteRegister` while the capture is on, and the uniform draw's change of `RB_SURFACE_INFO` marks itself.
- **The replay** keeps one register file per list (with step 1's constants) and the front half's groups; with the check it computes the pipeline description, viewport, scissor, blend factor and stencil reference, system constants (its own copy, kept across lists) and sampler parameters from that copy and compares them with the draw's.

On `I:\FM4-build-usa` at 240 Hz and 1x with `-Native`'s cvars (`rexgpu-xenos.dll` SHA-256 beginning `d9037beac1615424`, `fm4_rex.exe` `45c9fc22a52f2c05`, `rexruntime.dll` `fc53a3fb5e89a96f`):

| Check | Result |
|---|---|
| The check with perturbation 1,000 and `d3d12_deferred_constants` (`n4d-chk-career`: race, replay, menus) | 34,778,359 draws checked; mismatches only in the 34,778 perturbed pipeline comparisons, none in the viewport, scissor, blend factor and stencil reference, system constants or sampler parameters |
| The same with all 1,024 context registers captured (the first build, `n4c-chk-career`) | 39,667,998 draws, likewise only the 39,667 perturbed |
| Smoke at 1x with `-Look "original,taa=0"` on RTV (`n4c-rtv`) and `-Rov` (`n4c-rov`), the first build | 6 of 6 cells reached their goals, the photos saved and loaded, no clear gray frame, the three error keys of RTV |

The capture in the race (`n4d-race-cap1`, `-cap2`):

| Per draw | |
|---|---|
| Bytes | 120: 2.1 context registers (of 27 to 29 written since the previous draw), 2.7 fetch constant slots, 32 bytes of groups, the header and masks; about 300 at a list's first draw |
| Capture on the render thread | about 98 ns (time stamp counter) |
| Replay on the submit thread | 23 ns, 0.20 us per KB |

A/B at 1,800 draws a race frame (alternating `n4d-race-off1`, `-cap1`, `-off2`, `-cap2`, fitted per window): render thread busy 3.393 against 3.631 ms (+0.24: 0.17 inside the capture, 0.07 for the marking and around it), the bindings zone unchanged (0.507 and 0.504), the submit thread's recording 0.728 against 0.966 ms (+0.24, of which 0.035 the replay of the commands), p99 5.17 to 5.66 ms with the draw load. Filtering by the 76-register set halved the bytes (from 90 at 5.2 context registers with all 1,024, `n4c-race-cap*`) but not the capture's time (102 against 98 ns): its cost is the per-draw work around the registers, not their comparison.

The render thread outside the draw zone (`n4c-split-race`, `REX_CP_SPLIT`, 1,919 draws a race frame; the split's timers raise the busy time to 4.40 ms, so the values are read as proportions):

| Part | ms a race frame |
|---|---|
| Draw zone | 2.99 (its parts 2.56, the timers' remainder 0.42) |
| Register packets | 0.51 (0.48 in replays) |
| Records of the converted writers (other packets) | 0.21 (711 records, 92,307 registers) |
| Shader loads | 0.14 |
| Draw packets beyond the draw zone | 0.06 |
| Untimed: dispatch, parse, indirect buffers, the record queue | 0.50 |
| `WAIT_REG_MEM` (a wait, not busy time) | 0.65 |

Without the split at 1,800 draws (`n4c-race-off*`): busy 3.42 ms, the draw zone 2.37 ms (textures 0.47, bindings 0.51, render targets 0.26, primitives 0.21, pipeline 0.14, copies 0.12, vertex buffers 0.09, the rest 0.57) and 1.05 ms outside it. Where the last 0.3 to 0.4 ms after increment 2 comes from is in 17.6 (the budget).

### 17.8 Step 2, increment 2 results, 23 October 2026

`d3d12_draw_back_end=2` (off by default; it needs bindless resources, host render targets and `async_shader_compilation` off, as `-Native` sets them, and turns itself off otherwise):

- **The front end** translates the shaders (`PipelineCache::TranslateShadersForDraw`) instead of configuring the pipeline, records no pipeline, viewport, scissor, blend factor, stencil reference, system constants, root parameters or topology for guest draws, fills the system constants' inputs that come from the render target cache and the texture cache, takes the texture SRV indices where the texture cache decides them, and writes `kDrawBackEnd` at the draw with the index buffer view. Afterwards it marks its own caches of that state stale, so its next commands (render target transfers, texture loads, post passes) set what they need again. The capture also carries the float, bool and loop constants of the draw (step 1's slots) and a few knobs of the look.
- **The replay** keeps one register file per list and the D3D12 state the list sets, including what the front end's own commands set as they are replayed (`draw_back_end::ReplayState`), and records the back end into the command list being replayed: the pipeline description from its registers, the lookup and, when missing, the synchronous creation (`ConfigurePipelineDescribed`); viewport, scissor, blend factor and stencil reference; the system constants; the samplers, the bindless sampler heap and its map; the constant buffers from a pool of its own; the descriptor indices; root signature, root parameters, topology, index buffer and the draw. The view heap stays the front end's; the replay sets the sampler heap at each list's start.
- **The check** computes the same results from the live register file at the draw and compares them with the replay's: the pipeline description, viewport, scissor, blend factor and stencil reference, system constants, sampler parameters and the constant buffers' bytes. The capture format is one encoder (`CaptureDrawBackEnd`) and one decoder (`draw_back_end::Decode`).

On `I:\FM4-build-usa` at 240 Hz and 1x with `-Native`'s cvars and the e2e fast route (`rexgpu-xenos.dll` SHA-256 beginning `afcf26f89a895735`, `fm4_rex.exe` `6b843735b19128e7`, `rexruntime.dll` `7ef511d0c95f99d7`):

| Check | Result |
|---|---|
| The check with perturbation 1,000 (`n4g-chk2-career`: race, replay, menus; the back end fills the constant buffers itself, `d3d12_deferred_constants` off) | 41,252,704 draws checked; mismatches only in the 41,252 perturbed pipeline comparisons, none in the other six kinds; no draw dropped, no pipeline created on the replay, the three error keys of RTV, no clear gray frame |
| The same on the build before the last change (`n4f-chk2-career`) | 42,415,761 draws, likewise only the 42,415 perturbed; the race and replay pictures correct |

The step gate, A/B alternating `n4g-race-off1`, `-rep1`, `-off2`, `-rep2`, fitted per window at 1,800 draws a race frame (the AI races put 1,453 to 2,488 draws a frame into the runs):

| At 1,800 draws a race frame | Off | `d3d12_draw_back_end=2` |
|---|---|---|
| Render thread busy | 3.383 ms (1.732 us a draw) | 2.993 ms (1.373 us a draw): −0.39 ms |
| Draw zone | 2.366 | 1.931 |
| Bindings, pipeline, textures zones | 0.509, 0.137, 0.460 | 0.102, 0.033, 0.334 |
| The capture (inside the draw zone) | | 0.248 (138 to 139 ns a draw, 484 bytes: 22 constant slots, 2.0 context registers, 54 bytes of groups) |
| Submit thread recording | 0.722 ms | 1.567 ms |
| Submit thread busy | 0.852 ms | 1.698 ms |
| p99, all race windows pooled | 6.40 ms (1,928 draws a frame) | 5.26 ms (2,064) |

- **Render thread.** The draw zone loses 0.68 ms (the bindings 0.41, the pipeline 0.10, the fixed function state and the calls, and 0.13 in the textures zone, whose code is the same: 245 to 266 ns a draw off against 178 to 184 with the back end moved, in every run, read as the cache effect of the shorter draw) and gains the capture's 0.25 ms; the marking of written registers outside the draw zone (0.07 ms in 17.7) brings the net to −0.39 ms. At the race's mean load of the runs (2,064 draws) the slopes give −0.48 ms.
- **Submit thread.** The replay of `kDrawBackEnd` takes 606 to 612 ns a draw (1.22 to 1.26 ms a race frame), its D3D12 calls included; those calls took about 140 ns a draw in the off runs' replay (pipeline handles, root constant buffer views, index buffer, draw; `n4f-rsplit-roff1` against `-rrep1` with `REX_N4_REPLAY_SPLIT`), so the back end's own work on the replay is about 470 ns a draw, against about 350 ns it took on the render thread. The walk of the list drops from 0.16 to 0.04 ms (fewer commands); the other command kinds cost the same. The recording stays under the 2.5 ms budget at the highest loads of the runs (1.57 + 0.57 us a draw beyond 1,800: 1.97 ms at 2,500 draws).
- **Total CPU time.** The 120 ns a draw the back end costs more on the replay (0.2 ms a race frame at 1,800 draws) is the price of the hand-over: decoding the capture, keeping the register copy, and caches on another core. With the capture and the marking, the two threads together spend about 0.45 ms a race frame more than without the back end moved (render thread −0.39, submit thread +0.85): the price of the pipelining, paid on the thread with room. The Ally X runs of N4 show whether the replay becomes the critical thread there.
- **p99 at the same load.** In windows of 1,700 to 2,000 draws: 4.98 ms off (5 windows, 1,758 draws) against 5.22 (19 windows, 1,875 draws); 2,000 to 2,300 draws: 6.06 (4 windows, 2,276 draws) against 5.25 (24 windows, 2,158 draws). The off runs have few windows in either band, so the bands do not separate the two; pooled, p99 falls.
- **Pipelines on the replay.** `rep1` created 2 pipelines on the replay (26 ms) in a window before the race (its longest frame 21 ms); `rep2` and the check run created none and dropped no draw. With `async_shader_compilation` off the front end creates the same pipelines synchronously where it meets them, so the replay waits only where the render thread would have waited.

The gate's render thread target was a drop of at least 0.4 ms at 1,800 draws: −0.39 ms misses it by 0.01 ms, with the capture costing 0.25 ms of it plus the marking. The submit thread target (2.5 ms) and p99 hold. The main session accepted the gate on 23 October 2026 (the 0.01 ms is within the runs' noise, −0.48 ms at the real load, the check exact, p99 lower) on one condition, since the increment restructures the draw path that RTV and `-Rov` run with the cvar off. On the same binary with the fast route (`n4h-*`):

- **Smokes** at 1x with `-Look "original,taa=0"` on RTV (`n4h-rtv`) and `-Rov` (`n4h-rov`): 6 of 6 cells reached their goals, the photos saved and loaded, no clear gray frame, the error keys of RTV. The warnings against increment 1's smokes (which ran without the fast route) are timing and capture statistics; the smokes' frame rates are not comparable, since a build ran beside them.
- **RTV's race** at 1x with the launcher's defaults, alternating local main's build (`43c7a2c`: `n4h-race-rtvb1`, `-rtvb2`) with this one (`-rtvn1`, `-rtvn2`), fitted per window at 1,800 draws: the draw zone 2.646 against 2.651 ms, its parts within 0.02 ms (bindings 0.499 against 0.516, textures 0.542 against 0.526, pipeline 0.143 against 0.151), the submit thread's recording 0.893 against 0.889 ms, p99 6.67 against 6.31 ms pooled; per draw 1,404 to 1,526 ns against 1,452 to 1,484 ns. The draw path with the cvar off costs the same.

Increment 3 (the capture and the marking to about 0.1 ms, 17.6) is where the rest of the render thread's saving lies.

### 17.9 Step 2, increment 3 results, 23 October 2026

The capture and the marking of written registers, with `d3d12_draw_back_end=2` (still off by default):

- **Marking per range.** The base command processor's `WriteRegistersFromMem` stores a range without marking each register (`WriteRegisterUnmarked`) and marks the range at once (`MarkRegistersWritten`). Single writes through `WriteRegisterFast` and hooked registers through `WriteRegister` still mark themselves.
- **The groups in place.** The capture compares the front half's results with the list's state and writes the changed ones into it, without a temporary list state and its copy; the texture cache's answers are compared for the used textures only. The context registers are compared without a branch.
- **The fetch constants per shader pair.** The capture takes only the fetch constant slots the draw's shaders read: their vertex fetch constants and the fetch constants of their textures and samplers, computed once per shader after its translation (`D3D12Shader::back_end_fetch_slots`). The other written slots stay pending until a draw reads them. The replay still fills the whole fetch constant buffer from its copy; the check compares the slots the shaders read.
- **No zeroed arguments.** The deferred command list grows its stream without zeroing it (an allocator that default-initializes): every command's writer fills its arguments. This applies to every command, with the cvar off too.
- **The trace's own cost.** The capture's time stamps run around every 16th capture (counted 16 times), and the written-register statistic only with the trace. `REX_N4_CAPTURE_SPLIT` times every capture by part.

On `I:\FM4-build-usa` at 240 Hz and 1x with `-Native`'s cvars and the fast route, in two rounds: round 1 (`n4be6`, `rexgpu-xenos.dll` SHA-256 beginning `f66f31de2907ff9e`) without the branchless comparison and the sampled time stamps, round 2 (`n4be7`, `d11fe60c8279e7fc`) with them.

| Check | Result |
|---|---|
| The check with perturbation 1,000 (`n4i-chk2-career`, `n4j-chk2-career`: race, replay, menus) | 40,276,214 and 41,874,077 draws; mismatches only in the 40,276 and 41,874 perturbed pipeline comparisons, none in the other six kinds; no draw dropped, no pipeline created on the replay, the error keys of RTV, no clear gray frame |

The step gate, each round alternating off and the back end twice (`n4i-race-*`, `n4j-race-*`), fitted per window at 1,800 draws a race frame, next to increment 2's (`n4g-race-*`):

| At 1,800 draws a race frame | Increment 2 | Increment 3, round 1 | Round 2 |
|---|---|---|---|
| Render thread busy, off | 3.383 ms | 3.440 | 3.521 |
| Render thread busy, back end on the replay | 2.993 ms | 2.937 | 2.935 |
| The difference | −0.39 ms | −0.50 | −0.59 |
| Outside the draw zone, off and on | 1.016 and 1.062 ms | 1.048 and 1.041 | 1.067 and 1.037 |
| The capture | 138 to 139 ns a draw | 127 to 131 | 137 to 143 (sampled) |
| Bytes and constant slots a draw | 484, 22.0 | 409 to 432, 17.4 to 18.7 | 415 to 425, 17.8 to 18.4 |
| Submit thread recording, on | 1.567 ms | 1.524 | 1.544 |
| p99 pooled, off and on | 6.40 and 5.26 ms | 5.62 and 5.21 | 6.25 and 4.86 |

- **The marking is gone.** Outside the draw zone the back end now costs no more than off (1.041 against 1.048, 1.037 against 1.067), where increment 2 cost 0.046 ms more.
- **The capture.** 127 to 131 ns a draw in round 1, about 10 ns less than increment 2, for 11 to 15% fewer bytes: the fetch slots fall from about 8 to 3.7 a draw. The split (`n4i-csplit-csplit1`, 1,621 draws a frame, its own seven time stamps included): context registers 39.5 ns, constant masks 10.6, groups 33.4, the command's allocation 14.1, the record up to the constant slots 35.2, the rest 13.4. The work is spread over the comparison of about 30 written context registers a draw (2.1 change), the five groups, and the copy of 17.7 constant slots (13.9 float, 3.7 fetch, 0.2 bool and loop); no part dominates. Round 2's sampled time stamps include the same interval, so they do not show its saving.
- **Round 2** does not show beyond the noise: its off runs ran 2 to 3% slower in every zone than round 1's (the laptop warmer), and the back end's equal.

The render thread is at 2.94 ms at 1,800 draws, against the main session's target of about 2.8 ms; the capture at about 0.23 ms against about 0.1, the marking at about 0. What would take the capture further is structural: the constant writes recorded into the list where they are written (the capture then copies no constant slot, about 30 ns a draw by the split, while every written constant is copied whether a draw reads it or not), or the comparison of the context registers moved to the replay (more bytes). Neither is more than about 0.05 ms a race frame. The levers for the remaining 0.14 ms are the next items of 17.6's budget: the bulk register-write path (0.1 to 0.2 ms) and the texture keys (0.19 to 0.21 ms).

The main session accepted increment 3 on 23 October 2026 after a proof on `n4be7` with the cvar off, since the unzeroed arguments change every command of RTV and `-Rov` (`n4l-*`, the fast route):

- **Smokes** at 1x with `-Look "original,taa=0"` on RTV (`n4l-rtv`) and `-Rov` (`n4l-rov`): 6 of 6 cells reached their goals, the photos saved and loaded, no clear gray frame, the error keys of RTV; the warnings are capture statistics and timing.
- **Frozen frames** at 1x (`--scenario freeze` with the draw resolution scale 1), local main's build (`43c7a2c`, `n4l-freeze-*-main`) against `n4be7` (`-proof`), `parity.py`: `-Rov` passes with 3,270.48 against 3,272.93 draws a frame (+0.07%), the predicated packets within 0.03% and 23.33% of the pixels above 4; RTV passes with 3,270.92 against 3,267.93 draws (−0.09%) and 22.48%. Both lie in the spread of one binary's runs ([frozen frames](../e2e/frozen-frames.md#baseline-variance): 21.76% for `-Rov` against `-Rov`), and the crops show the whole scene a few pixels apart (the frozen moment), not a different rendering.
- **RTV's race** at 1x with the launcher's defaults, alternating local main's build with `n4be7` (`n4l-race-rtvm*`, `-rtvp*`), fitted at 1,800 draws: the draw zone 2.653 against 2.678 ms (1,439 to 1,519 against 1,498 to 1,561 ns a draw), the command processor's busy time (the window less its waits) 3.602 against 3.657 ms, both threads together 4.640 against 4.726 ms.

**Open: RTV's busy time with the cvar off.** Both RTV comparisons against local main show the branch's command processor busier by 0.04 to 0.06 ms a race frame (1 to 1.5%: 3.578 against 3.615 with `n4be5`, 3.602 against 3.657 with `n4be7`), at the edge of the noise, while the draw zone per draw overlaps. The close gate's three RTV perf runs against local main's build settle it. If it holds above 1%, the refactor's cost to the path with the cvar off is found and removed before the back end is turned on in `-Native` (inlining lost across `draw_back_end.cpp` with the register file passed in, calls that were inlined into `IssueDraw`, a branch per draw): RTV is the default and does not pay for the native path.

### 17.10 The bulk register-write path, 23 October 2026

The second item of 17.6's budget: the base command processor's `WriteRegistersFromMem` copied the registers between two hooked ones at once (`memory::copy_and_swap`) and sent each hooked register through `WriteRegister` in its place in the range (`n4be8`, on top of increment 3).

- **The identity check** (`FM4_NATIVE_CHECK` with `host_render_check`, perturbation 1,000, the back end on the replay, `n4l-idchk-new`: race, replay, menus) compared 13,929,836 ranges: mismatches only in the 12,505 perturbed, the overwritten, incomplete and dropped ranges as in earlier runs.
- **No saving.** Alternating `n4be7` and `n4be8` at 1x (`n4l-race-repp*` against `-repn*` with the back end on the replay, `-offp1` against `-offn1` without), the render thread's time outside the draw zone at 1,500 draws a race frame (where `n4be8`'s runs lay) was 0.876 against 0.941 ms with the back end and 0.878 against 0.906 ms without; the draw zone 1.906 against 1.894 ms at 1,800 draws. RTV's race (`n4l-race-rtvs*` against `-rtvp*`) kept the command processor's busy time: 3.657 against 3.660 ms at 1,800 draws.

The stores of the registers are not where the register packets' time (17.7) goes: a call per run of unhooked registers costs about what the loop did. The change is not kept, and the 0.1 to 0.2 ms the budget gave it is not there.

### 17.11 The texture keys, 23 October 2026

The third item of 17.6's budget. With `REX_N4_TEXTURE_KEYS=1` the texture cache counts what `RequestTextures` computes again for the used bindings out of sync (`n4m-tkeys-tkeys1`, `n4be9`, the back end on the replay, with `REX_CP_SPLIT`, 1,544 draws a race frame): 2,327 keys a frame (1.51 a draw). 44.1% of them come from a fetch constant with the six dwords of the binding's last one: the title wrote it again, often with other values in between that no draw used, which is why the write-time filter of `d3d12_reuse_draw_bindings` saved nothing in N2 (15.12). 0.6% keep the binding with other dwords (the sampler's fields only), so a compare of the key's bits alone would gain nothing. 55.7% change the key, and 36% of these return to one of the binding's last four keys.

`texture_bindings_keep_same_dwords` (off by default; with `-Native` from the close gate on): each binding keeps the fetch constant it was made from, and a binding out of sync whose fetch constant has the same six dwords is put back in sync without computing anything (no key, signs, swizzle or scale bits, no texture lookup, no change of the bindings; an unchanged key loads nothing either). A binding's reset (each frame, when a texture is destroyed or becomes outdated) clears its dwords. The main session decided against a per-binding list of recent keys (about 0.02 to 0.03 ms for a generation count on the textures' lifetime).

On `I:\FM4-build-usa` at 240 Hz and 1x with `-Native`'s cvars, the back end on the replay and the fast route (`n4be10`):

- **The binding check** (`d3d12_reuse_draw_bindings_check`, perturbation 1,000, `n4n-tkchk-career`: race, replay, menus) compared 34,992,998 bindings kept in sync with keys computed fresh: mismatches only the 34,992 perturbed.
- **The A/B** on the same binary (`n4n-race-tkoff*` against `-tkon*`, fitted at 1,800 draws): the textures zone 0.331 against 0.312 ms, the render thread busy 2.961 against 2.879 ms, both threads together 4.676 against 4.557 ms, p99 5.43 against 4.99 ms pooled. The runs' draw loads differ (2,132 to 2,363 draws a frame off, 1,710 to 1,816 on) and their busy time per draw overlaps (1.565 to 1.608 against 1.591 to 1.616 us), so the textures zone's −0.02 ms is the measure of the change.

The keys that change still cost most of the keys zone: decoding the new fetch constant, the texture lookup and the load's check.

**The render thread re-profiled** (`n4n-split-tksplit1`, `REX_CP_SPLIT` with the step on, 1,633 draws a race frame; the split's timers raise the busy time to 3.22 ms, so the values are read as proportions):

| Part | ms a race frame |
|---|---|
| Draw zone | 2.04 |
| Textures | 0.40 (keys 0.17, loads 0.05, binding update 0.07, the rest 0.12) |
| Render targets (`RenderTargetCache::Update`) | 0.26 |
| The draw call, with the capture | 0.24 |
| Primitives | 0.18 |
| Between the primitives and the render targets (draw traces, shader modifications, the uniform targets' overwrite) | 0.15 |
| Setup before the primitives | 0.10 |
| Bindings (the system constants' inputs and the texture SRV indices) | 0.09 |
| Vertex buffers | 0.09 |
| Between the render targets and the pipeline | 0.07 |
| State, pipeline, before the textures | 0.04, 0.03, 0.02 |
| Not in a part (mostly the timers) | 0.38 |
| Outside the draw zone | 1.18 |
| Register packets | 0.41 (0.38 in the replays of recorded command buffers) |
| Other packets (the converted writers' records) | 0.20 (696 records, 87,785 registers) |
| Shader loads | 0.12 |
| Copies, swap, draw packets beyond the draw zone | 0.12, 0.03, 0.05 |
| Untimed: dispatch, parse, indirect buffers, the record queue | 0.41 |
| `WAIT_REG_MEM` (a wait, not busy time) | 1.11 |

The split's timers cost most where the parts are small and many. The register packets are 23,635 a frame at 17.2 ns each with the split's two time stamps around each, so most of their 0.41 ms is the split itself: without it they take about 0.15 ms, which is also why copying the registers at once saved nothing (17.10). The shader loads (2,165 a frame) and the other packets (896) are less affected; the draw zone's detail timers add about 0.2 ms. Without the split the render thread spends about 1.04 ms a race frame outside the draw zone at 1,800 draws (17.9), of which the packet handlers, copies and swap take about 0.6 ms; the rest, about 0.4 ms, is the dispatch and parse of about 28,000 packets a frame, the indirect buffers (519 replays, 238,912 dwords a frame) and the record queue.

### 17.12 The primitive processor's reuse, 23 October 2026

The main session's next candidate after the re-profile: `PrimitiveProcessor::Process` took 106 to 111 ns a draw in the race. `primitive_processor_reuse` kept the results of the frame's earlier draws in a table of 256 entries keyed by the registers `Process` reads (the draw initiator, the index buffer's base and size, the output path, the tessellation mode, the primitive reset's switch and index), valid until the frame's cache was cleared or a cached index range invalidated; a guest index buffer was still requested in the shared memory at each draw (`n4be11`, on top of the texture keys' step).

- **The check** (`primitive_processor_reuse_check`, perturbation 1,000, `n4o-prchk-career`: race, replay, menus): 11,667,589 reused draws processed again, mismatches only the 11,667 perturbed. 32.7% of the draws reused an earlier draw's processing (30.7% in the race).
- **No saving.** The A/B with every native switch on (`n4o-race-proff*` against `-pron*`): the primitives zone took 106 to 111 ns a draw without the reuse and 121 to 122 ns with it, 0.195 against 0.215 ms at 1,800 draws; the render thread 2.920 against 2.968 ms.

The work a reused draw saves is small next to what every draw keeps (the shared memory request), and the lookup and the store cost the other 69% of the draws more than the 31% save. The change is not kept.

With every kept switch on (the back end on the replay, `texture_bindings_keep_same_dwords`), the render thread is busy 2.92 ms at 1,800 draws a race frame and both threads together 4.63 ms (`n4o-race-proff*`).

### 17.13 The render target cache's update at the draw, 23 October 2026

`render_target_update_guard` (off by default; with `-Native` from the close gate on; host render targets only, never `-Rov`): `D3D12CommandProcessor::UpdateRenderTargets` skips `RenderTargetCache::Update` at a draw when all of these hold, without changing `Update` itself (the main session's decision, so that it does not collide with N1's increment 4):

- **The key** equals the last update's: `Update`'s arguments (rasterization, the normalized depth control and colour mask) and the registers that it and `DrawExtentEstimator::EstimateMaxY` read: `RB_SURFACE_INFO`, `RB_DEPTH_INFO`, `RB_COLOR_INFO` 0 to 3, `PA_SC_WINDOW_OFFSET`, the window scissor's offset switch and bottom right, the screen scissor's bottom right, `PA_CL_VTE_CNTL`, the vertex window offset switch, `PA_SU_VTX_CNTL`, and the viewport's Y scale and offset. With clipping off (`PA_CL_CLIP_CNTL.clip_disable`) the draw extent may come from the vertices, so such a draw always updates.
- **The cache's update epoch** is unchanged since the last update. It counts every change to what `Update` finds besides the key: a change of the EDRAM's ownership (`ChangeOwnership`, where it takes a range over), render targets made, replaced (the growth of native_rt_surfaces bit 8), evicted or destroyed, `ClearCache`, the deferred tiles flushed (bit 4), and every change of a host render target's resource state (`D3D12RenderTarget::SetResourceState`), which covers the resolves, the resolves from the host render target (16.11), the transfers, the dumps and the post passes' `TransitionLastColorRenderTarget` (N1's answer: GTAO, the motion vectors, the cockpit window mask and the mod passes read resolved textures, not host render targets).
- **The last update left nothing for the next one** (`CanRepeatLastUpdate`): the command list has its render targets bound (a new submission, a retired target or `InvalidateCommandListRenderTargets` clear that), no pending transfers, no transfers in the last update, no uniform draw before or now and no uniform draw signature waiting, no deferred tiles, not over the memory cap, and native_rt_surfaces bit 1 off (its counts run per update).

`render_target_update_guard_check` updates anyway at every draw that would skip and checks that the update changed nothing: no command recorded in the deferred command list, the epoch unchanged, the same render targets and formats bound, and nothing left for the next update; `render_target_update_guard_check_perturb=N` changes every N-th verdict.

On `I:\FM4-build-usa` at 240 Hz and 1x with `-Native`'s cvars, the back end on the replay, `texture_bindings_keep_same_dwords` and the fast route (`n4be12`, `rexgpu-xenos.dll` SHA-256 beginning `d8e7b8f3dfca29a8`):

| Check (perturbation 1,000; race, replay, menus) | Result |
|---|---|
| `-Native` (`n4p-gchk-career`) | 36,302,177 of 38,259,615 keyed draws would skip (95%); mismatches only the 36,302 perturbed |
| `-Native` with the one pass in 2x MSAA and the private depth (`FM4_ONE_PASS_MSAA=2`, `n4p-gchk-career-2x`) | 38,094,707 of 40,105,287 (95%; 23,187,228 one-pass updates); mismatches only the 38,094 perturbed |

The A/B (`n4p-race-goff*` against `-gon*`, fitted at 1,800 draws a race frame; 85 to 88% of the race's draws skip): the render targets zone 0.297 against 0.112 ms, the render thread busy 2.952 against 2.720 ms, both threads together 4.660 against 4.399 ms, the submit thread's recording 1.580 against 1.551 ms.

**The race's frame times with the final `-Native` set** (the back end on the replay, `texture_bindings_keep_same_dwords`, the guard; deferred constants left out, the back end fills the buffers itself), three runs (`n4p-race-gon1` to `-gon3`, 1,914 draws a frame, 21,448 frames), against the same switches without the guard and against `-Native` without the back end (`n4i-race-off*`, `n4j-race-off*`, earlier the same day):

| | Final set | Without the guard | Without the back end |
|---|---|---|---|
| Frames per second (per window: mean, 5th percentile) | 237.8, 232.5 | 235.7, 226.4 | 217.8, 198.0 |
| Frame time p50, p99, p99.9 | 4.14, 5.02, 7.52 ms | 4.19, 5.11, 7.87 ms | 4.50, 6.04, 9.22 ms |
| Frames over 5 ms | 1.05% | 1.31% | 14.98% |
| p99 at 1,400 to 2,000 draws a frame | 4.93 to 4.97 ms | 4.96 to 5.00 ms | 5.19 to 5.86 ms |
| p99 at 2,000 to 2,400 draws a frame | 5.29 to 5.33 ms | 5.25 to 5.71 ms | 5.93 to 6.55 ms |
| p99 over 2,400 draws a frame | 5.10 ms (5 windows) | | 6.34 ms |

At 240 Hz the race now runs at the refresh rate in most windows; p99 holds 5.0 ms up to 2,000 draws a frame and exceeds it by 0.3 ms above that.

### 17.14 The main PC's CPU target (the main session's decision of 23 October 2026)

The main session judged the CPU criterion of section 10 met on the main PC with the switches of 17.8 to 17.13 (the back end on the replay, `texture_bindings_keep_same_dwords`, `render_target_update_guard`):

- The render thread is busy 2.72 ms per race frame at 1,800 draws, against the 2.5 ms of section 10.
- The race runs at the refresh rate at 240 Hz (238.6 fps in the median window, 1.05% of the frames over 5 ms), with p99 5.02 ms pooled, at most 5.0 ms up to 2,000 draws a frame and 5.1 to 5.3 ms above (17.13).
- The 2.5 ms stood in for p99 at 240 Hz, and that purpose holds within 0.02 ms pooled. Most of what remains is the dispatch of about 28,000 PM4 packets a frame, about 0.4 ms (17.11), which only fewer packets reduce: it moves to the API-level stage, where draws become records.
- The criterion's other half, the ROG Xbox Ally X (8.1), stays open.

Next: N4's close gate, which turns the three switches on in `-Native`, then the other items of N4's row (section 9) with their own step gates.
