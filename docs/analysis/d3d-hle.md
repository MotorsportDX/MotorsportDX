# D3D-level HLE: feasibility and plan

Status: analysis, 21 October 2026. Nothing here is implemented. The same day the project owner chose a native port as the target; its architecture and stages are in sections 8 and 9, and the measurements of stage N0 are in section 13. Addresses are the USA `default.xex` (the Japan/Asia executable has the same code, see [release comparison](releases.md)). The generated C++ that was read is `<BuildRoot>\project\generated\default`; the catalog, call graph and decompiled functions stay outside the repository under `I:\FM4-re` ([function catalog](function-catalog.md)).

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

The static scan behind [`fm4-d3d-trace.cpp`](../../work/fm4-rex/src/fm4-d3d-trace.cpp) lists 71 functions that write type-3 packets: 38 in the XDK range `0x826E1550` to `0x826F9B58`, 33 in the game range. Type-0 packets with computed headers are not found by that scan, so coverage has to be proven at run time: in a test mode, every ring allocation by a caller that the HLE has not replaced is logged. The coverage log of 21 October 2026 (13.5) found 19 writers outside the list, among them the tiling stream (`sub_823719A8`, `sub_8236F838`, `sub_822B7248`), the segment overflow `sub_822FEF08` and the type-0 writers; the allocations and overflows of the scenes measured come from 26 of the 71.

### 2.4 Recorded command buffers

`BeginCommandBuffer` (`sub_826E23E8`) points the device's current recording at the buffer and stores an optional tile table and the tile count. `EndCommandBuffer` (`sub_826E4050`) flushes all dirty state into the recording by value, then clears the device state. A recording is a linked list of PM4 chunks at +116 of the buffer (next pointer, size in dwords, data). `D3DCommandBuffer_CreateClone` (`sub_826E2350`) copies it. No code that patches a recording after `EndCommandBuffer` was found. The buffers replayed in a race are not the recorded objects: about half of the replays run byte copies of recordings made by game code, the rest PM4 that no `EndCommandBuffer` of the run produced, and the replayed PM4 changed between replays in 0.02% of them (13.2).

`RunCommandBuffer` (`sub_82310818`) first flushes the device's currently dirty groups, so a replay inherits the state set before it (for example the part transform that `CCarModel::RunPartCommandBuffer` sets). Then, when bit `0x20` of the buffer's flags at +108 is clear, it writes one `INDIRECT_BUFFER` per descriptor straight into the segment it is writing. Only with the bit set does it close the segment and call `sub_826E4A28` per chunk, which handles it in one of three ways: while a tiling bracket is open (device+13584) the chunk descriptors are deferred to `EndTiling`; while device+11152 is non-zero (a count of indirect buffer entries not yet drained) the descriptors are copied into the ring; otherwise one `INDIRECT_BUFFER` per descriptor is written into the primary ring. In the scenes measured on 21 October 2026 every replay took the inline path and `sub_826E4A28` was never called (13.2).

Every draw path, flush, replay and `EndCommandBuffer` also writes `LOAD_ALU_CONSTANT` packets (writer `sub_82343608`, through `sub_823436B8`). They carry only a guest address; the runtime reads the constants from guest memory when the CP executes the packet (`command_processor.cpp`, `ExecutePacketType3_LOAD_ALU_CONSTANT`). The constant tables come from the current shader objects; who fills them is not established. The CPU does rewrite blocks referenced from recordings between the recording and the replay (0.69% of those executions in a race, 12.7% in a replay; 13.3), and never rewrote a block referenced from the ring before its execution.

Recording sites: car parts per view slot, part and LOD (`RecordCommandBuffers` `sub_8294D100`), crowd models (`sub_82946C00`, with `CreateClone`), track objects through the render engine's group allocator `sub_82875458`, the half-rate bloom (`sub_8237CC08`) and five recorders without a name (`sub_827DF7C0`, `sub_827E5E48`, `sub_827F55B0`, `sub_8280BE70`, `sub_82860758`). `CRenderEngine::RunCommandBuffer` (`sub_82310FC8`) can queue a replay into a per-thread group, so recording and replay may run on different threads (inferred). Measured: recordings run on the render thread and on a second thread at load, and every replay ran on the render thread (13.7).

### 2.5 Predicated tiling

The tiles are not produced by game code and not by the GPU alone. Between `BeginTiling` (`sub_82371B80`) and `EndTiling` (`sub_8236FA20`) the XDK writes a tagged stream (tags `0x80` to `0x90`), and a CPU-side interpreter, `sub_826ED978` (reached through `sub_826EDCE8` and `sub_826EDFA8`), walks the tile body once per tile: tag `0x86` marks the body, `0x87` steps the tile index and jumps back, `0x88` is a predicated block that is skipped when its mask ANDed with a word of the interpreter's context (default `0x7FFFFFFF`, probably the bin select) is zero, and the interpreter writes `INDIRECT_BUFFER` and `SET_BIN_SELECT` packets for the GPU. The `EndTiling` loop calls `SetPredication` (mask `3 << 2i` for tile i), `sub_82392628` and `Resolve` once per tile, and no draw; after the loop it calls `SetPredication(0)`, `sub_82392628` again and, with more than one tile, `SetPredication(0x80000000)`.

`sub_826EDFA8` has no static caller. It is a worker loop: it waits on an event, drains the stream through `sub_826EDCE8` when signalled, and on a timeout calls `Swap` under a critical section when a device flag is set. It runs on a guest thread of its own, which calls `sub_826ED978` about twice per tiling bracket (13.7).

`sub_82374660` prepares the tiles: it copies the caller's tile rectangles to device+13104 (16 bytes each, count at device+13100), stores each tile's origin rounded down to 32 at device+13344+8i, and sets the viewport, the scissor and a clear. These origins are EDRAM origins used by the resolve's address arithmetic, not a screen translation of the draws. `BeginTiling` writes the rectangles into the stream relative to tile 0's origin, so absolute rectangles come from device+13104. The tiling scenarios at `0x82D883C8` (stride `0x4D0`, five entries) are: 0, three tiles of 1280 by 256, 256 and 208 rows; 1, two tiles of 512 and 208 rows; 2, one tile of 1280x720; 3, one tile of 1120x584; 4, one tile of 1280x368.

A one-pass HLE can run the body once at full size and resolve once. It must keep evaluating each predicated block against the bins, with a scissor over the absolute rectangles of the bins it selects: `CMirrorRenderer::Render` (`sub_82392AE0`) calls `SetPredication(3 << (2k-2))`, with k from the tile count function `sub_822E4650`, and no reset was found in the part of it that was read, so the next pass may inherit that mask. The bin encoding is not fully decoded: `EndTiling` builds its masks as `3 << 2i`, but its initial bin word is a 6-bit field. Its per-tile resolves also differ: tiles before the last one add the colour clear flag (0x100) when a colour target is bound and the depth clear flag (0x200) when a depth surface is bound, with a float from device+13568, and the last one passes the caller's flags; collapsing them into one resolve needs those flags decoded.

### 2.6 Shaders and their hash keys

The runtime keys shaders by XXH3 over the raw microcode dwords at the `IM_LOAD` source address, or over the ring copy for `IM_LOAD_IMMEDIATE`; translation never changes those bytes. A vertex shader object holds its microcode at +32 and its pass records from +896; a pixel shader object holds it at +24. The draw flush (`sub_823436B8` through `sub_82353BA8`) calls `sub_826EC0C0` whenever the bound vertex layout changes, and that function writes the rewritten 12-byte fetch instructions back into the shader's own microcode (`fm4_rex_recomp.20.cpp`, around line 12560). The vertex shader keys the runtime sees today are therefore keys of rewritten bytes.

Look mods are pixel shaders only, keyed by the hash and an optional modification suffix; no rewrite of pixel shader microcode was found. `passes.toml` names vertex shaders by hash (the scene camera and the car shader set). An HLE keeps every key of the ring if it leaves the XDK flush running and hashes at the point where `IM_LOAD` is written; it gets different vertex shader keys if it hashes at `CreateVertexShader` or `SetVertexShader`. For `IM_LOAD`s inside recordings the key at the point of writing is not enough: the rewrite changes the microcode between recording and replay in 0.63% of those executions in a race and 16.7% in a replay, so they have to be hashed at replay (13.3). Two built-in shaders come from fixed data at `0x82067308` (96 bytes) and `0x82067368` (36 bytes) through `IM_LOAD_IMMEDIATE`.

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
| Transport and threads | Each guest producer writes fixed records into a bounded queue with a global sequence number. One host render thread consumes them in order, owns the submission state and makes every guest-visible write (consumed ring index, fences, vblank counter, occlusion results, readbacks, swap). The existing submit thread keeps recording the D3D12 command lists. `LOAD_ALU_CONSTANT` blocks are copied into the record when the queue may run ahead of the guest. The consumer waits on events, not spin loops. Queue depth starts at one frame. | The backend's submission state is unlocked and assumes one producer; recordings are made on two guest threads, replays and direct draws on the render thread (13.7); the guest's fences are ring indices that need a published progress value (2.7); the spin loops compete for cores on a slower CPU. Unleashed Recompiled uses the same shape (section 5). |
| Recorded command buffers | First, the render thread replays recorded PM4 through the existing interpreter (`ExecutePacket`). Then each recording is decoded once into an operation list with precomputed pipeline and binding keys; a replay applies the inherited state, re-reads the `LOAD_ALU_CONSTANT` blocks, and falls back to PM4 per recording on any mismatch. | 82% of executed draws are replays in the tiled trace (section 3); per-draw translation is 30% of the CP and parsing about 24%. The decode is worth it only if recordings are reused: they are, a replayed buffer runs about twice a frame for thousands of frames (13.2). Most replayed buffers are copies or built with the assets, so the decode keys on the replayed buffer and its PM4, not on `EndCommandBuffer`. |
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
- vertex shader overrides, keyed by the hash taken where `IM_LOAD` is written (2.6), for example for a widescreen projection;
- behaviour per pass: the same game shader can be treated differently in the scene, the mirror or the shadow pass;
- new passes at D3D-level points (after the opaque geometry, before the transparent geometry, before the HUD), with Shader Model 6 and compute shaders (DXC is needed, as in the [FSR plan](fsr-plan.md));
- an offline translation of every game shader to HLSL and DXIL on the user's machine, as XenosRecomp does for Unleashed Recompiled, so that no shader compiles during play and any shader can be edited.

What stays tied to the game is its data, not the Xenos GPU: the meaning of the constants the game writes ([material atlas](material-atlas.md)), the vertex and texture formats in guest memory (the native path converts them before a shader sees them), and the identity of each game shader by its microcode hash. Shaders written for the project may go into the repository like the look mods; translations of the game's own shaders are derived from game data, so they are generated on the user's machine like the recompiled code and are not committed.

## 9. Stages

Every stage is behind a switch, off by default until its gate passes, with the PM4 path as the fallback. The effort is person-weeks for one developer with an AI agent; it is inferred and not calibrated against past work.

| Stage | Scope | Exit criteria | Effort |
|---|---|---|---|
| N0, foundations and measurements | One hook registry (merge the trace and diagnostics hooks); counters for the replay paths of `sub_826E4A28`, recordings per second and replays per recording, the `IM_LOAD` key check of 2.6, the coverage log of 2.3, `REX_LATE_WRITE_TRACE` on a race and a tiled race, the sync packets of 2.7, the guest render thread's time per frame (a zone around `sub_822DB318`) and a thread trace of the D3D entry points; the CP time split at 240 Hz and 2x; the frozen-frame A/B harness and the FAIL rules of section 10; a baseline on the ROG Xbox Ally X (8.1). | Every number recorded here with its run; the coverage list complete for a menu, a race and a replay; the variance of two identical baseline runs known. | 2 to 3 |
| N1, native producer on the existing backend | The producers of section 8 feed the host render thread, which drives the existing D3D12 backend through `WriteRegisterFast`, `WriteRegistersFromMem`, `LoadShader` and `IssueDraw`; recordings replay as PM4 through `ExecutePacket`; render targets stay on the RTV cache; the ring and the CP thread are no longer used on this path. | State-level dual run without mismatches; frozen frames identical to RTV; no PM4 written in the coverage scenes; smoke and perf gates against the RTV build of the same commit; the success criteria for CP time and p99 measured. | 6 to 9 |
| N2, decoded recordings | Operation lists with precomputed pipeline and binding keys; inherited state and `LOAD_ALU_CONSTANT` re-read at replay; fallback to PM4 per recording. | Dual run per replay without mismatches; render thread time per frame lower than N1 by a measured amount. | 3 to 5 |
| N3, native render targets | Surface-keyed host targets with virtual placement, copies for aliased contents, one-pass tiles, native MSAA as a launcher option (2x and 4x in one pass, TAA on top), resolves as host copies and demand readback, post-pass anchors on surfaces, look mods and GTAO moved onto the native path. Starts with a GPU-only prototype at 2x (one scene pass and one resolve) against the RTV path. | Frozen frames within the agreed threshold against RTV and against `-Rov` with `-Look "original,taa=0"`; GTAO, TAA, motion vectors and the windshield checked at 2x; GPU time at 2x not above RTV; 4x MSAA in one pass working. | 12 to 20 |
| N4, threading and slower CPUs | Event waits instead of spins, queue depth and latency tuned, a capped-CPU run on the main PC (fewer cores through the process affinity) as a quick proxy, and runs on the ROG Xbox Ally X (8.1). | The success criteria of section 10 met on the main PC and on the Ally X. | 3 to 5 |
| N5, visual upgrades on the native base | Float targets for `k_16_16`, linear blending as an A/B, upscalers on the native anchors ([FSR plan](fsr-plan.md)), a 1.5x render scale for the Ally X's 1080p panel, the native shader contract of 8.2, wider aspect ratios (needs a FOV and culling hook), and HDR10 once an HDR display exists. | One A/B and one decision per item. | per item |

The measurement part of N0 was done on 21 October 2026 (section 13). N0 to N4 total about 26 to 42 person-weeks. The earlier estimates for design A alone were 24 by the design and 40 to 56 with the reviewers' corrections; this plan reuses the backend in N1 and N2, which is why its range starts lower.

## 10. Validation and success criteria

- Races are not deterministic ([e2e](../e2e/README.md)), so image parity uses frozen rewind frames at a pinned look (`-Look "original,taa=0"`, the tiles and MSAA of `-Quality` where tiles are compared) with the occlusion knobs pinned, plus the variance of two identical baseline runs.
- A state-level dual run per batch or replay: register file, draw parameters and predicate decision of the native path against the PM4 path, with the guest address on a mismatch.
- `compare.py` fails on a crash, a goal not reached, fps more than 10% below the baseline (also per phase), p99 more than 20% above it, a new error key or a missing phase; image statistics and draw counts only warn or inform. The native path needs FAIL rules for the draw count, the predicated run and skip counts and the frozen-frame image difference, and every rejected draw or failed shader load goes through `REXGPU_ERROR` so that it shows as a new error key. Its own metrics replace `cp_busy` (render thread busy, guest render thread busy, queue depth).
- The harness for these rules exists since 21 October 2026 ([frozen-frame parity](../e2e/frozen-frames.md)): the `freeze` scenario (a Quick Race rewound to its oldest frame, 2x, the pinned look and occlusion), `frame_diff.py` (the share of pixels above a level, 1:1 crops), `parity.py` (FAIL on draws and predicated packets per frame, the image difference and new error keys) and the backend-neutral `summary.json` fields. Within a run the frozen frame is byte-identical, but Quick Race picks other opponents and grid slots each run, so the variance of two baseline runs and the threshold wait for a fixed seed of the race setup.
- RTV against `-Rov` stays the reference comparison (AGENTS.md); no stage changes `-Rov`.
- A watchdog that logs any guest wait longer than a set time, and soak runs over menus, races, the career, replays and photo mode.
- Success criteria (proposed targets, to be confirmed against the N0 baseline of the USA build with the launcher defaults): at 240 Hz and 1x in the race, the render thread busy at most 2.5 ms per frame (the CP took 3.2 to 3.4 ms) and p99 at most 5.0 ms (5.59 to 5.78 ms), both against the 20 October 2026 smoke runs on the Japan/Asia BuildRoot `I:\FM4-build-pass`; the guest render thread reported per frame; on the ROG Xbox Ally X the device targets of 8.1; at 2x a GPU time per frame not above the RTV path; frozen frames within the threshold and no new error keys.

## 11. Decisions

Made by the project owner on 21 October 2026:

- The target is a native port, judged by all five goals of section 8.
- The slower target PC is the ROG Xbox Ally X, the owner's second device (8.1).
- MSAA comes back as an option on the native path, rendered in one pass without tiles; one tile with TAA stays the default.
- Look mods and GTAO target RTV and the native path; `-Rov` stays the uncorrected reference and the native path is compared against it (AGENTS.md, render target paths).

Still open:

- The image threshold that counts as parity for the native path, and whether it may differ from RTV by design (one pass, MSAA, `LOAD_ALU_CONSTANT` read time, formats).
- Whether the PM4 RTV path stays a permanent reference once the native path passes its gates.
- Which modes N1 must cover (race, photo mode, replays, split screen, menus, video) and which may stay on PM4 at first.
- How far the runtime may diverge from the upstream ReXGlue command processor.

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

All runs are on `I:\FM4-build-usa` (USA release) with the counters of [traces](../e2e/traces.md#d3d-layer-counters-fm4_n0_trace) (`FM4_N0_TRACE`, `FM4_N0_KEYCHECK`, `REX_PM4_STATS`, `REX_CP_SPLIT`), the launcher defaults (look `pc`, quality `high`, one tile with TAA, the asynchronous swap), 240 Hz and 1x unless stated, the title's AI driving. The `career` scenario covers a menu (Career, World Tour, the event screens), a race (Tsukuba Short Circuit, Ford Ka, 40 s) and a replay (30 s); its phases are the steady part as `e2e.py` measures them, the menu is the windows from PLAY CAREER to START RACE.

| Run | Settings | Code |
|---|---|---|
| `n0-career-c` | `FM4_N0_TRACE`, `REX_PM4_STATS` | `4e32b17` |
| `n0-msaa-b` | the same with `-Msaa` (three predicated tiles, 4x MSAA) | `4e32b17` |
| `n0-key`, `n0-key-msaa` | `FM4_N0_KEYCHECK` and `REX_LATE_WRITE_TRACE`, one tile and `-Msaa` | `9b46523` |
| `n0-key2` | `FM4_N0_KEYCHECK` with the checks of copies and of later executions | `b3ef3bc` |
| `n0-p1x`, `n0-p1x-split`, `n0-p2x-split` | `photo`, three runs each: no counters; `REX_CP_SPLIT=1`; `REX_CP_SPLIT=1` at 2x | `b3ef3bc` |

The counters cost frame rate (the `career` race ran at 195.5 fps with `FM4_N0_TRACE` and 153.6 fps with `FM4_N0_KEYCHECK`), so the fps of the counter runs are not performance figures; those come from the `n0-p*` runs. Earlier runs of the same day (`n0-career`, `n0-career-b`, `n0-msaa`) are superseded: their counters were incomplete, and `n0-msaa` crashed in a summary writer that read freed guest memory (fixed before `n0-career-c`).

### 13.2 Replays and recordings

| Per frame (`n0-career-c`, `n0-msaa-b`) | Race, one tile | Race, tiles | Replay, one tile | Replay, tiles | Menu |
|---|---|---|---|---|---|
| fps in the counter run | 195.5 | 162.9 | 113.5 | 99.8 | 54.7 |
| Draws executed | 2,109 | 2,743 | 1,879 | 2,637 | 455 |
| `RunCommandBuffer` | 743 | 758 | 643 | 598 | 21 |
| of which the inline path | 743 | 758 | 643 | 598 | 21 |
| `sub_826E4A28` (deferred, copied, queued, direct) | 0 | 0 | 0 | 0 | 0 |
| `INDIRECT_BUFFER` executed (skipped by predication) | 868 | 1,311 (879) | 706 | 1,010 (564) | 97 (13) |
| Distinct buffers replayed per second | 374 | 384 | 202 | 183 | 5.5 |
| `BeginCommandBuffer` per second | 0 | 0 | 0 | 0 | 4.5 |

- Every replay took a path that section 2.4 did not describe: when bit `0x20` of the buffer's flags at +108 is clear, `RunCommandBuffer` writes one `INDIRECT_BUFFER` per descriptor straight into the segment it is writing (`loc_82310CF8`), and only with the bit set does it close the segment and go through `sub_826E4BE0` and `sub_826E4A28`. In these runs the bit was never set, and `sub_826E4A28` was not called at all, from `RunCommandBuffer` or from the swap. Each replayed buffer had one descriptor.
- Recordings happen at load, not per frame: 10,209 `BeginCommandBuffer`/`EndCommandBuffer` pairs in a run, 8,705 of them on a second guest thread (13.7), and about 4.5 per second in the menu, with as many `D3DCommandBuffer_CreateClone` calls.
- The buffer objects that `RunCommandBuffer` replays are not the recorded objects, and they do not point at the recorded PM4 chunks: in `n0-career-c` 99.9% of the replays were of buffer objects never seen at `EndCommandBuffer`. With `FM4_N0_KEYCHECK`, 54% of the race replays (`n0-key2`; 39% in `n0-key`), 68% of the replay phase and 40% of the menu execute PM4 that is byte-identical to a recording of the same run, so those are copies made by game code (not through `CreateClone`); the rest is PM4 that no `EndCommandBuffer` of the run produced (inferred: command buffers built with the assets). The replayed objects are resource type 9 (`0x00400009` or `0x00500009` at +0), most of them in physical memory.
- Reuse is high: a run replays 2,413 to 4,289 distinct buffer objects, most of them 256 to 8,191 times (`n0-key2`: 1,906 of 2,413). In the one-tile race 374 distinct buffers a second take 743 replays a frame, so a buffer is replayed about twice a frame, frame after frame. With the PM4 hashed at every replay, 0.02% of the race replays found the bytes of a buffer changed since its previous replay (26.8 of 114,647 per second in `n0-key2`); whether that is a patch or a new buffer at the same address is not distinguished.

### 13.3 Shader keys and constant blocks

`n0-key2` hashed every `IM_LOAD` source and every `LOAD_ALU_CONSTANT` block when `sub_823436B8` wrote the packet, gave byte copies of recordings the hashes of the recording's packets, and compared them with the bytes the command processor reads when it executes the packet. Packets that no hook saw being written (in the replayed buffers that are not copies of a recording) are compared with their first execution.

| `n0-key2`, executions | Race | Replay | Menu |
|---|---|---|---|
| `IM_LOAD` written into the ring: checked, changed | 888,211, 0 | 208,438, 0 | 460,336, 0 |
| `IM_LOAD` written into a recording: checked, changed | 90,322, 570 (0.63%) | 108,176, 18,040 (16.7%) | 2,285, 5 |
| `IM_LOAD` against the first execution: checked, changed | 11,345,684, 4 | 4,361,342, 2 | 234,370, 0 |
| `LOAD_ALU_CONSTANT` in the ring: checked, changed | 559,844, 0 | 106,194, 0 | 384,466, 0 |
| `LOAD_ALU_CONSTANT` in a recording: checked, changed | 76,379, 530 (0.69%) | 22,704, 2,884 (12.7%) | 1,960, 0 |

- A key taken where `IM_LOAD` is written matches the runtime's key for every packet written into the ring. For packets inside recordings it does not: between the recording and the replay the XDK's vertex fetch rewrite (2.6) changes the microcode in place, so the replay loads other bytes (56 distinct packet and key pairs in the run). The console GPU also reads the microcode when it executes the packet, so the runtime's key is the faithful one, and a native path has to hash recorded `IM_LOAD`s at replay, not at recording.
- The `LOAD_ALU_CONSTANT` blocks of recordings are rewritten by the CPU between recording and replay as well (0.69% in the race, 12.7% in the replay phase); blocks referenced from the ring were never rewritten before execution. A decoded recording therefore has to re-read its constant blocks at every replay, as section 8 already plans.
- `n0-key` and `n0-key-msaa` (without the copy and first-execution checks, so they could check only the recordings replayed as themselves) agree for the ring: 0 changes in 719,004 and 701,478 ring `IM_LOAD`s of the race, and 0 in the 5,168 and 5,866 recorded ones.

### 13.4 Late writes

`REX_LATE_WRITE_TRACE` (guest memory the command processor consumed and changed before the next signal the CPU can observe): in `n0-key` (one tile) no changed range in 15,907 frames and 31.2 million checked ranges; in `n0-key-msaa` (tiles) 25 changed ranges in 14,977 frames and 24.6 million checked, all of them one vertex range (`0x115A6A00`, 252,000 bytes, vertex shader `82130FF0`) in the race. No indirect buffer, constant block or shader changed after the command processor read it.

### 13.5 Coverage of the PM4 writers

The ring allocations by return address of `n0-career-c` and `n0-msaa-b` (boot, menus, a race and a replay each) come from 26 functions of the static list of 71 and from these functions it does not have:

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
| `sub_82353F18`, `sub_8239DDA0`, `sub_822BA060` (`SetShaderGPRAllocation`), `sub_822B0788`, `sub_826F4D00`, `BeginTiling`, `EndTiling`, `BeginCommandBuffer`, `BeginCommand` | overflow | inline writers that overflowed a segment |

The overflow handler only shows inline writers that happened to fill a segment, so this list is a lower bound for them; type-0 and inline type-3 writers that never overflowed in these scenes are not proven absent.

### 13.6 Synchronization packets and occlusion

| Per frame | Race, one tile | Race, tiles | Replay, one tile | Menu |
|---|---|---|---|---|
| `EVENT_WRITE_ZPD` | 2.0 | 6.0 | 2.0 | 0 |
| `REG_TO_MEM`, `COND_WRITE`, `MEM_WRITE` | 0 | 0 | 0 | 0 |
| `REG_RMW` | 1.0 | 1.0 | 1.0 | 1.0 |
| `WAIT_REG_MEM` | 375 | 470 | 382 | 43 |
| `EVENT_WRITE_SHD` | 107 | 31 | 50 | 19 |
| `INTERRUPT` | 2.0 | 4.0 (+2 skipped) | 2.0 | 5.1 |

The command processor waited 0 ms for occlusion results in every phase (`wait_ms.occlusion`), so the GPU wait per occlusion report is 0 with the launcher defaults. Most `WAIT_REG_MEM`s are the segment-overflow waits of 13.5 (one per segment, satisfied at once); the vblank wait of the swap is one of them.

### 13.7 Threads and the render thread's frame

| Guest thread | Calls in `n0-msaa-b` |
|---|---|
| Render thread (`sub_822DB318`) | every `Swap`, draw, `RunCommandBuffer`, `Resolve` and `Clear`, 63.4 million `SetTexture`, 1,504 recordings, `BeginTiling` and `EndTiling` |
| A second thread | 8,705 recordings, 48 of the 64 `RecordCommandBuffers` calls, 395,318 `SetTexture` and 13,578 `DrawIndexedVertices` (into recordings), at load |
| The tile stream worker | `sub_826EDFA8` once (it never returns), `sub_826EDCE8` and `sub_826ED978` about twice per `BeginTiling` (41,781 calls for 20,881 brackets) |

Every `RunCommandBuffer` ran on the render thread, 99.9% of them through `CRenderEngine::RunCommandBuffer`, so its queue to another thread (2.4) was not used in these scenes. The calls of `sub_822DB318` that presented a frame took 5.10 ms each in the one-tile race (195.9 per second, so 999 ms/s) and 6.14 ms in the tiled race (162.6 per second): in the race the guest render thread is busy for the whole frame, at the same time as the command processor (948 and 969 ms/s). In the replay phase it took 7.26 ms (one tile) and 2.96 ms (tiles) per frame and spun through the calls without a frame (3,690 and 17,570 per second).

### 13.8 Command processor time split

The `photo` race (12 cars, the launcher defaults), three runs each, ms per second of the command processor thread over each run's measured race windows, median of the three runs per row. `REX_CP_SPLIT` times the packets by kind (registers: type 0 and 1, `SET_CONSTANT2`, `LOAD_ALU_CONSTANT`, bin masks and selects; shaders: `IM_LOAD`; draws: `DRAW_INDX`, `DRAW_INDX_2`, which contain the `draw` zone, which contains the `copy` zone). The waits inside draw packets (the GPU wait at 2x) are taken out of the draw packets; "rest" is the busy time outside every timed kind and the swap: packet dispatch, indirect buffers, `WAIT_REG_MEM` and `EVENT_WRITE_ZPD` outside their waits.

| Race phase | 1x, no split (`n0-p1x`) | 1x (`n0-p1x-split`) | 2x (`n0-p2x-split`) |
|---|---|---|---|
| fps, p99 frame time | 217.5, 6.91 ms | 206.6, 7.17 ms | 155.5, 10.46 ms |
| GPU per frame | 2.89 ms | 2.90 ms | 6.23 ms |
| Draws per frame | 1,661 | 1,780 | 1,745 |
| Command processor busy | 815 | 847 | 667 |
| Busy per draw | 2.25 us | 2.32 us | 2.57 us |
| Wait for the vblank (`WAIT_REG_MEM`) | 167 | 131 | 14 |
| Wait for the GPU (frame latency) | 0 | 0 | 319 |
| Other waits | 19 | 16 | 5 |
| Register packets | | 111 (28,641 packets per frame) | 82 |
| Shader loads | | 31 | 23 |
| Draw packets | | 603 | 471 |
| of which the `draw` zone, copies included | 603 | 592 | 463 |
| of which copies (resolves) | 71 | 64 | 41 |
| Other timed packets | | 4.8 | 5.2 |
| Swap | 11 | 10 | 8.8 |
| Rest: dispatch, indirect buffers, untimed packets | | 88 | 76 |

- At 1x the busy time splits into draw work 71% (draws 62%, resolves 7.6%), register packets 13%, dispatch and the untimed packets 10%, shader loads 3.7%, the swap 1.2% and other packets 0.6%. Against the profile of 20 October 2026 behind section 6 (draws 60%, parsing, registers and other packets 24%, swap 13%, copies 3%), the asynchronous swap took the swap from 13% to about 1%, and the front end (dispatch, registers, shaders: about 230 ms/s, 27%) is what a native producer can remove or move off this thread; the draw work stays with the backend in N1.
- `REX_CP_SPLIT` costs about 3% of the command processor's time per draw (2.32 against 2.25 us), mostly in the register packets, of which it times about 29,000 per frame.
- At 2x the GPU sets the frame rate: the command processor waits 319 ms/s for frames in flight, the busy time per draw is 2.57 us, and the split is otherwise the same.
- The three runs of each cell vary with the AI race: 196.6 to 220.0 fps and 1,517 to 2,073 draws per frame without the split, the busy time per draw within 2.24 to 2.25 us.
