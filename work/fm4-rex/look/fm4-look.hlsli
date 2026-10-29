// FM4 look mods: knobs and functions shared by the shader_<hash>.mod.hlsli files
// of this directory (Build.ps1 -Look, pipeline.py --look: shader_mod_path and
// shader_mod_defines). Original code of this project: the game's own shaders
// are generated from the user's game at run time, and these only hook into
// their texture fetches, constants, inputs and outputs (shader_override.hlsli in
// work/runtime). Every knob defaults to the original look.
//   FM4_LOOK_SHARPEN      0...1  adaptive sharpening of the scene before tonemapping
//   FM4_LOOK_BLOOM        >= 0   bloom energy (1: original)
//   FM4_LOOK_EXPOSURE     stops  added to the auto exposure (0: original)
//   FM4_LOOK_ADAPTATION   0, 1   1: the auto exposure adapts as fast as the game's at 60 fps
//                                at any guest vblank rate (0: original, per frame, so faster
//                                above 60); shader_CAF3881F7C4CDCAA.mod.hlsli
//   FM4_LOOK_CONTRAST     0...1  S-curve on the final image (0: original)
//   FM4_LOOK_SATURATION   >= 0   saturation of the final image (1: original)
//   FM4_LOOK_VIGNETTE     >= 0   vignette strength (1: original, 0: off)
//   FM4_LOOK_MOTION_BLUR  0...1  motion blur (1: original, 0: off)
//   FM4_LOOK_TONEMAP      0...4  tone curve before the color grading LUT: 0 the game's
//                                filmic curve, 1 GT (Uchimura), 2 ACES (Narkowicz fit),
//                                3 neutral (Khronos PBR Neutral), 4 GT7 (Polyphony
//                                Digital's GT7 tone mapping, SDR)
//   FM4_LOOK_AO           0...1  ambient occlusion of the runtime's post_ao pass on the scene
//                                (0: off; needs post_ao=true)
//   FM4_LOOK_AO_DEBUG     0, 1   1: the final combine shaders show the runtime's post_ao
//                                texture as it is (xe_post_ao; post_ao_debug picks its
//                                view) instead of the scene (0: off)
//   FM4_LOOK_HDR_PROBE    0, 1   1: the final combine shaders write a diagnostic image of
//                                the scene's highlight range for the HDR output design
//                                (docs/analysis/hdr-output.md) instead of the scene (0: off)
//   FM4_LOOK_HAZE         >= 0   aerial haze: above 0 the scene is mixed toward the haze's
//                                color by the post passes' haze amount (haze.hlsl, whose HAZE
//                                param is the strength; 0: off)
//   FM4_LOOK_HAZE_BRIGHTNESS >= 0 the haze's brightness in units of 2 stops above the
//                                game's middle gray (1)
//   FM4_LOOK_TEMPERATURE  -1...1 white balance: above 0 warmer, below 0 cooler; 1 raises red
//                                over blue by about 1.27 (0: original). Of the final image, or
//                                with FM4_LOOK_HAZE of the scene's near field before the haze
//                                mix, so the distance and the sky keep the haze's color
//   FM4_LOOK_BLOOM_WIDTH  >= 0   a wide halo of the bloom texture (two rings of taps 3% and
//                                6% of the screen's width out) added to the bloom, in units of
//                                its own energy (0: off)

#ifndef FM4_LOOK_SHARPEN
#define FM4_LOOK_SHARPEN 0.0
#endif
#ifndef FM4_LOOK_BLOOM
#define FM4_LOOK_BLOOM 1.0
#endif
#ifndef FM4_LOOK_EXPOSURE
#define FM4_LOOK_EXPOSURE 0.0
#endif
#ifndef FM4_LOOK_CONTRAST
#define FM4_LOOK_CONTRAST 0.0
#endif
#ifndef FM4_LOOK_SATURATION
#define FM4_LOOK_SATURATION 1.0
#endif
#ifndef FM4_LOOK_VIGNETTE
#define FM4_LOOK_VIGNETTE 1.0
#endif
#ifndef FM4_LOOK_MOTION_BLUR
#define FM4_LOOK_MOTION_BLUR 1.0
#endif
#ifndef FM4_LOOK_TONEMAP
#define FM4_LOOK_TONEMAP 0.0
#endif
#ifndef FM4_LOOK_AO
#define FM4_LOOK_AO 0.0
#endif
#ifndef FM4_LOOK_AO_DEBUG
#define FM4_LOOK_AO_DEBUG 0.0
#endif
#ifndef FM4_LOOK_HDR_PROBE
#define FM4_LOOK_HDR_PROBE 0.0
#endif
#ifndef FM4_LOOK_HAZE
#define FM4_LOOK_HAZE 0.0
#endif
#ifndef FM4_LOOK_HAZE_BRIGHTNESS
#define FM4_LOOK_HAZE_BRIGHTNESS 1.0
#endif
#ifndef FM4_LOOK_TEMPERATURE
#define FM4_LOOK_TEMPERATURE 0.0
#endif
#ifndef FM4_LOOK_BLOOM_WIDTH
#define FM4_LOOK_BLOOM_WIDTH 0.0
#endif

// The bloom texture at a coordinate, filtered as the final combine fetches it: the classic
// mods' fetch 2; a native contract entry defines FM4_BLOOM_TAP before it includes this file.
#if !defined(FM4_BLOOM_TAP) && defined(XE_TEXTURE2_2D_U)
#define FM4_BLOOM_TAP(uv) XE_TFETCH2D(2, uv)
#endif

#ifdef XE_TEXTURE0_2D_U

// Contrast adaptive sharpening (after AMD FidelityFX CAS, cross neighbors only)
// of the scene texture, fetch 0 of the final combine shaders. Its values are
// HDR with gamma 2 (the shaders square them), so the weight comes from the
// range clamped to 0...1, and highlights stay unsharpened.
float4 fm4_sharpen_scene(float4 center, float2 uv) {
  if (FM4_LOOK_SHARPEN <= 0.0) {
    return center;
  }
  float2 texel = 1.0 / xe_texture_size_2d(0u);
  float3 up = XE_TFETCH2D(0, uv + float2(0.0, -texel.y)).rgb;
  float3 left = XE_TFETCH2D(0, uv + float2(-texel.x, 0.0)).rgb;
  float3 right = XE_TFETCH2D(0, uv + float2(texel.x, 0.0)).rgb;
  float3 down = XE_TFETCH2D(0, uv + float2(0.0, texel.y)).rgb;
  float3 middle = center.rgb;
  float3 low = saturate(min(middle, min(min(up, down), min(left, right))));
  float3 high = saturate(max(middle, max(max(up, down), max(left, right))));
  float3 amount = sqrt(saturate(min(low, 1.0 - high) / max(high, 1.0 / 256.0)));
  float3 weight = amount * (-1.0 / lerp(8.0, 5.0, saturate(FM4_LOOK_SHARPEN)));
  float3 sharpened = (middle + weight * (up + down + left + right)) / (1.0 + 4.0 * weight);
  return float4(max(sharpened, 0.0), center.a);
}

// The motion blur shader replaced by the unblurred scene at the pixel, in part
// or completely (its output and the scene texture are both gamma 2).
void fm4_motion_blur(inout XePixel p, XePixel input) {
  if (FM4_LOOK_MOTION_BLUR == 1.0) {
    return;
  }
  float3 scene = XE_TFETCH2D(0, input.r[0].xy).rgb;
  p.color[0].rgb = lerp(scene, p.color[0].rgb, saturate(FM4_LOOK_MOTION_BLUR));
}

#endif  // XE_TEXTURE0_2D_U

// FM4_LOOK_BLOOM_WIDTH: the bloom (gamma 2, as the final combine fetches it) with a wide halo
// added in linear terms, the mean of 16 taps on two rings around the pixel, 3% and 6% of the
// screen's width out (the bloom texture is already blurred, so the taps make a smooth ring).
float4 fm4_bloom_wide(float4 bloom, float2 uv) {
#ifdef FM4_BLOOM_TAP
  if (FM4_LOOK_BLOOM_WIDTH <= 0.0) {
    return bloom;
  }
  float3 halo = 0.0;
  [unroll] for (int i = 0; i < 8; ++i) {
    float angle = (i + 0.5) * (6.2831853 / 8.0);
    float2 step = float2(cos(angle) * (9.0 / 16.0), sin(angle)) * 0.03;
    float3 inner = FM4_BLOOM_TAP(uv + step).rgb;
    float3 outer = FM4_BLOOM_TAP(uv + 2.0 * step).rgb;
    halo += inner * inner + outer * outer;
  }
  return float4(sqrt(bloom.rgb * bloom.rgb + halo * (FM4_LOOK_BLOOM_WIDTH / 16.0)), bloom.a);
#else
  return bloom;
#endif
}

// The bloom texture, squared by the final combine shaders before it is added.
float4 fm4_bloom(float4 bloom) {
  if (FM4_LOOK_BLOOM == 1.0) {
    return bloom;
  }
  return float4(bloom.rgb * sqrt(max(FM4_LOOK_BLOOM, 0.0)), bloom.a);
}

// Tone curves. The final combine shaders map the exposed scene color x with a
// filmic curve F(x) = (x(Ax + CB) + DE) / (x(Ax + B) + DF) - E/F (A and B in Y
// and Z of the first of two constants, CB, DF, DE and E/F in the second), normalize it so
// white reaches 1 (F at white comes in interpolator 1 Z) and look the result up
// in the track's color grading LUT at t * scale + offset. These replace F
// between the two: the LUT coordinate gives F(x) back, F is inverted for x, and
// the new curve maps x, scaled so that middle gray (0.18) stays where the game's
// curve puts it, which keeps the auto exposure and the grading as they are.

// F at white, interpolator 1 Z of the final combine shaders (fm4_combine_input).
static float fm4_white_filmic = 1.0;
// The scene coordinate of the pixel, fetch 0 of the final combine shaders.
static float2 fm4_scene_uv = 0.0;
// Their exposure, 2 to the power of interpolator 1 W (fm4_combine_input).
static float fm4_exposure_scale = 1.0;
// For FM4_LOOK_HDR_PROBE: the scene and bloom fetches (gamma 2) and the exposed
// scene values at which the game's curve gives middle gray and white.
static float3 fm4_probe_scene = 0.0;
static float3 fm4_probe_bloom = 0.0;
static float fm4_probe_gray = 1.0;
static float fm4_probe_white = 1.0;

// The post passes' ambient occlusion at a scene coordinate, 0 (none) to 1: by
// its name for a native shader contract entry that declares it (srv =
// ["post:gtao_occlusion"] in shaders.toml), else by its slot (xe_post_ao).
float fm4_post_ao(float2 uv) {
#ifdef XE_SRV_GTAO_OCCLUSION
  return xe_srv_gtao_occlusion(uv).x;
#else
  return xe_post_ao(uv);
#endif
}

// Ambient occlusion of the runtime's post_ao pass on the scene (gamma 2) at the
// pixel. The scene does not separate ambient light from sunlight, so it fades
// out on bright pixels: from an exposed luminance of 0.2 to 0.8 (middle gray is
// 0.18).
float4 fm4_ambient_occlusion(float4 scene, float2 uv) {
  if (FM4_LOOK_AO <= 0.0) {
    return scene;
  }
  float3 linear_scene = scene.rgb * scene.rgb;
  float luminance = dot(linear_scene, float3(0.2126, 0.7152, 0.0722)) * fm4_exposure_scale;
  float weight = saturate(FM4_LOOK_AO) * (1.0 - smoothstep(0.2, 0.8, luminance));
  return float4(scene.rgb * sqrt(saturate(1.0 - weight * fm4_post_ao(uv))), scene.a);
}

// x for which the game's filmic curve gives y (the positive root of the
// quadratic, in the form that stays precise for either sign of its B term).
float3 fm4_filmic_inverse(float3 y, float4 ab, float4 cdef) {
  float3 q = min(y + cdef.w, 1.0 - 1.0e-6);
  float3 a = ab.y * (1.0 - q);
  float3 b = cdef.x - q * ab.z;
  float3 c = cdef.z - q * cdef.y;
  float3 root = sqrt(max(b * b - 4.0 * a * c, 0.0));
  float3 x = (b >= 0.0) ? (2.0 * c) / min(-b - root, -1.0e-20) : (root - b) / max(2.0 * a, 1.0e-20);
  return max(x, 0.0);
}

// The post passes' haze at a scene coordinate (haze.hlsl): x the amount, 0 (none) to 1, y the
// near field's weight, 1 near and 0 on the sky; by its name for a native shader contract entry
// that declares it, else by its slot.
float2 fm4_post_haze(float2 uv) {
#ifdef XE_SRV_HAZE_AMOUNT
  return xe_srv_haze_amount(uv).xy;
#else
  return xe_post_texture(3u, uv).xy;
#endif
}

// FM4_LOOK_TEMPERATURE on linear color: red up and blue down by 12% at 1, the luminance kept.
float3 fm4_white_balance(float3 linear_color) {
  float t = clamp(FM4_LOOK_TEMPERATURE, -1.0, 1.0) * 0.12;
  float3 balanced = linear_color * float3(1.0 + t, 1.0, 1.0 - t);
  float3 weights = float3(0.2126, 0.7152, 0.0722);
  return balanced * (dot(linear_color, weights) / max(dot(balanced, weights), 1.0e-6));
}

// FM4_LOOK_HAZE: the scene (gamma 2) at the pixel mixed in linear terms toward the haze, a pale
// blue-gray with the luminance of FM4_LOOK_HAZE_BRIGHTNESS times 2 stops above the game's
// middle gray after the exposure, by the post passes' haze amount; before it the white balance
// (FM4_LOOK_TEMPERATURE) warms the near field by its weight. ab and cdef are the shader's
// filmic constants (the game's middle gray).
float4 fm4_haze(float4 scene, float2 uv, float4 ab, float4 cdef) {
  if (FM4_LOOK_HAZE <= 0.0) {
    return scene;
  }
  float2 post = fm4_post_haze(uv);
  float gray = fm4_filmic_inverse((0.18 * fm4_white_filmic).xxx, ab, cdef).x;
  float3 haze = float3(0.80, 0.87, 1.0) / 0.8645 *
                (4.0 * max(FM4_LOOK_HAZE_BRIGHTNESS, 0.0) * gray / max(fm4_exposure_scale, 1.0e-6));
  float3 linear_scene = scene.rgb * scene.rgb;
  if (FM4_LOOK_TEMPERATURE != 0.0) {
    linear_scene = lerp(linear_scene, fm4_white_balance(linear_scene), post.y);
  }
  return float4(sqrt(lerp(linear_scene, haze, post.x)), scene.a);
}

// GT tone curve (Hajime Uchimura, CEDEC 2017): a power toe, a linear middle and
// an exponential shoulder towards 1; P 1, a 1, m 0.22, l 0.4, c 1.33, b 0.
float3 fm4_curve_gt(float3 x) {
  const float m = 0.22, c = 1.33;
  const float l0 = (1.0 - m) * 0.4;
  const float s0 = m + l0, s1 = m + l0;
  const float cp = -1.0 / (1.0 - s1);
  float3 w0 = 1.0 - smoothstep(0.0, m, x);
  float3 w2 = step(s0, x);
  float3 w1 = 1.0 - w0 - w2;
  float3 toe = m * pow(max(x, 0.0) / m, c);
  float3 shoulder = 1.0 - (1.0 - s1) * exp(cp * (x - s0));
  return toe * w0 + x * w1 + shoulder * w2;
}

// ACES filmic curve fit (Krzysztof Narkowicz, 2015).
float3 fm4_curve_aces(float3 x) {
  return (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);
}

// Khronos PBR Neutral: linear up to 0.76, then the brightest channel compressed
// towards 1 and the color desaturated by how much was compressed, so hues keep.
float3 fm4_curve_neutral(float3 color) {
  const float start = 0.8 - 0.04, desaturation = 0.15;
  float low = min(color.r, min(color.g, color.b));
  color -= (low < 0.08) ? low - 6.25 * low * low : 0.04;
  float peak = max(color.r, max(color.g, color.b));
  if (peak < start) {
    return color;
  }
  const float d = 1.0 - start;
  float new_peak = 1.0 - d * d / (peak + d - start);
  color *= new_peak / peak;
  float g = 1.0 - 1.0 / (desaturation * (peak - new_peak) + 1.0);
  return lerp(color, new_peak.xxx, g);
}

// The GT7 operator (fm4_curve_gt7): a port of Polyphony Digital's MIT-licensed sample, in its
// own file so that fm4_look_tonemap_tests can run it on WARP against the sample's formulas.
#include "fm4-gt7.hlsli"

// The selected curve at the exposed scene color x, scaled so that middle gray
// stays where the game's curve puts it. ab and cdef are the shader's filmic
// constants.
float3 fm4_tonemap_curve(float3 x, float4 ab, float4 cdef) {
  // Each curve's input for 0.18, solved offline.
  float curve_gray = FM4_LOOK_TONEMAP == 1.0 ? 0.180942 : FM4_LOOK_TONEMAP == 2.0 ? 0.130171 :
                     FM4_LOOK_TONEMAP == 4.0 ? 0.180599 : 0.22;
  float gray = fm4_filmic_inverse((0.18 * fm4_white_filmic).xxx, ab, cdef).x;
  x *= curve_gray / max(gray, 1.0e-6);
  float3 t = FM4_LOOK_TONEMAP == 1.0 ? fm4_curve_gt(x) :
             FM4_LOOK_TONEMAP == 2.0 ? fm4_curve_aces(x) :
             FM4_LOOK_TONEMAP == 4.0 ? fm4_curve_gt7(x) : fm4_curve_neutral(x);
  return saturate(t);
}

// The LUT coordinate with the selected curve instead of the game's: x from the
// game's coordinate. scale and offset are those of the LUT.
float3 fm4_tonemap_lut_coord(float3 coord, float4 ab, float4 cdef, float scale, float offset) {
  float to_filmic = fm4_white_filmic / (abs(scale) > 1.0e-20 ? scale : 1.0e-20);
  float3 x = fm4_filmic_inverse((coord - offset) * to_filmic, ab, cdef);
  return fm4_tonemap_curve(x, ab, cdef) * scale + offset;
}

// The final combine without post effects writes F(x) / F(white) linear, with
// no LUT: the tone curve replaces F at its output, as at a LUT with scale 1 and
// offset 0. Its input only gives F at white (interpolator 1 Z).
void fm4_tonemap_input(inout XePixel p) {
  fm4_white_filmic = p.r[1].z;
}

void fm4_tonemap_output(inout XePixel p, float4 filmic_ab, float4 filmic_cdef) {
  if (FM4_LOOK_TONEMAP != 0.0) {
    p.color[0].rgb = fm4_tonemap_lut_coord(p.color[0].rgb, filmic_ab, filmic_cdef, 1.0, 0.0);
  }
}

// Fetches of the final combine shaders: 0 the scene, 2 the bloom, 7 the color
// grading LUT (its coordinate is the tone curve's output). filmic_ab, filmic_cdef,
// lut_scale and lut_offset are the shader's constants for fm4_tonemap_lut_coord.
float4 fm4_combine_fetch(uint fetch, float4 value, float4 coord, float4 filmic_ab,
                         float4 filmic_cdef, float lut_scale, float lut_offset) {
#ifdef XE_TEXTURE0_2D_U
  if (fetch == 0u) {
    fm4_scene_uv = coord.xy;
    if (FM4_LOOK_HDR_PROBE != 0.0) {
      fm4_probe_scene = value.rgb;
      fm4_probe_gray = fm4_filmic_inverse((0.18 * fm4_white_filmic).xxx, filmic_ab, filmic_cdef).x;
      fm4_probe_white = fm4_filmic_inverse(fm4_white_filmic.xxx, filmic_ab, filmic_cdef).x;
    }
    return fm4_haze(fm4_ambient_occlusion(fm4_sharpen_scene(value, coord.xy), coord.xy), coord.xy,
                    filmic_ab, filmic_cdef);
  }
#endif
  if (fetch == 2u) {
    fm4_probe_bloom = value.rgb;
    return fm4_bloom(fm4_bloom_wide(value, coord.xy));
  }
#ifdef XE_TEXTURE7_3D_U
  if (fetch == 7u && FM4_LOOK_TONEMAP != 0.0) {
    return XE_TFETCH3D(7, fm4_tonemap_lut_coord(coord.xyz, filmic_ab, filmic_cdef, lut_scale,
                                                lut_offset));
  }
#endif
  return value;
}

// The final combine shaders' exposure: 2 to the power of interpolator 1 W.
void fm4_exposure(inout XePixel p) {
  if (FM4_LOOK_EXPOSURE != 0.0) {
    p.r[1].w = p.r[1].w + FM4_LOOK_EXPOSURE;
  }
}

// Inputs of the final combine shaders: the exposure, and F at white for the
// tone curves.
void fm4_combine_input(inout XePixel p) {
  fm4_exposure(p);
  fm4_white_filmic = p.r[1].z;
  fm4_exposure_scale = exp2(p.r[1].w);
}

// FM4_LOOK_HDR_PROBE. Stops are log2 of an exposed scene value over the one the
// game's curve maps to middle gray (pre-LUT 0.18). By the scene coordinate:
// x < 0.05, a calibration ramp of 17 gray steps along y (n / 16), so a capture
// can undo the gamma ramp; x < 0.1, constants as gray: y < 1/3 the stops of the
// scene's resolve clip (a scene value of 1, without bloom), y < 2/3 the stops of
// the curve's white, else log2 of the exposure, each as (stops + 8) / 16; the
// rest: R the pixel's brightest channel in stops ((stops + 8) / 16), G 1 where
// the scene fetch is clipped at 1, B 1 where the value is at 98% of the curve's white
// or more.
float3 fm4_hdr_probe() {
  float2 uv = fm4_scene_uv;
  float gray = max(fm4_probe_gray, 1.0e-6);
  if (uv.x < 0.05) {
    return (floor(saturate(uv.y) * 16.999) / 16.0).xxx;
  }
  if (uv.x < 0.1) {
    float stops = uv.y < 1.0 / 3.0 ? log2(fm4_exposure_scale / gray) :
                  uv.y < 2.0 / 3.0 ? log2(fm4_probe_white / gray) : log2(fm4_exposure_scale);
    return saturate((stops + 8.0) / 16.0).xxx;
  }
  float3 x = (fm4_probe_scene * fm4_probe_scene + fm4_probe_bloom * fm4_probe_bloom) *
             fm4_exposure_scale;
  float peak = max(x.r, max(x.g, x.b));
  float stops = log2(max(peak, 1.0e-6) / gray);
  float clipped = max(fm4_probe_scene.r, max(fm4_probe_scene.g, fm4_probe_scene.b)) >=
                  1.0 - 1.0 / 2048.0 ? 1.0 : 0.0;
  float white = peak >= 0.98 * fm4_probe_white ? 1.0 : 0.0;
  return float3(saturate((stops + 8.0) / 16.0), clipped, white);
}

// White balance, saturation and contrast of the final image (gamma 2, 0...1).
void fm4_grade(inout XePixel p) {
  float3 color = p.color[0].rgb;
  if (FM4_LOOK_TEMPERATURE != 0.0 && FM4_LOOK_HAZE <= 0.0) {
    // With the haze, fm4_haze applies it to the scene's near field instead.
    color = sqrt(saturate(fm4_white_balance(color * color)));
  }
  if (FM4_LOOK_SATURATION != 1.0) {
    float3 linear_color = color * color;
    float luminance = dot(linear_color, float3(0.2126, 0.7152, 0.0722));
    color = sqrt(saturate(lerp(luminance.xxx, linear_color, max(FM4_LOOK_SATURATION, 0.0))));
  }
  if (FM4_LOOK_CONTRAST != 0.0) {
    float3 s_curve = color * color * (3.0 - 2.0 * color);
    color = lerp(color, s_curve, saturate(FM4_LOOK_CONTRAST));
  }
  if (FM4_LOOK_AO_DEBUG != 0.0) {
    color = fm4_post_ao(fm4_scene_uv).xxx;
  }
  if (FM4_LOOK_HDR_PROBE != 0.0) {
    color = fm4_hdr_probe();
  }
  p.color[0].rgb = color;
}

// The vignette strength in the W of the vignette color constant.
float4 fm4_vignette(uint n, float4 value, uint vignette_color) {
  if (n == vignette_color && FM4_LOOK_VIGNETTE != 1.0) {
    value.w = value.w * max(FM4_LOOK_VIGNETTE, 0.0);
  }
  return value;
}
