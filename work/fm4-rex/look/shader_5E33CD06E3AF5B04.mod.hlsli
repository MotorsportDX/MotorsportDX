// The lit road (ROAD_BLND_2, ROAD_BLND_TRILIN_2) of 17 race locations, among them the Bernese
// Alps; docs/analysis/look-mod-coverage.md. The asphalt texture (Blend_B) in fetch 0, its alpha
// the specular mask; the blend value of the rubber in fetch 1 (Y); the lightmap in fetch 7; the
// grayscale shadow map in fetch 11. Its registers at the end, for the debug views: r0.w the
// highlight's shadow times its amount, r1 the fog color minus the light before the fog, r3 the
// asphalt color in the lightmap's light, r4 YZW the asphalt color, r7 the unit normal, r8 YZW
// the eye direction plus the sun direction, r10.x the blend value, r12 the asphalt texel. See
// fm4-road.hlsli.
#include "fm4-road.hlsli"

// The terms of the game's result, from its registers: the highlight is
// pow(N.H, power) times sunColor (c251), its shadow times amount (r0.w) and the
// W of c249, with the power from RoadGlossiness and RoadGlossiness2 (c224 X and
// Y) by the amount, and the amount from RoadSpecularParams1 (c223): X to Y by
// the texel's alpha plus W to Z by the blend value.
void fm4_road_5e33_debug(inout XePixel p, XePixel input) {
  float4 gloss = XE_C(223), glossiness = XE_C(224);
  FM4RoadTerms t;
  t.amount_texture = gloss.x + p.r[12].w * (gloss.y - gloss.x);
  t.amount_rubber = gloss.w + p.r[10].x * (gloss.z - gloss.w);
  float amount = max(t.amount_texture + t.amount_rubber, 0.0);
  float power = glossiness.x + (glossiness.y - glossiness.x) * amount;
  float lobe = exp2(power * log2(saturate(dot(p.r[7].xyz, normalize(p.r[8].yzw)))));
  t.output = p.r[0].xyz;
  t.highlight = lobe * XE_C(251).xyz * p.r[0].w * fm4_road_constant(249u, XE_C(249)).w;
  t.diffuse = XE_C(242).xyz - p.r[1].xyz - t.highlight;
  t.lightmap_light = p.r[3].xyz;
  t.fog = input.r[2].x;
  t.fog_color = XE_C(242).xyz;
  fm4_road_debug(p, t);
}

// The sheen's weight from the game's specular amount: the asphalt texel's alpha (fetch 0) and
// the blend value (fetch 1, Y).
float fm4_road_5e33_weight() {
  return fm4_road_weight(fm4_road_amount(fm4_road_texels[0].w, fm4_road_texels[1].y, XE_C(223)));
}

void fm4_road_5e33(inout XePixel p, XePixel input) {
  FM4_ROAD_LIT_OUTPUT(p, input, fm4_road_5e33_weight());
  if (FM4_LOOK_ROAD_DEBUG > 0.0 && FM4_LOOK_ROAD_DEBUG < 6.0) {
    fm4_road_5e33_debug(p, input);
  }
}

#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) FM4_ROAD_ASPHALT_FETCH(0, fetch, value, coord)
#undef XE_HOOK_CONSTANT
#define XE_HOOK_CONSTANT(n, value) fm4_road_constant(n, value)
#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) fm4_road_5e33(p, input)
