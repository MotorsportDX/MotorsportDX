// FinalCombine with depth of field, vignette and a watermark (fetch 3, left
// as it is): vignetteColor in c4, the strength in W; filmic tonemap in c23 and
// c24, LUT scale and offset in c6.x and c7.x. See fm4-look.hlsli.
#include "fm4-look.hlsli"
#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) \
  fm4_combine_fetch(fetch, value, coord, XE_C(23), XE_C(24), XE_C(6).x, XE_C(7).x)
#undef XE_HOOK_CONSTANT
#define XE_HOOK_CONSTANT(n, value) fm4_vignette(n, value, 4u)
#undef XE_HOOK_INPUT
#define XE_HOOK_INPUT(p) fm4_combine_input(p)
#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) fm4_grade(p)
