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
| N4, threading and slower CPUs | The CPU targets of section 10 (render thread busy at most 2.5 ms per race frame and p99 at most 5.0 ms at 240 Hz), toward which N2 and N3 record their measured share; the profile of 15.10 points at the descriptor and constant buffer work leaving the render thread ([CP draw offload](cp-draw-offload.md), options 1 and 2). Event waits instead of the remaining spins (the host render thread of N1 already waits on its queue's event; the guest's own waits and the `WAIT_REG_MEM` polls without a sleep time still spin), the vblank interrupt and the readback copies moved onto the host render thread (8, 14.2), queue depth and latency tuned (the depth target starts at one frame; increment 1 measured about one frame of kicks, 14.6), a capped-CPU run on the main PC (fewer cores through the process affinity) as a quick proxy, and runs on the ROG Xbox Ally X (8.1). | The success criteria of section 10 met on the main PC and on the Ally X. | 3 to 5 |
| N5, visual upgrades on the native base | Float targets for `k_16_16`, linear blending as an A/B, upscalers on the native anchors ([FSR plan](fsr-plan.md)), a 1.5x render scale for the Ally X's 1080p panel, the native shader contract of 8.2, wider aspect ratios (needs a FOV and culling hook), and HDR10 once an HDR display exists. | One A/B and one decision per item. | per item |

The measurement part of N0 was done on 21 October 2026 (section 13). N0 to N4 total about 26 to 42 person-weeks. The earlier estimates for design A alone were 24 by the design and 40 to 56 with the reviewers' corrections; this plan reuses the backend in N1 and N2, which is why its range starts lower.

## 10. Validation and success criteria

- Races are not deterministic ([e2e](../e2e/README.md)), so image parity uses frozen rewind frames at a pinned look (`-Look "original,taa=0"`, the tiles and MSAA of `-Quality` where tiles are compared) with the occlusion knobs pinned, plus the variance of two identical baseline runs.
- A state-level dual run per batch or replay: register file, draw parameters and predicate decision of the native path against the PM4 path, with the guest address on a mismatch.
- `compare.py` fails on a crash, a goal not reached, fps more than 10% below the baseline (also per phase), p99 more than 20% above it, a new error key or a missing phase; image statistics and draw counts only warn or inform. The native path needs FAIL rules for the draw count, the predicated run and skip counts and the frozen-frame image difference, and every rejected draw or failed shader load goes through `REXGPU_ERROR` so that it shows as a new error key. Its own metrics replace `cp_busy` (render thread busy, guest render thread busy, queue depth).
- The harness for these rules exists since 21 October 2026 ([frozen-frame parity](../e2e/frozen-frames.md)): the `freeze` scenario (a Quick Race rewound to its oldest frame, 2x, the pinned look and occlusion), `frame_diff.py` (the share of pixels above a level, 1:1 crops), `parity.py` (FAIL on draws and predicated packets per frame, the image difference and new error keys) and the backend-neutral `summary.json` fields. Within a run the frozen frame is byte-identical. With the fixed seed of the race setup (`FM4_FIXED_SEED`) and the rewind's first press pinned to race frame 720, four baseline runs (two RTV, two `-Rov`) differ by up to 1.6% in draws and predicated packets per frame, and RTV and `-Rov` at about the same moment agree within 0.1%; `parity.py` fails above 3%. Their frozen frames still differ in 19 to 34% of the pixels (race frames counted in pad polls; up to 48% with the earlier presses on the wall clock): the race advances by wall time, so the same race frame is another moment in each run. `parity.py` fails above 60% of the pixels, which catches a broken frame only; that is the image limit of the native path (section 11), and wrong details are left to the state-level dual run and the 1:1 crops. The race clock is found (the main loop's step; `FM4_FIXED_FRAME_TIME=1` fixes it, and the 3-2-1 starts on a fixed update), but the race still diverges from a last-bit difference during the revving before the start, so a pair with it differs in 25.6% of the pixels; a tighter image limit would need that input found as well (see [what pinning the moment needs](../e2e/frozen-frames.md#what-pinning-the-moment-needs)).
- RTV against `-Rov` stays the reference comparison (AGENTS.md); no stage changes `-Rov`.
- A watchdog that logs any guest wait longer than a set time, and soak runs over menus, races, the career, replays and photo mode.
- Success criteria (proposed targets, to be confirmed against the N0 baseline of the USA build with the launcher defaults): at 240 Hz and 1x in the race, the render thread busy at most 2.5 ms per frame (the CP took 3.2 to 3.4 ms) and p99 at most 5.0 ms (5.59 to 5.78 ms), both against the 20 October 2026 smoke runs on the Japan/Asia BuildRoot `I:\FM4-build-pass`; the guest render thread reported per frame; on the ROG Xbox Ally X the device targets of 8.1; at 2x a GPU time per frame not above the RTV path; frozen frames within the image limit of section 11 and no new error keys.

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

### 16.2 Where N3 can save, by the measurement

1. **The full-screen resolves**: 1.95 of the 2.75 ms of resolves at 2x. Today each one dumps or copies the host render target into the scaled resolve buffer in the guest's tiled layout, the texture cache loads it back into a texture when a pass samples it, and the readback copies it to guest memory. A resolve as a host copy writes the texture the next passes sample straight from the render target and leaves the rest to demand.
2. **The readbacks**: 24.8 a frame, 13.9 MB at 2x, of every resolve, because any of them may be read by the CPU (2.7). Readback on demand needs a read trap on the resolved pages, which the runtime does not have (section 8). Their cost is measured: the perf gate at 2x with `readback_resolve=none` (`n3m2xnorbp`, three runs, the CPU's readers not served) ran the race at 185.7 against 172.1 fps, p99 7.87 against 8.71 ms, and 5.26 against 5.73 ms of GPU time per frame (medians of the runs), at 2,061 against 1,854 draws a frame: at least 0.47 ms of GPU time a frame, 8%.
3. **The ownership transfers**: 20 a frame, 0.25 ms of passes that only transfer at 2x, plus the transfers inside other passes. Surface-keyed targets with virtual placement remove those between surfaces that do not alias.
4. **The render target cache's CPU time**: about 0.4 ms a race frame (16.1), part of N4's CPU targets.

### 16.3 Increments

The order and the scope of increments 1 and 2 are the main session's decision of 22 October 2026: increment 1 is on the critical path of N3 (the resolves as host copies of section 8 are what one-pass tiles and native MSAA build on), and its ceiling, 1.95 ms at 2x, is larger than the readbacks' 0.47 ms.


| Increment | Scope | Gate |
|---|---|---|
| 1 | The full-screen depth and colour resolves of the scene (16.2, 1) become host conversion copies into the texture cache's host textures for their destinations (depth to the guest's `8_8_8_8` packing, float to `2_10_10_10` with the guest's exponent bias), behind `native_rt_resolve` (off). Production-correct: they keep their readback, served from the host texture through the existing asynchronous readback path, so the CPU's readers (the photo save) are served and it runs in every gate. It saves the copy into the scaled resolve buffer and the texture load from it; the readback stays until increment 2. A check mode compares the converted texture's bytes with the RTV path's (the scaled resolve plus the load) on a few frames. | GPU time at 2x per race frame against RTV of the same commit; smoke, perf, `parity.py` on the one-tile `freeze`, the photo save; 1:1 crops at 2x of GTAO, TAA, motion vectors and the windshield. |
| 2 | First a count-only measurement: which resolved ranges the CPU reads, how often and from which threads (the 0.47 ms of 16.2 is an upper bound with no read served). Then, only if the CPU reads few of them, the readback on demand with a read trap on the resolved pages, its handler designed for the runtime's own reads of guest memory as well (a thread that faults must never wait for a readback it has to issue itself); a risk item with its own check, since section 8 notes that a read trap is not built or verified. | The read counts recorded; then the photo save and the texture replacement's exclusion of resolved ranges work, and the readback bytes per frame are measured. |
| 3 | Surface-keyed host targets with virtual placement, the transfers only where a pass reads what another surface left (16.2, 3); the resolve and tiling writers of N1 as records. | Frozen frames within the image limit against RTV and against `-Rov` with `-Look "original,taa=0"`; transfers per frame measured. |
| 4 | One-pass tiles and native MSAA (2x and 4x in one pass, TAA on top), so `-Quality`'s tiles leave the PM4 RTV path. | 4x MSAA in one pass; the tiled scenes in one pass within the image limit. |
| 5 | The post passes anchored on surfaces; GTAO, TAA, motion vectors and the windshield checked at 2x on the native path. | The N3 gate of section 9. |
