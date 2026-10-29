// Aerial haze (the look's haze knob; HAZE above 0): how much of each scene pixel's light the
// air between it and the camera replaces with its own in-scattered light, from the view
// distance d: 1 - exp(-max(d - HAZE_START, 0) / HAZE_DISTANCE), times HAZE; the sky takes
// HAZE_SKY times HAZE. The final combine mixes the scene toward the haze's color by this amount
// (fm4_haze in work/fm4-rex/look/fm4-look.hlsli). The cockpit interior's mask reads as the
// near plane and gets none. Original code of this project.

Texture2D<float> haze_depth : register(t0);
RWTexture2D<unorm float> haze_out : register(u0);

[numthreads(8, 8, 1)]
void haze_amount(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= xe_pass_size)) {
    return;
  }
  float w = xe_scene_w(haze_depth[id.xy]);
  float amount;
  if (xe_scene_is_sky(w)) {
    amount = HAZE_SKY;
  } else {
    float distance = length(xe_scene_view_position(float2(id.xy) + 0.5, w));
    amount = 1.0 - exp(-max(distance - HAZE_START, 0.0) / max(HAZE_DISTANCE, 1.0));
  }
  haze_out[id.xy] = saturate(amount * HAZE);
}
