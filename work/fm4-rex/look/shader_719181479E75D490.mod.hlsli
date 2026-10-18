// The lit road with two blend textures (ROAD_2BLND_3, ROAD_2BLND_TRILIN_3) of the
// Nurburgring Nordschleife, Road Atlanta, Sebring and Sunset Peninsula Infield. The asphalt
// texture (Blend_B) in fetch 0 (its alpha the specular mask), the blend mask (Blend_A) in
// fetch 1, the blend value of the rubber in fetch 2 (Y).
// docs/analysis/look-mod-coverage.md. Lit by the lightmap (fetch 7) and the grayscale shadow
// map (fetch 11) like the road; the sheen is weighted by its specular amount. See
// fm4-road.hlsli.
#include "fm4-road.hlsli"

// The sheen's weight from the game's specular amount: the asphalt texel's alpha (fetch 0) and
// the blend value (fetch 2, Y), as in ROAD_BLND_2.
float fm4_road_7191_weight() {
  return fm4_road_weight(fm4_road_amount(fm4_road_texels[0].w, fm4_road_texels[2].y, XE_C(223)));
}

#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) FM4_ROAD_ASPHALT_FETCH(0, fetch, value, coord)
#undef XE_HOOK_CONSTANT
#define XE_HOOK_CONSTANT(n, value) fm4_road_constant(n, value)
#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) FM4_ROAD_LIT_OUTPUT(p, input, fm4_road_7191_weight())
