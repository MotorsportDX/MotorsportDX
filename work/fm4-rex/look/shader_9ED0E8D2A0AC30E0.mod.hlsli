// Body paint of shaders_slod_v16 (showroom and Autovista;
// docs/analysis/look-mod-coverage.md). It has no sun direction and no track
// lightmap, so the clear coat only adds the reflection of the environment cube
// at grazing angles (no sun highlight and no flakes): fetch 2 the dynamic cube,
// fetch 3 the static one (bool 130), at the cube offset c255.x, both stored as
// encoded HDR when bool 138 or 141 is set (decode literals c250.z, c253.w,
// c250.w and c252.w, matrix rows c251, c252 and c254). Interpolator 1 the world
// normal, 2 the world position; c47 the fog, c59 the eye position, c80 the
// paint color. See fm4_clearcoat in fm4-paint.hlsli.
#define FM4_PAINT_ENV_DECODE(f)                                                                  \
  fm4_decode_env(f, float4(XE_C(250).z, XE_C(253).w, XE_C(250).w, XE_C(252).w), XE_C(251).xyz, \
                 XE_C(252).xyz, XE_C(254).xyz)
#include "fm4-paint.hlsli"

void fm4_body_paint_showroom(inout XePixel p, XePixel input) {
  if (FM4_LOOK_CLEARCOAT <= 0.0) {
    return;
  }
  float3 n = normalize(input.r[1].xyz);
  float3 coord = fm4_env_cube_coord(xe_cube(fm4_paint_reflection(input).zzxy), XE_C(255).x);
  bool use_static = xe_bool_constant(130u);
  float3 env = fm4_env_texel(use_static ? XE_TFETCHCUBE(3, coord) : XE_TFETCHCUBE(2, coord),
                             xe_bool_constant(use_static ? 141u : 138u));
  fm4_clearcoat(p, n, input.r[2].xyz, XE_C(59).xyz, n, 0.0, env, 1.0, saturate(XE_C(47).w),
                input.r[0].xy, XE_C(80).xyz);
}

#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) fm4_body_paint_showroom(p, input)
