// Trees (TREE_DIFF_OPAC_DIRLIGHT_2_2SD) with the output times c222.w. Lit as
// 9C8D86C330784DD7.
// docs/analysis/look-mod-coverage.md. The diffuse texture in fetch 0, fogged towards c242.
// See fm4_foliage in fm4-nature.hlsli.
#include "fm4-nature.hlsli"

#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) fm4_foliage_fetch(fetch, value)
