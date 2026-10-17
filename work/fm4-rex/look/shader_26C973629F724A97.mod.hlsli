// FinalCombine with a watermark (fetch 3, left as it is): otherwise as
// E97AA211B556A9B3, with the filmic tonemap in c6 and c7, LUT scale and offset
// in c1.x and c2.x. See fm4-look.hlsli.
#include "fm4-look.hlsli"
#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) \
  fm4_combine_fetch(fetch, value, coord, XE_C(6), XE_C(7), XE_C(1).x, XE_C(2).x)
#undef XE_HOOK_INPUT
#define XE_HOOK_INPUT(p) fm4_combine_input(p)
#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) fm4_grade(p)
