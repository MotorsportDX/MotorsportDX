// The lit road of Top Gear and Crown Jewel (ROAD_BLND_DIFF_SPEC_3 and _TRILIN_3). The
// asphalt texture (RoadSampler) in fetch 0, the bump blend in fetch 1 (fetched twice), the
// skids in fetch 2. docs/analysis/look-mod-coverage.md. Lit by the lightmap (fetch 7) and the
// grayscale shadow map (fetch 11) like the road. See fm4-road.hlsli.
// The shader adds c1 to the asphalt texel for its color.
#include "fm4-road.hlsli"

// The game's specular amount of this shader, from RoadSpecularParams1 (c223): X to Y by the
// asphalt texel's alpha times the W of the first fetch 1 texel and the Y of the second, plus
// W to Z by the Y of the skids (fetch 2). The sheen's weight follows it, as on ROAD_BLND_2.
float fm4_road_e2de_weight() {
  float mask = fm4_road_texels[0].w * fm4_road_previous[1].w * fm4_road_texels[1].y;
  return fm4_road_weight(fm4_road_amount(mask, fm4_road_texels[2].y, XE_C(223)));
}

// The darkening of the asphalt by the skid marks: the shader scales its color from 1 to the
// Y of the second fetch 1 texel by the W of the skids.
float fm4_road_e2de_albedo() {
  return lerp(1.0, fm4_road_texels[1].y, fm4_road_texels[2].w);
}

#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) \
  FM4_ROAD_ASPHALT_OFFSET_FETCH(0, XE_C(1).xyz, fetch, value, coord)
#undef XE_HOOK_CONSTANT
#define XE_HOOK_CONSTANT(n, value) fm4_road_constant(n, value)
#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) \
  FM4_ROAD_LIT_OUTPUT_ALBEDO(p, input, fm4_road_e2de_weight(), fm4_road_e2de_albedo())
