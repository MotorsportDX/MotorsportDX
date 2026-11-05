// Cockpit interior of shaders_16car_v16, shaders_splitscreen_v16, shaders_ui_v16, shaders_v16.
// MinCockpitShadow in c89; docs/analysis/cockpit-brightness.md. See fm4-cockpit.hlsli.
// The fill: shLightScalars in c69 (Y) scales the hemispherical term, the track lightmap
// is fetch 10 (c60 its range), the normal is interpolator 1.
#define FM4_COCKPIT_FLOOR_CONSTANT 89u
#define FM4_COCKPIT_AMBIENT_CONSTANT 69u
#define FM4_COCKPIT_LIGHTMAP_FETCH 10u
// The albedo texture is fetch 2; the fill's limit weighs it.
#define FM4_COCKPIT_ALBEDO_FETCH 2u
#include "fm4-cockpit.hlsli"

#undef XE_HOOK_CONSTANT
#define XE_HOOK_CONSTANT(n, value) fm4_cockpit_constant(n, value)
#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) fm4_cockpit_interior_fetch(fetch, value)
#undef XE_HOOK_INPUT
#define XE_HOOK_INPUT(p) fm4_cockpit_input(p.r[1])
