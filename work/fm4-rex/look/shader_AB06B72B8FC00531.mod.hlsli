// The unlit road with two blend textures (ROAD_2BLND_3, TERR_ROAD_2BLND_3) of the tracks of
// 719181479E75D490: the asphalt (Blend_B) in fetch 0.
// docs/analysis/look-mod-coverage.md. See fm4_road_asphalt in fm4-road.hlsli.
#include "fm4-road.hlsli"

#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) FM4_ROAD_ASPHALT_FETCH(0, fetch, value, coord)
