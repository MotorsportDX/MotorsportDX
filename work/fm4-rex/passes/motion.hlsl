// Motion vectors of the scene, a post pass of the runtime's post pass framework
// (passes.toml, post_pass_path; see docs/analysis/motion-vectors.md and
// docs/analysis/post-pass-framework.md). Original code of this project. Per
// pixel: the view position from the resolved scene depth, the object slot from
// the scene stencil (FM4 writes 0 for the track and 7n for car n, as its own
// motion blur reads it), and that slot's reprojection into the previous frame's
// view, which the runtime builds on the CPU from the scene camera and each car's
// world matrix in both frames (the manifest's [motion]).
//
// Entry points of passes.toml:
//   motion_vectors     compute: depth and stencil -> motion (RG16F), the
//                      previous frame's host pixel position of the surface
//                      minus this frame's, in host pixels of the scene
//                      textures and in their layout (FM4's is rotated 180
//                      degrees against the screen); zero without a valid
//                      previous frame (xe_motion_valid(): the first frame, a
//                      camera cut)
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

// The motion of a host pixel of the scene on the object of a slot.
float2 Motion(uint2 pixel, float stored_depth, uint slot) {
  float2 center = float2(pixel) + 0.5;
  float3 view = xe_scene_view_position(center, xe_scene_w(stored_depth));
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
  float2 motion = 0.0;
  if (xe_motion_valid()) {
    uint slot = xe_motion_slot(motion_stencil.Load(int3(id.xy, 0)));
    motion = Motion(id.xy, motion_depth.Load(int3(id.xy, 0)), slot);
  }
  motion_out[id.xy] = motion;
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

Texture2D<float2> motion_debug_motion : register(t0);
Texture2D<uint> motion_debug_stencil : register(t1);
Texture2D<float> motion_debug_depth : register(t2);
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
  uint slot = xe_motion_slot(motion_debug_stencil.Load(int3(id.xy, 0)));
  float strength = 0.0;
  float4 color;
  if (MOTION_DEBUG == 3u) {
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
      world_motion = Motion(id.xy, motion_debug_depth.Load(int3(id.xy, 0)), 0u);
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
