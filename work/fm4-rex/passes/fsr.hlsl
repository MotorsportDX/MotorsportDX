// The inputs and the output of AMD FSR 3.1's upscaler at the scene_color point, post passes
// of the runtime's post pass framework (passes.toml, post_pass_path; see
// docs/analysis/fsr-plan.md and docs/analysis/post-pass-framework.md). FSR itself is the
// pass fsr_upscale of type "fsr" between them (the runtime's fsr.h, built with REX_FSR).
// Original code of this project.
//
// Entry points of passes.toml:
//   fsr_prepare  compute: the scene color, the scene depth, the motion mask and the
//                reactive mark -> FSR's color (linear: the stored values are gamma 2), its
//                depth (an inverted projection with an infinite far plane: FSR_DEPTH_NEAR / W,
//                0 for the sky), its reactive mask (motion_mask G, car glass, times
//                FSR_REACTIVE, or more where shader mods marked the pixel: reactive_mark)
//                and its transparency and composition mask (motion_mask R, no motion known,
//                times FSR_COMPOSITION); all in the scene layout, as the motion vectors are
//   fsr_output   pixel: FSR's output (linear) encoded to gamma 2, the game's alpha kept,
//                into a texture that the framework writes back into the game's scene, so
//                the motion blur, bloom, exposure and final combine sample it
// Colors are never the clear gray, which e2e counts as missing geometry.

Texture2D<float4> fsr_scene : register(t0);
Texture2D<float> fsr_scene_depth : register(t1);
Texture2D<float2> fsr_mask : register(t2);
Texture2D<float> fsr_mark : register(t3);
RWTexture2D<float4> fsr_color_out : register(u0);
RWTexture2D<float> fsr_depth_out : register(u1);
RWTexture2D<unorm float> fsr_reactive_out : register(u2);
RWTexture2D<unorm float> fsr_composition_out : register(u3);

[numthreads(8, 8, 1)]
void fsr_prepare(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= xe_pass_size)) {
    return;
  }
  int3 pixel = int3(id.xy, 0);
  float3 color = fsr_scene.Load(pixel).rgb;
  fsr_color_out[id.xy] = float4(color * color, 1.0);
  float w = xe_scene_w(fsr_scene_depth.Load(pixel));
  fsr_depth_out[id.xy] = xe_scene_is_sky(w) ? 0.0 : saturate(FSR_DEPTH_NEAR / w);
  float2 mask = fsr_mask.Load(pixel);
  fsr_reactive_out[id.xy] = saturate(max(mask.g * FSR_REACTIVE, fsr_mark.Load(pixel)));
  fsr_composition_out[id.xy] = saturate(mask.r * FSR_COMPOSITION);
}

Texture2D<float4> fsr_output_color : register(t0);
Texture2D<float4> fsr_output_scene : register(t1);

float4 fsr_output(XePostPassVertex input) : SV_Target {
  int3 pixel = int3(input.position.xy, 0);
  return float4(sqrt(max(fsr_output_color.Load(pixel).rgb, 0.0)),
                fsr_output_scene.Load(pixel).a);
}
