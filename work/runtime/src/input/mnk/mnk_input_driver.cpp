/**
 * @file        input/mnk/mnk_input_driver.cpp
 * @brief       Keyboard/mouse input driver implementation.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */
#include <rex/input/mnk/mnk_input_driver.h>

#include <rex/cvar.h>
#include <rex/input/input.h>
#include <rex/input/keyboard_model.h>
#include <rex/input/title_input_ranges.h>
#include <rex/logging.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string_view>

REXCVAR_DEFINE_BOOL(mnk_mode, false, "Input", "Enable keyboard/mouse controller emulation");
REXCVAR_DEFINE_BOOL(mnk_mouse, false, "Input",
                    "Use the mouse for the right stick. Off means the right stick comes "
                    "from the keybind_rstick_* keys only");
REXCVAR_DEFINE_DOUBLE(mnk_sensitivity, 1.0, "Input", "Mouse sensitivity for right stick")
    .range(0.01, 10.0);

// The keyboard's steering and pedal model (rex/input/keyboard_model.h,
// docs/analysis/keyboard-controls.md). Off, the keys are digital as before.
REXCVAR_DEFINE_BOOL(mnk_smooth, false, "Input",
                    "Keyboard steering, throttle and brake move at limited rates instead of "
                    "jumping, mapped into the title's own deadzones");
REXCVAR_DEFINE_DOUBLE(mnk_steer_attack, 0.30, "Input",
                      "Keyboard steering: seconds from centre to full lock")
    .range(0.0, 2.0);
REXCVAR_DEFINE_DOUBLE(mnk_steer_release, 0.20, "Input",
                      "Keyboard steering: seconds from full lock back to centre")
    .range(0.0, 2.0);
REXCVAR_DEFINE_DOUBLE(mnk_throttle_attack, 0.25, "Input",
                      "Keyboard throttle (right trigger): seconds from 0 to full")
    .range(0.0, 2.0);
REXCVAR_DEFINE_DOUBLE(mnk_throttle_release, 0.10, "Input",
                      "Keyboard throttle (right trigger): seconds from full to 0")
    .range(0.0, 2.0);
REXCVAR_DEFINE_DOUBLE(mnk_brake_attack, 0.15, "Input",
                      "Keyboard brake (left trigger): seconds from 0 to full")
    .range(0.0, 2.0);
REXCVAR_DEFINE_DOUBLE(mnk_brake_release, 0.10, "Input",
                      "Keyboard brake (left trigger): seconds from full to 0")
    .range(0.0, 2.0);
REXCVAR_DEFINE_DOUBLE(mnk_precision, 0.5, "Input",
                      "Share of full lock and full pedal while the precision key is held")
    .range(0.1, 1.0);
REXCVAR_DEFINE_BOOL(mnk_title_deadzones, true, "Input",
                    "Map the keyboard's steering and pedals into the deadzones the title "
                    "publishes, so they act from the first press");
REXCVAR_DEFINE_BOOL(mnk_mouse_steering, false, "Input",
                    "The mouse's horizontal motion steers (the right stick then gets no mouse)");
REXCVAR_DEFINE_DOUBLE(mnk_mouse_steer_sensitivity, 1.0, "Input",
                      "Mouse steering: thousandths of full lock per count of motion")
    .range(0.1, 10.0);
REXCVAR_DEFINE_DOUBLE(mnk_mouse_steer_centering, 1.0, "Input",
                      "Mouse steering: time constant in seconds of the return to centre (0: off)")
    .range(0.0, 5.0);

REXCVAR_DEFINE_STRING(keybind_a, "Semicolon,Space", "Input/Keybinds/Controller", "A button");
REXCVAR_DEFINE_STRING(keybind_b, "Quote,Backspace", "Input/Keybinds/Controller", "B button");
REXCVAR_DEFINE_STRING(keybind_x, "L", "Input/Keybinds/Controller", "X button");
REXCVAR_DEFINE_STRING(keybind_y, "P", "Input/Keybinds/Controller", "Y button");
REXCVAR_DEFINE_STRING(keybind_left_trigger, "Q,I", "Input/Keybinds/Controller", "Left trigger");
REXCVAR_DEFINE_STRING(keybind_right_trigger, "E,O", "Input/Keybinds/Controller", "Right trigger");
REXCVAR_DEFINE_STRING(keybind_left_shoulder, "1", "Input/Keybinds/Controller", "Left shoulder");
REXCVAR_DEFINE_STRING(keybind_right_shoulder, "3", "Input/Keybinds/Controller", "Right shoulder");
REXCVAR_DEFINE_STRING(keybind_lstick_up, "W", "Input/Keybinds/Controller", "Left stick up");
REXCVAR_DEFINE_STRING(keybind_lstick_down, "S", "Input/Keybinds/Controller", "Left stick down");
REXCVAR_DEFINE_STRING(keybind_lstick_left, "A", "Input/Keybinds/Controller", "Left stick left");
REXCVAR_DEFINE_STRING(keybind_lstick_right, "D", "Input/Keybinds/Controller", "Left stick right");
REXCVAR_DEFINE_STRING(keybind_lstick_press, "F", "Input/Keybinds/Controller", "Left stick press");
REXCVAR_DEFINE_STRING(keybind_rstick_up, "Up", "Input/Keybinds/Controller", "Right stick up");
REXCVAR_DEFINE_STRING(keybind_rstick_down, "Down", "Input/Keybinds/Controller", "Right stick down");
REXCVAR_DEFINE_STRING(keybind_rstick_left, "Left", "Input/Keybinds/Controller", "Right stick left");
REXCVAR_DEFINE_STRING(keybind_rstick_right, "Right", "Input/Keybinds/Controller",
                      "Right stick right");
REXCVAR_DEFINE_STRING(keybind_rstick_press, "K", "Input/Keybinds/Controller", "Right stick press");
REXCVAR_DEFINE_STRING(keybind_dpad_up, "Shift+Up", "Input/Keybinds/Controller", "D-pad up");
REXCVAR_DEFINE_STRING(keybind_dpad_down, "Shift+Down", "Input/Keybinds/Controller", "D-pad down");
REXCVAR_DEFINE_STRING(keybind_dpad_left, "Shift+Left", "Input/Keybinds/Controller", "D-pad left");
REXCVAR_DEFINE_STRING(keybind_dpad_right, "Shift+Right", "Input/Keybinds/Controller",
                      "D-pad right");
REXCVAR_DEFINE_STRING(keybind_back, "Z,Tab", "Input/Keybinds/Controller", "Back button");
REXCVAR_DEFINE_STRING(keybind_start, "X,Return", "Input/Keybinds/Controller", "Start button");
REXCVAR_DEFINE_STRING(keybind_guide, "", "Input/Keybinds/Controller", "Guide button");
REXCVAR_DEFINE_STRING(keybind_precision, "", "Input/Keybinds/Controller",
                      "Precision (hold): limits steering and pedals");

namespace rex::input::mnk {

namespace {

// A single device, so its handle is a constant.
constexpr rex::input::DeviceId kMnkDevice = static_cast<rex::input::DeviceId>(0x4D4E4B00);

// Bind values are a comma-separated list of alternatives, each optionally
// carrying modifier prefixes: "Q,I" or "Shift+W" (keyboard::ParseBind). Modifier
// matching is exact, so "Up" stays silent while Shift is held and "Shift+Up"
// stays silent without it. That is what lets the D-pad share the arrow keys.
// A binding whose key is itself a modifier ("Control" for the precision key)
// fires while it is held, and that modifier is then left out of every other
// binding's comparison, so W still presses the throttle with Ctrl held
// (keyboard::BindHeld).
enum Bind : size_t {
  kBindA,
  kBindB,
  kBindX,
  kBindY,
  kBindLeftShoulder,
  kBindRightShoulder,
  kBindLeftThumb,
  kBindRightThumb,
  kBindBack,
  kBindStart,
  kBindGuide,
  kBindDpadUp,
  kBindDpadDown,
  kBindDpadLeft,
  kBindDpadRight,
  kBindLeftTrigger,
  kBindRightTrigger,
  kBindLStickUp,
  kBindLStickDown,
  kBindLStickLeft,
  kBindLStickRight,
  kBindRStickUp,
  kBindRStickDown,
  kBindRStickLeft,
  kBindRStickRight,
  kBindPrecision,
  kBindCount
};

std::string& (*const kBindCvars[kBindCount])() = {
    &FLAGS_keybind_a_storage_,           &FLAGS_keybind_b_storage_,
    &FLAGS_keybind_x_storage_,           &FLAGS_keybind_y_storage_,
    &FLAGS_keybind_left_shoulder_storage_, &FLAGS_keybind_right_shoulder_storage_,
    &FLAGS_keybind_lstick_press_storage_, &FLAGS_keybind_rstick_press_storage_,
    &FLAGS_keybind_back_storage_,        &FLAGS_keybind_start_storage_,
    &FLAGS_keybind_guide_storage_,       &FLAGS_keybind_dpad_up_storage_,
    &FLAGS_keybind_dpad_down_storage_,   &FLAGS_keybind_dpad_left_storage_,
    &FLAGS_keybind_dpad_right_storage_,  &FLAGS_keybind_left_trigger_storage_,
    &FLAGS_keybind_right_trigger_storage_, &FLAGS_keybind_lstick_up_storage_,
    &FLAGS_keybind_lstick_down_storage_, &FLAGS_keybind_lstick_left_storage_,
    &FLAGS_keybind_lstick_right_storage_, &FLAGS_keybind_rstick_up_storage_,
    &FLAGS_keybind_rstick_down_storage_, &FLAGS_keybind_rstick_left_storage_,
    &FLAGS_keybind_rstick_right_storage_, &FLAGS_keybind_precision_storage_,
};

constexpr std::pair<Bind, uint16_t> kButtonBinds[] = {
    {kBindA, X_INPUT_GAMEPAD_A},
    {kBindB, X_INPUT_GAMEPAD_B},
    {kBindX, X_INPUT_GAMEPAD_X},
    {kBindY, X_INPUT_GAMEPAD_Y},
    {kBindLeftShoulder, X_INPUT_GAMEPAD_LEFT_SHOULDER},
    {kBindRightShoulder, X_INPUT_GAMEPAD_RIGHT_SHOULDER},
    {kBindLeftThumb, X_INPUT_GAMEPAD_LEFT_THUMB},
    {kBindRightThumb, X_INPUT_GAMEPAD_RIGHT_THUMB},
    {kBindBack, X_INPUT_GAMEPAD_BACK},
    {kBindStart, X_INPUT_GAMEPAD_START},
    {kBindGuide, X_INPUT_GAMEPAD_GUIDE},
    {kBindDpadUp, X_INPUT_GAMEPAD_DPAD_UP},
    {kBindDpadDown, X_INPUT_GAMEPAD_DPAD_DOWN},
    {kBindDpadLeft, X_INPUT_GAMEPAD_DPAD_LEFT},
    {kBindDpadRight, X_INPUT_GAMEPAD_DPAD_RIGHT},
};

uint16_t ParseKeyName(std::string_view name) {
  return static_cast<uint16_t>(rex::ui::ParseVirtualKey(name));
}

// Keys the diagnostic controller holds (SetInjectedKeys), apart from the
// driver so that it needs no pointer to it.
struct InjectedKeys {
  std::mutex mutex;
  bool down[256] = {};
  bool any = false;
};

InjectedKeys& GetInjectedKeys() {
  static InjectedKeys keys;
  return keys;
}

std::atomic<bool> mouse_look_active{true};

}  // namespace

void SetMouseLookActive(bool active) {
  mouse_look_active.store(active, std::memory_order_relaxed);
}

bool IsMouseLookActive() {
  return mouse_look_active.load(std::memory_order_relaxed);
}

void SetInjectedKeys(const std::vector<uint16_t>& virtual_keys) {
  InjectedKeys& injected = GetInjectedKeys();
  std::lock_guard lock(injected.mutex);
  std::memset(injected.down, 0, sizeof(injected.down));
  injected.any = false;
  for (uint16_t vk : virtual_keys) {
    if (vk != 0 && vk < 256) {
      injected.down[vk] = true;
      injected.any = true;
    }
  }
}

using rex::ui::VirtualKey;

MnkInputDriver::MnkInputDriver(rex::ui::Window* window, size_t window_z_order)
    : InputDriver(window, window_z_order) {}

MnkInputDriver::~MnkInputDriver() {
  // Detach handled by OnClosing; if window outlives the driver, clean up here.
  DetachFromWindow();
}

X_STATUS MnkInputDriver::Setup() {
  return X_STATUS_SUCCESS;
}

void MnkInputDriver::OnWindowAvailable(rex::ui::Window* window) {
  if (window) {
    {
      std::lock_guard lock(state_mutex_);
      attached_window_ = window;
    }
    window->AddInputListener(this, window_z_order());
    window->AddListener(this);
  }
}

void MnkInputDriver::OnClosing(rex::ui::UIEvent&) {
  DetachFromWindow();
}

void MnkInputDriver::DetachFromWindow() {
  rex::ui::Window* window = attached_window_;
  if (!window) {
    return;
  }
  window->app_context().CallInUIThreadSynchronous([this, window] {
    // Detach first so nothing new is queued, then run out what already was.
    {
      std::lock_guard lock(state_mutex_);
      attached_window_ = nullptr;
    }
    if (mouse_capture_update_queued_.load(std::memory_order_relaxed)) {
      window->app_context().ExecutePendingFunctionsFromUIThread();
    }
    ReleaseMouseCaptureFromUIThread(window);
    window->RemoveInputListener(this);
    window->RemoveListener(this);
  });
}

bool MnkInputDriver::IsEnabled() const {
  return REXCVAR_GET(mnk_mode);
}

void MnkInputDriver::EnumerateDevices(std::vector<DeviceInfo>& out) {
  // Disabled means no device at all, so it never occupies a guest user slot.
  if (!IsEnabled()) {
    return;
  }
  DeviceInfo info;
  info.id = kMnkDevice;
  info.name = "Keyboard and Mouse";
  info.synthetic = true;
  out.push_back(info);
}

X_RESULT MnkInputDriver::GetDeviceCapabilities(DeviceId id, uint32_t flags,
                                               X_INPUT_CAPABILITIES* out_caps) {
  if (!IsEnabled() || id != kMnkDevice) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if (out_caps) {
    std::memset(out_caps, 0, sizeof(*out_caps));
    out_caps->type = 0x01;
    out_caps->sub_type = 0x01;
    out_caps->flags = 0;
    out_caps->gamepad.buttons = 0xFFFF;
    out_caps->gamepad.left_trigger = 0xFF;
    out_caps->gamepad.right_trigger = 0xFF;
    out_caps->gamepad.thumb_lx = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_ly = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_rx = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_ry = static_cast<int16_t>(0x7FFF);
    out_caps->vibration.left_motor_speed = 0xFFFF;
    out_caps->vibration.right_motor_speed = 0xFFFF;
  }
  return X_ERROR_SUCCESS;
}

X_RESULT MnkInputDriver::GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) {
  if (!IsEnabled() || id != kMnkDevice) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  const bool mouse_steering = REXCVAR_GET(mnk_mouse_steering);
  // Mouse look is opt in. Without this gate, keyboard input alone would hide
  // and lock the cursor, breaking the ImGui overlays.
  QueueMouseCaptureUpdate(IsEnabled() && (REXCVAR_GET(mnk_mouse) || mouse_steering) &&
                          IsMouseLookActive() && has_focus_ && is_active());

  std::lock_guard lock(state_mutex_);

  bool down[256];
  const bool injected = HeldKeys(down);
  // Injected keys are not desktop input, so they count without focus.
  if (!is_active() || (!has_focus_ && !injected)) {
    ResetModel();
    mouse_dx_ = 0.0f;
    mouse_dy_ = 0.0f;
    if (out_state) {
      std::memset(out_state, 0, sizeof(*out_state));
      out_state->packet_number = packet_number_;
    }
    return X_ERROR_SUCCESS;
  }

  RefreshBinds();
  uint16_t buttons = 0;
  for (const auto& [bind, bit] : kButtonBinds) {
    if (Held(bind, down)) {
      buttons |= bit;
    }
  }

  const bool smooth = REXCVAR_GET(mnk_smooth);
  const Clock::time_point now = Clock::now();
  if (smooth) {
    AdvanceModel(now);
  } else {
    model_running_ = false;
  }
  UpdateTargets(down);

  int32_t lx = 0;
  uint8_t lt = 0;
  uint8_t rt = 0;
  if (!smooth && !mouse_steering) {
    // Digital keys, as before the model: full deflection at once, opposite
    // keys cancel.
    lt = Held(kBindLeftTrigger, down) ? 0xFF : 0;
    rt = Held(kBindRightTrigger, down) ? 0xFF : 0;
    if (Held(kBindLStickLeft, down))
      lx -= INT16_MAX;
    if (Held(kBindLStickRight, down))
      lx += INT16_MAX;
  } else {
    double steer = smooth ? steer_ : steer_target_;
    const double throttle = smooth ? throttle_ : throttle_target_;
    const double brake = smooth ? brake_ : brake_target_;
    if (mouse_steering) {
      // Auto-centring decays toward 0, then the motion since the last poll adds.
      const double centering = REXCVAR_GET(mnk_mouse_steer_centering);
      if (mouse_running_ && centering > 0.0) {
        const double dt = std::chrono::duration<double>(now - mouse_time_).count();
        mouse_steer_ *= std::exp(-dt / centering);
      }
      mouse_time_ = now;
      mouse_running_ = true;
      mouse_steer_ = std::clamp(
          mouse_steer_ + double(mouse_dx_) * REXCVAR_GET(mnk_mouse_steer_sensitivity) / 1000.0,
          -1.0, 1.0);
      const double limit = Held(kBindPrecision, down) ? REXCVAR_GET(mnk_precision) : 1.0;
      steer = std::clamp(steer + mouse_steer_, -limit, limit);
    }
    // The keyboard always feeds guest user 0 (device_assignment.h).
    const keyboard::TitleRanges ranges =
        REXCVAR_GET(mnk_title_deadzones) ? GetTitleInputRanges(0) : keyboard::TitleRanges{};
    lx = keyboard::StickFromTitle(steer, ranges.steering);
    lt = keyboard::TriggerFromTitle(brake, ranges.left_trigger);
    rt = keyboard::TriggerFromTitle(throttle, ranges.right_trigger);
  }

  int32_t ly = 0;
  if (Held(kBindLStickUp, down))
    ly += INT16_MAX;
  if (Held(kBindLStickDown, down))
    ly -= INT16_MAX;

  int32_t rx = 0;
  int32_t ry = 0;
  if (Held(kBindRStickLeft, down))
    rx -= INT16_MAX;
  if (Held(kBindRStickRight, down))
    rx += INT16_MAX;
  if (Held(kBindRStickUp, down))
    ry += INT16_MAX;
  if (Held(kBindRStickDown, down))
    ry -= INT16_MAX;

  if (REXCVAR_GET(mnk_mouse) && !mouse_steering && IsMouseLookActive()) {
    double sensitivity = REXCVAR_GET(mnk_sensitivity);
    constexpr double kBaseScale = 200.0;
    rx += static_cast<int32_t>(double(mouse_dx_) * sensitivity * kBaseScale);
    ry += static_cast<int32_t>(double(-mouse_dy_) * sensitivity * kBaseScale);
  }
  // Drained unconditionally: deltas keep accumulating in OnMouseMove while the
  // mouse is off, and toggling it on would otherwise dump the whole backlog
  // into one frame as a camera snap.
  mouse_dx_ = 0.0f;
  mouse_dy_ = 0.0f;

  auto clamp16 = [](int32_t v) -> int16_t {
    return static_cast<int16_t>(std::clamp(v, (int32_t)INT16_MIN, (int32_t)INT16_MAX));
  };

  packet_number_++;

  if (out_state) {
    out_state->packet_number = packet_number_;
    out_state->gamepad.buttons = buttons;
    out_state->gamepad.left_trigger = lt;
    out_state->gamepad.right_trigger = rt;
    out_state->gamepad.thumb_lx = clamp16(lx);
    out_state->gamepad.thumb_ly = clamp16(ly);
    out_state->gamepad.thumb_rx = clamp16(rx);
    out_state->gamepad.thumb_ry = clamp16(ry);
  }
  return X_ERROR_SUCCESS;
}

void MnkInputDriver::RefreshBinds() {
  if (bind_tokens_.size() != kBindCount) {
    bind_text_.assign(kBindCount, std::string());
    bind_tokens_.assign(kBindCount, {});
  }
  bool changed = false;
  for (size_t i = 0; i < kBindCount; ++i) {
    const std::string& text = kBindCvars[i]();
    if (text != bind_text_[i]) {
      bind_text_[i] = text;
      bind_tokens_[i] = keyboard::ParseBind(text, ParseKeyName);
      changed = true;
    }
  }
  if (changed) {
    ignored_modifiers_ = 0;
    for (const auto& tokens : bind_tokens_) {
      ignored_modifiers_ |= keyboard::BareModifiers(tokens);
    }
  }
}

bool MnkInputDriver::HeldKeys(bool (&down)[256]) const {
  std::memcpy(down, key_down_, sizeof(down));
  InjectedKeys& injected = GetInjectedKeys();
  std::lock_guard lock(injected.mutex);
  if (injected.any) {
    for (size_t i = 0; i < 256; ++i) {
      down[i] = down[i] || injected.down[i];
    }
  }
  return injected.any;
}

bool MnkInputDriver::Held(size_t bind, const bool (&down)[256]) const {
  return keyboard::BindHeld(bind_tokens_[bind], down, keyboard::LiveModifiers(down),
                            ignored_modifiers_);
}

void MnkInputDriver::AdvanceModel(Clock::time_point now) {
  if (!model_running_) {
    model_time_ = now;
    model_running_ = true;
    return;
  }
  const double dt = std::chrono::duration<double>(now - model_time_).count();
  model_time_ = now;
  steer_ = keyboard::AdvanceAxis(steer_, steer_target_, dt,
                                 {REXCVAR_GET(mnk_steer_attack), REXCVAR_GET(mnk_steer_release)});
  throttle_ = keyboard::AdvanceAxis(
      throttle_, throttle_target_, dt,
      {REXCVAR_GET(mnk_throttle_attack), REXCVAR_GET(mnk_throttle_release)});
  brake_ = keyboard::AdvanceAxis(brake_, brake_target_, dt,
                                 {REXCVAR_GET(mnk_brake_attack), REXCVAR_GET(mnk_brake_release)});
}

void MnkInputDriver::UpdateTargets(const bool (&down)[256]) {
  RefreshBinds();
  const double steer =
      steering_keys_.Update(Held(kBindLStickLeft, down), Held(kBindLStickRight, down));
  const double limit = Held(kBindPrecision, down) ? REXCVAR_GET(mnk_precision) : 1.0;
  steer_target_ = steer * limit;
  throttle_target_ = Held(kBindRightTrigger, down) ? limit : 0.0;
  brake_target_ = Held(kBindLeftTrigger, down) ? limit : 0.0;
}

void MnkInputDriver::ResetModel() {
  steer_ = 0.0;
  throttle_ = 0.0;
  brake_ = 0.0;
  model_running_ = false;
  mouse_steer_ = 0.0;
  mouse_running_ = false;
}

X_RESULT MnkInputDriver::SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) {
  if (!IsEnabled() || id != kMnkDevice) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  return X_ERROR_SUCCESS;
}

X_RESULT MnkInputDriver::GetDeviceKeystroke(DeviceId id, uint32_t flags,
                                            X_INPUT_KEYSTROKE* out_keystroke) {
  if (!IsEnabled() || id != kMnkDevice) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  std::lock_guard lock(state_mutex_);
  if (keystroke_queue_.empty()) {
    return X_ERROR_EMPTY;
  }
  if (out_keystroke) {
    *out_keystroke = keystroke_queue_.front();
  }
  keystroke_queue_.pop();
  return X_ERROR_SUCCESS;
}

void MnkInputDriver::EnqueueKeystroke(uint16_t vk_pad, bool down) {
  X_INPUT_KEYSTROKE ks = {};
  ks.virtual_key = vk_pad;
  ks.unicode = 0;
  ks.flags = down ? X_INPUT_KEYSTROKE_KEYDOWN : X_INPUT_KEYSTROKE_KEYUP;
  // InputSystem stamps the guest user this device is assigned to.
  ks.user_index = 0;
  ks.hid_code = 0;
  keystroke_queue_.push(ks);
}

void MnkInputDriver::QueueMouseCaptureUpdate(bool should_capture) {
  mouse_capture_requested_.store(should_capture, std::memory_order_relaxed);
  bool already_queued = false;
  if (!mouse_capture_update_queued_.compare_exchange_strong(already_queued, true)) {
    return;
  }
  std::lock_guard lock(state_mutex_);
  if (!attached_window_) {
    mouse_capture_update_queued_.store(false, std::memory_order_relaxed);
    return;
  }
  // Deferred, not CallInUIThread: running inline would re-enter state_mutex_.
  attached_window_->app_context().CallInUIThreadDeferred([this] {
    mouse_capture_update_queued_.store(false, std::memory_order_relaxed);
    ApplyMouseCaptureFromUIThread();
  });
}

void MnkInputDriver::ApplyMouseCaptureFromUIThread() {
  rex::ui::Window* window = attached_window_;
  if (!window) {
    return;
  }
  bool should_capture = mouse_capture_requested_.load(std::memory_order_relaxed);
  if (should_capture == mouse_captured_) {
    return;
  }
  if (!should_capture) {
    ReleaseMouseCaptureFromUIThread(window);
    return;
  }
  mouse_captured_ = true;
  precapture_cursor_visibility_ = window->GetCursorVisibility();
  window->SetCursorVisibility(rex::ui::Window::CursorVisibility::kHidden);
  window->CaptureMouse();
  relative_mouse_mode_ = window->SetRelativeMouseMode(true);
  if (!relative_mouse_mode_) {
    REXLOG_WARN("Pointer lock unavailable, mouse look falls back to recentering the cursor");
  }
  // Reset deltas to avoid a spike on capture start
  std::lock_guard lock(state_mutex_);
  mouse_dx_ = 0.0f;
  mouse_dy_ = 0.0f;
}

void MnkInputDriver::ReleaseMouseCaptureFromUIThread(rex::ui::Window* window) {
  if (!mouse_captured_) {
    return;
  }
  mouse_captured_ = false;
  window->SetCursorVisibility(precapture_cursor_visibility_);
  window->SetRelativeMouseMode(false);
  relative_mouse_mode_ = false;
  window->ReleaseMouse();
}

void MnkInputDriver::RecenterCursorFromUIThread(int32_t x, int32_t y) {
  rex::ui::Window* window = attached_window_;
  if (!window) {
    return;
  }
  int32_t width = int32_t(window->GetActualPhysicalWidth());
  int32_t height = int32_t(window->GetActualPhysicalHeight());
  if (width <= 0 || height <= 0) {
    return;
  }
  // Only once the pointer has drifted well off the middle, to keep warps rare.
  if (std::abs(x - width / 2) < width / 4 && std::abs(y - height / 2) < height / 4) {
    return;
  }
  int32_t center_x = 0;
  int32_t center_y = 0;
  if (window->WarpMouseToCenter(center_x, center_y)) {
    prev_mouse_x_ = center_x;
    prev_mouse_y_ = center_y;
  }
}

void MnkInputDriver::SetKeyState(uint16_t vk, bool down) {
  if (vk < 256) {
    key_down_[vk] = down;
  }
}

void MnkInputDriver::ApplyKey(uint16_t vk, bool down) {
  if (REXCVAR_GET(mnk_smooth)) {
    AdvanceModel(Clock::now());
  }
  SetKeyState(vk, down);
  bool keys[256];
  HeldKeys(keys);
  UpdateTargets(keys);
}

void MnkInputDriver::OnKeyDown(rex::ui::KeyEvent& e) {
  // Alt combinations belong to the host (Alt+Return toggles fullscreen, Alt+Tab,
  // Alt+F4): they must not press the controller buttons of their keys.
  if (!IsEnabled() || !has_focus_ || e.is_alt_pressed())
    return;
  std::lock_guard lock(state_mutex_);
  ApplyKey(static_cast<uint16_t>(e.virtual_key()), true);
}

void MnkInputDriver::OnKeyUp(rex::ui::KeyEvent& e) {
  if (!IsEnabled())
    return;
  std::lock_guard lock(state_mutex_);
  ApplyKey(static_cast<uint16_t>(e.virtual_key()), false);
}

void MnkInputDriver::OnMouseDown(rex::ui::MouseEvent& e) {
  if (!IsEnabled() || !has_focus_)
    return;
  std::lock_guard lock(state_mutex_);
  switch (e.button()) {
    case rex::ui::MouseEvent::Button::kLeft:
      ApplyKey(static_cast<uint16_t>(VirtualKey::kLButton), true);
      break;
    case rex::ui::MouseEvent::Button::kRight:
      ApplyKey(static_cast<uint16_t>(VirtualKey::kRButton), true);
      break;
    case rex::ui::MouseEvent::Button::kMiddle:
      ApplyKey(static_cast<uint16_t>(VirtualKey::kMButton), true);
      break;
    default:
      break;
  }
}

void MnkInputDriver::OnMouseUp(rex::ui::MouseEvent& e) {
  if (!IsEnabled())
    return;
  std::lock_guard lock(state_mutex_);
  switch (e.button()) {
    case rex::ui::MouseEvent::Button::kLeft:
      ApplyKey(static_cast<uint16_t>(VirtualKey::kLButton), false);
      break;
    case rex::ui::MouseEvent::Button::kRight:
      ApplyKey(static_cast<uint16_t>(VirtualKey::kRButton), false);
      break;
    case rex::ui::MouseEvent::Button::kMiddle:
      ApplyKey(static_cast<uint16_t>(VirtualKey::kMButton), false);
      break;
    default:
      break;
  }
}

void MnkInputDriver::OnMouseMove(rex::ui::MouseEvent& e) {
  if (!IsEnabled() || !has_focus_)
    return;
  int32_t x = e.x();
  int32_t y = e.y();
  {
    std::lock_guard lock(state_mutex_);
    if (relative_mouse_mode_) {
      // The pointer is locked, so the absolute position no longer moves.
      mouse_dx_ += e.dx();
      mouse_dy_ += e.dy();
    } else {
      mouse_dx_ += float(x - prev_mouse_x_);
      mouse_dy_ += float(y - prev_mouse_y_);
    }
  }
  prev_mouse_x_ = x;
  prev_mouse_y_ = y;
  // Without a pointer lock the cursor still has to be kept off the edges.
  if (mouse_captured_ && !relative_mouse_mode_) {
    RecenterCursorFromUIThread(x, y);
  }
}

void MnkInputDriver::OnLostFocus(rex::ui::UISetupEvent&) {
  has_focus_ = false;
  // Withdraw the request too, or an update queued before the focus loss grabs
  // the cursor straight back.
  mouse_capture_requested_.store(false, std::memory_order_relaxed);
  {
    std::lock_guard lock(state_mutex_);
    std::memset(key_down_, 0, sizeof(key_down_));
    mouse_dx_ = 0.0f;
    mouse_dy_ = 0.0f;
    ResetModel();
  }
  if (attached_window_) {
    ReleaseMouseCaptureFromUIThread(attached_window_);
  }
}

void MnkInputDriver::OnGotFocus(rex::ui::UISetupEvent&) {
  has_focus_ = true;
}

}  // namespace rex::input::mnk
