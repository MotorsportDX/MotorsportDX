// The driver's body (default.xex); docs/analysis/cockpit-brightness.md. No floor constant:
// the shadow term is the smaller of the track lightmap's share (fetch 1, c60 its range) and
// the variance shadow map's bound (fetch 13, at interpolator 4: XY the coordinate, Z the
// pixel's depth). See fm4_cockpit_driver_fetch in fm4-cockpit.hlsli.
#include "fm4-cockpit.hlsli"

#undef XE_HOOK_INPUT
#define XE_HOOK_INPUT(p) fm4_cockpit_depth = p.r[4].z
#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) fm4_cockpit_driver_fetch(fetch, value, 1u, XE_C(60).xy)
