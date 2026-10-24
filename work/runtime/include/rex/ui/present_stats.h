#pragma once
// The host presents of the presenter, for the perf trace (stage N4 of
// docs/analysis/d3d-hle-n4.md, section 17.22): how many showed a guest output
// frame that no earlier present had shown, how many showed the same frame again
// (a repaint for the user interface), and how many the UI thread made; and how
// many came sooner after the previous present than a refresh interval at 120
// and 240 Hz (8.33 and 4.17 ms), which a VRR panel at that rate cannot show.

#include <cstdint>
#include <cstdlib>

namespace rex::ui {

struct PresentStats {
  uint64_t new_frames = 0;
  uint64_t repeats = 0;
  uint64_t ui_thread = 0;
  uint64_t under_8_33_ms = 0;
  uint64_t under_4_17_ms = 0;
  // The GPU time of the presenter's paints whose timestamps were read (the
  // guest output scaled into the back buffer, its effect and the UI;
  // d3d-hle-n4.md 17.24), with the perf trace only.
  double paint_gpu_ms = 0;
  uint64_t paint_gpu_paints = 0;
};

// A paint's GPU time, from the presenter's timestamps.
void AddPaintGpuMs(double ms);

// The presents since the last call.
PresentStats TakePresentStats();

// Whether the perf trace runs (REX_PERF_TRACE_FILE), so the presenter measures
// its paints' GPU time.
inline bool PaintGpuTimingEnabled() {
  static const bool enabled = [] {
    const char* path = std::getenv("REX_PERF_TRACE_FILE");
    return path && *path;
  }();
  return enabled;
}

}  // namespace rex::ui
