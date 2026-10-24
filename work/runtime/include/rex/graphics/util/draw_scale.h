/**
 * @file        graphics/util/draw_scale.h
 * @brief       A uniform draw resolution scale as a fraction
 *
 * Header only and free of runtime dependencies, so the CPU tests check the
 * same math as the runtime. The native path renders the scene at the panel's
 * own resolution, its height over 720: 3/2 for 1080 lines, 20/9 for 1600, 3
 * for 2160. The scale is the same along both axes. A whole scale has a
 * denominator of 1, and every function below then gives the integer result
 * the runtime computed before. See docs/analysis/d3d-hle-n5.md, section 18.
 */

#pragma once

#include <cstdint>
#include <numeric>
#include <optional>
#include <string_view>

namespace rex::graphics {

struct DrawScale {
  // The largest scale along an axis, as TextureCache::kMaxDrawResolutionScaleAlongAxis.
  static constexpr uint32_t kMax = 7;
  // The largest denominator accepted: enough for any panel height over 720.
  static constexpr uint32_t kMaxDenominator = 720;

  uint32_t numerator = 1;
  uint32_t denominator = 1;

  static constexpr DrawScale Whole(uint32_t scale) { return {scale, 1}; }

  // Reduced to lowest terms, or nothing if it is below 1, above kMax or its
  // denominator is above kMaxDenominator once reduced.
  static constexpr std::optional<DrawScale> Make(uint64_t numerator, uint64_t denominator) {
    if (!numerator || !denominator) {
      return std::nullopt;
    }
    uint64_t divisor = std::gcd(numerator, denominator);
    numerator /= divisor;
    denominator /= divisor;
    if (numerator < denominator || numerator > kMax * denominator ||
        denominator > kMaxDenominator) {
      return std::nullopt;
    }
    return DrawScale{uint32_t(numerator), uint32_t(denominator)};
  }

  // "3/2", "1.5", "2" or "1080/720"; nothing for anything else.
  static constexpr std::optional<DrawScale> Parse(std::string_view text) {
    uint64_t numerator = 0, denominator = 1;
    bool digits = false, point = false, slash = false;
    uint64_t divisor = 0;
    for (char c : text) {
      if (c >= '0' && c <= '9') {
        uint64_t& value = slash ? divisor : numerator;
        if (value > UINT64_C(100000000)) {
          return std::nullopt;
        }
        value = value * 10 + uint64_t(c - '0');
        if (point) {
          if (denominator > UINT64_C(100000000)) {
            return std::nullopt;
          }
          denominator *= 10;
        }
        digits = true;
      } else if (c == '.' && !point && !slash && digits) {
        point = true;
      } else if (c == '/' && !point && !slash && digits) {
        slash = true;
        digits = false;
      } else {
        return std::nullopt;
      }
    }
    if (!digits) {
      return std::nullopt;
    }
    if (slash) {
      denominator = divisor;
    }
    return Make(numerator, denominator);
  }

  constexpr bool IsWhole() const { return denominator == 1; }
  // The whole scale below or at this one.
  constexpr uint32_t Floor() const { return numerator / denominator; }
  constexpr float ToFloat() const { return float(numerator) / float(denominator); }

  // A guest length or coordinate in host pixels, rounded down or up.
  constexpr uint32_t ScaleDown(uint32_t guest) const {
    return uint32_t(uint64_t(guest) * numerator / denominator);
  }
  constexpr uint32_t ScaleUp(uint32_t guest) const {
    return uint32_t((uint64_t(guest) * numerator + denominator - 1) / denominator);
  }
  // A guest pixel edge in host pixels, rounded to the nearest, halves up. The
  // host pixels whose centres lie between two scaled edges are the ones a
  // rectangle between them covers by the top-left rule, so neighbouring
  // rectangles share their host edge and leave no gap.
  constexpr uint32_t ScaleEdge(uint32_t guest) const {
    return uint32_t((uint64_t(guest) * numerator * 2 + denominator) / (uint64_t(denominator) * 2));
  }

  constexpr bool operator==(const DrawScale& other) const {
    return numerator == other.numerator && denominator == other.denominator;
  }
};

}  // namespace rex::graphics
