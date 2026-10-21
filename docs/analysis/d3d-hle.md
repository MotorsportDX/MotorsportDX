# D3D-level HLE: feasibility and plan

Status: analysis, 21 October 2026. Nothing here is implemented. Addresses are the USA `default.xex` (the Japan/Asia executable has the same code, see [release comparison](releases.md)). The generated C++ that was read is `<BuildRoot>\project\generated\default`; the catalog, call graph and decompiled functions stay outside the repository under `I:\FM4-re` ([function catalog](function-catalog.md)).

## Summary

- **Proposal**: hook the title's XDK D3D functions (`D3DDevice_SetTexture`, `SetRenderTarget`, `DrawIndexedVertices`, `Clear`, `Resolve`, `Swap` and so on) and implement them on D3D12, so that PM4, the register file and the EDRAM emulation are no longer used. The title's presentation code (track, cars, crowd, particles, shadows, mirror) runs unchanged, and Xenos shaders are still translated by the existing DXBC translator, so the hash-keyed look mods keep working.
- **Verdict**: possible, and Unleashed Recompiled shows the pattern works, but on FM4 it does not remove what the proposal expects. The D3D device object is a shadow of Xenos registers that game code also writes directly; about 80% of the draws the command processor (CP) executes are replays of PM4 recorded in guest memory; the XDK produces the tiles with its own CPU-side stream interpreter; and the XDK rewrites vertex shader microcode in place at draw time. An FM4 HLE therefore keeps the register-to-D3D12 translation and has to decode recorded PM4 once per recording. What goes away is the ring, the CP thread's packet parsing and, in a second and much larger stage, the EDRAM emulation.
- **Gain on the current build**: small with the launcher defaults. Races in one tile with TAA run at 230.7 to 236.9 fps at 240 Hz while the CP waits for the vblank 376 to 458 ms/s, and 2x is GPU bound (5.54 ms per race frame). An HLE helps the tiled modes (`-Msaa`: 200.5 to 218.1 fps on a CP busy 818 to 948 ms/s), slower CPUs, and, only with the EDRAM-free second stage, MSAA without tiles, render targets of any size and higher-precision formats.
- **Recommendation**: a measurement gate first (about one week), then decoded replays of recorded command buffers inside the PM4 path if the numbers support it. A full API-level HLE is a separate decision about capabilities, with a GPU-only prototype before any commitment (section 8).

## 1. The proposal and what was checked

The proposal moves the boundary between the title and the GPU emulation from the PM4 ring up to the D3D API. The existing assets it builds on: the XDK D3D layer of 361 catalogued functions, of which 59 are named with confidence, and the mapped draw flow of cars, track, crowd, particles, shadows, mirror and post effects ([render path](ghidra-render-path.md), [function catalog](function-catalog.md#rendering-subsystem-manual-pass)).

Checked for this note: the call sites and bodies of the D3D entry points in the generated C++, the device object, the flush to PM4, recorded command buffers, predicated tiling, shader provenance, guest-visible GPU effects, the runtime components an HLE could reuse, the project features that depend on PM4, and Unleashed Recompiled's GPU layer. The per-frame numbers come from an existing `FM4_D3D_TRACE` run (section 3).

## 2. The title's D3D layer

### 2.1 Entry points

The entry points are real functions called by direct `bl` from game code, so a `REX_HOOK_RAW` on the address replaces the body for every caller:

| Function | Address | Direct call sites |
|---|---|---|
| `D3DDevice_SetTexture` | `sub_8233A9A8` | 152 |
| `SetPixelShader` | `sub_82351BD0` | 58 |
| `SetDepthStencilSurface` | `sub_822D9968` | 52 |
| `SetVertexShader` | `sub_82351A00` | 47 |
| `SetViewport` | `sub_822FEAA8` | 30 |
| `Resolve` | `sub_822E2120` (forwards to `sub_82348DC8`) | 30 |
| `SetStreamSource` | `sub_823445A0` | 29 |
| `DrawVertices` | `sub_8233AC60` | 22 |
| `DrawVerticesUP` | `sub_822B95D8` | 20 |
| `Clear` | `sub_8236D840` | 18 |
| `DrawIndexedVertices` | `sub_82311080` | 9 |
| `SetRenderTarget` (implementation) | `sub_823563B8` | 8 |
| `SetScissorRect` | `sub_822FF050` | 5 |
| `Swap` | `sub_8237FA28` | 3 |

The catalog counts 361 functions in the module "XDK D3D"; 59 of them have a confidence of 0.75 or more. Link-time code generation placed the main setters and `Clear` (`SetTexture`, `Clear`, `Resolve`, `SetRenderTarget`, `SetViewport`, `SetScissorRect`, `SetDepthStencilSurface`, the shader setters) in the game's address range, where the catalog files them under "Forza game" at 0.55 to 0.85; the draw functions (`DrawVertices`, `DrawIndexedVertices`, `DrawVerticesUP`) are filed under "XDK D3D". The D3D layer is therefore larger than 361 functions, and every hook address needs a runtime check before its body is replaced. `names-manual.tsv` gives `0x8237FA28` two names; its body calls `BeginCommand` and then `VdSwap` with the front buffer fetch constant, so it is `Swap`.

### 2.2 State written straight into the device object

The XDK setters do not issue commands. They store Xenos-encoded values into the device object and set dirty bits; `SetTexture`, for example, packs the fetch constant words and ORs its slot bit into the dirty word at +24. Game code does the same without any call: `sub_8280BE70` stores four floats at device+6416 to +6428, ORs the pixel constant dirty word at +8 and then calls `DrawVertices`. An approximate scan finds about 244 functions that read and write a dirty word (not every hit is verified to be the device). So the API arguments do not carry the full state; the device object does.

| Offset | Content |
|---|---|
| +0 to +40 | six 64-bit dirty words: +0 vertex float constants, +8 pixel float constants, +16 and +32 register groups, +24 fetch constants |
| +48, +52 | ring write pointer and end |
| +1152 | fetch constants (register `0x4800`, 6 dwords each) |
| +1920 | vertex float constants (`0x4000`) |
| +6016 | pixel float constants (`0x4400`) |
| +10112 | bool and loop constants (`0x4900`) |
| +10368 onward | register groups `0x2000` (from `RB_SURFACE_INFO`), `0x2100`, `0x2180` (from `SQ_PROGRAM_CNTL`), `0x2200` (from `RB_DEPTHCONTROL`, with `PA_SU_SC_MODE_CNTL` at `0x2205`), `0x2280`, `0x2300`, `0x2380` |
| +13044, +13048 | current pixel and vertex shader |
| +13904 | current command buffer recording |

Two consequences. An HLE has to read the shadow at flush time instead of following the API calls, and the state it reads is still in Xenos register form, so the register-to-D3D12 translation (pipeline state, system constants, fetch constants, primitive processing) stays in every design.

### 2.3 The flush to PM4

`D3DDevice_FlushDirtyState` (`sub_826F6E18`) has one caller, the prologue `sub_82348DC8`, which `Resolve` and the game's draw helpers call. It writes the dirty groups through three writers: `WriteDirtyRegisters` `sub_823114F8`, `WriteDirtyFloatConstants` `sub_82310E78` and `WriteDirtyFetchConstants` `sub_8230FA40`, which build type-0 packets through `sub_82379530`. `RunCommandBuffer` and `EndCommandBuffer` call the same three writers directly, without `FlushDirtyState`. A hook on the writers covers every flush; a hook on `FlushDirtyState` alone misses the command buffer paths.

The static scan behind [`fm4-d3d-trace.cpp`](../../work/fm4-rex/src/fm4-d3d-trace.cpp) lists 71 functions that write type-3 packets: 38 in the XDK range `0x826E1550` to `0x826F9B58`, 33 in the game range. Type-0 packets with computed headers are not found by that scan, so coverage has to be proven at run time: in a test mode, every ring allocation by a caller that the HLE has not replaced is logged.

### 2.4 Recorded command buffers

`BeginCommandBuffer` (`sub_826E23E8`) points the device's current recording at the buffer and stores an optional tile table and the tile count. `EndCommandBuffer` (`sub_826E4050`) flushes all dirty state into the recording by value, then clears the device state. A recording is a linked list of PM4 chunks at +116 of the buffer (next pointer, size in dwords, data). `D3DCommandBuffer_CreateClone` (`sub_826E2350`) copies it. No code that patches a recording after `EndCommandBuffer` was found.

`RunCommandBuffer` (`sub_82310818`) first flushes the device's currently dirty groups, so a replay inherits the state set before it (for example the part transform that `CCarModel::RunPartCommandBuffer` sets). Then `sub_826E4A28` handles each chunk in one of three ways: while a tiling bracket is open (device+13584) the chunk descriptors are deferred to `EndTiling`; while device+11152 is non-zero (a count of indirect buffer entries not yet drained) the descriptors are copied into the ring; otherwise one `INDIRECT_BUFFER` per descriptor is written. The split between these paths is not measured.

Every draw path, flush, replay and `EndCommandBuffer` also writes `LOAD_ALU_CONSTANT` packets (writer `sub_82343608`, through `sub_823436B8`). They carry only a guest address; the runtime reads the constants from guest memory when the CP executes the packet (`command_processor.cpp`, `ExecutePacketType3_LOAD_ALU_CONSTANT`). The constant tables come from the current shader objects; who fills them and whether the CPU rewrites them between a recording and its replay is not established.

Recording sites: car parts per view slot, part and LOD (`RecordCommandBuffers` `sub_8294D100`), crowd models (`sub_82946C00`, with `CreateClone`), track objects through the render engine's group allocator `sub_82875458`, the half-rate bloom (`sub_8237CC08`) and five recorders without a name (`sub_827DF7C0`, `sub_827E5E48`, `sub_827F55B0`, `sub_8280BE70`, `sub_82860758`). `CRenderEngine::RunCommandBuffer` (`sub_82310FC8`) can queue a replay into a per-thread group, so recording and replay can run on different threads.

### 2.5 Predicated tiling

The tiles are not produced by game code and not by the GPU alone. Between `BeginTiling` (`sub_82371B80`) and `EndTiling` (`sub_8236FA20`) the XDK writes a tagged stream (tags `0x80` to `0x90`), and a CPU-side interpreter, `sub_826ED978` (reached through `sub_826EDCE8` and `sub_826EDFA8`), walks the tile body once per tile: tag `0x86` marks the body, `0x87` steps the tile index and jumps back, `0x88` is a predicated block that is skipped when its mask does not meet the current bin select, and the interpreter writes `INDIRECT_BUFFER` and `SET_BIN_SELECT` packets for the GPU. The `EndTiling` loop itself calls only `SetPredication` (mask `3 << 2i` for tile i) and `Resolve` per tile.

A one-pass HLE can run the body once at full size with every bin selected and resolve once over the union of the tile rectangles. It must keep predication itself: `CMirrorRenderer::Render` (`sub_82392AE0`) sets a mask and does not reset it, so the next pass inherits it. Not yet known: where the per-tile window offset is applied (the tile rectangles minus a window offset go into the `BeginTiling` record), what the `Resolve` body `sub_82348DC8` copies, and which thread runs `sub_826EDFA8`.

### 2.6 Shaders and their hash keys

The runtime keys shaders by XXH3 over the raw microcode dwords at the `IM_LOAD` source address, or over the ring copy for `IM_LOAD_IMMEDIATE`; translation never changes those bytes. A vertex shader object holds its microcode at +32 and its pass records from +896; a pixel shader object holds it at +24. The draw flush (`sub_823436B8` through `sub_82353BA8`) calls `sub_826EC0C0` whenever the bound vertex layout changes, and that function writes the rewritten 12-byte fetch instructions back into the shader's own microcode (`fm4_rex_recomp.20.cpp`, around line 12560). The vertex shader keys the runtime sees today are therefore keys of rewritten bytes.

Look mods are pixel shaders only, keyed by the hash and an optional modification suffix; no rewrite of pixel shader microcode was found. `passes.toml` names vertex shaders by hash (the scene camera and the car shader set). An HLE keeps every key if it leaves the XDK flush running and hashes at the point where `IM_LOAD` is written; it gets different vertex shader keys if it hashes at `CreateVertexShader` or `SetVertexShader`. Two built-in shaders come from fixed data at `0x82067308` (96 bytes) and `0x82067368` (36 bytes) through `IM_LOAD_IMMEDIATE`.

### 2.7 What the guest reads back

| Mechanism | Today | What an HLE has to provide |
|---|---|---|
| Ring index waits (`sub_822B0788`, catalog name `Resource::WaitForFence`; `BlockUntilIdle` `sub_826E4D20`) | the guest waits until the CP read pointer passes a ring write index; the runtime publishes the read pointer after each executed batch | a consumed index at the same granularity, or the waits spin until the XDK's timeout path (probably `sub_826F7EF0`, reached from the timed poll `sub_826E8358`) |
| Resource fences | a guest dword at resource+24, polled by `WAIT_REG_MEM` (`sub_82386BF8`, `sub_82386C48`); `WAIT_REG_MEM` has no timeout in the runtime | the value written in order, after the work before it; never before (stale reads), never missing (hang) |
| Vblank and swap counter | `MarkVblank` dispatches the guest interrupt callback and wakes waits; `EVENT_WRITE_SHD` with bit 31 writes the counter; `vblank_vrr` reads whether the CP waits | the same counter, callback and wake-up |
| Occlusion queries | `EVENT_WRITE_ZPD`; the sun query is awaited; the launcher sets `rov_occlusion_async=previous`, so reports are published at once from the previous frame's samples (the runtime default `off` makes every report wait for the GPU); the `sun` knob rescales host samples | native queries written to the guest's sample memory, with the knob re-derived |
| `VIZ_QUERY` | faked as visible | the same, or a real query |
| Resolve readbacks | 26 to 31 per race frame, 14.9 MB; known CPU readers: the photo save and the exclusion of resolved ranges in texture replacement; `d3d12_readback_publish_await_signals` is off, so a fence can precede a readback | readbacks only for ranges the CPU reads, in order with the fences |
| Memexport | none in the 1,040 shader entries of the four road inventories | nothing, until another scenario shows one |

## 3. Where the executed draws come from

`I:\FM4-build-usa\e2e\d3d-trace-pred` (20 October 2026, `FM4_D3D_TRACE`, a Quick Race run of 145 seconds with 6,975 swaps, menus included; the race renders in tiles, about 4.3 bin selects per frame; in the perf trace windows with more than 1,200 draws per frame, 2,470 CP draws, 4,028 predicated packets run and 1,461 skipped per frame):

| Source | Calls in the run | Per swap |
|---|---|---|
| Draws executed by the CP (perf trace) | 7,712,934 | |
| `DrawIndexedVertices` | 1,135,931 | 162.9 |
| `DrawVertices` | 134,697 | 19.3 |
| `DRAW_INDX_2` writer `sub_82370370` (clears, screen passes) | 108,526 | 15.6 |
| `BeginVertices`, `BeginIndexedVertices` | 18,888, 4,468 | 3.3 |
| `RunCommandBuffer` | 1,210,220 | 173.5 |
| `LOAD_ALU_CONSTANT` | 694,014 | 99.5 |
| `SetPredication` | 172,526 | 24.7 |
| `Resolve` prologue | 145,949 | 20.9 |
| Ring overflow waits (`WAIT_REG_MEM` from `CommandBufferOverflow`) | 89,273 | 12.8 |

Direct draw calls are 1,402,510, 18.2% of the draws the CP executed. The other 82% are replays of recorded command buffers and per-tile replays. A hook on the draw functions sees a recorded draw once, when it is recorded.

## 4. The proposal's claims, checked

| Claim | Finding |
|---|---|
| PM4, the register file and EDRAM are no longer used | Partly. The device state is a Xenos register shadow (2.2), 82% of executed draws are recorded PM4 (3), and the tiles come from the XDK's tag stream (2.5). The ring, the CP thread and the parsing of direct draws can go; register translation stays; recorded PM4 has to be decoded; EDRAM can go only with a new render target model (section 6, stage 2 of A). |
| The presentation code runs unchanged | Yes. The XDK setters and the flush should run unchanged too, because of the inline writes (2.2) and the microcode rewrite (2.6). |
| The DXBC translator can be reused, so the look mods keep working | Yes, if the HLE feeds it the same system constants, constant buffers and fetch constants, and hashes shaders where `IM_LOAD` is written (2.6). |
| Unleashed Recompiled uses a similar path | Yes for the pattern (section 5). It shows nothing about predicated tiling, recorded command buffers, `LOAD_ALU_CONSTANT` or awaited queries, which are FM4's hard parts. |
| 361 functions, 59 named with certainty | 361 in the module "XDK D3D", 59 at confidence 0.75 or more. The main setters are catalogued in the game module at 0.55 to 0.85 (2.1). |
| The draw flow is mapped | Yes, statically ([function catalog](function-catalog.md#rendering-subsystem-manual-pass)). Recording rates and the replay path split are not measured. |

## 5. Prior art: Unleashed Recompiled

Read from `hedge-dev/UnleashedRecomp` (`UnleashedRecomp/gpu/video.cpp`, `video.h`, `README.md`) and `hedge-dev/XenosRecomp` (`README.md`) on 21 October 2026, main branch, through summarized reads:

- `video.cpp` ends with 42 `GUEST_FUNCTION_HOOK` lines on guest addresses: device creation, `SetRenderTarget`, `SetDepthStencilSurface`, `Clear`, `SetViewport`, `SetTexture`, `SetScissorRect`, `DrawPrimitive`, `DrawIndexedPrimitive`, `DrawPrimitiveUP`, vertex declarations, shader creation and binding, `SetStreamSource`, `SetIndices`, `StretchRect`, `Present`, resource creation and locks, and a few game functions; and about 15 `GUEST_FUNCTION_STUB` lines (for example `SetGammaRamp`).
- A `GuestDevice` struct of `0x5E00` bytes mirrors the XDK device in guest memory (dirty flags, a table of 0x65 render state functions of which 17 point to host functions, sampler states, 0x400 float constants per stage). Calls become render commands on a queue that a render thread executes, two frames in flight.
- Shaders are recompiled offline by XenosRecomp (Xenos microcode to HLSL, then DXIL or SPIR-V) and looked up by an XXH3 hash at run time.
- Resolves are `StretchRect` copies and MSAA resolve shaders. No EDRAM, PM4 or tiling.

Sonic Unleashed's engine calls a D3D9-style path (`StretchRect`, `DrawPrimitive`, render state functions); FM4 calls the lower XDK layer (inline shadow writes, recorded PM4 command buffers, predication). No code of these projects is proposed for reuse; reading them for design ideas needs no license step, reusing code would need the review in [sources and licensing](../sources-and-licensing.md).

## 6. Three ways to cut it

Three designs were drafted and scored by three reviewers, each from one point of view: benefit, risk and validation (a higher score means less risk), and effort and project fit. The effort figures are each design's own estimate, revised where a reviewer found it too low; none is calibrated against past work.

| | A: API-level HLE | B: HLE at the flush boundary | C: decoded replays inside the PM4 path |
|---|---|---|---|
| Boundary | about 26 guest functions (entry points and the flush); a new render path; stage 2 replaces EDRAM with host render targets keyed by guest surface | the PM4 producers (dirty writers, draw, clear, resolve, event, wait and query writers, command buffers, tiling) write host records into an in-order queue that drives the existing backend | `EndCommandBuffer` decodes each recording once into an operation list; `RunCommandBuffer` replays the list instead of PM4 chunks |
| Removes | ring and CP parsing (stage 1), EDRAM (stage 2) | ring and PM4 parsing of the replaced producers | chunk parsing and packet dispatch of replays |
| Keeps | register translation, the backend in stage 1 | register translation, the backend, EDRAM | everything, with PM4 as the fallback per recording |
| Effort, person-weeks for one developer with an AI agent | stage 1: 12 by the design, 16 to 20 by a reviewer; stage 2: 12 by the design, two to three times that by a reviewer | 18 to 30 | core 7 by the design, 9 to 10 by a reviewer; optional one-pass tiles 2.5, readback skip 2.5, compact flush 1.5 |
| 240 Hz, 1x | up to the 240 Hz cap (+3 to +9 fps from 231 to 237 fps) | 0 to +3% | 0 to +3% at the defaults |
| 2x (GPU bound) | none in stage 1; stage 2 unmeasured | none | none in the core; the optional readback skip is unmeasured |
| New capabilities | stage 2 only: MSAA without tiles, render targets of any size, wider formats, post-pass anchors from D3D calls | none | none |
| Scores: benefit, risk (higher is safer), fit | 5, 5, 5 | 3, 6, 4 | 6, 8, 8 |

Reviewer findings that apply to every design:

- The cost split used for the estimates (draw work 60%; PM4 parsing, register writes and other packets 24%; swap 13%; copies 3%) is the launcher-default race profile of 20 October 2026 in [CP draw offload](cp-draw-offload.md), taken before one tile became the default (2,400 to 3,050 draws per frame, which matches the tiled counts) and before the asynchronous swap. It has to be measured again on the current build.
- No design touches the per-draw translation (textures, bindings and pipeline are 30% of the CP; all draw work is 60%). An operation list can carry precomputed pipeline and binding keys per recorded draw, so they are paid once per recording; the cheap per-draw reuse of [CP draw offload, option 3](cp-draw-offload.md#option-3-less-work-per-draw-on-the-one-thread) is the comparator for any of these designs.
- A one-pass tiled scene targets the tiled modes (`-Msaa`, looks without TAA). It is not measured. The nearest measurement is the tiles with 4x MSAA against one tile without MSAA, the current default: CP 948 to 790 ms/s and 200.5 to 232.0 fps in the `career` race (20 October 2026), and GPU 6.50 to 5.54 ms at 2x (19 October 2026, one run each). That comparison also drops MSAA, so it bounds the gain from above. One pass does nothing for the defaults and must not change `-Rov`, which stays the uncorrected reference (AGENTS.md).
- Hooks: no guest address is hooked twice today, but `fm4-d3d-trace.cpp` hooks eight entry points that an HLE replaces (`RunCommandBuffer`, `DrawIndexedVertices`, `SetPredication`, `DrawVertices`, `BeginVertices`, `BeginIndexedVertices`, `InsertAsyncCommandBufferCall`, `Swap`) and the `VIZ_QUERY` and `LOAD_ALU_CONSTANT` writers, and `fm4-jpeg-diagnostics.cpp` hooks `sub_822B0788` and one texture lock (`sub_826DEF78`). A guest address can have one definition, so these move into one hook registry first.
- The backend can be driven without the ring, but only through the paths the PM4 handlers use (`WriteRegisterFast` and `WriteRegistersFromMem`, so the constant, fetch constant, scratch and gamma side effects fire; `LoadShader` and the active shader pointers; the bin select and mask), and from one producer thread: the submission state is unlocked, `CallInThread`'s queue has no lock, and `PollIdleWork` can end a submission while occlusion snapshots are pending, also on RTV.

## 7. What it can gain on the current build

Smoke gates at 240 Hz and 1x, 20 October 2026 ([current status](../status/current.md#one-tile-with-taa-the-presets-and-taas-history-20-october-2026)):

| Race | Tiles (`-Msaa`) | One tile | One tile, asynchronous swap |
|---|---|---|---|
| `photo` | 218.1 fps, CP 818 ms/s, 1,963 draws | 236.9 fps, 689 ms/s, 1,589 draws | 230.7 fps, 774 ms/s, 1,893 draws |
| `career` | 200.5 fps, CP 948 ms/s, 2,308 draws | 232.0 fps, 790 ms/s, 1,712 draws | 233.2 fps, 745 ms/s, 1,711 draws |

With one tile the CP waits for the vblank 376 to 458 ms/s, so at the defaults the race is close to the 240 Hz limit and a faster CP adds a few fps at most; at 2x the GPU sets the frame rate (5.54 ms per race frame with TAA in one tile, 19 October 2026, one run). Where an HLE or its first stage can pay off:

- the tiled modes, whose CP is saturated (one-pass tiles, decoded replays);
- other computers with slower CPUs (the [GUI launcher and export](../status/current.md#gui-launcher-and-export-20-october-2026)), where the cost per draw is higher; not measured;
- frame-time stability (p99) rather than average fps;
- capabilities that need render targets outside the EDRAM model (MSAA without tiles, wider aspect ratios, higher-precision scene formats for [HDR output](hdr-output.md), anchors for upscalers): stage 2 of design A only.

## 8. Recommendation and stages

1. **G0, measurement gate** (about 1 person-week for the counters of design C's first phase; the other items are not estimated; no change to the game's output). Each item has a go or no-go use:
   - Re-measure the perf tier at 240 Hz and 2x, three runs each, and split the CP time into packet parsing, register writes (`WriteRegisterFast`, `WriteRegistersFromMem`), draw work, swap, copies and the vblank wait (a zone around the type-0 and type-3 dispatch in `command_processor.cpp`).
   - Count the three replay paths of `sub_826E4A28` per frame (deferred, copied, indirect buffer) and the `BeginCommandBuffer` and `EndCommandBuffer` calls per second and per recording, in a race and a menu: decoded replays pay off only if recordings are reused.
   - Log the XXH3 key at `IM_LOAD` execution against the hash of `object+32+V` at emission for every shader of one race (2.6).
   - Run the coverage log (2.3) in a menu, a race and a replay, and cross-check it against the 71 static writers.
   - Run `REX_LATE_WRITE_TRACE` on a race and a tiled race: any guest write between a recording's end and its replay, or into a `LOAD_ALU_CONSTANT` block before the CP reads it, decides whether replays may be decoded once.
   - Count `EVENT_WRITE_ZPD`, `REG_TO_MEM`, `REG_RMW`, `COND_WRITE` and `MEM_WRITE` per frame, and the GPU wait per occlusion report.
   - Hook `sub_826ED978` and `sub_826EDFA8` with the thread and caller in a tiled race, and read `sub_82348DC8` (the resolve body).
2. **S1, decoded replays (design C)**, 7 to 10 person-weeks, if G0 shows that replay parsing is a real share of the CP. Decode at `EndCommandBuffer`, replay through `WriteRegisterFast`, `LoadShader` and `IssueDraw` on the CP thread, keep pipeline and binding keys per recorded draw, fall back to PM4 per recording on any mismatch, switch off by default. Compare against option 3 of [CP draw offload](cp-draw-offload.md).
3. **S2, one pass for tiled scenes on RTV** (design C's optional stage, 2.5 person-weeks by the design), for `-Msaa` and looks without TAA, after the window offset, the resolve body and the predication rules of 2.5 are known. `-Rov` keeps its tiles.
4. **S3, the API-level HLE with host render targets** (design A, stage 1 and 2: 24 person-weeks by the design, about 40 to 56 by the reviewers' corrections), only if the capabilities in section 7 are wanted. It starts with a GPU-only prototype (one scene pass and one resolve at 2x against the current RTV path), needs a new rule in AGENTS.md for a third render path, keeps the PM4 path selectable as the reference, and keeps the XDK setters and flush running as the state source (2.2, 2.6).

## 9. Validation rules for any stage

- Races are not deterministic ([e2e](../e2e/README.md)), so image parity uses frozen rewind frames at a pinned look (`-Look "original,taa=0"`, the tiles and MSAA of `-Quality` where tiles are compared) with the occlusion knobs pinned, plus the variance of two identical baseline runs.
- A state-level dual run per batch or replay: register file, draw parameters and predicate decision of the new path against the PM4 path, with the guest address on a mismatch.
- `compare.py` fails on a crash, a goal not reached, fps more than 10% below the baseline (also per phase), p99 more than 20% above it, a new error key or a missing phase; image statistics and draw counts only warn or inform. A new path needs FAIL rules for the draw count, the predicated run and skip counts and the frozen-frame image difference, and every rejected draw or failed shader load goes through `REXGPU_ERROR` so that it shows as a new error key.
- RTV against `-Rov` stays the reference comparison (AGENTS.md); no stage changes `-Rov`.
- A watchdog that logs any wait longer than a set time, and soak runs over menus, races, the career and photo mode.

## 10. Decisions for the project owner

- The goal: average fps at 240 Hz, frame time p99, 2x GPU time, slower CPUs, or the capabilities of section 7. Only the capabilities need the full HLE.
- Whether to stop after G0 if its numbers show no reachable gain.
- Whether output may differ from the PM4 RTV path within a stated image threshold (one-pass tiles, a change in when `LOAD_ALU_CONSTANT` blocks are read).
- Which modes the first HLE stage must cover (race, photo mode, replays, split screen, menus, video), and which stay on PM4.
- Whether the PM4 path stays a permanent selectable reference, and whether diverging further from the upstream ReXGlue command processor is acceptable.

## 11. Open questions

- Who fills the `LOAD_ALU_CONSTANT` tables and whether the CPU rewrites the blocks between recording and replay.
- Where the per-tile window offset is applied, what `sub_82348DC8` copies, and which thread runs the tag stream interpreter.
- The register-level meaning of the dirty bits inside the words at +16 and +32.
- Resource creation, locks and streaming under an HLE (Unleashed hooks them; FM4's streaming has its own fallback, see [current status](../status/current.md#open-issues)).
- Whether the video, Kinect and facade paths (`XMediaFacade_default.xex`, `SpeechFacade_default.xex`) call the same D3D entry points.
