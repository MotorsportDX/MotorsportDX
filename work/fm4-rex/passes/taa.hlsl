// Temporal anti-aliasing at the final point or the scene_color point, a post
// pass of the runtime's post pass framework (passes.toml, post_pass_path; see
// docs/analysis/taa.md and docs/analysis/post-pass-framework.md). Original code
// of this project.
//
// The scene draws are jittered by a sub-pixel Halton sequence ([jitter] of the
// manifest, xe_scene_jitter), and this pass accumulates the frames: the input
// is final_color, the final combine's output before the HUD (screen
// orientation, display referred), or with TAA_SCENE 1 scene_color, the scene
// before the game's post chain (the scene layout, gamma 2); the history is
// the previous frame's output (linear, the number of frames accumulated in
// alpha), moved by the motion vectors of the motion_vectors pass (scene
// layout, rotated 180 degrees against the screen) and limited to the colors
// around the pixel.
//
// Entry points of passes.toml:
//   taa_resolve  compute: the input, the scene depth, the motion vectors and
//                their mask, the previous history and the previous scene
//                depth -> this frame's history (RGBA16F) and, with TAA_DEBUG,
//                the debug views (RGBA8)
//   taa_output   pixel: the history sharpened (TAA_SHARPEN) and encoded, or a
//                debug view, drawn into final_color before the HUD; with
//                TAA_SCENE 1 into a texture that the framework writes back
//                into the game's scene (the game's alpha kept), so the motion
//                blur, bloom, exposure and final combine sample it
//
// Per pixel (taa_resolve):
// 1. The current frame without the jitter: the 3x3 neighbours weighted by a
//    Gaussian of the distance between the unjittered position of what each
//    shows and the pixel center (close to a Blackman-Harris window).
// 2. The box: mean and standard deviation of the 3x3 in YCoCg (TAA_CLIP_GAMMA
//    of them), within the 3x3 minimum and maximum.
// 3. The motion of the neighbour nearest the camera (the largest stored depth:
//    Z is reversed), so the edges of a moving car move with the car.
// 4. The history at the pixel plus that motion, filtered with Catmull-Rom
//    (TAA_HISTORY_FILTER 1) or bilinear (0).
// 5. A history whose previous scene depth (the farthest of its 3x3) was clearly
//    nearer than anything in the 3x3 now showed something else
//    (TAA_DISOCCLUSION).
// 6. The history clipped toward the center of the box.
// 7. The weight of the current frame: TAA_BLEND, more for a young history (1 /
//    frames accumulated), more with the motion (up to TAA_BLEND_MOTION at
//    TAA_MOTION_PIXELS), at least TAA_REACTIVE on reactive pixels (motion_mask
//    G, a tighter box there), all of it without a usable history (no
//    xe_history_valid(), motion_mask R, off the screen, disoccluded); colors
//    weighted by 1 / (1 + luma) so bright sub-pixel highlights do not flicker.
// 8. On a still camera (xe_camera_still(): [jitter] still, TAA_STILL), a pixel
//    that moves less than TAA_STILL_PIXELS accumulates the jitter's phases
//    instead, also where its motion is unknown or it is reactive (nothing
//    moves): it keeps a usable history, the weight of the current
//    frame is 1 / (frames accumulated + 1), down to 1 / (TAA_STILL_FRAMES + 1),
//    and the clip box is the minimum and maximum of the 5x5, which holds every
//    sample that any phase brings into the 3x3 of step 1 (the box of standard
//    deviations, or the 3x3 alone, cuts the history back to one phase's samples
//    and the still picture shimmers, most where an edge only just reaches a
//    pixel, since gamma 2 enlarges small linear steps near black).
// 9. The phases of a still camera (TAA_STILL_PHASES, at the final point): the
//    luma of the current frame of the last xe_scene_jitter_phases frames, one
//    per phase (taa_phases). Once the camera has been still for a whole cycle,
//    the current frame against the one of the same phase a cycle ago (the same
//    samples) tells what changes from what only the jitter moves. A pixel that
//    stayed within TAA_STILL_CHANGE levels for a whole cycle (taa_unchanged
//    counts the frames) keeps step 8, but its history is not clipped to the
//    box: its luma is held within that of every phase and of the 5x5, and its
//    color is left alone. A detail thinner than a pixel, which the samples of
//    some phases miss within the whole 5x5, was else clipped out of the history
//    in those phases and back in the others, and shimmered; so did a color that
//    one phase's 5x5 lacks (a red seam, a lit edge), since the clip moves luma
//    and color together. A pixel that changed in the last cycle (a flag in the
//    wind, an animated texture) blends as on a moving camera, so it does not
//    smear over the frames.
//
// Debug views (TAA_DEBUG, shown by taa_output instead of the image):
//   1 the history alone, moved by the motion (a frozen camera with TAA_JITTER
//     0 must stay still), magenta where there is none
//   2 the weight of the current frame: blue 0, red 1
//   3 how far the clip moved the history (orange, 8 times)
//   4 the reasons for no history: no motion known (mask R) red, reactive
//     (mask G) green, disoccluded yellow, off the screen or no valid history
//     blue, over the image darkened
//   5 the current frame without the jitter alone
//   6 where the phases accumulate (step 8): cyan, over the image darkened
//   7 the phases (step 9): green unchanged since a cycle ago, red changed, blue
//     a still pixel before a whole still cycle, over the image darkened
// Colors are never the clear gray, which e2e counts as missing geometry.

#ifndef TAA_SCENE
#define TAA_SCENE 0
#endif
// FM4 draws its scene rotated 180 degrees within the tiles: the scene textures
// are upside down and mirrored against the frame. At the scene_color point the
// input is the scene itself.
static const bool kSceneRotated = TAA_SCENE == 0;
// The size of the input, the history and the output.
#if TAA_SCENE
#define TAA_SIZE xe_scene_size
#else
#define TAA_SIZE xe_final_size
#endif
// Frames of history at most (the alpha of taa_history); on a still camera
// TAA_STILL_FRAMES.
static const float kMaxAge = 32.0;

// The stored values are gamma 2: squared, they are about linear.
float3 TaaLinear(float3 color) { return color * color; }
float3 TaaDisplay(float3 color) { return sqrt(max(color, 0.0)); }
float TaaLuma(float3 color) { return dot(color, float3(0.299, 0.587, 0.114)); }
float3 TaaToYCoCg(float3 color) {
  return float3(dot(color, float3(0.25, 0.5, 0.25)), dot(color, float3(0.5, 0.0, -0.5)),
                dot(color, float3(-0.25, 0.5, -0.25)));
}
float3 TaaFromYCoCg(float3 color) {
  return float3(color.x + color.y - color.z, color.x + color.z, color.x - color.y - color.z);
}

// The scene texture pixel of an input pixel (same sizes).
int2 TaaScenePixel(int2 final_pixel) {
  int2 last = int2(xe_scene_size) - 1;
  int2 pixel = clamp(final_pixel, 0, last);
  return kSceneRotated ? last - pixel : pixel;
}

// ---------------------------------------------------------------------------
// taa_resolve: one pixel per thread.

Texture2D<float4> taa_final : register(t0);
Texture2D<float> taa_depth : register(t1);
Texture2D<float2> taa_motion : register(t2);
Texture2D<float2> taa_mask : register(t3);
Texture2D<float4> taa_previous : register(t4);
Texture2D<float> taa_previous_depth : register(t5);
RWTexture2D<float4> taa_history_out : register(u0);
RWTexture2D<unorm float4> taa_debug_out : register(u1);
#if TAA_SCENE == 0
// Step 9: per phase the square root of the current frame's luma in levels of
// 255, 8 bits each from the lowest (phases 0 to 3 in x, 4 to 7 in y), and the
// frames since the pixel last changed (255 for none seen).
Texture2D<uint2> taa_previous_phases : register(t6);
Texture2D<uint> taa_previous_unchanged : register(t7);
RWTexture2D<uint2> taa_phases_out : register(u2);
RWTexture2D<uint> taa_unchanged_out : register(u3);
#endif
// The phases that step 9 keeps.
static const uint kPhaseSlots = 8u;

// The level of phase i in the packed phases.
uint TaaPhaseLevel(uint2 packed, uint i) {
  return ((i < 4u ? packed.x : packed.y) >> ((i & 3u) * 8u)) & 0xFFu;
}

// The previous history at an input position (pixel centers at + 0.5).
float4 TaaHistory(float2 position) {
  float2 inverse_size = 1.0 / float2(TAA_SIZE);
#if TAA_HISTORY_FILTER
  // Catmull-Rom in 5 bilinear taps (the corners' weights left out).
  float2 center = floor(position - 0.5) + 0.5;
  float2 f = position - center;
  float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
  float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
  float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
  float2 w3 = f * f * (-0.5 + 0.5 * f);
  float2 w12 = w1 + w2;
  float2 uv0 = (center - 1.0) * inverse_size;
  float2 uv3 = (center + 2.0) * inverse_size;
  float2 uv12 = (center + w2 / w12) * inverse_size;
  float4 sum =
      taa_previous.SampleLevel(xe_linear_clamp, float2(uv12.x, uv0.y), 0.0) * (w12.x * w0.y);
  sum += taa_previous.SampleLevel(xe_linear_clamp, float2(uv0.x, uv12.y), 0.0) * (w0.x * w12.y);
  sum += taa_previous.SampleLevel(xe_linear_clamp, uv12, 0.0) * (w12.x * w12.y);
  sum += taa_previous.SampleLevel(xe_linear_clamp, float2(uv3.x, uv12.y), 0.0) * (w3.x * w12.y);
  sum += taa_previous.SampleLevel(xe_linear_clamp, float2(uv12.x, uv3.y), 0.0) * (w12.x * w3.y);
  float weight = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
  return max(sum / weight, 0.0);
#else
  return taa_previous.SampleLevel(xe_linear_clamp, position * inverse_size, 0.0);
#endif
}

// The history clipped toward the center of a box (YCoCg).
float3 TaaClip(float3 history, float3 box_min, float3 box_max) {
  float3 center = 0.5 * (box_max + box_min);
  float3 extents = max(0.5 * (box_max - box_min), 1.0e-6);
  float3 offset = history - center;
  float3 units = abs(offset / extents);
  float outside = max(units.x, max(units.y, units.z));
  return outside > 1.0 ? center + offset / outside : history;
}

[numthreads(8, 8, 1)]
void taa_resolve(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= TAA_SIZE)) {
    return;
  }
  int2 pixel = int2(id.xy);
  int2 last = int2(TAA_SIZE) - 1;
  float3 input = TaaLinear(taa_final.Load(int3(pixel, 0)).rgb);
  // Another size than the scene's (no case known): the frame as it is.
  if (any(TAA_SIZE != xe_scene_size)) {
    taa_history_out[pixel] = float4(input, 0.0);
    return;
  }
  // The jitter in input pixels: the content at input position q is the
  // unjittered scene at q - jitter.
  float2 jitter = kSceneRotated ? -xe_scene_jitter : xe_scene_jitter;
  float3 current = 0.0, moment1 = 0.0, moment2 = 0.0;
  float3 box_low = 1.0e9, box_high = -1.0e9;
  float weight_sum = 0.0, nearest_depth = -1.0;
  int2 nearest = 0;
  [unroll] for (int y = -1; y <= 1; ++y) {
    [unroll] for (int x = -1; x <= 1; ++x) {
      int2 offset = int2(x, y);
      int2 neighbour = clamp(pixel + offset, 0, last);
      float3 color = TaaLinear(taa_final.Load(int3(neighbour, 0)).rgb);
      float2 distance = float2(offset) - jitter;
      float weight = exp(-2.29 * dot(distance, distance));
      current += color * weight;
      weight_sum += weight;
      float3 ycocg = TaaToYCoCg(color);
      moment1 += ycocg;
      moment2 += ycocg * ycocg;
      box_low = min(box_low, ycocg);
      box_high = max(box_high, ycocg);
      float depth = taa_depth.Load(int3(TaaScenePixel(neighbour), 0));
      if (depth > nearest_depth) {
        nearest_depth = depth;
        nearest = neighbour;
      }
    }
  }
  current /= weight_sum;
  float2 mask = taa_mask.Load(int3(TaaScenePixel(pixel), 0));
  bool reactive = mask.y > 0.5;
  float gamma = reactive ? min(TAA_CLIP_GAMMA, 0.75) : TAA_CLIP_GAMMA;
  float3 mean = moment1 / 9.0;
  float3 deviation = sqrt(max(moment2 / 9.0 - mean * mean, 0.0));
  float3 box_min = max(mean - gamma * deviation, box_low);
  float3 box_max = min(mean + gamma * deviation, box_high);

  float2 motion = taa_motion.Load(int3(TaaScenePixel(nearest), 0));
  if (kSceneRotated) motion = -motion;
  float2 position = float2(pixel) + 0.5 + motion;
  bool on_screen = all(position >= 0.0) && all(position <= float2(TAA_SIZE));
  bool known = mask.x < 0.5;
  // Step 8: a pixel that does not move on a still camera. Nothing on the screen
  // moves then, so it keeps its history also without known motion (the masks
  // of the interior, the mirror panel) and as reactive (car glass).
  bool still = xe_camera_still() && length(motion) <= TAA_STILL_PIXELS;
  bool usable = xe_history_valid() && on_screen && (known || still);
  bool disoccluded = false;
#if TAA_DISOCCLUSION
  // Not on a still pixel of a still camera: nothing can uncover it, and the
  // jitter moves the depth of the 3x3 between the near interior and the world
  // at their edges, which would drop the history there again and again.
  if (usable && !still) {
    // The farthest previous scene depth of the 3x3 where the history is,
    // against the nearest W of the 3x3 around the pixel now: when even that
    // was clearly nearer than anything here, a solid surface hid what the
    // pixel shows. (Pixel against pixel, the jitter moving a thin near object
    // across pixels, or out of every sample in some frames, would drop its
    // history again and again.)
    int2 previous_pixel = kSceneRotated ? int2(float2(xe_scene_size) - position)
                                        : int2(position);
    float previous_farthest = 1.0e9;
    [unroll] for (int py = -1; py <= 1; ++py) {
      [unroll] for (int px = -1; px <= 1; ++px) {
        int2 sample = clamp(previous_pixel + int2(px, py), 0, int2(xe_scene_size) - 1);
        previous_farthest = min(previous_farthest, taa_previous_depth.Load(int3(sample, 0)));
      }
    }
    float previous_w = xe_previous_w(previous_farthest);
    float nearest_w = xe_scene_w(nearest_depth);
    disoccluded = previous_w > 0.0 && previous_w < nearest_w * 0.9 - 0.05;
    usable = !disoccluded;
  }
#endif
  float4 history = usable ? TaaHistory(position) : 0.0;
  float age = usable ? taa_previous.Load(int3(clamp(int2(position), 0, last), 0)).a : 0.0;
  // Step 9: the luma of every phase's current frame where the pixel has not
  // changed for a whole cycle (phases_known), and whether it changed within
  // the last cycle (changed).
  float phase_low = 0.0, phase_high = 0.0;
  bool phases_known = false, tested = false, changed = false;
#if TAA_SCENE == 0
  uint phases = xe_scene_jitter_phases;
  if (xe_camera_still() && TAA_STILL_PHASES > 0.0 && phases >= 2u && phases <= kPhaseSlots) {
    uint phase = xe_scene_jitter_phase;
    uint2 packed = taa_previous_phases.Load(int3(pixel, 0));
    uint level = uint(round(sqrt(saturate(TaaToYCoCg(current).x)) * 255.0));
    uint cycle_ago = TaaPhaseLevel(packed, phase);
    uint shift = (phase & 3u) * 8u;
    if (phase < 4u) {
      packed.x = (packed.x & ~(0xFFu << shift)) | (level << shift);
    } else {
      packed.y = (packed.y & ~(0xFFu << shift)) | (level << shift);
    }
    taa_phases_out[pixel] = packed;
    // Every phase written on this still camera: the one of this phase a cycle
    // ago saw the same samples.
    uint unchanged = 255u;
    tested = still && xe_history_valid() && xe_scene_still_frames >= phases;
    if (tested) {
      bool differs = abs(int(level) - int(cycle_ago)) > int(TAA_STILL_CHANGE);
      unchanged = differs ? 0u : min(taa_previous_unchanged.Load(int3(pixel, 0)) + 1u, 255u);
      changed = unchanged < phases;
      phases_known = !changed;
      if (phases_known) {
        uint low = 255u, high = 0u;
        [unroll] for (uint i = 0u; i < kPhaseSlots; ++i) {
          if (i < phases) {
            low = min(low, TaaPhaseLevel(packed, i));
            high = max(high, TaaPhaseLevel(packed, i));
          }
        }
        // Half a level of rounding either way.
        phase_low = max(float(low) - 0.5, 0.0) / 255.0;
        phase_high = (float(high) + 0.5) / 255.0;
      }
    }
    taa_unchanged_out[pixel] = unchanged;
  }
#endif
  // A still pixel that changed within the last cycle blends as a moving one.
  bool accumulate = still && !changed;
  // The phases of a still pixel accumulate. With the jitter a sample moves by
  // up to half a pixel, so the samples within two pixels can reach the 3x3
  // that the current frame is built from: the box is their minimum and maximum.
  if (accumulate) {
    box_min = box_low;
    box_max = box_high;
    [unroll] for (int ry = -2; ry <= 2; ++ry) {
      [unroll] for (int rx = -2; rx <= 2; ++rx) {
        if (abs(rx) < 2 && abs(ry) < 2) continue;
        int2 ring = clamp(pixel + int2(rx, ry), 0, last);
        float3 ycocg = TaaToYCoCg(TaaLinear(taa_final.Load(int3(ring, 0)).rgb));
        box_min = min(box_min, ycocg);
        box_max = max(box_max, ycocg);
      }
    }
  }
  float3 history_ycocg = TaaToYCoCg(history.rgb);
  float3 clipped;
  if (phases_known) {
    // Step 9: the luma within that of every phase's current frame and of the
    // 5x5, the color unclipped.
    history_ycocg.x = clamp(history_ycocg.x, min(box_min.x, phase_low * phase_low),
                            max(box_max.x, phase_high * phase_high));
    clipped = TaaFromYCoCg(history_ycocg);
  } else {
    clipped = TaaFromYCoCg(TaaClip(history_ycocg, box_min, box_max));
  }

  float speed = saturate(length(motion) / TAA_MOTION_PIXELS);
  float alpha = accumulate ? 1.0 / (min(age, TAA_STILL_FRAMES) + 1.0)
                           : max(max(TAA_BLEND, 1.0 / (age + 1.0)),
                                 lerp(TAA_BLEND, TAA_BLEND_MOTION, speed));
  if (reactive && !accumulate) alpha = max(alpha, TAA_REACTIVE);
  if (!usable) alpha = 1.0;
  float current_weight = alpha / (1.0 + TaaLuma(current));
  float history_weight = (1.0 - alpha) / (1.0 + TaaLuma(clipped));
  float3 result = (current * current_weight + clipped * history_weight) /
                  max(current_weight + history_weight, 1.0e-6);
  taa_history_out[pixel] =
      float4(result, usable ? min(age + 1.0, accumulate ? TAA_STILL_FRAMES : kMaxAge) : 1.0);

  if (TAA_DEBUG > 0u) {
    float3 color;
    if (TAA_DEBUG == 1u) {
      color = usable ? TaaDisplay(history.rgb) : float3(1.0, 0.0, 1.0);
    } else if (TAA_DEBUG == 2u) {
      color = lerp(float3(0.0, 0.2, 1.0), float3(1.0, 0.2, 0.0), alpha);
    } else if (TAA_DEBUG == 3u) {
      float moved = usable ? abs(clipped.r + clipped.g + clipped.b - history.r - history.g -
                                 history.b) : 0.0;
      color = float3(1.0, 0.6, 0.2) * saturate(moved * 8.0);
    } else if (TAA_DEBUG == 4u) {
      color = TaaDisplay(current) * 0.35;
      if (!known) {
        color = float3(1.0, 0.1, 0.1);
      } else if (disoccluded) {
        color = float3(1.0, 0.9, 0.1);
      } else if (!xe_history_valid() || !on_screen) {
        color = float3(0.1, 0.3, 1.0);
      } else if (reactive) {
        color = float3(0.1, 1.0, 0.2);
      }
    } else if (TAA_DEBUG == 6u) {
      color = still && usable ? float3(0.1, 0.9, 1.0) : TaaDisplay(current) * 0.35;
    } else if (TAA_DEBUG == 7u) {
      color = TaaDisplay(current) * 0.35;
      if (tested) {
        color = changed ? float3(1.0, 0.15, 0.1) : float3(0.1, 0.9, 0.2);
      } else if (still) {
        color = float3(0.1, 0.3, 1.0);
      }
    } else {
      color = TaaDisplay(current);
    }
    taa_debug_out[pixel] = float4(color, 1.0);
  }
}

// ---------------------------------------------------------------------------
// taa_output: the history into final_color (or the scene's write-back),
// sharpened, or a debug view.

Texture2D<float4> taa_output_history : register(t0);
Texture2D<float4> taa_output_debug : register(t1);
// TAA_SCENE 1: the game's scene, whose alpha the output keeps.
Texture2D<float4> taa_output_scene : register(t2);

float3 TaaOutputColor(int2 pixel) {
  int2 last = int2(TAA_SIZE) - 1;
  return TaaDisplay(taa_output_history.Load(int3(clamp(pixel, 0, last), 0)).rgb);
}

float4 taa_output(XePostPassVertex input) : SV_Target {
  int2 pixel = int2(input.position.xy);
#if TAA_SCENE
  float alpha = taa_output_scene.Load(int3(pixel, 0)).a;
#else
  float alpha = 1.0;
#endif
  if (TAA_DEBUG > 0u) {
    return float4(taa_output_debug.Load(int3(pixel, 0)).rgb, alpha);
  }
  float3 middle = TaaOutputColor(pixel);
  if (TAA_SHARPEN <= 0.0) {
    return float4(middle, alpha);
  }
  // Contrast-adaptive sharpening of the display values: less where the
  // neighbourhood is dark or near white, so edges do not ring.
  float3 up = TaaOutputColor(pixel + int2(0, -1));
  float3 down = TaaOutputColor(pixel + int2(0, 1));
  float3 left = TaaOutputColor(pixel + int2(-1, 0));
  float3 right = TaaOutputColor(pixel + int2(1, 0));
  float3 low = min(middle, min(min(up, down), min(left, right)));
  float3 high = max(middle, max(max(up, down), max(left, right)));
  float3 amount = sqrt(saturate(min(low, 1.0 - high) / max(high, 1.0 / 256.0)));
  float3 weight = amount * (-1.0 / lerp(8.0, 5.0, saturate(TAA_SHARPEN)));
  float3 sharpened = (middle + weight * (up + down + left + right)) / (1.0 + 4.0 * weight);
  return float4(saturate(sharpened), alpha);
}
