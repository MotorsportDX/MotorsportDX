// Cut-out objects (DIFF_OPAC_2_2SD) of 22 race locations, among them the dark tree lines of
// the Bernese Alps. Lit by c247 to c248 blended by the lightmap (fetch 7), times the vertex
// color (interpolator 5), plus a sun highlight (c250, c251) masked by the texel's red; its
// other objects keep their color unless they are green.
// docs/analysis/look-mod-coverage.md. The diffuse texture in fetch 0, fogged towards c242.
// See fm4_foliage in fm4-nature.hlsli.
#include "fm4-nature.hlsli"

#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) fm4_foliage_fetch(fetch, value)
