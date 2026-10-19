// Cockpit interior material shader (cockpit_cloth, cockpit_rubber, engine_cloth). The engine
// bay materials share it. MinShadowValue in c83; docs/analysis/cockpit-brightness.md. See
// fm4-cockpit.hlsli.
#define FM4_COCKPIT_FLOOR_CONSTANT 83u
#include "fm4-cockpit.hlsli"

#undef XE_HOOK_CONSTANT
#define XE_HOOK_CONSTANT(n, value) fm4_cockpit_constant(n, value)
