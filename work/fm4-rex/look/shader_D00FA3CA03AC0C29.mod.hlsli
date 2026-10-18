// Grass and tree cards (GRASS_DIFF_OPAC_2_2SD_noCARSHAD,
// TREECARD_DIFF_OPAC_TANFADE_2_2SD). Lit by c247 to c248 blended by the
// lightmap (fetch 7).
// docs/analysis/look-mod-coverage.md. The diffuse texture in fetch 0, fogged towards c242.
// See fm4_foliage in fm4-nature.hlsli.
#include "fm4-nature.hlsli"

#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) fm4_foliage_fetch(fetch, value)
