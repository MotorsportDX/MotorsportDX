# Native port, stage N3: host render targets

Section 16 of the [D3D-level HLE design](d3d-hle.md), moved here on 23 October 2026 so the design note stays within the repository's file size limit. Section numbers are unchanged; sections 1 to 15 are in [d3d-hle.md](d3d-hle.md) and section 17 in [d3d-hle-n4.md](d3d-hle-n4.md).

## 16. Stage N3 design, 22 October 2026

Stage N3 (section 9) moves the render targets out of the EDRAM model: host targets keyed by the title's surfaces, resolves as host copies with readback on demand, one-pass tiles and native MSAA, the post passes anchored on surfaces. The design table of section 8 holds; this section measures where the GPU and the render thread spend the work N3 changes, and orders the increments by it. The resolve and tiling writers deferred from N1 (14.11) belong here.

### 16.1 Measured, 22 October 2026

RTV with the launcher defaults (one tile with TAA), the `career` scenario with `REX_FRAME_GRAPH` every 60th frame at 1x (`n3m-fg-career`, 268 race frames) and at 2x (`n3m-fg2x-career`, 163 race frames), and the perf gate at 2x (`n3m2xp`, the `photo` race, three runs), on `I:\FM4-build-usa` at 240 Hz. Per race frame:

| | 1x | 2x |
|---|---|---|
| GPU time in passes (frame graph) | 1.94 ms | 3.97 ms |
| GPU time in resolves, with the transfers and texture loads issued for them | 1.15 ms | 2.75 ms (41% of the frame's) |
| of which the full-screen depth resolve (1280x720 to `8_8_8_8`) | 0.39 ms (1.25 a frame) | 1.11 ms (1.43 a frame) |
| of which the full-screen colour resolve (1280x720, `2_10_10_10`) | 0.42 ms (2.21 a frame) | 0.84 ms (2.59 a frame) |
| Resolves | 28 (28 to 35) | 28 |
| Ownership transfers | 20 (18 to 24) | 20, 0.25 ms in the passes that only transfer |
| Surfaces (distinct base, format, MSAA and pitch) | 15 (14 to 17) | |
| EDRAM bases shared by more than one surface | 1 | |
| GPU time per frame (perf gate, median of the runs) | | 5.73 ms, race 172.1 fps, p99 8.71 ms |
| Resolve readbacks to guest memory (`readback_resolve=fast`) | | 24.8, 13.9 MB |

- The scene renders into one colour surface at EDRAM base 0 (`2_10_10_10_FLOAT`, also viewed as `16_16_16_16`) with its depth at base 720 in one tile; probably the rear-view mirror into a 2x MSAA surface at pitch 400, the shadow cascades into depth at pitch 560 (resolved to 512x512), and the bloom and exposure chain into small surfaces at bases 0 and 288 (inferred from the pass shapes, not from the shaders).
- The frames of the photo and garage scenes keep the tiles and 4x MSAA of `-Quality`: 11.7 tile resolves of 1280x720 a frame there.
- On the render thread at 1x (15.10) the render target cache takes 0.23 ms a race frame and the code between the primitives and it about 0.15 ms; the resolves (the copy zone) 0.18 ms.
- The GPU time of the resolves split by kind (`n3s2xp`, the perf gate at 2x with GPU timestamps around the resolve copies, the texture loads and the readback copies, medians of the windows of three runs): copies 0.638 ms, texture loads 0.484 ms and readbacks 0.814 ms a race frame, of 5.77 ms. The frame graph's resolve time of 2.75 ms counts these together with the transfers issued for the resolves, so the 1.95 ms of the two full-screen resolves is not what a host copy alone removes: their copies and loads are part of 1.12 ms, and the readbacks are the largest single part.

### 16.2 Where N3 can save, by the measurement

1. **The full-screen resolves**: 1.95 of the 2.75 ms of resolves at 2x. Today each one dumps or copies the host render target into the scaled resolve buffer in the guest's tiled layout, the texture cache loads it back into a texture when a pass samples it, and the readback copies it to guest memory. A resolve as a host copy writes the texture the next passes sample straight from the render target and leaves the rest to demand.
2. **The readbacks**: 24.8 a frame, 13.9 MB at 2x, of every resolve, because any of them may be read by the CPU (2.7). Readback on demand needs a read trap on the resolved pages, which the runtime does not have (section 8). Their cost is measured: the perf gate at 2x with `readback_resolve=none` (`n3m2xnorbp`, three runs, the CPU's readers not served) ran the race at 185.7 against 172.1 fps, p99 7.87 against 8.71 ms, and 5.26 against 5.73 ms of GPU time per frame (medians of the runs), at 2,061 against 1,854 draws a frame: at least 0.47 ms of GPU time a frame, 8%.
3. **The ownership transfers**: 20 a frame, 0.25 ms of passes that only transfer at 2x, plus the transfers inside other passes. Surface-keyed targets with virtual placement remove those between surfaces that do not alias.
4. **The render target cache's CPU time**: about 0.4 ms a race frame (16.1), part of N4's CPU targets.

### 16.3 Increments

The order and the scope of increments 1 and 2 are the main session's decisions of 22 October 2026. The first put increment 1 first: it is on the critical path of N3 (the resolves as host copies of section 8 are what one-pass tiles and native MSAA build on), and its ceiling, 1.95 ms at 2x by the frame graph, looked larger than the readbacks' 0.47 ms. The GPU timestamps of 16.1 then showed the readbacks as the largest part (0.81 ms a race frame at 2x) and the copies and loads of all 28 resolves at 1.12 ms, the two full-screen ones' about 0.4 to 0.6 ms, so the revised decision measures first which read-back bytes the CPU reads (a count-only diagnostic) and decides by it: if most of them are never read by a guest thread, increment 2 (readback on demand) goes next, otherwise increment 1 with its ceiling stated as about 0.5 ms. Increment 1 stays on the critical path either way.

By the read trace of 16.4 no read-back byte of the races and the replay is read, so the main session decided on 22 October 2026 that increment 2 (readback on demand) goes next, its ceiling the 0.81 ms of readback copies a race frame at 2x plus the copy into guest memory, with these constraints: a resolve to a trapped destination skips the readback and leaves a pending readback for the latest resolve to the range, its data staying in shared memory; the first guest access, read or write, performs that readback before the access continues (before a write, so the write is not overwritten; other CPU data on the same system page stays correct); the fault handler never holds the global memory lock while it waits for the GPU, and never waits on work that only the faulting thread can issue (the host render thread reads through the physical view and must never fault, asserted); the runtime's own readers of these ranges (shared memory uploads, the texture replacement's exclusion of resolved ranges, any reader of the physical view) are served from the GPU copy or a forced readback, never by a guest fault, audited and listed here; off by default, on with `-Native` and with RTV only by an explicit `--arg`, `-Rov` unchanged, RTV by default only after a soak; gated by the photo save (its image equal to the RTV run's), loading and the menus, no new error key, smoke, perf and parity at 1x and 2x against RTV, the GPU time at 2x, and the demand readbacks and their stall time per frame (about 0 in the races), with the read trace kept as its diagnostic.

After increment 2 (16.6), the main session decided on 22 October 2026: the readback on demand stays off by default on RTV and soaks through `-Native`; RTV gets it by default only after N3's later increments have run it for a while with no timeout, command processor fault or lost trap, so every gate records those counters. Increment 1 goes next, on the critical path, with its ceiling of about 0.5 ms; since the readback is now on demand, it needs no readback shader of its own: a pending readback of its destinations is served from the host texture only when a guest access asks for it, and the design checks whether that removes one of its three shaders. Increment 3 (surface-keyed targets) follows, then increment 4 (one-pass tiles and native MSAA with TAA on top), which measures 2x MSAA at 2x against 4x MSAA at 1x to pick the defaults.


| Increment | Scope | Gate |
|---|---|---|
| 1 | The full-screen depth and colour resolves of the scene (16.2, 1) become host conversion copies into the texture cache's host textures for their destinations (depth to the guest's `8_8_8_8` packing, float to `2_10_10_10` with the guest's exponent bias), behind `native_rt_resolve` (off). Production-correct: they keep their readback, served from the host texture through the existing asynchronous readback path, so the CPU's readers (the photo save) are served and it runs in every gate. It saves the copy into the scaled resolve buffer and the texture load from it; the readback stays until increment 2. A check mode compares the converted texture's bytes with the RTV path's (the scaled resolve plus the load) on a few frames. | GPU time at 2x per race frame against RTV of the same commit; smoke, perf, `parity.py` on the one-tile `freeze`, the photo save; 1:1 crops at 2x of GTAO, TAA, motion vectors and the windshield. |
| 2 | First a count-only measurement: which resolved ranges the CPU reads, how often and from which threads (the 0.47 ms of 16.2 is an upper bound with no read served). Then, only if the CPU reads few of them, the readback on demand with a read trap on the resolved pages, its handler designed for the runtime's own reads of guest memory as well (a thread that faults must never wait for a readback it has to issue itself); a risk item with its own check, since section 8 notes that a read trap is not built or verified. | The read counts recorded; then the photo save and the texture replacement's exclusion of resolved ranges work, and the readback bytes per frame are measured. |
| 3 | Surface-keyed host targets with virtual placement, the transfers only where a pass reads what another surface left (16.2, 3); the resolve and tiling writers of N1 as records. | Frozen frames within the image limit against RTV and against `-Rov` with `-Look "original,taa=0"`; transfers per frame measured. |
| 4 | One-pass tiles and native MSAA (2x and 4x in one pass, TAA on top), so `-Quality`'s tiles leave the PM4 RTV path. | 4x MSAA in one pass; the tiled scenes in one pass within the image limit. |
| 5 | The post passes anchored on surfaces; GTAO, TAA, motion vectors and the windshield checked at 2x on the native path. | The N3 gate of section 9. |

### 16.4 Who reads the readbacks, 22 October 2026

The count-only measurement of 16.3 is the diagnostic `readback_read_trace` (off by default; [traces.md](../e2e/traces.md)). After each resolve readback is published, its destination pages become inaccessible in the three guest views of physical memory until the next readback of the same destination; the first access to a trapped page restores the protection it had, counts the access with its thread and continues, without waiting. The readback stays on, so the guest reads the right data. The trap keeps the write watches intact: the protection it restores is the one the watch set (`kReadOnly`), so a write faults a second time into the watch; a watch enabled on a trapped page, or unwatched by a write elsewhere, updates the protection the trap restores instead of removing the trap; a trap that another protection change removed anyway is counted as lost. Readers through the runtime's physical view (the uploads into shared memory and the readback copy itself) are not trapped: they use the GPU's copy or write it.

Measured on `I:\FM4-build-usa` at 2x and 240 Hz with the launcher defaults, one run each of `photo` (`n3r-photo-2x`, the race, photo mode and the photo save) and `career` (`n3r-career-2x`, the race and the replay); no new error key, the photo saved, no trap lost:

| Phase | Readbacks a frame | Bytes a frame | Read by any thread | Written first |
|---|---|---|---|---|
| `photo` race (5,440 frames) | 24.4 | 13.35 MB | none | none |
| `career` race (6,748 frames) | 25.3 | 13.72 MB | none | none |
| `career` replay (2,936 frames) | 30.5 | 14.73 MB | none | none |
| photo mode (1,312 frames) | 37.6 | 19.92 MB | the photo save only | none |

- In the races and the replay not one trapped page was read, by a guest thread or a host thread: every page stayed trapped from one readback to the next (`kept_pages` 10,253 a frame in the `photo` race, the 13.35 MB in all three views). All readback bytes there are unread, so a readback on demand removes the readbacks of the race completely: the 0.81 ms of GPU time of the readback copies at 2x (16.1) and their copy into guest memory on the command processor.
- The guest reads resolves in three places over both runs, all outside the race (132 destinations in `photo`, 130 in `career`, of about 300 each run): the photo save reads the full-screen destination at `1C0C1000` (3.77 MB, its latest of 10,337 readbacks, 920 page faults) and a 416 KB destination resolved once for it at `16709000`; during loading 128 destinations of 64 KB, each resolved once, are read once (2,048 faults); in the menus a 256 KB range at `1C0C1000` is read twice (64 faults each). Every first reader was a guest thread; no host thread read a trapped page. That covers only host code that reads through the guest views: the runtime's own readers go through the physical view (`TranslatePhysical`), which the trace does not see.
- Guest writes over resolved ranges before any read (memory reused for other data) happen in the menus and loading, 15 to 2,008 page faults a window; the write watches then work as without the trace.
- The trace costs little: the `photo` race ran at 164.3 fps with a p99 of 9.39 ms in this single run, against 172.1 fps and 8.71 ms in the perf gate `n3m2xp` (three runs); not a performance claim.

By the rule of 16.3, most read-back bytes are never read by a guest thread, so increment 2 (readback on demand) goes next (the main session's decision of 22 October 2026, 16.3). Its design has to serve the photo save, the loading reads and the menu read from the trap, and the runtime's own readers through the physical view (a shared memory upload of a range that holds only GPU data, the texture replacement's exclusion of resolved ranges) from the runtime's copy, without a guest fault.

### 16.5 Increment 2: readback on demand, 22 October 2026

`readback_resolve_on_demand` (off by default; set by `-Native`; on RTV only as a cvar; never on the ROV path, which keeps its readback, nor with `readback_resolve=none`) follows the main session's constraints of 16.3:

1. **A resolve skips its readback.** After the resolve copy, `IssueCopy_ReadbackResolvePath` records the destination as pending (address, length, pixel size, and at a draw resolution scale the scaled resolve range the resolve made current) and traps its pages in the three guest views (`Memory::ArmReadTrace`, the trap of 16.4). The data stays where the resolve put it: in shared memory at 1x, in the scaled resolve buffer at 2x. Every resolve does this, so the pending readback is always the latest resolve's; a page that another destination's readback restored is trapped again by the next resolve. Arming costs host protection calls, about 1 ms of the command processor's time a race frame at 29 resolves when every resolve armed again, so a destination whose trap is intact keeps it: no access, no readback and no disarm touched its pages since its arm, and the physical views' protection has not changed since (`Memory::physical_protect_epoch`, counted where an allocation, a protection change or a release sets host protection). No resolve reads back eagerly: a destination the CPU read every frame would cost one synchronous readback a frame, which the counts below would show (none does in the measured scenes).
2. **The first access waits for the readback.** A fault on a trapped page of a pending destination, read or write, joins or makes a request for it, asks the command processor for service (`CommandProcessor::RequestHostService`) and waits. The command processor serves requests at the start of each packet, in its idle loops (the ring and the host render queue) and in the `WAIT_REG_MEM` loop, so the guest thread it may be waiting for is served too. The service records the readback copies as the resolve path did (`RecordReadbackCopy`, the same downscale shader at 2x), awaits them (`AwaitAllQueueOperationsCompletion`), copies into guest memory with `SharedMemory::CopyReadbackToGuest` (only pages that still hold GPU data, so CPU data on the same pages stays) and then restores the whole range's protection. The faulting access is retried after that: a read finds the data, a write lands on top of it and meets its write watch as before. A page that a newer resolve trapped again in the meantime faults again for its own readback.
3. **Deadlock rules.** The fault handler releases the global memory lock before it waits and takes it again after; the command processor never takes the read trap's mutex while it holds the global lock. The command processor itself never waits for a readback: if it faults on a pending page (it reads guest memory through the physical view, so it should not), the page is restored without the readback and `cp_faults` counts it with an error. An access waits at most 5 s; then it continues with the data guest memory has and logs an error (`timeouts`).
4. **The runtime's own access to these ranges**, audited:
   - Shared memory uploads read the physical view, but only pages that are not valid on the GPU; a pending page is valid and GPU-written until something invalidates it. The upload counts pages whose readback was still pending (`physical_reads_pending`) to show this.
   - The texture replacement does not hash ranges that were ever resolved (`IsRangeEverResolved`), so it never reads a pending range.
   - Texture loads read shared memory or the scaled resolve buffer on the GPU, never guest memory.
   - A file read into a physical buffer (`XFile::Read`) writes through the physical view: it serves the pending readbacks of its range first (`Memory::ServeReadTrace`), so the file data lands on top.
   - Making physical memory writable (`PhysicalHeap::Protect`) invalidates the GPU's copy of the range: it serves the pending readbacks first too. Releasing or decommitting it (`PhysicalHeap::Release`, `Decommit`) discards them: the contents no longer matter, and a later readback skips the pages the new data invalidates.
   - The command processor's other readers of the physical view (the ring and indirect buffers, shader microcode, index buffers converted on the CPU, the draw extent estimator, memory writes of the command stream) read or write command, shader, index and fence memory, which the title never resolves to; they are not trapped and not counted.
   - XMA audio reads and writes its own buffers through the physical view.
5. **Counters** in the perf trace (`demand_readback`, [traces.md](../e2e/traces.md)): resolves deferred, requests, readbacks served and their bytes, the accessing threads' stall (sum and maximum), the command processor's service time, timeouts, command processor faults, failures, lost traps, uploads of pending pages and invalidations over pending pages.

### 16.6 Increment 2 results, 22 October 2026

The gate of 16.3 against RTV of the same binary (with the kept traps of 16.5), on `I:\FM4-build-usa` at 240 Hz with the launcher defaults (one tile with TAA), RTV and the readback on demand back to back:

| Gate | RTV | Readback on demand | Change |
|---|---|---|---|
| perf 2x, `photo` race (`n3d2xp`, `n3d2xpd`, three runs) | 155.5 fps, p99 9.50 ms | 176.7 fps, p99 8.31 ms | +13.6%, p99 -12.5% |
| GPU time a race frame at 2x | 6.25 ms | 5.51 ms | -0.74 ms (the readback copies were 0.81 ms in 16.1) |
| GPU time a photo mode frame at 2x | 10.09 ms | 8.78 ms | -1.31 ms |
| perf 1x, `photo` race (`n3d1xp`, `n3d1xpd`) | 216.2 fps, p99 6.44 ms, GPU 3.01 ms | 220.8 fps, p99 5.62 ms, GPU 2.37 ms | +2.1%, GPU -0.64 ms |
| smoke 2x races (`n3d2x`, `n3d2xd`) | `photo` 160.5, `career` 154.9, replay 105.6 fps | 176.5, 174.5, 115.0 fps | +10.0%, +12.6%, +8.8% |
| smoke 1x races (`n3d1x`, `n3d1xd`) | `photo` 213.1, `career` 225.0 fps | 210.9, 229.6 fps | within one run's noise |
| `parity.py` on the one-tile `freeze` | | 19.9% (1x) and 17.4% (2x) of the image above 4 | pass |
| smoke 1x with `-Native`'s cvars (`n3d1xn`, against `n3d1x`) | | races 237.5 and 224.9 fps | pass |

- No FAIL and no new error key in any cell; the WARNs are capture statistics of different race moments. The saved photos are intact at 1x and 2x (their thumbnails, and the full photo in `photo_view`); the races differ between runs, so the photo files cannot be compared with RTV's pixel for pixel.
- Readbacks on demand over all demand cells (271,909 frames): none in the races (89,399 frames), the replays and the frozen frames, so no stall there; 24 in photo mode (the photo save's two destinations, at most 8.9 ms); 3,156 in the menus and loading (at most 68 ms each), 17 of them for file reads; no timeout, no command processor fault, no failure, no lost trap, no upload or invalidation of a pending page.
- 99.9% of the deferred resolves kept their intact trap. The first build armed again at every resolve: about 1 ms of the command processor's time a race frame at 1x (its copy zone from 35 to 254 ms a second), which made the races slower than RTV at 1x; the kept traps removed it.
- The first build also recorded the readback on demand outside a submission when the command processor was idle, so the copy never ran and the photo save wrote garbage; the service opens a submission now. A photo check is part of every run of this path since.
- Whether RTV gets the readback on demand by default is the main session's decision after a soak (16.3).

### 16.7 Increment 1 design, 22 October 2026

Measured with the readback on demand (`n3i1m2xpd`, the perf gate at 2x, three runs, medians of the race windows): the GPU time a race frame is 5.44 ms, of which the resolves take 0.72 ms and the texture loads 0.58 ms. The resolves and loads of at least 1280x720 (new timestamp kinds `resolve_full_ms` and `load_full_ms` in the perf trace) take 0.32 and 0.33 ms: increment 1's ceiling is about 0.65 ms. The frame graph at 2x (`n3i1-fg2x-career`) shows the full-screen destinations of a race frame: the scene depth at EDRAM base 720 resolved to `8_8_8_8` at `1C0C1000` twice, the scene colour (`2_10_10_10_FLOAT`) resolved to `2_10_10_10` at `1B811000` twice, and the final colour (`2_10_10_10`) at `1B479000`; one full-screen pass reads the depth and the first colour resolve, the last pass the depth and the second.

Today each of them takes three GPU steps: the dump of the render target into the EDRAM buffer and the resolve copy shader into the scaled resolve buffer (the copy), then the texture load from the scaled resolve buffer into the texture the next pass samples (the load). Increment 1 writes that texture straight from the render target with a conversion compute shader per format: the depth packed as the guest's `8_8_8_8`, and the float colour as `2_10_10_10` with the guest's exponent bias. Two ways to do it:

| | A: skip the load | B: skip the copy and the load |
|---|---|---|
| Saves at most | 0.33 ms | 0.65 ms |
| Scaled resolve buffer | written as today | not written; the host texture holds the data |
| Readback on demand | unchanged (from the scaled resolve buffer) | from the host texture, on the CPU: copy the texture to a readback buffer, then downscale, tile and pack in the guest's layout on the CPU, only when a guest access asks for it |
| Other GPU readers of the range (a texture of another format or size over it) | unchanged | the scaled resolve buffer is filled from the host texture first, the same way through the CPU; counted, expected none in races |
| New shaders | 2 (the conversions) | 2 (the conversions); the readback shader of the earlier plan is CPU code instead |

With the readback on demand, the third shader of the earlier plan (a re-tile from the host texture for the readback) is no longer needed in either way: in A the readback stays on the scaled resolve buffer, in B the rare readbacks (the photo save, the loading reads) and fills run on the CPU. A check mode keeps the old path beside the new one on a few frames and compares the converted texture with the loaded one, and in B the CPU's guest layout with the resolve copy's bytes.

The main session decided on 22 October 2026: A first, then B, as two gated steps of increment 1. B builds on A's two conversion shaders; A proves them and their byte check while the scaled resolve buffer still backs every other reader, so B's only new risks are the CPU's tiling, packing and endianness and the fill for other GPU readers. A's gate: the check mode shows the converted texels byte-equal to the old load path on sampled frames (1x and 2x; race, replay, menus); smoke, perf and parity against RTV; the GPU time saved at 2x. B's gate: the CPU readback byte-equal to the resolve copy's bytes in the check mode (the photo save's two destinations and the loading ones); the fill for other GPU readers counted, and none in races; the photo save intact; the same gates as A, its saving measured against A. If B's CPU path turns out large or fragile, increment 1 stops at A with its numbers recorded and increment 3 goes next.

### 16.8 Increment 1, step A results, 22 October 2026

Step A is `native_rt_resolve` (off by default, not in `-Native`): after the resolve copy, a full-screen 32bpp resolve (1x MSAA, at least 1280x720, destination offset 0, `8_8_8_8` or `2_10_10_10`) writes the host textures that sample its destination straight from the EDRAM buffer with `resolve_texture_32bpp` (FXC from `fm4-resolve`): the samples read and packed as the resolve copy does (bitwise with the fast copy's red and blue swap, or converted as the full copy), the destination's endianness and then the texture's applied, stored through a UINT view of the texture's format. The targets are the textures that already exist for the destination's key (both `packed_mips` keys, every endianness, the resolve's scaling), created with unordered access; they are marked loaded and watched, so the texture cache does not load them from the resolved memory. The scaled resolve buffer is still written, so the readback on demand and every other reader are unchanged. `native_rt_resolve_check=N` writes scratch copies every N-th frame instead, keeps the loads, and compares the loaded texels with the scratch bytes.

The gate on `I:\FM4-build-usa` at 240 Hz, against the same binary with only the readback on demand (`n3a*`):

| Check | Result |
|---|---|
| Byte check, every 30th frame | 6,750 textures compared, all equal: `career` at 1x (2,125) and 2x (1,670), races, replay and menus; `photo` at 1x (1,615) and 2x (1,340), race and photo mode. The first build differed in 629 checks of the `2_10_10_10` destination at `1B0E1000` with red and blue swapped: the fast copy swaps them for color, which the shader now does too |
| Destinations found | all full-screen resolves after the first frames (2 to 4 misses a run); 1.48 textures written a resolve on average (both `packed_mips` keys) |
| GPU a race frame at 2x (perf, three runs, medians) | loads of at least 1280x720 0.32 to 0.13 ms, resolves of at least 1280x720 0.32 to 0.52 ms: frame 5.34 against 5.48 ms, no saving |
| perf 2x `photo` race | 183.2 fps, p99 7.89 ms against 178.9 fps, p99 8.17 ms (-2.4%); the baseline's run 2 missed the "SAVE SUCCESSFUL" text by OCR while the screen showed the save succeeded |
| smoke 1x, 2x | pass with no new error key; races within -3% to +1%; the WARNs are capture statistics; the saved photos are intact (`photo_view`) |
| `parity.py` on `freeze` | pass at 1x (35.1% of the image above 4) and 2x (28.4%) |

Step A is correct but saves nothing: writing the texture from the EDRAM buffer costs about what the load from the scaled resolve buffer cost (both move a 2560x1440 32bpp image), and the resolve copy and the dump stay. The saving of increment 1 needs work removed, not moved: a conversion straight from the host render target (with the dump's encoding of the guest formats, proven by the same byte check) in place of the dump, the resolve copy and the load, with step B's CPU readback for the rare guest reads.

The main session decided on 22 October 2026 to close increment 1 at step A, off by default, and to go on with increment 3: step A shows that moving the work saves nothing, and the saving needs the resolve straight from a host render target. Increment 3 makes the render targets surface-keyed host targets, so a resolve becomes a copy or conversion from that host surface (section 8, the resolves row) rather than from the EDRAM buffer; building the conversion from today's render targets now would replicate the EDRAM dump's encodings (float24 or unorm24 depth with stencil, 7e3 color) that increment 3 replaces. The conversion therefore belongs to increment 3's design, which reuses step A's shader, check mode and target lookup.

### 16.9 Increment 3 design: surface-keyed host targets, 22 October 2026

Measured on `I:\FM4-build-usa` at 2x and 240 Hz with the readback on demand (`n3s3-career-2x`, the `career` scenario with `REX_FRAME_GRAPH` and `REX_TRANSFER_DRAW_TRACE` every 60th frame, 286 race frames with at least 1,000 draws), and the render target cache's CPU time at 1x from the step A gate's baseline (`n3a1x`):

| Per race frame | |
|---|---|
| Passes | 85 |
| Surfaces (distinct base, format, pitch, MSAA) | 16 (8 to 17) |
| Resolves | 28 |
| Ownership transfers | 20 (7 to 22), about 2,480 tiles |
| Transfers whose draw overwrites the claimed rectangle (uniform draw check 9 or 10) | 95% of the tiles (2,364) |
| Transfers whose draw may read the old contents (check 3, alpha; 11, not evaluated) | 5% of the tiles (114) |
| GPU time in passes that only transfer, at 2x (16.1) | 0.25 ms |
| Render target cache CPU time at 1x (`draw_render_targets`) | 0.31 ms, and 0.13 ms in the resolves (`copy`) |

The surfaces seen in every race frame: the scene colour `2_10_10_10_FLOAT` at base 0 with pitch 1280 (also viewed as `2_10_10_10_FLOAT_AS_16_16_16_16`, and `2_10_10_10` / `2_10_10_10_AS_10_10_10_10` for the final image) and its depth `D24FS8` at base 720; 4x MSAA depth `D24S8` at base 0 with pitches 640 and 520 (with `16_16` 4x colour at base 832, the 512x512 resolve); the shadow and post chain at pitches 560 and 320 (`8_8_8_8` at bases 0 and 288, `D24FS8` at base 64, `D24S8` at base 0); a `32_FLOAT` 80-pitch surface at base 0 (the exposure); in some frames the mirror at pitch 400 (2x MSAA) and a 4x pitch 280 pass. Base 0 is shared by nine of them, which is where the transfers come from: the largest per frame are the 4x depth at base 0 into the scene colour (3.0, 158 tiles), the scene colour into the 4x depth (2.0, 160 tiles), the scene depth at 720 into the 4x `16_16` colour at 832 (1.0, 597 tiles) and the `2_10_10_10` final colour into the depths at base 0 (1.5, 556 tiles). The draws that trigger them are clears and full-screen passes that overwrite their rectangle; the transferred tiles are the parts of the claimed range outside it (the cutout already skips the overwritten rectangle).

Every resolve's source is the surface bound at base and format, read today through the dump of its host render target into the EDRAM buffer (16.7).

The design:

1. **Surfaces keep their host targets; transfers become lazy.** The ownership map keeps which surface last wrote each EDRAM tile range. Binding a surface over tiles another surface wrote no longer copies them at once: the tiles are marked stale for that surface. A draw that overwrites its rectangle (the uniform draw check) clears the stale mark inside it without a copy; a draw or resolve that reads stale tiles (blending, a depth or stencil test, a resolve of them, a pass that samples nothing but keeps the old values) gets the transfer of exactly those tiles first. That keeps the EDRAM semantics for the 5% that read, and drops the copies nobody reads.
2. **Resolves from the host surface.** A resolve reads its source surface's host target directly with a conversion to the guest format (step A's shader with a render target source, the dump's encodings of float24 and unorm24 depth with stencil and of 7e3 color, proven with step A's byte check), in place of the dump, the resolve copy and, through step A, the load; the scaled resolve buffer then comes from the host texture only when another reader needs it (step B of 16.7).
3. **The N1 writers.** The title's resolve writer becomes a record that names the source surface (its object, base and format) and the destination, so the host knows the surface without decoding the copy registers; the tiling bracket writers belong to increment 4 (one-pass tiles).

Steps, each behind `native_rt_surfaces` (off) with a step gate: (1) a count-only mode that marks stale tiles instead of nothing and counts, per transfer the cache performs today, whether its tiles are read before they are overwritten (the ceiling of the lazy transfers, as 16.4 did for the readbacks); (2) the lazy transfers; (3) the resolves from the host surface with the byte check; (4) the resolve writer as a record. The close gate follows step 4, or an earlier step if the numbers end the increment.

### 16.10 Increment 3, step 1: which transfers are read, 22 October 2026

`native_rt_surfaces=1` counts, without changing anything, how the tiles of each ownership transfer end ([traces.md](../e2e/traces.md)): taken by another render target before anything touched them, overwritten (a draw's overwrite rectangle covers them wholly, or a resolve clears them), or read (a draw that is not a uniform overwrite touches them within its scissor, or a resolve copies them). Measured on `I:\FM4-build-usa` at 2x with the readback on demand (`n3s1-count-career-2x`, one `career` run), per frame:

| | Race (7,762 frames) | Replay (3,106 frames) |
|---|---|---|
| Transfers | 18.5 | 21.8 |
| Tiles transferred, and left after the cutout | 2,382 and 2,289 | 2,750 and 2,657 |
| Taken by another render target unread | 228 | 249 |
| Overwritten | 1,019 | 1,318 |
| Read by a draw (an upper bound) | 999 | 1,059 |
| Read by a resolve | 43 | 30 |
| Transfers with no tile read, and with some | 3.3 and 15.3 | 3.3 and 18.5 |

At least 55% of the transferred tiles in the race (1,247 of 2,289) are never read, so lazy transfers remove at least that share of the copies; the reads by draws are counted whenever a draw that may read has the tile within its scissor, which the full-screen scissors of the 3D passes make an overestimate.

The main session approved the design of 16.9 on 22 October 2026 with four corrections. (1) A stale tile counts as overwritten only when every bit of it is written: the colour write mask covers every channel of the surface (for depth, depth writes on with a full stencil write mask, or no stencil in the format), blending off or reading no destination, no MSAA sample mask, the scissor, viewport and window offset covering it, and for depth a test that cannot reject (function always) or a clear; anything less is a read and gets the transfer first. The overwrite share is to be reported under this rule as well as under the uniform draw check (codes 9 and 10). The "overwritten" row above uses the uniform draw check: a draw's overwrite rectangle comes only from draws that pass `GetDrawOverwrite` (primitive, polygon offset and sample mask, alpha test, pixel kill, depth and stencil function, blending, colour mask) with an estimated covered rectangle. (2) Keeping every surface's host target costs memory: the extra host render target memory at 1x, 2x and with 4x MSAA is to be reported, with a cap and LRU eviction, where an eviction forces the transfer that lazy mode skipped. (3) `-Rov` keeps its own path unchanged; `native_rt_surfaces` applies only with `-Native` and the RTV path. (4) The byte check of step 3 covers float24 and unorm24 depth with stencil, 7e3, and the `8_8_8_8` and `2_10_10_10` destinations, at 1x and 2x, in the race, the replay, photo mode and the menus.

The main session accepted step 1 and decided on 22 October 2026 that step 3 (the resolves from the host surface) comes before step 2 (the lazy transfers): its ceiling is larger (about 0.45 ms at 2x against 0.14 to 0.25 ms), and it is on the critical path of increment 4, whose one-pass tiles and native MSAA need resolves that read the MSAA host surface directly, since a dump of MSAA tiles into the EDRAM buffer is what increment 4 removes. The lazy transfers can follow at any time; the overwrite share under the strict rule and the memory of keeping every surface come with step 2 at the latest.

### 16.11 Increment 3, step 3: resolves from the host render target, 22 October 2026

`native_rt_surfaces` is a set of bits since this step: 1 the count of 16.10, 2 the resolves from the host render target. With bit 2 a 32bpp resolve (the fast copy or `resolve_full_32bpp`) whose source tiles one host render target owns with the same pitch, sample count and format reads that render target with `resolve_host_32bpp` (FXC from `fm4-resolve`, twelve builds: float, uint and depth sources, single- and multisampled, unscaled and scaled) instead of its dump into the EDRAM buffer: each sample is encoded as the dump stores it (unorm24 depth rounded to the nearest even and float24 depth as `PreClampedDepthTo20e4` with the host depth in [0, 0.5), each with stencil; 7e3 color as `PreClampedFloat32To7e3` with alpha in two bits; fixed point; 16_16 and 32_FLOAT as their bits), then copied bitwise with the fast copy's red and blue swap, or unpacked, averaged, biased and packed as the full copy does, into the same destination. The stencil comes through a root table of its own, because one-use descriptors are not contiguous with bindless resources (the first build read the wrong descriptor for it). `native_rt_surfaces_check=N` resolves as before every N-th frame, then into scratch that starts with the destination's bytes, and compares both. The unscaled fast copies of multisampled sources stay on the dump: every 4x depth resolve of photo mode's tiles at 1x differed (the diagnostic below), while the scaled ones match.

The step gate on `I:\FM4-build-usa` at 240 Hz with `-Native`'s cvars and bit 2, byte check every 30th frame:

| Run | Byte checks | Mismatches | Source formats checked |
|---|---|---|---|
| `career` 2x (race, replay, menus) | 16,697 | 0 | 8_8_8_8 3,948, 2_10_10_10 1,000, 2_10_10_10_FLOAT 9,351, 16_16 536, 32_FLOAT 618, D24S8 516, D24FS8 728 |
| `career` 1x | 19,743 | 0 | 8_8_8_8 4,871, 2_10_10_10 1,194, 2_10_10_10_FLOAT 10,864, 16_16 639, 32_FLOAT 723, D24S8 618, D24FS8 834 |
| `photo` 2x (race, photo mode) | 13,708 | 0 | 8_8_8_8 2,802, 2_10_10_10 948, 2_10_10_10_FLOAT 7,796, 16_16 439, 32_FLOAT 520, D24S8 403, D24FS8 800 |
| `photo` 1x | 14,951 | 0 | 8_8_8_8 3,242, 2_10_10_10 1,044, 2_10_10_10_FLOAT 8,633, 16_16 496, 32_FLOAT 583, D24S8 463, D24FS8 490 |

The destinations are `8_8_8_8` (the depth resolves), `2_10_10_10` and the rest of the title's 32bpp formats. In the race 25.0 of 28.8 resolves a frame come from the host render target; 3.8 a frame cannot (other copy shaders or several owners). At 1x photo mode's 4x MSAA depth resolves keep the dump (above).

The race at 2x (`race` scenario, one run each, the `-Native` cvars): without bit 2 (`n3h-race-native0-2x`) 182.3 fps, p99 7.83 ms, GPU 5.41 ms a frame with 0.72 ms of resolves; with bit 2 (`n3h-race-native-2x`) 190.2 fps, p99 7.27 ms, GPU 5.16 ms with 0.48 ms of resolves (the full-screen ones 0.32 to 0.18 ms): 0.25 ms of GPU time a frame less. RTV with the launcher defaults (`n3h-race-rtv-2x`) ran 155.4 fps, GPU 6.28 ms, at 2,520 against 1,881 draws a frame (another race load).

Host render target memory, for step 2: the render target cache already keeps every render target key it made until `ClearCache`, each covering the whole EDRAM from its base (2,048 tiles): the 16 surfaces of a race frame take about 203 MB at 1x and 810 MB at 2x today (2_10_10_10_FLOAT and D24FS8 at 8 bytes a sample), so keeping every surface's target alive adds nothing; sizing them to the surfaces' extents would save most of it. A one-pass 4x MSAA scene (increment 4) needs 1280x720 with 4 samples, which the EDRAM-sized targets cannot hold: about 28 MB for colour and 28 MB for depth at 1x, 112 MB each at 2x.

The main session accepted step 3 on 22 October 2026 and decided: the close gate runs now with bit 2 on in `-Native`, so the gain lands in main and soaks beside the readback on demand; step 2 (the lazy transfers, with the overwrite share under the strict rule of 16.10 first) follows, then step 4, then increment 4. Two items stay open here. First, the unscaled fast copies of 4x MSAA sources and the resolve from the host render target pick different samples for photo mode's tile depth at 1x, so one of them is wrong; which one is to be found against `-Rov`'s output of the same depth, and if the precompiled path is wrong, it is an existing RTV bug for [docs/status/current.md](../status/current.md), not fixed in this increment unless trivial. Second, the memory: every host render target covers 2,048 tiles from its base, 810 MB for the race's 16 surfaces at 2x; sizing them to the surfaces' extents (a 1280x720 scene target at 8 bytes a sample takes 7.4 MB at 1x and 29.5 MB at 2x, against 21 MB and 84 MB for an EDRAM-sized one) belongs to step 2 or increment 4, where 4x MSAA at 2x and the Ally X's shared memory make it matter.


The first open item was time-boxed by the main session to one diagnostic build and one run. The diagnostic `native_rt_surfaces_unscaled_msaa` lifts the exclusion. The `photo` run at 1x with a byte check at every frame (`msaa-photo-1x`, 22 October 2026) compared:

- 9,120 bitwise 4x MSAA depth resolves (D24FS8, sample 0, photo mode's three tile destinations of 1280x256 and 1280x208). 9,084 differ, in 30,710 words each on average (about one word in ten); 36 match.
- 42,661 full copies of 4x MSAA sources, which average the samples, and every 2x MSAA copy. All of these match, so both sides address the same pixels.

The differing depths are close: the 24-bit values differ by about 0x70, by the same amount along a row and with the copy always the larger. That is what another sample of the same sloped surface gives, or a different depth conversion of the same sample. The words do not appear elsewhere on the other side, so this is no displacement. By their code, the dump (EDRAM sample bit 0 of x to host sample bit 0, bit 0 of y to host sample bit 1), the precompiled fast copy (guest sample bit 1 horizontal, bit 0 vertical, the same at scale 1 in the unscaled and scaled builds, which `-Rov` also uses) and `resolve_host_32bpp` all take host sample 0 for guest sample 0. The cause is not found within the time box, and nothing shows the precompiled path to be wrong, so no RTV bug is recorded. These copies keep the dump, which is correct and covers only photo mode at 1x; the diagnostic cvar stays for a later look. The main session reads the signature (a constant offset along each row of about 112 float24 ulps, the copy always the larger, the averaged copies all equal) as a different host sample in the vertical direction on a sloped depth plane, not a conversion. The first thing to check later is which host sample the 4x depth dump reads for guest sample 0: the dump's sample index or swizzle on the depth path, and any detour through a host depth store, not the copy shader. Which side is right needs a byte comparison against `-Rov` in photo mode at 1x.

The close gate of increment 3 on `I:\FM4-build-usa` at 240 Hz, on the binary of `d9a0c9f` (one binary for RTV and `-Native`'s cvars with bit 2, 22 October 2026):

| Gate | RTV | `-Native`'s cvars |
|---|---|---|
| perf 2x (`n3c2xpr`, `n3c2xpn`), race, three runs | 158.4 fps, p99 9.29 ms, GPU 6.13 ms a frame, resolves 0.68 ms | 190.1 fps, p99 7.68 ms, GPU 5.16 ms, resolves 0.47 ms; `compare.py` PASS |
| smoke 2x (`n3c2x`, `n3c2xn`), races | `photo` 166.0, `career` 157.6 fps | 188.4, 183.3 fps |
| smoke 1x (`n3c1x`, `n3c1xn`), races | `photo` 233.0, `career` 218.9 fps | 234.8, 224.1 fps |
| `parity.py` on `freeze`, 1x and 2x (`n3c-freeze-*`) | | PASS, 22.3% and 31.1% of the image above 4 (35.1% and 28.4% at the close of increment 2) |
| identity check with perturbation (`n3c-perturb-career`) | | 13,525 ranges perturbed, 13,525 reported |

Every cell reached its goal with RTV's error keys; the smoke and perf warnings are capture statistics of races that differ (the `photo_view` photos checked by eye). With bit 2 the race resolves 26.1 times a frame from the host render target at 1x and 2x (3.75 a frame cannot), the replay 33.0 and photo mode 39.2. The readback on demand had no timeout, CP fault or lost trap; the races read nothing back. The first RTV perf cell (`n3c2xp`) lost its third run to a guest access violation (a write of guest address 0 while the race loaded, on RTV's defaults, which bit 2 does not touch); the repeated cell ran three of three, and the crash did not recur in the other 25 runs of the gate. The crash handler's dump (`I:\FM4-build-usa\guest-null-crash.dmp`), symbolized by the main session against the snapshot's map file, puts the fault in guest code: `sub_82956DA8`, guest instruction `0x8295720C` (`stwx r11,r10,r18`). The function loads an object from its r6 argument; the buffer pointer at offset 160 of that object is null, so the store writes 0 to guest address 0. A game object's buffer was not set yet while the race loaded, either a load-order race between guest threads or an allocation that returned 0. Nothing points at the GPU path. If it recurs, the runtime log near the crash is to be checked for a failed allocation.

### 16.12 Increment 3, step 2 design: lazy ownership transfers, 22 October 2026

**The strict overwrite share.** Every condition of the strict rule of 16.10 is already a condition of the overwrite count. `GetDrawOverwrite` requires a full sample mask (`PA_SC_AA_MASK` 0xF), no polygon offset, no alpha test or alpha to coverage, no pixel kill or memexport, depth and stencil functions always, blending ONE, ZERO and ADD for colour and alpha, a colour write mask over every channel of the surface's format, and for depth and stencil depth writes with stencil REPLACE and a write mask of 0xFF on both faces. `EstimateCoveredRectangle` applies the viewport, the window offset and the half-pixel offset, rejects user clip planes, geometry clipped by the near or far plane, and culled or degenerate triangles, and accepts only rectangles or one quad that tile their box. The cutout is that box intersected with the scissor, counting only pixels with every sample inside. The tiles overwritten under the strict rule are therefore the 1,019 of 2,289 transferred tiles a race frame of 16.10 (44.5%); with the 228 taken by another render target unread, at least 54.5% of the transferred tiles need no copy at the time of the transfer.

**Today.** When a draw binds a render target, `ChangeOwnership` claims the tiles of its range and lists a transfer for each run of tiles another render target owned. The transfers outside the overwrite cutout are copied before the draw, by a draw per source in `PerformTransfersAndResolveClears`. Every render target key stays alive until `ClearCache`, which keeps only the render targets that own tiles.

**Lazy mode** (`native_rt_surfaces` bit 4, off by default, only with the RTV path):

- **The holder.** A per-tile table records, for each tile whose transfer was deferred, the render target that still holds its data (the holder), and for depth with a different host encoding the host depth holder. A tile owned by X with holder S has its data in S's pixels; S's pixels there stay valid because S only draws into tiles it claims.
- **The transfer at claim time.** Before the claims, the deferred tiles of the claimed ranges that another render target owns leave their holder, so that a claim that lists no transfer for them (a range wholly inside an overwrite cutout or a clear) cannot leave a stale holder behind. Only the transferred tiles that the claiming draw may touch are copied now. These are the tiles within its scissor, and for a draw with a covered rectangle (a uniform draw) within that rectangle; the overwrite cutout then drops those it covers. The rest are deferred, with their holder set to the transfer's source or to the holder they left.
- **Chains.** When a tile with holder S is claimed again, its transfer comes from S, not from the current owner. When S claims it back, the deferral ends with no copy, because the data is already in S.
- **Flushes.** The deferred transfers are copied into the owner, grouped by holder, before anything reads the tile:
  - a draw on the owner that is not an overwrite of the tile and has it within its scissor;
  - a resolve whose copy span includes the tile (before the dump or the resolve from the host render target); a resolve's clear copies the partly cleared tiles from their holder in its own transfers;
  - any dump of the tile into the EDRAM buffer;
  - `ClearCache` and the destruction of a holder.
- **Ending a deferral without a copy.** An overwrite draw's cutout or a resolve clear covering the tile wholly ends the deferral without a copy.
- **First version.** Transfers with a separate host depth source and transfers into depth render targets whose host encoding differs (float24 kept in 32-bit floats) stay eager. They carry the host depth precision, which the ownership map tracks per range.
- **Memory.** Lazy mode keeps no extra render target alive: holders are keys that exist until `ClearCache` today, which then also keeps every holder. The memory cap with LRU eviction (an eviction flushes the evicted holder's deferred tiles) belongs to the sizing of host targets to surface extents, decided before increment 4.

**Counters** (perf trace `lazy_transfers`, in tiles):

- copied at the claim;
- deferred, and deferred again for a new owner;
- ended without a copy: claimed back by the holder, or inside an overwrite draw's cutout;
- ended with a copy: into a new owner that reads them, or at a flush by the kind of reader (draw, resolve copy, resolve clear, post pass);
- still deferred at the end of the window.

The GPU time of the transfers is a timestamp kind of its own (`gpu.transfer_ms`, which includes the copies inside resolves).

**Check mode** (`native_rt_surfaces_lazy_check`). After each draw's flush, every tile the draw may touch within the range it claims must have no deferral left (violations). No tile it may touch beyond that range may be held by the drawing render target itself, which would draw into data it holds for another owner without claiming it (hazards). `native_rt_surfaces_lazy_perturb=N` skips the copy of every N-th deferred tile that a draw reads, so the check must report exactly the skipped ones. Resolves and post passes take every deferred tile of what they read before they read it.

**Expected gain.** The transfers are about 0.14 to 0.25 ms of GPU time a race frame at 2x (16.10); lazy mode removes the copies of at least 54.5% of the tiles and the draws of the transfers whose tiles are all deferred.

**Step gate.** The `career` check run with the transfer check and its perturbation proof; one 2x race perf run with lazy mode, against RTV's `n3c2xpr`; the freeze parity at 1x and 2x, because the image can change if a flush is missed.

### 16.13 Increment 3, step 2 results: lazy ownership transfers, 23 October 2026

`native_rt_surfaces` bit 4 is built as designed in 16.12 and stays off by default, also in `-Native`. The step gate ran on `I:\FM4-build-usa` at 240 Hz and 2x, with `-Native`'s cvars and `native_rt_surfaces=6`.

The first build (`n3l-*`) had two faults:

- Its check reported 224 violations a frame. A claim lists no transfer for a range wholly inside an overwrite cutout or a resolve clear, so such tiles changed their owner while still recorded as deferred. A later claim would then have copied the holder's stale data over the overwritten tile.
- It deferred only 193 tiles a frame, because it took the scissor as the region a uniform draw touches.

The second build (`n3m-*`, the commits of this step) fixes both. Deferred tiles leave their holder before every claim, and a uniform draw touches only its covered rectangle.

| Run (`n3m-*`) | Result |
|---|---|
| `career` with `native_rt_surfaces_lazy_check` | 0 violations, 0 hazards; race 187.5 fps |
| `career` with the check and `native_rt_surfaces_lazy_perturb=1000` | 26,630 copies skipped, 26,630 violations reported, 0 hazards |
| `race` with bit 4, against bit 2 alone (one run each) | 191.5 against 182.9 fps, GPU 5.15 against 5.34 ms a frame (1,658 against 2,404 draws a frame, another race load); transfers 0.306 against 0.328 ms a frame |
| `parity.py` on `freeze` against RTV's `n3c-freeze-*-rtv`, 1x and 2x | PASS, 25.4% and 30.7% of the image above 4 (22.3% and 31.1% without bit 4) |

In the race, a frame claims 2,216 tiles from another owner:

- 434 are copied at the claim.
- 1,782 are deferred, and about as many deferrals end in the same frame:
  - 1,004 (56.3%) end without a copy: 143 claimed back by their holder, 861 inside an overwrite draw's cutout;
  - 778 end with a copy: 768 before a draw touches them, 9 into a new owner, 1 before a resolve.
- 14 are deferred again for a new owner.

The copies of tiles fall by about 45%, but the GPU time of the transfers falls by only 0.022 ms a frame, under the 0.05 ms that the main session set for turning the bit on. Bit 4 therefore lands off, for the record, with its check.

**Why the transfers cost 0.33 ms rather than 0.14 to 0.25 ms.** The estimate of 16.10 scaled the transfer time by tiles. A race at 2x with the transfer inventory (`native_rt_surfaces_transfer_inventory`, `n3i-race-2x`, bit 2 without bit 4) shows where the time goes. It has 19.5 transfers and 2,384 tiles a frame, and 0.346 ms of transfers a frame. 0.309 ms (89%) of that goes into depth render targets, which receive only 1,320 of the tiles (55%). This GPU has no stencil reference output from the pixel shader, so every transfer into a depth target draws its depth and then its stencil one bit at a time: nine passes over its rectangles, against one for a colour target. The race's transfers a frame:

| Destination (format, samples) | Source | Transfers | Tiles |
|---|---|---|---|
| 16_16, 4x | D24FS8, 1x | 1 | 608 |
| D24S8, 4x | 2_10_10_10, 1x | 1 | 496 |
| D24S8, 1x | 2_10_10_10, 1x | 1 | 224 |
| D24S8, 4x | D24S8, 1x | 1 | 224 |
| 2_10_10_10_FLOAT, 1x | D24S8, 4x | 4 | 160 |
| D24S8, 4x | 2_10_10_10_FLOAT, 1x | 2 | 160 |
| 8_8_8_8, 1x | D24S8, 4x | 2.25 | 152 |
| D24S8, 4x | D24FS8, 1x | 1 | 112 |
| D24FS8, 1x | D24S8, 4x, with a host depth source | 1.5 | 64 |
| 8_8_8_8, 1x | 8_8_8_8, 1x (another surface) | 1 | 62 |
| 8_8_8_8, 1x | D24FS8, 1x | 0.75 | 40 |
| 8_8_8_8, 2x | 8_8_8_8, 1x | 0.5 | 40 |
| D24FS8, 2x | 8_8_8_8, 1x and D24S8, 4x, with a host depth source; D24FS8, 1x | 1.5 | 40 |
| 32_FLOAT, 1x | 2_10_10_10_FLOAT, 1x | 1 | 2 |

The transfers are the title's reuse of the same EDRAM for depth and colour and for 1x and 4x surfaces, such as a scene depth read back as colour and a colour surface reused as the next pass's depth. The one-pass scene of increment 4 removes most of them with the tiles that alias. What remains costs mainly through the stencil bit passes, which a GPU with stencil reference output (`PSSpecifiedStencilRefSupported`) does not need. Lazy transfers can avoid only the share no one reads, and in the race that is small in time.

The main session's decisions of 23 October 2026: bit 4 lands off; the transfers that remain after increment 4 are judged from its own inventory. Writing the stencil from the pixel shader on GPUs with `PSSpecifiedStencilRefSupported` (no stencil bit passes) is an optional later item, not worked on now.

### 16.14 Step 4 deferred, 23 October 2026

The main session's decision of 23 October 2026: step 4 of 16.9, the title's resolve writer as a record, leaves stage N3 and is deferred to the API-level stage, where draws become records too. The measurements behind it:

- The writer's body (`sub_82348DC8`) is about 2,550 lines of recompiled PPC, against a few dozen for the dirty writers of N1. A record built from its arguments would have to reproduce every register it writes (surface, copy, window and viewport state, the rectangle's draw); a record made by running the body into scratch and decoding it moves the work instead of removing it.
- In a race at 2x on the native path (`n3i-race-2x`), the whole resolve execution (the command processor's `copy` zone) takes 0.142 ms of the render thread's 4.35 ms a frame. The PM4 that a record would replace is about 30 resolves of about 37 dwords, about 1,100 dwords a frame, under 0.02 ms of decoding.
- The host already finds a resolve's source surface from the copy registers (`FindHostResolveSource`, 16.11), which is what increment 4's resolves consume.

Whether increment 4 needs the tiling bracket writers to find the tile loop is decided in its design note.

### 16.15 Host render targets sized to their surfaces: design, 23 October 2026

The main session's decision of 23 October 2026 puts this before increment 4: a one-pass 4x MSAA scene of 1280x720 does not fit an EDRAM-sized host target (16.11), and the race's 16 surfaces take 810 MB at 2x. With `native_rt_surfaces` bit 8 (off, RTV path only):

- **Size.** A host target is created with the tile rows the creating draw or clear needs, rounded up to a multiple of 8 tile rows (a 720-line scene at 1x takes 48 of its 128 EDRAM rows). Every target a draw binds is sized to the draw's rows, not only to the range the target claims: the render area of a draw is the intersection of its views, so a target cut short by the next target's base must not clip the others.
- **Growth.** When a later draw or clear needs more rows, a new target of the same key replaces it. It has the larger height (at least half as large again, at most the EDRAM rows). All tiles of the old target are copied into it by an ownership transfer of the same key, which is bit-exact: colour keeps its bits through the transfer format; depth keeps the host float32 value, because the formats are equal, and the stencil. The old target is destroyed once the GPU has finished the submissions that use it. Every reference moves to the new target: the cache's map, the bound and accumulated targets, the holders of deferred tiles, and the transfers kept pending.
- **Memory.** Every target's allocation is counted. The perf trace's `rt_memory` gives the targets, the MB in use and the peak MB, and the growths and evictions of the window.
- **Cap.** With `native_rt_surfaces_memory_cap_mb=N`, the cache destroys the least recently bound targets while it is over the cap, at the start of a draw's update. Only targets that own no tile are destroyed: not a host depth source of any range, and not bound in the last update. Their deferred tiles as a holder are copied into their owners first (16.12). A target that owns tiles stays, so the cap can be exceeded.
- **Step gate.** The `career` check run with bit 8 (the lazy check where bit 4 is on as well); the target memory at 1x and 2x before and after, in the race and the replay, peak and steady; one 2x race perf run per side; the `freeze` parity at 1x and 2x, since a target's extent can clip.

### 16.16 Host render targets sized to their surfaces: results, 23 October 2026

Bit 8 is built as designed in 16.15 and stays off by default. The step gate ran on `I:\FM4-build-usa` at 240 Hz, with `-Native`'s cvars and `native_rt_surfaces=10` (bits 2 and 8) against `native_rt_surfaces=2`. The memory is that of the host render targets (`rt_memory`); it does not shrink, so the steady value of a phase is also the run's peak after the menus:

| Run (`n3z-*`) | Targets | Memory, without bit 8 | Memory, with bit 8 |
|---|---|---|---|
| `career` 1x, race and replay | 43 | 589.8 MB | 195.5 MB (-67%) |
| `career` 2x, race and replay | 43 | 2,142.2 MB | 564.5 MB (-74%) |
| `race` 2x | 42 | 2,102.2 MB | 548.5 MB (-74%) |

- **Checks.** The byte check of the resolves from the host render target (`native_rt_surfaces_check=30`) compared 19,636 resolves at 1x and 16,552 at 2x with no mismatch, after the targets were sized and grown. Each run grew one target. `parity.py` on `freeze` against RTV's `n3c-freeze-*-rtv` passes at 1x and 2x, with 20.8% and 29.0% of the image above 4 (22.3% and 31.1% without bit 8). Every run reached its goal with RTV's error keys.
- **Cap.** With `native_rt_surfaces_memory_cap_mb=200` at 2x (`n3z-career-2x-cap`), the cache kept 19 to 20 targets and 185 to 197 MB in the race and the replay, with a peak of 323.5 MB. To stay there it evicted 3,029 targets in the run, and they were created again. The working set of the targets that own tiles is about 550 MB at 2x, so a cap below it trades memory for re-creations. No byte check differed.
- **Performance.** Three race pairs at 2x (`n3z-race-2x-*`), one run each, the race's load between 1,460 and 2,114 draws a frame:

| | Without bit 8 | With bit 8 |
|---|---|---|
| Race fps | 197.0, 192.1, 194.5 | 185.7, 195.8, 188.6 |
| GPU ms a frame | 4.97, 5.08, 5.03 | 5.28, 5.03, 5.18 |
| Resolves, GPU ms a frame | 0.503, 0.487, 0.506 | 0.581, 0.605, 0.587 |
| Transfers, GPU ms a frame | 0.339, 0.343, 0.342 | 0.267, 0.252, 0.261 |
| Render thread time per draw | 2.092, 2.079, 2.108 us | 2.035, 2.080, 2.090 us |

  With bit 8 the resolves take about 0.09 ms a frame more and the transfers about 0.08 ms less, so the frame's GPU time stays about the same. The full-screen resolves (0.17 ms) and the number of resolves from the host render target (25.3 a frame) are unchanged, so the added time is in the other resolves; its cause is not found yet.

### 16.17 Increment 4, step 0: the one-pass probe, 23 October 2026

The main session approved the outline of increment 4 on 23 October 2026, with these decisions:

- One pass is decided on the guest side, through the title's tiling scenario table, written into guest memory at start-up only on the native path; RTV with `-Quality` stays the tiled reference and `-Rov` is untouched.
- 2x MSAA goes first, end to end. 4x is a second sub-step, after `-Rov` settles the depth sample question of 16.11.
- The host's 2x and 4x sample positions are checked against the Xenos pattern.
- Depth and stencil come from sample 0 for the motion vectors and the cockpit window.
- The defaults (2x MSAA at 2x against 4x MSAA at 1x) are judged from perf and from 2x 1:1 crops of the race, the cockpit, the window edges and the foliage, against tiled RTV with `-Look "original,taa=0"`.

**The table.** The tiling scenarios at `0x82D883C8` (stride `0x4D0`) hold the screen height at +0 (720), the sample count at +4 (log2: 2 for 4x, 1 for 2x, 0 for none), the tile count at +8 and +16, and the tile rectangles from +24 (left, top, right, bottom). The five entries are:

| Entry | Name in the image | Samples | Tiles |
|---|---|---|---|
| 0 | `4xMSAA` | 4x | 1280x256, 1280x256, 1280x208 |
| 1 | `2xMSAA` | 2x | 1280x512, 1280x208 |
| 2 | `NonTiling` | none | 1280x720 |
| 3 | `Small2xMSAA` | 2x | 1120x584 |
| 4 | `SplitScreen_NonTiling2xMSAA` | 2x | 1280x368 |

`FM4_ONE_PASS_MSAA=2` or `4` (title, `CTileBuffer::SetScenario` hook) rewrites entry 1 or 0 as one 1280x720 tile before the title reads it. `REX_SURFACE_PROBE` logs every new combination of a draw's raw surface registers.

**The probe** (`n4p-*`, RTV, `race` at 1x and 240 Hz, `-Look "original,taa=0"`, `--env FM4_TILING_SCENARIO=1` for 2x and `0` for 4x), per race frame:

| Run | Draws | GPU ms | Race fps | Resolves (full, mid, small, clears only) |
|---|---|---|---|---|
| Tiled 2x (`n4p-tiled-2x`) | 2,376 | 2.59 | 163.2 | 1, 9.5, 20.5, 2 |
| One pass 2x (`n4p-onepass-2x`) | 1,530 | 2.32 | 207.6 | 3, 5.5, 20.5, 0 |
| Tiled 4x (`n4p-tiled-4x`) | 2,390 | 2.83 | 161.7 | 1, 11.5, 20.5, 6 |

- **One pass 2x** reaches the race and runs it to the end. The tile body runs once, so the race has 36% fewer draws. The title's per-tile resolve pairs collapse by themselves into one pair, a depth resolve and a colour resolve of 1280x720: tiled 2x resolves 1280x512 and 1280x208 from base 1024 (depth) and base 0 (colour) in each frame. The clears of the tiles go away. In the tiled modes, the tiles move the window offset (0, then -256 and -512 lines); one pass leaves it 0 and the window scissor at the full screen. No other per-tile constant is set. The HiZ pitch in `RB_SURFACE_INFO` equals the surface pitch in every mode.
- **Placement.** The title keeps the tiled placement in one pass: colour at base 0, depth at base 1024. A 1280x720 2x surface takes 1,440 tiles, so the depth overlaps the colour from tile 1024 to 1440 and wraps past tile 2,048 over the colour's first 416 tiles. RTV shows exactly that: the top 208 lines of the race are wrong and the rest is right.
- **One pass 4x** does not boot (`n4p-onepass-4x`, step 2 of the scenario timed out). Entry 0 also serves the user interface (Autovista's `UITilingScenario` 0) and the pre-race, the replays and photo mode, so 4x needs an entry of its own.

**The design that follows from it, for 2x:**

1. **A no-wrap tile space.** The host's tile space grows from 2,048 to 4,096 tiles, the 12 bits the registers carry (bit 11 of `RB_COLOR_INFO` and `RB_DEPTH_INFO`, which Xenos ignores). This covers the ownership map, the render target keys, the lazy and stale tables and the resolve spans, on the native path only. The dump into the EDRAM buffer cannot address past tile 2,048, so a resolve whose span passes it must come from the host render target. The unscaled fast copies of 2x MSAA sources are taken off the dump exclusion: all 1,650 of them matched in 16.11's diagnostic; only 4x depth differed.
2. **The one-pass depth placed after the colour.** With one pass on, the scene's depth surface (2x, pitch 16 tiles, base 1024) is read at a host base after the colour (1,440). The draws and the resolves apply the same placement, so the 2x scene takes tiles 0 to 2,880 without an overlap. The probe shows no colour surface at base 1024 that would read that depth through the EDRAM. The first step gate checks that with the transfer inventory, and so that no transfer crosses between the moved depth and its old tiles.
3. **One pass under `-Native`.** The launcher passes `FM4_ONE_PASS_MSAA=2` and the 2x scenario to the race with `-Native -Msaa 2`. The table patch stays a probe variable of the title and never reaches RTV or `-Rov`.
4. **TAA and the post passes.** TAA runs on the resolved scene. The motion vectors and the cockpit window get depth and stencil from sample 0 through a shader pass. The host's 2x sample positions are checked against the Xenos pattern first.
5. **Gates.** Step gates: the `career` check run with the resolve byte check, one 2x race perf run per side, and the freeze parity. The close gate is as in the outline, with `native_rt_surfaces` bits 2, 4 and 8 on in `-Native`, the growth stress run, and the transfer inventory against 16.13.

**The private scene depth (the main session's decision of 23 October 2026).** Steps 1 and 2 above became one narrower step for 2x. The scene depth of one-pass 2x gets a private host target outside the shared tile map; that is also where the port is going, with surfaces as host objects rather than EDRAM tiles. The no-wrap tile space (option A) waits for 4x, where the 2,880-tile scene colour, aliased by other surfaces at base 0, needs it or a private colour with explicit copies for its aliases. The choice is made from the 4x inventory.

With `native_one_pass_msaa=2` (set by `e2e.py --native` with `FM4_ONE_PASS_MSAA=2` and the 2x scenario), the depth key with base 1024, a pitch of 16 tiles and 2x MSAA is private. Every consumer of that depth:

- **Draws:** bind the private target, which claims no tiles and does not shorten the colour's range (the colour then owns its 1,440 tiles).
- **Resolves of the depth:** read the private target. A resolve from the host render target takes it as its source (`FindHostResolveSource`). The dump, the direct resolve and the byte check's dump dump that target alone, never the owners of the tile map.
- **Clears of the depth:** clear the private target in place, with no ownership change and without the colour's overlap clamp.
- **Post passes (GTAO, the motion vectors for TAA, the cockpit window's stencil, the mod passes):** read the scene depth and stencil only through resolved textures (`RequestResolvedTexture` on the depth group's resolve destinations). No post pass binds a depth render target, so they get the private depth through its resolves. The pass of step 4 that takes depth from sample 0 is to read the private target too.
- **Memory, sizing and eviction:** the private target is created on first use through the sizing rule, with exactly the rows of the draw at the current scale and 2x MSAA, and it is counted in `rt_memory`. The eviction skips it, `ClearCache` keeps it, and shutdown destroys it with the other targets.
- **Lazy transfers:** it is never an owner or a holder.

The check counts as violations (`one_pass.violations`, which must stay 0):

- an ownership change into the private key;
- a dump through the tile map that names the private target.

It also counts the private target's draws, resolves and clears. The unscaled fast copies of 2x MSAA sources now resolve from the host render target; only 4x keeps the dump. The transfer inventory names each surface's base and pitch, for the aliasing proof.

**The step gate of the private depth** (`n4a-*`, `I:\FM4-build-usa`, 240 Hz, `-Look "original,taa=0"`, the 2x scenario, the fast route; native one pass 2x against RTV tiled 2x):

| Run | Result |
|---|---|
| Native one pass, `race` with the resolve byte check and the transfer inventory (`n4a-inv-onepass`) | 0 violations; 11,440 byte checks, 0 mismatches; 13,252 resolves and 12,180 clears of the private depth |
| Native one pass, `career` with the byte check (`n4a-check-career`) | 0 violations; 19,233 byte checks, 0 mismatches; 25,020 resolves and 18,972 clears of the private depth |
| `freeze` at 2x, RTV tiled against native one pass (`n4a-freeze-*`) | draws -15.5%, predicated run packets -27%, predicated skipped packets -100%: FAIL by design, the expected difference of one pass ([frozen-frames.md](../e2e/frozen-frames.md)); 29.5% of the image above 4, two different race moments. The 1:1 crops of the top 416 lines and of the old tile seam (line 1024 at 2x) show no artefact. |
| `race` at 2x, one run each (`n4a-race-2x-*`) | RTV tiled 175.0 fps, GPU 4.65 ms a frame, 2,740 draws, resolves 0.784 ms; native one pass 218.8 fps, GPU 3.24 ms (-30%), 1,844 draws (-33%), resolves 0.462 ms |

**The aliasing proof.** The transfer inventory of RTV tiled 2x names one transfer from the scene depth into another surface: about once a race frame, into the 16_16 colour with 4x MSAA at base 832 (pitch 13 tiles), whose 832 tiles overlap the depth's from 1024. That surface is the 512x512 shadow map of the cars ([render path](ghidra-render-path.md)), resolved every frame and sampled by the scene, so it reaches the screen.

- Eagerly, the transfer copies 224 tiles a frame: all 13 tile columns, rows 46 to 63 (`n4c-tiled-eager`).
- With lazy transfers, which copy only the tiles a draw touches without overwriting them (`n4c-tiled-lazy`), 18 tiles a frame remain, all in the thirteenth column (bit 12 of the column mask).

Columns 0 to 11 are overwritten before anything reads them, so the pass's first draws cover the target; the thirteenth column (pixels 480 to 520 at 4x) is only partly inside the target's 512 pixels, so it is copied and the overwrite covers its part up to pixel 512. The depth's bytes therefore survive only in pixels 512 to 520, which the 512x512 resolve never reads. The alias carries nothing to the screen, so the private depth needs no alias copy. In the tiled modes the 2x tiles are drawn in order (window offsets 0, then -512 lines), and the tiles before the last one end with a resolve that clears the depth, so the depth tiles past 1024 hold the last tile's rows or the clear value at that point. Either way they reach only those pixels.

**Steps 3 and 4.** `Build.ps1 -Native -MsaaSamples 2` (`pipeline.py --msaa-samples 2`) runs the races in one pass with 2x MSAA. It sets `FM4_ONE_PASS_MSAA=2`, the 2x scenario and `native_one_pass_msaa=2` with the native cvars. The launcher and `e2e.py` refuse it with `-Rov`, and the launcher refuses it without `-Native`; on RTV the variable stays a probe of the title. The GUI launcher has no `-Native`, so it gets the option only when one pass becomes a choice there.

On the multisampled scene, TAA and the post passes need no change:

- The title resolves the scene colour (an average of the samples) and the scene depth (sample 0, bitwise; 16.11's 2x copies all matched) as in the tiled modes.
- The post passes read those resolves: GTAO, the motion vectors, the cockpit window's stencil and TAA at the final point. So they see depth from sample 0 without a pass of their own.

The host's 2x MSAA uses the standard Direct3D pattern, two samples on the diagonal at a quarter pixel from the centre. Per the runtime's notes this is the same shape as the Xenos pattern (top left and bottom right), and `GetD3D10SampleIndexForGuest2xMSAA` maps the order. That is read from the code, not measured.

The look (`n4t-*`, the race at 2x and 240 Hz, the default look `aces` with TAA, one run each): the 1:1 crops of the left window's edge and the A-pillar, the rear-view mirror, and the dashboard and wheel (GTAO) show the same depth effects in the one-pass and the tiled RTV 2x frames, without edge artefacts. A pop-up that looks grey in one pass is fading in (it appears in that frame).

| Run, race at 2x with TAA | Race fps | GPU ms a frame | Resolves, ms | Draws a frame |
|---|---|---|---|---|
| RTV tiled 2x (`n4t-tiled-taa-2x`) | 153.3 | 6.37 | 0.855 | 1,834 |
| Native one pass 2x (`n4t-onepass-taa-2x`) | 188.1 | 5.21 | 0.545 | 1,666 |
| Native one tile without MSAA, today's `-Native` (`n4t-onetile-taa-2x`) | 190.2 | 5.17 | 0.509 | 1,717 |

One pass with 2x MSAA costs about 0.04 ms of GPU time a frame more than today's one tile without MSAA, and 1.16 ms less than the tiled 2x on RTV.

**What option A touches** (the scope list for 4x):

- the transfer shaders' address constant, one 32-bit word with a signed 12-bit base adjustment and two 10-bit pitches, and their masks with the tile count (also on the host depth path);
- the dump shaders and the EDRAM buffer of 2,048 tiles, which is also the host depth store of depth transfers;
- the resolve spans of the draw utilities, `GetRangeRectangles`, the extent splitting of the ownership map, and about 59 wrap sites in the common cache;
- `RenderTargetKey::base_tiles` (11 bits; the key has room for 13).

**The close gate of increment 4 at 2x** (`n4g*`, `I:\FM4-build-usa`, commit `2850567`, 240 Hz, the fast route, the launcher's default look with TAA unless noted). Native one pass 2x (`FM4_ONE_PASS_MSAA=2`, the 2x scenario, `native_one_pass_msaa=2`, `native_rt_surfaces=10`, the other cvars of `-Native`) is compared against RTV tiled 2x (`FM4_TILING_SCENARIO=1`). No cvar of stage N4 was on: all are off by default, and the gate passed none.

| Check | RTV tiled | Native one pass | Verdict |
|---|---|---|---|
| smoke 1x (`n4g1x`, `n4g1xn`), races | `photo` 175.1, `career` 203.0 fps | 227.0, 225.6 fps | every cell reaches its goal |
| smoke 2x (`n4g2x`, `n4g2xn`), races | `photo` 151.3, `career` 151.3 fps | 178.3, 177.8 fps | every cell reaches its goal; `photo_view` FAILs only on the XAM error key the other cells also log, and its luma WARN is another photo on screen |
| perf 1x, 3 runs (`n4g1xp`, `n4g1xpn`), race | 197.8 fps, GPU 3.15 ms a frame, 2.31 µs of render thread time a draw | 219.1 fps, GPU 2.32 ms, 1.92 µs | pass |
| perf 2x, 3 runs (`n4g2xp`, `n4g2xpn`), race | 148.7 fps, GPU 6.51 ms, 2.33 µs a draw, resolves 0.86 ms | 179.9 fps, GPU 5.42 ms, 2.16 µs, resolves 0.56 ms | pass |
| RTV's defaults (one tile) at 2x, 3 runs (`n4g2xpd` against `n3c2xpr`), race | 158.6 fps, GPU 6.14 ms (160.1 fps, 6.08 ms before) | | RTV not slower |
| `parity.py` on `freeze`, 1x and 2x (`n4g-freeze-*`) | `-Rov` against RTV passes: 37.1% and 26.0% of the image above 4 | against RTV and `-Rov`: draws -14%, predicated packets -25%, FAIL by design ([frozen-frames.md](../e2e/frozen-frames.md)); 31.3% and 25.4% of the image above 4 against RTV | within the spread of two race moments; the 1:1 crops of the old tile seam (line 1024 at 2x) and of the top 416 lines show no artefact |
| identity check with perturbation 1,000, `career` (`n4g-perturb-career`) | | 12,847 ranges perturbed, 12,847 reported | pass |
| grow stress with the resolve byte check, `career` (`n4g-grow-career`) | | 19,422 byte checks, 0 mismatches, 39 resizes; 16.1 M draws, 25,049 resolves and 19,187 clears of the private depth, 0 violations | pass |
| render target memory, `career` | 589.8 MB at 1x, 2,142.2 MB at 2x | 203.5 MB at 1x (207.5 MB at the peak of the grow stress), 596.5 MB at 2x | |

**The transfer inventory of one pass at 2x** (`n4g-inventory-2x`, the gate's binary from `snapshots\n4g2xpn`, `race`, rerun after the gate because the first attempt ran out of disk space). The race has 20.5 transfers and 2,800 tiles a frame, steady from window to window. They take 0.34 to 0.35 ms of GPU time a frame, 0.27 ms of that into depth targets. One tile at 2x had 19.5 transfers, 2,384 tiles and 0.346 ms (`n3i-race-2x`, 16.13). Tiled RTV at 2x had 0.31 ms in the smoke cell `n4g2x-240-photo`. The kinds are those of 16.13, with the same tiles, plus one:

| Destination (format, samples, base, pitch) | Source | Transfers | Tiles | Columns, rows |
|---|---|---|---|---|
| format 3 (the scene colour), 2x, base 0, pitch 16 | the 16_16 shadow map, 4x, base 832, pitch 13 | 1 | 416 | all 16, rows 64 to 89 |

The one-pass scene colour spans 90 tile rows from base 0. Its rows 64 to 89 overlap the shadow map's tiles from 832, which the title writes before the scene each frame. So taking the scene colour back copies its bottom 208 lines (416 at 2x scale) from the shadow map. A clear claims its tiles without a transfer, so the title draws into these rows without clearing them after the shadow map. The 1:1 crops of lines 1024 to 1440 of the 2x `freeze` frames (`n4g-freeze-2x-*`) show no shadow data and no artefact; the mean difference per 32-line band against RTV is 0.03 to 7.6 (`-Rov` against RTV: 0.05 to 6.5). That the draws cover every pixel in every frame is not proven; the lazy check with bit 4 would show it, and the copy could then be skipped, a later candidate of about 0.04 ms a frame. The scene depth stays private and has no transfer; the remaining cost, as in 16.13, is the stencil bit passes of the transfers into depth targets.

**The main session's decisions of 23 October 2026**, applied after this gate:

1. `-Native` runs races in one pass with 2x MSAA by default, with `native_rt_surfaces=10` (bit 8, the sized host render targets, beside bit 2). `-MsaaSamples 0` and `-SingleTile` keep one tile without MSAA; `-Msaa` keeps the tiles of `-Quality` on the PM4 RTV path. `e2e.py --native` does the same unless `FM4_TILING_SCENARIO`, `FM4_ONE_PASS_MSAA` or `--msaa` is given. Photo mode, replays and split screen keep their own scenarios. The GUI launcher (`plan.cpp`) has no `-Native` yet, so `-MsaaSamples` comes to it with `-Native`.
2. 4x stays in stage N3. Its gate is 4x MSAA in one pass that works. It starts with a probe: a scenario entry of its own (not entry 0, which the UI and the pre-race screens share), and the transfer inventory of 4x. Then the design chooses between a private colour and depth (alias copies only where something reads an alias) and the no-wrap space of option A. The `-Rov` depth sample comparison of 16.11 belongs to 4x at 1x.
3. After 4x works, the defaults are compared: 2x MSAA at 2x against 4x MSAA at 1x, with the GPU time of both at 1x, which sets the default of the ROG Xbox Ally X.

**The 4x probe** (`n4x-probe-1x`, RTV, `race` at 1x, `FM4_ONE_PASS_MSAA=4` with `FM4_TILING_SCENARIO=3`, the surface probe and the transfer inventory). The patch makes entry 3 (`Small2xMSAA`, not shared with the user interface as entry 0 is) one 1280x720 tile with 4x MSAA.

- **Patched too late.** Patched at the first `SetScenario` call, the race kept the entry's own 2x surface (pitch 1120, depth at 1022), and 21,587 resolves of 1280 pixels failed. The title reads the entry earlier, so the 4x patch now runs from the command line constructor (`fm4-command-line.cpp`). The 2x patch stays at `SetScenario`. Patched early, the 2x depth leaves base 1024 (probably after the colour, as at 4x), so the private depth key never matches. N4's `n4be13`, which linked the early patch by mistake, drew no private depth in its races. At 4x the private keys follow the early layout: the depth at raw base 2880, with bit 11 set. The 2x re-check of the fixed build (`n3q-race-2x`, `n3q-check-career`, the `-Native` defaults) has the depth at base 1024 again and one pass engaged: 11,803 private resolves and 10,854 clears in the race. The race at 2x runs at 185.4 fps with 5.31 ms of GPU time a frame, against 184.7 fps and 5.28 ms in `n4g-inventory-2x`. The career's 19,170 byte checks show no mismatch, and the 1:1 crops of the race are correct.
- **The scene.** One 4x surface, pitch 32 tiles: colour at base 0 (format 3, for some draws 12), depth (D24FS8) at base 2880, after the colour's 2,880 tiles, with bit 11 of `RB_DEPTH_INFO` set. The cache masks it to 832, inside the colour and over the shadow map; the middle half of the picture is wrong.
- **The transfers**: 21.5 and 3,824 tiles a race frame (20.5 and 2,800 at 2x), the kinds of 16.13 plus the scene depth (at 832, pitch 32) into the scene colour (1,216 tiles, rows 26 to 63) and into the 16_16 shadow map (224 tiles, rows 46 to 63). No transfer reads the scene colour into another surface; the surfaces at base 0 with pitches 4 to 16 keep their transfers among themselves.
- **The space.** Colour and depth need 5,760 tiles, more than the 4,096 the 12-bit bases address, so option A would have to move the depth anyway.

**The 4x design (the main session's decision of 23 October 2026): a private colour and a private depth.** The no-wrap space cannot hold the scene's 5,760 tiles. With `native_one_pass_msaa=4`, two surfaces have private host targets outside the tile map, as the 2x depth does:

- the scene depth: depth, 4x, pitch 32 tiles, base 2880 (832 in the 11 bits of the key);
- the scene colour: 4x, base 0, pitch 32, the storage format of `2_10_10_10_FLOAT`. Formats 3 and 12 share that storage, so they are one surface with no transfer between them.

Update binds them without claiming tiles and without shortening the other targets' ranges. Their targets are twice the EDRAM's height (1,024 lines at 4x, of which the scene uses 720). Clears of them claim no tiles. Their resolves always come from the host render target, the unscaled 4x depth fast copies included, because the dump into the EDRAM buffer cannot hold 2,880 tiles. A dump of a private 4x surface counts as a violation. The private colour key also serves entry 0's 4x colour at base 0 (the pre-race and car screens). In the whole probe run, no transfer read that surface through the EDRAM.

The limit: resolves from the private 4x surfaces have no byte check, since there is no dump to compare them with. The `-Rov` depth sample comparison and the 1:1 image checks stand in for it.

**The 4x step gate** (snapshot `n3q-fix`, 1x, 240 Hz, the fast route; native one pass 4x is `FM4_ONE_PASS_MSAA=4` with `FM4_TILING_SCENARIO=3` and `native_one_pass_msaa=4`):

| Check | Native one pass 4x | RTV tiled 4x |
|---|---|---|
| `race`, one run each (`n3x-race-native`, `n3x-race-rtv`) | 219.3 fps, GPU 2.37 ms a frame, 1.83 µs of render thread time a draw | 190.2 fps, GPU 3.32 ms |
| render target memory (`n3x-native-4x`, `n3x-rtv-4x`) | 231.5 MB | 577.8 MB |
| transfers a race frame (`n3x-native-4x`) | 19.5 and 2,384 tiles (0.110 ms), the one-tile set of 16.13 | |
| `career` with the resolve byte check and the identity check with perturbation 1,000 (`n3x-check-career`) | 11,430 byte checks, 0 mismatches; 34,573 private resolves and 29,778 clears, 0 violations; 12,464 ranges perturbed, 12,465 reported | |
| 1:1 images, the same steps | the race correct; the 2D menus identical (0.0% above 4); the car select (entry 0, 4x tiles, the private colour) without seams at lines 256 and 512 | |

The extra identity mismatch is one range of writer `82311080` (`DrawIndexedVertices`), 16 dwords and no marker: its last dword changed after the writer returned. It happened in the menus or the load. A career at the 2x default with the same check (`n3d-idchk-career`) reported exactly the 12,085 perturbed ranges. The main session's decision: it does not block 4x. It is an open item of N1's record path (the record would carry the old dword), and the perturbed identity check stays in every close gate. A second sighting is chased with the guest call stack of the late write. It came in the combination check below, at the 2x default without 4x: writer `82311080`, a range of 17 dwords, dword 15. So it belongs to N1's record path, not to one pass.

**The `-Rov` depth sample comparison (16.11's open item).** The diagnostic `native_rt_surfaces_depth_dump=<directory>` (off by default, any render target path) writes the destination bytes of every unscaled 4x depth fast copy to a file when its submission completes. The 4x tiled race freezes (`n3d-*`) made no such copy. Photo mode at 1x makes three a frame, its tile depths of 1280x256, 1280x256 and 1280x208 (`n3p-*`: `-Rov`, RTV with the dump, RTV with the host resolve and `native_rt_surfaces_unscaled_msaa`). Every pair of runs differed in 83 to 100% of the words, RTV's two variants included. Despite `FM4_FIXED_SEED` and `FM4_FIXED_FRAME_TIME`, the runs freeze photo mode on different race moments, so the comparison is inconclusive. A native 4x race with the dump on (`n3c-race-native`) showed that one pass makes these copies too: the scene depth resolve of 1280x720, 8,940 times in a race. They come from the private depth through `resolve_host_32bpp`'s sample 0, with no byte check and no dump to fall back on. In the tiled race the same destination is written per tile by other copies. The main session's decision: the question is open, and it blocks making 4x a default. It is settled next by computing the dump and the host resolve of the same copy within one tiled RTV run in photo mode, and by checking which corner of a sloped surface sample 0 takes. If they differ, `resolve_host_32bpp` follows the dump.

**The combination check** (snapshot `n3m`: the 4x commit rebased onto N4's close gate at `93bb4dc`, with N4's switches on in `-Native`; launched without `guest_ring_wait_sleep`). The `-Native` default race at 2x (`n3m-race-2x`) runs at 187.8 fps with 5.20 ms of GPU time a frame, against 181.5 fps and 5.36 ms for N4's `n4be14`. One pass is engaged: 12,402 private resolves and 11,273 clears, 580.5 MB. N4's combined-check career with perturbation and the resolve byte check (`n3m-check-career`) shows:

- the binding check: 18,798 perturbed, 18,798 reported;
- the update guard: 18,627 perturbed, 18,627 reported;
- the back end: 21,437 perturbed;
- the resolve byte check: 11,806 checks, 0 mismatches;
- the identity check: 11,937 perturbed, 11,938 reported, the extra being the second sighting above.

RTV at 1x (`n3m-race-rtv`) runs at 217.9 fps and 2.81 ms with 577.8 MB, as N4's RTV races on `n4be19` (213.6 to 221.1 fps). The 2x race is correct at 1:1.

**The 4x depth sample, settled (16.11's open item).** With the resolve byte check, `native_rt_surfaces_depth_dump` also writes the first 24 differing 4x depth copies from both sides. `n3b2-photo` is RTV photo mode at 1x, with the host resolve of these copies and the check at every frame. In each pair the two sides differ in about 10% of the pixels, always the same ones: pixel columns 20 to 23 of every 40-pixel tile (4x), in every row. These are the first 8 samples after the depth tile's 40-sample half boundary, the halves that EDRAM swaps for depth.

On a sloped surface the host side steps evenly along a row (−575 a pixel). The dump side jumps by +112 inside the band and back after it. A least-squares fit of dump minus host over the band gives a position difference of (−0.250, +0.499) pixels, which is Direct3D's 4x sample 2 minus sample 0. So inside the band the copy returns the vertical sample (guest sample 1), and everywhere else sample 0, as the host resolve does.

`-Rov` has the same band. The photo depth dumps of `n3p-*` show the jump in 2,592 of 2,666 tile rows on `-Rov` and on RTV with the dump, against 27 on RTV with the host resolve. The cause is the shared 4x depth fast copy's addressing at the half-tile boundary, not RTV's dump. The host resolve's sample 0 is consistent across the tile, so the one-pass 4x race's depth resolves are right.

The cause, from the disassembly in `shaders/bytecode/d3d12_5_1/resolve_fast_32bpp_4xmsaa_cs.h` (upstream Xenia's fast copy, which this tree has as bytecode only):

- One thread copies 8 pixels of a 4x row (16 samples wide) with four loads from consecutive EDRAM addresses after one address computation.
- The depth half-tile swap (`uge r1.y, r0.x, l(40)` / `movc l(-40), l(40)`) is applied once, to the group's first sample.
- The group of pixels 16 to 23 in a 40-pixel tile (samples 32 to 47) straddles the swap at sample 40. Its second half is read from the swapped address of sample 32 onward, samples 80 to 87, which is the next sample row of the tile: the vertical sample.
- At 2x and 4x resolution scale the half (80 or 160 samples) is a multiple of a group's 16 samples, which is why the scaled copies matched; at 3x it is not. The 1x/2x MSAA fast copies read one sample a pixel, and their 8-pixel groups divide every half. Depth is never 64bpp.

The main session's decisions of 23 October 2026: `resolve_host_32bpp` stays, and the open item on the one-pass race's depth sample is closed. The project owner approved fixing the shared fast copy on 23 October 2026, knowing that it changes `-Rov`'s output. It is an emulation bug, not a title correction.

**The fix.** `fm4-resolve/resolve_fast_32bpp_4xmsaa.xesli` replaces the precompiled 4x fast copy, together with its `_scaled` variant. It works like the full copies: 4 pixels a thread, every pixel's address from `XeEdramOffsetBytes`, the raw EDRAM buffer, the fast copy's red and blue swap of colour, and the bitwise store. `pipeline.py` compiles it with the other FM4 resolve shaders. The upstream headers left the repository, and `resolve_copy_shader_info` binds the raw buffer for it. Proof on snapshot `n3f`:

| Check | Before | After |
|---|---|---|
| RTV photo at 1x with the host resolve, the byte check at every frame and the pair dump (`n3f-rtv-photo`) | 3,552 differing 4x depth copies | 158,835 checks, 0 mismatches, no differing pair |
| the strict band test on `-Rov`'s photo depth copies (steady steps, a jump at pixel 20 cancelled at 24), the three tile depths | 2,242, 1,622 and 314 rows (`n3p-rov-b`) | 0, 0 and 0 rows (`n3f-rov-photo`); RTV's host resolve had 0 before |
| `parity.py` on `freeze`, `-Rov` against RTV, 4x tiles (`n3f-freeze-*`) | | PASS: draws −0.2%, predicated packets −0.2%, 32.55% of the image above 4 |
| `-Rov` smoke, `original,taa=0` (`n3f-rovsmoke`) | | 3 of 3 cells, the race at 102.9 fps |
| 1:1 image of `-Rov`'s photo mode | | correct |

**Photo mode under `-Native`.** With bit 2, the unscaled fast copies of 4x MSAA sources now come from the host render target too, and the exclusion of 16.11 is gone. `native_rt_surfaces_unscaled_msaa` only logs the multisampled checks that match. A `-Native` photo run with the byte check at every frame (`n3f-native-photo`) compared 190,899 resolves without a mismatch.

**2x at 2x against 4x at 1x** (the defaults of decision 3; snapshot `n3z`, `13393dc`; `-Native` with N4's switches, the launcher's look, 240 Hz, the perf tier of 3 runs; this laptop's RTX 5070 Ti Laptop GPU):

| One pass | Race fps | p99 | GPU ms a frame (runs) | Render thread busy a frame |
|---|---|---|---|---|
| 2x at 2x (`n3d-2x2`) | 181.2 | 7.57 ms | 5.17 to 5.44 | 4.25 ms |
| 4x at 1x (`n3d-4x1`) | 238.2 (the 240 Hz cap) | 4.95 ms | 2.22 to 2.49 | 3.78 ms |
| 2x at 1x (`n3d-2x1`) | 238.4 (the 240 Hz cap) | 4.96 ms | 2.23 to 2.42 | 3.78 ms |

At 1x, 4x costs about 0.1 to 0.15 ms of GPU time a frame more than 2x; 2x at 2x costs about 2.2 times the GPU time of 4x at 1x. At 1:1 (`n3d-img-*`, `original,taa=0`), 2x at 2x is clearly sharper in the textures and the far terrain, while the geometric edges of 4x at 1x are as clean.

The assumption for the ROG Xbox Ally X, not measured: its Radeon 890M is about 4 times slower than this GPU here (3 to 5 times), so a race frame would take about 21 ms (16 to 27) at 2x at 2x and about 10 ms (7 to 12) at 4x at 1x. The main session's decision of 24 October 2026: 4x at 1x is the Ally X candidate, to be confirmed on the device; `-Native` keeps one pass 2x as its default, and `-MsaaSamples 4` selects 4x in `Build.ps1`, `pipeline.py`, `e2e.py --msaa-samples 4` and the GUI launcher's "Native MSAA" choice.
