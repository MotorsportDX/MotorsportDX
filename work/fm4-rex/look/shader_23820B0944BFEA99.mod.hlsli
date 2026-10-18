// The road with a normal map, a cube map reflection and vertex color of Tsukuba
// (ROAD_BLND_REFL_NORM_ANI3_VCLR_4). The asphalt (Blend_B) in fetch 0. Its highlight is the cube
// map (fetch 2) times the W of c249, with no sun direction, so it keeps it: only the tone knobs
// apply.
// docs/analysis/look-mod-coverage.md. See fm4_road_asphalt in fm4-road.hlsli.
#include "fm4-road.hlsli"

#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) FM4_ROAD_ASPHALT_FETCH(0, fetch, value, coord)
