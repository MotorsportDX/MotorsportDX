/**
 * @file        input/keyboard_model.h
 * @brief       The keyboard's steering and pedal model and its bindings
 *
 * Header only and free of runtime dependencies, so the CPU tests
 * (work/fm4-rex/tests/keyboard-model-tests.cpp) check the same math as the
 * keyboard and mouse driver. See docs/analysis/keyboard-controls.md.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace rex::input::keyboard {

// How fast an analog control driven by keys moves, in seconds for the whole
// range (0 to full, or full to 0). 0 moves at once.
struct AxisRates {
  double attack = 0.0;
  double release = 0.0;
};

namespace detail {
inline double Rate(double seconds) {
  return seconds > 0.0 ? 1.0 / seconds : std::numeric_limits<double>::infinity();
}
inline double Sign(double value) {
  return value < 0.0 ? -1.0 : 1.0;
}
}  // namespace detail

// Moves 'value' toward 'target' for 'dt' seconds and returns it, in the
// title's terms (steering -1..1, a pedal 0..1): away from 0 at the attack
// rate, toward 0 at the release rate, and toward a target on the other side of
// 0 at the faster of the two until the value crosses 0, then at the attack
// rate. The path is piecewise linear, so advancing in several steps gives the
// same value as one step over their sum, which keeps the model the same at
// every poll rate.
inline double AdvanceAxis(double value, double target, double dt, const AxisRates& rates) {
  const double attack = detail::Rate(rates.attack);
  const double release = detail::Rate(rates.release);
  dt = std::max(dt, 0.0);
  while (value != target) {
    if ((value > 0.0 && target < 0.0) || (value < 0.0 && target > 0.0)) {
      // A reversal: back to 0 first.
      const double rate = std::max(attack, release);
      const double distance = std::fabs(value);
      if (std::isinf(rate) || rate * dt >= distance) {
        dt -= std::isinf(rate) ? 0.0 : distance / rate;
        value = 0.0;
        continue;
      }
      return value - detail::Sign(value) * rate * dt;
    }
    const double rate = std::fabs(target) > std::fabs(value) ? attack : release;
    const double distance = std::fabs(target - value);
    if (std::isinf(rate) || rate * dt >= distance) {
      return target;
    }
    return value + detail::Sign(target - value) * rate * dt;
  }
  return value;
}

// The steering keys' direction: -1 while the left key is held, +1 for the
// right one, and with both held the one pressed last (a quick counter-steer
// needs no release first). 0 with neither.
class SteeringKeys {
 public:
  double Update(bool left, bool right) {
    if (left && !left_) {
      left_order_ = ++order_;
    }
    if (right && !right_) {
      right_order_ = ++order_;
    }
    left_ = left;
    right_ = right;
    if (left && right) {
      return left_order_ > right_order_ ? -1.0 : 1.0;
    }
    return left ? -1.0 : right ? 1.0 : 0.0;
  }

 private:
  bool left_ = false;
  bool right_ = false;
  uint64_t left_order_ = 0;
  uint64_t right_order_ = 0;
  uint64_t order_ = 0;
};

// The range of one pad axis that the title maps to 0..1 of its control, in the
// pad's raw units (0..32767 for a stick's magnitude, 0..255 for a trigger):
// at or below 'inner' the title reads 0, at 'outer' and above full.
struct TitleRange {
  double inner = 0.0;
  double outer = 0.0;
};

// The ranges the title applies to the axes the keyboard drives.
struct TitleRanges {
  TitleRange steering{0.0, 32767.0};  // the left stick's magnitude, radial
  TitleRange left_trigger{0.0, 255.0};
  TitleRange right_trigger{0.0, 255.0};
};

// The raw left stick X that makes the title's radial deadzone return 'value'
// (-1..1) with the stick's Y at 0: the title computes
// (|x| - inner) / (outer - inner), so the raw value starts just past 'inner'
// and grows linearly. Rounded away from 0, so any non-zero value is past the
// deadzone. 0 gives 0.
inline int16_t StickFromTitle(double value, const TitleRange& range) {
  if (value == 0.0) {
    return 0;
  }
  const double magnitude =
      range.inner + std::min(std::fabs(value), 1.0) * (range.outer - range.inner);
  const double raw = std::min(std::ceil(magnitude), 32767.0);
  return static_cast<int16_t>(detail::Sign(value) * raw);
}

// The raw trigger that makes the title's trigger range return 'value' (0..1),
// rounded up to the next step past 'inner' so that any non-zero value counts.
// 0 gives 0.
inline uint8_t TriggerFromTitle(double value, const TitleRange& range) {
  if (value <= 0.0) {
    return 0;
  }
  double raw = std::ceil(range.inner + std::min(value, 1.0) * (range.outer - range.inner));
  raw = std::max(raw, std::floor(range.inner) + 1.0);
  return static_cast<uint8_t>(std::clamp(raw, 0.0, 255.0));
}

// Bindings: comma-separated alternatives, each with optional modifier
// prefixes ("Q,I" or "Shift+Up"). The modifier codes are rex::ui::VirtualKey's.
constexpr uint16_t kVkShift = 0x10;
constexpr uint16_t kVkControl = 0x11;
constexpr uint16_t kVkAlt = 0x12;
constexpr uint8_t kModShift = 1u << 0;
constexpr uint8_t kModCtrl = 1u << 1;
constexpr uint8_t kModAlt = 1u << 2;

inline uint8_t ModifierOfKey(uint16_t vk) {
  switch (vk) {
    case kVkShift:
      return kModShift;
    case kVkControl:
      return kModCtrl;
    case kVkAlt:
      return kModAlt;
    default:
      return 0;
  }
}

// One alternative: a key and the modifiers it wants. vk 0 never fires.
struct BindToken {
  uint16_t vk = 0;
  uint8_t mods = 0;
};

inline std::string_view TrimSpaces(std::string_view s) {
  while (!s.empty() && s.front() == ' ') {
    s.remove_prefix(1);
  }
  while (!s.empty() && s.back() == ' ') {
    s.remove_suffix(1);
  }
  return s;
}

// Parses a binding; 'parse_key' maps a bare key name to its virtual key (0 for
// an unknown name).
template <typename ParseKey>
std::vector<BindToken> ParseBind(std::string_view text, ParseKey&& parse_key) {
  std::vector<BindToken> tokens;
  while (!text.empty()) {
    const size_t comma = text.find(',');
    std::string_view token = TrimSpaces(text.substr(0, comma));
    text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
    if (token.empty()) {
      continue;
    }
    BindToken parsed;
    for (;;) {
      const size_t plus = token.find('+');
      if (plus == std::string_view::npos || plus == 0) {
        break;
      }
      const std::string_view head = token.substr(0, plus);
      uint8_t bit = 0;
      if (head == "Shift") {
        bit = kModShift;
      } else if (head == "Ctrl" || head == "Control") {
        bit = kModCtrl;
      } else if (head == "Alt") {
        bit = kModAlt;
      } else {
        break;
      }
      parsed.mods |= bit;
      token.remove_prefix(plus + 1);
    }
    parsed.vk = static_cast<uint16_t>(parse_key(token));
    if (parsed.vk != 0 && parsed.vk < 256) {
      tokens.push_back(parsed);
    }
  }
  return tokens;
}

inline uint8_t LiveModifiers(const bool (&down)[256]) {
  uint8_t mods = 0;
  if (down[kVkShift]) mods |= kModShift;
  if (down[kVkControl]) mods |= kModCtrl;
  if (down[kVkAlt]) mods |= kModAlt;
  return mods;
}

// The modifiers that a binding names as its key ("Control" for the precision
// key): they are left out of every binding's modifier comparison, so the other
// bindings keep matching while one is held.
inline uint8_t BareModifiers(const std::vector<BindToken>& tokens) {
  uint8_t mods = 0;
  for (const BindToken& token : tokens) {
    mods |= ModifierOfKey(token.vk);
  }
  return mods;
}

// Whether a binding is held. Modifiers match exactly ("Up" is silent while
// Shift is held, "Shift+Up" without it), except those in 'ignored'
// (BareModifiers of every binding); a binding whose key is a modifier matches
// while that key is held.
inline bool BindHeld(const std::vector<BindToken>& tokens, const bool (&down)[256],
                     uint8_t live_mods, uint8_t ignored) {
  for (const BindToken& token : tokens) {
    if (!down[token.vk]) {
      continue;
    }
    const uint8_t own = ModifierOfKey(token.vk);
    const uint8_t mask = static_cast<uint8_t>(~(ignored | own));
    if ((live_mods & mask) == (token.mods & mask)) {
      return true;
    }
  }
  return false;
}

}  // namespace rex::input::keyboard
