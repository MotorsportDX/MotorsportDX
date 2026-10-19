// Cockpit interior material shader (cockpit_bump_plastic, cockpit_bump_plastic2,
// cockpit_bump_plastic3, cockpit_bump_plastic4, cockpit_bump_plastic5, engine_bump_plastic,
// engine_bump_plastic2, engine_bump_plastic3, engine_bump_plastic4, engine_bump_plastic5). The
// engine bay materials share it. MinShadowValue in c84; docs/analysis/cockpit-brightness.md.
// See fm4-cockpit.hlsli.
#define FM4_COCKPIT_FLOOR_CONSTANT 84u
#include "fm4-cockpit.hlsli"

#undef XE_HOOK_CONSTANT
#define XE_HOOK_CONSTANT(n, value) fm4_cockpit_constant(n, value)
