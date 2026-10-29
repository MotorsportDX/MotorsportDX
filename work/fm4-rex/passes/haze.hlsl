// Aerial haze (the look's haze knob; HAZE above 0): how much of each scene pixel's light the
// air between it and the camera replaces with its own in-scattered light, from the view
// distance d: 1 - exp(-max(d - HAZE_START, 0) / HAZE_DISTANCE), times HAZE; the sky takes
// HAZE_SKY times HAZE. The final combine mixes the scene toward the haze's color by this amount
// (fm4_haze in work/fm4-rex/look/fm4-look.hlsli). The second channel is the near field's weight,
// exp(-max(d - HAZE_START, 0) / HAZE_DISTANCE), 0 on the sky: the look's white balance warms the
// scene by it before the haze mix, so the distance and the sky keep the haze's pale blue-gray.
// The cockpit interior's mask reads as the near plane and gets no haze. Original code of this
// project.

Texture2D<float> haze_depth : register(t0);
RWTexture2D<unorm float2> haze_out : register(u0);

[numthreads(8, 8, 1)]
void haze_amount(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= xe_pass_size)) {
    return;
  }
  float w = xe_scene_w(haze_depth[id.xy]);
  float amount, near;
  if (xe_scene_is_sky(w)) {
    amount = HAZE_SKY;
    near = 0.0;
  } else {
    float distance = length(xe_scene_view_position(float2(id.xy) + 0.5, w));
    near = exp(-max(distance - HAZE_START, 0.0) / max(HAZE_DISTANCE, 1.0));
    amount = 1.0 - near;
  }
  haze_out[id.xy] = float2(saturate(amount * HAZE), near);
}
