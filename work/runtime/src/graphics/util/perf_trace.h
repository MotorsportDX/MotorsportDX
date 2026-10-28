#pragma once
// Optional per-second summary of where the GPU command processor thread spends
// its time, written as JSON lines to REX_PERF_TRACE_FILE. Diagnostic only: when
// the variable is unset every helper returns immediately.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <intrin.h>
#include <map>
#include <string>
#include <vector>

#include <rex/system/gpu_progress.h>
#include <rex/ui/present_stats.h>

#include "cvar_ab.h"
#include "process_cpu.h"
#include "read_trace.h"

namespace rex::graphics::perf_trace {

enum class Wait : uint32_t {
  kGuestIdle,        // Command processor waiting for guest commands (CPU-bound).
  kFrameLatency,     // Waiting for an old frame before opening a new one (GPU-bound).
  kResolveReadback,  // Synchronous resolve readback to guest memory.
  kOcclusion,        // Occlusion query results.
  kMemexport,        // Memexport readback.
  kWaitRegMem,       // Sleeping or yielding in PM4_WAIT_REG_MEM until its condition holds.
  kReadbackPublish,  // Resolve readback copies into guest memory on their thread.
  kOther,
  kCount
};

// Event counters per window, to judge which stalls are worth removing.
enum class Count : uint32_t {
  kOcclusionAwaited,    // ROV occlusion snapshot for a report the guest spins on.
  kOcclusionUnawaited,  // ROV occlusion snapshot nobody waits for yet.
  kVizQueries,          // PM4_VIZ_QUERY begin.
  kVizDraws,            // Draw conditional on a viz query (viz_use bit).
  kBinSelects,          // SET_BIN_SELECT(_LO); HI is not counted to avoid counting pairs twice.
  kPredicatedRun,       // Predicated type-3 packet whose bin check passed.
  kPredicatedSkipped,   // Predicated type-3 packet skipped by the bin check.
  kPipelineDrawsWaited,   // Async shaders: draw waited for its pipeline, then drawn.
  kPipelineDrawsDropped,  // Async shaders: draw dropped, pipeline not ready.
  kSubmissions,           // Command lists submitted to the GPU queue.
  kUploadRanges,          // Shared memory uploads (consecutive invalidated pages each).
  kCount
};

// Wall time of command-processor work, inclusive (kDraw contains the kDraw*
// parts; kSubmit may run inside kDraw or kCopy).
enum class Zone : uint32_t {
  kDraw,
  kDrawPrimitives,
  kDrawRenderTargets,
  kDrawPipeline,
  kDrawTextures,
  kDrawBindings,
  kDrawVertexBuffers,
  kCopy,
  kSubmit,
  kSwap,
  kSharedMemoryLock,    // Acquiring the global lock in SharedMemory::RequestRanges.
  kSharedMemoryUpload,  // Uploading invalidated guest memory to the GPU buffer.
  kSubmitRecord,        // Replaying the deferred command list into the D3D12 one.
  kSubmitExecute,       // Close + ExecuteCommandLists + Signal.
  kSharedMemoryProtect, // Upload: marking pages valid and watching them again.
  kSharedMemoryCopy,    // Upload: copying guest memory into the upload buffer.
  kDrawSetup,           // Draw: shader analysis and the submission, before the primitives.
  kDrawState,           // Draw: pipeline bind, viewport and fixed function state.
  kDrawCall,            // Draw: topology, memexport and the draw call, after the vertex buffers.
  kDrawPreTargets,      // Draw: between the primitives and the render targets.
  kDrawPrePipeline,     // Draw: between the render targets and the pipeline.
  kDrawPreTextures,     // Draw: between the pipeline and the textures.
  kBindingsRoot,        // Bindings: root signature and the common root constant buffers.
  kBindingsConstants,   // Bindings: the constant buffer data.
  kBindingsDescriptors, // Bindings: the descriptors.
  kBindingsRootParameters,  // Bindings: the root parameters.
  kTexturesKeys,        // Textures: the keys of the bindings out of sync.
  kTexturesLoads,       // Textures: the shared memory requests and texture loads.
  kTexturesBindings,    // Textures: the binding update of changed bindings.
  kCount
};

// With REX_CP_SPLIT=1 (and the trace), packet handlers are timed by kind,
// exclusive of each other: register writes (type 0 and 1, SET_CONSTANT(2),
// LOAD_ALU_CONSTANT, SET_SHADER_CONSTANTS, bin masks and selects), shader loads
// (IM_LOAD, IM_LOAD_IMMEDIATE), draw packets (DRAW_INDX, DRAW_INDX_2, with the
// draw and copy zones and any wait inside them) and other packets. Indirect
// buffers, WAIT_REG_MEM, EVENT_WRITE_ZPD and the swap are not timed here (they
// nest packets or wait). Written as "cp_split_ms"; the time stamp counter keeps
// the cost per packet low, but it is not zero, so compare cp_busy with a run
// without the switch.
enum class Split : uint32_t {
  kRegisters,
  kShaders,
  kDraw,
  kOther,
  kCount,
  kNone = kCount
};

struct State {
  std::ofstream output;
  Wait cause = Wait::kOther;
  uint64_t frequency = 0, origin = 0, window_start = 0, last_swap = 0;
  // ab_cvar: the side the window's frames were drawn with (-1: off, or the
  // window before the first flip) and the index of its period.
  int32_t ab_side = -1;
  uint64_t ab_period = 0;
  uint64_t max_frame = 0, readback_bytes = 0, upload_bytes = 0;
  uint32_t swaps = 0, draws = 0, copies = 0, readbacks = 0;
  uint64_t wait_ticks[size_t(Wait::kCount)]{};
  uint32_t waits[size_t(Wait::kCount)]{};
  uint32_t counts[size_t(Count::kCount)]{};
  uint64_t zone_ticks[size_t(Zone::kCount)]{};
  std::vector<float> frame_ms;
  // GPU timestamps of frames that completed in this window.
  uint32_t gpu_frames = 0;
  double gpu_busy_ms = 0, gpu_resolve_ms = 0, gpu_span_ms = 0;
  // Texture loads and resolve readback copies (N3, d3d-hle.md 16.1).
  double gpu_load_ms = 0, gpu_readback_ms = 0;
  // Of the resolves and loads, those of at least 1280x720 (d3d-hle.md 16.7).
  double gpu_resolve_full_ms = 0, gpu_load_full_ms = 0;
  // Ownership transfers between host render targets (d3d-hle.md 16.12), also
  // those inside resolves.
  double gpu_transfer_ms = 0, gpu_transfer_depth_ms = 0;
  // Resolves by kind (full screen, at least 256x256 at 32bpp, smaller, clears
  // only): GPU time and count.
  double gpu_resolve_bucket_ms[4] = {};
  uint64_t gpu_resolve_bucket_count[4] = {};
  // The parts of the full-screen resolves (kResolveParts).
  double gpu_resolve_part_ms[5] = {};
  // The host texture writes of resolves by class (kResolveHostWrites): GPU
  // time and count.
  double gpu_host_write_ms[4] = {};
  uint64_t gpu_host_write_count[4] = {};
  // The post pass groups (kGpuPasses) and the swap's passes (d3d-hle-n4.md 17.24).
  double gpu_pass_ms[6 + 1] = {};
  // Samples passing depth/stencil per occlusion snapshot interval, as reported
  // to the guest (after any divisor), to compare paths and calibrate.
  double occlusion_samples[2]{};  // Unawaited, awaited.
  uint32_t occlusion_intervals[2]{};
  // Command processor draining the submit thread before a synchronous
  // submission (the swap): calls, calls that blocked, jobs still queued or
  // running at the call, and wall time spent. Counted as busy, not as a wait,
  // so cp_busy stays comparable with traces that lack it.
  uint32_t submit_flushes = 0, submit_flushes_blocked = 0, submit_flush_jobs = 0;
  uint64_t submit_flush_ticks = 0;
  uint64_t split_tsc[size_t(Split::kCount)]{};
  uint32_t split_packets[size_t(Split::kCount)]{};
  // REX_CP_SPLIT: the same split inside indirect buffers at depth two and more
  // (the replays of recorded command buffers, executed from the command
  // buffer segments; docs/analysis/d3d-hle.md, section 15.2), their total time
  // stamp counter ticks, count and dwords.
  uint64_t replay_split_tsc[size_t(Split::kCount)]{};
  uint64_t replay_tsc = 0, replays = 0, replay_dwords = 0;
  uint64_t window_tsc = 0;
  // Host render thread (host_render_thread): records taken, and the sum and
  // maximum of the queue depth when each was taken.
  uint64_t host_records = 0, host_depth_sum = 0, host_depth_max = 0;
  // And each record's wait in the queue, from its push to its take (d3d-hle-n4.md
  // 17.18): sum, maximum and 0.25 ms bins (the last holds 16 ms and more), in ns.
  static constexpr uint32_t kHostWaitBins = 65;
  uint64_t host_waited = 0, host_wait_sum = 0, host_wait_max = 0;
  uint32_t host_wait_bins[kHostWaitBins]{};
  // Frames over kLongFrameMs: their time, the shader work on the draw path
  // inside them, and of that the FXC compiles of look mods and of post passes
  // (ShaderWork, d3d-hle-n6.md 19.3.4).
  std::vector<std::array<float, 4>> long_frames;
};

// The frames the trace lists with their shader work (long_frames).
constexpr double kLongFrameMs = 50.0;

// Every resolve destination seen in the run, rewritten as JSON next to the
// trace (<REX_PERF_TRACE_FILE>.resolves.json) every 10 windows. Used to find
// which resolves the CPU actually needs.
struct ResolveInventory {
  struct Entry {
    uint64_t count = 0;
    double first_s = 0, last_s = 0;
  };
  // address, length, format, pitch, height.
  std::map<std::array<uint32_t, 5>, Entry> entries;
  std::string path;
  uint32_t windows = 0;
};

inline uint64_t Now() {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
}

inline State* Get() {
  static State* state = []() -> State* {
    const char* path = std::getenv("REX_PERF_TRACE_FILE");
    if (!path || !*path) return nullptr;
    auto* result = new State;
    result->output.open(path, std::ios::app);
    if (!result->output.is_open()) return nullptr;
    result->frequency = 1000000000;
    result->origin = result->window_start = Now();
    result->window_tsc = __rdtsc();
    return result;
  }();
  return state;
}

inline bool SplitEnabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("REX_CP_SPLIT");
    return Get() && value && *value && *value != '0';
  }();
  return enabled;
}
// Depth of the indirect buffer being executed (command processor or host
// render thread only).
inline uint32_t indirect_depth = 0;

class SplitScope {
 public:
  explicit SplitScope(Split split)
      : split_(SplitEnabled() ? split : Split::kNone),
        start_(split_ != Split::kNone ? __rdtsc() : 0) {}
  ~SplitScope() {
    if (split_ == Split::kNone) return;
    State* state = Get();
    const uint64_t ticks = __rdtsc() - start_;
    state->split_tsc[size_t(split_)] += ticks;
    ++state->split_packets[size_t(split_)];
    if (indirect_depth >= 2) state->replay_split_tsc[size_t(split_)] += ticks;
  }

 private:
  Split split_;
  uint64_t start_;
};

// Attributes fence waits inside its scope to a cause.
class CauseScope {
 public:
  explicit CauseScope(Wait cause) : state_(Get()) {
    if (!state_) return;
    previous_ = state_->cause;
    state_->cause = cause;
  }
  ~CauseScope() {
    if (state_) state_->cause = previous_;
  }

 private:
  State* state_;
  Wait previous_ = Wait::kOther;
};

inline void AddWait(uint64_t start, Wait cause) {
  State* state = Get();
  if (!state) return;
  state->wait_ticks[size_t(cause)] += Now() - start;
  ++state->waits[size_t(cause)];
}
inline void AddFenceWait(uint64_t start) {
  if (State* state = Get()) AddWait(start, state->cause);
}
inline void CountDraw() {
  if (State* state = Get()) ++state->draws;
}
inline void CountCopy() {
  if (State* state = Get()) ++state->copies;
}
// Shared memory invalidations by guest CPU writes (any thread): count and bytes
// invalidated, including the pages around the written one.
inline std::atomic<uint32_t> invalidations{0};
inline std::atomic<uint64_t> invalidated_bytes{0};
inline void CountInvalidation(uint64_t bytes) {
  if (Get()) {
    invalidations.fetch_add(1, std::memory_order_relaxed);
    invalidated_bytes.fetch_add(bytes, std::memory_order_relaxed);
  }
}

// Guest vblanks by how the vsync thread delivered them (any thread): on the
// refresh grid, late (vblank_vrr: the grid vblank passed while the command
// processor worked and was delivered when it waited), forced (two intervals
// without a wait).
inline std::atomic<uint32_t> vblanks[3]{};
inline void CountVblank(uint32_t kind) {
  if (Get()) vblanks[kind].fetch_add(1, std::memory_order_relaxed);
}

// Host render thread (host_render_thread, stage N1 of docs/analysis/d3d-hle.md):
// set when it replaces the command processor thread, so that the windows carry
// render_thread_busy_ms (the thread's wall time not spent waiting, the same
// measure as cp_busy), queue_depth_mean and queue_depth_max (the records queued
// when each was taken) and host_records and host_merged_spans (PM4 spans merged
// into the last record because the queue was full; guest threads).
inline std::atomic<bool> host_render{false};
inline std::atomic<uint32_t> host_merged_spans{0};
// The identity check (host_render_check): ranges compared by writer, the
// mismatches by kind (the PM4 changed after production, a register, the type-3
// packets and predicate decisions, the draws) and the registers that differed
// (these three on the host render thread only), ranges never compared (replaced
// by a newer copy at the same address before they were reached, or left before
// their end) and ranges changed on purpose (host_render_check_perturb).
enum class HostCheckReason : uint32_t { kBytes, kRegisters, kType3, kDraws, kCount };
// The records of converted writers (host_render_native): records applied and
// the registers they wrote (host render thread only), markers reached without a
// record.
inline std::atomic<bool> host_records{false};
inline uint64_t host_records_applied = 0, host_record_registers = 0, host_records_empty = 0;
inline std::atomic<uint64_t> host_markers_missing{0};
// The op lists of replays (host_render_oplists): replays run from an op list,
// op lists decoded (host render thread only), op lists dropped by a write.
inline std::atomic<bool> host_oplists{false};
// d3d12_reuse_draw_bindings: fetch constant writes that changed no dword.
inline uint64_t fetch_writes_unchanged = 0;
// native_rt_resolve (d3d-hle.md 16.7): full-screen 32bpp resolves that looked
// for host textures, the textures written, the resolves with none, and the
// checked textures and those that differed (native_rt_resolve_check).
inline std::atomic<uint64_t> native_resolves{0}, native_resolve_textures{0},
    native_resolve_misses{0}, native_resolve_checks{0}, native_resolve_mismatches{0};
inline void CountNativeResolve(uint32_t textures) {
  native_resolves.fetch_add(1, std::memory_order_relaxed);
  native_resolve_textures.fetch_add(textures, std::memory_order_relaxed);
  if (!textures) native_resolve_misses.fetch_add(1, std::memory_order_relaxed);
}
// Textures at the scene's scale (d3d-hle-n5.md 18.15): created by a resolve,
// and fetched before any resolve wrote them.
inline std::atomic<uint64_t> native_resolve_scene_created{0}, native_resolve_scene_unwritten{0};
inline void CountSceneTextureCreated() {
  native_resolve_scene_created.fetch_add(1, std::memory_order_relaxed);
}
inline void CountSceneTextureUnwritten() {
  native_resolve_scene_unwritten.fetch_add(1, std::memory_order_relaxed);
}
// native_rt_resolve_skip (d3d-hle-n6.md 19.7.4): host texture writes left to
// the texture's load and to its packed_mips sibling, the sibling copies made
// when such a texture was sampled, and those that found no sibling with the
// resolve's data.
inline std::atomic<uint64_t> native_resolve_skips_load{0}, native_resolve_skips_sibling{0},
    native_resolve_fallbacks{0}, native_resolve_fallbacks_missing{0};
inline void CountResolveTextureSkip(bool sibling) {
  (sibling ? native_resolve_skips_sibling : native_resolve_skips_load)
      .fetch_add(1, std::memory_order_relaxed);
}
inline void CountResolveTextureFallback(bool copied) {
  (copied ? native_resolve_fallbacks : native_resolve_fallbacks_missing)
      .fetch_add(1, std::memory_order_relaxed);
}
// native_rt_resolve_layout (d3d-hle-n6.md 19.7.5): the guest's layout of a
// full-screen destination at the scene's scale, written from its host texture
// or from the render target (no texture took the resolve's data), deferred,
// what became of the deferred ones: replaced by the same resolve again,
// written for a reader (a texture
// load, another GPU request, a CPU readback, an overlapping resolve, the
// check's probe) or when too many were pending, dropped by the perturbation; CPU writes over them;
// the source textures' loads while their layouts are deferred (a scene
// texture's load reads no memory); and the faults: a source texture's data
// changed (the invariant), GPU writes over a deferred layout that no hook
// ordered, and readers off the command processor's thread.
enum class GuestLayout : uint32_t {
  kFromTexture,
  kFromTarget,
  kDeferred,
  kSuperseded,
  kWrittenLoad,
  kWrittenRequest,
  kWrittenReadback,
  kWrittenResolve,
  kWrittenOverflow,
  kWrittenProbe,
  kPerturbed,
  kCpuWrites,
  kInvariant,
  kForeignThread,
  kSourceLoads,
  kGpuWrites,
  kCount
};
inline std::atomic<uint64_t> guest_layout[size_t(GuestLayout::kCount)];
inline void CountGuestLayout(GuestLayout what) {
  guest_layout[size_t(what)].fetch_add(1, std::memory_order_relaxed);
}
// native_rt_resolve_layout_check: the layouts compared by class (0 nearest
// 8_8_8_8, 1 nearest 2_10_10_10, 2 box 8_8_8_8, 3 box 2_10_10_10), their
// pixels, the pixels and bytes that differ, and the largest difference of one
// channel in encoded steps.
inline std::atomic<uint64_t> guest_layout_checks[4], guest_layout_check_pixels[4],
    guest_layout_check_changed[4], guest_layout_check_bytes[4];
inline std::atomic<uint32_t> guest_layout_check_max_step[4];
inline void CountGuestLayoutCheck(uint32_t check_class, uint64_t pixels, uint64_t changed,
                                  uint64_t bytes, uint32_t max_step) {
  guest_layout_checks[check_class].fetch_add(1, std::memory_order_relaxed);
  guest_layout_check_pixels[check_class].fetch_add(pixels, std::memory_order_relaxed);
  guest_layout_check_changed[check_class].fetch_add(changed, std::memory_order_relaxed);
  guest_layout_check_bytes[check_class].fetch_add(bytes, std::memory_order_relaxed);
  uint32_t previous = guest_layout_check_max_step[check_class].load(std::memory_order_relaxed);
  while (max_step > previous &&
         !guest_layout_check_max_step[check_class].compare_exchange_weak(
             previous, max_step, std::memory_order_relaxed)) {
  }
}
// Scene draws at a fractional scale (d3d-hle-n5.md 18.16): pixel shader
// translations and pipelines with the scene_scale modification made while the
// title runs, not loaded from the storage.
inline std::atomic<uint64_t> scene_variant_translations{0}, scene_variant_pipelines{0};
// native_rt_surfaces=1 (d3d-hle.md 16.9): ownership transfers, their tiles and
// those left after the cutout, how the transferred tiles ended (dropped when
// another render target took them, overwritten, read by a draw or a resolve),
// and the transfers that ended with no tile read and with some.
enum class StaleTileOutcome : uint32_t { kDropped, kOverwritten, kReadByDraw, kReadByResolve,
                                         kCount };
inline std::atomic<uint64_t> stale_transfers{0}, stale_transfer_tiles{0}, stale_pending_tiles{0},
    stale_tiles[size_t(StaleTileOutcome::kCount)]{}, stale_transfers_unread{0},
    stale_transfers_read{0};
inline void CountStaleTransfer(uint32_t tiles, uint32_t pending) {
  stale_transfers.fetch_add(1, std::memory_order_relaxed);
  stale_transfer_tiles.fetch_add(tiles, std::memory_order_relaxed);
  stale_pending_tiles.fetch_add(pending, std::memory_order_relaxed);
  if (!pending) stale_transfers_unread.fetch_add(1, std::memory_order_relaxed);
}
inline void CountStaleTile(StaleTileOutcome outcome) {
  stale_tiles[size_t(outcome)].fetch_add(1, std::memory_order_relaxed);
}
inline void CountStaleTransferEnd(bool read) {
  (read ? stale_transfers_read : stale_transfers_unread).fetch_add(1, std::memory_order_relaxed);
}
// native_rt_surfaces bit 4 (d3d-hle.md 16.12), in tiles: copied at the claim,
// deferred; and how deferrals end or go on: claimed back by their holder (no
// copy), deferred again for a new owner, copied for a new owner from the
// holder, inside an overwrite draw's cutout (no copy), copied before a draw, a
// resolve's copy, a resolve's clear or a post pass reads them; the check's
// violations and perturbed (skipped) copies, and its hazards (a render target
// may draw where it holds another owner's tile); the deferred tiles at the end
// of the window.
enum class LazyTileOutcome : uint32_t { kCopied, kDeferred, kClaimedBack, kFlushedDraw,
                                        kFlushedResolve, kFlushedOther, kViolation, kPerturbed,
                                        kRedeferred, kChainCopied, kOverwritten, kFlushedClear,
                                        kHazard, kCount };
inline std::atomic<uint64_t> lazy_tiles[size_t(LazyTileOutcome::kCount)]{};
inline std::atomic<uint64_t> lazy_pending_tiles{0};
// native_rt_surfaces bit 8 (d3d-hle.md 16.15): the host render targets, their
// memory now and at its peak, and the growths and evictions of the window.
inline std::atomic<uint64_t> rt_memory_bytes{0}, rt_memory_peak_bytes{0}, rt_targets{0},
    rt_resizes{0}, rt_evictions{0};
inline void SetRenderTargetMemory(uint64_t bytes, uint64_t peak, uint32_t targets) {
  rt_memory_bytes.store(bytes, std::memory_order_relaxed);
  rt_memory_peak_bytes.store(peak, std::memory_order_relaxed);
  rt_targets.store(targets, std::memory_order_relaxed);
}
inline void CountRenderTargetResize() { rt_resizes.fetch_add(1, std::memory_order_relaxed); }
// native_one_pass_msaa (d3d-hle.md 16.17): draws, resolves and clears of the
// private scene surfaces (the depth, and at 4x the colour), and the
// violations (their guest tiles reached through the EDRAM tile map, or a 4x
// resolve of them left to the dump).
inline std::atomic<uint64_t> one_pass_draws{0}, one_pass_resolves{0}, one_pass_clears{0},
    one_pass_violations{0};
inline void CountOnePass(std::atomic<uint64_t>& counter, uint64_t count = 1) {
  counter.fetch_add(count, std::memory_order_relaxed);
}
inline void CountRenderTargetEvictions(uint32_t count) {
  rt_evictions.fetch_add(count, std::memory_order_relaxed);
}
inline void CountLazyTiles(LazyTileOutcome outcome, uint32_t tiles) {
  lazy_tiles[size_t(outcome)].fetch_add(tiles, std::memory_order_relaxed);
}
// native_rt_surfaces bit 2: resolves that could read the host render target,
// those that did (not in a check frame, nothing failed), the byte checks and
// the mismatches.
inline std::atomic<uint64_t> host_resolves_found{0}, host_resolves_done{0}, host_resolves_other{0},
    host_resolve_checks{0}, host_resolve_mismatches{0};
inline void CountHostResolve(bool found, bool done) {
  (found ? host_resolves_found : host_resolves_other).fetch_add(1, std::memory_order_relaxed);
  if (done) host_resolves_done.fetch_add(1, std::memory_order_relaxed);
}
// The byte checks by the source's format: color formats 0 to 15, depth 16 and
// 17.
inline std::atomic<uint64_t> host_resolve_check_formats[18]{};
inline void CountHostResolveCheckFormat(bool is_depth, uint32_t format) {
  host_resolve_check_formats[(is_depth ? 16 : 0) + (format & 15)].fetch_add(
      1, std::memory_order_relaxed);
}
inline void CountHostResolveChecks(uint32_t checked, uint32_t mismatched) {
  host_resolve_checks.fetch_add(checked, std::memory_order_relaxed);
  host_resolve_mismatches.fetch_add(mismatched, std::memory_order_relaxed);
}
inline void CountNativeResolveChecks(uint32_t checked, uint32_t mismatched) {
  native_resolve_checks.fetch_add(checked, std::memory_order_relaxed);
  native_resolve_mismatches.fetch_add(mismatched, std::memory_order_relaxed);
}

// The constant buffers of the draws (d3d12_deferred_constants, N4 step 1 of
// d3d-hle.md, section 17), on the thread that runs the draws: the float, bool
// and loop, and fetch constant buffers filled on either path and their 16-byte
// slots, the slots captured from the register file for the replay (deferred
// only), the sampler parameters computed and the descriptor index buffers
// written (bindless). Kept apart from the replay's counters, which another
// thread writes.
// With REX_N4_REGISTER_DELTA=1 also the context registers (0x2000 to 0x23FF,
// where every register of the draw's pipeline, fixed function and system
// constant state lies) that changed since the previous draw: draws compared,
// changed dwords and changed 16-byte granules (d3d-hle.md 17.5).
struct alignas(64) DrawConstantCounts {
  uint64_t fills = 0, fill_slots = 0, captured_slots = 0, samplers = 0, descriptor_fills = 0;
  uint64_t delta_draws = 0, delta_dwords = 0, delta_granules = 0;
};
inline DrawConstantCounts draw_constants;
inline void CountConstantFills(uint32_t fills, uint32_t slots, uint32_t captured) {
  if (!Get()) return;
  draw_constants.fills += fills;
  draw_constants.fill_slots += slots;
  draw_constants.captured_slots += captured;
}
inline void CountDrawDescriptors(uint32_t samplers, uint32_t descriptor_fills) {
  if (!Get()) return;
  draw_constants.samplers += samplers;
  draw_constants.descriptor_fills += descriptor_fills;
}
inline bool RegisterDeltaEnabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("REX_N4_REGISTER_DELTA");
    return Get() && value && *value && *value != '0';
  }();
  return enabled;
}
inline void CountRegisterDelta(uint32_t dwords, uint32_t granules) {
  ++draw_constants.delta_draws;
  draw_constants.delta_dwords += dwords;
  draw_constants.delta_granules += granules;
}
// The replay of the deferred constants (the thread that replays the deferred
// command list): time stamp counter ticks, buffers compared with the inline
// bytes (d3d12_deferred_constants_check), those that differed, and comparisons
// changed on purpose.
struct alignas(64) ConstantReplayCounts {
  std::atomic<uint64_t> tsc{0}, commands{0}, checks{0}, mismatches{0}, perturbed{0};
};
inline ConstantReplayCounts constant_replay;
inline void AddDeferredConstantReplay(uint64_t tsc, uint32_t checks, uint32_t mismatches,
                                      uint32_t perturbed) {
  if (!Get()) return;
  constant_replay.tsc.fetch_add(tsc, std::memory_order_relaxed);
  constant_replay.commands.fetch_add(1, std::memory_order_relaxed);
  if (checks) {
    constant_replay.checks.fetch_add(checks, std::memory_order_relaxed);
    constant_replay.mismatches.fetch_add(mismatches, std::memory_order_relaxed);
    constant_replay.perturbed.fetch_add(perturbed, std::memory_order_relaxed);
  }
}

// The draw back end's capture (d3d12_draw_back_end, N4 step 2 of d3d-hle.md,
// 17.6), on the thread that runs the draws: commands written, their bytes
// without the check's, the context registers, fetch constant slots and bytes
// of the front half's groups in them, and the time stamp counter ticks of the
// capture.
struct alignas(64) BackEndCaptureCounts {
  uint64_t captures = 0, bytes = 0, context_dwords = 0, written_dwords = 0, fetch_slots = 0,
           group_bytes = 0, tsc = 0;
};
inline BackEndCaptureCounts back_end_capture;
// written_dwords: the context registers written since the previous capture,
// before the back end's set and the change filter.
inline void CountBackEndCapture(uint32_t bytes, uint32_t context_dwords, uint32_t written_dwords,
                                uint32_t fetch_slots, uint32_t group_bytes, uint64_t tsc) {
  if (!Get()) return;
  ++back_end_capture.captures;
  back_end_capture.bytes += bytes;
  back_end_capture.context_dwords += context_dwords;
  back_end_capture.written_dwords += written_dwords;
  back_end_capture.fetch_slots += fetch_slots;
  back_end_capture.group_bytes += group_bytes;
  back_end_capture.tsc += tsc;
}
// REX_N4_CAPTURE_SPLIT=1 (with the trace): the capture by part (the context
// registers, the constant masks, the front half's groups, the command's
// allocation in the deferred command list, the record up to the constants,
// the rest with the check) in time stamp counter ticks, and its constant slots
// by kind: float, fetch, bool and loop.
constexpr size_t kCaptureSplitParts = 6;
struct alignas(64) CaptureSplitCounts {
  uint64_t tsc[kCaptureSplitParts] = {};
  uint64_t float_slots = 0, fetch_slots = 0, bool_loop_slots = 0;
};
inline CaptureSplitCounts capture_split;
inline bool CaptureSplitEnabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("REX_N4_CAPTURE_SPLIT");
    return Get() && value && *value && *value != '0';
  }();
  return enabled;
}
// Its replay (the thread that replays the deferred command list): commands,
// time stamp counter ticks without the check, draws checked, mismatches by kind
// (pipeline, viewport, scissor, fixed function, system constants, samplers)
// and comparisons changed on purpose.
// With d3d12_draw_back_end=2 also the pipelines the replay created (and its
// time stamp counter ticks for them) and the draws it dropped.
struct alignas(64) BackEndReplayCounts {
  std::atomic<uint64_t> replays{0}, tsc{0}, checks{0}, perturbed{0};
  std::atomic<uint64_t> pipelines{0}, pipeline_tsc{0}, dropped{0};
  std::atomic<uint64_t> mismatches[7]{};
};
inline BackEndReplayCounts back_end_replay;

// REX_N4_REPLAY_SPLIT=1 (with the trace): the replay of the deferred command
// lists by command kind (DeferredCommandList::Command order): time stamp
// counter ticks and counts, and the ticks of whole Execute calls (the replaying
// thread; N4 step 2, d3d-hle.md 17.8).
constexpr size_t kReplaySplitKinds = 64;
struct alignas(64) ReplaySplitCounts {
  std::atomic<uint64_t> execute_tsc{0}, executes{0};
  std::atomic<uint64_t> tsc[kReplaySplitKinds]{};
  std::atomic<uint64_t> counts[kReplaySplitKinds]{};
};
inline ReplaySplitCounts replay_split;
inline bool ReplaySplitEnabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("REX_N4_REPLAY_SPLIT");
    return Get() && value && *value && *value != '0';
  }();
  return enabled;
}
inline void AddReplaySplit(const uint64_t* tsc, const uint32_t* counts, size_t kinds,
                           uint64_t execute_tsc) {
  replay_split.execute_tsc.fetch_add(execute_tsc, std::memory_order_relaxed);
  replay_split.executes.fetch_add(1, std::memory_order_relaxed);
  for (size_t i = 0; i < kinds && i < kReplaySplitKinds; ++i) {
    if (counts[i]) {
      replay_split.tsc[i].fetch_add(tsc[i], std::memory_order_relaxed);
      replay_split.counts[i].fetch_add(counts[i], std::memory_order_relaxed);
    }
  }
}

// render_target_update_guard: the draws keyed (clipping on), those that
// skipped the render target cache's update, and with its check the updates
// run anyway and compared, the verdicts changed on purpose and the mismatches.
struct RenderTargetGuardCounts {
  uint64_t draws = 0, skips = 0, checks = 0, perturbed = 0, mismatches = 0;
};
inline RenderTargetGuardCounts render_target_guard;

// REX_N4_TEXTURE_KEYS=1 (with the trace): the keys that RequestTextures
// computes again for used bindings out of sync: all of them, those whose fetch
// constant has the dwords of the binding's last one, those whose binding stayed
// the same with other dwords, those whose key changed, and of these the keys
// equal to one of the binding's last four.
//
// And the binding resets by cause (d3d-hle-n6.md 19.6: the frame start, a
// texture outdated by the GPU, by the CPU, by both, textures destroyed): the
// resets, the valid bindings reset, the keys computed again for them, those
// that found the textures they had, those that loaded a texture, the bindings
// texture_binding_resets kept, and at an outdated reset, the valid bindings
// with an outdated texture.
struct BindingResetCounts {
  uint64_t resets = 0, bindings = 0, keys = 0, same_textures = 0, loads = 0, kept = 0,
           outdated = 0;
};
inline constexpr size_t kBindingResetCauses = 5;
struct TextureKeyCounts {
  uint64_t recomputes = 0, same_dwords = 0, same_binding = 0, key_changes = 0, recent_keys = 0;
  BindingResetCounts resets[kBindingResetCauses];
};
inline TextureKeyCounts texture_keys;
inline bool TextureKeysEnabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("REX_N4_TEXTURE_KEYS");
    return Get() && value && *value && *value != '0';
  }();
  return enabled;
}

// The binding check (d3d12_reuse_draw_bindings_check): bindings kept in sync
// compared with fresh keys, mismatches, comparisons changed on purpose.
inline std::atomic<uint64_t> binding_checks{0}, binding_check_mismatches{0},
    binding_check_perturbed{0};
// With texture_binding_resets (d3d-hle-n6.md 19.6): in sync bindings whose
// texture differs from a lookup of their key, in sync bindings with an
// outdated texture, those met while another texture became outdated (not
// counted as mismatches), and the resets left out on purpose.
inline std::atomic<uint64_t> binding_check_textures{0}, binding_check_outdated{0},
    binding_check_outdated_racing{0}, binding_check_resets_perturbed{0};
inline uint64_t host_oplist_replays = 0, host_oplists_decoded = 0;
inline std::atomic<uint64_t> host_oplists_invalidated{0}, host_oplists_stale{0};
inline std::atomic<bool> host_check{false};
inline std::map<uint32_t, uint64_t> host_checks;
inline uint64_t host_check_reasons[size_t(HostCheckReason::kCount)]{};
inline std::map<uint32_t, uint64_t> host_check_registers;
inline std::atomic<uint64_t> host_check_mismatches{0}, host_check_overwritten{0},
    host_check_incomplete{0}, host_check_dropped{0}, host_check_perturbed{0};
inline void CountHostMergedSpan() {
  if (Get()) host_merged_spans.fetch_add(1, std::memory_order_relaxed);
}
inline void CountHostRecord(uint64_t depth, uint64_t pushed) {
  State* state = Get();
  if (!state) return;
  ++state->host_records;
  state->host_depth_sum += depth;
  state->host_depth_max = depth > state->host_depth_max ? depth : state->host_depth_max;
  if (pushed) {
    const uint64_t now = Now();
    const uint64_t wait = now > pushed ? now - pushed : 0;
    ++state->host_waited;
    state->host_wait_sum += wait;
    state->host_wait_max = wait > state->host_wait_max ? wait : state->host_wait_max;
    ++state->host_wait_bins[std::min<uint64_t>(wait / 250000, State::kHostWaitBins - 1)];
  }
}

// Shader work in play (docs/analysis/d3d-hle-n6.md 19.3.4), with the trace.
// On the draw path, where a draw meets it: the DXBC translations of vertex and
// pixel shaders and the look mods' FXC compiles and override cache loads where
// the draw is issued (the host render thread or the command processor), the
// pipelines created for a draw (there, or on the draw back end's replay), and
// the post passes' FXC compiles. Off it, on the compile workers
// (shader_override_async): the mods compiled, and the pipelines recreated when
// a compiled mod replaces the plain translation. Not counted: the start-up's
// translations and pipelines (InitializeShaderStorage) and hot reload.
enum class ShaderWork : uint32_t {
  kVertexTranslation,
  kPixelTranslation,
  kModCompile,
  kModCacheLoad,
  kPipeline,
  kModSwap,
  kPassCompile,
  kModCompileWorker,
  kCount
};
inline constexpr const char* kShaderWorkNames[] = {
    "translations_vs", "translations_ps", "mod_compiles", "mod_cache_loads",
    "pipelines",       "mod_swaps",       "pass_compiles", "mod_compiles_worker"};
static_assert(sizeof(kShaderWorkNames) / sizeof(kShaderWorkNames[0]) == size_t(ShaderWork::kCount));
// The plan's threshold for one piece of work on the draw path (19.3.4).
constexpr uint64_t kShaderWorkLongNs = 4000000;
struct alignas(64) ShaderWorkCounts {
  std::atomic<uint64_t> count{0}, ns{0}, max_ns{0}, long_count{0};
};
inline ShaderWorkCounts shader_work[size_t(ShaderWork::kCount)];
// The draw path's shader work since the last swap, and of it the FXC compiles
// of look mods and of post passes.
inline std::atomic<uint64_t> frame_shader_ns{0}, frame_mod_compile_ns{0},
    frame_pass_compile_ns{0};
// Set on a thread while it translates shaders for a draw: only then do the
// translations and the mods' compiles and cache loads count.
inline thread_local bool shader_work_for_draw = false;
class ShaderWorkForDrawScope {
 public:
  ShaderWorkForDrawScope() : previous_(shader_work_for_draw) { shader_work_for_draw = true; }
  ~ShaderWorkForDrawScope() { shader_work_for_draw = previous_; }

 private:
  bool previous_;
};
// Mods queued for a compile worker or for their swap (shader_override_async).
inline std::atomic<int64_t> mods_pending{0};
inline void AddShaderWork(ShaderWork kind, uint64_t ns) {
  if (!Get()) return;
  ShaderWorkCounts& counts = shader_work[size_t(kind)];
  counts.count.fetch_add(1, std::memory_order_relaxed);
  counts.ns.fetch_add(ns, std::memory_order_relaxed);
  uint64_t max = counts.max_ns.load(std::memory_order_relaxed);
  while (ns > max && !counts.max_ns.compare_exchange_weak(max, ns, std::memory_order_relaxed)) {
  }
  if (kind == ShaderWork::kModCompileWorker || kind == ShaderWork::kModSwap) return;
  if (ns > kShaderWorkLongNs) counts.long_count.fetch_add(1, std::memory_order_relaxed);
  frame_shader_ns.fetch_add(ns, std::memory_order_relaxed);
  if (kind == ShaderWork::kModCompile) {
    frame_mod_compile_ns.fetch_add(ns, std::memory_order_relaxed);
  } else if (kind == ShaderWork::kPassCompile) {
    frame_pass_compile_ns.fetch_add(ns, std::memory_order_relaxed);
  }
}

// Submit thread (its own thread): jobs finished, and wall time per job in
// total (busy), replaying the deferred command list (record) and Close +
// ExecuteCommandLists + Signal (execute). Callbacks (with
// d3d12_submit_thread_async_swap: the presenter's fence signal and the
// presentation of the frame) are counted separately and are part of busy.
inline std::atomic<uint32_t> submit_thread_jobs{0}, submit_thread_callbacks{0};
inline std::atomic<uint64_t> submit_thread_ns[4]{};  // Busy, record, execute, callback.
inline void AddSubmitThreadJob(uint64_t start, uint64_t recorded, uint64_t executed,
                               uint64_t end) {
  submit_thread_jobs.fetch_add(1, std::memory_order_relaxed);
  submit_thread_ns[0].fetch_add(end - start, std::memory_order_relaxed);
  submit_thread_ns[1].fetch_add(recorded - start, std::memory_order_relaxed);
  submit_thread_ns[2].fetch_add(executed - recorded, std::memory_order_relaxed);
}

inline void AddSubmitThreadCallback(uint64_t start, uint64_t end) {
  submit_thread_callbacks.fetch_add(1, std::memory_order_relaxed);
  submit_thread_ns[0].fetch_add(end - start, std::memory_order_relaxed);
  submit_thread_ns[3].fetch_add(end - start, std::memory_order_relaxed);
}

inline void AddSubmitFlush(uint64_t start, uint32_t pending_jobs) {
  State* state = Get();
  if (!state) return;
  ++state->submit_flushes;
  if (pending_jobs) {
    ++state->submit_flushes_blocked;
    state->submit_flush_jobs += pending_jobs;
  }
  state->submit_flush_ticks += Now() - start;
}

inline void CountUpload(uint64_t bytes) {
  if (State* state = Get()) {
    ++state->counts[size_t(Count::kUploadRanges)];
    state->upload_bytes += bytes;
  }
}

inline void CountReadback(uint32_t bytes) {
  if (State* state = Get()) {
    ++state->readbacks;
    state->readback_bytes += bytes;
  }
}

// Draw packets since the last occlusion snapshot (diagnostics only):
// predicated away, dropped by viz kill, issued to the backend.
inline uint32_t segment_draws[3]{};

inline void Increment(Count counter) {
  if (State* state = Get()) ++state->counts[size_t(counter)];
}

inline uint64_t ZoneStart() { return Get() ? Now() : 0; }
// The detail zones of the draw (draw_setup and after, bindings_*, textures_*):
// only with REX_CP_SPLIT, because their timers cost about 0.5 ms a race frame.
inline uint64_t DetailZoneStart() { return SplitEnabled() ? Now() : 0; }
inline void ZoneEnd(Zone zone, uint64_t start) {
  if (State* state = Get(); state && start) state->zone_ticks[size_t(zone)] += Now() - start;
}
class ZoneScope {
 public:
  explicit ZoneScope(Zone zone) : zone_(zone), start_(ZoneStart()) {}
  ~ZoneScope() { ZoneEnd(zone_, start_); }

 private:
  Zone zone_;
  uint64_t start_;
};
class DetailZoneScope {
 public:
  explicit DetailZoneScope(Zone zone) : zone_(zone), start_(DetailZoneStart()) {}
  ~DetailZoneScope() { ZoneEnd(zone_, start_); }

 private:
  Zone zone_;
  uint64_t start_;
};

inline void AddOcclusionInterval(bool awaited, uint64_t passed_samples) {
  State* state = Get();
  if (!state) return;
  state->occlusion_samples[awaited] += double(passed_samples);
  ++state->occlusion_intervals[awaited];
}

// busy: sum of submission durations; resolve: sum of resolve durations; span:
// first to last GPU timestamp of the frame, including idle gaps.
inline void AddGpuFrame(double busy_ms, double resolve_ms, double span_ms, double load_ms = 0,
                        double readback_ms = 0, double resolve_full_ms = 0,
                        double load_full_ms = 0, double transfer_ms = 0,
                        double transfer_depth_ms = 0) {
  State* state = Get();
  if (!state) return;
  ++state->gpu_frames;
  state->gpu_busy_ms += busy_ms;
  state->gpu_resolve_ms += resolve_ms;
  state->gpu_span_ms += span_ms;
  state->gpu_load_ms += load_ms;
  state->gpu_readback_ms += readback_ms;
  state->gpu_resolve_full_ms += resolve_full_ms;
  state->gpu_load_full_ms += load_full_ms;
  state->gpu_transfer_ms += transfer_ms;
  state->gpu_transfer_depth_ms += transfer_depth_ms;
}

// The post pass groups of the perf trace's gpu passes_ms, in the order of the
// command processor's GpuTimestampKind::kPass... kinds.
inline constexpr const char* kGpuPasses[] = {"gtao", "taa", "motion", "windshield", "fsr", "other"};
inline constexpr size_t kGpuPassCount = sizeof(kGpuPasses) / sizeof(kGpuPasses[0]);
static_assert(kGpuPassCount + 1 == sizeof(State::gpu_pass_ms) / sizeof(double));

// ms: the GPU time of each post pass group, then of the swap's passes.
inline void AddGpuPasses(const double* ms) {
  State* state = Get();
  if (!state) return;
  for (size_t i = 0; i < kGpuPassCount + 1; ++i) state->gpu_pass_ms[i] += ms[i];
}

inline void AddGpuResolveBuckets(const double* ms, const uint32_t* counts) {
  State* state = Get();
  if (!state) return;
  for (size_t i = 0; i < 4; ++i) {
    state->gpu_resolve_bucket_ms[i] += ms[i];
    state->gpu_resolve_bucket_count[i] += counts[i];
  }
}

// The parts of the full-screen resolves (D3D12CommandProcessor::ResolvePart,
// d3d-hle-n6.md 19.7.1), the perf trace's gpu resolve_full_parts_ms.
inline constexpr const char* kResolveParts[] = {"transfers", "dump", "guest_layout",
                                                "host_textures", "clear"};
inline void AddGpuResolveParts(const double* ms) {
  State* state = Get();
  if (!state) return;
  for (size_t i = 0; i < 5; ++i) state->gpu_resolve_part_ms[i] += ms[i];
}

// The host textures resolves write from the render target, by class
// (D3D12CommandProcessor::BeginResolveHostWriteTimestamp, d3d-hle-n6.md
// 19.7.3), the perf trace's gpu resolve_host_writes: at the scene's scale or
// the whole scale, sampled or not since the texture's last write.
inline constexpr const char* kResolveHostWrites[] = {"scene_sampled", "scene_unsampled",
                                                     "whole_sampled", "whole_unsampled"};
inline void AddGpuResolveHostWrites(const double* ms, const uint32_t* counts) {
  State* state = Get();
  if (!state) return;
  for (size_t i = 0; i < 4; ++i) {
    state->gpu_host_write_ms[i] += ms[i];
    state->gpu_host_write_count[i] += counts[i];
  }
}

inline ResolveInventory* GetResolveInventory() {
  static ResolveInventory* inventory = []() -> ResolveInventory* {
    if (!Get()) return nullptr;
    auto* result = new ResolveInventory;
    result->path = std::string(std::getenv("REX_PERF_TRACE_FILE")) + ".resolves.json";
    return result;
  }();
  return inventory;
}

inline void CountResolve(uint32_t address, uint32_t length, uint32_t format, uint32_t pitch,
                         uint32_t height) {
  ResolveInventory* inventory = GetResolveInventory();
  if (!inventory || !length) return;
  const double seconds = double(Now() - Get()->origin) / 1e9;
  auto& entry = inventory->entries[{address, length, format, pitch, height}];
  if (!entry.count++) entry.first_s = seconds;
  entry.last_s = seconds;
}

inline void WriteResolveInventory(double seconds) {
  ResolveInventory* inventory = GetResolveInventory();
  if (!inventory) return;
  std::ofstream output(inventory->path, std::ios::trunc);
  output << "{\"seconds\":" << seconds << ",\"resolves\":[";
  bool first = true;
  for (const auto& [key, entry] : inventory->entries) {
    output << (first ? "" : ",") << "\n{\"address\":" << key[0] << ",\"length\":" << key[1]
           << ",\"format\":" << key[2] << ",\"pitch\":" << key[3] << ",\"height\":" << key[4]
           << ",\"count\":" << entry.count << ",\"first_s\":" << entry.first_s
           << ",\"last_s\":" << entry.last_s << "}";
    first = false;
  }
  output << "]}\n";
}

inline void OnSwap() {
  State* state = Get();
  if (!state) return;
  const uint64_t now = Now();
  // The draw path's shader work in this frame.
  const uint64_t frame_shader = frame_shader_ns.exchange(0, std::memory_order_relaxed);
  const uint64_t frame_mod_compile = frame_mod_compile_ns.exchange(0, std::memory_order_relaxed);
  const uint64_t frame_pass_compile = frame_pass_compile_ns.exchange(0, std::memory_order_relaxed);
  if (state->last_swap) {
    const uint64_t frame = now - state->last_swap;
    state->max_frame = frame > state->max_frame ? frame : state->max_frame;
    state->frame_ms.push_back(float(double(frame) / 1e6));
    if (double(frame) / 1e6 > kLongFrameMs) {
      state->long_frames.push_back(
          {float(double(frame) / 1e6), float(double(frame_shader) / 1e6),
           float(double(frame_mod_compile) / 1e6), float(double(frame_pass_compile) / 1e6)});
    }
  }
  state->last_swap = now;
  ++state->swaps;
  const uint64_t elapsed = now - state->window_start;
  // With ab_cvar, windows end on a grid of ab_window_ms and at each flip
  // (cvar_ab::OnSwap runs first), so each holds the frames of one side.
  const cvar_ab::Config* ab = cvar_ab::Get();
  if (ab) {
    const cvar_ab::Applied applied = cvar_ab::Current();
    if (now / ab->window_ns == state->window_start / ab->window_ns &&
        applied.side == state->ab_side && applied.period == state->ab_period) {
      return;
    }
  } else if (elapsed < state->frequency) {
    return;
  }
  const auto ms = [&](uint64_t ticks) { return double(ticks) * 1000.0 / double(state->frequency); };
  static const char* const kNames[] = {"guest_idle", "frame_latency", "resolve_readback",
                                       "occlusion", "memexport", "wait_reg_mem",
                                       "readback_publish", "other"};
  state->output << "{\"t\":" << ms(now - state->origin) / 1000.0
                << ",\"window_ms\":" << ms(elapsed);
  if (ab && state->ab_side >= 0) {
    // The side, its value, its period of the steady clock and the window's
    // start within that period.
    state->output << ",\"ab\":{\"side\":" << state->ab_side << ",\"value\":\""
                  << ab->values[state->ab_side] << "\",\"period\":" << state->ab_period
                  << ",\"offset_ms\":"
                  << double(state->window_start - state->ab_period * ab->period_ns) / 1e6 << '}';
  }
  state->output << ",\"fps\":" << double(state->swaps) * 1000.0 / ms(elapsed)
                << ",\"frame_max_ms\":" << ms(state->max_frame) << ",\"draws\":" << state->draws
                << ",\"copies\":" << state->copies << ",\"readbacks\":" << state->readbacks
                << ",\"readback_kb\":" << (state->readback_bytes >> 10)
                << ",\"upload_kb\":" << (state->upload_bytes >> 10)
                << ",\"invalidations\":" << invalidations.exchange(0, std::memory_order_relaxed)
                << ",\"invalidated_kb\":"
                << (invalidated_bytes.exchange(0, std::memory_order_relaxed) >> 10)
                << ",\"fetch_writes_unchanged\":" << fetch_writes_unchanged
                << ",\"binding_check\":{\"checks\":"
                << binding_checks.exchange(0, std::memory_order_relaxed) << ",\"mismatches\":"
                << binding_check_mismatches.exchange(0, std::memory_order_relaxed)
                << ",\"perturbed\":" << binding_check_perturbed.exchange(0, std::memory_order_relaxed)
                << ",\"textures\":" << binding_check_textures.exchange(0, std::memory_order_relaxed)
                << ",\"outdated\":" << binding_check_outdated.exchange(0, std::memory_order_relaxed)
                << ",\"outdated_racing\":"
                << binding_check_outdated_racing.exchange(0, std::memory_order_relaxed)
                << ",\"resets_perturbed\":"
                << binding_check_resets_perturbed.exchange(0, std::memory_order_relaxed) << '}'
                << ",\"native_rt_resolve\":{\"resolves\":"
                << native_resolves.exchange(0, std::memory_order_relaxed) << ",\"textures\":"
                << native_resolve_textures.exchange(0, std::memory_order_relaxed)
                << ",\"misses\":" << native_resolve_misses.exchange(0, std::memory_order_relaxed)
                << ",\"checks\":" << native_resolve_checks.exchange(0, std::memory_order_relaxed)
                << ",\"mismatches\":"
                << native_resolve_mismatches.exchange(0, std::memory_order_relaxed)
                << ",\"scene_created\":"
                << native_resolve_scene_created.exchange(0, std::memory_order_relaxed)
                << ",\"scene_unwritten\":"
                << native_resolve_scene_unwritten.exchange(0, std::memory_order_relaxed)
                << ",\"skips_load\":"
                << native_resolve_skips_load.exchange(0, std::memory_order_relaxed)
                << ",\"skips_sibling\":"
                << native_resolve_skips_sibling.exchange(0, std::memory_order_relaxed)
                << ",\"fallbacks\":"
                << native_resolve_fallbacks.exchange(0, std::memory_order_relaxed)
                << ",\"fallbacks_missing\":"
                << native_resolve_fallbacks_missing.exchange(0, std::memory_order_relaxed) << '}';
  {
    static const char* const kGuestLayoutNames[] = {
        "from_texture",     "from_target",     "deferred",         "superseded",
        "written_load",     "written_request", "written_readback", "written_resolve",
        "written_overflow", "written_probe",   "perturbed",        "cpu_writes",
        "invariant",        "foreign_thread",  "source_loads",     "gpu_writes"};
    static_assert(std::size(kGuestLayoutNames) == size_t(GuestLayout::kCount));
    state->output << ",\"guest_layout\":{";
    for (size_t i = 0; i < size_t(GuestLayout::kCount); ++i) {
      state->output << (i ? ",\"" : "\"") << kGuestLayoutNames[i]
                    << "\":" << guest_layout[i].exchange(0, std::memory_order_relaxed);
    }
    state->output << ",\"checks\":[";
    for (size_t i = 0; i < 4; ++i) {
      state->output << (i ? ",[" : "[")
                    << guest_layout_checks[i].exchange(0, std::memory_order_relaxed) << ','
                    << guest_layout_check_pixels[i].exchange(0, std::memory_order_relaxed) << ','
                    << guest_layout_check_changed[i].exchange(0, std::memory_order_relaxed) << ','
                    << guest_layout_check_bytes[i].exchange(0, std::memory_order_relaxed) << ','
                    << guest_layout_check_max_step[i].exchange(0, std::memory_order_relaxed)
                    << ']';
    }
    state->output << "]}";
  }
  state->output
                << ",\"scene_variants\":{\"translations\":"
                << scene_variant_translations.exchange(0, std::memory_order_relaxed)
                << ",\"pipelines\":"
                << scene_variant_pipelines.exchange(0, std::memory_order_relaxed) << '}'
                << ",\"host_resolve\":{\"found\":"
                << host_resolves_found.exchange(0, std::memory_order_relaxed) << ",\"done\":"
                << host_resolves_done.exchange(0, std::memory_order_relaxed) << ",\"other\":"
                << host_resolves_other.exchange(0, std::memory_order_relaxed) << ",\"checks\":"
                << host_resolve_checks.exchange(0, std::memory_order_relaxed)
                << ",\"mismatches\":"
                << host_resolve_mismatches.exchange(0, std::memory_order_relaxed)
                << ",\"check_formats\":[";
  for (size_t i = 0; i < 18; ++i) {
    state->output << (i ? "," : "")
                  << host_resolve_check_formats[i].exchange(0, std::memory_order_relaxed);
  }
  state->output << "]}"
                << ",\"rt_transfers\":{\"transfers\":"
                << stale_transfers.exchange(0, std::memory_order_relaxed) << ",\"tiles\":"
                << stale_transfer_tiles.exchange(0, std::memory_order_relaxed)
                << ",\"pending_tiles\":"
                << stale_pending_tiles.exchange(0, std::memory_order_relaxed)
                << ",\"dropped\":"
                << stale_tiles[0].exchange(0, std::memory_order_relaxed)
                << ",\"overwritten\":"
                << stale_tiles[1].exchange(0, std::memory_order_relaxed)
                << ",\"read_draw\":"
                << stale_tiles[2].exchange(0, std::memory_order_relaxed)
                << ",\"read_resolve\":"
                << stale_tiles[3].exchange(0, std::memory_order_relaxed)
                << ",\"transfers_unread\":"
                << stale_transfers_unread.exchange(0, std::memory_order_relaxed)
                << ",\"transfers_read\":"
                << stale_transfers_read.exchange(0, std::memory_order_relaxed) << '}'
                << ",\"lazy_transfers\":{\"copied\":"
                << lazy_tiles[0].exchange(0, std::memory_order_relaxed) << ",\"deferred\":"
                << lazy_tiles[1].exchange(0, std::memory_order_relaxed) << ",\"claimed_back\":"
                << lazy_tiles[2].exchange(0, std::memory_order_relaxed) << ",\"flushed_draw\":"
                << lazy_tiles[3].exchange(0, std::memory_order_relaxed)
                << ",\"flushed_resolve\":"
                << lazy_tiles[4].exchange(0, std::memory_order_relaxed) << ",\"flushed_other\":"
                << lazy_tiles[5].exchange(0, std::memory_order_relaxed) << ",\"violations\":"
                << lazy_tiles[6].exchange(0, std::memory_order_relaxed) << ",\"perturbed\":"
                << lazy_tiles[7].exchange(0, std::memory_order_relaxed) << ",\"redeferred\":"
                << lazy_tiles[8].exchange(0, std::memory_order_relaxed) << ",\"chain_copied\":"
                << lazy_tiles[9].exchange(0, std::memory_order_relaxed) << ",\"overwritten\":"
                << lazy_tiles[10].exchange(0, std::memory_order_relaxed) << ",\"flushed_clear\":"
                << lazy_tiles[11].exchange(0, std::memory_order_relaxed) << ",\"hazards\":"
                << lazy_tiles[12].exchange(0, std::memory_order_relaxed) << ",\"pending\":"
                << lazy_pending_tiles.load(std::memory_order_relaxed) << '}'
                << ",\"rt_memory\":{\"targets\":"
                << rt_targets.load(std::memory_order_relaxed) << ",\"mb\":"
                << double(rt_memory_bytes.load(std::memory_order_relaxed)) / 1048576.0
                << ",\"peak_mb\":"
                << double(rt_memory_peak_bytes.load(std::memory_order_relaxed)) / 1048576.0
                << ",\"resizes\":" << rt_resizes.exchange(0, std::memory_order_relaxed)
                << ",\"evictions\":" << rt_evictions.exchange(0, std::memory_order_relaxed)
                << '}' << ",\"one_pass\":{\"draws\":"
                << one_pass_draws.exchange(0, std::memory_order_relaxed) << ",\"resolves\":"
                << one_pass_resolves.exchange(0, std::memory_order_relaxed) << ",\"clears\":"
                << one_pass_clears.exchange(0, std::memory_order_relaxed) << ",\"violations\":"
                << one_pass_violations.exchange(0, std::memory_order_relaxed) << '}';
  // Shader work by kind: [count, ms, longest ms, count over 4 ms] (ShaderWork).
  state->output << ",\"shader_work\":{";
  for (size_t i = 0; i < size_t(ShaderWork::kCount); ++i) {
    ShaderWorkCounts& counts = shader_work[i];
    state->output << (i ? "," : "") << '"' << kShaderWorkNames[i] << "\":["
                  << counts.count.exchange(0, std::memory_order_relaxed) << ','
                  << double(counts.ns.exchange(0, std::memory_order_relaxed)) / 1e6 << ','
                  << double(counts.max_ns.exchange(0, std::memory_order_relaxed)) / 1e6 << ','
                  << counts.long_count.exchange(0, std::memory_order_relaxed) << ']';
  }
  state->output << ",\"mods_pending\":" << mods_pending.load(std::memory_order_relaxed) << '}';
  {
    const double tsc_per_ms = double(__rdtsc() - state->window_tsc) / ms(elapsed);
    const uint64_t replay_tsc = constant_replay.tsc.exchange(0, std::memory_order_relaxed);
    state->output << ",\"constants\":{\"fills\":" << draw_constants.fills
                  << ",\"slots\":" << draw_constants.fill_slots
                  << ",\"captured\":" << draw_constants.captured_slots
                  << ",\"samplers\":" << draw_constants.samplers
                  << ",\"descriptor_fills\":" << draw_constants.descriptor_fills
                  << ",\"delta_draws\":" << draw_constants.delta_draws
                  << ",\"delta_dwords\":" << draw_constants.delta_dwords
                  << ",\"delta_granules\":" << draw_constants.delta_granules
                  << ",\"replays\":"
                  << constant_replay.commands.exchange(0, std::memory_order_relaxed)
                  << ",\"replay_ms\":"
                  << (tsc_per_ms > 0 ? double(replay_tsc) / tsc_per_ms : 0.0)
                  << ",\"checks\":" << constant_replay.checks.exchange(0, std::memory_order_relaxed)
                  << ",\"mismatches\":"
                  << constant_replay.mismatches.exchange(0, std::memory_order_relaxed)
                  << ",\"perturbed\":"
                  << constant_replay.perturbed.exchange(0, std::memory_order_relaxed) << '}';
    draw_constants = DrawConstantCounts{};
    const uint64_t back_end_tsc = back_end_replay.tsc.exchange(0, std::memory_order_relaxed);
    state->output << ",\"back_end\":{\"captures\":" << back_end_capture.captures
                  << ",\"bytes\":" << back_end_capture.bytes
                  << ",\"context_dwords\":" << back_end_capture.context_dwords
                  << ",\"written_dwords\":" << back_end_capture.written_dwords
                  << ",\"fetch_slots\":" << back_end_capture.fetch_slots
                  << ",\"group_bytes\":" << back_end_capture.group_bytes << ",\"capture_ms\":"
                  << (tsc_per_ms > 0 ? double(back_end_capture.tsc) / tsc_per_ms : 0.0)
                  << ",\"replays\":"
                  << back_end_replay.replays.exchange(0, std::memory_order_relaxed)
                  << ",\"replay_ms\":"
                  << (tsc_per_ms > 0 ? double(back_end_tsc) / tsc_per_ms : 0.0)
                  << ",\"checks\":" << back_end_replay.checks.exchange(0, std::memory_order_relaxed)
                  << ",\"perturbed\":"
                  << back_end_replay.perturbed.exchange(0, std::memory_order_relaxed)
                  << ",\"pipelines\":"
                  << back_end_replay.pipelines.exchange(0, std::memory_order_relaxed)
                  << ",\"pipeline_ms\":"
                  << (tsc_per_ms > 0 ? double(back_end_replay.pipeline_tsc.exchange(
                                           0, std::memory_order_relaxed)) /
                                           tsc_per_ms
                                     : 0.0)
                  << ",\"dropped\":"
                  << back_end_replay.dropped.exchange(0, std::memory_order_relaxed)
                  << ",\"mismatches\":[";
    for (size_t i = 0; i < 7; ++i) {
      state->output << (i ? "," : "")
                    << back_end_replay.mismatches[i].exchange(0, std::memory_order_relaxed);
    }
    state->output << "]}";
    back_end_capture = BackEndCaptureCounts{};
    if (render_target_guard.draws) {
      state->output << ",\"rt_update_guard\":{\"draws\":" << render_target_guard.draws
                    << ",\"skips\":" << render_target_guard.skips
                    << ",\"checks\":" << render_target_guard.checks
                    << ",\"perturbed\":" << render_target_guard.perturbed
                    << ",\"mismatches\":" << render_target_guard.mismatches << '}';
      render_target_guard = RenderTargetGuardCounts{};
    }
    if (TextureKeysEnabled()) {
      state->output << ",\"texture_keys\":{\"recomputes\":" << texture_keys.recomputes
                    << ",\"same_dwords\":" << texture_keys.same_dwords
                    << ",\"same_binding\":" << texture_keys.same_binding
                    << ",\"key_changes\":" << texture_keys.key_changes
                    << ",\"recent_keys\":" << texture_keys.recent_keys;
      static const char* const kCauseNames[kBindingResetCauses] = {"frame", "gpu", "cpu", "both",
                                                                   "destroyed"};
      state->output << ",\"resets\":{";
      for (size_t i = 0; i < kBindingResetCauses; ++i) {
        const BindingResetCounts& r = texture_keys.resets[i];
        state->output << (i ? ",\"" : "\"") << kCauseNames[i] << "\":[" << r.resets << ','
                      << r.bindings << ',' << r.keys << ',' << r.same_textures << ',' << r.loads
                      << ',' << r.kept << ',' << r.outdated << ']';
      }
      state->output << "}}";
      texture_keys = TextureKeyCounts{};
    }
    if (CaptureSplitEnabled()) {
      state->output << ",\"capture_split\":{\"ms\":[";
      for (size_t i = 0; i < kCaptureSplitParts; ++i) {
        state->output << (i ? "," : "")
                      << (tsc_per_ms > 0 ? double(capture_split.tsc[i]) / tsc_per_ms : 0.0);
      }
      state->output << "],\"float_slots\":" << capture_split.float_slots
                    << ",\"fetch_slots\":" << capture_split.fetch_slots
                    << ",\"bool_loop_slots\":" << capture_split.bool_loop_slots << '}';
      capture_split = CaptureSplitCounts{};
    }
    if (ReplaySplitEnabled()) {
      const auto tsc_ms = [&](uint64_t ticks) {
        return tsc_per_ms > 0 ? double(ticks) / tsc_per_ms : 0.0;
      };
      state->output << ",\"replay_split\":{\"executes\":"
                    << replay_split.executes.exchange(0, std::memory_order_relaxed)
                    << ",\"execute_ms\":"
                    << tsc_ms(replay_split.execute_tsc.exchange(0, std::memory_order_relaxed))
                    << ",\"ms\":[";
      for (size_t i = 0; i < kReplaySplitKinds; ++i) {
        state->output << (i ? "," : "")
                      << tsc_ms(replay_split.tsc[i].exchange(0, std::memory_order_relaxed));
      }
      state->output << "],\"counts\":[";
      for (size_t i = 0; i < kReplaySplitKinds; ++i) {
        state->output << (i ? "," : "")
                      << replay_split.counts[i].exchange(0, std::memory_order_relaxed);
      }
      state->output << "]}";
    }
  }
  state->output << ",\"vblanks\":{\"grid\":" << vblanks[0].exchange(0, std::memory_order_relaxed)
                << ",\"late\":" << vblanks[1].exchange(0, std::memory_order_relaxed)
                << ",\"forced\":" << vblanks[2].exchange(0, std::memory_order_relaxed) << '}';
  if (ProcessCpuEnabled()) WriteProcessCpu(state->output);
  // The host presents: new guest frames, the same frame again, and those of the UI
  // thread (d3d-hle-n4.md 17.22).
  {
    const rex::ui::PresentStats presents = rex::ui::TakePresentStats();
    state->output << ",\"host_presents\":{\"new_frames\":" << presents.new_frames
                  << ",\"repeats\":" << presents.repeats << ",\"ui_thread\":" << presents.ui_thread
                  << ",\"under_8_33_ms\":" << presents.under_8_33_ms
                  << ",\"under_4_17_ms\":" << presents.under_4_17_ms << ",\"paint_gpu_ms\":"
                  << (presents.paint_gpu_paints ? presents.paint_gpu_ms / double(presents.paint_gpu_paints)
                                                : 0.0)
                  << '}';
  }
  // The title's ring index waits that slept (guest_ring_wait_sleep, d3d-hle-n4.md 17.16).
  {
    const rex::system::gpu_progress::WaitStats ring = rex::system::gpu_progress::TakeWaitStats();
    state->output << ",\"guest_ring_waits\":{\"waits\":" << ring.waits
                  << ",\"timeouts\":" << ring.timeouts << ",\"ms\":" << ring.ns / 1000000.0 << '}';
  }
  state->output << ",\"wait_ms\":{";
  for (size_t i = 0; i < size_t(Wait::kCount); ++i) {
    state->output << (i ? "," : "") << '"' << kNames[i] << "\":" << ms(state->wait_ticks[i]);
  }
  state->output << "},\"waits\":{";
  for (size_t i = 0; i < size_t(Wait::kCount); ++i) {
    state->output << (i ? "," : "") << '"' << kNames[i] << "\":" << state->waits[i];
  }
  static const char* const kCountNames[] = {"occlusion_awaited", "occlusion_unawaited",
                                            "viz_queries",       "viz_draws",
                                            "bin_selects",       "predicated_run",
                                            "predicated_skipped", "pipeline_draws_waited",
                                            "pipeline_draws_dropped", "submissions",
                                            "upload_ranges"};
  state->output << "},\"counts\":{";
  for (size_t i = 0; i < size_t(Count::kCount); ++i) {
    state->output << (i ? "," : "") << '"' << kCountNames[i] << "\":" << state->counts[i];
  }
  static const char* const kZoneNames[] = {"draw",          "draw_primitives",
                                           "draw_render_targets", "draw_pipeline",
                                           "draw_textures", "draw_bindings",
                                           "draw_vertex_buffers", "copy",
                                           "submit",        "swap",
                                           "shared_memory_lock", "shared_memory_upload",
                                           "submit_record", "submit_execute",
                                           "shared_memory_protect", "shared_memory_copy",
                                           "draw_setup", "draw_state", "draw_call",
                                           "draw_pre_targets", "draw_pre_pipeline",
                                           "draw_pre_textures", "bindings_root",
                                           "bindings_constants", "bindings_descriptors",
                                           "bindings_root_parameters", "textures_keys",
                                           "textures_loads", "textures_bindings"};
  state->output << "},\"cpu_ms\":{";
  for (size_t i = 0; i < size_t(Zone::kCount); ++i) {
    state->output << (i ? "," : "") << '"' << kZoneNames[i] << "\":" << ms(state->zone_ticks[i]);
  }
  // Mean passing samples per occlusion interval.
  const auto mean_samples = [&](size_t i) {
    return state->occlusion_intervals[i] ? state->occlusion_samples[i] / state->occlusion_intervals[i]
                                         : 0.0;
  };
  state->output << "},\"occlusion_samples\":{\"unawaited\":" << mean_samples(0)
                << ",\"awaited\":" << mean_samples(1);
  // Means per GPU frame, from timestamps read a few frames late.
  const double gpu_frames = state->gpu_frames ? double(state->gpu_frames) : 1.0;
  state->output << "},\"gpu\":{\"frames\":" << state->gpu_frames
                << ",\"frame_ms\":" << state->gpu_busy_ms / gpu_frames
                << ",\"resolve_ms\":" << state->gpu_resolve_ms / gpu_frames
                << ",\"span_ms\":" << state->gpu_span_ms / gpu_frames
                << ",\"load_ms\":" << state->gpu_load_ms / gpu_frames
                << ",\"readback_ms\":" << state->gpu_readback_ms / gpu_frames
                << ",\"resolve_full_ms\":" << state->gpu_resolve_full_ms / gpu_frames
                << ",\"load_full_ms\":" << state->gpu_load_full_ms / gpu_frames
                << ",\"transfer_ms\":" << state->gpu_transfer_ms / gpu_frames
                << ",\"transfer_depth_ms\":" << state->gpu_transfer_depth_ms / gpu_frames
                << ",\"resolve_buckets\":{";
  {
    static const char* const kBuckets[] = {"full", "mid", "small", "clear"};
    for (size_t i = 0; i < 4; ++i) {
      state->output << (i ? "," : "") << '"' << kBuckets[i] << "\":["
                    << state->gpu_resolve_bucket_ms[i] / gpu_frames << ','
                    << double(state->gpu_resolve_bucket_count[i]) / gpu_frames << ']';
    }
  }
  state->output << "},\"resolve_full_parts_ms\":{";
  for (size_t i = 0; i < 5; ++i) {
    state->output << (i ? "," : "") << '"' << kResolveParts[i]
                  << "\":" << state->gpu_resolve_part_ms[i] / gpu_frames;
  }
  state->output << "},\"resolve_host_writes\":{";
  for (size_t i = 0; i < 4; ++i) {
    state->output << (i ? "," : "") << '"' << kResolveHostWrites[i] << "\":["
                  << state->gpu_host_write_ms[i] / gpu_frames << ','
                  << double(state->gpu_host_write_count[i]) / gpu_frames << ']';
  }
  state->output << "},\"passes_ms\":{";
  {
    // The post passes by group, the swap's passes, and the rest of the frame's
    // GPU time: the guest's draws and resolves with its own post-processing,
    // which the look mods change in place (d3d-hle-n4.md 17.24).
    double passes = 0;
    for (size_t i = 0; i < kGpuPassCount; ++i) {
      state->output << (i ? "," : "") << '"' << kGpuPasses[i]
                    << "\":" << state->gpu_pass_ms[i] / gpu_frames;
      passes += state->gpu_pass_ms[i];
    }
    state->output << "},\"swap_ms\":" << state->gpu_pass_ms[kGpuPassCount] / gpu_frames
                  << ",\"scene_ms\":"
                  << std::max(0.0, state->gpu_busy_ms - passes - state->gpu_pass_ms[kGpuPassCount]) /
                         gpu_frames;
  }
  const auto ns_ms = [](uint64_t ns) { return double(ns) / 1e6; };
  state->output << "},\"submit_thread\":{\"jobs\":"
                << submit_thread_jobs.exchange(0, std::memory_order_relaxed) << ",\"busy_ms\":"
                << ns_ms(submit_thread_ns[0].exchange(0, std::memory_order_relaxed))
                << ",\"record_ms\":"
                << ns_ms(submit_thread_ns[1].exchange(0, std::memory_order_relaxed))
                << ",\"execute_ms\":"
                << ns_ms(submit_thread_ns[2].exchange(0, std::memory_order_relaxed))
                << ",\"callbacks\":"
                << submit_thread_callbacks.exchange(0, std::memory_order_relaxed)
                << ",\"callback_ms\":"
                << ns_ms(submit_thread_ns[3].exchange(0, std::memory_order_relaxed))
                << ",\"flushes\":" << state->submit_flushes
                << ",\"flushes_blocked\":" << state->submit_flushes_blocked
                << ",\"flush_jobs\":" << state->submit_flush_jobs
                << ",\"flush_wait_ms\":" << ms(state->submit_flush_ticks);
  if (SplitEnabled()) {
    const uint64_t tsc = __rdtsc() - state->window_tsc;
    const double tsc_per_ms = double(tsc) / ms(elapsed);
    static const char* const kSplitNames[] = {"registers", "shaders", "draw", "other"};
    state->output << "},\"cp_split_ms\":{";
    for (size_t i = 0; i < size_t(Split::kCount); ++i) {
      state->output << (i ? "," : "") << '"' << kSplitNames[i]
                    << "\":" << (tsc_per_ms > 0 ? double(state->split_tsc[i]) / tsc_per_ms : 0.0);
    }
    state->output << "},\"cp_split_packets\":{";
    for (size_t i = 0; i < size_t(Split::kCount); ++i) {
      state->output << (i ? "," : "") << '"' << kSplitNames[i] << "\":" << state->split_packets[i];
    }
    state->output << "},\"replay_split_ms\":{";
    for (size_t i = 0; i < size_t(Split::kCount); ++i) {
      state->output << (i ? "," : "") << '"' << kSplitNames[i] << "\":"
                    << (tsc_per_ms > 0 ? double(state->replay_split_tsc[i]) / tsc_per_ms : 0.0);
    }
    state->output << "},\"replay\":{\"ms\":"
                  << (tsc_per_ms > 0 ? double(state->replay_tsc) / tsc_per_ms : 0.0)
                  << ",\"count\":" << state->replays << ",\"dwords\":" << state->replay_dwords;
  }
  if (host_render.load(std::memory_order_relaxed)) {
    uint64_t wait_ticks = 0;
    for (uint64_t ticks : state->wait_ticks) wait_ticks += ticks;
    // The top-level names are the backend-neutral ones of e2e.py.
    state->output << "},\"render_thread_busy_ms\":"
                  << ms(elapsed > wait_ticks ? elapsed - wait_ticks : 0) << ",\"queue_depth_mean\":"
                  << (state->host_records ? double(state->host_depth_sum) / state->host_records : 0.0)
                  << ",\"queue_depth_max\":" << state->host_depth_max;
    // The records' wait in the queue: mean, median, p99 (the upper edge of
    // their 0.25 ms bin) and maximum.
    {
      auto quantile = [state](double fraction) {
        const uint64_t target = uint64_t(fraction * double(state->host_waited));
        uint64_t seen = 0;
        for (uint32_t i = 0; i < State::kHostWaitBins; ++i) {
          seen += state->host_wait_bins[i];
          if (seen > target) return (i + 1) * 0.25;
        }
        return State::kHostWaitBins * 0.25;
      };
      state->output << ",\"queue_wait_ms\":{\"mean\":"
                    << (state->host_waited ? state->host_wait_sum / 1e6 / state->host_waited : 0.0)
                    << ",\"p50\":" << (state->host_waited ? quantile(0.5) : 0.0)
                    << ",\"p99\":" << (state->host_waited ? quantile(0.99) : 0.0)
                    << ",\"max\":" << state->host_wait_max / 1e6 << '}';
    }
    state->output << ",\"host_render\":{\"records\":" << state->host_records
                  << ",\"merged_spans\":" << host_merged_spans.exchange(0, std::memory_order_relaxed);
    if (host_records.load(std::memory_order_relaxed)) {
      state->output << ",\"records_applied\":" << host_records_applied
                    << ",\"record_registers\":" << host_record_registers
                    << ",\"markers_missing\":"
                    << host_markers_missing.exchange(0, std::memory_order_relaxed);
      state->output << ",\"records_empty\":" << host_records_empty;
      host_records_applied = host_record_registers = host_records_empty = 0;
    }
    if (host_oplists.load(std::memory_order_relaxed)) {
      state->output << ",\"oplist_replays\":" << host_oplist_replays
                    << ",\"oplists_decoded\":" << host_oplists_decoded
                    << ",\"oplists_invalidated\":"
                    << host_oplists_invalidated.exchange(0, std::memory_order_relaxed)
                    << ",\"oplists_stale\":"
                    << host_oplists_stale.exchange(0, std::memory_order_relaxed);
      host_oplist_replays = host_oplists_decoded = 0;
    }
    if (host_check.load(std::memory_order_relaxed)) {
      state->output << ",\"checks\":{";
      bool first_writer = true;
      for (const auto& [writer, count] : host_checks) {
        char name[16];
        std::snprintf(name, sizeof(name), "%08X", writer);
        state->output << (first_writer ? "" : ",") << '"' << name << "\":" << count;
        first_writer = false;
      }
      host_checks.clear();
      static const char* const kReasonNames[] = {"bytes", "registers", "type3", "draws"};
      state->output << "},\"check_mismatches\":"
                    << host_check_mismatches.exchange(0, std::memory_order_relaxed)
                    << ",\"check_reasons\":{";
      for (size_t i = 0; i < size_t(HostCheckReason::kCount); ++i) {
        state->output << (i ? "," : "") << '"' << kReasonNames[i]
                      << "\":" << host_check_reasons[i];
        host_check_reasons[i] = 0;
      }
      state->output << "},\"check_registers\":{";
      bool first_register = true;
      for (const auto& [index, count] : host_check_registers) {
        char name[8];
        std::snprintf(name, sizeof(name), "%04X", index);
        state->output << (first_register ? "" : ",") << '"' << name << "\":" << count;
        first_register = false;
      }
      host_check_registers.clear();
      state->output << "},\"check_overwritten\":"
                    << host_check_overwritten.exchange(0, std::memory_order_relaxed)
                    << ",\"check_incomplete\":"
                    << host_check_incomplete.exchange(0, std::memory_order_relaxed)
                    << ",\"check_dropped\":"
                    << host_check_dropped.exchange(0, std::memory_order_relaxed)
                    << ",\"check_perturbed\":"
                    << host_check_perturbed.exchange(0, std::memory_order_relaxed);
    }
  }
  read_trace::WriteWindow(state->output, ms(now - state->origin) / 1000.0);
  // The frames over kLongFrameMs: [ms, shader work ms, of it the look mods' and
  // the post passes' FXC ms].
  state->output << "},\"long_frames\":[";
  for (size_t i = 0; i < state->long_frames.size(); ++i) {
    const auto& frame = state->long_frames[i];
    state->output << (i ? "," : "") << '[' << frame[0] << ',' << frame[1] << ',' << frame[2]
                  << ',' << frame[3] << ']';
  }
  // Every frame duration in the window, for run-wide percentiles and hitches.
  state->output << "],\"frame_ms\":[";
  for (size_t i = 0; i < state->frame_ms.size(); ++i) {
    state->output << (i ? "," : "") << state->frame_ms[i];
  }
  state->output << "]}\n";
  state->output.flush();
  if (ResolveInventory* inventory = GetResolveInventory(); inventory && ++inventory->windows % 10 == 0) {
    WriteResolveInventory(ms(now - state->origin) / 1000.0);
  }
  fetch_writes_unchanged = 0;
  const Wait cause = state->cause;
  std::ofstream output = std::move(state->output);
  std::vector<float> frame_ms = std::move(state->frame_ms);
  frame_ms.clear();
  const uint64_t frequency = state->frequency, origin = state->origin;
  *state = State{};
  state->output = std::move(output);
  state->frame_ms = std::move(frame_ms);
  state->cause = cause;
  state->frequency = frequency;
  state->origin = origin;
  state->window_start = state->last_swap = now;
  state->window_tsc = __rdtsc();
  if (ab) {
    const cvar_ab::Applied applied = cvar_ab::Current();
    state->ab_side = applied.side;
    state->ab_period = applied.period;
  }
}

}  // namespace rex::graphics::perf_trace
