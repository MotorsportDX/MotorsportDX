// The unlit road of Top Gear and Crown Jewel (ROAD_BLND_DIFF_SPEC_3, _LOCALSPEC_3): the
// asphalt (RoadSampler) in fetch 0.
// docs/analysis/look-mod-coverage.md. See fm4_road_asphalt in fm4-road.hlsli.
#include "fm4-road.hlsli"

#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) FM4_ROAD_ASPHALT_FETCH(0, fetch, value, coord)
