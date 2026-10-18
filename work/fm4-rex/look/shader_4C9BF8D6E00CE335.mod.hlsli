// Trees and bushes (TREE_DIFF_OPAC_2_2SD, BUSH_DIFF_OPAC_2_2SD_FADE_VCLR_ANIM).
// Lit by c247 to c248 blended by the lightmap (fetch 7), times a ModelData (c148)
// value.
// docs/analysis/look-mod-coverage.md. The diffuse texture in fetch 0, fogged towards c242.
// See fm4_foliage in fm4-nature.hlsli.
#include "fm4-nature.hlsli"

#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) fm4_foliage_fetch(fetch, value)
