// Chrome rims (with blurRimGeomTexBlend) of shaders_16car_v16 (16-car races) and
// shaders_splitscreen_v16 (split-screen races); docs/analysis/look-mod-coverage.md. The static
// environment cube in fetch 1 (gamma 2), no dynamic one. No shadow map; the lightmap in fetch
// 10 (range c60). See fm4_chrome in fm4-car.hlsli.
#include "fm4-car.hlsli"

void fm4_chrome_mod(inout XePixel p, XePixel input) {
  if (FM4_LOOK_CHROME <= 0.0) {
    return;
  }
  FM4CarView view = fm4_car_view(input, XE_C(59).xyz);
  float3 env = fm4_car_env(XE_TFETCHCUBE(1, view.cube), false);
  float lit = fm4_car_lightmap_shadow(input.r[4].xy, XE_C(60));
  fm4_chrome(p, view, XE_C(43).xyz, XE_C(42).xyz, env, lit, XE_C(47).w);
}

#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) fm4_chrome_mod(p, input)
