// Motion vectors of the scene, a post pass of the runtime's post pass framework
// (passes.toml, post_pass_path; see docs/analysis/motion-vectors.md and
// docs/analysis/post-pass-framework.md). Original code of this project. Per
// pixel: the view position from the resolved scene depth, the object slot from
// the scene stencil (FM4 writes 0 for the track and 7n for car n, as its own
// motion blur reads it), and that slot's reprojection into the previous frame's
// view, which the runtime builds on the CPU from the scene camera and each car's
// world matrix in both frames (the manifest's [motion]).
//
// Two kinds of pixels do not show the surface their depth and stencil name:
// - the interior ([motion] interior): FM4 draws the cockpit interior of the
//   player's car and then covers it with a mask at the stored depth 1.0 (the
//   near plane), so its pixels carry the car's stencil but no depth of their
//   own; they move with the interior's own matrix at MOTION_INTERIOR_W;
// - see-through pixels: car glass writes the car's stencil but not its depth,
//   so the world behind the glass keeps its own depth under the car's ID; a
//   pixel farther than MOTION_OBJECT_RADIUS from its car's origin is taken as
//   the world seen through it (the world's motion, and reactive).
// Pixels at the mask depth that are not an interior, and pixels within 1% of the
// near plane's W or nearer (no geometry is that close to the camera), have no
// motion known: the rear-view mirror panel of the hood and bumper cameras reads
// as such.
//
// Entry points of passes.toml:
//   motion_vectors     compute: depth and stencil -> motion (RG16F), the
//                      previous frame's host pixel position of the surface
//                      minus this frame's, in host pixels of the scene
//                      textures and in their layout (FM4's is rotated 180
//                      degrees against the screen); zero without a valid
//                      previous frame (xe_motion_valid(): the first frame, a
//                      camera cut); and the mask (RG8): R 1 where no motion
//                      is known (leave out of the history), G 1 where the
//                      pixel mixes surfaces that move apart (reactive)
//   motion_debug       compute: motion, stencil and depth -> the color coding
//                      of MOTION_DEBUG (RGBA8, alpha for blending)
//   motion_debug_show  pixel, final point: draws the color coding over the
//                      frame, before the HUD

// FM4 draws its scene rotated 180 degrees within the tiles: the scene textures
// are upside down and mirrored against the frame.
static const bool kSceneRotated = true;
// A surface behind the previous frame's camera: a motion this long, which
// leaves the screen.
static const float kOffscreen = 16384.0;

// ---------------------------------------------------------------------------
// motion_vectors: one pixel per thread.

Texture2D<float> motion_depth : register(t0);
Texture2D<uint> motion_stencil : register(t1);
RWTexture2D<float2> motion_out : register(u0);
RWTexture2D<unorm float2> motion_mask_out : register(u1);

// What a host pixel of the scene shows.
static const uint kKindSurface = 0u;     // its depth and stencil
static const uint kKindInterior = 1u;    // the interior under the mask
static const uint kKindSeeThrough = 2u;  // the world through a car's glass
static const uint kKindUnknown = 3u;     // the mask without an interior
struct MotionPixel {
  uint kind;
  // The slot whose motion it takes, and its W.
  uint slot;
  float w;
};

MotionPixel ClassifyPixel(uint2 pixel, float stored_depth, uint stencil) {
  MotionPixel result;
  result.kind = kKindSurface;
  result.slot = xe_motion_slot(stencil);
  result.w = xe_scene_w(stored_depth);
  bool at_mask = xe_motion_interior_depth >= 0.0 &&
                 (abs(stored_depth - xe_motion_interior_depth) <= 1.0e-6 ||
                  result.w < xe_scene_w(xe_motion_interior_depth) * 1.01);
  if (xe_motion_is_interior(result.slot, stored_depth)) {
    result.kind = kKindInterior;
    result.slot = XE_MOTION_INTERIOR_SLOT;
    result.w = MOTION_INTERIOR_W;
  } else if (at_mask) {
    result.kind = kKindUnknown;
    result.slot = 0u;
  } else if (result.slot != 0u) {
    float4 origin = xe_motion_object_origin[result.slot];
    float3 view = xe_scene_view_position(float2(pixel) + 0.5, result.w);
    if (origin.w > 0.0 && length(view - origin.xyz) > MOTION_OBJECT_RADIUS) {
      result.kind = kKindSeeThrough;
      result.slot = 0u;
    }
  }
  return result;
}

// The motion of a host pixel of the scene at W on the object of a slot.
float2 Motion(uint2 pixel, float w, uint slot) {
  float2 center = float2(pixel) + 0.5;
  float3 view = xe_scene_view_position(center, w);
  float previous_w;
  float2 previous = xe_motion_previous_host_position(slot, view, previous_w);
  float2 motion = clamp(previous - center, -kOffscreen, kOffscreen);
  return previous_w > 1.0e-4 ? motion : kOffscreen;
}

[numthreads(8, 8, 1)]
void motion_vectors(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= xe_scene_size)) {
    return;
  }
  MotionPixel pixel = ClassifyPixel(id.xy, motion_depth.Load(int3(id.xy, 0)),
                                    motion_stencil.Load(int3(id.xy, 0)));
  float2 motion = 0.0;
  if (xe_motion_valid() && pixel.kind != kKindUnknown) {
    motion = Motion(id.xy, pixel.w, pixel.slot);
  }
  motion_out[id.xy] = motion;
  motion_mask_out[id.xy] = float2(pixel.kind == kKindUnknown ? 1.0 : 0.0,
                                  pixel.kind == kKindSeeThrough ? 1.0 : 0.0);
}

// ---------------------------------------------------------------------------
// motion_debug: the color coding of MOTION_DEBUG, in the scene's layout.
//   1  motion: the hue is the direction on the screen (right red, down
//      yellow-green, left cyan, up violet), the saturation the length up to
//      MOTION_DEBUG_PIXELS host pixels (white: still); opaque
//   2  the same over the frame, more opaque the longer the motion
//   3  object slots: the track transparent, each car slot its own color, slots
//      without their own motion (a car new in the frame) striped magenta
//   4  the object motion alone: the motion less the static world's at the same
//      depth, coded as 1 (the track and cars that move with the camera white)
//   5  the pixel kinds over sepia by W (darker nearer, the sky blue): the
//      interior red, the world through glass cyan, the mask without an
//      interior magenta
//   6  the mask: no motion known red, reactive green, the rest transparent

Texture2D<float2> motion_debug_motion : register(t0);
Texture2D<uint> motion_debug_stencil : register(t1);
Texture2D<float> motion_debug_depth : register(t2);
Texture2D<float2> motion_debug_mask : register(t3);
RWTexture2D<unorm float4> motion_debug_out : register(u0);

float3 Hue(float hue) {
  float3 rgb = abs(frac(hue + float3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0;
  return saturate(rgb);
}

// The color of a motion of the scene's layout, and its length relative to
// MOTION_DEBUG_PIXELS.
float3 MotionColor(float2 motion, out float strength) {
  float2 screen = kSceneRotated ? -motion : motion;
  strength = saturate(length(screen) / MOTION_DEBUG_PIXELS);
  float hue = atan2(screen.y, screen.x) * (0.5 / 3.14159265);
  return lerp(1.0, Hue(hue), strength);
}

[numthreads(8, 8, 1)]
void motion_debug(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= xe_scene_size)) {
    return;
  }
  float2 motion = motion_debug_motion.Load(int3(id.xy, 0));
  uint stencil = motion_debug_stencil.Load(int3(id.xy, 0));
  float stored_depth = motion_debug_depth.Load(int3(id.xy, 0));
  uint slot = xe_motion_slot(stencil);
  float strength = 0.0;
  float4 color;
  if (MOTION_DEBUG == 5u) {
    MotionPixel pixel = ClassifyPixel(id.xy, stored_depth, stencil);
    float w = xe_scene_w(stored_depth);
    // Sepia rather than gray: e2e counts the clear gray as missing geometry.
    color = float4(xe_scene_is_sky(w)
                       ? float3(0.3, 0.5, 1.0)
                       : saturate(log2(max(w, 0.01)) / 12.0 + 0.3) * float3(1.0, 0.85, 0.65),
                   1.0);
    if (pixel.kind == kKindInterior) {
      color.rgb = float3(1.0, 0.15, 0.1);
    } else if (pixel.kind == kKindSeeThrough) {
      color.rgb = lerp(color.rgb, float3(0.0, 1.0, 1.0), 0.7);
    } else if (pixel.kind == kKindUnknown) {
      color.rgb = float3(1.0, 0.0, 1.0);
    }
  } else if (MOTION_DEBUG == 6u) {
    float2 mask = motion_debug_mask.Load(int3(id.xy, 0));
    color = float4(mask.x, mask.y, 0.0, max(mask.x, mask.y) * 0.85);
  } else if (MOTION_DEBUG == 3u) {
    if (slot == 0u) {
      color = 0.0;
    } else if (!xe_motion_valid() || xe_motion_slot_own(slot)) {
      color = float4(Hue(frac(float(slot) * 0.618034)), 0.65);
    } else {
      bool stripe = ((id.x + id.y) / 8u & 1u) != 0u;
      color = float4(stripe ? float3(1.0, 0.0, 1.0) : 0.0, 0.85);
    }
  } else if (MOTION_DEBUG == 4u) {
    float2 world_motion = 0.0;
    if (xe_motion_valid()) {
      MotionPixel pixel = ClassifyPixel(id.xy, stored_depth, stencil);
      world_motion = Motion(id.xy, pixel.w, 0u);
    }
    color = float4(MotionColor(motion - world_motion, strength), 1.0);
  } else {
    color = float4(MotionColor(motion, strength), 1.0);
    if (MOTION_DEBUG == 2u) {
      color.a = sqrt(strength) * 0.85;
    }
  }
  motion_debug_out[id.xy] = color;
}

// ---------------------------------------------------------------------------
// motion_debug_show: the coding over the frame (the final point's
// final_color), blended by its alpha.

Texture2D<float4> motion_debug_color : register(t0);

float4 motion_debug_show(XePostPassVertex input) : SV_Target {
  float2 uv = kSceneRotated ? 1.0 - input.uv : input.uv;
  int2 pixel = int2(uv * float2(xe_scene_size));
  return motion_debug_color.Load(int3(clamp(pixel, 0, int2(xe_scene_size) - 1), 0));
}
