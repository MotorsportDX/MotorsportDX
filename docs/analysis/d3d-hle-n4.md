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
- The criterion's other half, the ROG Xbox Ally X (8.1), stays open (measured in [17.21](#1721-the-rog-xbox-ally-x-runs-24-october-2026)).

Next: N4's close gate, which turns the three switches on in `-Native`, then the other items of N4's row (section 9) with their own step gates.

### 17.15 The close gate, 23 October 2026

From the close gate on, `-Native` sets the three switches of 17.8 to 17.13 beside N1's native cvars: `d3d12_draw_back_end=2`, `texture_bindings_keep_same_dwords` and `render_target_update_guard`. `d3d12_deferred_constants` stays off: with the back end on the replay, the replay fills the constant buffers itself (17.8).

**The gate** ran on `n4be12` (85cf1fb's code, with the three switches), with the fast route at 240 Hz. `-Native` ran one tile, as before e2e's one-pass default of the same day.

| Check | Result |
|---|---|
| The combined checks (`n4r-check-career`: identity, back end, bindings and guard, each perturbed every 1,000th; race, replay, menus) | back end 21.2 million draws, bindings 17.3 million, guard 18.1 million skips, identity 14.3 million ranges: mismatches only the perturbed |
| The same in one pass with 2x MSAA (`n4r-check-career-2x`) | 21.3, 17.4, 18.3 and 14.5 million: mismatches only the perturbed; two new pipelines created on the replay (none in one tile) |
| Frozen frames, RTV against `-Native`, both with `FM4_TILING_SCENARIO=2` (`n4r-parity-1x`, `-2x`, `parity.py`) | 1x passes: 2,236.43 against 2,236.94 draws a frame (+0.02%), the predicated packets +0.01%, 19.26% of the pixels above 4; on the frozen frame the render thread is 28% less busy than RTV's command processor. 2x passes: 2,234.45 against 2,236.93 draws (+0.1%), 21.09%. RTV's first 2x freeze timed out waiting for the rewind menu and passed when run again (`n4r2-freeze-rtv-2x`). Correction (24 October 2026): the `freeze` scenario pins the look `original,taa=0` and render scale 2x unless `--look` or `--arg` override them (`FREEZE_LOOK`, `FREEZE_ARGS` in `e2e.py`), and the 1x cells set neither, so both rows ran at 2x. Parity at 2x passed, which N4's conclusions rest on; N1's rerun adds a parity at 1x |
| Smokes at 1x with `-Look "original,taa=0"` on RTV and `-Rov` (`n4r-rtv`, `n4r-rov`) | 6 of 6 cells reached their goals, the photos saved and loaded, no clear gray frame, the error keys of RTV; the warnings are capture statistics (other photo moments, checked by eye) and one wait timing |

The races, three runs each way, alternating (`n4r-race-*`):

| Race at 240 Hz | RTV, 1x | `-Native`, 1x | RTV, 2x | `-Native`, 2x |
|---|---|---|---|---|
| Frames per second (the race phase, mean) | 218.3 | 237.7 | 165.6 | 198.3 |
| GPU time a frame | 2.88 to 2.98 ms | 2.21 to 2.27 | 5.80 to 6.15 | 4.89 to 5.00 |
| p99 pooled | 6.36 ms | 5.01 | 8.98 | 6.75 |
| Command processor (RTV) or render thread busy at 1,800 draws a frame | 3.659 ms | 2.687 | 3.951 | 2.826 |
| Both threads together at 1,800 draws | 4.676 ms | 4.343 | 6.727 | 5.382 |

At 1x, `-Native`'s p99 lies at 4.73 to 5.18 ms in every band of draw load. At 2x its GPU time stays under RTV's, as section 10 requires.

**RTV with the switches off.** In the gate, RTV's command processor was 3% busier than on local main's build before step 2 (`n4main`, 43c7a2c): 3.659 against 3.552 ms at 1,800 draws, and the draw zone 2.688 against 2.613 ms. A bisect, three races per binary, alternating in one session:

| Binary | Command processor busy at 1,800 draws | Draw zone |
|---|---|---|
| `n4main` (43c7a2c) | 3.608 ms | 2.644 |
| `n4bis1` (7ffba4f: increments 1 to 3 and the texture keys) | 3.649 (+0.041) | 2.658 |
| `n4bis2` (dc996eb: with N1's four commits of that day) | 3.639 | 2.668 |
| `n4be12` (with the guard) | 3.647 (+0.039) | 2.668 |

That is +1.1%, all of it from N4's increments. Per run, `n4main` took 1.969 to 2.037 µs per draw and `n4be12` 1.969 to 2.040, so the difference sits at the edge of the noise. The gate's 3% was the session's drift.

By zone, the same in every build:
- the pipeline +0.007 to 0.009 ms: the split of `ConfigurePipeline` for the back end, a call of `ConfigurePipelineDescribed` with 12 arguments;
- the textures +0.009 ms: the copy of the fetch dwords and the larger `TextureBinding`, with the switch off too;
- outside the draw zone +0.015 ms.

Two cheap changes, neither of which changes behaviour:
- `ConfigurePipelineDescribed` is inlined into `ConfigurePipeline` (`[[gnu::always_inline]]`);
- the texture cache compares and copies the fetch dwords only with `texture_bindings_keep_same_dwords` on.

The re-measure on `n4be13` (with both changes), three runs each, alternating with `n4main`:
- 3.751 against 3.660 ms at 1,800 draws (+2.5% by the fit);
- per run, 1.981 to 2.204 µs per draw against 1.963 to 2.108, at 1,465 to 2,589 draws a frame, so the ranges overlap;
- the fit moves by about 2% between sessions.

The changes show no measurable effect. The main session accepted RTV's cost as at the edge of the noise, without a further round. Both RTV tables may be affected by the stale title objects described below, since `n4bis1`, `n4bis2`, `n4be12` and `n4be13` were all built with the same copy of the title's sources. On `n4be13`, RTV rendered correctly.

**Stale title objects: `n4be13`'s `-Native` numbers are void.** On `n4be13`, `-Native` with e2e's one-pass default (`FM4_TILING_SCENARIO=1`, `FM4_ONE_PASS_MSAA=2`, `native_one_pass_msaa=2`, `native_rt_surfaces=10`) made no one-pass resolve. A race on `n4be12` and one on N1's `n4g2xpn` with the same settings and N4's switches off made 6,530 and 6,842. Its races at 1x and 2x showed the upper half of the screen washed out to white and the scene missing. Its numbers (230.6 fps at 1x, 199.7 at 2x) measure that broken rendering and are void.

The combined check passed, since it compares the paths with each other, not with the image, and no clear gray frame was counted. `n4be13` and the corrected `n4be14` have the same `rexgpu-xenos.dll` and `rexruntime.dll` and differ in `fm4_rex.exe` only.

The cause: N4's build script (outside the repository) copied the title's sources into the BuildRoot's `project\src` with `Copy-Item`, which keeps a file's time. Objects that another session had compiled minutes earlier from its uncommitted title change were newer than the copied sources, so the build linked them. The runtime was not affected: `Build.ps1` mirrors it with `pipeline.copy_source`, which writes a changed file with the current time.

Since then:
- **The copy.** N4's build script copies a changed file with the current time, and `copy_source` sets the time explicitly.
- **The source of a build.** `build-result.json` records `source`: the HEAD of the checkout that built, whether its `work/runtime` or `work/fm4-rex` differ from it, and a SHA-256 over the BuildRoot's `runtime-source` and the project's title sources. A `matrix.py` snapshot keeps the file, and each e2e run copies `source` into `binary` in `summary.json`.
- **The image in every step gate** (the main session's rule): a native race or photo capture at 1:1 beside the reference, N1's runs or RTV.
- **A WARN for washed-out captures.** `e2e.py` counts the race's captures with a horizontal band washed out to white (`washed_out_frames`), and `compare.py` warns when the count rises. Over 5,098 race captures of 478 earlier runs, it flags only `n4be13`'s five `-Native` runs, in 82 of their 83 race captures.

**The step gate on `n4be14`** (f766d0f's code, the title compiled again; `rexgpu-xenos.dll` SHA-256 beginning `90f2ed4a8c03c569`, `fm4_rex.exe` `63b7567f902d236a`) ran with e2e's `-Native` default, one pass with 2x MSAA and the three switches, one run each. The references are N1's `n4g1xpn` and `n4g2xpn` (`-Native` without N4's switches, three runs each, the race phase of `photo`):

| Race at 240 Hz | `n4be14`, 1x | `n4g1xpn` | `n4be14`, 2x | `n4g2xpn` |
|---|---|---|---|---|
| Frames per second (the race phase, mean) | 238.2 | 215.1 to 236.0 | 181.5 | 177.8 to 181.9 |
| GPU time a frame | 2.25 ms | 2.17 to 2.32 | 5.36 | 5.36 to 5.50 |
| p99 | 4.96 ms | 5.10 to 5.84 | 7.90 | 7.98 to 8.19 |
| Draws a frame | 1,581 | 1,472 to 2,043 | 1,890 | 2,065 to 2,370 |
| Render thread busy per draw | 1.61 µs | 1.85 to 1.94 | 1.74 | 2.04 to 2.09 |
| One-pass resolves in the race | 7,398 | 7,332 to 8,043 | 5,643 | 5,884 to 6,019 |
| Render target memory | 199.5 MB | 199.5 | 580.5 | 580.5 |

- **The render thread** takes 13 to 17% less time per draw at 1x and 14 to 16% less at 2x. `n4be14`'s runs had fewer draws a frame, which raises the time per draw (the frame's fixed costs are spread over fewer draws), so the comparison is conservative.
- **The GPU** time is the same as without N4's switches. At 2x the race is bound by the GPU.
- **The one pass** shows in the one-pass resolves and the render target memory, as in N1's runs. With the guard, `one_pass.draws` counts the render target updates in one pass, not the draws, because the skipped draws repeat the last update: 1.4% of the race's draws against 39 to 41% without the guard.
- **The image.** The 2x captures (2560x1440, `--keep-ppm`) sit at 1:1 beside RTV's on the same binary (`n4w-race-rtv2k1`: 164.5 fps, 5.93 ms of GPU time). The scene is complete in every band, with the same detail on the cockpit, the hood, the road, the mirror and the glass roof. No capture is washed out or shows the clear color.
- **The combined checks** (`n4w-check-career`, e2e's `-Native` default: identity, back end, bindings and guard, each perturbed every 1,000th): back end 21,251,886 draws, bindings 17,814,546, guard 18,299,170 skips of 19,382,660 keyed draws (94%), identity 14,203,182 ranges: mismatches only the perturbed. No draw was dropped; two new pipelines were created on the replay (none in one tile, `n4r-check-career`). The run counted 10,414,321 one-pass updates, no clear gray or washed-out capture, and the error keys of RTV and of the perturbed checks.

N4's close gate is done. Next are the other items of N4's row (section 9), each with its own step gate:
1. event waits in place of spins, measured by the process's CPU time and the package power;
2. the vblank interrupt and the readback copies on the host render thread;
3. the queue depth and the latency;
4. a proxy for a smaller CPU: the affinity set to 4 physical cores without SMT, and to 8 threads.

Then comes the ROG Xbox Ally X export.

### 17.16 Event waits in place of the remaining spins, 23 October 2026

The first item after the close gate.

**Where the CPU goes.** e2e now records the game process's CPU time and the CPU package's and cores' power each second, and the CPU time of each thread per phase (`cpu_power`, [e2e harness](../e2e/README.md); the power from the Windows "Energy Meter" counters, RAPL on this laptop). In the 240 Hz race at 1x on `n4be14` (`n4x-race-natp1`, `n4x-race-rtvp1`):
- the process used 3.61 cores on `-Native` and on RTV, and the package 77 and 76 W;
- one guest thread used a whole core, 978 and 969 ms a second;
- the host render thread used 704 ms a second, which is its busy time: it does not spin.

**What spins.** A sampling profiler (the instruction pointers of the busiest threads mapped to the title's functions; `n4y-race-prof1`) and the new `REX_GUEST_YIELD_TRACE` ([traces](../e2e/traces.md#guest-yields-rex_guest_yield_trace)) on `n4be15`:
- The thread with the whole core runs the title's command buffers (`D3DDevice_RunCommandBuffer`). It waits for the GPU in the ring index wait (`sub_822B0788`, `Resource::WaitForFence`), a loop around the timed poll `sub_826E8358` that reads the read pointer the command processor writes back, without sleeping.
- The guest's yields cost little. `NtYieldExecution` from a spin lock (`sub_822B96E0`) ran 3.5 million times in a race on the main thread, about 3 ms a second, and `Sleep(0)` about 6 ms a second.
- `WAIT_REG_MEM`'s polls without a sleep time never wait: 3.8 million packets, none waited (`REX_WAIT_REG_MEM_TRACE`). The swap's vblank wait sleeps on its event.

**The change.** `guest_ring_wait_sleep` is off by default; the launcher turns it on for RTV and `-Native` and off for `-Rov` (below):
- the command processor and the host render thread count a publication after each write-back of the read pointer (`rex::system::gpu_progress`);
- the title's hook of the ring index wait marks the awaited index, and each timed poll first sleeps until the read pointer has passed it or is published again, at most 1 ms;
- the timed poll then runs as before, its GPU hang timeout included;
- the perf trace counts the sleeps (`guest_ring_waits`).

**The step gate** ran on `n4be16` (ca7c690's code) at 240 Hz and 1x with e2e's `-Native` default, the switch off and on, twice each, alternating (`n4z-race-off1`, `-on1`, `-off2`, `-on2`):

| Race at 240 Hz, 1x | Off | On |
|---|---|---|
| The process's CPU time | 3,608 and 3,456 ms a second | 3,318 and 3,241 |
| The package's power | 74.1 and 72.2 W | 70.8 and 69.8 |
| The waiting thread's CPU time | 972 and 980 ms a second | 788 and 793 |
| Frames per second | 236.6 and 239.0 | 238.3 and 238.8 |
| p99 | 5.04 and 4.97 ms | 5.05 and 4.97 |
| Render thread busy per draw | 1.47 and 1.54 µs | 1.48 and 1.52 |
| GPU time a frame | 2.35 and 2.24 ms | 2.33 and 2.27 |
| Ring waits that slept | | 205 to 213 a second, 182 to 192 ms a second; 45 to 49 a second ended at the limit |

- **The CPU.** The process uses 0.22 to 0.29 cores less (6 to 8%), and the package 2.4 to 3.3 W less. The render thread, the frame rate, p99 and the GPU time do not change.
- **The waiting thread** now sleeps about a fifth of its time. The rest is work: a profile with the switch (`n4z-race-prof2`) shows the command buffers, vector math and the native records, and the condition variable's wait where it slept.
- **The image.** At 2x with the switch (`n4z-race-on2k`): 182.9 fps, 5.37 ms of GPU time, p99 7.5 ms. The 2560x1440 captures at 1:1 beside RTV's (`n4w-race-rtv2k1`) show the whole scene with the same detail, and none is washed out.
- **A career with the switch** (`n4z-oncareer`) reached its goal through the race and the replay, with the error keys of RTV.
- **RTV with the switch off** runs only the publication's count. Its command processor took 1.98 µs per draw at 2,098 draws a frame (`n4z-race-rtv1`), against 2.03 µs at 1,988 on `n4be14` (`n4x-race-rtvp1`).
- **The power counters** compare alternating runs to within about 1 W between runs of one setting. A run with the profiler, which suspends the threads it samples, read 87.7 W.

**On RTV as well (the main session's decision of 23 October 2026).** RTV's own waiting thread spun the same way (969 ms a second). RTV is the launcher's default path and the ROG Xbox Ally X gains from the lower power, so the launcher now turns the switch on for RTV as well as `-Native`. `-Rov` stays off: it remains the unchanged reference and runs no new title hook (`Build.ps1 -Rov`, and `e2e.py` with `render_target_path_d3d12=rov`).

The RTV step gate ran on `n4be17` (9d26366's code; the switch is the same as on `n4be16`) at 240 Hz and 1x, off and on twice each, alternating (`n4rw-race-rtvoff1`, `-rtvon1`, `-rtvoff2`, `-rtvon2`):

| RTV race at 240 Hz, 1x | Off | On |
|---|---|---|
| The process's CPU time | 3,625 and 3,641 ms a second | 3,316 and 3,249 |
| The package's power | 75.5 and 75.5 W | 72.9 and 72.7 |
| The waiting thread's CPU time | 965 and 979 ms a second | 732 and 732 |
| Command processor busy per draw | 2.00 and 2.02 µs | 1.98 and 1.99 |
| GPU time a frame | 3.01 and 2.94 ms | 3.01 and 2.94 |
| Frames per second, p99 | 204.2 at 2,175 draws and 228.6 at 1,630; 6.83 and 5.90 ms | 219.8 at 1,802 and 225.8 at 1,675; 6.16 and 5.96 ms |

- **The CPU.** On RTV the process uses 0.31 to 0.39 cores less (9 to 11%), and the package 2.6 to 2.8 W less. The command processor's time per draw and the GPU time do not change; the frame rates follow the runs' draw loads.
- **A career with the switch** (`n4rw-rtvcareer`) reached its goal through the race and the replay, with the error keys of RTV.
- **The image.** At 2x with the switch (`n4rw-race-rtv2kon`: 156.4 fps, 6.19 ms of GPU time), the 2560x1440 captures at 1:1 beside RTV's without it (`n4w-race-rtv2k1`) show the same rendering in frames of the same light. The race's light changes over time (the gauges lit by the sun early in the lap, dark later), so frames are matched by their light, not their number.

Next: the vblank interrupt and the readback copies on the host render thread.

### 17.17 The vblank interrupt and the readback copies, 23 October 2026

Section 9's row planned to move the vblank interrupt and the resolve readbacks' copies onto the host render thread (14.2), so that one thread would make every guest-visible write. Measured with the `-Native` default on `n4be16` and `n4be17`:
- **No readback copy runs.** Every resolve's readback waits for the CPU's first access (16.5): about 6,960 resolves a second are deferred in the race, and none is requested, served or forced, in the race or in the replay of a career (`n4z-oncareer`).
- **The vblank interrupt** costs the vsync thread about 7.5 ms a second (`cpu_power`), for 215 to 233 vblanks a second on the grid and 5 to 21 late ones. The swap's wait for the vblank sleeps on its event (17.16).
- **The ordering** that the move was for holds without it: the identity, back end, binding and guard checks stay exact (17.15).

The move has nothing to gain; the item is dropped.

### 17.18 The queue depth and the latency, 23 October 2026

The perf trace now times each record's wait in the host render queue, from its push by the guest thread that kicked the ring to its take by the host render thread (`queue_wait_ms`, [traces](../e2e/traces.md#performance-trace-rex_perf_trace_file)). In the 240 Hz race at 1x on `n4be17` with the `-Native` default (`n4af-race-on1` and the affinity runs of 17.19):
- a record waits 0.73 to 0.86 ms on average, 0.5 to 1 ms at the median, 3 to 3.75 ms at p99 and 6.5 to 12.4 ms at most;
- about 690 to 800 records are taken a second, with 18.5 to 20.4 queued when one is taken, and no span merged (the capacity is 256).

Even at p99 a record waits less than a frame (4.17 ms): the queue adds at most a frame to the latency, as designed (one frame of depth, 14.6). A smaller capacity would merge spans without shortening the wait, and the guest's run-ahead is bounded by its own ring index waits and the swap. Nothing is tuned; the item is closed with the measure.

### 17.19 A smaller CPU, and the timer resolution, 23 October 2026

The last item before the ROG Xbox Ally X: a run with fewer cores as a quick proxy for a slower CPU. `e2e.py --affinity <mask>` confines the game process to some logical processors ([e2e harness](../e2e/README.md)). `cpu_power.py` sets the mask after the launch and again each second, since the runtime sets the system's mask once at its start, and records the processors' clock and busy share. The laptop's Core Ultra 7 255HX has 8 performance cores (P; logical 0, 1, 6, 7, 8, 9, 18 and 19) and 12 efficient cores (E; logical 2 to 5 and 10 to 17), without SMT.

**The P-cores** (`n4be17`, the 240 Hz race at 1x, one run each; `n4af-race-*`), p99 and the share of frames over 5 ms in the bands of draw load the runs share:

| Race at 240 Hz, 1x | 1,600 to 2,000 draws a frame | Frames per second |
|---|---|---|
| All cores, `-Native` (three runs, 17.16) | p99 4.94 to 5.09 ms, 0.8 to 1.2% over 5 ms | 237.3 |
| 4 P-cores (`0xC3`), `-Native` with the ring wait's sleep | p99 5.01 to 5.40 ms, 1.3 to 2.2% | 238.2 |
| 4 P-cores, `-Native` without it | p99 6.49 to 6.88 ms, 7.8 to 8.8% | 225.6 |
| 4 P-cores, RTV without it | p99 7.72 to 8.31 ms, 15 to 30% | 219.1 |
| 8 P-cores (`0xC03C3`), `-Native` with it | p99 4.74 to 4.96 ms, 0.7 to 1.0% | 239.3 |
| 8 P-cores, RTV without it | p99 5.83 to 6.59 ms, 5 to 16% | 220.5 |

- **Four P-cores** hold `-Native` at the refresh rate when the ring wait sleeps. Without the sleep the waiting thread takes one of the four cores, and p99 rises by about 1.5 ms. The CPU time falls from 3,314 to 2,926 ms a second and the package from 71.7 to 66.1 W.
- **Eight P-cores** behave like the whole CPU.
- The P-cores run at about 5 GHz, so they stand in for fewer cores, not slower ones.

**The E-cores are not a valid proxy here.** Races confined to E-cores fell to 12 to 56 fps with p99 of 76 to 942 ms, on `-Native` and RTV, with the ring wait's sleep and without it, on `n4be17`, `n4be18` and `n4be19`. One run without the sleep recovered after three windows (17 to 24 fps, then about 228). In such a run:
- the processors stayed at about 4.4 GHz but were 28 to 45% busy;
- almost every vblank was forced (89 to 108 a second, 0 to 15 on the grid);
- the host render thread waited for the guest (guest_idle 800 to 860 ms a second);
- the guest's `Sleep(0)` took up to 35.8 ms a call and `Sleep(1)` 9.05 ms (`REX_GUEST_YIELD_TRACE`, `n4qs-race-trace18`).

Ready threads waited for a core. Spinning processes confined the same way show why (the processors' busy share each second, without the game):

| Spinning processes, mask | Busy share of the masked processors |
|---|---|
| 4 on 4 E-cores (`0x3C`) | 98/2/2/3, 100/33/33/33, then 100/0/0/0 with the other three at 0 MHz |
| 8 on 8 E-cores (`0x3C3C`) | 100/100/100/100 and 0/0/0/0: one cluster of four runs, the other stays parked |
| 4 on 4 P-cores (`0xC3`) | 100/100/100/100 |

Windows parks E-cores inside an affinity mask, so a mask of four E-cores gives one to four cores, as the parking decides. The collapse is that of a single core, with or without the ring wait's sleep; the sleep only lowers the load and so makes the parked state last. Without a mask the scheduler unparks cores as the load needs. The E-core runs are recorded as invalid, and a bounded spin before the sleep (100 µs, `n4be18`) did not change them and was removed.

**The timer resolution.** Without a request of its own, a process's `Sleep(1)` takes the default tick, 15.66 ms on this laptop, and 1.5 to 2 ms with `timeBeginPeriod(1)`. The guest's `Sleep(1)` took 1.58 ms on average in the races, so some other component of the process asks for the resolution today. `high_resolution_timing` (on by default, every path, `-Rov` included: a host platform setting, not a title correction) asks for 1 ms at the runtime's start and opts the process out of power throttling (`ProcessPowerThrottling`: the execution speed and the timer resolution kept), which a Windows handheld may apply. It removes the hidden dependency. It does not change the E-core collapse (`n4be19`).

Its cost at full CPU, with the setting off and on in pairs at 240 Hz and 1x (`n4qs-race-fulltoff` and `-fullon`, `-fullrtvtoff` and `-fullrtvon` on `n4be19`; `n4fn-race-*` on `n4be20`):
- the package's power differs by −3.6 to +3.7 W across five pairs, +0.3 W on average and +0.2 W at the median, as the races' draw loads (1,511 to 2,308 draws a frame) move it more than the setting;
- the render thread's and the command processor's time per draw stay the same (1.50 to 1.60 µs on `-Native`, 1.84 to 2.01 µs on RTV);
- on `-Rov` (`n4fn-race-rovtoff1`, `-rovton1`) the pair's draw loads differ too much to compare frame rates (1,988 and 3,205 draws a frame). The command processor's time per draw (2.00 and 1.89 µs) and the GPU's (4.3 and 3.5 µs) follow the load.

That is as expected: the resolution was already 1 ms through another component, and Windows does not throttle a foreground process on AC power. The cost is under the 1 W the main session set, so the setting stays on.

**For the Ally X runs**, without Python on the handheld:
- the GUI launcher runs the native path (`-Native`, `-MsaaSamples 0`);
- it writes the performance trace to the logs folder;
- it applies extra cvars for the A/B runs ([running](../running.md#ally-x-run-set)).

The perf trace now carries the process's CPU time and its threads' by name in each window (`process_cpu`, `perf_trace_cpu`). A sampler thread of its own measures each second. The first version sampled at the swap on the render thread, and its thread snapshot every tenth second held a frame for 45 to 48 ms (`n4be20`).

**Item 4 closes** with the P-core masks as the only valid proxy on this laptop. A proxy for slower cores would need the power plan's maximum processor state lowered, a system setting left to the user. The ROG Xbox Ally X runs are the real test of a slower CPU: `-Native` and RTV with the defaults, then `-Native` with `guest_ring_wait_sleep=false` and with `high_resolution_timing=false`.

### 17.20 The identity check's late write, 23 October 2026

N3's careers with the identity check saw a non-marker range change after its writer returned twice, in the menus or the load ([16.17](d3d-hle-n3.md#1617-increment-4-step-0-the-one-pass-probe-23-october-2026)):
- in `n3x-check-career` (4x): writer `82311080`, `DrawIndexedVertices`, a range of 16 dwords, dword 15;
- in `n3m-check-career` (the 2x default): the same writer, 17 dwords, dword 15.

N1's record path would carry the old dword, so the late write is chased.

**The writer.** In a tiling bracket (device+11068 bit 0), `DrawIndexedVertices` stores the draw's address in the device's draw list (device+13600: entries of 16 bytes in chunks allocated from the ring by `sub_823719A8`). After its register writes it writes a tail of 14 dwords:
- `SET_BIN_MASK_LO` `0xFFFFFFFF`;
- `DRAW_INDX` with its four dwords;
- `SET_BIN_MASK_LO` `0x80000000`;
- `EVENT_WRITE_EXT`, which writes the draw's screen extents into its list entry;
- `EVENT_WRITE` `25`, which closes the tail.

Dword 15 lies in the closing `EVENT_WRITE`: its data in a range of 16 dwords, its header in one of 17. `EndTiling` (`sub_8236FA20`) passes a pointer to the list to the tile stream interpreter (`sub_826ED978`), which writes only its own context. The runtime's `EVENT_WRITE_EXT` writes exactly the 12 bytes of an entry's extents.

**The diagnostic.**
- The identity check reports a changed range with the dword's guest physical address, its old and new value and the number of changed dwords.
- The late write trace (with `FM4_NATIVE_CHECK`, off with `FM4_LATE_WRITE_TRACE=0`; [traces](../e2e/traces.md#identity-check-of-the-native-path-fm4_native_check-host_render_check)) keeps the last four dwords of the 1,024 latest checked ranges.
- The trace hooks `EndTiling`, the tile stream interpreter and its drain (`sub_826EDCE8`), the list helper, the three other readers of the list (`sub_827E6758`, `sub_827E6DC8`, `sub_827E5468`) and `DrawIndexedVertices` itself (its 32 latest ranges). Each compares the dwords of ranges the host render thread has not reached before and after the original body.
- A change is noted with the function, the dword and the guest call chain (the link register and the saved link registers of the stack's back chain). The identity check prints the notes of a range whose PM4 it found changed and drops the others.
- Printing every note (`FM4_LATE_WRITE_TRACE=2`) gave 1,461 to 4,542 notes a career. All were of ranges that never ran: entries that wait until a newer one replaces them (`check_overwritten`) while the ring reuses their memory, for example for `EndTiling`'s stream and the list helper's chunk links.
- The trace costs no visible frame rate in the check careers (the race at 103 to 116 fps, as before).

**The search.** The search ran 14 `-Native` careers with the identity check on `n4be22` to `n4be24` (`n4lw-career-lw1` to `-lw14`). It compared 206,032,485 ranges and found no mismatch, where the earlier rate of about one in 12 million would have given about 17.

Both sightings ran without `guest_ring_wait_sleep` (`n3m` was launched without it, and `n3x` came before it). Four careers without it (`n4lw-career-ls1` to `-ls4`): 60,248,600 ranges, no mismatch either. The sleep is not shown to matter: 266,281,085 ranges without a mismatch in all.

The late write did not come back in the time box, so it stays open:
- **The candidates** are the code that touches the tiling bracket's draws after the writer returned: `EndTiling` and its list, the list helper `sub_823719A8` (which links a new chunk into ring memory), and the tile stream interpreter. The trace hooks all of them, and also catches the case that none of them wrote the dword.
- **The residual risk** concerns N1's record path only, which would carry the old dword of such a range. The defaults (RTV and `-Native`) run with the ring wait's sleep; `-Rov` runs without it but does not use the record path.
- **The diagnostic stays on** with `FM4_NATIVE_CHECK` in every close gate's career. A further sighting then names the function that rewrote the dword, or rules out the hooked ones. The fix follows from that: the record reads the dword again at the hand-over if the rewrite is the title's own, or the writer side changes if it is not.

### 17.21 The ROG Xbox Ally X runs, 24 October 2026

The user ran the [Ally X run set](../running.md#ally-x-run-set) on the handheld (AMD Radeon 890M, the 1080p panel at 120 Hz) from an export of `de4e9bd`, once on AC power at a 35 W TDP and once on battery at 17 W, with runs 1 to 4, 6 and 7 (not 5). Each is a Quick Race of about 160 s with the race at 120 fps. The traces carry no phase marks: the race is taken as the windows after the countdown (the vblank at 240 Hz) while a frame has more than 400 draws, without the first two. The draw load is the same in every run, 1,220 to 1,270 draws a frame (1,250 to 1,370 at 2x).

| Run | Power | fps | p99 ms | frames > 8.33 ms | > 16.7 ms | GPU ms a frame | render thread ms a frame | µs a draw | process CPU ms/s |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 RTV | 35 W | 106.5 | 18.22 | 89.1% | 1.17% | 7.95 | 4.60 (CP) | 3.67 | 2,886 |
| 2 Native | 35 W | 112.5 | 14.34 | 72.9% | 0.73% | 7.54 | 4.36 | 3.45 | 2,933 |
| 3 Native, sleep off | 35 W | 110.3 | 14.61 | 78.9% | 0.74% | 7.70 | 4.36 | 3.49 | 3,149 |
| 4 Native, timer off | 35 W | 113.2 | 13.49 | 68.3% | 0.62% | 7.46 | 4.35 | 3.48 | 2,914 |
| 6 Native, 4x | 35 W | 103.4 | 15.26 | 94.0% | 0.85% | 8.33 | 4.35 | 3.53 | 2,736 |
| 7 Native, 2x scale | 35 W | 39.1 | 40.71 | 99.8% | 97.6% | 23.91 | 8.09 | 5.91 | 2,437 |
| 1 RTV | 17 W | 91.6 | 24.45 | 96.7% | 2.39% | 9.21 | 6.90 (CP) | 5.62 | 3,277 |
| 2 Native | 17 W | 93.3 | 19.52 | 97.9% | 1.52% | 9.35 | 6.11 | 4.99 | 3,237 |
| 3 Native, sleep off | 17 W | 90.5 | 19.59 | 98.2% | 1.72% | 9.66 | 6.18 | 5.05 | 3,512 |
| 4 Native, timer off | 17 W | 92.4 | 19.32 | 98.1% | 1.60% | 9.43 | 6.28 | 4.96 | 3,209 |
| 6 Native, 4x | 17 W | 85.7 | 20.73 | 98.8% | 1.85% | 10.20 | 6.39 | 5.05 | 3,089 |
| 7 Native, 2x scale | 17 W | 34.6 | 45.09 | 100% | 98.5% | 27.15 | 9.28 | 7.41 | 2,517 |

The render thread of RTV is the command processor (the window less its waits). One run a side, so differences of a few percent are within the noise of a race (17.15).

**The threads** (`perf_trace_cpu`, ms a second, run 2 at 35 W): the busiest guest thread 735, the next two 473 and 410, the host render thread 461, the submit thread 279 (its replay 168), the timer queue 125. At 17 W the host render thread takes 14% more and the submit thread 10% more, at 17% fewer frames. No thread is saturated.

**(a) Native against RTV** (2 against 1): at 35 W 5.6% more fps and p99 21% lower (14.34 against 18.22 ms), the GPU 0.41 ms a frame less; at 17 W 1.9% more fps and p99 20% lower, the GPU 0.14 ms a frame more. The render thread is busy 0.24 ms (35 W) and 0.79 ms (17 W) a frame less than RTV's command processor, 6 to 11% a draw.

**(b) The switches.**
- The ring wait's sleep (2 against 3) saves 216 ms of CPU a second at 35 W and 275 ms at 17 W: the busiest guest thread spins without it (998 and 975 ms a second against 735 and 649). With the sleep the race runs 2.0% (35 W) and 3.1% (17 W) faster, and the GPU takes 0.16 and 0.31 ms a frame less: the power the spin leaves goes to the GPU, as 8.1 expected.
- The timer (2 against 4) changes nothing beyond the noise: fps +0.6% and -1.0%, p99 -0.85 and -0.20 ms, CPU -19 and -28 ms a second without it.
- On battery there is no collapse or parking signature: the slowest windows (64 to 74 fps) are those of the highest draw load, with the GPU at 10 to 14 ms a frame and the render thread busy as usual; the process's CPU time stays at 2,500 to 4,400 ms a second. The CPU is slower on battery (5.0 against 3.45 µs a draw, +45%), the GPU less so (+24% a frame).

**(c) MSAA and the render scale.**
- One pass with 4x against 2x at 1x (6 against 2): the GPU takes 0.79 ms (35 W) and 0.85 ms (17 W) a frame more, +9 to 10%; fps -8.1% and -8.1%, p99 +0.9 and +1.2 ms. N1's estimate of about 10 ms at 4x and 1x matches: 8.33 ms at 35 W, 10.20 ms at 17 W.
- At the default render scale, 2x (7): 23.91 ms at 35 W and 27.15 ms at 17 W a frame, 14% and 29% above N1's estimate of about 21 ms. The race runs at 39.1 and 34.6 fps with p99 41 and 45 ms.

**(d) GPU bound in every mode.** At 1x the GPU is busy 85% (35 W) to 87% (17 W) of the frame interval, and 93 to 94% at 2x. The render thread waits for the frame latency 270 to 490 ms a second at 1x and about 675 at 2x, and is busy 49 to 57% of the wall time. Neither the replay (155 to 190 ms a second) nor the submit thread (274 to 333) is critical: the hand-over of 17.5 has room on the Ally X as on the main PC. The GPU time of the host's own presents is not in the trace's GPU time; the presenter repaints at each of the panel's vblanks besides the guest frames, which is the host presents item of N4.

**(e) Section 10 on the Ally X.**
- The device target of 8.1 at the panel's resolution (2x, 60 fps or more with p99 at most 16.7 ms) is not met: 39.1 and 34.6 fps.
- At 1x it holds at 35 W (112.5 fps, p99 14.34 ms). At 17 W the frame rate holds (93.3 fps) but p99 does not (19.52 ms, 1.5% of the frames over 16.7 ms).
- The CPU criterion (the render thread busy at most 2.5 ms a race frame) is not met as such: 4.36 ms at 35 W and 6.11 ms at 17 W at 1x. It does not limit the Ally X, which waits for its GPU; the busy time a frame there follows the CPU's clock.
- No new error key: the runtime logs (which keep every run of a folder, five and six launches) have the three keys of the main PC (`BaseHeap::Release`, the unimplemented XAM message, `update:\media.zip`), and their GPU warnings are the post passes' and the shader overrides' of the main PC, plus the 2x run's unscaled swap source, which the main PC's 2x runs log as well.

**The recommended Ally X default** (a proposal, the main session decides):
- render scale 1x, not the launcher's 2x for a 1080p panel: 2x is GPU bound at 35 to 39 fps;
- the native path: the best frame rate and p99 in both power modes;
- one pass with 2x MSAA (the default): 4x costs 8%, which the 120 Hz cap does not leave at 35 W either;
- `guest_ring_wait_sleep` on (the default): 2 to 3% more fps and a quarter of a core less;
- `high_resolution_timing` on (the default): no measurable effect either way.

So the data supports one change of default for the Ally X: render scale 1x instead of the 2x the launcher picks for a 1080p panel, by a device or GPU class rule or the Ally X profile. The panel's 1.5x and an upscaler stay N5's (8.1).

**The main session's decisions of 24 October 2026.**
- **N4's CPU half on the Ally X is met in substance.** The device is GPU bound in every mode, the render thread's busy time follows the CPU's clock and does not limit, and no thread is saturated. The 2.5 ms busy time a frame is a criterion of the main PC.
- **The device target at the panel's resolution moves to N5**, the native-resolution work the project owner approved on 24 October 2026: a fractional 1.5x render scale for 1080p, widescreen and up to 4K, on the native path ([d3d-hle-n5.md](d3d-hle-n5.md)).
- **N4 closes** once the host presents item (17.22) is finished.
- **The Ally X default for now: render scale 1x** through a launcher rule for integrated GPUs (a UMA adapter, or under about 1 GB of dedicated video memory) on a 1080p panel, until N5's 1.5x is measured on the device; the user can still choose another scale. The render path's default stays RTV everywhere; the native path stays an option in the GUI launcher, and flipping the global default is a separate decision after N5.

### 17.22 Host presents, 24 October 2026

Armoury Crate's monitor on the ROG Xbox Ally X showed more than 150 fps in the menus and more than 120 fps on every screen with the race at 120: the host presented more often than the guest swapped.

**The counter.** The perf trace counts the presenter's presents in each window (`host_presents`, [traces](../e2e/traces.md)):
- `new_frames`, of a guest output frame no earlier present showed, and `repeats`, of the same frame again;
- `ui_thread`, those the UI thread made (the others come from the thread that produced the frame, or the submit thread with `d3d12_submit_thread_async_swap`);
- `under_8_33_ms` and `under_4_17_ms`, presents sooner after the previous one than a refresh at 120 and 240 Hz.

`e2e.py` sums them per phase (`host_presents_per_s`, `host_presents_per_frame`), and the `menu_race` scenario measures 30 s on the main menu before the race of `race`.

**The cause.** The runtime creates the achievement toast at the launch, whether an achievement ever shows or not, so the ImGui drawer is always one of the presenter's UI drawers. That has two effects:
- every frame is presented by the UI thread (`kUIThreadOnRequest`) instead of the guest output thread, which the presenter does only while a UI drawer exists;
- the drawer asks for a repaint at every UI tick, so the UI thread presents the same guest frame again between the guest's frames. In the menus at 60 fps that gave 60 new and 60 repeated presents a second.

**The fix.**
- An ImGui dialog says whether it is idle (`ImGuiDialog::IsIdle`): it shows nothing that changes on its own. The toast is idle while nothing is queued, and the settings overlay (F2) is always idle, since its text follows the guest frames. While every dialog is idle, the drawer is painted with the guest frames only.
- A dialog says whether it is empty (`IsEmpty`), drawing nothing at all, as the toast with nothing queued. While every dialog is empty, the drawer leaves the presenter, which presents from the guest output thread again, as without a user interface; the toast brings the drawer back when an achievement arrives (`ImGuiDrawer::WakeFromAnyThread`).
- `ui_repaint_idle_dialogs=true` restores the earlier repaint for comparisons. Both changes are generic presenter behaviour, so `-Rov` has them too; nothing in them is specific to the title.

**The e2e runs had the overlay on.** `e2e.py` showed the settings overlay from the start (`FM4_INFO_SHOW=1`), so every e2e run presented from the UI thread. The overlay was there for whoever watches the window; the captures, OCR and `compare.py` never see it. Since 24 October 2026 `e2e.py` shows it only with `--info`, as the launcher does with `-Info`, and the gates measure the players' present path.

The earlier results stay valid as measurements of the guest side: its frame rate, p99, the GPU and CPU times. What reached the screen in those runs was limited by the UI thread's presents (below).

**The deferred present.** With the drawer gone, the frames come from the submit thread (`d3d12_submit_thread_async_swap`, on in the launcher): each frame's present runs after its submission. It skipped its frame when a newer one had been refreshed but not yet submitted, expecting that frame's own present to follow. While the host presents slower than the guest renders, that newer frame's present found a newer one again, so frames were dropped: at 2x the race presented 75 of 186 frames a second (`n4pt-kf`), fewer than the UI thread's 120 before. A deferred present now shows the newest frame whose work is submitted, at least its own, instead of dropping it, and never one older than the last it showed (`D3D12Presenter::OnGuestOutputRefreshed` keeps the four latest images with their submission; the present's command list goes to the queue before the later refreshes' work, which may already reuse the image's mailbox slot).

`present_skip_superseded=true` restores the skip. `e2e.py` sets it by default, so the guest throughput of its gates stays comparable with every earlier gate on this PC; `--arg present_skip_superseded=false` runs the players' path ([e2e](../e2e/README.md#host-presents)).

Correction (24 October 2026): with the skip and without the overlay, the frames that were presented came from the submit thread, whose present blocks on this PC (below), and RTV's resolve readbacks, which go through that thread, waited: `readback_publish` rose from about 1 to 25 to 55 ms/s in the race (`n4pt-fr`, `n4ts-fr`, `n4rb-fr` and `-fr2`; N1 saw 12.7 ms/s in perf and 56.3 in a smoke, and RTV 3.4% slower). Presenting from the UI thread, as the earlier gates did with the overlay, brings it back (`n4rb-frh` and `-frh2`: 4.4 and 0.2 ms/s; `-Native` reads back on demand and stays at 0), so `e2e.py` sets `host_present_from_non_ui_thread=false` together with the skip. The players' path is not affected: there the guest follows the presents, and the wait stays at 0 (`n4px-fru`). The GPU timestamps of 17.24 run only with the perf trace, off for players.

**The display's cap on the main PC.** Every path presents at most about 120 times a second at 240 Hz on this laptop: the UI thread, the guest output thread, the submit thread, RTV and `-Native`. With `d3d12_submit_thread_async_swap=false` the present blocks the command processor and the race runs at 120 fps (`n4pu-fna`, against 238 fps with it). Half of the intervals between presents are under 8.33 ms: they come in pairs at a 60 Hz rhythm. The desktop compositor reports its 240 Hz refresh (a period of 4.17 ms) and composes about once a second during the race, so the frames are not composed by it. The cap is taken as a property of this hybrid laptop (the NVIDIA GPU renders, the Intel GPU drives the panel), not of the runtime; PresentMon, which would show the present mode, waits for the user's approval. At `-FrameRate 120` the presents match the frames.

**Results** (`I:\FM4-build-usa`, `menu_race` on the fast route, 1x unless noted, one run a side; the menu values are medians of five runs a side without the overlay, round `n4pt`; the race and the limiter of rounds `n4pt` to `n4pw`):

| | Before | After |
| --- | --- | --- |
| Menu presents a guest frame | 2.0 (60 new, 60 repeated) | 1.0 |
| Menu process CPU | 537 ms/s | 404 ms/s |
| Menu package power | 31.5 W | 29.8 W |
| Menu GPU power (NVIDIA, `nvidia-smi`) | 22 to 27 W at 950 to 1,660 MHz | about 21 W at 665 to 810 MHz |
| Race at 240 Hz, `-Native`: guest fps, presents a second | 239 fps, 120 | 120 fps, 120 (p99 12.7 ms, the paired presents) |
| Race at 240 Hz, 2x: guest fps, presents a second | 193 fps, 120 (75 before the deferred fix) | 120 fps, 120 |
| Race at 120 Hz | 120 fps, 120, p99 8.9 ms | the same |
| A limiter at 30 (`present_min_interval_us=33333`) | the guest at 239 fps, 29 presents | 29.8 guest frames, 29.8 presents |

- What reaches the screen does not change on this PC (120 a second before and after); the guest no longer renders frames that are never shown, and it follows the display's pacing.
- The limiter stand-in holds the presenting thread after each present, as a driver or overlay limiter that blocks in the present call. Before, the UI thread's presents decoupled the guest from it, and a limiter's 30 presents a second also carried the UI's repeated frames (60 a second in the menus): the Ally X's "under 30" with Armoury Crate's limiter at 30.
- The 1:1 image check at 2x (`n4pt-kb` against `n4pt-kf`, the menu and a race frame): the same picture; the change is in the presents only, and the captures are taken on the guest side.
- The settings overlay keeps the UI thread's presents (`n4pw-fno`: 239 fps, 120 presents).
- On the final build (`n4px`, 1x at 240 Hz): e2e's default, with the skip, races at 239.1 fps with 120 presents a second (RTV 231.2 fps); the players' path at 120.0 fps with 120 presents and p99 12.0 ms (RTV 120.0 fps, 12.5 ms).

**For the ROG Xbox Ally X**, with one GPU and a VRR panel at 120 Hz: the menus present once a guest frame instead of twice, the presents no longer add the UI's ticks, and a frame limiter gives as many guest frames as presents. Its next run shows whether its presents are capped too.

**Open.** The cause of the main PC's cap (PresentMon, or the laptop's MUX or dGPU-only mode, both the user's). `present_letterbox` matters for the fullscreen fill of N5; it is left to N1.

### 17.23 VSync, 24 October 2026

The host presented at once with tearing allowed (sync interval 0 with `DXGI_PRESENT_ALLOW_TEARING`), relying on a variable refresh rate (VRR) display; the runtime's `vsync` cvar is guest timing. That tears on a fixed refresh display, and on a VRR display whenever presents come faster than the refresh: the ROG Xbox Ally X's traces at the 120 cap have 1 to 4.5% of the frames under 8.0 ms at 35 W, catch-up frames after a long one.

**The option.** The launchers' `-VSync` (`Build.ps1`, `pipeline.py --vsync`, the GUI launcher's Display page; [running](../running.md#-vsync)):
- `auto`, the default: on a display with a VRR range up to its refresh, tearing presents held at least a refresh apart (`present_min_interval_us`, which holds the presenting thread after a present, so a catch-up frame waits instead of tearing), and races at most 3 fps under the refresh (117 at 120 Hz, 237 at 240 Hz), inside the range. Without a range it is `on`.
- `on`: the display's refresh without tearing (`present_vsync`, sync interval 1).
- `off`: the earlier presents, which `e2e.py` keeps, so the gates stay comparable.

**The VRR range.** DXGI reports only whether tearing presents are supported (`DXGI_FEATURE_PRESENT_ALLOW_TEARING`), not whether the display has a VRR range. The launchers read it from the primary display's EDID in the registry (`pipeline.vrr_range`, `VrrRange` in `runner.cpp`): DisplayID's Adaptive-Sync data block, else EDID 1.4's range limits with the continuous frequency bit, which DisplayPort Adaptive-Sync panels use. The main PC's panel reports 60 to 240 Hz in DisplayID. The Ally X's panel is sold as FreeSync Premium (8.1); its range shows in the launcher's description on the next run ("VSync auto: VRR 48-120 Hz, ..." or "VSync on (no VRR range)"). Below the range's floor the presenter still presents each frame once; low frame rate compensation, which FreeSync Premium requires, is the driver's.

**The gate** (`n4vs-*`, `I:\FM4-build-usa`, `menu_race`, `-Native`, 1x, the players' path `present_skip_superseded=false`, one run a side; the race phase):

| Mode | Race at 240 fps: fps, p50, p95, p99 | Race at 120 fps: fps, p50, p95, p99 |
| --- | --- | --- |
| off | 120.0, 8.25, 12.78, 13.20 ms | 120.0, 8.32, 8.73, 8.89 ms |
| on | 120.0, 8.29, 11.53, 13.09 ms | 120.0, 8.33, 8.71, 8.89 ms |
| auto (237 fps, a present at least 4.17 ms apart) | 120.0, 8.35, 9.15, 9.77 ms | 120.0, 8.33, 8.71, 8.92 ms |

- Every mode presents 120 times a second on this PC, its display cap (17.22), so none of them changes the frame rate here, and no present came sooner than 4.17 ms after the previous one in any cell. Auto's hold evens out the paired presents at 240: p95 9.15 and p99 9.77 ms against 12.78 and 13.20 ms.
- At 120 fps the three modes do not differ, and the menus not at all (60 presents for 60 frames).
- Not measured here: the latency of `on` (it queues a frame for the refresh; PresentMon would measure it), and a 120 Hz panel setting, which is the user's. The Ally X's run shows auto at work below the cap: its race frames under 8.33 ms are counted by `host_presents.under_8_33_ms`.
- `launcher_tests` passes with the VSync modes on five test displays (the main PC's, a fixed 60 Hz screen, a 144 Hz VRR screen, a fixed 1280x800 screen and the Ally X's panel): 95 plans, no failure.

### 17.24 GPU time per pass, 24 October 2026

The ROG Xbox Ally X is GPU bound (17.21), and its trace split the GPU time only into resolves, texture loads and transfers: about 5.7 of its 7.9 ms a race frame were not attributed. The perf trace now times the passes the runtime adds and the presenter's paint, with the timestamp pairs the trace already resolves (only while it runs):

- `gpu.passes_ms`: the post passes by group of their names in `passes.toml`: `gtao` (`gtao_*`), `taa` (`taa_*`), `motion` (`motion_*`), `windshield` (`windshield_*`) and `other`;
- `gpu.swap_ms`: the swap's gamma and FXAA passes into the guest output;
- `gpu.scene_ms`: the rest of the frame's GPU time: the guest's draws and resolves with its own post-processing, which the look mods change in place, so they are part of it;
- `host_presents.paint_gpu_ms`: a presenter paint (the guest output into the back buffer with its effect, and the UI), from timestamps around the presenter's command list.

`e2e.py` summarizes them per phase (`gpu_passes_ms_median`, `host_paint_gpu_ms_median`).

**The gate** (`n4ts-*` against `n4tso-*` on the build before, `menu_race`, 240 Hz, e2e's defaults, one run a side; the race phase, ms of GPU time a frame):

| Run | fps, before | GPU frame, before | gtao | taa | motion | windshield | swap | scene | paint |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `-Native` 1x | 239.5, 238.4 | 2.08, 2.20 | 0.18 | 0.10 | 0.03 | 0.09 | 0.01 | 1.68 | 0.013 |
| RTV 1x | 218.8, 227.9 | 2.71, 2.68 | 0.18 | 0.10 | 0.03 | 0.09 | 0.01 | 2.32 | 0.013 |
| `-Native` 2x | 190.0, 191.2 | 5.05, 5.08 | 0.68 | 0.36 | 0.11 | 0.32 | 0.03 | 3.58 | 0.041 |

- The frame rate and the GPU time a frame do not move beyond the noise of single runs (RTV's 1x races vary from 210 to 234 fps, 17.22), so the timestamps stay on with the trace.
- The groups, the swap and the scene add up to the frame's GPU time (the scene is the rest); the passes the runtime adds take 0.40 ms of the 2.08 ms at 1x (19%) and 1.48 of the 5.05 ms at 2x (29%), GTAO the most.
- The presenter's paint is separate from the frame's GPU time: 0.013 ms at 1x and 0.041 ms at 2x on this PC.
- The Ally X's next run gives these per pass: whether GTAO, TAA or the scene is the lever there.

### 17.25 The second ROG Xbox Ally X run, 24 October 2026

The user ran four profiles on the handheld from the export of `96da8da`, on AC power at 35 W (its folder is named "cgarge 30w", a typo; the TDP was 35 W, as in 17.21) and on battery at 17 W: 1 RTV, 2 `-Native`, 3 `-Native` with one pass at 4x MSAA, 4 `-Native` with `-VSync off`. All ran at the launcher's defaults otherwise: VSync Auto and render scale 1x (the integrated GPU rule), the race at 120 fps. Battery has a second, short perf-2 file of an aborted run; the longer one is used. The race is taken as in 17.21 (about 160 s).

**VSync Auto found a VRR range.** In profiles 1 to 3 the runtime reports a 117 Hz refresh to the title, the cap of VSync Auto on a 120 Hz panel with a range up to 120 Hz; profile 4 (off) reports 120 Hz. The range itself is not in the logs; the launcher shows it on its Display page.

| Run | Power | fps | p50, p99 ms | frames > 8.33 ms | > 16.7 ms | < 8.0 ms | GPU ms a frame | presents a second (under 8.33 ms) | `readback_publish` ms/s | process CPU ms/s |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 RTV | 35 W | 113.9 | 8.61, 13.27 | 82.4% | 0.48% | 1.26% | 7.80 | 113.9 (0) | 56.5 | 3,232 |
| 2 Native | 35 W | 114.4 | 8.60, 12.85 | 79.5% | 0.54% | 1.55% | 7.46 | 114.4 (0) | 0 | 3,087 |
| 3 Native, 4x | 35 W | 113.5 | 8.60, 13.08 | 86.4% | 0.51% | 0.79% | 8.14 | 113.5 (0) | 0 | 3,412 |
| 4 Native, VSync off | 35 W | 118.2 | 8.33, 11.38 | 48.4% | 0.42% | 6.49% | 7.28 | 118.2 (58.7) | 0 | 3,079 |
| 1 RTV | 17 W | 100.1 | 9.07, 19.81 | 93.6% | 1.59% | 0.86% | 9.50 | 100.1 (0) | 15.8 | 3,722 |
| 2 Native | 17 W | 110.5 | 8.69, 15.30 | 83.8% | 0.80% | 2.06% | 8.28 | 110.5 (0) | 0 | 3,735 |
| 3 Native, 4x | 17 W | 90.3 | 10.69, 20.49 | 99.1% | 1.76% | 0.30% | 10.62 | 90.3 (0) | 0 | 3,742 |
| 4 Native, VSync off | 17 W | 101.3 | 9.14, 17.46 | 92.7% | 1.20% | 1.51% | 9.41 | 101.3 (29.1) | 0 | 3,708 |

With VSync Auto the race runs at most at 117 fps, a frame at least 8.55 ms, so the share of frames over 8.33 ms is high by design; p99 and the frames over 16.7 ms show the pacing. One run a side. Profile 3 ran without the ring wait's sleep (no ring waits, and a guest thread at 979 to 991 ms a second, as with `guest_ring_wait_sleep=false` in 17.21), most likely an extra cvar left in the profile, so its CPU time is not comparable.

**The GPU per pass on the 890M** (17.24, ms a race frame, `-Native` with VSync Auto):

| Power | GPU frame | scene | GTAO | TAA | motion vectors | windshield | swap | paint |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 35 W | 7.46 | 5.18 | 0.71 | 0.51 | 0.35 | 0.62 | 0.09 | 0.14 |
| 17 W | 8.28 | 5.76 | 0.92 | 0.54 | 0.35 | 0.65 | 0.09 | 0.14 |

- The passes the runtime adds take 2.19 ms of 7.46 at 35 W (29%) and 2.46 of 8.28 at 17 W (30%), against 19% on the main PC at 1x: GTAO the most, then the windshield (0.62 ms, almost as much), TAA and the motion vectors. The scene (the title's draws, resolves and own post-processing with the look mods) is the rest, 5.2 to 5.8 ms.
- One pass at 4x adds 0.77 ms of scene at 35 W (5.95) and 2.05 ms at 17 W (7.81), with GTAO at 1.17 ms there.
- The presenter's paint takes 0.14 ms.

**Host presents.** The Ally X presents every guest frame once in every profile (presents equal the frame rate, no repeats, none from the UI thread): its display path does not cap the presents as the main PC's does (17.22), and Armoury Crate's counter now shows the guest's rate. VSync Auto keeps every present at least a refresh apart (no interval under 8.33 ms); with VSync off 58.7 a second at 35 W and 29.1 at 17 W come sooner, which a 120 Hz VRR panel shows torn.

**VSync Auto against off** (profile 2 against 4): at 35 W off runs 3.2% faster (118.2 against 114.4 fps, Auto's cap is 117) with p99 11.38 against 12.85 ms and 0.42 against 0.54% of the frames over 16.7 ms, but tears; at 17 W Auto is faster and smoother (110.5 against 101.3 fps, p99 15.30 against 17.46 ms, 0.80 against 1.20% over 16.7 ms), though its run had a 5% lighter draw load (1,238 against 1,299 draws a frame).

**RTV's resolve readbacks wait under VSync Auto:** `readback_publish` 56.5 ms a second at 35 W and 15.8 at 17 W, against 0 for `-Native`, which reads back on demand. Auto's hold ran on the submit thread, which RTV's readbacks go through (the present thread below).

**Against the first run** (17.21, `de4e9bd`, 35 W and 17 W, VSync as off, the race at 120):
- `-Native` with VSync off at 35 W (like for like but for the 1x rule, which 17.21's runs set by hand): 118.2 against 112.5 fps, p99 11.38 against 14.34 ms, 0.42 against 0.73% of the frames over 16.7 ms, GPU 7.28 against 7.54 ms a frame, process CPU 3,079 against 2,933 ms a second at 5% more frames.
- At 17 W: 101.3 against 93.3 fps with VSync off and 110.5 with Auto, p99 17.46 and 15.30 against 19.52 ms, 1.20 and 0.80 against 1.52% over 16.7 ms.
- RTV: 113.9 against 106.5 fps at 35 W (p99 13.27 against 18.22 ms), 100.1 against 91.6 at 17 W (19.81 against 24.45 ms).
- The UI's repaints (17.22) no longer present the same frame between the guest's frames, which on the 120 Hz panel added up to 120 paints a second of GPU time and power; with the other changes between the two exports (N1's private surfaces among them) the device gains 5 to 9% in fps and up to a fifth in p99.
- No new error key: the three keys of the main PC, and the post passes' warnings.

**The present thread.** With `d3d12_submit_thread_async_swap` (on in the launcher), a frame's present ran on the submit thread after its submission, and VSync Auto's hold (`present_min_interval_us`), VSync On or an external limiter then held the submissions behind it, RTV's readbacks among them. A frame's present now runs on a thread of its own (`GPU Present`): the submit thread hands it over after the frame's submission and goes on, the present thread presents at once, and the swap of the next frame waits only until the present before it is done, so one frame is in flight and the guest still follows the presents (a limiter at 30 gives 30 guest frames). The deferred present shows its own frame once its work is submitted (the refresh number it was deferred for), not the newest one, so every frame is presented once.

The gate on the main PC (`n4pth*`, the players' path `present_skip_superseded=false`, 240 Hz, 1x, one run a side; the race phase):

| Run | fps | p99 ms | `readback_publish` ms/s | presents a frame |
| --- | --- | --- | --- | --- |
| RTV, VSync off, on, auto | 119.9, 120.0, 119.8 | 11.34, 12.06, 11.39 | 0.1, 0.1, 0.0 | 1.0 |
| `-Native`, VSync off, on, auto | 120.0, 120.0, 120.0 | 10.45, 10.49, 10.38 | 0 | 1.0 |
| Limiter at 30, `-Native` and RTV | 29.7, 29.9 | 37.4, 40.7 | 0 | 1.0 |

- The main PC presents at most about 120 times a second (17.22), so it did not show the readback wait in the players' path before the change either (`n4vr-*`: 0.0 to 0.4 ms/s with VSync off, on and auto): the Ally X, whose presents follow the guest, is where the hold filled most of each frame. Its next run confirms the fix there.
- The menus present once a frame (60 for 60, none repeated); RTV's menus ran at 59.6 to 59.8 fps against 60.0.
- The 1:1 image check at 2x (`n4pt-kf` against `n4pthl-kfu`, the menu): the same picture.
- e2e's benchmark mode presents from the UI thread (17.22) and is not changed.

**The recommended Ally X defaults** (the main session decides):
- the native path: the best frame rate and p99 on battery (110.5 against 100.1 fps, p99 15.30 against 19.81 ms), and no readback wait;
- VSync Auto (the default): no present within a refresh of the one before, and on battery the better pacing of the two; on AC it costs 3% against off;
- render scale 1x (the integrated GPU rule) and one pass with 2x MSAA: 4x adds 0.8 to 2.1 ms of GPU time a frame;
- `guest_ring_wait_sleep` and `high_resolution_timing` on, as before.
- The levers for the GPU, if more frame rate on battery is wanted: GTAO (0.7 to 0.9 ms) and the windshield (0.6 ms) together take about 1.3 to 1.6 ms of the 7.5 to 8.3 ms a frame.

**The main session's decisions of 24 October 2026.**
- The native path becomes the default render path with integrated GPUs only, by the rule of the 1x render scale (`-Rtv` and the GUI launcher's render path keep RTV available; `-Rov` and `-Msaa` keep RTV as before). RTV stays the default elsewhere until after N5.
- VSync Auto, render scale 1x, one pass with 2x MSAA, the ring wait's sleep and the timer stay as they are.
- GTAO (0.7 to 0.9 ms) and the windshield (0.6 ms, almost as much as GTAO) are candidates for an "Ally X performance" look preset, later.
