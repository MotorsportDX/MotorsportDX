#pragma once
#include <rex/input/input_driver.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/virtual_key.h>
#include "fm4-diagnostic-state.h"
#include <chrono>
#include <fstream>
#include <mutex>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

// Opt-in, application-local controller for reproducible startup tests. It never
// sends desktop input. Missing or malformed files mean all controls released.
class FM4TestInput final : public rex::input::InputDriver {
 public:
  using X_STATUS = rex::X_STATUS;
  using X_RESULT = rex::X_RESULT;
  explicit FM4TestInput(std::string path)
      : InputDriver(nullptr, 0), path_(std::move(path)) {}
  X_STATUS Setup() override { return X_STATUS_SUCCESS; }
  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
    rex::input::DeviceInfo info;
    info.id = kDevice;
    info.name = "FM4 diagnostic controller";
    info.synthetic = true;
    out.push_back(info);
  }
  X_RESULT GetDeviceState(rex::input::DeviceId id, rex::input::X_INPUT_STATE* out) override {
    if (id != kDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
    std::lock_guard lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_read_) {
      next_read_ = now + std::chrono::milliseconds(50);
      int buttons = 0, lt = 0, rt = 0, lx = 0, ly = 0, rx = 0, ry = 0, pulse = 0;
      long long from = -1;
      std::ifstream input(path_);
      std::stringstream text;
      text << input.rdbuf();
      // An optional line "keys <names>" holds keyboard keys (keybind names separated by
      // spaces, '+' or ','), which the keyboard driver treats as held beside the real ones,
      // through its bindings and steering model (docs/analysis/keyboard-controls.md).
      std::string keys;
      for (std::string line; std::getline(text, line);) {
        if (line.rfind("keys ", 0) == 0) keys = line.substr(5);
      }
      text.clear();
      text.seekg(0);
      std::istream& file = text;
      if (keys != keys_) {
        keys_ = keys;
        std::vector<uint16_t> held;
        std::string name;
        for (char c : keys + " ") {
          if (c == ' ' || c == '+' || c == ',' || c == '\r') {
            if (!name.empty() && name != "none") {
              const auto vk = rex::ui::ParseVirtualKey(name);
              if (vk != rex::ui::VirtualKey::kNone) held.push_back(uint16_t(vk));
              else std::fprintf(stderr, "FM4 test input: unknown key %s\n", name.c_str());
            }
            name.clear();
          } else {
            name += c;
          }
        }
        rex::input::mnk::SetInjectedKeys(held);
        std::fprintf(stderr, "FM4 test input: keys=%s (%zu held) packet=%u\n", keys.c_str(),
                     held.size(), packet_ + 1);
      }
      rex::input::X_INPUT_STATE next{};
      uint16_t next_pulse = 0;
      int64_t next_from = -1;
      if ((file >> buttons >> lt >> rt >> lx >> ly >> rx >> ry) &&
          buttons >= 0 && buttons <= 65535 && lt >= 0 && lt <= 255 && rt >= 0 && rt <= 255 &&
          lx >= -32768 && lx <= 32767 && ly >= -32768 && ly <= 32767 &&
          rx >= -32768 && rx <= 32767 && ry >= -32768 && ry <= 32767) {
        next.gamepad.buttons = uint16_t(buttons);
        next.gamepad.left_trigger = uint8_t(lt);
        next.gamepad.right_trigger = uint8_t(rt);
        next.gamepad.thumb_lx = int16_t(lx);
        next.gamepad.thumb_ly = int16_t(ly);
        next.gamepad.thumb_rx = int16_t(rx);
        next.gamepad.thumb_ry = int16_t(ry);
        // Optional eighth and ninth fields: pulsed buttons and the race frame they start
        // on (see below). A ninth field that is not a frame from 0 to 2^31 - 1 is ignored,
        // so the pulse starts at once.
        if ((file >> pulse) && pulse > 0 && pulse <= 65535) {
          next_pulse = uint16_t(pulse);
          if ((file >> from) && from >= 0 && from <= 0x7FFFFFFF) next_from = from;
        }
      }
      next.packet_number = ++packet_;
      if (uint16_t(next.gamepad.buttons) != uint16_t(state_.gamepad.buttons))
        std::fprintf(stderr, "FM4 test input: buttons=%u packet=%u\n", uint16_t(next.gamepad.buttons), packet_);
      if (next_pulse != pulse_ || next_from != pulse_from_) {
        std::fprintf(stderr, "FM4 test input: pulse=%u from race frame %lld after %u pulsed polls\n",
                     next_pulse, static_cast<long long>(next_from), pulse_polls_);
        pulse_polls_ = 0;
        pulse_start_ = 0;
      }
      pulse_ = next_pulse;
      pulse_from_ = next_from;
      state_ = next;
      fm4_diagnostic_buttons.store(uint16_t(state_.gamepad.buttons), std::memory_order_relaxed);
    }
    if (out) {
      *out = state_;
      const uint32_t poll = fm4_diagnostic_polls.fetch_add(1, std::memory_order_relaxed) + 1;
      // Pulsed buttons alternate between pressed and released on each state poll of the
      // guest, starting with pressed, instead of following the wall clock. In a race the
      // title polls about once per frame (twice in the countdown) and its buttons are
      // edge-triggered, so a pulsed button acts on the first frame the title accepts it, or
      // the one after. With a race frame the pulse waits for that frame instead of starting
      // at once. Race frames count the title's session updates since it pushed its race
      // render scenario (fm4-vblank-rate.cpp, fm4-race-clock.cpp), not polls: now and then
      // a frame polls twice or not at all, so a poll count drifts against the race. The
      // pulse starts on the first poll at or after that frame, so the first press lands on
      // the same session update in every run. Outside a race (before it starts, after its
      // scenario is popped) the pulse waits. Keystrokes do not see pulses.
      if (pulse_) {
        int64_t index = pulse_polls_;
        if (pulse_from_ >= 0) {
          const uint32_t race = fm4_diagnostic_race_update.load(std::memory_order_relaxed);
          if (race != pulse_race_) {
            pulse_race_ = race;
            pulse_start_ = 0;
          }
          const uint32_t updates = fm4_diagnostic_updates.load(std::memory_order_relaxed);
          if (race && !pulse_start_ && int64_t(updates - race) >= pulse_from_) {
            pulse_start_ = poll;
            std::fprintf(stderr,
                         "FM4 test input: pulse starts at race frame %lld (session update %u, "
                         "input poll %u, swap %u, session time %.6f)\n",
                         static_cast<long long>(updates - race), updates, poll,
                         fm4_diagnostic_swaps.load(std::memory_order_relaxed),
                         fm4_diagnostic_session_time.load(std::memory_order_relaxed));
          }
          index = pulse_start_ ? int64_t(poll - pulse_start_) : -1;
        }
        if (index >= 0) {
          ++pulse_polls_;
          if (index % 2 == 0) out->gamepad.buttons = uint16_t(uint16_t(out->gamepad.buttons) | pulse_);
        }
      }
    }
    return X_ERROR_SUCCESS;
  }
  X_RESULT GetDeviceCapabilities(rex::input::DeviceId id, uint32_t,
                                rex::input::X_INPUT_CAPABILITIES* out) override {
    if (id != kDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (out) {
      *out = {};
      out->type = 1;
      out->sub_type = 1;
      // Match the standard SDL gamepad's supported-bit masks. Capabilities
      // describe available bits, not the largest positive analog value.
      out->gamepad.buttons = uint16_t(0xF3FF);
      out->gamepad.left_trigger = out->gamepad.right_trigger = 255;
      out->gamepad.thumb_lx = out->gamepad.thumb_ly = int16_t(-1);
      out->gamepad.thumb_rx = out->gamepad.thumb_ry = int16_t(-1);
      out->vibration.left_motor_speed = out->vibration.right_motor_speed = uint16_t(0xFFFF);
      // A local diagnostic switch allows comparison without rebuilding.
      int legacy = 0;
      std::ifstream(path_ + ".legacy-caps") >> legacy;
      if (legacy) {
        out->gamepad.buttons = uint16_t(0xFFFF);
        out->gamepad.thumb_lx = out->gamepad.thumb_ly = int16_t(32767);
        out->gamepad.thumb_rx = out->gamepad.thumb_ry = int16_t(32767);
        out->vibration.left_motor_speed = out->vibration.right_motor_speed = 0;
      }
    }
    return X_ERROR_SUCCESS;
  }
  X_RESULT SetDeviceVibration(rex::input::DeviceId id, rex::input::X_INPUT_VIBRATION*) override {
    return id == kDevice ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
  }
  X_RESULT GetDeviceKeystroke(rex::input::DeviceId id, uint32_t,
                             rex::input::X_INPUT_KEYSTROKE* out) override {
    if (id != kDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (!out) return X_ERROR_BAD_ARGUMENTS;
    GetDeviceState(id, nullptr);
    std::lock_guard lock(mutex_);
    using Key = rex::ui::VirtualKey;
    static constexpr Key keys[] = {
      Key::kXInputPadDpadUp, Key::kXInputPadDpadDown, Key::kXInputPadDpadLeft,
      Key::kXInputPadDpadRight, Key::kXInputPadStart, Key::kXInputPadBack,
      Key::kXInputPadLThumbPress, Key::kXInputPadRThumbPress,
      Key::kXInputPadLShoulder, Key::kXInputPadRShoulder, Key::kNone, Key::kNone,
      Key::kXInputPadA, Key::kXInputPadB, Key::kXInputPadX, Key::kXInputPadY};
    if (++keystroke_queries_ <= 3)
      std::fprintf(stderr, "FM4 diagnostic keystroke query n=%u\n", keystroke_queries_);
    const uint16_t current = state_.gamepad.buttons;
    for (unsigned bit = 0; bit < 16; ++bit) {
      const uint16_t mask = uint16_t(1u << bit);
      if (!((current ^ keystroke_buttons_) & mask)) continue;
      keystroke_buttons_ ^= mask;
      if (keys[bit] == Key::kNone) continue;
      *out = {};
      out->virtual_key = uint16_t(keys[bit]);
      out->flags = current & mask ? rex::input::X_INPUT_KEYSTROKE_KEYDOWN : rex::input::X_INPUT_KEYSTROKE_KEYUP;
      std::fprintf(stderr, "FM4 diagnostic keystroke key=%x flags=%x\n", uint16_t(out->virtual_key), uint16_t(out->flags));
      return X_ERROR_SUCCESS;
    }
    return X_ERROR_EMPTY;
  }
 private:
  static constexpr rex::input::DeviceId kDevice = static_cast<rex::input::DeviceId>(0x464D3400);
  std::string path_;
  std::mutex mutex_;
  std::chrono::steady_clock::time_point next_read_{};
  uint32_t packet_ = 0;
  std::string keys_;
  uint16_t pulse_ = 0;
  int64_t pulse_from_ = -1;
  uint32_t pulse_polls_ = 0;
  uint32_t pulse_race_ = 0;   // the race (fm4_diagnostic_race_update) of pulse_start_
  uint32_t pulse_start_ = 0;  // the poll the pulse started on; 0 before its race frame
  uint16_t keystroke_buttons_ = 0;
  unsigned keystroke_queries_ = 0;
  rex::input::X_INPUT_STATE state_{};
};
