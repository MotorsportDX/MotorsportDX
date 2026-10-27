// FM4 look mods of the track's road surface: the asphalt, its painted lines, its
// edges and the paved shoulders beside it. Original code of this project, like
// fm4-nature.hlsli: it hooks the texture fetches, constants and outputs of the
// game's road shaders (docs/analysis/look-mod-coverage.md) and changes the color
// of the asphalt texture and the sun highlight.
//   FM4_LOOK_ROAD_BRIGHTNESS  >= 0     brightness of the asphalt texture (1: original)
//   FM4_LOOK_ROAD_CONTRAST    >= 0     contrast of the aggregate: each texel's brightness
//                                      against the texture's local mean, which keeps the
//                                      overall tone (0: original)
//   FM4_LOOK_ROAD_SPECULAR    >= 0     the game's sun highlight on the road, its lines,
//                                      edges and shoulders (1: original, 0: off)
//   FM4_LOOK_ROAD_SHEEN       0...1    a GGX sun highlight with Schlick Fresnel on the same
//                                      surfaces, in the sun's share of their diffuse light
//                                      (0: off)
//   FM4_LOOK_ROAD_ROUGHNESS   > 0      roughness of that highlight (GGX alpha is its square)
//   FM4_LOOK_ROAD_SHEEN_GRAIN >= 0     how much that highlight follows the aggregate and the
//                                      skid marks: the power of the asphalt texel's
//                                      luminance against its mean around the pixel (0: a
//                                      smooth sheen)
//   FM4_LOOK_ROAD_DEBUG       0...7    debug views: 1 to 5 of the lit road ROAD_BLND_2 (see
//                                      fm4_road_debug), 6 and 7 of the sheen (see
//                                      fm4_road_output); 0: off
// The track knob road_gloss (pipeline.py) scales RoadSpecular and RoadSpecular2 in
// the track lighting files instead.
//
// The lit road shaders (ROAD_BLND_2 and its variants, the lines RDLINE_*, the
// edges RDEDG_* and the paved shoulders SHLDR_*) all end the same way: the
// surface color times the lower of two lights, each blended from
// V2LightmapColor1 (c247, the shadowed light) to V2LightmapColor2 (c248, the
// sunlit light), one by the dynamic shadow of the sun (the grayscale shadow
// map, fetch 11, through shad_color c240), one by the lightmap (fetch 7). Then
// a highlight of the sun is added: pow(N.H, RoadGlossiness) times sunColor
// (c251), the shadow and a specular amount (on the road from RoadSpecularParams1
// c223 by the texture's alpha and by the blend value of the rubber), times the W
// of TimeGain (c249), with no Fresnel and no N.L. Finally the fog (towards
// FogColor c242) and the square root (gamma 2). Their unlit variants drop the
// shadow map and the highlight.
//
// The tone knobs change the asphalt texel in its fetch, so the game's tint,
// blend, lighting and fog apply to the new color (its alpha, the specular
// mask, stays). road_specular scales the W of c249, which these shaders read
// for their highlight only. road_sheen adds a highlight at the output, before
// the fog. Its light is the sun's share of the diffuse light, c248 - c247, so it
// agrees with how bright the same pixel is in the sun; the game's highlight uses
// sunColor, which has no such relation. The road shaders weight it by the
// game's own specular amount, so the rubber keeps its pattern; the other
// surfaces by a uniform weight of the same size, so they meet the road without a
// seam. At full strength a GGX highlight of a smooth dielectric towards a low sun
// is a bright sheet of its own: the weight stands for the pores of dry asphalt,
// which hide most of its surface from the sun. The specular amount is a smooth
// mask on most tracks, and a highlight that is a share of the diffuse light
// lifts dark texels as much as light ones, so on its own the sheen is an even
// haze over the aggregate and the skid marks (Top Gear, facing the sun). The
// grain gives it their pattern back: where the shader has the asphalt texture,
// the sheen is scaled by the texel's luminance against the mean of the texels
// a few pixels around it (the light stones catch the sun, the binder in the
// pores does not), and where it knows the darkening of the skid marks, by that
// too. The ratio has a mean near 1, so the overall brightness stays and the
// surfaces without the texture (lines, shoulders) meet the road without a
// seam. It scales the highlight after its soft limit, so the grain stays where
// the sheen is brightest.
//
// A rain look can build on this: water fills the pores, so wetness darkens the
// asphalt (road_brightness below 1), raises the sheen's weight towards 1 and
// lowers its roughness.

#ifndef FM4_LOOK_ROAD_BRIGHTNESS
#define FM4_LOOK_ROAD_BRIGHTNESS 1.0
#endif
#ifndef FM4_LOOK_ROAD_CONTRAST
#define FM4_LOOK_ROAD_CONTRAST 0.0
#endif
#ifndef FM4_LOOK_ROAD_SPECULAR
#define FM4_LOOK_ROAD_SPECULAR 1.0
#endif
#ifndef FM4_LOOK_ROAD_SHEEN
#define FM4_LOOK_ROAD_SHEEN 0.0
#endif
#ifndef FM4_LOOK_ROAD_ROUGHNESS
#define FM4_LOOK_ROAD_ROUGHNESS 0.5
#endif
#ifndef FM4_LOOK_ROAD_SHEEN_GRAIN
#define FM4_LOOK_ROAD_SHEEN_GRAIN 1.0
#endif
#ifndef FM4_LOOK_ROAD_DEBUG
#define FM4_LOOK_ROAD_DEBUG 0.0
#endif

// The shared GGX helpers of fm4-paint.hlsli (visibility, Schlick).
#define FM4_PAINT_HELPERS_ONLY
#include "fm4-paint.hlsli"

// Rec. 709 luminance of linear color.
static const float3 FM4_ROAD_LUMA = float3(0.2126, 0.7152, 0.0722);
// Head-on reflectance of asphalt (a dielectric, index about 1.5).
static const float FM4_ROAD_F0 = 0.04;
// The soft limit of the sheen, in units of the sun's diffuse light: a GGX
// highlight towards a low sun reaches tens of times that.
static const float FM4_ROAD_SHEEN_LIMIT = 4.0;
// How many mip levels coarser the local mean of the asphalt texture is read.
static const float FM4_ROAD_MEAN_LOD = 4.0;
// The sheen's weight on surfaces without the road's specular amount: that of a
// road texel with the amount 0.25.
static const float FM4_ROAD_UNIFORM_WEIGHT = 0.5;
// How far from the pixel, in pixels along each screen axis, the grain reads the
// asphalt's mean around it: about the size of a stone of the aggregate near the
// car at 2x.
static const float FM4_ROAD_GRAIN_SPAN = 3.0;
// The range of the grain ratio: dark binder and light stones lie within it.
static const float FM4_ROAD_GRAIN_MIN = 0.25;
static const float FM4_ROAD_GRAIN_MAX = 4.0;

// ---------------------------------------------------------------------------
// The asphalt texture.

// A 2D fetch of fetch constant n with an LOD bias, else as the instruction's
// defaults (XE_TFETCH2D).
#define FM4_ROAD_TFETCH2D_BIAS(n, coord, bias)                                             \
  xe_tfetch_2d(n##u, XE_TEXTURE##n##_2D_U, XE_TEXTURE##n##_2D_S, XE_SAMPLER_DEFAULT(n),     \
               coord, 0.0, false, XE_LOD_DEFAULT, bias, 0.0)

// The asphalt texel (linear) with the tone knobs; mean: the same texture's
// local mean. The contrast raises the ratio of each texel's luminance to the
// mean's to the power 1 + contrast (within 1/8 to 8), so light stones get
// lighter and the binder darker while the mean stays; the hue stays. The alpha
// (the game's specular mask) stays as it is.
float4 fm4_road_asphalt(float4 texel, float4 mean) {
  if (FM4_LOOK_ROAD_BRIGHTNESS == 1.0 && FM4_LOOK_ROAD_CONTRAST == 0.0) {
    return texel;
  }
  float3 color = max(texel.rgb, 0.0);
  if (FM4_LOOK_ROAD_CONTRAST != 0.0) {
    float luminance = max(dot(color, FM4_ROAD_LUMA), 1.0e-6);
    float average = max(dot(max(mean.rgb, 0.0), FM4_ROAD_LUMA), 1.0e-6);
    float ratio = clamp(luminance / average, 0.125, 8.0);
    color *= average * pow(ratio, 1.0 + max(FM4_LOOK_ROAD_CONTRAST, 0.0)) / luminance;
  }
  return float4(color * max(FM4_LOOK_ROAD_BRIGHTNESS, 0.0), texel.a);
}

// The texels of the shader's fetches, the last one of each fetch constant, for
// the output: the lightmap (fetch 7) for the sheen's shadow, and the specular
// masks of the road shaders; fm4_road_previous the one before it, for shaders
// that fetch a constant twice.
static float4 fm4_road_texels[32];
static float4 fm4_road_previous[32];

float4 fm4_road_keep(uint fetch, float4 value) {
  fm4_road_previous[fetch & 31u] = fm4_road_texels[fetch & 31u];
  fm4_road_texels[fetch & 31u] = value;
  return value;
}

// The asphalt color's luminance against its mean around the pixel, for the
// sheen's grain; 1 in shaders without the asphalt texture.
static float fm4_road_grain_ratio = 1.0;

// A texture coordinate FM4_ROAD_GRAIN_SPAN pixels away on screen, diagonally
// (x and y: 1 or -1).
float2 fm4_road_beside(float2 coord, float x, float y) {
  return coord + FM4_ROAD_GRAIN_SPAN * (x * ddx(coord) + y * ddy(coord));
}

// The asphalt fetch: keeps the grain ratio for the sheen and gives the texel
// the tone knobs. mean: the texture's local mean (for road_contrast); center
// and around: the texel fetched again at the pixel, and the mean of four
// texels around it on screen, at the same level of detail and by the same
// code. The grain does not divide the game's texel by them: in the game the
// fetches of this file return a darker texel than the shader's own fetch of
// the same one, which made the ratio a smooth factor of about 3 on Top Gear
// (18 October 2026). offset: what the shader adds to the texel for the
// asphalt's color (0 where it scales the texel instead). The asphalt textures
// of ROAD_BLND_DIFF_SPEC_3, ROAD_BLND_DIFF_LOCALSPEC_3 and the road edges are a
// dark detail that the shader adds to a constant color, so the ratio of the
// texel alone would be far from the color's.
float4 fm4_road_asphalt_fetch(float4 texel, float4 mean, float4 center, float4 around,
                              float3 offset) {
  if (FM4_LOOK_ROAD_SHEEN > 0.0 && FM4_LOOK_ROAD_SHEEN_GRAIN != 0.0) {
    float luminance = dot(max(center.rgb + offset, 0.0), FM4_ROAD_LUMA);
    float average = dot(max(around.rgb + offset, 0.0), FM4_ROAD_LUMA);
    fm4_road_grain_ratio = average > 1.0e-4 ? luminance / average : 1.0;
  }
  return fm4_road_asphalt(texel, mean);
}

// The XE_HOOK_TFETCH of the road shaders: FM4_ROAD_FETCH keeps every texel,
// FM4_ROAD_ASPHALT_FETCH also gives the asphalt texture of fetch n the tone
// knobs and the grain, and FM4_ROAD_ASPHALT_OFFSET_FETCH takes the offset of
// fm4_road_asphalt_fetch. The fetch number of each call is a literal, so only
// the asphalt fetch gets the code, and texels the output does not read cost
// nothing.
#define FM4_ROAD_FETCH(fetch, value, coord) fm4_road_keep(fetch, value)
#define FM4_ROAD_TFETCH2D_BESIDE(n, coord, x, y)                                           \
  FM4_ROAD_TFETCH2D_BIAS(n, fm4_road_beside((coord).xy, x, y), 0.0)
#define FM4_ROAD_ASPHALT_OFFSET_FETCH(n, offset, fetch, value, coord)                      \
  fm4_road_keep(fetch,                                                                     \
                (fetch) == n##u                                                            \
                    ? fm4_road_asphalt_fetch(                                              \
                          value, FM4_ROAD_TFETCH2D_BIAS(n, (coord).xy, FM4_ROAD_MEAN_LOD), \
                          FM4_ROAD_TFETCH2D_BIAS(n, (coord).xy, 0.0),                      \
                          0.25 * (FM4_ROAD_TFETCH2D_BESIDE(n, coord, 1.0, 1.0) +           \
                                  FM4_ROAD_TFETCH2D_BESIDE(n, coord, -1.0, 1.0) +          \
                                  FM4_ROAD_TFETCH2D_BESIDE(n, coord, 1.0, -1.0) +          \
                                  FM4_ROAD_TFETCH2D_BESIDE(n, coord, -1.0, -1.0)),         \
                          offset)                                                          \
                    : (value))
#define FM4_ROAD_ASPHALT_FETCH(n, fetch, value, coord)                                     \
  FM4_ROAD_ASPHALT_OFFSET_FETCH(n, 0.0, fetch, value, coord)

// ---------------------------------------------------------------------------
// The sun highlight.

// The XE_HOOK_CONSTANT of the lit road shaders: road_specular scales the W of
// TimeGain (c249), the gain of their sun highlight.
float4 fm4_road_constant(uint n, float4 value) {
  if (n == 249u && FM4_LOOK_ROAD_SPECULAR != 1.0) {
    value.w *= max(FM4_LOOK_ROAD_SPECULAR, 0.0);
  }
  return value;
}

// The specular amount of the road shaders ROAD_BLND_2 and ROAD_2BLND_3, from
// RoadSpecularParams1 (gloss, c223): X to Y by the asphalt texel's alpha, plus W
// to Z by the blend value of the rubber, at least 0.
float fm4_road_amount(float texture_alpha, float blend, float4 gloss) {
  return max(gloss.x + texture_alpha * (gloss.y - gloss.x) + gloss.w + blend * (gloss.z - gloss.w),
             0.0);
}

// The sheen's weight from the road's specular amount: in full from 0.5.
float fm4_road_weight(float amount) {
  return saturate(2.0 * amount);
}

// The view of a lit road pixel.
struct FM4RoadView {
  float3 n;       // unit normal
  float3 v;       // unit direction to the eye
  float shadow;   // the sun's shadow: 1 lit, 0 shadowed
  float weight;   // the sheen's weight, 0...1
  float grain;    // the sheen's grain: the asphalt against its mean around, 1 for none
  float fog;      // the fog amount
};

#ifdef XE_TEXTURE11_2D_U
// The sun's shadow as every lit road shader computes it for its highlight:
// three taps of the grayscale shadow map (fetch 11), Y at interpolator 3 ZW
// (lit where interpolator 2 Z is negative), Z at 4 XY, and X at 3 XY (lit where
// 2 W, 4 Z and 4 W all are negative), and the lightmap shadow, the lightmap's Y
// mapped from RoadLightmapDarkPoint to RoadLightmapLightPoint (Z and W of
// points, c224). 1 lit, 0 shadowed.
float fm4_road_shadow(XePixel input, float lightmap_y, float4 points) {
  float near = input.r[2].z < 0.0 ? 1.0 : XE_TFETCH2D(11, input.r[3].zw).y;
  float middle = XE_TFETCH2D(11, input.r[4].xy).z;
  bool outside = input.r[2].w < 0.0 && input.r[4].z < 0.0 && input.r[4].w < 0.0;
  float far = outside ? 1.0 : XE_TFETCH2D(11, input.r[3].xy).x;
  float lightmap = saturate((lightmap_y - points.z) / (points.w - points.z));
  return min(min(min(near, middle), far), lightmap);
}

// The view of a lit road shader: interpolator 0 the normal (without the normal
// map of the variants that have one), 1 the direction to the eye, 2 X the fog,
// the lightmap kept from fetch 7; points: c224; weight: the sheen's weight;
// albedo: the shader's own darkening of the asphalt texel (the skid marks), 1
// for none. The grain is the asphalt's grain ratio times that albedo, to the
// power road_sheen_grain.
FM4RoadView fm4_road_view(XePixel input, float4 points, float weight, float albedo) {
  FM4RoadView view;
  view.n = normalize(input.r[0].xyz);
  view.v = normalize(input.r[1].xyz);
  view.shadow = fm4_road_shadow(input, fm4_road_texels[7].y, points);
  view.weight = saturate(weight);
  view.grain = pow(clamp(fm4_road_grain_ratio * max(albedo, 0.0), FM4_ROAD_GRAIN_MIN,
                         FM4_ROAD_GRAIN_MAX),
                   max(FM4_LOOK_ROAD_SHEEN_GRAIN, 0.0));
  view.fog = input.r[2].x;
  return view;
}
#endif

// The sheen: an energy normalized GGX highlight of the sun (height correlated
// Smith visibility, Schlick Fresnel from F0 0.04), scaled by pi like the clear
// coat's to match the game's lighting, which leaves out 1/pi, times the weight
// and the shadow, soft limited at FM4_ROAD_SHEEN_LIMIT, then times the grain.
// sun: the sun's light that the diffuse of the pixel gets (c248 - c247). Linear
// light, before the fog.
float3 fm4_road_sheen(FM4RoadView view, float3 to_sun, float3 sun) {
  float3 l = normalize(to_sun);
  float3 h = normalize(l + view.v);
  float n_dot_v = max(dot(view.n, view.v), 1.0e-4);
  float n_dot_l = saturate(dot(view.n, l));
  float n_dot_h = saturate(dot(view.n, h));
  float r = max(fm4_pass_roughness(FM4_LOOK_ROAD_ROUGHNESS), 0.02);
  float a2 = r * r * r * r;
  float denominator = n_dot_h * n_dot_h * (a2 - 1.0) + 1.0;
  float distribution = a2 / (FM4_PI * denominator * denominator);
  float fresnel = fm4_schlick(FM4_ROAD_F0, dot(l, h));
  float glint =
      FM4_PI * distribution * fm4_ggx_visibility(n_dot_v, n_dot_l, a2) * fresnel * n_dot_l;
  glint *= view.weight * view.shadow;
  glint = glint / (1.0 + glint / FM4_ROAD_SHEEN_LIMIT) * view.grain;
  return glint * max(sun, 0.0) * saturate(FM4_LOOK_ROAD_SHEEN);
}

// Adds linear light before the fog to an output in gamma 2 (the road shaders
// write the square root of their linear color).
void fm4_road_add(inout XePixel p, float3 light, float fog) {
  float3 color = p.color[0].rgb;
  p.color[0].rgb = sqrt(max(color * color + light * (1.0 - saturate(fog)), 0.0));
}

// The sheen of a lit road shader at its output, with its view. On every lit
// road shader with the sheen on, road_debug 6 shows the game's result times
// the grain instead (the same image where the grain is 1, so the auto exposure
// keeps it readable), 7 the sheen alone (fogged towards black).
void fm4_road_output(inout XePixel p, FM4RoadView view, float3 to_sun, float3 sun) {
  float3 sheen = fm4_road_sheen(view, to_sun, sun);
  if (FM4_LOOK_ROAD_DEBUG == 6.0) {
    p.color[0].rgb *= sqrt(view.grain);
  } else if (FM4_LOOK_ROAD_DEBUG == 7.0) {
    p.color[0].rgb = sqrt(max(sheen * (1.0 - saturate(view.fog)), 0.0));
  } else {
    fm4_road_add(p, sheen, view.fog);
  }
}

// The XE_HOOK_OUTPUT of a lit road shader: the sheen towards the sun (c250) in
// the sun's diffuse light (c248 - c247), with the lightmap points of c224, at
// the weight given (FM4_ROAD_UNIFORM_WEIGHT, or fm4_road_weight of the road),
// and in FM4_ROAD_LIT_OUTPUT_ALBEDO with the shader's darkening of the asphalt.
#define FM4_ROAD_LIT_OUTPUT_ALBEDO(p, input, weight, albedo)                               \
  if (FM4_LOOK_ROAD_SHEEN > 0.0) {                                                         \
    fm4_road_output(p, fm4_road_view(input, XE_C(224), weight, albedo), XE_C(250).xyz,     \
                    XE_C(248).xyz - XE_C(247).xyz);                                        \
  }
#define FM4_ROAD_LIT_OUTPUT(p, input, weight) FM4_ROAD_LIT_OUTPUT_ALBEDO(p, input, weight, 1.0)

// ---------------------------------------------------------------------------
// Debug views (road_debug) of shaders whose mod passes the terms of the game's
// result (ROAD_BLND_2). Views 1, 4 and 5 keep the luminance of the game's
// output, so the auto exposure sees the same scene; 2 and 3 do not.
//   1  the share of the sun highlight in the light before the fog: blue none,
//      green half, red all of it; whiter with fog
//   2  the game's result without its sun highlight (diffuse and fog)
//   3  the game's sun highlight alone (fogged towards black)
//   4  which light the diffuse takes: green where the lightmap is darker,
//      magenta where the dynamic shadow is
//   5  the specular amount: red its share from the texture's alpha
//      (RoadSpecular, RoadSpecular2), green its share from the blend value
//      (the rubber, Z and W of c223), blue where it is 0

struct FM4RoadTerms {
  float3 output;          // the game's output, linear
  float3 highlight;       // the game's sun highlight before the fog, linear
  float3 diffuse;         // the rest of the light before the fog
  float3 lightmap_light;  // the asphalt color in the light of the lightmap alone
  float amount_texture;   // the specular amount from the texture's alpha
  float amount_rubber;    // the specular amount from the blend value
  float fog;
  float3 fog_color;
};

// A color of unit luminance in the luminance of y.
float3 fm4_road_false_color(float3 color, float y) {
  return color * (y / max(dot(color, FM4_ROAD_LUMA), 1.0e-6));
}

void fm4_road_debug(inout XePixel p, FM4RoadTerms t) {
  float y = dot(max(t.output, 0.0), FM4_ROAD_LUMA);
  float3 color = t.output;
  if (FM4_LOOK_ROAD_DEBUG == 1.0) {
    float highlight = dot(max(t.highlight, 0.0), FM4_ROAD_LUMA);
    float share = highlight / max(highlight + dot(max(t.diffuse, 0.0), FM4_ROAD_LUMA), 1.0e-6);
    float3 ramp = share < 0.5 ? lerp(float3(0.1, 0.3, 1.0), float3(0.1, 1.0, 0.1), 2.0 * share)
                              : lerp(float3(0.1, 1.0, 0.1), float3(1.0, 0.05, 0.05), 2.0 * share - 1.0);
    color = fm4_road_false_color(lerp(ramp, 1.0, saturate(t.fog)), y);
  } else if (FM4_LOOK_ROAD_DEBUG == 2.0) {
    color = lerp(t.diffuse, t.fog_color, saturate(t.fog));
  } else if (FM4_LOOK_ROAD_DEBUG == 3.0) {
    color = t.highlight * (1.0 - saturate(t.fog));
  } else if (FM4_LOOK_ROAD_DEBUG == 4.0) {
    // The diffuse is the lower of the two lights per channel: below the
    // lightmap's, the dynamic shadow decided it.
    bool shadow = dot(t.diffuse, FM4_ROAD_LUMA) < dot(t.lightmap_light, FM4_ROAD_LUMA) * 0.999;
    color = fm4_road_false_color(shadow ? float3(1.0, 0.1, 1.0) : float3(0.1, 1.0, 0.1), y);
  } else if (FM4_LOOK_ROAD_DEBUG == 5.0) {
    float texture_part = max(t.amount_texture, 0.0), rubber = max(t.amount_rubber, 0.0);
    float total = texture_part + rubber;
    float3 tint = total > 1.0e-4 ? lerp(float3(1.0, 0.05, 0.05), float3(0.1, 1.0, 0.1),
                                        rubber / total)
                                 : float3(0.1, 0.3, 1.0);
    color = fm4_road_false_color(tint, y);
  }
  p.color[0].rgb = sqrt(max(color, 0.0));
}
