// The lit road edge with bump and skids (RDEDG_BLND_DIFF_SPEC_BUMP_5) of the Bernese Alps.
// It blends the shoulder texture (fetch 0) with the road's asphalt (RoadSampler, fetch 1) and
// the skids (fetch 2) by the bump blend (fetch 4); the sheen takes the normal without the bump.
// docs/analysis/look-mod-coverage.md. Lit by the lightmap (fetch 7) and the grayscale shadow
// map (fetch 11) like the road; the sheen has the uniform weight. See
// fm4-road.hlsli.
#include "fm4-road.hlsli"

#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) FM4_ROAD_ASPHALT_FETCH(1, fetch, value, coord)
#undef XE_HOOK_CONSTANT
#define XE_HOOK_CONSTANT(n, value) fm4_road_constant(n, value)
#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) FM4_ROAD_LIT_OUTPUT(p, input, FM4_ROAD_UNIFORM_WEIGHT)
