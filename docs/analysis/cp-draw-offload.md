# Draw work off the command processor thread: feasibility

Status: superseded by the native port. Analysis of 20 October 2026; nothing here was implemented as written. The next day the project chose the native port ([D3D-level HLE](d3d-hle.md)), whose stage N4 moved the draw back end onto the replay side of the host render thread ([D3D-level HLE, 17](d3d-hle-n4.md)). The measurements below describe the RTV command processor of that day.

## Question

At 240 Hz and 1x the race is limited by the single command processor (CP) thread. The submit thread already replays and executes the command lists, and the asynchronous swap (`d3d12_submit_thread_async_swap`, in progress) removes the swap's wait for it. This note asks whether the per-draw work itself can move to other threads, and what it would gain.

## Where the CP time goes

Race phase, `I:\FM4-build-usa`, launcher defaults, 240 Hz, 1x (gates `sflc` and `sflp`, 20 October 2026, see [current status](../status/history-results.md#submit-thread-at-the-swap-20-october-2026)): 155.8-165.2 fps, 2,400-3,050 draws per frame, CP busy 969-976 ms/s. Zones in ms/s (inclusive; the perf trace's own zones cost about 7% of CP samples, so the values are a little high):

| Part | ms/s | Share of CP |
|---|---|---|
| `draw` (all draws) | 584-588 | 60% |
| of which `draw_textures` | 128 | 13% |
| of which `draw_bindings` (system constants and `UpdateBindings`) | 127 | 13% |
| of which `draw_render_targets` | 65 | 7% |
| of which `draw_primitives` | 51 | 5% |
| of which `draw_pipeline` | 37 | 4% |
| of which `draw_vertex_buffers` | 24 | 2% |
| of which the rest (shader analysis, uniform draws, viewport and scissor, barriers, recording into the deferred list) | 155 | 16% |
| `swap` (mostly the submit thread flush, 96) | 122-124 | 13% |
| `copy` (resolves) | 30-31 | 3% |
| Outside the zones: PM4 parsing, register writes, other packets | about 236 | 24% |

The draw zone is about 1.2 us per draw. No single step dominates. The last profile with symbols (`REX_CP_PROFILE`, 240 Hz, [history](../status/history-performance.md)) is equally flat: register copy 9%, texture path 8%, `UpdateBindings` 6%, swap flush 5%.

Per frame the race also has 40 copies, 31.5 resolve readbacks (14.9 MB), 6 submissions, 6 occlusion snapshots of which 1 is awaited by the guest, 1 `WAIT_REG_MEM` (the swap waiting for the vblank flag) and 2 short waits for guest commands (`guest_idle` 22 ms/s).

## What each draw step depends on

| Step | Reads | Shared state it changes | Order-dependent |
|---|---|---|---|
| Shader analysis | shader microcode (cached) | per-shader analysis | no (cached after the first use) |
| `PrimitiveProcessor::Process` | registers, guest index memory | converted index cache, shared memory requests | yes: the upload must precede the draw, with the guest memory of that moment |
| `RenderTargetCache::Update` | registers | EDRAM ownership; records transfers into the command list | yes, strictly |
| `ConfigurePipeline` | registers, shader translations, the formats bound by the step above | pipeline map (creation is already asynchronous) | no, given its inputs |
| `RequestTextures` | fetch constants | texture cache: keys, loads and untiling dispatches recorded into the list, invalidation by guest writes and by resolves of the same frame | yes |
| System constants, `UpdateBindings` | the live register file (float, bool, loop and fetch constants) | constant upload pool of the submission, bindless index tables, sampler heap | partly: mostly a copy of the registers the shaders use |
| Vertex buffers | fetch constants | shared memory requests | yes |
| Recording the draw | all of the above | the deferred command list | yes |

## Option 1: hand parts of each draw to worker threads

Not worth it.

- Every step reads the live register file, which the PM4 parser overwrites for the next draw. A worker needs a copy of the registers it reads, per draw. For the bindings that copy is the work itself: `UpdateBindings` mostly copies the used constants into the upload pool.
- The order-dependent caches (render targets, textures, shared memory, primitives) must stay on one thread, in stream order.
- What is left without order or copy dependencies is the pipeline lookup, the viewport and scissor, and part of the system constants: about 40-80 ms/s (4-8% of CP). At 1.2 us per draw, one cross-thread hand-off per draw (about 0.1-0.5 us with its cache misses and atomics) eats a large part of that.

Expected gain below 5%, with per-draw synchronization added to every draw.

## Option 2: split at the PM4 boundary (front end and back end)

The only split with a real upper bound.

- **Front end (thread A)**: reads the ring and indirect buffers, parses PM4, applies register writes to a front register file, handles `WAIT_REG_MEM`. It sends a queue to the back end: the register ranges changed since the last command, then the draw, copy, swap and event commands.
- **Back end (thread B)**: today's draw, copy and swap processing with its own back register file and all caches, and every write the guest can observe: `EVENT_WRITE_SHD`, `MEM_WRITE`, `COND_WRITE`, `REG_TO_MEM`, interrupts, occlusion results, readbacks and memexport.
- **Guest-visible order**: because the guest-visible writes stay in back-end order, the guest never sees a fence before the draws in front of it have consumed their memory (vertex, index and texture data the guest rewrites after that fence). The front end must drain the back end before:
  - a `WAIT_REG_MEM` or `COND_WRITE` on an address the back end still has to write (the vblank flag that the swap waits for is written by the guest's interrupt, not by the back end);
  - an occlusion report the guest awaits (1 per frame);
  - a memexport readback, and register reads of back-end state (`REG_TO_MEM`, `REG_RMW`).

Upper bound: the front end is about 236 ms/s (24% of CP) and the back end about 640-690 ms/s after the asynchronous swap. With ideal overlap the frame costs max(A, B): +25-35% fps over one thread with the asynchronous swap. Realistically +10-20%:
- the register ranges are copied twice: the front file is written today (9% of samples), and the back end has to apply the same deltas;
- every drain loses the overlap of the frame tail.

Effort and risk are large:

- The `CommandProcessor` base class (shared with the upstream ReXGlue and Xenia code) splits its packet handlers into front and back parts. The side effects of `WriteRegister` split too: scratch registers, `COHER`, gamma ramp, and fetch and constant dirty tracking.
- Every subsystem that reads the register file (texture cache, render target cache, primitive processor, pipeline cache, post passes, draw list and frame graph traces) must read the back file.
- `Save` and `Restore`, the trace tools and the debug dumps assume one thread.
- Failure modes are timing-dependent and hard to reproduce: geometry corrupted by a guest that rewrites a dynamic buffer early, stale resolves or occlusion results, deadlocks between drains.
- The gain is capped by the guest threads. Their headroom is not measured: the CP waits for guest commands only 22 ms/s today, so a faster CP may find the ring empty more often.

This is weeks of work, not days.

## Option 3: less work per draw on the one thread

Cheaper and measurable with the existing zones: skip work that repeats between consecutive draws, as the vertex buffer path already does with `vertex_buffers_in_sync_`.

- `RenderTargetCache::Update`: return early when the render target and EDRAM registers have not changed and no copy, resolve or uniform draw happened since the last draw.
- `RequestTextures`: reuse the last bindings when the used fetch constants and the texture cache's invalidation epoch have not changed.
- `ConfigurePipeline`: reuse the last pipeline handle when the shaders, the modifications, the bound formats and the pipeline-relevant registers have not changed.
- Constants: copy only the ranges written since the last upload, where the shaders read the same map.

Each item saves only what repeats from one draw to the next. The repeat rates are not measured yet.

## Measurements before a decision

1. A zone for PM4 parsing and register writes, so the front-end share is measured, not derived.
2. Per-draw repeat rates: the same render target state, textures, pipeline and constant ranges as the previous draw.
3. Drain points per frame for option 2: `WAIT_REG_MEM` and `COND_WRITE` on addresses with pending back-end writes, awaited occlusion reports, `REG_TO_MEM`, `REG_RMW` and memexport readbacks.
4. Guest headroom: CPU time of the guest's render thread per frame, and how full the ring is when the CP reads it.
5. A `REX_CP_PROFILE` sample at 240 Hz without `REX_PERF_TRACE_FILE`, so the zone overhead is not in the profile.

## Recommendation

1. Finish and gate the asynchronous swap (0.35-0.6 ms per frame, the cheapest gain).
2. Take the measurements above (one or two days).
3. Do option 3 where the repeat rates are high, one zone at a time, each gated against a baseline of the same binary.
4. Start option 2 only if the front-end share stays at 20% or more after option 3, the drains are a few per frame, and the guest threads have headroom.
