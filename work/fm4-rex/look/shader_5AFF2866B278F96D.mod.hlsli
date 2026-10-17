// Body paint of shaders_ui_v16 (garage, car menus, HomeSpace;
// docs/analysis/look-mod-coverage.md): the registers of B85EF04779ABD438, but
// no self-shadow bool (the shadow map is always read) and other literal
// registers, so the minimum variance (0.001) and the cube offset (1.5) are
// given as numbers. See fm4_body_paint_env_cube in fm4-paint.hlsli.
// Its environment cubes may be encoded HDR (bools 138 and 141): the decode
// literals are c251.w, c254.w, c251.x and c253.w, the matrix rows c253 to c255.
#define FM4_PAINT_ENV_DECODE(f)                                                                  \
  fm4_decode_env(f, float4(XE_C(251).w, XE_C(254).w, XE_C(251).x, XE_C(253).w), XE_C(253).xyz, \
                 XE_C(254).xyz, XE_C(255).xyz)
#include "fm4-paint.hlsli"

#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) fm4_body_paint_env_cube(p, input, 0.001, 1.5, true)
