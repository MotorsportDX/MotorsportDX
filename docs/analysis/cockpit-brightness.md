# Cockpit Interior Brightness (19 October 2026)

In the cockpit camera on the Bernese Alps, the dashboard, the steering wheel and the driver's hands are very dark, the dashboard and the headliner almost black. This note records where that comes from and what a knob could change.

## Verdict

- **Game behavior, not an emulation bug of the RTV path and not a look mod.** `-Look original` on RTV and `-Rov` give the same interior within a few levels of 255 at the same track points, so the RTV path is not the cause. The `pc` look makes the interior darker still, by its ACES tone curve alone (15 to 35% in the patches above 1 of 255); the AO does not reach the cockpit.
- **The cause is the title's lighting model for the interior.** The interior shaders light a pixel with the sun times the cockpit's own variance shadow map plus a small baked ambient term. In the sun, the car's roof and pillars shade most of the interior, and the game's floor for that shadow (`MinCockpitShadow`) is 0, so the shaded interior keeps only the ambient term. The auto exposure follows the bright snow and sky through the windows, so that term lands near black.
- **Not a transient.** The interior is equally dark in the first capture after switching to the cockpit camera and stays so; in a tunnel the exposure rises and the interior becomes readable.
- Not compared with the original Xbox. The stages that both render paths share (the shader translation, the vertex fetch of the baked ambient, the shadow map) are not proven against hardware by the RTV and ROV comparison; the shadow map's content looks correct (below).

## Setup

BuildRoot `I:\FM4-build-usa` (USA discs, game build of 19 October 2026), `e2e.py` at 2x (`draw_resolution_scale_x=2`, `draw_resolution_scale_y=2`, 2560x1440) with `--keep-ppm`, 60 Hz, quality `high`, `--profile copy`, the title's AI driver (`engageaionstart`). Quick Race with the default car on the Bernese Alps Festival Circuit; the profile starts races in the cockpit camera. Nine freezes per run with rewind (`Y`), about 25 s apart, then resume (`A`). Runs of the same steps put the car at nearly the same track points, so freezes with the same number are compared across runs; the frozen frame within one run is exact.

| Run | Settings |
|---|---|
| `cockpit-pc-ab` | `-Look pc` with `--shader-dev`; in each frozen frame, `defines:` steps switch between `pc`, the mods at their neutral values (tone curve 0, saturation 1, bloom 1, clear coat 0, AO 0, TAA off), `pc` without ACES, and `pc` without AO |
| `cockpit-orig` | `-Look original`, RTV |
| `cockpit-rov` | `-Look original`, `render_target_path_d3d12=rov`, `rov_occlusion_async=all`, `edram_single_rt_no_wrap=false` (as `-Rov`) |
| `cockpit-dbg` | `-Look original` plus temporary diagnostic mods on the 27 interior pixel shaders that read the cockpit shadow map (`s_varianceShadowMap`, fetch 13) and the track lightmap (fetch 10), switched live |
| `cockpit-fill`, `cockpit-floor` | `-Look original` plus temporary prototype mods (below) |
| `cockpit-exp2` | `-Look original` with `FM4_TRACK_SETTINGS` adding 2 to the four `Exp_*` values of `ToneMapCockpitExposure` |

The temporary mods were written into the BuildRoot (`look-cockpit-dbg`, `look-cockpit-floor`), not into the repository.

Patches, in 2560x1440 pixels (x, y, width, height); the value is the mean Rec. 709 luma of the 8-bit capture:

| Patch | Rectangle |
|---|---|
| dashboard top | 1040, 980, 360, 60 |
| gauges | 1060, 1080, 420, 120 |
| wheel hub | 1160, 1220, 200, 80 |
| right hand | 1600, 1080, 160, 120 |
| road (outside) | 900, 800, 300, 80 |
| sky (outside) | 840, 380, 200, 80 |

The head moves a little with the car, so a patch covers slightly different parts from freeze to freeze; within a frozen frame it is exact.

## Results

### The look mods (same frozen frame)

| Freeze | Variant | Dashboard | Gauges | Wheel hub | Hand | Sky |
|---|---|---|---|---|---|---|
| Grid, interior in the roof's shadow | `pc` | 0.2 | 1.2 | 16.6 | 6.9 | 195.8 |
| | mods neutral | 0.7 | 1.8 | 20.1 | 9.9 | 211.6 |
| | `pc` without ACES | 0.7 | 1.9 | 20.4 | 10.0 | 211.5 |
| | `pc` without AO | 0.2 | 1.2 | 16.6 | 6.9 | 196.2 |
| Sun through the windshield on the wheel | `pc` | 1.0 | 3.8 | 78.7 | 6.1 | 182.3 |
| | mods neutral | 2.0 | 5.7 | 80.2 | 9.3 | 195.9 |
| Rock wall shade | `pc` | 0.6 | 4.1 | 67.3 | 26.3 | 119.0 |
| | mods neutral | 1.4 | 4.8 | 68.2 | 29.7 | 122.7 |

The ACES curve (`tonemap=aces`) accounts for the whole difference: it keeps middle gray and darkens the toe. Offline, with the Bernese Alps `filmicTone`, an output the game's curve puts at 18 of 255 comes out at 13, 26 at 21, 36 at 32; from 72 up the two match. Saturation, bloom and TAA change the interior by less than 0.5. AO changes nothing: the interior has no AO ([GTAO design](gtao-design.md)).

### RTV against ROV (`-Look original`, same freeze number)

| Freeze | Path | Dashboard | Gauges | Wheel hub | Hand |
|---|---|---|---|---|---|
| 1, grid | RTV | 0.8 | 1.8 | 19.5 | 9.3 |
| | ROV | 0.7 | 1.7 | 19.4 | 10.1 |
| 5, sun on the wheel | RTV | 27.9 | 4.0 | 113.7 | 10.0 |
| | ROV | 27.3 | 4.3 | 114.3 | 11.2 |
| 6, tunnel entrance in rock shade | RTV | 1.3 | 5.7 | 69.2 | 36.4 |
| | ROV | 1.2 | 5.7 | 68.0 | 35.5 |

The 1:1 crops of the interior at freeze 6 show the same image on both paths: gray gloves, readable gauges, a dark dashboard. Freezes where the AI driver's line differed between the runs differ by the scene, not by the path.

### Over the lap and after a camera switch

- On the open sunny parts of the lap, with the sun ahead or behind, the dashboard top stays at 0.1 to 3 and the gauges at 1 to 6 in every run, the wheel hub at 15 to 40 and the hand patch mostly at 6 to 16. The dashboard reaches 25 to 28 and the wheel hub 79 to 125 only where the sun falls on them through the windshield.
- In the rock cutting and the dark tunnel near the end of the lap (sky 15 to 20) the exposure rises: the hand patch reaches 33 to 36, the wheel hub 59 to 70 and the gauges 5 to 6, and the interior becomes readable.
- After a switch away from the cockpit (hood, bumper, both chase cameras) and back (`-Look pc`), the first capture (within 2 s) has the dashboard at 0.5 and the gauges at 1.2, the same as before the switch, and the first capture of the race after the start lights has 0.1 and 1.2. No adaptation transient shows; with captures every 2 s, one shorter than that would not show either.

### Why: the interior shader

The interior pixel shaders (for example `7DB215140DF8CBC6`, the cloth, read from its ucode) compute

- a shadow term: the mean of four taps of the cockpit's variance shadow map (fetch 13), the Chebyshev bound of the pixel's depth, raised to `ShadowDarkeningRamp`, limited by the track lightmap under the car (fetch 10, between `trackLightMapParams` 0.4 and 0.75), and at least `MinCockpitShadow`;
- the color: the albedo times (sun color times `shLightScalars.x` times N·L times the shadow term, plus the interpolated ambient times `shLightScalars.y`), then fog and gamma 2.

The interpolated ambient comes from baked per-vertex data of the interior (a signed 16_16_16_16 vertex element through three constant rows of the vertex shader `4EE552137CA415E3`), not from the sky.

Constants of `7DB215140DF8CBC6` on the Bernese Alps (`REX_DRAW_LIST_CONSTANTS`, `cockpit-exp2`): sun color 0.65 (gray), `shLightScalars` (5, 8), `ShadowDarkeningRamp` 4, `MinCockpitShadow` 0, `trackLightMapParams` (0.4, 0.75), sun direction (-0.10, 0.49, -0.87), about 29 degrees above the horizon.

Diagnostic mods in one frozen frame (freeze 1, grid):

| Variant | Dashboard | Gauges | Wheel hub | Hand |
|---|---|---|---|---|
| Game | 0.7 | 1.8 | 19.3 | 9.9 |
| Shadow map forced lit (fetch 13 returns moments far behind the pixel) | 3.7 | 66.6 | 56.0 | 38.9 |
| Track lightmap forced to 1 | 0.7 | 1.8 | 19.3 | 9.9 |
| Ambient term times 2, 4, 8 (`shLightScalars.y`; `cockpit-fill`, freeze 1) | 1.8, 3.9, 8.7 | 3.7, 7.2, 13.1 | 19.7 to 20.2 | 10.0 to 10.4 |

So the shadow map shades the interior, the track lightmap does not, and the ambient term is small: eight times the game's value lifts the gauges only to 13. The gloves are drawn by the driver shader `30712480E0AB46E4`, which reads the same shadow map but had no diagnostic mod: they stay dark in every variant, and the hand patch rose with the forced-lit shadow map only through the dashboard behind the glove. A view of the shadow map's mean moments in the same frame is smooth, lies between 0 and 1, has the second moment close to the square of the first, and shows the outline of the roof and the A-pillars on the dashboard: a working shadow map, not cleared or corrupt memory.

The cockpit has its own auto exposure curve, `ToneMapCockpitExposure` in `TrackSettings.xml` (see [visual style](visual-style.md#assets-tracksettingsxml-per-track)). On the Bernese Alps it adds 1.737 stops at a scene key of 0, falling to 0 at a key of 9, and 0 above that; the outside cameras get 1.705 stops at a key of 2, 0.001 at 18 and -0.576 at 70. Among the 24 race locations, the Bernese Alps have the lowest cockpit keys (`Key_Light` 17 and `Key_ExtraLight` 20, against 26 to 70 and 35 to 82 on the others) and the only cockpit curve without a negative value at the light end (the others take 0.5 to 1.2 stops off there). The cockpit curve of this track is therefore not set darker than those of the other tracks: the darkness comes from the ratio between the shaded interior and the sunlit snow and sky.

## Knob candidates

Measured on `-Look original`, RTV:

| Candidate | Freeze 1 (grid) dashboard, gauges, wheel hub, hand | Outside | Notes |
|---|---|---|---|
| none | 0.8, 1.8, 19.5, 9.3 | sky 224, road 50 | |
| cockpit exposure +2 stops (`FM4_TRACK_SETTINGS`, `ToneMapCockpitExposure.Exp_*+=2`) | 4.1, 7.5, 73.2, 44.9 | sky 242 to 255, road 161 | The interior is readable (white gloves, gray headliner), but the windows blow out to white with a wide bloom halo on every sunny freeze. In the tunnel the hands reach 106. |
| ambient times 8 (cockpit shaders) | 8.7, 13.1, 20.2, 10.4 | unchanged | Too weak, and the driver shader has no such term. |
| shadow floor: `MinCockpitShadow` at least 0.15, 0.3, 0.5 (`cockpit-floor`, the 21 interior shaders that have it) | dashboard 0.7 to 1.1, 1.5, 2.0; gauges 1.8 to 17.7, 29.6, 41.5; wheel hub 19.7 to 22.5; hand unchanged | unchanged | The gauge faces become readable; at freeze 2 the gauges go from 2.1 to 23.1, 37.4, 51.8. The dashboard plastic, the wheel rim and the gloves stay dark. The gloves are the driver shader, which the prototype did not cover; which shader draws the dashboard top and the rim was not traced (candidates are the `MinShadowValue` variants, of which `68BAB030C58A2A06` and `9202A9306737F867` drew in the atlas sample, or a face turned from the sun, which a floor through N·L cannot light). Where the interior was already lit or in a tunnel, nothing changes. |

Proposal: a look knob for the interior's shadow floor, for example `cockpit_light`, rather than a cockpit exposure knob. It is built: see [the `cockpit_light` knob](#the-cockpit_light-knob-19-october-2026).

- It lifts only the shaded interior and leaves the windows, the exposure and the bloom as they are, which an exposure knob cannot do: the auto exposure is one value for the whole frame (interpolator 1 W of the final combine, [B4 research](b4-shader-research.md#finalcombine-formula)), so every stop for the interior is also a stop for the sunlit snow.
- It needs every interior pixel shader: the shaders with `MinCockpitShadow` (21 in the material atlas), those with `MinShadowValue` (17 more, two of which drew in the atlas sample), and the driver shader `30712480E0AB46E4`, whose shadow term has no floor constant and needs its result limited from below in the mod. Hashes of the 16-car, split-screen and UI shader sets come with them.
- The floor lights the interior from the sun's direction (N·L), not from the sky, so faces turned away from the sun stay dark. A floor of about 0.3 made the gauges readable in the A/B without changing the exterior; the value for a preset needs the crops of the whole interior with every shader covered.
- RTV only, like the other look mods; `-Rov` and `-Look original` keep the game's interior.
- A cockpit exposure knob (`ToneMapCockpitExposure`, a track lighting knob through `FM4_TRACK_SETTINGS` like `road_gloss` and `sky`) needs no shader work, but at +2 stops it blows out every sunny window. A small value (0.5 to 1 stop) could go with the floor; it was not measured.

## The `cockpit_light` knob (19 October 2026)

`-Look "...,cockpit_light=<0 to 1>"` raises the shadow floor of the interior and the driver ([`fm4-cockpit.hlsli`](../../work/fm4-rex/look/fm4-cockpit.hlsli); [running](../running.md#look)). The default 0 is the game's interior; `pc` set 0.5 from 20 to 26 October 2026 ([on top of pc](#in-the-pc-preset-20-october-2026)), and no preset sets it since ([the sky fill](#the-sky-fill-in-the-presets-26-october-2026)).

- **Interior shaders with a floor constant** (38 hashes, the material atlas class `cockpit`): 21 with `MinCockpitShadow` and 17 with `MinShadowValue`, of the race, 16-car, split-screen, UI and `shaders_slod_v16` sets and the `cockpit_*` material shaders. Each ucode reads the constant's X in a `max` with the shadow term (after the track lightmap's `min`); the mod's `XE_HOOK_CONSTANT` makes that X at least the knob. Five of the material shaders are shared with the engine bay (`engine_*` sources), which the floor lifts as well.
- **The driver** (six hashes without a floor constant): the body `30712480E0AB46E4` (default.xex) and the suit `89F06E34104DF40C` (WhiteFelt), `85CB5573E78162CB` (ApendagesSuede), `1891C4772E7E7617` (Sleeves), `A4ECA613BEDB4468` (Stitching) and `C5CF0F92A4F120F2` (logos). Their shadow term is `min` of the track lightmap's share, `saturate((L * L - c60.x) / (c60.y - c60.x))` (fetch 1 for the body, 10 for the suit), and one tap of the variance shadow map darkened to p^4 (the ucode squares the Chebyshev bound twice); the suit then takes a small literal floor. The mod raises the lightmap texel and the second moment of the shadow map texel (with the pixel's depth from the interpolator of the fetch's coordinate) so that each term is at least the knob, which equals a floor after the `min`.
- RTV only (`AGENTS.md`); `-Rov` and `-Look original` keep the game's interior.

The gloves are the suit shaders, not `30712480E0AB46E4`: a bit-coded trace (run `cockpit-trace3`: 52 car, cockpit and driver hashes of a cockpit draw list, each writing the bits of its index as white or black in seven frozen steps, decoded per pixel) shows WhiteFelt and ApendagesSuede on the gloves, `327942867CAF7123` on the dashboard top and the gauge hood, `C7B414C46424E22F` and `327942867CAF7123` on the wheel rim, and `D5D4B516629428F6` on the cluster face; the body shader covers only a few hundred pixels at 1/4 sampling. A first trace with every candidate written at 50 turned the frame white through a full-screen shader of default.xex and the bloom; the decoded one writes 1.

### A/B in the game

`I:\FM4-build-pass` built from main `372ef6b` plus the knob (the runtime is unchanged since), with the USA discs (`game_data_root`) and the `I:\FM4-build-usa` profile, 2x, `-Look original` plus the mods of the knob alone (`shader_mod_path` with the 39 hashes of the first version, without the suit), `--shader-dev` style live knobs, the AI driver. Eight freezes in the cockpit camera, then one in each of the hood, chase near and chase far cameras; in each, `defines:` steps 0, 0.15, 0.3, 0.5 and 0 again (run `cockpit-light`). Patches as above; the hand patch holds the right glove where it is in view (freezes 3, 5 and 8 in the crops), and in freeze 1 the console beside the cluster.

| Freeze | Dashboard (0 / 0.15 / 0.3 / 0.5) | Gauges | Wheel hub | Hand |
|---|---|---|---|---|
| 1, grid, sun behind the car | 0.8 / 1.1 / 1.5 / 1.9 | 1.8 / 18.0 / 30.0 / 42.1 | 19.8 / 19.9 / 20.8 / 22.4 | 9.5 / 10.9 / 14.6 / 20.2 |
| 2 | 2.7 / 2.8 / 2.9 / 3.1 | 3.9 / 15.4 / 24.3 / 33.4 | 18.2 / 18.6 / 19.9 / 22.0 | 9.6 / 11.0 / 14.8 / 20.5 |
| 3, sun on the wheel | 2.0 / 3.0 / 3.9 / 5.1 | 5.3 / 29.3 / 44.4 / 59.7 | 72.0 / 72.2 / 73.2 / 74.8 | 9.0 / 9.2 / 9.5 / 9.8 |
| 4 | 12.8 / 12.9 / 13.0 / 13.1 | 1.9 / 13.2 / 21.9 / 31.6 | 36.9 / 37.0 / 37.2 / 37.3 | 17.4 / 17.5 / 17.6 / 17.6 |
| 5, sun ahead | 4.8 / 4.8 / 4.9 / 4.9 | 1.7 / 1.8 / 1.9 / 2.0 | 31.9 / 31.9 / 32.3 / 33.0 | 16.5 / 16.5 / 16.5 / 16.5 |
| 6 | 5.3 / 5.3 / 5.9 / 6.9 | 6.2 / 6.3 / 6.3 / 6.5 | 32.8 / 32.9 / 33.1 / 33.3 | 17.4 / 17.4 / 17.4 / 17.4 |
| 7 | 1.2 / 1.2 / 1.2 / 1.2 | 2.2 / 2.3 / 2.3 / 2.4 | 22.5 / 22.5 / 22.6 / 23.0 | 15.3 / 15.3 / 15.3 / 15.3 |
| 8 | 1.1 / 1.1 / 1.1 / 1.1 | 1.9 / 2.0 / 2.1 / 2.2 | 20.3 / 20.3 / 20.4 / 20.7 | 39.2 / 39.2 / 39.2 / 39.2 |

- **The windows and the exposure do not change.** The road and sky patches are identical at every value in every cockpit freeze, and the step back to 0 restores each frame. In the hood camera no pixel changes at 0.5; in chase near 2,331 pixels (0.06%) and in chase far 1,323 (0.04%) change, all of them the driver and the headrest seen through the rear window. No artifacts in the 1:1 crops.
- **What it lifts:** with the sun behind or beside the car (freezes 1 to 4) the gauge faces go from 2 to 4 of 255 to 32 to 60 at 0.5, the cluster bezel, the console and the headliner's grab handle become visible, and the console beside the cluster doubles (9.5 to 20.2). At 0.3 the gauges are readable (22 to 44).
- **What it cannot lift:** with the sun ahead (freezes 5, 7, 8) the faces toward the driver have no N·L, and nothing changes by more than 1.1. The dashboard top and the wheel rim stay near black at every value even though their shaders are covered: their albedo is black plastic and rubber, which in direct sun reaches only 25 to 28 (see [over the lap](#over-the-lap-and-after-a-camera-switch)), and half of that light lands in the game's tone curve toe. The floor cannot brighten them further without lighting from the sky, which these shaders do not have.

The gloves stayed as they were in this run (hand patch unchanged in freezes 4 to 8), which led to the trace above. Run `cockpit-light2` repeats the A/B with all 44 hashes (the same setup; six cockpit freezes, then chase near with 0, 0.5, 0):

| Freeze | Dashboard (0 / 0.15 / 0.3 / 0.5) | Gauges | Wheel hub | Hand |
|---|---|---|---|---|
| 1, grid | 0.7 / 1.2 / 1.6 / 2.1 | 1.8 / 17.7 / 29.5 / 41.4 | 19.2 / 19.4 / 20.2 / 21.7 | 9.9 / 15.0 / 28.7 / 44.6 |
| 2, sun on the wheel | 22.3 / 22.4 / 22.5 / 22.7 | 2.3 / 21.2 / 34.8 / 48.2 | 74.9 / 75.1 / 75.2 / 75.5 | 5.8 / 8.9 / 18.3 / 30.3 |
| 3 | 0.9 / 1.0 / 1.1 / 1.2 | 2.1 / 2.2 / 2.3 / 2.5 | 27.6 / 27.6 / 29.2 / 31.6 | 3.7 / 5.0 / 8.9 / 13.8 |
| 4 | 10.0 / 10.0 / 10.0 / 10.0 | 2.3 / 2.4 / 2.4 / 2.5 | 32.9 / 32.9 / 33.3 / 34.0 | 12.9 / 12.9 / 12.9 / 13.0 |
| 5 and 6 (the car stood at the same point) | 3.1 at every value | 11.5 to 12.2 at every value | 30.1 / 30.1 / 30.2 / 30.5 | 10.5 to 10.7 at every value |

- In freeze 1 the right glove goes from a dark shape to a white glove with its red logo (9.9 to 44.6 at 0.5, 28.7 at 0.3), and the left glove's palm and the sleeve cuffs show; the shading of the folds stays. The rise is smooth over the four values, as the p^4 ramp of the suit's shadow term predicts. With the sun ahead (freezes 4 to 6) the gloves change by at most 0.2.
- The road and sky patches change by at most 0.3 in the cockpit freezes (freeze 3, likely the bloom of the brighter interior; the step back to 0 restores each frame), and in chase near 1,276 pixels (0.04%) change, the driver seen through the rear window. No artifacts in the 1:1 crops: no seams at the shadow edges, no light leaking onto the wheel rim or the windows.

Crops (1:1 at 2560x1440, not in the repository) are in `I:\FM4-build-pass\e2e\cockpit-light-report`.

## In the pc Preset (20 October 2026)

`pc` set `cockpit_light=0.5` and `windshield=1` from 20 October 2026, at the project owner's request; since 26 October 2026 the presets set `cockpit_fill=1` instead of the floor ([the sky fill](#the-sky-fill-in-the-presets-26-october-2026)). Run `pc2-cockpit-ab` (`I:\FM4-build-pass`, the USA Disc 1 and profile, 2x, `-Look pc` with `--shader-dev`, nine cockpit freezes, live defines: the new `pc`, the earlier one, each knob alone):

| Freeze | Earlier `pc`: gauges, hand, road | New `pc`: gauges, hand, road |
|---|---|---|
| 1, grid | 1.2, 6.9, 75.1 | 39.0, 43.3, 72.4 |
| 2 | 3.6, 2.0, 112.2 | 58.0, 6.9, 109.7 |
| 8 | 1.2, 8.0, 12.8 | 37.5, 17.2, 12.3 |
| 9 | 3.4, 7.1, 80.0 | 61.8, 47.8, 76.1 |
| 3 to 7 | 1.2 to 3.0, 14 to 62 | each value changes by less than 4 |

- `cockpit_light` makes every change inside the car; the glass alone lowers the patches seen through the windows by 1 to 5 and leaves the interior as it is.
- In freezes 3 to 7 the gauges and the gloves face away from the sun, so the floor (through N·L) lifts them little.
- The 1:1 crops show readable gauges and white gloves, a light haze on the windows, and no artifacts.
- The smoke gate of the new `pc` against the earlier one is in [current status](../status/history.md#20-october-2026-pc-with-the-cockpit-knobs).

## Windshield and Side Windows

In the cockpit camera the windshield and the side windows look like open frames: no tint, reflection or glare on the view outside. The game does not draw them in that camera.

- Run `cockpit-glass` (same setup, `-Look original`, five freezes) with `REX_DRAW_LIST_TRACE` every 1,200 frames and a temporary mod on the car glass shader `E62CFDF09D758B6C` (live: glass hidden, opaque magenta coverage, opacity as gray, opacity times 4).
- The player's car is the stencil reference of the interior draws (`4EE552137CA415E3`). In the 3-2-1 countdown camera, its draws include the body (`8D05A46C3584CA9E`), chrome (`A42BEA3C0A6E5603`), the lamps (`AD10B610621DEFE5`) and the windows: `D7B380C7A7F72EF5` with `E62CFDF09D758B6C`, two meshes of 883 and 910 indices, each drawn twice (`reflectionScaler` 0, then 1), with alpha blending, `GlassColor` (0.01, 0.01, 0.01) at alpha 0.3 and `GlassMaxOpacity` 1.
- In every traced frame of the race in the cockpit camera (14 frames), the player's car draws none of these. The only glass left is a mesh of 17 indices with `GlassColor` alpha 0, the glass over the gauges. The draw list is the guest's command stream, so the title itself issues no window draws in this camera; the render path cannot change that, and the ROV captures show the same open windows.
- The magenta coverage shows the windows of the other cars, the reflection of them in the mirrors and the gauge glass, and nothing on the player's windshield or side windows. Hiding the glass changes no pixel outside the gauges, and the gauge glass is fully transparent (opacity view 0.1 of 255).
- The executable has a title command line option `hidecockpitglass` (in the table with `engageaionstart`, read through `FM4_GAME_CMDLINE`) and a windshield reflection feature (`WindshieldReflection`, `ICarPresentationSetWindshieldReflectionTexture`, the pixel shader `7226D929481D4274` with `WindshieldReflectionScale`), whose shader is only in `shaders_slod_v16`, the car set of `UI_Showroom`. Neither was tested; the option most likely hides the gauge glass, the only cockpit glass the race draws.

Verdict: game behavior, the same on both render paths. A visible windshield would be new work: a post pass in the cockpit camera that tints the world seen through the windows (the pixels that are not the interior: the player's stencil reference) and adds a faint reflection or glare, or a hook that makes the title draw its exterior glass in this camera, whose shader is made for views from outside. The post pass is the look knob `windshield`, 0 by default and 1 in `pc` since 20 October 2026 ([windshield](windshield.md)); the hook is not built.

## Swings between bright and dark (25 October 2026)

The user saw the interior in the cockpit camera turn from very bright to very dark from one frame to the next, on the main PC and the ROG Xbox Ally X, seemingly with the sun's position. Three emulation suspects were checked: the luminance resolve read back for the auto exposure, the sun's occlusion queries, and the frame rate.

**The auto exposure runs on the GPU only.** Its pixel shader (`CAF3881F7C4CDCAA`) samples the luminance (fetch 0) and the previous adapted value (fetch 1) and writes `previous + (current - previous) * c2.x`; the final combine reads the result (interpolator 1 W). No guest or host thread reads a resolve readback in the races ([d3d-hle-n3.md 16.4](d3d-hle-n3.md#164-who-reads-the-readbacks-22-october-2026), whose runs started in the cockpit camera), so the readback mode (`fast`, `full`, or on demand under `-Native`) only changes the copy in guest memory, which nothing reads; the GPU reads the resolved data directly. `c2.x`, the share of the new value in each frame, is 0.045 in every frame at 60 fps (`n4ae-ae60`, 1,357 race frames) and after the race at 120 and 240 fps (the post pass log did not reach the shader's race draws there, although it runs in every race frame at both rates: `n4dl-60`, `n4dl-240`). If the race also keeps 0.045, the exposure adapts per frame and so four times as fast at 240 fps as at 60 (a time constant of about 0.09 s against 0.36 s); that is not confirmed for the race.

**Measured over a lap** (`I:\FM4-build-usa`, the build of `02ff637` with the capture statistics below, 1x, the Bernese Alps Festival Circuit with the AI driver in the cockpit camera, `FM4_FIXED_SEED`, about 110 s; one run a side):
- Every 2 s with `-Look original` (`n4ck-*`): the interior and sky patches follow the same course in `-Rov` with `readback_resolve=full`, RTV with `fast` and with `full`, RTV at 240 fps and `-Native` at 240 fps. The interior brightens and darkens at the same places (the rock cuttings and the tunnel) and by the same amount.
- Nearly every frame with `FM4_CAPTURE_STATS=1` (`n4cs-*`: 6,300 to 6,600 distinct frames at 60 fps, 22,400 at 240): no path jumps from one frame to the next. The largest change of the interior patches between two consecutive frames is 13.1 luma (of 255) for `-Rov` with full readback and `rov_occlusion_async=all`, 10.4 for RTV with `-Look original`, 15.2 for RTV with the default look, 7.8 for `-Native` and 7.1 for RTV at 240 fps; the whole frame changes by at most 4 to 7. Over 500 ms the interior changes by up to 20 to 26 in every path, at the same places.
- The sun query (the awaited occlusion report) saw no sample in the whole lap in any path (8,459 to 9,203 awaited reports a run, `REX_OCCLUSION_SEGMENT_TRACE`): on this circuit and with this car the sun is never in view from the cockpit, so the sun glare could not be tested.

So on this lap the emulation does not add swings: the slow ones are the title's exposure following the windows (above), and no sudden ones occur. The sudden, sun-dependent swings need the place where the user sees them (the track, the time of day, the car, the camera and the point of the lap); the sun glare from the awaited occlusion reports (which washes the screen out at about 4,000 counted samples, [current status](../status/current.md#open-issues)) and the default look's `sun=2` are the next suspects there. The same day the swings were traced to the default look's `cockpit_light`, not to the emulation or the glare: see [the cause](#the-cause-the-cockpit_light-floor-25-october-2026).

**The capture statistics** (`FM4_CAPTURE_STATS=1`, [e2e](../e2e/README.md)): between the captures every 2 s, the guest output is captured as often as it can be and the mean luma of the frame and of six patches is appended to `capture/stats.csv`, so a jump from one frame to the next shows.

## The cause: the cockpit_light floor (25 October 2026)

The project owner reported the swings again: in both cockpit views the interior turns from much too bright to much too dark within a moment, depending on where the sun is. The cause is the default look's `cockpit_light=0.5` alone. The game itself, the emulation, the sun glare, the cockpit glass, the tone curve, the AO and TAA do not make the swing.

**Why the floor swings.** The floor raises the interior's shadow term to at least 0.5 (the [knob](#the-cockpit_light-knob-19-october-2026)), so the interior takes half of the sun's direct light, times N·L, where the car's own roof and pillars shade it. That light follows the sun's direction relative to the car: with the sun behind or beside the car the gauges and gloves face it and light up; with the sun ahead they face away and keep nothing. In a corner the car turns through that range within one to three seconds. The floor also sits after the `min` with the track lightmap, so it lights the interior where the world is shaded: in the tunnel and the rock cuttings, where the auto exposure rises for the dark windows, the interior glows. The game draws the shaded interior with its small ambient term only, near black and steady.

**Over a lap** (`I:\FM4-build-usa`, game build of `5c346c9`, 1x, 60 Hz, Quick Race on the Bernese Alps Festival Circuit with the AI driver in the cockpit camera, `FM4_FIXED_SEED=1`, `FM4_CAPTURE_STATS=1`, about 6,600 distinct frames a run; the patches of [the setup](#setup) at 1x): the range of the 1 s means from the start to 125 s, the largest change within 1 s, and the means in the open (sky patch above 100) and in the tunnel (70 to 95 s, sky below 40).

| Run | Look | Gauges: range, within 1 s, open, tunnel | Hand: range, within 1 s, tunnel |
|---|---|---|---|
| `ck-lap-today` | default (`aces`) | 1.2 to 92.6, 32.0, 28.9, 88.8 | 4.0 to 103.4, 81.6, 93.7 |
| `ck-ae240` | default, 240 Hz | 1.3 to 93.4, 35.8, 29.7, 89.4 | 5.7 to 104.8, 80.6, 94.0 |
| `ck-lap-a` | `aces,cockpit_light=0` | 1.0 to 12.8, 16.2, 2.2, 4.8 | 2.0 to 67.6, 52.8, 30.4 |
| `n4cs-rtvo60` | `original` | 1.5 to 12.9, 14.6, 2.9, 5.6 | 2.2 to 66.8, 55.5, 32.8 |
| `n4cs-rov60` | `-Rov`, `original` | 1.6 to 10.9, 9.5, 2.6, 5.3 | 3.7 to 61.9, 51.1, 32.4 |

With `cockpit_light=0` and otherwise the default look, the interior patches follow `-Look original` and `-Rov` over the whole lap: the swing is the knob's. The hand patch's 1 s changes come from the glove moving through the patch as the driver steers (the steps take three to four frames); in the default look the gloves are white, so the patch changes more. The game's own adaptation is slow and the same in every path (see the lap above). At 240 Hz the default look swings as at 60.

**The other knobs, in frozen frames** (`ck-ab1b`: 2x, `--shader-dev`, `--keep-ppm`, four rewind freezes on the same lap, each knob switched live and held 8 s for TAA and the glass to settle; patches at 2560x1440). In each freeze, `windshield=0` changes only the window pixels (the road and sky patches by 0.4 to 14, the interior patches by at most 0.1), `ao=0` changes the interior by at most 0.2, and `tonemap=filmic` in place of ACES brightens it by 1 to 5 (the ACES toe). `cockpit_light` 0.5 against 0: the gauges 39.1 against 1.3, 53.3 against 3.5, 12.4 against 1.2 and 39.3 against 3.0 in the four freezes.

**The sun glare** (`sun=2`). On the Bernese Alps lap the sun is never in view from the cockpit (above). On Road Atlanta (the same car and route, `ck-lap-ra`) it shows at the top of the windshield from 4 s to 18 s of the race. A lap with `sun=0` (`ck-lap-ra-sun0`) gives the same patches within 2 of 255 in every second, also while the sun shows (frame 63.5 against 65.3, sky 216.6 against 218.4): the glare adds a little light to the windows and does not move the exposure. The interior swings there as well, gauges 0.8 to 50.3 in both laps, from the same floor.

**The adaptation at high frame rates.** In a lap at 240 Hz (`ck-ae240b`, `REX_DRAW_LIST_CONSTANTS=CAF3881F7C4CDCAA:2-2`, every 120th frame) the auto exposure's share of the new value, `c2.x`, is 0.045 in all 283 sampled race frames, as at 60 Hz. The title adapts per frame, so at 240 Hz the whole picture's exposure settles four times as fast as the game does at 60 (a time constant of about 0.09 s against 0.36 s), and at 120 Hz twice as fast. That is a change against the game at every frame rate above 60 for every pixel, not a cockpit swing: the lap at 240 Hz has the same interior ranges as at 60 (above). Since 26 October 2026 the look knob `adaptation=1` scales `c2.x` on RTV and the native path (1 - 0.955^(60 x frame interval), with the title's measured frame interval; 60 / vblank rate in its first version) and gives the game's speed; the presets `aces`, `gt` and `neutral` set it ([running](../running.md#exposure-adaptation), [current status](../status/current.md#auto-exposure-adaptation-by-frame-time-26-october-2026)).

## Two fixes (25 October 2026)

**(a) As the game.** The presets stop setting `cockpit_light`. The interior is the game's: steady, and near black on the sunny parts of a lap, the state the knob was made for on 19 October 2026 (gauges 1.0 to 12.8 over the lap, `ck-lap-a` above).

**(b) Natural: the floor becomes a sky fill.** The light the shaded interior lacks is the sky's and the bounce's through the windows, which does not follow the sun's direction. The prototype of 25 October 2026 (live defines with separate strengths for each part, the gain k, the normal weight, the shade g and the driver's floor) keeps the floor at 0 in the interior and instead:

- scales the game's own hemispherical term of the interior shaders (the baked per-vertex ambient times `shLightScalars.y`, `CockpitHemisphericalScalar` of `TrackSettings.xml`; `XE_HOOK_CONSTANT` on c69) by 1 + k × w × s;
- weights it by the sky's share for the pixel's interpolated normal, w = (1 + N.y) / 2 (`XE_HOOK_INPUT`, interpolator 1, world Y up): the dashboard top facing up gets the most, the gauges facing the driver half, the headliner facing down almost none;
- limits it by the track lightmap's share of the sun under the car, as the interior shader computes it (`XE_HOOK_TFETCH` on fetch 10 and c60): s = 1 - g × (1 - share), so in tunnels and in the shade of rock walls and stands a part 1 - g of the fill stays;
- gives the driver's shaders a floor of their own that raises only the car's shadow map term, so the track lightmap's share still limits it.

Measured on the lap above (`ck-lap-*`, 1x) and in the freezes of `ck-hemi2x` (2x, live defines, one freeze with the sun ahead and one with it behind):

| Variant | Gauges over the lap: range, within 1 s, open, tunnel | Freeze, sun ahead / behind: gauges | Notes |
|---|---|---|---|
| today, floor 0.5 | 1.2 to 92.6, 32.0, 28.9, 88.8 | 1.4 / 35.3 | |
| (a) | 1.0 to 12.8, 16.2, 2.2, 4.8 | 1.2 / 1.2 | |
| fill k=8, uniform (w = 1), g = 1, driver 0.25 (`ck-lap-b8`) | 1.9 to 20.8, 19.2, 14.0, 4.8 | 13.1 / 13.1 | the light headliner and pillars turn flat light gray (1:1 crops) |
| fill k=20, w by the normal, g = 1, driver 0.25 (`ck-lap-h20`) | 1.9 to 29.9, 29.4, 20.2, 4.9 | 19.0 / 18.6 | the headliner stays dark; every shadow edge drops the gauges to the game's within a second |
| fill k=20, w by the normal, g = 0.65, driver 0.25 (`ck-lap-h20p`) | 12.0 to 29.7, 17.2, 21.5, 24.6 | (as above in the sun) | the steadiest: the gauges keep 12 to 30 over the whole lap |

In the 1:1 crops at 2x (`I:\FM4-build-usa\e2e\ck-report`, not in the repository) the sky fill shows black gauge faces with readable yellow numbers and the dark red dashboard with the sun ahead, behind and in the rock cutting, where today's floor gives white gauge faces and white gloves with the sun behind and in the tunnel and black gauges with the sun ahead. The driver's floor 0.25 keeps the gloves gray in the shade and the game's in the tunnel. In the tunnel at 2x (laps `ck-lap2x-today`, `-a` and `-bp` with g = 0.65, the capture at 84.7 s to 84.9 s): the gauges 82.1 today, 4.7 with (a) and 21.8 with the sky fill, the hand 98.6, 31.5 and 31.2; with the sky fill the gauges read as in the open and the gloves are the game's. In a darker frozen frame the brighter interior lowers the auto exposure a little (`ck-cut2x`: the road patch from 88.7 to 77.0 at k=16 uniform); with the normal weight at k=20 the frame mean rises by 5 to 6 and the road and sky patches stay as they are (`ck-hemi2x`).

Coverage of (b): the 20 interior shaders that have the track lightmap and `shLightScalars`, the two cockpit chrome shaders (`670B85AE4903DC7E`, `B1C51D0C3686C920`, their ambient is `ChromeAmbientScale`), and the six driver shaders. The two `shaders_slod_v16` shaders and the 14 `cockpit_*` material shaders have no track lightmap constants (and the material shaders an `AmbientColor` instead), so (b) would leave them as the game draws them; none of them drew in the races of the material atlas.

## The sky fill in the presets (26 October 2026)

The main session chose (b) with g = 0.65. Since 26 October 2026 the presets `aces`, `gt` and `neutral` (and `pc`) set `cockpit_fill=1` and no longer `cockpit_light`; `original` keeps the game's interior ([running](../running.md#cockpit-light)).

- **`cockpit_fill`** (`FM4_LOOK_COCKPIT_FILL`, 0 to 2, [`fm4-cockpit.hlsli`](../../work/fm4-rex/look/fm4-cockpit.hlsli)): the hemispherical term times 1 + 20 × fill × w × s, with w = (1 + N.y) / 2 and s = 1 - 0.65 × (1 - the track lightmap's share), and the driver's shadow map floor 0.25 × fill. At 1 it is the measured `ck-lap-h20p`. The gain, the shade and the driver's floor are defines of the file (`FM4_COCKPIT_FILL_GAIN`, `_SHADE`, `_DRIVER`), not knobs. `-Look "aces,cockpit_fill=0"` is (a).
- **`cockpit_light`** keeps its meaning (the floor of the sun's direct light, also on the track lightmap) and is in no preset; `-Look "aces,cockpit_fill=0,cockpit_light=0.5"` is the look of 20 to 26 October 2026.
- The mods: the 22 interior shaders of the fill define `FM4_COCKPIT_AMBIENT_CONSTANT` (69, or 81 with `FM4_COCKPIT_AMBIENT_COMPONENT 0` for the chrome), `FM4_COCKPIT_LIGHTMAP_FETCH 10` and the hooks of the lightmap fetch and the normal (interpolator 1, interpolator 0 for `F7174B6D1F50A4F3`); the other 16 interior mods are those of `cockpit_light` alone. Each shader's lightmap fetch comes before its reads of the scale and the floor (checked in the ucode of every contract).
- RTV and the native path only; `-Rov`, `-Look original` and `-Look "original,taa=0"` load no mod.

The gate of 26 October 2026 ([current status](../status/current.md#cockpit-sky-fill-26-october-2026)): over the Bernese Alps lap at 1x the gauges keep 10.2 to 32.0 (21.5 in the open, 24.5 in the tunnel, `ck-fin-lap`), the 2x lap gives the crops of `ck-reportinal` (the grid, the sun ahead, the tunnel and the shade), the parity of `original,taa=0` on RTV and `-Rov` passes, and the smoke gate at 240 Hz passes 3 of 3 cells. On a light interior, the career's Ford Ka on Tsukuba, the fill lights the gray dashboard and wheel evenly (the interior's mean luma 83 to 94 against the game's 30 to 43) without clipping; on Laguna Seca, the track with the brightest hemispherical light, the Abarth's gauges read 19.3 to 28.6, as on the Bernese Alps. Light interiors are where a lower `cockpit_fill` may suit; the project owner judges the strength through an export.

## Related

- [Visual style](visual-style.md): `TrackSettings.xml`, the auto exposure and the cockpit exposure.
- [Look mod coverage](look-mod-coverage.md): the interior hashes have no look mod.
- [GTAO design](gtao-design.md): no AO in the cockpit interior.
- [Material atlas](material-atlas.md): the cockpit class and its constant names.
