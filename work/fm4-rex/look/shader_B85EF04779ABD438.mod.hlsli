// Car body paint of shaders_v16 (races, replays, photo mode;
// docs/analysis/material-atlas.md): c254.w the minimum shadow variance and
// c255.x the cube coordinate offset (its literals), bool 128 the self shadow.
// See fm4_body_paint_env_cube in fm4-paint.hlsli.
#include "fm4-paint.hlsli"

#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) \
  fm4_body_paint_env_cube(p, input, XE_C(254).w, XE_C(255).x, xe_bool_constant(128u))
