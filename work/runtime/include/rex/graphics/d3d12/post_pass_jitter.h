/**
 * @file        graphics/d3d12/post_pass_jitter.h
 * @brief       The sub-pixel jitter of the scene draws for temporal post passes
 *
 * Header only and free of runtime dependencies, so the CPU tests check the
 * same math as post_pass.cpp and the command processor. The jitter is a shift
 * of the scene's image by less than half a host pixel per frame, added to the
 * translated vertex shaders' ndc_offset: the content at host position p is the
 * unjittered scene at p - jitter (SceneConstants::jitter). The jitter is armed
 * only while it pays (JitterGate): while the scene camera moves, at a frame
 * rate at which its phases cycle faster than the eye follows. See
 * docs/analysis/taa.md.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include <rex/graphics/d3d12/post_pass_camera.h>

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

// The scene camera's motion between the previous frame and this one in host
// pixels: the largest move of the static world at clip W = distance seen
// through the center, the corners and the edge midpoints of the scene
// (guest_width x guest_height guest pixels), reprojected with
// view_to_previous_view (ViewToPreviousView without world matrices) and both
// frames' unpack constants. Rotation, translation and a change of the focal
// length all count; host_per_guest scales guest pixels to host pixels. A point
// that falls behind the previous camera gives a huge motion.
inline double CameraMotionPixels(const MatrixD& view_to_previous_view, const Unpack& now,
                                 const Unpack& previous, double guest_width, double guest_height,
                                 double host_per_guest_x, double host_per_guest_y,
                                 double distance) {
  const MatrixD& m = view_to_previous_view;
  double largest = 0.0;
  for (int iy = 0; iy < 3; ++iy) {
    for (int ix = 0; ix < 3; ++ix) {
      double x = guest_width * 0.5 * ix, y = guest_height * 0.5 * iy;
      const double view[3] = {distance * (x * now.x_mul + now.x_add),
                              distance * (y * now.y_mul + now.y_add), distance * now.z_from_w};
      double moved[3];
      for (int i = 0; i < 3; ++i) {
        moved[i] = m.m[i][0] * view[0] + m.m[i][1] * view[1] + m.m[i][2] * view[2] + m.m[i][3];
      }
      double previous_w = moved[2] / previous.z_from_w;
      if (!(previous_w > distance * 1.0e-3)) {
        return 1.0e9;
      }
      double previous_x = (moved[0] / previous_w - previous.x_add) / previous.x_mul;
      double previous_y = (moved[1] / previous_w - previous.y_add) / previous.y_mul;
      largest = std::fmax(largest, std::hypot((previous_x - x) * host_per_guest_x,
                                              (previous_y - y) * host_per_guest_y));
    }
  }
  return largest;
}

// When the jitter pays. Its phases average out only while the history follows
// a moving picture: on a still camera TAA's color clip keeps a residue of each
// phase, and the picture shimmers, most visibly at 30 fps, where 8 phases
// repeat 3.75 times a second. So the jitter is armed while the scene camera
// moves and the frames come fast enough (docs/analysis/taa.md, section 3.5).
struct JitterGateSettings {
  // The camera motion in host pixels per frame (CameraMotionPixels) that arms
  // the jitter; 0 or less: a still camera too.
  double min_motion = 0.0;
  // Frames the jitter stays armed after the last frame with that motion, so a
  // camera near the threshold does not switch it on and off every frame.
  uint32_t hold = 8;
  // The frame rate the jitter needs (JitterGate::FrameRate); 0 or less: any.
  double min_frame_rate = 0.0;
};

class JitterGate {
 public:
  // Frame times in the median of FrameRate.
  static constexpr uint32_t kRateFrames = 8;

  // At the end of every frame: the scene camera's motion in it in host pixels
  // (negative if unknown: no scene or view, a camera cut) and the time since the
  // end of the previous frame in seconds (0 or less if unknown).
  void EndFrame(double motion_pixels, double frame_seconds, const JitterGateSettings& settings) {
    motion_ = motion_pixels;
    if (motion_pixels >= 0.0 && motion_pixels >= settings.min_motion) {
      frames_still_ = 0;
    } else if (frames_still_ < UINT32_MAX) {
      ++frames_still_;
    }
    if (frame_seconds > 0.0) {
      seconds_[next_] = frame_seconds;
      next_ = (next_ + 1) % kRateFrames;
      count_ = std::min(count_ + 1, kRateFrames);
    }
  }

  // Frames per second, from the median of the last kRateFrames frame times (a
  // single long frame does not move it); 0 before any is known.
  double FrameRate() const {
    if (!count_) {
      return 0.0;
    }
    std::array<double, kRateFrames> sorted = seconds_;
    std::sort(sorted.begin(), sorted.begin() + count_);
    double median = count_ & 1 ? sorted[count_ / 2]
                               : 0.5 * (sorted[count_ / 2 - 1] + sorted[count_ / 2]);
    return median > 0.0 ? 1.0 / median : 0.0;
  }

  // The next frame may be jittered.
  bool Armed(const JitterGateSettings& settings) const {
    bool moving = settings.min_motion <= 0.0 || frames_still_ < settings.hold;
    bool fast = settings.min_frame_rate <= 0.0 || FrameRate() >= settings.min_frame_rate;
    return moving && fast;
  }

  // The last frame's motion (negative if unknown), and the frames since the
  // camera last moved by the threshold (0: in the last frame).
  double motion() const { return motion_; }
  uint32_t frames_still() const { return frames_still_; }

 private:
  double motion_ = -1.0;
  uint32_t frames_still_ = UINT32_MAX;
  std::array<double, kRateFrames> seconds_ = {};
  uint32_t next_ = 0, count_ = 0;
};

}  // namespace rex::graphics::d3d12::post_pass
