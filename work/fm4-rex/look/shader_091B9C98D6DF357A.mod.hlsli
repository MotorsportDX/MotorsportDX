// The sky dome with the sun glow (SKY_DIFF_1) of every race track;
// docs/analysis/look-mod-coverage.md. The dome texture in fetch 0, desaturated
// by c222.y and times c222.x, plus a glow around the direction to the sun (c250,
// SunGainScale c214) at the direction of the view in interpolator 0. See fm4_sky
// in fm4-nature.hlsli.
#include "fm4-nature.hlsli"

#undef XE_HOOK_INPUT
#define XE_HOOK_INPUT(p) fm4_sky_input(p)
#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) fm4_sky_fetch(fetch, value)
