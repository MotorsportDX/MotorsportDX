// Auto exposure (tonemapAdaptiveLuminanceKeys). Original code of this project: it changes one
// constant of the game's shader.
//   FM4_LOOK_ADAPTATION  0, 1  1: the exposure adapts as fast as the game's at 60 frames a
//                              second whatever the frame rate (0: the title's, per frame)
//
// The shader moves the adapted luminance (fetch 1) towards the frame's (fetch 0) by the share
// c2.x of the difference in each frame: 0.045 at every frame rate
// (docs/analysis/cockpit-brightness.md). The title presents one frame per 60 Hz vblank in
// races and one per two in its 30 fps scenarios; the launcher raises the guest vblank rate
// for faster races (FM4_FRAME_RATE, and with FM4_FULL_RATE the countdown and the replays), so
// the exposure there settles as many times faster as frames come per second more. The hook
// gives c2.x the share that covers the same part of the difference in the frame's time at the
// game's speed: 1 - (1 - c2.x)^(design rate x frame interval), with the title's design frame
// rate for the scenario (60, or 30) and its measured frame interval, so a race below its
// target rate keeps the game's speed too. Without those (a runtime or title integration that
// does not report them) the share is for one frame at the vblank rate,
// 1 - (1 - c2.x)^(60 / rate). At a 60 Hz vblank, and in contracts without the rate, c2 stays
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
      // How many of the game's frames this frame stands for.
      float frames = 60.0 / rate;
#ifdef XE_SYS_GUEST_FRAME_INTERVAL
      float interval = xe_guest_frame_interval();
      float design = xe_guest_design_frame_rate();
      if (interval > 0.0 && design > 0.0) {
        frames = design * interval;
      }
#endif
      value.x = 1.0 - pow(saturate(1.0 - value.x), frames);
    }
  }
#endif
  return value;
}

#undef XE_HOOK_CONSTANT
#define XE_HOOK_CONSTANT(n, value) fm4_adaptation_constant(n, value)
