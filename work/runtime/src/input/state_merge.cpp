/**
 * @file        input/state_merge.cpp
 * @brief       Combining several devices into one guest controller state.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <rex/input/state_merge.h>

#include <algorithm>
#include <cstdlib>

namespace rex::input {

namespace {

bool PastDeadzone(int16_t axis) {
  return std::abs(static_cast<int>(axis)) >= kThumbDeadzone;
}

}  // namespace

void MergeInto(X_INPUT_STATE& dst, const X_INPUT_STATE& src) {
  dst.gamepad.buttons =
      static_cast<uint16_t>(dst.gamepad.buttons) | static_cast<uint16_t>(src.gamepad.buttons);
  dst.gamepad.left_trigger = std::max(dst.gamepad.left_trigger, src.gamepad.left_trigger);
  dst.gamepad.right_trigger = std::max(dst.gamepad.right_trigger, src.gamepad.right_trigger);
  int16_t lx = dst.gamepad.thumb_lx;
  int16_t ly = dst.gamepad.thumb_ly;
  MergeStick(lx, ly, src.gamepad.thumb_lx, src.gamepad.thumb_ly);
  dst.gamepad.thumb_lx = lx;
  dst.gamepad.thumb_ly = ly;
  int16_t rx = dst.gamepad.thumb_rx;
  int16_t ry = dst.gamepad.thumb_ry;
  MergeStick(rx, ry, src.gamepad.thumb_rx, src.gamepad.thumb_ry);
  dst.gamepad.thumb_rx = rx;
  dst.gamepad.thumb_ry = ry;

  if (static_cast<uint32_t>(src.packet_number) > static_cast<uint32_t>(dst.packet_number)) {
    dst.packet_number = src.packet_number;
  }
}

bool IsNeutral(const X_INPUT_GAMEPAD& gamepad) {
  if (static_cast<uint16_t>(gamepad.buttons) != 0) {
    return false;
  }
  if (gamepad.left_trigger >= kTriggerThreshold || gamepad.right_trigger >= kTriggerThreshold) {
    return false;
  }
  return !PastDeadzone(gamepad.thumb_lx) && !PastDeadzone(gamepad.thumb_ly) &&
         !PastDeadzone(gamepad.thumb_rx) && !PastDeadzone(gamepad.thumb_ry);
}

void ActiveDeviceTracker::Observe(uint32_t user_index, DeviceId id,
                                  const X_INPUT_GAMEPAD& gamepad) {
  if (user_index >= kMaxGuestUsers || IsNeutral(gamepad)) {
    return;
  }
  active_[user_index] = id;
}

DeviceId ActiveDeviceTracker::Active(uint32_t user_index) const {
  if (user_index >= kMaxGuestUsers) {
    return DeviceId::kInvalid;
  }
  return active_[user_index];
}

void ActiveDeviceTracker::Forget(DeviceId id) {
  for (auto& active : active_) {
    if (active == id) {
      active = DeviceId::kInvalid;
    }
  }
}

}  // namespace rex::input
