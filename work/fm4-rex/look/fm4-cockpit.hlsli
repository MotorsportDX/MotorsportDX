// FM4 cockpit interior light. Original code of this project: it raises inputs of the game's
// interior and driver shaders (docs/analysis/cockpit-brightness.md) and adds no lighting
// code of its own beyond the weights below.
//   FM4_LOOK_COCKPIT_FILL   0...2  the sky's fill of the shaded interior (1 in the presets,
//                                  0: the game's interior)
//   FM4_LOOK_COCKPIT_LIGHT  0...1  the least share of the sun's direct light that the
//                                  interior keeps in its own shadow (0: the game's interior;
//                                  off in the presets since 26 October 2026)
//
// The interior shaders light a pixel with the sun times N.L times a shadow term, plus a
// small hemispherical term: a baked per-vertex ambient times shLightScalars.y (the track's
// CockpitHemisphericalScalar). The shadow term is the Chebyshev bound of the cockpit's
// variance shadow map (fetch 13), limited by the track lightmap under the car, and at least
// a floor constant of the material (MinCockpitShadow or MinShadowValue), which is 0 in the
// cars of the game. On a sunny track the roof and pillars shade most of the interior while
// the auto exposure follows the bright snow and sky in the windows, so the shaded interior
// keeps only the hemispherical term and lands near black.
//
// The fill scales that hemispherical term by 1 + FM4_COCKPIT_FILL_GAIN * fill * w * s:
// w = (1 + N.y) / 2 is the sky's share for the pixel's interpolated normal (world Y up), so
// a dashboard top facing up takes the most and a headliner facing down almost none; s is
// 1 - FM4_COCKPIT_FILL_SHADE * (1 - the track lightmap's share of the sun under the car), so
// in tunnels and in the shade of walls and stands part of the fill goes. It does not follow
// the sun's direction, which made the floor (FM4_LOOK_COCKPIT_LIGHT) swing between a lit and
// a black interior as the car turned, and lit the interior in tunnels.
//
// The fill's light on a surface is its albedo a times the added factor e = G * fill * w * s,
// so a light surface (white paddles, a light gray dashboard) took it in full and lost its
// shading near white while a dark one stayed readable (5 November 2026). The added light
// a * e is soft limited to FM4_COCKPIT_FILL_LIMIT: e / (1 + a * e / limit) in place of e.
// a is the mean of the albedo texture's channels, from the fetch FM4_COCKPIT_ALBEDO_FETCH
// (before the shader reads the hemispherical scale), or 1 with FM4_COCKPIT_FILL_FLAT for
// the shaders that add the hemispherical term without an albedo (the interior chrome). A
// shader with neither keeps the fill unlimited.
//
// fm4_cockpit_constant is the XE_HOOK_CONSTANT of the interior shaders: their
// shader_<hash>.mod.hlsli defines, before the include, FM4_COCKPIT_FLOOR_CONSTANT (the floor
// constant, X), and for the fill FM4_COCKPIT_AMBIENT_CONSTANT with
// FM4_COCKPIT_AMBIENT_COMPONENT (the hemispherical term's scale: shLightScalars Y, or
// ChromeAmbientScale X) and FM4_COCKPIT_LIGHTMAP_FETCH (the track lightmap, its range in c60),
// with fm4_cockpit_interior_fetch as XE_HOOK_TFETCH and fm4_cockpit_input as XE_HOOK_INPUT
// (the interpolated normal). The fill reads these after the shader's lightmap fetch, which
// comes first in every interior shader. The driver's shaders (the body, and the gloves,
// sleeves, stitching and logos of the suit) have no hemispherical term and no floor constant:
// fm4_cockpit_driver_fetch limits their shadow term from below through the fetches instead.

#ifndef FM4_LOOK_COCKPIT_LIGHT
#define FM4_LOOK_COCKPIT_LIGHT 0.0
#endif
#ifndef FM4_LOOK_COCKPIT_FILL
#define FM4_LOOK_COCKPIT_FILL 0.0
#endif

// The fill at strength 1, from the A/B of 25 October 2026 (docs/analysis/cockpit-brightness.md).
#ifndef FM4_COCKPIT_FILL_GAIN
#define FM4_COCKPIT_FILL_GAIN 20.0
#endif
#ifndef FM4_COCKPIT_FILL_SHADE
#define FM4_COCKPIT_FILL_SHADE 0.65
#endif
// The soft limit of the fill's added light, in units of the hemispherical term; 0: none.
#ifndef FM4_COCKPIT_FILL_LIMIT
#define FM4_COCKPIT_FILL_LIMIT 2.0
#endif
// The driver's floor on its own shadow map term per unit of fill.
#ifndef FM4_COCKPIT_FILL_DRIVER
#define FM4_COCKPIT_FILL_DRIVER 0.25
#endif
#ifndef FM4_COCKPIT_AMBIENT_COMPONENT
#define FM4_COCKPIT_AMBIENT_COMPONENT 1
#endif

// The sky's share for the pixel's interpolated normal.
static float fm4_cockpit_up = 1.0;

void fm4_cockpit_input(float4 normal) {
  float length_sq = dot(normal.xyz, normal.xyz);
  fm4_cockpit_up = length_sq > 1.0e-8 ? saturate(0.5 + 0.5 * normal.y * rsqrt(length_sq)) : 1.0;
}

// The track lightmap texel (X) under the car, as the interior shader fetched it; -1 before.
static float fm4_cockpit_lightmap = -1.0;

// The track lightmap's share of the sun, saturate((L * L - c60.x) / (c60.y - c60.x)), as the
// interior shaders compute it; 1 without a lightmap.
float fm4_cockpit_sun_share() {
#ifdef FM4_COCKPIT_LIGHTMAP_FETCH
  if (fm4_cockpit_lightmap >= 0.0) {
    float2 range = XE_C(60).xy;
    return saturate((fm4_cockpit_lightmap * fm4_cockpit_lightmap - range.x) /
                    (range.y - range.x));
  }
#endif
  return 1.0;
}

// The albedo the fill's limit weighs: the albedo texel's mean, 1 for a flat hemispherical
// term, -1 when unknown (no limit).
#ifdef FM4_COCKPIT_FILL_FLAT
static float fm4_cockpit_albedo = 1.0;
#else
static float fm4_cockpit_albedo = -1.0;
#endif

float4 fm4_cockpit_interior_fetch(uint fetch, float4 value) {
#ifdef FM4_COCKPIT_LIGHTMAP_FETCH
  if (fetch == FM4_COCKPIT_LIGHTMAP_FETCH) {
    fm4_cockpit_lightmap = value.x;
  }
#endif
#ifdef FM4_COCKPIT_ALBEDO_FETCH
  if (fetch == FM4_COCKPIT_ALBEDO_FETCH) {
    fm4_cockpit_albedo = saturate((value.x + value.y + value.z) * (1.0 / 3.0));
  }
#endif
  return value;
}

// The factor of the hemispherical term.
float fm4_cockpit_fill() {
  float shade = 1.0 - FM4_COCKPIT_FILL_SHADE * (1.0 - fm4_cockpit_sun_share());
  float added = FM4_COCKPIT_FILL_GAIN * max(FM4_LOOK_COCKPIT_FILL, 0.0) * fm4_cockpit_up * shade;
  if (FM4_COCKPIT_FILL_LIMIT > 0.0 && fm4_cockpit_albedo >= 0.0) {
    added /= 1.0 + added * fm4_cockpit_albedo / FM4_COCKPIT_FILL_LIMIT;
  }
  return 1.0 + added;
}

#ifdef FM4_COCKPIT_FLOOR_CONSTANT
float4 fm4_cockpit_constant(uint n, float4 value) {
  if (n == FM4_COCKPIT_FLOOR_CONSTANT) {
    value.x = max(value.x, saturate(FM4_LOOK_COCKPIT_LIGHT));
  }
#ifdef FM4_COCKPIT_AMBIENT_CONSTANT
  if (n == FM4_COCKPIT_AMBIENT_CONSTANT) {
#if FM4_COCKPIT_AMBIENT_COMPONENT == 0
    value.x *= fm4_cockpit_fill();
#else
    value.y *= fm4_cockpit_fill();
#endif
  }
#endif
  return value;
}
#endif

// The driver's shaders. Their shadow term is the smaller of the track lightmap's share,
// saturate((L * L - c60.x) / (c60.y - c60.x)) of the lightmap L (X of lightmap_fetch: 1 for
// the body, 10 for the suit), and the Chebyshev bound p = v / (v + d * d) of one tap of the
// variance shadow map (fetch 13: X the mean depth M1, Y the mean square M2; v = M2 - M1 * M1,
// d the pixel's depth minus M1), darkened to p^4. The suit's shaders then raise it to a
// small floor of their own. A floor f raises the shadow map term so that it is at least f:
// v >= q * d * d / (1 - q) with q = f^(1/4) (a ramp gentler than p^4 lands above f). The
// fill's floor (FM4_COCKPIT_FILL_DRIVER per unit) raises only that term, so the track
// lightmap's share still limits it in tunnels and shade; FM4_LOOK_COCKPIT_LIGHT also raises
// the lightmap, L >= sqrt(c60.x + f * (c60.y - c60.x)), as its floor does in the interior.
// fm4_cockpit_depth: the pixel's depth in the shadow map (the Z of the shadow map
// coordinate's interpolator), from the shader's input.
static float fm4_cockpit_depth = 0.0;

float4 fm4_cockpit_driver_fetch(uint fetch, float4 value, uint lightmap_fetch,
                                float2 lightmap_range) {
  float light = saturate(FM4_LOOK_COCKPIT_LIGHT);
  float shadow_floor =
      max(light, saturate(FM4_COCKPIT_FILL_DRIVER * max(FM4_LOOK_COCKPIT_FILL, 0.0)));
  if (fetch == lightmap_fetch) {
    if (light > 0.0) {
      float lightmap = lightmap_range.x + light * (lightmap_range.y - lightmap_range.x);
      value.x = max(value.x, sqrt(max(lightmap, 0.0)));
    }
  } else if (fetch == 13u && shadow_floor > 0.0) {
    float q = min(sqrt(sqrt(shadow_floor)), 0.999);
    float d = max(fm4_cockpit_depth - value.x, 0.0);
    value.y = max(value.y, value.x * value.x + q * d * d / (1.0 - q));
  }
  return value;
}
