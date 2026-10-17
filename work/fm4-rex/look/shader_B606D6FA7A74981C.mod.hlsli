// FinalCombine with vignette and a watermark (fetch 3, left as it is):
// vignetteColor in c2, the strength in W; filmic tonemap in c9 and c10, LUT
// scale and offset in c4.x and c5.x. See fm4-look.hlsli.
#include "fm4-look.hlsli"
#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) \
  fm4_combine_fetch(fetch, value, coord, XE_C(9), XE_C(10), XE_C(4).x, XE_C(5).x)
#undef XE_HOOK_CONSTANT
#define XE_HOOK_CONSTANT(n, value) fm4_vignette(n, value, 2u)
#undef XE_HOOK_INPUT
#define XE_HOOK_INPUT(p) fm4_combine_input(p)
#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) fm4_grade(p)
