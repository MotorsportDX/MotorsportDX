# D3D-level HLE: feasibility and plan

Status: analysis, 21 October 2026. The same day the project owner chose a native port as the target; its architecture and stages are in sections 8 and 9, the measurements of stage N0 are in section 13, and the design of stage N1 with the order of its increments is in section 14, with the results of increments 1 to 3 in 14.6, 14.8 and 14.10 and the end of N1 in 14.11; the design of stage N2 is in section 15, and the design of stage N3 in section 16. Addresses are the USA `default.xex` (the Japan/Asia executable has the same code, see [release comparison](releases.md)). The generated C++ that was read is `<BuildRoot>\project\generated\default`; the catalog, call graph and decompiled functions stay outside the repository under `I:\FM4-re` ([function catalog](function-catalog.md)).

## Summary

- **Proposal**: hook the title's XDK D3D functions (`D3DDevice_SetTexture`, `SetRenderTarget`, `DrawIndexedVertices`, `Clear`, `Resolve`, `Swap` and so on) and implement them on D3D12, so that PM4, the register file and the EDRAM emulation are no longer used. The title's presentation code (track, cars, crowd, particles, shadows, mirror) runs unchanged, and Xenos shaders are still translated by the existing DXBC translator, so the hash-keyed look mods keep working.
- **Verdict**: possible, and Unleashed Recompiled shows the pattern works, but on FM4 it does not remove what the proposal expects. The D3D device object is a shadow of Xenos registers that game code also writes directly; about 80% of the draws the command processor (CP) executes are replays of PM4 recorded in guest memory; the XDK produces the tiles with its own CPU-side stream interpreter; and the XDK rewrites vertex shader microcode in place at draw time. An FM4 HLE therefore keeps the register-to-D3D12 translation and has to decode recorded PM4 once per recording. What goes away is the ring, the CP thread's packet parsing and, in a second and much larger stage, the EDRAM emulation.
- **Gain on the current build**: small with the launcher defaults. Races in one tile with TAA run at 230.7 to 236.9 fps at 240 Hz while the CP waits for the vblank 376 to 458 ms/s, and 2x is GPU bound (5.54 ms per race frame). An HLE helps the tiled modes (`-Msaa`: 200.5 to 218.1 fps on a CP busy 818 to 948 ms/s), slower CPUs, and, only with the EDRAM-free second stage, MSAA without tiles, render targets of any size and higher-precision formats.
- **Decision (21 October 2026)**: the target is a native port, efficient and with good visuals, for all of these goals at once: fps at 240 Hz, frame time p99, GPU time at 2x, slower CPUs, and render targets outside the EDRAM model. The plan keeps the title's XDK setters and dirty-state flush as the state source, replaces the PM4 producers with host records consumed by one host render thread, decodes recorded command buffers once, and renders into host targets keyed by the title's surfaces with resolves as host copies (section 8). It is built in stages, each gated against the PM4 path, which stays selectable as the reference (section 9).

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

`D3DDevice_FlushDirtyState` (`sub_826F6E18`) has one caller, `sub_82348DC8`, the large writer behind `Resolve` (2.8), and only runs when the argument in r7 (its fifth integer argument) is non-zero; `Resolve` passes zero and writes the dirty registers directly. Whether the game helpers that call `sub_82348DC8` directly (`sub_8280BE70`, `sub_827F55B0`, `sub_8280A720`) pass a non-zero value is not checked. `FlushDirtyState` writes the dirty groups through three writers: `WriteDirtyRegisters` `sub_823114F8`, `WriteDirtyFloatConstants` `sub_82310E78` and `WriteDirtyFetchConstants` `sub_8230FA40`, which build type-0 packets through `sub_82379530`. `RunCommandBuffer` and `EndCommandBuffer` call the same three writers directly, without `FlushDirtyState`. A hook on the writers covers every flush; a hook on `FlushDirtyState` alone misses the command buffer paths.

The static scan behind [`fm4-d3d-trace.cpp`](../../work/fm4-rex/src/fm4-d3d-trace.cpp) lists 71 functions that write type-3 packets: 38 in the XDK range `0x826E1550` to `0x826F9B58`, 33 in the game range. Type-0 packets with computed headers are not found by that scan, so coverage has to be proven at run time: in a test mode, every ring allocation by a caller that the HLE has not replaced is logged. The coverage log of 21 October 2026 (13.5) found 19 writers outside the list, among them the tiling stream (`sub_823719A8`, `sub_8236F838`, `sub_822B7248`), the segment overflow `sub_822FEF08` and the type-0 writers; the allocations and overflows of the scenes measured come from 26 or 27 of the 71, depending on how a return address is assigned to a function.

### 2.4 Recorded command buffers

`BeginCommandBuffer` (`sub_826E23E8`) points the device's current recording at the buffer and stores an optional tile table and the tile count. `EndCommandBuffer` (`sub_826E4050`) flushes all dirty state into the recording by value, then clears the device state. A recording is a linked list of PM4 chunks at +116 of the buffer (next pointer, size in dwords, data). `D3DCommandBuffer_CreateClone` (`sub_826E2350`) copies it. No code that patches a recording after `EndCommandBuffer` was found. The buffers replayed in a race are not the recorded objects: about half of the race replays (48 to 54%) and two thirds of the replay phase run byte copies of recordings made by game code, the rest PM4 that no `EndCommandBuffer` of the run produced (inferred: built with the assets), and the replayed PM4 changed between replays in 0.02% of them (13.2).

`RunCommandBuffer` (`sub_82310818`) first flushes the device's currently dirty groups, so a replay inherits the state set before it (for example the part transform that `CCarModel::RunPartCommandBuffer` sets). Then, when bit `0x20` of the buffer's flags at +108 is clear, it writes one `INDIRECT_BUFFER` per descriptor straight into the segment it is writing. Only with the bit set does it close the segment and call `sub_826E4A28` per chunk, which handles it in one of three ways: while a tiling bracket is open (device+13584) the chunk descriptors are deferred to `EndTiling`; while device+11152 is non-zero (a count of indirect buffer entries not yet drained) the descriptors are copied into the ring; otherwise one `INDIRECT_BUFFER` per descriptor is written into the primary ring. In the scenes measured on 21 October 2026 every replay took the inline path and `sub_826E4A28` was never called (13.2).

Every draw path, flush, replay and `EndCommandBuffer` also writes `LOAD_ALU_CONSTANT` packets (writer `sub_82343608`, through `sub_823436B8`). They carry only a guest address; the runtime reads the constants from guest memory when the CP executes the packet (`command_processor.cpp`, `ExecutePacketType3_LOAD_ALU_CONSTANT`). The constant tables come from the current shader objects; who fills them is not established. The CPU does rewrite blocks referenced from recordings between the recording and the replay (1.84% of those executions in a race, 13.1% in a replay; 13.3), and never rewrote a block referenced from the ring before its execution.

Recording sites: car parts per view slot, part and LOD (`RecordCommandBuffers` `sub_8294D100`), crowd models (`sub_82946C00`, with `CreateClone`), track objects through the render engine's group allocator `sub_82875458`, the half-rate bloom (`sub_8237CC08`) and five recorders without a name (`sub_827DF7C0`, `sub_827E5E48`, `sub_827F55B0`, `sub_8280BE70`, `sub_82860758`). `CRenderEngine::RunCommandBuffer` (`sub_82310FC8`) can queue a replay into a per-thread group, so recording and replay may run on different threads (inferred). Measured: recordings run on the render thread and on a second thread at load, and every replay ran on the render thread (13.7).

### 2.5 Predicated tiling

The tiles are not produced by game code and not by the GPU alone. Between `BeginTiling` (`sub_82371B80`) and `EndTiling` (`sub_8236FA20`) the XDK writes a tagged stream (tags `0x80` to `0x90`), and a CPU-side interpreter, `sub_826ED978` (reached through `sub_826EDCE8` and `sub_826EDFA8`), walks the tile body once per tile: tag `0x86` marks the body, `0x87` steps the tile index and jumps back, `0x88` is a predicated block that is skipped when its mask ANDed with a word of the interpreter's context (default `0x7FFFFFFF`, probably the bin select) is zero, and the interpreter writes `INDIRECT_BUFFER` and `SET_BIN_SELECT` packets for the GPU. The `EndTiling` loop calls `SetPredication` (mask `3 << 2i` for tile i), `sub_82392628` and `Resolve` once per tile, and no draw; after the loop it calls `SetPredication(0)`, `sub_82392628` again and, with more than one tile, `SetPredication(0x80000000)`.

`sub_826EDFA8` has no static caller. It is a worker loop: it waits on an event, drains the stream through `sub_826EDCE8` when signalled, and on a timeout calls `Swap` under a critical section when a device flag is set. It runs on a guest thread of its own, which calls `sub_826ED978` about twice per tiling bracket (13.7).

`sub_82374660` prepares the tiles: it copies the caller's tile rectangles to device+13104 (16 bytes each, count at device+13100), stores each tile's origin rounded down to 32 at device+13344+8i, and sets the viewport, the scissor and a clear. These origins are EDRAM origins used by the resolve's address arithmetic, not a screen translation of the draws. `BeginTiling` writes the rectangles into the stream relative to tile 0's origin, so absolute rectangles come from device+13104. The tiling scenarios at `0x82D883C8` (stride `0x4D0`, five entries) are: 0, three tiles of 1280 by 256, 256 and 208 rows; 1, two tiles of 512 and 208 rows; 2, one tile of 1280x720; 3, one tile of 1120x584; 4, one tile of 1280x368.

A one-pass HLE can run the body once at full size and resolve once. It must keep evaluating each predicated block against the bins, with a scissor over the absolute rectangles of the bins it selects: `CMirrorRenderer::Render` (`sub_82392AE0`) calls `SetPredication(3 << (2k-2))`, with k from the tile count function `sub_822E4650`, and no reset was found in the part of it that was read, so the next pass may inherit that mask. The bin encoding is not fully decoded: `EndTiling` builds its masks as `3 << 2i`, but its initial bin word is a 6-bit field. Its per-tile resolves also differ: tiles before the last one add the colour clear flag (0x100) when a colour target is bound and the depth clear flag (0x200) when a depth surface is bound, with a float from device+13568, and the last one passes the caller's flags; collapsing them into one resolve needs those flags decoded.

### 2.6 Shaders and their hash keys

The runtime keys shaders by XXH3 over the raw microcode dwords at the `IM_LOAD` source address, or over the ring copy for `IM_LOAD_IMMEDIATE`; translation never changes those bytes. A vertex shader object holds its microcode at +32 and its pass records from +896; a pixel shader object holds it at +24. The draw flush (`sub_823436B8` through `sub_82353BA8`) calls `sub_826EC0C0` whenever the bound vertex layout changes, and that function writes the rewritten 12-byte fetch instructions back into the shader's own microcode (`fm4_rex_recomp.20.cpp`, around line 12560). The vertex shader keys the runtime sees today are therefore keys of rewritten bytes.

Look mods are pixel shaders only, keyed by the hash and an optional modification suffix; no rewrite of pixel shader microcode was found. `passes.toml` names vertex shaders by hash (the scene camera and the car shader set). An HLE keeps every key of the ring if it leaves the XDK flush running and hashes at the point where `IM_LOAD` is written; it gets different vertex shader keys if it hashes at `CreateVertexShader` or `SetVertexShader`. For `IM_LOAD`s inside recordings the key at the point of writing is not enough: the rewrite changes the microcode between recording and replay in 1.63% of those executions in a race and 16.6% in a replay, so they have to be hashed at replay (13.3). Two built-in shaders come from fixed data at `0x82067308` (96 bytes) and `0x82067368` (36 bytes) through `IM_LOAD_IMMEDIATE`.

### 2.7 What the guest reads back

| Mechanism | Today | What an HLE has to provide |
|---|---|---|
| Ring index waits (`sub_822B0788`, catalog name `Resource::WaitForFence`; `BlockUntilIdle` `sub_826E4D20`) | the guest waits until the CP read pointer passes a ring write index; the runtime publishes the read pointer after each executed batch | a consumed index at the same granularity, or the waits spin until the XDK's timeout path (probably `sub_826F7EF0`, reached from the timed poll `sub_826E8358`) |
| Resource fences | in the ring: `D3DResource_BlockUntilNotBusy` `sub_82386BF8` (21 call sites) writes a `WAIT_REG_MEM` (`sub_82386C48`) on the masked word at resource+24 (whether it is a pointer or a fence value is not settled) against the masked word at resource+28; on the CPU: `sub_826E4768` (3 callers: car and crowd command buffer release, video) waits on a ring index at resource+8 through `sub_822B0788`; `WAIT_REG_MEM` has no deadline in the runtime: it sleeps and re-checks until the value matches or the worker shuts down | the value written in order, after the work before it; never before (stale reads), never missing (hang) |
| Vblank and swap counter | `MarkVblank` dispatches the guest interrupt callback and wakes waits; `EVENT_WRITE_SHD` with bit 31 writes the counter; `vblank_vrr` reads whether the CP waits | the same counter, callback and wake-up |
| Occlusion queries | `EVENT_WRITE_ZPD` (2 per race frame with one tile, 6 with the tiles); the sun query is awaited; the launcher sets `rov_occlusion_async=previous`, so reports are published at once from the previous frame's samples (the runtime default `off` makes every report wait for the GPU); measured with the launcher defaults, the command processor waits 0 ms per report (13.6); the `sun` knob rescales host samples | native queries written to the guest's sample memory, with the knob re-derived |
| `VIZ_QUERY` | faked as visible | the same, or a real query |
| Resolve readbacks | about 19 to 31 per race frame, 8 to 15 MB (perf trace race windows; [CP draw offload](cp-draw-offload.md) gives 31.5 and 14.9 MB); known CPU readers: the photo save and the exclusion of resolved ranges in texture replacement, and possibly any of the 77 call sites of the texture lock `sub_826DEF78`, which returns a pointer into the texture's guest memory (video frames, saved textures, a shadow map helper, presentation code); `d3d12_readback_publish_await_signals` is off, so a fence can precede a readback | readbacks for every range the CPU can read, in order with the fences |
| Memexport | none in the 1,040 shader entries of the four road inventories | nothing, until another scenario shows one |

### 2.8 Resolves

`D3DDevice_Resolve` (`sub_822E2120`) forwards to `sub_82348DC8`, about 2,550 lines of generated code. Its flags select the source (bits 0 to 2: colour target 0 to 3, whose surface is at device+12792+4i, or 4 for the depth surface at device+12808), the sample select (bits 4 to 6, derived from the surface's MSAA field when zero), a colour clear (0x100) and a depth clear (0x200). It writes `RB_COPY_CONTROL`, `RB_COPY_DEST_BASE`, `RB_COPY_DEST_PITCH`, `RB_COPY_DEST_INFO` (with the exponent bias), `RB_COPY_FUNC`, `RB_COPY_REF` and `RB_COPY_MASK`, switches `RB_MODECONTROL` to copy mode, writes a `DRAW_INDX_2` that the runtime turns into `IssueCopy`, and afterwards resets `RB_COPY_CONTROL` and `RB_COPY_DEST_INFO` to zero. The copy command is "null" with flag 0x2000, "raw" for depth, "constant one" for one destination format (61) and "convert" otherwise; the runtime implements only raw and convert. The runtime takes the source rectangle from three guest vertices read through vertex fetch slot 0; an empty rectangle makes the resolve a no-op, which is how resolves outside a tile are discarded.

There are 30 direct call sites: 9 for tiles (`CTileBuffer::End` six times, `EndTiling` twice, `sub_828824E0`), the shadow cascades (`ResolveCascade` `sub_823829E0`, twice), the livery renderer (four), the environment map blits (two), the screen and textured quads (two), the photo accumulation target, `CParticleManager::Render`, video playback and a few unnamed functions. The mirror and the bloom chain reach resolves through other functions.

Today every copy resolve writes guest memory: the runtime dumps the host render targets into its EDRAM buffer and a copy shader writes the destination (`TryResolveCopyDirectly` still dumps). A texture bound later at that address goes through `RequestResolvedTexture`, which calls `LoadTextureData` (that it reads the guest copy is inferred), and the post passes see each copy through `PostPasses::OnResolve` with its address and length.

### 2.9 Render target surfaces and EDRAM placement

`D3DDevice_CreateRenderTargetSurface` (`sub_826DF248`) builds a 48-byte surface object: the MSAA mode in the low bits of +24, a Xenos info word at +28 whose low 12 bits are the EDRAM base tile, a depth word at +32, the size at +36, the format at +40 and the footprint at +44 (the field meanings beyond the offsets are partly inferred). The EDRAM has 2,048 tiles of 80x16 samples. A surface is placed either by a bitmap allocator, which returns no surface when it would pass tile 2,048, or at a base the caller chooses (the shadow cascades, and callers that advance a tile cursor). `SetDepthStencilSurface` stores the depth surface pointer at device+12808 and copies its words to device+10376 and +10560; the resolve reads colour surface pointers from device+12792+4i, which `SetRenderTarget` presumably writes (not read).

A 1280x720 surface of 4 bytes per sample takes 720 tiles; 4x MSAA doubles both dimensions, so the colour and depth of one 4x tile would take 5,760 tiles, more than the EDRAM and more than the 12-bit base field can address (4,096). That is why the title tiles, and why MSAA without tiles needs render target identity and placement outside the EDRAM address space.

Surfaces share EDRAM tiles across passes. In the frame graphs of a photo capture and of a scene capture of 19 October 2026, 7.6 to 8.0% of the events carry ownership transfers (14.4 to 15.8 copies per frame), most often between surfaces at EDRAM base 0 (a 4x depth buffer and 1x colour targets). The post passes identify their surfaces by EDRAM base today: the scene depth at base 1,024 with tiles or 720 in one tile, the rear-view mirror's depth at base 80 and the hood and bumper mirror panel at base 64 ([motion vectors](motion-vectors.md)). Transfers on the one-tile default are not measured.

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
| The draw flow is mapped | Yes, statically ([function catalog](function-catalog.md#rendering-subsystem-manual-pass)). Recording rates and the replay paths were measured on 21 October 2026 (13.2). |

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

- The cost split used for the estimates (draw work 60%; PM4 parsing, register writes and other packets 24%; swap 13%; copies 3%) is the launcher-default race profile of 20 October 2026 in [CP draw offload](cp-draw-offload.md), taken before one tile became the default (2,400 to 3,050 draws per frame, which matches the tiled counts) and before the asynchronous swap. It has to be measured again on the current build (13.8).
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

## 8. The native port

On 21 October 2026, after reading sections 1 to 7, the project owner chose a native port: rendering that is efficient and looks good, judged by all five goals at once (fps at 240 Hz, frame time p99, GPU time at 2x, slower CPUs, and render targets outside the EDRAM model). Design A is the base, with the state source of design B and the decoded replays of design C. Each choice below follows from section 2.

| Layer | Design | Why |
|---|---|---|
| State | Keep the XDK setters, the device object and its dirty bits. Replace the dirty writers (`sub_823114F8`, `sub_82310E78`, `sub_8230FA40`, `sub_82379530`) and the PM4 writers (the 71 of the static scan in 2.3 plus any the coverage log finds) with producers of host records that carry register deltas in Xenos form, draw parameters, resolves and events. | Game code writes the device object without calls (2.2); only the flush sees the whole state (2.3). |
| Shaders | Hash where `IM_LOAD` is written (`sub_823436B8`), after the XDK's vertex fetch rewrite, for packets written into the ring; hash the `IM_LOAD`s of recordings at replay, because the rewrite changes their microcode after recording (13.3); key the two built-in shaders by their fixed data. Keep the DXBC translator and its constant buffer contract. | The vertex shader keys of `passes.toml` are keys of rewritten bytes (2.6); look mods are pixel shaders and keep their keys. |
| Transport and threads | Each guest producer writes fixed records into a bounded queue with a global sequence number. One host render thread consumes them in order, owns the submission state and makes every guest-visible write (consumed ring index, fences, vblank counter, occlusion results, readbacks, swap); in N1 the vblank interrupt stays on the vsync thread and the readback copies on the readback thread, ordered by the host render thread's waits, and they move to it in N4 (14.2). The existing submit thread keeps recording the D3D12 command lists. `LOAD_ALU_CONSTANT` blocks are copied into the record when the queue may run ahead of the guest. The consumer waits on events, not spin loops. Queue depth starts at one frame. | The backend's submission state is unlocked and assumes one producer; recordings are made on two guest threads, replays and direct draws on the render thread (13.7); the guest's fences are ring indices that need a published progress value (2.7); the spin loops compete for cores on a slower CPU. Unleashed Recompiled uses the same shape (section 5). |
| Recorded command buffers | First, the render thread replays recorded PM4 through the existing interpreter (`ExecutePacket`). Then each recording is decoded once into an operation list with precomputed pipeline and binding keys; a replay applies the inherited state, re-reads the `LOAD_ALU_CONSTANT` blocks, and falls back to PM4 per recording on any mismatch. | 82% of executed draws are replays in the tiled trace (section 3); per-draw translation is 30% of the CP and parsing about 24%. The decode is worth it only if recordings are reused: they are, a replayed buffer runs about twice a frame for about 130 to 4,100 frames (13.2). About half of the race replays are byte copies of recordings and the rest come from buffers no `EndCommandBuffer` of the run produced (inferred: built with the assets), so the decode keys on the replayed buffer and its PM4, not on `EndCommandBuffer`. |
| Tiles | One pass. Every predicated block is still evaluated against the bins and drawn with a scissor over the absolute rectangles of the bins it selects (device+13104); the mirror's leftover mask keeps working. `EndTiling`'s per-tile resolves become one resolve plus the clears once the flags of 2.5 are decoded. | 2.5. |
| Render targets | Host render targets keyed by the title's surface objects, with a virtual placement: surface creation and the EDRAM allocator are hooked so that placement never fails and never wraps, while the overlap of surfaces is still tracked in that virtual space. Contents are copied between surfaces only where a pass reads what another surface left without clearing first. Native MSAA follows each surface's MSAA mode; the host sample positions are checked against the Xenos pattern before 4x is offered. | MSAA without tiles cannot be addressed in EDRAM tiles (2.9); the ownership transfers show that some passes do read aliased contents (2.9). |
| Resolves | A resolve becomes a host copy or MSAA resolve into the host texture keyed by the destination guest address (the texture cache's resolved-texture key), recorded with its source surface, selects, clears and clear values, destination layout, exponent bias and rectangle. `PostPasses::OnResolve` keeps receiving the same address and length in the same order, and the `RB_COPY_*` shadow resets stay. Guest memory is first written for every resolve, as today; later a resolved range is written only when the CPU touches it, through page protection (the runtime's watches catch writes only; a trap on reads is not built or verified). | 2.8; the CPU readers include the 77 texture-lock sites, so an allow-list is not safe (2.7). |
| Formats | Keep the RTV choices: the 7e3 scene as `R16G16B16A16_FLOAT` (no clamp at 31.875; `R11G11B10_FLOAT` loses mantissa bits), the float24 depth as `D32_FLOAT_S8X24`, gamma targets as 16-bit UNORM in gamma space. Change only with their readers in one verified step: the depth z range halving (it exists for EDRAM transfers; GTAO, TAA's previous depth and the motion vectors read that depth), the divide by 32 for `k_16_16` targets (it is part of the exponent bias contract of the look mods), and linear blending. The guest frame stays SDR so the photo save and readbacks do not change; HDR comes from the final combine ([HDR output](hdr-output.md)). | Fidelity is decided by the format and blending rules, not by the transport. |
| Post passes | Anchors move from EDRAM bases to surfaces and call sites: the scene depth is the depth surface of the camera vertex shader's draws (until that is verified, the last wide depth resolve, as today); scene colour is the colour resolve after it (as the framework's colour group works today; not re-checked for the native path); the final point keeps its pixel shader hashes; camera and object constants come from the device shadow at the draw. The jitter stays in the `ndc_offset` system constant, with the cockpit mask (`2F76904F`) and any screen-space rectangle draw excluded by hash, and its pixel scale checked at 2x. With native MSAA, depth and stencil for motion vectors and the cockpit window come from sample 0 through a shader pass. | The post passes find their surfaces by EDRAM base today (2.9). |
| Rules | The native path uses host render targets with bindless resources, like RTV. Look mods and GTAO target RTV and the native path (AGENTS.md, owner's decision of 21 October 2026). The PM4 RTV path stays selectable, and `-Rov` stays the uncorrected reference: the native path is compared against `-Rov` and RTV with `-Look "original,taa=0"`. | AGENTS.md, render target paths. |

### 8.1 The second target device: ROG Xbox Ally X

The project owner's slower target PC is an ASUS ROG Xbox Ally X handheld, the owner's second device (decision of 21 October 2026). Vendor and review specs ([HotHardware](https://hothardware.com/news/asus-rog-xbox-ally-x-handheld-revealed), [PC Games Hardware](https://www.pcgameshardware.de/Handheld-PC-Hardware-279763/Tests/Asus-ROG-Xbox-Ally-X-mit-Ryzen-Z2-AI-Extreme-Preis-Release-Steam-1484282/)): AMD Ryzen AI Z2 Extreme with 8 cores and 16 threads, a Radeon 890M integrated GPU (RDNA 3.5, 16 compute units), 24 GB of LPDDR5X-8000 shared by the CPU and the GPU, a configurable TDP of 15 to 35 W, a 7-inch 1920x1080 panel at 120 Hz with FreeSync Premium (VRR), and an 80 Wh battery. HDR support of the panel is not confirmed.

What follows for the native port (inferred until measured on the device):

- The device is more likely GPU bound than CP bound. The default look with GTAO and TAA may already use the whole GPU at 1x, and the launcher's choice for a 1080p screen, 2x (the smallest whole multiple of 1280x720 that covers it, [running](../running.md#-display-and-render-scale)), is probably too heavy.
- 1080p is 1.5 times 1280x720, and the runtime's render scale is a whole multiple. Host render targets keyed by surface (N3) may allow 1.5x, the panel's own resolution, if the texture cache's scaled resolves follow; otherwise an upscaler from 720p ([FSR plan](fsr-plan.md)) is the route to a sharp 1080p image.
- The CPU and the GPU share one power budget, so CPU time saved by the native path (no PM4, no spinning CP thread) can let the GPU clock higher.
- The frame budget is 8.33 ms at 120 Hz and 16.7 ms at 60 Hz; VRR covers frame rates in between.

Plan changes: N0 adds a device baseline (the current build through the [export](../running.md#export-to-another-computer), at 1x and 2x, with the default look and with AO off, plugged in and on battery, with the TDP profile noted, and `REX_PERF_TRACE_FILE` set for fps, p99, GPU and CP times). N4 keeps the capped-CPU run on the main PC as a quick proxy, but its gate runs on the Ally X. N5 adds the 1.5x render scale and the upscaler for the device. Proposed device targets, to be set after the baseline: races at 60 fps or more with p99 at most 16.7 ms on the default look at the panel's resolution, and as close to 120 fps as the GPU allows with VRR.

### 8.2 Shaders on the native path

On RTV today the shader override path replaces a pixel shader per hash with HLSL (`shader_<hash>.ps.hlsl`) or hooks into its translation (`shader_<hash>.mod.hlsli`), under the contract of the DXBC translation: the Xenos constant buffers (float constants c0 to c255, bool and loop constants, fetch constants), the translated interpolators, and the exponent bias and gamma rules on output. Vertex shaders have no override.

The native path creates the pipelines and binds the resources itself, so a native shader contract (stage N5) can add:

- replacement shaders that declare their own resources per pass (the scene depth, the GTAO result, higher-resolution shadow maps, lookup tables, new textures) and their own constant buffers;
- vertex shader overrides, keyed by the hash taken where `IM_LOAD` is written (2.6), or at replay for `IM_LOAD`s inside recordings (13.3), for example for a widescreen projection;
- behaviour per pass: the same game shader can be treated differently in the scene, the mirror or the shadow pass;
- new passes at D3D-level points (after the opaque geometry, before the transparent geometry, before the HUD), with Shader Model 6 and compute shaders (DXC is needed, as in the [FSR plan](fsr-plan.md));
- an offline translation of every game shader to HLSL and DXIL on the user's machine, as XenosRecomp does for Unleashed Recompiled, so that no shader compiles during play and any shader can be edited.

What stays tied to the game is its data, not the Xenos GPU: the meaning of the constants the game writes ([material atlas](material-atlas.md)), the vertex and texture formats in guest memory (the native path converts them before a shader sees them), and the identity of each game shader by its microcode hash. Shaders written for the project may go into the repository like the look mods; translations of the game's own shaders are derived from game data, so they are generated on the user's machine like the recompiled code and are not committed.

## 9. Stages

Every stage is behind a switch, off by default until its gate passes, with the PM4 path as the fallback. The effort is person-weeks for one developer with an AI agent; it is inferred and not calibrated against past work.

| Stage | Scope | Exit criteria | Effort |
|---|---|---|---|
| N0, foundations and measurements | One hook registry (merge the trace and diagnostics hooks); counters for the replay paths of `sub_826E4A28`, recordings per second and replays per recording, the `IM_LOAD` key check of 2.6, the coverage log of 2.3, `REX_LATE_WRITE_TRACE` on a race and a tiled race, the sync packets of 2.7, the guest render thread's time per frame (a zone around `sub_822DB318`) and a thread trace of the D3D entry points; the CP time split at 240 Hz and 2x; the frozen-frame A/B harness and the FAIL rules of section 10; a baseline on the ROG Xbox Ally X (8.1). | Every number recorded here with its run; the coverage list complete for a menu, a race and a replay; the variance of two identical baseline runs known. | 2 to 3 |
| N1, native producer on the existing backend | The producers of section 8 feed the host render thread, which drives the existing D3D12 backend through `WriteRegisterFast`, `WriteRegistersFromMem`, `LoadShader` and `IssueDraw`; recordings replay as PM4 through `ExecutePacket`; a PM4 writer not yet converted keeps writing PM4, which the host render thread executes in order the same way (the fallback); render targets stay on the RTV cache; the ring and the CP thread are no longer used on this path. The modes it covers are in section 11: the native path runs with one tile, and the tiles of `-Quality` stay on the PM4 RTV path, chosen at launch, until N3. | State-level dual run without mismatches (the identity check); frozen frames within the image limit of section 11 against RTV; the identity check of the converted dirty writers with a perturbation proof; smoke and perf gates against the RTV build of the same commit; the success criteria for CP time and p99 measured. Closed after increment 3 by the main session's decision of 22 October 2026 (14.11): the direct writers of increments 4 and 5 are deferred, and the CP time and p99 targets become N2's gate. | 6 to 9 |
| N2, decoded recordings | Operation lists with precomputed pipeline and binding keys; inherited state and `LOAD_ALU_CONSTANT` re-read at replay; fallback to PM4 per recording (section 15). | Dual run per replay without mismatches, with a perturbation proof; render thread time per frame lower than N1 by a measured amount; smoke, perf and parity against RTV of the same commit. The CPU targets of section 10 belong to N4 (the main session's decision of 22 October 2026, 15.11); N2 records its share toward them. | 3 to 5 |
| N3, native render targets | Surface-keyed host targets with virtual placement, copies for aliased contents, one-pass tiles, native MSAA as a launcher option (2x and 4x in one pass, TAA on top), resolves as host copies and demand readback, post-pass anchors on surfaces, look mods and GTAO moved onto the native path. Starts with a GPU-only prototype at 2x (one scene pass and one resolve) against the RTV path. | Frozen frames within the image limit of section 11 against RTV and against `-Rov` with `-Look "original,taa=0"`; GTAO, TAA, motion vectors and the windshield checked at 2x; GPU time at 2x not above RTV; 4x MSAA in one pass working. | 12 to 20 |
| N4, threading and slower CPUs | The CPU targets of section 10 (render thread busy at most 2.5 ms per race frame and p99 at most 5.0 ms at 240 Hz), toward which N2 and N3 record their measured share; the profile of 15.10 points at the descriptor and constant buffer work leaving the render thread ([CP draw offload](cp-draw-offload.md), options 1 and 2). Event waits instead of the remaining spins (the host render thread of N1 already waits on its queue's event; the guest's own waits and the `WAIT_REG_MEM` polls without a sleep time still spin), the vblank interrupt and the readback copies moved onto the host render thread (8, 14.2), queue depth and latency tuned (the depth target starts at one frame; increment 1 measured about one frame of kicks, 14.6), a capped-CPU run on the main PC (fewer cores through the process affinity) as a quick proxy, and runs on the ROG Xbox Ally X (8.1). The design and its steps are in section 17. | The success criteria of section 10 met on the main PC and on the Ally X. | 3 to 5 |
| N5, visual upgrades on the native base | Float targets for `k_16_16`, linear blending as an A/B, upscalers on the native anchors ([FSR plan](fsr-plan.md)), a 1.5x render scale for the Ally X's 1080p panel, the native shader contract of 8.2, wider aspect ratios (needs a FOV and culling hook), and HDR10 once an HDR display exists. | One A/B and one decision per item. | per item |

The measurement part of N0 was done on 21 October 2026 (section 13). N0 to N4 total about 26 to 42 person-weeks. The earlier estimates for design A alone were 24 by the design and 40 to 56 with the reviewers' corrections; this plan reuses the backend in N1 and N2, which is why its range starts lower.

## 10. Validation and success criteria

- Races are not deterministic ([e2e](../e2e/README.md)), so image parity uses frozen rewind frames at a pinned look (`-Look "original,taa=0"`, the tiles and MSAA of `-Quality` where tiles are compared) with the occlusion knobs pinned, plus the variance of two identical baseline runs.
- A state-level dual run per batch or replay: register file, draw parameters and predicate decision of the native path against the PM4 path, with the guest address on a mismatch.
- `compare.py` fails on a crash, a goal not reached, fps more than 10% below the baseline (also per phase), p99 more than 20% above it, a new error key or a missing phase; image statistics and draw counts only warn or inform. The native path needs FAIL rules for the draw count, the predicated run and skip counts and the frozen-frame image difference, and every rejected draw or failed shader load goes through `REXGPU_ERROR` so that it shows as a new error key. Its own metrics replace `cp_busy` (render thread busy, guest render thread busy, queue depth).
- The harness for these rules exists since 21 October 2026 ([frozen-frame parity](../e2e/frozen-frames.md)): the `freeze` scenario (a Quick Race rewound to its oldest frame, 2x, the pinned look and occlusion), `frame_diff.py` (the share of pixels above a level, 1:1 crops), `parity.py` (FAIL on draws and predicated packets per frame, the image difference and new error keys) and the backend-neutral `summary.json` fields. Within a run the frozen frame is byte-identical. With the fixed seed of the race setup (`FM4_FIXED_SEED`) and the rewind's first press pinned to race frame 720, four baseline runs (two RTV, two `-Rov`) differ by up to 1.6% in draws and predicated packets per frame, and RTV and `-Rov` at about the same moment agree within 0.1%; `parity.py` fails above 3%. Their frozen frames still differ in 19 to 34% of the pixels (race frames counted in pad polls; up to 48% with the earlier presses on the wall clock): the race advances by wall time, so the same race frame is another moment in each run. `parity.py` fails above 60% of the pixels, which catches a broken frame only; that is the image limit of the native path (section 11), and wrong details are left to the state-level dual run and the 1:1 crops. The race clock is found (the main loop's step; `FM4_FIXED_FRAME_TIME=1` fixes it, and the 3-2-1 starts on a fixed update), but the race still diverges from a last-bit difference during the revving before the start, so a pair with it differs in 25.6% of the pixels; a tighter image limit would need that input found as well (see [what pinning the moment needs](../e2e/frozen-frames.md#what-pinning-the-moment-needs)).
- RTV against `-Rov` stays the reference comparison (AGENTS.md); no stage changes `-Rov`.
- A watchdog that logs any guest wait longer than a set time, and soak runs over menus, races, the career, replays and photo mode.
- Success criteria (proposed targets, to be confirmed against the N0 baseline of the USA build with the launcher defaults; the main PC's CPU criterion was judged met on 23 October 2026, [17.14](#1714-the-main-pcs-cpu-target-the-main-sessions-decision-of-23-october-2026)): at 240 Hz and 1x in the race, the render thread busy at most 2.5 ms per frame (the CP took 3.2 to 3.4 ms) and p99 at most 5.0 ms (5.59 to 5.78 ms), both against the 20 October 2026 smoke runs on the Japan/Asia BuildRoot `I:\FM4-build-pass`; the guest render thread reported per frame; on the ROG Xbox Ally X the device targets of 8.1; at 2x a GPU time per frame not above the RTV path; frozen frames within the image limit of section 11 and no new error keys.

### Gate tiers (the main session's decision of 22 October 2026)

A full gate took 1.5 to 2 hours, too long to follow every step of an increment, so the stages from N3 on use two tiers:

- **Step gate**, after each step or fix inside an increment, about 30 minutes or less: one `career` run on the native path with the check modes that apply (the identity check, the binding check or a conversion's byte check); one perf run each for RTV and the native path, at the resolution the change targets only (2x for GPU work, 1x for CPU work), with the `race` scenario (the `photo` scenario's race phase without photo mode, about 90 s shorter); the `parity.py` freeze only if the change can alter the image.
- **Close gate**, once per increment before the fast-forward is asked for: smoke, perf with three runs, `parity.py` on the freeze at 1x and 2x, and the perturbation proof as one `career` run.
- Performance is judged on the busy time per draw and the GPU time per frame, which the AI races' varying draw load moves less than the fps; so one run a side is enough at a step gate, and three runs are for the close gate.
- A step gate reuses its RTV baseline while the step's diff changes only code behind a cvar that is off on RTV (checked with `git diff`); the close gate measures RTV again on the final binary.
- Where a run's time goes (`photo` at 1x, 252 s): the boot to the main menu 28 s, the menus and loading to the race 120 s (with a fixed 40 s wait after GO RACE, about 20 s of it idle), the measured race 34 s, photo mode and the save about 70 s. The fast route (`e2e.py --fast-route`, since 23 October 2026), whose steps wait on the title's screen events instead of fixed delays, takes `photo` from about 251 s to about 193 s a run, about 57 s less; the boot (about 24 s) and the loading (17 s) remain as the floor. The `race` scenario drops photo mode as well.
- Durations measured on 22 October 2026 (`sg0`, 2x, the build of 16.8): a step gate takes 10 minutes of game runs (`career` with the identity check 3.7, `race` on RTV 3.3, `race` native 3.3) and 13.5 with the freeze (3.5); a build adds 15 to 20 minutes when the runtime changed. A close gate takes about 65 minutes (smoke on RTV and native 18, perf with three runs a side 26, the freeze at 1x and 2x a side 14, the perturbation proof 4).

## 11. Decisions

Made by the project owner on 21 October 2026:

- The target is a native port, judged by all five goals of section 8.
- The slower target PC is the ROG Xbox Ally X, the owner's second device (8.1).
- MSAA comes back as an option on the native path, rendered in one pass without tiles; one tile with TAA stays the default.
- Look mods and GTAO target RTV and the native path; `-Rov` stays the uncorrected reference and the native path is compared against it (AGENTS.md, render target paths).
- The image limit of frozen-frame parity is final at 60% of the pixels above level 4 (the `parity.py` default), the same against RTV and against `-Rov`. Because the frozen moment differs between runs (section 10), it catches a broken frame, not a wrong detail; wrong details are left to the state-level dual run and the 1:1 crops.
- N1 covers its modes in three tiers. Native with the full gates of section 9 (dual run, frozen-frame parity, perf, no PM4 written): the menus from boot to the pre-race and the post-race, the race with one tile (the default) and the replay. These are the scenes every session passes, the 240 Hz, p99 and CP time targets, the only scene with a frozen-frame harness, and the slowest mode, where the rewritten recordings (13.3) and the late writes (13.4) show up; covering the menus also avoids a switch between PM4 and the native path at the start and end of a race. Native with the PM4 fallback where they use writers not yet converted, and a smoke check only (no crash, a photo saved, no new error key): photo mode, split screen and the intro videos. The tiles and MSAA of `-Quality` stay on the PM4 RTV path, chosen at launch, until N3 replaces them with one pass, so the tiling writers are not converted twice.
- The runtime's GPU layer (`work/runtime/src/graphics`) is the project's own and may diverge from the upstream ReXGlue SDK as far as the native path needs; the recompiler, core and kernel stay close to upstream. On 21 October 2026 the fork is pinned to upstream v0.10.0 (21 August 2026, still the latest tag): `src/codegen` and `src/core` are identical to it, `src/kernel` differs in 4 files, and `src/graphics` in 47 entries, among them 2,555 of the 7,476 lines of `d3d12/command_processor.cpp`, with the Vulkan backend and the SPIR-V translator removed. Upstream had 141 commits from 1 June 2026, 11 of them in `src/graphics`. So:
  - The native path goes into new files beside the existing command processor and reaches the backend through the interfaces it already has (`WriteRegisterFast`, `WriteRegistersFromMem`, `LoadShader`, `IssueDraw`), so the PM4 RTV path stays selectable and `-Rov` unchanged (AGENTS.md).
  - The fork stays pinned. Each new upstream release is reviewed: fixes to the recompiler, core and kernel are taken; graphics changes are ported only when they fix a problem seen in FM4.
  - The upstream BSD-3-Clause and Xenia notices stay in the files that change ([sources and licensing](../sources-and-licensing.md)).

Still open:

- Whether the native path may differ from RTV by design (one pass, MSAA, `LOAD_ALU_CONSTANT` read time, formats), judged on the 1:1 crops.
- Whether the PM4 RTV path stays a permanent reference once the native path passes its gates.

## 12. Open questions

- Who fills the `LOAD_ALU_CONSTANT` tables (that the CPU rewrites blocks of recordings between recording and replay is measured, 13.3).
- Where the replayed command buffers that are not copies of a recording come from (inferred: built with the assets), and whether the 0.02% of replays whose PM4 changed between replays are patches or new buffers at the same address (13.2).
- The bin encoding of `EndTiling` and of the predicated blocks, and the meaning of the clear flags and the float of its per-tile resolves (2.5).
- The resolve body's remaining arguments (the clear values written by `sub_822C4010` and `sub_822CF0A8`, the flags 0x4000 and 0x8000) and support for the "constant one" and "null" copies (2.8).
- Which passes read aliased EDRAM contents on the one-tile default, and whether a caller-placed surface ever passes tile 2,048 (2.9).
- The register-level meaning of the dirty bits inside the words at +16 and +32.
- Resource creation and buffer locks (only the texture lock is identified), and how much dynamic geometry goes through the ring per frame.
- Whether the video, Kinect and facade paths (`XMediaFacade_default.xex`, `SpeechFacade_default.xex`) call the same D3D entry points.

## 13. Stage N0 measurements, 21 October 2026

The measurement part of stage N0 (section 9). The frozen-frame A/B harness and the FAIL rules are a separate part of N0 and are not covered here.

### 13.1 Runs

All runs are on `I:\FM4-build-usa` (USA release) with the counters of [traces](../e2e/traces.md#d3d-layer-counters-fm4_n0_trace) (`FM4_N0_TRACE`, `FM4_N0_KEYCHECK`, `REX_PM4_STATS`, `REX_CP_SPLIT`), the launcher defaults (look `pc`, quality `high`, one tile with TAA, the asynchronous swap), 240 Hz and 1x unless stated, the title's AI driving. The `career` scenario covers a menu, a race (Tsukuba Short Circuit, Ford Ka, 40 s) and a replay (30 s).

Windows: the race and the replay are the whole phases as `e2e.py` summarizes them (the perf windows from one phase mark to the next; for each run the offset between `e2e.py`'s clock and the perf trace was chosen so that these windows reproduce the run's `summary.json` exactly). The menu is the windows from the main menu showing PLAY CAREER to START RACE selected (about 28 s: the main menu, Career, World Tour and the event screens). `REX_PM4_STATS` lines are one per perf window; an `FM4_N0_TRACE` line covers the second before its steady clock, and a phase takes the whole seconds inside its windows (the rule of `e2e.py`'s `steady_by_phase`). fps is frames over time.

| Run | Settings | Build |
|---|---|---|
| `n0-career-c` | `FM4_N0_TRACE`, `REX_PM4_STATS` | 1 |
| `n0-msaa-b` | the same with `-Msaa` (three predicated tiles, 4x MSAA) | 1 |
| `n0-key`, `n0-key-msaa` | `FM4_N0_KEYCHECK` and `REX_LATE_WRITE_TRACE`, one tile and `-Msaa` | 2 |
| `n0-key2` | `FM4_N0_KEYCHECK` with the checks of copies and of later executions | 3 |
| `n0-p1x`, `n0-p1x-split`, `n0-p2x-split` | `photo`, three runs each: no counters; `REX_CP_SPLIT=1`; `REX_CP_SPLIT=1` at 2x | 3 |

The three builds are `8d987f5` with the counters as they grew that day: build 1 without the content links of 13.2, build 2 with them, build 3 also with the copy and first-execution checks of 13.3 (the counters as committed); the other code is the same in all three.

The counters cost frame rate (the `career` race ran at 196.5 fps with `FM4_N0_TRACE` and 153.9 fps with `FM4_N0_KEYCHECK`), so the fps of the counter runs are not performance figures; those come from the `n0-p*` runs. Earlier runs of the same day (`n0-career`, `n0-career-b`, `n0-msaa`) are superseded: their counters were incomplete, and `n0-msaa` crashed in a summary writer that read freed guest memory (fixed before `n0-career-c`).

### 13.2 Replays and recordings

| Per frame (`n0-career-c`, `n0-msaa-b`) | Race, one tile | Race, tiles | Replay, one tile | Replay, tiles | Menu |
|---|---|---|---|---|---|
| fps in the counter run | 196.5 | 166.8 | 113.7 | 98.3 | 54.8 |
| Draws executed | 2,108 | 2,681 | 1,835 | 2,700 | 704 |
| `RunCommandBuffer` | 739 | 733 | 625 | 648 | 19 |
| of which the inline path | 739 | 733 | 625 | 648 | 19 |
| `sub_826E4A28` (deferred, copied, queued, direct) | 0 | 0 | 0 | 0 | 0 |
| `INDIRECT_BUFFER` executed (skipped by predication) | 853 | 1,250 (804) | 709 | 1,062 (676) | 101 (13) |
| Distinct buffers replayed per second | 373 | 368 | 213 | 212 | 4.9 |
| `BeginCommandBuffer` per second | 0 | 0 | 0 | 0 | 4.4 |

- Every replay took a path that section 2.4 did not describe: when bit `0x20` of the buffer's flags at +108 is clear, `RunCommandBuffer` writes one `INDIRECT_BUFFER` per descriptor straight into the segment it is writing (`loc_82310CF8`), and only with the bit set does it close the segment and go through `sub_826E4BE0` and `sub_826E4A28`. In these runs the bit was never set, and `sub_826E4A28` was not called at all, from `RunCommandBuffer` or from the swap. Each replayed buffer had one descriptor.
- Recordings happen at load, not per frame: 10,209 `BeginCommandBuffer`/`EndCommandBuffer` pairs in a run, 8,705 of them on a second guest thread (13.7), and 4.4 per second in the menu, with as many `D3DCommandBuffer_CreateClone` calls.
- The buffer objects that `RunCommandBuffer` replays are not the recorded objects, and they do not point at the recorded PM4 chunks: in the race of `n0-career-c` 99.9% of the replays were of buffer objects never seen at `EndCommandBuffer`. With `FM4_N0_KEYCHECK`, 54% of the race replays (`n0-key2`; 48% in `n0-key`), 65% of the replay phase (66% in `n0-key`) and 43 to 47% of the menu execute PM4 that is byte-identical to a recording of the same run (1 minus `unseen` over `run` of the `cb` counters; the `same` counter compares a buffer with its own previous replay and is not this share), so those are copies made by game code (not through `CreateClone`); the rest is PM4 that no `EndCommandBuffer` of the run produced (inferred: command buffers built with the assets). The 16 most replayed buffers of each run are resource type 9 (`0x00400009` or `0x00500009` at +0), most of them in physical memory.
- Reuse is high: `n0-career-c` replays 4,289 distinct buffer objects (`n0-msaa-b` 4,259), 3,039 of them 256 to 8,191 times. In the one-tile race 373 distinct buffers a second take 739 replays a frame, so a buffer is replayed about twice a frame, which makes 256 to 8,191 replays about 130 to 4,100 frames. With the PM4 hashed at every replay, 0.02% of the race replays found the bytes of a buffer changed since its previous replay (22.8 of 115,576 per second in `n0-key2`); whether that is a patch or a new buffer at the same address is not distinguished.

### 13.3 Shader keys and constant blocks

`n0-key2` hashed every `IM_LOAD` source and every `LOAD_ALU_CONSTANT` block when `sub_823436B8` wrote the packet, gave byte copies of recordings the hashes of the recording's packets, and compared them with the bytes the command processor reads when it executes the packet. Packets that no hook saw being written (in the replayed buffers that are not copies of a recording) are compared with their first execution.

| `n0-key2`, executions | Race | Replay | Menu |
|---|---|---|---|
| `IM_LOAD` written into the ring: checked, changed | 1,393,364, 0 | 322,123, 0 | 617,863, 0 |
| `IM_LOAD` written into a recording: checked, changed | 155,676, 2,544 (1.63%) | 155,584, 25,900 (16.6%) | 17, 5 |
| `IM_LOAD` against the first execution: checked, changed | 16,433,654, 4 | 6,755,512, 4 | 231,132, 0 |
| `LOAD_ALU_CONSTANT` in the ring: checked, changed | 893,848, 0 | 168,500, 0 | 515,183, 0 |
| `LOAD_ALU_CONSTANT` in a recording: checked, changed | 135,671, 2,496 (1.84%) | 32,232, 4,228 (13.1%) | 16, 0 |

- A key taken where `IM_LOAD` is written matches the runtime's key for every packet written into the ring. For packets inside recordings it does not: between the recording and the replay the XDK's vertex fetch rewrite (2.6) changes the microcode in place, so the replay loads other bytes (56 distinct packet and key pairs in the run). The console GPU also reads the microcode when it executes the packet, so the runtime's key is the faithful one, and a native path has to hash recorded `IM_LOAD`s at replay, not at recording.
- The `LOAD_ALU_CONSTANT` blocks of recordings are rewritten by the CPU between recording and replay as well (1.84% in the race, 13.1% in the replay phase); blocks referenced from the ring were never rewritten before execution. A decoded recording therefore has to re-read its constant blocks at every replay, as section 8 already plans.
- `n0-key` and `n0-key-msaa` (without the copy and first-execution checks, so they could check only the recordings replayed as themselves) agree for the ring: 0 changes in 982,155 and 1,149,275 ring `IM_LOAD`s of the race, and 0 in the 8,958 and 7,966 recorded ones.

### 13.4 Late writes

`REX_LATE_WRITE_TRACE` (guest memory the command processor consumed and changed before the next signal the CPU can observe), mapped to the phases by its steady clock: in `n0-key` (one tile) no changed range in 15,907 frames and 31.2 million checked ranges (13.2 million in the race). In `n0-key-msaa` (tiles) 25 changed ranges in 14,977 frames and 24.6 million checked: 0 in the race (9.8 million checked), 17 in the replay phase and 8 in the menus between the race and the replay, all of them one vertex range (`0x115A6A00`, 252,000 bytes, vertex shader `82130FF0`). No indirect buffer, constant block or shader changed after the command processor read it.

### 13.5 Coverage of the PM4 writers

The ring allocations by return address of `n0-career-c` and `n0-msaa-b` (boot, menus, a race and a replay each), each return address taken to the function that contains it, come from 26 functions of the static list of 71 (a mapping to the nearest entry of the list gives 27) and from these 19 functions it does not have:

| Function | Seen as | Note |
|---|---|---|
| `sub_822B7248` | `BeginCommand` | once per tiling bracket (4,580 calls one tile, 20,881 tiles) |
| `sub_826F2F60` (`D3DDevice_UpdateScaler`) | `BeginCommand`, overflow | once per run |
| `sub_8236F838` (`CTileBuffer` helper) | `AllocateRingSpace` (three sites) | per tiling bracket; it also calls the `WAIT_REG_MEM` writer `sub_822DB108` |
| `sub_823719A8` | `AllocateRingSpace` | the tiling stream, about 1,000 dwords per call |
| `sub_826E1EC0` | `AllocateRingSpace` | once per `BeginCommandBuffer` |
| `sub_822FEF08` (`D3DDevice_CommandBufferOverflow`) | `AllocateRingSpace` | on every segment change, with a `WAIT_REG_MEM` through `sub_822DB108` |
| `sub_823102D0` | overflow | 614,621 to 980,500 calls a run; next to `CCarModel::RunPartCommandBuffer` |
| `sub_822C5260` | overflow | about 108,000 calls a run |
| `sub_82379530` (`D3DDevice_WriteRegistersChunked`), `sub_8230FA40` (`WriteDirtyFetchConstants`) | overflow | type-0 writers, which the static scan cannot find |
| `sub_82353F18`, `sub_8239DDA0`, `sub_822BA060` (`SetShaderGPRAllocation`), `sub_822B0788`, `sub_826F4D00`, `sub_82371B80` (`BeginTiling`), `sub_8236FA20` (`EndTiling`), `sub_826E23E8` (`BeginCommandBuffer`), `sub_823913D0` (`BeginCommand`) | overflow | inline writers that overflowed a segment |

The overflow handler only shows inline writers that happened to fill a segment, so this list is a lower bound for them; type-0 and inline type-3 writers that never overflowed in these scenes are not proven absent.

### 13.6 Synchronization packets and occlusion

| Per frame (`n0-career-c`, `n0-msaa-b`) | Race, one tile | Race, tiles | Replay, one tile | Menu |
|---|---|---|---|---|
| `EVENT_WRITE_ZPD` | 2.0 | 6.0 | 2.0 | 0 |
| `REG_TO_MEM`, `COND_WRITE`, `MEM_WRITE` | 0 | 0 | 0 | 0 |
| `REG_RMW` | 1.0 | 1.0 | 1.0 | 1.0 |
| `WAIT_REG_MEM` executed (skipped) | 370 | 447 (6) | 370 | 48 (12) |
| `EVENT_WRITE_SHD` | 99 | 30 | 60 | 22 |
| `INTERRUPT` executed (skipped) | 2.0 | 4.0 (2.0) | 2.0 | 6.0 (4.0) |

The command processor waited 0 ms for occlusion results in every phase (`wait_ms.occlusion`), so the GPU wait per occlusion report is 0 with the launcher defaults. The swap's vblank wait is one `WAIT_REG_MEM` per frame; most of the others are probably the waits that `sub_822FEF08` writes at every segment change (13.5; inferred, not counted per phase).

### 13.7 Threads and the render thread's frame

| Guest thread | Calls in `n0-msaa-b` |
|---|---|
| Render thread (`sub_822DB318`) | every `Swap`, `RunCommandBuffer`, `Resolve` and `Clear`, every draw that reaches the frame directly, 63.4 million `SetTexture`, 1,504 recordings, `BeginTiling` and `EndTiling` |
| A second thread | 8,705 recordings, 48 of the 64 `RecordCommandBuffers` calls, 395,318 `SetTexture` and 13,578 `DrawIndexedVertices` (into recordings), at load |
| The tile stream worker | `sub_826EDFA8` once (it never returns), `sub_826ED978` 41,781 and `sub_826EDCE8` 41,758 times, about twice per `BeginTiling` (20,881) |

Every `RunCommandBuffer` ran on the render thread, 99.9% of them through `CRenderEngine::RunCommandBuffer`, so its queue to another thread (2.4) was not used in these scenes. The calls of `sub_822DB318` that presented a frame took 4.97 ms each in the one-tile race (196.7 a second, 977 ms/s) and 5.96 ms in the tiled race (166.5 a second, 992 ms/s): in the race the guest render thread is busy for almost the whole frame, at the same time as the command processor (940 and 957 ms/s). In the replay phase it took 7.22 ms (one tile) and 3.09 ms (tiles) per frame and spun through calls without a frame (3,657 and 17,387 a second). These figures are per `n0.jsonl` second of the phase.

### 13.8 Command processor time split

The `photo` race (12 cars, the launcher defaults), three runs each. The first rows are the harness's per-run figures (median of the three runs); the rest are ms per second of the command processor thread over the race windows of the three runs together (time-weighted, so the rows add up). `REX_CP_SPLIT` times the packets by kind (registers: type 0 and 1, `SET_CONSTANT2`, `LOAD_ALU_CONSTANT`, bin masks and selects; shaders: `IM_LOAD`; draws: `DRAW_INDX`, `DRAW_INDX_2`, which contain the `draw` zone, which contains the `copy` zone). The waits that happen inside draw packets (`frame_latency`, `resolve_readback`, `readback_publish`) are taken out of the draw packets and of the `draw` zone, at 1x and 2x alike; "other waits" are every wait but the vblank and the frame latency (guest idle, readbacks). "Rest" is the busy time outside every timed kind and the swap: packet dispatch, indirect buffers, and `WAIT_REG_MEM` and `EVENT_WRITE_ZPD` outside their waits.

| Race phase | 1x, no split (`n0-p1x`) | 1x (`n0-p1x-split`) | 2x (`n0-p2x-split`) |
|---|---|---|---|
| fps (frames over time) and p99 frame time, median of the runs | 217.5, 6.91 ms | 206.6, 7.17 ms | 155.5, 10.46 ms |
| fps of the three runs | 196.6 to 220.0 | 188.8 to 228.2 | 153.6 to 158.9 |
| GPU per frame (median of runs) | 2.89 ms | 2.90 ms | 6.23 ms |
| Command processor busy (median of runs) | 803 | 855 | 678 |
| Draws per frame of the three runs | 1,546 to 2,065 | 1,486 to 2,106 | 1,517 to 1,787 |
| Command processor busy, three runs together | 817 | 832 | 667 |
| Busy per draw | 2.21 us | 2.27 us | 2.52 us |
| Wait for the vblank (`WAIT_REG_MEM`) | 157 | 152 | 16 |
| Wait for the GPU (frame latency) | 0 | 0 | 308 |
| Other waits | 26 | 16 | 10 |
| Register packets | | 108 (28,827 packets per frame) | 80 |
| Shader loads | | 30 | 23 |
| Draw packets | | 575 | 472 |
| of which the `draw` zone, resolves included | 586 | 564 | 464 |
| of which resolves (`copy` zone) | 73 | 64 | 46 |
| Other timed packets | | 4.8 | 5.3 |
| Swap | 11 | 10 | 8.8 |
| Rest: dispatch, indirect buffers, untimed packets | | 104 | 78 |

- At 1x the busy time splits into draw packets 69% (the `draw` zone 68%: draws 60% and resolves 7.6%; the packet handling around them 1.3%), register packets 13%, the rest 12.5%, shader loads 3.6%, the swap 1.2% and other packets 0.6%. Against the profile of 20 October 2026 behind section 6 (draws 60%, parsing, registers and other packets 24%, swap 13%, copies 3%), the asynchronous swap took the swap from 13% to about 1%, and the front end (registers, shader loads and the rest: about 242 ms/s, 29%) is what a native producer can remove or move off this thread; the draw work stays with the backend in N1.
- `REX_CP_SPLIT` costs about 3 to 5% of the command processor's time per draw (2.27 against 2.21 us over the three runs together, 2.30 against 2.20 us as the median of the runs), mostly in the register packets; the runs of one cell differ by about as much (2.17 to 2.34 us with the split).
- At 2x the GPU sets the frame rate: the command processor waits 308 ms/s for frames in flight, the busy time per draw is 2.52 us, and the split is otherwise similar (draw packets 71%, registers 12%, the rest 12%).

## 14. Stage N1 design, 21 October 2026

The design of stage N1 (section 9) on the code as it is: what the host render thread replaces, what a record is, how converted and unconverted writers stay in order, and the order of the increments. The modes are the three tiers of section 11.

### 14.1 What the command processor thread does today

- `GraphicsSystem::WriteRegister` turns the guest's write of `CP_RB_WPTR` into `CommandProcessor::UpdateWritePointer`, which stores the index and sets an event.
- `CommandProcessor::WorkerThreadMain` (the thread "GPU Commands") spins on that index (500 polls with `PollIdleWork`, then 5 ms event waits), runs `CallInThread` functions from a queue without a lock, executes the ring from its read index to the write index (`ExecutePrimaryBuffer`) and writes the read index back to the guest (`CP_RB_RPTR_ADDR`) after each batch.
- Every guest-visible effect of a packet is made inside its handler, so on the thread that executes the packet: fences and the swap counter (`EVENT_WRITE_SHD`, `MEM_WRITE`, `COND_WRITE`, `REG_TO_MEM`), occlusion reports (`EVENT_WRITE_ZPD`), the waits on guest memory (`WAIT_REG_MEM`), the interrupts (`INTERRUPT`) and the swap. Two writes are made elsewhere and stay there in N1: resolve readbacks are copied into guest memory on the readback thread, ordered by the waits of the thread that issued them, and the vblank interrupt runs the guest's callback on the vsync thread, which also increments the counter that `EVENT_WRITE_SHD` writes.
- The backend (`D3D12CommandProcessor`) assumes one producer thread: the submission state has no lock, and `PollIdleWork` flushes pending occlusion snapshots between batches.

### 14.2 The host render thread and its records

- One host render thread replaces the command processor thread on the native path. It is the only thread that drives the backend, and it makes every guest-visible write that the command processor thread made (14.1). The vblank interrupt and the readback copies stay on their threads in N1 and move to the host render thread in N4.
- Producers write fixed records into one bounded queue, in order, with a global sequence number. A record is one of: a PM4 span of the ring (the fallback), a call (`CallInThread`, now behind the queue's lock), and later the native kinds of 14.3 (register deltas, draws, shader loads, constant blocks, replays, resolves, events, waits, swap).
- A full queue never blocks the guest: the guest may be inside the XDK's ring wait for an index only this thread publishes, so blocking the producer could deadlock. A new PM4 span merges into the last span instead (the merged span stays shorter than the ring, because the guest never writes past the published read index). Native records that cannot merge make the producer wait, which is safe only once no writer waits for the ring from inside a record (checked in increment 3).
- The consumer waits on the queue's event, not in a spin loop; while the backend has pending work (`PollIdleWork`) it waits with a timeout of 1 ms.
- After each record the consumer publishes the ring position the record ends at (the consumed ring index of 2.7), so the XDK's ring index waits work for converted writers as well.
- The perf trace gets the backend-neutral fields of [frozen-frame parity](../e2e/frozen-frames.md): `render_thread_busy_ms`, `queue_depth_mean`, `queue_depth_max`, and the records and merged spans per window.

### 14.3 Converted writers and the order with PM4

- "PM4 written" in the gates of N1 means PM4 translated by the command processor in the ring and its command buffer segments: a converted writer leaves only its marker (below). A converted writer replaces its PM4 with a record when it writes into the ring. When the device's current recording (device+13904) is set, it keeps writing PM4 into the recording, because recordings replay as PM4 in N1 (section 9).
- Order with the unconverted writers (the main session's decision of 22 October 2026, replacing a position rule): a converted writer writes, in place of its PM4, a type-3 `NOP` marker into the same segment (three dwords: the header, a tag and the low 32 bits of the record's sequence), and its record goes into a table keyed by the marker's guest physical address (the table of the identity check, 14.7). When the host render thread reaches the marker through the normal ring and indirect buffer execution, it takes the record and applies it. The record therefore runs exactly where its PM4 would have run, after the guest's kick and never ahead of it, through segment switches and allocators, without any assumption that the PM4 before device+48 is final. A marker without a record (overwritten or never produced) is an error, because the state would then be wrong.
- Converted writers no longer advance the ring, so the ring indices the guest waits on count PM4 only, and `D3DDevice_CommandBufferOverflow` (`sub_822FEF08`) sees fewer segment changes.
- The identity check (the state-level dual run of section 10) runs in a check mode: the hook also runs the original writer into a scratch segment, the host render thread decodes that PM4 into register writes, draw parameters and the predicate decision, and compares them with the record; a mismatch goes through `REXGPU_ERROR` with the writer and the guest address, so it shows as a new error key.

### 14.4 Writers by scene

From the ring allocations of `n0-career-c` (13.5). The counts are for the whole run of boot, menus, a race and a replay, because the coverage log is per run, not per phase:

| Writer | Ring allocations or overflows in the run | Menus | One-tile race | Replay |
|---|---|---|---|---|
| Dirty writers `sub_823114F8`, `sub_82310E78`, `sub_8230FA40`, `sub_82379530` | inline; 52,807 overflows in `sub_82379530` | yes | yes | yes |
| `DRAW_INDX_2` writer `sub_82370370` (clears, screen passes) | 413,338 | yes | yes | yes |
| `DrawIndexedVertices`, `DrawVertices`, `DrawVerticesUP`, `BeginVertices` | inline | yes | yes | yes |
| `IM_LOAD` and `LOAD_ALU_CONSTANT` (`sub_823436B8`, `sub_82343608`) | inline | yes | yes | yes |
| `RunCommandBuffer` (`sub_82310818`, inline `INDIRECT_BUFFER`) and `sub_823102D0` | 614,621 overflows in `sub_823102D0` | 19 a frame | 739 a frame | 625 a frame |
| `Resolve` body `sub_82348DC8` | 665,001 and 190,574 (31.5 million dwords) | yes | yes | yes |
| `Swap` `sub_8237FA28` with `BeginCommand` | 22,889 | yes | yes | yes |
| Ring waits `sub_822DB108` and `CommandBufferOverflow` `sub_822FEF08` | 817,081 and 812,501 | yes | yes | yes |
| The tiling bracket with one tile (`BeginTiling`, `EndTiling`, `sub_822B7248`, `sub_8236F838`, `sub_823719A8`) | 4,580 brackets, 32,190 stream writes | yes (measured; inferred for the race until 14.8) | | |
| `EVENT_WRITE_ZPD` | | | 2 a frame | 2 a frame |
| `BeginCommandBuffer` (`sub_826E1EC0`) | 10,209 | 4.4 a second | at load | |

The table above is inferred from 13.2, 13.6 and 13.7. Measured by phase on 21 October 2026 (`n2chk5-career`, 14.8: the `career` scenario with `FM4_N0_TRACE`, the identity check and the host render thread; menus from PLAY CAREER to START RACE, 29 s and 1,612 frames; the one-tile race, 42 s and 4,783 frames; the replay, 30 s and 2,723 frames). Per frame: ring allocations through `AllocateRingSpace` (A) or `BeginCommand` (B) and segment overflows (O) by the function that called them, and the ranges the identity check compared by outermost hooked writer (checked). Writers below 0.01 a frame in every phase are left out.

| Writer | Menus | One-tile race | Replay |
|---|---|---|---|
| `WriteDirtyRegisters` `sub_823114F8` | checked 102.7 | checked 193.7 | checked 278.5 |
| `WriteDirtyFloatConstants` `sub_82310E78` | checked 34.5 | checked 824.1 | checked 922.1 |
| `WriteDirtyFetchConstants` `sub_8230FA40` | checked 1.7, O 0.03 | checked 70.9, O 0.02 | checked 74.6, O 0.01 |
| `D3DDevice_WriteRegistersChunked` `sub_82379530` | O 0.30 | O 2.29 | O 2.42 |
| `DrawIndexedVertices` `sub_82311080` | checked 158.8, O 0.24 | checked 188.7, O 0.18 | checked 29.8, O 0.04 |
| `DrawVertices` `sub_8233AC60` | checked 16.3 | checked 28.7 | checked 34.0 |
| `DrawVerticesUP` `sub_822B95D8` | | checked 2.0 | |
| `DRAW_INDX_2` writer `sub_82370370` | checked 15.1, A 14.40 | checked 19.5, A 19.02 | checked 19.0, A 18.41 |
| Draw flush `sub_823436B8` (outermost) | checked 0.7 | checked 7.6 | checked 13.1 |
| `LOAD_ALU_CONSTANT` writer `sub_82343608` | | O 0.10 | O 0.01 |
| `BeginVertices` `sub_8234D278` | A 0.42 | A 17.32 | A 0.07 |
| `BeginIndexedVertices` `sub_8234DAE8` | | A 2.46 | A 5.71 |
| `RunCommandBuffer` `sub_82310818` and `sub_823102D0` | O 12.77 | O 0.47 and 37.76 | O 0.49 and 22.26 |
| `Resolve` body `sub_82348DC8` | A 20.54 | A 28.88, O 0.03 | A 35.60, O 0.04 |
| `Swap` `sub_8237FA28` | B 0.95, O 0.95 | B 0.98, O 0.98 | B 0.97, O 0.97 |
| Ring wait `sub_822DB108` | A 10.09 | A 46.86 | A 31.30 |
| `CommandBufferOverflow` `sub_822FEF08` | A 8.19 | A 46.86 | A 31.30 |
| `sub_822C5260` | O 5.32 | O 4.88 | O 4.84 |
| `sub_826EC9C0` | B 0.12 | B 1.30 | |
| The tiling bracket: `BeginTiling` `sub_82371B80`, `EndTiling` `sub_8236FA20`, `sub_822B7248`, `sub_8236F838`, `sub_823719A8` | O 1.90, O 1.90, B 1.90, A 5.69, A 13.35 | | |
| `BeginCommandBuffer` `sub_826E23E8` and `sub_826E1EC0` | O 0.08, A 0.08 | | |
| `sub_8239DDA0`, `sub_82310530`, `sub_82353F18`, `sub_82333538`, `sub_822EBE20`, `sub_822BF380` | up to 0.02 | up to 0.03 | up to 0.06 |

- The menus run the tiling bracket about twice a frame and the one-tile race and the replay never do (the first version of the table above, `3669bdb`, had it the other way round). The menus also record command buffers (0.08 a frame); the race and the replay record none.
- `EVENT_WRITE_ZPD` and the ring waits of `BlockUntilNotBusy` (`sub_82386C48`) are inline writers without an allocation in these phases; their counts per frame are in 13.6.
- The allocations and overflows are a lower bound for the inline writers, as in 13.5; the checked ranges are exact for the hooked writers, counted for the outermost one only (a dirty writer inside `DrawIndexedVertices` counts as `DrawIndexedVertices`).

### 14.5 Increments

| Increment | Scope | Gate |
|---|---|---|
| 1 | The host render thread and the record queue, with every writer on the fallback: PM4 spans and calls only. Behind the cvar `host_render_thread` and the launcher option `-Native`, off by default. `-Native` with the tiles (`-Msaa`, a look without TAA) keeps the PM4 RTV path, and `-Native` with `-Rov` is refused. | A menu, a one-tile race and a replay without a crash and without a new error key; the `compare.py` smoke and perf gates and `parity.py` on `freeze` against RTV of the same commit. |
| 2 | The coverage log by phase, and the identity check of 14.3 with its decoder. | The coverage list per scene recorded; the check finds no mismatch on the fallback (PM4 against itself). |
| 3 | The dirty writers become register delta records behind markers (14.9). | The identity check without mismatches in the coverage scenes; the gates of increment 1. |
| 4 (deferred, 14.11) | The menu writers: draws, `sub_82370370`, shader loads and constant blocks (copied into the record when the queue may run ahead of the guest), resolves, the swap, the ring waits, the tiling bracket with one tile (the menus run it about twice a frame, 14.4), and `RunCommandBuffer` as a replay record (the host render thread executes the recorded PM4 through `ExecuteIndirectBuffer`; the menus replay 19 buffers a frame). | The identity check; no PM4 translated in the menus beyond markers (recordings keep their PM4); the gates of increment 1. |
| 5 (deferred, 14.11) | The race and replay writers: `BeginVertices` and `BeginIndexedVertices`, `EVENT_WRITE_ZPD`, the remaining fences, and the replay record at the race's rate. | The exit criteria of N1 (section 9); photo mode, split screen and the intro videos smoke checked on the fallback. |

### 14.6 Increment 1 results, 21 October 2026

On `I:\FM4-build-usa` (USA release), commit `e720054`, one binary for both paths (`fm4_rex.exe` SHA-256 `3ddfa5821c72ed7dae71b2ad0f48d70d3b73026db7b0234ed43bab6fd6ae5814`, `rexgpu-xenos.dll` `99f06d47c2afc5944eb220459dbe7420f277e3512518100c9750487277e1ddf7`, `rexruntime.dll` `22a1c4c7729a46b712a706fde9d28b8fffdadf10bee13928aa5a8faac47189ef`): RTV with `host_render_thread` off, and the host render thread with it on. The smoke and perf gates ran with the launcher defaults at 240 Hz and 1x, the title's AI driving; the `freeze` pair ran at 60 Hz with the scenario's pins (below).

| Gate | RTV | Host render thread | `compare.py` or `parity.py` |
|---|---|---|---|
| Smoke, `photo` race (`n1rtv2`, `n1nat2`) | 219.4 fps, p99 6.39 ms | 237.3 fps, p99 5.09 ms | WARN on capture statistics only |
| Smoke, `photo_view` | 56.96 fps | 56.97 fps | WARN on capture statistics only |
| Smoke, `career` race | 225.2 fps, p99 6.27 ms | 224.1 fps, p99 6.36 ms | PASS |
| Smoke, `career` replay | 117.1 fps, p99 10.26 ms | 118.1 fps, p99 9.91 ms | PASS |
| Perf, `photo` race, three runs (`n1rtvp2`, `n1natp2`), median of the runs | 229.4 fps, p99 5.48 ms (runs 226.5 to 237.5 fps) | 219.6 fps, p99 6.08 ms (runs 209.3 to 228.2 fps) | WARN on capture statistics only (black share +10.2 points) |
| `parity.py`, `freeze` with one tile (`n1fz2-rtv`, `n1fz2-nat`) | | draws -0.6%, predicated run -1.5%, image 23.03% | PASS |

- In the perf gate the race ran 4.3% slower on the host render thread by the medians of the runs, with a p99 10.9% higher (by the means of the three runs: -5.2% and +11.0%); both are inside the limits of `compare.py` (FAIL at -10% and +20%).
- Every cell reached its goal. Both paths log the same error keys: three in every cell but `photo_view`, which logs two on both (no `BaseHeap::Release` key). The native runs log no packet overflow (`ExecutePacketType0 overflow`, `ExecutePacketType3 overflow`) and no "Failed to execute packet".
- The perf difference follows the draw load of the AI races: over the race windows of the three runs together the host render thread was busy 1.833 us per draw at 2,024 draws a frame, the command processor thread 1.786 us at 1,769 (+2.6% per draw). Submissions per race frame are the same (2.97 in `career` on both).
- The host render thread takes about one record a frame in the race (1.1, after merging consecutive spans): it waits for the vblank inside the swap's `WAIT_REG_MEM` while the guest kicks the next frame, as the command processor thread did. The queue holds a median of 52.6 to 60.9 records when a record is taken in the three perf runs (at most 210), and 56.9 and 72.2 in the smoke races of `photo` and `career` (at most 214 and 256, the capacity); each record is one kick of the write pointer.
- The first build of the same day (`8ced80d`, `n1nat`) executed each kick as its own span and ended a submission per kick: 45.2 submissions per `career` race frame against 2.9, 198.4 against 223.1 fps (FAIL). Merging consecutive spans (`e720054`) removed it.
- The freeze pair ran at 60 Hz with the `freeze` scenario's pins (`original,taa=0`, the 2x draw scale, the fake occlusion), `FM4_FIXED_SEED=1` and one tile without TAA (`--env FM4_TILING_SCENARIO=2`), because `-Native` covers one tile. Both runs capture the phase `freeze` at 2560x1440, as the tiled `freeze` runs of the same day did (`fzpin-rtv-a`); their warning "draw resolution scaling is enabled, but the swap source is unscaled (1280x720)" is logged once, at the first swap of a boot screen, as in the 2x run `n0-p2x-split` with the `aces` look; see [frozen-frame parity](../e2e/frozen-frames.md#the-freeze-scenario). The image share is within the pairs of RTV runs ([baseline variance](../e2e/frozen-frames.md#baseline-variance)).
- Against the success criteria of section 10, increment 1 does not meet them yet, as expected with every producer still on PM4: the host render thread is busy about 3.7 ms per race frame (target 2.5 ms) and the race p99 is 6.08 ms (target 5.0 ms).
- Not covered: 60 and 120 Hz for the smoke and perf gates, a 2x swap, the ROG Xbox Ally X, photo mode's own captures, split screen and the intro videos, and the identity check (increment 2).

### 14.7 Increment 2 design: coverage by phase and the identity check

Two parts, both off by default, with the PM4 RTV path and `-Rov` unchanged.

**Coverage by phase.** `FM4_N0_TRACE` adds the ring allocations and segment overflows of each second to its line in `n0.jsonl` (`"alloc"`, by allocator and return address, only those that changed), next to the run-wide table of `<file>.summary`. `coverage.py` maps each return address to the function that contains it (the greatest `sub_` address at or below it, from the function list of the generated code in the BuildRoot) and sums the seconds of each phase of `summary.json` (`phase_steady_ms`, the clock of `steady_ms`). The identity check below counts its checked ranges per writer and second as well, so the hooked writers are measured exactly, not only through their allocations and overflows.

**The identity check** (`FM4_NATIVE_CHECK=1` in the title and `host_render_check=true` in the runtime, with `host_render_thread`):

- Writers hooked in increment 2: the dirty writers `sub_823114F8`, `sub_82310E78`, `sub_8230FA40` and `sub_82379530`, the draw flush `sub_823436B8`, and the draw writers `DrawIndexedVertices` (`sub_82311080`), `DrawVertices` (`sub_8233AC60`), `DrawVerticesUP` (`sub_822B95D8`) and `sub_82370370`. These are the writers of increments 3 and 4 that write register state, shader loads and draws.
- Producer: a hook reads the device's ring write pointer (device+48, the last dword written) before and after the original body, on the outermost hooked writer of the thread only (the draw writers call the flush and the dirty writers). The PM4 between the two is copied into an entry of a table keyed by the guest physical address of its first packet, with the writer and a sequence number: in increment 2 this copy is the "record" (the scratch segment of 14.3 is the ring itself, because the writer still writes PM4 there). The write pointer is in a command buffer segment, not in the 32 KB primary ring, and a ring allocation inside the writer (`BeginCommand` `sub_823913D0`, `AllocateRingSpace` `sub_8234D738`, the segment overflow `sub_822FEF08`) may move it to another segment, so each of them ends the range before the call and starts a new one after it; the PM4 the allocators write themselves is not copied. `D3DDevice_WriteRegistersChunked` (`sub_82379530`) does not keep device+48 current (it writes at the pointer in r4 and returns the new one in r3), so its range comes from those registers, and one with an allocation inside is dropped and counted. Nothing is recorded while the device records a command buffer (device+13904 set): that PM4 is not in the ring. A range is recorded before the next allocation, which is where the XDK kicks a segment; a writer that kicked the write pointer outside an allocation would let the host render thread reach the range before its copy exists, and that range would go unchecked without a count; the checked ranges per writer against the writer's calls would show it.
- Consumer: when the host render thread reaches a packet whose address has an entry, it takes the entry and decodes the copy (`pm4_decode`): register writes (type 0, type 1, `SET_CONSTANT`, `SET_CONSTANT2`, `SET_SHADER_CONSTANTS`), draws (`DRAW_INDX`, `DRAW_INDX_2`: the draw initiator, index base and size), shader loads, constant loads, bin masks and selects, and for each type-3 packet the predicate decision, simulated from the bin state at the start of the range. It then executes the range as usual and records what the command processor did: each type-3 packet's opcode and whether it ran, and each draw's parameters. At the end of the range it compares:
  - the PM4 in guest memory at the end of the range with the copy (a writer or the CPU changed it after production: the assumption of 14.3 that PM4 before device+48 is final);
  - each register's last value in the decoded writes with the register file. Not compared: the registers loaded from memory (`LOAD_ALU_CONSTANT`) or read back (`REG_RMW`), the registers the command processor writes itself (the event initiator of the `EVENT_WRITE` packets and `VIZ_QUERY`, the viz query status), the coherency request `COHER_STATUS_HOST` (written with bit 31 set and cleared by `MakeCoherent`) and the gamma ramp port (`DC_LUT_*`, whose index the data writes advance); a range with an `INDIRECT_BUFFER` or a packet the decoder does not model (`COND_WRITE` and any unknown opcode) is not compared at all;
  - the draws and the predicate decisions, in order.
- A mismatch goes through `REXGPU_ERROR` with the writer, the guest address and the first difference ("Native check: ..."), so it is a new error key in `compare.py` and `parity.py`; after 64 messages of a kind only the counts go on. The perf trace's `host_render` block gets the counts ([traces](../e2e/traces.md#identity-check-of-the-native-path-fm4_native_check-host_render_check)): ranges compared by writer, mismatches by kind and register, copies overwritten before they were reached, ranges left before their end, ranges dropped by a hook and ranges perturbed. A range ends at the address of the next packet or at the end of the buffer that holds it.
- `host_render_check_perturb=N` changes the decoded copy of every Nth range (the last value of its first register write, or the index count of its first draw) before the comparison, so a run with it must report one mismatch per perturbed range (`check_mismatches` equal to `check_perturbed`): the proof that the check catches a difference.
- Cost: with the check off, each hook tests one static flag and the packet loop one member flag. With it on, a copy per hooked call, a table insert and lookup per range and a decode on the host render thread; measured with the perf gate.

In increment 3 the dirty writers' records become native (register deltas read from the device shadow before the original body runs into a scratch segment instead of the ring), and the same decoder and comparison check them against that PM4.

### 14.8 Increment 2 results, 21 and 22 October 2026

On `I:\FM4-build-usa` (USA release), commit `1bf8233` (`fm4_rex.exe` SHA-256 `f3e2ae8574f0ffa54a0aea0940806818ed2a2be6386880496d2738d9f2bec8d1`, `rexgpu-xenos.dll` `826205d716040999ac2512daf0613b51656ba7ce20e4164bec879490b1a8b1b6`, `rexruntime.dll` `b57464c77c3009a172784e5c4f85a041bdcd4a2b0d3e466935ac2a6f42450a7f`), the host render thread, the launcher defaults at 240 Hz and 1x, the title's AI driving.

| Run | Setting | Ranges compared | Mismatches | Other |
|---|---|---|---|---|
| `n2chk5-career` (menus, one-tile race, replay) | check, `FM4_N0_TRACE` | 16,863,547 | 0 | 16 left before their end, 138 dropped, 0 overwritten |
| `n2prt-career` | check, `host_render_check_perturb=1000` | 16,214,571 | 16,095, all registers | 16,095 perturbed |
| `n2chkp` (perf, `photo`, three runs) | check | | 0 (no new error key) | |

- The fallback passes its own identity check in the menus, the one-tile race and the replay: no register, type-3 packet, predicate decision, draw or byte of PM4 differed. The perturbed run reported exactly the ranges it changed, so the comparison catches a difference.
- The first runs found what the check had to learn: `COHER_STATUS_HOST`, which the command processor rewrites, gave 24,869 mismatches in `n2chk-career`, and ranges that crossed into another command buffer segment through `AllocateRingSpace` or `BeginCommand` were left unfinished about once a frame in the menus (1,632 in `n2chk2-career`). Both are handled (14.7); the 16 ranges left before their end in `n2chk5-career` jump more than one segment or overlap a later range, and are not compared.
- The coverage list per scene is the measured table of 14.4.
- Cost with the check off (`n2natp` against `n1natp2`, the perf gate of increment 1, three runs each): race 227.2 against 219.6 fps by the medians of the runs, p99 5.57 against 6.08 ms, PASS; over the race windows of the three runs, 1.813 against 1.833 us of render thread time per draw. The new hooks and the packet loop's flag cost nothing measurable.
- Cost with the check on (`n2chkp` against `n2natp`): race 94.1 against 227.2 fps, p99 15.17 against 5.57 ms (FAIL, as expected of a diagnostic mode); 4.825 against 1.813 us per draw, about 3 us more per draw, most of it probably the table lookup under a lock at every packet. A filter in front of the table would lower it before increment 3 runs the check on the converted writers.
- Not covered: 60 and 120 Hz, 2x, photo mode, split screen and the intro videos with the check; the overflow body's own PM4 and the allocators' PM4, which are not copied.

### 14.9 Increment 3 design: the dirty writers as records

Decisions of the main session, 22 October 2026: the in-stream marker of 14.3 instead of a ring position; a lock-free filter in front of the keyed tables, allocated only when they are used; one fixed error line per kind of mismatch with the details in a warning beside it, so the error keys of the gates stay stable.

**What the dirty writers do** (read from the generated code; they never touch the dirty bits, which their callers clear):

| Writer | Arguments | PM4 |
|---|---|---|
| `WriteDirtyRegisters` `sub_823114F8` | device, dirty mask (r4, 64 bits, the most significant bit first), first register (r5), source (r6) | one type-0 packet per run of set bits: register r5+i from the dword at r6+4i |
| `WriteDirtyFloatConstants` `sub_82310E78` | the same | each set bit is 16 registers: register r5+16i+j from r6+64i+4j; type-2 padding aligns the data to 16 bytes |
| `WriteDirtyFetchConstants` `sub_8230FA40` | device, dirty mask | each set bit is one fetch constant: register 0x4800+6i+j from device+1152+24i+4j; then a type-0 write of 0, 0x25000 and 0 to registers 0x5000 to 0x5002 (0, 0, 0 after a segment overflow), and type-2 padding to 8 bytes |

All three fall back to `D3DDevice_WriteRegistersChunked` (`sub_82379530`) when the run does not fit in the segment; it writes the same registers in chunks across segments.

**The record.** `host_render_native` (runtime) with the host render thread turns the conversion on, and the GPU plugin publishes it to the title (`rex/system/native_records.h`). A hooked dirty writer then, unless the device records a command buffer (device+13904) or the segment has no room for the marker:

1. takes the next sequence and its slot in a pool of 65,536 records, and builds the record there from its arguments: runs of (first register, count) and their values, copied from the source in guest byte order, in the order of the PM4;
2. publishes the slot with the sequence, before the marker exists;
3. writes the marker at device+48 (`NOP` with two data dwords: the tag `0x4E41544B` and the low 32 bits of the record's sequence), advances device+48 by three dwords and does not run the original body.

The host render thread applies the record when its `NOP` handler executes a marker: the sequence in the marker names the slot, and when the slot still holds that sequence the runs go through `WriteRegistersFromMem`, as the type-0 packets would have (the same side effects of the constant, fetch constant and hooked registers). A record stays in its slot until the pool wraps around (about 30 frames at the race's rate), so a segment executed more than once, such as the tile bodies of the menus' tiling bracket, applies it each time. A marker whose slot holds another sequence (never published, or the pool wrapped) is the error "Native record: marker without a record". The pool replaced a table keyed by the marker's address on 22 October 2026: kept for the tile bodies, its records filled the reused segments' addresses, so that most packets went to a locked map of up to 457,000 records, 1.13 ms a race frame (14.10).

**The identity check of a record** (with `FM4_NATIVE_CHECK=1` and `host_render_check`): the hook also runs the original body into a scratch segment of the hooking thread (64 KB of guest memory from the system heap, so a run always fits and the writer never calls the chunked writer or the overflow), with device+48 and device+52 pointing into it and restored afterwards, and stores the scratch PM4 as the check's copy under the marker's address. At the marker the host render thread applies the record, decodes the copy and compares the decoded registers with the register file, so the record must set every register the PM4 would have set, to the same value. The comparison of the PM4 bytes, the type-3 packets and the draws is left out for a marker (the scratch PM4 is not in the stream).

**The filter.** One bit per dword of the 512 MB physical space (16 MB), set when an entry is stored, cleared when it is taken, tested without a lock before every packet; the table's lock is taken only on a hit. It serves the check's table (the records do not use a table).

**Gates.** The identity check without mismatches in the coverage scenes (`career`), and a perturbed run that reports every perturbed range; the gates of increment 1 (smoke and perf against RTV of the same commit, `parity.py` on the one-tile `freeze`); the check's cost before and after the filter; the PM4 the command processor no longer translates, from the type-0 packet counts.

### 14.10 Increment 3 results, 22 October 2026

On `I:\FM4-build-usa` (USA release), commit `f4241ae` (`fm4_rex.exe` SHA-256 `e51dbd3a17ff64e1fd7ee335118312c2fda03cc2e597f95d1a2e62ce04d106dc`, `rexgpu-xenos.dll` `9202f16b05b36361e8e4888c99607bd4e00fa5deab05b69990bba72e0b929131`, `rexruntime.dll` `cb92f9687785aa1e93e292b2f69250e847b131090a74e53f8736e088235edc4c`), one binary for RTV (`host_render_thread` off) and the native path (`host_render_thread` and `host_render_native`), the launcher defaults at 240 Hz and 1x, the title's AI driving; the `freeze` pair at 60 Hz with its pins and one tile.

| Gate | RTV | Native, records | Verdict |
|---|---|---|---|
| Smoke, `career` race (`n3brtv`, `n3bnat`) | 228.1 fps, p99 5.81 ms | 224.2 fps, p99 6.03 ms | PASS |
| Smoke, `career` replay | 116.3 fps, p99 10.19 ms | 114.9 fps, p99 10.66 ms | PASS |
| Smoke, `photo` race | 207.8 fps, p99 6.37 ms | 216.0 fps, p99 5.86 ms | WARN on capture statistics only |
| Smoke, `photo_view` | 56.99 fps | 55.9 fps | WARN on capture statistics only |
| Perf, `photo` race, three runs (`n3brtvp`, `n3bnatp`), median of the runs | 233.7 fps, p99 5.14 ms (runs 221.9 to 238.2) | 214.2 fps, p99 5.93 ms (runs 214.1 to 231.4) | WARN on capture statistics only; fps -8.4% and p99 +15.4%, inside the limits |
| `parity.py`, `freeze` with one tile (`n3bfz-rtv`, `n3bfz-nat`) | | draws -0.0%, predicated run -0.0%, image 17.14% | PASS |

- The identity check with the records (`n3bprt-career`, perturbed every 1,000th range): 33,262,248 ranges compared in the menus, the one-tile race and the replay, 32,860,019 records applied with 1,479,896,286 register values, no marker without a record, and 31,617 mismatches, exactly the perturbed ranges. Every cell reached its goal with the error keys of RTV.
- What the records take off the command processor in the race (`n3bpm4-off` and `n3bpm4-on`, `REX_PM4_STATS`): type-0 packets 26,850 against 25,836 a frame (-3.8%), type-2 padding 19,320 against 17,366, type-3 packets 13,759 against 16,590 (the markers, 2,397 a frame). Most of the race's register writes come from the replayed command buffers, which keep their PM4 in N1 (13.2); the dirty writers called directly are a small part.
- Over the race windows of the three perf runs, the host render thread was busy 1.894 us per draw at 1,936 draws a frame, the command processor thread 1.724 us at 1,773 (+9.9% per draw). Applying the records took 0.42 ms a frame in a run with a timing zone (`n3natp3`), about what parsing the packets they replace cost; the records do not save host time yet.
- The first two designs of the record lookup are measured as well: a table that consumed records at their marker lost the second execution of the menus' tile bodies (about 23,000 markers a second without a record, the game hung, `n3chk-career`); a table that kept them cost 1.13 ms a race frame in its lookups (`n3zone-photo`) and the race ran at 176.9 against 225.0 fps on RTV (`n3natp`, FAIL). The pool of 14.9 replaced both.
- The identity check with the filter (`n3bchkp` against `n2chkp`, check on, records off): 4.511 against 4.825 us per draw; the filter removed the per-packet lock but most of the check's cost is in the copies and the decode.
- Not covered: 60 and 120 Hz, 2x, photo mode, split screen and the intro videos with the records.

### 14.11 The end of N1, 22 October 2026

Decisions of the main session after increment 3:

- N1 closes after increment 3 and two fixes of the records (below). Its CP time and p99 targets become N2's gate, and N2's decoded replays come next (section 15).
- The direct writers of increments 4 and 5 (draws, the draw flush, resolves, the swap, the tiling bracket, the ring waits) are taken up where a later stage needs them: the resolves and the tiling bracket in N3, the rest after N2 if measurements show CP time there.
- If the records were still slower per draw than RTV after the fixes, `-Native` would keep `host_render_native` off until N2.

The fixes, measured on `I:\FM4-build-usa` (commit `cfbe416`, 240 Hz, 1x):

- **Empty records**: none. The host render thread counted no record without a register write in the perturbed check run and in the perf runs; 14.10's 1,014 fewer type-0 packets a frame against 2,397 markers compared two races with different loads, not the packets one record replaces (each record carried about 46 registers, 14.10).
- **Joined records**: a converted call on the same device whose write pointer is still at the end of the thread's last marker joins that marker's record (a new slot with both calls' runs, and the marker's sequence dword rewritten). Markers per race frame fell from 2,072 to 760. It first assumed that the guest does not kick between two such calls; since the first N2 commit (the main session's follow-up) a call joins only while the count of kicks of the ring write pointer is unchanged since the marker, because a kick moves the primary ring and not the segment's write pointer.
- With both: the perturbed identity check (`n3cchk-career`) compared 15,828,454 ranges with 14,094 mismatches, the 14,093 perturbed ones and one byte difference: a range copied before a join rewrote the sequence dword of a marker it held (no register differed, so the host render thread read the new sequence). The byte comparison now skips the sequence dwords of markers.
- Perf (`n3crtvp` against `n3cnatp`, three runs each): the race at 209.8 against 232.9 fps by the medians of the runs (216.5 against 231.2 over all race frames), p99 6.22 against 5.24 ms, WARN on capture statistics only; 1.753 against 1.775 us of render thread time per draw (+1.3%) at 2,180 and 1,775 draws a frame. The records now cost about what the PM4 they replace did, so `-Native` keeps `host_render_native` on.

## 15. Stage N2 design, 22 October 2026

Stage N2 (section 9) decodes each replayed command buffer once and replays the result instead of its PM4. The main session's decision of 22 October 2026 (14.11) brings it forward: in the race most of the register writes and about 82% of the draws come from the replays (13.2, 14.10), so the CP time and p99 targets of N1 are N2's gate.

### 15.1 What a replay is on the host render thread

`RunCommandBuffer` writes one `INDIRECT_BUFFER` per descriptor of the replayed buffer into the command buffer segment it is writing (13.2: 739 replays a race frame, each with one descriptor). The segments themselves are executed through `INDIRECT_BUFFER` packets of the 32 KB primary ring (14.8), so a replay is an indirect buffer at depth two: `ExecuteIndirectBuffer` of the segment executes the replay's `INDIRECT_BUFFER`, which executes the recorded PM4 through `ExecutePacket`. Other indirect buffers at depth two (the tile stream's) are rare in the one-tile race.

### 15.2 Measure first

Before the op lists, a run measures what they can save: with `REX_CP_SPLIT`, the host render thread's time inside indirect buffers at depth two and more, split into the kinds of 13.8 (register packets, shader loads, draw packets with their draw zone, other packets, and the rest: dispatch and parsing), against the time outside them, per race frame, together with the replays per frame, their dwords and the distinct buffers. The parse share of the replays (dispatch, headers, the type-0 loops) is what the op lists remove; the draw work stays with the backend, as in N1. The gain that the gate can expect follows from that number.

### 15.3 Op lists

- **Key**: the guest physical address and length of the replayed indirect buffer.
- **Decode**: at the first replay of a key, the PM4 is decoded into a list of operations, in order: register runs (type 0, type 1, `SET_CONSTANT`, `SET_CONSTANT2`, `SET_SHADER_CONSTANTS`, with their values copied), constant loads (`LOAD_ALU_CONSTANT`: the address, the kind and the size; the values are read from guest memory at every replay, because the CPU rewrites recorded blocks between replays, 13.3), shader loads (`IM_LOAD` and `IM_LOAD_IMMEDIATE`), draws (`DRAW_INDX`, `DRAW_INDX_2`: the dwords after the header), the bin masks and selects, and any other packet as raw PM4 executed through `ExecutePacket` (fences, waits, events, nested indirect buffers). Each type-3 operation keeps its predicate bit, evaluated at replay against the bin state of that moment.
- **Validity**: the buffer's pages get a write watch of the shared memory (`WatchMemoryRange`); a write by the CPU drops the op list, and the next replay decodes again. A replay whose key has no valid op list executes the PM4 (the fallback). Whether a watch fires for pages that the GPU copy never requested is checked first; if it does not, the decode requests the range.
- **Shaders**: an `IM_LOAD` keeps its microcode address and size. The XDK's vertex fetch rewrite changes recorded microcode in place between replays (16.6% of the replay phase's recorded `IM_LOAD`s, 13.3), so the op list caches the loaded shader only while a watch on the microcode's pages holds, and loads it again (hash and lookup, as `ExecutePacketType3_IM_LOAD`) after a write. How often a watch drops an op list or a shader is counted.
- **Inherited state**: a replay inherits the registers set before it (the part transform, 2.4). The op list precomputes nothing that depends on registers it does not set; the pipeline and binding keys are resolved at each draw as today, so the backend's draw path is unchanged in N2. Precomputed keys are a later step, measured separately.
- **Execution**: the host render thread replaces the replay's `ExecuteIndirectBuffer` with the op list: register runs through `WriteRegistersFromMem`, constant loads through the same functions as the packet, shader loads through `LoadShader` and the active shader pointers, draws through the draw packet's handler fed from the copied dwords, raw packets through `ExecutePacket`. The guest-visible effects stay in the packets that have them, which stay raw.

### 15.4 The identity check per replay

With `host_render_check`, a replay executed from an op list is checked against its PM4: the decoder of 14.7 decodes the buffer's PM4 at that moment and the comparison of 14.7 runs on the register file, the type-3 packets with their predicate decisions and the draws after the op list ran. `host_render_check_perturb` changes every Nth op list's comparison, so a run must report every perturbed replay. The PM4's bytes against the op list's copy are compared as well, which checks the watches.

### 15.5 Gates

- The identity check per replay without mismatches in the menus, the one-tile race and the replay, and a perturbed run that reports every perturbed replay.
- Render thread time per race frame lower than N1 (increment 3) by a measured amount.
- The success criteria of section 10 measured: render thread busy at most 2.5 ms per race frame at 240 Hz and p99 at most 5.0 ms.
- Smoke, perf and `parity.py` on the one-tile `freeze` against RTV of the same commit.

### 15.6 Two steps (the main session's decision of 22 October 2026)

The measurement of 15.2 (15.7) puts 72% of the host render thread's time in the replays, but only about 0.5 to 0.6 ms of a 3.41 ms race frame in what the op lists can remove (the parse, the shader loads and part of the register packets); the rest is the draw path. N2 therefore has two steps, each with its own gate:

1. **Op lists** as in 15.3. Gate: the identity check per replay without mismatches, its perturbation proof, the drop of the render thread's busy time measured against 3.41 ms a race frame, and smoke, perf and parity against RTV of the same commit.
2. **Reuse per replayed draw**: the texture and binding keys and the descriptors built from them are reused while the draw's fetch constants and the shaders' bindings are unchanged since the op list's previous replay. Only the computation of keys and the building of descriptors are reused: the texture cache's validity (watches, uploads, invalidations) and the ownership of render targets still run at every draw, so a reused key never skips a texture reload the PM4 path would do. The identity check gets a binding mode that computes the keys both ways at every draw of a check run and compares them, with a perturbation proof. Step 2 starts from option 3 of [CP draw offload](cp-draw-offload.md#option-3-less-work-per-draw-on-the-one-thread), the comparator the plan names, and profiles the rest of `IssueDraw` (0.68 ms a race frame) before it chooses what to reuse. Gate: the section 10 targets (render thread busy at most 2.5 ms per race frame and p99 at most 5.0 ms at 240 Hz) and the checks of step 1.

### 15.7 The replays measured, 22 October 2026

The `photo` race, three runs each with `REX_CP_SPLIT` (`n2m-nat-split`: the native path with the records; `n2m-rtv-split`: RTV), on `I:\FM4-build-usa` at commit `274eee9`, 240 Hz and 1x, per race frame over 22,829 and 22,878 frames:

| Per race frame | Native | RTV |
|---|---|---|
| Render thread busy | 3.41 ms | 3.43 ms |
| Inside replays (indirect buffers at depth two) | 2.47 ms (72%), 560 replays, 263,139 dwords | 2.51 ms, 560 replays, 265,131 dwords |
| of which draw packets | 1.66 ms | 1.69 ms |
| of which register packets | 0.375 ms | 0.375 ms |
| of which shader loads | 0.104 ms | 0.105 ms |
| of which the rest (dispatch and parse) | 0.33 ms | 0.335 ms |
| Draw zone in all (native) | 2.24 ms: textures 0.44, bindings 0.45, render targets 0.24, primitives 0.21, pipeline 0.13, vertex buffers 0.09, the rest of `IssueDraw` about 0.68 | |
| Packets (native) | 26,081 register packets, 2,279 shader loads, 1,781 draws | |

The records of the converted dirty writers take about 0.19 ms a frame, counted as other packets (their markers).

### 15.8 Step 1, first results, 22 October 2026

`host_render_oplists` (off by default; with the host render thread): a replay at depth two runs from an op list of register runs, written straight from guest memory through `WriteRegistersFromMem`, and blocks of every other packet executed through `ExecutePacket`. Validity comes from physical memory write watches (`EnablePhysicalMemoryAccessCallbacks`), not the shared memory's: those need a GPU request of the range.

- The first build's watch callback answered that the range to unprotect "does not matter", so a write elsewhere on a page unprotected the pages of other op lists without telling them: their buffers changed unnoticed, and the stale op lists ran garbage as PM4 (362 packet overflows and failed draws in `n2a-career`). `host_render_oplists_verify` (a hash of each replayed buffer at every replay, diagnostics only) found 1,239 stale op lists in `n2b-career`, all on pages that had lost their watch. The callback now returns the triggered range: `n2c-career` with the verification had none, 14,875,959 replays from op lists, 8,153 decodes and 8,004 drops by a write, and no new error key.
- Performance (`n2c-nat-split`, the `photo` race, three runs with `REX_CP_SPLIT`, against `n2m-nat-split` of 15.7): 4.113 ms of render thread time per race frame at 1,949 draws against 3.41 ms at 1,781, 2.11 against 1.92 us per draw. The replays' register packets took 0.337 ms and their rest 0.406 ms a frame, against 0.375 and 0.33: the op lists remove the dispatch of the type-0 packets only, the type-3 packets keep theirs, and the time per draw rose, probably through more access violations now that a fault unprotects only its own pages. Step 1 in this form saves nothing; it stays off.

### 15.9 Step 2 without op lists (the main session's decision of 22 October 2026)

- Step 1 regressed per draw (2.11 against 1.92 us, 15.8). Its likely cause, the access violations of the page watches, is not measured; it is an open item, to be measured only if the op lists are revived. Step 1's gate of a lower busy time is dropped, and the op lists stay off by default.
- Step 2 does not build on the op lists or on page watches. The reuse per draw is keyed on the draw's inputs as the backend sees them at `IssueDraw`: the fetch constants that the bound shaders use (from the register file), the shader pair, and the sampler and binding state. A reuse is valid while those inputs, a few dwords per texture or binding, compare equal; the texture memory's validity stays with the texture cache's own watches. It works for direct draws and replays alike, without op lists, and matches the cheap reuse per draw of [CP draw offload, option 3](cp-draw-offload.md#option-3-less-work-per-draw-on-the-one-thread).
- Order: profile the rest of `IssueDraw` (0.68 ms a race frame) and the texture (0.44 ms) and binding (0.45 ms) zones by their calls; build the reuse with the binding check mode (the keys computed both ways at every draw of a check run, with a perturbation proof); gate with the section 10 targets and smoke, perf and parity against RTV.

### 15.10 The draw path profiled, 22 October 2026

The native `photo` race, three runs each with `REX_CP_SPLIT`, on `I:\FM4-build-usa` at 240 Hz and 1x, ms per race frame. `n2d-nat-split` has the draw zone's first level of parts (fewer timers per draw); `n2f-nat-split` adds the parts of the gaps, the bindings and the textures, and its timers inflate it (render thread busy 3.77 against 3.23 ms), so its numbers are read as proportions. Since then the detail zones run only with `REX_CP_SPLIT`.

| Part of the draw zone | `n2d` | `n2f` |
|---|---|---|
| The whole draw zone | 2.18 | 2.64 |
| Textures (`RequestTextures`) | 0.41 | 0.545: the keys of bindings out of sync 0.188, loads 0.048, binding update 0.056, the rest (the 3D-as-2D wrappers and timers) |
| Bindings (`UpdateSystemConstantValues`, `UpdateBindings`) | 0.39 | 0.559: constant buffer data 0.199, descriptors 0.156, root parameters 0.040, root signature and common buffers 0.020, the rest the system constants |
| Render targets | 0.23 | 0.234 |
| Primitives | 0.20 | 0.199 |
| Pipeline | 0.12 | 0.121 |
| Vertex buffers | 0.08 | 0.086 |
| Before the primitives (shader analysis, the submission) | 0.10 | 0.099 |
| Between the primitives and the render targets (draw traces, shader modifications, the overwrite of uniform targets) | | 0.148 |
| Between the render targets and the pipeline | | 0.063 |
| Between the pipeline and the textures | | 0.021 |
| Pipeline bind, viewport, fixed function state | 0.07 | 0.076 |
| Topology, memexport and the draw call | 0.04 | 0.044 |
| Not in a part (mostly the timers) | 0.55 | 0.447 |

- The texture cache already keeps a binding while its fetch constant is not written (`texture_bindings_in_sync_`). Its key work (0.19 ms) comes from fetch constants that are written at every draw, also with the values they had, which marks the bindings out of sync.
- What a reuse per draw can save: the keys of rewritten fetch constants with unchanged values, and the descriptors of a draw whose textures and samplers equal the previous draw's of the same shader pair; about 0.3 to 0.4 ms a frame by this profile. The constant buffer data changes per draw (the transforms), and the render targets and the gaps around them are the render target cache's work (N3).

### 15.11 Step 2: what it builds and how it closes (the main session's decision of 22 October 2026)

- Step 2 builds two reuses and then N2 closes: (i) a fetch constant rewritten with the dwords it had keeps the texture bindings in sync, the fetch constant buffer up to date and the vertex buffer residency (`d3d12_reuse_draw_bindings`), compared where the write happens instead of in a second key cache; (ii) the descriptor writes are skipped only when the bound textures, their views and the samplers equal the previous draw's of the same shader pair and root layout, with a texture reload or invalidation forcing fresh descriptors. The bindless path already keeps descriptor indices while the shader's layout and the textures' SRV keys stay the same (`AreActiveTextureSRVKeysUpToDate`), so (ii) adds only what that does not cover.
- The gate: the binding check mode (`d3d12_reuse_draw_bindings_check`: every binding kept in sync against a key computed fresh from its fetch constant at every draw of a check run) with a perturbation proof (`d3d12_reuse_draw_bindings_check_perturb`), no mismatch in the menus, the one-tile race and the replay; the drop of the render thread's time per race frame against 15.7; smoke, perf and parity against RTV. No fixed 2.5 ms target in N2: the CPU targets of section 10 go to N4.
- Timebox: if (i) and (ii) together save less than 0.15 ms a race frame, they stay off by default, the numbers are recorded and N2 closes all the same.

### 15.12 Step 2 results and the end of N2, 22 October 2026

On `I:\FM4-build-usa`, the native path at 240 Hz and 1x (build of 22 October 2026, 07:53):

- **(i) Unchanged fetch constants** (`d3d12_reuse_draw_bindings`): the perturbed binding check (`n2g-chk-career`, menus, the one-tile race and the replay) compared 60,870,738 bindings kept in sync with fresh keys and reported 60,870 mismatches, exactly the perturbed ones; 26,031,696 fetch constant writes changed no dword. In the perf gate (`n2greusep` against `n2gnatp`, the `photo` race, three runs each) 1,152 such writes a race frame were kept, and the render thread's time per draw stayed the same: 1.830 against 1.828 us (3.328 against 3.095 ms a frame at 1,819 and 1,693 draws). The key work of 15.10 is not in the rewrites of unchanged fetch constants.
- **(ii) Descriptors**: not built. The bindless path already keeps descriptor indices while the shaders' layouts and the textures' SRV keys stay the same, and the whole descriptor phase took 0.156 ms a frame with the timers of 15.10, so (i) and (ii) together cannot reach the 0.15 ms of the timebox. The main session accepted leaving it out on 22 October 2026.
- By the timebox, `d3d12_reuse_draw_bindings` stays off by default, and N2 closes. Its result: the replays are measured (15.7), the draw path is profiled (15.10), and neither the op lists (15.8) nor the reuse of bindings saves render thread time on this backend. The render thread's time per race frame with the launcher defaults and `-Native` is 3.1 to 3.4 ms (15.7, 15.12) against the 2.5 ms of section 10, which N4 takes up; the profile points at the descriptor and constant buffer work and the render target cache leaving the render thread or changing in N3.
- The closing gate on the same build (`fm4_rex.exe` SHA-256 beginning `78bedf74a7dcd99a`, `rexgpu-xenos.dll` `c517e68a6f680fa2`, `rexruntime.dll` `69ff51d7dace118a`; the op lists and the reuse off, `-Native` with the records): smoke `n2zrtv` against `n2znat`, the `career` race 231.9 against 220.4 fps and the replay 115.3 against 118.2 fps (PASS), the `photo` race 221.5 against 218.6 fps and `photo_view` 57.0 against 56.9 fps (WARN on capture statistics only); `parity.py` on the one-tile `freeze` (`n2zfz-rtv`, `n2zfz-nat`) PASS, draws +0.0%, image 22.78%; the error keys of RTV everywhere.

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

The one-pass scene colour spans 90 tile rows from base 0. Its rows 64 to 89 overlap the shadow map's tiles from 832, which the title writes before the scene each frame. So taking the scene colour back copies its bottom 208 lines (416 at 2x scale) from the shadow map. A clear claims its tiles without a transfer, so the copy shows that the title draws into these rows without clearing them after the shadow map. The 1:1 crops of the bottom 416 lines of the 2x `freeze` frames (`n4g-freeze-2x-*`, lines 1024 to 1440) show no shadow data and no artefact. The mean difference per 32-line band against RTV tiled 2x is 0.03 to 7.6, against 0.05 to 6.5 for `-Rov` against RTV. So in these frames the scene's draws cover the rows. That they cover every pixel in every frame is not proven: the lazy check (`native_rt_surfaces_lazy_check` with bit 4) would count the tiles a draw reads before overwriting them. If it shows them always overwritten, the copy can be skipped. That is a later candidate, worth about 0.04 ms a frame (0.35 against 0.31 ms of transfers on tiled RTV), not part of increment 4. The copy is into a colour target, one pass over its rectangles, so it adds little time. The scene depth stays private and has no transfer. The remaining cost, as in 16.13, is the stencil bit passes of the transfers into depth targets.

**The main session's decisions of 23 October 2026**, applied after this gate:

1. `-Native` runs races in one pass with 2x MSAA by default, with `native_rt_surfaces=10` (bit 8, the sized host render targets, beside bit 2). `-MsaaSamples 0` and `-SingleTile` keep one tile without MSAA; `-Msaa` keeps the tiles of `-Quality` on the PM4 RTV path. `e2e.py --native` does the same unless `FM4_TILING_SCENARIO`, `FM4_ONE_PASS_MSAA` or `--msaa` is given. Photo mode, replays and split screen keep their own scenarios. The GUI launcher (`plan.cpp`) has no `-Native` yet, so `-MsaaSamples` comes to it with `-Native`.
2. 4x stays in stage N3. Its gate is 4x MSAA in one pass that works. It starts with a probe: a scenario entry of its own (not entry 0, which the UI and the pre-race screens share), and the transfer inventory of 4x. Then the design chooses between a private colour and depth (alias copies only where something reads an alias) and the no-wrap space of option A. The `-Rov` depth sample comparison of 16.11 belongs to 4x at 1x.
3. After 4x works, the defaults are compared: 2x MSAA at 2x against 4x MSAA at 1x, with the GPU time of both at 1x, which sets the default of the ROG Xbox Ally X.

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
