# Running FM4-PC

This guide covers launching a finished build with `Build.ps1 -Action run`: what
the launcher checks and sets up, its options, the look knobs, the controls and the
data directories. Build the game first; see [setup.md](setup.md).

Performance numbers in this file describe the launcher defaults, and the option
details keep the measurements that explain each option. Measurements that depend on
a specific build are dated in the status documents: [status/current.md](status/current.md)
and [status/history.md](status/history.md).

## Launching

```powershell
.\outputs\FM4-build\Build.ps1 -Action run -BuildRoot 'D:\FM4-build' -LLVMRoot 'C:\Program Files\LLVM' -Python 'C:\Python311\python.exe'
```

If you open a new PowerShell session, define `$common` again as shown in
[setup.md](setup.md#commands). `run` uses the Disc and content locations saved by
`generate`; it does not read `-Disc1` or `-Disc2`. `-Content` on `run` replaces the
saved content locations (see [Downloadable content](setup.md#downloadable-content)).

The launcher then:

1. Checks the build artifacts and the game revision. It compares the artifact
   hashes with `build-result.json`, validates the Disc and content paths in
   `local.json` and checks the three executables.
2. Starts with a new local profile in `BuildRoot\user-data` (see
   [Data directories](#data-directories)).
3. Mounts Disc 1 and imports the content packages: Disc 2's four, if Disc 2 was
   given, and those found in the `-Content` paths. It lists them in
   `BuildRoot\content-packages.txt`. A package is imported once, into
   `BuildRoot\user-data`; later runs skip it while its size is unchanged. The first
   import needs extra disk space, about the size of the packages.
4. Prints the display and render scale that it chose and starts the game.

The game window runs fullscreen on the primary monitor by default. Use Alt+Enter to
switch to a window (see [Controls](#controls)).

## Launcher defaults

Running with no options gives this configuration:

- **Shader compilation** is synchronous (`async_shader_compilation=false`).
- **Anisotropic filtering** is 16x for every mipmapped linear texture
  (`anisotropic_override=5`). The title asks for none on most textures and 2x to 16x
  on a few; the runtime default is 4x.
- **Rear-view mirror** is rendered every other frame in every race, as the title does
  for 12- and 16-car races. One- and eight-car races render it every frame. See
  [-FullRateMirror](#-fullratemirror).
- **Shadows** use the title's shadow map for every camera. See
  [-ShadowWorkaround](#-shadowworkaround).
- **Race quality** is `high`. See [-Quality](#-quality).
- **Look** is the `pc` preset, with screen-space ambient occlusion, temporal
  anti-aliasing, the cockpit interior's shadow floor, the cockpit glass, the road sheen and the
  glass, chrome and rim mods. See [Look](#look).
- **Display** is `native`: borderless fullscreen on the primary monitor, with a render
  scale of 2x on 1080p, 1440p and 2560x1600 screens and 3x on 4K. See
  [-Display](#-display-and-render-scale).
- **Render path** is the host render target path (RTV) with the frame pacing and
  memory settings described in [Render path and frame pacing](#render-path-and-frame-pacing).

Performance on the defaults: Quick Race holds 60 fps on the development computer.
The GPU time per race frame with the defaults (`high` quality, `pc` look with
ambient occlusion) is about 3.0 ms at 1x (1280x720) and about 6.1 ms at 2x, measured
before `pc` had TAA, which adds about 0.1 ms at 1x and 0.15 ms at 2x. The 2x
scale is the one that `native` uses on 1080p, 1440p and 2560x1600 screens.

### Render path and frame pacing

The launcher uses these runtime settings (set in `launch_cvars` in
[pipeline.py](../outputs/FM4-build/pipeline.py)):

- Rendering uses the host render target path `render_target_path_d3d12=rtv` with
  `edram_single_rt_no_wrap=true`. Earlier measurements put the GPU time at about 2.5 ms
  per race frame on this path, against about 7 ms on the pixel shader interlock path
  (`-Rov`). Quick Race runs at a stable 60 fps.
- Guest memory that the CPU wrote is uploaded to the GPU once, not every frame
  (`clear_memory_page_state=false`).
- Command lists are recorded and executed on a thread of their own
  (`d3d12_submit_thread=true`). The copies of resolve readbacks into guest memory run
  on their own thread too (`d3d12_readback_publish_thread=true`).
- Occlusion reports are published at once from the previous frame's samples
  (`rov_occlusion_async=previous`). Resolve readbacks do not wait for the GPU
  (`readback_resolve=fast`).
- A frame that misses a guest vblank gets it as soon as it is ready
  (`vblank_vrr=true`). See [-FixedVblank](#-fixedvblank).
- The road sections that this path used to leave undrawn came from the title's track
  streaming. They are fixed for every render path.

Known limits of this path: the sun glare has not been calibrated on it (see
[status/current.md](status/current.md#open-issues)). Interactive Autovista has not
been tested on it. If rendering misbehaves, compare with `-Rov`. Which scenarios have passed on this path is recorded
in [status/current.md](status/current.md).

## Options

Pass the options to `Build.ps1 -Action run`. Each option is a switch unless it
takes a value.

| Option | Default | Description |
| --- | --- | --- |
| `-FrameRate 30-240` | 60 | Race frame rate, also for replays and the 3-2-1 countdown. Values above 60 lift the race limit; menus, post-race and photo mode keep their own timing. |
| `-Display native\|720p\|1080p\|1440p\|4k` | `native` | Screen and render size. `native` is borderless fullscreen on the primary monitor. Other values are a window of that size. |
| `-Fullscreen` | off | Fullscreen with the `-Display` size and its render scale. |
| `-Supersample` | off | Render one scale above the screen (up to 4x), then average the result down to the screen. |
| `-ResolutionScale 1-4` | from `-Display` | Render at this multiple of 1280x720. Overrides the render scale of `-Display`. |
| `-Quality xbox\|high\|ultra` | `high` | Render quality of races. |
| `-Look <preset/knobs>` | `pc` | Post-processing look and car paint. See [Look](#look). |
| `-ShaderDev` | off | Shader mod development: the look mods reload when a file in `work/fm4-rex/look` is saved, and the knobs change live. See [Shader mod development](#shader-mod-development). |
| `-TextureDump <dir>` | off | Write each new game texture once as `<hash>.dds` to this directory. See [Texture replacement](#texture-replacement). |
| `-TextureReplace <dir>` | off | Use `<hash>.dds` files from this directory instead of the game textures with that hash. See [Texture replacement](#texture-replacement). |
| `-Info` | off | Show the settings overlay at start. F2 toggles it in any run. |
| `-Rov` | off | Use the earlier pixel shader interlock render target path, for comparisons. |
| `-SingleTile` | off | Render races as one 720p tile without MSAA, with FXAA on the swap. |
| `-FixedVblank` | off | Hold every swap to the fixed 60 Hz vblank grid. |
| `-FullRateMirror` | off | Render the rear-view mirror in every frame of every race. |
| `-ShadowWorkaround` | off | Render the shadow map only with the cockpit camera, as the old Xenia workaround did. |

`-Action warmcache` is not a run option; it is described in
[Shader cache](#shader-cache). The `--ao` flag of `pipeline.py` and `e2e.py` has no
switch in `Build.ps1`. It runs the GTAO pass without a look applying it; see
[Look](#look).

### Option details

#### -FrameRate

`-FrameRate 120` lifts the 60 fps limit of races. The title sees a 120 Hz display,
the `FrameRate` of its race render scenarios becomes 120, and the title scales its
time step (race clock against wall time 1.001). The guest vblank stays at 60 Hz in
menus, post-race and photo mode (30 fps, as on the Xbox 360). It runs at 120 Hz only
while a race scenario sets the frame rate (`fm4-vblank-rate.cpp`).

Since 20 October 2026 replays and the 3-2-1 countdown run at the race frame rate too
(`FM4_FULL_RATE=replay,countdown` in the launcher environment), instead of the title's
30: 60 fps without `-FrameRate`, 240 at `-FrameRate 240`. The replay scenarios get the
race rate in their file; the countdown shares its scenario with the post-race, so its
rate is set when the countdown is pushed after the pre-race, and put back for the
post-race. The title presents both every second vblank whatever their frame rate, so
while one of them runs the guest vblank goes to twice the race rate, at most 240 Hz:
60 fps at the default (vblank 120 Hz), up to 120 fps at `-FrameRate 120` or more. The
replay and countdown clocks follow wall time at every rate (replay clock against wall
time 1.00 at 30, 60 and 240), so only the motion gets smoother. The post-race screens
and photo mode keep 30.

Measured on 20 October 2026 (`career` scenario, Tsukuba, AI driver): at 1x the replay ran
at 59.7 fps by default and 94.7 to 101.6 fps at 240 Hz (30 before), the countdown at 60 and
90 to 115 fps (30 before); at 2x the replay ran at 59.2 and 94.4 fps.

Measured with Quick Race, three runs: 118 to 120 fps, frame p99 8.8 to 9.8 ms, and
every frame fully rendered. At 240, races run at 220 to 235 fps (p99 5.3 to 6.2 ms).
At 240 the command processor sets the limit: it needs about 4.3 ms per race frame,
against about 2.2 ms of GPU time. Values from 30 to 240 are accepted.

#### -Display and render scale

`native` (the default) is borderless fullscreen on the primary monitor. The other
sizes (`720p`, `1080p`, `1440p`, `4k`) are a window of that many pixels. They go
fullscreen with `-Fullscreen`, or when they do not fit the monitor. Window sizes take
the Windows display scale into account.

The render scale is the smallest whole multiple of 1280x720 that covers the 16:9
picture of the screen: 720p 1x, 1080p and 1440p 2x, 2560x1600 2x, and 4k 3x. `-Display
720p` was the earlier default (1x in a 1280x720 window). If the render is larger than
the window, the presenter averages it over each screen pixel in linear light
(`work/runtime/src/ui/shaders/guest_output_area.ps.hlsl`; `present_area_downscale=false`
selects the earlier single bilinear tap). So 2x on a 1080p screen is supersampling,
not a shrink that skips texels.

`-Supersample` renders one scale above the screen, up to 4x (5120x2880), and averages
it down. Edges are smoother and thin objects are more stable, at the GPU cost of that
scale.

`-ResolutionScale 2` renders at 2560x1440 and `-ResolutionScale 3` at 3840x2160,
instead of the size from `-Display` (`draw_resolution_scale_x` and
`draw_resolution_scale_y`; CPU readbacks of resolves are downscaled). Measured with
Quick Race: 59.7 fps with about 3.7 ms of GPU time per frame at 2x, and 59.9 fps with
about 6.8 ms at 3x. The HUD and the scene are sharp. The smoke gate at 2x (240 Hz)
passes: races run at 202 to 206 fps, photo mode at 30 fps (GPU 7.5 ms), a photo can be
saved and loaded, and a career race with its replay runs at 30 fps (GPU 8.2 ms). The
replay used to drop to 23.5 fps after a long frame; see `-FixedVblank`. These numbers
predate `-Quality high` and the clear coat.

Alt+Enter switches between borderless fullscreen and the window in any run
(`bind_fullscreen`). The render scale stays the same.

#### -Quality

Sets the render quality of races through their render scenarios
(`FM4_RACE_SETTINGS`, `fm4-render-overrides.cpp`). It applies to the N-Car, Autocross
and Bowling scenarios, not to split screen.

- `xbox` keeps the title's own settings.
- `high` (the default) gives races what the title gives its replays and photo mode: 4x
  MSAA in three predicated tiles (`TilingScenario` 0, the title's 4xMSAA scenario,
  instead of 1, which is 2xMSAA in two), soft particles, shadows of collidable objects,
  full-detail wheels, and car LOD transitions at twice the distance (30/50/100 m). It
  also undoes the cuts that the title makes for 16-car races: the cheaper car shader,
  no damage, an environment map that is never updated, half-rate bloom, and no
  collidables in the mirror.
- `ultra` also keeps every car at its top LOD (`CarKeepLOD0`, as 1-car races do) and
  renders the mirror every frame, with up to 11 cars.

Measured at 240 Hz in the 12-car race phase of the photo scenario: `xbox` 219 to 224
fps in three runs (GPU 2.3 ms a frame). `high` 178 to 225 fps in four runs, 201 on
average (GPU 2.8 ms), because the three tiles execute 4,500 to 8,300 draws a frame
depending on where the cars are (about 5,200 on `xbox`). `ultra` 144.8 fps (median of
three runs, GPU 3.1 ms, command processor busy 974 ms a second). All three hold 60 fps
with room to spare. `high`, at 178 fps in the worst run, leaves room for 120 Hz (not
measured at 120 Hz). `ultra` is meant for 60 to 120 Hz.

#### -Info

Shows the settings overlay at the top left from the start. It lists the display and
render scale, the race quality, the look and its knobs, the ambient occlusion, the
anti-aliasing (TAA and its point and sharpening), the shadows, the anisotropic filtering, the race frame rate and pacing, the mirror rate,
the render target path, and the window size and present rate, as the launcher set them
up (`info_overlay` in `pipeline.py`, `FM4_INFO`; `work/fm4-rex/src/fm4-info-overlay.h`).
The presenter draws the overlay over the guest output, so captures never show it.
F2 toggles it in any run.

#### -Rov

Uses the earlier pixel shader interlock render target path
(`render_target_path_d3d12=rov`, `rov_occlusion_async=all`) for comparisons. Measured
with Quick Race: 60.0 fps, with about 7.1 ms of GPU time per frame (2.5 ms on the
default path). Its occlusion reports are the counted guest samples, without a title
correction (since 18 October 2026 the launcher no longer passes
`rov_occlusion_awaited_report_divisor=2`). Use it as the reference when rendering
misbehaves on the default path.

Shader mods and ambient occlusion are RTV only, by design: with `-Rov` the runtime
turns off the shader overrides ("Shader overrides need bindless resources and host
render targets" in the log), so every look mod and the AO are gone and the picture is
the title's own. Compare the default path against `-Rov` with `-Look original`.

#### -SingleTile

Renders races as one 720p tile without MSAA. This is the title's own UI tiling
scenario, selected through the render scenario override. The swap applies FXAA, unless the look
has `taa=1` (see [Temporal anti-aliasing](#temporal-anti-aliasing)).
Measured with Quick Race on `-Rov`: 58.6 fps instead of 54.3, frame p95 20 ms instead of
33 ms, GPU 13 ms instead of 17.5 ms. Edges are visibly more jagged than with MSAA (2x
with `-Quality xbox`, 4x with the default `high`): thin cables break up. Photo mode and
split screen keep their own settings.

#### -FixedVblank

Holds every swap to the fixed 60 Hz guest vblank grid (`vblank_vrr=false`). By default
(`vblank_vrr=true`), a frame that misses a vblank gets it as soon as it is ready,
instead of waiting for the next one. Measured with Quick Race on `-Rov`, three runs
interleaved for each setting: the same average fps (52.5 against 52.7), frame p95 of
23 ms instead of 33 ms, frame p99 of 26 ms instead of 35 ms, and unchanged game speed.

Scenarios that render at 30 fps (post-race and photo mode) keep
the fixed grid either way. Their render thread waits for the vblanks itself. With
`vblank_vrr`, one long frame left every later vblank forced or late: the career replay
at `-ResolutionScale 2` stayed at 23 fps instead of 30. The countdown and replays, at the
race rate since 20 October 2026, keep `vblank_vrr`: at 2x the replay recovered at once from
a 165 ms hitch and held 59.2 fps by default.

#### -FullRateMirror

Renders the rear-view mirror in every frame of every race. By default every race renders
it every other frame, as the title does for 12- and 16-car races
(`HalfRateMirror` in the race render scenarios). One- and eight-car races render it in
every frame. Measured with Quick Race (12 cars) and the mirror in every frame, before the
upload change described in [Render path and frame pacing](#render-path-and-frame-pacing):
55.6 fps instead of 59.7, and GPU 9.5 ms instead of 8.1 ms a frame. At `-FrameRate 120`:
75 fps instead of 84.

#### -ShadowWorkaround

Adds `SkipShadowMapUnlessCockpit` to the race render scenarios (`fm4-render-overrides.cpp`),
as the title's split screen does. The shadow map, a depth pass of 70 to 160 draws
resolved to 512x512 plus a 16_16 4x MSAA pass, is then rendered with the cockpit camera
only. Without it, the hood, bumper and chase cameras also get the shadow map. Their car
shadows are sharp, with the wheels apart instead of soft, at a cost of about 0.2 to 0.4
ms of GPU time a frame. Xenia needed this workaround against square shadows. Captures of
every race camera on this runtime show none.

## Look

The look mods are original shader code hooked into the HLSL that the runtime
generates from the game's post-processing shaders, car shaders (body paint,
glass, chrome, rims and the cockpit interior) and track shaders (sky, trees, bushes, grass and the road surface;
`work/fm4-rex/look`; see [Shader overrides](e2e/experiments.md)). `-Look` takes a
preset, knobs, or both. A preset sets only its own knobs. Later items win, so
`-Look "pc,sharpen=0.6"` uses the `pc` preset with sharpening at 0.6.

| Preset | Settings |
| --- | --- |
| `pc` (default) | `tonemap=aces`, `saturation=1.1`, `bloom=0.7`, `sharpen=0.4`, `clearcoat=1`, `clearcoat_glint=1`, `ao=0.6`, `sun=2`, `sky=0.7`, `taa=1`, `cockpit_light=0.5`, `windshield=1`, `road_specular=0`, `road_sheen=0.4`, `glass=1`, `chrome=1`, `rims=1` |
| `aces` | `tonemap=aces`, `clearcoat=1`, `ao=0.6`, `sun=2` |
| `gt` | `tonemap=gt`, `clearcoat=1`, `ao=0.6`, `sun=2` |
| `neutral` | `tonemap=neutral`, `clearcoat=1`, `ao=0.6`, `sun=2` |
| `original` | No mods: the game's own look. Clears the knobs given before it in the same value. |

Knobs that are not set keep the original look. Examples: `-Look "aces,clearcoat=0,ao=0"`
and `-Look original`.

| Knob | Values and default | Effect |
| --- | --- | --- |
| `tonemap` | `filmic`, `gt`, `aces`, `neutral` | Tone curve. |
| `sharpen` | number | Sharpening. |
| `bloom` | number | Bloom. |
| `exposure` | number | Exposure. |
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
| `cockpit_light` | number, 0 to 1 | The least share of the sun's direct light that the cockpit interior and the driver keep in the car's own shadow (`fm4-cockpit.hlsli`), in every car shader set: it raises the floor of their shadow term, which the game sets to 0. In the sun the shaded gauges, cluster and gloves become readable while the exposure, the windows and the bloom stay as they are. Faces turned away from the sun and the black dashboard top and wheel rim stay dark. See [cockpit brightness](analysis/cockpit-brightness.md). |
| `ao` | number, `0` turns it off | Strength of the screen-space ambient occlusion. |
| `taa` | `1` on (in `pc`), `0` off (the default of the other presets) | Temporal anti-aliasing before the HUD, with the scene jittered while the camera moves; the look's `sharpen` moves after it. See [Temporal anti-aliasing](#temporal-anti-aliasing). |
| `taa_point` | `0` (default) or `1` | With `taa`: `1` runs TAA at the scene_color point, before the game's post chain, instead of before the HUD; the look's `sharpen` stays in the final combine. See [Temporal anti-aliasing](#temporal-anti-aliasing). |
| `windshield` | number, 0 (off) to 2; 1 in `pc` | Glass on the windshield and side windows of the race cockpit camera, which the game leaves open: a light tint, a faint reflection of the dashboard, and glare and dust toward bright light. See [Cockpit glass](#cockpit-glass). |
| `sun` | number, `0` turns the glare off | Strength of the sun glare on the RTV path, the same at every MSAA level. Without it (`original`) the glare follows the counted samples. See [Sun glare](#sun-glare). |
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
`pc` sets `cockpit_light=0.5` and `windshield=1` since 20 October 2026; `-Look "pc,cockpit_light=0,windshield=0"`
is the `pc` look before them. In an A/B at 2x on the Bernese Alps on top of `pc` (nine frozen cockpit
frames), `cockpit_light=0.5` took the gauges from 1 to 4 of 255 to 37 to 62 and the gloves from 2 to 8
to 7 to 48 in four of the frames; in the other five the shaded faces were turned away from the sun
and changed by less than 4. The windows and the other cameras stay as they
were (see [cockpit brightness](analysis/cockpit-brightness.md#the-cockpit_light-knob-19-october-2026)).

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

The `pc` preset sets `sky=0.7`; `aces`, `gt` and `neutral` keep the game's sky, and no preset
sets `road_gloss`. `-Look "pc,sky=1"` restores the sky of `pc` before 18 October 2026. In 2x
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
shimmering. The HUD and the side mirrors are drawn after it and stay sharp. The `pc` preset
sets it since 19 October 2026, with the `high` tiling; `-Look "pc,taa=0"` is the `pc` look
before it (the look's sharpening, no jitter).

- The scene draws are jittered only while the camera moves and the frames come at 45 fps or
  more (since 20 October 2026): rewind, pause, photo mode, a still camera and the 30 fps
  post-race get a still picture instead of a shimmering one; replays and the countdown, at the
  race frame rate since 20 October 2026, are jittered while their camera moves. The pass
  params `TAA_JITTER_MOTION` (0.5 host pixels per frame) and `TAA_JITTER_RATE` (45) set the
  thresholds and change live with `-ShaderDev`; both at 0 jitter every frame
  ([TAA, arming](analysis/taa.md#35-arming-and-switching)).
- The look's `sharpen` (0.4 in `pc`) is applied after TAA instead of before it (the pass param
  `TAA_SHARPEN`), since sharpening the jittered frame would sharpen the jitter. TAA without
  sharpening is visibly softer; 0.4 comes close to `pc` without its stair steps.
- With `-SingleTile` the swap's FXAA is left out: TAA anti-aliases the single tile itself.
- It costs about 0.1 ms of GPU time per race frame at 1x and 0.15 to 0.2 ms at 2x.
- `shader_mod_set TAA=0` switches it off at once with `-ShaderDev`; `TAA_DEBUG=1` to `5` shows
  the debug views ([TAA, debug views](analysis/taa.md#76-debug-views-taa_debug)).
- The `-Info` overlay shows "Anti-aliasing: TAA before the HUD, sharpen 0.4, scene jittered
  while the camera moves".
- RTV only, like the other post passes; not with `-Rov` or `-Look original`.
- `taa_point=1` (`-Look "pc,taa_point=1"`) moves TAA to the scene_color point: it runs on the scene before the game's motion blur, bloom, exposure and final combine, and its output is written back into the game's scene, which those then sample. The occlusion gets its own temporal filter there (`GTAO_TEMPORAL`), and the look's `sharpen` stays in the final combine, after TAA. It costs about 0.47 ms of GPU time per race frame at 2x, and the `-Info` overlay shows "Anti-aliasing: TAA before the post chain (scene_color), the look's sharpening, scene jittered while the camera moves". `pc` keeps the final point; see [TAA, the comparison](analysis/taa.md#23-comparison).

### Cockpit glass

In the race cockpit camera FM4 draws no glass for the player's car, so the windshield and side
windows look like open frames ([cockpit brightness](analysis/cockpit-brightness.md#windshield-and-side-windows)).
`windshield=1` (in `pc` since 20 October 2026; `-Look "pc,windshield=0"` turns it off) adds a post pass that puts glass over the pixels that
see the world through the cockpit's windows ([windshield](analysis/windshield.md)): a light
green-gray tint, a reflection of the dashboard (faint, since FM4's dashboards are dark), and a
haze with dust and wiper arcs where bright sky or sun shows through. `windshield=2` doubles
every effect.

- Only frames of the cockpit camera change. The chase, hood and bumper cameras, the 3-2-1
  camera, the HUD, the side mirrors and the interior mirror stay as they are.
- It runs before the HUD and after TAA. The `-Info` overlay shows "Cockpit glass: strength 1".
- RTV only, like the other post passes; not with `-Rov`.
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
  of `fm4-look.hlsli`, `fm4-paint.hlsli`, `fm4-car.hlsli`, `fm4-nature.hlsli` and `fm4-road.hlsli`; `FM4_LOOK_TONEMAP` takes 0 to 3 (`filmic`,
  `gt`, `aces`, `neutral`).
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

## Controls

The controller uses the SDL backend. The keyboard mapping is the launcher's default,
set by `launch_cvars` in [pipeline.py](../outputs/FM4-build/pipeline.py).

| Key | Function |
| --- | --- |
| `W` / `S` | Gas / brake |
| `A` / `D` | Steer left / right |
| `Space` / `Backspace` | Controller A / B |
| `Enter` / `Tab` | Start / Back |
| Arrow keys | D-pad |
| `Alt+Enter` | Switch between borderless fullscreen and the window (`bind_fullscreen`) |
| `F2` | Show or hide the settings overlay |

The keyboard controller ignores keys pressed with Alt, so Alt+Enter does not also
press Start.

## Shader cache

`-Action warmcache` runs the e2e Quick Race timeline once, without a network connection,
so that the shader and pipeline cache under `BuildRoot\cache` is filled before you play.
Later runs then skip those compilations. The compiled look mods are kept in
`BuildRoot\cache\shader-overrides`, one entry per shader, look and render scale; the
directory can be deleted at any time. The run details are written to
`BuildRoot\e2e\warmcache`, and the log to `BuildRoot\logs\warmcache.log`.

```powershell
.\outputs\FM4-build\Build.ps1 -Action warmcache @common
```

## Data directories

Everything the launcher writes is under `BuildRoot`. The full layout is in
[BuildRoot layout](setup.md#buildroot-layout).

- **Profile:** each run uses a new local profile in `BuildRoot\user-data`. Saves from
  earlier builds are not copied into it automatically.
- **Disc 2 and downloadable content:** imported packages are stored in the profile
  under `BuildRoot\user-data\0000000000000000\4D530910\00000002`, one directory per
  package, with a `<package>.import-complete` marker in `BuildRoot\user-data`. They
  stay installed when a later run no longer lists them. To remove one, delete its
  directory, its marker and its `.header` file in `4D530910\Headers\00000002`.
- **Discs:** the discs are read in place. Writes relative to the game directory are
  disabled (`allow_game_relative_writes=false`).
- **Cache:** game and shader cache files are in `BuildRoot\cache`.
- **Logs:** the launcher log is `BuildRoot\logs\play.log`, and the runtime log is
  `BuildRoot\logs\runtime.log`.
- **e2e output:** captures, logs and `summary.json` of e2e runs are in
  `BuildRoot\e2e`. The captures show the game, so keep them out of the repository.
  See [e2e/README.md](e2e/README.md).

Nothing is uploaded by `run`. Never commit or publish `BuildRoot`.
