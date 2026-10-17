// Motion blur of the scene with a sample loop (numTexSamples, equalWeights),
// probably a photo-mode variant (docs/analysis/look-mod-coverage.md): the
// result is blended with the scene at the pixel (fetch 0 at interpolator 0
// XY) like 4746CAD32EF168CF. The player sets this blur in photo mode, so the
// motion_blur knob is left at 1 (original) unless set. See fm4-look.hlsli.
#include "fm4-look.hlsli"
#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) fm4_motion_blur(p, input)
