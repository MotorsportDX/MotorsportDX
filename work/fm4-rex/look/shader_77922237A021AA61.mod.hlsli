// Cockpit interior material shader (cockpit_anodized_aluminum, engine_anodized_aluminum). The
// engine bay materials share it. MinShadowValue in c86; docs/analysis/cockpit-brightness.md.
// See fm4-cockpit.hlsli.
#define FM4_COCKPIT_FLOOR_CONSTANT 86u
#include "fm4-cockpit.hlsli"

#undef XE_HOOK_CONSTANT
#define XE_HOOK_CONSTANT(n, value) fm4_cockpit_constant(n, value)
