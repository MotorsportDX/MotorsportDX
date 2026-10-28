/**
 * @file        input/wheel/wheel_host.cpp
 * @brief       The racing wheel's device and force feedback host
 *
 * See wheel_host.h and docs/analysis/racing-wheels.md (stage 0).
 */

#include <rex/input/wheel/wheel_host.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <format>
#include <numbers>

#include <SDL3/SDL.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <share.h>
#endif

namespace rex::input::wheel {

namespace {

// The hosts the emergency stop reaches; a slot is cleared by Stop.
constexpr size_t kMaxHosts = 4;
std::array<std::atomic<WheelHost*>, kMaxHosts> g_hosts{};
std::once_flag g_handlers_once;

#if defined(_WIN32)
LPTOP_LEVEL_EXCEPTION_FILTER g_previous_filter = nullptr;

LONG WINAPI WheelUnhandledExceptionFilter(EXCEPTION_POINTERS* exception) {
  WheelHost::EmergencyStopAll();
  return g_previous_filter ? g_previous_filter(exception) : EXCEPTION_CONTINUE_SEARCH;
}
#endif

void (*g_previous_abort)(int) = nullptr;

void WheelAbortHandler(int signal) {
  WheelHost::EmergencyStopAll();
  if (g_previous_abort && g_previous_abort != SIG_DFL && g_previous_abort != SIG_IGN) {
    g_previous_abort(signal);
  }
}

void InstallEmergencyHandlers() {
  std::call_once(g_handlers_once, []() {
#if defined(_WIN32)
    g_previous_filter = SetUnhandledExceptionFilter(WheelUnhandledExceptionFilter);
#endif
    g_previous_abort = std::signal(SIGABRT, WheelAbortHandler);
    std::atexit([]() { WheelHost::EmergencyStopAll(); });
  });
}

double Seconds(uint64_t ns) { return double(ns) * 1e-9; }

std::string Lower(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  return out;
}

const char* JoystickTypeName(SDL_JoystickType type) {
  switch (type) {
    case SDL_JOYSTICK_TYPE_GAMEPAD: return "gamepad";
    case SDL_JOYSTICK_TYPE_WHEEL: return "wheel";
    case SDL_JOYSTICK_TYPE_ARCADE_STICK: return "arcade_stick";
    case SDL_JOYSTICK_TYPE_FLIGHT_STICK: return "flight_stick";
    case SDL_JOYSTICK_TYPE_DANCE_PAD: return "dance_pad";
    case SDL_JOYSTICK_TYPE_GUITAR: return "guitar";
    case SDL_JOYSTICK_TYPE_DRUM_KIT: return "drum_kit";
    case SDL_JOYSTICK_TYPE_ARCADE_PAD: return "arcade_pad";
    case SDL_JOYSTICK_TYPE_THROTTLE: return "throttle";
    default: return "unknown";
  }
}

std::string HapticFeatureNames(uint32_t features) {
  struct Name {
    uint32_t bit;
    const char* name;
  };
  static const Name names[] = {
      {SDL_HAPTIC_CONSTANT, "constant"},   {SDL_HAPTIC_SINE, "sine"},
      {SDL_HAPTIC_SQUARE, "square"},       {SDL_HAPTIC_TRIANGLE, "triangle"},
      {SDL_HAPTIC_SAWTOOTHUP, "sawtoothup"}, {SDL_HAPTIC_SAWTOOTHDOWN, "sawtoothdown"},
      {SDL_HAPTIC_RAMP, "ramp"},           {SDL_HAPTIC_SPRING, "spring"},
      {SDL_HAPTIC_DAMPER, "damper"},       {SDL_HAPTIC_INERTIA, "inertia"},
      {SDL_HAPTIC_FRICTION, "friction"},   {SDL_HAPTIC_LEFTRIGHT, "leftright"},
      {SDL_HAPTIC_CUSTOM, "custom"},       {SDL_HAPTIC_GAIN, "gain"},
      {SDL_HAPTIC_AUTOCENTER, "autocenter"}, {SDL_HAPTIC_STATUS, "status"},
      {SDL_HAPTIC_PAUSE, "pause"},
  };
  std::string out;
  for (const auto& name : names) {
    if (features & name.bit) {
      out += out.empty() ? "" : ",";
      out += name.name;
    }
  }
  return out.empty() ? "none" : out;
}

// A wheel candidate: SDL's wheel type, or any joystick SDL does not take as a gamepad (a
// wheel without SDL's wheel list entry, a separate pedal set).
bool IsCandidate(SDL_JoystickID id) {
  return SDL_GetJoystickTypeForID(id) == SDL_JOYSTICK_TYPE_WHEEL || !SDL_IsGamepad(id);
}

SDL_HapticEffect ConstantEffect(int16_t level) {
  SDL_HapticEffect effect{};
  effect.type = SDL_HAPTIC_CONSTANT;
  effect.constant.direction.type = SDL_HAPTIC_STEERING_AXIS;
  effect.constant.length = SDL_HAPTIC_INFINITY;
  effect.constant.level = level;
  return effect;
}

SDL_HapticEffect SpringEffect(double level) {
  SDL_HapticEffect effect{};
  effect.type = SDL_HAPTIC_SPRING;
  effect.condition.direction.type = SDL_HAPTIC_CARTESIAN;
  effect.condition.direction.dir[0] = 1;
  effect.condition.length = SDL_HAPTIC_INFINITY;
  // The first axis only. The saturation bounds the spring's force to the level, so the cap
  // holds whatever the wheel's position.
  const double clamped = std::clamp(level, 0.0, 1.0);
  effect.condition.right_sat[0] = uint16_t(std::lround(clamped * 65535.0));
  effect.condition.left_sat[0] = effect.condition.right_sat[0];
  effect.condition.right_coeff[0] = int16_t(std::lround(clamped * 32767.0));
  effect.condition.left_coeff[0] = effect.condition.right_coeff[0];
  return effect;
}

}  // namespace

struct WheelHost::Device {
  SDL_Joystick* joystick = nullptr;
  SDL_JoystickID id = 0;
  SDL_Haptic* haptic = nullptr;
  uint32_t features = 0;
  SDL_HapticEffectID constant = -1;
  SDL_HapticEffectID spring = -1;
  bool constant_running = false;
  bool spring_running = false;
  int16_t constant_level = 0;
  double spring_level = 0.0;
};

WheelHost::WheelHost(HostOptions options, LogFunction log)
    : options_(std::move(options)), log_(std::move(log)), governor_(options_.governor) {
  options_.rate_hz = std::clamp(options_.rate_hz, 100, 2000);
  governor_.SetBlocked(kBlockNoDevice, true);
}

WheelHost::~WheelHost() { Stop(); }

void WheelHost::Log(LogLevel level, std::string_view text) const {
  if (log_) {
    log_(level, text);
  }
}

void WheelHost::Start() {
  if (running_.exchange(true)) {
    return;
  }
  InstallEmergencyHandlers();
  for (auto& slot : g_hosts) {
    WheelHost* expected = nullptr;
    if (slot.compare_exchange_strong(expected, this)) {
      break;
    }
  }
  if (!options_.record_path.empty()) {
#if defined(_WIN32)
    // The path is UTF-8.
    const std::string& path = options_.record_path;
    std::wstring wide(size_t(MultiByteToWideChar(CP_UTF8, 0, path.data(), int(path.size()),
                                                 nullptr, 0)),
                      L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.data(), int(path.size()), wide.data(), int(wide.size()));
    // Readable while the host writes it (_wfopen_s would deny other readers).
    record_ = _wfsopen(wide.c_str(), L"w", _SH_DENYWR);
#else
    record_ = std::fopen(options_.record_path.c_str(), "w");
#endif
    if (record_) {
      std::fputs("time_s,target,output,spring,envelope,blocked,flags,send_us\n", record_);
    } else {
      Log(LogLevel::kWarning,
          std::format("Wheel: cannot write the force recording '{}'", options_.record_path));
    }
  }
  const auto& g = governor_.config();
  Log(LogLevel::kInfo,
      std::format("Wheel: force feedback host started (sink={}, cap={:.2f}, gain={:.2f}, "
                  "slew={:.1f}/s, ramp={:.1f} s, watchdog={:.0f} ms, rate={} Hz, invert={}{}{})",
                  options_.sink == Sink::kDevice ? "device" : "record", g.cap, g.gain,
                  g.slew_per_s, g.ramp_s, g.watchdog_s * 1000.0, options_.rate_hz,
                  options_.invert ? 1 : 0, record_ ? ", recording to " : "",
                  record_ ? options_.record_path : ""));
  stop_ = false;
  thread_ = std::thread([this]() { Run(); });
}

void WheelHost::Stop() {
  if (!running_.exchange(false)) {
    return;
  }
  stop_ = true;
  if (thread_.joinable()) {
    thread_.join();
  }
  for (auto& slot : g_hosts) {
    WheelHost* expected = this;
    slot.compare_exchange_strong(expected, nullptr);
  }
  if (record_) {
    std::fclose(record_);
    record_ = nullptr;
  }
  const Snapshot snapshot = GetSnapshot();
  Log(LogLevel::kInfo,
      std::format("Wheel: force feedback host stopped; {} device updates (mean {:.0f} us, max "
                  "{:.0f} us), tick mean {:.3f} ms, max {:.3f} ms",
                  snapshot.sends, snapshot.send_us_mean, snapshot.send_us_max,
                  snapshot.tick_ms_mean, snapshot.tick_ms_max));
}

void WheelHost::SetBlocked(uint32_t reason, bool blocked) {
  if (blocked) {
    blocked_.fetch_or(reason);
  } else {
    blocked_.fetch_and(~reason);
  }
}

void WheelHost::SubmitForce(double force) {
  source_bits_.store(std::bit_cast<uint64_t>(force), std::memory_order_relaxed);
  source_ns_.store(SDL_GetTicksNS(), std::memory_order_release);
}

void WheelHost::SetTest(TestEffect test, double amplitude) {
  test_amplitude_bits_.store(std::bit_cast<uint64_t>(amplitude));
  test_started_ns_.store(SDL_GetTicksNS());
  test_.store(int(test));
}

Snapshot WheelHost::GetSnapshot() const {
  std::lock_guard lock(snapshot_mutex_);
  return snapshot_;
}

void WheelHost::EmergencyStopAll() {
  for (auto& slot : g_hosts) {
    if (WheelHost* host = slot.load()) {
      host->EmergencyStop();
    }
  }
}

void WheelHost::EmergencyStop() {
  emergency_ = true;
  // The force thread zeroes and stops the effects within a tick. If it does not answer (it
  // may be the thread that crashed), stop them from here.
  for (int i = 0; i < 50 && !emergency_done_.load(); ++i) {
    if (thread_.get_id() == std::this_thread::get_id()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (!emergency_done_.load()) {
    if (auto* haptic = static_cast<SDL_Haptic*>(haptic_published_.load())) {
      SDL_StopHapticEffects(haptic);
    }
  }
}

void WheelHost::Scan(double now_s) {
  int count = 0;
  SDL_JoystickID* ids = SDL_GetJoysticks(&count);
  if (!ids) {
    return;
  }
  const std::string filter = Lower(options_.name_filter);
  SDL_JoystickID chosen = 0;
  int chosen_rank = 0;
  for (int i = 0; i < count; ++i) {
    const SDL_JoystickID id = ids[i];
    if (!IsCandidate(id)) {
      continue;
    }
    const char* name = SDL_GetJoystickNameForID(id);
    if (!filter.empty() && Lower(name ? name : "").find(filter) == std::string::npos) {
      continue;
    }
    // SDL's wheel type first, then any other joystick.
    const int rank = SDL_GetJoystickTypeForID(id) == SDL_JOYSTICK_TYPE_WHEEL ? 2 : 1;
    if (rank > chosen_rank) {
      chosen = id;
      chosen_rank = rank;
    }
  }
  SDL_free(ids);
  if (!chosen) {
    return;
  }
  SDL_Joystick* joystick = SDL_OpenJoystick(chosen);
  if (!joystick) {
    Log(LogLevel::kWarning, std::format("Wheel: cannot open joystick {}: {}", chosen,
                                        SDL_GetError()));
    return;
  }
  device_ = std::make_unique<Device>();
  device_->joystick = joystick;
  device_->id = chosen;
  const char* name = SDL_GetJoystickName(joystick);
  const char* path = SDL_GetJoystickPath(joystick);

  std::string haptic_text = "haptic=0";
  if (options_.sink == Sink::kDevice) {
    SDL_Haptic* haptic = SDL_IsJoystickHaptic(joystick) ? SDL_OpenHapticFromJoystick(joystick)
                                                        : nullptr;
    if (!haptic) {
      // SDL's haptic list can hold the wheel under the same name when the joystick's driver
      // cannot give it.
      int haptic_count = 0;
      if (SDL_HapticID* haptic_ids = SDL_GetHaptics(&haptic_count)) {
        for (int i = 0; i < haptic_count && !haptic; ++i) {
          const char* haptic_name = SDL_GetHapticNameForID(haptic_ids[i]);
          if (name && haptic_name && std::string_view(name) == haptic_name) {
            haptic = SDL_OpenHaptic(haptic_ids[i]);
          }
        }
        SDL_free(haptic_ids);
      }
    }
    if (haptic) {
      device_->haptic = haptic;
      device_->features = SDL_GetHapticFeatures(haptic);
      if (device_->features & SDL_HAPTIC_GAIN) {
        SDL_SetHapticGain(haptic, 100);
      }
      if (device_->features & SDL_HAPTIC_CONSTANT) {
        SDL_HapticEffect effect = ConstantEffect(0);
        device_->constant = SDL_CreateHapticEffect(haptic, &effect);
        if (device_->constant < 0) {
          Log(LogLevel::kWarning,
              std::format("Wheel: cannot create the constant force: {}", SDL_GetError()));
        }
      }
      if (device_->features & SDL_HAPTIC_SPRING) {
        SDL_HapticEffect effect = SpringEffect(0.0);
        device_->spring = SDL_CreateHapticEffect(haptic, &effect);
      }
      haptic_published_.store(haptic);
      haptic_text = std::format(
          "haptic=1 haptic_axes={} max_effects={} max_playing={} features={}",
          SDL_GetNumHapticAxes(haptic), SDL_GetMaxHapticEffects(haptic),
          SDL_GetMaxHapticEffectsPlaying(haptic), HapticFeatureNames(device_->features));
    } else {
      haptic_text = std::format("haptic=0 ({})", SDL_IsJoystickHaptic(joystick)
                                                     ? SDL_GetError()
                                                     : "the joystick reports no force feedback");
    }
  } else {
    haptic_text = "haptic=not opened (sink=record)";
  }
  Log(LogLevel::kInfo,
      std::format("Wheel report: name=\"{}\" type={} gamepad={} vendor={:04x} product={:04x} "
                  "axes={} buttons={} hats={} {} path=\"{}\"",
                  name ? name : "", JoystickTypeName(SDL_GetJoystickType(joystick)),
                  SDL_IsGamepad(chosen) ? 1 : 0, SDL_GetJoystickVendor(joystick),
                  SDL_GetJoystickProduct(joystick), SDL_GetNumJoystickAxes(joystick),
                  SDL_GetNumJoystickButtons(joystick), SDL_GetNumJoystickHats(joystick),
                  haptic_text, path ? path : ""));
  {
    std::lock_guard lock(snapshot_mutex_);
    snapshot_.connected = true;
    snapshot_.name = name ? name : "";
    snapshot_.vendor = SDL_GetJoystickVendor(joystick);
    snapshot_.product = SDL_GetJoystickProduct(joystick);
    snapshot_.is_wheel_type = SDL_GetJoystickType(joystick) == SDL_JOYSTICK_TYPE_WHEEL;
    snapshot_.haptic = device_->haptic != nullptr;
    snapshot_.spring = device_->spring >= 0;
  }
  (void)now_s;
}

void WheelHost::ZeroDevice() {
  if (!device_ || !device_->haptic) {
    return;
  }
  if (device_->constant >= 0 && (device_->constant_running || device_->constant_level != 0)) {
    SDL_HapticEffect effect = ConstantEffect(0);
    SDL_UpdateHapticEffect(device_->haptic, device_->constant, &effect);
    SDL_StopHapticEffect(device_->haptic, device_->constant);
  }
  if (device_->spring >= 0 && device_->spring_running) {
    SDL_StopHapticEffect(device_->haptic, device_->spring);
  }
  device_->constant_running = false;
  device_->spring_running = false;
  device_->constant_level = 0;
  device_->spring_level = 0.0;
}

void WheelHost::CloseDevice() {
  if (!device_) {
    return;
  }
  ZeroDevice();
  if (device_->haptic) {
    haptic_published_.store(nullptr);
    SDL_StopHapticEffects(device_->haptic);
    if (device_->constant >= 0) {
      SDL_DestroyHapticEffect(device_->haptic, device_->constant);
    }
    if (device_->spring >= 0) {
      SDL_DestroyHapticEffect(device_->haptic, device_->spring);
    }
    SDL_CloseHaptic(device_->haptic);
  }
  SDL_CloseJoystick(device_->joystick);
  device_.reset();
  std::lock_guard lock(snapshot_mutex_);
  snapshot_.connected = false;
  snapshot_.haptic = false;
  snapshot_.spring = false;
  snapshot_.axes.clear();
  snapshot_.buttons.clear();
  snapshot_.hats.clear();
}

void WheelHost::Record(double now_s, double constant, double spring, double send_us) {
  if (!record_) {
    return;
  }
  std::fprintf(record_, "%.6f,%.6f,%.6f,%.6f,%.4f,%u,%u,%.1f\n", now_s, governor_.target(),
               constant, spring, governor_.envelope(), governor_.blocked(), governor_.flags(),
               send_us);
}

void WheelHost::SendForces(double now_s, double constant, double spring) {
  const int16_t level = int16_t(std::lround(std::clamp(constant, -1.0, 1.0) * 32767.0));
  const bool blocked = governor_.blocked() != kBlockNone;
  const bool device = options_.sink == Sink::kDevice && device_ && device_->haptic;
  double send_us = 0.0;
  bool sent = false;
  uint64_t sends = 0;
  double cost_sum = 0.0;
  double cost_max = 0.0;

  const auto timed = [&](auto&& call) {
    const uint64_t start = SDL_GetTicksNS();
    call();
    const double us = double(SDL_GetTicksNS() - start) * 1e-3;
    send_us += us;
    ++sends;
    cost_sum += us;
    cost_max = std::max(cost_max, us);
  };

  if (blocked) {
    if (device && (device_->constant_running || device_->spring_running ||
                   device_->constant_level != 0)) {
      timed([&]() { ZeroDevice(); });
      sent = true;
    } else if (!device && (last_sent_level_ != 0 || last_sent_spring_ != 0.0)) {
      sent = true;
    }
    last_sent_level_ = 0;
    last_sent_spring_ = 0.0;
  } else {
    if (level != last_sent_level_) {
      if (device && device_->constant >= 0) {
        timed([&]() {
          SDL_HapticEffect effect = ConstantEffect(options_.invert ? int16_t(-level) : level);
          SDL_UpdateHapticEffect(device_->haptic, device_->constant, &effect);
          if (!device_->constant_running) {
            SDL_RunHapticEffect(device_->haptic, device_->constant, 1);
            device_->constant_running = true;
          }
        });
        device_->constant_level = level;
      }
      last_sent_level_ = level;
      sent = true;
    }
    // The spring's level changes only during its ramp and when it starts or stops.
    if (std::fabs(spring - last_sent_spring_) >= 0.002 || (spring == 0.0) != (last_sent_spring_ == 0.0)) {
      if (device && device_->spring >= 0) {
        timed([&]() {
          SDL_HapticEffect effect = SpringEffect(spring);
          SDL_UpdateHapticEffect(device_->haptic, device_->spring, &effect);
          if (spring > 0.0 && !device_->spring_running) {
            SDL_RunHapticEffect(device_->haptic, device_->spring, 1);
            device_->spring_running = true;
          } else if (spring == 0.0 && device_->spring_running) {
            SDL_StopHapticEffect(device_->haptic, device_->spring);
            device_->spring_running = false;
          }
        });
        device_->spring_level = spring;
      }
      last_sent_spring_ = spring;
      sent = true;
    }
  }

  if (sent || governor_.flags() != last_recorded_flags_ ||
      governor_.blocked() != last_recorded_blocked_) {
    Record(now_s, double(last_sent_level_) / 32767.0, last_sent_spring_, send_us);
    last_recorded_flags_ = governor_.flags();
    last_recorded_blocked_ = governor_.blocked();
  }
  if (sends) {
    send_count_ += sends;
    send_us_sum_ += cost_sum;
    send_us_max_ = std::max(send_us_max_, cost_max);
  }
}

void WheelHost::Run() {
#if defined(_WIN32)
  // DirectInput's effect calls are COM calls.
  const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
  SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH);
  const uint64_t period_ns = 1'000'000'000ull / uint64_t(options_.rate_hz);
  uint64_t next_ns = SDL_GetTicksNS();
  uint64_t last_tick_ns = 0;
  uint64_t last_scan_ns = 0;
  uint64_t last_snapshot_ns = 0;
  uint64_t last_flush_ns = 0;
  uint64_t last_source_ns = 0;
  uint64_t ticks = 0;
  double tick_sum_ms = 0.0;
  double tick_max_ms = 0.0;
  bool had_scan = false;

  while (!stop_.load(std::memory_order_relaxed)) {
    const uint64_t now_ns = SDL_GetTicksNS();
    const double now_s = Seconds(now_ns);
    if (last_tick_ns) {
      const double tick_ms = double(now_ns - last_tick_ns) * 1e-6;
      tick_sum_ms += tick_ms;
      tick_max_ms = std::max(tick_max_ms, tick_ms);
      ++ticks;
    }
    last_tick_ns = now_ns;

    if (emergency_.load()) {
      governor_.SetBlocked(kBlockExit, true);
      governor_.Step(now_s);
      SendForces(now_s, 0.0, 0.0);
      if (record_) {
        std::fflush(record_);
      }
      emergency_done_ = true;
      // Nothing more is sent until the process ends; the thread stays to answer Stop.
      while (!stop_.load(std::memory_order_relaxed)) {
        SDL_DelayNS(period_ns);
      }
      break;
    }

    if (device_ && !SDL_JoystickConnected(device_->joystick)) {
      Log(LogLevel::kInfo, "Wheel: the wheel was disconnected");
      CloseDevice();
    }
    if (!device_ && (!had_scan || now_ns - last_scan_ns >= 1'000'000'000ull)) {
      Scan(now_s);
      had_scan = true;
      last_scan_ns = now_ns;
    }

    const bool has_target =
        options_.sink == Sink::kRecord || (device_ && device_->haptic);
    governor_.SetBlocked(kBlockNoDevice, !has_target);
    const uint32_t external = blocked_.load();
    for (uint32_t reason : {kBlockFocus, kBlockPaused, kBlockExit, kBlockUser}) {
      governor_.SetBlocked(reason, (external & reason) != 0);
    }

    // The source: a test force, refreshed every tick, or the latest submitted value.
    const auto test = TestEffect(test_.load());
    const double amplitude = std::bit_cast<double>(test_amplitude_bits_.load());
    const double test_s = Seconds(now_ns - std::min(now_ns, test_started_ns_.load()));
    double spring_request = 0.0;
    switch (test) {
      case TestEffect::kPulse:
        if (test_s < 1.0) {
          governor_.Submit(amplitude, now_s);
        } else {
          int expected = int(TestEffect::kPulse);
          test_.compare_exchange_strong(expected, int(TestEffect::kNone));
        }
        break;
      case TestEffect::kSpring:
        governor_.Submit(0.0, now_s);
        spring_request = amplitude;
        break;
      case TestEffect::kSine:
        governor_.Submit(amplitude * std::sin(2.0 * std::numbers::pi * test_s), now_s);
        break;
      case TestEffect::kNone: {
        const uint64_t source_ns = source_ns_.load(std::memory_order_acquire);
        if (source_ns != last_source_ns) {
          last_source_ns = source_ns;
          governor_.Submit(std::bit_cast<double>(source_bits_.load(std::memory_order_relaxed)),
                           Seconds(source_ns));
        }
        break;
      }
    }

    const double constant = governor_.Step(now_s);
    const double spring = governor_.ConditionLevel(spring_request);
    SendForces(now_s, constant, spring);

    // A process that is killed keeps what was flushed (the e2e harness ends the game that way).
    if (record_ && now_ns - last_flush_ns >= 250'000'000ull) {
      last_flush_ns = now_ns;
      std::fflush(record_);
    }

    if (now_ns - last_snapshot_ns >= 8'000'000ull) {
      last_snapshot_ns = now_ns;
      std::lock_guard lock(snapshot_mutex_);
      snapshot_.target = governor_.target();
      snapshot_.output = constant;
      snapshot_.spring_level = spring;
      snapshot_.envelope = governor_.envelope();
      snapshot_.blocked = governor_.blocked();
      snapshot_.flags = governor_.flags();
      snapshot_.test = TestEffect(test_.load());
      snapshot_.sends = send_count_;
      snapshot_.send_us_mean = send_count_ ? send_us_sum_ / double(send_count_) : 0.0;
      snapshot_.send_us_max = send_us_max_;
      snapshot_.tick_ms_mean = ticks ? tick_sum_ms / double(ticks) : 0.0;
      snapshot_.tick_ms_max = tick_max_ms;
      if (device_) {
        SDL_Joystick* joystick = device_->joystick;
        snapshot_.axes.resize(size_t(std::max(0, SDL_GetNumJoystickAxes(joystick))));
        for (size_t i = 0; i < snapshot_.axes.size(); ++i) {
          snapshot_.axes[i] = SDL_GetJoystickAxis(joystick, int(i));
        }
        snapshot_.buttons.resize(size_t(std::max(0, SDL_GetNumJoystickButtons(joystick))));
        for (size_t i = 0; i < snapshot_.buttons.size(); ++i) {
          snapshot_.buttons[i] = SDL_GetJoystickButton(joystick, int(i)) ? 1 : 0;
        }
        snapshot_.hats.resize(size_t(std::max(0, SDL_GetNumJoystickHats(joystick))));
        for (size_t i = 0; i < snapshot_.hats.size(); ++i) {
          snapshot_.hats[i] = SDL_GetJoystickHat(joystick, int(i));
        }
      }
    }

    next_ns += period_ns;
    const uint64_t after_ns = SDL_GetTicksNS();
    if (next_ns > after_ns) {
      SDL_DelayNS(next_ns - after_ns);
    } else {
      // Behind by more than a tick: start the schedule again rather than catch up.
      next_ns = after_ns;
    }
  }

  governor_.SetBlocked(kBlockExit, true);
  governor_.Step(Seconds(SDL_GetTicksNS()));
  SendForces(Seconds(SDL_GetTicksNS()), 0.0, 0.0);
  CloseDevice();
  {
    std::lock_guard lock(snapshot_mutex_);
    snapshot_.output = 0.0;
    snapshot_.spring_level = 0.0;
    snapshot_.blocked = governor_.blocked();
    snapshot_.sends = send_count_;
    snapshot_.send_us_mean = send_count_ ? send_us_sum_ / double(send_count_) : 0.0;
    snapshot_.send_us_max = send_us_max_;
    snapshot_.tick_ms_mean = ticks ? tick_sum_ms / double(ticks) : 0.0;
    snapshot_.tick_ms_max = tick_max_ms;
  }
#if defined(_WIN32)
  if (SUCCEEDED(com)) {
    CoUninitialize();
  }
#endif
}

}  // namespace rex::input::wheel
