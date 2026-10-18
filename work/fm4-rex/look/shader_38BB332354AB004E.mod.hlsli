// The painted lines (RDLINE_BLND_DIFF_OPAC_2) of 7 race locations: the paint in fetch 0,
// the road alpha in fetch 1.
// docs/analysis/look-mod-coverage.md. Lit by the lightmap (fetch 7) and the grayscale shadow
// map (fetch 11) like the road; the sheen has the uniform weight. See fm4-road.hlsli.
#include "fm4-road.hlsli"

#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) FM4_ROAD_FETCH(fetch, value, coord)
#undef XE_HOOK_CONSTANT
#define XE_HOOK_CONSTANT(n, value) fm4_road_constant(n, value)
#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) FM4_ROAD_LIT_OUTPUT(p, input, FM4_ROAD_UNIFORM_WEIGHT)
