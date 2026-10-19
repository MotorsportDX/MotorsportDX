/**
 * @file        graphics/d3d12/post_pass_jitter.h
 * @brief       The sub-pixel jitter of the scene draws for temporal post passes
 *
 * Header only and free of runtime dependencies, so the CPU tests check the
 * same math as post_pass.cpp and the command processor. The jitter is a shift
 * of the scene's image by less than half a host pixel per frame, added to the
 * translated vertex shaders' ndc_offset: the content at host position p is the
 * unjittered scene at p - jitter (SceneConstants::jitter). See
 * docs/analysis/taa.md.
 */

#pragma once

#include <cstdint>

namespace rex::graphics::d3d12::post_pass {

// Phases of the [jitter] sequence of a manifest, at most.
constexpr uint32_t kMaxJitterPhases = 64;

// The Halton sequence: the radical inverse of index in base, 0 to 1.
inline float Halton(uint32_t index, uint32_t base) {
  float result = 0.0f, fraction = 1.0f;
  while (index) {
    fraction /= float(base);
    result += fraction * float(index % base);
    index /= base;
  }
  return result;
}

// The jitter of a phase (0 to phases - 1) in host pixels: Halton (2, 3) of
// phase + 1, less 0.5, so between -0.5 and 0.5 and never half a pixel.
inline void JitterOffset(uint32_t phase, float out[2]) {
  out[0] = Halton(phase + 1, 2) - 0.5f;
  out[1] = Halton(phase + 1, 3) - 0.5f;
}

// What decides whether a draw is a draw of the scene, which the jitter moves.
struct JitterDraw {
  // The EDRAM base of the draw's depth buffer, and of the scene depth (known
  // once a scene depth was resolved).
  uint32_t depth_base = 0;
  bool scene_depth_known = false;
  uint32_t scene_depth_base = 0;
  // The draw tests depth or stencil (post chain draws do neither, even with
  // the scene's depth buffer still named).
  bool depth_or_stencil = false;
  // A rectangle list, or a clear the render target cache draws as one.
  bool rectangle = false;
  // Its vertex shader is in the manifest's [jitter] exclude.
  bool excluded = false;
};

inline bool IsJitteredDraw(const JitterDraw& draw) {
  return draw.scene_depth_known && draw.depth_base == draw.scene_depth_base &&
         draw.depth_or_stencil && !draw.rectangle && !draw.excluded;
}

// Adds the jitter (host pixels, x right, y down) to the ndc_offset of a draw
// whose host viewport is extent host pixels (draw_util::ViewportInfo, both
// branches of GetHostViewportInfo): Direct3D's normalized device Y points up.
inline void AddJitterToNdcOffset(const float jitter[2], const uint32_t extent[2],
                                 float ndc_offset[3]) {
  if (extent[0]) ndc_offset[0] += 2.0f * jitter[0] / float(extent[0]);
  if (extent[1]) ndc_offset[1] -= 2.0f * jitter[1] / float(extent[1]);
}

}  // namespace rex::graphics::d3d12::post_pass
