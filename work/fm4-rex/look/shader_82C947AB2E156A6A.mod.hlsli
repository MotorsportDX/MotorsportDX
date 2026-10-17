// The glass technique without a glass color or opacity (its alpha is the car's fade c57.x
// alone) of shaders_v16 (races, replays, photo mode); docs/analysis/look-mod-coverage.md. The
// dynamic environment cube in fetch 1, the static one in fetch 2 (bool 130). The lightmap in
// fetch 10; c80.x the specular amount and c82 the specular color; c57.x the car's fade. See
// fm4_glass in fm4-car.hlsli.
#include "fm4-car.hlsli"

void fm4_glass_mod(inout XePixel p, XePixel input) {
  if (FM4_LOOK_GLASS <= 0.0) {
    return;
  }
  FM4CarView view = fm4_car_view(input, XE_C(59).xyz);
  bool use_static = xe_bool_constant(130u);
  float3 env = fm4_car_env(use_static ? XE_TFETCHCUBE(2, view.cube) : XE_TFETCHCUBE(1, view.cube),
                           false);
  fm4_glass(p, view, XE_C(43).xyz, XE_C(80).x * XE_C(82).rgb, env,
            fm4_car_lightmap(input.r[4].xy), XE_C(47).w, XE_C(57).x);
}

#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) fm4_glass_mod(p, input)
