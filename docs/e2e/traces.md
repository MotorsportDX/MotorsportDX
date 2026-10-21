# Debug trace environment variables

These variables switch on diagnostic output in the runtime and in the FM4 integration code. They are off by default. Pass them with `--env NAME=value` on [`e2e.py`](README.md) (or `matrix.py`, which passes them to every cell). The files they write are local and must go outside the repository, for example under `<BuildRoot>\e2e\<name>`.

`e2e.py` strips the `FM4_*` and `REX_*` variables that it inherits from the shell, so only `--env` values and the launcher's own variables reach the game. Experiment switches that change rendering or timing (A/B overrides, workarounds) are in [experiments.md](experiments.md).

Summary of the variables on this page:

| Variable | Writes | Default cadence | Section |
| --- | --- | --- | --- |
| `REX_PERF_TRACE_FILE` | one JSON line per second (and `.resolves.json`) | every second | [Performance trace](#performance-trace-rex_perf_trace_file) |
| `REX_CP_PROFILE` | stack samples of the command-processor thread | about every millisecond | [Command-processor profiler](#command-processor-profiler-rex_cp_profile) |
| `REX_WAIT_REG_MEM_TRACE` | summary of WAIT_REG_MEM conditions | per run | [WAIT_REG_MEM](#wait_reg_mem-rex_wait_reg_mem_trace) |
| `REX_OCCLUSION_SEGMENT_TRACE`, `REX_OCCLUSION_DRAW_TRACE`, `REX_DRAW_DEPTH_HIST` | occlusion and depth state | per snapshot or frame | [Occlusion and depth](#occlusion-and-depth) |
| `REX_DRAW_LIST_TRACE` | every draw of a sampled frame | `REX_DRAW_LIST_EVERY`, default 10 | [Draw traces](#draw-traces) |
| `REX_IB_TRACE` | indirect buffers | every 60th frame | [Draw traces](#draw-traces) |
| `REX_DRAW_STATS` | pipeline statistics per draw | `REX_DRAW_STATS_EVERY`, default 60 | [Draw traces](#draw-traces) |
| `REX_DRAW_DUMP` | vertex fetch data of matching draws | `REX_DRAW_DUMP_EVERY`, default 30 | [Draw traces](#draw-traces) |
| `REX_BIN_TRACE` | draw packets per bin select | `REX_BIN_TRACE_EVERY`, default 10 | [Draw traces](#draw-traces) |
| `REX_GPU_INVENTORY` | state per shader pair (JSON) | every 600 frames | [Inventory](#inventory-rex_gpu_inventory) |
| `FM4_D3D_TRACE` | calls of PM4 packet writers | every 5 seconds | [PM4 writers](#pm4-writers-fm4_d3d_trace) |
| `FM4_N0_TRACE`, `FM4_N0_KEYCHECK` | the title's D3D layer: replay paths, recordings, threads, ring allocations, emission hashes | every second, summary every 5 seconds | [D3D layer counters](#d3d-layer-counters-fm4_n0_trace) |
| `REX_PM4_STATS` | packets per opcode, emission checks of IM_LOAD and LOAD_ALU_CONSTANT | every perf window | [PM4 statistics](#pm4-statistics-rex_pm4_stats) |
| `REX_CP_SPLIT` | command-processor time per packet kind, in the perf trace | every perf window | [Performance trace](#performance-trace-rex_perf_trace_file) |
| `REX_FRAME_GRAPH` | passes, resolves and GPU times (JSON lines) | `REX_FRAME_GRAPH_EVERY`, default 600 | [Frame graph](#frame-graph-rex_frame_graph) |
| `REX_TRANSFER_DRAW_TRACE` | draws that needed ownership transfers | `REX_TRANSFER_DRAW_EVERY`, default 60 | [Transfer draws](#transfer-draws-rex_transfer_draw_trace) |
| `REX_LATE_WRITE_TRACE`, `REX_STALE_PAGE_TRACE`, `REX_READBACK_SKIP_TRACE` | data race and stale data findings | every frame or every 4th frame | [Data race and stale data](#data-race-and-stale-data) |
| `REX_READBACK_DUMP` | bytes of one resolve readback | `REX_READBACK_DUMP_EVERY`, default 120 | [Readback dumps](#readback-dumps-rex_readback_dump) |
| `REX_RESOLVE_PUBLICATION_TRACE` | one JSON line per resolve publication, after `<file>.arm` exists | every publication | [Resolve publication](#resolve-publication-rex_resolve_publication_trace) |
| `REX_FILE_READ_TRACE`, `REX_IO_TRACE`, `FM4_STREAM_TRACE` | guest file and I/O activity | every call | [File and I/O](#file-and-io) |

## Performance trace (REX_PERF_TRACE_FILE)

[`work/runtime/src/graphics/util/perf_trace.h`](../../work/runtime/src/graphics/util/perf_trace.h) writes one JSON line per second to the file named by `REX_PERF_TRACE_FILE`. Without the variable it does nothing. `e2e.py` sets it to `<out>\perf.jsonl` for every run, so the harness always has this trace.

Each line (one window) has:

- `t`, `window_ms`: the time of the window and its length.
- `fps`: guest swaps per second in the window. `frame_max_ms`, and `frame_ms` (the time of every frame in the window).
- `draws`, `copies` (resolves), `readbacks`, `readback_kb`, `upload_kb`.
- `invalidations`, `invalidated_kb`: shared memory invalidations by guest CPU writes (any thread; with the pages around the written one).
- `vblanks`: guest vblanks the vsync thread delivered. `grid` is on the refresh grid, `late` is a grid vblank that passed while the command processor worked and was delivered when it waited (`vblank_vrr`), and `forced` is a vblank delivered after two intervals without a wait.
- `wait_ms`: time per cause for the GPU command-processor thread, and `waits`: the number of waits per cause. The causes are:
  - `guest_idle`: waiting for guest commands, i.e. CPU-bound.
  - `frame_latency`: GPU-bound.
  - `resolve_readback`.
  - `occlusion`.
  - `memexport`.
  - `wait_reg_mem`: PM4_WAIT_REG_MEM sleeping until its condition holds. In FM4 this is the swap waiting for the flag that the guest vblank interrupt clears.
  - `readback_publish`: waiting for the resolve readback copies of `d3d12_readback_publish_thread`, either before a readback slot is reused or, with `d3d12_readback_publish_await_signals`, at fence writes, interrupts and the swap.
  - `other`.
- `counts`: events per window:
  - `occlusion_awaited` and `occlusion_unawaited`: ROV occlusion snapshots whose report the guest spins on, or not.
  - `viz_queries` and `viz_draws` (draws conditional on a viz query).
  - `bin_selects` (SET_BIN_SELECT and _LO; tiling).
  - `predicated_run` and `predicated_skipped` (bin-predicated packets).
  - `upload_ranges` (shared memory uploads).
  - `pipeline_draws_waited`, `pipeline_draws_dropped`, and `submissions`.
- `cpu_ms`: command-processor time per zone, inclusive (`draw` contains the `draw_*` zones): `draw`, `draw_primitives`, `draw_render_targets`, `draw_pipeline`, `draw_textures`, `draw_bindings`, `draw_vertex_buffers`, `copy`, `submit`, `swap`, `shared_memory_lock`, `shared_memory_upload` (split into `shared_memory_copy` and `shared_memory_protect`), `submit_record` and `submit_execute`.
- `cp_split_ms` and `cp_split_packets`, only with `REX_CP_SPLIT=1` (which does nothing without `REX_PERF_TRACE_FILE`; `e2e.py` always sets `REX_PERF_TRACE_FILE`): command-processor time and packets per packet kind, exclusive of each other and measured with the time stamp counter: `registers` (type-0 and type-1 packets, `SET_CONSTANT`, `SET_CONSTANT2`, `LOAD_ALU_CONSTANT`, `SET_SHADER_CONSTANTS`, bin masks and selects), `shaders` (`IM_LOAD`, `IM_LOAD_IMMEDIATE`), `draw` (`DRAW_INDX`, `DRAW_INDX_2`, including the `draw` zone, which contains the `copy` zone, and the waits inside them: `frame_latency`, `resolve_readback`, `readback_publish`) and `other`. Indirect buffers, `WAIT_REG_MEM`, `EVENT_WRITE_ZPD` and the swap are not timed. What `cp_busy` has beyond these and the `swap` zone is packet dispatch and the untimed packets. The timing costs a little per packet (about 3 to 5% of the busy time per draw on 21 October 2026), so compare `cp_busy` with a run without the switch.
- `submit_thread`: the `d3d12_submit_thread` replay thread and its drain at the swap:
  - `jobs`: submissions the thread finished; `busy_ms`: its wall time in total, split into `record_ms` (replaying the deferred command lists), `execute_ms` (`Close`, `ExecuteCommandLists` and `Signal`) and `callback_ms`.
  - `callbacks`, `callback_ms`: with `d3d12_submit_thread_async_swap`, the work the thread does after the swap submission: the presenter's guest output signal and the presentation of the frame (painting and `Present` when the guest output thread paints). Zero otherwise; missing in older traces.
  - `flushes`, `flushes_blocked`, `flush_jobs`, `flush_wait_ms`: the command processor waiting for the thread to drain before a synchronous submission (the swap): calls, calls that found work queued or running, the jobs they found, and the wall time. This wait lies inside the `submit` and `swap` zones and is counted as busy, not in `wait_ms`, so `cp_busy` stays comparable with older traces.
- `gpu`: GPU timestamps of the frames in the window (`frames`, busy time, `resolve_ms`, `span_ms`).
- `occlusion_samples`: mean passing samples per occlusion interval, `unawaited` and `awaited`.
- With `host_render_thread` (`-Native`, [running](../running.md#-native)) only: `render_thread_busy_ms` (the host render thread's wall time not spent waiting, the same measure as `cp_busy`), `queue_depth_mean` and `queue_depth_max` (the records queued when each record was taken), and `host_render` with `records` (records taken) and `merged_spans` (spans of the ring merged into the last record because the queue was full). The waits of `wait_ms` are then the host render thread's; `guest_idle` is its wait on the empty queue.

Compare stall times as well as FPS: FPS varies with what happens in a race. Group seconds by draws per frame to compare runs at the same load (a car facing a wall draws a third as much). `summary.json` has the medians of these per run (see [README](README.md#reading-summaryjson)).

### Resolve inventory

Next to the trace, the runtime rewrites `<REX_PERF_TRACE_FILE>.resolves.json` every 10 windows. It lists every resolve destination seen in the run (address, length, format, pitch and height), with the number of resolves, and the first and last time (seconds) each was seen. It is used to find which resolves the CPU actually needs. (The harness writes this file as `perf.jsonl.resolves.json` in the run directory.)

## Command-processor profiler (REX_CP_PROFILE)

`REX_CP_PROFILE=<file>` samples the stacks of the GPU command-processor thread about every millisecond ([`cp_sampler.h`](../../work/runtime/src/graphics/util/cp_sampler.h)). Sampling starts after `REX_CP_PROFILE_DELAY` seconds (default 0) and lasts `REX_CP_PROFILE_SECONDS` (default 30). The stacks are written as lines of `<count> <module>+0x<offset> ...`, innermost frame first, for offline symbolization.

Build the runtime with line tables first. `Build.ps1 -Action profbuild` builds `<BuildRoot>\game-build-prof` from a mirror of `runtime-source` (`runtime-source-prof`); the default build is not touched. Then run with `--game-build game-build-prof` and summarize:

```
python outputs\FM4-build\e2e\e2e.py --build-root <BuildRoot> --game-build game-build-prof ... --env REX_CP_PROFILE=<file>
python outputs\FM4-build\e2e\cp_profile.py stacks.txt <BuildRoot>\game-build-prof
python outputs\FM4-build\e2e\cp_profile.py stacks.txt <BuildRoot>\game-build-prof --function IssueDraw
```

`cp_profile.py` reports per runtime function, or with `--function` per source line of the matching functions. For a Quick Race, `REX_CP_PROFILE_DELAY=156` starts sampling in the race. At 240 Hz (`--arg=--video_mode_refresh_rate=240 --env FM4_FRAME_RATE=240`) the command processor, not the vblank, sets the frame rate.

## WAIT_REG_MEM (REX_WAIT_REG_MEM_TRACE)

`REX_WAIT_REG_MEM_TRACE=<file>` summarizes the WAIT_REG_MEM conditions of the command processor ([`work/runtime/src/graphics/command_processor.cpp`](../../work/runtime/src/graphics/command_processor.cpp)): how often each condition was waited on, its polls, how many sleeps a vblank ended early, and its wait time.

## Occlusion and depth

- `REX_OCCLUSION_SEGMENT_TRACE=<file>`: the occlusion snapshot segments.
- `REX_OCCLUSION_DRAW_TRACE=<file>`: the draw after each occlusion snapshot.
- `REX_DRAW_DEPTH_HIST=<file>`: the per-frame depth state.

These are read in [`work/runtime/src/graphics/d3d12/command_processor.cpp`](../../work/runtime/src/graphics/d3d12/command_processor.cpp). The related cvar `rtv_occlusion_debug=noqueries|fake`, which changes the RTV occlusion reports for experiments, is in [experiments.md](experiments.md).

## Draw traces

All of these write text files. The sampling variables select frames; the defaults are listed.

- `REX_DRAW_LIST_TRACE=<file>` (`REX_DRAW_LIST_EVERY`, default 10): lists every draw of every Nth frame, with shaders, buffer addresses, render target state and the backend outcome. A race frame has about 3,000 lines; use 20 to keep the timing close to an untraced run. Each frame starts with `F <frame> <steady clock ms>`, and each draw has the vertex and pixel shader, index count and base, the color and depth info, the window offset, the EDRAM mode, `RB_DEPTHCONTROL`, `RB_STENCILREFMASK` (the stencil reference is its low byte), the viewport Z scale and offset (`PA_CL_VPORT_ZSCALE`, `PA_CL_VPORT_ZOFFSET`, as float dwords), `RB_BLENDCONTROL0`, `RB_COLOR_MASK`, the jitter of the post passes (x and y in host pixels, as float dwords; 0 unless the draw was jittered, see [post pass framework](../analysis/post-pass-framework.md#jitter)) and the outcome. Captures write their steady-clock time to `capture\captures.txt` to match frames.
  - `REX_DRAW_LIST_CONSTANTS=<shader hash>:<first>[-<last>];...` (the 64-bit ucode hash of a vertex or pixel shader, decimal constant numbers of that shader) adds a line `C <first> <dwords>` after each listed draw of that shader, in hexadecimal like the draw lines, with the float constants as raw dwords; the first constant is counted over all 512, so a pixel shader's c0 is `100` (256). Example: `8D05A46C3584CA9E:0-11;4746CAD32EF168CF:0-69` gives the car body's `matWVP`, `matWV` and `matW`, and the motion blur's 17 matrices with its viewport and Q constants.
  - `REX_DRAW_LIST_AFTER_PS=<pixel shader hash>` also lists every frame that follows a frame in which that pixel shader drew, so matrices that need the previous frame come in pairs of consecutive frames (with `4746CAD32EF168CF`, every frame of the game's motion blur but the first).
- `REX_IB_TRACE=<file>`: for every 60th frame, each indirect buffer with its nesting depth, guest address, length, content hash and the draws inside it.
- `REX_DRAW_STATS=<file>` (`REX_DRAW_STATS_EVERY`, default 60): for each draw of a sampled frame, the shader pair, buffers, render targets and window offset, with the D3D12 pipeline statistics: input assembler primitives, clipper invocations and primitives, and pixel shader invocations.
- `REX_DRAW_DUMP=<file>` with `REX_DRAW_DUMP_VS=<vertex shader hash, low 32 bits>` (`REX_DRAW_DUMP_EVERY`, default 30): for matching draws, the vertex fetch constants, index and vertex data hashes, all vertex shader float constants, and the positions of the first 24 vertices computed on the CPU. Use it to compare the same draw between runs.
- `REX_BIN_TRACE=<file>` (`REX_BIN_TRACE_EVERY`, default 10): every draw packet the command processor reaches, per shader pair, bin select and mask, and whether predication skipped it. It tells "the game did not send it" from "skipped".

## Inventory (REX_GPU_INVENTORY)

`REX_GPU_INVENTORY=<file.json>` aggregates the guest GPU state per shader pair: render target and depth formats, MSAA, blend, depth state, texture formats, primitive types and memexport. The file is rewritten every 600 frames. Summarize one file per scenario and the combination with:

```
python outputs\FM4-build\e2e\inventory.py --build-root <BuildRoot> a.json b.json
```

The inventory is used to plan a native renderer.

## PM4 writers (FM4_D3D_TRACE)

`FM4_D3D_TRACE=<file>` counts calls, and the top callers, of the guest functions that build PM4 packets. The file is rewritten every 5 seconds. The function list in [`work/fm4-rex/src/fm4-d3d-trace.cpp`](../../work/fm4-rex/src/fm4-d3d-trace.cpp) comes from a static scan of the generated code (addresses and packet names only):

```
python outputs\FM4-build\e2e\pm4_writers.py <BuildRoot>\project\generated\default <BuildRoot>\runtime-source\include\rex\graphics\xenos.h out.json
```

The scan misses headers whose packet count is computed at run time (SET_CONSTANT). It finds predicated headers (bit 0 set) and reports them with a `(pred)` suffix. The scene draws are predicated `DRAW_INDX` packets (`0xC0032201`, `0xC0012201`), so their writers (`sub_82311080`, `sub_8233AC60`, `sub_8234D278`, `sub_8234DAE8`, see [Ghidra render path](../analysis/ghidra-render-path.md)) carry `DRAW_INDX(pred)` in the trace, next to the `SET_BIN_MASK_LO` header they also write.

## D3D layer counters (FM4_N0_TRACE)

`FM4_N0_TRACE=<file>` counts what stage N0 of the [D3D-level HLE plan](../analysis/d3d-hle.md#9-stages) measures, in [`work/fm4-rex/src/fm4-n0-trace.cpp`](../../work/fm4-rex/src/fm4-n0-trace.cpp). The hooks are in the hook registry [`fm4-guest-hooks.cpp`](../../work/fm4-rex/src/fm4-guest-hooks.cpp), which holds every hook of the D3D layer and its diagnostics (one definition per guest address). One JSON line per second:

- `t`, `steady_ms`: seconds since the first hook call, and the steady clock (the clock of `REX_PM4_STATS`).
- `frames`: `Swap` calls.
- `ib`: replays and their descriptors, as `[calls, descriptors]` per path: `inline` (`RunCommandBuffer` writes one `INDIRECT_BUFFER` per descriptor into the segment it is writing, when bit `0x20` of the buffer's flags at +108 is clear), and the calls of `sub_826E4A28`: `deferred` (a tiling bracket is open, device+13584), `copied` (device+11152 is non-zero, the descriptors are copied into the ring), `queued` (the swap's call with r7 set: a tag stream and a DPC) and `direct` (it returns 0 and `sub_826E4BE0` writes `INDIRECT_BUFFER` at return address `0x826E4C20`).
- `cb`: `begin`, `end`, `clone` and `run` (`BeginCommandBuffer`, `EndCommandBuffer`, `D3DCommandBuffer_CreateClone`, `RunCommandBuffer`), `distinct` buffers replayed in the second (a clone counts as its source), replays of a recording ended in an earlier frame (`reused`) or in the same frame (`same_frame`), the replays among those through another buffer object that points at the recording's first PM4 chunk (`by_chunk`) or, with `FM4_N0_KEYCHECK`, that holds a byte copy of the recording's PM4 (`by_content`), replays of buffers whose recording ended before the trace started (`unseen`), runs of the other object kind (`type10`), and with `FM4_N0_KEYCHECK` the first replay of a buffer that was not recorded in the trace (`first`) and replays whose PM4 is the same as at the previous replay or at `EndCommandBuffer` (`same`) or not (`changed`).
- `emitted`: with `FM4_N0_KEYCHECK`, the `IM_LOAD` and `LOAD_ALU_CONSTANT` packets hashed at emission, the written ranges that did not parse as whole packets (`scan_errors`), and the emission entries given to the packets of a byte copy of a recording at its first replay (`copied`).
- `render`: the calls of the render thread's frame `sub_822DB318` that called `Swap`, with their total and longest time, and the calls without a `Swap` (`idle_calls`, `idle_ms`; the render loop calls it also while it has nothing to present).
- `alloc`: the ring allocations and segment overflows of the second, `"<allocator>:<return address>": [calls, dwords]`, only those that occurred (the summary's `A` table per second; `coverage.py` sums them by phase).

`<file>.summary` is rewritten every 5 seconds: `L <replays> <recordings>` (how many replays each recording had before it was recorded again, open ones with their replays so far), `H <runs from> <buffers>` (a histogram of the replays of buffers never seen ending) and `U <buffer> <replays> <words at +0, +4, +8> <flags at +108> <nodes> <descriptors> <dwords of the first descriptor>` for the 16 most replayed of them, as read at their first replay, `T <entry point> <thread id> <calls>` for the D3D entry points, the tile stream functions and the render frame, and `A <allocator> <return address> <calls> <dwords>` for `BeginCommand` `sub_823913D0`, `AllocateRingSpace` `sub_8234D738` and the segment overflow `sub_822FEF08` (called by inline writers when their segment is full).

`FM4_N0_KEYCHECK=1` hashes, when the draw flush `sub_823436B8` writes them, the microcode of each `IM_LOAD` and the block of each `LOAD_ALU_CONSTANT`, keyed by the guest physical address of the packet, for the runtime's check ([PM4 statistics](#pm4-statistics-rex_pm4_stats)), and hashes the PM4 of each recording at `EndCommandBuffer` and again at each replay. It costs frame rate; use it for counts, not timing.

## Identity check of the native path (FM4_NATIVE_CHECK, host_render_check)

The check of stage N1 increment 2 ([D3D-level HLE, 14.7](../analysis/d3d-hle.md#147-increment-2-design-coverage-by-phase-and-the-identity-check)), off by default. It needs both sides and the host render thread: `--env FM4_NATIVE_CHECK=1 --native --arg host_render_check=true` with `e2e.py` or `matrix.py`.

- `FM4_NATIVE_CHECK=1` (title, [`fm4-native-check.cpp`](../../work/fm4-rex/src/fm4-native-check.cpp)): the dirty state writers, the draw flush and the draw writers copy the PM4 that the outermost of them put into the command buffer segment, split at the ring allocators and the segment overflow (their own PM4 is not copied).
- `host_render_check=true` (runtime, [`host_render_check.cpp`](../../work/runtime/src/graphics/host_render_check.cpp)): the host render thread decodes each copy and compares it with the PM4 it read, the register file (without the registers loaded from memory, those of packets the decoder does not model, and the coherency and gamma ramp registers the command processor changes itself) and the type-3 packets, predicate decisions and draws it executed. A difference is a `REXGPU_ERROR` "Native check: ...", the first 64 of each kind.
- `host_render_check_perturb=N`: every Nth compared range is changed before the comparison; a run with it must have `check_mismatches` equal to `check_perturbed`.
- In `perf.jsonl`, the `host_render` block adds `checks` (ranges compared, by writer), `check_mismatches`, `check_reasons` (`bytes`: the PM4 changed after the writer returned; `registers`, `type3`, `draws`), `check_registers` (the registers that differed), `check_overwritten` (a copy replaced by a newer one at the same address before it was reached), `check_incomplete` (a range left before its end), `check_dropped` (a range a hook could not copy) and `check_perturbed`. With `host_render_native` the block also has `records_applied` (records of converted writers applied at their markers, a segment executed twice applies its records twice), `record_registers` (the register values they wrote) and `markers_missing` (markers reached without a record, each also the error "Native record: marker without a record"). A converted writer's checked range is its marker, and the check compares its record with the PM4 the original body wrote into a scratch segment.
- Cost: with both switches off, one flag test per hooked call and per packet; with them on, a copy per hooked call and a table lookup per packet (measured in [section 14.8](../analysis/d3d-hle.md#148-increment-2-results-21-and-22-october-2026): no measurable cost off, about 3 us more per draw on).

## PM4 statistics (REX_PM4_STATS)

`REX_PM4_STATS=<file>` writes one JSON line per perf trace window (so the lines match `perf.jsonl` and its phases; one per second without `REX_PERF_TRACE_FILE`), in [`pm4_stats.h`](../../work/runtime/src/graphics/util/pm4_stats.h): `steady_ms`, `swaps`, `packets` by type 0 to 3, `type3` as `"<opcode in hexadecimal>": [executed, skipped by predication]`, `indirect_buffers` and their dwords, and the emission checks `im_load` and `load_alu_constant`: a packet that `FM4_N0_KEYCHECK` recorded is hashed again when the command processor executes it, `same_*` or `changed_*` for packets written into the ring or into a recording, `unknown` for a packet no hook recorded: while `FM4_N0_KEYCHECK` records, only its first execution, whose bytes then serve as the reference for its later executions (`same_executed`, `changed_executed`); without it, every execution. `changed_shader_pairs` counts the distinct packet and hash pairs of changed `IM_LOAD`s in the run. `<file>.changes` lists the first 4,096 changed packets (kind: 0 `IM_LOAD`, 1 `LOAD_ALU_CONSTANT`; packet address, bytes, origin: 0 ring, 1 recording, 2 first execution; reference hash, hash at execution). The emission table and the counters' maps of buffers and recordings are never pruned and grow with the run.

## Frame graph (REX_FRAME_GRAPH)

`REX_FRAME_GRAPH=<file.jsonl>` (`REX_FRAME_GRAPH_EVERY`, default 600 frames) records the passes of sampled frames, as JSON lines:

- surface pitch, MSAA, color and depth targets with EDRAM base and format, and window offset;
- every resolve (source, clears, destination);
- which resolve outputs each pass samples, also from earlier frames.

With the host render target path, each pass also has the smallest and largest EDRAM length that its draws claimed per binding slot, and its ownership transfers. GPU timestamps give each pass and resolve its GPU time (including the transfers and texture loads issued for it), plus the time between submissions and after the swap. It is cheap enough for `REX_FRAME_GRAPH_EVERY=1`. `frame` lines have the steady clock (`ms`) to match `captures\captures.txt`.

Print a frame with:

```
python outputs\FM4-build\e2e\frame_graph.py --build-root <BuildRoot> trace.jsonl [--frame N | --list]
```

## Transfer draws (REX_TRANSFER_DRAW_TRACE)

`REX_TRANSFER_DRAW_TRACE=<file>` (`REX_TRANSFER_DRAW_EVERY`, default 60 frames) lists every draw whose host render target update needed ownership transfers:

- shader hashes, primitive type and index count, pixel shader flags, normalized depth control and color mask;
- the registers that decide whether the draw overwrites what it claims: depth/stencil control and reference, color mask, blend, surface, render target infos, viewport, scissors and window offset;
- the transfers as `[slot, start tile, end tile, source render target key]`;
- then the clear check (`u<code>`), and the clear rectangle bounds and transfer cutout that the render target cache used.

The column order of the registers is `kTransferDrawRegisters` in [`work/runtime/src/graphics/d3d12/command_processor.cpp`](../../work/runtime/src/graphics/d3d12/command_processor.cpp), which also explains the codes. To compare runs with the clear handling off, see `--arg=--rtv_uniform_draws=false` in [experiments.md](experiments.md).

## Data race and stale data

These checks were written for the RTV missing-road issue, and they are usable for any renderer work. All write files outside the repository.

- `REX_LATE_WRITE_TRACE=<file>`: hashes the memory that the command processor consumes (indirect buffers, constants and shaders loaded from memory, and vertex ranges up to 256 KB), and checks it again at the next signal that the guest CPU can observe. `L` lines are ranges that the CPU changed in between.
- `REX_STALE_PAGE_TRACE=<file>`: keeps a shadow copy of everything uploaded to the GPU and, every 4th frame, compares the bytes that draws request in valid pages with guest memory. `S` lines mean the GPU copy is out of date. `K` lines are vertex ranges whose reuse without the invalidation epoch would have drawn out-of-date data. Each check frame writes an `F` line.
  - `REX_STALE_PAGE_UNWATCHED=1` (slow, about 15 fps) also lists valid pages that the guest could write without a write watch (`U` lines).
  - `REX_STALE_PAGE_FIX=1` invalidates the stale pages so that they are uploaded again. This changes rendering; use it for A/B comparisons only.
- `REX_READBACK_SKIP_TRACE=<file>`: resolve readbacks that left pages alone because the guest wrote them after the resolve.

## Readback dumps (REX_READBACK_DUMP)

`REX_READBACK_DUMP=<directory>` with `REX_READBACK_DUMP_ADDRESS=<guest address, hexadecimal>` (`REX_READBACK_DUMP_EVERY`, default 120) writes the bytes that a resolve readback copies into guest memory, for every Nth readback of that address. Each file, `<address>_<sequence>.bin`, starts with a header line `RBDUMP <RB_COPY_DEST_INFO> <RB_COPY_DEST_PITCH> <length>` (hexadecimal), followed by the bytes. Use it to check the downscaled readbacks of resolution scaling against 1x.

## Resolve publication (REX_RESOLVE_PUBLICATION_TRACE)

`REX_RESOLVE_PUBLICATION_TRACE=<file>` appends one JSON line per resolve publication, up to 100,000 lines, with `sequence`, `address`, `length`, the performance counter `ticks` and `frequency`, and `nonzero_samples_of_64` (how many of 64 bytes sampled across the resolved range are nonzero). Recording starts only once the file `<file>.arm` exists: the runtime checks for it every 250 ms, and once it finds the file it keeps recording (the code does not disarm again, so deleting `.arm` does not stop the trace). Create the `.arm` file while the game runs to start the trace at that point. Source: [`command_processor.cpp`](../../work/runtime/src/graphics/d3d12/command_processor.cpp).

## File and I/O

- `REX_FILE_READ_TRACE=<file>`: every guest file read, with time, status, offset, sizes, buffer, async flag and path. Compare runs to find streaming differences.
- `REX_IO_TRACE=<file>`: `NtReadFile` calls with the event, APC, IOSB and returned status, the guest return addresses of the call chain, and the waits on read events.
- `FM4_STREAM_TRACE=<file>`: the title's resource loader ([`work/fm4-rex/src/fm4-stream-trace.cpp`](../../work/fm4-rex/src/fm4-stream-trace.cpp)): request steps, the section allocation and its failure flags, archive stream reads, `ReadFile` and `GetOverlappedResult` results, decompressor results, and the memory manager's pool table when a section allocation fails.

The streaming fix itself is always on. `FM4_STREAMING_FALLBACK=0` turns it off for comparisons; see [experiments.md](experiments.md).
