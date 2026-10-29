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

#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <rex/input/input_driver.h>

#include <SDL3/SDL.h>

#define HID_SDL_THUMB_THRES 0x4E00
#define HID_SDL_TRIGG_THRES 0x1F
#define HID_SDL_REPEAT_DELAY 400
#define HID_SDL_REPEAT_RATE 100

namespace rex::input::wheel {
class WheelHost;
}

namespace rex::input::sdl {

class SDLInputDriver final : public InputDriver, public rex::ui::WindowListener {
 public:
  explicit SDLInputDriver(rex::ui::Window* window, size_t window_z_order);
  ~SDLInputDriver() override;

  X_STATUS Setup() override;

  void EnumerateDevices(std::vector<DeviceInfo>& out) override;
  X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) override;
  X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t flags,
                                 X_INPUT_CAPABILITIES* out_caps) override;
  X_RESULT SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) override;
  void SetDeviceUserState(DeviceId id, const X_INPUT_GAMEPAD& gamepad) override;
  X_RESULT GetDeviceKeystroke(DeviceId id, uint32_t flags,
                              X_INPUT_KEYSTROKE* out_keystroke) override;
  void OnWindowAvailable(rex::ui::Window* window) override;
  void StopVibration() override;

 private:
  enum class RepeatState {
    Idle,       // no buttons pressed or repeating has ended
    Waiting,    // a button is held and the delay is awaited
    Repeating,  // actively repeating at a rate
  };
  struct KeystrokeState {
    uint64_t buttons;
    RepeatState repeat_state;
    // the button number that was pressed last:
    uint8_t repeat_butt_idx;
    // the last time (ms) a down (and/or repeat) event for that button was send:
    uint32_t repeat_time;
  };

  struct ControllerState {
    SDL_Gamepad* sdl;
    X_INPUT_CAPABILITIES caps;
    X_INPUT_STATE state;
    bool state_changed;
    bool is_active;
    DeviceId id;
    // Per pad rather than per guest user, so it survives reassignment.
    KeystrokeState keystroke;
    // The SDL buttons held, one bit per SDL_GamepadButton below kSdlButtonCount;
    // the guest buttons are their mapped union.
    uint32_t sdl_buttons = 0;
    // The pad reports trigger motors (SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN),
    // and the last trigger rumble sent to it was not zero.
    bool trigger_rumble_capable = false;
    bool trigger_rumble_sent = false;
    // The guest's last read of this pad's user (InputSystem, SetDeviceUserState).
    uint8_t user_left_trigger = 0;
    uint8_t user_right_trigger = 0;
    // The latency probe's virtual pad (input_latency_probe): its last right stick X and the
    // SDL time of the update that sent it, until a read takes it.
    bool probe = false;
    bool probe_unread = false;
    int16_t probe_value = 0;
    uint64_t probe_pumped_ns = 0;
  };
  struct LatencyProbe;

  // WindowListener
  void OnClosing(rex::ui::UIEvent& e) override;
  void OnLostFocus(rex::ui::UISetupEvent& e) override;
  void OnGotFocus(rex::ui::UISetupEvent& e) override;

  void HandleEvent(const SDL_Event& event);
  std::unique_lock<std::mutex> DrainAndLock();
  void ProcessEventLocked(const SDL_Event& event);
  void OnControllerDeviceAddedLocked(const SDL_Event& event);
  void OnControllerDeviceRemovedLocked(const SDL_Event& event);
  void OnControllerDeviceAxisMotionLocked(const SDL_Event& event);
  void OnControllerDeviceButtonChangedLocked(const SDL_Event& event);

  inline uint64_t AnalogToKeyfield(const X_INPUT_GAMEPAD& gamepad) const;
  std::optional<size_t> GetControllerIndexFromInstanceID(SDL_JoystickID instance_id);
  ControllerState* FindController(DeviceId id);
  DeviceId AllocateDeviceId();
  bool TestSDLVersion() const;
  void UpdateXCapabilities(ControllerState& state);
  void QueueControllerUpdate();
  // The SDL joystick driver hints of sdl_joystick_driver, before SDL_INIT_GAMEPAD.
  void ApplyDriverHints();
  // sdl_joystick_driver=gameinput: one line on whether a GameInput v3 runtime starts.
  void LogGameInputAvailability();
  // The guest button an SDL button maps to (0: none).
  uint16_t GuestButton(uint32_t sdl_button) const;
  void LogPadReport(const ControllerState& state) const;
  void StopTriggerRumbleLocked();
  void StartInputThread();
  void StopInputThread();
  void StartLatencyProbe();
  void StopLatencyProbe();
  void ObserveProbeReadLocked(ControllerState& controller);
  // wheel: the racing wheel's host (rex/input/wheel/wheel_host.h), on the UI thread.
  void StartWheel();
  // The focus the wheel host gets: the window's, or always with the recording backend.
  bool WheelFocus() const;
  void StopWheel();
  // wheel_virtual: an SDL virtual wheel driven by a script, for checks without hardware.
  struct VirtualWheel;
  void StartVirtualWheel();
  void StopVirtualWheel();

  rex::ui::Window* attached_window_ = nullptr;
  bool sdl_events_initialized_;
  bool SDL_Gamepad_initialized_;
  std::atomic<int> sdl_events_unflushed_;
  std::atomic<bool> sdl_pumpevents_queued_;
  // Appended in connection order, never re-sorted, and unbounded: the
  // assignment decides how many the guest sees.
  std::vector<ControllerState> controllers_;
  std::mutex controllers_mutex_;
  std::mutex event_queue_mutex_;
  std::vector<SDL_Event> pending_events_;
  // Never rewound, so a handle held past a disconnect resolves to nothing.
  uint64_t next_device_id_ = 1;
  std::unique_ptr<LatencyProbe> probe_;
  // The guest buttons of SDL's Misc1, the four paddles and the touchpad click
  // (pad_misc1, pad_right_paddle1, ...), resolved at Setup.
  std::array<uint16_t, 6> extra_buttons_ = {};
  // The window's keyboard focus: trigger rumble stops without it, unless SDL reads the pads
  // in the background too (SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, set for the probe).
  std::atomic<bool> has_focus_{true};
  bool background_events_ = false;
  // StopVibration ran: the process is exiting, and no more rumble is sent.
  std::atomic<bool> vibration_stopped_{false};
  // input_sdl_thread: SDL's pad updates on a thread of their own. The guest's threads read
  // input_thread_running_ (QueueControllerUpdate), never input_thread_, which only the UI
  // thread starts and joins.
  std::thread input_thread_;
  std::atomic<bool> input_thread_running_{false};
  std::atomic<bool> input_thread_stop_{false};
  std::unique_ptr<rex::input::wheel::WheelHost> wheel_;
  // The SDL subsystems StartWheel started (SDL_INIT_JOYSTICK, SDL_INIT_HAPTIC).
  SDL_InitFlags wheel_subsystems_ = 0;
  std::unique_ptr<VirtualWheel> virtual_wheel_;
};

}  // namespace rex::input::sdl
