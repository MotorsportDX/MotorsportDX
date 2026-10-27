/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <map>
#include <cmath>
#include <set>
#include <tuple>
#include <vector>
#include <sstream>
#include <utility>

#include "../util/perf_trace.h"
#include "../util/pm4_stats.h"

#include <rex/assert.h>
#include <rex/cvar.h>
#include <rex/dbg.h>
#include <rex/perf/counter.h>
#include <rex/graphics/d3d12/command_processor.h>
#include <rex/graphics/d3d12/fsr.h>
#include <rex/graphics/d3d12/graphics_system.h>
#include <rex/graphics/d3d12/post_pass.h>
#include <rex/graphics/d3d12/post_pass_jitter.h>
#include <rex/graphics/d3d12/shader.h>
#include <rex/graphics/flags.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/util/draw.h>
#include <rex/graphics/util/draw_scale.h>
#include <rex/graphics/pipeline/shader/interpreter.h>
#include <rex/graphics/xenos.h>
#include <rex/hash.h>
#include <rex/kernel/xboxkrnl/video.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/ui/d3d12/d3d12_presenter.h>
#include <rex/ui/d3d12/d3d12_util.h>

REXCVAR_DEFINE_BOOL(d3d12_bindless, true, "GPU/D3D12", "Use bindless resources where available")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DECLARE(bool, host_render_native);
REXCVAR_DECLARE(int32_t, native_rt_surfaces);
REXCVAR_DECLARE(int32_t, native_one_pass_msaa);
REXCVAR_DEFINE_STRING(native_draw_scale, "", "GPU",
                      "The scene's draw resolution scale on the native path as a fraction, such "
                      "as 3/2 for 1920x1080 (docs/analysis/d3d-hle-n5.md); its whole part must "
                      "be draw_resolution_scale_x and _y, the scale of the other surfaces. "
                      "Empty: the scene at the whole scale. RTV path with host_render_native only")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(native_scene_input_scale, "", "GPU",
                      "The scale the scene renders at before the upscaler on the native path "
                      "(docs/analysis/fsr-plan.md 11), such as 4/3 for 960 lines: the MSAA scene "
                      "surfaces are at it, the one-sample ones at native_draw_scale. From 1 to "
                      "native_draw_scale; its whole part must be draw_resolution_scale_x and _y. "
                      "Needs native_draw_scale and native_rt_surfaces bit 16. Empty: "
                      "native_draw_scale")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_INT32(native_logical_width, 1280, "GPU",
                     "The scene's width in guest pixels at 720 lines on the native path: above "
                     "1280 the scene draws widen their viewports and show more on each side "
                     "(Hor+, docs/analysis/d3d-hle-n5.md 18.21); needs the scene draws' path "
                     "(a fractional native_draw_scale or native_draw_scale_variant)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_INT32(native_logical_height, 720, "GPU",
                     "The scene's height in guest pixels at 1280 wide on the native path: above "
                     "720 the scene draws heighten their viewports and show more above and "
                     "below (Vert+, docs/analysis/d3d-hle-n5.md 18.22); needs the scene draws' "
                     "path, as native_logical_width")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(native_draw_scale_variant, false, "GPU",
                    "Diagnostic: draws into the private surfaces use the scene draws' path "
                    "(the scene_scale shader variant, the rational viewport and scissor) also "
                    "at a whole native_draw_scale, to test it against the whole path "
                    "(docs/analysis/d3d-hle-n5.md 18.13)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(native_scene_read_trace, false, "GPU",
                    "Diagnostic: log each shader fetch from a scene-scaled texture once (the "
                    "shader, the fetch constant, the opcode, unnormalized coordinates, texel "
                    "offsets, the draw's scale class) and each vertex fetch from a scene-resolved "
                    "range, for the upscaling audit (docs/analysis/fsr-plan.md 11)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(d3d12_reuse_draw_bindings_check, false, "GPU/D3D12",
                    "Compare every texture binding kept in sync with a key computed fresh from "
                    "its fetch constant (the check of d3d12_reuse_draw_bindings)");
REXCVAR_DEFINE_INT32(d3d12_reuse_draw_bindings_check_perturb, 0, "GPU/D3D12",
                     "Test of d3d12_reuse_draw_bindings_check: change every Nth fresh key so "
                     "that it must be reported (0: off)")
    .range(0, 1000000);
REXCVAR_DEFINE_BOOL(render_target_update_guard, false, "GPU/D3D12",
                    "Skip the render target cache's update at a draw whose render target "
                    "registers, draw extent registers and cache state are those of the last "
                    "update (host render targets; N4 of docs/analysis/d3d-hle-n4.md, section 17.13)");
REXCVAR_DEFINE_BOOL(render_target_update_guard_check, false, "GPU/D3D12",
                    "With render_target_update_guard: update anyway at every draw that would "
                    "skip, and check that the update changed nothing");
REXCVAR_DEFINE_INT32(render_target_update_guard_check_perturb, 0, "GPU/D3D12",
                     "Test of render_target_update_guard_check: change every Nth verdict so that "
                     "it must be reported (0: off)")
    .range(0, 1000000);
REXCVAR_DEFINE_BOOL(d3d12_reuse_draw_bindings, false, "GPU/D3D12",
                    "Keep the texture bindings, the fetch constant buffer and the vertex buffer "
                    "residency of fetch constants rewritten with their previous values (N2 step 2 "
                    "of docs/analysis/d3d-hle.md, section 15.11)");

REXCVAR_DEFINE_BOOL(d3d12_readback_memexport, false, "GPU/D3D12",
                    "Read data written by memory export in shaders on the CPU")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(d3d12_readback_resolve, false, "GPU/D3D12",
                    "Read render-to-texture results on the CPU")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(d3d12_submit_on_primary_buffer_end, true, "GPU/D3D12",
                    "Submit command list when PM4 primary buffer ends")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_INT32(rov_occlusion_awaited_report_divisor, 1, "GPU/D3D12",
                     "Divide the ROV occlusion samples counted between an awaited END "
                     "report and the report before it. Experiment: 2 turns FM4's sun "
                     "visibility samples at 2x MSAA into pixels; 1 keeps the counted samples")
    .range(1, 16)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_STRING(rov_occlusion_async, "off", "GPU/D3D12",
                      "ROV occlusion reports without waiting for the GPU.\n"
                      " off: every report waits for the GPU (accurate, stalls)\n"
                      " unawaited: reports the guest does not spin on are written when their\n"
                      "   copy completes; awaited reports still wait\n"
                      " all: every report is written when its copy completes, in order\n"
                      " previous: every report is written immediately, each interval using\n"
                      "   the samples of the same interval in the previous frame")
    .allowed({"off", "unawaited", "all", "previous"})
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(rtv_occlusion_snapshots, true, "GPU/D3D12",
                    "RTV path: treat EVENT_WRITE_ZPD as cumulative snapshots measured with "
                    "host occlusion queries, like the ROV path. Off uses begin/end pairs at "
                    "one address, which titles writing QueryBatch-style reports break")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(rtv_occlusion_debug, "", "GPU/D3D12",
                      "Diagnostics for RTV snapshots: noqueries (report 0 samples without host "
                      "queries), fake (host queries, but report N samples for awaited intervals; "
                      "N from query_occlusion_fake_sample_count)");
REXCVAR_DEFINE_DOUBLE(rtv_occlusion_awaited_scale, -1.0, "GPU/D3D12",
                      "RTV path: 0 or more reports the samples of awaited intervals (FM4 "
                      "measures its sun visibility this way) as guest pixels, the same at every "
                      "MSAA level, times this scale. Below 0 keeps the counted host samples")
    .range(-1.0, 16.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(rtv_occlusion_awaited_limit, 0.0, "GPU/D3D12",
                      "RTV path, with rtv_occlusion_awaited_scale: soft limit of the scaled "
                      "awaited pixels. Up to a third of it they pass unchanged, above that they "
                      "approach it without reaching it. 0: no limit")
    .range(0.0, 1000000.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(rtv_uniform_draws, true, "GPU/D3D12",
                    "RTV path: for screen-space rectangles writing the same values to every "
                    "sample (clears), skip the ownership transfers of what they overwrite, and "
                    "draw 4x MSAA clears at half the pitch into the render targets their EDRAM "
                    "is next used with")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(d3d12_submit_thread, false, "GPU/D3D12",
                    "Replay and execute command lists on a separate thread, taking the "
                    "D3D12 recording cost off the command processor thread")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(d3d12_submit_thread_async_swap, false, "GPU/D3D12",
                    "With d3d12_submit_thread: submit the swap through the submit thread too, "
                    "and signal the presenter and present the frame on that thread after it, "
                    "instead of waiting for the thread to drain at every swap")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_INT32(d3d12_submit_draw_interval, 0, "GPU/D3D12",
                     "With d3d12_submit_thread: end the submission before a draw once it holds "
                     "this many draws, so the submit thread replays the frame while it is "
                     "recorded and the swap waits for a short tail only (0: off)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(d3d12_readback_publish_thread, false, "GPU/D3D12",
                    "Copy resolve readbacks (readback_resolve=fast/some) into guest memory on a "
                    "separate thread, in order. The first data of a resolve destination is "
                    "still copied before the command processor continues")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(d3d12_readback_publish_await_signals, false, "GPU/D3D12",
                    "With d3d12_readback_publish_thread, wait for the queued copies before "
                    "anything the guest can observe (fence writes, interrupts, the swap). The "
                    "published data is from an earlier completed submission either way, so this "
                    "only changes timing; it costs about the copy time")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(readback_resolve_on_demand, false, "GPU/D3D12",
                    "Resolves skip their readback and trap their guest pages instead; the first "
                    "access by the CPU waits for the readback of the latest resolve "
                    "(docs/analysis/d3d-hle.md 16.5). Not with the ROV path or "
                    "readback_resolve=none")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_INT32(async_shader_draw_wait_us, 0, "GPU/D3D12",
                     "With async_shader_compilation, how long a draw waits for its pipeline "
                     "before it is dropped, in microseconds. 0 drops it immediately")
    .range(0, 100000)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace rex::graphics::d3d12 {

namespace {

// The command processor's side of the readback on demand: an access that
// faults asks for the service at the next packet or wait.
class DemandServicer : public read_trace::Servicer {
 public:
  explicit DemandServicer(CommandProcessor& processor) : processor_(processor) {}
  void RequestReadbackService() override { processor_.RequestHostService(); }

 private:
  CommandProcessor& processor_;
};
// Optional bounded publication timeline. Sampling is diagnostic only: a zero
// sample does not prove that the entire resolved image is zero.
// Optional trace of resolve readbacks that kept guest data
// (REX_READBACK_SKIP_TRACE=<file>): "<steady ms> <address> <bytes> <pages
// written by the guest since the resolve, not overwritten>".
void TraceReadbackSkip(uint32_t address, uint32_t length, uint32_t skipped) {
  static std::ofstream output = [] {
    const char* path = std::getenv("REX_READBACK_SKIP_TRACE");
    return path && *path ? std::ofstream(path) : std::ofstream();
  }();
  if (!skipped || !output.is_open())
    return;
  output << std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count()
         << ' ' << std::hex << address << std::dec << ' ' << length << ' ' << skipped << std::endl;
}
// Optional dump of resolve readbacks into guest memory (REX_READBACK_DUMP=<directory>,
// REX_READBACK_DUMP_ADDRESS=<guest address, hexadecimal>, every
// REX_READBACK_DUMP_EVERY-th readback of that address, default 120): files
// <address>_<sequence>.bin with a header line "RBDUMP <RB_COPY_DEST_INFO>
// <RB_COPY_DEST_PITCH> <length>" (hexadecimal) followed by the bytes copied.
void DumpReadback(const RegisterFile& regs, uint32_t address, uint32_t length,
                  const uint8_t* bytes) {
  static const char* directory = std::getenv("REX_READBACK_DUMP");
  if (!directory || !*directory) return;
  static const uint32_t dump_address = [] {
    const char* value = std::getenv("REX_READBACK_DUMP_ADDRESS");
    return value ? uint32_t(std::strtoul(value, nullptr, 16)) : uint32_t(0);
  }();
  static const uint32_t every = [] {
    const char* value = std::getenv("REX_READBACK_DUMP_EVERY");
    return value ? std::max(1, std::atoi(value)) : 120;
  }();
  static uint32_t sequence = 0;
  if (address != dump_address || sequence++ % every) return;
  char name[32];
  std::snprintf(name, sizeof(name), "%08x_%06u.bin", address, sequence - 1);
  std::ofstream output(std::filesystem::path(directory) / name, std::ios::binary);
  char header[64];
  int header_length = std::snprintf(header, sizeof(header), "RBDUMP %x %x %x\n",
                                    regs[XE_GPU_REG_RB_COPY_DEST_INFO],
                                    regs[XE_GPU_REG_RB_COPY_DEST_PITCH], length);
  output.write(header, header_length);
  output.write(reinterpret_cast<const char*>(bytes), length);
}
void TraceResolvePublication(uint32_t address, uint32_t length, const uint8_t* bytes) {
  static std::ofstream output = [] {
    const char* path = std::getenv("REX_RESOLVE_PUBLICATION_TRACE");
    return path && *path ? std::ofstream(path, std::ios::app) : std::ofstream();
  }();
  static uint32_t sequence = 0;
  if (!output.is_open() || sequence >= 100000 || !length) return;
  static const std::string arm_path = std::string(std::getenv("REX_RESOLVE_PUBLICATION_TRACE")) + ".arm";
  static bool armed = false;
  static ULONGLONG next_poll = 0;
  if (!armed) {
    const ULONGLONG now = GetTickCount64();
    if (now < next_poll) return;
    next_poll = now + 250;
    armed = GetFileAttributesA(arm_path.c_str()) != INVALID_FILE_ATTRIBUTES;
    if (!armed) return;
  }
  LARGE_INTEGER ticks{}, frequency{};
  QueryPerformanceCounter(&ticks);
  QueryPerformanceFrequency(&frequency);
  unsigned nonzero_samples = 0;
  for (unsigned i = 0; i < 64; ++i) {
    nonzero_samples += bytes[(uint64_t(length - 1) * i) / 63] != 0;
  }
  output << "{\"sequence\":" << sequence++ << ",\"address\":" << address
         << ",\"length\":" << length << ",\"ticks\":" << ticks.QuadPart
         << ",\"frequency\":" << frequency.QuadPart
         << ",\"nonzero_samples_of_64\":" << nonzero_samples << "}\n";
  output.flush();
}
// Optional, bounded packet evidence for query protocol investigations. Kept
// separate from rotating runtime logs so the first failure isn't lost.
void TraceOcclusionQuery(const char* stage, uint32_t address,
                         const xenos::xe_gpu_depth_sample_counts* counts,
                         bool enabled, bool resources, bool active,
                         uint32_t active_address) {
  static std::ofstream output = [] {
    const char* path = std::getenv("REX_OCCLUSION_TRACE_FILE");
    return path && *path ? std::ofstream(path, std::ios::app) : std::ofstream();
  }();
  static uint32_t sequence = 0;
  if (!output.is_open() || sequence >= 20000) return;
  output << "{\"sequence\":" << sequence++ << ",\"stage\":\"" << stage
         << "\",\"address\":" << address << ",\"enabled\":" << enabled
         << ",\"resources\":" << resources << ",\"active\":" << active
         << ",\"active_address\":" << active_address << ",\"words\":[";
  uint32_t words[8]{};
  if (counts) std::memcpy(words, counts, sizeof(words));
  for (unsigned i = 0; i < 8; ++i) {
    if (i) output << ',';
    output << words[i];
  }
  output << "]}\n";
  output.flush();
}
// Optional per-frame histogram of depth state over issued draws
// (REX_DRAW_DEPTH_HIST): key = edram mode, z enable, z write, z func.
struct DrawDepthHistogram {
  std::ofstream output;
  std::map<uint32_t, uint32_t> counts;
  uint32_t frame = 0;
};
DrawDepthHistogram* GetDrawDepthHistogram() {
  static DrawDepthHistogram* histogram = [] {
    const char* path = std::getenv("REX_DRAW_DEPTH_HIST");
    if (!path || !*path) return static_cast<DrawDepthHistogram*>(nullptr);
    auto* result = new DrawDepthHistogram;
    result->output.open(path);
    return result;
  }();
  return histogram;
}

// Optional state of the first draw after each occlusion snapshot
// (REX_OCCLUSION_DRAW_TRACE): the draw a short query brackets.
std::ofstream* OcclusionDrawTrace() {
  static std::ofstream output = [] {
    const char* path = std::getenv("REX_OCCLUSION_DRAW_TRACE");
    return path && *path ? std::ofstream(path) : std::ofstream();
  }();
  static uint32_t lines = 0;
  if (!output.is_open() || perf_trace::segment_draws[2] != 1 || lines++ >= 100000) return nullptr;
  return &output;
}

// Optional list of the draws reaching the backend (REX_DRAW_LIST_TRACE), for
// every REX_DRAW_LIST_EVERY-th frame (default 10). Lines, hexadecimal:
//   F <frame> <steady clock ms, decimal>
//   <vs> <ps> <indices> <index base> <vertex base> <color 0 info> <depth info>
//   <window offset> <edram mode> <depth control> <stencil ref mask>
//   <viewport z scale> <viewport z offset> <blend control 0> <color mask>
//   <jitter x> <jitter y> <outcome>
// (the jitter of the post passes in host pixels as float dwords, 0 unless the
// draw was jittered)
// REX_DRAW_LIST_CONSTANTS="<shader hash>:<first>[-<last>];..." (64-bit ucode
// hash of a vertex or pixel shader, decimal constant numbers of that shader)
// adds after each matching draw the line
//   C <first of the 512 float constants> <the constants as dwords>
// (the pixel shader's follow the 256 of the vertex shader), and
// REX_DRAW_LIST_AFTER_PS=<pixel shader hash> also lists each frame that follows
// a frame in which that pixel shader drew, for pairs of consecutive frames.
struct DrawListTrace {
  struct Constants {
    uint64_t shader_hash;
    uint32_t first;
    uint32_t count;
  };
  std::ofstream output;
  uint32_t every = 10;
  uint32_t frame = 0;
  std::vector<Constants> constants;
  uint64_t after_pixel_shader = 0;
  bool after_drawn = false;  // the pixel shader drew in this frame
  bool after_frame = false;  // the pixel shader drew in the previous frame
};
DrawListTrace* GetDrawListTrace() {
  static DrawListTrace* trace = [] {
    const char* path = std::getenv("REX_DRAW_LIST_TRACE");
    if (!path || !*path) return static_cast<DrawListTrace*>(nullptr);
    auto* result = new DrawListTrace;
    result->output.open(path);
    if (const char* every = std::getenv("REX_DRAW_LIST_EVERY")) {
      result->every = std::max(1, std::atoi(every));
    }
    if (const char* constants = std::getenv("REX_DRAW_LIST_CONSTANTS")) {
      std::istringstream list(constants);
      for (std::string entry; std::getline(list, entry, ';');) {
        const size_t colon = entry.find(':');
        if (colon == std::string::npos) continue;
        char* end = nullptr;
        const uint32_t first = uint32_t(std::strtoul(entry.c_str() + colon + 1, &end, 10));
        const uint32_t last = *end == '-' ? uint32_t(std::strtoul(end + 1, nullptr, 10)) : first;
        if (first <= last && last < 256) {
          result->constants.push_back(
              {std::strtoull(entry.c_str(), nullptr, 16), first, last - first + 1});
        }
      }
    }
    if (const char* after = std::getenv("REX_DRAW_LIST_AFTER_PS")) {
      result->after_pixel_shader = std::strtoull(after, nullptr, 16);
    }
    return result;
  }();
  return trace;
}
DrawListTrace* GetSampledDrawListTrace() {
  DrawListTrace* trace = GetDrawListTrace();
  return trace && (trace->frame % trace->every == 0 || trace->after_frame) ? trace : nullptr;
}
// Writes the draw when it goes out of scope; outcome is set at the exits.
struct DrawListEntry {
  DrawListTrace* trace = nullptr;
  uint32_t values[17] = {};
  // Per REX_DRAW_LIST_CONSTANTS match: first constant, dword count, dwords.
  std::vector<uint32_t> constants;
  const char* outcome = "fail";
  ~DrawListEntry() {
    if (!trace) return;
    trace->output << std::hex;
    for (uint32_t value : values) trace->output << value << ' ';
    trace->output << std::dec << outcome << '\n';
    for (size_t i = 0; i + 1 < constants.size(); i += 2 + constants[i + 1]) {
      trace->output << std::hex << "C " << constants[i];
      for (uint32_t k = 0; k < constants[i + 1]; ++k) trace->output << ' ' << constants[i + 2 + k];
      trace->output << std::dec << '\n';
    }
  }
};

// Optional list of the draws whose render target update needed ownership
// transfers (REX_TRANSFER_DRAW_TRACE), with the state that decides whether the
// draw overwrites what it claims, for every REX_TRANSFER_DRAW_EVERY-th frame
// (default 60). Lines, hexadecimal: frame, vertex and pixel shader hashes,
// primitive type, index count, pixel shader flags (1 writes depth, 2 kills
// pixels, 4 position input, 8 texture fetches), normalized depth control and
// color mask, the registers of kTransferDrawRegisters, then the transfers as
// [slot,start tiles,end tiles,source key], "u" with the uniform draw check
// (0 not checked, 1-7 the rectangle overwrite check that failed: 1 primitive,
// clipping or EDRAM mode, 2 polygon offset or sample mask, 3 alpha, 4 pixel
// shader kill or memexport, 5 depth function, 6 stencil function, 7 blending;
// 9 an overwriting rectangle with values that differ between samples, 10 a
// uniform one, 11 its vertices could not be evaluated) and the uniform draw as
// received by the render target cache (s<slots>:<bounds>>c<slots>:<cutout>).
constexpr uint32_t kTransferDrawRegisters[] = {
    XE_GPU_REG_RB_MODECONTROL,         XE_GPU_REG_RB_DEPTHCONTROL,
    XE_GPU_REG_RB_STENCILREFMASK,      XE_GPU_REG_RB_STENCILREFMASK_BF,
    XE_GPU_REG_RB_COLOR_MASK,          XE_GPU_REG_RB_COLORCONTROL,
    XE_GPU_REG_RB_BLENDCONTROL0,       XE_GPU_REG_RB_COLOR_INFO,
    XE_GPU_REG_RB_DEPTH_INFO,          XE_GPU_REG_RB_SURFACE_INFO,
    XE_GPU_REG_PA_SU_SC_MODE_CNTL,     XE_GPU_REG_PA_CL_VTE_CNTL,
    XE_GPU_REG_PA_CL_CLIP_CNTL,        XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL,
    XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR, XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL,
    XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR, XE_GPU_REG_PA_SC_WINDOW_OFFSET,
    XE_GPU_REG_PA_SC_AA_MASK,          XE_GPU_REG_PA_CL_VPORT_XSCALE,
    XE_GPU_REG_PA_CL_VPORT_XOFFSET,    XE_GPU_REG_PA_CL_VPORT_YSCALE,
    XE_GPU_REG_PA_CL_VPORT_YOFFSET,    XE_GPU_REG_PA_CL_VPORT_ZSCALE,
    XE_GPU_REG_PA_CL_VPORT_ZOFFSET,
};
// Diagnostics: why the last draw was or wasn't a uniform draw.
uint32_t uniform_draw_debug_reason = 0;
struct TransferDrawTrace {
  std::ofstream output;
  uint32_t every = 60;
  uint32_t frame = 0;
};
TransferDrawTrace* GetTransferDrawTrace() {
  static TransferDrawTrace* trace = [] {
    const char* path = std::getenv("REX_TRANSFER_DRAW_TRACE");
    if (!path || !*path) return static_cast<TransferDrawTrace*>(nullptr);
    auto* result = new TransferDrawTrace;
    result->output.open(path);
    if (const char* every = std::getenv("REX_TRANSFER_DRAW_EVERY")) {
      result->every = std::max(1, std::atoi(every));
    }
    return result;
  }();
  return trace;
}
// native_scene_read_trace: every fetch from a scene-scaled texture and every
// vertex fetch from a scene-resolved range, logged the first time each is seen.
void TraceSceneTextureReads(const RegisterFile& regs, const TextureCache& texture_cache,
                            const RenderTargetCache& render_target_cache,
                            const Shader& vertex_shader, const Shader* pixel_shader,
                            bool scene_draw, bool scene_input_draw, uint64_t frame) {
  static std::set<std::tuple<uint64_t, uint32_t, uint32_t, uint32_t>> seen;
  // The draw's class: 0 not a scene draw, 1 a scene draw at the scene's scale,
  // 2 one at the input scale.
  const uint32_t draw_class = scene_draw ? (scene_input_draw ? 2 : 1) : 0;
  const char* const draw_names[] = {"other", "scene", "scene-input"};
  for (const Shader* shader : {&vertex_shader, pixel_shader}) {
    if (!shader) {
      continue;
    }
    for (const Shader::TextureBinding& binding : shader->texture_bindings()) {
      TextureCache::SceneTextureInfo info;
      if (!texture_cache.GetActiveSceneTexture(binding.fetch_constant, info)) {
        continue;
      }
      const ParsedTextureFetchInstruction& fetch = binding.fetch_instr;
      const bool offsets = fetch.attributes.offset_x != 0.0f || fetch.attributes.offset_y != 0.0f;
      const uint32_t fetch_bits = uint32_t(fetch.opcode) |
                                  (uint32_t(fetch.attributes.unnormalized_coordinates) << 8) |
                                  (uint32_t(offsets) << 9) | (draw_class << 10);
      if (!seen.emplace(shader->ucode_data_hash(), binding.fetch_constant,
                        fetch_bits | (uint32_t(info.input) << 12), info.base)
               .second) {
        continue;
      }
      REXGPU_INFO("Scene texture read: frame {} {} shader {:016X} fetch constant {} {} "
                  "unnormalized {} offsets {} {} {}, texture {:08X} {}x{} format {} at the {} "
                  "scale, draw {} (targets at {}/{})",
                  frame, shader->type() == xenos::ShaderType::kVertex ? "vertex" : "pixel",
                  shader->ucode_data_hash(), binding.fetch_constant,
                  fetch.opcode_name ? fetch.opcode_name : "?",
                  int(fetch.attributes.unnormalized_coordinates), fetch.attributes.offset_x,
                  fetch.attributes.offset_y, int(offsets), info.base, info.width, info.height,
                  info.format, info.input ? "input" : "output", draw_names[draw_class],
                  render_target_cache.last_update_scale().numerator,
                  render_target_cache.last_update_scale().denominator);
    }
  }
  for (const Shader::VertexBinding& binding : vertex_shader.vertex_bindings()) {
    const auto fetch = regs.GetVertexFetch(binding.fetch_constant);
    const uint32_t address = fetch.address << 2, size = fetch.size << 2;
    if (!texture_cache.IsRangeSceneResolved(address, size)) {
      continue;
    }
    if (!seen.emplace(vertex_shader.ucode_data_hash(), binding.fetch_constant + 1000, draw_class,
                      address)
             .second) {
      continue;
    }
    REXGPU_INFO("Scene vertex read: frame {} vertex shader {:016X} fetch constant {} range "
                "{:08X} size {}, draw {}",
                frame, vertex_shader.ucode_data_hash(), binding.fetch_constant, address, size,
                draw_names[draw_class]);
  }
}

void TraceTransferDraw(const RegisterFile& regs, const RenderTargetCache& cache,
                       const Shader& vertex_shader, const Shader* pixel_shader,
                       xenos::PrimitiveType primitive_type, uint32_t index_count,
                       uint32_t ps_param_gen_pos, reg::RB_DEPTHCONTROL depth_control,
                       uint32_t color_mask) {
  TransferDrawTrace* trace = GetTransferDrawTrace();
  if (!trace || trace->frame % trace->every != 0) return;
  std::string transfers;
  if (!cache.AppendLastUpdateTransfers(transfers)) return;
  uint32_t ps_flags = 0;
  if (pixel_shader) {
    ps_flags |= uint32_t(pixel_shader->writes_depth()) | uint32_t(pixel_shader->kills_pixels()) << 1 |
                uint32_t(ps_param_gen_pos != UINT32_MAX) << 2 |
                uint32_t(!pixel_shader->texture_bindings().empty()) << 3;
  }
  std::ostream& out = trace->output;
  out << std::hex << trace->frame << ' ' << uint32_t(vertex_shader.ucode_data_hash()) << ' '
      << (pixel_shader ? uint32_t(pixel_shader->ucode_data_hash()) : 0) << ' '
      << uint32_t(primitive_type) << ' ' << index_count << ' ' << ps_flags << ' '
      << depth_control.value << ' ' << color_mask;
  for (uint32_t index : kTransferDrawRegisters) out << ' ' << regs.values[index];
  out << std::dec << ' ' << transfers << " u" << uniform_draw_debug_reason << ' '
      << cache.DescribeLastUpdateUniformDraw() << '\n';
  uniform_draw_debug_reason = 0;
}

// Whether a draw replaces what it covers in its render targets: rectangles or a
// quad (the geometry is checked by DrawExtentEstimator::EstimateCoveredRectangle)
// without blending, kills, alpha tests or depth and stencil tests that can
// fail, as FM4 clears (often with 4x MSAA at half the pitch) and full-screen
// passes do. overwritten_slots_out: the render targets it overwrites entirely
// ([0] depth and stencil, [1...] every channel of a color render target).
// values_may_be_uniform_out: a single screen-space rectangle (kRectangleList,
// 3 vertices, clipping disabled) whose pixel shader output can only depend on
// the interpolators (no textures, position input or depth output), so it
// writes the same values to every sample if its interpolators and Z are the
// same at all vertices.
bool GetDrawOverwrite(const RegisterFile& regs, xenos::PrimitiveType primitive_type,
                      uint32_t index_count, const Shader* pixel_shader, uint32_t ps_param_gen_pos,
                      reg::RB_DEPTHCONTROL depth_control, uint32_t color_mask,
                      uint32_t& overwritten_slots_out, bool& values_may_be_uniform_out) {
  overwritten_slots_out = 0;
  values_may_be_uniform_out = false;
  uniform_draw_debug_reason = 1;
  xenos::EdramMode edram_mode = regs.Get<reg::RB_MODECONTROL>().edram_mode;
  if (edram_mode != xenos::EdramMode::kColorDepth && edram_mode != xenos::EdramMode::kDepthOnly) {
    return false;
  }
  uniform_draw_debug_reason = 2;
  auto pa_su_sc_mode_cntl = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
  if (pa_su_sc_mode_cntl.poly_offset_front_enable || pa_su_sc_mode_cntl.poly_offset_back_enable ||
      pa_su_sc_mode_cntl.poly_offset_para_enable || (regs[XE_GPU_REG_PA_SC_AA_MASK] & 0xF) != 0xF) {
    return false;
  }
  uniform_draw_debug_reason = 3;
  // Without a pixel shader (no color written), coverage doesn't depend on alpha
  // (see draw_util::IsPixelShaderNeededWithRasterization).
  auto rb_colorcontrol = regs.Get<reg::RB_COLORCONTROL>();
  if (pixel_shader && (rb_colorcontrol.alpha_to_mask_enable ||
                       (rb_colorcontrol.alpha_test_enable &&
                        rb_colorcontrol.alpha_func != xenos::CompareFunction::kAlways))) {
    return false;
  }
  uniform_draw_debug_reason = 4;
  if (pixel_shader && (pixel_shader->kills_pixels() || pixel_shader->memexport_eM_written())) {
    return false;
  }
  uniform_draw_debug_reason = 5;
  if (depth_control.z_enable && depth_control.zfunc != xenos::CompareFunction::kAlways) {
    return false;
  }
  uniform_draw_debug_reason = 6;
  if (depth_control.stencil_enable &&
      (depth_control.stencilfunc != xenos::CompareFunction::kAlways ||
       (depth_control.backface_enable &&
        depth_control.stencilfunc_bf != xenos::CompareFunction::kAlways))) {
    return false;
  }
  for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
    uint32_t rt_mask = (color_mask >> (4 * i)) & 0xF;
    if (!rt_mask) {
      continue;
    }
    // No blending (source ONE, destination ZERO, ADD for color and alpha).
    if ((regs[reg::RB_BLENDCONTROL::rt_register_indices[i]] & 0x1FFF1FFF) != 0x00010001) {
      uniform_draw_debug_reason = 7;
      return false;
    }
    auto color_info = regs.Get<reg::RB_COLOR_INFO>(reg::RB_COLOR_INFO::rt_register_indices[i]);
    uint32_t all_components =
        (uint32_t(1) << xenos::GetColorRenderTargetFormatComponentCount(
                            xenos::GetStorageColorFormat(color_info.color_format))) -
        1;
    if ((rt_mask & all_components) == all_components) {
      overwritten_slots_out |= uint32_t(1) << (1 + i);
    }
  }
  // Depth and stencil both written everywhere (stencil passes and Z always).
  auto stencil_overwritten = [](xenos::StencilOp zpass, reg::RB_STENCILREFMASK ref_mask) {
    return zpass == xenos::StencilOp::kReplace && ref_mask.stencilwritemask == 0xFF;
  };
  if (depth_control.z_enable && depth_control.z_write_enable && depth_control.stencil_enable &&
      stencil_overwritten(depth_control.stencilzpass, regs.Get<reg::RB_STENCILREFMASK>()) &&
      (!depth_control.backface_enable ||
       stencil_overwritten(depth_control.stencilzpass_bf,
                           regs.Get<reg::RB_STENCILREFMASK>(XE_GPU_REG_RB_STENCILREFMASK_BF)))) {
    overwritten_slots_out |= 1;
  }
  values_may_be_uniform_out =
      primitive_type == xenos::PrimitiveType::kRectangleList && index_count == 3 &&
      regs.Get<reg::PA_CL_CLIP_CNTL>().clip_disable &&
      (!pixel_shader || (ps_param_gen_pos == UINT32_MAX && !pixel_shader->writes_depth() &&
                         pixel_shader->texture_bindings().empty()));
  uniform_draw_debug_reason = 8;
  return true;
}

// Optional inventory of the guest GPU state per shader pair (REX_GPU_INVENTORY),
// rewritten as JSON every 600 frames, for planning a native renderer.
struct GpuInventory {
  struct Pair {
    uint64_t draws = 0;
    uint32_t frames = 0;
    uint32_t last_frame = UINT32_MAX;
    bool memexport = false;
    std::set<uint32_t> primitives;
    std::set<uint32_t> edram_modes;
    // color formats (4 x 4 bits), written color targets (4 bits), depth
    // format and use (2 bits), MSAA samples (2 bits)
    std::set<uint32_t> render_targets;
    std::set<uint32_t> blend;  // RB_BLENDCONTROL of written targets, first only
    std::set<uint32_t> depth;  // normalized RB_DEPTHCONTROL
    // format (6 bits), dimension (2), tiled, packed mips, signed (any), stage
    std::set<uint32_t> textures;
  };
  std::string path;
  uint32_t frame = 0;
  std::map<std::pair<uint64_t, uint64_t>, Pair> pairs;
};
GpuInventory* GetGpuInventory() {
  static GpuInventory* inventory = [] {
    const char* path = std::getenv("REX_GPU_INVENTORY");
    if (!path || !*path) return static_cast<GpuInventory*>(nullptr);
    auto* result = new GpuInventory;
    result->path = path;
    return result;
  }();
  return inventory;
}
void WriteGpuInventory(const GpuInventory& inventory) {
  std::ofstream output(inventory.path + ".tmp");
  const auto list = [&output](const char* name, const std::set<uint32_t>& values) {
    output << ",\"" << name << "\":[";
    bool first = true;
    for (uint32_t value : values) {
      output << (first ? "" : ",") << value;
      first = false;
    }
    output << ']';
  };
  output << "{\"frames\":" << inventory.frame << ",\"pairs\":[";
  bool first = true;
  for (const auto& [key, pair] : inventory.pairs) {
    char hashes[64];
    std::snprintf(hashes, sizeof(hashes), "\"vs\":\"%016llx\",\"ps\":\"%016llx\"",
                  static_cast<unsigned long long>(key.first),
                  static_cast<unsigned long long>(key.second));
    output << (first ? "\n{" : ",\n{") << hashes << ",\"draws\":" << pair.draws
           << ",\"frames\":" << pair.frames << ",\"memexport\":" << int(pair.memexport);
    list("primitives", pair.primitives);
    list("edram_modes", pair.edram_modes);
    list("render_targets", pair.render_targets);
    list("blend", pair.blend);
    list("depth", pair.depth);
    list("textures", pair.textures);
    output << '}';
    first = false;
  }
  output << "]}\n";
  output.close();
  std::error_code error;
  std::filesystem::rename(inventory.path + ".tmp", inventory.path, error);
}

// Optional frame graph of every REX_FRAME_GRAPH_EVERY-th frame (default 600)
// (REX_FRAME_GRAPH=<file>), JSON lines: "frame", "pass" (consecutive draws
// with the same surface state: EDRAM pitch, MSAA, written color targets with
// base and format, depth base and format, window offset, edram mode; the
// resolves whose output its textures read) and "resolve" (source, clears,
// destination), each with its "event" index in the frame, and later "gpu"
// (the sampled frame's GPU time per event, between submissions and after the
// swap; see FrameGraphMark). Resolve outputs are tracked across frames to find
// readers. "frame" also has the steady clock in ms ("ms") to match captures.
// Cheap enough for REX_FRAME_GRAPH_EVERY=1: the pass description is only
// formatted when the surface state changes.
struct FrameGraphTrace {
  struct Output {
    uint32_t length, id;
  };
  std::ofstream output;
  uint32_t every = 600;
  uint32_t frame = 0;
  uint32_t next_id = 0;
  // Most recent resolve per destination range, by address, not overlapping.
  std::map<uint32_t, Output> outputs;
  std::array<uint32_t, 12> pass_key{};  // Surface state of the open pass.
  bool pass_open = false;
  std::string pass;  // Surface description of the open pass.
  uint32_t pass_draws = 0;
  std::set<uint32_t> pass_reads;
  // Per slot: smallest and largest claimed EDRAM length in the pass.
  std::array<std::pair<uint32_t, uint32_t>, 1 + xenos::kMaxColorRenderTargets> pass_ranges{};
  std::string pass_transfers;  // At most 32 per pass.
  uint32_t pass_transfer_count = 0;
  uint32_t event = 0;  // Next pass or resolve index in the frame.
  uint32_t pass_event = 0;
  int32_t resolve_event = -1;
  bool sampled() const { return frame % every == 0; }
  void Flush() {
    if (!pass_draws) return;
    output << "{\"event\":" << pass_event << ",\"pass\":" << pass << ",\"draws\":" << pass_draws
           << ",\"reads\":[";
    bool first = true;
    for (uint32_t id : pass_reads) {
      output << (first ? "" : ",") << id;
      first = false;
    }
    output << "],\"ranges\":[";
    bool first_range = true;
    for (uint32_t slot = 0; slot < pass_ranges.size(); ++slot) {
      if (!pass_ranges[slot].second)
        continue;
      output << (first_range ? "" : ",") << '[' << slot << ',' << pass_ranges[slot].first << ','
             << pass_ranges[slot].second << ']';
      first_range = false;
    }
    output << "],\"transfers\":" << pass_transfer_count << ",\"transfer_list\":[" << pass_transfers
           << "]}\n";
    pass_ranges = {};
    pass_transfers.clear();
    pass_transfer_count = 0;
    pass_draws = 0;
    pass_reads.clear();
  }
};
FrameGraphTrace* GetFrameGraphTrace() {
  static FrameGraphTrace* trace = [] {
    const char* path = std::getenv("REX_FRAME_GRAPH");
    if (!path || !*path) return static_cast<FrameGraphTrace*>(nullptr);
    auto* result = new FrameGraphTrace;
    result->output.open(path);
    if (const char* every = std::getenv("REX_FRAME_GRAPH_EVERY")) {
      result->every = std::max(1, std::atoi(every));
    }
    return result;
  }();
  return trace;
}
std::string FrameGraphDescribePass(const RegisterFile& regs, uint32_t color_mask, bool depth_used,
                                   xenos::EdramMode edram_mode);
// Returns the event index if the draw starts a pass of a sampled frame, or -1.
int32_t FrameGraphDraw(const RegisterFile& regs, const Shader& vertex_shader,
                       const Shader* pixel_shader, uint32_t color_mask,
                       reg::RB_DEPTHCONTROL depth_control, xenos::EdramMode edram_mode) {
  FrameGraphTrace* trace = GetFrameGraphTrace();
  if (!trace || !trace->sampled())
    return -1;
  const bool depth_used = depth_control.z_enable || depth_control.stencil_enable;
  std::array<uint32_t, 12> key = {regs.values[XE_GPU_REG_RB_SURFACE_INFO], uint32_t(edram_mode),
                                  color_mask,
                                  depth_used ? regs.values[XE_GPU_REG_RB_DEPTH_INFO] : 0,
                                  regs.values[XE_GPU_REG_PA_SC_WINDOW_OFFSET]};
  for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
    if (color_mask >> (i * 4) & 0xF) {
      key[5 + i] = regs.values[reg::RB_COLOR_INFO::rt_register_indices[i]];
    }
  }
  int32_t started = -1;
  if (!trace->pass_open || key != trace->pass_key) {
    trace->Flush();
    trace->pass_key = key;
    trace->pass_open = true;
    trace->pass = FrameGraphDescribePass(regs, color_mask, depth_used, edram_mode);
    trace->pass_event = trace->event++;
    started = int32_t(trace->pass_event);
  }
  ++trace->pass_draws;
  for (const Shader* shader : {&vertex_shader, pixel_shader}) {
    if (!shader)
      continue;
    for (const Shader::TextureBinding& binding : shader->texture_bindings()) {
      const uint32_t address = regs.GetTextureFetch(binding.fetch_constant).base_address << 12;
      auto it = trace->outputs.upper_bound(address);
      if (it == trace->outputs.begin())
        continue;
      --it;
      if (address < it->first + it->second.length)
        trace->pass_reads.insert(it->second.id);
    }
  }
  return started;
}
std::string FrameGraphDescribePass(const RegisterFile& regs, uint32_t color_mask, bool depth_used,
                                   xenos::EdramMode edram_mode) {
  char buffer[96];
  std::string pass = "{";
  const auto surface = regs.Get<reg::RB_SURFACE_INFO>();
  std::snprintf(buffer, sizeof(buffer), "\"pitch\":%u,\"msaa\":%u,\"mode\":%u,\"color\":[",
                surface.surface_pitch, uint32_t(surface.msaa_samples), uint32_t(edram_mode));
  pass += buffer;
  bool first = true;
  for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
    if (!(color_mask >> (i * 4) & 0xF)) continue;
    const auto info = regs.Get<reg::RB_COLOR_INFO>(reg::RB_COLOR_INFO::rt_register_indices[i]);
    std::snprintf(buffer, sizeof(buffer), "%s[%u,%u,%u]", first ? "" : ",", i,
                  info.color_base | info.color_base_bit_11 << 11, uint32_t(info.color_format));
    pass += buffer;
    first = false;
  }
  pass += "]";
  if (depth_used) {
    const auto depth = regs.Get<reg::RB_DEPTH_INFO>();
    std::snprintf(buffer, sizeof(buffer), ",\"depth\":[%u,%u]",
                  depth.depth_base | depth.depth_base_bit_11 << 11, uint32_t(depth.depth_format));
    pass += buffer;
  }
  const uint32_t window_offset = regs.values[XE_GPU_REG_PA_SC_WINDOW_OFFSET];
  std::snprintf(buffer, sizeof(buffer), ",\"offset\":[%d,%d]}",
                int32_t(window_offset << 17) >> 17, int32_t(window_offset << 1) >> 17);
  pass += buffer;
  return pass;
}
// Called before a resolve; returns its event index in a sampled frame, or -1.
int32_t FrameGraphBeginResolve() {
  FrameGraphTrace* trace = GetFrameGraphTrace();
  if (!trace || !trace->sampled())
    return -1;
  trace->Flush();
  trace->pass_open = false;
  trace->resolve_event = int32_t(trace->event++);
  return trace->resolve_event;
}
void FrameGraphResolve(const RegisterFile& regs, uint32_t address, uint32_t length) {
  FrameGraphTrace* trace = GetFrameGraphTrace();
  if (!trace) return;
  const uint32_t id = trace->next_id++;
  if (length) {
    auto& outputs = trace->outputs;
    auto it = outputs.lower_bound(address);
    if (it != outputs.begin() && std::prev(it)->first + std::prev(it)->second.length > address) {
      --it;
    }
    while (it != outputs.end() && it->first < address + length)
      it = outputs.erase(it);
    outputs[address] = {length, id};
  }
  if (!trace->sampled()) return;
  trace->Flush();
  trace->pass_open = false;
  const uint32_t event =
      trace->resolve_event >= 0 ? uint32_t(trace->resolve_event) : trace->event++;
  trace->resolve_event = -1;
  const auto control = regs.Get<reg::RB_COPY_CONTROL>();
  const auto info = regs.Get<reg::RB_COPY_DEST_INFO>();
  const auto pitch = regs.Get<reg::RB_COPY_DEST_PITCH>();
  const auto surface = regs.Get<reg::RB_SURFACE_INFO>();
  uint32_t source_base, source_format;
  if (control.copy_src_select >= xenos::kMaxColorRenderTargets) {
    const auto depth = regs.Get<reg::RB_DEPTH_INFO>();
    source_base = depth.depth_base | depth.depth_base_bit_11 << 11;
    source_format = uint32_t(depth.depth_format);
  } else {
    const auto color = regs.Get<reg::RB_COLOR_INFO>(
        reg::RB_COLOR_INFO::rt_register_indices[control.copy_src_select]);
    source_base = color.color_base | color.color_base_bit_11 << 11;
    source_format = uint32_t(color.color_format);
  }
  char buffer[320];
  std::snprintf(
      buffer, sizeof(buffer),
      "{\"event\":%u,\"resolve\":%u,\"source\":[%u,%u,%u],\"pitch\":%u,\"msaa\":%u,\"sample\":%u,"
      "\"command\":%u,\"clear\":[%u,%u],\"dest\":[%u,%u,%u,%u,%u]}\n",
      event, id, control.copy_src_select, source_base, source_format, surface.surface_pitch,
      uint32_t(surface.msaa_samples), uint32_t(control.copy_sample_select),
      uint32_t(control.copy_command), control.color_clear_enable, control.depth_clear_enable,
      address, length, uint32_t(info.copy_dest_format), pitch.copy_dest_pitch,
      pitch.copy_dest_height);
  trace->output << buffer;
}
// Returns whether the frame that ends was sampled.
bool FrameGraphSwap() {
  FrameGraphTrace* trace = GetFrameGraphTrace();
  if (!trace)
    return false;
  const bool sampled = trace->sampled();
  if (sampled) {
    trace->Flush();
    trace->output << std::flush;
  }
  trace->pass_open = false;
  trace->event = 0;
  trace->resolve_event = -1;
  if (++trace->frame % trace->every == 0) {
    trace->output << "{\"frame\":" << trace->frame << ",\"ms\":"
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count()
                  << "}\n";
  }
  return sampled;
}
// After the render target update of a draw in a sampled frame.
void FrameGraphRenderTargets(const RenderTargetCache& cache) {
  FrameGraphTrace* trace = GetFrameGraphTrace();
  if (!trace || !trace->sampled() || !trace->pass_open)
    return;
  for (uint32_t slot = 0; slot < trace->pass_ranges.size(); ++slot) {
    const uint32_t length = cache.GetLastUpdateRange(slot).second;
    if (!length)
      continue;
    auto& range = trace->pass_ranges[slot];
    range.first = range.second ? std::min(range.first, length) : length;
    range.second = std::max(range.second, length);
  }
  if (trace->pass_transfer_count < 32) {
    trace->pass_transfer_count += cache.AppendLastUpdateTransfers(trace->pass_transfers);
  } else {
    std::string ignored;
    trace->pass_transfer_count += cache.AppendLastUpdateTransfers(ignored);
  }
}
uint32_t FrameGraphFrame() {
  FrameGraphTrace* trace = GetFrameGraphTrace();
  return trace ? trace->frame : 0;
}
void FrameGraphWriteGpu(uint32_t frame, const std::vector<double>& events, double gaps,
                        double swap) {
  FrameGraphTrace* trace = GetFrameGraphTrace();
  if (!trace)
    return;
  char buffer[32];
  trace->output << "{\"gpu\":" << frame << ",\"events\":[";
  for (size_t i = 0; i < events.size(); ++i) {
    std::snprintf(buffer, sizeof(buffer), "%s%.4f", i ? "," : "", events[i]);
    trace->output << buffer;
  }
  std::snprintf(buffer, sizeof(buffer), "],\"gaps\":%.4f", gaps);
  trace->output << buffer;
  std::snprintf(buffer, sizeof(buffer), ",\"swap\":%.4f}\n", swap);
  trace->output << buffer << std::flush;
}

// Optional dump of the inputs of selected draws (REX_DRAW_DUMP=<file>,
// REX_DRAW_DUMP_VS=<vertex shader hash, low 32 bits, hexadecimal>, every
// REX_DRAW_DUMP_EVERY-th frame, default 30), to compare the same draw between
// runs: "V <frame> <steady ms> <draw in frame> <pixel shader> <guest index
// base> <index bytes> <index data hash>", per vertex binding "B <fetch
// constant> <address> <bytes> <endian> <stride words> <data hash>", then
// "R <registers 0x2100-0x23FF>", "C <hash of the 256 vertex shader float
// constants> <the 1024 dwords>" and "P" (the shader run on the CPU, below),
// hexadecimal; hashes are XXH3 low 32 bits of guest memory at draw time.
struct DrawDumpTrace {
  std::ofstream output;
  uint32_t vertex_shader = 0;
  uint32_t every = 30;
  uint32_t frame = 0;
  uint32_t draw = 0;
};
DrawDumpTrace* GetDrawDumpTrace() {
  static DrawDumpTrace* trace = [] {
    const char* path = std::getenv("REX_DRAW_DUMP");
    const char* vs = std::getenv("REX_DRAW_DUMP_VS");
    if (!path || !*path || !vs)
      return static_cast<DrawDumpTrace*>(nullptr);
    auto* result = new DrawDumpTrace;
    result->output.open(path);
    result->vertex_shader = uint32_t(std::strtoul(vs, nullptr, 16));
    if (const char* every = std::getenv("REX_DRAW_DUMP_EVERY")) {
      result->every = std::max(1, std::atoi(every));
    }
    return result;
  }();
  return trace;
}
void DrawDump(const RegisterFile& regs, const memory::Memory& memory, const Shader& vertex_shader,
              const Shader* pixel_shader, uint32_t index_base, uint32_t index_bytes) {
  DrawDumpTrace* trace = GetDrawDumpTrace();
  if (!trace)
    return;
  const uint32_t draw = trace->draw++;
  if (trace->frame % trace->every ||
      uint32_t(vertex_shader.ucode_data_hash()) != trace->vertex_shader) {
    return;
  }
  auto hash = [&](uint32_t address, uint32_t length) {
    const void* data = memory.TranslatePhysical(address);
    return data && length ? uint32_t(XXH3_64bits(data, length)) : 0;
  };
  std::ostream& out = trace->output;
  out << std::hex << "V " << trace->frame << ' '
      << std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
             .count()
      << ' ' << draw << ' ' << (pixel_shader ? uint32_t(pixel_shader->ucode_data_hash()) : 0) << ' '
      << index_base << ' ' << index_bytes << ' ' << (index_base ? hash(index_base, index_bytes) : 0)
      << '\n';
  for (const Shader::VertexBinding& binding : vertex_shader.vertex_bindings()) {
    const auto fetch = regs.GetVertexFetch(binding.fetch_constant);
    out << "B " << binding.fetch_constant << ' ' << (fetch.address << 2) << ' ' << (fetch.size << 2)
        << ' ' << uint32_t(fetch.endian) << ' ' << binding.stride_words << ' '
        << hash(fetch.address << 2, fetch.size << 2) << '\n';
  }
  // Context registers 0x2100-0x23FF (vertex grouper, clipper, viewport,
  // rasterizer setup, render backend).
  out << "R";
  for (uint32_t index = 0x2100; index < 0x2400; ++index)
    out << ' ' << regs.values[index];
  out << '\n';
  const uint32_t* constants = &regs.values[XE_GPU_REG_SHADER_CONSTANT_000_X];
  out << "C " << uint32_t(XXH3_64bits(constants, 256 * 4 * sizeof(uint32_t)));
  for (uint32_t i = 0; i < 256 * 4; ++i)
    out << ' ' << constants[i];
  out << std::dec << '\n';
  // The vertex shader on the CPU for the first 96 indices, from the same guest
  // state: "P <vertices> <inside the clip volume> <x y z w of the first 24>
  // <surface info>".
  if (!ShaderInterpreter::CanInterpretShader(vertex_shader))
    return;
  struct PositionSink : ShaderInterpreter::ExportSink {
    float position[4] = {};
    bool written = false;
    void Export(ucode::ExportRegister export_register, const float* value,
                uint32_t value_mask) override {
      if (export_register != ucode::ExportRegister::kVSPosition)
        return;
      for (uint32_t i = 0; i < 4; ++i) {
        if (value_mask & (1u << i))
          position[i] = value[i];
      }
      written = true;
    }
  };
  static ShaderInterpreter* interpreter = new ShaderInterpreter(regs, memory);
  PositionSink sink;
  interpreter->SetShader(vertex_shader);
  interpreter->SetExportSink(&sink);
  const auto initiator = regs.Get<reg::VGT_DRAW_INITIATOR>();
  const auto dma_size = regs.Get<reg::VGT_DMA_SIZE>();
  const bool dma = initiator.source_select == xenos::SourceSelect::kDMA;
  const bool index_16 = initiator.index_size == xenos::IndexFormat::kInt16;
  xenos::Endian index_endian = dma_size.swap_mode;
  if (index_16) {
    index_endian = index_endian == xenos::Endian::k8in32    ? xenos::Endian::k8in16
                   : index_endian == xenos::Endian::k16in32 ? xenos::Endian::kNone
                                                            : index_endian;
  }
  const auto* indices = memory.TranslatePhysical<const uint8_t*>(regs[XE_GPU_REG_VGT_DMA_BASE] &
                                                                 ~uint32_t(index_16 ? 1 : 3));
  const uint32_t reset_index = regs.Get<reg::VGT_MULTI_PRIM_IB_RESET_INDX>().reset_indx;
  const bool reset = regs.Get<reg::PA_SU_SC_MODE_CNTL>().multi_prim_ib_ena;
  const uint32_t offset = regs.Get<reg::VGT_INDX_OFFSET>().indx_offset;
  const uint32_t min_index = regs.Get<reg::VGT_MIN_VTX_INDX>().min_indx;
  const uint32_t max_index = regs.Get<reg::VGT_MAX_VTX_INDX>().max_indx;
  uint32_t vertices = 0, inside = 0;
  float first[4] = {};
  float corners[24][4] = {};
  for (uint32_t i = 0; i < std::min(initiator.num_indices, 96u); ++i) {
    uint32_t index = i;
    if (dma) {
      if (i >= dma_size.num_words || !indices)
        break;
      index = index_16 ? uint32_t(reinterpret_cast<const uint16_t*>(indices)[i])
                       : reinterpret_cast<const uint32_t*>(indices)[i];
      index = xenos::GpuSwap(index, index_endian) & 0xFFFFFF;
      if (reset && index == reset_index)
        continue;
    }
    index = std::min(max_index, std::max(min_index, (index + offset) & 0xFFFFFF));
    sink.written = false;
    interpreter->temp_registers()[0] = float(index);
    interpreter->Execute();
    if (!sink.written)
      continue;
    const float* p = sink.position;
    if (vertices < 24)
      std::memcpy(corners[vertices], p, sizeof(first));
    if (!vertices++)
      std::memcpy(first, p, sizeof(first));
    inside += std::abs(p[0]) <= p[3] && std::abs(p[1]) <= p[3] && p[2] >= 0.0f && p[2] <= p[3];
  }
  interpreter->SetExportSink(nullptr);
  out << "P " << vertices << ' ' << inside << ' ' << first[0] << ' ' << first[1] << ' ' << first[2]
      << ' ' << first[3];
  for (uint32_t i = 1; i < std::min(vertices, 24u); ++i)
    out << ' ' << corners[i][0] << ' ' << corners[i][1] << ' ' << corners[i][2] << ' '
        << corners[i][3];
  out << ' ' << std::hex << regs[XE_GPU_REG_RB_SURFACE_INFO] << std::dec << '\n';
}
void DrawDumpSwap() {
  if (DrawDumpTrace* trace = GetDrawDumpTrace()) {
    ++trace->frame;
    trace->draw = 0;
    trace->output.flush();
  }
}

// Optional per-segment occlusion samples (REX_OCCLUSION_SEGMENT_TRACE) to
// compare render target paths query by query.
void TraceOcclusionSegment(uint32_t interval, bool awaited, bool host_queries, uint32_t address,
                           const uint64_t* segment, uint32_t counters, const uint32_t* draws) {
  static std::ofstream output = [] {
    const char* path = std::getenv("REX_OCCLUSION_SEGMENT_TRACE");
    return path && *path ? std::ofstream(path) : std::ofstream();
  }();
  static const auto start = std::chrono::steady_clock::now();
  static uint32_t lines = 0;
  if (!output.is_open() || lines++ >= 400000) return;
  output << std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count()
         << ' ' << interval << ' ' << awaited << ' ' << host_queries << ' ' << address;
  for (uint32_t i = 0; i < counters; ++i) output << ' ' << segment[i];
  output << " d" << draws[0] << ' ' << draws[1] << ' ' << draws[2];
  output << std::endl;
}
}  // namespace

// Generated with `xb buildshaders`.
namespace shaders {
#include "../shaders/bytecode/d3d12_5_1/apply_gamma_pwl_cs.h"
#include "../shaders/bytecode/d3d12_5_1/apply_gamma_pwl_fxaa_luma_cs.h"
#include "../shaders/bytecode/d3d12_5_1/apply_gamma_table_cs.h"
#include "../shaders/bytecode/d3d12_5_1/apply_gamma_table_fxaa_luma_cs.h"
#include "../shaders/bytecode/d3d12_5_1/fxaa_cs.h"
#include "../shaders/bytecode/d3d12_5_1/fxaa_extreme_cs.h"
#include "../shaders/bytecode/d3d12_5_1/resolve_downscale_cs.h"
}  // namespace shaders

D3D12CommandProcessor::D3D12CommandProcessor(D3D12GraphicsSystem* graphics_system,
                                             system::KernelState* kernel_state)
    : CommandProcessor(graphics_system, kernel_state), deferred_command_list_(*this) {
  legacy_readback_memexport_cvar_name_ = "d3d12_readback_memexport";
  // The ranges with side effects in D3D12CommandProcessor::WriteRegister.
  SetRegisterWritesHooked(XE_GPU_REG_SHADER_CONSTANT_000_X, XE_GPU_REG_SHADER_CONSTANT_511_W);
  SetRegisterWritesHooked(XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031,
                          XE_GPU_REG_SHADER_CONSTANT_LOOP_31);
  SetRegisterWritesHooked(XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0,
                          XE_GPU_REG_SHADER_CONSTANT_FETCH_31_5);
}
D3D12CommandProcessor::~D3D12CommandProcessor() = default;

void D3D12CommandProcessor::UpdateDebugMarkersEnabled() {
  debug_markers_enabled_ = IsGpuDebugMarkersEnabled();
}

void D3D12CommandProcessor::PushDebugMarker(const char* format, ...) {
  if (!debug_markers_enabled_) {
    return;
  }
  char label[256];
  va_list args;
  va_start(args, format);
  vsnprintf(label, sizeof(label), format, args);
  va_end(args);
  deferred_command_list_.BeginDebugMarker(label);
}

void D3D12CommandProcessor::PopDebugMarker() {
  if (!debug_markers_enabled_) {
    return;
  }
  deferred_command_list_.EndDebugMarker();
}

void D3D12CommandProcessor::InsertDebugMarker(const char* format, ...) {
  if (!debug_markers_enabled_) {
    return;
  }
  char label[256];
  va_list args;
  va_start(args, format);
  vsnprintf(label, sizeof(label), format, args);
  va_end(args);
  deferred_command_list_.InsertDebugMarker(label);
}

void D3D12CommandProcessor::ClearCaches() {
  CommandProcessor::ClearCaches();
  InvalidateAllVertexBufferResidency();
  cache_clear_requested_ = true;
}

void D3D12CommandProcessor::InvalidateGpuMemory() {
  if (shared_memory_) {
    shared_memory_->InvalidateAllPages();
  }
}

void D3D12CommandProcessor::InvalidateAllVertexBufferResidency() {
  vertex_buffers_in_sync_[0] = 0;
  vertex_buffers_in_sync_[1] = 0;
  for (VertexBufferState& state : vertex_buffer_states_) {
    state.address = UINT32_MAX;
    state.size = UINT32_MAX;
  }
}

void D3D12CommandProcessor::InvalidateVertexBufferResidency(uint32_t vfetch_index) {
  if (vfetch_index >= vertex_buffer_states_.size()) {
    return;
  }
  vertex_buffers_in_sync_[vfetch_index >> 6] &= ~(uint64_t(1) << (vfetch_index & 63));
}

void D3D12CommandProcessor::InvalidateVertexBufferResidencyRange(uint32_t first_vfetch,
                                                                 uint32_t last_vfetch) {
  if (first_vfetch > last_vfetch) {
    std::swap(first_vfetch, last_vfetch);
  }
  if (first_vfetch >= vertex_buffer_states_.size()) {
    return;
  }
  last_vfetch = std::min(last_vfetch, uint32_t(vertex_buffer_states_.size() - 1));
  for (uint32_t vfetch_index = first_vfetch; vfetch_index <= last_vfetch; ++vfetch_index) {
    InvalidateVertexBufferResidency(vfetch_index);
  }
}

void D3D12CommandProcessor::InitializeShaderStorage(const std::filesystem::path& cache_root,
                                                    uint32_t title_id, bool blocking) {
  CommandProcessor::InitializeShaderStorage(cache_root, title_id, blocking);
  pipeline_cache_->InitializeShaderStorage(cache_root, title_id, blocking);
}

bool D3D12CommandProcessor::ExecutePacketType3_EVENT_WRITE_ZPD(memory::RingBuffer* reader,
                                                               uint32_t packet, uint32_t count) {
  const uint32_t trace_address = register_file_->values[XE_GPU_REG_RB_SAMPLE_COUNT_ADDR];
  const auto* trace_counts =
      memory_->TranslatePhysical<xenos::xe_gpu_depth_sample_counts*>(trace_address);
  const auto trace = [&](const char* stage) {
    TraceOcclusionQuery(stage, trace_address, trace_counts,
                        REXCVAR_GET(occlusion_query_enable), occlusion_query_resources_available_,
                        active_occlusion_query_.valid, active_occlusion_query_.sample_count_address);
  };
  trace("packet");
  if (!REXCVAR_GET(occlusion_query_enable) || !occlusion_query_resources_available_) {
    const bool result = CommandProcessor::ExecutePacketType3_EVENT_WRITE_ZPD(reader, packet, count);
    trace("base-fallback-result");
    return result;
  }

  if (render_target_cache_->GetPath() == RenderTargetCache::Path::kPixelShaderInterlock) {
    assert_true(count == 1);
    const uint32_t initiator = reader->ReadAndSwap<uint32_t>();
    WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);
    auto* counts = memory_->TranslatePhysical<xenos::xe_gpu_depth_sample_counts*>(trace_address);
    const bool result = counts && WriteROVOcclusionSnapshot(counts);
    trace(result ? "rov-snapshot" : "rov-snapshot-failed");
    return result;
  }
  if (UseRTVOcclusionSnapshots()) {
    assert_true(count == 1);
    const uint32_t initiator = reader->ReadAndSwap<uint32_t>();
    WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);
    auto* counts = memory_->TranslatePhysical<xenos::xe_gpu_depth_sample_counts*>(trace_address);
    const bool result = counts && WriteRTVOcclusionSnapshot(counts);
    trace(result ? "rtv-snapshot" : "rtv-snapshot-failed");
    return result;
  }

  const uint32_t kQueryFinished = rex::byte_swap(0xFFFFFEED);
  assert_true(count == 1);
  uint32_t initiator = reader->ReadAndSwap<uint32_t>();
  WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);

  uint32_t sample_count_addr = register_file_->values[XE_GPU_REG_RB_SAMPLE_COUNT_ADDR];
  auto* sample_counts =
      memory_->TranslatePhysical<xenos::xe_gpu_depth_sample_counts*>(sample_count_addr);
  if (!sample_counts) {
    DisableHostOcclusionQueries();
    return true;
  }

  auto write_fallback_result = [sample_counts, kQueryFinished]() -> bool {
    auto fake_sample_count = REXCVAR_GET(query_occlusion_fake_sample_count);
    if (fake_sample_count < 0) {
      return true;
    }
    bool is_end_via_z_pass =
        sample_counts->ZPass_A == kQueryFinished || sample_counts->ZPass_B == kQueryFinished;
    bool is_end_via_z_fail =
        sample_counts->ZFail_A == kQueryFinished || sample_counts->ZFail_B == kQueryFinished;
    std::memset(sample_counts, 0, sizeof(xenos::xe_gpu_depth_sample_counts));
    if (is_end_via_z_pass || is_end_via_z_fail) {
      sample_counts->ZPass_A = fake_sample_count;
      sample_counts->Total_A = fake_sample_count;
    }
    return true;
  };

  bool is_end_via_z_pass =
      sample_counts->ZPass_A == kQueryFinished || sample_counts->ZPass_B == kQueryFinished;
  bool is_end_via_z_fail =
      sample_counts->ZFail_A == kQueryFinished || sample_counts->ZFail_B == kQueryFinished;
  bool is_end = is_end_via_z_pass || is_end_via_z_fail;

  if (!is_end) {
    if (active_occlusion_query_.valid &&
        active_occlusion_query_.sample_count_address != sample_count_addr) {
      trace("different-begin-address");
      DisableHostOcclusionQueries();
      return write_fallback_result();
    }
    if (!BeginGuestOcclusionQuery(sample_count_addr)) {
      trace("begin-failed");
      return write_fallback_result();
    }
    return true;
  }

  if (!active_occlusion_query_.valid ||
      active_occlusion_query_.sample_count_address != sample_count_addr) {
    trace("end-without-matching-begin");
    DisableHostOcclusionQueries();
    return write_fallback_result();
  }

  if (!EndGuestOcclusionQuery(sample_count_addr, sample_counts)) {
    trace("end-failed");
    return write_fallback_result();
  }

  trace("host-result");
  return true;
}

bool D3D12CommandProcessor::PushTransitionBarrier(ID3D12Resource* resource,
                                                  D3D12_RESOURCE_STATES old_state,
                                                  D3D12_RESOURCE_STATES new_state,
                                                  UINT subresource) {
  if (old_state == new_state) {
    return false;
  }
  D3D12_RESOURCE_BARRIER barrier;
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = resource;
  barrier.Transition.Subresource = subresource;
  barrier.Transition.StateBefore = old_state;
  barrier.Transition.StateAfter = new_state;
  barriers_.push_back(barrier);
  return true;
}

void D3D12CommandProcessor::PushAliasingBarrier(ID3D12Resource* old_resource,
                                                ID3D12Resource* new_resource) {
  D3D12_RESOURCE_BARRIER barrier;
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Aliasing.pResourceBefore = old_resource;
  barrier.Aliasing.pResourceAfter = new_resource;
  barriers_.push_back(barrier);
}

void D3D12CommandProcessor::PushUAVBarrier(ID3D12Resource* resource) {
  D3D12_RESOURCE_BARRIER barrier;
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.UAV.pResource = resource;
  barriers_.push_back(barrier);
}

void D3D12CommandProcessor::SubmitBarriers() {
  UINT barrier_count = UINT(barriers_.size());
  if (barrier_count != 0) {
    deferred_command_list_.D3DResourceBarrier(barrier_count, barriers_.data());
    barriers_.clear();
  }
}

ID3D12RootSignature* D3D12CommandProcessor::GetRootSignature(const DxbcShader* vertex_shader,
                                                             const DxbcShader* pixel_shader,
                                                             bool tessellated) {
  if (bindless_resources_used_) {
    return tessellated ? root_signature_bindless_ds_ : root_signature_bindless_vs_;
  }

  D3D12_SHADER_VISIBILITY vertex_visibility =
      tessellated ? D3D12_SHADER_VISIBILITY_DOMAIN : D3D12_SHADER_VISIBILITY_VERTEX;

  uint32_t texture_count_vertex =
      uint32_t(vertex_shader->GetTextureBindingsAfterTranslation().size());
  uint32_t sampler_count_vertex =
      uint32_t(vertex_shader->GetSamplerBindingsAfterTranslation().size());
  uint32_t texture_count_pixel =
      pixel_shader ? uint32_t(pixel_shader->GetTextureBindingsAfterTranslation().size()) : 0;
  uint32_t sampler_count_pixel =
      pixel_shader ? uint32_t(pixel_shader->GetSamplerBindingsAfterTranslation().size()) : 0;

  // Better put the pixel texture/sampler in the lower bits probably because it
  // changes often.
  uint32_t index = 0;
  uint32_t index_offset = 0;
  index |= texture_count_pixel << index_offset;
  index_offset += D3D12Shader::kMaxTextureBindingIndexBits;
  index |= sampler_count_pixel << index_offset;
  index_offset += D3D12Shader::kMaxSamplerBindingIndexBits;
  index |= texture_count_vertex << index_offset;
  index_offset += D3D12Shader::kMaxTextureBindingIndexBits;
  index |= sampler_count_vertex << index_offset;
  index_offset += D3D12Shader::kMaxSamplerBindingIndexBits;
  index |= uint32_t(vertex_visibility == D3D12_SHADER_VISIBILITY_DOMAIN) << index_offset;
  ++index_offset;
  assert_true(index_offset <= 32);

  // Try an existing root signature.
  auto it = root_signatures_bindful_.find(index);
  if (it != root_signatures_bindful_.end()) {
    return it->second;
  }

  // Create a new one.
  D3D12_ROOT_SIGNATURE_DESC desc;
  D3D12_ROOT_PARAMETER parameters[kRootParameter_Bindful_Count_Max];
  desc.NumParameters = kRootParameter_Bindful_Count_Base;
  desc.pParameters = parameters;
  desc.NumStaticSamplers = 0;
  desc.pStaticSamplers = nullptr;
  desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  // Base parameters.

  // Fetch constants.
  {
    auto& parameter = parameters[kRootParameter_Bindful_FetchConstants];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter.Descriptor.ShaderRegister =
        uint32_t(DxbcShaderTranslator::CbufferRegister::kFetchConstants);
    parameter.Descriptor.RegisterSpace = 0;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }

  // Vertex float constants.
  {
    auto& parameter = parameters[kRootParameter_Bindful_FloatConstantsVertex];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter.Descriptor.ShaderRegister =
        uint32_t(DxbcShaderTranslator::CbufferRegister::kFloatConstants);
    parameter.Descriptor.RegisterSpace = 0;
    parameter.ShaderVisibility = vertex_visibility;
  }

  // Pixel float constants.
  {
    auto& parameter = parameters[kRootParameter_Bindful_FloatConstantsPixel];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter.Descriptor.ShaderRegister =
        uint32_t(DxbcShaderTranslator::CbufferRegister::kFloatConstants);
    parameter.Descriptor.RegisterSpace = 0;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  }

  // System constants.
  {
    auto& parameter = parameters[kRootParameter_Bindful_SystemConstants];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter.Descriptor.ShaderRegister =
        uint32_t(DxbcShaderTranslator::CbufferRegister::kSystemConstants);
    parameter.Descriptor.RegisterSpace = 0;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }

  // Bool and loop constants.
  {
    auto& parameter = parameters[kRootParameter_Bindful_BoolLoopConstants];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter.Descriptor.ShaderRegister =
        uint32_t(DxbcShaderTranslator::CbufferRegister::kBoolLoopConstants);
    parameter.Descriptor.RegisterSpace = 0;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }

  // Shared memory and, if ROVs are used, EDRAM.
  D3D12_DESCRIPTOR_RANGE shared_memory_and_edram_ranges[4];
  {
    auto& parameter = parameters[kRootParameter_Bindful_SharedMemoryAndEdram];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 2;
    parameter.DescriptorTable.pDescriptorRanges = shared_memory_and_edram_ranges;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    shared_memory_and_edram_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    shared_memory_and_edram_ranges[0].NumDescriptors = 1;
    shared_memory_and_edram_ranges[0].BaseShaderRegister =
        uint32_t(DxbcShaderTranslator::SRVMainRegister::kSharedMemory);
    shared_memory_and_edram_ranges[0].RegisterSpace =
        uint32_t(DxbcShaderTranslator::SRVSpace::kMain);
    shared_memory_and_edram_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    shared_memory_and_edram_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    shared_memory_and_edram_ranges[1].NumDescriptors = 1;
    shared_memory_and_edram_ranges[1].BaseShaderRegister =
        UINT(DxbcShaderTranslator::UAVRegister::kSharedMemory);
    shared_memory_and_edram_ranges[1].RegisterSpace = 0;
    shared_memory_and_edram_ranges[1].OffsetInDescriptorsFromTableStart = 1;
    if (render_target_cache_->GetPath() == RenderTargetCache::Path::kPixelShaderInterlock) {
      ++parameter.DescriptorTable.NumDescriptorRanges;
      shared_memory_and_edram_ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
      shared_memory_and_edram_ranges[2].NumDescriptors = 1;
      shared_memory_and_edram_ranges[2].BaseShaderRegister =
          UINT(DxbcShaderTranslator::UAVRegister::kEdram);
      shared_memory_and_edram_ranges[2].RegisterSpace = 0;
      shared_memory_and_edram_ranges[2].OffsetInDescriptorsFromTableStart = 2;
      ++parameter.DescriptorTable.NumDescriptorRanges;
      shared_memory_and_edram_ranges[3] = shared_memory_and_edram_ranges[2];
      shared_memory_and_edram_ranges[3].BaseShaderRegister =
          UINT(DxbcShaderTranslator::UAVRegister::kOcclusionCounter);
      shared_memory_and_edram_ranges[3].OffsetInDescriptorsFromTableStart = 3;
    }
  }

  // Extra parameters.

  // Pixel textures.
  D3D12_DESCRIPTOR_RANGE range_textures_pixel;
  if (texture_count_pixel > 0) {
    auto& parameter = parameters[desc.NumParameters];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range_textures_pixel;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    range_textures_pixel.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range_textures_pixel.NumDescriptors = texture_count_pixel;
    range_textures_pixel.BaseShaderRegister =
        uint32_t(DxbcShaderTranslator::SRVMainRegister::kBindfulTexturesStart);
    range_textures_pixel.RegisterSpace = uint32_t(DxbcShaderTranslator::SRVSpace::kMain);
    range_textures_pixel.OffsetInDescriptorsFromTableStart = 0;
    ++desc.NumParameters;
  }

  // Pixel samplers.
  D3D12_DESCRIPTOR_RANGE range_samplers_pixel;
  if (sampler_count_pixel > 0) {
    auto& parameter = parameters[desc.NumParameters];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range_samplers_pixel;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    range_samplers_pixel.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    range_samplers_pixel.NumDescriptors = sampler_count_pixel;
    range_samplers_pixel.BaseShaderRegister = 0;
    range_samplers_pixel.RegisterSpace = 0;
    range_samplers_pixel.OffsetInDescriptorsFromTableStart = 0;
    ++desc.NumParameters;
  }

  // Vertex textures.
  D3D12_DESCRIPTOR_RANGE range_textures_vertex;
  if (texture_count_vertex > 0) {
    auto& parameter = parameters[desc.NumParameters];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range_textures_vertex;
    parameter.ShaderVisibility = vertex_visibility;
    range_textures_vertex.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range_textures_vertex.NumDescriptors = texture_count_vertex;
    range_textures_vertex.BaseShaderRegister =
        uint32_t(DxbcShaderTranslator::SRVMainRegister::kBindfulTexturesStart);
    range_textures_vertex.RegisterSpace = uint32_t(DxbcShaderTranslator::SRVSpace::kMain);
    range_textures_vertex.OffsetInDescriptorsFromTableStart = 0;
    ++desc.NumParameters;
  }

  // Vertex samplers.
  D3D12_DESCRIPTOR_RANGE range_samplers_vertex;
  if (sampler_count_vertex > 0) {
    auto& parameter = parameters[desc.NumParameters];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range_samplers_vertex;
    parameter.ShaderVisibility = vertex_visibility;
    range_samplers_vertex.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    range_samplers_vertex.NumDescriptors = sampler_count_vertex;
    range_samplers_vertex.BaseShaderRegister = 0;
    range_samplers_vertex.RegisterSpace = 0;
    range_samplers_vertex.OffsetInDescriptorsFromTableStart = 0;
    ++desc.NumParameters;
  }

  ID3D12RootSignature* root_signature =
      ui::d3d12::util::CreateRootSignature(GetD3D12Provider(), desc);
  if (root_signature == nullptr) {
    REXGPU_ERROR(
        "Failed to create a root signature with {} pixel textures, {} pixel "
        "samplers, {} vertex textures and {} vertex samplers",
        texture_count_pixel, sampler_count_pixel, texture_count_vertex, sampler_count_vertex);
    return nullptr;
  }
  root_signatures_bindful_.emplace(index, root_signature);
  return root_signature;
}

uint32_t D3D12CommandProcessor::GetRootBindfulExtraParameterIndices(
    const DxbcShader* vertex_shader, const DxbcShader* pixel_shader,
    RootBindfulExtraParameterIndices& indices_out) {
  uint32_t index = kRootParameter_Bindful_Count_Base;
  if (pixel_shader && !pixel_shader->GetTextureBindingsAfterTranslation().empty()) {
    indices_out.textures_pixel = index++;
  } else {
    indices_out.textures_pixel = RootBindfulExtraParameterIndices::kUnavailable;
  }
  if (pixel_shader && !pixel_shader->GetSamplerBindingsAfterTranslation().empty()) {
    indices_out.samplers_pixel = index++;
  } else {
    indices_out.samplers_pixel = RootBindfulExtraParameterIndices::kUnavailable;
  }
  if (!vertex_shader->GetTextureBindingsAfterTranslation().empty()) {
    indices_out.textures_vertex = index++;
  } else {
    indices_out.textures_vertex = RootBindfulExtraParameterIndices::kUnavailable;
  }
  if (!vertex_shader->GetSamplerBindingsAfterTranslation().empty()) {
    indices_out.samplers_vertex = index++;
  } else {
    indices_out.samplers_vertex = RootBindfulExtraParameterIndices::kUnavailable;
  }
  return index;
}

uint64_t D3D12CommandProcessor::RequestViewBindfulDescriptors(
    uint64_t previous_heap_index, uint32_t count_for_partial_update, uint32_t count_for_full_update,
    D3D12_CPU_DESCRIPTOR_HANDLE& cpu_handle_out, D3D12_GPU_DESCRIPTOR_HANDLE& gpu_handle_out) {
  assert_false(bindless_resources_used_);
  assert_true(submission_open_);
  uint32_t descriptor_index;
  uint64_t current_heap_index = view_bindful_heap_pool_->Request(
      frame_current_, previous_heap_index, count_for_partial_update, count_for_full_update,
      descriptor_index);
  if (current_heap_index == ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid) {
    // There was an error.
    return ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid;
  }
  ID3D12DescriptorHeap* heap = view_bindful_heap_pool_->GetLastRequestHeap();
  if (view_bindful_heap_current_ != heap) {
    view_bindful_heap_current_ = heap;
    deferred_command_list_.SetDescriptorHeaps(view_bindful_heap_current_,
                                              sampler_bindful_heap_current_);
  }
  const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
  cpu_handle_out = provider.OffsetViewDescriptor(
      view_bindful_heap_pool_->GetLastRequestHeapCPUStart(), descriptor_index);
  gpu_handle_out = provider.OffsetViewDescriptor(
      view_bindful_heap_pool_->GetLastRequestHeapGPUStart(), descriptor_index);
  return current_heap_index;
}

uint32_t D3D12CommandProcessor::RequestPersistentViewBindlessDescriptor() {
  assert_true(bindless_resources_used_);
  if (!view_bindless_heap_free_.empty()) {
    uint32_t descriptor_index = view_bindless_heap_free_.back();
    view_bindless_heap_free_.pop_back();
    return descriptor_index;
  }
  if (view_bindless_heap_allocated_ >= kViewBindlessHeapSize) {
    return UINT32_MAX;
  }
  return view_bindless_heap_allocated_++;
}

void D3D12CommandProcessor::ReleaseViewBindlessDescriptorImmediately(uint32_t descriptor_index) {
  assert_true(bindless_resources_used_);
  view_bindless_heap_free_.push_back(descriptor_index);
}

bool D3D12CommandProcessor::RequestOneUseSingleViewDescriptors(
    uint32_t count, ui::d3d12::util::DescriptorCpuGpuHandlePair* handles_out) {
  assert_true(submission_open_);
  if (!count) {
    return true;
  }
  assert_not_null(handles_out);
  const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
  if (bindless_resources_used_) {
    // Request separate bindless descriptors that will be freed when this
    // submission is completed by the GPU.
    if (count >
        kViewBindlessHeapSize - view_bindless_heap_allocated_ + view_bindless_heap_free_.size()) {
      return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t descriptor_index;
      if (!view_bindless_heap_free_.empty()) {
        descriptor_index = view_bindless_heap_free_.back();
        view_bindless_heap_free_.pop_back();
      } else {
        descriptor_index = view_bindless_heap_allocated_++;
      }
      view_bindless_one_use_descriptors_.push_back(
          std::make_pair(descriptor_index, submission_current_));
      handles_out[i] = std::make_pair(
          provider.OffsetViewDescriptor(view_bindless_heap_cpu_start_, descriptor_index),
          provider.OffsetViewDescriptor(view_bindless_heap_gpu_start_, descriptor_index));
    }
  } else {
    // Request a range within the current heap for bindful resources path.
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle_start;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_start;
    if (RequestViewBindfulDescriptors(ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid, count,
                                      count, cpu_handle_start, gpu_handle_start) ==
        ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid) {
      return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
      handles_out[i] = std::make_pair(provider.OffsetViewDescriptor(cpu_handle_start, i),
                                      provider.OffsetViewDescriptor(gpu_handle_start, i));
    }
  }
  return true;
}

ui::d3d12::util::DescriptorCpuGpuHandlePair D3D12CommandProcessor::GetSystemBindlessViewHandlePair(
    SystemBindlessView view) const {
  assert_true(bindless_resources_used_);
  const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
  return std::make_pair(
      provider.OffsetViewDescriptor(view_bindless_heap_cpu_start_, uint32_t(view)),
      provider.OffsetViewDescriptor(view_bindless_heap_gpu_start_, uint32_t(view)));
}

ui::d3d12::util::DescriptorCpuGpuHandlePair
D3D12CommandProcessor::GetSharedMemoryUintPow2BindlessSRVHandlePair(
    uint32_t element_size_bytes_pow2) const {
  SystemBindlessView view;
  switch (element_size_bytes_pow2) {
    case 2:
      view = SystemBindlessView::kSharedMemoryR32UintSRV;
      break;
    case 3:
      view = SystemBindlessView::kSharedMemoryR32G32UintSRV;
      break;
    case 4:
      view = SystemBindlessView::kSharedMemoryR32G32B32A32UintSRV;
      break;
    default:
      assert_unhandled_case(element_size_bytes_pow2);
      view = SystemBindlessView::kSharedMemoryR32UintSRV;
  }
  return GetSystemBindlessViewHandlePair(view);
}

ui::d3d12::util::DescriptorCpuGpuHandlePair
D3D12CommandProcessor::GetSharedMemoryUintPow2BindlessUAVHandlePair(
    uint32_t element_size_bytes_pow2) const {
  SystemBindlessView view;
  switch (element_size_bytes_pow2) {
    case 2:
      view = SystemBindlessView::kSharedMemoryR32UintUAV;
      break;
    case 3:
      view = SystemBindlessView::kSharedMemoryR32G32UintUAV;
      break;
    case 4:
      view = SystemBindlessView::kSharedMemoryR32G32B32A32UintUAV;
      break;
    default:
      assert_unhandled_case(element_size_bytes_pow2);
      view = SystemBindlessView::kSharedMemoryR32UintUAV;
  }
  return GetSystemBindlessViewHandlePair(view);
}

ui::d3d12::util::DescriptorCpuGpuHandlePair
D3D12CommandProcessor::GetEdramUintPow2BindlessSRVHandlePair(
    uint32_t element_size_bytes_pow2) const {
  SystemBindlessView view;
  switch (element_size_bytes_pow2) {
    case 2:
      view = SystemBindlessView::kEdramR32UintSRV;
      break;
    case 3:
      view = SystemBindlessView::kEdramR32G32UintSRV;
      break;
    case 4:
      view = SystemBindlessView::kEdramR32G32B32A32UintSRV;
      break;
    default:
      assert_unhandled_case(element_size_bytes_pow2);
      view = SystemBindlessView::kEdramR32UintSRV;
  }
  return GetSystemBindlessViewHandlePair(view);
}

ui::d3d12::util::DescriptorCpuGpuHandlePair
D3D12CommandProcessor::GetEdramUintPow2BindlessUAVHandlePair(
    uint32_t element_size_bytes_pow2) const {
  SystemBindlessView view;
  switch (element_size_bytes_pow2) {
    case 2:
      view = SystemBindlessView::kEdramR32UintUAV;
      break;
    case 3:
      view = SystemBindlessView::kEdramR32G32UintUAV;
      break;
    case 4:
      view = SystemBindlessView::kEdramR32G32B32A32UintUAV;
      break;
    default:
      assert_unhandled_case(element_size_bytes_pow2);
      view = SystemBindlessView::kEdramR32UintUAV;
  }
  return GetSystemBindlessViewHandlePair(view);
}

uint64_t D3D12CommandProcessor::RequestSamplerBindfulDescriptors(
    uint64_t previous_heap_index, uint32_t count_for_partial_update, uint32_t count_for_full_update,
    D3D12_CPU_DESCRIPTOR_HANDLE& cpu_handle_out, D3D12_GPU_DESCRIPTOR_HANDLE& gpu_handle_out) {
  assert_false(bindless_resources_used_);
  assert_true(submission_open_);
  uint32_t descriptor_index;
  uint64_t current_heap_index = sampler_bindful_heap_pool_->Request(
      frame_current_, previous_heap_index, count_for_partial_update, count_for_full_update,
      descriptor_index);
  if (current_heap_index == ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid) {
    // There was an error.
    return ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid;
  }
  ID3D12DescriptorHeap* heap = sampler_bindful_heap_pool_->GetLastRequestHeap();
  if (sampler_bindful_heap_current_ != heap) {
    sampler_bindful_heap_current_ = heap;
    deferred_command_list_.SetDescriptorHeaps(view_bindful_heap_current_,
                                              sampler_bindful_heap_current_);
  }
  const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
  cpu_handle_out = provider.OffsetSamplerDescriptor(
      sampler_bindful_heap_pool_->GetLastRequestHeapCPUStart(), descriptor_index);
  gpu_handle_out = provider.OffsetSamplerDescriptor(
      sampler_bindful_heap_pool_->GetLastRequestHeapGPUStart(), descriptor_index);
  return current_heap_index;
}

ID3D12Resource* D3D12CommandProcessor::RequestScratchGPUBuffer(uint32_t size,
                                                               D3D12_RESOURCE_STATES state) {
  assert_true(submission_open_);
  assert_false(scratch_buffer_used_);
  if (!submission_open_ || scratch_buffer_used_ || size == 0) {
    return nullptr;
  }

  if (size <= scratch_buffer_size_) {
    PushTransitionBarrier(scratch_buffer_, scratch_buffer_state_, state);
    scratch_buffer_state_ = state;
    scratch_buffer_used_ = true;
    return scratch_buffer_;
  }

  size = rex::align(size, kScratchBufferSizeIncrement);

  const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
  ID3D12Device* device = provider.GetDevice();
  D3D12_RESOURCE_DESC buffer_desc;
  ui::d3d12::util::FillBufferResourceDesc(buffer_desc, size,
                                          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  ID3D12Resource* buffer;
  if (FAILED(device->CreateCommittedResource(&ui::d3d12::util::kHeapPropertiesDefault,
                                             provider.GetHeapFlagCreateNotZeroed(), &buffer_desc,
                                             state, nullptr, IID_PPV_ARGS(&buffer)))) {
    REXGPU_ERROR("Failed to create a {} MB scratch GPU buffer", size >> 20);
    return nullptr;
  }
  if (scratch_buffer_ != nullptr) {
    resources_for_deletion_.emplace_back(submission_current_, scratch_buffer_);
  }
  scratch_buffer_ = buffer;
  scratch_buffer_size_ = size;
  scratch_buffer_state_ = state;
  scratch_buffer_used_ = true;
  return scratch_buffer_;
}

void D3D12CommandProcessor::ReleaseScratchGPUBuffer(ID3D12Resource* buffer,
                                                    D3D12_RESOURCE_STATES new_state) {
  assert_true(submission_open_);
  assert_true(scratch_buffer_used_);
  scratch_buffer_used_ = false;
  if (buffer == scratch_buffer_) {
    scratch_buffer_state_ = new_state;
  }
}

void D3D12CommandProcessor::SetExternalPipeline(ID3D12PipelineState* pipeline) {
  if (current_external_pipeline_ != pipeline) {
    current_external_pipeline_ = pipeline;
    current_guest_pipeline_ = nullptr;
    deferred_command_list_.D3DSetPipelineState(pipeline);
  }
}

void D3D12CommandProcessor::InvalidateCommandListState() {
  current_guest_pipeline_ = nullptr;
  current_external_pipeline_ = nullptr;
  current_graphics_root_signature_ = nullptr;
  current_graphics_root_up_to_date_ = 0;
}

void D3D12CommandProcessor::SetExternalGraphicsRootSignature(ID3D12RootSignature* root_signature) {
  if (current_graphics_root_signature_ != root_signature) {
    current_graphics_root_signature_ = root_signature;
    deferred_command_list_.D3DSetGraphicsRootSignature(root_signature);
  }
  // Force-invalidate because setting a non-guest root signature.
  current_graphics_root_up_to_date_ = 0;
}

void D3D12CommandProcessor::SetViewport(const D3D12_VIEWPORT& viewport) {
  ff_viewport_update_needed_ |= ff_viewport_.TopLeftX != viewport.TopLeftX;
  ff_viewport_update_needed_ |= ff_viewport_.TopLeftY != viewport.TopLeftY;
  ff_viewport_update_needed_ |= ff_viewport_.Width != viewport.Width;
  ff_viewport_update_needed_ |= ff_viewport_.Height != viewport.Height;
  ff_viewport_update_needed_ |= ff_viewport_.MinDepth != viewport.MinDepth;
  ff_viewport_update_needed_ |= ff_viewport_.MaxDepth != viewport.MaxDepth;
  if (ff_viewport_update_needed_) {
    ff_viewport_ = viewport;
    deferred_command_list_.RSSetViewport(ff_viewport_);
    ff_viewport_update_needed_ = false;
  }
}

void D3D12CommandProcessor::SetScissorRect(const D3D12_RECT& scissor_rect) {
  ff_scissor_update_needed_ |= ff_scissor_.left != scissor_rect.left;
  ff_scissor_update_needed_ |= ff_scissor_.top != scissor_rect.top;
  ff_scissor_update_needed_ |= ff_scissor_.right != scissor_rect.right;
  ff_scissor_update_needed_ |= ff_scissor_.bottom != scissor_rect.bottom;
  if (ff_scissor_update_needed_) {
    ff_scissor_ = scissor_rect;
    deferred_command_list_.RSSetScissorRect(ff_scissor_);
    ff_scissor_update_needed_ = false;
  }
}

void D3D12CommandProcessor::SetStencilReference(uint32_t stencil_ref) {
  ff_stencil_ref_update_needed_ |= ff_stencil_ref_ != stencil_ref;
  if (ff_stencil_ref_update_needed_) {
    ff_stencil_ref_ = stencil_ref;
    deferred_command_list_.D3DOMSetStencilRef(stencil_ref);
    ff_stencil_ref_update_needed_ = false;
  }
}

void D3D12CommandProcessor::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY primitive_topology) {
  if (primitive_topology_ != primitive_topology) {
    primitive_topology_ = primitive_topology;
    deferred_command_list_.D3DIASetPrimitiveTopology(primitive_topology);
  }
}

std::string D3D12CommandProcessor::GetWindowTitleText() const {
  std::ostringstream title;
  title << "Direct3D 12";
  if (render_target_cache_) {
    // Rasterizer-ordered views are a feature very rarely used as of 2020 and
    // that faces adoption complications (outside of Direct3D - on Vulkan - at
    // least), but crucial to Xenia - raise awareness of its usage.
    // https://github.com/KhronosGroup/Vulkan-Ecosystem/issues/27#issuecomment-455712319
    // "In Xenia's title bar "D3D12 ROV" can be seen, which was a surprise, as I
    //  wasn't aware that Xenia D3D12 backend was using Raster Order Views
    //  feature" - oscarbg in that issue.
    switch (render_target_cache_->GetPath()) {
      case RenderTargetCache::Path::kHostRenderTargets:
        title << " - RTV/DSV";
        break;
      case RenderTargetCache::Path::kPixelShaderInterlock:
        title << " - ROV";
        break;
      default:
        break;
    }
    uint32_t draw_resolution_scale_x =
        texture_cache_ ? texture_cache_->draw_resolution_scale_x() : 1;
    uint32_t draw_resolution_scale_y =
        texture_cache_ ? texture_cache_->draw_resolution_scale_y() : 1;
    if (draw_resolution_scale_x > 1 || draw_resolution_scale_y > 1) {
      title << ' ' << draw_resolution_scale_x << 'x' << draw_resolution_scale_y;
    }
  }
  return title.str();
}

bool D3D12CommandProcessor::SetupContext() {
  if (!CommandProcessor::SetupContext()) {
    REXGPU_ERROR("Failed to initialize base command processor context");
    return false;
  }
  InvalidateAllVertexBufferResidency();
  UpdateDebugMarkersEnabled();
  if (FsrUpscaler::Available()) {
    REXGPU_INFO("FSR {}: linked", FsrUpscaler::Version());
    FsrUpscaler::SetMessageSink([](bool error, const std::string& message) {
      if (error) {
        REXGPU_ERROR("{}", message);
      } else {
        REXGPU_WARN("{}", message);
      }
    });
  }

  const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
  ID3D12Device* device = provider.GetDevice();
  ID3D12CommandQueue* direct_queue = provider.GetDirectQueue();

  InitializeDeferredConstants();
  if (REXCVAR_GET(d3d12_submit_thread)) {
    submit_shutdown_ = false;
    submit_async_swap_ = REXCVAR_GET(d3d12_submit_thread_async_swap);
    submit_thread_ = std::thread([this]() { SubmitThreadMain(); });
    if (submit_async_swap_) {
      present_shutdown_ = false;
      present_in_flight_ = false;
      present_thread_ = std::thread([this]() { PresentThreadMain(); });
    }
  }
  if (REXCVAR_GET(d3d12_readback_publish_thread)) {
    readback_publish_await_signals_ = REXCVAR_GET(d3d12_readback_publish_await_signals);
    readback_publish_shutdown_ = false;
    readback_publish_thread_ = std::thread([this]() { ReadbackPublishThreadMain(); });
  }

  fence_completion_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  if (fence_completion_event_ == nullptr) {
    REXGPU_ERROR("Failed to create the fence completion event");
    return false;
  }
  if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&submission_fence_)))) {
    REXGPU_ERROR("Failed to create the submission fence");
    return false;
  }
  if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                 IID_PPV_ARGS(&queue_operations_since_submission_fence_)))) {
    REXGPU_ERROR(
        "Failed to create the fence for awaiting queue operations done since "
        "the latest submission");
    return false;
  }

  // Create the command list and one allocator because it's needed for a command
  // list.
  ID3D12CommandAllocator* command_allocator;
  if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_PPV_ARGS(&command_allocator)))) {
    REXGPU_ERROR("Failed to create a command allocator");
    return false;
  }
  command_allocator_writable_first_ = new CommandAllocator;
  command_allocator_writable_first_->command_allocator = command_allocator;
  command_allocator_writable_first_->last_usage_submission = 0;
  command_allocator_writable_first_->next = nullptr;
  command_allocator_writable_last_ = command_allocator_writable_first_;
  if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator,
                                       nullptr, IID_PPV_ARGS(&command_list_)))) {
    REXGPU_ERROR("Failed to create the graphics command list");
    return false;
  }
  // Initially in open state, wait until a deferred command list submission.
  command_list_->Close();
  // Optional - added in Creators Update (SDK 10.0.15063.0).
  command_list_->QueryInterface(IID_PPV_ARGS(&command_list_1_));

  bindless_resources_used_ = REXCVAR_GET(d3d12_bindless) &&
                             provider.GetResourceBindingTier() >= D3D12_RESOURCE_BINDING_TIER_2;

  // Get the draw resolution scale for the render target cache and the texture
  // cache.
  uint32_t draw_resolution_scale_x, draw_resolution_scale_y;
  bool draw_resolution_scale_not_clamped =
      TextureCache::GetConfigDrawResolutionScale(draw_resolution_scale_x, draw_resolution_scale_y);
  if (!D3D12TextureCache::ClampDrawResolutionScaleToMaxSupported(
          draw_resolution_scale_x, draw_resolution_scale_y, provider)) {
    draw_resolution_scale_not_clamped = false;
  }
  if (!draw_resolution_scale_not_clamped) {
    REXGPU_WARN(
        "The requested draw resolution scale is not supported by the device or "
        "the emulator, reducing to {}x{}",
        draw_resolution_scale_x, draw_resolution_scale_y);
  }

  shared_memory_ = std::make_unique<D3D12SharedMemory>(*this, *memory_);
  if (!shared_memory_->Initialize()) {
    REXGPU_ERROR("Failed to initialize shared memory");
    return false;
  }

  // Initialize the render target cache before configuring binding - need to
  // know if using rasterizer-ordered views for the bindless root signature.
  render_target_cache_ = std::make_unique<D3D12RenderTargetCache>(
      *register_file_, *memory_, draw_resolution_scale_x, draw_resolution_scale_y, *this,
      bindless_resources_used_);
  if (!render_target_cache_->Initialize()) {
    REXGPU_ERROR("Failed to initialize the render target cache");
    return false;
  }

  // Not with the ROV path (the reference keeps its readback, AGENTS.md) or
  // without readbacks.
  if (REXCVAR_GET(readback_resolve_on_demand) &&
      render_target_cache_->GetPath() != RenderTargetCache::Path::kPixelShaderInterlock &&
      GetReadbackResolveMode(REXCVAR_GET(d3d12_readback_resolve)) !=
          ReadbackResolveMode::kDisabled) {
    demand_servicer_ = new DemandServicer(*this);
  }
  read_trace::Configure(*memory_, demand_servicer_);

  // Initialize resource binding.
  constant_buffer_pool_ = std::make_unique<ui::d3d12::D3D12UploadBufferPool>(
      provider, std::max(ui::d3d12::D3D12UploadBufferPool::kDefaultPageSize,
                         sizeof(float) * 4 * D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT));
  if (bindless_resources_used_) {
    D3D12_DESCRIPTOR_HEAP_DESC view_bindless_heap_desc;
    view_bindless_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    view_bindless_heap_desc.NumDescriptors = kViewBindlessHeapSize;
    view_bindless_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    view_bindless_heap_desc.NodeMask = 0;
    if (FAILED(device->CreateDescriptorHeap(&view_bindless_heap_desc,
                                            IID_PPV_ARGS(&view_bindless_heap_)))) {
      REXGPU_ERROR("Failed to create the bindless CBV/SRV/UAV descriptor heap");
      return false;
    }
    view_bindless_heap_cpu_start_ = view_bindless_heap_->GetCPUDescriptorHandleForHeapStart();
    view_bindless_heap_gpu_start_ = view_bindless_heap_->GetGPUDescriptorHandleForHeapStart();
    view_bindless_heap_allocated_ = uint32_t(SystemBindlessView::kCount);

    D3D12_DESCRIPTOR_HEAP_DESC sampler_bindless_heap_desc;
    sampler_bindless_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    sampler_bindless_heap_desc.NumDescriptors = kSamplerHeapSize;
    sampler_bindless_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    sampler_bindless_heap_desc.NodeMask = 0;
    if (FAILED(device->CreateDescriptorHeap(&sampler_bindless_heap_desc,
                                            IID_PPV_ARGS(&sampler_bindless_heap_current_)))) {
      REXGPU_ERROR("Failed to create the bindless sampler descriptor heap");
      return false;
    }
    sampler_bindless_heap_cpu_start_ =
        sampler_bindless_heap_current_->GetCPUDescriptorHandleForHeapStart();
    sampler_bindless_heap_gpu_start_ =
        sampler_bindless_heap_current_->GetGPUDescriptorHandleForHeapStart();
    sampler_bindless_heap_allocated_ = 0;
  } else {
    view_bindful_heap_pool_ = std::make_unique<ui::d3d12::D3D12DescriptorHeapPool>(
        device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kViewBindfulHeapSize);
    sampler_bindful_heap_pool_ = std::make_unique<ui::d3d12::D3D12DescriptorHeapPool>(
        device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, kSamplerHeapSize);
  }

  if (bindless_resources_used_) {
    // Global bindless resource root signatures.
    // No CBV or UAV descriptor ranges with any descriptors to be allocated
    // dynamically (via RequestPersistentViewBindlessDescriptor or
    // RequestOneUseSingleViewDescriptors) should be here, because they would
    // overlap the unbounded SRV range, which is not allowed on Nvidia Fermi!
    D3D12_ROOT_SIGNATURE_DESC root_signature_bindless_desc;
    D3D12_ROOT_PARAMETER
    root_parameters_bindless[kRootParameter_Bindless_Count];
    root_signature_bindless_desc.NumParameters = kRootParameter_Bindless_Count;
    root_signature_bindless_desc.pParameters = root_parameters_bindless;
    root_signature_bindless_desc.NumStaticSamplers = 0;
    root_signature_bindless_desc.pStaticSamplers = nullptr;
    root_signature_bindless_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    // Fetch constants.
    {
      auto& parameter = root_parameters_bindless[kRootParameter_Bindless_FetchConstants];
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
      parameter.Descriptor.ShaderRegister =
          uint32_t(DxbcShaderTranslator::CbufferRegister::kFetchConstants);
      parameter.Descriptor.RegisterSpace = 0;
      parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    // Vertex float constants.
    {
      auto& parameter = root_parameters_bindless[kRootParameter_Bindless_FloatConstantsVertex];
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
      parameter.Descriptor.ShaderRegister =
          uint32_t(DxbcShaderTranslator::CbufferRegister::kFloatConstants);
      parameter.Descriptor.RegisterSpace = 0;
      parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    }
    // Pixel float constants.
    {
      auto& parameter = root_parameters_bindless[kRootParameter_Bindless_FloatConstantsPixel];
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
      parameter.Descriptor.ShaderRegister =
          uint32_t(DxbcShaderTranslator::CbufferRegister::kFloatConstants);
      parameter.Descriptor.RegisterSpace = 0;
      parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    // Pixel shader descriptor indices.
    {
      auto& parameter = root_parameters_bindless[kRootParameter_Bindless_DescriptorIndicesPixel];
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
      parameter.Descriptor.ShaderRegister =
          uint32_t(DxbcShaderTranslator::CbufferRegister::kDescriptorIndices);
      parameter.Descriptor.RegisterSpace = 0;
      parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    // Vertex shader descriptor indices.
    {
      auto& parameter = root_parameters_bindless[kRootParameter_Bindless_DescriptorIndicesVertex];
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
      parameter.Descriptor.ShaderRegister =
          uint32_t(DxbcShaderTranslator::CbufferRegister::kDescriptorIndices);
      parameter.Descriptor.RegisterSpace = 0;
      parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    }
    // System constants.
    {
      auto& parameter = root_parameters_bindless[kRootParameter_Bindless_SystemConstants];
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
      parameter.Descriptor.ShaderRegister =
          uint32_t(DxbcShaderTranslator::CbufferRegister::kSystemConstants);
      parameter.Descriptor.RegisterSpace = 0;
      parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    // Bool and loop constants.
    {
      auto& parameter = root_parameters_bindless[kRootParameter_Bindless_BoolLoopConstants];
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
      parameter.Descriptor.ShaderRegister =
          uint32_t(DxbcShaderTranslator::CbufferRegister::kBoolLoopConstants);
      parameter.Descriptor.RegisterSpace = 0;
      parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    // Shared memory SRV and UAV.
    D3D12_DESCRIPTOR_RANGE root_shared_memory_view_ranges[2];
    {
      auto& parameter = root_parameters_bindless[kRootParameter_Bindless_SharedMemory];
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameter.DescriptorTable.NumDescriptorRanges =
          uint32_t(rex::countof(root_shared_memory_view_ranges));
      parameter.DescriptorTable.pDescriptorRanges = root_shared_memory_view_ranges;
      parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
      {
        auto& range = root_shared_memory_view_ranges[0];
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        range.BaseShaderRegister = UINT(DxbcShaderTranslator::SRVMainRegister::kSharedMemory);
        range.RegisterSpace = UINT(DxbcShaderTranslator::SRVSpace::kMain);
        range.OffsetInDescriptorsFromTableStart = 0;
      }
      {
        auto& range = root_shared_memory_view_ranges[1];
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        range.NumDescriptors = 1;
        range.BaseShaderRegister = UINT(DxbcShaderTranslator::UAVRegister::kSharedMemory);
        range.RegisterSpace = 0;
        range.OffsetInDescriptorsFromTableStart = 1;
      }
    }
    // Sampler heap.
    D3D12_DESCRIPTOR_RANGE root_bindless_sampler_range;
    {
      auto& parameter = root_parameters_bindless[kRootParameter_Bindless_SamplerHeap];
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      // Will be appending.
      parameter.DescriptorTable.NumDescriptorRanges = 1;
      parameter.DescriptorTable.pDescriptorRanges = &root_bindless_sampler_range;
      parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
      root_bindless_sampler_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
      root_bindless_sampler_range.NumDescriptors = UINT_MAX;
      root_bindless_sampler_range.BaseShaderRegister = 0;
      root_bindless_sampler_range.RegisterSpace = 0;
      root_bindless_sampler_range.OffsetInDescriptorsFromTableStart = 0;
    }
    // View heap.
    D3D12_DESCRIPTOR_RANGE root_bindless_view_ranges[6];
    {
      auto& parameter = root_parameters_bindless[kRootParameter_Bindless_ViewHeap];
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      // Will be appending.
      parameter.DescriptorTable.NumDescriptorRanges = 0;
      parameter.DescriptorTable.pDescriptorRanges = root_bindless_view_ranges;
      parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
      // EDRAM.
      if (render_target_cache_->GetPath() == RenderTargetCache::Path::kPixelShaderInterlock) {
        assert_true(parameter.DescriptorTable.NumDescriptorRanges <
                    rex::countof(root_bindless_view_ranges));
        auto& range = root_bindless_view_ranges[parameter.DescriptorTable.NumDescriptorRanges++];
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        range.NumDescriptors = 1;
        range.BaseShaderRegister = UINT(DxbcShaderTranslator::UAVRegister::kEdram);
        range.RegisterSpace = 0;
        range.OffsetInDescriptorsFromTableStart = UINT(SystemBindlessView::kEdramR32UintUAV);
        auto& counter_range = root_bindless_view_ranges[parameter.DescriptorTable.NumDescriptorRanges++];
        counter_range = range;
        counter_range.BaseShaderRegister = UINT(DxbcShaderTranslator::UAVRegister::kOcclusionCounter);
        counter_range.OffsetInDescriptorsFromTableStart = UINT(SystemBindlessView::kOcclusionCounterRawUAV);
      }
      // The post passes' reactive mark (shader overrides: xe_post_reactive_mark, u0 in
      // space 4).
      {
        assert_true(parameter.DescriptorTable.NumDescriptorRanges <
                    rex::countof(root_bindless_view_ranges));
        auto& range = root_bindless_view_ranges[parameter.DescriptorTable.NumDescriptorRanges++];
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        range.NumDescriptors = 1;
        range.BaseShaderRegister = 0;
        range.RegisterSpace = 4;
        range.OffsetInDescriptorsFromTableStart =
            UINT(SystemBindlessView::kPostPassReactiveMarkUAV);
      }
      // Used UAV and SRV ranges must not overlap on Nvidia Fermi, so textures
      // have OffsetInDescriptorsFromTableStart after all static descriptors of
      // other types.
      // 2D array textures.
      {
        assert_true(parameter.DescriptorTable.NumDescriptorRanges <
                    rex::countof(root_bindless_view_ranges));
        auto& range = root_bindless_view_ranges[parameter.DescriptorTable.NumDescriptorRanges++];
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = UINT_MAX;
        range.BaseShaderRegister = 0;
        range.RegisterSpace = UINT(DxbcShaderTranslator::SRVSpace::kBindlessTextures2DArray);
        range.OffsetInDescriptorsFromTableStart = UINT(SystemBindlessView::kUnboundedSRVsStart);
      }
      // 3D textures.
      {
        assert_true(parameter.DescriptorTable.NumDescriptorRanges <
                    rex::countof(root_bindless_view_ranges));
        auto& range = root_bindless_view_ranges[parameter.DescriptorTable.NumDescriptorRanges++];
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = UINT_MAX;
        range.BaseShaderRegister = 0;
        range.RegisterSpace = UINT(DxbcShaderTranslator::SRVSpace::kBindlessTextures3D);
        range.OffsetInDescriptorsFromTableStart = UINT(SystemBindlessView::kUnboundedSRVsStart);
      }
      // Cube textures.
      {
        assert_true(parameter.DescriptorTable.NumDescriptorRanges <
                    rex::countof(root_bindless_view_ranges));
        auto& range = root_bindless_view_ranges[parameter.DescriptorTable.NumDescriptorRanges++];
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = UINT_MAX;
        range.BaseShaderRegister = 0;
        range.RegisterSpace = UINT(DxbcShaderTranslator::SRVSpace::kBindlessTexturesCube);
        range.OffsetInDescriptorsFromTableStart = UINT(SystemBindlessView::kUnboundedSRVsStart);
      }
    }
    root_signature_bindless_vs_ =
        ui::d3d12::util::CreateRootSignature(provider, root_signature_bindless_desc);
    if (!root_signature_bindless_vs_) {
      REXGPU_ERROR(
          "Failed to create the global root signature for bindless resources, "
          "the version for use without tessellation");
      return false;
    }
    root_parameters_bindless[kRootParameter_Bindless_FloatConstantsVertex].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_DOMAIN;
    root_parameters_bindless[kRootParameter_Bindless_DescriptorIndicesVertex].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_DOMAIN;
    root_signature_bindless_ds_ =
        ui::d3d12::util::CreateRootSignature(provider, root_signature_bindless_desc);
    if (!root_signature_bindless_ds_) {
      REXGPU_ERROR(
          "Failed to create the global root signature for bindless resources, "
          "the version for use with tessellation");
      return false;
    }
  }

  primitive_processor_ =
      std::make_unique<D3D12PrimitiveProcessor>(*register_file_, *memory_, *shared_memory_, *this);
  if (!primitive_processor_->Initialize()) {
    REXGPU_ERROR("Failed to initialize the geometric primitive processor");
    return false;
  }

  texture_cache_ =
      D3D12TextureCache::Create(*register_file_, *shared_memory_, draw_resolution_scale_x,
                                draw_resolution_scale_y, *this, bindless_resources_used_);
  if (!texture_cache_) {
    REXGPU_ERROR("Failed to initialize the texture cache");
    return false;
  }
  texture_cache_->SetBindingCheck(REXCVAR_GET(d3d12_reuse_draw_bindings_check),
                                  uint32_t(REXCVAR_GET(d3d12_reuse_draw_bindings_check_perturb)));
  if (!REXCVAR_GET(native_draw_scale).empty()) {
    const std::string& text = REXCVAR_GET(native_draw_scale);
    std::optional<DrawScale> scale = DrawScale::Parse(text);
    if (!scale) {
      REXGPU_ERROR("native_draw_scale {} is not a scale from 1 to 7, such as 3/2", text);
    } else if (render_target_cache_->GetPath() != RenderTargetCache::Path::kHostRenderTargets ||
               !REXCVAR_GET(host_render_native)) {
      REXGPU_ERROR("native_draw_scale {} needs the native path (host render targets and "
                   "host_render_native); the scene keeps the whole scale",
                   text);
    } else {
      // The input scale before the upscaler (fsr-plan.md 11): the other
      // surfaces are at its whole part.
      std::optional<DrawScale> input_scale;
      const std::string& input_text = REXCVAR_GET(native_scene_input_scale);
      if (!input_text.empty()) {
        input_scale = DrawScale::Parse(input_text);
        if (!input_scale) {
          REXGPU_ERROR("native_scene_input_scale {} is not a scale from 1 to 7, such as 4/3",
                       input_text);
        } else if (input_scale->ToFloat() > scale->ToFloat()) {
          REXGPU_ERROR("native_scene_input_scale {} is above native_draw_scale {}; the scene "
                       "is not upscaled",
                       input_text, text);
          input_scale.reset();
        } else if (!REXCVAR_GET(native_one_pass_msaa) || !(REXCVAR_GET(native_rt_surfaces) & 16)) {
          REXGPU_ERROR("native_scene_input_scale {} needs native_one_pass_msaa and "
                       "native_rt_surfaces bit 16; the scene is not upscaled",
                       input_text);
          input_scale.reset();
        } else if (*input_scale == *scale) {
          input_scale.reset();
        }
      }
      const uint32_t whole_scale = input_scale ? input_scale->Floor() : scale->Floor();
      if (draw_resolution_scale_x != draw_resolution_scale_y ||
          whole_scale != draw_resolution_scale_x) {
        REXGPU_ERROR("native_draw_scale {} needs draw_resolution_scale_x and _y of {}, not {}x{}",
                     input_scale ? input_text : text, whole_scale, draw_resolution_scale_x,
                     draw_resolution_scale_y);
      } else {
        texture_cache_->SetSceneDrawScale(*scale);
        render_target_cache_->SetSceneDrawScale(*scale);
        if (input_scale) {
          texture_cache_->SetSceneInputScale(*input_scale);
          render_target_cache_->SetSceneInputScale(*input_scale);
        }
        render_target_cache_->SetSceneTextures(!scale->IsWhole() || input_scale ||
                                               REXCVAR_GET(native_draw_scale_variant));
        if (input_scale) {
          REXGPU_INFO("Native draw scale: {}/{} for the scene, upscaled from {}/{}, {} for the "
                      "other surfaces",
                      scale->numerator, scale->denominator, input_scale->numerator,
                      input_scale->denominator, draw_resolution_scale_x);
        } else {
          REXGPU_INFO("Native draw scale: {}/{} for the scene, {} for the other surfaces",
                      scale->numerator, scale->denominator, draw_resolution_scale_x);
        }
      }
    }
  }
  if (REXCVAR_GET(native_logical_width) != 1280) {
    const int32_t width = REXCVAR_GET(native_logical_width);
    if (!render_target_cache_->scene_textures()) {
      REXGPU_ERROR("native_logical_width {} needs the scene draws' path (a fractional "
                   "native_draw_scale or native_draw_scale_variant); the scene keeps 1280",
                   width);
    } else if (width < 1280 || width > 3840 || width % 8) {
      REXGPU_ERROR("native_logical_width {} is not a multiple of 8 from 1280 to 3840", width);
    } else {
      draw_back_end::SetSceneLogicalWidth(uint32_t(width));
      render_target_cache_->SetSceneLogicalWidth(uint32_t(width));
      texture_cache_->SetSceneLogicalWidth(uint32_t(width));
      REXGPU_INFO("Native logical width: {} at 720 lines (Hor+)", width);
    }
  }
  if (REXCVAR_GET(native_logical_height) != 720) {
    const int32_t height = REXCVAR_GET(native_logical_height);
    if (!render_target_cache_->scene_textures()) {
      REXGPU_ERROR("native_logical_height {} needs the scene draws' path (a fractional "
                   "native_draw_scale or native_draw_scale_variant); the scene keeps 720",
                   height);
    } else if (height < 720 || height > 1024 || height % 8) {
      REXGPU_ERROR("native_logical_height {} is not a multiple of 8 from 720 to 1024", height);
    } else {
      draw_back_end::SetSceneLogicalHeight(uint32_t(height));
      render_target_cache_->SetSceneLogicalHeight(uint32_t(height));
      texture_cache_->SetSceneLogicalHeight(uint32_t(height));
      REXGPU_INFO("Native logical height: {} at 1280 wide (Vert+)", height);
    }
  }

  pipeline_cache_ = std::make_unique<PipelineCache>(
      *this, *register_file_, *render_target_cache_.get(), bindless_resources_used_);
  if (!pipeline_cache_->Initialize()) {
    REXGPU_ERROR("Failed to initialize the graphics pipeline cache");
    return false;
  }
  const ShaderOverrides* shader_overrides = pipeline_cache_->shader_overrides();
  shader_mod_knobs_source_ =
      (shader_overrides && shader_overrides->live_knobs()) ? shader_overrides : nullptr;
  shader_mod_knobs_generation_ = 0;
  shader_mod_host_values_used_ = shader_overrides != nullptr;
  std::memset(shader_mod_host_values_, 0, sizeof(shader_mod_host_values_));
  native_shader_contract_ = shader_overrides ? shader_overrides->native_contract() : nullptr;
  native_shader_pass_debug_ = native_shader_contract_ && shader_overrides->native_pass_debug();
  native_shader_pass_ = 0;

  D3D12_HEAP_FLAGS heap_flag_create_not_zeroed = provider.GetHeapFlagCreateNotZeroed();

  // Create gamma ramp resources.
  gamma_ramp_256_entry_table_up_to_date_ = false;
  gamma_ramp_pwl_up_to_date_ = false;
  D3D12_RESOURCE_DESC gamma_ramp_buffer_desc;
  ui::d3d12::util::FillBufferResourceDesc(gamma_ramp_buffer_desc, (256 + 128 * 3) * 4,
                                          D3D12_RESOURCE_FLAG_NONE);
  // The first action will be uploading.
  gamma_ramp_buffer_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
  if (FAILED(device->CreateCommittedResource(&ui::d3d12::util::kHeapPropertiesDefault,
                                             heap_flag_create_not_zeroed, &gamma_ramp_buffer_desc,
                                             gamma_ramp_buffer_state_, nullptr,
                                             IID_PPV_ARGS(&gamma_ramp_buffer_)))) {
    REXGPU_ERROR("Failed to create the gamma ramp buffer");
    return false;
  }
  // The upload buffer is frame-buffered.
  gamma_ramp_buffer_desc.Width *= kQueueFrames;
  if (FAILED(device->CreateCommittedResource(&ui::d3d12::util::kHeapPropertiesUpload,
                                             heap_flag_create_not_zeroed, &gamma_ramp_buffer_desc,
                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                             IID_PPV_ARGS(&gamma_ramp_upload_buffer_)))) {
    REXGPU_ERROR("Failed to create the gamma ramp upload buffer");
    return false;
  }
  if (FAILED(gamma_ramp_upload_buffer_->Map(
          0, nullptr, reinterpret_cast<void**>(&gamma_ramp_upload_buffer_mapping_)))) {
    REXGPU_ERROR("Failed to map the gamma ramp upload buffer");
    gamma_ramp_upload_buffer_mapping_ = nullptr;
    return false;
  }

  // Initialize compute pipelines for output with gamma ramp.
  D3D12_ROOT_PARAMETER
  apply_gamma_root_parameters[UINT(ApplyGammaRootParameter::kCount)];
  {
    D3D12_ROOT_PARAMETER& apply_gamma_root_parameter_constants =
        apply_gamma_root_parameters[UINT(ApplyGammaRootParameter::kConstants)];
    apply_gamma_root_parameter_constants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    apply_gamma_root_parameter_constants.Constants.ShaderRegister = 0;
    apply_gamma_root_parameter_constants.Constants.RegisterSpace = 0;
    apply_gamma_root_parameter_constants.Constants.Num32BitValues =
        sizeof(ApplyGammaConstants) / sizeof(uint32_t);
    apply_gamma_root_parameter_constants.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_DESCRIPTOR_RANGE apply_gamma_root_descriptor_range_dest;
  apply_gamma_root_descriptor_range_dest.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  apply_gamma_root_descriptor_range_dest.NumDescriptors = 1;
  apply_gamma_root_descriptor_range_dest.BaseShaderRegister = 0;
  apply_gamma_root_descriptor_range_dest.RegisterSpace = 0;
  apply_gamma_root_descriptor_range_dest.OffsetInDescriptorsFromTableStart = 0;
  {
    D3D12_ROOT_PARAMETER& apply_gamma_root_parameter_dest =
        apply_gamma_root_parameters[UINT(ApplyGammaRootParameter::kDestination)];
    apply_gamma_root_parameter_dest.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    apply_gamma_root_parameter_dest.DescriptorTable.NumDescriptorRanges = 1;
    apply_gamma_root_parameter_dest.DescriptorTable.pDescriptorRanges =
        &apply_gamma_root_descriptor_range_dest;
    apply_gamma_root_parameter_dest.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_DESCRIPTOR_RANGE apply_gamma_root_descriptor_range_source;
  apply_gamma_root_descriptor_range_source.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  apply_gamma_root_descriptor_range_source.NumDescriptors = 1;
  apply_gamma_root_descriptor_range_source.BaseShaderRegister = 1;
  apply_gamma_root_descriptor_range_source.RegisterSpace = 0;
  apply_gamma_root_descriptor_range_source.OffsetInDescriptorsFromTableStart = 0;
  {
    D3D12_ROOT_PARAMETER& apply_gamma_root_parameter_source =
        apply_gamma_root_parameters[UINT(ApplyGammaRootParameter::kSource)];
    apply_gamma_root_parameter_source.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    apply_gamma_root_parameter_source.DescriptorTable.NumDescriptorRanges = 1;
    apply_gamma_root_parameter_source.DescriptorTable.pDescriptorRanges =
        &apply_gamma_root_descriptor_range_source;
    apply_gamma_root_parameter_source.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_DESCRIPTOR_RANGE apply_gamma_root_descriptor_range_ramp;
  apply_gamma_root_descriptor_range_ramp.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  apply_gamma_root_descriptor_range_ramp.NumDescriptors = 1;
  apply_gamma_root_descriptor_range_ramp.BaseShaderRegister = 0;
  apply_gamma_root_descriptor_range_ramp.RegisterSpace = 0;
  apply_gamma_root_descriptor_range_ramp.OffsetInDescriptorsFromTableStart = 0;
  {
    D3D12_ROOT_PARAMETER& apply_gamma_root_parameter_gamma_ramp =
        apply_gamma_root_parameters[UINT(ApplyGammaRootParameter::kRamp)];
    apply_gamma_root_parameter_gamma_ramp.ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    apply_gamma_root_parameter_gamma_ramp.DescriptorTable.NumDescriptorRanges = 1;
    apply_gamma_root_parameter_gamma_ramp.DescriptorTable.pDescriptorRanges =
        &apply_gamma_root_descriptor_range_ramp;
    apply_gamma_root_parameter_gamma_ramp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_ROOT_SIGNATURE_DESC apply_gamma_root_signature_desc;
  apply_gamma_root_signature_desc.NumParameters = UINT(ApplyGammaRootParameter::kCount);
  apply_gamma_root_signature_desc.pParameters = apply_gamma_root_parameters;
  apply_gamma_root_signature_desc.NumStaticSamplers = 0;
  apply_gamma_root_signature_desc.pStaticSamplers = nullptr;
  apply_gamma_root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  *(apply_gamma_root_signature_.ReleaseAndGetAddressOf()) =
      ui::d3d12::util::CreateRootSignature(provider, apply_gamma_root_signature_desc);
  if (!apply_gamma_root_signature_) {
    REXGPU_ERROR("Failed to create the gamma ramp application root signature");
    return false;
  }
  *(apply_gamma_table_pipeline_.ReleaseAndGetAddressOf()) = ui::d3d12::util::CreateComputePipeline(
      device, shaders::apply_gamma_table_cs, sizeof(shaders::apply_gamma_table_cs),
      apply_gamma_root_signature_.Get());
  if (!apply_gamma_table_pipeline_) {
    REXGPU_ERROR(
        "Failed to create the 256-entry table gamma ramp application compute "
        "pipeline");
    return false;
  }
  *(apply_gamma_table_fxaa_luma_pipeline_.ReleaseAndGetAddressOf()) =
      ui::d3d12::util::CreateComputePipeline(device, shaders::apply_gamma_table_fxaa_luma_cs,
                                             sizeof(shaders::apply_gamma_table_fxaa_luma_cs),
                                             apply_gamma_root_signature_.Get());
  if (!apply_gamma_table_fxaa_luma_pipeline_) {
    REXGPU_ERROR(
        "Failed to create the 256-entry table gamma ramp application compute "
        "pipeline with perceptual luma output");
    return false;
  }
  *(apply_gamma_pwl_pipeline_.ReleaseAndGetAddressOf()) = ui::d3d12::util::CreateComputePipeline(
      device, shaders::apply_gamma_pwl_cs, sizeof(shaders::apply_gamma_pwl_cs),
      apply_gamma_root_signature_.Get());
  if (!apply_gamma_pwl_pipeline_) {
    REXGPU_ERROR("Failed to create the PWL gamma ramp application compute pipeline");
    return false;
  }
  *(apply_gamma_pwl_fxaa_luma_pipeline_.ReleaseAndGetAddressOf()) =
      ui::d3d12::util::CreateComputePipeline(device, shaders::apply_gamma_pwl_fxaa_luma_cs,
                                             sizeof(shaders::apply_gamma_pwl_fxaa_luma_cs),
                                             apply_gamma_root_signature_.Get());
  if (!apply_gamma_pwl_fxaa_luma_pipeline_) {
    REXGPU_ERROR(
        "Failed to create the PWL gamma ramp application compute pipeline with "
        "perceptual luma output");
    return false;
  }

  // Initialize compute pipelines for post-processing anti-aliasing.
  D3D12_ROOT_PARAMETER fxaa_root_parameters[UINT(FxaaRootParameter::kCount)];
  {
    D3D12_ROOT_PARAMETER& fxaa_root_parameter_constants =
        fxaa_root_parameters[UINT(ApplyGammaRootParameter::kConstants)];
    fxaa_root_parameter_constants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    fxaa_root_parameter_constants.Constants.ShaderRegister = 0;
    fxaa_root_parameter_constants.Constants.RegisterSpace = 0;
    fxaa_root_parameter_constants.Constants.Num32BitValues =
        sizeof(FxaaConstants) / sizeof(uint32_t);
    fxaa_root_parameter_constants.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_DESCRIPTOR_RANGE fxaa_root_descriptor_range_dest;
  fxaa_root_descriptor_range_dest.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  fxaa_root_descriptor_range_dest.NumDescriptors = 1;
  fxaa_root_descriptor_range_dest.BaseShaderRegister = 0;
  fxaa_root_descriptor_range_dest.RegisterSpace = 0;
  fxaa_root_descriptor_range_dest.OffsetInDescriptorsFromTableStart = 0;
  {
    D3D12_ROOT_PARAMETER& fxaa_root_parameter_dest =
        fxaa_root_parameters[UINT(FxaaRootParameter::kDestination)];
    fxaa_root_parameter_dest.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    fxaa_root_parameter_dest.DescriptorTable.NumDescriptorRanges = 1;
    fxaa_root_parameter_dest.DescriptorTable.pDescriptorRanges = &fxaa_root_descriptor_range_dest;
    fxaa_root_parameter_dest.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_DESCRIPTOR_RANGE fxaa_root_descriptor_range_source;
  fxaa_root_descriptor_range_source.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  fxaa_root_descriptor_range_source.NumDescriptors = 1;
  fxaa_root_descriptor_range_source.BaseShaderRegister = 0;
  fxaa_root_descriptor_range_source.RegisterSpace = 0;
  fxaa_root_descriptor_range_source.OffsetInDescriptorsFromTableStart = 0;
  {
    D3D12_ROOT_PARAMETER& fxaa_root_parameter_source =
        fxaa_root_parameters[UINT(FxaaRootParameter::kSource)];
    fxaa_root_parameter_source.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    fxaa_root_parameter_source.DescriptorTable.NumDescriptorRanges = 1;
    fxaa_root_parameter_source.DescriptorTable.pDescriptorRanges =
        &fxaa_root_descriptor_range_source;
    fxaa_root_parameter_source.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_STATIC_SAMPLER_DESC fxaa_root_sampler;
  fxaa_root_sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  fxaa_root_sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  fxaa_root_sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  fxaa_root_sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  fxaa_root_sampler.MipLODBias = 0.0f;
  fxaa_root_sampler.MaxAnisotropy = 1;
  fxaa_root_sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  fxaa_root_sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
  fxaa_root_sampler.MinLOD = 0.0f;
  fxaa_root_sampler.MaxLOD = 0.0f;
  fxaa_root_sampler.ShaderRegister = 0;
  fxaa_root_sampler.RegisterSpace = 0;
  fxaa_root_sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC fxaa_root_signature_desc;
  fxaa_root_signature_desc.NumParameters = UINT(FxaaRootParameter::kCount);
  fxaa_root_signature_desc.pParameters = fxaa_root_parameters;
  fxaa_root_signature_desc.NumStaticSamplers = 1;
  fxaa_root_signature_desc.pStaticSamplers = &fxaa_root_sampler;
  fxaa_root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  *(fxaa_root_signature_.ReleaseAndGetAddressOf()) =
      ui::d3d12::util::CreateRootSignature(provider, fxaa_root_signature_desc);
  if (!fxaa_root_signature_) {
    REXGPU_ERROR("Failed to create the FXAA root signature");
    return false;
  }
  *(fxaa_pipeline_.ReleaseAndGetAddressOf()) = ui::d3d12::util::CreateComputePipeline(
      device, shaders::fxaa_cs, sizeof(shaders::fxaa_cs), fxaa_root_signature_.Get());
  if (!fxaa_pipeline_) {
    REXGPU_ERROR("Failed to create the FXAA compute pipeline");
    return false;
  }
  *(fxaa_extreme_pipeline_.ReleaseAndGetAddressOf()) = ui::d3d12::util::CreateComputePipeline(
      device, shaders::fxaa_extreme_cs, sizeof(shaders::fxaa_extreme_cs),
      fxaa_root_signature_.Get());
  if (!fxaa_pipeline_) {
    REXGPU_ERROR("Failed to create the extreme-quality FXAA compute pipeline");
    return false;
  }

  // Resolve downscale compute pipeline for scaled readback resolve.
  D3D12_ROOT_PARAMETER
  resolve_downscale_root_parameters[UINT(ResolveDownscaleRootParameter::kCount)];
  {
    D3D12_ROOT_PARAMETER& constants_parameter =
        resolve_downscale_root_parameters[UINT(ResolveDownscaleRootParameter::kConstants)];
    constants_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    constants_parameter.Constants.ShaderRegister = 0;
    constants_parameter.Constants.RegisterSpace = 0;
    constants_parameter.Constants.Num32BitValues =
        sizeof(ResolveDownscaleConstants) / sizeof(uint32_t);
    constants_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_DESCRIPTOR_RANGE resolve_downscale_source_range;
  resolve_downscale_source_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  resolve_downscale_source_range.NumDescriptors = 1;
  resolve_downscale_source_range.BaseShaderRegister = 0;
  resolve_downscale_source_range.RegisterSpace = 0;
  resolve_downscale_source_range.OffsetInDescriptorsFromTableStart = 0;
  {
    D3D12_ROOT_PARAMETER& source_parameter =
        resolve_downscale_root_parameters[UINT(ResolveDownscaleRootParameter::kSource)];
    source_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    source_parameter.DescriptorTable.NumDescriptorRanges = 1;
    source_parameter.DescriptorTable.pDescriptorRanges = &resolve_downscale_source_range;
    source_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_DESCRIPTOR_RANGE resolve_downscale_destination_range;
  resolve_downscale_destination_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  resolve_downscale_destination_range.NumDescriptors = 1;
  resolve_downscale_destination_range.BaseShaderRegister = 0;
  resolve_downscale_destination_range.RegisterSpace = 0;
  resolve_downscale_destination_range.OffsetInDescriptorsFromTableStart = 0;
  {
    D3D12_ROOT_PARAMETER& destination_parameter =
        resolve_downscale_root_parameters[UINT(ResolveDownscaleRootParameter::kDestination)];
    destination_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    destination_parameter.DescriptorTable.NumDescriptorRanges = 1;
    destination_parameter.DescriptorTable.pDescriptorRanges = &resolve_downscale_destination_range;
    destination_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_ROOT_SIGNATURE_DESC resolve_downscale_root_signature_desc;
  resolve_downscale_root_signature_desc.NumParameters = UINT(ResolveDownscaleRootParameter::kCount);
  resolve_downscale_root_signature_desc.pParameters = resolve_downscale_root_parameters;
  resolve_downscale_root_signature_desc.NumStaticSamplers = 0;
  resolve_downscale_root_signature_desc.pStaticSamplers = nullptr;
  resolve_downscale_root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  *(resolve_downscale_root_signature_.ReleaseAndGetAddressOf()) =
      ui::d3d12::util::CreateRootSignature(provider, resolve_downscale_root_signature_desc);
  if (resolve_downscale_root_signature_) {
    *(resolve_downscale_pipeline_.ReleaseAndGetAddressOf()) =
        ui::d3d12::util::CreateComputePipeline(device, shaders::resolve_downscale_cs,
                                               sizeof(shaders::resolve_downscale_cs),
                                               resolve_downscale_root_signature_.Get());
  }
  if (!resolve_downscale_root_signature_ || !resolve_downscale_pipeline_) {
    resolve_downscale_pipeline_.Reset();
    resolve_downscale_root_signature_.Reset();
    REXGPU_WARN("Failed to initialize D3D12 resolve-downscale readback pipeline");
  }

  if (bindless_resources_used_) {
    // Create the system bindless descriptors once all resources are
    // initialized.
    // kNullRawSRV.
    ui::d3d12::util::CreateBufferRawSRV(
        device,
        provider.OffsetViewDescriptor(view_bindless_heap_cpu_start_,
                                      uint32_t(SystemBindlessView::kNullRawSRV)),
        nullptr, 0);
    // kNullRawUAV.
    ui::d3d12::util::CreateBufferRawUAV(
        device,
        provider.OffsetViewDescriptor(view_bindless_heap_cpu_start_,
                                      uint32_t(SystemBindlessView::kNullRawUAV)),
        nullptr, 0);
    // kPostPassReactiveMarkUAV: null until the post passes bind their reactive mark.
    {
      D3D12_UNORDERED_ACCESS_VIEW_DESC null_uav_desc = {};
      null_uav_desc.Format = DXGI_FORMAT_R8_UNORM;
      null_uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
      device->CreateUnorderedAccessView(
          nullptr, nullptr, &null_uav_desc,
          provider.OffsetViewDescriptor(view_bindless_heap_cpu_start_,
                                        uint32_t(SystemBindlessView::kPostPassReactiveMarkUAV)));
    }
    // kNullTexture2DArray.
    D3D12_SHADER_RESOURCE_VIEW_DESC null_srv_desc;
    null_srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    null_srv_desc.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
        D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0, D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0,
        D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0, D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0);
    null_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    null_srv_desc.Texture2DArray.MostDetailedMip = 0;
    null_srv_desc.Texture2DArray.MipLevels = 1;
    null_srv_desc.Texture2DArray.FirstArraySlice = 0;
    null_srv_desc.Texture2DArray.ArraySize = 1;
    null_srv_desc.Texture2DArray.PlaneSlice = 0;
    null_srv_desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
    device->CreateShaderResourceView(
        nullptr, &null_srv_desc,
        provider.OffsetViewDescriptor(view_bindless_heap_cpu_start_,
                                      uint32_t(SystemBindlessView::kNullTexture2DArray)));
    // kNullTexture3D.
    null_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    null_srv_desc.Texture3D.MostDetailedMip = 0;
    null_srv_desc.Texture3D.MipLevels = 1;
    null_srv_desc.Texture3D.ResourceMinLODClamp = 0.0f;
    device->CreateShaderResourceView(
        nullptr, &null_srv_desc,
        provider.OffsetViewDescriptor(view_bindless_heap_cpu_start_,
                                      uint32_t(SystemBindlessView::kNullTexture3D)));
    // kNullTextureCube.
    null_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    null_srv_desc.TextureCube.MostDetailedMip = 0;
    null_srv_desc.TextureCube.MipLevels = 1;
    null_srv_desc.TextureCube.ResourceMinLODClamp = 0.0f;
    device->CreateShaderResourceView(
        nullptr, &null_srv_desc,
        provider.OffsetViewDescriptor(view_bindless_heap_cpu_start_,
                                      uint32_t(SystemBindlessView::kNullTextureCube)));
    // kPostPassViewsStart...: zero until a post pass writes them.
    null_srv_desc.Format = DXGI_FORMAT_R8_UNORM;
    null_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    null_srv_desc.Texture2DArray.MostDetailedMip = 0;
    null_srv_desc.Texture2DArray.MipLevels = 1;
    null_srv_desc.Texture2DArray.FirstArraySlice = 0;
    null_srv_desc.Texture2DArray.ArraySize = 1;
    null_srv_desc.Texture2DArray.PlaneSlice = 0;
    null_srv_desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
    for (uint32_t i = 0; i < kPostPassViewCount; ++i) {
      device->CreateShaderResourceView(
          nullptr, &null_srv_desc,
          provider.OffsetViewDescriptor(view_bindless_heap_cpu_start_,
                                        uint32_t(SystemBindlessView::kPostPassViewsStart) + i));
    }
    // kLookFileViewsStart...: zero unless a look file is loaded into it.
    for (uint32_t i = 0; i < kLookFileViewCount; ++i) {
      device->CreateShaderResourceView(
          nullptr, &null_srv_desc,
          provider.OffsetViewDescriptor(view_bindless_heap_cpu_start_,
                                        uint32_t(SystemBindlessView::kLookFileViewsStart) + i));
    }
    if (!CreateLookFileTextures()) {
      return false;
    }
    // kSharedMemoryRawSRV.
    shared_memory_->WriteRawSRVDescriptor(provider.OffsetViewDescriptor(
        view_bindless_heap_cpu_start_, uint32_t(SystemBindlessView::kSharedMemoryRawSRV)));
    // kSharedMemoryR32UintSRV.
    shared_memory_->WriteUintPow2SRVDescriptor(
        provider.OffsetViewDescriptor(view_bindless_heap_cpu_start_,
                                      uint32_t(SystemBindlessView::kSharedMemoryR32UintSRV)),
        2);
    // kSharedMemoryR32G32UintSRV.
    shared_memory_->WriteUintPow2SRVDescriptor(
        provider.OffsetViewDescriptor(view_bindless_heap_cpu_start_,
                                      uint32_t(SystemBindlessView::kSharedMemoryR32G32UintSRV)),
        3);
    // kSharedMemoryR32G32B32A32UintSRV.
    shared_memory_->WriteUintPow2SRVDescriptor(
        provider.OffsetViewDescriptor(
            view_bindless_heap_cpu_start_,
            uint32_t(SystemBindlessView::kSharedMemoryR32G32B32A32UintSRV)),
        4);
    // kSharedMemoryRawUAV.
    shared_memory_->WriteRawUAVDescriptor(provider.OffsetViewDescriptor(
        view_bindless_heap_cpu_start_, uint32_t(SystemBindlessView::kSharedMemoryRawUAV)));
    // kSharedMemoryR32UintUAV.
    shared_memory_->WriteUintPow2UAVDescriptor(
        provider.OffsetViewDescriptor(view_bindless_heap_cpu_start_,
                                      uint32_t(SystemBindlessView::kSharedMemoryR32UintUAV)),
        2);
    // kSharedMemoryR32G32UintUAV.
    shared_memory_->WriteUintPow2UAVDescriptor(
        provider.OffsetViewDescriptor(view_bindless_heap_cpu_start_,
                                      uint32_t(SystemBindlessView::kSharedMemoryR32G32UintUAV)),
        3);
    // kSharedMemoryR32G32B32A32UintUAV.
    shared_memory_->WriteUintPow2UAVDescriptor(
        provider.OffsetViewDescriptor(
            view_bindless_heap_cpu_start_,
            uint32_t(SystemBindlessView::kSharedMemoryR32G32B32A32UintUAV)),
        4);
    // kEdramRawSRV.
    render_target_cache_->WriteEdramRawSRVDescriptor(provider.OffsetViewDescriptor(
        view_bindless_heap_cpu_start_, uint32_t(SystemBindlessView::kEdramRawSRV)));
    // kEdramR32UintSRV.
    render_target_cache_->WriteEdramUintPow2SRVDescriptor(
        provider.OffsetViewDescriptor(view_bindless_heap_cpu_start_,
                                      uint32_t(SystemBindlessView::kEdramR32UintSRV)),
        2);
    // kEdramR32G32UintSRV.
    render_target_cache_->WriteEdramUintPow2SRVDescriptor(
        provider.OffsetViewDescriptor(view_bindless_heap_cpu_start_,
                                      uint32_t(SystemBindlessView::kEdramR32G32UintSRV)),
        3);
    // kEdramR32G32B32A32UintSRV.
    render_target_cache_->WriteEdramUintPow2SRVDescriptor(
        provider.OffsetViewDescriptor(view_bindless_heap_cpu_start_,
                                      uint32_t(SystemBindlessView::kEdramR32G32B32A32UintSRV)),
        4);
    // kEdramRawUAV.
    render_target_cache_->WriteEdramRawUAVDescriptor(provider.OffsetViewDescriptor(
        view_bindless_heap_cpu_start_, uint32_t(SystemBindlessView::kEdramRawUAV)));
    // kEdramR32UintUAV.
    render_target_cache_->WriteEdramUintPow2UAVDescriptor(
        provider.OffsetViewDescriptor(view_bindless_heap_cpu_start_,
                                      uint32_t(SystemBindlessView::kEdramR32UintUAV)),
        2);
    // kEdramR32G32UintUAV.
    render_target_cache_->WriteEdramUintPow2UAVDescriptor(
        provider.OffsetViewDescriptor(view_bindless_heap_cpu_start_,
                                      uint32_t(SystemBindlessView::kEdramR32G32UintUAV)),
        3);
    // kEdramR32G32B32A32UintUAV.
    render_target_cache_->WriteEdramUintPow2UAVDescriptor(
        provider.OffsetViewDescriptor(view_bindless_heap_cpu_start_,
                                      uint32_t(SystemBindlessView::kEdramR32G32B32A32UintUAV)),
        4);
    // kGammaRampTableSRV.
    WriteGammaRampSRV(
        false, provider.OffsetViewDescriptor(view_bindless_heap_cpu_start_,
                                             uint32_t(SystemBindlessView::kGammaRampTableSRV)));
    // kGammaRampPWLSRV.
    WriteGammaRampSRV(
        true, provider.OffsetViewDescriptor(view_bindless_heap_cpu_start_,
                                            uint32_t(SystemBindlessView::kGammaRampPWLSRV)));
  }

  occlusion_query_resources_available_ = InitializeOcclusionQueryResources();
  InitializeGpuTimestamps();
  InitializeFrameGraphMarks();
  InitializeDrawStats();
  if (bindless_resources_used_) {
    WriteROVOcclusionUAVDescriptor(provider.OffsetViewDescriptor(
        view_bindless_heap_cpu_start_, uint32_t(SystemBindlessView::kOcclusionCounterRawUAV)));
  }

  // Just not to expose uninitialized memory.
  std::memset(&system_constants_, 0, sizeof(system_constants_));

  post_passes_ = PostPasses::Create(*this);

  InitializeDrawBackEnd();
  render_target_update_guard_ =
      REXCVAR_GET(render_target_update_guard) &&
      render_target_cache_->GetPath() == RenderTargetCache::Path::kHostRenderTargets;
  render_target_update_check_ =
      render_target_update_guard_ && REXCVAR_GET(render_target_update_guard_check);
  render_target_update_perturb_ =
      render_target_update_check_
          ? uint32_t(REXCVAR_GET(render_target_update_guard_check_perturb))
          : 0;
  render_target_update_key_valid_ = false;
  if (render_target_update_guard_) {
    REXGPU_INFO("Render target update guard: on{}{}", render_target_update_check_ ? ", checked" : "",
                render_target_update_perturb_ ? ", perturbed" : "");
  }

  return true;
}

void D3D12CommandProcessor::ShutdownContext() {
  read_trace::Shutdown(*memory_);
  delete demand_servicer_;
  demand_servicer_ = nullptr;
  ShutdownReadbackPublishThread();
  ShutdownSubmitThread();
  AwaitAllQueueOperationsCompletion();
  post_passes_.reset();
  InvalidateAllVertexBufferResidency();
  ShutdownOcclusionQueryResources();
  ShutdownGpuTimestamps();
  ShutdownFrameGraphMarks();
  ShutdownDrawStats();

  ui::d3d12::util::ReleaseAndNull(readback_buffer_);
  readback_buffer_size_ = 0;
  if (demand_readback_buffer_) {
    demand_readback_buffer_->Unmap(0, nullptr);
    demand_readback_mapping_ = nullptr;
  }
  ui::d3d12::util::ReleaseAndNull(demand_readback_buffer_);
  demand_readback_buffer_size_ = 0;
  for (auto& resolve_readback_pair : readback_buffers_) {
    auto& readback = resolve_readback_pair.second;
    for (uint32_t i = 0; i < kReadbackSlots; ++i) {
      if (readback.buffers[i]) {
        if (readback.mapped_data[i]) {
          readback.buffers[i]->Unmap(0, nullptr);
          readback.mapped_data[i] = nullptr;
        }
        readback.buffers[i]->Release();
        readback.buffers[i] = nullptr;
      }
      readback.sizes[i] = 0;
      readback.submission_written[i] = 0;
      readback.written_size[i] = 0;
    }
  }
  readback_buffers_.clear();
  for (auto& memexport_readback_pair : memexport_readback_buffers_) {
    auto& readback = memexport_readback_pair.second;
    for (uint32_t i = 0; i < kReadbackSlots; ++i) {
      if (readback.buffers[i]) {
        if (readback.mapped_data[i]) {
          readback.buffers[i]->Unmap(0, nullptr);
          readback.mapped_data[i] = nullptr;
        }
        readback.buffers[i]->Release();
        readback.buffers[i] = nullptr;
      }
      readback.sizes[i] = 0;
      readback.submission_written[i] = 0;
      readback.written_size[i] = 0;
    }
  }
  memexport_readback_buffers_.clear();

  ui::d3d12::util::ReleaseAndNull(scratch_buffer_);
  scratch_buffer_size_ = 0;
  resolve_downscale_buffer_size_ = 0;
  resolve_downscale_buffer_.Reset();

  for (const std::pair<uint64_t, ID3D12Resource*>& resource_for_deletion :
       resources_for_deletion_) {
    resource_for_deletion.second->Release();
  }
  resources_for_deletion_.clear();

  fxaa_source_texture_submission_ = 0;
  fxaa_source_texture_.Reset();

  fxaa_extreme_pipeline_.Reset();
  fxaa_pipeline_.Reset();
  fxaa_root_signature_.Reset();
  resolve_downscale_pipeline_.Reset();
  resolve_downscale_root_signature_.Reset();

  apply_gamma_pwl_fxaa_luma_pipeline_.Reset();
  apply_gamma_pwl_pipeline_.Reset();
  apply_gamma_table_fxaa_luma_pipeline_.Reset();
  apply_gamma_table_pipeline_.Reset();
  apply_gamma_root_signature_.Reset();

  // Unmapping will be done implicitly by the destruction.
  gamma_ramp_upload_buffer_mapping_ = nullptr;
  gamma_ramp_upload_buffer_.Reset();
  gamma_ramp_buffer_.Reset();
  look_file_uploads_.clear();
  look_file_textures_.clear();
  look_file_footprints_.clear();

  texture_cache_.reset();

  shader_mod_knobs_source_ = nullptr;
  shader_mod_host_values_used_ = false;
  native_shader_contract_ = nullptr;
  pipeline_cache_.reset();

  primitive_processor_.reset();

  // Shut down binding - bindless descriptors may be owned by subsystems like
  // the texture cache.

  // Root signatures are used by pipelines, thus freed after the pipelines.
  ui::d3d12::util::ReleaseAndNull(root_signature_bindless_ds_);
  ui::d3d12::util::ReleaseAndNull(root_signature_bindless_vs_);
  for (auto it : root_signatures_bindful_) {
    it.second->Release();
  }
  root_signatures_bindful_.clear();

  if (bindless_resources_used_) {
    texture_cache_bindless_sampler_map_.clear();
    for (const auto& sampler_bindless_heap_overflowed : sampler_bindless_heaps_overflowed_) {
      sampler_bindless_heap_overflowed.first->Release();
    }
    sampler_bindless_heaps_overflowed_.clear();
    sampler_bindless_heap_allocated_ = 0;
    ui::d3d12::util::ReleaseAndNull(sampler_bindless_heap_current_);
    view_bindless_one_use_descriptors_.clear();
    view_bindless_heap_free_.clear();
    ui::d3d12::util::ReleaseAndNull(view_bindless_heap_);
  } else {
    sampler_bindful_heap_pool_.reset();
    view_bindful_heap_pool_.reset();
  }
  constant_buffer_pool_.reset();
  back_end_constant_pool_.reset();

  render_target_cache_.reset();

  shared_memory_.reset();

  deferred_command_list_.Reset();
  ui::d3d12::util::ReleaseAndNull(command_list_1_);
  ui::d3d12::util::ReleaseAndNull(command_list_);
  ClearCommandAllocatorCache();

  frame_open_ = false;
  frame_current_ = 1;
  frame_completed_ = 0;
  std::memset(closed_frame_submissions_, 0, sizeof(closed_frame_submissions_));

  // First release the fences since they may reference fence_completion_event_.

  queue_operations_done_since_submission_signal_ = false;
  queue_operations_since_submission_fence_last_ = 0;
  ui::d3d12::util::ReleaseAndNull(queue_operations_since_submission_fence_);

  ui::d3d12::util::ReleaseAndNull(submission_fence_);
  submission_open_ = false;
  submission_current_ = 1;
  submission_completed_ = 0;

  if (fence_completion_event_) {
    CloseHandle(fence_completion_event_);
    fence_completion_event_ = nullptr;
  }

  device_removed_ = false;

  CommandProcessor::ShutdownContext();
}

void D3D12CommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  CommandProcessor::WriteRegister(index, value);
  if (track_constants_) {
    pending_constants_.MarkWritten(index, index);
  }

  if (index >= XE_GPU_REG_SHADER_CONSTANT_000_X && index <= XE_GPU_REG_SHADER_CONSTANT_511_W) {
    if (frame_open_) {
      uint32_t float_constant_index = (index - XE_GPU_REG_SHADER_CONSTANT_000_X) >> 2;
      if (float_constant_index >= 256) {
        float_constant_index -= 256;
        if (current_float_constant_map_pixel_[float_constant_index >> 6] &
            (1ull << (float_constant_index & 63))) {
          cbuffer_binding_float_pixel_.up_to_date = false;
        }
      } else {
        if (current_float_constant_map_vertex_[float_constant_index >> 6] &
            (1ull << (float_constant_index & 63))) {
          cbuffer_binding_float_vertex_.up_to_date = false;
        }
      }
    }
  } else if (index >= XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031 &&
             index <= XE_GPU_REG_SHADER_CONSTANT_LOOP_31) {
    cbuffer_binding_bool_loop_.up_to_date = false;
  } else if (index >= XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 &&
             index <= XE_GPU_REG_SHADER_CONSTANT_FETCH_31_5) {
    cbuffer_binding_fetch_.up_to_date = false;
    if (texture_cache_ != nullptr) {
      texture_cache_->TextureFetchConstantWritten((index - XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0) /
                                                  6);
    }
    InvalidateVertexBufferResidency((index - XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0) / 2);
  }
}

void D3D12CommandProcessor::WriteRegistersFromMem(uint32_t start_index, uint32_t* base,
                                                  uint32_t num_registers) {
  if (!num_registers) {
    return;
  }
  uint32_t end_index = start_index + num_registers - 1;

  auto range_has_any_constant_usage = [](const uint64_t* usage_map, uint32_t first_constant,
                                         uint32_t last_constant) -> bool {
    if (first_constant > last_constant) {
      return false;
    }
    uint32_t first_word = first_constant >> 6;
    uint32_t last_word = last_constant >> 6;
    uint32_t first_bit = first_constant & 63;
    uint32_t last_bit = last_constant & 63;
    if (first_word == last_word) {
      uint32_t bit_count = last_bit - first_bit + 1;
      uint64_t mask = bit_count == 64 ? UINT64_MAX : ((UINT64_C(1) << bit_count) - 1) << first_bit;
      return (usage_map[first_word] & mask) != 0;
    }
    if (usage_map[first_word] & (UINT64_MAX << first_bit)) {
      return true;
    }
    for (uint32_t word = first_word + 1; word < last_word; ++word) {
      if (usage_map[word]) {
        return true;
      }
    }
    uint64_t last_mask = last_bit == 63 ? UINT64_MAX : ((UINT64_C(1) << (last_bit + 1)) - 1);
    return (usage_map[last_word] & last_mask) != 0;
  };

  if (start_index >= XE_GPU_REG_SHADER_CONSTANT_000_X &&
      end_index <= XE_GPU_REG_SHADER_CONSTANT_511_W) {
    memory::copy_and_swap(register_file_->values + start_index, base, num_registers);
    if (track_constants_) {
      pending_constants_.MarkWritten(start_index, end_index);
    }
    if (frame_open_) {
      uint32_t first_float_constant = (start_index - XE_GPU_REG_SHADER_CONSTANT_000_X) >> 2;
      uint32_t last_float_constant = (end_index - XE_GPU_REG_SHADER_CONSTANT_000_X) >> 2;
      if (first_float_constant < 256) {
        uint32_t last_vertex_constant = std::min(last_float_constant, 255u);
        if (range_has_any_constant_usage(current_float_constant_map_vertex_, first_float_constant,
                                         last_vertex_constant)) {
          cbuffer_binding_float_vertex_.up_to_date = false;
        }
      }
      if (last_float_constant >= 256) {
        uint32_t first_pixel_constant =
            first_float_constant >= 256 ? first_float_constant - 256 : 0;
        uint32_t last_pixel_constant = last_float_constant - 256;
        if (range_has_any_constant_usage(current_float_constant_map_pixel_, first_pixel_constant,
                                         last_pixel_constant)) {
          cbuffer_binding_float_pixel_.up_to_date = false;
        }
      }
    }
    return;
  }

  if (start_index >= XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031 &&
      end_index <= XE_GPU_REG_SHADER_CONSTANT_LOOP_31) {
    memory::copy_and_swap(register_file_->values + start_index, base, num_registers);
    cbuffer_binding_bool_loop_.up_to_date = false;
    if (track_constants_) {
      pending_constants_.MarkWritten(start_index, end_index);
    }
    return;
  }

  if (start_index >= XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 &&
      end_index <= XE_GPU_REG_SHADER_CONSTANT_FETCH_31_5 &&
      REXCVAR_GET(d3d12_reuse_draw_bindings)) {
    // The title rewrites fetch constants at every draw, mostly with the values
    // they have: only the fetch constants whose dwords change invalidate what
    // depends on them.
    uint32_t changed_textures = 0;
    uint32_t first_changed = UINT32_MAX, last_changed = 0;
    for (uint32_t i = 0; i < num_registers; ++i) {
      const uint32_t value = memory::load_and_swap<uint32_t>(base + i);
      uint32_t& stored = register_file_->values[start_index + i];
      if (stored == value) {
        continue;
      }
      stored = value;
      const uint32_t fetch_dword = start_index + i - XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0;
      changed_textures |= UINT32_C(1) << (fetch_dword / 6);
      first_changed = std::min(first_changed, fetch_dword);
      last_changed = fetch_dword;
    }
    perf_trace::fetch_writes_unchanged += !changed_textures;
    if (!changed_textures) {
      return;
    }
    if (track_constants_) {
      pending_constants_.MarkWritten(XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + first_changed,
                                     XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + last_changed);
    }
    cbuffer_binding_fetch_.up_to_date = false;
    if (texture_cache_) {
      uint32_t index;
      while (rex::bit_scan_forward(changed_textures, &index)) {
        changed_textures &= ~(UINT32_C(1) << index);
        texture_cache_->TextureFetchConstantWritten(index);
      }
    }
    InvalidateVertexBufferResidencyRange(first_changed / 2, last_changed / 2);
    return;
  }

  if (start_index >= XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 &&
      end_index <= XE_GPU_REG_SHADER_CONSTANT_FETCH_31_5) {
    memory::copy_and_swap(register_file_->values + start_index, base, num_registers);
    cbuffer_binding_fetch_.up_to_date = false;
    if (track_constants_) {
      pending_constants_.MarkWritten(start_index, end_index);
    }
    uint32_t first_fetch_dword = start_index - XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0;
    uint32_t last_fetch_dword = end_index - XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0;
    if (texture_cache_) {
      texture_cache_->TextureFetchConstantsWritten(first_fetch_dword / 6, last_fetch_dword / 6);
    }
    InvalidateVertexBufferResidencyRange(first_fetch_dword / 2, last_fetch_dword / 2);
    return;
  }

  CommandProcessor::WriteRegistersFromMem(start_index, base, num_registers);
}

void D3D12CommandProcessor::OnGammaRamp256EntryTableValueWritten() {
  gamma_ramp_256_entry_table_up_to_date_ = false;
}

void D3D12CommandProcessor::OnGammaRampPWLValueWritten() {
  gamma_ramp_pwl_up_to_date_ = false;
}

void D3D12CommandProcessor::IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                                      uint32_t frontbuffer_height) {
  SCOPE_profile_cpu_f("gpu");
  perf_trace::OnSwap();
  pm4_stats::OnSwap();
  perf_trace::ZoneScope perf_zone(perf_trace::Zone::kSwap);
  rov_occlusion_frame_interval_ = 0;
  vertex_buffers_in_sync_[0] = 0;
  vertex_buffers_in_sync_[1] = 0;
  if (DrawDepthHistogram* histogram = GetDrawDepthHistogram()) {
    // Every 60th frame keeps the file small.
    if (histogram->frame++ % 60 == 0) {
      histogram->output << histogram->frame;
      for (const auto& [key, count] : histogram->counts) {
        histogram->output << ' ' << std::hex << key << std::dec << ':' << count;
      }
      histogram->output << std::endl;
    }
    histogram->counts.clear();
  }
  if (GpuInventory* inventory = GetGpuInventory()) {
    if (++inventory->frame % 600 == 0) WriteGpuInventory(*inventory);
  }
  shared_memory_->EndStalePageFrame();
  DrawDumpSwap();
  if (draw_stats_output_) {
    if (draw_stats_recording_) {
      draw_stats_recording_ = false;
      if (!draw_stats_draws_.empty() && submission_open_) {
        deferred_command_list_.D3DResolveQueryData(
            draw_stats_heap_.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0,
            uint32_t(draw_stats_draws_.size()), draw_stats_readback_.Get(), 0);
        draw_stats_submission_ = submission_current_;
        draw_stats_frame_recorded_ = draw_stats_frame_;
      } else {
        draw_stats_draws_.clear();
      }
    }
    if (++draw_stats_frame_ % draw_stats_every_ == 0 && !draw_stats_submission_) {
      draw_stats_recording_ = true;
      draw_stats_draws_.clear();
      draw_stats_frame_ms_ = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
    }
  }
  if (FrameGraphSwap() && frame_graph_recording_) {
    FrameGraphMark(kFrameGraphMarkSwap);
    frame_graph_closing_ = true;
  }
  if (TransferDrawTrace* transfer_draws = GetTransferDrawTrace()) {
    if (++transfer_draws->frame % transfer_draws->every == 0) {
      transfer_draws->output << std::flush;
    }
  }
  if (DrawListTrace* draw_list = GetDrawListTrace()) {
    draw_list->after_frame = draw_list->after_drawn;
    draw_list->after_drawn = false;
    if (++draw_list->frame % draw_list->every == 0 || draw_list->after_frame) {
      draw_list->output << "F " << draw_list->frame << ' '
                        << std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count()
                        << '\n';
    }
  }

  if (!graphics_system_)
    return;
  ui::Presenter* presenter = graphics_system_->presenter();
  if (!presenter) {
    REXGPU_ERROR("IssueSwap: presenter is null");
    return;
  }

  // In case the swap command is the only one in the frame.
  if (!BeginSubmission(true)) {
    REXGPU_ERROR("IssueSwap: BeginSubmission failed");
    return;
  }
  if (post_passes_) {
    // The end of the guest frame: history, cleared outputs, hot reload.
    post_passes_->OnSwap();
  }

  // Obtain the actual swap source texture size (resolution-scaled if it's a
  // resolve destination, or not otherwise).
  D3D12_SHADER_RESOURCE_VIEW_DESC swap_texture_srv_desc;
  xenos::TextureFormat frontbuffer_format;
  uint32_t frontbuffer_width_unscaled = 0, frontbuffer_height_unscaled = 0;
  ID3D12Resource* swap_texture_resource =
      texture_cache_->RequestSwapTexture(swap_texture_srv_desc, frontbuffer_format,
                                         &frontbuffer_width_unscaled, &frontbuffer_height_unscaled);
  if (!swap_texture_resource) {
    // Dump texture fetch constant 0 for debugging
    const auto& regs = *register_file_;
    auto fetch = regs.GetTextureFetch(0);
    REXGPU_ERROR(
        "IssueSwap: RequestSwapTexture failed - fetch0: {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}",
        fetch.dword_0, fetch.dword_1, fetch.dword_2, fetch.dword_3, fetch.dword_4, fetch.dword_5);
    return;
  }
  D3D12_RESOURCE_DESC swap_texture_desc = swap_texture_resource->GetDesc();
  // The swap gamma / FXAA pass samples source texels by pixel index, but swap
  // textures may be allocation-padded. Prefer the active frontbuffer region
  // from the swap packet, scaled proportionally to the actual source texture.
  uint32_t source_width_scaled = uint32_t(swap_texture_desc.Width);
  uint32_t source_height_scaled = uint32_t(swap_texture_desc.Height);
  auto get_active_swap_dimension = [](uint32_t packet_unscaled, uint32_t source_unscaled,
                                      uint32_t source_scaled) -> uint32_t {
    if (!source_scaled) {
      return 0;
    }
    uint32_t active_unscaled = packet_unscaled ? packet_unscaled : source_unscaled;
    if (!active_unscaled) {
      return source_scaled;
    }
    if (source_unscaled) {
      active_unscaled = std::min(active_unscaled, source_unscaled);
      uint64_t active_scaled =
          (uint64_t(active_unscaled) * source_scaled + (source_unscaled >> 1)) / source_unscaled;
      return uint32_t(std::clamp<uint64_t>(active_scaled, 1, source_scaled));
    }
    return std::min(active_unscaled, source_scaled);
  };
  uint32_t guest_output_width =
      get_active_swap_dimension(frontbuffer_width, frontbuffer_width_unscaled, source_width_scaled);
  uint32_t guest_output_height = get_active_swap_dimension(
      frontbuffer_height, frontbuffer_height_unscaled, source_height_scaled);
  if (!guest_output_width) {
    guest_output_width = source_width_scaled
                             ? source_width_scaled
                             : (frontbuffer_width ? frontbuffer_width : frontbuffer_width_unscaled);
  }
  if (!guest_output_height) {
    guest_output_height = source_height_scaled ? source_height_scaled
                                               : (frontbuffer_height ? frontbuffer_height
                                                                     : frontbuffer_height_unscaled);
  }
  bool swap_source_scaled = frontbuffer_width_unscaled && frontbuffer_height_unscaled &&
                            (source_width_scaled != frontbuffer_width_unscaled ||
                             source_height_scaled != frontbuffer_height_unscaled);
  if (texture_cache_->IsDrawResolutionScaled() && !swap_source_scaled) {
    static bool draw_scale_swap_unscaled_logged = false;
    if (!draw_scale_swap_unscaled_logged) {
      draw_scale_swap_unscaled_logged = true;
      REXGPU_WARN(
          "D3D12 draw resolution scaling is enabled, but the swap source is "
          "unscaled ({}x{}). This title may be presenting from an unscaled "
          "resolve path.",
          guest_output_width, guest_output_height);
    }
  }

  system::X_VIDEO_MODE video_mode;
  kernel::xboxkrnl::VdQueryVideoMode(&video_mode);
  uint32_t display_width = std::max(uint32_t(1), uint32_t(video_mode.display_width));
  uint32_t display_height = std::max(uint32_t(1), uint32_t(video_mode.display_height));
  // A frame at the scene's logical width (18.21): its aspect, not the mode's.
  const uint32_t logical_width = render_target_cache_->scene_logical_width();
  if (logical_width != 1280 && frontbuffer_width_unscaled == 1280 &&
      source_width_scaled == texture_cache_->SceneTextureHostWidth(1280)) {
    display_width = display_width * logical_width / 1280;
  }
  const uint32_t logical_height = render_target_cache_->scene_logical_height();
  if (logical_height != 720 && frontbuffer_height_unscaled == 720 &&
      source_height_scaled == texture_cache_->SceneTextureHostHeight(720)) {
    display_height = display_height * logical_height / 720;
  }

  // With d3d12_submit_thread_async_swap, the frame is presented on the submit
  // thread after the swap submission.
  ui::Presenter::GuestOutputPaintDeferrer paint_deferrer;
  if (submit_async_swap_) {
    paint_deferrer = [this](std::function<void()> present) { DeferPresent(std::move(present)); };
  }
  presenter->RefreshGuestOutput(
      guest_output_width, guest_output_height, display_width, display_height,
      [this, &swap_texture_srv_desc, frontbuffer_format, swap_texture_resource, guest_output_width,
       guest_output_height](ui::Presenter::GuestOutputRefreshContext& context) -> bool {
        const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
        ID3D12Device* device = provider.GetDevice();

        // With d3d12_submit_thread_async_swap, whichever way this returns,
        // submit the swap as a submit thread job, and let the presenter signal
        // its fence on that thread after it.
        std::function<void()> submitted_callback;
        if (submit_async_swap_) {
          submitted_callback =
              static_cast<ui::d3d12::D3D12Presenter::D3D12GuestOutputRefreshContext&>(context)
                  .TakeSubmittedCallback();
        }
        struct SubmittedCallbackGuard {
          std::function<void()> on_exit;
          ~SubmittedCallbackGuard() { on_exit(); }
        } submitted_callback_guard{[this, &submitted_callback]() {
          if (submitted_callback) {
            EndSubmission(true);
            EnqueueSubmitCallback(std::move(submitted_callback));
          }
        }};

        SwapPostEffect swap_post_effect = GetActualSwapPostEffect();
        bool use_fxaa = swap_post_effect == SwapPostEffect::kFxaa ||
                        swap_post_effect == SwapPostEffect::kFxaaExtreme;
        if (use_fxaa) {
          // Make sure the texture of the correct size is available for FXAA.
          if (fxaa_source_texture_) {
            D3D12_RESOURCE_DESC fxaa_source_texture_desc = fxaa_source_texture_->GetDesc();
            if (fxaa_source_texture_desc.Width != guest_output_width ||
                fxaa_source_texture_desc.Height != guest_output_height) {
              if (submission_completed_ < fxaa_source_texture_submission_) {
                fxaa_source_texture_->AddRef();
                resources_for_deletion_.emplace_back(fxaa_source_texture_submission_,
                                                     fxaa_source_texture_.Get());
              }
              fxaa_source_texture_.Reset();
              fxaa_source_texture_submission_ = 0;
            }
          }
          if (!fxaa_source_texture_) {
            D3D12_RESOURCE_DESC fxaa_source_texture_desc;
            fxaa_source_texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            fxaa_source_texture_desc.Alignment = 0;
            fxaa_source_texture_desc.Width = guest_output_width;
            fxaa_source_texture_desc.Height = guest_output_height;
            fxaa_source_texture_desc.DepthOrArraySize = 1;
            fxaa_source_texture_desc.MipLevels = 1;
            fxaa_source_texture_desc.Format = kFxaaSourceTextureFormat;
            fxaa_source_texture_desc.SampleDesc.Count = 1;
            fxaa_source_texture_desc.SampleDesc.Quality = 0;
            fxaa_source_texture_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            fxaa_source_texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            if (FAILED(device->CreateCommittedResource(
                    &ui::d3d12::util::kHeapPropertiesDefault, provider.GetHeapFlagCreateNotZeroed(),
                    &fxaa_source_texture_desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    nullptr, IID_PPV_ARGS(&fxaa_source_texture_)))) {
              REXGPU_ERROR("Failed to create the FXAA input texture");
              swap_post_effect = SwapPostEffect::kNone;
              use_fxaa = false;
            }
          }
        }

        // This is according to D3D::InitializePresentationParameters from a
        // game executable, which initializes the 256-entry table gamma ramp for
        // 8_8_8_8 output and the PWL gamma ramp for 2_10_10_10.
        // TODO(Triang3l): Choose between the table and PWL based on
        // DC_LUTA_CONTROL, support both for all formats (and also different
        // increments for PWL).
        bool use_pwl_gamma_ramp =
            frontbuffer_format == xenos::TextureFormat::k_2_10_10_10 ||
            frontbuffer_format == xenos::TextureFormat::k_2_10_10_10_AS_16_16_16_16;

        context.SetIs8bpc(!use_pwl_gamma_ramp && !use_fxaa);

        // Upload the new gamma ramp, using the upload buffer for the current
        // frame (will close the frame after this anyway, so can't write
        // multiple times per frame).
        if (!(use_pwl_gamma_ramp ? gamma_ramp_pwl_up_to_date_
                                 : gamma_ramp_256_entry_table_up_to_date_)) {
          uint32_t gamma_ramp_offset_bytes = use_pwl_gamma_ramp ? 256 * 4 : 0;
          uint32_t gamma_ramp_upload_offset_bytes =
              uint32_t(frame_current_ % kQueueFrames) * ((256 + 128 * 3) * 4) +
              gamma_ramp_offset_bytes;
          uint32_t gamma_ramp_size_bytes = (use_pwl_gamma_ramp ? 128 * 3 : 256) * 4;
          if (std::endian::native != std::endian::little && use_pwl_gamma_ramp) {
            // R16G16 is first R16, where the shader expects the base, and
            // second G16, where the delta should be, but gamma_ramp_pwl_rgb()
            // is an array of 32-bit DC_LUT_PWL_DATA registers - swap 16 bits in
            // each 32.
            auto gamma_ramp_pwl_upload_buffer = reinterpret_cast<reg::DC_LUT_PWL_DATA*>(
                gamma_ramp_upload_buffer_mapping_ + gamma_ramp_upload_offset_bytes);
            const reg::DC_LUT_PWL_DATA* gamma_ramp_pwl = gamma_ramp_pwl_rgb();
            for (size_t i = 0; i < 128 * 3; ++i) {
              reg::DC_LUT_PWL_DATA& gamma_ramp_pwl_upload_buffer_entry =
                  gamma_ramp_pwl_upload_buffer[i];
              reg::DC_LUT_PWL_DATA gamma_ramp_pwl_entry = gamma_ramp_pwl[i];
              gamma_ramp_pwl_upload_buffer_entry.base = gamma_ramp_pwl_entry.delta;
              gamma_ramp_pwl_upload_buffer_entry.delta = gamma_ramp_pwl_entry.base;
            }
          } else {
            std::memcpy(gamma_ramp_upload_buffer_mapping_ + gamma_ramp_upload_offset_bytes,
                        use_pwl_gamma_ramp ? static_cast<const void*>(gamma_ramp_pwl_rgb())
                                           : static_cast<const void*>(gamma_ramp_256_entry_table()),
                        gamma_ramp_size_bytes);
          }
          PushTransitionBarrier(gamma_ramp_buffer_.Get(), gamma_ramp_buffer_state_,
                                D3D12_RESOURCE_STATE_COPY_DEST);
          gamma_ramp_buffer_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
          SubmitBarriers();
          deferred_command_list_.D3DCopyBufferRegion(
              gamma_ramp_buffer_.Get(), gamma_ramp_offset_bytes, gamma_ramp_upload_buffer_.Get(),
              gamma_ramp_upload_offset_bytes, gamma_ramp_size_bytes);
          (use_pwl_gamma_ramp ? gamma_ramp_pwl_up_to_date_
                              : gamma_ramp_256_entry_table_up_to_date_) = true;
        }

        // Destination, source, and if bindful, gamma ramp.
        ui::d3d12::util::DescriptorCpuGpuHandlePair apply_gamma_descriptors[3];
        ui::d3d12::util::DescriptorCpuGpuHandlePair apply_gamma_descriptor_gamma_ramp;
        if (!RequestOneUseSingleViewDescriptors(bindless_resources_used_ ? 2 : 3,
                                                apply_gamma_descriptors)) {
          return false;
        }
        // Must not call anything that can change the descriptor heap from now
        // on!
        if (bindless_resources_used_) {
          apply_gamma_descriptor_gamma_ramp = GetSystemBindlessViewHandlePair(
              use_pwl_gamma_ramp ? SystemBindlessView::kGammaRampPWLSRV
                                 : SystemBindlessView::kGammaRampTableSRV);
        } else {
          apply_gamma_descriptor_gamma_ramp = apply_gamma_descriptors[2];
          WriteGammaRampSRV(use_pwl_gamma_ramp, apply_gamma_descriptor_gamma_ramp.first);
        }

        ID3D12Resource* guest_output_resource =
            static_cast<ui::d3d12::D3D12Presenter::D3D12GuestOutputRefreshContext&>(context)
                .resource_uav_capable();

        if (use_fxaa) {
          fxaa_source_texture_submission_ = submission_current_;
        }

        ID3D12Resource* apply_gamma_dest =
            use_fxaa ? fxaa_source_texture_.Get() : guest_output_resource;
        D3D12_RESOURCE_STATES apply_gamma_dest_initial_state =
            use_fxaa ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
                     : ui::d3d12::D3D12Presenter::kGuestOutputInternalState;
        static_cast<ui::d3d12::D3D12Presenter::D3D12GuestOutputRefreshContext&>(context)
            .resource_uav_capable();
        PushTransitionBarrier(apply_gamma_dest, apply_gamma_dest_initial_state,
                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        // From now on, even in case of failure, apply_gamma_dest must be
        // transitioned back to apply_gamma_dest_initial_state!
        D3D12_UNORDERED_ACCESS_VIEW_DESC apply_gamma_dest_uav_desc;
        apply_gamma_dest_uav_desc.Format =
            use_fxaa ? kFxaaSourceTextureFormat : ui::d3d12::D3D12Presenter::kGuestOutputFormat;
        apply_gamma_dest_uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        apply_gamma_dest_uav_desc.Texture2D.MipSlice = 0;
        apply_gamma_dest_uav_desc.Texture2D.PlaneSlice = 0;
        device->CreateUnorderedAccessView(apply_gamma_dest, nullptr, &apply_gamma_dest_uav_desc,
                                          apply_gamma_descriptors[0].first);

        device->CreateShaderResourceView(swap_texture_resource, &swap_texture_srv_desc,
                                         apply_gamma_descriptors[1].first);

        PushTransitionBarrier(gamma_ramp_buffer_.Get(), gamma_ramp_buffer_state_,
                              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        gamma_ramp_buffer_state_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

        // The swap's passes (perf trace gpu swap_ms): only queries from here on,
        // which leave the descriptor heap as it is.
        const uint32_t swap_timestamp = BeginGpuTimestamp(GpuTimestampKind::kSwap);
        deferred_command_list_.D3DSetComputeRootSignature(apply_gamma_root_signature_.Get());
        ApplyGammaConstants apply_gamma_constants;
        apply_gamma_constants.size[0] = guest_output_width;
        apply_gamma_constants.size[1] = guest_output_height;
        deferred_command_list_.D3DSetComputeRoot32BitConstants(
            UINT(ApplyGammaRootParameter::kConstants),
            sizeof(apply_gamma_constants) / sizeof(uint32_t), &apply_gamma_constants, 0);
        deferred_command_list_.D3DSetComputeRootDescriptorTable(
            UINT(ApplyGammaRootParameter::kDestination), apply_gamma_descriptors[0].second);
        deferred_command_list_.D3DSetComputeRootDescriptorTable(
            UINT(ApplyGammaRootParameter::kSource), apply_gamma_descriptors[1].second);
        deferred_command_list_.D3DSetComputeRootDescriptorTable(
            UINT(ApplyGammaRootParameter::kRamp), apply_gamma_descriptor_gamma_ramp.second);
        ID3D12PipelineState* apply_gamma_pipeline;
        if (use_pwl_gamma_ramp) {
          apply_gamma_pipeline = use_fxaa ? apply_gamma_pwl_fxaa_luma_pipeline_.Get()
                                          : apply_gamma_pwl_pipeline_.Get();
        } else {
          apply_gamma_pipeline = use_fxaa ? apply_gamma_table_fxaa_luma_pipeline_.Get()
                                          : apply_gamma_table_pipeline_.Get();
        }
        SetExternalPipeline(apply_gamma_pipeline);
        SubmitBarriers();
        uint32_t group_count_x = (guest_output_width + 15) / 16;
        uint32_t group_count_y = (guest_output_height + 7) / 8;
        deferred_command_list_.D3DDispatch(group_count_x, group_count_y, 1);

        // Apply FXAA.
        if (use_fxaa) {
          // Destination and source.
          ui::d3d12::util::DescriptorCpuGpuHandlePair fxaa_descriptors[2];
          if (!RequestOneUseSingleViewDescriptors(uint32_t(rex::countof(fxaa_descriptors)),
                                                  fxaa_descriptors)) {
            // Failed to obtain descriptors for FXAA - just copy after gamma
            // ramp application without applying FXAA.
            PushTransitionBarrier(apply_gamma_dest, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                  D3D12_RESOURCE_STATE_COPY_SOURCE);
            PushTransitionBarrier(guest_output_resource,
                                  ui::d3d12::D3D12Presenter::kGuestOutputInternalState,
                                  D3D12_RESOURCE_STATE_COPY_DEST);
            SubmitBarriers();
            deferred_command_list_.D3DCopyResource(guest_output_resource, apply_gamma_dest);
            PushTransitionBarrier(apply_gamma_dest, D3D12_RESOURCE_STATE_COPY_SOURCE,
                                  apply_gamma_dest_initial_state);
            PushTransitionBarrier(guest_output_resource, D3D12_RESOURCE_STATE_COPY_DEST,
                                  ui::d3d12::D3D12Presenter::kGuestOutputInternalState);
            EndGpuTimestamp(swap_timestamp);
            return false;
          } else {
            assert_true(apply_gamma_dest_initial_state ==
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            PushTransitionBarrier(apply_gamma_dest, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                  apply_gamma_dest_initial_state);
            PushTransitionBarrier(guest_output_resource,
                                  ui::d3d12::D3D12Presenter::kGuestOutputInternalState,
                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            // From now on, even in case of failure, guest_output_resource must
            // be transitioned back to kGuestOutputInternalState!
            deferred_command_list_.D3DSetComputeRootSignature(fxaa_root_signature_.Get());
            FxaaConstants fxaa_constants;
            fxaa_constants.size[0] = guest_output_width;
            fxaa_constants.size[1] = guest_output_height;
            fxaa_constants.size_inv[0] = 1.0f / float(fxaa_constants.size[0]);
            fxaa_constants.size_inv[1] = 1.0f / float(fxaa_constants.size[1]);
            deferred_command_list_.D3DSetComputeRoot32BitConstants(
                UINT(FxaaRootParameter::kConstants), sizeof(fxaa_constants) / sizeof(uint32_t),
                &fxaa_constants, 0);
            D3D12_UNORDERED_ACCESS_VIEW_DESC fxaa_dest_uav_desc;
            fxaa_dest_uav_desc.Format = ui::d3d12::D3D12Presenter::kGuestOutputFormat;
            fxaa_dest_uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            fxaa_dest_uav_desc.Texture2D.MipSlice = 0;
            fxaa_dest_uav_desc.Texture2D.PlaneSlice = 0;
            device->CreateUnorderedAccessView(guest_output_resource, nullptr, &fxaa_dest_uav_desc,
                                              fxaa_descriptors[0].first);
            deferred_command_list_.D3DSetComputeRootDescriptorTable(
                UINT(FxaaRootParameter::kDestination), fxaa_descriptors[0].second);
            D3D12_SHADER_RESOURCE_VIEW_DESC fxaa_source_srv_desc;
            fxaa_source_srv_desc.Format = kFxaaSourceTextureFormat;
            fxaa_source_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            fxaa_source_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            fxaa_source_srv_desc.Texture2D.MostDetailedMip = 0;
            fxaa_source_srv_desc.Texture2D.MipLevels = 1;
            fxaa_source_srv_desc.Texture2D.PlaneSlice = 0;
            fxaa_source_srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
            device->CreateShaderResourceView(fxaa_source_texture_.Get(), &fxaa_source_srv_desc,
                                             fxaa_descriptors[1].first);
            deferred_command_list_.D3DSetComputeRootDescriptorTable(
                UINT(FxaaRootParameter::kSource), fxaa_descriptors[1].second);
            SetExternalPipeline(swap_post_effect == SwapPostEffect::kFxaaExtreme
                                    ? fxaa_extreme_pipeline_.Get()
                                    : fxaa_pipeline_.Get());
            SubmitBarriers();
            deferred_command_list_.D3DDispatch(group_count_x, group_count_y, 1);
            PushTransitionBarrier(guest_output_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                  ui::d3d12::D3D12Presenter::kGuestOutputInternalState);
          }
        } else {
          assert_true(apply_gamma_dest_initial_state ==
                      ui::d3d12::D3D12Presenter::kGuestOutputInternalState);
          PushTransitionBarrier(apply_gamma_dest, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                apply_gamma_dest_initial_state);
        }

        // Need to submit all the commands before giving the image back to the
        // presenter so it can submit its own commands for displaying it to the
        // queue.
        SubmitBarriers();
        EndGpuTimestamp(swap_timestamp);
        EndSubmission(true);
        return true;
      },
      paint_deferrer);

  // End the frame even if did not present for any reason (the image refresher
  // was not called), to prevent leaking per-frame resources.
  EndSubmission(true);
}

void D3D12CommandProcessor::OnPrimaryBufferEnd() {
  if (REXCVAR_GET(d3d12_submit_on_primary_buffer_end) && submission_open_ &&
      CanEndSubmissionImmediately()) {
    EndSubmission(false);
  }
  if (!rov_occlusion_pending_.empty()) {
    CheckSubmissionFence(0);
  }
}

Shader* D3D12CommandProcessor::LoadShader(xenos::ShaderType shader_type, uint32_t guest_address,
                                          const uint32_t* host_address, uint32_t dword_count) {
  return pipeline_cache_->LoadShader(shader_type, host_address, dword_count);
}

bool D3D12CommandProcessor::IssueDraw(xenos::PrimitiveType primitive_type, uint32_t index_count,
                                      IndexBufferInfo* index_buffer_info,
                                      bool major_mode_explicit) {
#if XE_GPU_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_GPU_FINE_GRAINED_DRAW_SCOPES
  perf_trace::CountDraw();
  perf_trace::ZoneScope perf_zone(perf_trace::Zone::kDraw);
  // The parts of the draw zone outside its named zones (N2, d3d-hle.md 15.6):
  // setup (shader analysis, the submission) before the primitives, state
  // (pipeline bind, viewport and fixed function state) between the textures
  // and the bindings (which include the system constants), and the call
  // (topology, memexport, the draw itself) after the vertex buffers.
  const uint64_t setup_start = perf_trace::DetailZoneStart();
  if (submission_open_ && submit_thread_.joinable()) {
    const int32_t interval = REXCVAR_GET(d3d12_submit_draw_interval);
    if (interval > 0 && ++submission_draws_ >= uint32_t(interval) &&
        CanEndSubmissionImmediately()) {
      EndSubmission(false);
    }
  }

  ID3D12Device* device = GetD3D12Provider().GetDevice();
  const RegisterFile& regs = *register_file_;

  xenos::EdramMode edram_mode = regs.Get<reg::RB_MODECONTROL>().edram_mode;
  DrawListEntry draw_list_entry;
  if (DrawListTrace* draw_list = GetSampledDrawListTrace()) {
    draw_list_entry.trace = draw_list;
    uint32_t* values = draw_list_entry.values;
    values[2] = index_count;
    values[3] = index_buffer_info ? index_buffer_info->guest_base : 0;
    values[5] = regs.values[reg::RB_COLOR_INFO::rt_register_indices[0]];
    values[6] = regs.values[XE_GPU_REG_RB_DEPTH_INFO];
    values[7] = regs.values[XE_GPU_REG_PA_SC_WINDOW_OFFSET];
    values[8] = uint32_t(edram_mode);
    values[9] = regs.values[XE_GPU_REG_RB_DEPTHCONTROL];
    values[10] = regs.values[XE_GPU_REG_RB_STENCILREFMASK];
    values[11] = regs.values[XE_GPU_REG_PA_CL_VPORT_ZSCALE];
    values[12] = regs.values[XE_GPU_REG_PA_CL_VPORT_ZOFFSET];
    values[13] = regs.values[XE_GPU_REG_RB_BLENDCONTROL0];
    values[14] = regs.values[XE_GPU_REG_RB_COLOR_MASK];
  }
  if (edram_mode == xenos::EdramMode::kCopy) {
    // Special copy handling.
    draw_list_entry.outcome = "copy";
    return IssueCopy();
  }

  bool surface_pitch_is_zero = regs.Get<reg::RB_SURFACE_INFO>().surface_pitch == 0;
  if (rtv_occlusion_open_query_ != UINT32_MAX) {
    rtv_occlusion_segment_msaa_ =
        std::max(rtv_occlusion_segment_msaa_,
                 uint32_t(regs.Get<reg::RB_SURFACE_INFO>().msaa_samples) + 1);
  }

  // Vertex shader analysis.
  auto vertex_shader = static_cast<D3D12Shader*>(active_vertex_shader());
  if (!vertex_shader) {
    // Always need a vertex shader.
    return false;
  }
  pipeline_cache_->AnalyzeShaderUcode(*vertex_shader);
  if (post_passes_) {
    post_passes_->OnDraw(regs, vertex_shader->ucode_data_hash(),
                         active_pixel_shader() ? active_pixel_shader()->ucode_data_hash() : 0,
                         vertex_shader->constant_register_map().float_bitmap);
  }
  if (DrawListTrace* draw_list = GetDrawListTrace()) {
    const uint64_t vertex_shader_hash = vertex_shader->ucode_data_hash();
    const uint64_t pixel_shader_hash =
        active_pixel_shader() ? active_pixel_shader()->ucode_data_hash() : 0;
    if (pixel_shader_hash && pixel_shader_hash == draw_list->after_pixel_shader) {
      draw_list->after_drawn = true;
    }
    if (draw_list_entry.trace) {
      uint32_t* values = draw_list_entry.values;
      values[0] = uint32_t(vertex_shader_hash);
      values[1] = uint32_t(pixel_shader_hash);
      if (!vertex_shader->vertex_bindings().empty()) {
        values[4] =
            regs.GetVertexFetch(vertex_shader->vertex_bindings()[0].fetch_constant).address << 2;
      }
      std::vector<uint32_t>& constants = draw_list_entry.constants;
      for (const DrawListTrace::Constants& range : draw_list->constants) {
        uint32_t first;
        if (range.shader_hash == vertex_shader_hash) {
          first = range.first;
        } else if (range.shader_hash == pixel_shader_hash) {
          first = 256 + range.first;
        } else {
          continue;
        }
        const uint32_t* source = &regs.values[XE_GPU_REG_SHADER_CONSTANT_000_X + first * 4];
        constants.push_back(first);
        constants.push_back(range.count * 4);
        constants.insert(constants.end(), source, source + range.count * 4);
      }
    }
  }
  bool memexport_used_vertex = vertex_shader->memexport_eM_written() != 0;
  std::ofstream* draw_trace = OcclusionDrawTrace();
  if (DrawDepthHistogram* histogram = GetDrawDepthHistogram()) {
    const uint32_t depth = regs.values[XE_GPU_REG_RB_DEPTHCONTROL];
    ++histogram->counts[uint32_t(edram_mode) << 12 | (depth & 0x76)];
  }
  if (draw_trace) {
    *draw_trace << '\n' << "draw prim " << uint32_t(primitive_type) << " indices " << index_count
                << " indexed " << (index_buffer_info != nullptr) << " vs " << std::hex
                << vertex_shader->ucode_data_hash() << " ps "
                << (active_pixel_shader() ? active_pixel_shader()->ucode_data_hash() : 0)
                << " raster " << draw_util::IsRasterizationPotentiallyDone(
                                     regs, draw_util::IsPrimitivePolygonal(regs));
    static const std::pair<const char*, uint32_t> kRegs[] = {
        {"mode", XE_GPU_REG_RB_MODECONTROL}, {"depth", XE_GPU_REG_RB_DEPTHCONTROL},
        {"cmask", XE_GPU_REG_RB_COLOR_MASK}, {"cctl", XE_GPU_REG_RB_COLORCONTROL},
        {"sc", XE_GPU_REG_PA_SU_SC_MODE_CNTL}, {"clip", XE_GPU_REG_PA_CL_CLIP_CNTL},
        {"vte", XE_GPU_REG_PA_CL_VTE_CNTL}, {"viz", XE_GPU_REG_PA_SC_VIZ_QUERY},
        {"wtl", XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL}, {"wbr", XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR},
        {"woff", XE_GPU_REG_PA_SC_WINDOW_OFFSET}, {"surf", XE_GPU_REG_RB_SURFACE_INFO},
        {"dinfo", XE_GPU_REG_RB_DEPTH_INFO}, {"sref", XE_GPU_REG_RB_STENCILREFMASK},
        {"xs", XE_GPU_REG_PA_CL_VPORT_XSCALE}, {"xo", XE_GPU_REG_PA_CL_VPORT_XOFFSET},
        {"zs", XE_GPU_REG_PA_CL_VPORT_ZSCALE}, {"zo", XE_GPU_REG_PA_CL_VPORT_ZOFFSET}};
    for (const auto& [name, index] : kRegs) *draw_trace << ' ' << name << ' ' << regs.values[index];
    *draw_trace << std::dec;
    // Clip-space corners of the 4-vertex query sprite, evaluated like its
    // vertex shader: ((pos * c3) * c1 + c2) transformed by c128..c131.
    if (index_count == 4 && index_buffer_info &&
        vertex_shader->ucode_data_hash() == UINT64_C(0x6ded9331bb36d0f4)) {
      const auto c = [&](uint32_t i, uint32_t k) {
        return regs.Get<float>(XE_GPU_REG_SHADER_CONSTANT_000_X + i * 4 + k);
      };
      const xenos::xe_gpu_vertex_fetch_t fetch = regs.GetVertexFetch(0);
      const auto* vertices = memory_->TranslatePhysical<const uint32_t*>(fetch.address << 2);
      const auto* indices = memory_->TranslatePhysical<const uint8_t*>(index_buffer_info->guest_base);
      for (uint32_t v = 0; vertices && indices && v < 4; ++v) {
        uint32_t index;
        if (index_buffer_info->format == xenos::IndexFormat::kInt16) {
          index = xenos::GpuSwap(reinterpret_cast<const uint16_t*>(indices)[v],
                                 index_buffer_info->endianness);
        } else {
          index = xenos::GpuSwap(reinterpret_cast<const uint32_t*>(indices)[v],
                                 index_buffer_info->endianness);
        }
        float position[4];
        for (uint32_t k = 0; k < 3; ++k) {
          uint32_t bits = xenos::GpuSwap(vertices[index * 4 + k], fetch.endian);
          float value;
          std::memcpy(&value, &bits, sizeof(value));
          position[k] = value * c(3, k) * c(1, k) + c(2, k);
        }
        position[3] = 1.0f;
        *draw_trace << " v" << v << " [";
        for (uint32_t row = 0; row < 4; ++row) {
          float clip = 0.0f;
          for (uint32_t k = 0; k < 4; ++k) clip += c(128 + row, k) * position[k];
          *draw_trace << (row ? "," : "") << clip;
        }
        *draw_trace << ']';
      }
    }
  }

  // Pixel shader analysis.
  bool primitive_polygonal = draw_util::IsPrimitivePolygonal(regs);
  bool is_rasterization_done = draw_util::IsRasterizationPotentiallyDone(regs, primitive_polygonal);
  if (surface_pitch_is_zero && is_rasterization_done) {
    // Doesn't actually draw.
    // Unlikely that zero would even really be legal though.
    draw_list_entry.outcome = "pitch0";
    return true;
  }
  D3D12Shader* pixel_shader = nullptr;
  if (is_rasterization_done) {
    // See xenos::EdramMode for explanation why the pixel shader is only used
    // when it's kColorDepth here.
    if (edram_mode == xenos::EdramMode::kColorDepth) {
      pixel_shader = static_cast<D3D12Shader*>(active_pixel_shader());
      if (pixel_shader) {
        pipeline_cache_->AnalyzeShaderUcode(*pixel_shader);
        if (!draw_util::IsPixelShaderNeededWithRasterization(*pixel_shader, regs)) {
          pixel_shader = nullptr;
        }
      }
    }
  } else {
    // Disabling pixel shader for this case is also required by the pipeline
    // cache.
    if (!memexport_used_vertex) {
      // This draw has no effect.
      draw_list_entry.outcome = "noraster";
      return true;
    }
  }
  bool memexport_used_pixel = pixel_shader && (pixel_shader->memexport_eM_written() != 0);
  bool memexport_used = memexport_used_vertex || memexport_used_pixel;

  if (!BeginSubmission(true)) {
    return false;
  }

  perf_trace::ZoneEnd(perf_trace::Zone::kDrawSetup, setup_start);
  // Process primitives.
  PrimitiveProcessor::ProcessingResult primitive_processing_result;
  uint64_t perf_start = perf_trace::ZoneStart();
  if (!primitive_processor_->Process(primitive_processing_result)) {
    return false;
  }
  perf_trace::ZoneEnd(perf_trace::Zone::kDrawPrimitives, perf_start);
  const uint64_t pre_targets_start = perf_trace::DetailZoneStart();
  if (draw_trace) {
    *draw_trace << " host_vertices " << primitive_processing_result.host_draw_vertex_count
                << std::flush;
  }
  if (!primitive_processing_result.host_draw_vertex_count) {
    // Nothing to draw.
    draw_list_entry.outcome = "novertices";
    return true;
  }

  reg::RB_DEPTHCONTROL normalized_depth_control = draw_util::GetNormalizedDepthControl(regs);

  // Shader modifications.
  uint32_t ps_param_gen_pos = UINT32_MAX;
  uint32_t interpolator_mask =
      pixel_shader ? (vertex_shader->writes_interpolators() &
                      pixel_shader->GetInterpolatorInputMask(regs.Get<reg::SQ_PROGRAM_CNTL>(),
                                                             regs.Get<reg::SQ_CONTEXT_MISC>(),
                                                             ps_param_gen_pos))
                   : 0;
  DxbcShaderTranslator::Modification vertex_shader_modification =
      pipeline_cache_->GetCurrentVertexShaderModification(
          *vertex_shader, primitive_processing_result.host_vertex_shader_type, interpolator_mask);
  DxbcShaderTranslator::Modification pixel_shader_modification =
      pixel_shader
          ? pipeline_cache_->GetCurrentPixelShaderModification(
                *pixel_shader, interpolator_mask, ps_param_gen_pos, normalized_depth_control)
          : DxbcShaderTranslator::Modification(0);

  // Set up the render targets - this may perform dispatches and draws.
  uint32_t normalized_color_mask =
      pixel_shader ? draw_util::GetNormalizedColorMask(regs, pixel_shader->writes_color_targets())
                   : 0;

  // Screen-space rectangles that write the same values to every sample, such as
  // clears: skip the transfers of what they overwrite, and draw a 4x MSAA clear
  // at half the pitch (the same EDRAM layout with a quarter of the pixels) into
  // the render targets of the sample count its EDRAM is next used with, with
  // guest pixels scaled like resolution scaling: 2 render target pixels along X
  // (and Y for 1x), which maps every guest sample to one host pixel or sample.
  // The surface info register is changed for this draw only.
  uint32_t uniform_draw_scale_x = 1, uniform_draw_scale_y = 1;
  struct RegisterRestore {
    RegisterFile* register_file = nullptr;
    uint32_t index = 0;
    uint32_t value = 0;
    // The written register mask of d3d12_draw_back_end, or nullptr.
    uint64_t* written_registers = nullptr;
    ~RegisterRestore() {
      if (register_file) {
        register_file->values[index] = value;
        if (written_registers) {
          written_registers[index >> 6] |= uint64_t(1) << (index & 63);
        }
      }
    }
  } surface_info_restore;
  uint32_t uniform_draw_overwritten_slots;
  bool uniform_draw_values_may_be_uniform;
  // A uniform overwrite of the render target cache, such as a clear: never
  // jittered.
  bool uniform_draw_set = false;
  uniform_draw_debug_reason = 0;
  // At a fractional scene scale, a logical size or while upscaling, the
  // private surfaces are not at the other surfaces' scale: a uniform draw then
  // only for draws that bind none (docs/analysis/fsr-plan.md 13); without them
  // every clear of a shared surface transferred the tiles it overwrites.
  const bool uniform_draws_private_only =
      !render_target_cache_->scene_draw_scale().IsWhole() ||
      render_target_cache_->IsSceneUpscaled() ||
      render_target_cache_->scene_logical_width() != 1280 ||
      render_target_cache_->scene_logical_height() != 720;
  if (render_target_cache_->GetPath() == RenderTargetCache::Path::kHostRenderTargets &&
      REXCVAR_GET(rtv_uniform_draws) && is_rasterization_done && !memexport_used &&
      !(uniform_draws_private_only &&
        render_target_cache_->DrawBindsPrivateSurface(normalized_depth_control,
                                                      normalized_color_mask)) &&
      GetDrawOverwrite(regs, primitive_type, index_count, pixel_shader, ps_param_gen_pos,
                       normalized_depth_control, normalized_color_mask,
                       uniform_draw_overwritten_slots, uniform_draw_values_may_be_uniform)) {
    RenderTargetCache::UniformDraw uniform_draw;
    DrawExtentEstimator::CoveredRectangle covered;
    uniform_draw.bounds_valid =
        render_target_cache_->EstimateCoveredRectangle(*vertex_shader, interpolator_mask, covered);
    std::memcpy(uniform_draw.bounds, covered.bounds, sizeof(uniform_draw.bounds));
    bool z_written = normalized_depth_control.z_enable && normalized_depth_control.z_write_enable;
    bool uniform_values = uniform_draw.bounds_valid && uniform_draw_values_may_be_uniform &&
                          covered.flat_interpolators && (covered.flat_z || !z_written);
    uniform_draw_debug_reason = uniform_draw.bounds_valid ? (uniform_values ? 10 : 9) : 11;
    if (uniform_draw.bounds_valid) {
      uniform_draw.overwritten_slots = uniform_draw_overwritten_slots;
      // FNV-1a over the state that identifies the clear.
      const bool depth_used = normalized_depth_control.z_enable || normalized_depth_control.stencil_enable;
      uint64_t signature = UINT64_C(0xCBF29CE484222325);
      auto mix = [&signature](uint32_t value) {
        signature = (signature ^ value) * UINT64_C(0x100000001B3);
      };
      mix(regs[XE_GPU_REG_RB_SURFACE_INFO]);
      mix(depth_used ? regs[XE_GPU_REG_RB_DEPTH_INFO] : 0);
      mix(normalized_color_mask);
      for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
        if ((normalized_color_mask >> (4 * i)) & 0xF) {
          mix(regs[reg::RB_COLOR_INFO::rt_register_indices[i]]);
        }
      }
      // Only uniform draws can be drawn with a different sample count.
      uniform_draw.signature = uniform_values ? signature | 1 : 0;
      auto rb_surface_info = regs.Get<reg::RB_SURFACE_INFO>();
      xenos::MsaaSamples next_msaa =
          render_target_cache_->GetUniformDrawNextMsaa(uniform_draw.signature);
      if (uniform_values && rb_surface_info.msaa_samples == xenos::MsaaSamples::k4X &&
          next_msaa < xenos::MsaaSamples::k4X &&
          rb_surface_info.surface_pitch * 2 <= xenos::kTexture2DCubeMaxWidthHeight) {
        uniform_draw_scale_x = 2;
        uniform_draw_scale_y = next_msaa == xenos::MsaaSamples::k1X ? 2 : 1;
        surface_info_restore.register_file = register_file_;
        surface_info_restore.index = XE_GPU_REG_RB_SURFACE_INFO;
        surface_info_restore.value = rb_surface_info.value;
        rb_surface_info.surface_pitch *= 2;
        rb_surface_info.msaa_samples = next_msaa;
        register_file_->values[XE_GPU_REG_RB_SURFACE_INFO] = rb_surface_info.value;
        if (track_register_writes_) {
          surface_info_restore.written_registers = written_registers_;
          written_registers_[XE_GPU_REG_RB_SURFACE_INFO >> 6] |=
              uint64_t(1) << (XE_GPU_REG_RB_SURFACE_INFO & 63);
        }
      }
      // The changed sample count may name a private surface, which is not at
      // the 4x surface's scale: the uniform draw then keeps the draw's own
      // sample count (the register goes back).
      if (uniform_draws_private_only && surface_info_restore.register_file &&
          render_target_cache_->DrawBindsPrivateSurface(normalized_depth_control,
                                                        normalized_color_mask)) {
        register_file_->values[XE_GPU_REG_RB_SURFACE_INFO] = surface_info_restore.value;
        surface_info_restore.register_file = nullptr;
        uniform_draw_scale_x = uniform_draw_scale_y = 1;
      }
      uniform_draw.scale_x = uniform_draw_scale_x;
      uniform_draw.scale_y = uniform_draw_scale_y;
      render_target_cache_->SetUniformDraw(uniform_draw);
      uniform_draw_set = true;
    }
  }

  if (const int32_t event =
          FrameGraphDraw(regs, *vertex_shader, pixel_shader, normalized_color_mask,
                         normalized_depth_control, edram_mode);
      event >= 0) {
    FrameGraphMark(event);
  }
  if (GpuInventory* inventory = GetGpuInventory()) {
    GpuInventory::Pair& pair = inventory->pairs[{vertex_shader->ucode_data_hash(),
                                                 pixel_shader ? pixel_shader->ucode_data_hash() : 0}];
    ++pair.draws;
    if (pair.last_frame != inventory->frame) {
      pair.last_frame = inventory->frame;
      ++pair.frames;
    }
    pair.memexport |= memexport_used;
    pair.primitives.insert(uint32_t(primitive_type));
    pair.edram_modes.insert(uint32_t(edram_mode));
    uint32_t render_targets = 0, written = 0;
    for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
      if (!(normalized_color_mask >> (i * 4) & 0xF)) continue;
      written |= 1 << i;
      const auto color_info = regs.Get<reg::RB_COLOR_INFO>(reg::RB_COLOR_INFO::rt_register_indices[i]);
      render_targets |= uint32_t(color_info.color_format) << (i * 4);
      if (written == (1u << i)) {
        pair.blend.insert(regs.values[reg::RB_BLENDCONTROL::rt_register_indices[i]]);
      }
    }
    const bool depth_used = normalized_depth_control.z_enable || normalized_depth_control.stencil_enable;
    render_targets |= written << 16;
    render_targets |= uint32_t(regs.Get<reg::RB_DEPTH_INFO>().depth_format) << 20;
    render_targets |= uint32_t(depth_used) << 21;
    render_targets |= uint32_t(regs.Get<reg::RB_SURFACE_INFO>().msaa_samples) << 22;
    pair.render_targets.insert(render_targets);
    pair.depth.insert(normalized_depth_control.value);
    for (uint32_t stage = 0; stage < 2; ++stage) {
      const Shader* shader = stage ? static_cast<const Shader*>(pixel_shader) : vertex_shader;
      if (!shader) continue;
      for (const Shader::TextureBinding& binding : shader->texture_bindings()) {
        const xenos::xe_gpu_texture_fetch_t fetch = regs.GetTextureFetch(binding.fetch_constant);
        const bool is_signed = fetch.sign_x != xenos::TextureSign::kUnsigned ||
                               fetch.sign_y != xenos::TextureSign::kUnsigned ||
                               fetch.sign_z != xenos::TextureSign::kUnsigned ||
                               fetch.sign_w != xenos::TextureSign::kUnsigned;
        pair.textures.insert(uint32_t(fetch.format) | uint32_t(fetch.dimension) << 6 |
                             uint32_t(fetch.tiled) << 8 | uint32_t(fetch.packed_mips) << 9 |
                             uint32_t(is_signed) << 10 | stage << 11);
      }
    }
  }
  perf_trace::ZoneEnd(perf_trace::Zone::kDrawPreTargets, pre_targets_start);
  perf_start = perf_trace::ZoneStart();
  if (!UpdateRenderTargets(is_rasterization_done, normalized_depth_control, normalized_color_mask,
                           *vertex_shader)) {
    return false;
  }
  // A scene draw (docs/analysis/d3d-hle-n5.md 18.13): into the private
  // surfaces at a fractional scene scale (or any with the diagnostic), drawn
  // with the scene_scale pixel shader variant, the rational viewport and
  // scissor. While upscaling (docs/analysis/fsr-plan.md 11), every draw into
  // them, at the scale of the bound surfaces: the input scale for the MSAA
  // ones (scene_input_scale), the scene's for the one-sample ones.
  const bool scene_upscaled = render_target_cache_->IsSceneUpscaled();
  const DrawScale scene_draw_scale = render_target_cache_->last_update_scale();
  const bool scene_draw = render_target_cache_->last_update_private() && !uniform_draw_set &&
                          (!scene_draw_scale.IsWhole() || scene_upscaled ||
                           REXCVAR_GET(native_draw_scale_variant));
  const bool scene_input_draw =
      scene_draw && scene_upscaled && render_target_cache_->last_update_input_scale();
  if (scene_draw && pixel_shader) {
    pixel_shader_modification.pixel.scene_scale = 1;
    pixel_shader_modification.pixel.scene_input_scale = uint32_t(scene_input_draw);
  }
  // The native shader contract (docs/analysis/shader-contract.md): a pixel
  // shader with a manifest entry takes it in the passes the entry lists and the
  // plain translation elsewhere, or its classic mod in the A/B's classic
  // periods; its draws carry their pass class in the host values.
  if (native_shader_contract_ && pixel_shader) {
    if (const native_shader_contract::PixelEntry* entry =
            native_shader_contract_->FindPixel(pixel_shader->ucode_data_hash())) {
      const auto depth_info = regs.Get<reg::RB_DEPTH_INFO>();
      const uint32_t depth_base = depth_info.depth_base | depth_info.depth_base_bit_11 << 11;
      // The viewport's width in guest pixels, 0 without viewport scaling.
      const auto vte = regs.Get<reg::PA_CL_VTE_CNTL>();
      const uint32_t viewport_width =
          vte.vport_x_scale_ena
              ? uint32_t(std::fabs(regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_XSCALE)) * 2.0f + 0.5f)
              : 0;
      const bool depth_tested =
          normalized_depth_control.z_enable || normalized_depth_control.stencil_enable;
      const bool after_final = post_passes_ && post_passes_->IsAfterFinal();
      native_shader_pass_ = native_shader_contract_->Classify(depth_tested, depth_base,
                                                              viewport_width, after_final);
      if (native_shader_pass_debug_) {
        // Each kind of draw an entry's pixel shader gets, once (the first 64).
        static std::set<std::tuple<uint64_t, uint32_t, uint32_t, uint32_t, bool, bool>> logged;
        auto kind = std::make_tuple(pixel_shader->ucode_data_hash(), native_shader_pass_,
                                    depth_base, viewport_width, depth_tested, after_final);
        if (logged.size() < 64 && logged.insert(kind).second) {
          REXGPU_INFO(
              "Native shader contract: pixel shader {:016X} in pass {} (depth base {}, viewport "
              "{} wide, depth or stencil test {}, after the final point {})",
              pixel_shader->ucode_data_hash(),
              native_shader_contract_->PassName(native_shader_pass_), depth_base, viewport_width,
              depth_tested, after_final);
        }
      }
      if (!(entry->pass_mask & (uint32_t(1) << native_shader_pass_))) {
        pixel_shader_modification.pixel.native_plain = 1;
      } else if (ShaderOverrides::IsNativeAbClassicPhase()) {
        pixel_shader_modification.pixel.native_classic = 1;
      }
    }
  }
  current_draw_scene_ = scene_draw;
  current_draw_scene_input_ = scene_input_draw;
  FrameGraphRenderTargets(*render_target_cache_);
  TraceTransferDraw(regs, *render_target_cache_, *vertex_shader, pixel_shader, primitive_type,
                    index_count, ps_param_gen_pos, normalized_depth_control,
                    normalized_color_mask);
  perf_trace::ZoneEnd(perf_trace::Zone::kDrawRenderTargets, perf_start);
  const uint64_t pre_pipeline_start = perf_trace::DetailZoneStart();

  // Create the pipeline (for this, need the actually used render target formats
  // from the render target cache), translating the shaders - doing this now to
  // obtain the used textures.
  D3D12Shader::D3D12Translation* vertex_shader_translation =
      static_cast<D3D12Shader::D3D12Translation*>(
          vertex_shader->GetOrCreateTranslation(vertex_shader_modification.value));
  D3D12Shader::D3D12Translation* pixel_shader_translation =
      pixel_shader ? static_cast<D3D12Shader::D3D12Translation*>(
                         pixel_shader->GetOrCreateTranslation(pixel_shader_modification.value))
                   : nullptr;
  uint32_t bound_depth_and_color_render_target_bits;
  uint32_t bound_depth_and_color_render_target_formats[1 + xenos::kMaxColorRenderTargets];
  bool host_render_targets_used =
      render_target_cache_->GetPath() == RenderTargetCache::Path::kHostRenderTargets;
  if (host_render_targets_used) {
    bound_depth_and_color_render_target_bits =
        render_target_cache_->GetLastUpdateBoundRenderTargets(
            bound_depth_and_color_render_target_formats);
  } else {
    bound_depth_and_color_render_target_bits = 0;
  }
  void* pipeline_handle;
  ID3D12RootSignature* root_signature;
  perf_trace::ZoneEnd(perf_trace::Zone::kDrawPrePipeline, pre_pipeline_start);
  perf_start = perf_trace::ZoneStart();
  if (draw_back_end_ == 2) {
    // d3d12_draw_back_end=2: the replay configures the pipeline; the shaders
    // are translated here, for the textures they use.
    pipeline_handle = nullptr;
    root_signature = nullptr;
    if (!pipeline_cache_->TranslateShadersForDraw(vertex_shader_translation,
                                                  pixel_shader_translation)) {
      return false;
    }
  } else if (!pipeline_cache_->ConfigurePipeline(
                 vertex_shader_translation, pixel_shader_translation,
                 primitive_processing_result, normalized_depth_control, normalized_color_mask,
                 bound_depth_and_color_render_target_bits,
                 bound_depth_and_color_render_target_formats, &pipeline_handle,
                 &root_signature)) {
    return false;
  }
  perf_trace::ZoneEnd(perf_trace::Zone::kDrawPipeline, perf_start);
  const uint64_t pre_textures_start = perf_trace::DetailZoneStart();
  if (draw_back_end_ != 2 && REXCVAR_GET(async_shader_compilation) &&
      pipeline_cache_->GetD3D12PipelineByHandle(pipeline_handle) == nullptr) {
    // The pipeline is still being created. Give it a short time before the draw
    // is dropped: dropped draws can leave whole passes empty (black photos).
    bool ready = false;
    const int32_t wait_us = REXCVAR_GET(async_shader_draw_wait_us);
    if (wait_us > 0) {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(wait_us);
      do {
        rex::thread::MaybeYield();
        ready = pipeline_cache_->GetD3D12PipelineByHandle(pipeline_handle) != nullptr;
      } while (!ready && std::chrono::steady_clock::now() < deadline);
    }
    perf_trace::Increment(ready ? perf_trace::Count::kPipelineDrawsWaited
                                : perf_trace::Count::kPipelineDrawsDropped);
    if (!ready) {
      draw_list_entry.outcome = "nopipeline";
      return true;
    }
  }

  // Update the textures - this may bind pipelines.
  uint32_t used_texture_mask =
      vertex_shader->GetUsedTextureMaskAfterTranslation() |
      (pixel_shader != nullptr ? pixel_shader->GetUsedTextureMaskAfterTranslation() : 0);
  perf_trace::ZoneEnd(perf_trace::Zone::kDrawPreTextures, pre_textures_start);
  perf_start = perf_trace::ZoneStart();
  texture_cache_->RequestTextures(used_texture_mask);
  if (REXCVAR_GET(native_scene_read_trace)) {
    TraceSceneTextureReads(regs, *texture_cache_, *render_target_cache_, *vertex_shader,
                           pixel_shader, scene_draw, scene_input_draw, frame_current_);
  }
  perf_trace::ZoneEnd(perf_trace::Zone::kDrawTextures, perf_start);
  const uint64_t state_start = perf_trace::DetailZoneStart();

  // Bind the pipeline after configuring it and doing everything that may bind
  // other pipelines.
  if (draw_back_end_ != 2 && current_guest_pipeline_ != pipeline_handle) {
    deferred_command_list_.SetPipelineStateHandle(reinterpret_cast<void*>(pipeline_handle));
    current_guest_pipeline_ = pipeline_handle;
    current_external_pipeline_ = nullptr;
  }

  // Get dynamic rasterizer state.
  uint32_t draw_resolution_scale_x = texture_cache_->draw_resolution_scale_x();
  uint32_t draw_resolution_scale_y = texture_cache_->draw_resolution_scale_y();

  bool convert_z_to_float24 =
      host_render_targets_used && render_target_cache_->depth_float24_convert_in_pixel_shader();
  bool ps_writes_depth = pixel_shader && pixel_shader->writes_depth();

  // At a logical width other than 1280 (18.21): a scene draw through the camera
  // (depth or stencil on) widens its view, the HUD's draws stay in the centre.
  uint32_t logical_flags = 0;
  if (scene_draw &&
      (draw_back_end::SceneLogicalWidth() != 1280 || draw_back_end::SceneLogicalHeight() != 720)) {
    if (post_passes_ && post_passes_->IsAfterFinal()) {
      logical_flags = draw_back_end::kFlagLogicalHud;
    } else if (normalized_depth_control.z_enable || normalized_depth_control.stencil_enable) {
      // Not a screen-space draw at a constant depth (a viewport Z scale of 0),
      // such as the mask FM4 draws over the cockpit interior.
      const auto vte = regs.Get<reg::PA_CL_VTE_CNTL>();
      if (!vte.vport_z_scale_ena || regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_ZSCALE) != 0.0f) {
        logical_flags = draw_back_end::kFlagLogicalCamera;
      }
    }
  }
  // A draw outside the scene's scale after the final point that samples a
  // full-screen scene texture at a logical size sees the texture's centred
  // 1280x720, the picture the title framed: photo mode's save copies the
  // frontbuffer at the whole scale (d3d-hle-n5.md 18.28). Before the final
  // point, such a draw's output goes back into the logical picture, stretched
  // as it was squeezed (photo mode's effects).
  if (!scene_draw && pixel_shader && post_passes_ && post_passes_->IsAfterFinal() &&
      (draw_back_end::SceneLogicalWidth() != 1280 || draw_back_end::SceneLogicalHeight() != 720)) {
    for (const Shader::TextureBinding& binding : pixel_shader->texture_bindings()) {
      TextureCache::SceneTextureInfo info;
      if (texture_cache_->GetActiveSceneTexture(binding.fetch_constant, info) &&
          info.width == 1280 && info.height == 720) {
        logical_flags = draw_back_end::kFlagLogicalCrop;
        static std::set<uint64_t> logged;
        if (logged.size() < 16 && logged.insert(pixel_shader->ucode_data_hash()).second) {
          REXGPU_INFO("Logical crop: pixel shader {:016X} samples the scene texture {:08X} "
                      "outside the scene's scale",
                      pixel_shader->ucode_data_hash(), info.base);
        }
        break;
      }
    }
  }

  // Build a cache key from all viewport-affecting state to skip redundant
  // recalculation when the viewport registers haven't changed between draws.
  ViewportCacheKey viewport_key;
  viewport_key.pa_cl_clip_cntl = regs[XE_GPU_REG_PA_CL_CLIP_CNTL];
  viewport_key.pa_cl_vte_cntl = regs[XE_GPU_REG_PA_CL_VTE_CNTL];
  viewport_key.pa_su_sc_mode_cntl = regs[XE_GPU_REG_PA_SU_SC_MODE_CNTL];
  viewport_key.pa_su_vtx_cntl = regs[XE_GPU_REG_PA_SU_VTX_CNTL];
  viewport_key.pa_sc_window_offset = regs[XE_GPU_REG_PA_SC_WINDOW_OFFSET];
  viewport_key.normalized_depth_control = normalized_depth_control.value;
  std::memcpy(viewport_key.vport_regs, &regs[XE_GPU_REG_PA_CL_VPORT_XSCALE],
              sizeof(viewport_key.vport_regs));
  // The depth format decides the float24 depth bounds conversion.
  viewport_key.flags = (uint32_t(convert_z_to_float24) << 0) |
                       (uint32_t(host_render_targets_used) << 1) | (uint32_t(ps_writes_depth) << 2) |
                       (uniform_draw_scale_x << 3) | (uniform_draw_scale_y << 5) |
                       (uint32_t(regs.Get<reg::RB_DEPTH_INFO>().depth_format) << 8) |
                       (uint32_t(scene_draw) << 16) | (logical_flags >> 14 << 17) |
                       (uint32_t(scene_input_draw) << 19) |
                       (uint32_t((logical_flags & draw_back_end::kFlagLogicalCrop) != 0) << 20);

  draw_util::ViewportInfo viewport_info;
  if (draw_back_end_ == 2) {
    // Computed where the list is replayed.
  } else if (viewport_cache_valid_ && viewport_key == previous_viewport_key_) {
    viewport_info = previous_viewport_info_;
  } else {
    if (scene_draw) {
      draw_util::GetHostViewportInfo(regs, scene_draw_scale, true, D3D12_VIEWPORT_BOUNDS_MAX,
                                     D3D12_VIEWPORT_BOUNDS_MAX, false, normalized_depth_control,
                                     convert_z_to_float24, host_render_targets_used,
                                     ps_writes_depth, viewport_info);
      draw_util::Scissor unused_scissor = {};
      draw_back_end::ApplyLogicalWidth(regs, draw_back_end::kFlagSceneScale | logical_flags,
                                       scene_draw_scale, viewport_info, unused_scissor);
    } else {
      draw_util::GetHostViewportInfo(regs, draw_resolution_scale_x * uniform_draw_scale_x,
                                     draw_resolution_scale_y * uniform_draw_scale_y, true,
                                     D3D12_VIEWPORT_BOUNDS_MAX, D3D12_VIEWPORT_BOUNDS_MAX, false,
                                     normalized_depth_control, convert_z_to_float24,
                                     host_render_targets_used, ps_writes_depth, viewport_info);
      draw_back_end::ApplyLogicalCrop(logical_flags, draw_resolution_scale_x * uniform_draw_scale_x,
                                      draw_resolution_scale_y * uniform_draw_scale_y,
                                      viewport_info);
    }
    previous_viewport_key_ = viewport_key;
    previous_viewport_info_ = viewport_info;
    viewport_cache_valid_ = true;
  }
  // The post passes' jitter of the scene draws (TAA), in host pixels of the
  // render target: a shift of the image through ndc_offset, on this draw's
  // copy of the viewport info only.
  float jitter[2];
  const bool jitter_applied =
      post_passes_ &&
      post_passes_->DrawJitter(regs, vertex_shader->ucode_data_hash(), normalized_depth_control,
                               primitive_type == xenos::PrimitiveType::kRectangleList ||
                                   uniform_draw_set,
                               jitter);
  if (jitter_applied) {
    if (draw_back_end_ != 2) {
      post_pass::AddJitterToNdcOffset(jitter, viewport_info.xy_extent, viewport_info.ndc_offset);
    }
    if (draw_list_entry.trace) {
      std::memcpy(&draw_list_entry.values[15], &jitter[0], sizeof(float));
      std::memcpy(&draw_list_entry.values[16], &jitter[1], sizeof(float));
    }
  }

  draw_util::Scissor scissor;
  draw_util::GetScissor(regs, scissor);
  if (scene_draw) {
    draw_util::ScaleScissor(scene_draw_scale, scissor);
    draw_util::ViewportInfo unused_viewport = {};
    draw_back_end::ApplyLogicalWidth(regs, draw_back_end::kFlagSceneScale | logical_flags,
                                     scene_draw_scale, unused_viewport, scissor);
  } else {
    scissor.offset[0] *= draw_resolution_scale_x * uniform_draw_scale_x;
    scissor.offset[1] *= draw_resolution_scale_y * uniform_draw_scale_y;
    scissor.extent[0] *= draw_resolution_scale_x * uniform_draw_scale_x;
    scissor.extent[1] *= draw_resolution_scale_y * uniform_draw_scale_y;
  }

  // Update viewport, scissor, blend factor and stencil reference.
  if (draw_back_end_ != 2) {
    UpdateFixedFunctionState(viewport_info, scissor, primitive_polygonal,
                             normalized_depth_control);
  }

  // Update system constants before uploading them.
  // TODO(Triang3l): With ROV, pass the disabled render target mask for safety.
  perf_trace::ZoneEnd(perf_trace::Zone::kDrawState, state_start);
  CountContextRegisterDelta();
  perf_start = perf_trace::ZoneStart();
  if (draw_back_end_ == 2) {
    // The replay computes the system constants and the bindings; the front
    // half passes what only it knows.
    FillSystemConstantInputs(memexport_used, primitive_polygonal,
                             primitive_processing_result.line_loop_closing_index,
                             primitive_processing_result.host_shader_index_endian,
                             used_texture_mask, normalized_depth_control, normalized_color_mask);
    PrepareDrawBackEndInputs(vertex_shader, pixel_shader);
  } else {
    UpdateSystemConstantValues(memexport_used, primitive_polygonal,
                               primitive_processing_result.line_loop_closing_index,
                               primitive_processing_result.host_shader_index_endian,
                               viewport_info, used_texture_mask, normalized_depth_control,
                               normalized_color_mask);

    // Update constant buffers, descriptors and root parameters.
    if (!UpdateBindings(vertex_shader, pixel_shader, root_signature, memexport_used)) {
      return false;
    }
  }
  perf_trace::ZoneEnd(perf_trace::Zone::kDrawBindings, perf_start);
  DrawBackEndCapture back_end_capture;
  if (draw_back_end_) {
    // The back end's inputs into the deferred command list (N4 step 2,
    // increment 1, d3d-hle-n4.md 17.6).
    namespace be = draw_back_end;
    const be::SystemConstantInputs& inputs = system_constant_inputs_;
    DrawBackEndCapture& capture = back_end_capture;
    capture.vertex_translation = vertex_shader_translation;
    capture.pixel_translation = pixel_shader_translation;
    capture.primitive = &primitive_processing_result;
    capture.bound_render_target_bits = bound_depth_and_color_render_target_bits;
    capture.bound_render_target_formats = bound_depth_and_color_render_target_formats;
    capture.flags = (memexport_used ? be::kFlagMemexport : 0) |
                    (convert_z_to_float24 ? be::kFlagConvertZToFloat24 : 0) |
                    (host_render_targets_used ? be::kFlagHostRenderTargets : 0) |
                    (inputs.edram_rov_used ? be::kFlagEdramRov : 0) |
                    (inputs.gamma_render_target_as_unorm16 ? be::kFlagGammaAsUnorm16 : 0) |
                    (inputs.fixed16_truncated_to_minus_1_to_1 ? be::kFlagFixed16Truncated : 0) |
                    (inputs.rov_occlusion_query ? be::kFlagRovOcclusionQuery : 0) |
                    (jitter_applied ? be::kFlagJitter : 0) |
                    (uniform_draw_scale_x << be::kFlagUniformScaleShift) |
                    (uniform_draw_scale_y << (be::kFlagUniformScaleShift + 2)) |
                    (scene_draw ? be::kFlagSceneScale | logical_flags
                                : logical_flags & be::kFlagLogicalCrop) |
                    (scene_input_draw ? be::kFlagSceneInput : 0) |
                    (native_shader_pass_ << be::kFlagNativePassShift);
    capture.jitter[0] = jitter_applied ? jitter[0] : 0.0f;
    capture.jitter[1] = jitter_applied ? jitter[1] : 0.0f;
    capture.pipeline_handle = pipeline_handle;
    capture.viewport = &viewport_info;
    capture.scissor = &scissor;
    capture.index_buffer = {};
    if (draw_back_end_ == 1) {
      CaptureDrawBackEnd(capture);
    }
  }
  // Must not call anything that can change the descriptor heap from now on!

  // Ensure vertex buffers are resident.
  perf_start = perf_trace::ZoneStart();
  const Shader::ConstantRegisterMap& constant_map_vertex = vertex_shader->constant_register_map();
  const uint64_t memory_epoch = shared_memory_->invalidation_epoch();
  if (memory_epoch != vertex_buffers_epoch_) {
    vertex_buffers_in_sync_[0] = 0;
    vertex_buffers_in_sync_[1] = 0;
    vertex_buffers_epoch_ = memory_epoch;
  }
  for (uint32_t i = 0; i < rex::countof(constant_map_vertex.vertex_fetch_bitmap); ++i) {
    uint32_t vfetch_bits_remaining = constant_map_vertex.vertex_fetch_bitmap[i];
    uint32_t j;
    while (rex::bit_scan_forward(vfetch_bits_remaining, &j)) {
      vfetch_bits_remaining &= ~(uint32_t(1) << j);
      uint32_t vfetch_index = i * 32 + j;
      uint64_t vfetch_bit = uint64_t(1) << (vfetch_index & 63);
      if (vertex_buffers_in_sync_[vfetch_index >> 6] & vfetch_bit) {
        continue;
      }
      xenos::xe_gpu_vertex_fetch_t vfetch_constant = regs.GetVertexFetch(vfetch_index);
      switch (vfetch_constant.type) {
        case xenos::FetchConstantType::kVertex:
          break;
        case xenos::FetchConstantType::kInvalidVertex:
          if (REXCVAR_GET(gpu_allow_invalid_fetch_constants)) {
            break;
          }
          REXGPU_WARN(
              "Vertex fetch constant {} ({:08X} {:08X}) has \"invalid\" type! "
              "This is incorrect behavior, but you can try bypassing this by "
              "launching Xenia with --gpu_allow_invalid_fetch_constants=true.",
              vfetch_index, vfetch_constant.dword_0, vfetch_constant.dword_1);
          return false;
        default:
          REXGPU_WARN("Vertex fetch constant {} ({:08X} {:08X}) is completely invalid!",
                      vfetch_index, vfetch_constant.dword_0, vfetch_constant.dword_1);
          return false;
      }
      TraceGuestRead(GuestRead::kVertices, vfetch_constant.address << 2, vfetch_constant.size << 2,
                     uint32_t(vertex_shader->ucode_data_hash()));
      VertexBufferState& state = vertex_buffer_states_[vfetch_index];
      if (state.address == vfetch_constant.address && state.size == vfetch_constant.size) {
        if (state.epoch == memory_epoch) {
          vertex_buffers_in_sync_[vfetch_index >> 6] |= vfetch_bit;
          continue;
        }
        shared_memory_->CheckReusedRequest(vfetch_constant.address << 2, vfetch_constant.size << 2);
      }
      if (!shared_memory_->RequestRange(vfetch_constant.address << 2, vfetch_constant.size << 2)) {
        REXGPU_ERROR(
            "Failed to request vertex buffer at 0x{:08X} (size {}) in the "
            "shared memory",
            vfetch_constant.address << 2, vfetch_constant.size << 2);
        return false;
      }
      state.address = vfetch_constant.address;
      state.size = vfetch_constant.size;
      state.epoch = memory_epoch;
      vertex_buffers_in_sync_[vfetch_index >> 6] |= vfetch_bit;
    }
  }

  perf_trace::ZoneEnd(perf_trace::Zone::kDrawVertexBuffers, perf_start);
  const perf_trace::DetailZoneScope call_zone(perf_trace::Zone::kDrawCall);

  // Gather memexport ranges and ensure the heaps for them are resident, and
  // also load the data surrounding the export and to fill the regions that
  // won't be modified by the shaders.
  memexport_ranges_.clear();
  if (memexport_used_vertex) {
    draw_util::AddMemExportRanges(regs, *vertex_shader, memexport_ranges_);
  }
  if (memexport_used_pixel) {
    draw_util::AddMemExportRanges(regs, *pixel_shader, memexport_ranges_);
  }
  for (const draw_util::MemExportRange& memexport_range : memexport_ranges_) {
    if (!shared_memory_->RequestRange(memexport_range.base_address_dwords << 2,
                                      memexport_range.size_bytes)) {
      REXGPU_ERROR(
          "Failed to request memexport stream at 0x{:08X} (size {}) in the "
          "shared memory",
          memexport_range.base_address_dwords << 2, memexport_range.size_bytes);
      return false;
    }
  }
  if (memexport_used && memexport_ranges_.empty()) {
    if (!shared_memory_->RequestRange(0, SharedMemory::kBufferSize)) {
      REXGPU_ERROR(
          "Failed to request full shared memory residency for unresolved "
          "memexport destinations");
      return false;
    }
  }

  // Primitive topology.
  D3D_PRIMITIVE_TOPOLOGY primitive_topology;
  if (primitive_processing_result.IsTessellated()) {
    switch (primitive_processing_result.host_primitive_type) {
      // TODO(Triang3l): Support all primitive types.
      case xenos::PrimitiveType::kTriangleList:
        primitive_topology = D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
        break;
      case xenos::PrimitiveType::kQuadList:
        primitive_topology = D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST;
        break;
      case xenos::PrimitiveType::kTrianglePatch:
        primitive_topology =
            (regs.Get<reg::VGT_HOS_CNTL>().tess_mode == xenos::TessellationMode::kAdaptive)
                ? D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST
                : D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST;
        break;
      case xenos::PrimitiveType::kQuadPatch:
        primitive_topology =
            (regs.Get<reg::VGT_HOS_CNTL>().tess_mode == xenos::TessellationMode::kAdaptive)
                ? D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST
                : D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST;
        break;
      default:
        REXGPU_ERROR(
            "Host tessellated primitive type {} returned by the primitive "
            "processor is not supported by the Direct3D 12 command processor",
            uint32_t(primitive_processing_result.host_primitive_type));
        assert_unhandled_case(primitive_processing_result.host_primitive_type);
        return false;
    }
  } else {
    switch (primitive_processing_result.host_primitive_type) {
      case xenos::PrimitiveType::kPointList:
        primitive_topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        break;
      case xenos::PrimitiveType::kLineList:
        primitive_topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        break;
      case xenos::PrimitiveType::kLineStrip:
        primitive_topology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        break;
      case xenos::PrimitiveType::kTriangleList:
      case xenos::PrimitiveType::kRectangleList:
        primitive_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        break;
      case xenos::PrimitiveType::kTriangleStrip:
        primitive_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        break;
      case xenos::PrimitiveType::kQuadList:
        primitive_topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
        break;
      default:
        REXGPU_ERROR(
            "Host primitive type {} returned by the primitive processor is not "
            "supported by the Direct3D 12 command processor",
            uint32_t(primitive_processing_result.host_primitive_type));
        assert_unhandled_case(primitive_processing_result.host_primitive_type);
        return false;
    }
  }
  if (draw_back_end_ != 2) {
    SetPrimitiveTopology(primitive_topology);
  }
  // Must not call anything that may change the primitive topology from now on!

  DrawDump(
      regs, *memory_, *vertex_shader, pixel_shader, primitive_processing_result.guest_index_base,
      primitive_processing_result.host_draw_vertex_count *
          (primitive_processing_result.host_index_format == xenos::IndexFormat::kInt16 ? 2 : 4));
  uint32_t draw_stats_values[9] = {};
  if (draw_stats_recording_) {
    draw_stats_values[0] = uint32_t(vertex_shader->ucode_data_hash());
    draw_stats_values[1] = pixel_shader ? uint32_t(pixel_shader->ucode_data_hash()) : 0;
    if (!vertex_shader->vertex_bindings().empty()) {
      const auto fetch = regs.GetVertexFetch(vertex_shader->vertex_bindings()[0].fetch_constant);
      draw_stats_values[2] = fetch.address << 2;
      draw_stats_values[8] =
          shared_memory_->CountGpuWrittenPages(fetch.address << 2, fetch.size << 2);
    }
    draw_stats_values[3] = primitive_processing_result.guest_index_base;
    draw_stats_values[4] = primitive_processing_result.host_draw_vertex_count;
    draw_stats_values[5] = regs.values[reg::RB_COLOR_INFO::rt_register_indices[0]];
    draw_stats_values[6] = regs.values[XE_GPU_REG_RB_DEPTH_INFO];
    draw_stats_values[7] = regs.values[XE_GPU_REG_PA_SC_WINDOW_OFFSET];
  }

  // Draw.
  if (primitive_processing_result.index_buffer_type ==
      PrimitiveProcessor::ProcessedIndexBufferType::kNone) {
    if (memexport_used) {
      shared_memory_->UseForWriting();
    } else {
      shared_memory_->UseForReading();
    }
    SubmitBarriers();
    PROFILE_DRAW_CALL();
    PROFILE_VERTICES(primitive_processing_result.host_draw_vertex_count);
    const bool draw_stats = BeginDrawStats(draw_stats_values);
    if (draw_back_end_ == 2) {
      // The replay sets the state and records the draw.
      back_end_capture.index_buffer.Format = DXGI_FORMAT_UNKNOWN;
      CaptureDrawBackEnd(back_end_capture);
      InvalidateStateAfterDrawBackEnd();
    } else {
      deferred_command_list_.D3DDrawInstanced(primitive_processing_result.host_draw_vertex_count,
                                              1, 0, 0);
    }
    if (draw_stats)
      EndDrawStats();
  } else {
    D3D12_INDEX_BUFFER_VIEW index_buffer_view;
    index_buffer_view.SizeInBytes = primitive_processing_result.host_draw_vertex_count;
    if (primitive_processing_result.host_index_format == xenos::IndexFormat::kInt16) {
      index_buffer_view.SizeInBytes *= sizeof(uint16_t);
      index_buffer_view.Format = DXGI_FORMAT_R16_UINT;
    } else {
      index_buffer_view.SizeInBytes *= sizeof(uint32_t);
      index_buffer_view.Format = DXGI_FORMAT_R32_UINT;
    }
    ID3D12Resource* scratch_index_buffer = nullptr;
    switch (primitive_processing_result.index_buffer_type) {
      case PrimitiveProcessor::ProcessedIndexBufferType::kGuestDMA: {
        if (memexport_used) {
          // If the shared memory is a UAV, it can't be used as an index buffer
          // (UAV is a read/write state, index buffer is a read-only state).
          // Need to copy the indices to a buffer in the index buffer state.
          scratch_index_buffer = RequestScratchGPUBuffer(index_buffer_view.SizeInBytes,
                                                         D3D12_RESOURCE_STATE_COPY_DEST);
          if (scratch_index_buffer == nullptr) {
            return false;
          }
          shared_memory_->UseAsCopySource();
          SubmitBarriers();
          deferred_command_list_.D3DCopyBufferRegion(
              scratch_index_buffer, 0, shared_memory_->GetBuffer(),
              primitive_processing_result.guest_index_base, index_buffer_view.SizeInBytes);
          PushTransitionBarrier(scratch_index_buffer, D3D12_RESOURCE_STATE_COPY_DEST,
                                D3D12_RESOURCE_STATE_INDEX_BUFFER);
          index_buffer_view.BufferLocation = scratch_index_buffer->GetGPUVirtualAddress();
        } else {
          index_buffer_view.BufferLocation =
              shared_memory_->GetGPUAddress() + primitive_processing_result.guest_index_base;
        }
      } break;
      case PrimitiveProcessor::ProcessedIndexBufferType::kHostConverted:
        index_buffer_view.BufferLocation = primitive_processor_->GetConvertedIndexBufferGpuAddress(
            primitive_processing_result.host_index_buffer_handle);
        break;
      case PrimitiveProcessor::ProcessedIndexBufferType::kHostBuiltinForAuto:
      case PrimitiveProcessor::ProcessedIndexBufferType::kHostBuiltinForDMA:
        index_buffer_view.BufferLocation = primitive_processor_->GetBuiltinIndexBufferGpuAddress(
            primitive_processing_result.host_index_buffer_handle);
        break;
      default:
        assert_unhandled_case(primitive_processing_result.index_buffer_type);
        return false;
    }
    if (draw_back_end_ != 2) {
      deferred_command_list_.D3DIASetIndexBuffer(&index_buffer_view);
    }
    if (memexport_used) {
      shared_memory_->UseForWriting();
    } else {
      shared_memory_->UseForReading();
    }
    SubmitBarriers();
    PROFILE_DRAW_CALL();
    PROFILE_VERTICES(primitive_processing_result.host_draw_vertex_count);
    const bool draw_stats = BeginDrawStats(draw_stats_values);
    if (draw_back_end_ == 2) {
      back_end_capture.index_buffer = index_buffer_view;
      CaptureDrawBackEnd(back_end_capture);
      InvalidateStateAfterDrawBackEnd();
    } else {
      deferred_command_list_.D3DDrawIndexedInstanced(
          primitive_processing_result.host_draw_vertex_count, 1, 0, 0, 0);
    }
    if (draw_stats)
      EndDrawStats();
    if (scratch_index_buffer != nullptr) {
      ReleaseScratchGPUBuffer(scratch_index_buffer, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    }
  }

  if (memexport_used) {
    // Make sure this memexporting draw is ordered with other work using shared
    // memory as a UAV.
    // TODO(Triang3l): Find some PM4 command that can be used for indication of
    // when memexports should be awaited?
    shared_memory_->MarkUAVWritesCommitNeeded();
    // Invalidate textures in memexported memory and watch for changes.
    if (!memexport_ranges_.empty()) {
      for (const draw_util::MemExportRange& memexport_range : memexport_ranges_) {
        shared_memory_->RangeWrittenByGpu(memexport_range.base_address_dwords << 2,
                                          memexport_range.size_bytes);
      }
    } else {
      // Stream constants can be invalid or dynamic, so exact destinations may
      // be unknown. Keep invalidation conservative in this case.
      shared_memory_->RangeWrittenByGpu(0, SharedMemory::kBufferSize);
    }
    if (IsReadbackMemexportEnabled(REXCVAR_GET(d3d12_readback_memexport)) &&
        !memexport_ranges_.empty()) {
      uint32_t memexport_total_size = 0;
      for (const draw_util::MemExportRange& memexport_range : memexport_ranges_) {
        memexport_total_size += memexport_range.size_bytes;
      }
      if (memexport_total_size != 0) {
        if (REXCVAR_GET(readback_memexport_fast)) {
          IssueDraw_MemexportReadbackFastPath(memexport_total_size);
        } else {
          IssueDraw_MemexportReadbackFullPath(memexport_total_size);
        }
      }
    }
  }

  draw_list_entry.outcome = "ok";
  return true;
}

bool D3D12CommandProcessor::IssueDraw_MemexportReadbackFullPath(uint32_t total_size) {
  perf_trace::CauseScope perf_cause(perf_trace::Wait::kMemexport);
  if (!total_size || memexport_ranges_.empty()) {
    return true;
  }

  ID3D12Resource* readback_buffer = RequestReadbackBuffer(total_size);
  if (!readback_buffer) {
    return true;
  }

  shared_memory_->UseAsCopySource();
  SubmitBarriers();
  ID3D12Resource* shared_memory_buffer = shared_memory_->GetBuffer();
  uint32_t readback_buffer_offset = 0;
  for (const draw_util::MemExportRange& memexport_range : memexport_ranges_) {
    deferred_command_list_.D3DCopyBufferRegion(
        readback_buffer, readback_buffer_offset, shared_memory_buffer,
        memexport_range.base_address_dwords << 2, memexport_range.size_bytes);
    readback_buffer_offset += memexport_range.size_bytes;
  }

  if (!AwaitAllQueueOperationsCompletion()) {
    return true;
  }

  D3D12_RANGE readback_range = {};
  readback_range.Begin = 0;
  readback_range.End = total_size;
  void* readback_mapping = nullptr;
  if (FAILED(readback_buffer->Map(0, &readback_range, &readback_mapping))) {
    return true;
  }

  const uint8_t* readback_bytes = reinterpret_cast<const uint8_t*>(readback_mapping);
  AwaitAllReadbackPublish();
  for (const draw_util::MemExportRange& memexport_range : memexport_ranges_) {
    std::memcpy(memory_->TranslatePhysical(memexport_range.base_address_dwords << 2),
                readback_bytes, memexport_range.size_bytes);
    readback_bytes += memexport_range.size_bytes;
  }

  D3D12_RANGE readback_write_range = {};
  readback_buffer->Unmap(0, &readback_write_range);
  return true;
}

bool D3D12CommandProcessor::IssueDraw_MemexportReadbackFastPath(uint32_t total_size) {
  if (!total_size || memexport_ranges_.empty()) {
    return true;
  }

  const uint64_t readback_key =
      MakeMemexportReadbackKey(memexport_ranges_.front().base_address_dwords, total_size);
  ReadbackBuffer& readback = memexport_readback_buffers_[readback_key];
  readback.last_used_frame = frame_current_;

  auto ensure_readback_slot = [&](uint32_t index, uint32_t size) -> bool {
    if (readback.buffers[index] && readback.mapped_data[index] && size <= readback.sizes[index]) {
      return true;
    }

    const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
    ID3D12Device* device = provider.GetDevice();
    D3D12_RESOURCE_DESC buffer_desc;
    ui::d3d12::util::FillBufferResourceDesc(buffer_desc, size, D3D12_RESOURCE_FLAG_NONE);
    ID3D12Resource* buffer = nullptr;
    if (FAILED(device->CreateCommittedResource(
            &ui::d3d12::util::kHeapPropertiesReadback, provider.GetHeapFlagCreateNotZeroed(),
            &buffer_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&buffer)))) {
      return false;
    }

    D3D12_RANGE read_range = {0, size};
    void* mapped_data = nullptr;
    if (FAILED(buffer->Map(0, &read_range, &mapped_data))) {
      buffer->Release();
      return false;
    }

    if (readback.buffers[index]) {
      if (!AwaitAllQueueOperationsCompletion()) {
        buffer->Unmap(0, nullptr);
        buffer->Release();
        return false;
      }
      if (readback.mapped_data[index]) {
        readback.buffers[index]->Unmap(0, nullptr);
      }
      readback.buffers[index]->Release();
    }

    readback.buffers[index] = buffer;
    readback.mapped_data[index] = mapped_data;
    readback.sizes[index] = size;
    readback.submission_written[index] = 0;
    readback.written_size[index] = 0;
    return true;
  };

  const uint32_t write_index = readback.current_index;
  const uint32_t read_index = 1 - write_index;
  const uint32_t readback_size = AlignReadbackBufferSize(total_size);
  if (!ensure_readback_slot(write_index, readback_size)) {
    return IssueDraw_MemexportReadbackFullPath(total_size);
  }

  shared_memory_->UseAsCopySource();
  SubmitBarriers();
  ID3D12Resource* shared_memory_buffer = shared_memory_->GetBuffer();
  uint32_t readback_offset = 0;
  for (const draw_util::MemExportRange& memexport_range : memexport_ranges_) {
    deferred_command_list_.D3DCopyBufferRegion(
        readback.buffers[write_index], readback_offset, shared_memory_buffer,
        memexport_range.base_address_dwords << 2, memexport_range.size_bytes);
    readback_offset += memexport_range.size_bytes;
  }
  readback.submission_written[write_index] = submission_current_;
  readback.written_size[write_index] = total_size;

  CheckSubmissionFence(0);
  bool previous_slot_ready = readback.buffers[read_index] && readback.mapped_data[read_index] &&
                             total_size <= readback.sizes[read_index] &&
                             total_size <= readback.written_size[read_index] &&
                             readback.submission_written[read_index] &&
                             readback.submission_written[read_index] <= submission_completed_;
  if (!previous_slot_ready) {
    IssueDraw_MemexportReadbackFullPath(total_size);
    readback.current_index = read_index;
    return true;
  }

  const uint8_t* readback_bytes = static_cast<const uint8_t*>(readback.mapped_data[read_index]);
  AwaitAllReadbackPublish();
  for (const draw_util::MemExportRange& memexport_range : memexport_ranges_) {
    std::memcpy(memory_->TranslatePhysical(memexport_range.base_address_dwords << 2),
                readback_bytes, memexport_range.size_bytes);
    readback_bytes += memexport_range.size_bytes;
  }
  readback.current_index = read_index;
  return true;
}

bool D3D12CommandProcessor::IssueCopy() {
#if XE_GPU_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_GPU_FINE_GRAINED_DRAW_SCOPES
  perf_trace::CountCopy();
  perf_trace::ZoneScope perf_zone(perf_trace::Zone::kCopy);
  if (!BeginSubmission(true)) {
    return false;
  }
  if (const int32_t event = FrameGraphBeginResolve(); event >= 0) {
    FrameGraphMark(event);
  }
  if (post_passes_) {
    // A final point still pending runs before the resolve copies the target.
    post_passes_->OnBeforeResolve();
  }
  ReadbackResolveMode readback_mode = GetReadbackResolveMode(REXCVAR_GET(d3d12_readback_resolve));
  if (readback_mode == ReadbackResolveMode::kDisabled) {
    uint32_t written_address, written_length;
    const uint32_t timestamp = BeginGpuTimestamp(GpuTimestampKind::kResolve);
    const bool result = render_target_cache_->Resolve(*memory_, *shared_memory_, *texture_cache_,
                                                      written_address, written_length);
    EndGpuTimestamp(timestamp);
    SetGpuTimestampKind(timestamp, GetResolveTimestampKind(written_length));
    if (result) {
      CountResolveDestination(written_address, written_length);
      if (post_passes_) {
        post_passes_->OnResolve(*register_file_, written_address, written_length);
      }
    }
    return result;
  }
  return IssueCopy_ReadbackResolvePath();
}

void D3D12CommandProcessor::CountResolveDestination(uint32_t address, uint32_t length) const {
  const auto info = register_file_->Get<reg::RB_COPY_DEST_INFO>();
  const auto pitch = register_file_->Get<reg::RB_COPY_DEST_PITCH>();
  FrameGraphResolve(*register_file_, address, length);
  perf_trace::CountResolve(address, length, uint32_t(info.copy_dest_format),
                           pitch.copy_dest_pitch, pitch.copy_dest_height);
}

bool D3D12CommandProcessor::IssueCopy_ReadbackResolvePath() {
  perf_trace::CauseScope perf_cause(perf_trace::Wait::kResolveReadback);
  uint32_t written_address, written_length;
  const uint32_t timestamp = BeginGpuTimestamp(GpuTimestampKind::kResolve);
  const bool resolved = render_target_cache_->Resolve(*memory_, *shared_memory_, *texture_cache_,
                                                      written_address, written_length);
  EndGpuTimestamp(timestamp);
  SetGpuTimestampKind(timestamp, GetResolveTimestampKind(written_length));
  if (!resolved) {
    return false;
  }
  CountResolveDestination(written_address, written_length);
  if (post_passes_) {
    post_passes_->OnResolve(*register_file_, written_address, written_length);
  }

  if (!written_length) {
    return true;
  }
  // The GPU time of the readback copies (N3, d3d-hle.md 16.1).
  struct ReadbackTimestamp {
    D3D12CommandProcessor& processor;
    uint32_t pair;
    ~ReadbackTimestamp() { processor.EndGpuTimestamp(pair); }
  } readback_timestamp{*this, BeginGpuTimestamp(GpuTimestampKind::kReadback)};

  if (!memory_->TranslatePhysical(written_address)) {
    return true;
  }

  bool is_scaled = texture_cache_->IsDrawResolutionScaled();
  if (demand_servicer_) {
    // The readback waits for the first access by the CPU (d3d-hle.md 16.5).
    read_trace::Demand demand;
    demand.address = written_address;
    demand.length = written_length;
    if (is_scaled) {
      const uint64_t scale = uint64_t(texture_cache_->draw_resolution_scale_x()) *
                             texture_cache_->draw_resolution_scale_y();
      if (!GetResolvePixelSizeLog2(demand.pixel_size_log2) || !scale) {
        return true;
      }
      demand.scaled_start =
          uint32_t(texture_cache_->GetCurrentScaledResolveRangeStartScaled() / scale);
      demand.scaled_length =
          uint32_t(texture_cache_->GetCurrentScaledResolveRangeLengthScaled() / scale);
    }
    read_trace::OnDeferredResolve(*memory_, demand);
    return true;
  }
  uint64_t resolve_key = MakeReadbackResolveKey(written_address, written_length);
  ReadbackBuffer& rb = readback_buffers_[resolve_key];
  rb.last_used_frame = frame_current_;

  uint32_t write_index = rb.current_index;
  uint32_t size = AlignReadbackBufferSize(written_length);
  // The slot may be replaced or written by the GPU again below.
  AwaitReadbackPublish(rb.publish_sequence[write_index]);

  if (size > rb.sizes[write_index]) {
    const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
    ID3D12Device* device = provider.GetDevice();
    D3D12_RESOURCE_DESC buffer_desc;
    ui::d3d12::util::FillBufferResourceDesc(buffer_desc, size, D3D12_RESOURCE_FLAG_NONE);
    ID3D12Resource* buffer = nullptr;
    if (FAILED(device->CreateCommittedResource(
            &ui::d3d12::util::kHeapPropertiesReadback, provider.GetHeapFlagCreateNotZeroed(),
            &buffer_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&buffer)))) {
      REXGPU_ERROR("Failed to create a {} MB readback buffer", size >> 20);
      return true;
    }
    if (rb.buffers[write_index]) {
      if (rb.mapped_data[write_index]) {
        rb.buffers[write_index]->Unmap(0, nullptr);
        rb.mapped_data[write_index] = nullptr;
      }
      rb.buffers[write_index]->Release();
    }
    rb.buffers[write_index] = buffer;
    rb.sizes[write_index] = size;
    D3D12_RANGE read_range = {0, size};
    if (FAILED(buffer->Map(0, &read_range, &rb.mapped_data[write_index]))) {
      REXGPU_ERROR("Failed to persistently map resolve readback buffer");
      rb.mapped_data[write_index] = nullptr;
    }
  }

  if (!rb.buffers[write_index]) {
    return true;
  }

  uint32_t pixel_size_log2 = 0;
  if (is_scaled && !GetResolvePixelSizeLog2(pixel_size_log2)) {
    return true;
  }
  if (!RecordReadbackCopy(written_address, written_length, pixel_size_log2,
                          rb.buffers[write_index], 0)) {
    return true;
  }

  ReadbackResolveMode readback_mode = GetReadbackResolveMode(REXCVAR_GET(d3d12_readback_resolve));
  bool use_delayed_sync =
      readback_mode == ReadbackResolveMode::kFast || readback_mode == ReadbackResolveMode::kSome;
  rb.submission_written[write_index] = submission_current_;
  rb.written_size[write_index] = written_length;
  rb.current_index = (write_index + 1) % kReadbackSlots;
  uint32_t read_index = write_index;
  bool is_cache_miss = false;
  if (use_delayed_sync) {
    // Publish the newest copy of this key that the GPU has finished, without
    // waiting: keys resolved several times per frame often have their previous
    // copy still in the open submission. Only a key with no earlier copy at all
    // waits, so that its first data is not missing.
    CheckSubmissionFence(0);
    uint64_t newest_submission = 0;
    bool any_earlier = false;
    for (uint32_t i = 0; i < kReadbackSlots; ++i) {
      const uint64_t submission = rb.submission_written[i];
      if (i == write_index || !submission || !rb.mapped_data[i] ||
          written_length > rb.written_size[i]) {
        continue;
      }
      any_earlier = true;
      if (submission <= submission_completed_ && submission > newest_submission) {
        newest_submission = submission;
        read_index = i;
      }
    }
    if (!any_earlier) {
      is_cache_miss = true;
      read_index = write_index;
      if (!AwaitAllQueueOperationsCompletion()) {
        return true;
      }
      newest_submission = rb.submission_written[write_index];
    }
    if (!newest_submission || newest_submission == rb.published_submission) {
      // Nothing newer finished yet; the guest keeps the previous data.
      return true;
    }
    rb.published_submission = newest_submission;
  } else if (!AwaitAllQueueOperationsCompletion()) {
    return true;
  }

  bool should_copy = (readback_mode == ReadbackResolveMode::kSome) ? is_cache_miss : true;
  if (should_copy && rb.buffers[read_index] && written_length <= rb.sizes[read_index] &&
      rb.mapped_data[read_index]) {
    uint8_t* destination = memory_->TranslatePhysical(written_address);
    if (destination) {
      // Not over pages that the guest has written since: the copied range
      // covers whole tiles (and may be from an earlier frame), so it can hold
      // guest data next to the resolved texture.
      const auto* data = static_cast<const uint8_t*>(rb.mapped_data[read_index]);
      if (readback_publish_thread_.joinable()) {
        rb.publish_sequence[read_index] =
            QueueReadbackPublish(written_address, written_length, data);
        if (is_cache_miss) {
          // The first data of a key is in guest memory before the command
          // processor continues, as without the thread.
          AwaitReadbackPublish(rb.publish_sequence[read_index]);
        }
      } else {
        const uint32_t skipped =
            shared_memory_->CopyReadbackToGuest(written_address, written_length, data);
        TraceReadbackSkip(written_address, written_length, skipped);
      }
      perf_trace::CountReadback(written_length);
      read_trace::OnReadback(*memory_, written_address, written_length);
      TraceResolvePublication(written_address, written_length,
                              static_cast<const uint8_t*>(rb.mapped_data[read_index]));
      DumpReadback(*register_file_, written_address, written_length,
                   static_cast<const uint8_t*>(rb.mapped_data[read_index]));
    }
  }
  return true;
}

bool D3D12CommandProcessor::GetResolvePixelSizeLog2(uint32_t& pixel_size_log2) const {
  reg::RB_COPY_DEST_INFO copy_dest_info = register_file_->Get<reg::RB_COPY_DEST_INFO>();
  const FormatInfo* format_info = FormatInfo::Get(uint32_t(copy_dest_info.copy_dest_format));
  uint32_t bits_per_pixel = format_info->bits_per_pixel;
  if (bits_per_pixel != 8 && bits_per_pixel != 16 && bits_per_pixel != 32 &&
      bits_per_pixel != 64) {
    return false;
  }
  return rex::bit_scan_forward(bits_per_pixel >> 3, &pixel_size_log2);
}

bool D3D12CommandProcessor::RecordReadbackCopy(uint32_t address, uint32_t length,
                                               uint32_t pixel_size_log2,
                                               ID3D12Resource* destination,
                                               uint64_t destination_offset) {
  if (texture_cache_->IsDrawResolutionScaled()) {
    if (!resolve_downscale_pipeline_ || !resolve_downscale_root_signature_) {
      return false;
    }

    uint32_t tile_size_1x = 32 * 32 * (uint32_t(1) << pixel_size_log2);
    uint32_t tile_count = length / tile_size_1x;
    if (!tile_count) {
      return false;
    }

    uint32_t scaled_length = uint32_t(texture_cache_->GetCurrentScaledResolveRangeLengthScaled());
    uint64_t scaled_address = texture_cache_->GetCurrentScaledResolveRangeStartScaled();
    if (!scaled_length) {
      return false;
    }

    uint32_t downscale_buffer_size = AlignReadbackBufferSize(length);
    if (downscale_buffer_size > resolve_downscale_buffer_size_) {
      const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
      ID3D12Device* device = provider.GetDevice();
      D3D12_RESOURCE_DESC buffer_desc;
      ui::d3d12::util::FillBufferResourceDesc(buffer_desc, downscale_buffer_size,
                                              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
      ID3D12Resource* buffer = nullptr;
      if (FAILED(device->CreateCommittedResource(
              &ui::d3d12::util::kHeapPropertiesDefault, provider.GetHeapFlagCreateNotZeroed(),
              &buffer_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
              IID_PPV_ARGS(&buffer)))) {
        REXGPU_ERROR("Failed to create a {} MB resolve downscale buffer",
                     downscale_buffer_size >> 20);
        return false;
      }
      if (resolve_downscale_buffer_) {
        resources_for_deletion_.emplace_back(GetCurrentSubmission(),
                                             resolve_downscale_buffer_.Detach());
      }
      resolve_downscale_buffer_.Attach(buffer);
      resolve_downscale_buffer_size_ = downscale_buffer_size;
    }

    if (!resolve_downscale_buffer_) {
      return false;
    }

    ID3D12Resource* scaled_resolve_buffer = texture_cache_->GetCurrentScaledResolveBufferResource();
    size_t scaled_resolve_buffer_index = texture_cache_->GetCurrentScaledResolveBufferIndexPublic();
    if (!scaled_resolve_buffer) {
      return false;
    }
    uint64_t scaled_buffer_base = uint64_t(scaled_resolve_buffer_index) << 30;
    if (scaled_address < scaled_buffer_base) {
      return false;
    }
    uint64_t source_offset = scaled_address - scaled_buffer_base;

    ui::d3d12::util::DescriptorCpuGpuHandlePair downscale_descriptors[2];
    if (!RequestOneUseSingleViewDescriptors(2, downscale_descriptors)) {
      return false;
    }

    const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
    ID3D12Device* device = provider.GetDevice();
    uint32_t aligned_scaled_length =
        rex::align(scaled_length, uint32_t(D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT));
    ui::d3d12::util::CreateBufferRawSRV(device, downscale_descriptors[0].first,
                                        scaled_resolve_buffer, aligned_scaled_length,
                                        source_offset);
    uint32_t aligned_length =
        rex::align(length, uint32_t(D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT));
    ui::d3d12::util::CreateBufferRawUAV(device, downscale_descriptors[1].first,
                                        resolve_downscale_buffer_.Get(), aligned_length, 0);

    PushUAVBarrier(scaled_resolve_buffer);
    texture_cache_->TransitionCurrentScaledResolveRange(
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    SubmitBarriers();

    SetExternalPipeline(resolve_downscale_pipeline_.Get());
    deferred_command_list_.D3DSetComputeRootSignature(resolve_downscale_root_signature_.Get());
    ResolveDownscaleConstants constants;
    constants.scale_x = texture_cache_->draw_resolution_scale_x();
    constants.scale_y = texture_cache_->draw_resolution_scale_y();
    constants.pixel_size_log2 = pixel_size_log2;
    constants.tile_count = tile_count;
    constants.half_pixel_offset = (REXCVAR_GET(readback_resolve_half_pixel_offset) &&
                                   (constants.scale_x > 1 || constants.scale_y > 1))
                                      ? 1u
                                      : 0u;
    deferred_command_list_.D3DSetComputeRoot32BitConstants(
        UINT(ResolveDownscaleRootParameter::kConstants), sizeof(constants) / sizeof(uint32_t),
        &constants, 0);
    deferred_command_list_.D3DSetComputeRootDescriptorTable(
        UINT(ResolveDownscaleRootParameter::kSource), downscale_descriptors[0].second);
    deferred_command_list_.D3DSetComputeRootDescriptorTable(
        UINT(ResolveDownscaleRootParameter::kDestination), downscale_descriptors[1].second);
    deferred_command_list_.D3DDispatch(tile_count, 1, 1);

    PushUAVBarrier(resolve_downscale_buffer_.Get());
    PushTransitionBarrier(resolve_downscale_buffer_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                          D3D12_RESOURCE_STATE_COPY_SOURCE);
    SubmitBarriers();
    deferred_command_list_.D3DCopyBufferRegion(destination, destination_offset,
                                               resolve_downscale_buffer_.Get(), 0, length);
    PushTransitionBarrier(resolve_downscale_buffer_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                          D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    texture_cache_->TransitionCurrentScaledResolveRange(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    SubmitBarriers();
  } else {
    shared_memory_->UseAsCopySource();
    SubmitBarriers();
    ID3D12Resource* shared_memory_buffer = shared_memory_->GetBuffer();
    deferred_command_list_.D3DCopyBufferRegion(destination, destination_offset,
                                               shared_memory_buffer, address, length);
  }
  return true;
}

void D3D12CommandProcessor::CheckSubmissionFence(uint64_t await_submission) {
  if (await_submission >= submission_current_) {
    if (submission_open_) {
      EndSubmission(false);
    }
    // Ending an open submission should result in queue operations done directly
    // (like UpdateTileMappings) to be tracked within the scope of that
    // submission, but just in case of a failure, or queue operations being done
    // outside of a submission, await explicitly.
    if (queue_operations_done_since_submission_signal_) {
      UINT64 fence_value = ++queue_operations_since_submission_fence_last_;
      ID3D12CommandQueue* direct_queue = GetD3D12Provider().GetDirectQueue();
      if (SUCCEEDED(direct_queue->Signal(queue_operations_since_submission_fence_, fence_value) &&
                    SUCCEEDED(queue_operations_since_submission_fence_->SetEventOnCompletion(
                        fence_value, fence_completion_event_)))) {
        PROFILE_CMD_BUFFER_STALL();
        const uint64_t perf_start = perf_trace::Now();
        WaitForSingleObject(fence_completion_event_, INFINITE);
        perf_trace::AddFenceWait(perf_start);
        queue_operations_done_since_submission_signal_ = false;
      } else {
        REXGPU_ERROR(
            "Failed to await an out-of-submission queue operation completion "
            "Direct3D 12 fence");
      }
    }
    // A submission won't be ended if it hasn't been started, or if ending
    // has failed - clamp the index.
    await_submission = submission_current_ - 1;
  }

  uint64_t submission_completed_before = submission_completed_;
  submission_completed_ = submission_fence_->GetCompletedValue();
  if (submission_completed_ < await_submission) {
    if (SUCCEEDED(
            submission_fence_->SetEventOnCompletion(await_submission, fence_completion_event_))) {
      PROFILE_CMD_BUFFER_STALL();
      const uint64_t perf_start = perf_trace::Now();
      WaitForSingleObject(fence_completion_event_, INFINITE);
      perf_trace::AddFenceWait(perf_start);
      submission_completed_ = submission_fence_->GetCompletedValue();
    }
  }
  if (submission_completed_ < await_submission) {
    REXGPU_ERROR("Failed to await a submission completion Direct3D 12 fence");
  }
  if (submission_completed_ <= submission_completed_before) {
    // Not updated - no need to reclaim or download things.
    return;
  }

  RetireROVOcclusionSnapshots();

  // Reclaim command allocators.
  while (command_allocator_submitted_first_) {
    if (command_allocator_submitted_first_->last_usage_submission > submission_completed_) {
      break;
    }
    if (command_allocator_writable_last_) {
      command_allocator_writable_last_->next = command_allocator_submitted_first_;
    } else {
      command_allocator_writable_first_ = command_allocator_submitted_first_;
    }
    command_allocator_writable_last_ = command_allocator_submitted_first_;
    command_allocator_submitted_first_ = command_allocator_submitted_first_->next;
    command_allocator_writable_last_->next = nullptr;
  }
  if (!command_allocator_submitted_first_) {
    command_allocator_submitted_last_ = nullptr;
  }

  // Release single-use bindless descriptors.
  while (!view_bindless_one_use_descriptors_.empty()) {
    if (view_bindless_one_use_descriptors_.front().second > submission_completed_) {
      break;
    }
    ReleaseViewBindlessDescriptorImmediately(view_bindless_one_use_descriptors_.front().first);
    view_bindless_one_use_descriptors_.pop_front();
  }

  // Delete transient resources marked for deletion.
  while (!resources_for_deletion_.empty()) {
    if (resources_for_deletion_.front().first > submission_completed_) {
      break;
    }
    resources_for_deletion_.front().second->Release();
    resources_for_deletion_.pop_front();
  }

  shared_memory_->CompletedSubmissionUpdated();

  render_target_cache_->CompletedSubmissionUpdated();

  primitive_processor_->CompletedSubmissionUpdated();

  texture_cache_->CompletedSubmissionUpdated(submission_completed_);
}

void D3D12CommandProcessor::LogDeviceRemovalDiagnostics(ID3D12Device* device, HRESULT reason) {
  const char* reason_str = "Unknown";
  switch (reason) {
    case DXGI_ERROR_DEVICE_HUNG:
      reason_str = "DEVICE_HUNG (TDR - GPU command took too long)";
      break;
    case DXGI_ERROR_DEVICE_REMOVED:
      reason_str = "DEVICE_REMOVED (driver internal error or hot-unplug)";
      break;
    case DXGI_ERROR_DEVICE_RESET:
      reason_str = "DEVICE_RESET (bad GPU command)";
      break;
    case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
      reason_str = "DRIVER_INTERNAL_ERROR";
      break;
    case DXGI_ERROR_INVALID_CALL:
      reason_str = "INVALID_CALL";
      break;
  }
  REXGPU_ERROR("D3D12 device removed: HRESULT 0x{:08X} - {}", static_cast<unsigned>(reason),
               reason_str);

  Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData> dred;
  if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dred)))) {
    return;
  }

  D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs = {};
  if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&breadcrumbs))) {
    for (const D3D12_AUTO_BREADCRUMB_NODE* node = breadcrumbs.pHeadAutoBreadcrumbNode; node;
         node = node->pNext) {
      if (!node->pLastBreadcrumbValue || !node->pCommandHistory ||
          *node->pLastBreadcrumbValue == 0) {
        continue;
      }
      REXGPU_ERROR("DRED breadcrumb: completed {} of {} ops", *node->pLastBreadcrumbValue,
                   node->BreadcrumbCount);
      uint32_t last = std::min(*node->pLastBreadcrumbValue, node->BreadcrumbCount);
      uint32_t start = last > 3 ? last - 3 : 0;
      uint32_t end = std::min(last + 1, node->BreadcrumbCount);
      for (uint32_t i = start; i < end; i++) {
        REXGPU_ERROR("  [{}] op type {}{}", i, static_cast<int>(node->pCommandHistory[i]),
                     i == last ? " <-- FAULT" : "");
      }
    }
  }

  D3D12_DRED_PAGE_FAULT_OUTPUT page_fault = {};
  if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&page_fault)) && page_fault.PageFaultVA != 0) {
    REXGPU_ERROR("DRED page fault at VA 0x{:016X}", page_fault.PageFaultVA);
  }
}

bool D3D12CommandProcessor::CreateLookFileTextures() {
  ShaderOverrides* shader_overrides = pipeline_cache_->shader_overrides();
  if (!shader_overrides) {
    return true;
  }
  const auto& files = shader_overrides->look_files();
  const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
  ID3D12Device* device = provider.GetDevice();
  static_assert(native_shader_contract::kMaxLookFiles <= kLookFileViewCount);
  for (uint32_t i = 0; i < uint32_t(files.size()); ++i) {
    // A file that could not be loaded is one neutral texel (LoadLookFiles).
    const ShaderOverrides::LookFile& file = files[i];
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = file.width;
    desc.Height = file.height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    if (FAILED(device->CreateCommittedResource(&ui::d3d12::util::kHeapPropertiesDefault,
                                               provider.GetHeapFlagCreateNotZeroed(), &desc,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&texture)))) {
      REXGPU_ERROR("Native shader contract: cannot create the texture of look file {}", file.name);
      return false;
    }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT rows;
    UINT64 row_size, total_size;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &row_size, &total_size);
    D3D12_RESOURCE_DESC upload_desc;
    ui::d3d12::util::FillBufferResourceDesc(upload_desc, total_size, D3D12_RESOURCE_FLAG_NONE);
    Microsoft::WRL::ComPtr<ID3D12Resource> upload;
    uint8_t* mapping = nullptr;
    if (FAILED(device->CreateCommittedResource(&ui::d3d12::util::kHeapPropertiesUpload,
                                               provider.GetHeapFlagCreateNotZeroed(), &upload_desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&upload))) ||
        FAILED(upload->Map(0, nullptr, reinterpret_cast<void**>(&mapping)))) {
      REXGPU_ERROR("Native shader contract: cannot upload look file {}", file.name);
      return false;
    }
    for (UINT row = 0; row < rows; ++row) {
      std::memcpy(mapping + footprint.Offset + size_t(footprint.Footprint.RowPitch) * row,
                  file.rgba.data() + size_t(file.width) * 4 * row, size_t(file.width) * 4);
    }
    upload->Unmap(0, nullptr);
    // The texels are in the upload buffer. SetupContext runs once a process; a
    // context set up again would create the pipeline cache, and with it the
    // look files, again.
    shader_overrides->ReleaseLookFileTexels(i);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = desc.Format;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2DArray.MipLevels = 1;
    srv_desc.Texture2DArray.ArraySize = 1;
    device->CreateShaderResourceView(
        texture.Get(), &srv_desc,
        provider.OffsetViewDescriptor(view_bindless_heap_cpu_start_,
                                      uint32_t(SystemBindlessView::kLookFileViewsStart) + i));
    look_file_textures_.push_back(std::move(texture));
    look_file_uploads_.push_back(std::move(upload));
    look_file_footprints_.push_back(footprint);
  }
  return true;
}

void D3D12CommandProcessor::RecordLookFileUploads() {
  if (look_file_uploads_.empty()) {
    return;
  }
  if (look_file_upload_submission_) {
    // The upload buffers are free once the submission that copied them is.
    if (GetCompletedSubmission() >= look_file_upload_submission_) {
      look_file_uploads_.clear();
      look_file_footprints_.clear();
    }
    return;
  }
  // Before the submission's first draw: every texture from its upload buffer.
  for (size_t i = 0; i < look_file_textures_.size(); ++i) {
    D3D12_TEXTURE_COPY_LOCATION destination = {};
    destination.pResource = look_file_textures_[i].Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION source = {};
    source.pResource = look_file_uploads_[i].Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint = look_file_footprints_[i];
    deferred_command_list_.CopyTexture(destination, source);
    PushTransitionBarrier(look_file_textures_[i].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  }
  SubmitBarriers();
  look_file_upload_submission_ = submission_current_;
}

bool D3D12CommandProcessor::BeginSubmission(bool is_guest_command) {
#if XE_GPU_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_GPU_FINE_GRAINED_DRAW_SCOPES

  if (device_removed_) {
    return false;
  }

  bool is_opening_frame = is_guest_command && !frame_open_;
  if (submission_open_ && !is_opening_frame) {
    return true;
  }

  // Check if the device is still available.
  ID3D12Device* device = GetD3D12Provider().GetDevice();
  HRESULT device_removed_reason = device->GetDeviceRemovedReason();
  if (FAILED(device_removed_reason)) {
    device_removed_ = true;
    LogDeviceRemovalDiagnostics(device, device_removed_reason);
    if (graphics_system_) {
      graphics_system_->OnHostGpuLossFromAnyThread(device_removed_reason !=
                                                   DXGI_ERROR_DEVICE_REMOVED);
    }
    return false;
  }

  // Check the fence - needed for all kinds of submissions (to reclaim transient
  // resources early) and specifically for frames (not to queue too many), and
  // await the availability of the current frame.
  {
    perf_trace::CauseScope perf_cause(is_opening_frame ? perf_trace::Wait::kFrameLatency
                                                       : perf_trace::Wait::kOther);
    CheckSubmissionFence(is_opening_frame ? closed_frame_submissions_[frame_current_ % kQueueFrames]
                                          : 0);
  }
  // TODO(Triang3l): If failed to await (completed submission < awaited frame
  // submission), do something like dropping the draw command that wanted to
  // open the frame.
  if (is_opening_frame) {
    // Update the completed frame index, also obtaining the actual completed
    // frame number (since the CPU may be actually less than 3 frames behind)
    // before reclaiming resources tracked with the frame number.
    frame_completed_ = std::max(frame_current_, uint64_t(kQueueFrames)) - kQueueFrames;
    for (uint64_t frame = frame_completed_ + 1; frame < frame_current_; ++frame) {
      if (closed_frame_submissions_[frame % kQueueFrames] > submission_completed_) {
        break;
      }
      frame_completed_ = frame;
    }
  }

  const bool opening_submission = !submission_open_;
  if (!submission_open_) {
    submission_open_ = true;
    submission_draws_ = 0;
    // Each submission's deferred command list captures every register it reads
    // (d3d12_deferred_constants, d3d12_draw_back_end).
    pending_constants_.SetAll();
    back_end_list_captured_ = false;

    // Start a new deferred command list - will submit it to the real one in the
    // end of the submission (when async pipeline creation requests are
    // fulfilled).
    deferred_command_list_.Reset();

    // Reset cached state of the command list.
    ff_viewport_update_needed_ = true;
    ff_scissor_update_needed_ = true;
    ff_blend_factor_update_needed_ = true;
    ff_stencil_ref_update_needed_ = true;
    viewport_cache_valid_ = false;
    current_guest_pipeline_ = nullptr;
    current_external_pipeline_ = nullptr;
    current_graphics_root_signature_ = nullptr;
    current_graphics_root_up_to_date_ = 0;
    if (bindless_resources_used_) {
      // With d3d12_draw_back_end=2 the replay owns the sampler heap and sets it
      // in place of nullptr.
      deferred_command_list_.SetDescriptorHeaps(
          view_bindless_heap_, draw_back_end_ == 2 ? nullptr : sampler_bindless_heap_current_);
    } else {
      view_bindful_heap_current_ = nullptr;
      sampler_bindful_heap_current_ = nullptr;
    }
    primitive_topology_ = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

    render_target_cache_->BeginSubmission();

    primitive_processor_->BeginSubmission();

    texture_cache_->BeginSubmission(submission_current_);

    RecordLookFileUploads();
  }

  if (is_opening_frame) {
    ReadGpuTimestampFrame();
    {
      // native_rt_resolve_check (d3d-hle.md 16.7).
      uint32_t checked, mismatched;
      texture_cache_->PollResolveTextureChecks(submission_completed_, checked, mismatched);
      perf_trace::CountNativeResolveChecks(checked, mismatched);
      render_target_cache_->PollHostResolveChecks(submission_completed_, checked, mismatched);
      perf_trace::CountHostResolveChecks(checked, mismatched);
    }
    frame_open_ = true;

    // Reset bindings that depend on the data stored in the pools.
    std::memset(current_float_constant_map_vertex_, 0, sizeof(current_float_constant_map_vertex_));
    std::memset(current_float_constant_map_pixel_, 0, sizeof(current_float_constant_map_pixel_));
    cbuffer_binding_system_.up_to_date = false;
    cbuffer_binding_float_vertex_.up_to_date = false;
    cbuffer_binding_float_pixel_.up_to_date = false;
    cbuffer_binding_bool_loop_.up_to_date = false;
    cbuffer_binding_fetch_.up_to_date = false;
    current_shared_memory_binding_is_uav_.reset();
    if (bindless_resources_used_) {
      cbuffer_binding_descriptor_indices_vertex_.up_to_date = false;
      cbuffer_binding_descriptor_indices_pixel_.up_to_date = false;
    } else {
      draw_view_bindful_heap_index_ = ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid;
      draw_sampler_bindful_heap_index_ = ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid;
      bindful_textures_written_vertex_ = false;
      bindful_textures_written_pixel_ = false;
      bindful_samplers_written_vertex_ = false;
      bindful_samplers_written_pixel_ = false;
    }

    // Reclaim pool pages - no need to do this every small submission since some
    // may be reused.
    constant_buffer_pool_->Reclaim(frame_completed_);
    if (!bindless_resources_used_) {
      view_bindful_heap_pool_->Reclaim(frame_completed_);
      sampler_bindful_heap_pool_->Reclaim(frame_completed_);
    }
    EvictOldReadbackBuffers(readback_buffers_);
    EvictOldReadbackBuffers(memexport_readback_buffers_);

    primitive_processor_->BeginFrame();

    texture_cache_->BeginFrame();
  }

  if (opening_submission || is_opening_frame) {
    gpu_timestamp_submission_ = BeginGpuTimestamp(GpuTimestampKind::kSubmission);
  }
  if (opening_submission) {
    if (frame_graph_submission_ && submission_completed_ >= frame_graph_submission_) {
      ReadFrameGraphMarks();
    }
    if (draw_stats_submission_ && submission_completed_ >= draw_stats_submission_) {
      ReadDrawStats();
    }
    if (frame_graph_recording_)
      FrameGraphMark(kFrameGraphMarkSubmissionBegin);
  }
  if (opening_submission) {
    OpenRTVOcclusionQuery();
  }

  return true;
}

bool D3D12CommandProcessor::EndSubmission(bool is_swap) {
  const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();

  // Make sure there is a command allocator to write commands to.
  if (submission_open_ && !command_allocator_writable_first_) {
    ID3D12CommandAllocator* command_allocator;
    if (FAILED(provider.GetDevice()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                            IID_PPV_ARGS(&command_allocator)))) {
      REXGPU_ERROR("Failed to create a command allocator");
      // Try to submit later. Completely dropping the submission is not
      // permitted because resources would be left in an undefined state.
      return false;
    }
    command_allocator_writable_first_ = new CommandAllocator;
    command_allocator_writable_first_->command_allocator = command_allocator;
    command_allocator_writable_first_->last_usage_submission = 0;
    command_allocator_writable_first_->next = nullptr;
    command_allocator_writable_last_ = command_allocator_writable_first_;
  }

  bool is_closing_frame = is_swap && frame_open_;

  if (is_closing_frame) {
    texture_cache_->EndFrame();

    primitive_processor_->EndFrame();
  }

  if (submission_open_) {
    assert_false(scratch_buffer_used_);

    // Not across submissions: the source render targets may be destroyed after.
    render_target_cache_->PerformPendingTransfers();

    if (active_occlusion_query_.valid && occlusion_query_heap_) {
      deferred_command_list_.D3DEndQuery(occlusion_query_heap_.Get(), D3D12_QUERY_TYPE_OCCLUSION,
                                         active_occlusion_query_.host_index);
      active_occlusion_query_ = {};
    }

    pipeline_cache_->EndSubmission();

    // Submit barriers now because resources with the queued barriers may be
    // destroyed between frames.
    SubmitBarriers();

    CloseRTVOcclusionQuery();
    EndGpuTimestamp(gpu_timestamp_submission_);
    gpu_timestamp_submission_ = kGpuTimestampInvalid;
    if (is_closing_frame) {
      ResolveGpuTimestampFrame();
    }
    if (frame_graph_recording_) {
      FrameGraphMark(kFrameGraphMarkSubmissionEnd);
      if (is_closing_frame && frame_graph_closing_) {
        deferred_command_list_.D3DResolveQueryData(
            frame_graph_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0,
            uint32_t(frame_graph_labels_.size()), frame_graph_readback_.Get(), 0);
        frame_graph_submission_ = submission_current_;
        frame_graph_recording_ = false;
        frame_graph_closing_ = false;
      }
    }

    ID3D12CommandQueue* direct_queue = provider.GetDirectQueue();
    perf_trace::ZoneScope perf_zone(perf_trace::Zone::kSubmit);

    // Submit the deferred command list.
    // Only one deferred command list must be executed in the same
    // ExecuteCommandLists - the boundaries of ExecuteCommandLists are a full
    // UAV and aliasing barrier, and subsystems of the emulator assume it
    // happens between Xenia submissions.
    ID3D12CommandAllocator* command_allocator =
        command_allocator_writable_first_->command_allocator;
    const bool on_submit_thread = submit_thread_.joinable() && (!is_swap || submit_async_swap_);
    if (on_submit_thread) {
      SubmitJob job;
      {
        std::lock_guard<std::mutex> lock(submit_mutex_);
        if (!submit_free_lists_.empty()) {
          job.list = std::move(submit_free_lists_.back());
          submit_free_lists_.pop_back();
        }
      }
      if (!job.list) {
        job.list = std::make_unique<DeferredCommandList>(*this);
      }
      job.list->Swap(deferred_command_list_);
      job.allocator = command_allocator;
      job.submission = submission_current_;
      {
        std::lock_guard<std::mutex> lock(submit_mutex_);
        submit_jobs_.push_back(std::move(job));
      }
      submit_request_cond_.notify_one();
    } else {
      FlushSubmitThread();
      if (draw_back_end_ == 2) {
        pipeline_cache_->SwapCompiledMods(submission_current_);
      }
      command_allocator->Reset();
      command_list_->Reset(command_allocator, nullptr);
      uint64_t perf_start = perf_trace::ZoneStart();
      deferred_command_list_.Execute(command_list_, command_list_1_);
      perf_trace::ZoneEnd(perf_trace::Zone::kSubmitRecord, perf_start);
      perf_start = perf_trace::ZoneStart();
      command_list_->Close();
      ID3D12CommandList* execute_command_lists[] = {command_list_};
      direct_queue->ExecuteCommandLists(1, execute_command_lists);
      perf_trace::ZoneEnd(perf_trace::Zone::kSubmitExecute, perf_start);
    }
    perf_trace::Increment(perf_trace::Count::kSubmissions);
    command_allocator_writable_first_->last_usage_submission = submission_current_;
    if (command_allocator_submitted_last_) {
      command_allocator_submitted_last_->next = command_allocator_writable_first_;
    } else {
      command_allocator_submitted_first_ = command_allocator_writable_first_;
    }
    command_allocator_submitted_last_ = command_allocator_writable_first_;
    command_allocator_writable_first_ = command_allocator_writable_first_->next;
    command_allocator_submitted_last_->next = nullptr;
    if (!command_allocator_writable_first_) {
      command_allocator_writable_last_ = nullptr;
    }

    if (!on_submit_thread) {
      direct_queue->Signal(submission_fence_, submission_current_);
    }
    ++submission_current_;

    submission_open_ = false;

    // Queue operations done directly (like UpdateTileMappings) will be awaited
    // alongside the last submission if needed.
    queue_operations_done_since_submission_signal_ = false;
  }

  if (is_closing_frame) {
    if (REXCVAR_GET(clear_memory_page_state) && shared_memory_) {
      shared_memory_->SetSystemPageBlocksValidWithGpuDataWritten();
    }
    frame_open_ = false;
    // Submission already closed now, so minus 1.
    closed_frame_submissions_[(frame_current_++) % kQueueFrames] = submission_current_ - 1;

    if (cache_clear_requested_ && AwaitAllQueueOperationsCompletion()) {
      cache_clear_requested_ = false;

      ClearCommandAllocatorCache();

      ui::d3d12::util::ReleaseAndNull(scratch_buffer_);
      scratch_buffer_size_ = 0;

      if (bindless_resources_used_) {
        texture_cache_bindless_sampler_map_.clear();
        for (const auto& sampler_bindless_heap_overflowed : sampler_bindless_heaps_overflowed_) {
          sampler_bindless_heap_overflowed.first->Release();
        }
        sampler_bindless_heaps_overflowed_.clear();
        sampler_bindless_heap_allocated_ = 0;
      } else {
        sampler_bindful_heap_pool_->ClearCache();
        view_bindful_heap_pool_->ClearCache();
      }
      constant_buffer_pool_->ClearCache();
      if (back_end_constant_pool_) {
        back_end_constant_pool_->ClearCache();
        back_end_pool_frame_ = UINT64_MAX;
      }

      texture_cache_->ClearCache();

      // Not clearing the root signatures as they're referenced by pipelines,
      // which are not destroyed.

      primitive_processor_->ClearCache();

      render_target_cache_->ClearCache();

      shared_memory_->ClearCache();
    }
  }

  return true;
}

bool D3D12CommandProcessor::CanEndSubmissionImmediately() const {
  return !submission_open_ || !pipeline_cache_->IsCreatingPipelines();
}

void D3D12CommandProcessor::ClearCommandAllocatorCache() {
  while (command_allocator_submitted_first_) {
    auto next = command_allocator_submitted_first_->next;
    command_allocator_submitted_first_->command_allocator->Release();
    delete command_allocator_submitted_first_;
    command_allocator_submitted_first_ = next;
  }
  command_allocator_submitted_last_ = nullptr;
  while (command_allocator_writable_first_) {
    auto next = command_allocator_writable_first_->next;
    command_allocator_writable_first_->command_allocator->Release();
    delete command_allocator_writable_first_;
    command_allocator_writable_first_ = next;
  }
  command_allocator_writable_last_ = nullptr;
}

void D3D12CommandProcessor::UpdateFixedFunctionState(
    const draw_util::ViewportInfo& viewport_info, const draw_util::Scissor& scissor,
    bool primitive_polygonal, reg::RB_DEPTHCONTROL normalized_depth_control) {
#if XE_GPU_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_GPU_FINE_GRAINED_DRAW_SCOPES

  // Viewport.
  D3D12_VIEWPORT viewport;
  viewport.TopLeftX = float(viewport_info.xy_offset[0]);
  viewport.TopLeftY = float(viewport_info.xy_offset[1]);
  viewport.Width = float(viewport_info.xy_extent[0]);
  viewport.Height = float(viewport_info.xy_extent[1]);
  viewport.MinDepth = viewport_info.z_min;
  viewport.MaxDepth = viewport_info.z_max;
  SetViewport(viewport);

  // Scissor.
  D3D12_RECT scissor_rect;
  scissor_rect.left = LONG(scissor.offset[0]);
  scissor_rect.top = LONG(scissor.offset[1]);
  scissor_rect.right = LONG(scissor.offset[0] + scissor.extent[0]);
  scissor_rect.bottom = LONG(scissor.offset[1] + scissor.extent[1]);
  SetScissorRect(scissor_rect);

  if (render_target_cache_->GetPath() == RenderTargetCache::Path::kHostRenderTargets) {
    // Blend factor and stencil reference value (draw_back_end, N4 step 2).
    float blend_factor[4];
    uint32_t stencil_ref;
    draw_back_end::ComputeBlendFactorAndStencilReference(
        *register_file_, primitive_polygonal, normalized_depth_control, blend_factor, stencil_ref);
    // std::memcmp instead of != so in case of NaN, every draw won't be
    // invalidating it.
    ff_blend_factor_update_needed_ |=
        std::memcmp(ff_blend_factor_, blend_factor, sizeof(float) * 4) != 0;
    if (ff_blend_factor_update_needed_) {
      std::memcpy(ff_blend_factor_, blend_factor, sizeof(float) * 4);
      deferred_command_list_.D3DOMSetBlendFactor(ff_blend_factor_);
      ff_blend_factor_update_needed_ = false;
    }

    ff_stencil_ref_update_needed_ |= ff_stencil_ref_ != stencil_ref;
    if (ff_stencil_ref_update_needed_) {
      ff_stencil_ref_ = stencil_ref;
      deferred_command_list_.D3DOMSetStencilRef(ff_stencil_ref_);
      ff_stencil_ref_update_needed_ = false;
    }
  }
}

void D3D12CommandProcessor::UpdateSystemConstantValues(
    bool shared_memory_is_uav, bool primitive_polygonal, uint32_t line_loop_closing_index,
    xenos::Endian index_endian, const draw_util::ViewportInfo& viewport_info,
    uint32_t used_texture_mask, reg::RB_DEPTHCONTROL normalized_depth_control,
    uint32_t normalized_color_mask) {
#if XE_GPU_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_GPU_FINE_GRAINED_DRAW_SCOPES

  // The values come from draw_back_end::ComputeSystemConstants, which reads
  // the register file it is given (N4 step 2, d3d-hle-n4.md 17.6).
  FillSystemConstantInputs(shared_memory_is_uav, primitive_polygonal, line_loop_closing_index,
                           index_endian, used_texture_mask, normalized_depth_control,
                           normalized_color_mask);
  bool dirty = draw_back_end::ComputeSystemConstants(*register_file_, viewport_info,
                                                     system_constant_inputs_, system_constants_);

  // Live knobs of the shader mods, changed by shader_mod_set.
  if (shader_mod_knobs_source_ &&
      shader_mod_knobs_source_->UpdateKnobs(shader_mod_knobs_, shader_mod_knobs_generation_)) {
    dirty = true;
  }
  // The host values of the shader mods: the guest vblank rate, which a title
  // hook may switch while running (SetVblankRate), the title's frame interval,
  // which changes with every swap, its design frame rate, and the native shader
  // contract's pass class.
  if (shader_mod_host_values_used_) {
    const float host_values[4] = {float(graphics_system_->guest_vblank_rate()),
                                  float(graphics_system_->guest_frame_interval()),
                                  float(graphics_system_->guest_design_frame_rate()),
                                  float(native_shader_pass_)};
    if (std::memcmp(host_values, shader_mod_host_values_, sizeof(host_values))) {
      std::memcpy(shader_mod_host_values_, host_values, sizeof(host_values));
      dirty = true;
    }
  }

  cbuffer_binding_system_.up_to_date &= !dirty;
}

bool D3D12CommandProcessor::UpdateRenderTargets(bool is_rasterization_done,
                                                reg::RB_DEPTHCONTROL normalized_depth_control,
                                                uint32_t normalized_color_mask,
                                                const Shader& vertex_shader) {
  RenderTargetUpdateKey key;
  bool keyed = false;
  if (render_target_update_guard_) {
    const RegisterFile& regs = *register_file_;
    // Without clipping, the draw extent may come from the vertices (executed
    // on the CPU): not a function of the registers.
    if (!regs.Get<reg::PA_CL_CLIP_CNTL>().clip_disable) {
      keyed = true;
      std::memset(&key, 0, sizeof(key));
      key.rasterization_done = is_rasterization_done;
      key.depth_control = normalized_depth_control.value;
      key.color_mask = normalized_color_mask;
      key.surface_info = regs[XE_GPU_REG_RB_SURFACE_INFO];
      key.depth_info = regs[XE_GPU_REG_RB_DEPTH_INFO];
      for (uint32_t i = 0; i < 4; ++i) {
        key.color_info[i] = regs[reg::RB_COLOR_INFO::rt_register_indices[i]];
      }
      key.window_offset = regs[XE_GPU_REG_PA_SC_WINDOW_OFFSET];
      key.window_offset_disable = regs.Get<reg::PA_SC_WINDOW_SCISSOR_TL>().window_offset_disable;
      key.window_scissor_br = regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR];
      key.screen_scissor_br = regs[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR];
      key.vte_cntl = regs[XE_GPU_REG_PA_CL_VTE_CNTL];
      key.vtx_window_offset_enable = regs.Get<reg::PA_SU_SC_MODE_CNTL>().vtx_window_offset_enable;
      key.vtx_cntl = regs[XE_GPU_REG_PA_SU_VTX_CNTL];
      key.vport_yscale = regs[XE_GPU_REG_PA_CL_VPORT_YSCALE];
      key.vport_yoffset = regs[XE_GPU_REG_PA_CL_VPORT_YOFFSET];
    }
  }
  perf_trace::RenderTargetGuardCounts& counts = perf_trace::render_target_guard;
  if (keyed) {
    ++counts.draws;
  }
  if (keyed && render_target_update_key_valid_ &&
      !std::memcmp(&key, &render_target_update_key_, sizeof(key)) &&
      RenderTargetCache::update_epoch() == render_target_update_epoch_ &&
      render_target_cache_->CanRepeatLastUpdate()) {
    ++counts.skips;
    if (!render_target_update_check_) {
      return true;
    }
    // The check: the update this draw skips must change nothing - no command
    // recorded, no state the epoch counts, the same render targets bound, and
    // nothing left for the next update.
    uint32_t formats_before[1 + xenos::kMaxColorRenderTargets];
    const uint32_t bound_before = render_target_cache_->GetLastUpdateBoundRenderTargets(formats_before);
    const size_t stream_before = deferred_command_list_.GetStreamSize();
    if (!render_target_cache_->Update(is_rasterization_done, normalized_depth_control,
                                      normalized_color_mask, vertex_shader)) {
      render_target_update_key_valid_ = false;
      return false;
    }
    uint32_t formats_after[1 + xenos::kMaxColorRenderTargets];
    const uint32_t bound_after = render_target_cache_->GetLastUpdateBoundRenderTargets(formats_after);
    bool same = deferred_command_list_.GetStreamSize() == stream_before &&
                RenderTargetCache::update_epoch() == render_target_update_epoch_ &&
                bound_after == bound_before &&
                !std::memcmp(formats_after, formats_before, sizeof(formats_after)) &&
                render_target_cache_->CanRepeatLastUpdate();
    ++counts.checks;
    if (render_target_update_perturb_ &&
        ++render_target_update_checks_ % render_target_update_perturb_ == 0) {
      same = !same;
      ++counts.perturbed;
    }
    if (!same) {
      ++counts.mismatches;
      static uint32_t messages = 0;
      if (messages < 16) {
        ++messages;
        REXGPU_ERROR("Render target update guard: a skipped update changed the state");
      }
    }
    render_target_update_epoch_ = RenderTargetCache::update_epoch();
    return true;
  }
  if (!render_target_cache_->Update(is_rasterization_done, normalized_depth_control,
                                    normalized_color_mask, vertex_shader)) {
    render_target_update_key_valid_ = false;
    return false;
  }
  render_target_update_key_valid_ = keyed;
  if (keyed) {
    render_target_update_key_ = key;
    render_target_update_epoch_ = RenderTargetCache::update_epoch();
  }
  return true;
}

void D3D12CommandProcessor::FillSystemConstantInputs(
    bool shared_memory_is_uav, bool primitive_polygonal, uint32_t line_loop_closing_index,
    xenos::Endian index_endian, uint32_t used_texture_mask,
    reg::RB_DEPTHCONTROL normalized_depth_control, uint32_t normalized_color_mask) {
  draw_back_end::SystemConstantInputs& inputs = system_constant_inputs_;
  inputs.edram_rov_used =
      render_target_cache_->GetPath() == RenderTargetCache::Path::kPixelShaderInterlock;
  inputs.rov_occlusion_query =
      inputs.edram_rov_used && rov_occlusion_initialized_ && REXCVAR_GET(occlusion_query_enable);
  inputs.host_render_targets_used =
      render_target_cache_->GetPath() == RenderTargetCache::Path::kHostRenderTargets;
  inputs.gamma_render_target_as_unorm16 = render_target_cache_->gamma_render_target_as_unorm16();
  inputs.fixed16_truncated_to_minus_1_to_1 = render_target_cache_->IsFixed16TruncatedToMinus1To1();
  inputs.shared_memory_is_uav = shared_memory_is_uav;
  inputs.primitive_polygonal = primitive_polygonal;
  inputs.index_endian = index_endian;
  inputs.line_loop_closing_index = line_loop_closing_index;
  inputs.used_texture_mask = used_texture_mask;
  inputs.normalized_color_mask = normalized_color_mask;
  inputs.normalized_depth_control = normalized_depth_control;
  inputs.draw_resolution_scale_x = texture_cache_->draw_resolution_scale_x();
  inputs.draw_resolution_scale_y = texture_cache_->draw_resolution_scale_y();
  inputs.scene_draw = current_draw_scene_;
  inputs.scene_scale = current_draw_scene_input_ ? render_target_cache_->scene_input_scale()
                                                 : render_target_cache_->scene_draw_scale();
  inputs.textures_resolution_scaled = 0;
  uint32_t textures_remaining = used_texture_mask;
  uint32_t texture_index;
  while (rex::bit_scan_forward(textures_remaining, &texture_index)) {
    textures_remaining &= ~(uint32_t(1) << texture_index);
    inputs.texture_swizzled_signs[texture_index] =
        texture_cache_->GetActiveTextureSwizzledSigns(texture_index);
    inputs.texture_integer_scale_bits[texture_index] =
        texture_cache_->GetActiveIntegerScaleBits(texture_index);
    inputs.textures_resolution_scaled |=
        uint32_t(texture_cache_->IsActiveTextureResolutionScaled(texture_index)) << texture_index;
  }
}

bool D3D12CommandProcessor::UpdateBindings(const D3D12Shader* vertex_shader,
                                           const D3D12Shader* pixel_shader,
                                           ID3D12RootSignature* root_signature,
                                           bool shared_memory_is_uav) {
  const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
  ID3D12Device* device = provider.GetDevice();
  const RegisterFile& regs = *register_file_;
  // Phases of the bindings zone (N2, d3d-hle.md 15.9): the root signature and
  // the common root constant buffers, the constant buffer data, the
  // descriptors, and the root parameters.
  uint64_t phase_start = perf_trace::DetailZoneStart();

#if XE_GPU_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_GPU_FINE_GRAINED_DRAW_SCOPES

  // Set the new root signature.
  if (current_graphics_root_signature_ != root_signature) {
    current_graphics_root_signature_ = root_signature;
    if (!bindless_resources_used_) {
      GetRootBindfulExtraParameterIndices(vertex_shader, pixel_shader,
                                          current_graphics_root_bindful_extras_);
    }
    // Changing the root signature invalidates all bindings.
    current_graphics_root_up_to_date_ = 0;
    deferred_command_list_.D3DSetGraphicsRootSignature(root_signature);
  }

  // Select the root parameter indices depending on the used binding model.
  uint32_t root_parameter_fetch_constants = bindless_resources_used_
                                                ? kRootParameter_Bindless_FetchConstants
                                                : kRootParameter_Bindful_FetchConstants;
  uint32_t root_parameter_float_constants_vertex =
      bindless_resources_used_ ? kRootParameter_Bindless_FloatConstantsVertex
                               : kRootParameter_Bindful_FloatConstantsVertex;
  uint32_t root_parameter_float_constants_pixel = bindless_resources_used_
                                                      ? kRootParameter_Bindless_FloatConstantsPixel
                                                      : kRootParameter_Bindful_FloatConstantsPixel;
  uint32_t root_parameter_system_constants = bindless_resources_used_
                                                 ? kRootParameter_Bindless_SystemConstants
                                                 : kRootParameter_Bindful_SystemConstants;
  uint32_t root_parameter_bool_loop_constants = bindless_resources_used_
                                                    ? kRootParameter_Bindless_BoolLoopConstants
                                                    : kRootParameter_Bindful_BoolLoopConstants;
  uint32_t root_parameter_shared_memory_and_bindful_edram =
      bindless_resources_used_ ? kRootParameter_Bindless_SharedMemory
                               : kRootParameter_Bindful_SharedMemoryAndEdram;

  //
  // Update root constant buffers that are common for bindful and bindless.
  //

  // These are the constant base addresses/ranges for shaders.
  // We have these hardcoded right now cause nothing seems to differ on the Xbox
  // 360 (however, OpenGL ES on Adreno 200 on Android has different ranges).
  assert_true(regs[XE_GPU_REG_SQ_VS_CONST] == 0x000FF000 ||
              regs[XE_GPU_REG_SQ_VS_CONST] == 0x00000000);
  assert_true(regs[XE_GPU_REG_SQ_PS_CONST] == 0x000FF100 ||
              regs[XE_GPU_REG_SQ_PS_CONST] == 0x00000000);
  perf_trace::ZoneEnd(perf_trace::Zone::kBindingsRoot, phase_start);
  phase_start = perf_trace::DetailZoneStart();
  // Check if the float constant layout is still the same and get the counts.
  const Shader::ConstantRegisterMap& float_constant_map_vertex =
      vertex_shader->constant_register_map();
  uint32_t float_constant_count_vertex = float_constant_map_vertex.float_count;
  for (uint32_t i = 0; i < 4; ++i) {
    if (current_float_constant_map_vertex_[i] != float_constant_map_vertex.float_bitmap[i]) {
      current_float_constant_map_vertex_[i] = float_constant_map_vertex.float_bitmap[i];
      // If no float constants at all, we can reuse any buffer for them, so not
      // invalidating.
      if (float_constant_count_vertex) {
        cbuffer_binding_float_vertex_.up_to_date = false;
      }
    }
  }
  uint32_t float_constant_count_pixel = 0;
  if (pixel_shader != nullptr) {
    const Shader::ConstantRegisterMap& float_constant_map_pixel =
        pixel_shader->constant_register_map();
    float_constant_count_pixel = float_constant_map_pixel.float_count;
    for (uint32_t i = 0; i < 4; ++i) {
      if (current_float_constant_map_pixel_[i] != float_constant_map_pixel.float_bitmap[i]) {
        current_float_constant_map_pixel_[i] = float_constant_map_pixel.float_bitmap[i];
        if (float_constant_count_pixel) {
          cbuffer_binding_float_pixel_.up_to_date = false;
        }
      }
    }
  } else {
    std::memset(current_float_constant_map_pixel_, 0, sizeof(current_float_constant_map_pixel_));
  }

  // Write the constant buffer data.
  if (!cbuffer_binding_system_.up_to_date) {
    // With shader overrides, the host values follow the system constants, and
    // with live knobs the knobs follow them, where the translations don't read.
    size_t system_constants_size =
        shader_mod_knobs_source_
            ? ShaderOverrides::kKnobsOffset + ShaderOverrides::kKnobsSize
            : shader_mod_host_values_used_
                  ? ShaderOverrides::kHostValuesOffset + ShaderOverrides::kHostValuesSize
                  : sizeof(system_constants_);
    uint8_t* system_constants = constant_buffer_pool_->Request(
        frame_current_, system_constants_size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT,
        nullptr, nullptr, &cbuffer_binding_system_.address);
    if (system_constants == nullptr) {
      return false;
    }
    std::memcpy(system_constants, &system_constants_, sizeof(system_constants_));
    if (shader_mod_host_values_used_) {
      std::memset(system_constants + sizeof(system_constants_), 0,
                  ShaderOverrides::kHostValuesOffset - sizeof(system_constants_));
      std::memcpy(system_constants + ShaderOverrides::kHostValuesOffset, shader_mod_host_values_,
                  ShaderOverrides::kHostValuesSize);
    }
    if (shader_mod_knobs_source_) {
      std::memcpy(system_constants + ShaderOverrides::kKnobsOffset, shader_mod_knobs_,
                  ShaderOverrides::kKnobsSize);
    }
    cbuffer_binding_system_.up_to_date = true;
    current_graphics_root_up_to_date_ &= ~(1u << root_parameter_system_constants);
  }
  // With d3d12_deferred_constants, the buffers below are allocated and bound
  // there and filled when the deferred command list is replayed; they are up
  // to date after it, so the inline fills are skipped.
  if (deferred_constants_) {
    if (!UpdateConstantBuffersDeferred(
            vertex_shader, pixel_shader, root_parameter_float_constants_vertex,
            root_parameter_float_constants_pixel, root_parameter_bool_loop_constants,
            root_parameter_fetch_constants)) {
      return false;
    }
  } else if (perf_trace::Get()) {
    uint32_t fills = 0, slots = 0;
    const auto count_map = [&](const uint64_t* map) {
      ++fills;
      for (uint32_t i = 0; i < 4; ++i) slots += uint32_t(__builtin_popcountll(map[i]));
    };
    if (!cbuffer_binding_float_vertex_.up_to_date) {
      count_map(float_constant_map_vertex.float_bitmap);
    }
    if (!cbuffer_binding_float_pixel_.up_to_date) {
      static constexpr uint64_t kNoMap[4] = {};
      count_map(pixel_shader ? pixel_shader->constant_register_map().float_bitmap : kNoMap);
    }
    if (!cbuffer_binding_bool_loop_.up_to_date) {
      ++fills;
      slots += deferred_constants::kBoolLoopSlotCount;
    }
    if (!cbuffer_binding_fetch_.up_to_date) {
      ++fills;
      slots += deferred_constants::kFetchSlotCount;
    }
    perf_trace::CountConstantFills(fills, slots, 0);
  }
  if (!cbuffer_binding_float_vertex_.up_to_date) {
    // Even if the shader doesn't need any float constants, a valid binding must
    // still be provided, so if the first draw in the frame with the current
    // root signature doesn't have float constants at all, still allocate an
    // empty buffer.
    uint8_t* float_constants = constant_buffer_pool_->Request(
        frame_current_, sizeof(float) * 4 * std::max(float_constant_count_vertex, uint32_t(1)),
        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, nullptr, nullptr,
        &cbuffer_binding_float_vertex_.address);
    if (float_constants == nullptr) {
      return false;
    }
    for (uint32_t i = 0; i < 4; ++i) {
      uint64_t float_constant_map_entry = float_constant_map_vertex.float_bitmap[i];
      uint32_t float_constant_index;
      while (rex::bit_scan_forward(float_constant_map_entry, &float_constant_index)) {
        float_constant_map_entry &= ~(1ull << float_constant_index);
        std::memcpy(
            float_constants,
            &regs[XE_GPU_REG_SHADER_CONSTANT_000_X + (i << 8) + (float_constant_index << 2)],
            4 * sizeof(float));
        float_constants += 4 * sizeof(float);
      }
    }
    cbuffer_binding_float_vertex_.up_to_date = true;
    current_graphics_root_up_to_date_ &= ~(1u << root_parameter_float_constants_vertex);
  }
  if (!cbuffer_binding_float_pixel_.up_to_date) {
    uint8_t* float_constants = constant_buffer_pool_->Request(
        frame_current_, sizeof(float) * 4 * std::max(float_constant_count_pixel, uint32_t(1)),
        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, nullptr, nullptr,
        &cbuffer_binding_float_pixel_.address);
    if (float_constants == nullptr) {
      return false;
    }
    if (pixel_shader != nullptr) {
      const Shader::ConstantRegisterMap& float_constant_map_pixel =
          pixel_shader->constant_register_map();
      for (uint32_t i = 0; i < 4; ++i) {
        uint64_t float_constant_map_entry = float_constant_map_pixel.float_bitmap[i];
        uint32_t float_constant_index;
        while (rex::bit_scan_forward(float_constant_map_entry, &float_constant_index)) {
          float_constant_map_entry &= ~(1ull << float_constant_index);
          std::memcpy(
              float_constants,
              &regs[XE_GPU_REG_SHADER_CONSTANT_256_X + (i << 8) + (float_constant_index << 2)],
              4 * sizeof(float));
          float_constants += 4 * sizeof(float);
        }
      }
    }
    cbuffer_binding_float_pixel_.up_to_date = true;
    current_graphics_root_up_to_date_ &= ~(1u << root_parameter_float_constants_pixel);
  }
  if (!cbuffer_binding_bool_loop_.up_to_date) {
    constexpr uint32_t kBoolLoopConstantsSize = (8 + 32) * sizeof(uint32_t);
    uint8_t* bool_loop_constants = constant_buffer_pool_->Request(
        frame_current_, kBoolLoopConstantsSize, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT,
        nullptr, nullptr, &cbuffer_binding_bool_loop_.address);
    if (bool_loop_constants == nullptr) {
      return false;
    }
    std::memcpy(bool_loop_constants, &regs[XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031],
                kBoolLoopConstantsSize);
    cbuffer_binding_bool_loop_.up_to_date = true;
    current_graphics_root_up_to_date_ &= ~(1u << root_parameter_bool_loop_constants);
  }
  if (!cbuffer_binding_fetch_.up_to_date) {
    constexpr uint32_t kFetchConstantsSize = 32 * 6 * sizeof(uint32_t);
    uint8_t* fetch_constants = constant_buffer_pool_->Request(
        frame_current_, kFetchConstantsSize, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT,
        nullptr, nullptr, &cbuffer_binding_fetch_.address);
    if (fetch_constants == nullptr) {
      return false;
    }
    std::memcpy(fetch_constants, &regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0], kFetchConstantsSize);
    cbuffer_binding_fetch_.up_to_date = true;
    current_graphics_root_up_to_date_ &= ~(1u << root_parameter_fetch_constants);
  }

  //
  perf_trace::ZoneEnd(perf_trace::Zone::kBindingsConstants, phase_start);
  phase_start = perf_trace::DetailZoneStart();
  // Update descriptors.
  //

  if (!current_shared_memory_binding_is_uav_.has_value() ||
      current_shared_memory_binding_is_uav_.value() != shared_memory_is_uav) {
    current_shared_memory_binding_is_uav_ = shared_memory_is_uav;
    current_graphics_root_up_to_date_ &= ~(1u << root_parameter_shared_memory_and_bindful_edram);
  }

  // Get textures and samplers used by the vertex shader, check if the last used
  // samplers are compatible and update them.
  size_t texture_layout_uid_vertex = vertex_shader->GetTextureBindingLayoutUserUID();
  size_t sampler_layout_uid_vertex = vertex_shader->GetSamplerBindingLayoutUserUID();
  const std::vector<D3D12Shader::TextureBinding>& textures_vertex =
      vertex_shader->GetTextureBindingsAfterTranslation();
  const std::vector<D3D12Shader::SamplerBinding>& samplers_vertex =
      vertex_shader->GetSamplerBindingsAfterTranslation();
  size_t texture_count_vertex = textures_vertex.size();
  size_t sampler_count_vertex = samplers_vertex.size();
  if (sampler_count_vertex) {
    if (current_sampler_layout_uid_vertex_ != sampler_layout_uid_vertex) {
      current_sampler_layout_uid_vertex_ = sampler_layout_uid_vertex;
      cbuffer_binding_descriptor_indices_vertex_.up_to_date = false;
      bindful_samplers_written_vertex_ = false;
    }
    current_samplers_vertex_.resize(
        std::max(current_samplers_vertex_.size(), sampler_count_vertex));
    for (size_t i = 0; i < sampler_count_vertex; ++i) {
      D3D12TextureCache::SamplerParameters parameters =
          texture_cache_->GetSamplerParameters(samplers_vertex[i]);
      if (current_samplers_vertex_[i] != parameters) {
        cbuffer_binding_descriptor_indices_vertex_.up_to_date = false;
        bindful_samplers_written_vertex_ = false;
        current_samplers_vertex_[i] = parameters;
      }
    }
  }

  // Get textures and samplers used by the pixel shader, check if the last used
  // samplers are compatible and update them.
  size_t texture_layout_uid_pixel, sampler_layout_uid_pixel;
  const std::vector<D3D12Shader::TextureBinding>* textures_pixel;
  const std::vector<D3D12Shader::SamplerBinding>* samplers_pixel;
  size_t texture_count_pixel, sampler_count_pixel;
  if (pixel_shader != nullptr) {
    texture_layout_uid_pixel = pixel_shader->GetTextureBindingLayoutUserUID();
    sampler_layout_uid_pixel = pixel_shader->GetSamplerBindingLayoutUserUID();
    textures_pixel = &pixel_shader->GetTextureBindingsAfterTranslation();
    texture_count_pixel = textures_pixel->size();
    samplers_pixel = &pixel_shader->GetSamplerBindingsAfterTranslation();
    sampler_count_pixel = samplers_pixel->size();
    if (sampler_count_pixel) {
      if (current_sampler_layout_uid_pixel_ != sampler_layout_uid_pixel) {
        current_sampler_layout_uid_pixel_ = sampler_layout_uid_pixel;
        cbuffer_binding_descriptor_indices_pixel_.up_to_date = false;
        bindful_samplers_written_pixel_ = false;
      }
      current_samplers_pixel_.resize(
          std::max(current_samplers_pixel_.size(), size_t(sampler_count_pixel)));
      for (uint32_t i = 0; i < sampler_count_pixel; ++i) {
        D3D12TextureCache::SamplerParameters parameters =
            texture_cache_->GetSamplerParameters((*samplers_pixel)[i]);
        if (current_samplers_pixel_[i] != parameters) {
          current_samplers_pixel_[i] = parameters;
          cbuffer_binding_descriptor_indices_pixel_.up_to_date = false;
          bindful_samplers_written_pixel_ = false;
        }
      }
    }
  } else {
    texture_layout_uid_pixel = PipelineCache::kLayoutUIDEmpty;
    sampler_layout_uid_pixel = PipelineCache::kLayoutUIDEmpty;
    textures_pixel = nullptr;
    texture_count_pixel = 0;
    samplers_pixel = nullptr;
    sampler_count_pixel = 0;
  }

  assert_true(sampler_count_vertex + sampler_count_pixel <= kSamplerHeapSize);
  uint32_t descriptor_index_fills = 0;

  if (bindless_resources_used_) {
    //
    // Bindless descriptors path.
    //

    // Check if need to write new descriptor indices.
    // Samplers have already been checked.
    if (texture_count_vertex && cbuffer_binding_descriptor_indices_vertex_.up_to_date &&
        (current_texture_layout_uid_vertex_ != texture_layout_uid_vertex ||
         !texture_cache_->AreActiveTextureSRVKeysUpToDate(current_texture_srv_keys_vertex_.data(),
                                                          textures_vertex.data(),
                                                          texture_count_vertex))) {
      cbuffer_binding_descriptor_indices_vertex_.up_to_date = false;
    }
    if (texture_count_pixel && cbuffer_binding_descriptor_indices_pixel_.up_to_date &&
        (current_texture_layout_uid_pixel_ != texture_layout_uid_pixel ||
         !texture_cache_->AreActiveTextureSRVKeysUpToDate(current_texture_srv_keys_pixel_.data(),
                                                          textures_pixel->data(),
                                                          texture_count_pixel))) {
      cbuffer_binding_descriptor_indices_pixel_.up_to_date = false;
    }

    // Get sampler descriptor indices, write new samplers, and handle sampler
    // heap overflow if it happens.
    if ((sampler_count_vertex && !cbuffer_binding_descriptor_indices_vertex_.up_to_date) ||
        (sampler_count_pixel && !cbuffer_binding_descriptor_indices_pixel_.up_to_date)) {
      for (uint32_t i = 0; i < 2; ++i) {
        if (i) {
          // Overflow happened - invalidate sampler bindings because their
          // descriptor indices can't be used anymore (and even if heap creation
          // fails, because current_sampler_bindless_indices_#_ are in an
          // undefined state now) and switch to a new sampler heap.
          cbuffer_binding_descriptor_indices_vertex_.up_to_date = false;
          cbuffer_binding_descriptor_indices_pixel_.up_to_date = false;
          ID3D12DescriptorHeap* sampler_heap_new;
          if (!sampler_bindless_heaps_overflowed_.empty() &&
              sampler_bindless_heaps_overflowed_.front().second <= submission_completed_) {
            sampler_heap_new = sampler_bindless_heaps_overflowed_.front().first;
            sampler_bindless_heaps_overflowed_.pop_front();
          } else {
            D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_new_desc;
            sampler_heap_new_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
            sampler_heap_new_desc.NumDescriptors = kSamplerHeapSize;
            sampler_heap_new_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            sampler_heap_new_desc.NodeMask = 0;
            if (FAILED(device->CreateDescriptorHeap(&sampler_heap_new_desc,
                                                    IID_PPV_ARGS(&sampler_heap_new)))) {
              REXGPU_ERROR(
                  "Failed to create a new bindless sampler descriptor heap "
                  "after an overflow of the previous one");
              return false;
            }
          }
          // Only change the heap if a new heap was created successfully, not to
          // leave the values in an undefined state in case CreateDescriptorHeap
          // has failed.
          sampler_bindless_heaps_overflowed_.push_back(
              std::make_pair(sampler_bindless_heap_current_, submission_current_));
          sampler_bindless_heap_current_ = sampler_heap_new;
          sampler_bindless_heap_cpu_start_ =
              sampler_bindless_heap_current_->GetCPUDescriptorHandleForHeapStart();
          sampler_bindless_heap_gpu_start_ =
              sampler_bindless_heap_current_->GetGPUDescriptorHandleForHeapStart();
          sampler_bindless_heap_allocated_ = 0;
          // The only thing the heap is used for now is texture cache samplers -
          // invalidate all of them.
          texture_cache_bindless_sampler_map_.clear();
          deferred_command_list_.SetDescriptorHeaps(view_bindless_heap_,
                                                    sampler_bindless_heap_current_);
          current_graphics_root_up_to_date_ &= ~(1u << kRootParameter_Bindless_SamplerHeap);
        }
        bool samplers_overflowed = false;
        if (sampler_count_vertex && !cbuffer_binding_descriptor_indices_vertex_.up_to_date) {
          current_sampler_bindless_indices_vertex_.resize(std::max(
              current_sampler_bindless_indices_vertex_.size(), size_t(sampler_count_vertex)));
          for (uint32_t j = 0; j < sampler_count_vertex; ++j) {
            D3D12TextureCache::SamplerParameters sampler_parameters = current_samplers_vertex_[j];
            uint32_t sampler_index;
            auto it = texture_cache_bindless_sampler_map_.find(sampler_parameters.value);
            if (it != texture_cache_bindless_sampler_map_.end()) {
              sampler_index = it->second;
            } else {
              if (sampler_bindless_heap_allocated_ >= kSamplerHeapSize) {
                samplers_overflowed = true;
                break;
              }
              sampler_index = sampler_bindless_heap_allocated_++;
              texture_cache_->WriteSampler(sampler_parameters,
                                           provider.OffsetSamplerDescriptor(
                                               sampler_bindless_heap_cpu_start_, sampler_index));
              texture_cache_bindless_sampler_map_.emplace(sampler_parameters.value, sampler_index);
            }
            current_sampler_bindless_indices_vertex_[j] = sampler_index;
          }
        }
        if (samplers_overflowed) {
          continue;
        }
        if (sampler_count_pixel && !cbuffer_binding_descriptor_indices_pixel_.up_to_date) {
          current_sampler_bindless_indices_pixel_.resize(std::max(
              current_sampler_bindless_indices_pixel_.size(), size_t(sampler_count_pixel)));
          for (uint32_t j = 0; j < sampler_count_pixel; ++j) {
            D3D12TextureCache::SamplerParameters sampler_parameters = current_samplers_pixel_[j];
            uint32_t sampler_index;
            auto it = texture_cache_bindless_sampler_map_.find(sampler_parameters.value);
            if (it != texture_cache_bindless_sampler_map_.end()) {
              sampler_index = it->second;
            } else {
              if (sampler_bindless_heap_allocated_ >= kSamplerHeapSize) {
                samplers_overflowed = true;
                break;
              }
              sampler_index = sampler_bindless_heap_allocated_++;
              texture_cache_->WriteSampler(sampler_parameters,
                                           provider.OffsetSamplerDescriptor(
                                               sampler_bindless_heap_cpu_start_, sampler_index));
              texture_cache_bindless_sampler_map_.emplace(sampler_parameters.value, sampler_index);
            }
            current_sampler_bindless_indices_pixel_[j] = sampler_index;
          }
        }
        if (!samplers_overflowed) {
          break;
        }
      }
    }

    if (!cbuffer_binding_descriptor_indices_vertex_.up_to_date) {
      uint32_t* descriptor_indices = reinterpret_cast<uint32_t*>(constant_buffer_pool_->Request(
          frame_current_,
          std::max(texture_count_vertex + sampler_count_vertex, size_t(1)) * sizeof(uint32_t),
          D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, nullptr, nullptr,
          &cbuffer_binding_descriptor_indices_vertex_.address));
      if (!descriptor_indices) {
        return false;
      }
      for (size_t i = 0; i < texture_count_vertex; ++i) {
        const D3D12Shader::TextureBinding& texture = textures_vertex[i];
        descriptor_indices[texture.bindless_descriptor_index] =
            texture_cache_->GetActiveTextureBindlessSRVIndex(texture) -
            uint32_t(SystemBindlessView::kUnboundedSRVsStart);
      }
      current_texture_layout_uid_vertex_ = texture_layout_uid_vertex;
      if (texture_count_vertex) {
        current_texture_srv_keys_vertex_.resize(
            std::max(current_texture_srv_keys_vertex_.size(), size_t(texture_count_vertex)));
        texture_cache_->WriteActiveTextureSRVKeys(current_texture_srv_keys_vertex_.data(),
                                                  textures_vertex.data(), texture_count_vertex);
      }
      // Current samplers have already been updated.
      for (size_t i = 0; i < sampler_count_vertex; ++i) {
        descriptor_indices[samplers_vertex[i].bindless_descriptor_index] =
            current_sampler_bindless_indices_vertex_[i];
      }
      cbuffer_binding_descriptor_indices_vertex_.up_to_date = true;
      ++descriptor_index_fills;
      current_graphics_root_up_to_date_ &= ~(1u << kRootParameter_Bindless_DescriptorIndicesVertex);
    }

    if (!cbuffer_binding_descriptor_indices_pixel_.up_to_date) {
      uint32_t* descriptor_indices = reinterpret_cast<uint32_t*>(constant_buffer_pool_->Request(
          frame_current_,
          std::max(texture_count_pixel + sampler_count_pixel, size_t(1)) * sizeof(uint32_t),
          D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, nullptr, nullptr,
          &cbuffer_binding_descriptor_indices_pixel_.address));
      if (!descriptor_indices) {
        return false;
      }
      for (size_t i = 0; i < texture_count_pixel; ++i) {
        const D3D12Shader::TextureBinding& texture = (*textures_pixel)[i];
        descriptor_indices[texture.bindless_descriptor_index] =
            texture_cache_->GetActiveTextureBindlessSRVIndex(texture) -
            uint32_t(SystemBindlessView::kUnboundedSRVsStart);
      }
      current_texture_layout_uid_pixel_ = texture_layout_uid_pixel;
      if (texture_count_pixel) {
        current_texture_srv_keys_pixel_.resize(
            std::max(current_texture_srv_keys_pixel_.size(), size_t(texture_count_pixel)));
        texture_cache_->WriteActiveTextureSRVKeys(current_texture_srv_keys_pixel_.data(),
                                                  textures_pixel->data(), texture_count_pixel);
      }
      // Current samplers have already been updated.
      for (size_t i = 0; i < sampler_count_pixel; ++i) {
        descriptor_indices[(*samplers_pixel)[i].bindless_descriptor_index] =
            current_sampler_bindless_indices_pixel_[i];
      }
      cbuffer_binding_descriptor_indices_pixel_.up_to_date = true;
      ++descriptor_index_fills;
      current_graphics_root_up_to_date_ &= ~(1u << kRootParameter_Bindless_DescriptorIndicesPixel);
    }
  } else {
    //
    // Bindful descriptors path.
    //

    // See what descriptors need to be updated.
    // Samplers have already been checked.
    bool write_textures_vertex =
        texture_count_vertex && (!bindful_textures_written_vertex_ ||
                                 current_texture_layout_uid_vertex_ != texture_layout_uid_vertex ||
                                 !texture_cache_->AreActiveTextureSRVKeysUpToDate(
                                     current_texture_srv_keys_vertex_.data(),
                                     textures_vertex.data(), texture_count_vertex));
    bool write_textures_pixel =
        texture_count_pixel &&
        (!bindful_textures_written_pixel_ ||
         current_texture_layout_uid_pixel_ != texture_layout_uid_pixel ||
         !texture_cache_->AreActiveTextureSRVKeysUpToDate(
             current_texture_srv_keys_pixel_.data(), textures_pixel->data(), texture_count_pixel));
    bool write_samplers_vertex = sampler_count_vertex && !bindful_samplers_written_vertex_;
    bool write_samplers_pixel = sampler_count_pixel && !bindful_samplers_written_pixel_;
    bool edram_rov_used =
        render_target_cache_->GetPath() == RenderTargetCache::Path::kPixelShaderInterlock;

    // Allocate the descriptors.
    size_t view_count_partial_update = 0;
    if (write_textures_vertex) {
      view_count_partial_update += texture_count_vertex;
    }
    if (write_textures_pixel) {
      view_count_partial_update += texture_count_pixel;
    }
    // Shared memory SRV and null UAV + null SRV and shared memory UAV +
    // textures.
    size_t view_count_full_update = 4 + texture_count_vertex + texture_count_pixel;
    if (edram_rov_used) {
      // + EDRAM UAV in two tables (with the shared memory SRV and with the
      // shared memory UAV).
      view_count_full_update += 4;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE view_cpu_handle;
    D3D12_GPU_DESCRIPTOR_HANDLE view_gpu_handle;
    uint32_t descriptor_size_view = provider.GetViewDescriptorSize();
    uint64_t view_heap_index = RequestViewBindfulDescriptors(
        draw_view_bindful_heap_index_, uint32_t(view_count_partial_update),
        uint32_t(view_count_full_update), view_cpu_handle, view_gpu_handle);
    if (view_heap_index == ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid) {
      REXGPU_ERROR("Failed to allocate view descriptors");
      return false;
    }
    size_t sampler_count_partial_update = 0;
    if (write_samplers_vertex) {
      sampler_count_partial_update += sampler_count_vertex;
    }
    if (write_samplers_pixel) {
      sampler_count_partial_update += sampler_count_pixel;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE sampler_cpu_handle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE sampler_gpu_handle = {};
    uint32_t descriptor_size_sampler = provider.GetSamplerDescriptorSize();
    uint64_t sampler_heap_index = ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid;
    if (sampler_count_vertex != 0 || sampler_count_pixel != 0) {
      sampler_heap_index = RequestSamplerBindfulDescriptors(
          draw_sampler_bindful_heap_index_, uint32_t(sampler_count_partial_update),
          uint32_t(sampler_count_vertex + sampler_count_pixel), sampler_cpu_handle,
          sampler_gpu_handle);
      if (sampler_heap_index == ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid) {
        REXGPU_ERROR("Failed to allocate sampler descriptors");
        return false;
      }
    }
    if (draw_view_bindful_heap_index_ != view_heap_index) {
      // Need to update all view descriptors.
      write_textures_vertex = texture_count_vertex != 0;
      write_textures_pixel = texture_count_pixel != 0;
      bindful_textures_written_vertex_ = false;
      bindful_textures_written_pixel_ = false;
      // If updating fully, write the shared memory SRV and UAV descriptors and,
      // if needed, the EDRAM descriptor.
      // SRV + null UAV + EDRAM.
      gpu_handle_shared_memory_srv_and_edram_ = view_gpu_handle;
      shared_memory_->WriteRawSRVDescriptor(view_cpu_handle);
      view_cpu_handle.ptr += descriptor_size_view;
      view_gpu_handle.ptr += descriptor_size_view;
      ui::d3d12::util::CreateBufferRawUAV(device, view_cpu_handle, nullptr, 0);
      view_cpu_handle.ptr += descriptor_size_view;
      view_gpu_handle.ptr += descriptor_size_view;
      if (edram_rov_used) {
        render_target_cache_->WriteEdramUintPow2UAVDescriptor(view_cpu_handle, 2);
        view_cpu_handle.ptr += descriptor_size_view;
        view_gpu_handle.ptr += descriptor_size_view;
        WriteROVOcclusionUAVDescriptor(view_cpu_handle);
        view_cpu_handle.ptr += descriptor_size_view;
        view_gpu_handle.ptr += descriptor_size_view;
      }
      // Null SRV + UAV + EDRAM.
      gpu_handle_shared_memory_uav_and_edram_ = view_gpu_handle;
      ui::d3d12::util::CreateBufferRawSRV(device, view_cpu_handle, nullptr, 0);
      view_cpu_handle.ptr += descriptor_size_view;
      view_gpu_handle.ptr += descriptor_size_view;
      shared_memory_->WriteRawUAVDescriptor(view_cpu_handle);
      view_cpu_handle.ptr += descriptor_size_view;
      view_gpu_handle.ptr += descriptor_size_view;
      if (edram_rov_used) {
        render_target_cache_->WriteEdramUintPow2UAVDescriptor(view_cpu_handle, 2);
        view_cpu_handle.ptr += descriptor_size_view;
        view_gpu_handle.ptr += descriptor_size_view;
        WriteROVOcclusionUAVDescriptor(view_cpu_handle);
        view_cpu_handle.ptr += descriptor_size_view;
        view_gpu_handle.ptr += descriptor_size_view;
      }
      current_graphics_root_up_to_date_ &= ~(1u << kRootParameter_Bindful_SharedMemoryAndEdram);
    }
    if (sampler_heap_index != ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid &&
        draw_sampler_bindful_heap_index_ != sampler_heap_index) {
      write_samplers_vertex = sampler_count_vertex != 0;
      write_samplers_pixel = sampler_count_pixel != 0;
      bindful_samplers_written_vertex_ = false;
      bindful_samplers_written_pixel_ = false;
    }

    // Write the descriptors.
    if (write_textures_vertex) {
      assert_true(current_graphics_root_bindful_extras_.textures_vertex !=
                  RootBindfulExtraParameterIndices::kUnavailable);
      gpu_handle_textures_vertex_ = view_gpu_handle;
      for (size_t i = 0; i < texture_count_vertex; ++i) {
        texture_cache_->WriteActiveTextureBindfulSRV(textures_vertex[i], view_cpu_handle);
        view_cpu_handle.ptr += descriptor_size_view;
        view_gpu_handle.ptr += descriptor_size_view;
      }
      current_texture_layout_uid_vertex_ = texture_layout_uid_vertex;
      current_texture_srv_keys_vertex_.resize(
          std::max(current_texture_srv_keys_vertex_.size(), size_t(texture_count_vertex)));
      texture_cache_->WriteActiveTextureSRVKeys(current_texture_srv_keys_vertex_.data(),
                                                textures_vertex.data(), texture_count_vertex);
      bindful_textures_written_vertex_ = true;
      current_graphics_root_up_to_date_ &=
          ~(1u << current_graphics_root_bindful_extras_.textures_vertex);
    }
    if (write_textures_pixel) {
      assert_true(current_graphics_root_bindful_extras_.textures_pixel !=
                  RootBindfulExtraParameterIndices::kUnavailable);
      gpu_handle_textures_pixel_ = view_gpu_handle;
      for (size_t i = 0; i < texture_count_pixel; ++i) {
        texture_cache_->WriteActiveTextureBindfulSRV((*textures_pixel)[i], view_cpu_handle);
        view_cpu_handle.ptr += descriptor_size_view;
        view_gpu_handle.ptr += descriptor_size_view;
      }
      current_texture_layout_uid_pixel_ = texture_layout_uid_pixel;
      current_texture_srv_keys_pixel_.resize(
          std::max(current_texture_srv_keys_pixel_.size(), size_t(texture_count_pixel)));
      texture_cache_->WriteActiveTextureSRVKeys(current_texture_srv_keys_pixel_.data(),
                                                textures_pixel->data(), texture_count_pixel);
      bindful_textures_written_pixel_ = true;
      current_graphics_root_up_to_date_ &=
          ~(1u << current_graphics_root_bindful_extras_.textures_pixel);
    }
    if (write_samplers_vertex) {
      assert_true(current_graphics_root_bindful_extras_.samplers_vertex !=
                  RootBindfulExtraParameterIndices::kUnavailable);
      gpu_handle_samplers_vertex_ = sampler_gpu_handle;
      for (size_t i = 0; i < sampler_count_vertex; ++i) {
        texture_cache_->WriteSampler(current_samplers_vertex_[i], sampler_cpu_handle);
        sampler_cpu_handle.ptr += descriptor_size_sampler;
        sampler_gpu_handle.ptr += descriptor_size_sampler;
      }
      // Current samplers have already been updated.
      bindful_samplers_written_vertex_ = true;
      current_graphics_root_up_to_date_ &=
          ~(1u << current_graphics_root_bindful_extras_.samplers_vertex);
    }
    if (write_samplers_pixel) {
      assert_true(current_graphics_root_bindful_extras_.samplers_pixel !=
                  RootBindfulExtraParameterIndices::kUnavailable);
      gpu_handle_samplers_pixel_ = sampler_gpu_handle;
      for (size_t i = 0; i < sampler_count_pixel; ++i) {
        texture_cache_->WriteSampler(current_samplers_pixel_[i], sampler_cpu_handle);
        sampler_cpu_handle.ptr += descriptor_size_sampler;
        sampler_gpu_handle.ptr += descriptor_size_sampler;
      }
      // Current samplers have already been updated.
      bindful_samplers_written_pixel_ = true;
      current_graphics_root_up_to_date_ &=
          ~(1u << current_graphics_root_bindful_extras_.samplers_pixel);
    }

    // Wrote new descriptors on the current page.
    draw_view_bindful_heap_index_ = view_heap_index;
    if (sampler_heap_index != ui::d3d12::D3D12DescriptorHeapPool::kHeapIndexInvalid) {
      draw_sampler_bindful_heap_index_ = sampler_heap_index;
    }
  }

  perf_trace::ZoneEnd(perf_trace::Zone::kBindingsDescriptors, phase_start);
  perf_trace::CountDrawDescriptors(uint32_t(sampler_count_vertex + sampler_count_pixel),
                                   descriptor_index_fills);
  const perf_trace::DetailZoneScope root_parameters_zone(
      perf_trace::Zone::kBindingsRootParameters);
  // Update the root parameters.
  if (!(current_graphics_root_up_to_date_ & (1u << root_parameter_fetch_constants))) {
    deferred_command_list_.D3DSetGraphicsRootConstantBufferView(root_parameter_fetch_constants,
                                                                cbuffer_binding_fetch_.address);
    current_graphics_root_up_to_date_ |= 1u << root_parameter_fetch_constants;
  }
  if (!(current_graphics_root_up_to_date_ & (1u << root_parameter_float_constants_vertex))) {
    deferred_command_list_.D3DSetGraphicsRootConstantBufferView(
        root_parameter_float_constants_vertex, cbuffer_binding_float_vertex_.address);
    current_graphics_root_up_to_date_ |= 1u << root_parameter_float_constants_vertex;
  }
  if (!(current_graphics_root_up_to_date_ & (1u << root_parameter_float_constants_pixel))) {
    deferred_command_list_.D3DSetGraphicsRootConstantBufferView(
        root_parameter_float_constants_pixel, cbuffer_binding_float_pixel_.address);
    current_graphics_root_up_to_date_ |= 1u << root_parameter_float_constants_pixel;
  }
  if (!(current_graphics_root_up_to_date_ & (1u << root_parameter_system_constants))) {
    deferred_command_list_.D3DSetGraphicsRootConstantBufferView(root_parameter_system_constants,
                                                                cbuffer_binding_system_.address);
    current_graphics_root_up_to_date_ |= 1u << root_parameter_system_constants;
  }
  if (!(current_graphics_root_up_to_date_ & (1u << root_parameter_bool_loop_constants))) {
    deferred_command_list_.D3DSetGraphicsRootConstantBufferView(root_parameter_bool_loop_constants,
                                                                cbuffer_binding_bool_loop_.address);
    current_graphics_root_up_to_date_ |= 1u << root_parameter_bool_loop_constants;
  }
  if (!(current_graphics_root_up_to_date_ &
        (1u << root_parameter_shared_memory_and_bindful_edram))) {
    assert_true(current_shared_memory_binding_is_uav_.has_value());
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_shared_memory_and_bindful_edram;
    if (bindless_resources_used_) {
      gpu_handle_shared_memory_and_bindful_edram = provider.OffsetViewDescriptor(
          view_bindless_heap_gpu_start_,
          uint32_t(current_shared_memory_binding_is_uav_.value()
                       ? SystemBindlessView ::kNullRawSRVAndSharedMemoryRawUAVStart
                       : SystemBindlessView ::kSharedMemoryRawSRVAndNullRawUAVStart));
    } else {
      gpu_handle_shared_memory_and_bindful_edram = current_shared_memory_binding_is_uav_.value()
                                                       ? gpu_handle_shared_memory_uav_and_edram_
                                                       : gpu_handle_shared_memory_srv_and_edram_;
    }
    deferred_command_list_.D3DSetGraphicsRootDescriptorTable(
        root_parameter_shared_memory_and_bindful_edram, gpu_handle_shared_memory_and_bindful_edram);
    current_graphics_root_up_to_date_ |= 1u << root_parameter_shared_memory_and_bindful_edram;
  }
  if (bindless_resources_used_) {
    if (!(current_graphics_root_up_to_date_ &
          (1u << kRootParameter_Bindless_DescriptorIndicesPixel))) {
      deferred_command_list_.D3DSetGraphicsRootConstantBufferView(
          kRootParameter_Bindless_DescriptorIndicesPixel,
          cbuffer_binding_descriptor_indices_pixel_.address);
      current_graphics_root_up_to_date_ |= 1u << kRootParameter_Bindless_DescriptorIndicesPixel;
    }
    if (!(current_graphics_root_up_to_date_ &
          (1u << kRootParameter_Bindless_DescriptorIndicesVertex))) {
      deferred_command_list_.D3DSetGraphicsRootConstantBufferView(
          kRootParameter_Bindless_DescriptorIndicesVertex,
          cbuffer_binding_descriptor_indices_vertex_.address);
      current_graphics_root_up_to_date_ |= 1u << kRootParameter_Bindless_DescriptorIndicesVertex;
    }
    if (!(current_graphics_root_up_to_date_ & (1u << kRootParameter_Bindless_SamplerHeap))) {
      deferred_command_list_.D3DSetGraphicsRootDescriptorTable(kRootParameter_Bindless_SamplerHeap,
                                                               sampler_bindless_heap_gpu_start_);
      current_graphics_root_up_to_date_ |= 1u << kRootParameter_Bindless_SamplerHeap;
    }
    if (!(current_graphics_root_up_to_date_ & (1u << kRootParameter_Bindless_ViewHeap))) {
      deferred_command_list_.D3DSetGraphicsRootDescriptorTable(kRootParameter_Bindless_ViewHeap,
                                                               view_bindless_heap_gpu_start_);
      current_graphics_root_up_to_date_ |= 1u << kRootParameter_Bindless_ViewHeap;
    }
  } else {
    uint32_t extra_index;
    extra_index = current_graphics_root_bindful_extras_.textures_pixel;
    if (extra_index != RootBindfulExtraParameterIndices::kUnavailable &&
        !(current_graphics_root_up_to_date_ & (1u << extra_index))) {
      deferred_command_list_.D3DSetGraphicsRootDescriptorTable(extra_index,
                                                               gpu_handle_textures_pixel_);
      current_graphics_root_up_to_date_ |= 1u << extra_index;
    }
    extra_index = current_graphics_root_bindful_extras_.samplers_pixel;
    if (extra_index != RootBindfulExtraParameterIndices::kUnavailable &&
        !(current_graphics_root_up_to_date_ & (1u << extra_index))) {
      deferred_command_list_.D3DSetGraphicsRootDescriptorTable(extra_index,
                                                               gpu_handle_samplers_pixel_);
      current_graphics_root_up_to_date_ |= 1u << extra_index;
    }
    extra_index = current_graphics_root_bindful_extras_.textures_vertex;
    if (extra_index != RootBindfulExtraParameterIndices::kUnavailable &&
        !(current_graphics_root_up_to_date_ & (1u << extra_index))) {
      deferred_command_list_.D3DSetGraphicsRootDescriptorTable(extra_index,
                                                               gpu_handle_textures_vertex_);
      current_graphics_root_up_to_date_ |= 1u << extra_index;
    }
    extra_index = current_graphics_root_bindful_extras_.samplers_vertex;
    if (extra_index != RootBindfulExtraParameterIndices::kUnavailable &&
        !(current_graphics_root_up_to_date_ & (1u << extra_index))) {
      deferred_command_list_.D3DSetGraphicsRootDescriptorTable(extra_index,
                                                               gpu_handle_samplers_vertex_);
      current_graphics_root_up_to_date_ |= 1u << extra_index;
    }
  }

  return true;
}

void D3D12CommandProcessor::EvictOldReadbackBuffers(
    std::unordered_map<uint64_t, ReadbackBuffer>& buffer_map) {
  if (buffer_map.empty()) {
    return;
  }
  const uint64_t eviction_frame_floor = (frame_current_ > kReadbackBufferEvictionAgeFrames)
                                            ? (frame_current_ - kReadbackBufferEvictionAgeFrames)
                                            : 0;
  for (auto it = buffer_map.begin(); it != buffer_map.end();) {
    ReadbackBuffer& readback = it->second;
    bool evict =
        buffer_map.size() > kMaxReadbackBuffers || readback.last_used_frame < eviction_frame_floor;
    if (!evict) {
      ++it;
      continue;
    }
    for (uint32_t i = 0; i < kReadbackSlots; ++i) {
      AwaitReadbackPublish(readback.publish_sequence[i]);
    }
    for (uint32_t i = 0; i < kReadbackSlots; ++i) {
      if (readback.buffers[i]) {
        if (readback.mapped_data[i]) {
          readback.buffers[i]->Unmap(0, nullptr);
        }
        readback.buffers[i]->Release();
      }
      readback.buffers[i] = nullptr;
      readback.mapped_data[i] = nullptr;
      readback.sizes[i] = 0;
      readback.submission_written[i] = 0;
      readback.written_size[i] = 0;
    }
    it = buffer_map.erase(it);
  }
}

ID3D12Resource* D3D12CommandProcessor::RequestReadbackBuffer(uint32_t size) {
  if (size == 0) {
    return nullptr;
  }
  size = rex::align(size, kReadbackBufferSizeIncrement);
  if (size > readback_buffer_size_) {
    const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
    ID3D12Device* device = provider.GetDevice();
    D3D12_RESOURCE_DESC buffer_desc;
    ui::d3d12::util::FillBufferResourceDesc(buffer_desc, size, D3D12_RESOURCE_FLAG_NONE);
    ID3D12Resource* buffer;
    if (FAILED(device->CreateCommittedResource(
            &ui::d3d12::util::kHeapPropertiesReadback, provider.GetHeapFlagCreateNotZeroed(),
            &buffer_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&buffer)))) {
      REXGPU_ERROR("Failed to create a {} MB readback buffer", size >> 20);
      return nullptr;
    }
    if (readback_buffer_ != nullptr) {
      readback_buffer_->Release();
    }
    readback_buffer_ = buffer;
    readback_buffer_size_ = size;
  }
  return readback_buffer_;
}

bool D3D12CommandProcessor::InitializeOcclusionQueryResources() {
  active_occlusion_query_ = {};
  rtv_occlusion_open_query_ = UINT32_MAX;
  rtv_occlusion_segment_count_ = 0;
  rtv_occlusion_segment_msaa_ = 0;
  occlusion_query_cursor_ = 0;
  occlusion_query_resources_available_ = false;
  occlusion_query_heap_.Reset();
  occlusion_query_readback_.Reset();
  occlusion_query_readback_mapping_ = nullptr;

  if (render_target_cache_->GetPath() == RenderTargetCache::Path::kPixelShaderInterlock) {
    return InitializeROVOcclusionResources();
  }

  ID3D12Device* device = GetD3D12Provider().GetDevice();
  if (!device) {
    return false;
  }

  D3D12_QUERY_HEAP_DESC heap_desc;
  heap_desc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
  heap_desc.Count = kMaxOcclusionQueries;
  heap_desc.NodeMask = 0;
  if (FAILED(device->CreateQueryHeap(&heap_desc, IID_PPV_ARGS(&occlusion_query_heap_)))) {
    REXGPU_WARN(
        "D3D12CommandProcessor: Failed to create occlusion query heap, using fake sample counts");
    return false;
  }

  D3D12_RESOURCE_DESC buffer_desc;
  ui::d3d12::util::FillBufferResourceDesc(buffer_desc, sizeof(uint64_t) * kMaxOcclusionQueries,
                                          D3D12_RESOURCE_FLAG_NONE);
  if (FAILED(device->CreateCommittedResource(&ui::d3d12::util::kHeapPropertiesReadback,
                                             GetD3D12Provider().GetHeapFlagCreateNotZeroed(),
                                             &buffer_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&occlusion_query_readback_)))) {
    REXGPU_WARN(
        "D3D12CommandProcessor: Failed to allocate occlusion query readback buffer, using fake "
        "sample counts");
    occlusion_query_heap_.Reset();
    return false;
  }

  D3D12_RANGE read_range = {0, sizeof(uint64_t) * kMaxOcclusionQueries};
  void* mapping = nullptr;
  if (FAILED(occlusion_query_readback_->Map(0, &read_range, &mapping))) {
    REXGPU_WARN(
        "D3D12CommandProcessor: Failed to map occlusion query readback buffer, using fake sample "
        "counts");
    occlusion_query_readback_.Reset();
    occlusion_query_heap_.Reset();
    return false;
  }

  occlusion_query_readback_mapping_ = reinterpret_cast<uint64_t*>(mapping);
  occlusion_query_resources_available_ = true;
  return true;
}

void D3D12CommandProcessor::ShutdownOcclusionQueryResources() {
  DisableHostOcclusionQueries();

  if (occlusion_query_readback_ && occlusion_query_readback_mapping_) {
    occlusion_query_readback_->Unmap(0, nullptr);
  }
  occlusion_query_readback_mapping_ = nullptr;
  occlusion_query_readback_.Reset();
  occlusion_query_heap_.Reset();
  rov_occlusion_counter_.Reset();
  rov_occlusion_zero_upload_.Reset();
  rov_occlusion_initialized_ = false;
  rov_occlusion_previous_.fill(0);
  rov_occlusion_cumulative_samples_.fill(0);
  rov_occlusion_pending_.clear();
  rov_occlusion_last_segment_.clear();
}

bool D3D12CommandProcessor::InitializeROVOcclusionResources() {
  auto* device = GetD3D12Provider().GetDevice();
  constexpr uint32_t bytes = kROVOcclusionWords * sizeof(uint32_t);
  D3D12_RESOURCE_DESC desc;
  ui::d3d12::util::FillBufferResourceDesc(desc, bytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  if (FAILED(device->CreateCommittedResource(&ui::d3d12::util::kHeapPropertiesDefault,
      D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
      IID_PPV_ARGS(&rov_occlusion_counter_)))) return false;
  desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  if (FAILED(device->CreateCommittedResource(&ui::d3d12::util::kHeapPropertiesUpload,
      D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(&rov_occlusion_zero_upload_)))) return false;
  void* mapping = nullptr;
  D3D12_RANGE no_read{0, 0};
  if (FAILED(rov_occlusion_zero_upload_->Map(0, &no_read, &mapping))) return false;
  std::memset(mapping, 0, bytes);
  rov_occlusion_zero_upload_->Unmap(0, nullptr);
  // One slot per snapshot in flight (rov_occlusion_async).
  ui::d3d12::util::FillBufferResourceDesc(desc, bytes * kROVOcclusionSlots, D3D12_RESOURCE_FLAG_NONE);
  if (FAILED(device->CreateCommittedResource(&ui::d3d12::util::kHeapPropertiesReadback,
      D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
      IID_PPV_ARGS(&occlusion_query_readback_)))) return false;
  D3D12_RANGE read_range{0, bytes * kROVOcclusionSlots};
  if (FAILED(occlusion_query_readback_->Map(0, &read_range, &mapping))) return false;
  occlusion_query_readback_mapping_ = static_cast<uint64_t*>(mapping);
  rov_occlusion_initialized_ = false;
  rov_occlusion_previous_.fill(0);
  rov_occlusion_cumulative_samples_.fill(0);
  rov_occlusion_pending_.clear();
  rov_occlusion_last_segment_.clear();
  rov_occlusion_next_slot_ = 0;
  rov_occlusion_frame_interval_ = 0;
  return true;
}

void D3D12CommandProcessor::WriteROVOcclusionUAVDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle) const {
  ui::d3d12::util::CreateBufferRawUAV(GetD3D12Provider().GetDevice(), handle,
      rov_occlusion_counter_.Get(), rov_occlusion_counter_ ? kROVOcclusionWords * sizeof(uint32_t) : 0);
}

bool D3D12CommandProcessor::WriteROVOcclusionSnapshot(xenos::xe_gpu_depth_sample_counts* counts) {
  perf_trace::CauseScope perf_cause(perf_trace::Wait::kOcclusion);
  if (!rov_occlusion_counter_ || !occlusion_query_readback_mapping_ || !BeginSubmission(true)) return false;
  const uint32_t kQueryPending = rex::byte_swap(0xFFFFFEEDu);
  const bool awaited = counts->ZPass_A == kQueryPending || counts->ZFail_A == kQueryPending;
  perf_trace::Increment(awaited ? perf_trace::Count::kOcclusionAwaited
                                : perf_trace::Count::kOcclusionUnawaited);
  constexpr uint32_t bytes = kROVOcclusionWords * sizeof(uint32_t);
  if (!rov_occlusion_initialized_) {
    deferred_command_list_.D3DCopyBufferRegion(rov_occlusion_counter_.Get(), 0,
        rov_occlusion_zero_upload_.Get(), 0, bytes);
    PushTransitionBarrier(rov_occlusion_counter_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    SubmitBarriers();
    rov_occlusion_initialized_ = true;
    PublishROVOcclusionReport(counts);
    return true;
  }

  if (rov_occlusion_pending_.size() >= kROVOcclusionSlots) {
    // Ring full: the oldest copy must complete before its slot is reused.
    CheckSubmissionFence(rov_occlusion_pending_.front().submission);
    RetireROVOcclusionSnapshots();
    if (rov_occlusion_pending_.size() >= kROVOcclusionSlots) return false;
  }
  const uint32_t slot = rov_occlusion_next_slot_;
  rov_occlusion_next_slot_ = (slot + 1) % kROVOcclusionSlots;
  PushTransitionBarrier(rov_occlusion_counter_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  SubmitBarriers();
  deferred_command_list_.D3DCopyBufferRegion(occlusion_query_readback_.Get(), uint64_t(slot) * bytes,
      rov_occlusion_counter_.Get(), 0, bytes);
  PushTransitionBarrier(rov_occlusion_counter_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  SubmitBarriers();
  ROVOcclusionPending pending = {};
  pending.submission = submission_current_;
  pending.slot = slot;
  return QueueOcclusionSnapshot(pending, counts, awaited);
}

bool D3D12CommandProcessor::QueueOcclusionSnapshot(ROVOcclusionPending pending,
                                                   xenos::xe_gpu_depth_sample_counts* counts,
                                                   bool awaited) {
  const std::string mode(REXCVAR_GET(rov_occlusion_async));
  const bool estimate = mode == "previous";
  const bool wait = mode == "off" || (mode == "unawaited" && awaited);
  pending.guest_address = register_file_->values[XE_GPU_REG_RB_SAMPLE_COUNT_ADDR];
  pending.interval = rov_occlusion_frame_interval_++;
  for (uint32_t i = 0; i < 3; ++i) {
    pending.draws[i] = perf_trace::segment_draws[i];
    perf_trace::segment_draws[i] = 0;
  }
  pending.awaited = awaited;
  pending.published = estimate;
  if (estimate) {
    // Publish now as if this interval counted what it counted last frame; the
    // measured samples become the estimate for the next frame when retired.
    const auto last = rov_occlusion_last_segment_.find(pending.interval << 1 | uint32_t(awaited));
    if (last != rov_occlusion_last_segment_.end()) {
      for (uint32_t counter = 0; counter < kROVOcclusionCounters; ++counter) {
        rov_occlusion_cumulative_samples_[counter] += last->second[counter];
      }
    }
    PublishROVOcclusionReport(counts);
  }
  rov_occlusion_pending_.push_back(pending);
  if (wait) {
    if (!AwaitAllQueueOperationsCompletion()) return false;
  } else {
    CheckSubmissionFence(0);
  }
  RetireROVOcclusionSnapshots();
  return true;
}

void D3D12CommandProcessor::RetireROVOcclusionSnapshots() {
  constexpr uint32_t bytes = kROVOcclusionWords * sizeof(uint32_t);
  const uint64_t divisor = uint64_t(REXCVAR_GET(rov_occlusion_awaited_report_divisor));
  while (!rov_occlusion_pending_.empty() &&
         rov_occlusion_pending_.front().submission <= submission_completed_) {
    const ROVOcclusionPending pending = rov_occlusion_pending_.front();
    rov_occlusion_pending_.pop_front();
    std::array<uint64_t, kROVOcclusionCounters> segment{};
    if (pending.host_queries) {
      // Host queries only count passing samples.
      using Counter = DxbcShaderTranslator::ROVOcclusionCounter;
      for (uint32_t i = 0; i < pending.query_count; ++i) {
        segment[uint32_t(Counter::kZPass)] +=
            occlusion_query_readback_mapping_[(pending.first_query + i) % kMaxOcclusionQueries];
      }
    } else {
      const auto* values = reinterpret_cast<const uint32_t*>(
          reinterpret_cast<const uint8_t*>(occlusion_query_readback_mapping_) +
          size_t(pending.slot) * bytes);
      for (uint32_t i = 0; i < kROVOcclusionWords; ++i) {
        // Unsigned delta also handles an individual GPU bucket wrapping.
        segment[i / kROVOcclusionBuckets] += uint32_t(values[i] - rov_occlusion_previous_[i]);
        rov_occlusion_previous_[i] = values[i];
      }
    }
    // D3D pre-fills an END report with 0xFFFFFEED and the guest spins on it,
    // so this segment is the bracketed query interval rather than one of the
    // free-running intervals between QueryBatch-style snapshots. FM4 measures
    // its sun visibility this way. The counter counts guest samples, so the
    // sun sprite reports 1x, 2x and 4x its pixel area at 1x, 2x and 4x MSAA;
    // a divisor can scale only these segments for experiments, while the
    // frame-wide reports (auto exposure) stay as they are.
    if (pending.awaited && divisor > 1 && !pending.host_queries) {
      for (uint64_t& samples : segment) samples = (samples + (divisor >> 1)) / divisor;
    }
    if (pending.awaited && pending.host_queries && REXCVAR_GET(rtv_occlusion_debug) == "fake") {
      segment[uint32_t(DxbcShaderTranslator::ROVOcclusionCounter::kZPass)] =
          uint64_t(std::max(REXCVAR_GET(query_occlusion_fake_sample_count), 0));
    }
    const double awaited_scale = REXCVAR_GET(rtv_occlusion_awaited_scale);
    if (pending.awaited && pending.host_queries && awaited_scale >= 0.0) {
      // Host queries count every sample, so the same sun reports 1x, 2x and
      // 4x its pixels at 1x, 2x and 4x MSAA. Count guest pixels instead (the
      // resolution scale is removed when the report is published), scale them
      // and pass them through the soft limit.
      const double resolution_scale = OcclusionResolutionScale();
      const double limit = REXCVAR_GET(rtv_occlusion_awaited_limit);
      for (uint64_t& samples : segment) {
        double pixels = double(samples) / (std::max(pending.host_samples_per_pixel, 1u) *
                                           resolution_scale) * awaited_scale;
        const double knee = limit / 3.0;
        if (limit > 0.0 && pixels > knee) {
          // Linear up to the knee, then approaching the limit.
          const double over = pixels - knee;
          pixels = knee + over / (1.0 + over / (limit - knee));
        }
        samples = uint64_t(pixels * resolution_scale + 0.5);
      }
    }
    TraceOcclusionSegment(pending.interval, pending.awaited, pending.host_queries,
                          pending.guest_address, segment.data(), kROVOcclusionCounters,
                          pending.draws);
    perf_trace::AddOcclusionInterval(
        pending.awaited,
        NormalizeOcclusionSamples(
            segment[uint32_t(DxbcShaderTranslator::ROVOcclusionCounter::kZPass)]));
    if (pending.published) {
      rov_occlusion_last_segment_[pending.interval << 1 | uint32_t(pending.awaited)] = segment;
      continue;
    }
    for (uint32_t counter = 0; counter < kROVOcclusionCounters; ++counter) {
      rov_occlusion_cumulative_samples_[counter] += segment[counter];
    }
    // A newer snapshot to the same address supersedes this one: writing it
    // would complete a query the guest has already re-armed with stale data.
    const bool superseded = std::any_of(
        rov_occlusion_pending_.begin(), rov_occlusion_pending_.end(),
        [&](const ROVOcclusionPending& other) { return other.guest_address == pending.guest_address; });
    auto* counts =
        memory_->TranslatePhysical<xenos::xe_gpu_depth_sample_counts*>(pending.guest_address);
    if (counts && !superseded) {
      PublishROVOcclusionReport(counts);
    }
  }
}

void D3D12CommandProcessor::PublishROVOcclusionReport(
    xenos::xe_gpu_depth_sample_counts* counts) const {
  // EVENT_WRITE_ZPD is a cumulative snapshot, not a same-address begin/end
  // command. The guest subtracts two reports, including QueryBatch reports.
  using Counter = DxbcShaderTranslator::ROVOcclusionCounter;
  const auto normalized = [&](Counter counter) {
    return NormalizeOcclusionSamples(rov_occlusion_cumulative_samples_[uint32_t(counter)]);
  };
  const uint64_t passed = normalized(Counter::kZPass);
  const uint64_t failed_depth = normalized(Counter::kZFail);
  const uint64_t failed_stencil = normalized(Counter::kStencilFail);
  const auto lane_b = [](uint64_t value) { return uint32_t(value) >> 1; };
  const auto lane_a = [&](uint64_t value) { return uint32_t(value) - lane_b(value); };
  const uint64_t total = passed + failed_depth + failed_stencil;
  counts->Total_A = lane_a(total);
  counts->Total_B = lane_b(total);
  counts->ZFail_A = lane_a(failed_depth);
  counts->ZFail_B = lane_b(failed_depth);
  // Guest polling may wait on ZPass or StencilFail. Publish these completion
  // lanes together after the other fields, as in the Xenos report ABI.
  xenos::xe_gpu_depth_sample_counts completed{};
  completed.ZPass_A = lane_a(passed);
  completed.ZPass_B = lane_b(passed);
  completed.StencilFail_A = lane_a(failed_stencil);
  completed.StencilFail_B = lane_b(failed_stencil);
  std::memcpy(&counts->ZPass_A, &completed.ZPass_A,
              sizeof(completed) - offsetof(xenos::xe_gpu_depth_sample_counts, ZPass_A));
}

bool D3D12CommandProcessor::FlushROVOcclusionSnapshots() {
  if (rov_occlusion_pending_.empty()) return false;
  // The guest may be spinning on a report whose copy is still in the open
  // submission: submit it, then poll without blocking.
  if (submission_open_ && rov_occlusion_pending_.back().submission >= submission_current_ &&
      CanEndSubmissionImmediately()) {
    EndSubmission(false);
  }
  CheckSubmissionFence(0);
  RetireROVOcclusionSnapshots();
  return !rov_occlusion_pending_.empty();
}

void D3D12CommandProcessor::PrepareForWait() { FlushROVOcclusionSnapshots(); }

bool D3D12CommandProcessor::PollIdleWork() { return FlushROVOcclusionSnapshots(); }

void D3D12CommandProcessor::ServiceHostRequests() { ServiceDemandReadbacks(); }

void D3D12CommandProcessor::ServiceDemandReadbacks() {
  std::vector<read_trace::Demand> demands = read_trace::TakeRequests();
  if (demands.empty()) {
    return;
  }
  const auto start = std::chrono::steady_clock::now();
  std::vector<uint64_t> offsets(demands.size());
  uint64_t total = 0;
  for (size_t i = 0; i < demands.size(); ++i) {
    offsets[i] = total;
    total += AlignReadbackBufferSize(demands[i].length);
  }
  bool copied = true;
  if (total > demand_readback_buffer_size_) {
    if (demand_readback_buffer_) {
      // Not in use: every readback on demand awaits its copy.
      demand_readback_buffer_->Unmap(0, nullptr);
      demand_readback_mapping_ = nullptr;
      ui::d3d12::util::ReleaseAndNull(demand_readback_buffer_);
      demand_readback_buffer_size_ = 0;
    }
    const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
    D3D12_RESOURCE_DESC buffer_desc;
    ui::d3d12::util::FillBufferResourceDesc(buffer_desc, total, D3D12_RESOURCE_FLAG_NONE);
    D3D12_RANGE read_range = {0, SIZE_T(total)};
    if (FAILED(provider.GetDevice()->CreateCommittedResource(
            &ui::d3d12::util::kHeapPropertiesReadback, provider.GetHeapFlagCreateNotZeroed(),
            &buffer_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&demand_readback_buffer_))) ||
        FAILED(demand_readback_buffer_->Map(0, &read_range, &demand_readback_mapping_))) {
      REXGPU_ERROR("Failed to create a {} MB buffer for readbacks on demand", total >> 20);
      ui::d3d12::util::ReleaseAndNull(demand_readback_buffer_);
      demand_readback_mapping_ = nullptr;
      copied = false;
    } else {
      demand_readback_buffer_size_ = total;
    }
  }
  std::vector<bool> recorded(demands.size(), false);
  // Served in idle loops too, where no submission is open.
  if (copied && !BeginSubmission(false)) {
    copied = false;
  }
  if (copied) {
    const bool is_scaled = texture_cache_->IsDrawResolutionScaled();
    for (size_t i = 0; i < demands.size(); ++i) {
      const read_trace::Demand& demand = demands[i];
      // The scaled resolve range that the resolve made current, read as the
      // readback at the resolve reads it.
      if (is_scaled && !texture_cache_->MakeScaledResolveRangeCurrent(demand.scaled_start,
                                                                      demand.scaled_length)) {
        continue;
      }
      recorded[i] = RecordReadbackCopy(demand.address, demand.length, demand.pixel_size_log2,
                                       demand_readback_buffer_, offsets[i]);
    }
    copied = AwaitAllQueueOperationsCompletion();
  }
  if (copied) {
    const auto* data = static_cast<const uint8_t*>(demand_readback_mapping_);
    for (size_t i = 0; i < demands.size(); ++i) {
      if (!recorded[i]) {
        copied = false;
        continue;
      }
      const uint32_t skipped = shared_memory_->CopyReadbackToGuest(
          demands[i].address, demands[i].length, data + offsets[i]);
      TraceReadbackSkip(demands[i].address, demands[i].length, skipped);
      perf_trace::CountReadback(demands[i].length);
    }
  }
  read_trace::CompleteRequests(
      *memory_, demands, copied,
      uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now() - start)
                   .count()));
}

void D3D12CommandProcessor::SubmitThreadMain() {
  // Named for the e2e harness's CPU time per thread (cpu_power.py).
  rex::thread::set_current_thread_name("GPU Submit");
  ID3D12CommandQueue* direct_queue = GetD3D12Provider().GetDirectQueue();
  std::unique_lock<std::mutex> lock(submit_mutex_);
  while (true) {
    submit_request_cond_.wait(lock, [this]() { return submit_shutdown_ || !submit_jobs_.empty(); });
    if (submit_jobs_.empty()) {
      return;
    }
    SubmitJob job = std::move(submit_jobs_.front());
    submit_jobs_.pop_front();
    submit_busy_ = true;
    lock.unlock();
    if (!job.list) {
      const uint64_t perf_start = perf_trace::ZoneStart();
      job.callback();
      job.callback = nullptr;
      if (perf_start) {
        perf_trace::AddSubmitThreadCallback(perf_start, perf_trace::Now());
      }
      lock.lock();
      submit_busy_ = false;
      if (submit_jobs_.empty()) {
        submit_done_cond_.notify_all();
      }
      continue;
    }
    // The allocator is only reused after this submission's fence value, which
    // is signaled below, so it is free to reset here.
    const uint64_t perf_start = perf_trace::ZoneStart();
    if (draw_back_end_ == 2) {
      // The replay creates the pipelines: compiled look mods swap in here.
      pipeline_cache_->SwapCompiledMods(job.submission);
    }
    job.allocator->Reset();
    command_list_->Reset(job.allocator, nullptr);
    job.list->Execute(command_list_, command_list_1_);
    const uint64_t perf_recorded = perf_start ? perf_trace::Now() : 0;
    command_list_->Close();
    ID3D12CommandList* execute_command_lists[] = {command_list_};
    direct_queue->ExecuteCommandLists(1, execute_command_lists);
    direct_queue->Signal(submission_fence_, job.submission);
    const uint64_t perf_executed = perf_start ? perf_trace::Now() : 0;
    job.list->Reset();
    if (perf_start) {
      perf_trace::AddSubmitThreadJob(perf_start, perf_recorded, perf_executed, perf_trace::Now());
    }
    lock.lock();
    submit_free_lists_.push_back(std::move(job.list));
    submit_busy_ = false;
    if (submit_jobs_.empty()) {
      submit_done_cond_.notify_all();
    }
  }
}

void D3D12CommandProcessor::EnqueueSubmitCallback(std::function<void()> callback) {
  SubmitJob job;
  job.callback = std::move(callback);
  {
    std::lock_guard<std::mutex> lock(submit_mutex_);
    submit_jobs_.push_back(std::move(job));
  }
  submit_request_cond_.notify_one();
}

void D3D12CommandProcessor::FlushSubmitThread() {
  if (!submit_thread_.joinable()) return;
  const uint64_t perf_start = perf_trace::ZoneStart();
  std::unique_lock<std::mutex> lock(submit_mutex_);
  const uint32_t pending_jobs = uint32_t(submit_jobs_.size()) + (submit_busy_ ? 1 : 0);
  submit_done_cond_.wait(lock, [this]() { return submit_jobs_.empty() && !submit_busy_; });
  if (perf_start) perf_trace::AddSubmitFlush(perf_start, pending_jobs);
}

void D3D12CommandProcessor::ReadbackPublishThreadMain() {
  rex::thread::set_current_thread_name("GPU Readback Publish");
  std::unique_lock<std::mutex> lock(readback_publish_mutex_);
  while (true) {
    readback_publish_request_cond_.wait(
        lock, [this]() { return readback_publish_shutdown_ || !readback_publish_jobs_.empty(); });
    if (readback_publish_jobs_.empty()) {
      break;
    }
    const ReadbackPublishJob job = readback_publish_jobs_.front();
    readback_publish_jobs_.pop_front();
    lock.unlock();
    const uint32_t skipped = shared_memory_->CopyReadbackToGuest(job.address, job.length, job.data);
    TraceReadbackSkip(job.address, job.length, skipped);
    lock.lock();
    readback_publish_done_atomic_.store(++readback_publish_done_, std::memory_order_release);
    readback_publish_done_cond_.notify_all();
  }
}

uint64_t D3D12CommandProcessor::QueueReadbackPublish(uint32_t address, uint32_t length,
                                                     const uint8_t* data) {
  uint64_t sequence;
  {
    std::lock_guard<std::mutex> lock(readback_publish_mutex_);
    readback_publish_jobs_.push_back({address, length, data});
    sequence = ++readback_publish_queued_;
  }
  readback_publish_request_cond_.notify_one();
  return sequence;
}

void D3D12CommandProcessor::AwaitReadbackPublish(uint64_t sequence) {
  if (sequence <= readback_publish_done_atomic_.load(std::memory_order_acquire)) {
    return;
  }
  const uint64_t perf_start = perf_trace::Now();
  std::unique_lock<std::mutex> lock(readback_publish_mutex_);
  readback_publish_done_cond_.wait(lock,
                                   [this, sequence]() { return readback_publish_done_ >= sequence; });
  perf_trace::AddWait(perf_start, perf_trace::Wait::kReadbackPublish);
}

void D3D12CommandProcessor::ShutdownReadbackPublishThread() {
  if (!readback_publish_thread_.joinable()) return;
  {
    std::lock_guard<std::mutex> lock(readback_publish_mutex_);
    readback_publish_shutdown_ = true;
  }
  readback_publish_request_cond_.notify_all();
  readback_publish_thread_.join();
}

void D3D12CommandProcessor::PresentThreadMain() {
  rex::thread::set_current_thread_name("GPU Present");
  std::unique_lock<std::mutex> lock(present_mutex_);
  while (true) {
    present_cond_.wait(lock, [this]() { return present_shutdown_ || present_job_; });
    if (!present_job_) {
      return;
    }
    std::function<void()> job = std::move(present_job_);
    present_job_ = nullptr;
    lock.unlock();
    job();
    lock.lock();
    present_in_flight_ = false;
    present_cond_.notify_all();
  }
}

void D3D12CommandProcessor::DeferPresent(std::function<void()> present) {
  if (!present_thread_.joinable()) {
    EnqueueSubmitCallback(std::move(present));
    return;
  }
  {
    std::unique_lock<std::mutex> lock(present_mutex_);
    present_cond_.wait(lock, [this]() { return present_shutdown_ || !present_in_flight_; });
    if (present_shutdown_) return;
    present_in_flight_ = true;
  }
  EnqueueSubmitCallback([this, present = std::move(present)]() mutable {
    {
      std::lock_guard<std::mutex> lock(present_mutex_);
      if (present_shutdown_) return;
      present_job_ = std::move(present);
    }
    present_cond_.notify_all();
  });
}

void D3D12CommandProcessor::ShutdownPresentThread() {
  if (!present_thread_.joinable()) return;
  {
    std::lock_guard<std::mutex> lock(present_mutex_);
    present_shutdown_ = true;
  }
  present_cond_.notify_all();
  present_thread_.join();
  present_job_ = nullptr;
}

void D3D12CommandProcessor::ShutdownSubmitThread() {
  ShutdownPresentThread();
  if (!submit_thread_.joinable()) return;
  {
    std::lock_guard<std::mutex> lock(submit_mutex_);
    submit_shutdown_ = true;
  }
  submit_request_cond_.notify_all();
  submit_thread_.join();
  submit_free_lists_.clear();
}

bool D3D12CommandProcessor::UseRTVOcclusionSnapshots() const {
  return REXCVAR_GET(rtv_occlusion_snapshots) && REXCVAR_GET(occlusion_query_enable) &&
         occlusion_query_resources_available_ && occlusion_query_heap_ &&
         occlusion_query_readback_mapping_ &&
         render_target_cache_->GetPath() != RenderTargetCache::Path::kPixelShaderInterlock;
}

void D3D12CommandProcessor::OpenRTVOcclusionQuery() {
  if (rtv_occlusion_open_query_ != UINT32_MAX || !submission_open_ ||
      !UseRTVOcclusionSnapshots() || REXCVAR_GET(rtv_occlusion_debug) == "noqueries") {
    return;
  }
  uint32_t index;
  AcquireOcclusionQueryIndex(index);
  deferred_command_list_.D3DBeginQuery(occlusion_query_heap_.Get(), D3D12_QUERY_TYPE_OCCLUSION,
                                       index);
  rtv_occlusion_open_query_ = index;
  if (!rtv_occlusion_segment_count_++) {
    rtv_occlusion_segment_first_ = index;
  }
}

void D3D12CommandProcessor::CloseRTVOcclusionQuery() {
  const uint32_t index = rtv_occlusion_open_query_;
  if (index == UINT32_MAX) return;
  rtv_occlusion_open_query_ = UINT32_MAX;
  if (!submission_open_ || !occlusion_query_heap_) return;
  deferred_command_list_.D3DEndQuery(occlusion_query_heap_.Get(), D3D12_QUERY_TYPE_OCCLUSION,
                                     index);
  deferred_command_list_.D3DResolveQueryData(occlusion_query_heap_.Get(),
                                             D3D12_QUERY_TYPE_OCCLUSION, index, 1,
                                             occlusion_query_readback_.Get(),
                                             sizeof(uint64_t) * index);
}

bool D3D12CommandProcessor::WriteRTVOcclusionSnapshot(xenos::xe_gpu_depth_sample_counts* counts) {
  perf_trace::CauseScope perf_cause(perf_trace::Wait::kOcclusion);
  if (!BeginSubmission(true)) return false;
  const uint32_t kQueryPending = rex::byte_swap(0xFFFFFEEDu);
  const bool awaited = counts->ZPass_A == kQueryPending || counts->ZFail_A == kQueryPending;
  perf_trace::Increment(awaited ? perf_trace::Count::kOcclusionAwaited
                                : perf_trace::Count::kOcclusionUnawaited);
  if (rov_occlusion_pending_.size() >= kROVOcclusionSlots) {
    CheckSubmissionFence(rov_occlusion_pending_.front().submission);
    RetireROVOcclusionSnapshots();
    if (rov_occlusion_pending_.size() >= kROVOcclusionSlots) return false;
  }
  CloseRTVOcclusionQuery();
  ROVOcclusionPending pending = {};
  pending.submission = submission_current_;
  pending.host_queries = true;
  pending.first_query = rtv_occlusion_segment_first_;
  pending.query_count = rtv_occlusion_segment_count_;
  // The MSAA of the segment's draws; the current surface only without draws.
  const xenos::MsaaSamples msaa =
      rtv_occlusion_segment_msaa_
          ? xenos::MsaaSamples(rtv_occlusion_segment_msaa_ - 1)
          : register_file_->Get<reg::RB_SURFACE_INFO>().msaa_samples;
  pending.host_samples_per_pixel =
      msaa == xenos::MsaaSamples::k2X && !render_target_cache_->msaa_2x_supported()
          ? 4u
          : 1u << uint32_t(msaa);
  rtv_occlusion_segment_count_ = 0;
  rtv_occlusion_segment_msaa_ = 0;
  OpenRTVOcclusionQuery();
  return QueueOcclusionSnapshot(pending, counts, awaited);
}

void D3D12CommandProcessor::InitializeDrawStats() {
  draw_stats_recording_ = false;
  draw_stats_submission_ = 0;
  draw_stats_draws_.clear();
  const char* path = std::getenv("REX_DRAW_STATS");
  if (!path || !*path)
    return;
  if (const char* every = std::getenv("REX_DRAW_STATS_EVERY")) {
    draw_stats_every_ = uint32_t(std::max(1, std::atoi(every)));
  }
  ID3D12Device* device = GetD3D12Provider().GetDevice();
  D3D12_QUERY_HEAP_DESC heap_desc = {};
  heap_desc.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
  heap_desc.Count = kDrawStatsMax;
  const size_t size = sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS) * kDrawStatsMax;
  D3D12_RESOURCE_DESC buffer_desc;
  ui::d3d12::util::FillBufferResourceDesc(buffer_desc, size, D3D12_RESOURCE_FLAG_NONE);
  void* mapping = nullptr;
  D3D12_RANGE read_range = {0, size};
  if (FAILED(device->CreateQueryHeap(&heap_desc, IID_PPV_ARGS(&draw_stats_heap_))) ||
      FAILED(device->CreateCommittedResource(
          &ui::d3d12::util::kHeapPropertiesReadback, D3D12_HEAP_FLAG_NONE, &buffer_desc,
          D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&draw_stats_readback_))) ||
      FAILED(draw_stats_readback_->Map(0, &read_range, &mapping))) {
    REXGPU_WARN("D3D12CommandProcessor: pipeline statistics unavailable for REX_DRAW_STATS");
    ShutdownDrawStats();
    return;
  }
  draw_stats_mapping_ = static_cast<const D3D12_QUERY_DATA_PIPELINE_STATISTICS*>(mapping);
  draw_stats_output_ = std::make_unique<std::ofstream>(path);
}

void D3D12CommandProcessor::ShutdownDrawStats() {
  if (draw_stats_readback_ && draw_stats_mapping_) {
    draw_stats_readback_->Unmap(0, nullptr);
  }
  draw_stats_mapping_ = nullptr;
  draw_stats_readback_.Reset();
  draw_stats_heap_.Reset();
  draw_stats_output_.reset();
  draw_stats_recording_ = false;
  draw_stats_submission_ = 0;
}

bool D3D12CommandProcessor::BeginDrawStats(const uint32_t* values) {
  if (!draw_stats_recording_ || draw_stats_draws_.size() >= kDrawStatsMax)
    return false;
  DrawStatsDraw draw;
  std::memcpy(draw.values, values, sizeof(draw.values));
  draw_stats_draws_.push_back(draw);
  deferred_command_list_.D3DBeginQuery(draw_stats_heap_.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS,
                                       uint32_t(draw_stats_draws_.size() - 1));
  return true;
}

void D3D12CommandProcessor::EndDrawStats() {
  deferred_command_list_.D3DEndQuery(draw_stats_heap_.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS,
                                     uint32_t(draw_stats_draws_.size() - 1));
}

void D3D12CommandProcessor::ReadDrawStats() {
  draw_stats_submission_ = 0;
  if (!draw_stats_mapping_ || !draw_stats_output_)
    return;
  std::ofstream& output = *draw_stats_output_;
  output << "F " << draw_stats_frame_recorded_ << ' ' << draw_stats_frame_ms_ << '\n';
  for (size_t i = 0; i < draw_stats_draws_.size(); ++i) {
    const uint32_t* v = draw_stats_draws_[i].values;
    const D3D12_QUERY_DATA_PIPELINE_STATISTICS& stats = draw_stats_mapping_[i];
    output << "D " << i << std::hex;
    for (uint32_t j = 0; j < 8; ++j)
      output << ' ' << v[j];
    output << std::dec << ' ' << stats.IAPrimitives << ' ' << stats.CInvocations << ' '
           << stats.CPrimitives << ' ' << stats.PSInvocations << ' ' << v[8] << '\n';
  }
  output.flush();
  draw_stats_draws_.clear();
}

void D3D12CommandProcessor::InitializeFrameGraphMarks() {
  frame_graph_labels_.clear();
  frame_graph_recording_ = false;
  frame_graph_closing_ = false;
  frame_graph_submission_ = 0;
  if (!GetFrameGraphTrace())
    return;
  const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
  ID3D12Device* device = provider.GetDevice();
  D3D12_QUERY_HEAP_DESC heap_desc = {};
  heap_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
  heap_desc.Count = kFrameGraphMarks;
  D3D12_RESOURCE_DESC buffer_desc;
  ui::d3d12::util::FillBufferResourceDesc(buffer_desc, sizeof(uint64_t) * kFrameGraphMarks,
                                          D3D12_RESOURCE_FLAG_NONE);
  void* mapping = nullptr;
  D3D12_RANGE read_range = {0, sizeof(uint64_t) * kFrameGraphMarks};
  if (FAILED(provider.GetDirectQueue()->GetTimestampFrequency(&frame_graph_frequency_)) ||
      !frame_graph_frequency_ ||
      FAILED(device->CreateQueryHeap(&heap_desc, IID_PPV_ARGS(&frame_graph_heap_))) ||
      FAILED(device->CreateCommittedResource(
          &ui::d3d12::util::kHeapPropertiesReadback, D3D12_HEAP_FLAG_NONE, &buffer_desc,
          D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&frame_graph_readback_))) ||
      FAILED(frame_graph_readback_->Map(0, &read_range, &mapping))) {
    REXGPU_WARN("D3D12CommandProcessor: GPU timestamps unavailable for the frame graph");
    ShutdownFrameGraphMarks();
    return;
  }
  frame_graph_mapping_ = static_cast<const uint64_t*>(mapping);
}

void D3D12CommandProcessor::ShutdownFrameGraphMarks() {
  if (frame_graph_readback_ && frame_graph_mapping_) {
    frame_graph_readback_->Unmap(0, nullptr);
  }
  frame_graph_mapping_ = nullptr;
  frame_graph_readback_.Reset();
  frame_graph_heap_.Reset();
  frame_graph_recording_ = false;
  frame_graph_submission_ = 0;
}

void D3D12CommandProcessor::FrameGraphMark(int32_t label) {
  if (!frame_graph_mapping_ || !submission_open_)
    return;
  if (!frame_graph_recording_) {
    // Only whole frames are recorded, one at a time.
    if (label != 0 || frame_graph_submission_)
      return;
    frame_graph_recording_ = true;
    frame_graph_frame_ = FrameGraphFrame();
    frame_graph_labels_.clear();
  }
  // Keep room for the submission and swap marks.
  if (frame_graph_labels_.size() >= kFrameGraphMarks - (label >= 0 ? 16 : 0))
    return;
  deferred_command_list_.D3DEndQuery(frame_graph_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                     uint32_t(frame_graph_labels_.size()));
  frame_graph_labels_.push_back(label);
}

void D3D12CommandProcessor::ReadFrameGraphMarks() {
  frame_graph_submission_ = 0;
  if (!frame_graph_mapping_ || frame_graph_labels_.empty())
    return;
  const double ms = 1000.0 / double(frame_graph_frequency_);
  std::vector<double> events;
  double gaps = 0.0, swap = 0.0;
  int32_t current = kFrameGraphMarkSubmissionEnd;
  for (size_t i = 0; i + 1 < frame_graph_labels_.size(); ++i) {
    const int32_t label = frame_graph_labels_[i];
    const uint64_t begin = frame_graph_mapping_[i], end = frame_graph_mapping_[i + 1];
    const double duration = end > begin ? double(end - begin) * ms : 0.0;
    if (label >= 0 || label == kFrameGraphMarkSwap)
      current = label;
    if (label == kFrameGraphMarkSubmissionEnd) {
      gaps += duration;
    } else if (current == kFrameGraphMarkSwap) {
      swap += duration;
    } else if (current >= 0) {
      if (events.size() <= size_t(current))
        events.resize(size_t(current) + 1, 0.0);
      events[size_t(current)] += duration;
    }
  }
  FrameGraphWriteGpu(frame_graph_frame_, events, gaps, swap);
  frame_graph_labels_.clear();
}

void D3D12CommandProcessor::InitializeGpuTimestamps() {
  gpu_timestamp_frames_ = {};
  gpu_timestamp_submission_ = kGpuTimestampInvalid;
  if (!perf_trace::Get()) return;
  const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
  ID3D12Device* device = provider.GetDevice();
  constexpr uint32_t count = kQueueFrames * kGpuTimestampPairsPerFrame * 2;
  D3D12_QUERY_HEAP_DESC heap_desc = {};
  heap_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
  heap_desc.Count = count;
  D3D12_RESOURCE_DESC buffer_desc;
  ui::d3d12::util::FillBufferResourceDesc(buffer_desc, sizeof(uint64_t) * count,
                                          D3D12_RESOURCE_FLAG_NONE);
  void* mapping = nullptr;
  D3D12_RANGE read_range = {0, sizeof(uint64_t) * count};
  if (FAILED(provider.GetDirectQueue()->GetTimestampFrequency(&gpu_timestamp_frequency_)) ||
      !gpu_timestamp_frequency_ ||
      FAILED(device->CreateQueryHeap(&heap_desc, IID_PPV_ARGS(&gpu_timestamp_heap_))) ||
      FAILED(device->CreateCommittedResource(&ui::d3d12::util::kHeapPropertiesReadback,
                                             D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&gpu_timestamp_readback_))) ||
      FAILED(gpu_timestamp_readback_->Map(0, &read_range, &mapping))) {
    REXGPU_WARN("D3D12CommandProcessor: GPU timestamps unavailable for the perf trace");
    ShutdownGpuTimestamps();
    return;
  }
  gpu_timestamp_mapping_ = static_cast<const uint64_t*>(mapping);
}

void D3D12CommandProcessor::ShutdownGpuTimestamps() {
  if (gpu_timestamp_readback_ && gpu_timestamp_mapping_) {
    gpu_timestamp_readback_->Unmap(0, nullptr);
  }
  gpu_timestamp_mapping_ = nullptr;
  gpu_timestamp_readback_.Reset();
  gpu_timestamp_heap_.Reset();
  gpu_timestamp_submission_ = kGpuTimestampInvalid;
}

void D3D12CommandProcessor::ReadGpuTimestampFrame() {
  // Called when a frame opens: the frame that used this slot kQueueFrames ago
  // has been awaited by BeginSubmission.
  const uint32_t slot = uint32_t(frame_current_ % kQueueFrames);
  GpuTimestampFrame& frame = gpu_timestamp_frames_[slot];
  if (gpu_timestamp_mapping_ && frame.resolved && frame.pairs &&
      closed_frame_submissions_[slot] <= submission_completed_) {
    const uint64_t* ticks = gpu_timestamp_mapping_ + size_t(slot) * kGpuTimestampPairsPerFrame * 2;
    uint64_t busy = 0, resolve = 0, load = 0, readback = 0, resolve_full = 0, load_full = 0,
             transfer = 0, transfer_depth = 0;
    // The post pass groups in the order of perf_trace::kGpuPasses, then the swap.
    uint64_t passes[perf_trace::kGpuPassCount + 1] = {};
    // Resolves by kind: full screen, mid, small, clears only.
    double bucket_ms[4] = {};
    uint32_t bucket_count[4] = {};
    // The parts of the full-screen resolves (ResolvePart).
    double part_ms[kResolvePartCount] = {};
    // The host texture writes of all resolves by class, and their count.
    double host_write_ms[4] = {};
    uint32_t host_write_count[4] = {};
    const double tick_ms = 1000.0 / double(gpu_timestamp_frequency_);
    uint64_t first = UINT64_MAX, last = 0;
    for (uint32_t pair = 0; pair < frame.pairs; ++pair) {
      const uint64_t begin = ticks[pair * 2], end = ticks[pair * 2 + 1];
      const uint64_t duration = end > begin ? end - begin : 0;
      switch (frame.kinds[pair]) {
        case GpuTimestampKind::kSubmission:
          busy += duration;
          break;
        case GpuTimestampKind::kResolve:
          resolve += duration;
          break;
        case GpuTimestampKind::kTextureLoad:
          load += duration;
          break;
        case GpuTimestampKind::kReadback:
          readback += duration;
          break;
        case GpuTimestampKind::kResolveFull:
          resolve += duration;
          resolve_full += duration;
          bucket_ms[0] += double(duration) * tick_ms;
          ++bucket_count[0];
          break;
        case GpuTimestampKind::kResolveMid:
        case GpuTimestampKind::kResolveSmall:
        case GpuTimestampKind::kResolveClear: {
          resolve += duration;
          const size_t bucket = 1 + size_t(frame.kinds[pair]) - size_t(GpuTimestampKind::kResolveMid);
          bucket_ms[bucket] += double(duration) * tick_ms;
          ++bucket_count[bucket];
        } break;
        case GpuTimestampKind::kTextureLoadFull:
          load += duration;
          load_full += duration;
          break;
        case GpuTimestampKind::kTransfer:
          transfer += duration;
          break;
        case GpuTimestampKind::kTransferDepth:
          transfer_depth += duration;
          break;
        case GpuTimestampKind::kPassGtao:
        case GpuTimestampKind::kPassTaa:
        case GpuTimestampKind::kPassMotion:
        case GpuTimestampKind::kPassWindshield:
        case GpuTimestampKind::kPassFsr:
        case GpuTimestampKind::kPassOther:
        case GpuTimestampKind::kSwap:
          passes[size_t(frame.kinds[pair]) - size_t(GpuTimestampKind::kPassGtao)] += duration;
          break;
        case GpuTimestampKind::kResolvePartTransfers:
        case GpuTimestampKind::kResolvePartDump:
        case GpuTimestampKind::kResolvePartGuestLayout:
        case GpuTimestampKind::kResolvePartHostTextures:
        case GpuTimestampKind::kResolvePartClear:
          part_ms[size_t(frame.kinds[pair]) - size_t(GpuTimestampKind::kResolvePartTransfers)] +=
              double(duration) * tick_ms;
          break;
        case GpuTimestampKind::kHostWriteSceneSampled:
        case GpuTimestampKind::kHostWriteSceneUnsampled:
        case GpuTimestampKind::kHostWriteWholeSampled:
        case GpuTimestampKind::kHostWriteWholeUnsampled: {
          const size_t host_write_class =
              size_t(frame.kinds[pair]) - size_t(GpuTimestampKind::kHostWriteSceneSampled);
          host_write_ms[host_write_class] += double(duration) * tick_ms;
          ++host_write_count[host_write_class];
        } break;
      }
      first = std::min(first, begin);
      last = std::max(last, end);
    }
    const double ms = 1000.0 / double(gpu_timestamp_frequency_);
    perf_trace::AddGpuFrame(double(busy) * ms, double(resolve) * ms,
                            last > first ? double(last - first) * ms : 0.0, double(load) * ms,
                            double(readback) * ms, double(resolve_full) * ms,
                            double(load_full) * ms, double(transfer) * ms,
                            double(transfer_depth) * ms);
    perf_trace::AddGpuResolveBuckets(bucket_ms, bucket_count);
    perf_trace::AddGpuResolveParts(part_ms);
    perf_trace::AddGpuResolveHostWrites(host_write_ms, host_write_count);
    double passes_ms[perf_trace::kGpuPassCount + 1];
    for (size_t i = 0; i < perf_trace::kGpuPassCount + 1; ++i) passes_ms[i] = double(passes[i]) * ms;
    perf_trace::AddGpuPasses(passes_ms);
  }
  frame = {};
}

uint32_t D3D12CommandProcessor::BeginTextureLoadTimestamp(bool full_screen) {
  return BeginGpuTimestamp(full_screen ? GpuTimestampKind::kTextureLoadFull
                                       : GpuTimestampKind::kTextureLoad);
}

D3D12CommandProcessor::GpuTimestampKind D3D12CommandProcessor::GetResolveTimestampKind(
    uint32_t written_length) {
  if (written_length >= 1280 * 720 * 4) {
    return GpuTimestampKind::kResolveFull;
  }
  if (written_length >= 256 * 256 * 4) {
    return GpuTimestampKind::kResolveMid;
  }
  return written_length ? GpuTimestampKind::kResolveSmall : GpuTimestampKind::kResolveClear;
}

uint32_t D3D12CommandProcessor::BeginPostPassTimestamp(std::string_view pass_name) {
  const auto group = [&]() {
    if (pass_name.starts_with("gtao")) return GpuTimestampKind::kPassGtao;
    if (pass_name.starts_with("taa")) return GpuTimestampKind::kPassTaa;
    if (pass_name.starts_with("motion")) return GpuTimestampKind::kPassMotion;
    if (pass_name.starts_with("windshield")) return GpuTimestampKind::kPassWindshield;
    if (pass_name.starts_with("fsr")) return GpuTimestampKind::kPassFsr;
    return GpuTimestampKind::kPassOther;
  };
  return BeginGpuTimestamp(group());
}

uint32_t D3D12CommandProcessor::BeginTransferTimestamp(bool depth) {
  return BeginGpuTimestamp(depth ? GpuTimestampKind::kTransferDepth
                                 : GpuTimestampKind::kTransfer);
}

uint32_t D3D12CommandProcessor::BeginResolvePartTimestamp(ResolvePart part) {
  return BeginGpuTimestamp(GpuTimestampKind(size_t(GpuTimestampKind::kResolvePartTransfers) +
                                            size_t(part)));
}

uint32_t D3D12CommandProcessor::BeginResolveHostWriteTimestamp(bool scene, bool unsampled) {
  return BeginGpuTimestamp(GpuTimestampKind(size_t(GpuTimestampKind::kHostWriteSceneSampled) +
                                            (scene ? 0 : 2) + (unsampled ? 1 : 0)));
}

void D3D12CommandProcessor::SetGpuTimestampKind(uint32_t pair, GpuTimestampKind kind) {
  if (pair == kGpuTimestampInvalid) return;
  gpu_timestamp_frames_[uint32_t(frame_current_ % kQueueFrames)].kinds[pair] = kind;
}

void D3D12CommandProcessor::EndTextureLoadTimestamp(uint32_t pair) { EndGpuTimestamp(pair); }

uint32_t D3D12CommandProcessor::BeginGpuTimestamp(GpuTimestampKind kind) {
  if (!gpu_timestamp_mapping_ || !frame_open_ || !submission_open_) return kGpuTimestampInvalid;
  const uint32_t slot = uint32_t(frame_current_ % kQueueFrames);
  GpuTimestampFrame& frame = gpu_timestamp_frames_[slot];
  if (frame.pairs >= kGpuTimestampPairsPerFrame) return kGpuTimestampInvalid;
  const uint32_t pair = frame.pairs++;
  frame.kinds[pair] = kind;
  deferred_command_list_.D3DEndQuery(gpu_timestamp_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                     (slot * kGpuTimestampPairsPerFrame + pair) * 2);
  return pair;
}

void D3D12CommandProcessor::EndGpuTimestamp(uint32_t pair) {
  if (pair == kGpuTimestampInvalid || !gpu_timestamp_mapping_ || !submission_open_) return;
  const uint32_t slot = uint32_t(frame_current_ % kQueueFrames);
  deferred_command_list_.D3DEndQuery(gpu_timestamp_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                     (slot * kGpuTimestampPairsPerFrame + pair) * 2 + 1);
}

void D3D12CommandProcessor::ResolveGpuTimestampFrame() {
  if (!gpu_timestamp_mapping_) return;
  const uint32_t slot = uint32_t(frame_current_ % kQueueFrames);
  GpuTimestampFrame& frame = gpu_timestamp_frames_[slot];
  if (!frame.pairs) return;
  const uint32_t first = slot * kGpuTimestampPairsPerFrame * 2;
  deferred_command_list_.D3DResolveQueryData(gpu_timestamp_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                             first, frame.pairs * 2, gpu_timestamp_readback_.Get(),
                                             sizeof(uint64_t) * first);
  frame.resolved = true;
}

bool D3D12CommandProcessor::AcquireOcclusionQueryIndex(uint32_t& host_index_out) {
  if (occlusion_query_cursor_ >= kMaxOcclusionQueries) {
    occlusion_query_cursor_ = 0;
  }
  host_index_out = occlusion_query_cursor_++;
  return true;
}

void D3D12CommandProcessor::DisableHostOcclusionQueries() {
  if (active_occlusion_query_.valid && occlusion_query_heap_) {
    uint32_t host_index = active_occlusion_query_.host_index;
    // Clear before EndSubmission to prevent the EndSubmission safety net from issuing a second
    // EndQuery for the same index.
    active_occlusion_query_ = {};
    if (BeginSubmission(true)) {
      deferred_command_list_.D3DEndQuery(occlusion_query_heap_.Get(), D3D12_QUERY_TYPE_OCCLUSION,
                                         host_index);
      EndSubmission(false);
    }
  } else {
    active_occlusion_query_ = {};
  }
  occlusion_query_cursor_ = 0;
  occlusion_query_resources_available_ = false;
}

bool D3D12CommandProcessor::BeginGuestOcclusionQuery(uint32_t sample_count_address) {
  if (!REXCVAR_GET(occlusion_query_enable) || !occlusion_query_resources_available_) {
    return false;
  }
  if (active_occlusion_query_.valid) {
    REXGPU_WARN(
        "D3D12CommandProcessor: Occlusion query begin issued while another query is active");
    DisableHostOcclusionQueries();
    return false;
  }

  uint32_t host_index = 0;
  if (!AcquireOcclusionQueryIndex(host_index)) {
    return false;
  }
  if (!BeginSubmission(true)) {
    return false;
  }

  deferred_command_list_.D3DBeginQuery(occlusion_query_heap_.Get(), D3D12_QUERY_TYPE_OCCLUSION,
                                       host_index);
  active_occlusion_query_.sample_count_address = sample_count_address;
  active_occlusion_query_.host_index = host_index;
  active_occlusion_query_.valid = true;
  return true;
}

bool D3D12CommandProcessor::EndGuestOcclusionQuery(
    uint32_t sample_count_address, xenos::xe_gpu_depth_sample_counts* sample_counts) {
  perf_trace::CauseScope perf_cause(perf_trace::Wait::kOcclusion);
  if (!REXCVAR_GET(occlusion_query_enable) || !occlusion_query_resources_available_ ||
      !active_occlusion_query_.valid || !occlusion_query_heap_ || !occlusion_query_readback_) {
    return false;
  }

  uint32_t host_index = active_occlusion_query_.host_index;
  active_occlusion_query_ = {};

  if (!BeginSubmission(true)) {
    return false;
  }

  deferred_command_list_.D3DEndQuery(occlusion_query_heap_.Get(), D3D12_QUERY_TYPE_OCCLUSION,
                                     host_index);
  deferred_command_list_.D3DResolveQueryData(
      occlusion_query_heap_.Get(), D3D12_QUERY_TYPE_OCCLUSION, host_index, 1,
      occlusion_query_readback_.Get(), sizeof(uint64_t) * host_index);

  if (!EndSubmission(false)) {
    return false;
  }

  uint64_t query_submission = submission_current_ ? submission_current_ - 1 : 0;
  CheckSubmissionFence(query_submission);
  if (submission_completed_ < query_submission) {
    return false;
  }
  if (!occlusion_query_readback_mapping_) {
    return false;
  }

  uint64_t samples = occlusion_query_readback_mapping_[host_index];
  samples = NormalizeOcclusionSamples(samples);
  WriteGuestOcclusionResult(sample_counts, samples);
  return true;
}

double D3D12CommandProcessor::OcclusionResolutionScale() const {
  if (!texture_cache_) {
    return 1.0;
  }
  // At a fractional scene scale the queries' draws go into the private
  // surfaces at the scene's scale (docs/analysis/d3d-hle-n5.md 18.16), the
  // MSAA scene at its input scale while upscaling (fsr-plan.md 11).
  const DrawScale scene_scale = render_target_cache_->scene_input_scale();
  if (render_target_cache_->scene_textures() && !scene_scale.IsWhole()) {
    const double scale = scene_scale.ToFloat();
    return scale * scale;
  }
  return double(texture_cache_->draw_resolution_scale_x() *
                texture_cache_->draw_resolution_scale_y());
}

uint64_t D3D12CommandProcessor::NormalizeOcclusionSamples(uint64_t samples) const {
  if (samples == 0 || !texture_cache_) {
    return samples;
  }
  const DrawScale scene_scale = render_target_cache_->scene_input_scale();
  if (render_target_cache_->scene_textures() && !scene_scale.IsWhole()) {
    return uint64_t(double(samples) / OcclusionResolutionScale() + 0.5);
  }
  uint64_t scale_x = texture_cache_->draw_resolution_scale_x();
  uint64_t scale_y = texture_cache_->draw_resolution_scale_y();
  uint64_t scale = scale_x * scale_y;
  if (scale <= 1) {
    return samples;
  }
  return (samples + (scale >> 1)) / scale;
}

void D3D12CommandProcessor::WriteGuestOcclusionResult(
    xenos::xe_gpu_depth_sample_counts* sample_counts, uint64_t samples) {
  if (!sample_counts) {
    return;
  }
  uint32_t clamped = samples > uint64_t(UINT32_MAX) ? UINT32_MAX : uint32_t(samples);
  sample_counts->Total_A = clamped;
  sample_counts->Total_B = 0;
  sample_counts->ZPass_A = clamped;
  sample_counts->ZPass_B = 0;
  sample_counts->ZFail_A = 0;
  sample_counts->ZFail_B = 0;
  sample_counts->StencilFail_A = 0;
  sample_counts->StencilFail_B = 0;
}

void D3D12CommandProcessor::WriteGammaRampSRV(bool is_pwl,
                                              D3D12_CPU_DESCRIPTOR_HANDLE handle) const {
  ID3D12Device* device = GetD3D12Provider().GetDevice();
  D3D12_SHADER_RESOURCE_VIEW_DESC desc;
  desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
  desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  desc.Buffer.StructureByteStride = 0;
  desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
  if (is_pwl) {
    desc.Format = DXGI_FORMAT_R16G16_UINT;
    desc.Buffer.FirstElement = 256 * 4 / 4;
    desc.Buffer.NumElements = 128 * 3;
  } else {
    desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
    desc.Buffer.FirstElement = 0;
    desc.Buffer.NumElements = 256;
  }
  device->CreateShaderResourceView(gamma_ramp_buffer_.Get(), &desc, handle);
}

}  // namespace rex::graphics::d3d12
