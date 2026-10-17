// Motion blur of the scene with fixed taps, like 4746CAD32EF168CF (fetch 0 at
// interpolator 0 XY); probably the photo-mode variant PPMotionBlurDepthPhotoFast
// (docs/analysis/look-mod-coverage.md). See fm4-look.hlsli.
#include "fm4-look.hlsli"
#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) fm4_motion_blur(p, input)
