# Debug trace environment variables

These variables switch on diagnostic output in the runtime and in the FM4 integration code. They are off by default. Pass them with `--env NAME=value` on [`e2e.py`](README.md) (or `matrix.py`, which passes them to every cell). The files they write are local and must go outside the repository, for example under `<BuildRoot>\e2e\<name>`.

`e2e.py` strips the `FM4_*` and `REX_*` variables that it inherits from the shell, so only `--env` values and the launcher's own variables reach the game. Experiment switches that change rendering or timing (A/B overrides, workarounds) are in [experiments.md](experiments.md).

Summary of the variables on this page:

| Variable | Writes | Default cadence | Section |
| --- | --- | --- | --- |
| `REX_PERF_TRACE_FILE` | one JSON line per second (and `.resolves.json`) | every second | [Performance trace](#performance-trace-rex_perf_trace_file) |
| `REX_CP_PROFILE` | stack samples of the command-processor thread | about every millisecond | [Command-processor profiler](#command-processor-profiler-rex_cp_profile) |
| `REX_WAIT_REG_MEM_TRACE` | summary of WAIT_REG_MEM conditions | per run | [WAIT_REG_MEM](#wait_reg_mem-rex_wait_reg_mem_trace) |
| `REX_GUEST_YIELD_TRACE` | the guest's delays and yields per thread and caller | every 65,536 calls | [Guest yields](#guest-yields-rex_guest_yield_trace) |
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
| `FM4_SCENARIO_TRACE=1` | the title's tiling scenario: every change of `CTileBuffer::SetScenario`'s scenario (`sub_82883948`, r7) and of its caller's (`sub_828718C0`, r6), with the caller and the steady clock in ms, on stderr | every change | the render scenario names of photo mode and the menus are in the e2e events file (`push PhotoMode`) |
| `REX_FRAME_GRAPH` | passes, resolves and GPU times (JSON lines) | `REX_FRAME_GRAPH_EVERY`, default 600 | [Frame graph](#frame-graph-rex_frame_graph) |
| `REX_TRANSFER_DRAW_TRACE` | draws that needed ownership transfers | `REX_TRANSFER_DRAW_EVERY`, default 60 | [Transfer draws](#transfer-draws-rex_transfer_draw_trace) |
| `REX_LATE_WRITE_TRACE`, `REX_STALE_PAGE_TRACE`, `REX_READBACK_SKIP_TRACE` | data race and stale data findings | every frame or every 4th frame | [Data race and stale data](#data-race-and-stale-data) |
| `REX_READBACK_DUMP` | bytes of one resolve readback | `REX_READBACK_DUMP_EVERY`, default 120 | [Readback dumps](#readback-dumps-rex_readback_dump) |
| `REX_RESOLVE_PUBLICATION_TRACE` | one JSON line per resolve publication, after `<file>.arm` exists | every publication | [Resolve publication](#resolve-publication-rex_resolve_publication_trace) |
| `REX_FILE_READ_TRACE`, `REX_IO_TRACE`, `FM4_STREAM_TRACE` | guest file and I/O activity | every call | [File and I/O](#file-and-io) |
| `FM4_INPUT_TRACE_FILE`, cvar `input_latency_probe` | the title's deadzones, stick and trigger output and rumble; the age of SDL pad states at the guest's read | every change; every 5 seconds | [Gamepad input](#gamepad-input) |

## Performance trace (REX_PERF_TRACE_FILE)

[`work/runtime/src/graphics/util/perf_trace.h`](../../work/runtime/src/graphics/util/perf_trace.h) writes one JSON line per second to the file named by `REX_PERF_TRACE_FILE`. Without the variable it does nothing. `e2e.py` sets it to `<out>\perf.jsonl` for every run, so the harness always has this trace.

Each line (one window) has:

- `t`, `window_ms`: the time of the window and its length.
- `fps`: guest swaps per second in the window. `frame_max_ms`, and `frame_ms` (the time of every frame in the window).
- `long_frames`: the frames over 50 ms of the window as `[ms, shader work ms, look mod FXC ms, post pass FXC ms]`: the frame's time, the shader work on the draw path inside it (the kinds of `shader_work` below but `mod_compiles_worker` and `mod_swaps`), and of that the FXC compiles of look mods and of post passes ([d3d-hle-n6.md 19.3.4](../analysis/d3d-hle-n6.md#1934-increments-and-gates)).
- `shader_work`: shaders and pipelines made in play, each kind as `[count, ms, longest ms, count over 4 ms]`: `translations_vs` and `translations_ps` (DXBC translations where a draw is issued: the host render thread or the command processor), `mod_compiles` (look mods compiled with FXC there) and `mod_cache_loads` (mods taken from `shader_override_cache_path` there), `pipelines` (pipelines created for a draw: there, or on the draw back end's replay), `pass_compiles` (the post passes' FXC compiles, any thread), and off the draw path, on the compile workers of `shader_override_async`: `mod_compiles_worker` (mods compiled) and `mod_swaps` (pipelines recreated with a compiled mod, which the pipeline cache then swaps in); `mods_pending` is the number of mods queued for a worker or for their swap at the window's end. The start-up's translations and pipelines (`InitializeShaderStorage`) and hot reload are not counted.
- `draws`, `copies` (resolves), `readbacks`, `readback_kb`, `upload_kb`.
- `invalidations`, `invalidated_kb`: shared memory invalidations by guest CPU writes (any thread; with the pages around the written one).
- `process_cpu` (with `perf_trace_cpu`, on by default): the process's CPU time in the window (`ms`) and the twelve thread names with the most CPU time, as `[ms, threads of that name]`. A sampler thread of its own (`Perf Trace CPU`, below normal priority) measures each second and the window takes its last second; it lists the threads again every tenth second, a snapshot that takes tens of milliseconds and on the render thread held a frame ([d3d-hle-n4.md 17.19](../analysis/d3d-hle-n4.md)).
- With `host_render_thread` only: `queue_wait_ms`, each record's wait in the host render queue from its push to its take: `mean`, `p50`, `p99` (upper edges of 0.25 ms bins) and `max` ([d3d-hle-n4.md 17.18](../analysis/d3d-hle-n4.md#1718-the-queue-depth-and-the-latency-23-october-2026)).
- `guest_ring_waits`: the title's ring index waits that slept with `guest_ring_wait_sleep` ([d3d-hle-n4.md 17.16](../analysis/d3d-hle-n4.md#1716-event-waits-in-place-of-the-remaining-spins-23-october-2026)): `waits`, the ones that ended at their 1 ms limit (`timeouts`), and their time (`ms`).
- `host_presents`: the presenter's presents in the window: of a guest output frame no earlier present showed (`new_frames`), of the same frame again (`repeats`), of those, the ones the UI thread made (`ui_thread`; the others come from the guest output thread or, with `d3d12_submit_thread_async_swap`, the submit thread), those sooner after the previous present than a refresh at 120 and 240 Hz (`under_8_33_ms`, `under_4_17_ms`), and the mean GPU time of a presenter paint (`paint_gpu_ms`, [17.24](../analysis/d3d-hle-n4.md#1724-gpu-time-per-pass-24-october-2026)) ([d3d-hle-n4.md 17.22](../analysis/d3d-hle-n4.md#1722-host-presents-24-october-2026)).
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

### Read trace of resolve readbacks (readback_read_trace)

With the cvar `readback_read_trace=true` (off by default) and the trace, every resolve readback published to guest memory makes its destination pages inaccessible in the three guest views of physical memory (`A0000000`, `C0000000`, `E0000000`) until the next readback of the same destination. The first access to a trapped page restores the protection it had (so a write still meets its write watch), is counted, and continues; nothing waits, and the readback itself is unchanged. Readers through the runtime's physical view (`TranslatePhysical`: the uploads into shared memory and the readback copy) are not trapped. Design and results: [d3d-hle-n3.md 16.4](../analysis/d3d-hle-n3.md).

Each window then has a `read_trace` object: `readbacks` and `readback_kb` published and trapped; arms closed by the next readback of their destination as `read` (a read came first), `written_first` (a write came first) or `unread` (no access), with their bytes in `read_kb`, `written_kb` and `unread_kb`; `read_faults` and `write_faults` (one per page and view); `guest_first` and `host_first` (whether the first reader of an arm was a guest thread); `armed_pages` (trapped now), `kept_pages` (still trapped from an earlier arm), `kept_arms` (arms left as they were because the trap was intact) and `lost` (traps another protection change removed). Every 10 windows `<REX_PERF_TRACE_FILE>.reads.json` lists the same counts per destination (address and length) for the whole run, with the names of the first readers' threads.

### Resolves into host textures (native_rt_resolve)

With `native_rt_resolve=true` (off by default) each window has a `native_rt_resolve` object: `resolves` (full-screen 32bpp resolves that looked for host textures of their destination), `textures` (the textures they wrote straight, which then need no load), `misses` (resolves that found none, so the texture loads as before), and with `native_rt_resolve_check=N` (every N-th frame the resolve shader writes scratch copies instead, the loads stay, and the loaded texels are compared with the scratch's) `checks` and `mismatches` (each mismatch also logs a warning with the first differing texel). The design is in [d3d-hle-n3.md 16.7](../analysis/d3d-hle-n3.md).

The object is in every window. It also counts the resolves from private surfaces that write host textures (`native_rt_surfaces` bit 32 and the scene's textures), and the writes that `native_rt_resolve_skip` leaves out ([d3d-hle-n6.md 19.7.4](../analysis/d3d-hle-n6.md#1974-step-a-cheaper-host-texture-writes-27-october-2026)): `skips_load` (textures at the whole scale, left to their load from the guest's layout), `skips_sibling` (textures at the scene's scale that nothing sampled since their last write, left to their `packed_mips` sibling), `fallbacks` (such textures copied from the sibling when something sampled them) and `fallbacks_missing` (sampled ones whose sibling no longer held the resolve's data; 0 unless the switch's perturbation drops the copies). The first 16 keys skipped for a sibling are logged as `Resolve texture write skipped`, and the first 16 keys at the scene's scale written while nothing sampled them since their last write as `Resolve texture written unsampled`, with their sibling's state.

Each window also has a `guest_layout` object (`native_rt_resolve_layout`, [d3d-hle-n6.md 19.7.5](../analysis/d3d-hle-n6.md#1975-step-b-the-guest-layout-on-demand-28-october-2026)): the guest's layouts of full-screen destinations at the scene's scale written from their host texture (`from_texture`) or from the render target because no texture took the resolve's data (`from_target`), and those deferred (`deferred`). A deferred layout ends as `superseded` (the same resolve again), `written_load`, `written_request`, `written_readback` or `written_resolve` (written for a texture load, another GPU request such as a vertex buffer or memexport, the readback on demand's service, or a resolve that overlaps it without writing all its bytes; the first 16 such pairs are logged as `Deferred guest layout ... written for a resolve`), `written_probe` (the check's reader), `written_overflow` (more than 32 pending) or `perturbed` (dropped by the switch's perturbation). `cpu_writes` counts CPU writes over a pending one, which stays (none expected: the readback on demand serves the CPU's first access first; the pages the CPU wrote upload over the layout's write when requested). `source_loads` counts loads of a deferred layout's source texture while the layout is pending: a texture at the scene's scale reads no memory when it loads and keeps its data, so these are not faults. `invariant` counts faults of the rule the deferral rests on, that a texture at the scene's scale changes only with a resolve to its own range: a source texture that no longer holds its resolve's data when the layout is written (logged as an error). `gpu_writes` counts GPU writes over a pending layout that no hook ordered first (the first 8 logged as `GPU write ... over the deferred guest layout`); every resolve and every other GPU request writes the layouts it overlaps first, so none are expected. `foreign_thread` counts readers that found a deferred layout off the command processor's thread (left deferred). `checks` has four entries, by class (nearest-pixel `8_8_8_8`, nearest-pixel `2_10_10_10`, box `8_8_8_8`, box `2_10_10_10`), each `[layouts compared, pixels, words that differ, bytes that differ, largest difference of one channel in encoded steps]` (`native_rt_resolve_layout_check`).

### Resolves from the host render target (native_rt_surfaces bit 2)

With bit 2 of `native_rt_surfaces` each window has a `host_resolve` object: `found` (resolves that could read their source from one host render target), `done` (those that did, outside check frames), `other` (copy resolves that could not), and with `native_rt_surfaces_check=N` `checks`, `mismatches` (each also logs a warning with the first differing words) and `check_formats` (checks by the source format: color formats 0 to 15, then D24S8 and D24FS8). The design is in [d3d-hle-n3.md 16.11](../analysis/d3d-hle-n3.md). The unscaled fast copies of 4x MSAA sources come from the host render target too ([d3d-hle-n3.md 16.17](../analysis/d3d-hle-n3.md)). The mismatch warnings give the EDRAM base and pitch, the resolve rectangle, the render target's base and pitch, the destination size, and where each side's first differing word appears on the other side; with the diagnostic `native_rt_surfaces_unscaled_msaa=true` the checks of multisampled sources that match log an info line. `native_rt_surfaces_depth_dump=<directory>` (off) writes every unscaled 4x depth fast copy's destination bytes there, one file per address, and with the check the first 24 differing 4x depth copies from both sides.

With bit 4 of `native_rt_surfaces` (lazy ownership transfers, [d3d-hle-n3.md 16.12](../analysis/d3d-hle-n3.md)) each window has a `lazy_transfers` object, counted in tiles:

- `copied`: copied when a render target claimed them.
- `deferred`: left with their holder.
- How deferrals end without a copy:
  - `claimed_back`: the holder claimed them again;
  - `overwritten`: copied for an overwrite draw but inside its cutout, which drops the copy.
- `redeferred`: claimed by another render target and deferred again, from the same holder.
- How deferrals end with a copy:
  - `chain_copied`: from the holder into a new owner that reads them;
  - `flushed_draw`, `flushed_resolve`, `flushed_clear` and `flushed_other`: before a draw, a resolve's copy, a resolve's clear or a post pass read them.
- `violations`, `hazards` and `perturbed`: from `native_rt_surfaces_lazy_check` (deferred tiles left where a draw touches the range it claims; tiles a render target holds for another owner where it may touch beyond its range) and `native_rt_surfaces_lazy_perturb`.
- `pending`: the tiles still deferred at the end of the window.

The perf trace's `gpu` object has `transfer_ms`: the GPU time of the ownership transfers a frame, including the copies inside resolves. `transfer_depth_ms` is the share of the transfers into depth render targets. `resolve_buckets` splits the resolves by the bytes they write: `full` (at least 1280x720 at 32bpp), `mid` (at least 256x256), `small` and `clear` (clears only), each as [GPU ms, resolves] a frame. `native_rt_surfaces_transfer_inventory=true` logs the transfers every 600 frames: one info line per kind (the destination's and the source's format, sample count, EDRAM base and pitch in tiles, with a separate host depth source, with stencil bit draws) with its transfers and tiles, the destination's tile columns it wrote (a bit per column) and its first and last tile rows.

The `gpu` object also has `resolve_full_parts_ms`, the GPU time a frame of the full-screen resolves' parts: `transfers` (the deferred transfers of the tiles a resolve copies), `dump` (the render targets dumped into the EDRAM buffer), `guest_layout` (the destination written in the guest's memory layout, the scaled resolve buffer or shared memory, from the host render target or the EDRAM buffer), `host_textures` (the host textures written straight from the render target, `native_rt_surfaces` bit 32 or the scene's textures) and `clear` (the resolve's clears with their transfers); what `resolve_full_ms` has beyond them is barriers and gaps ([d3d-hle-n6.md 19.7.1](../analysis/d3d-hle-n6.md#1971-resolves-without-the-guests-memory-layout)). `resolve_host_writes` has the host textures that all resolves write from the render target, as [GPU ms, writes] a frame by class: `scene_sampled` and `scene_unsampled` at the scene's scale, `whole_sampled` and `whole_unsampled` at the whole scale; a write is `unsampled` when nothing sampled the texture since its last write (a guest draw's binding, the swap or a post pass), so in a steady scene it counts the writes that nothing reads ([d3d-hle-n6.md 19.7.3](../analysis/d3d-hle-n6.md#1973-step-1-the-resolves-gpu-time-27-october-2026)). It has `passes_ms`, the GPU time a frame of the post passes by group (`gtao`, `taa`, `motion`, `windshield`, `fsr`, `other`, by the prefix of their names in `passes.toml`), `swap_ms`, the swap's gamma and FXAA passes, and `scene_ms`, the rest of the frame's GPU time (`frame_ms` less the two), which holds the guest's draws, resolves and own post-processing with the look mods ([d3d-hle-n4.md 17.24](../analysis/d3d-hle-n4.md#1724-gpu-time-per-pass-24-october-2026)).

Every window has an `rt_memory` object: the host render targets (`targets`), their memory in MB now (`mb`) and at its peak (`peak_mb`), and the growths (`resizes`) and evictions of the window. With `native_rt_surfaces` bit 8 the targets are sized to the tile rows their surfaces use and grow when a draw or a clear needs more ([d3d-hle-n3.md 16.15](../analysis/d3d-hle-n3.md)); `native_rt_surfaces_memory_cap_mb=N` evicts the least recently bound targets that own no tile while the memory is over N MB. `native_rt_surfaces_grow_stress=true` creates every target with 8 tile rows and grows it only to the rows asked for, so that every surface takes the growth path (a check run with the resolve byte check).

### Ownership transfers read or not (native_rt_surfaces bit 1)

With bit 1 of `native_rt_surfaces` (count only, off by default) each window has an `rt_transfers` object: the ownership `transfers` the render target cache made, their `tiles` and the `pending_tiles` left after the overwrite cutout, and how those tiles ended: `dropped` (another render target took them before anything touched them), `overwritten` (a draw's overwrite rectangle wholly covered them, or a resolve cleared them), `read_draw` (a draw that may read touched them: any draw that is not a uniform overwrite, within its scissor, so this overestimates) and `read_resolve` (a resolve copied them); `transfers_unread` and `transfers_read` count the transfers that ended with no tile read and with some. The design is in [d3d-hle-n3.md 16.9](../analysis/d3d-hle-n3.md).

### Constant buffers of the draws (d3d12_deferred_constants)

Each window has a `constants` object. On either path: `fills` (float vertex, float pixel, bool and loop, and fetch constant buffers filled at draws), `slots` (their 16-byte slots: one per used float constant, 10 for bool and loop, 48 for fetch), `samplers` (sampler parameters computed at draws) and `descriptor_fills` (bindless descriptor index buffers written). With `d3d12_deferred_constants=true` (off by default) the buffers are filled when the deferred command list is replayed: `captured` (the slots copied from the register file at the draws), `replays` (the commands replayed) and `replay_ms` (their time on the replaying thread, from the time stamp counter), and with `d3d12_deferred_constants_check=true` `checks` (buffers compared with the inline path's bytes), `mismatches` (each also logs an error, for the first 16) and `perturbed` (comparisons changed on purpose with `d3d12_deferred_constants_check_perturb=N`, one every N-th command; a perturbed run passes when `mismatches` equals `perturbed`). The design is in [d3d-hle-n4.md 17.2](../analysis/d3d-hle-n4.md).

With `REX_N4_REGISTER_DELTA=1` the `constants` object also has `delta_draws`, `delta_dwords` and `delta_granules`: the context registers (`0x2000` to `0x23FF`) that changed from one draw to the next, in dwords and 16-byte granules.

### The draw back end (d3d12_draw_back_end)

Each window has a `back_end` object. With `d3d12_draw_back_end=1` (off by default, the capture only) or `2` (the back end on the replay): `captures` (draws whose back-end inputs were captured into the deferred command list), `bytes` (their size without the check's results), `context_dwords` (context registers captured), `written_dwords` (context registers written since the previous capture, before the back end's set and the change filter), `fetch_slots` (16-byte constant register slots captured: with `1` the fetch constants of the samplers, with `2` the float constants of both shaders' maps, the fetch constants they read, and the bool and loop constants), `group_bytes` (the front half's results that changed), `capture_ms` (the capture's time on the render thread, from the time stamp counter around every 16th capture, counted 16 times; around every capture with `REX_N4_CAPTURE_SPLIT`), `replays` and `replay_ms` (the commands applied to the replay's copy and their time on the replaying thread), and with `d3d12_draw_back_end_check=true` `checks` (draws whose back-end results were computed again from the copy and compared), `perturbed` (comparisons changed on purpose with `d3d12_draw_back_end_check_perturb=N`) and `mismatches`, seven counts by kind: pipeline description, viewport, scissor, blend factor or stencil reference, system constants, sampler parameters, constant buffers (with `2`). A perturbed run passes when the pipeline count equals `perturbed` and the others are 0. With `2` also `pipelines` and `pipeline_ms` (pipelines the replay created, synchronously, and its time for them) and `dropped` (draws the replay could not record). The design is in [d3d-hle-n4.md 17.6](../analysis/d3d-hle-n4.md).

With `REX_N4_REPLAY_SPLIT=1` each window also has a `replay_split` object: the replay of the deferred command lists by command kind, `executes` and `execute_ms` (whole `DeferredCommandList::Execute` calls) and the arrays `ms` and `counts`, indexed by `DeferredCommandList::Command` in its declaration order (the time stamp counter around each command; their sum against `execute_ms` gives the walk).

With `REX_N4_CAPTURE_SPLIT=1` and the draw back end on, each window also has a `capture_split` object: `ms`, the capture's time on the render thread in six parts (the context registers, the constant masks, the front half's groups, the command's allocation in the deferred command list, the record up to the constant slots, the rest with the check's part; the split's own time stamps add to them), and the captured constant slots by kind: `float_slots`, `fetch_slots` and `bool_loop_slots`.

With `render_target_update_guard` (or its check) each window has an `rt_update_guard` object: the draws keyed (clipping on), those that skipped the render target cache's update (`skips`), and with `render_target_update_guard_check` the updates run anyway and compared (`checks`), the verdicts changed on purpose (`perturbed`) and the mismatches. A perturbed run passes when `mismatches` equals `perturbed`.

With `REX_N4_TEXTURE_KEYS=1` each window also has a `texture_keys` object: the keys that `RequestTextures` computed again for the used bindings out of sync (`recomputes`), those whose fetch constant had the six dwords of the binding's last one (`same_dwords`), those that kept the binding with other dwords (`same_binding`), those whose key changed (`key_changes`), and of these the keys equal to one of the binding's last four (`recent_keys`). The counting decodes each fetch constant a second time, so the keys zone runs slower with it. Its `resets` object counts the binding resets by cause ([d3d-hle-n6.md 19.6](../analysis/d3d-hle-n6.md#196-item-4-a-native-texture-cache)): `frame` (the frame start), `gpu`, `cpu` and `both` (a texture that became outdated through a GPU write such as a resolve, a CPU write, or both since the last draw) and `destroyed` (textures destroyed). Each is an array: the resets, the valid bindings reset, the keys computed again for those bindings, the keys that found the textures the binding had before the reset, the keys that loaded a texture, the valid bindings a reset left alone (`texture_binding_resets`), and at a reset for an outdated texture, the valid bindings that held an outdated or missing texture.

The `binding_check` object (with `d3d12_reuse_draw_bindings_check`) counts the bindings kept in sync compared with fresh keys (`checks`), the `mismatches` and the comparisons changed on purpose (`perturbed`), and after the loads, the used bindings whose texture differs from a lookup of their key (`textures`), those left with an outdated texture (`outdated`), those met while another texture had just become outdated (`outdated_racing`, not a mismatch: the next draw's reset covers them), and the resets `texture_binding_resets_perturb` left out (`resets_perturbed`).

### Readbacks on demand (readback_resolve_on_demand)

With `readback_resolve_on_demand=true` (set by `-Native`) each window has a `demand_readback` object: `deferred` and `deferred_kb` (resolves whose readback was left to the first access), `kept_arms` (of those, the ones whose trap was intact and stayed), `requests` (accesses or file reads that asked for a readback), `served` and `served_kb` (readbacks done on demand), `forced` (file reads into a pending range), `stall_ms` and `stall_max_ms` (time the accessing threads waited, summed and longest), `service_ms` (the command processor's time for them), `timeouts` (accesses that gave up after 5 s, with an error), `cp_faults` (the command processor itself touched a pending range, with an error), `failed` (readbacks that could not be recorded or awaited), `lost` and `lost_pending` (traps another protection change removed, and of those the ones with a pending readback), and `physical_reads_pending` (shared memory uploads that read a page whose readback was pending). The design is in [d3d-hle-n3.md 16.5](../analysis/d3d-hle-n3.md).

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

## Guest yields (REX_GUEST_YIELD_TRACE)

`REX_GUEST_YIELD_TRACE=<file>` summarizes the guest's `KeDelayExecutionThread` and `NtYieldExecution` calls ([`work/runtime/src/system/xthread.cpp`](../../work/runtime/src/system/xthread.cpp)): one line per thread name, guest caller (the link register at the call) and delay asked for, with the calls and their time. `delay_ms 4294967295` is a yield; a delay of 0 yields too. A thread with many of them and its CPU time in `cpu_power` (e2e) spins; the title's `Sleep` wrapper (`sub_8238C6E8`) is the caller of its sleeps, not their origin ([d3d-hle-n4.md 17.16](../analysis/d3d-hle-n4.md#1716-event-waits-in-place-of-the-remaining-spins-23-october-2026)).

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
- A range whose PM4 changed after its writer returned is reported with the changed dword's guest physical address, its old and new value and the number of changed dwords. The late write trace (title, with `FM4_NATIVE_CHECK` unless `FM4_LATE_WRITE_TRACE=0`) notes the functions that rewrote the last dwords of a range the host render thread had not reached yet (the tiling bracket's functions, the readers of the device's draw list and `DrawIndexedVertices`), with the guest call chain; the check prints the notes of such a range as `Native check: late write:` warnings, or that no hooked function noted one ([D3D-level HLE, 17.20](../analysis/d3d-hle-n4.md)). `FM4_LATE_WRITE_TRACE=2` also prints every note to stderr, most of them of ranges that never run.
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

## Gamepad input

- `FM4_INPUT_TRACE_FILE=<file>` ([`fm4-ui-diagnostics.cpp`](../../work/fm4-rex/src/fm4-ui-diagnostics.cpp)) writes the title's own pad handling, one line per change and up to 200,000 lines, with the steady clock in milliseconds: `cfg` lines with the ten deadzone floats the input conversion `sub_822B4130` gets (left stick, right stick, left trigger and right trigger inner and outer, then the right stick's second pair), `in` lines with the raw state (sticks, triggers, buttons) and the title's output (trigger bytes, and per stick its direction, angle, magnitude, x and y), and `rumble` lines with the motors of the title's `XInputSetState` (`rumble-async` for its overlapped path). `u=` is the session update count, `r=` the race marker of the diagnostic controller. Run it with a human driver: the title sends no rumble while the AI drives. The findings are in [modern gamepads](../analysis/modern-gamepads.md).
- `--arg input_latency_probe=true` (runtime cvar, SDL backend) attaches a virtual SDL gamepad whose right stick X flips every 20 to 49 ms on a thread of its own, and logs to `runtime.log` every 5 seconds the gaps between SDL's joystick updates, the wait from a new value to the update that sent it, from that update to the guest's read, the age at the read, and the gaps between the guest's reads (`SDL latency probe (ms): ...`), then what the driver sent to the virtual pad's rumble and trigger rumble callbacks in that window: the number of changes, the largest left and right values and the last ones (`rumble changes=... max=... last=...; trigger rumble changes=...`). The same rumble part is logged once more when the game window closes, after the pads' rumble is stopped (`SDL latency probe at exit:`); the e2e harness kills the game instead, so its runs do not have that line. The value (256) stays inside every stick deadzone. The probe is a physical SDL pad to the runtime: without another pad it is guest user 0 and merges with the diagnostic controller, so a human-driven run's rumble and the pad options (`pad_rumble_strength`, `pad_trigger_rumble`) reach it. It lets SDL take pad events without window focus, and with it trigger rumble keeps running without focus.
