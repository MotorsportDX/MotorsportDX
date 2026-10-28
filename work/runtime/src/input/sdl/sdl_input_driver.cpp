/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <tuple>

#include <rex/assert.h>
#include <rex/chrono/clock.h>
#include <rex/cvar.h>
#include <rex/input/flags.h>
#include <rex/input/pad_settings.h>
#include <rex/input/sdl/sdl_input_driver.h>
#include <rex/input/wheel/wheel_host.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/ui/virtual_key.h>

#if REX_PLATFORM_WIN32
#include <windows.h>  // NOLINT(build/include_order)
#include <objbase.h>  // NOLINT(build/include_order)
#endif
#if REX_GAMEINPUT_V3
// The Microsoft.GameInput package's MIT header (API v3), which SDL's GameInput driver is built
// with; its loader (GameInputInitialize) is built into SDL.
#include <gameinput.h>  // NOLINT(build/include_order)
#endif

REXCVAR_DEFINE_STRING(hid_mappings_file, "gamecontrollerdb.txt", "Input",
                      "Path to SDL gamecontroller mappings file");
REXCVAR_DEFINE_BOOL(input_latency_probe, false, "Input",
                    "Attach a virtual SDL gamepad that times new pad states until the guest "
                    "reads them; logs a summary every 5 seconds");
REXCVAR_DEFINE_STRING(sdl_joystick_driver, "default", "Input",
                      "SDL's Windows pad drivers: default (SDL's own: XInput for Xbox class pads, "
                      "HIDAPI for PlayStation pads), rawinput (Raw Input with Windows.Gaming.Input "
                      "correlation: trigger rumble on Xbox One and Series pads), wgi "
                      "(Windows.Gaming.Input, with XInput and DirectInput off), gameinput "
                      "(GameInput API v3 before the others: trigger rumble and the Share button "
                      "where GameInput has them; without a GameInput v3 runtime, SDL's others)")
    .allowed({"default", "rawinput", "wgi", "gameinput"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(input_sdl_thread, false, "Input",
                    "Update SDL's pads every millisecond on a thread of their own instead of the "
                    "UI thread's event loop, which a busy UI thread delays")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(pad_misc1, "guide", "Input",
                      "Guest button of SDL's Misc1 (Xbox Series Share, DualSense microphone)")
    .allowed({"a", "b", "x", "y", "lb", "rb", "back", "start", "ls", "rs", "up", "down", "left",
              "right", "guide", "none"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(pad_right_paddle1, "y", "Input",
                      "Guest button of the upper right paddle (Xbox Elite P1)")
    .allowed({"a", "b", "x", "y", "lb", "rb", "back", "start", "ls", "rs", "up", "down", "left",
              "right", "guide", "none"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(pad_left_paddle1, "b", "Input",
                      "Guest button of the upper left paddle (Xbox Elite P3)")
    .allowed({"a", "b", "x", "y", "lb", "rb", "back", "start", "ls", "rs", "up", "down", "left",
              "right", "guide", "none"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(pad_right_paddle2, "x", "Input",
                      "Guest button of the lower right paddle (Xbox Elite P2)")
    .allowed({"a", "b", "x", "y", "lb", "rb", "back", "start", "ls", "rs", "up", "down", "left",
              "right", "guide", "none"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(pad_left_paddle2, "a", "Input",
                      "Guest button of the lower left paddle (Xbox Elite P4)")
    .allowed({"a", "b", "x", "y", "lb", "rb", "back", "start", "ls", "rs", "up", "down", "left",
              "right", "guide", "none"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(wheel, false, "Input",
                    "Racing wheel (stage 0 of docs/analysis/racing-wheels.md): read a wheel "
                    "through SDL's joystick API and run its force feedback host; the game gets no "
                    "wheel input or force yet, so the force stays zero");
REXCVAR_DEFINE_STRING(wheel_name, "", "Input",
                      "Racing wheel: only a joystick whose name contains this (empty: SDL's "
                      "first wheel, else the first joystick that is not a gamepad)");
REXCVAR_DEFINE_STRING(wheel_ffb_sink, "device", "Input",
                      "Racing wheel force feedback: device (the wheel, through SDL's haptic API) "
                      "or record (no force reaches any device; with wheel_ffb_record)");
REXCVAR_DEFINE_STRING(wheel_ffb_record, "", "Input",
                      "Racing wheel force feedback: a CSV file of every force sent (empty: none)");
REXCVAR_DEFINE_DOUBLE(wheel_ffb_cap, 0.25, "Input",
                      "Racing wheel force feedback: the hard upper limit, a fraction of the "
                      "wheel's full force, independent of the gain")
    .range(0.0, 1.0);
REXCVAR_DEFINE_DOUBLE(wheel_ffb_gain, 1.0, "Input",
                      "Racing wheel force feedback: multiplies the game's force before the cap")
    .range(0.0, 4.0);
REXCVAR_DEFINE_BOOL(wheel_ffb_invert, false, "Input",
                    "Racing wheel force feedback: flip the force's direction");
REXCVAR_DEFINE_STRING(pad_touchpad, "guide", "Input",
                      "Guest button of the touchpad click (PlayStation pads)")
    .allowed({"a", "b", "x", "y", "lb", "rb", "back", "start", "ls", "rs", "up", "down", "left",
              "right", "guide", "none"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rex::input::sdl {

namespace {

// SDL clamps to SDL_MAX_RUMBLE_DURATION_MS, which is not a public constant.
constexpr uint32_t kRumbleDurationMs = 0xFFFF;

// The probe's right stick X alternates between 0 and this, far inside any stick deadzone.
constexpr int16_t kProbeValue = 256;
constexpr uint64_t kProbeWindowNs = 5'000'000'000;

// SDL_GamepadButton values up to the touchpad click; newer SDL buttons (Misc2 and on) are
// not mapped.
constexpr uint32_t kSdlButtonCount = SDL_GAMEPAD_BUTTON_TOUCHPAD + 1;

// The guest buttons of SDL's standard buttons, in SDL_GamepadButton order.
constexpr std::array<uint16_t, 15> kStandardButtons = {
    X_INPUT_GAMEPAD_A,          X_INPUT_GAMEPAD_B,
    X_INPUT_GAMEPAD_X,          X_INPUT_GAMEPAD_Y,
    X_INPUT_GAMEPAD_BACK,       X_INPUT_GAMEPAD_GUIDE,
    X_INPUT_GAMEPAD_START,      X_INPUT_GAMEPAD_LEFT_THUMB,
    X_INPUT_GAMEPAD_RIGHT_THUMB, X_INPUT_GAMEPAD_LEFT_SHOULDER,
    X_INPUT_GAMEPAD_RIGHT_SHOULDER, X_INPUT_GAMEPAD_DPAD_UP,
    X_INPUT_GAMEPAD_DPAD_DOWN,  X_INPUT_GAMEPAD_DPAD_LEFT,
    X_INPUT_GAMEPAD_DPAD_RIGHT,
};
static_assert(SDL_GAMEPAD_BUTTON_SOUTH == 0);
static_assert(SDL_GAMEPAD_BUTTON_DPAD_RIGHT == 14);
static_assert(SDL_GAMEPAD_BUTTON_MISC1 == 15 && SDL_GAMEPAD_BUTTON_TOUCHPAD == 20);

// A guest button by its cvar name (pad_misc1 and the paddles).
uint16_t GuestButtonNamed(const std::string& name) {
  static const std::array<std::pair<const char*, uint16_t>, 15> kNames = {{
      {"a", X_INPUT_GAMEPAD_A},
      {"b", X_INPUT_GAMEPAD_B},
      {"x", X_INPUT_GAMEPAD_X},
      {"y", X_INPUT_GAMEPAD_Y},
      {"lb", X_INPUT_GAMEPAD_LEFT_SHOULDER},
      {"rb", X_INPUT_GAMEPAD_RIGHT_SHOULDER},
      {"back", X_INPUT_GAMEPAD_BACK},
      {"start", X_INPUT_GAMEPAD_START},
      {"ls", X_INPUT_GAMEPAD_LEFT_THUMB},
      {"rs", X_INPUT_GAMEPAD_RIGHT_THUMB},
      {"up", X_INPUT_GAMEPAD_DPAD_UP},
      {"down", X_INPUT_GAMEPAD_DPAD_DOWN},
      {"left", X_INPUT_GAMEPAD_DPAD_LEFT},
      {"right", X_INPUT_GAMEPAD_DPAD_RIGHT},
      {"guide", X_INPUT_GAMEPAD_GUIDE},
  }};
  for (const auto& [text, button] : kNames) {
    if (name == text) {
      return button;
    }
  }
  return 0;
}

// The SDL driver that opened a joystick: byte 14 of its GUID (SDL_joystick.c, the
// SDL_IsJoystick* helpers); DirectInput GUIDs have 0 there.
std::string JoystickDriverName(const SDL_GUID& guid) {
  switch (guid.data[14]) {
    case 'x':
      return "xinput";
    case 'r':
      return "rawinput";
    case 'w':
      return "wgi";
    case 'g':
      return "gameinput";
    case 'h':
      return "hidapi";
    case 'v':
      return "virtual";
    case 'm':
      return "mfi";
    case 0:
      return "dinput";
    default:
      return fmt::format("0x{:02X}", guid.data[14]);
  }
}

const char* ConnectionName(SDL_JoystickConnectionState state) {
  switch (state) {
    case SDL_JOYSTICK_CONNECTION_WIRED:
      return "wired";
    case SDL_JOYSTICK_CONNECTION_WIRELESS:
      return "wireless";
    case SDL_JOYSTICK_CONNECTION_UNKNOWN:
      return "unknown";
    default:
      return "invalid";
  }
}

const char* PowerName(SDL_PowerState state) {
  switch (state) {
    case SDL_POWERSTATE_ON_BATTERY:
      return "battery";
    case SDL_POWERSTATE_NO_BATTERY:
      return "no_battery";
    case SDL_POWERSTATE_CHARGING:
      return "charging";
    case SDL_POWERSTATE_CHARGED:
      return "charged";
    case SDL_POWERSTATE_UNKNOWN:
      return "unknown";
    default:
      return "error";
  }
}

}  // namespace

// input_latency_probe: a virtual SDL gamepad stands in for a pad whose reports arrive at
// random times. A thread of its own flips the right stick X every 20 to 49 ms; SDL sends the
// new value on its next joystick update (normally the UI thread's event loop, which polls
// every millisecond while a joystick is open), and the guest sees it on its next state read.
// Per window it logs the gaps between joystick updates, the wait from a new value to the
// update that sent it, from that update to the guest's read, the whole age at the read, and
// the gaps between the guest's reads of this pad. Diagnostic only, off by default.
struct SDLInputDriver::LatencyProbe {
  SDL_JoystickID id = 0;
  SDL_Joystick* joystick = nullptr;
  std::thread thread;
  std::atomic<bool> stop{false};
  // When each of the two values was last set, by value.
  std::atomic<uint64_t> set_ns[2] = {};
  std::atomic<uint64_t> last_update_ns{0};
  std::mutex mutex;
  std::vector<double> update_gaps, pump_waits, read_waits, ages, read_gaps;
  uint64_t last_read_ns = 0;
  uint64_t window_start_ns = 0;
  // What the driver sent to the virtual pad (its Rumble and RumbleTriggers callbacks; SDL
  // calls them only when the values change): changes in the window, the largest values, and
  // the last ones, each as left << 16 | right.
  uint32_t rumble_changes = 0, trigger_changes = 0;
  uint32_t rumble_max_left = 0, rumble_max_right = 0, trigger_max_left = 0,
           trigger_max_right = 0;
  uint32_t rumble_last = 0, trigger_last = 0;

  static void SDLCALL Update(void* userdata) {
    auto* probe = static_cast<LatencyProbe*>(userdata);
    const uint64_t now = SDL_GetTicksNS();
    const uint64_t last = probe->last_update_ns.exchange(now);
    if (last) {
      std::lock_guard lock(probe->mutex);
      if (probe->update_gaps.size() < 100000) {
        probe->update_gaps.push_back(double(now - last) / 1e6);
      }
    }
  }

  static bool SDLCALL Rumble(void* userdata, Uint16 low, Uint16 high) {
    auto* probe = static_cast<LatencyProbe*>(userdata);
    std::lock_guard lock(probe->mutex);
    ++probe->rumble_changes;
    probe->rumble_max_left = std::max<uint32_t>(probe->rumble_max_left, low);
    probe->rumble_max_right = std::max<uint32_t>(probe->rumble_max_right, high);
    probe->rumble_last = uint32_t(low) << 16 | high;
    return true;
  }

  static bool SDLCALL RumbleTriggers(void* userdata, Uint16 left, Uint16 right) {
    auto* probe = static_cast<LatencyProbe*>(userdata);
    std::lock_guard lock(probe->mutex);
    ++probe->trigger_changes;
    probe->trigger_max_left = std::max<uint32_t>(probe->trigger_max_left, left);
    probe->trigger_max_right = std::max<uint32_t>(probe->trigger_max_right, right);
    probe->trigger_last = uint32_t(left) << 16 | right;
    return true;
  }

  // The rumble part of a summary; clears the window's counts and maxima. Under mutex.
  std::string RumbleSummary() {
    std::string text = fmt::format(
        "rumble changes={} max={},{} last={},{}; trigger rumble changes={} max={},{} last={},{}",
        rumble_changes, rumble_max_left, rumble_max_right, rumble_last >> 16, rumble_last & 0xFFFF,
        trigger_changes, trigger_max_left, trigger_max_right, trigger_last >> 16,
        trigger_last & 0xFFFF);
    rumble_changes = trigger_changes = 0;
    rumble_max_left = rumble_max_right = trigger_max_left = trigger_max_right = 0;
    return text;
  }

  static std::string Summary(std::vector<double>& values) {
    if (values.empty()) {
      return "none";
    }
    std::sort(values.begin(), values.end());
    double sum = 0;
    for (double value : values) {
      sum += value;
    }
    auto at = [&](double share) {
      return values[std::min(values.size() - 1, size_t(share * double(values.size())))];
    };
    return fmt::format("n={} mean={:.2f} p50={:.2f} p99={:.2f} max={:.2f}", values.size(),
                       sum / double(values.size()), at(0.5), at(0.99), values.back());
  }
};

SDLInputDriver::SDLInputDriver(rex::ui::Window* window, size_t window_z_order)
    : InputDriver(window, window_z_order),
      sdl_events_initialized_(false),
      SDL_Gamepad_initialized_(false),
      sdl_events_unflushed_(0),
      sdl_pumpevents_queued_(false),
      controllers_(),
      controllers_mutex_() {}

SDLInputDriver::~SDLInputDriver() {}

X_STATUS SDLInputDriver::Setup() {
  if (!TestSDLVersion()) {
    return X_STATUS_UNSUCCESSFUL;
  }

  // In SDL_GamepadButton order from Misc1.
  extra_buttons_ = {GuestButtonNamed(REXCVAR_GET(pad_misc1)),
                    GuestButtonNamed(REXCVAR_GET(pad_right_paddle1)),
                    GuestButtonNamed(REXCVAR_GET(pad_left_paddle1)),
                    GuestButtonNamed(REXCVAR_GET(pad_right_paddle2)),
                    GuestButtonNamed(REXCVAR_GET(pad_left_paddle2)),
                    GuestButtonNamed(REXCVAR_GET(pad_touchpad))};
  return X_STATUS_SUCCESS;
}

void SDLInputDriver::ApplyDriverHints() {
  // Set before SDL_INIT_GAMEPAD: the drivers read them as they start
  // (docs/analysis/modern-gamepads.md, "SDL drivers on Windows").
  const std::string mode = REXCVAR_GET(sdl_joystick_driver);
  if (mode == "rawinput") {
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "1");
  } else if (mode == "wgi") {
    // With XInput on, WGI leaves XInput devices to it; with DirectInput on, DirectInput
    // would claim them first.
    SDL_SetHint(SDL_HINT_JOYSTICK_WGI, "1");
    SDL_SetHint(SDL_HINT_XINPUT_ENABLED, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_DIRECTINPUT, "0");
  } else if (mode == "gameinput") {
    // SDL's GameInput driver comes before the others. When it cannot start, the others take
    // the pads as with default; when it starts, SDL leaves the pads it claims to it.
    SDL_SetHint(SDL_HINT_JOYSTICK_GAMEINPUT, "1");
  }
  REXLOG_INFO("SDL joystick driver option: {}", mode);
  if (mode == "gameinput") {
    LogGameInputAvailability();
  }
}

void SDLInputDriver::LogGameInputAvailability() {
#if REX_GAMEINPUT_V3
  // The same creation SDL's GameInput driver makes (the package's loader picks the newest
  // runtime DLL), so its result tells whether SDL's driver starts.
  IUnknown* game_input = nullptr;
  const HRESULT result = GameInput::v3::GameInputInitialize(GameInput::v3::IID_IGameInput,
                                                             reinterpret_cast<void**>(&game_input));
  if (SUCCEEDED(result)) {
    game_input->Release();
    REXLOG_INFO("SDL GameInput: a GameInput v3 runtime is available; GameInput takes the pads it "
                "supports");
  } else {
    REXLOG_INFO("SDL GameInput: no GameInput v3 runtime (HRESULT 0x{:08X}{}); SDL's other drivers "
                "take the pads",
                static_cast<uint32_t>(result),
                result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ? ", GameInput is not installed"
                : result == E_NOINTERFACE ? ", the installed GameInput is older than v3"
                                          : "");
  }
#else
  REXLOG_INFO("SDL GameInput: this runtime is built without the GameInput v3 header (setup "
              "fetches it); SDL's other drivers take the pads");
#endif
}

void SDLInputDriver::OnWindowAvailable(rex::ui::Window* window) {
  if (window && !attached_window_) {
    attached_window_ = window;
    window->AddListener(this);
    window->app_context().CallInUIThreadSynchronous([this]() {
      // Initialize SDL events subsystem
      if (!SDL_InitSubSystem(SDL_INIT_EVENTS)) {
        REXLOG_ERROR("SDL: Failed to init events subsystem: {}", SDL_GetError());
        return;
      }
      sdl_events_initialized_ = true;
      pending_events_.reserve(64);

      // With an event watch we will always get notified, even if the event queue
      // is full, which can happen if another subsystem does not clear its events.
      SDL_AddEventWatch(
          [](void* userdata, SDL_Event* event) -> bool {
            if (!userdata || !event) {
              assert_always();
              return false;
            }

            const auto type = event->type;
            if (type < SDL_EVENT_JOYSTICK_AXIS_MOTION || type >= SDL_EVENT_FINGER_DOWN) {
              return false;
            }

            // If another part of rex uses another SDL subsystem that generates
            // events, this may seem like a bad idea. They will however not
            // subscribe to controller events so we get away with that.
            const auto driver = static_cast<SDLInputDriver*>(userdata);
            driver->HandleEvent(*event);

            return false;
          },
          this);

      // SDL drops pad events while none of its windows has keyboard focus, and a game
      // started by the e2e harness need not have it.
      if (REXCVAR_GET(input_latency_probe)) {
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        background_events_ = true;
      }
      ApplyDriverHints();
      has_focus_ = attached_window_->HasFocus();

      // Initialize game controller subsystem
      if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        REXLOG_ERROR("SDL: Failed to init gamecontroller subsystem: {}", SDL_GetError());
        return;
      }
      SDL_Gamepad_initialized_ = true;

      // Load custom controller mappings if available
      if (!REXCVAR_GET(hid_mappings_file).empty()) {
        std::filesystem::path mappings_path(REXCVAR_GET(hid_mappings_file));
        if (!std::filesystem::exists(mappings_path)) {
          // The default name is optional (SDL's built-in mappings cover the common pads);
          // only a file the user named is worth a warning.
          if (rex::cvar::GetFlagSource("hid_mappings_file") == rex::cvar::Source::kDefault) {
            REXLOG_INFO("SDL GameControllerDB: no '{}'; SDL's built-in mappings apply.",
                        REXCVAR_GET(hid_mappings_file));
          } else {
            REXLOG_WARN("SDL GameControllerDB: file '{}' does not exist.",
                        REXCVAR_GET(hid_mappings_file));
          }
        } else {
          auto mappings_result =
              SDL_AddGamepadMappingsFromFile(REXCVAR_GET(hid_mappings_file).c_str());
          if (mappings_result < 0) {
            REXLOG_ERROR("SDL GameControllerDB: error loading file '{}': {}.",
                         REXCVAR_GET(hid_mappings_file), mappings_result);
          } else {
            REXLOG_INFO("SDL GameControllerDB: loaded {} mappings.", mappings_result);
          }
        }
      }
      if (REXCVAR_GET(input_latency_probe)) {
        StartLatencyProbe();
      }
      if (REXCVAR_GET(wheel)) {
        StartWheel();
      }
      if (REXCVAR_GET(input_sdl_thread)) {
        StartInputThread();
      }
      REXLOG_INFO("SDL input driver initialized successfully");
    });
  }
}

void SDLInputDriver::OnClosing(rex::ui::UIEvent&) {
  if (attached_window_) {
    attached_window_->RemoveListener(this);
    if (sdl_pumpevents_queued_) {
      attached_window_->app_context().CallInUIThreadSynchronous(
          [this]() { attached_window_->app_context().ExecutePendingFunctionsFromUIThread(); });
    }
    // The thread updates SDL's pads, so it ends before they close.
    StopInputThread();
    {
      auto guard = DrainAndLock();
      StopTriggerRumbleLocked();
    }
    StopLatencyProbe();
    StopWheel();
    for (auto& controller : controllers_) {
      SDL_CloseGamepad(controller.sdl);
    }
    controllers_.clear();
    if (SDL_Gamepad_initialized_) {
      SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
      SDL_Gamepad_initialized_ = false;
    }
    if (sdl_events_initialized_) {
      SDL_QuitSubSystem(SDL_INIT_EVENTS);
      sdl_events_initialized_ = false;
    }
    attached_window_ = nullptr;
  }
}

void SDLInputDriver::OnLostFocus(rex::ui::UISetupEvent&) {
  has_focus_ = false;
  if (wheel_) {
    wheel_->SetFocus(false);
  }
  if (background_events_) {
    REXLOG_INFO("SDL input: the window lost focus; pads are still read");
    return;
  }
  REXLOG_INFO("SDL input: the window lost focus; trigger rumble stops");
  if (SDL_Gamepad_initialized_) {
    auto guard = DrainAndLock();
    StopTriggerRumbleLocked();
  }
}

void SDLInputDriver::OnGotFocus(rex::ui::UISetupEvent&) {
  has_focus_ = true;
  if (wheel_) {
    wheel_->SetFocus(true);
  }
  REXLOG_INFO("SDL input: the window has focus");
}

void SDLInputDriver::StopVibration() {
  vibration_stopped_ = true;
  if (wheel_) {
    // The app hard-exits after this (std::_Exit: no atexit, no OnClosing), so the host zeroes
    // the force and closes the wheel here, on its own thread, before returning.
    wheel_->Stop();
  }
  if (!SDL_Gamepad_initialized_) {
    return;
  }
  // The guest still runs (ReXApp::OnClosing calls this before TerminateTitle) and holds the
  // lock only briefly; the wait is bounded anyway, since the process exits right after.
  std::unique_lock<std::mutex> lock(controllers_mutex_, std::defer_lock);
  for (int attempt = 0; attempt < 100 && !lock.try_lock(); ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (!lock.owns_lock()) {
    REXLOG_WARN("SDL input: the pads are busy; rumble not stopped at exit");
    return;
  }
  for (auto& controller : controllers_) {
    SDL_RumbleGamepad(controller.sdl, 0, 0, 0);
    if (controller.trigger_rumble_sent) {
      SDL_RumbleGamepadTriggers(controller.sdl, 0, 0, 0);
      controller.trigger_rumble_sent = false;
    }
  }
  REXLOG_INFO("SDL input: rumble stopped at exit");
  if (probe_) {
    std::lock_guard probe_lock(probe_->mutex);
    REXLOG_INFO("SDL latency probe at exit: {}", probe_->RumbleSummary());
  }
}

void SDLInputDriver::StartWheel() {
  namespace wheel = rex::input::wheel;
  if (!SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC)) {
    REXLOG_ERROR("Wheel: SDL's joystick and haptic subsystems did not start: {}", SDL_GetError());
    return;
  }
  wheel_subsystems_ = true;
  wheel::HostOptions options;
  options.governor.cap = REXCVAR_GET(wheel_ffb_cap);
  options.governor.gain = REXCVAR_GET(wheel_ffb_gain);
  options.invert = REXCVAR_GET(wheel_ffb_invert);
  options.sink = REXCVAR_GET(wheel_ffb_sink) == "record" ? wheel::Sink::kRecord
                                                          : wheel::Sink::kDevice;
  options.record_path = REXCVAR_GET(wheel_ffb_record);
  options.name_filter = REXCVAR_GET(wheel_name);
  wheel_ = std::make_unique<wheel::WheelHost>(
      options, [](wheel::LogLevel level, std::string_view text) {
        switch (level) {
          case wheel::LogLevel::kInfo: REXLOG_INFO("{}", text); break;
          case wheel::LogLevel::kWarning: REXLOG_WARN("{}", text); break;
          case wheel::LogLevel::kError: REXLOG_ERROR("{}", text); break;
        }
      });
  wheel_->SetFocus(has_focus_);
  wheel_->Start();
}

void SDLInputDriver::StopWheel() {
  if (wheel_) {
    wheel_->Stop();
    wheel_.reset();
  }
  if (wheel_subsystems_) {
    SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC);
    wheel_subsystems_ = false;
  }
}

void SDLInputDriver::StopTriggerRumbleLocked() {
  for (auto& controller : controllers_) {
    if (controller.trigger_rumble_sent) {
      SDL_RumbleGamepadTriggers(controller.sdl, 0, 0, 0);
      controller.trigger_rumble_sent = false;
    }
  }
}

void SDLInputDriver::EnumerateDevices(std::vector<DeviceInfo>& out) {
  // Polling can start before the window exists and the subsystems are up.
  if (!sdl_events_initialized_ || !SDL_Gamepad_initialized_) {
    return;
  }

  auto guard = DrainAndLock();

  for (const auto& controller : controllers_) {
    DeviceInfo info;
    info.id = controller.id;
    const char* name = SDL_GetGamepadName(controller.sdl);
    info.name = name ? name : "";
    char guid_text[33] = {};
    SDL_GUIDToString(SDL_GetJoystickGUID(SDL_GetGamepadJoystick(controller.sdl)), guid_text,
                     static_cast<int>(sizeof(guid_text)));
    info.guid = guid_text;
    info.synthetic = false;
    out.push_back(info);
  }
}

X_RESULT SDLInputDriver::GetDeviceCapabilities(DeviceId id, uint32_t flags,
                                               X_INPUT_CAPABILITIES* out_caps) {
  assert(sdl_events_initialized_ && SDL_Gamepad_initialized_);
  if (!out_caps) {
    return X_ERROR_BAD_ARGUMENTS;
  }

  QueueControllerUpdate();

  auto guard = DrainAndLock();

  auto controller = FindController(id);
  if (!controller) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  // Unfortunately drivers can't present all information immediately (e.g.
  // battery information) so this needs to be refreshed every time.
  UpdateXCapabilities(*controller);

  std::memcpy(out_caps, &controller->caps, sizeof(*out_caps));

  return X_ERROR_SUCCESS;
}

X_RESULT SDLInputDriver::GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) {
  assert(sdl_events_initialized_ && SDL_Gamepad_initialized_);

  auto is_active = this->is_active();

  if (is_active) {
    QueueControllerUpdate();
  }

  auto guard = DrainAndLock();

  auto controller = FindController(id);
  if (!controller) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  if (controller->probe) {
    ObserveProbeReadLocked(*controller);
  }

  // Make sure packet_number is only incremented by 1, even if there have been
  // multiple updates between GetState calls. Also track `is_active` to
  // increment the packet number if it changed.
  if ((is_active != controller->is_active) || (is_active && controller->state_changed)) {
    controller->state.packet_number++;
    controller->is_active = is_active;
    controller->state_changed = false;
  }
  std::memcpy(out_state, &controller->state, sizeof(*out_state));
  if (!is_active) {
    // Simulate an "untouched" controller. When we become active again the
    // pressed buttons aren't lost and will be visible again.
    std::memset(&out_state->gamepad, 0, sizeof(out_state->gamepad));
  } else {
    ShapeGamepad(out_state->gamepad, CurrentPadSettings());
  }
  return X_ERROR_SUCCESS;
}

X_RESULT SDLInputDriver::SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) {
  assert(sdl_events_initialized_ && SDL_Gamepad_initialized_);

  if (vibration_stopped_) {
    return X_ERROR_SUCCESS;
  }

  QueueControllerUpdate();

  auto guard = DrainAndLock();

  // Again under the lock: StopVibration may have run while this call waited for it.
  if (vibration_stopped_) {
    return X_ERROR_SUCCESS;
  }
  auto controller = FindController(id);
  if (!controller) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  const PadSettings settings = CurrentPadSettings();
  const uint16_t left_motor = vibration->left_motor_speed;
  const uint16_t right_motor = vibration->right_motor_speed;

  // Trigger rumble (pad_trigger_rumble) from the guest's motors and the triggers it read.
  // It is zero while the motors are, while an overlay takes the input (is_active), and with
  // the option off; also without focus, where SDL stops reading the pads and the triggers'
  // last values would go stale (unless background events are on, as for the probe). Nothing
  // is sent to a pad that never had it.
  if (controller->trigger_rumble_capable &&
      (settings.trigger_rumble || controller->trigger_rumble_sent)) {
    uint16_t left_trigger = 0;
    uint16_t right_trigger = 0;
    if (settings.trigger_rumble && (has_focus_ || background_events_) && is_active()) {
      std::tie(left_trigger, right_trigger) = DeriveTriggerRumble(
          left_motor, right_motor, controller->user_left_trigger, controller->user_right_trigger,
          settings.trigger_rumble_strength);
    }
    SDL_RumbleGamepadTriggers(controller->sdl, left_trigger, right_trigger, kRumbleDurationMs);
    controller->trigger_rumble_sent = left_trigger || right_trigger;
  }

  // XInput vibration holds until the guest changes it, but SDL rumble expires,
  // and a zero duration expires on the next SDL_UpdateJoysticks. Arm it for
  // SDL's maximum instead; each call cancels the previous effect anyway.
  return SDL_RumbleGamepad(controller->sdl, ScaleMotor(left_motor, settings.rumble_strength),
                           ScaleMotor(right_motor, settings.rumble_strength), kRumbleDurationMs)
             ? X_ERROR_SUCCESS
             : X_ERROR_FUNCTION_FAILED;
}

void SDLInputDriver::SetDeviceUserState(DeviceId id, const X_INPUT_GAMEPAD& gamepad) {
  auto guard = DrainAndLock();
  auto controller = FindController(id);
  if (controller) {
    controller->user_left_trigger = gamepad.left_trigger;
    controller->user_right_trigger = gamepad.right_trigger;
  }
}

X_RESULT SDLInputDriver::GetDeviceKeystroke(DeviceId id, uint32_t flags,
                                            X_INPUT_KEYSTROKE* out_keystroke) {
  // TODO(JoelLinn): Figure out the flags
  // https://github.com/evilC/UCR/blob/0489929e2a8e39caa3484c67f3993d3fba39e46f/Libraries/XInput.ahk#L85-L98
  assert(sdl_events_initialized_ && SDL_Gamepad_initialized_);
  if (!out_keystroke) {
    return X_ERROR_BAD_ARGUMENTS;
  }

  // The order of this list is also the order in which events are send if
  // multiple buttons change at once.
  static_assert(sizeof(X_INPUT_GAMEPAD::buttons) == 2);
  static constexpr std::array<rex::ui::VirtualKey, 34> kVkLookup = {
      // 00 - True buttons from xinput button field
      rex::ui::VirtualKey::kXInputPadDpadUp,
      rex::ui::VirtualKey::kXInputPadDpadDown,
      rex::ui::VirtualKey::kXInputPadDpadLeft,
      rex::ui::VirtualKey::kXInputPadDpadRight,
      rex::ui::VirtualKey::kXInputPadStart,
      rex::ui::VirtualKey::kXInputPadBack,
      rex::ui::VirtualKey::kXInputPadLThumbPress,
      rex::ui::VirtualKey::kXInputPadRThumbPress,
      rex::ui::VirtualKey::kXInputPadLShoulder,
      rex::ui::VirtualKey::kXInputPadRShoulder,
      rex::ui::VirtualKey::kNone, /* Guide has no VK */
      rex::ui::VirtualKey::kNone, /* Unknown */
      rex::ui::VirtualKey::kXInputPadA,
      rex::ui::VirtualKey::kXInputPadB,
      rex::ui::VirtualKey::kXInputPadX,
      rex::ui::VirtualKey::kXInputPadY,
      // 16 - Fake buttons generated from analog inputs
      rex::ui::VirtualKey::kXInputPadLTrigger,
      rex::ui::VirtualKey::kXInputPadRTrigger,
      // 18
      rex::ui::VirtualKey::kXInputPadLThumbUp,
      rex::ui::VirtualKey::kXInputPadLThumbDown,
      rex::ui::VirtualKey::kXInputPadLThumbRight,
      rex::ui::VirtualKey::kXInputPadLThumbLeft,
      rex::ui::VirtualKey::kXInputPadLThumbUpLeft,
      rex::ui::VirtualKey::kXInputPadLThumbUpRight,
      rex::ui::VirtualKey::kXInputPadLThumbDownRight,
      rex::ui::VirtualKey::kXInputPadLThumbDownLeft,
      // 26
      rex::ui::VirtualKey::kXInputPadRThumbUp,
      rex::ui::VirtualKey::kXInputPadRThumbDown,
      rex::ui::VirtualKey::kXInputPadRThumbRight,
      rex::ui::VirtualKey::kXInputPadRThumbLeft,
      rex::ui::VirtualKey::kXInputPadRThumbUpLeft,
      rex::ui::VirtualKey::kXInputPadRThumbUpRight,
      rex::ui::VirtualKey::kXInputPadRThumbDownRight,
      rex::ui::VirtualKey::kXInputPadRThumbDownLeft,
  };

  auto is_active = this->is_active();

  if (is_active) {
    QueueControllerUpdate();
  }

  auto guard = DrainAndLock();

  auto controller = FindController(id);
  if (!controller) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  // If input is not active (e.g. due to a dialog overlay), force buttons to
  // "unpressed". The algorithm will automatically send UP events when
  // 'is_active()' goes low and DOWN events when it goes high again.
  const uint64_t curr_butts =
      is_active ? (static_cast<uint64_t>(static_cast<uint16_t>(controller->state.gamepad.buttons)) |
                   AnalogToKeyfield(controller->state.gamepad))
                : uint64_t(0);
  KeystrokeState& last = controller->keystroke;

  // Handle repeating
  auto guest_now = rex::chrono::Clock::QueryGuestUptimeMillis();
  static_assert(HID_SDL_REPEAT_DELAY >= HID_SDL_REPEAT_RATE);
  if (last.repeat_state == RepeatState::Waiting &&
      (last.repeat_time + HID_SDL_REPEAT_DELAY < guest_now)) {
    last.repeat_state = RepeatState::Repeating;
  }
  if (last.repeat_state == RepeatState::Repeating &&
      (last.repeat_time + HID_SDL_REPEAT_RATE < guest_now)) {
    last.repeat_time = guest_now;
    rex::ui::VirtualKey vk = kVkLookup.at(last.repeat_butt_idx);
    assert_true(vk != rex::ui::VirtualKey::kNone);
    out_keystroke->virtual_key = uint16_t(vk);
    out_keystroke->unicode = 0;
    // InputSystem stamps the guest user this device is assigned to.
    out_keystroke->user_index = 0;
    out_keystroke->hid_code = 0;
    out_keystroke->flags = X_INPUT_KEYSTROKE_KEYDOWN | X_INPUT_KEYSTROKE_REPEAT;
    return X_ERROR_SUCCESS;
  }

  auto butts_changed = curr_butts ^ last.buttons;
  if (!butts_changed) {
    return X_ERROR_EMPTY;
  }

  // First try to clear buttons with up events. This is to match xinput
  // behavior when transitioning thumb sticks, e.g. so that THUMB_UPLEFT is
  // up before THUMB_LEFT is down.
  for (auto [clear_pass, pass] = std::tuple{true, 0}; pass < 2; clear_pass = false, pass++) {
    for (uint8_t i = 0; i < uint8_t(std::size(kVkLookup)); i++) {
      auto fbutton = uint64_t(1) << i;
      if (!(butts_changed & fbutton)) {
        continue;
      }
      rex::ui::VirtualKey vk = kVkLookup.at(i);
      if (vk == rex::ui::VirtualKey::kNone) {
        continue;
      }

      out_keystroke->virtual_key = uint16_t(vk);
      out_keystroke->unicode = 0;
      out_keystroke->user_index = 0;
      out_keystroke->hid_code = 0;

      bool is_pressed = curr_butts & fbutton;
      if (clear_pass && !is_pressed) {
        // up
        out_keystroke->flags = X_INPUT_KEYSTROKE_KEYUP;
        last.buttons &= ~fbutton;
        last.repeat_state = RepeatState::Idle;
        return X_ERROR_SUCCESS;
      }
      if (!clear_pass && is_pressed) {
        // down
        out_keystroke->flags = X_INPUT_KEYSTROKE_KEYDOWN;
        last.buttons |= fbutton;
        last.repeat_state = RepeatState::Waiting;
        last.repeat_butt_idx = i;
        last.repeat_time = guest_now;
        return X_ERROR_SUCCESS;
      }
    }
  }
  return X_ERROR_EMPTY;
}

void SDLInputDriver::HandleEvent(const SDL_Event& event) {
  // This callback will likely run on the thread that posts the event, which
  // may be a dedicated thread SDL has created for the joystick subsystem.

  // Event queue should never be (this) full
  assert(SDL_PeepEvents(nullptr, 0, SDL_PEEKEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST) < 0xFFFF);

  // The queue could grow up to 3.5MB since it is never polled.
  if (++sdl_events_unflushed_ > 64) {
    SDL_FlushEvents(SDL_EVENT_JOYSTICK_AXIS_MOTION, SDL_EVENT_FINGER_DOWN - 1);
    sdl_events_unflushed_ = 0;
  }

  // Buffer only - no controllers_mutex_ acquisition here.
  // This breaks the lock ordering inversion between controllers_mutex_ and
  // SDL's internal joystick lock that caused deadlocks.
  std::lock_guard<std::mutex> guard(event_queue_mutex_);
  pending_events_.push_back(event);
}

std::unique_lock<std::mutex> SDLInputDriver::DrainAndLock() {
  std::vector<SDL_Event> events;
  {
    std::lock_guard<std::mutex> guard(event_queue_mutex_);
    events.swap(pending_events_);
  }
  std::unique_lock<std::mutex> guard(controllers_mutex_);
  for (const auto& event : events) {
    ProcessEventLocked(event);
  }
  return guard;
}

void SDLInputDriver::ProcessEventLocked(const SDL_Event& event) {
  switch (event.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
      OnControllerDeviceAddedLocked(event);
      break;
    case SDL_EVENT_GAMEPAD_REMOVED:
      OnControllerDeviceRemovedLocked(event);
      break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
      OnControllerDeviceAxisMotionLocked(event);
      break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
      OnControllerDeviceButtonChangedLocked(event);
      break;
    default:
      break;
  }
}

void SDLInputDriver::OnControllerDeviceAddedLocked(const SDL_Event& event) {
  const auto controller = SDL_OpenGamepad(event.gdevice.which);
  if (!controller) {
    assert_always();
    return;
  }
  REXLOG_INFO(
      "SDL OnControllerDeviceAdded: \"{}\", "
      "JoystickType({}), "
      "GameControllerType({}), "
      "VendorID(0x{:04X}), "
      "ProductID(0x{:04X})",
      SDL_GetGamepadName(controller),
      static_cast<int>(SDL_GetJoystickType(SDL_GetGamepadJoystick(controller))),
      static_cast<int>(SDL_GetGamepadType(controller)), SDL_GetGamepadVendor(controller),
      SDL_GetGamepadProduct(controller));

  // SDL_GetGamepadPlayerIndex is deliberately ignored: on Windows it reports
  // the XInput user index the OS assigned, which is unrelated to connection
  // order and leaves non-XInput pads unnumbered.
  ControllerState state = {};
  state.sdl = controller;
  state.id = AllocateDeviceId();
  state.probe = probe_ && event.gdevice.which == probe_->id;
  state.trigger_rumble_capable = SDL_GetBooleanProperty(
      SDL_GetGamepadProperties(controller), SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN, false);
  state.state_changed = true;  // XInput starts with packet_number = 1
  UpdateXCapabilities(state);
  controllers_.push_back(state);

  const int ordinal = static_cast<int>(controllers_.size()) - 1;
  SDL_SetGamepadPlayerIndex(controller, ordinal);

  REXLOG_INFO("SDL OnControllerDeviceAdded: connection order {}, device {}.", ordinal,
              static_cast<uint64_t>(state.id));
  LogPadReport(state);
}

void SDLInputDriver::LogPadReport(const ControllerState& state) const {
  // One line per pad for the owner's logs (docs/running.md): what SDL exposes of it under
  // the chosen sdl_joystick_driver.
  SDL_Gamepad* gamepad = state.sdl;
  SDL_Joystick* joystick = SDL_GetGamepadJoystick(gamepad);
  const SDL_GUID guid = SDL_GetJoystickGUID(joystick);
  char guid_text[33] = {};
  SDL_GUIDToString(guid, guid_text, static_cast<int>(sizeof(guid_text)));
  const SDL_PropertiesID properties = SDL_GetGamepadProperties(gamepad);
  auto capability = [&](const char* name) {
    return SDL_GetBooleanProperty(properties, name, false) ? 1 : 0;
  };
  int paddles = 0;
  for (auto button : {SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1,
                      SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, SDL_GAMEPAD_BUTTON_LEFT_PADDLE2}) {
    paddles += SDL_GamepadHasButton(gamepad, button) ? 1 : 0;
  }
  int battery = -1;
  const SDL_PowerState power = SDL_GetGamepadPowerInfo(gamepad, &battery);
  const char* name = SDL_GetGamepadName(gamepad);
  const char* path = SDL_GetGamepadPath(gamepad);
  const char* type = SDL_GetGamepadStringForType(SDL_GetGamepadType(gamepad));
  REXLOG_INFO(
      "SDL pad report: device={} name=\"{}\" type={} vendor={:04X} product={:04X} driver={} "
      "connection={} rumble={} trigger_rumble={} rgb_led={} mono_led={} player_led={} gyro={} "
      "accel={} paddles={} misc1={} touchpad_button={} touchpads={} power={} battery={} "
      "path=\"{}\" guid={}",
      static_cast<uint64_t>(state.id), name ? name : "", type ? type : "unknown",
      SDL_GetGamepadVendor(gamepad), SDL_GetGamepadProduct(gamepad), JoystickDriverName(guid),
      ConnectionName(SDL_GetGamepadConnectionState(gamepad)),
      capability(SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN),
      capability(SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN),
      capability(SDL_PROP_GAMEPAD_CAP_RGB_LED_BOOLEAN),
      capability(SDL_PROP_GAMEPAD_CAP_MONO_LED_BOOLEAN),
      capability(SDL_PROP_GAMEPAD_CAP_PLAYER_LED_BOOLEAN),
      SDL_GamepadHasSensor(gamepad, SDL_SENSOR_GYRO) ? 1 : 0,
      SDL_GamepadHasSensor(gamepad, SDL_SENSOR_ACCEL) ? 1 : 0, paddles,
      SDL_GamepadHasButton(gamepad, SDL_GAMEPAD_BUTTON_MISC1) ? 1 : 0,
      SDL_GamepadHasButton(gamepad, SDL_GAMEPAD_BUTTON_TOUCHPAD) ? 1 : 0,
      SDL_GetNumGamepadTouchpads(gamepad), PowerName(power), battery, path ? path : "",
      guid_text);
}

void SDLInputDriver::OnControllerDeviceRemovedLocked(const SDL_Event& event) {
  auto idx = GetControllerIndexFromInstanceID(event.gdevice.which);
  if (!idx) {
    REXLOG_WARN("SDL OnControllerDeviceRemoved: Ignored. Unknown device.");
    return;
  }
  SDL_CloseGamepad(controllers_.at(*idx).sdl);
  controllers_.erase(controllers_.begin() + static_cast<ptrdiff_t>(*idx));

  // LEDs only. Guest user assignment does not shift, because InputSystem holds
  // each device's ordinal.
  for (size_t i = 0; i < controllers_.size(); i++) {
    SDL_SetGamepadPlayerIndex(controllers_[i].sdl, static_cast<int>(i));
  }
  REXLOG_INFO("SDL OnControllerDeviceRemoved: {} device(s) remain.", controllers_.size());
}

void SDLInputDriver::OnControllerDeviceAxisMotionLocked(const SDL_Event& event) {
  auto idx = GetControllerIndexFromInstanceID(event.gaxis.which);
  if (!idx) {
    // The pad can be removed between the event being posted and drained.
    return;
  }
  auto& pad = controllers_.at(*idx).state.gamepad;
  switch (event.gaxis.axis) {
    case SDL_GAMEPAD_AXIS_LEFTX:
      pad.thumb_lx = event.gaxis.value;
      break;
    case SDL_GAMEPAD_AXIS_LEFTY:
      pad.thumb_ly = ~event.gaxis.value;
      break;
    case SDL_GAMEPAD_AXIS_RIGHTX:
      pad.thumb_rx = event.gaxis.value;
      if (controllers_.at(*idx).probe) {
        auto& probe = controllers_.at(*idx);
        probe.probe_value = event.gaxis.value;
        probe.probe_pumped_ns = event.gaxis.timestamp;
        probe.probe_unread = true;
      }
      break;
    case SDL_GAMEPAD_AXIS_RIGHTY:
      pad.thumb_ry = ~event.gaxis.value;
      break;
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
      pad.left_trigger = static_cast<uint8_t>(event.gaxis.value >> 7);
      break;
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
      pad.right_trigger = static_cast<uint8_t>(event.gaxis.value >> 7);
      break;
    default:
      assert_always();
      break;
  }
  controllers_.at(*idx).state_changed = true;
}

uint16_t SDLInputDriver::GuestButton(uint32_t sdl_button) const {
  if (sdl_button < kStandardButtons.size()) {
    return kStandardButtons[sdl_button];
  }
  if (sdl_button < kSdlButtonCount) {
    // Misc1, the paddles and the touchpad click (pad_misc1, pad_right_paddle1, ...).
    return extra_buttons_[sdl_button - kStandardButtons.size()];
  }
  return 0;
}

void SDLInputDriver::OnControllerDeviceButtonChangedLocked(const SDL_Event& event) {
  auto idx = GetControllerIndexFromInstanceID(event.gdevice.which);
  if (!idx) {
    // The pad can be removed between the event being posted and drained.
    return;
  }
  auto& controller = controllers_.at(*idx);

  if (event.gbutton.button >= kSdlButtonCount) {
    // A newer SDL Version may have added new buttons.
    REXLOG_INFO("SDL HID: Unknown button was pressed: {}.", event.gbutton.button);
    return;
  }
  const uint32_t bit = uint32_t(1) << event.gbutton.button;
  if (event.gbutton.down) {
    controller.sdl_buttons |= bit;
  } else {
    controller.sdl_buttons &= ~bit;
  }
  // The union of the held buttons' guest buttons, so a guest button held through two SDL
  // buttons (a paddle mapped to A, and A) stays held until both are released. Guide only
  // with guide_button.
  uint16_t xbuttons = 0;
  for (uint32_t button = 0; button < kSdlButtonCount; ++button) {
    if (controller.sdl_buttons & (uint32_t(1) << button)) {
      const uint16_t xbutton = GuestButton(button);
      if (xbutton == X_INPUT_GAMEPAD_GUIDE && !REXCVAR_GET(guide_button)) {
        continue;
      }
      xbuttons |= xbutton;
    }
  }
  controller.state.gamepad.buttons = xbuttons;
  controller.state_changed = true;
}

std::optional<size_t> SDLInputDriver::GetControllerIndexFromInstanceID(SDL_JoystickID instance_id) {
  // Loop through our controllers and try to match the given ID.
  for (size_t i = 0; i < controllers_.size(); i++) {
    auto controller = controllers_.at(i).sdl;
    auto joystick = SDL_GetGamepadJoystick(controller);
    assert(joystick);
    auto joy_instance_id = SDL_GetJoystickID(joystick);
    assert(joy_instance_id >= 0);
    if (joy_instance_id == instance_id) {
      return i;
    }
  }
  return std::nullopt;
}

SDLInputDriver::ControllerState* SDLInputDriver::FindController(DeviceId id) {
  if (id == DeviceId::kInvalid) {
    return nullptr;
  }
  for (auto& controller : controllers_) {
    if (controller.id == id) {
      return &controller;
    }
  }
  return nullptr;
}

DeviceId SDLInputDriver::AllocateDeviceId() {
  return static_cast<DeviceId>(next_device_id_++);
}

bool SDLInputDriver::TestSDLVersion() const {
  REXLOG_DEBUG("SDL: Using version {}.{}.{}", SDL_MAJOR_VERSION, SDL_MINOR_VERSION,
               SDL_MICRO_VERSION);
  return true;
}

void SDLInputDriver::UpdateXCapabilities(ControllerState& state) {
  assert(state.sdl);
  uint16_t cap_flags = 0x0;

  // The RAWINPUT driver combines and enhances input from different APIs. For
  // details, see `SDL_rawinputjoystick.c`. This correlation however has latency
  // which might confuse games calling `GetCapabilities()` (The power level is
  // only available after the controller has been "touched"). Generally that
  // should not be a problem, when in doubt disable the RAWINPUT driver via hint
  // (env var).

  if (SDL_GetJoystickConnectionState(SDL_GetGamepadJoystick(state.sdl)) ==
      SDL_JOYSTICK_CONNECTION_WIRELESS) {
    cap_flags |= X_INPUT_CAPS_WIRELESS;
  }

  // Check if all navigational buttons are present
  static constexpr std::array<SDL_GamepadButton, 6> nav_buttons = {
      SDL_GAMEPAD_BUTTON_START,     SDL_GAMEPAD_BUTTON_BACK,      SDL_GAMEPAD_BUTTON_DPAD_UP,
      SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
  };
  for (auto it = nav_buttons.begin(); it < nav_buttons.end(); it++) {
    if (!SDL_GamepadHasButton(state.sdl, *it)) {
      cap_flags |= X_INPUT_CAPS_NO_NAVIGATION;
      break;
    }
  }

  auto& c = state.caps;
  c.type = 0x01;      // XINPUT_DEVTYPE_GAMEPAD
  c.sub_type = 0x01;  // XINPUT_DEVSUBTYPE_GAMEPAD
  c.flags = cap_flags;
  c.gamepad.buttons = 0xF3FF | (REXCVAR_GET(guide_button) ? X_INPUT_GAMEPAD_GUIDE : 0x0);
  c.gamepad.left_trigger = 0xFF;
  c.gamepad.right_trigger = 0xFF;
  c.gamepad.thumb_lx = static_cast<int16_t>(0xFFFFu);
  c.gamepad.thumb_ly = static_cast<int16_t>(0xFFFFu);
  c.gamepad.thumb_rx = static_cast<int16_t>(0xFFFFu);
  c.gamepad.thumb_ry = static_cast<int16_t>(0xFFFFu);
  c.vibration.left_motor_speed = 0xFFFFu;
  c.vibration.right_motor_speed = 0xFFFFu;
}

void SDLInputDriver::StartLatencyProbe() {
  auto probe = std::make_unique<LatencyProbe>();
  SDL_VirtualJoystickDesc desc;
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
  desc.nbuttons = SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1;
  desc.name = "FM4 latency probe";
  desc.userdata = probe.get();
  desc.Update = LatencyProbe::Update;
  // Rumble and trigger rumble callbacks give the pad those capabilities, so the driver's
  // rumble paths can be checked without a physical pad.
  desc.Rumble = LatencyProbe::Rumble;
  desc.RumbleTriggers = LatencyProbe::RumbleTriggers;
  probe->id = SDL_AttachVirtualJoystick(&desc);
  if (!probe->id) {
    REXLOG_ERROR("SDL latency probe: cannot attach a virtual gamepad: {}", SDL_GetError());
    return;
  }
  probe->joystick = SDL_OpenJoystick(probe->id);
  if (!probe->joystick) {
    REXLOG_ERROR("SDL latency probe: cannot open the virtual gamepad: {}", SDL_GetError());
    SDL_DetachVirtualJoystick(probe->id);
    return;
  }
  probe->window_start_ns = SDL_GetTicksNS();
  auto* raw = probe.get();
  probe->thread = std::thread([raw]() {
    int16_t value = 0;
    for (uint32_t step = 0; !raw->stop.load(); ++step) {
      // Out of step with the guest's frames and SDL's polling.
      std::this_thread::sleep_for(std::chrono::milliseconds(20 + (step * 7919) % 30));
      value = value ? 0 : kProbeValue;
      raw->set_ns[value != 0].store(SDL_GetTicksNS());
      SDL_SetJoystickVirtualAxis(raw->joystick, SDL_GAMEPAD_AXIS_RIGHTX, value);
    }
  });
  REXLOG_INFO("SDL latency probe: virtual gamepad {} attached", static_cast<uint32_t>(probe->id));
  probe_ = std::move(probe);
}

void SDLInputDriver::StopLatencyProbe() {
  if (!probe_) {
    return;
  }
  probe_->stop = true;
  if (probe_->thread.joinable()) {
    probe_->thread.join();
  }
  {
    std::lock_guard lock(probe_->mutex);
    REXLOG_INFO("SDL latency probe at shutdown: {}", probe_->RumbleSummary());
  }
  SDL_CloseJoystick(probe_->joystick);
  SDL_DetachVirtualJoystick(probe_->id);
  probe_.reset();
}

void SDLInputDriver::StartInputThread() {
  // SDL's event loop stops updating the pads while the thread does (SDL documents
  // SDL_UpdateGamepads as safe from any thread). COM is initialised for the drivers that
  // use it (DirectInput, Windows.Gaming.Input); see docs/analysis/modern-gamepads.md,
  // "Updating SDL off the UI thread".
  SDL_SetHint(SDL_HINT_AUTO_UPDATE_JOYSTICKS, "0");
  input_thread_stop_ = false;
  // Set before the thread exists: a guest read in between skips one queued pump, and the
  // thread's first update follows at once.
  input_thread_running_ = true;
  input_thread_ = std::thread([this]() {
#if REX_PLATFORM_WIN32
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH);
    while (!input_thread_stop_.load(std::memory_order_relaxed)) {
      SDL_UpdateGamepads();
      SDL_DelayNS(1'000'000);
    }
#if REX_PLATFORM_WIN32
    if (SUCCEEDED(com)) {
      CoUninitialize();
    }
#endif
  });
  REXLOG_INFO("SDL input thread: pads updated every millisecond off the UI thread");
}

void SDLInputDriver::StopInputThread() {
  if (!input_thread_running_.exchange(false)) {
    return;
  }
  input_thread_stop_ = true;
  input_thread_.join();
  SDL_ResetHint(SDL_HINT_AUTO_UPDATE_JOYSTICKS);
}

void SDLInputDriver::ObserveProbeReadLocked(ControllerState& controller) {
  auto& probe = *probe_;
  const uint64_t now = SDL_GetTicksNS();
  std::lock_guard lock(probe.mutex);
  if (probe.last_read_ns) {
    probe.read_gaps.push_back(double(now - probe.last_read_ns) / 1e6);
  }
  probe.last_read_ns = now;
  if (controller.probe_unread) {
    controller.probe_unread = false;
    const uint64_t set = probe.set_ns[controller.probe_value != 0].load();
    const uint64_t pumped = controller.probe_pumped_ns;
    if (set && set <= pumped && pumped <= now) {
      probe.pump_waits.push_back(double(pumped - set) / 1e6);
      probe.read_waits.push_back(double(now - pumped) / 1e6);
      probe.ages.push_back(double(now - set) / 1e6);
    }
  }
  if (now - probe.window_start_ns < kProbeWindowNs) {
    return;
  }
  REXLOG_INFO(
      "SDL latency probe (ms): joystick update gaps {}; new value to update {}; update to "
      "guest read {}; age at read {}; guest read gaps {}; {}",
      LatencyProbe::Summary(probe.update_gaps), LatencyProbe::Summary(probe.pump_waits),
      LatencyProbe::Summary(probe.read_waits), LatencyProbe::Summary(probe.ages),
      LatencyProbe::Summary(probe.read_gaps), probe.RumbleSummary());
  probe.update_gaps.clear();
  probe.pump_waits.clear();
  probe.read_waits.clear();
  probe.ages.clear();
  probe.read_gaps.clear();
  probe.window_start_ns = now;
}

void SDLInputDriver::QueueControllerUpdate() {
  // The input thread updates the pads itself, and SDL's event loop no longer does.
  if (input_thread_running_.load(std::memory_order_relaxed)) {
    return;
  }
  // Pump SDL events to ensure controller state is up to date.
  bool is_queued = false;
  sdl_pumpevents_queued_.compare_exchange_strong(is_queued, true);
  if (!is_queued) {
    attached_window_->app_context().CallInUIThread([this]() {
      SDL_PumpEvents();
      sdl_pumpevents_queued_ = false;
    });
  }
}

// Check if the analog inputs exceed their thresholds to become a button press
// and build the bitfield.
inline uint64_t SDLInputDriver::AnalogToKeyfield(const X_INPUT_GAMEPAD& gamepad) const {
  uint64_t f = 0;

  f |= static_cast<uint64_t>(gamepad.left_trigger > HID_SDL_TRIGG_THRES) << 16;
  f |= static_cast<uint64_t>(gamepad.right_trigger > HID_SDL_TRIGG_THRES) << 17;

  auto thumb_x = static_cast<int16_t>(gamepad.thumb_lx);
  auto thumb_y = static_cast<int16_t>(gamepad.thumb_ly);
  for (size_t i = 0; i <= 8; i = i + 8) {
    uint64_t u = thumb_y > HID_SDL_THUMB_THRES;
    uint64_t d = thumb_y < ~HID_SDL_THUMB_THRES;
    uint64_t r = thumb_x > HID_SDL_THUMB_THRES;
    uint64_t l = thumb_x < ~HID_SDL_THUMB_THRES;
    if (u && l) {
      u = l = 0;
      f |= uint64_t(1) << (22 + i);
    }
    if (u && r) {
      u = r = 0;
      f |= uint64_t(1) << (23 + i);
    }
    if (d && r) {
      d = r = 0;
      f |= uint64_t(1) << (24 + i);
    }
    if (d && l) {
      d = l = 0;
      f |= uint64_t(1) << (25 + i);
    }
    f |= u << (18 + i);
    f |= d << (19 + i);
    f |= r << (20 + i);
    f |= l << (21 + i);

    thumb_x = static_cast<int16_t>(gamepad.thumb_rx);
    thumb_y = static_cast<int16_t>(gamepad.thumb_ry);
  }
  return f;
}

}  // namespace rex::input::sdl
