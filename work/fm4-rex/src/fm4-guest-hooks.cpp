// The hook registry: every hook of the title's D3D layer and of the
// diagnostics that observe it, one definition per guest address (a guest
// address can have only one). Each hook calls its features around the original
// body in a fixed order: the PM4 writer counts of fm4-d3d-trace.cpp, then the
// stage N0 counters of fm4-n0-trace.cpp, then the photo save diagnostics of
// fm4-jpeg-diagnostics.cpp. Every feature is off unless its variable is set
// (fm4-guest-hooks.h). Native-path hooks of docs/analysis/d3d-hle.md belong
// here too, next to the addresses they replace.
#include "fm4-guest-hooks.h"
#include "fm4-diagnostic-state.h"

using namespace fm4::hooks;

// A PM4 writer that only fm4-d3d-trace.cpp counts.
#define FM4_TRACED_HOOK(address)                                    REX_EXTERN(__imp__sub_##address);                                 REX_HOOK_RAW(sub_##address) {                                       D3DTraceCount(0x##address, ctx);                                  __imp__sub_##address(ctx, base);                                }

// A PM4 writer of the identity check (fm4-native-check.cpp).
#define FM4_CHECKED_HOOK(address)                                   REX_EXTERN(__imp__sub_##address);                                 REX_HOOK_RAW(sub_##address) {                                       NativeCheckPre(0x##address, base);                                __imp__sub_##address(ctx, base);                                  NativeCheckPost(base);                                          }

// An entry point that only the N0 thread trace observes.
#define FM4_THREAD_HOOK(address, entry)                             REX_EXTERN(__imp__sub_##address);                                 REX_HOOK_RAW(sub_##address) {                                       N0Thread(N0Entry::entry);                                         __imp__sub_##address(ctx, base);                                }

// PM4 writers of the static scan (fm4-d3d-trace.cpp), in address order.
FM4_TRACED_HOOK(822A7E08)
FM4_TRACED_HOOK(822A7FD0)
FM4_TRACED_HOOK(822ACC80)
FM4_TRACED_HOOK(822B2FB8)
FM4_TRACED_HOOK(822BF380)
FM4_TRACED_HOOK(822C22A8)
FM4_TRACED_HOOK(822DADA8)
FM4_TRACED_HOOK(822DB108)
FM4_TRACED_HOOK(822EBE20)
FM4_TRACED_HOOK(82310530)
// sub_82310818: below.
// sub_82311080: below.
FM4_TRACED_HOOK(82333538)
// sub_8233AC60: below.
FM4_TRACED_HOOK(82343608)
// sub_823436B8: below.
FM4_TRACED_HOOK(82344038)
FM4_TRACED_HOOK(82348DC8)
// sub_8234D278: below.
// sub_8234DAE8: below.
FM4_TRACED_HOOK(823619B0)
FM4_TRACED_HOOK(82361D40)
FM4_TRACED_HOOK(8236F6F8)
// sub_82370370: below.
FM4_TRACED_HOOK(823712F8)
FM4_TRACED_HOOK(82374660)
// sub_8237FA28: below.
FM4_TRACED_HOOK(82381318)
FM4_TRACED_HOOK(82382A70)
FM4_TRACED_HOOK(82386C48)
FM4_TRACED_HOOK(82392800)
FM4_TRACED_HOOK(82395A48)
FM4_TRACED_HOOK(8239DF28)
FM4_TRACED_HOOK(826E1550)
FM4_TRACED_HOOK(826E35F8)
FM4_TRACED_HOOK(826E3CA8)
FM4_TRACED_HOOK(826E3E60)
FM4_TRACED_HOOK(826E4C90)
FM4_TRACED_HOOK(826E4E28)
FM4_TRACED_HOOK(826E5598)
FM4_TRACED_HOOK(826E5C20)
FM4_TRACED_HOOK(826E6490)
FM4_TRACED_HOOK(826E6D70)
FM4_TRACED_HOOK(826E7220)
FM4_TRACED_HOOK(826E75C0)
FM4_TRACED_HOOK(826E7718)
FM4_TRACED_HOOK(826E7AA0)
FM4_TRACED_HOOK(826E7B98)
FM4_TRACED_HOOK(826E80A8)
FM4_TRACED_HOOK(826E8928)
FM4_TRACED_HOOK(826E8C80)
FM4_TRACED_HOOK(826E8D60)
FM4_TRACED_HOOK(826E8E30)
FM4_TRACED_HOOK(826E8FD8)
FM4_TRACED_HOOK(826E92A0)
FM4_TRACED_HOOK(826E9CF0)
FM4_TRACED_HOOK(826EBBC0)
FM4_TRACED_HOOK(826EBCB0)
FM4_TRACED_HOOK(826EC9C0)
FM4_TRACED_HOOK(826ECF30)
FM4_TRACED_HOOK(826ED400)
FM4_TRACED_HOOK(826EE8E8)
FM4_TRACED_HOOK(826EE940)
FM4_TRACED_HOOK(826EEBC0)
FM4_TRACED_HOOK(826F1390)
FM4_TRACED_HOOK(826F3228)
FM4_TRACED_HOOK(826F3660)
FM4_TRACED_HOOK(826F36C8)
FM4_TRACED_HOOK(826F3858)
FM4_TRACED_HOOK(826F64A8)
FM4_TRACED_HOOK(826F7FB0)

// PM4 writers that the N0 counters also observe.
REX_EXTERN(__imp__sub_82310818);
REX_HOOK_RAW(sub_82310818) {  // D3DDevice_RunCommandBuffer
  D3DTraceCount(0x82310818, ctx);
  N0Thread(N0Entry::kRunCommandBuffer);
  N0RunCommandBufferPre(ctx, base);
  __imp__sub_82310818(ctx, base);
}
REX_EXTERN(__imp__sub_82311080);
REX_HOOK_RAW(sub_82311080) {  // D3DDevice_DrawIndexedVertices
  D3DTraceCount(0x82311080, ctx);
  N0Thread(N0Entry::kDrawIndexedVertices);
  NativeCheckPre(0x82311080, base);
  __imp__sub_82311080(ctx, base);
  NativeCheckPost(base);
}
REX_EXTERN(__imp__sub_8233AC60);
REX_HOOK_RAW(sub_8233AC60) {  // D3DDevice_DrawVertices
  D3DTraceCount(0x8233AC60, ctx);
  N0Thread(N0Entry::kDrawVertices);
  NativeCheckPre(0x8233AC60, base);
  __imp__sub_8233AC60(ctx, base);
  NativeCheckPost(base);
}
REX_EXTERN(__imp__sub_8234D278);
REX_HOOK_RAW(sub_8234D278) {  // D3DDevice_BeginVertices
  D3DTraceCount(0x8234D278, ctx);
  N0Thread(N0Entry::kBeginVertices);
  __imp__sub_8234D278(ctx, base);
}
REX_EXTERN(__imp__sub_8234DAE8);
REX_HOOK_RAW(sub_8234DAE8) {  // D3DDevice_BeginIndexedVertices (probably)
  D3DTraceCount(0x8234DAE8, ctx);
  N0Thread(N0Entry::kBeginIndexedVertices);
  __imp__sub_8234DAE8(ctx, base);
}
REX_EXTERN(__imp__sub_823436B8);
REX_HOOK_RAW(sub_823436B8) {  // Draw flush of the shaders: IM_LOAD, LOAD_ALU_CONSTANT
  D3DTraceCount(0x823436B8, ctx);
  N0FlushPre(ctx, base);
  NativeCheckDevice(ctx.r3.u32);
  NativeCheckPre(0x823436B8, base);
  __imp__sub_823436B8(ctx, base);
  NativeCheckPost(base);
  N0FlushPost(base);
}
REX_EXTERN(__imp__sub_82370370);
REX_HOOK_RAW(sub_82370370) {  // DRAW_INDX_2 writer (clears, screen passes)
  D3DTraceCount(0x82370370, ctx);
  NativeCheckPre(0x82370370, base);
  __imp__sub_82370370(ctx, base);
  NativeCheckPost(base);
}

// Dirty state writers of D3DDevice_FlushDirtyState, RunCommandBuffer and
// EndCommandBuffer (the identity check only).
FM4_CHECKED_HOOK(823114F8)  // WriteDirtyRegisters
FM4_CHECKED_HOOK(82310E78)  // WriteDirtyFloatConstants
FM4_CHECKED_HOOK(8230FA40)  // WriteDirtyFetchConstants
FM4_CHECKED_HOOK(82379530)  // D3DDevice_WriteRegistersChunked
REX_EXTERN(__imp__sub_8237FA28);
REX_HOOK_RAW(sub_8237FA28) {  // D3DDevice_Swap
  D3DTraceCount(0x8237FA28, ctx);
  N0Thread(N0Entry::kSwap);
  N0SwapPre();
  fm4_diagnostic_swaps.fetch_add(1, std::memory_order_relaxed);
  __imp__sub_8237FA28(ctx, base);
}

// D3D entry points that only the N0 thread trace observes.
FM4_THREAD_HOOK(8233A9A8, kSetTexture)
FM4_THREAD_HOOK(82351BD0, kSetPixelShader)
FM4_THREAD_HOOK(82351A00, kSetVertexShader)
FM4_THREAD_HOOK(822D9968, kSetDepthStencilSurface)
FM4_THREAD_HOOK(823563B8, kSetRenderTarget)
FM4_THREAD_HOOK(822FEAA8, kSetViewport)
FM4_THREAD_HOOK(822FF050, kSetScissorRect)
FM4_THREAD_HOOK(823445A0, kSetStreamSource)
FM4_THREAD_HOOK(8236D840, kClear)
FM4_THREAD_HOOK(822E2120, kResolve)
FM4_THREAD_HOOK(82310FC8, kQueueCommandBuffer)
FM4_THREAD_HOOK(8294D100, kRecordCommandBuffers)
FM4_THREAD_HOOK(82371B80, kBeginTiling)
FM4_THREAD_HOOK(8236FA20, kEndTiling)
FM4_THREAD_HOOK(826ED978, kTileStreamInterpreter)
FM4_THREAD_HOOK(826EDCE8, kTileStreamDrain)
FM4_THREAD_HOOK(826EDFA8, kTileStreamWorker)

REX_EXTERN(__imp__sub_822B95D8);
REX_HOOK_RAW(sub_822B95D8) {  // D3DDevice_DrawVerticesUP
  N0Thread(N0Entry::kDrawVerticesUP);
  NativeCheckPre(0x822B95D8, base);
  __imp__sub_822B95D8(ctx, base);
  NativeCheckPost(base);
}

// Recorded command buffers.
REX_EXTERN(__imp__sub_826E23E8);
REX_HOOK_RAW(sub_826E23E8) {  // D3DDevice_BeginCommandBuffer
  N0Thread(N0Entry::kBeginCommandBuffer);
  N0BeginCommandBufferPre(ctx, base);
  __imp__sub_826E23E8(ctx, base);
}
REX_EXTERN(__imp__sub_826E4050);
REX_HOOK_RAW(sub_826E4050) {  // D3DDevice_EndCommandBuffer
  N0Thread(N0Entry::kEndCommandBuffer);
  const uint32_t recording = N0EndCommandBufferPre(ctx, base);
  __imp__sub_826E4050(ctx, base);
  N0EndCommandBufferPost(recording, base);
}
REX_EXTERN(__imp__sub_826E2350);
REX_HOOK_RAW(sub_826E2350) {  // D3DCommandBuffer_CreateClone
  const uint32_t source = ctx.r4.u32;
  __imp__sub_826E2350(ctx, base);
  N0ClonePost(source, ctx.r3.u32);
}
REX_EXTERN(__imp__sub_826E4A28);
REX_HOOK_RAW(sub_826E4A28) {  // Indirect buffer paths of RunCommandBuffer and Swap
  const uint32_t state = N0IndirectPathPre(ctx, base);
  __imp__sub_826E4A28(ctx, base);
  N0IndirectPathPost(state, ctx);
}

// Ring allocations and segment overflows (coverage log).
REX_EXTERN(__imp__sub_823913D0);
REX_HOOK_RAW(sub_823913D0) {  // BeginCommand
  N0RingAllocation(0, ctx);
  __imp__sub_823913D0(ctx, base);
}
REX_EXTERN(__imp__sub_8234D738);
REX_HOOK_RAW(sub_8234D738) {  // AllocateRingSpace
  N0RingAllocation(1, ctx);
  __imp__sub_8234D738(ctx, base);
}
REX_EXTERN(__imp__sub_822FEF08);
REX_HOOK_RAW(sub_822FEF08) {  // Segment overflow of the inline writers
  N0OverflowPre(ctx, base);
  NativeCheckOverflowPre(base);
  __imp__sub_822FEF08(ctx, base);
  NativeCheckOverflowPost(base);
  N0OverflowPost(ctx, base);
}

// The render thread's frame.
REX_EXTERN(__imp__sub_822DB318);
REX_HOOK_RAW(sub_822DB318) {
  N0Thread(N0Entry::kRenderFrame);
  const uint64_t start = N0ZoneStart();
  __imp__sub_822DB318(ctx, base);
  N0RenderFrameEnd(start);
}

// Photo save diagnostics (fm4-jpeg-diagnostics.cpp).
REX_EXTERN(__imp__sub_82714208);
REX_HOOK_RAW(sub_82714208) { TraceJpeg(ctx, base, __imp__sub_82714208, 0x82714208); }
REX_EXTERN(__imp__sub_82714388);
REX_HOOK_RAW(sub_82714388) { TraceJpeg(ctx, base, __imp__sub_82714388, 0x82714388); }
REX_EXTERN(__imp__sub_82859218);
REX_HOOK_RAW(sub_82859218) { TracePhotoSave(ctx, base, __imp__sub_82859218); }
REX_EXTERN(__imp__sub_826DEF78);
REX_HOOK_RAW(sub_826DEF78) { TracePhotoTextureLock(ctx, base, __imp__sub_826DEF78); }
REX_EXTERN(__imp__sub_822B0788);
REX_HOOK_RAW(sub_822B0788) { TracePhotoFenceWait(ctx, base, __imp__sub_822B0788); }
REX_EXTERN(__imp__sub_8273B290);
REX_HOOK_RAW(sub_8273B290) { TracePhotoCopy(ctx, base, __imp__sub_8273B290); }
REX_EXTERN(__imp__sub_82BB96E0);
REX_HOOK_RAW(sub_82BB96E0) { TracePhotoEncoder(ctx, base, __imp__sub_82BB96E0); }
