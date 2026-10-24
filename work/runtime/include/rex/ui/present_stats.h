#pragma once
// The host presents of the presenter, for the perf trace (stage N4 of
// docs/analysis/d3d-hle-n4.md, section 17.22): how many showed a guest output
// frame that no earlier present had shown, how many showed the same frame again
// (a repaint for the user interface), and how many the UI thread made; and how
// many came sooner after the previous present than a refresh interval at 120
// and 240 Hz (8.33 and 4.17 ms), which a VRR panel at that rate cannot show.

#include <cstdint>

namespace rex::ui {

struct PresentStats {
  uint64_t new_frames = 0;
  uint64_t repeats = 0;
  uint64_t ui_thread = 0;
  uint64_t under_8_33_ms = 0;
  uint64_t under_4_17_ms = 0;
};

// The presents since the last call.
PresentStats TakePresentStats();

}  // namespace rex::ui
