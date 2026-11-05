// Cockpit interior of shaders_16car_v16, shaders_splitscreen_v16, shaders_v16.
// MinCockpitShadow in c87; docs/analysis/cockpit-brightness.md. See fm4-cockpit.hlsli.
// The fill: ChromeAmbientScale in c81 (X) scales the hemispherical term, the track lightmap
// is fetch 10 (c60 its range), the normal is interpolator 1.
#define FM4_COCKPIT_FLOOR_CONSTANT 87u
#define FM4_COCKPIT_AMBIENT_CONSTANT 81u
#define FM4_COCKPIT_AMBIENT_COMPONENT 0
#define FM4_COCKPIT_LIGHTMAP_FETCH 10u
// The hemispherical term is added without an albedo: the fill is limited as on white.
#define FM4_COCKPIT_FILL_FLAT
#include "fm4-cockpit.hlsli"

#undef XE_HOOK_CONSTANT
#define XE_HOOK_CONSTANT(n, value) fm4_cockpit_constant(n, value)
#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) fm4_cockpit_interior_fetch(fetch, value)
#undef XE_HOOK_INPUT
#define XE_HOOK_INPUT(p) fm4_cockpit_input(p.r[1])
