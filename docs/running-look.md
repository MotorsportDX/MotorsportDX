# Looks and textures

Part of [running Motorsport DX](running.md): the look presets and knobs of `-Look`, the cockpit,
exposure, sun, track lighting, ambient occlusion, TAA and cockpit glass, shader mod development,
and texture replacement. The launcher's other options are in [running.md](running.md#options).

## Contents

- [Look](#look)
  - [Cockpit light](#cockpit-light)
  - [Exposure adaptation](#exposure-adaptation)
  - [Sun glare](#sun-glare)
  - [Track lighting](#track-lighting)
  - [Ambient occlusion](#ambient-occlusion)
  - [Temporal anti-aliasing](#temporal-anti-aliasing)
  - [Cockpit glass](#cockpit-glass)
  - [Shader mod development](#shader-mod-development)
- [Texture replacement](#texture-replacement)

## Look

The look mods are original shader code hooked into the HLSL that the runtime
generates from the game's post-processing shaders, car shaders (body paint,
glass, chrome, rims and the cockpit interior) and track shaders (sky, trees, bushes, grass and the road surface;
`work/fm4-rex/look`; see [Shader overrides](e2e/experiments.md)). `-Look` takes a
preset, knobs, or both. A preset sets only its own knobs. Later items win, so
`-Look "aces,sharpen=0.6"` uses the `aces` preset with sharpening at 0.6.

| Preset | Settings |
| --- | --- |
| `aces` (default) | `tonemap=aces`, `saturation=1.1`, `bloom=0.7`, `sharpen=0.4`, `clearcoat=1`, `clearcoat_glint=1`, `ao=0.6`, `sun=2`, `sky=0.7`, `taa=1`, `cockpit_fill=1`, `windshield=1`, `road_specular=0`, `road_sheen=0.4`, `glass=1`, `chrome=1`, `rims=1`, `adaptation=1` |
| `gt` | the same with `tonemap=gt` (Hajime Uchimura's curve: a long linear middle, so midtone colors stay closest to the game's) |
| `neutral` | the same with `tonemap=neutral` (Khronos PBR Neutral: linear up to 0.76, then only the brightest channel compressed) |
| `gt7` | after Gran Turismo 7: the same with `tonemap=gt7` (GT7's own tone mapping: a darker toe, a long linear middle, a convergent shoulder, and the hues of bright colors kept), `sky_saturation=1.5` and `sky_contrast=0.3` for a deeper blue sky, `exposure=-0.2` (the GT7 curve reaches white 3 stops above middle gray) and `contrast=0.15` for its wider range. Since 29 October 2026, locked by the main session's decision of that day; see [look presets](analysis/look-presets-gt7-fm2023.md) |
| `fm2023` | after Forza Motorsport (2023): the `aces` mods with `saturation=1.05`, `ao=0.9`, `haze=0.4`, `temperature=0.7` and `exposure=-0.3`: by the references' ratios against `gt7`, warmer in the near field (with the haze the warmth stays off the distance and the sky, which keep a pale blue-gray), a little less saturated, hazier in the distance and lower in its highlights, with deeper occlusion. `bloom_width` is left out: with the sun in view it draws a flat, hard-edged disc, and it costs about 0.66 ms of GPU a frame at 2x. Since 29 October 2026, locked by the main session's decision of that day; see [look presets](analysis/look-presets-gt7-fm2023.md) |
| `original` | The game's own look with TAA (`taa=1`) and no shader mod. Clears the knobs given before it in the same value. `-Look "original,taa=0"` is the game's picture without any change. |
| `pc` | The earlier name of `aces`. |

Since 20 October 2026 `aces`, `gt` and `neutral` share the mods that `pc` had; before, they had
only the tone curve, `clearcoat=1`, `ao=0.6` and `sun=2`. Every preset has TAA, so races render
one tile without MSAA ([-SingleTile and -Msaa](running.md#-singletile-and--msaa)).

Knobs that are not set keep the original look. Examples: `-Look "aces,clearcoat=0,ao=0"`
and `-Look original`.

| Knob | Values and default | Effect |
| --- | --- | --- |
| `tonemap` | `filmic`, `gt`, `aces`, `neutral`, `gt7` | Tone curve. `gt7` is a port of Polyphony Digital's GT7 tone mapping (SIGGRAPH 2025, MIT; [look presets](analysis/look-presets-gt7-fm2023.md#2-the-gt7-operator)) with an SDR paper white of 250 cd/m^2. |
| `sharpen` | number | Sharpening. |
| `bloom` | number | Bloom. |
| `bloom_width` | number, default 0 | A wide halo around the bloom (two rings of taps 3% and 6% of the screen's width out), in units of the bloom's own energy, as a low sun spreads its glow. It costs about 0.66 ms of GPU a frame at 2x on this PC. |
| `temperature` | number, -1 to 1, default 0 | White balance: above 0 warmer, below 0 cooler; 1 raises red over blue by about 1.27, the luminance kept. Of the final image; with `haze` above 0 of the scene's near field before the haze mix, by the haze pass's near weight (1 near, falling with the distance, 0 on the sky), so the distance and the sky keep the haze's pale blue-gray. |
| `haze` | number, 0 to 1, default 0 | Aerial haze: the post pass `haze_amount` (`work/fm4-rex/passes/haze.hlsl`) turns each pixel's view distance into an amount, 1 - exp(-(d - 30 m) / 800 m) times the knob, and 0.35 times the knob on the sky, and the final combine mixes the scene toward a pale blue-gray by it, so distant hills and buildings fade and the sky gets paler. Above 0 it turns the post passes on. The shape is `HAZE_DISTANCE`, `HAZE_START` and `HAZE_SKY` in [passes.toml](../work/fm4-rex/passes/passes.toml). |
| `haze_brightness` | number, default 1 | Brightness of the haze, in units of 2 stops above the game's middle gray after the exposure. |
| `exposure` | number | Exposure. |
| `adaptation` | 0 or 1, default 0 | 1: the auto exposure adapts as fast as the game's at 60 fps at every frame rate ([below](#exposure-adaptation)); 1 in every preset but `original` (since 26 October 2026; `gt7` and `fm2023` since 29 October). |
| `contrast` | number | Contrast. |
| `saturation` | number | Saturation. |
| `vignette` | number | Vignette. |
| `motion_blur` | number | Motion blur, in all four motion blur shaders. Photo mode's blur, which the player sets, changes only when this is set. |
| `clearcoat` | number | Clear coat of the car body paint (`fm4-paint.hlsli`) in every car shader set: races, 16-car races, split screen, garage and showroom. In the showroom it adds only the reflection of the studio cube. |
| `clearcoat_roughness` | number, default 0.3 | Roughness of the clear coat. |
| `clearcoat_glint` | number, default 4; 1 in `pc` | Soft limit of the clear coat's sun highlight, in units of the sun's diffuse light; also the limit of the glass, chrome and rim highlights. At 4 the hood turns into a white blob towards the sun. |
| `flakes` | number | Flakes in the car paint. |
| `flake_scale` | number, default 1200 | Scale of the paint flakes. |
| `glass` | number | Windows and lamp covers (`fm4-car.hlsli`) in races, 16-car races, split screen and the garage: the environment's reflection at grazing angles and a sharp sun highlight, with the game's transparency kept. |
| `glass_roughness` | number, default 0.08 | Roughness of the glass highlight. |
| `chrome` | number | Chrome trim and chrome rims, in the same car shader sets: a sun highlight and the rise of the reflection at grazing angles. |
| `chrome_roughness` | number, default 0.1 | Roughness of the chrome highlight. |
| `rims` | number | Painted and alloy rims, in the same car shader sets: a clear lacquer over the game's metal. |
| `rims_roughness` | number, default 0.25 | Roughness of the rim lacquer. |
| `sky_saturation` | number | Saturation of the sky dome of the race tracks (`fm4-nature.hlsli`). |
| `sky_contrast` | number, 0 to 1 | Contrast of the sky dome: an S-curve on its brightness, so clouds stand out more against the sky. |
| `sky_warmth` | number, -1 to 1 | White balance of the sky dome: below 0 bluer, above 0 warmer. |
| `sky_horizon` | number, 0 to 1 | A gradient from the horizon to the zenith: a paler, brighter horizon and a deeper sky overhead. |
| `foliage_hue` | degrees | Hue of the green of trees, tree cards, bushes and grass on every track: below 0 towards yellow and orange (-30 dry, -60 autumn), above 0 towards blue green. Bark, flowers and gray chain-link fences keep their color. |
| `foliage_saturation` | number | Saturation of that green. |
| `foliage_brightness` | number | Brightness of that green. |
| `road_brightness` | number, default 1 | Brightness of the asphalt texture of the road and the road edges on every track (`fm4-road.hlsli`). |
| `road_contrast` | number, default 0 | Contrast of the asphalt aggregate against the texture's local mean: light stones lighter, the binder darker, the overall tone kept. |
| `road_specular` | number, default 1 | The game's sun highlight on the road, its lines, edges and paved shoulders. `0` removes it, and with it the bright sheet on sunlit asphalt toward the sun. |
| `road_sheen` | number, 0 to 1 | A GGX sun highlight with Fresnel on the same surfaces, in the sun's share of their diffuse light, glinting with the aggregate. With `road_specular=0` it replaces the game's highlight. |
| `road_roughness` | number, default 0.5 | Roughness of that highlight: lower is a smaller, brighter glare. |
| `road_sheen_grain` | number, default 1 | How much that highlight follows the aggregate and the skid marks: it scales the highlight by the asphalt texel's brightness against the texture's local mean, to this power (and on the Top Gear and Crown Jewel road by the darkening of the skid marks). `0` gives an even sheen. |
| `road_debug` | 0 to 7 | Debug views of the lit road `ROAD_BLND_2`: 1 the highlight's share of the light, 2 without the highlight, 3 the highlight alone, 4 which light the diffuse takes, 5 the parts of the specular amount. With `road_sheen` above 0, on every lit road shader: 6 the image times the sheen's grain, 7 the sheen alone. See [look mod coverage](analysis/look-mod-coverage.md#road-surface-18-october-2026). |
| `cockpit_fill` | number, 0 to 2; 1 in every preset but `original` | The sky's fill of the shaded cockpit interior (`fm4-cockpit.hlsli`), in the race, 16-car, split-screen and UI car shader sets: it scales the game's own hemispherical term of the interior by 1 + 20 times the knob, weighted by how much a surface faces up and lessened by 65% where the track lightmap shades the car (tunnels, rock walls, stands), and gives the driver a floor of 0.25 times the knob on the car's own shadow. The added light is soft limited by the surface's albedo (since 5 November 2026), so white paddles, gauge rings and light trim keep their shading. The gauges, cluster and dashboard stay readable with the sun ahead, beside or behind, and the interior does not glow in tunnels. `0` is the game's interior. See [Cockpit light](#cockpit-light). |
| `cockpit_light` | number, 0 to 1; no preset since 26 October 2026 | The least share of the sun's direct light that the cockpit interior and the driver keep in the car's own shadow (`fm4-cockpit.hlsli`), in every car shader set: it raises the floor of their shadow term, which the game sets to 0. It follows the sun's direction against the car and also lights the interior where the world is shaded, so the interior swings between lit and black as the car turns and glows in tunnels. See [Cockpit light](#cockpit-light). |
| `ao` | number, `0` turns it off | Strength of the screen-space ambient occlusion. |
| `taa` | `1` on (in every preset), `0` off | Temporal anti-aliasing before the HUD, with the scene jittered while the camera moves and on a still camera, whose frames it accumulates; the look's `sharpen` moves after it, and on RTV races render one tile without MSAA. See [Temporal anti-aliasing](#temporal-anti-aliasing). |
| `taa_point` | `0` (default) or `1` | With `taa`: `1` runs TAA at the scene_color point, before the game's post chain, instead of before the HUD; the look's `sharpen` stays in the final combine. See [Temporal anti-aliasing](#temporal-anti-aliasing). |
| `aa` | `taa` (default), `fsr` or `taau` | With `taa`: `fsr` runs AMD FSR 3.1's native anti-aliasing at the scene_color point instead of TAA, with the look's `sharpen` as FSR's sharpening; needs a runtime built with `-Fsr on`, else TAA runs at that point. `taau` runs TAA at the scene_color point (as `taa_point=1`), and with [-Upscale](running.md#-upscale) as the upscaler, with FSR's sharpening and the jitter of every frame. See [Temporal anti-aliasing](#temporal-anti-aliasing). |
| `fsr_sharpen` | number, 0 to 1; no preset | With `aa=fsr` (and `aa=taau` while upscaling): FSR's sharpening (RCAS; TAAU's own sharpening) in place of its default, the look's `sharpen` at ratio 1 and 0.4 times the [-Upscale](running.md#-upscale) ratio above 1 more while upscaling (0.6 at `quality` with the presets' 0.4), at most 1. For A/B runs, for example `-Look "aces,aa=fsr,fsr_sharpen=0.4"`. |
| `windshield` | number, 0 (off) to 2; 1 in every preset but `original` | Glass on the windshield and side windows of the race cockpit camera, which the game leaves open: a light tint, a faint reflection of the dashboard, and glare and dust toward bright light. See [Cockpit glass](#cockpit-glass). |
| `sun` | number, `0` turns the glare off | Strength of the sun glare on the host render target paths, the same at every MSAA level. Without it (`original`) the glare follows the counted samples. See [Sun glare](#sun-glare). |
| `road_gloss` | multiplier, default 1 | Specular of the road surface. See [Track lighting](#track-lighting). |
| `sky` | multiplier, default 1 | Brightness of the sky; the `sky_*` knobs above change its color. See [Track lighting](#track-lighting). |

`pc` sets `clearcoat_glint=1`, `road_specular=0`, `road_sheen=0.4`, `glass=1`, `chrome=1` and
`rims=1` since 20 October 2026, after an A/B at 2x with the camera turned toward the sun on the
Bernese Alps grid; `-Look "pc,clearcoat_glint=4,road_specular=1,road_sheen=0,glass=0,chrome=0,rims=0"`
is the `pc` look before them. At `clearcoat_glint=4` the hood and roof turned into a white blob
toward the sun, at 1 the highlight keeps its shape. The road knobs turn the bright sheet on sunlit
asphalt toward the sun into a glare with the aggregate showing through it (with the default
`road_sheen_grain=1`; without the grain the sheen is an even, milky haze on Top Gear), and views
away from the sun look the same. The glass, chrome and rim mods add a little reflection to the
windows at grazing angles and showed no bloom blob toward the sun (see
[look mod coverage](analysis/look-mod-coverage.md#facing-the-sun-and-the-pc-preset-20-october-2026)).
No preset sets the `sky_*` or `foliage_*` knobs: in an A/B at 2x on the Bernese Alps
and Maple Valley, `sky_saturation=1.15,sky_contrast=0.3,sky_horizon=0.5` gave a slightly deeper
blue sky, and `foliage_saturation=1.15` was barely visible (see
[look mod coverage](analysis/look-mod-coverage.md#in-game-checks-sky-and-vegetation-18-october-2026)).
For autumn colors try `-Look "pc,foliage_hue=-45"`.
### Cockpit light

In the sun the car's roof shades most of the cockpit interior, and FM4 draws the shaded part with a small
hemispherical term only, near black against an exposure set by the windows. Since 26 October 2026 the
presets `aces`, `gt` and `neutral` (and since 29 October `gt7` and `fm2023`) set `cockpit_fill=1`, a sky fill from that term; from 20 to 26 October
2026 they set `cockpit_light=0.5`, a floor of the sun's direct light, which the project owner saw swing
between much too bright and much too dark with the sun's position.

- Over a lap of the Bernese Alps with the AI driver (1x, the gauges' mean luma of 255): 1.2 to 92.6 with
  `cockpit_light=0.5`, 88.8 in the tunnel; 1.0 to 12.8 with neither knob, the same as `-Look original`
  and `-Rov`; 12.0 to 29.7 with `cockpit_fill=1`, 21.5 in the open and 24.6 in the tunnel.
- `-Look "aces,cockpit_fill=0"` is the game's interior; `-Look "aces,cockpit_fill=0,cockpit_light=0.5"`
  the look of 20 to 26 October 2026.
- Since 5 November 2026 the fill's added light is soft limited by the surface's albedo: on the 2009 BMW
  M5 at Circuit de Catalunya the white paddles stay at 214 of 255 instead of 250 with a bloom halo,
  while dark leather loses 5 to 12%
  ([light surfaces](analysis/cockpit-brightness.md#light-surfaces-5-november-2026)).
- The fill covers the 22 interior shaders that have the track lightmap and a hemispherical scale and the
  six driver shaders; the 16 others (the showroom set and the `cockpit_*` material shaders) keep the
  game's interior. It lights only the cockpit interiors and the driver, also where they show through a
  car's windows, not the windows themselves; a brighter interior can lower the auto exposure a little in
  dark scenes.
- `windshield=1` (since 20 October 2026; `-Look "aces,windshield=0"` turns it off) adds the glass
  ([Cockpit glass](#cockpit-glass)).

See [cockpit brightness](analysis/cockpit-brightness.md#the-cause-the-cockpit_light-floor-25-october-2026).

### Exposure adaptation

The game's auto exposure moves towards the scene's brightness by a fixed share of the
difference in every frame (0.045, the same at every frame rate), made for the Xbox 360's
60 fps. Above 60 fps it therefore settles faster than the game does: four times as fast at
`-FrameRate 240` (a time constant of about 0.09 s instead of 0.36 s), twice at 120, and twice
in the countdown and replays at the default rate, which run at 60 fps instead of the title's
30 ([-FrameRate](running.md#-framerate)). After a change of the scene's brightness, such as a tunnel
exit, the picture reaches its new exposure in about a quarter of the game's time at 240 fps.

`adaptation=1` gives each frame the share that covers the same part of the difference in
that frame's time at the game's speed: `1 - (1 - share)^(design rate x frame interval)`. The
design rate is the frame rate the title is designed to render the scenario at (60 in races,
30 in its 30 fps screens and in the countdown and replays, which it presents every second
vblank), as `fm4-vblank-rate.cpp` reports it; the frame interval is the title's measured time
between its presented frames, averaged over about eight frames, each at most 50 ms. The
runtime passes both to the shader mods (`xe_guest_design_frame_rate`, `xe_guest_frame_interval`),
so a race below its target rate, such as on the ROG Xbox Ally X, keeps the game's speed too.
The power is at least `60 / vblank rate`, the most frames the title can present at that rate,
so a switch of the scenario, which the averaged interval follows a few frames late, never
slows it, and at most 1: below the title's own rate it adapts per frame, as the title does.
At a 60 Hz guest vblank (the menus, the 30 fps screens, and races without `-FrameRate`) it
changes nothing. It is a shader mod of the auto exposure shader
(`shader_CAF3881F7C4CDCAA.mod.hlsli`), so it works on the RTV path and the native path;
`-Rov` keeps the title's adaptation. The GUI launcher shows it as the checkbox
**60 fps exposure speed**. Every preset but `original` sets it (`aces`, `gt` and `neutral` since 26 October 2026, `gt7` and `fm2023` since 29 October);
`-Look "aces,adaptation=0"` keeps the title's adaptation, as `original` does.

Measured on 26 October 2026 in a frozen race frame whose exposure input was stepped: the
exposure made 90% of its change 1.47 to 1.50 s after the step at 60 Hz, with and without the
knob; with it 1.41 s at 240 Hz on RTV held at 160 fps by its tiles, and 1.43 s on the native
path at 229 fps, against 0.59 and 0.34 s without it. Details in the
[current status](status/history-results.md#auto-exposure-adaptation-by-frame-time-26-october-2026).

### Sun glare

The title draws the sun glare from an occlusion query of the sun sprite. The host query
counts samples, so without a correction the same sun gives twice the glare at 2x MSAA
(`-Quality xbox` races) and four times at 4x (`high` races, replays, photo mode) as at 1x.
The `sun` knob makes the runtime report guest pixels instead, times the strength
(`rtv_occlusion_awaited_scale`), through a soft limit of 1,200 pixels
(`rtv_occlusion_awaited_limit`, `SUN_LIMIT` in `pipeline.py`): up to 400 pixels pass
unchanged, above that they approach the limit, so the glare cannot wash out the screen. A
fully visible sun is about 338 pixels at 720p. The knob is a launcher setting, not a
shader mod, so it also works with `-Look original,sun=1`. `-Rov` does not use it.

### Track lighting

`road_gloss` and `sky` do not change shaders. They edit the title's per-track lighting
files, `Media\tracks\<Track>\TrackSettings.xml` and `TrackSettings1.xml`, as multipliers of
each track's own values, so every track keeps its own character:

- `road_gloss` scales `RoadSpecular` and `RoadSpecular2` of the `RoadSpecular` element, the
  specular of the road surface. The glossiness (highlight size) is left as it is.
- `sky` scales `skyScale`, the brightness constant of the sky shaders.

The launcher passes them as `FM4_TRACK_SETTINGS` (`fm4-track-settings.h`). At start the game
copies those files of every race location on Disc 1 from your own data, edits the values,
writes the copies to `BuildRoot\cache\fm4-track-settings`, and opens the copies instead
of the disc files. The garage, showroom and car thumbnail scenes (`HomeSpace*`,
`UIThumb*`) and the tracks of downloadable content keep their files. A knob at 1 edits
nothing, and then no copies are made.

Every preset but `original` sets `sky=0.7` (`pc` since 18 October 2026), and no preset
sets `road_gloss`. `-Look "aces,sky=1"` restores the game's sky. In 2x
captures of that day on the Bernese Alps, `sky=0.7` gives a bluer, less washed out sky that
keeps the snow and clouds bright. Lowering `road_gloss` only removes the
sparkle of the asphalt aggregate. The bright sheet on sunlit asphalt toward the sun is the
road shader's sun highlight, but most of its specular amount comes from the blend value of
the rubber (`RoadSkid` and `RoadSkid2`), which `road_gloss` leaves as it is. `road_specular`
and `road_sheen` (see [Look](#look)) act on the whole highlight. See
[look mod coverage](analysis/look-mod-coverage.md#road-surface-18-october-2026).

### Ambient occlusion

`ao` above 0 turns on the runtime's post passes (`post_pass_path`, a copy of
`work/fm4-rex/passes` in `BuildRoot\passes`; see the
[post pass framework](analysis/post-pass-framework.md)), whose GTAO pass works on the
scene depth (see [GTAO design](analysis/gtao-design.md)) with a radius of 0.75 m. The
final combine shaders darken the scene by the occlusion, less on bright pixels. It covers
races, replays, photo mode, the full-screen car views of the garage, Autovista and the career
home; not the cockpit interior, and not the small car view of the car select screen.
`pipeline.py --ao` (and `e2e.py --ao`) runs the pass without a look applying it. With
`-ShaderDev` the passes load from `work/fm4-rex/passes` itself and reload when a file
there is saved; `shader_mod_set GTAO_RADIUS=1` in the console changes a pass param at
once.

The passes also write the scene's motion vectors (see
[motion vectors](analysis/motion-vectors.md#8-implementation-18-october-2026)), for later
temporal passes, and a mask of the pixels whose motion is unknown or mixed;
`shader_mod_set MOTION_VECTORS=0` skips them. `shader_mod_set MOTION_DEBUG=1` draws
them color coded over the frame, under the HUD (2: over the scene, 3: the car IDs, 4: the
cars' own motion, 5: the cockpit interior, the world through glass and the depth masks,
6: the mask; 0: off).

The GTAO pass costs about 0.2 ms of GPU time per race frame at 1x, and 0.7 to 0.8 ms at 2x
(2560x1440). Races stay at 60 fps. At 240 Hz, races at 2x drop from about 174 fps to
about 162 fps. Replays and photo mode have occlusion too. The cockpit interior, the
garage and the showroom get none. The first run of a look compiles the mod shaders with
FXC when they are first drawn. For the six measured, this took 0.15 to 1.4 s each and
2.5 to 3.0 s for the paint (about 5.4 s at the start). Later runs load them from
`BuildRoot\cache\shader-overrides` in milliseconds.

### Temporal anti-aliasing

`taa=1` turns on the TAA post pass ([TAA](analysis/taa.md)): the scene draws move by a
sub-pixel offset each frame, and the frames before the HUD are accumulated along the motion
vectors, so wires, fences, lattices, distant road lines and shading glints stop stepping and
shimmering. The HUD and the side mirrors are drawn after it and stay sharp. `pc` had it since
19 October 2026, and every preset since 20 October 2026; with it races render one tile
without MSAA ([-SingleTile and -Msaa](running.md#-singletile-and--msaa)). `-Look "aces,taa=0"` is the look
without it (the look's sharpening, no jitter, the tiles and MSAA of `-Quality`).

- The scene draws are jittered while the camera moves and the frames come at 45 fps or more,
  and on a still camera at any frame rate (since 20 October 2026): there TAA accumulates the
  jitter's phases where nothing moves, so rewind, pause, photo mode and a still replay or
  countdown camera get a still, anti-aliased picture instead of a shimmering one. A camera
  that moves at under 45 fps (the 30 fps post-race) is not jittered. The pass params
  `TAA_JITTER_MOTION` (0.5 host pixels per frame) and `TAA_JITTER_RATE` (45) set the
  thresholds, `TAA_STILL` (1) the accumulation on a still camera and `TAA_STILL_FRAMES` (256)
  how many frames it averages; they change live with `-ShaderDev`. `TAA_JITTER_MOTION` and
  `TAA_JITTER_RATE` at 0 jitter every frame, `TAA_STILL=0` turns the still camera's jitter off
  ([TAA, arming](analysis/taa.md#35-arming-and-switching)). On a still camera TAA also
  compares each pixel with the same jitter phase a cycle ago (`TAA_STILL_PHASES`, 1): what
  did not change keeps its thin details and colored edges still, and what changed (a flag in
  the wind) blends as in motion instead of smearing; `TAA_STILL_CHANGE` (2 levels of 255) is
  the threshold ([TAA, step 12](analysis/taa.md#72-per-pixel-taa_resolve)).
- The look's `sharpen` (0.4 in the presets) is applied after TAA instead of before it (the pass param
  `TAA_SHARPEN`), since sharpening the jittered frame would sharpen the jitter. TAA without
  sharpening is visibly softer; 0.4 comes close to the look without TAA, without its stair
  steps. `original` has TAA without sharpening.
- In the single tile the swap's FXAA is left out: TAA anti-aliases the tile itself.
- It costs about 0.1 ms of GPU time per race frame at 1x and 0.15 to 0.2 ms at 2x.
- `shader_mod_set TAA=0` switches it off at once with `-ShaderDev` (the tiling stays until the
  next race); `TAA_DEBUG=1` to `7` shows the debug views
  ([TAA, debug views](analysis/taa.md#76-debug-views-taa_debug)).
- The `-Info` overlay shows "Anti-aliasing: TAA before the HUD, sharpen 0.4, scene jittered
  while the camera moves, a still one accumulated".
- On the host render target paths only (the native path and RTV), like the other post passes;
  not with `-Rov` or `-Look "original,taa=0"`.
- `aa=fsr` (`-Look "aces,aa=fsr"`; with [-Upscale](running.md#-upscale) it also upscales) runs AMD FSR 3.1's native anti-aliasing (its upscaler at ratio 1) at the scene_color point instead of TAA, on a runtime built with `Build.ps1 -Fsr on` ([setup](setup.md#fsr-31-optional)): the scene is jittered in every frame, FSR accumulates still pictures itself, the look's `sharpen` becomes FSR's RCAS sharpening, and its output is written back into the game's scene. A runtime built without FSR keeps TAA at the scene_color point and logs a warning. The `-Info` overlay shows "Anti-aliasing: FSR 3.1 before the post chain (scene_color), sharpen 0.4, scene jittered every frame"; the GUI launcher offers TAA or FSR 3.1 under Temporal AA. `shader_mod_set FSR_DEBUG=1` shows FSR's debug view with `-ShaderDev`. It costs about three times TAA's GPU time (1.09 against 0.36 ms a race frame at 2x on this PC); the comparison and the A/B checklist are in [FSR plan, 9.9](analysis/fsr-plan.md#99-stage-2-the-checks-before-the-owners-ab-25-october-2026).
- `taa_point=1` (`-Look "aces,taa_point=1"`) moves TAA to the scene_color point: it runs on the scene before the game's motion blur, bloom, exposure and final combine, and its output is written back into the game's scene, which those then sample. The occlusion gets its own temporal filter there (`GTAO_TEMPORAL`), and the look's `sharpen` stays in the final combine, after TAA. It costs about 0.47 ms of GPU time per race frame at 2x, and the `-Info` overlay shows "Anti-aliasing: TAA before the post chain (scene_color), the look's sharpening, scene jittered while the camera moves". The presets keep the final point; see [TAA, the comparison](analysis/taa.md#23-comparison).

### Cockpit glass

In the race cockpit camera FM4 draws no glass for the player's car, so the windshield and side
windows look like open frames ([cockpit brightness](analysis/cockpit-brightness.md#windshield-and-side-windows)).
`windshield=1` (in `pc` and then `aces`, `gt` and `neutral` since 20 October 2026; `-Look "aces,windshield=0"` turns it off) adds a post pass that puts glass over the pixels that
see the world through the cockpit's windows ([windshield](analysis/windshield.md)): a light
green-gray tint, a reflection of the dashboard (faint, since FM4's dashboards are dark), and a
haze with dust and wiper arcs where bright sky or sun shows through. `windshield=2` doubles
every effect.

- Only frames of the cockpit camera change. The chase, hood and bumper cameras, the 3-2-1
  camera, the HUD, the side mirrors and the interior mirror stay as they are.
- It runs before the HUD and after TAA. The `-Info` overlay shows "Cockpit glass: strength 1".
- On the host render target paths only (the native path and RTV), like the other post passes;
  not with `-Rov`.
- With `-ShaderDev`, `shader_mod_set WINDSHIELD=0` switches it off at once, `WINDSHIELD_DEBUG=1`
  to `3` shows the debug views, and the `WINDSHIELD_*` defines of
  [passes.toml](../work/fm4-rex/passes/passes.toml) tune each effect.

### Shader mod development

`-ShaderDev` is for working on the look mods; see
[shader-mod-iteration.md](analysis/shader-mod-iteration.md).

- The mods load from `work/fm4-rex/look` itself instead of a copy. Saving a file there
  recompiles the shaders that use it, without a restart. FXC errors go to
  `BuildRoot\logs\runtime.log`, and the last shader that compiled stays on screen.
- Every knob of the look files is live. Open the console with the backtick key and type
  `shader_mod_set FM4_LOOK_SHARPEN=0.6 FM4_LOOK_BLOOM=0.8`: the image changes in the next
  frame. `shader_mod_set` alone lists the values. The names are the `FM4_LOOK_*` defines
  of `fm4-look.hlsli`, `fm4-paint.hlsli`, `fm4-car.hlsli`, `fm4-nature.hlsli` and `fm4-road.hlsli`; `FM4_LOOK_TONEMAP` takes 0 to 4 (`filmic`,
  `gt`, `aces`, `neutral`, `gt7`). The haze's strength is the pass param `HAZE` (with `FM4_LOOK_HAZE`
  above 0).
- `NAME=VALUE` lines in `work/fm4-rex/look/shader_mod_defines.txt` (not tracked by Git)
  apply in the same way whenever the file is saved.
- The GTAO pass only runs when `ao` is above 0 in `-Look` (or with `pipeline.py --ao`), so
  start with it on to change `FM4_LOOK_AO` live.
- Live knobs make the mods larger and slower. Put the values you found into `-Look` for
  normal runs, for example `-Look "pc,sharpen=0.6,bloom=0.8"`.

## Texture replacement

The runtime can write the game's textures to DDS files and use edited files in their
place, like a Dolphin texture pack. Each texture is named by a hash of its contents, so
a file keeps working across runs, builds and game releases. The design and its limits
are in [texture-replacement.md](analysis/texture-replacement.md).

1. Dump: run with `-TextureDump D:\fm4-dump` and drive where the texture is shown. Each
   new texture is written once as `<16 hex digits>.dds`, and a line about it (format,
   size, mip count, guest address) is added to `textures.txt`. Files that are already
   in the directory are not written again.
   Without the game, `python outputs/FM4-build/texture_extract.py --build-root <BuildRoot>`
   writes the same files for the textures stored in the track archives, the skies of
   every track among them, to `<BuildRoot>\texextract`
   ([offline extraction](analysis/texture-replacement.md#offline-extraction)).
2. Edit a dumped file, or make a new one with the same name, and put it in a pack
   directory (subdirectories are allowed).
3. Run with `-TextureReplace D:\fm4-pack`. `BuildRoot\logs\runtime.log` lists every
   file found, every texture that was replaced, and why a file was not used.

A replacement may have another size and format than the original: BC1 to BC7, RGBA8,
BGRA8, R8, RG8, R10G10B10A2 and RGBA16F, with or without mips. Give it a full mip chain;
a file without mips is sampled without them. sRGB files are read as their UNORM
equivalent, because the game's shaders decode gamma themselves. Block-compressed files
must be a multiple of 4 in width and height.

Both options are off by default and cost nothing then. With either one on, each
texture load is hashed on the CPU (an untiling copy of its base level). Render
targets, cube maps, arrays, 3D textures and textures that change all the time are never
dumped or replaced.
