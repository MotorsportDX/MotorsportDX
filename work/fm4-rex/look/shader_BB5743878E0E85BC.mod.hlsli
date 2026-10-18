// The sky dome of every race track (SKY_DIFF_1, SKY_DIFF_1_NoClamp);
// docs/analysis/look-mod-coverage.md. The dome texture in fetch 0, times SkyScale
// (c222.x) plus TimeGain (c249.y) times its alpha. The direction of the view in
// interpolator 0, which this shader does not read itself. See fm4_sky in
// fm4-nature.hlsli.
#include "fm4-nature.hlsli"

#undef XE_HOOK_INPUT
#define XE_HOOK_INPUT(p) fm4_sky_input(p)
#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) fm4_sky_fetch(fetch, value)
