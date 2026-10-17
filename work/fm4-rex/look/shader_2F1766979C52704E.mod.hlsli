// FinalCombine_NoPostEffects: only the filmic curve of the exposed scene
// (fetch 0), written linear as F(x) / F(white) (F at white in interpolator 1
// Z), with no bloom, LUT or gamma; filmic tonemap in c0 and c1. Only the tone
// curve knob applies here. See fm4_tonemap_output in fm4-look.hlsli.
#include "fm4-look.hlsli"
#undef XE_HOOK_INPUT
#define XE_HOOK_INPUT(p) fm4_tonemap_input(p)
#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) fm4_tonemap_output(p, XE_C(0), XE_C(1))
