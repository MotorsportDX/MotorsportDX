// Cockpit interior of shaders_16car_v16, shaders_splitscreen_v16, shaders_v16.
// MinCockpitShadow in c90; docs/analysis/cockpit-brightness.md. See fm4-cockpit.hlsli.
#define FM4_COCKPIT_FLOOR_CONSTANT 90u
#include "fm4-cockpit.hlsli"

#undef XE_HOOK_CONSTANT
#define XE_HOOK_CONSTANT(n, value) fm4_cockpit_constant(n, value)
