// The lit road edge with bump and skids (RDEDG_BLND_DIFF_SPEC_BUMP_4) of the Bernese Alps,
// Crown Jewel and Top Gear: the shoulder texture in fetch 0, the road's asphalt (RoadSampler)
// in fetch 1, the skids in fetch 2, the bump blend in fetch 4.
// docs/analysis/look-mod-coverage.md. Lit by the lightmap (fetch 7) and the grayscale shadow
// map (fetch 11) like the road; the sheen has the uniform weight. See
// fm4-road.hlsli.
// The shader adds c2 to the asphalt texel for its color.
#include "fm4-road.hlsli"

#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) \
  FM4_ROAD_ASPHALT_OFFSET_FETCH(1, XE_C(2).xyz, fetch, value, coord)
#undef XE_HOOK_CONSTANT
#define XE_HOOK_CONSTANT(n, value) fm4_road_constant(n, value)
#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) FM4_ROAD_LIT_OUTPUT(p, input, FM4_ROAD_UNIFORM_WEIGHT)
