// Chrome rims (with blurRimGeomTexBlend) of shaders_ui_v16 (garage, car menus, HomeSpace);
// docs/analysis/look-mod-coverage.md. The dynamic environment cube in fetch 1, the static one
// in fetch 2 (bool 130), stored as encoded HDR when bool 138 (dynamic) or 141 (static) is set.
// The variance shadow map in fetch 13, always read (minimum variance 0.001, darkening c80.x),
// and the lightmap in fetch 10 (range c60). See fm4_chrome in fm4-car.hlsli.
#include "fm4-car.hlsli"

void fm4_chrome_mod(inout XePixel p, XePixel input) {
  if (FM4_LOOK_CHROME <= 0.0) {
    return;
  }
  FM4CarView view = fm4_car_view(input, XE_C(59).xyz);
  bool use_static = xe_bool_constant(130u);
  float3 env = fm4_car_env(use_static ? XE_TFETCHCUBE(2, view.cube) : XE_TFETCHCUBE(1, view.cube),
                           xe_bool_constant(use_static ? 141u : 138u));
  float lit = fm4_car_lightmap_shadow(input.r[4].xy, XE_C(60));
  lit = min(lit, fm4_variance_shadow(input.r[3].xyz, 0.001, XE_C(80).x));
  fm4_chrome(p, view, XE_C(43).xyz, XE_C(42).xyz, env, lit, XE_C(47).w);
}

#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) fm4_chrome_mod(p, input)
