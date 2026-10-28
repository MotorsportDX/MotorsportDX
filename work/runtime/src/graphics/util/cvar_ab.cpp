#include "cvar_ab.h"

#include <chrono>
#include <numeric>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(ab_cvar, "", "GPU",
                      "In-run A/B: the runtime cvar set to the first of ab_values in even periods "
                      "of ab_period_ms of the steady clock and to the second in odd periods, at the "
                      "guest swap (empty: off; docs/e2e/experiments.md). Refuses cvars that need a "
                      "restart")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(ab_values, "", "GPU",
                      "In-run A/B: the two values of ab_cvar, separated by a comma")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(ab_period_ms, 500, "GPU",
                     "In-run A/B: the length of each side's period of the steady clock")
    .range(10, 3600000)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(ab_window_ms, 125, "GPU",
                     "In-run A/B: the perf trace's window on a grid of this many milliseconds "
                     "instead of one second, so no window spans a flip (a divisor of ab_period_ms)")
    .range(10, 3600000)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace rex::graphics::cvar_ab {

namespace {

Config* Load() {
  const std::string name = REXCVAR_GET(ab_cvar);
  if (name.empty()) return nullptr;
  const rex::cvar::FlagEntry* entry = rex::cvar::GetFlagInfo(name);
  if (!entry) {
    REXLOG_ERROR("ab_cvar: no cvar named '{}'; the A/B is off", name);
    return nullptr;
  }
  if (entry->type == rex::cvar::FlagType::Command) {
    REXLOG_ERROR("ab_cvar: '{}' is a command, not a value; the A/B is off", name);
    return nullptr;
  }
  if (entry->lifecycle != rex::cvar::Lifecycle::kHotReload) {
    REXLOG_ERROR(
        "ab_cvar: '{}' only takes effect at a restart or at initialization (kRequiresRestart or "
        "kInitOnly), so it cannot change within a run; compare it with separate runs. The A/B "
        "is off",
        name);
    return nullptr;
  }
  const std::string values = REXCVAR_GET(ab_values);
  const size_t comma = values.find(',');
  if (comma == std::string::npos || values.find(',', comma + 1) != std::string::npos) {
    REXLOG_ERROR("ab_values: '{}' is not two values separated by a comma; the A/B is off", values);
    return nullptr;
  }
  auto* config = new Config;
  config->cvar = name;
  config->values[0] = values.substr(0, comma);
  config->values[1] = values.substr(comma + 1);
  const uint64_t period_ms = uint64_t(REXCVAR_GET(ab_period_ms));
  uint64_t window_ms = uint64_t(REXCVAR_GET(ab_window_ms));
  if (period_ms % window_ms) {
    const uint64_t divisor = std::gcd(period_ms, window_ms);
    REXLOG_WARN("ab_window_ms: {} does not divide ab_period_ms {}; using {}", window_ms, period_ms,
                divisor);
    window_ms = divisor;
  }
  config->period_ns = period_ms * 1000000;
  config->window_ns = window_ms * 1000000;
  REXLOG_INFO("ab_cvar: {} = {} in even and {} in odd periods of {} ms (perf windows of {} ms)",
              name, config->values[0], config->values[1], period_ms, window_ms);
  return config;
}

Applied applied;
bool failed = false;

}  // namespace

const Config* Get() {
  static const Config* config = Load();
  return config;
}

Applied Current() { return applied; }

void OnSwap() {
  const Config* config = Get();
  if (!config || failed) return;
  const uint64_t now = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch())
                                    .count());
  const uint64_t period = now / config->period_ns;
  if (applied.side >= 0 && period == applied.period) return;
  const int32_t side = int32_t(period & 1);
  if (side != applied.side) {
    if (!rex::cvar::SetFlagByName(config->cvar, config->values[side])) {
      REXLOG_ERROR("ab_cvar: {} rejected the value '{}'; the A/B stops", config->cvar,
                   config->values[side]);
      failed = true;
      applied = {};
      return;
    }
  }
  applied = {side, period};
}

}  // namespace rex::graphics::cvar_ab
