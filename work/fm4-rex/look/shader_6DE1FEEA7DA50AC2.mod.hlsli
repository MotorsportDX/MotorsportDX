// Body paint of shaders_splitscreen_v16 (split-screen races);
// docs/analysis/look-mod-coverage.md: the static environment cube in fetch 4,
// the track lightmap, no shadow map; c82 the paint color. See
// fm4_body_paint_static_cube in fm4-paint.hlsli.
#include "fm4-paint.hlsli"

void fm4_body_paint_splitscreen(inout XePixel p, XePixel input) {
  if (!fm4_paint_enabled()) {
    return;
  }
  float3 coord = fm4_env_cube_coord(xe_cube(fm4_paint_reflection(input).zzxy), 1.5);
  float3 env = XE_TFETCHCUBE(4, coord).rgb;
  fm4_body_paint_static_cube(p, input, env * env);
}

#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) fm4_body_paint_splitscreen(p, input)
