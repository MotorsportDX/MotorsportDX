/**
 * @file        graphics/d3d12/post_pass_layout.h
 * @brief       The root signature and constant layouts shared by every post pass
 *
 * Header only and free of runtime dependencies, so the WARP tests can bind the
 * pass shaders exactly as post_pass.cpp does. Must match post_pass.hlsli.
 */

#pragma once

#include <cstdint>

namespace rex::graphics::d3d12::post_pass {

// Registers a pass may bind: t0-t7, u0-u7, and with a pixel pass up to four
// render targets.
constexpr uint32_t kMaxSrvs = 8;
constexpr uint32_t kMaxUavs = 8;
constexpr uint32_t kMaxRenderTargets = 4;
// Run-time params of a pass set (b1 after the pass header), 4 bytes each.
constexpr uint32_t kMaxParams = 28;
// Object slots of the motion vectors (the manifest's [motion]): slot 0 is the
// static world, which moves with the camera only. The last slot holds the
// interior ([motion] interior), so stencil slots stop one below it.
constexpr uint32_t kMotionSlots = 32;
constexpr uint32_t kMotionInteriorSlot = kMotionSlots - 1;
// SceneConstants::motion_interior_slot without an interior in the frame.
constexpr uint32_t kMotionNoInterior = 0xFFFFFFFFu;

// Root parameters, all visible to every stage: the scene constants (b0, a root
// CBV), the pass constants (b1, root constants: PassHeader then the params),
// then one descriptor table of one view per SRV register and per UAV register.
// Static samplers: s0 bilinear within one mip, s1 point, s2 trilinear, all
// clamped.
constexpr uint32_t kRootSceneConstants = 0;
constexpr uint32_t kRootPassConstants = 1;
constexpr uint32_t kRootSrvFirst = 2;
constexpr uint32_t kRootUavFirst = kRootSrvFirst + kMaxSrvs;
constexpr uint32_t kRootParameterCount = kRootUavFirst + kMaxUavs;

// The start of b1 (xe_pass_*): the size of the pass's dispatch or viewport.
struct PassHeader {
  uint32_t size[2];
  float inverse_size[2];
};
constexpr uint32_t kPassConstantCount =
    uint32_t(sizeof(PassHeader) / sizeof(uint32_t)) + kMaxParams;

// SceneConstants::flags.
// The previous frame's history (scene color, scene depth, camera) is usable.
constexpr uint32_t kSceneFlagHistoryValid = 1u << 0;
// view_to_world holds this frame's camera.
constexpr uint32_t kSceneFlagViewKnown = 1u << 1;
// The previous frame had a scene whose camera view is known, and the camera did
// not cut: view_to_previous_view, the previous camera and the motion matrices
// are valid (the history textures need kSceneFlagHistoryValid too).
constexpr uint32_t kSceneFlagMotionValid = 1u << 2;

// b0 (xe_scene_*, xe_previous_*, xe_final_*), in 16-byte rows like an HLSL
// cbuffer.
struct SceneConstants {
  // The scene depth's camera (post_pass_camera.h Unpack): view position from W
  // and the guest screen position, x = w * (x * xy_mul_add[0] + xy_mul_add[1]),
  // y likewise with [2] and [3], z = w * z_from_w.
  float xy_mul_add[4];
  // Host pixels of the scene depth, and the guest pixels per host pixel.
  uint32_t size[2];
  float guest_per_host[2];
  // Guest screen position of the center of host pixel 0, less half a host
  // pixel (-0.5 with the Direct3D 9 pixel center).
  float guest_origin[2];
  // W from the stored depth d: w_numerator / (d * w_depth_mul + w_depth_add).
  float w_numerator;
  float w_depth_mul;
  float w_depth_add;
  float z_from_w;
  // W at the far plane.
  float w_far;
  // Frames since the start (the command processor's frame counter, low bits).
  uint32_t frame;
  // Host pixels per view unit at W = 1 (the focal length), X and Y.
  float focal[2];
  uint32_t flags;
  // Seconds since the post passes were created.
  float time;
  // World position = view_to_world * (view position, 1), 3 rows, with
  // kSceneFlagViewKnown.
  float view_to_world[3][4];
  // Previous frame's view position = view_to_previous_view * (view position,
  // 1), with kSceneFlagMotionValid.
  float view_to_previous_view[3][4];
  // The previous frame's camera, as xy_mul_add and the W terms above, with
  // kSceneFlagMotionValid.
  float previous_xy_mul_add[4];
  float previous_w_numerator;
  float previous_w_depth_mul;
  float previous_w_depth_add;
  float previous_z_from_w;
  // The final point: host pixels of the final color, and the texture
  // coordinates of its far corner in its render target (the render target
  // covers more of the EDRAM).
  uint32_t final_size[2];
  float final_uv_scale[2];
  // Object motion (the manifest's [motion]). The slot of a scene stencil value
  // s is floor(s * motion_stencil_scale + 0.5) if s < motion_stencil_limit,
  // else 0; slots from motion_slot_count on are 0.
  float motion_stencil_scale;
  uint32_t motion_stencil_limit;
  uint32_t motion_slot_count;
  // Bit n: slot n holds its object's own motion (the object was captured in
  // this frame and the previous one); otherwise it holds slot 0's.
  uint32_t motion_slots_own;
  // The interior ([motion] interior): the stencil slot of the object whose
  // interior was drawn (kMotionNoInterior if none), and the stored depth of the
  // mask that covers it (negative without [motion] interior). Those pixels move
  // with slot kMotionInteriorSlot; other pixels at that depth are masks too,
  // with no motion known.
  uint32_t motion_interior_slot;
  float motion_interior_depth;
  uint32_t motion_padding[2];
  // Per slot, view_to_previous_view of the object: the previous frame's view
  // position of a view position of this frame on that object, with
  // kSceneFlagMotionValid. Slot 0 is view_to_previous_view.
  float motion_view_to_previous_view[kMotionSlots][3][4];
  // Per slot, the view position of the object's origin in this frame (w 1 if
  // the object was drawn, else 0).
  float motion_object_origin[kMotionSlots][4];
};
static_assert(sizeof(SceneConstants) == (16 + 4 * kMotionSlots) * 16,
              "SceneConstants is 16 cbuffer rows and 4 per motion slot");

}  // namespace rex::graphics::d3d12::post_pass
