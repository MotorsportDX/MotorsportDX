// FM4 cockpit interior light. Original code of this project: it raises an input of the
// game's interior shaders (docs/analysis/cockpit-brightness.md) and adds no code of its own
// to their lighting.
//   FM4_LOOK_COCKPIT_LIGHT  0...1  the least share of the sun's direct light that the
//                                  interior keeps in its own shadow (0: the game's interior)
//
// The interior shaders light a pixel with the sun times N.L times a shadow term, plus a
// small baked ambient term. The shadow term is the Chebyshev bound of the cockpit's variance
// shadow map (fetch 13), limited by the track lightmap under the car, and at least a floor
// constant of the material (MinCockpitShadow or MinShadowValue), which is 0 in the cars of
// the game. On a sunny track the roof and pillars shade most of the interior while the auto
// exposure follows the bright snow and sky in the windows, so the shaded interior keeps only
// the ambient term and lands near black. Raising the floor lights it from the sun's
// direction without changing the exposure, the windows or the bloom; faces turned away from
// the sun stay as dark as the game draws them.
//
// fm4_cockpit_constant is the XE_HOOK_CONSTANT of the interior shaders with a floor
// constant: their shader_<hash>.mod.hlsli defines FM4_COCKPIT_FLOOR_CONSTANT, the number of
// that constant (each shader reads its X), before the include. The driver's shaders (the
// body, and the gloves, sleeves, stitching and logos of the suit) have no such constant:
// fm4_cockpit_driver_fetch limits their shadow term from below through the fetches instead.

#ifndef FM4_LOOK_COCKPIT_LIGHT
#define FM4_LOOK_COCKPIT_LIGHT 0.0
#endif

#ifdef FM4_COCKPIT_FLOOR_CONSTANT
float4 fm4_cockpit_constant(uint n, float4 value) {
  if (n == FM4_COCKPIT_FLOOR_CONSTANT) {
    value.x = max(value.x, saturate(FM4_LOOK_COCKPIT_LIGHT));
  }
  return value;
}
#endif

// The driver's shaders. Their shadow term is the smaller of the track lightmap's share,
// saturate((L * L - c60.x) / (c60.y - c60.x)) of the lightmap L (X of lightmap_fetch: 1 for
// the body, 10 for the suit), and the Chebyshev bound p = v / (v + d * d) of one tap of the
// variance shadow map (fetch 13: X the mean depth M1, Y the mean square M2; v = M2 - M1 * M1,
// d the pixel's depth minus M1), darkened to p^4. The suit's shaders then raise it to a
// small floor of their own. The floor f of the knob raises both inputs so that each term is
// at least f, as the floor constant of the interior shaders does to the smaller of them:
// L >= sqrt(c60.x + f * (c60.y - c60.x)), and v >= q * d * d / (1 - q) with q = f^(1/4)
// (a ramp gentler than p^4 lands above f).
// fm4_cockpit_depth: the pixel's depth in the shadow map (the Z of the shadow map
// coordinate's interpolator), from the shader's input.
static float fm4_cockpit_depth = 0.0;

float4 fm4_cockpit_driver_fetch(uint fetch, float4 value, uint lightmap_fetch,
                                float2 lightmap_range) {
  float floor_share = saturate(FM4_LOOK_COCKPIT_LIGHT);
  if (floor_share <= 0.0) {
    return value;
  }
  if (fetch == lightmap_fetch) {
    float lightmap = lightmap_range.x + floor_share * (lightmap_range.y - lightmap_range.x);
    value.x = max(value.x, sqrt(max(lightmap, 0.0)));
  } else if (fetch == 13u) {
    float q = min(sqrt(sqrt(floor_share)), 0.999);
    float d = max(fm4_cockpit_depth - value.x, 0.0);
    value.y = max(value.y, value.x * value.x + q * d * d / (1.0 - q));
  }
  return value;
}
