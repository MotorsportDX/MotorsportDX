// Cockpit interior material shader (cockpit_bump_metal, cockpit_bump_metal2,
// cockpit_smooth_metal, engine_bump_metal, engine_bump_metal2, engine_smooth_metal). The
// engine bay materials share it. MinShadowValue in c85; docs/analysis/cockpit-brightness.md.
// See fm4-cockpit.hlsli.
#define FM4_COCKPIT_FLOOR_CONSTANT 85u
#include "fm4-cockpit.hlsli"

#undef XE_HOOK_CONSTANT
#define XE_HOOK_CONSTANT(n, value) fm4_cockpit_constant(n, value)
