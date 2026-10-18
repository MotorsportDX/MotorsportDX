// The unlit road edge (RDEDG_BLND_DIFF_SPEC_BUMP_5) of the Bernese Alps: the road's
// asphalt (RoadSampler) in fetch 1.
// docs/analysis/look-mod-coverage.md. See fm4_road_asphalt in fm4-road.hlsli.
#include "fm4-road.hlsli"

#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) FM4_ROAD_ASPHALT_FETCH(1, fetch, value, coord)
