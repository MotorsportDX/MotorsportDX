// The races' final combine (E97AA211B556A9B3) as the project's own tone mapping: a replacement
// of the native shader contract (shaders.toml, docs/analysis/shader-contract.md 8), which reads
// the game shader's inputs by name and takes the place of its HLSL. Original code of this
// project, written from the documented formula of the final combine (the project owner's
// reading of section 2.6 of shader-contract.md, 27 October 2026), not from its ucode or its
// generated HLSL. Provenance, docs/analysis/b4-shader-research.md:
//   "FinalCombine Formula": x = (scene^2 + bloom^2) * 2^exposure; the filmic curve f; the LUT
//     at f(x) * lut_scale / white + lut_offset; the colour transform dot(float4(lut, 1), row)
//     per channel, saturated; the square root (gamma 2); and its details for this shader (the
//     registers in shaders.toml, both fetches at interpolator 0 XY, an alpha of 1).
//   "B4 Step 4: Tone Curves": where A, B, CB, DF, DE and E/F sit in the two filmic constants.
// The look's knobs apply as in the classic mod (shader_E97AA211B556A9B3.mod.hlsli, the same
// functions of fm4-look.hlsli): exposure, sharpening and occlusion on the scene, bloom energy,
// saturation and contrast, the debug views. A tone curve of FM4_LOOK_TONEMAP maps x directly,
// where the classic mod inverts the game's curve from the LUT coordinate first.
// The bloom's wide halo (FM4_LOOK_BLOOM_WIDTH) taps the bloom input as the classic fetch 2 does.
#define FM4_BLOOM_TAP(uv) xe_tex_bloom(uv)
#include "fm4-look.hlsli"

// The game's filmic curve (Hable): A and B in Y and Z of ab, CB, DF, DE and E/F in cdef.
float3 fm4_filmic(float3 x, float4 ab, float4 cdef) {
  return (x * (ab.y * x + cdef.x) + cdef.z) / (x * (ab.y * x + ab.z) + cdef.y) - cdef.w;
}

void xe_replacement(XeInputs inputs, inout XePixel p) {
  // The inputs and the knobs of the scene, as fm4_combine_input and fm4_combine_fetch take them.
  float exposure = inputs.exposure;
  if (FM4_LOOK_EXPOSURE != 0.0) {
    exposure = exposure + FM4_LOOK_EXPOSURE;
  }
  fm4_white_filmic = inputs.white;
  fm4_exposure_scale = exp2(exposure);
  fm4_scene_uv = inputs.uv;
  float4 scene = xe_tex_scene(inputs.uv);
  float4 bloom = xe_tex_bloom(inputs.uv);
  if (FM4_LOOK_HDR_PROBE != 0.0) {
    fm4_probe_scene = scene.rgb;
    fm4_probe_bloom = bloom.rgb;
    fm4_probe_gray =
        fm4_filmic_inverse((0.18 * inputs.white).xxx, inputs.filmic_ab, inputs.filmic_cdef).x;
    fm4_probe_white = fm4_filmic_inverse(inputs.white.xxx, inputs.filmic_ab, inputs.filmic_cdef).x;
  }
  scene = fm4_haze(fm4_ambient_occlusion(fm4_sharpen_scene(scene, inputs.uv), inputs.uv), inputs.uv,
                   inputs.filmic_ab, inputs.filmic_cdef);
  bloom = fm4_bloom(fm4_bloom_wide(bloom, inputs.uv));

  // The exposed scene, the tone curve and the track's grading.
  float3 x = (scene.rgb * scene.rgb + bloom.rgb * bloom.rgb) * fm4_exposure_scale;
  float3 coord;
  if (FM4_LOOK_TONEMAP != 0.0) {
    coord = fm4_tonemap_curve(x, inputs.filmic_ab, inputs.filmic_cdef) * inputs.lut_scale +
            inputs.lut_offset;
  } else {
    coord = fm4_filmic(x, inputs.filmic_ab, inputs.filmic_cdef) *
                (inputs.lut_scale / inputs.white) +
            inputs.lut_offset;
  }
  float4 graded = float4(xe_tex_lut(coord).rgb, 1.0);
  float3 color = saturate(float3(dot(graded, inputs.transform_r), dot(graded, inputs.transform_g),
                                 dot(graded, inputs.transform_b)));
  p.color[0] = float4(sqrt(color), 1.0);
  fm4_grade(p);
}
