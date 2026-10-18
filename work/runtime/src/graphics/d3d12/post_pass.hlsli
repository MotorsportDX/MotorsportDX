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
  // XE_SCENE_HISTORY_VALID, XE_SCENE_VIEW_KNOWN.
  uint xe_scene_flags;
  // Seconds since the passes were created.
  float xe_time;
  // World position = mul(xe_scene_view_to_world, float4(view, 1.0)).
  row_major float3x4 xe_scene_view_to_world;
  // The previous frame's view position of a view position.
  row_major float3x4 xe_view_to_previous_view;
  // The previous frame's camera (like xe_scene_xy_mul_add and the W terms).
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
};

#define XE_SCENE_HISTORY_VALID 1u
#define XE_SCENE_VIEW_KNOWN 2u

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

// The view position of a host pixel position of the scene at W.
float3 xe_scene_view_position(float2 host_pixel_center, float w) {
  float2 guest = xe_scene_guest_position(host_pixel_center);
  return float3(w * (guest * xe_scene_xy_mul_add.xz + xe_scene_xy_mul_add.yw),
                w * xe_scene_z_from_w);
}

bool xe_scene_is_sky(float w) { return w >= xe_scene_w_far * 0.999; }

// The host pixel position of the scene where the previous frame saw a view
// position of this frame, and its W then; valid with xe_history_valid() and
// XE_SCENE_VIEW_KNOWN.
float2 xe_previous_host_position(float3 view_position, out float previous_w) {
  float3 previous = mul(xe_view_to_previous_view, float4(view_position, 1.0));
  previous_w = previous.z / xe_previous_z_from_w;
  float2 guest = (previous.xy / previous_w - xe_previous_xy_mul_add.yw) / xe_previous_xy_mul_add.xz;
  return (guest - xe_scene_guest_origin) / xe_scene_guest_per_host;
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
