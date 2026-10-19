// The S1 check of the scene_color write-back (docs/analysis/taa.md, step 6), a
// post pass of the runtime's post pass framework (passes.toml, post_pass_path;
// see docs/analysis/post-pass-framework.md). Original code of this project.
//
// Entry point of passes.toml:
//   scene_pattern  pixel: the scene color with a test pattern over its middle
//                  third (white and magenta squares of 16 host pixels in a
//                  green frame) -> scene_written, which the framework writes
//                  back into the game's scene texture, so the game's bloom,
//                  motion blur and final combine sample the pattern.

Texture2D<float4> scene_color_in : register(t0);

float4 scene_pattern(XePostPassVertex input) : SV_Target {
  uint2 pixel = uint2(input.position.xy);
  float4 color = scene_color_in.Load(int3(pixel, 0));
  uint2 low = xe_pass_size / 3u, high = xe_pass_size - xe_pass_size / 3u;
  if (any(pixel < low) || any(pixel >= high)) {
    return color;
  }
  // The game's alpha stays as it was.
  if (any(pixel < low + 4u) || any(pixel >= high - 4u)) {
    return float4(0.0, 1.0, 0.0, color.a);
  }
  uint2 cell = (pixel - low) >> 4;
  return ((cell.x + cell.y) & 1u) != 0u ? float4(1.0, 1.0, 1.0, color.a)
                                      : float4(1.0, 0.0, 1.0, color.a);
}
