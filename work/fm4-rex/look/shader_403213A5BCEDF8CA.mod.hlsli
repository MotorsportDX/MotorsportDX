// Cockpit interior of shaders_ui_v16. MinCockpitShadow in c91; docs/analysis/cockpit-
// brightness.md. See fm4-cockpit.hlsli.
// The fill: shLightScalars in c69 (Y) scales the hemispherical term, the track lightmap
// is fetch 10 (c60 its range), the normal is interpolator 1.
#define FM4_COCKPIT_FLOOR_CONSTANT 91u
#define FM4_COCKPIT_AMBIENT_CONSTANT 69u
#define FM4_COCKPIT_LIGHTMAP_FETCH 10u
#include "fm4-cockpit.hlsli"

#undef XE_HOOK_CONSTANT
#define XE_HOOK_CONSTANT(n, value) fm4_cockpit_constant(n, value)
#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) fm4_cockpit_interior_fetch(fetch, value)
#undef XE_HOOK_INPUT
#define XE_HOOK_INPUT(p) fm4_cockpit_input(p.r[1])
