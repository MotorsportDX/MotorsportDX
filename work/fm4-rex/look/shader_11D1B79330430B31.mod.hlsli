// Cockpit interior material shader (cockpit_tiling_cloth2, cockpit_tiling_cloth3).
// MinShadowValue in c87; docs/analysis/cockpit-brightness.md. See fm4-cockpit.hlsli.
#define FM4_COCKPIT_FLOOR_CONSTANT 87u
#include "fm4-cockpit.hlsli"

#undef XE_HOOK_CONSTANT
#define XE_HOOK_CONSTANT(n, value) fm4_cockpit_constant(n, value)
