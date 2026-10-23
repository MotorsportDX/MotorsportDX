#pragma once
// Features that observe the title's D3D layer and its diagnostics. A guest
// address can have one hook definition only, so every hook of these features
// is defined in fm4-guest-hooks.cpp, which calls the functions below in a fixed
// order around the original body. Add native-path hooks there as well.
#include <rex/hook.h>

#include <array>
#include <cstdint>
#include <vector>

namespace fm4::hooks {

// fm4-d3d-trace.cpp (FM4_D3D_TRACE): call counts of the PM4 packet writers.
void D3DTraceCount(uint32_t address, const PPCContext& ctx);

// fm4-jpeg-diagnostics.cpp (FM4_CAPTURE_DIR, FM4_PHOTO_ENCODER_TRACE): the photo
// save path. Each one calls original itself.
void TraceJpeg(PPCContext& ctx, uint8_t* base, PPCFunc* original, uint32_t address);
void TracePhotoSave(PPCContext& ctx, uint8_t* base, PPCFunc* original);
void TracePhotoTextureLock(PPCContext& ctx, uint8_t* base, PPCFunc* original);
void TracePhotoFenceWait(PPCContext& ctx, uint8_t* base, PPCFunc* original);
void TracePhotoCopy(PPCContext& ctx, uint8_t* base, PPCFunc* original);
void TracePhotoEncoder(PPCContext& ctx, uint8_t* base, PPCFunc* original);

// fm4-n0-trace.cpp (FM4_N0_TRACE, FM4_N0_KEYCHECK): stage N0 counters of
// docs/analysis/d3d-hle.md. Pre and post calls bracket the original body.
enum class N0Entry : uint32_t {
  kSetTexture,
  kSetPixelShader,
  kSetVertexShader,
  kSetDepthStencilSurface,
  kSetRenderTarget,
  kSetViewport,
  kSetScissorRect,
  kSetStreamSource,
  kClear,
  kResolve,
  kDrawVertices,
  kDrawIndexedVertices,
  kDrawVerticesUP,
  kBeginVertices,
  kBeginIndexedVertices,
  kSwap,
  kRunCommandBuffer,
  kQueueCommandBuffer,  // CRenderEngine::RunCommandBuffer
  kRecordCommandBuffers,
  kBeginCommandBuffer,
  kEndCommandBuffer,
  kBeginTiling,
  kEndTiling,
  kTileStreamInterpreter,  // sub_826ED978
  kTileStreamDrain,        // sub_826EDCE8
  kTileStreamWorker,       // sub_826EDFA8
  kRenderFrame,            // sub_822DB318
  kCount
};
bool N0Enabled();
void N0Thread(N0Entry entry);
void N0SwapPre();
void N0RunCommandBufferPre(const PPCContext& ctx, uint8_t* base);
void N0BeginCommandBufferPre(const PPCContext& ctx, uint8_t* base);
// D3DCommandBuffer_CreateClone: the source recording (r4) and the clone it returned.
void N0ClonePost(uint32_t source, uint32_t clone);
// Returns the recording that EndCommandBuffer closes, for N0EndCommandBufferPost.
uint32_t N0EndCommandBufferPre(const PPCContext& ctx, uint8_t* base);
void N0EndCommandBufferPost(uint32_t recording, uint8_t* base);
// sub_826E4A28: returns the path state taken before the call, for the post call.
uint32_t N0IndirectPathPre(const PPCContext& ctx, uint8_t* base);
void N0IndirectPathPost(uint32_t state, const PPCContext& ctx);
void N0RingAllocation(uint32_t allocator, const PPCContext& ctx);
// sub_823436B8 (draw flush: IM_LOAD and LOAD_ALU_CONSTANT) and the segment
// overflow sub_822FEF08 that may run inside it.
void N0FlushPre(const PPCContext& ctx, uint8_t* base);
void N0FlushPost(uint8_t* base);
void N0OverflowPre(const PPCContext& ctx, uint8_t* base);
void N0OverflowPost(const PPCContext& ctx, uint8_t* base);
uint64_t N0ZoneStart();
void N0RenderFrameEnd(uint64_t start);

// fm4-native-check.cpp (FM4_NATIVE_CHECK): the producer side of the identity
// check of the native path. Pre and post calls bracket a PM4 writer's original
// body; the device comes from the draw flush.
void NativeCheckDevice(uint32_t address);
// pointer: the write pointer where the range starts or ends (the last dword
// written), when the writer does not keep device+48 current; 0 for device+48.
void NativeCheckPre(uint32_t writer, uint8_t* base, uint32_t pointer = 0);
void NativeCheckPost(uint8_t* base, uint32_t pointer = 0);
// Around the ring allocators and the segment overflow, which may move the
// write pointer to another segment: the range ends before and restarts after.
void NativeCheckSplitPre(uint8_t* base);
void NativeCheckSplitPost(uint8_t* base);
bool NativeCheckEnabled();

// FM4_LATE_WRITE_TRACE (fm4-native-check.cpp): the last dwords of the latest
// checked ranges before a function, compared after it.
struct LateWriteSnapshot {
  struct Entry {
    uint32_t first = 0, dwords = 0, writer = 0;
    uint64_t sequence = 0;
    std::array<uint32_t, 4> tail{};
  };
  std::vector<Entry> entries;
};
// latest: how many of the latest ranges (all 1,024 by default).
LateWriteSnapshot LateWriteCheckPre(uint32_t latest = 1024);
// pcr: the guest r13 (the thread's PCR, which holds its stack bounds).
void LateWriteCheckPost(const LateWriteSnapshot& snapshot, uint32_t function, uint32_t lr,
                        uint32_t sp, uint32_t pcr, uint8_t* base);

// fm4-native-records.cpp (host_render_native in the runtime): the converted
// dirty state writers. Returns true when the writer left a marker and a record
// instead of running its original body.
enum class NativeWriter : uint32_t { kRegisters, kFloatConstants, kFetchConstants };
bool NativeConvert(NativeWriter writer, PPCContext& ctx, uint8_t* base, PPCFunc* original);

}  // namespace fm4::hooks
