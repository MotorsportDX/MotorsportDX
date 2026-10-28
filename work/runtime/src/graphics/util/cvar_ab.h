#pragma once

// In-run A/B of one runtime cvar (ab_cvar, ab_values, ab_period_ms): at the
// guest swap, the cvar is set to the first value in even periods of the steady
// clock and to the second in odd periods, so one run measures both sides in
// alternation (docs/e2e/experiments.md, "In-run perf A/B"). The perf trace
// labels its windows with the side and, in this mode, closes them on a grid of
// ab_window_ms, so no window spans a flip. Off (empty ab_cvar) by default; then
// OnSwap costs one branch.

#include <cstdint>
#include <string>

namespace rex::graphics::cvar_ab {

struct Config {
  std::string cvar;
  std::string values[2];
  // Nanoseconds of the steady clock (perf_trace::Now).
  uint64_t period_ns = 0;
  uint64_t window_ns = 0;
};

// The configuration, or nullptr when the A/B is off or refused (logged as an
// error once). Read from the cvars at the first call.
const Config* Get();

// The side set at the last swap (0: values[0], 1: values[1]; -1 before the
// first swap or when off), and the index of its period of the steady clock.
struct Applied {
  int32_t side = -1;
  uint64_t period = 0;
};
Applied Current();

// Called at every guest swap before perf_trace::OnSwap: sets the cvar when the
// side of the steady clock's period changed since the last swap.
void OnSwap();

}  // namespace rex::graphics::cvar_ab
