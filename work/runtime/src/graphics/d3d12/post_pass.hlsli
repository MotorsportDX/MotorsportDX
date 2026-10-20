// Shared code of the post passes (post_pass.cpp). Embedded in rexgpu-xenos as
// "xe_post_pass.hlsli". The runtime compiles every pass of a manifest
// (post_pass_path/passes.toml) with FXC as
//   #define XE_POST_PASS_PARAMS <the params of the manifest as cbuffer fields>
//   #include "xe_post_pass.hlsli"
//   #include "<the pass file>"
// and the defines of the manifest and the pass; cs_5_1 for compute passes,
// ps_5_1 for pixel passes (drawn with xe_post_pass_vs, a triangle covering the
// render targets). The root signature and the constant layouts are those of
// post_pass_layout.h.

#ifndef XE_POST_PASS_HLSLI
#define XE_POST_PASS_HLSLI

#ifndef XE_POST_PASS_PARAMS
#define XE_POST_PASS_PARAMS
#endif

// post_pass_layout.h kMotionSlots, kMotionInteriorSlot, kMotionNoInterior.
#define XE_MOTION_SLOTS 32
#define XE_MOTION_INTERIOR_SLOT 31u
#define XE_MOTION_NO_INTERIOR 0xFFFFFFFFu

// post_pass_layout.h SceneConstants.
cbuffer xe_post_pass_scene : register(b0) {
  // The scene depth's camera: view position from W and the guest screen
  // position, x = w * (x * xy_mul_add.x + xy_mul_add.y), y = w * (y *
  // xy_mul_add.z + xy_mul_add.w), z = w * z_from_w.
  float4 xe_scene_xy_mul_add;
  // Host pixels of the scene depth (and of the scene color).
  uint2 xe_scene_size;
  // Guest pixels per host pixel (1 / resolution scale), and the guest screen
  // position of the center of host pixel 0 is xe_scene_guest_per_host * 0.5 +
  // xe_scene_guest_origin (-0.5 with the Direct3D 9 pixel center).
  float2 xe_scene_guest_per_host;
  float2 xe_scene_guest_origin;
  // W (clip W, view distance along the axis) from the stored depth d:
  // w_numerator / (d * w_depth_mul + w_depth_add).
  float xe_scene_w_numerator;
  float xe_scene_w_depth_mul;
  float xe_scene_w_depth_add;
  float xe_scene_z_from_w;
  // W at the far plane: pixels beyond 99.9% of it are sky.
  float xe_scene_w_far;
  uint xe_frame;
  // Host pixels per view unit at W = 1 (the focal length).
  float2 xe_scene_focal;
  // XE_SCENE_HISTORY_VALID, XE_SCENE_VIEW_KNOWN, XE_SCENE_MOTION_VALID.
  uint xe_scene_flags;
  // Seconds since the passes were created.
  float xe_time;
  // World position = mul(xe_scene_view_to_world, float4(view, 1.0)).
  row_major float3x4 xe_scene_view_to_world;
  // The previous frame's view position of a view position (of the static
  // world), with xe_motion_valid().
  row_major float3x4 xe_view_to_previous_view;
  // The previous frame's camera (like xe_scene_xy_mul_add and the W terms),
  // with xe_motion_valid().
  float4 xe_previous_xy_mul_add;
  float xe_previous_w_numerator;
  float xe_previous_w_depth_mul;
  float xe_previous_w_depth_add;
  float xe_previous_z_from_w;
  // The final point: host pixels of the final color, and the texture
  // coordinates of its far corner in its render target (final_color covers
  // more than the frame).
  uint2 xe_final_size;
  float2 xe_final_uv_scale;
  // Object motion (the manifest's [motion]): the slot of a stencil value is
  // xe_motion_slot.
  float xe_motion_stencil_scale;
  uint xe_motion_stencil_limit;
  uint xe_motion_slot_count;
  // Bit n: slot n holds its object's own motion (xe_motion_slot_own).
  uint xe_motion_slots_own;
  // The stencil slot whose pixels at the stored depth xe_motion_interior_depth
  // are its interior (xe_motion_is_interior), or XE_MOTION_NO_INTERIOR; the
  // depth is negative without the manifest's [motion] interior.
  uint xe_motion_interior_slot;
  float xe_motion_interior_depth;
  // The jitter of this frame's scene draws in host pixels of the scene (x
  // right, y down): the content at host position p is the unjittered scene at
  // p - xe_scene_jitter. xe_scene_view_position takes it into account; the
  // previous frame's positions (xe_previous_*) are unjittered. Zero without
  // jitter.
  float2 xe_scene_jitter;
  // The jitter's phase in this frame (0 to xe_scene_jitter_phases - 1), and the
  // number of phases: 0 without the jitter or with a fixed offset. The phase
  // repeats every xe_scene_jitter_phases frames of a jitter that runs on.
  uint xe_scene_jitter_phase;
  uint xe_scene_jitter_phases;
  // With xe_camera_still(): the frames in a row before this one in which a
  // consumer of the jitter ran on a still camera with a valid history, each one
  // phase on from the one before; else 0.
  uint xe_scene_still_frames;
  uint xe_scene_padding0;
  // Per slot, the previous frame's view position of a view position on that
  // object; slot 0, the static world, is xe_view_to_previous_view, and slot
  // XE_MOTION_INTERIOR_SLOT the interior's.
  row_major float3x4 xe_motion_view_to_previous_view[XE_MOTION_SLOTS];
  // Per slot, the view position of the object's origin (w 1 if the object was
  // drawn in this frame, else 0).
  float4 xe_motion_object_origin[XE_MOTION_SLOTS];
};

#define XE_SCENE_HISTORY_VALID 1u
#define XE_SCENE_VIEW_KNOWN 2u
#define XE_SCENE_MOTION_VALID 4u
#define XE_SCENE_CAMERA_STILL 8u

// post_pass_layout.h PassHeader, then the params of the manifest.
cbuffer xe_post_pass_constants : register(b1) {
  // The pass's dispatch size (compute) or render target size (pixel).
  uint2 xe_pass_size;
  float2 xe_pass_inverse_size;
  XE_POST_PASS_PARAMS
};

// Bilinear within one mip, point, and trilinear; all clamped.
SamplerState xe_linear_clamp : register(s0);
SamplerState xe_point_clamp : register(s1);
SamplerState xe_trilinear_clamp : register(s2);

bool xe_history_valid() { return (xe_scene_flags & XE_SCENE_HISTORY_VALID) != 0u; }

// The previous frame had a scene with a known camera view, and the camera did
// not cut: the reprojection (xe_previous_host_position) and the motion matrices
// hold.
bool xe_motion_valid() { return (xe_scene_flags & XE_SCENE_MOTION_VALID) != 0u; }

// The scene is jittered on a still camera ([jitter] still): a temporal pass may
// accumulate the phases where nothing moves instead of following the picture.
bool xe_camera_still() { return (xe_scene_flags & XE_SCENE_CAMERA_STILL) != 0u; }

// The guest screen position of a host pixel position of the scene.
float2 xe_scene_guest_position(float2 host_pixel_center) {
  return host_pixel_center * xe_scene_guest_per_host + xe_scene_guest_origin;
}

// W of a stored scene depth: the cleared depth and the far plane come out at or
// beyond the far W, or as infinity or NaN, and are returned as the far W (sky).
float xe_scene_w(float stored_depth) {
  // Numerator and denominator have the sign of the projection (both negative
  // for D3D style ones).
  float w = xe_scene_w_numerator / (stored_depth * xe_scene_w_depth_mul + xe_scene_w_depth_add);
  return (w > 0.0 && w < xe_scene_w_far) ? w : xe_scene_w_far;
}

// The view position of what a host pixel position of the scene shows, at W:
// with the jitter, the unjittered position host_pixel_center - xe_scene_jitter.
float3 xe_scene_view_position(float2 host_pixel_center, float w) {
  float2 guest = xe_scene_guest_position(host_pixel_center - xe_scene_jitter);
  return float3(w * (guest * xe_scene_xy_mul_add.xz + xe_scene_xy_mul_add.yw),
                w * xe_scene_z_from_w);
}

bool xe_scene_is_sky(float w) { return w >= xe_scene_w_far * 0.999; }

// The host pixel position of the scene of a view position of the previous
// frame, and its W then; unjittered.
float2 xe_previous_view_host_position(float3 previous_view_position, out float previous_w) {
  previous_w = previous_view_position.z / xe_previous_z_from_w;
  float2 guest = (previous_view_position.xy / previous_w - xe_previous_xy_mul_add.yw) /
                 xe_previous_xy_mul_add.xz;
  return (guest - xe_scene_guest_origin) / xe_scene_guest_per_host;
}

// The host pixel position of the scene where the previous frame saw a view
// position of this frame on the static world, and its W then; valid with
// xe_motion_valid().
float2 xe_previous_host_position(float3 view_position, out float previous_w) {
  return xe_previous_view_host_position(mul(xe_view_to_previous_view, float4(view_position, 1.0)),
                                        previous_w);
}

// The object slot of a scene stencil value (scene_stencil): round(stencil /
// stencil_step) below the manifest's stencil_limit, else 0 (the static world).
uint xe_motion_slot(uint stencil) {
  uint slot = stencil < xe_motion_stencil_limit
                  ? uint(float(stencil) * xe_motion_stencil_scale + 0.5)
                  : 0u;
  return slot < xe_motion_slot_count ? slot : 0u;
}

// The slot holds its object's own motion; otherwise (an object new in this
// frame, or one that jumped) the static world's.
bool xe_motion_slot_own(uint slot) { return ((xe_motion_slots_own >> slot) & 1u) != 0u; }

// The pixel of a stencil slot and a stored depth shows the interior of that
// slot's object: it lies under the mask the manifest's interior_depth names,
// which carries no depth of the interior itself.
bool xe_motion_is_interior(uint slot, float stored_depth) {
  return slot == xe_motion_interior_slot && slot != 0u &&
         abs(stored_depth - xe_motion_interior_depth) <= 1.0e-6;
}

// As xe_previous_host_position for a view position on the object of a slot.
float2 xe_motion_previous_host_position(uint slot, float3 view_position, out float previous_w) {
  return xe_previous_view_host_position(
      mul(xe_motion_view_to_previous_view[slot], float4(view_position, 1.0)), previous_w);
}

// The previous frame's W of a stored depth of the previous scene depth.
float xe_previous_w(float stored_depth) {
  return xe_previous_w_numerator /
         (stored_depth * xe_previous_w_depth_mul + xe_previous_w_depth_add);
}

// Pixel passes: a triangle covering the render targets, and uv 0...1 over them.
struct XePostPassVertex {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

#ifdef XE_POST_PASS_VERTEX_SHADER
XePostPassVertex xe_post_pass_vs(uint id : SV_VertexID) {
  XePostPassVertex output;
  output.uv = float2((id << 1u) & 2u, id & 2u);
  output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
  return output;
}
#endif

#endif  // XE_POST_PASS_HLSLI
