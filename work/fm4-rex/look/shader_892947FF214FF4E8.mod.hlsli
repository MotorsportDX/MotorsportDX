// The second body paint technique of shaders_v16, with the same constants,
// registers and literals as B85EF04779ABD438 (docs/analysis/look-mod-coverage.md).
// See fm4_body_paint_env_cube in fm4-paint.hlsli.
#include "fm4-paint.hlsli"

#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) \
  fm4_body_paint_env_cube(p, input, XE_C(254).w, XE_C(255).x, xe_bool_constant(128u))
