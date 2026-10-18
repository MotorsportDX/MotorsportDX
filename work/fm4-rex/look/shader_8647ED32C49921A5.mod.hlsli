// Tree cards (TREECARD_DIFF_OPAC_2_VERT). Lit by c247 to c248 blended by the
// lightmap (fetch 7), times the vertex color (interpolator 3).
// docs/analysis/look-mod-coverage.md. The diffuse texture in fetch 0, fogged towards c242.
// See fm4_foliage in fm4-nature.hlsli.
#include "fm4-nature.hlsli"

#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) fm4_foliage_fetch(fetch, value)
