// The lit road with a normal map of Tsukuba (ROAD_BLND_NORM_ANI3_4). The asphalt texture
// (Blend_B) in fetch 0, the blend value in fetch 1, the normal map in fetch 3; the sheen takes
// the normal without the map.
// docs/analysis/look-mod-coverage.md. Lit by the lightmap (fetch 7) and the grayscale shadow
// map (fetch 11) like the road; the sheen has the uniform weight. See
// fm4-road.hlsli.
#include "fm4-road.hlsli"

#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) FM4_ROAD_ASPHALT_FETCH(0, fetch, value, coord)
#undef XE_HOOK_CONSTANT
#define XE_HOOK_CONSTANT(n, value) fm4_road_constant(n, value)
#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) FM4_ROAD_LIT_OUTPUT(p, input, FM4_ROAD_UNIFORM_WEIGHT)
