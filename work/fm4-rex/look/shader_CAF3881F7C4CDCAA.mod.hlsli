// Auto exposure (tonemapAdaptiveLuminanceKeys). Original code of this project: it changes one
// constant of the game's shader.
//   FM4_LOOK_ADAPTATION  0, 1  1: the exposure adapts as fast as the game's at 60 frames a
//                              second at any guest vblank rate (0: the title's, per frame)
//
// The shader moves the adapted luminance (fetch 1) towards the frame's (fetch 0) by the share
// c2.x of the difference in each frame: 0.045 at every frame rate
// (docs/analysis/cockpit-brightness.md). The title presents one frame per 60 Hz vblank in
// races and one per two in its 30 fps scenarios; the launcher raises the guest vblank rate
// for faster races (FM4_FRAME_RATE, and with FM4_FULL_RATE the countdown and the replays), so
// the exposure there settles rate / 60 times as fast. The hook gives c2.x the share that
// covers the same part of the difference in a 60 Hz vblank's time: 1 - (1 - c2.x)^(60 / rate)
// with the runtime's guest vblank rate. At 60 Hz, and in contracts without the rate, c2 stays
// as it is. It does not include fm4-look.hlsli, whose fetch 0 helpers need a point sampler
// this shader does not bind.

#ifndef FM4_LOOK_ADAPTATION
#define FM4_LOOK_ADAPTATION 0.0
#endif

float4 fm4_adaptation_constant(uint n, float4 value) {
#ifdef XE_SYS_GUEST_VBLANK_RATE
  if (n == 2u && FM4_LOOK_ADAPTATION > 0.0) {
    float rate = xe_guest_vblank_rate();
    if (rate > 0.0 && rate != 60.0) {
      value.x = 1.0 - pow(saturate(1.0 - value.x), 60.0 / rate);
    }
  }
#endif
  return value;
}

#undef XE_HOOK_CONSTANT
#define XE_HOOK_CONSTANT(n, value) fm4_adaptation_constant(n, value)
