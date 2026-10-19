// Cockpit interior material shader (cockpit_embossed_badge). MinShadowValue in c84;
// docs/analysis/cockpit-brightness.md. See fm4-cockpit.hlsli.
#define FM4_COCKPIT_FLOOR_CONSTANT 84u
#include "fm4-cockpit.hlsli"

#undef XE_HOOK_CONSTANT
#define XE_HOOK_CONSTANT(n, value) fm4_cockpit_constant(n, value)
