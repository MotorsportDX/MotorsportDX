// The driver's suit (ApendagesSuede: the gloves' suede); docs/analysis/cockpit-brightness.md.
// The shadow term is the smaller of the track lightmap's share (fetch 10, c60 its range) and
// the variance shadow map's bound (fetch 13, at interpolator 4: XY the coordinate, Z the
// pixel's depth), at least a small literal floor. See fm4_cockpit_driver_fetch in
// fm4-cockpit.hlsli.
#include "fm4-cockpit.hlsli"

#undef XE_HOOK_INPUT
#define XE_HOOK_INPUT(p) fm4_cockpit_depth = p.r[4].z
#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) fm4_cockpit_driver_fetch(fetch, value, 10u, XE_C(60).xy)
