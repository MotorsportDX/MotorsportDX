/**
 * @file        rex/input/mnk/mnk_input_driver.h
 * @brief       Keyboard/mouse input driver - maps MnK to Xbox 360 controller.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/input/input_driver.h>
#include <rex/input/keyboard_model.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

namespace rex::input::mnk {

void SetMouseLookActive(bool active);
bool IsMouseLookActive();

// Keys held beside the real ones, by virtual key, for the e2e harness's
// diagnostic controller (work/fm4-rex/src/fm4-test-input.h): they go through
// the same bindings and model, also without window focus, but not while an
// overlay takes the input. Replaces the previous set; empty releases them.
void SetInjectedKeys(const std::vector<uint16_t>& virtual_keys);

class MnkInputDriver final : public InputDriver,
                             public rex::ui::WindowInputListener,
                             public rex::ui::WindowListener {
 public:
  explicit MnkInputDriver(rex::ui::Window* window, size_t window_z_order);
  ~MnkInputDriver() override;

  X_STATUS Setup() override;

  void EnumerateDevices(std::vector<DeviceInfo>& out) override;
  X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) override;
  X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t flags,
                                 X_INPUT_CAPABILITIES* out_caps) override;
  X_RESULT SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) override;
  X_RESULT GetDeviceKeystroke(DeviceId id, uint32_t flags,
                              X_INPUT_KEYSTROKE* out_keystroke) override;

  void OnWindowAvailable(rex::ui::Window* window) override;

  // WindowInputListener
  void OnKeyDown(rex::ui::KeyEvent& e) override;
  void OnKeyUp(rex::ui::KeyEvent& e) override;
  void OnMouseDown(rex::ui::MouseEvent& e) override;
  void OnMouseUp(rex::ui::MouseEvent& e) override;
  void OnMouseMove(rex::ui::MouseEvent& e) override;

  // WindowListener
  void OnClosing(rex::ui::UIEvent& e) override;
  void OnLostFocus(rex::ui::UISetupEvent& e) override;
  void OnGotFocus(rex::ui::UISetupEvent& e) override;

 private:
  using Clock = std::chrono::steady_clock;

  bool IsEnabled() const;
  void SetKeyState(uint16_t vk, bool down);
  // A key or mouse button changed, from the UI thread: the model runs up to
  // now on the old keys first. Under state_mutex_.
  void ApplyKey(uint16_t vk, bool down);

  // The keyboard model (rex/input/keyboard_model.h,
  // docs/analysis/keyboard-controls.md); all under state_mutex_.
  void RefreshBinds();
  // The held keys, real and injected; returns whether any key is injected.
  bool HeldKeys(bool (&down)[256]) const;
  bool Held(size_t bind, const bool (&down)[256]) const;
  void AdvanceModel(Clock::time_point now);
  void UpdateTargets(const bool (&down)[256]);
  void ResetModel();
  void EnqueueKeystroke(uint16_t vk_pad, bool down);

  // Called from the guest thread. The rest of the capture path stays on the UI
  // thread, since every Window call in it reaches SDL.
  void QueueMouseCaptureUpdate(bool should_capture);
  void ApplyMouseCaptureFromUIThread();
  void ReleaseMouseCaptureFromUIThread(rex::ui::Window* window);
  void RecenterCursorFromUIThread(int32_t x, int32_t y);
  // Safe to call from any thread.
  void DetachFromWindow();

  // Only the UI thread writes it, so only guest thread access needs the lock.
  rex::ui::Window* attached_window_ = nullptr;

  std::mutex state_mutex_;
  bool key_down_[256] = {};

  // Mouse delta tracking. Fractional because relative motion arrives in
  // fractions of a pixel, and truncating each event drops slow movement.
  // Filled on the UI thread, drained on the guest thread, hence the lock.
  float mouse_dx_ = 0.0f;
  float mouse_dy_ = 0.0f;

  // UI thread only.
  int32_t prev_mouse_x_ = 0;
  int32_t prev_mouse_y_ = 0;
  bool mouse_captured_ = false;
  // Whether the window has the pointer locked and is reporting relative motion.
  bool relative_mouse_mode_ = false;
  // Cursor visibility to restore on capture release - the window owner may run
  // an auto-hide policy that capture must not permanently override.
  rex::ui::Window::CursorVisibility precapture_cursor_visibility_ =
      rex::ui::Window::CursorVisibility::kVisible;

  // Guest thread to UI thread. The queued flag coalesces the posts.
  std::atomic<bool> mouse_capture_requested_{false};
  std::atomic<bool> mouse_capture_update_queued_{false};

  std::atomic<bool> has_focus_{true};

  // The bindings as last parsed, per bind (the enum in the .cpp), and the
  // modifiers bound as keys of their own.
  std::vector<std::string> bind_text_;
  std::vector<std::vector<keyboard::BindToken>> bind_tokens_;
  uint8_t ignored_modifiers_ = 0;

  // The model's values and targets, in the title's terms: steering -1..1,
  // throttle and brake 0..1.
  double steer_ = 0.0;
  double throttle_ = 0.0;
  double brake_ = 0.0;
  double steer_target_ = 0.0;
  double throttle_target_ = 0.0;
  double brake_target_ = 0.0;
  keyboard::SteeringKeys steering_keys_;
  Clock::time_point model_time_{};
  bool model_running_ = false;
  // Mouse steering, -1..1, and the time of its last decay.
  double mouse_steer_ = 0.0;
  Clock::time_point mouse_time_{};
  bool mouse_running_ = false;

  // Keystroke queue
  std::queue<X_INPUT_KEYSTROKE> keystroke_queue_;

  // Packet number incremented on state change
  uint32_t packet_number_ = 0;
};

}  // namespace rex::input::mnk
