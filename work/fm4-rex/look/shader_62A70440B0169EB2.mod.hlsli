// Trees (TREE_DIFF_OPAC_DIRLIGHT_TRACK_2_2SD) of the Bernese Alps.
// Its UV scaled by c0; lit by c247 to c248 blended by interpolator 2 XYZ, times
// interpolator 3 W.
// docs/analysis/look-mod-coverage.md. The diffuse texture in fetch 0, fogged towards c242.
// See fm4_foliage in fm4-nature.hlsli.
#include "fm4-nature.hlsli"

#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) fm4_foliage_fetch(fetch, value)
