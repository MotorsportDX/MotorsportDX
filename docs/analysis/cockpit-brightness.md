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

`-Look "...,cockpit_light=<0 to 1>"` raises the shadow floor of the interior and the driver ([`fm4-cockpit.hlsli`](../../work/fm4-rex/look/fm4-cockpit.hlsli); [running](../running.md#look)). The default 0 is the game's interior, and no preset sets it.

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

## Windshield and Side Windows

In the cockpit camera the windshield and the side windows look like open frames: no tint, reflection or glare on the view outside. The game does not draw them in that camera.

- Run `cockpit-glass` (same setup, `-Look original`, five freezes) with `REX_DRAW_LIST_TRACE` every 1,200 frames and a temporary mod on the car glass shader `E62CFDF09D758B6C` (live: glass hidden, opaque magenta coverage, opacity as gray, opacity times 4).
- The player's car is the stencil reference of the interior draws (`4EE552137CA415E3`). In the 3-2-1 countdown camera, its draws include the body (`8D05A46C3584CA9E`), chrome (`A42BEA3C0A6E5603`), the lamps (`AD10B610621DEFE5`) and the windows: `D7B380C7A7F72EF5` with `E62CFDF09D758B6C`, two meshes of 883 and 910 indices, each drawn twice (`reflectionScaler` 0, then 1), with alpha blending, `GlassColor` (0.01, 0.01, 0.01) at alpha 0.3 and `GlassMaxOpacity` 1.
- In every traced frame of the race in the cockpit camera (14 frames), the player's car draws none of these. The only glass left is a mesh of 17 indices with `GlassColor` alpha 0, the glass over the gauges. The draw list is the guest's command stream, so the title itself issues no window draws in this camera; the render path cannot change that, and the ROV captures show the same open windows.
- The magenta coverage shows the windows of the other cars, the reflection of them in the mirrors and the gauge glass, and nothing on the player's windshield or side windows. Hiding the glass changes no pixel outside the gauges, and the gauge glass is fully transparent (opacity view 0.1 of 255).
- The executable has a title command line option `hidecockpitglass` (in the table with `engageaionstart`, read through `FM4_GAME_CMDLINE`) and a windshield reflection feature (`WindshieldReflection`, `ICarPresentationSetWindshieldReflectionTexture`, the pixel shader `7226D929481D4274` with `WindshieldReflectionScale`), whose shader is only in `shaders_slod_v16`, the car set of `UI_Showroom`. Neither was tested; the option most likely hides the gauge glass, the only cockpit glass the race draws.

Verdict: game behavior, the same on both render paths. A visible windshield would be new work: a post pass in the cockpit camera that tints the world seen through the windows (the pixels that are not the interior: the player's stencil reference) and adds a faint reflection or glare, or a hook that makes the title draw its exterior glass in this camera, whose shader is made for views from outside. The post pass is prototyped as the look knob `windshield`, off by default and in no preset ([windshield](windshield.md)); the hook is not built.

## Related

- [Visual style](visual-style.md): `TrackSettings.xml`, the auto exposure and the cockpit exposure.
- [Look mod coverage](look-mod-coverage.md): the interior hashes have no look mod.
- [GTAO design](gtao-design.md): no AO in the cockpit interior.
- [Material atlas](material-atlas.md): the cockpit class and its constant names.
