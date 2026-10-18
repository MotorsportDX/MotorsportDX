// FM4 look mods of the track's sky and vegetation. Original code of this project,
// like fm4-car.hlsli: it hooks the texture fetches of the game's sky, tree, bush
// and grass shaders (docs/analysis/look-mod-coverage.md) and changes the color
// they read.
//   FM4_LOOK_SKY_SATURATION      >= 0     saturation of the sky dome (1: original)
//   FM4_LOOK_SKY_CONTRAST        0...1    S-curve on the brightness of the sky dome, so
//                                         clouds stand out more against the sky (0: original)
//   FM4_LOOK_SKY_WARMTH          -1...1   white balance of the sky dome: below 0 bluer,
//                                         above 0 warmer (0: original)
//   FM4_LOOK_SKY_HORIZON         0...1    a gradient from the horizon to the zenith: a paler,
//                                         brighter horizon and a deeper zenith, as haze and
//                                         the scattering of the air give (0: original)
//   FM4_LOOK_FOLIAGE_HUE         degrees  the hue of the green of leaves and grass, turned on
//                                         the color wheel: below 0 towards yellow and orange
//                                         (-30 dry, -60 autumn), above 0 towards blue green
//                                         (0: original)
//   FM4_LOOK_FOLIAGE_SATURATION  >= 0     saturation of that green (1: original)
//   FM4_LOOK_FOLIAGE_BRIGHTNESS  >= 0     brightness of that green (1: original)
// The track knob sky (pipeline.py) scales the sky's brightness in the game's own
// constant (skyScale); these knobs change its color.
//
// The sky shaders (SKY_DIFF_1) read the sky dome texture in fetch 0 at the
// dome's UV (interpolator 1) and multiply it by SkyScale (c222.x) plus TimeGain
// (c249.y) times its alpha, which carries the brightness of the sky's bright
// parts above 1. Their vertex shader writes the world position of the dome into
// interpolator 0. The sky shader with the sun normalizes it and compares it
// with the direction to the sun (c250) for its sun glow, so it is the direction
// of the view; its Y is the height above the horizon.
//
// The vegetation shaders (TREE_*, TREECARD_*, BUSH_*, GRASS_*, and the cut-out
// DIFF_OPAC_2_2SD that draws trees on some tracks) read the diffuse texture in
// fetch 0, multiply it by the lighting (V2LightmapColor1 and 2, c247 and c248,
// blended by a vertex value or by the lightmap in fetch 7), fog it towards
// FogColor (c242) and write the square root (gamma 2). Their
// texel is linear, and its alpha cuts out the leaves.
//
// Both change the texel's color and keep its alpha, so the game's lighting,
// shadow, fog and cut-out apply to the new color as to the old one.

#ifndef FM4_LOOK_SKY_SATURATION
#define FM4_LOOK_SKY_SATURATION 1.0
#endif
#ifndef FM4_LOOK_SKY_CONTRAST
#define FM4_LOOK_SKY_CONTRAST 0.0
#endif
#ifndef FM4_LOOK_SKY_WARMTH
#define FM4_LOOK_SKY_WARMTH 0.0
#endif
#ifndef FM4_LOOK_SKY_HORIZON
#define FM4_LOOK_SKY_HORIZON 0.0
#endif
#ifndef FM4_LOOK_FOLIAGE_HUE
#define FM4_LOOK_FOLIAGE_HUE 0.0
#endif
#ifndef FM4_LOOK_FOLIAGE_SATURATION
#define FM4_LOOK_FOLIAGE_SATURATION 1.0
#endif
#ifndef FM4_LOOK_FOLIAGE_BRIGHTNESS
#define FM4_LOOK_FOLIAGE_BRIGHTNESS 1.0
#endif

// Rec. 709 luminance of linear color.
static const float3 FM4_NATURE_LUMA = float3(0.2126, 0.7152, 0.0722);

// The direction of the view of the sky pixel (interpolator 0; fm4_sky_input).
static float3 fm4_sky_view = float3(0.0, 1.0, 0.0);

void fm4_sky_input(XePixel p) {
  fm4_sky_view = p.r[0].xyz;
}

// The sky dome texel's color with the sky knobs.
float3 fm4_sky(float3 color) {
  color = max(color, 0.0);
  float luminance = dot(color, FM4_NATURE_LUMA);
  if (FM4_LOOK_SKY_CONTRAST != 0.0) {
    // The S-curve of the global contrast (fm4_grade), on the texel's luminance in
    // gamma 2, with the hue and saturation kept.
    float gamma = sqrt(saturate(luminance));
    float target = lerp(gamma, gamma * gamma * (3.0 - 2.0 * gamma), saturate(FM4_LOOK_SKY_CONTRAST));
    color *= target * target / max(luminance, 1.0e-6);
  }
  if (FM4_LOOK_SKY_WARMTH != 0.0) {
    float warmth = 0.2 * clamp(FM4_LOOK_SKY_WARMTH, -1.0, 1.0);
    float3 balance = float3(1.0 + warmth, 1.0, 1.0 - warmth);
    color *= balance / dot(balance, FM4_NATURE_LUMA);
  }
  luminance = dot(color, FM4_NATURE_LUMA);
  float saturation = max(FM4_LOOK_SKY_SATURATION, 0.0);
  float gain = 1.0;
  if (FM4_LOOK_SKY_HORIZON != 0.0) {
    // 0 at the horizon (and below it), 1 at the zenith, rising fast above the
    // horizon. At 1 the horizon is 0.6 times as saturated and a quarter stop
    // brighter, the zenith 1.4 times as saturated and half a stop darker.
    float height = sqrt(saturate(fm4_sky_view.y * rsqrt(max(dot(fm4_sky_view, fm4_sky_view), 1.0e-20))));
    float horizon = FM4_LOOK_SKY_HORIZON;
    saturation *= max(lerp(1.0 - 0.4 * horizon, 1.0 + 0.4 * horizon, height), 0.0);
    gain = exp2(horizon * lerp(0.25, -0.5, height));
  }
  return max(lerp(luminance.xxx, color, saturation), 0.0) * gain;
}

// Fetches of the sky shaders: 0 the sky dome.
float4 fm4_sky_fetch(uint fetch, float4 value) {
  if (fetch != 0u || (FM4_LOOK_SKY_SATURATION == 1.0 && FM4_LOOK_SKY_CONTRAST == 0.0 &&
                      FM4_LOOK_SKY_WARMTH == 0.0 && FM4_LOOK_SKY_HORIZON == 0.0)) {
    return value;
  }
  return float4(fm4_sky(value.rgb), value.a);
}

// How much of the foliage knobs a texel gets, 0...1. Leaves and grass share
// their textures with bark, branches and flowers, and the grass shader is also
// the chain-link fences' (CHAIN_DIFF_OPAC_2_2SD compiles to the same ucode), so
// the knobs act on green only: in full from yellow green to blue green (a hue of
// 60 to 170 degrees), not at all below 35 or above 195 degrees, and not on gray
// (a saturation below 0.08, in full from 0.3). The hue is the angle in the
// opponent plane (2R - G - B, sqrt(3) (G - B)): 0 red, 120 green, 240 blue.
float fm4_foliage_weight(float3 color) {
  float2 opponent = float2(2.0 * color.r - color.g - color.b, 1.7320508 * (color.g - color.b));
  // cos and sin of 115 degrees, the middle of the full range.
  float centered = dot(opponent, float2(-0.4226183, 0.9063078)) *
                   rsqrt(max(dot(opponent, opponent), 1.0e-20));
  float hue = smoothstep(0.1736482, 0.5735764, centered);
  float high = max(color.r, max(color.g, color.b));
  float low = min(color.r, min(color.g, color.b));
  return hue * smoothstep(0.08, 0.3, (high - low) / max(high, 1.0e-4));
}

// color turned on the color wheel by degrees: a rotation about the gray axis,
// where a positive angle turns red towards green and green towards blue.
float3 fm4_hue_turn(float3 color, float degrees) {
  float s, c;
  sincos(radians(degrees), s, c);
  float3 cross_gray = float3(color.b - color.g, color.r - color.b, color.g - color.r) * 0.5773503;
  float mean = (color.r + color.g + color.b) * (1.0 / 3.0);
  return color * c + cross_gray * s + mean * (1.0 - c);
}

// The vegetation texel's color with the foliage knobs. The turned hue keeps the
// texel's luminance (within a factor of 4), so only the brightness knob makes
// the foliage lighter or darker.
float3 fm4_foliage(float3 color) {
  color = max(color, 0.0);
  float weight = fm4_foliage_weight(color);
  float luminance = dot(color, FM4_NATURE_LUMA);
  if (FM4_LOOK_FOLIAGE_HUE != 0.0) {
    float3 turned = max(fm4_hue_turn(color, FM4_LOOK_FOLIAGE_HUE * weight), 0.0);
    color = turned * clamp(luminance / max(dot(turned, FM4_NATURE_LUMA), 1.0e-6), 0.25, 4.0);
  }
  float saturation = lerp(1.0, max(FM4_LOOK_FOLIAGE_SATURATION, 0.0), weight);
  float brightness = lerp(1.0, max(FM4_LOOK_FOLIAGE_BRIGHTNESS, 0.0), weight);
  return max(lerp(luminance.xxx, color, saturation), 0.0) * brightness;
}

// Fetches of the vegetation shaders: 0 the diffuse texture.
float4 fm4_foliage_fetch(uint fetch, float4 value) {
  if (fetch != 0u || (FM4_LOOK_FOLIAGE_HUE == 0.0 && FM4_LOOK_FOLIAGE_SATURATION == 1.0 &&
                      FM4_LOOK_FOLIAGE_BRIGHTNESS == 1.0)) {
    return value;
  }
  return float4(fm4_foliage(value.rgb), value.a);
}
