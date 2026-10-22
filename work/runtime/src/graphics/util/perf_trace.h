#pragma once
// Optional per-second summary of where the GPU command processor thread spends
// its time, written as JSON lines to REX_PERF_TRACE_FILE. Diagnostic only: when
// the variable is unset every helper returns immediately.

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
};

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
// The binding check (d3d12_reuse_draw_bindings_check): bindings kept in sync
// compared with fresh keys, mismatches, comparisons changed on purpose.
inline std::atomic<uint64_t> binding_checks{0}, binding_check_mismatches{0},
    binding_check_perturbed{0};
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
inline void CountHostRecord(uint64_t depth) {
  State* state = Get();
  if (!state) return;
  ++state->host_records;
  state->host_depth_sum += depth;
  state->host_depth_max = depth > state->host_depth_max ? depth : state->host_depth_max;
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
                        double readback_ms = 0) {
  State* state = Get();
  if (!state) return;
  ++state->gpu_frames;
  state->gpu_busy_ms += busy_ms;
  state->gpu_resolve_ms += resolve_ms;
  state->gpu_span_ms += span_ms;
  state->gpu_load_ms += load_ms;
  state->gpu_readback_ms += readback_ms;
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
  if (state->last_swap) {
    const uint64_t frame = now - state->last_swap;
    state->max_frame = frame > state->max_frame ? frame : state->max_frame;
    state->frame_ms.push_back(float(double(frame) / 1e6));
  }
  state->last_swap = now;
  ++state->swaps;
  const uint64_t elapsed = now - state->window_start;
  if (elapsed < state->frequency) return;
  const auto ms = [&](uint64_t ticks) { return double(ticks) * 1000.0 / double(state->frequency); };
  static const char* const kNames[] = {"guest_idle", "frame_latency", "resolve_readback",
                                       "occlusion", "memexport", "wait_reg_mem",
                                       "readback_publish", "other"};
  state->output << "{\"t\":" << ms(now - state->origin) / 1000.0
                << ",\"window_ms\":" << ms(elapsed)
                << ",\"fps\":" << double(state->swaps) * 1000.0 / ms(elapsed)
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
                << '}'
                << ",\"vblanks\":{\"grid\":" << vblanks[0].exchange(0, std::memory_order_relaxed)
                << ",\"late\":" << vblanks[1].exchange(0, std::memory_order_relaxed)
                << ",\"forced\":" << vblanks[2].exchange(0, std::memory_order_relaxed) << '}'
                << ",\"wait_ms\":{";
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
                << ",\"readback_ms\":" << state->gpu_readback_ms / gpu_frames;
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
                  << ",\"queue_depth_max\":" << state->host_depth_max
                  << ",\"host_render\":{\"records\":" << state->host_records
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
  // Every frame duration in the window, for run-wide percentiles and hitches.
  state->output << "},\"frame_ms\":[";
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
}

}  // namespace rex::graphics::perf_trace
