// The unlit road (ROAD_BLND_2, ROAD_BLND_TRILIN_2) of the 17 race locations of
// 5E33CD06E3AF5B04: the same asphalt (Blend_B, fetch 0), blend (fetch 1) and lightmap (fetch 7),
// with no shadow map and no sun highlight, times the W of c222. It draws in the main scene in
// about a third of the race frames, not in the frozen race cameras.
// docs/analysis/look-mod-coverage.md. See fm4_road_asphalt in fm4-road.hlsli.
#include "fm4-road.hlsli"

#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) FM4_ROAD_ASPHALT_FETCH(0, fetch, value, coord)
