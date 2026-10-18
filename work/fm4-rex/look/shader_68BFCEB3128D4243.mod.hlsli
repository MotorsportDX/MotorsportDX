// The unlit road of Indianapolis, Infineon and Tsukuba (ROAD_BLND_NORM_ANI3_4,
// ROAD_BLND_REFL_NORM_4, _NOCUBE_4): the asphalt (Blend_B) in fetch 0.
// docs/analysis/look-mod-coverage.md. See fm4_road_asphalt in fm4-road.hlsli.
#include "fm4-road.hlsli"

#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) FM4_ROAD_ASPHALT_FETCH(0, fetch, value, coord)
