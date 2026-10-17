# Look Mod Coverage (17 October 2026)

Look mods are keyed by the ucode hash of a pixel shader. A mode or track that draws the same role with a different ucode gets the original shader without any warning. This note lists, per track and per mode, which final combine, motion blur and car paint hashes occur, and the mods added on 17 October 2026 for the ones that had none. It contains hashes, technique names and constant names only, no shader code.

## Method

```
outputs/FM4-build/e2e/look_coverage.py --build-root <BuildRoot> [--dump <dump_shaders dir> ...] [--json <BuildRoot>\atlas\look-coverage.json]
```

([look_coverage.py](../../outputs/FM4-build/e2e/look_coverage.py))

- **Hash**: the runtime keys shaders by XXH3_64 of the big-endian ucode (`PipelineCache::LoadShader`). The script computes it offline with a Python XXH3 that reads the secret from the runtime's xxHash header. It matches xxHash on 1,104 random lengths and on all 260 containers of default.xex, and it finds the six modded hashes and `E5B1A71249D0CA9A` in the expected containers.
- **Sources**: the same containers as the [material atlas](material-atlas.md): 171 `.fxobj` files in `Media\Shaders`, the 32 track archives (`Media\tracks\<track>\bin.zip`, LZX through the runtime), the shader files in the DLC archives of the BuildRoot user data (the Launch Track Pack: `PhysicsTrack` and `Soccer`, 82 files), and default.xex. 1,076 unique pixel shaders.
- **Roles**, from the constant names: final combine (`filmicParams1`; the variant from `vignetteColor`, `dofFocalParams` and `watermarkLayout`), motion blur (`invCurrentVPThenPrevVP`), and the paint family (`PaintColor`, `StripeColor` and `TwoToneColor`), plus the five custom paint techniques.
- **Modes**: each file in `Media\RenderScenarios` names the car shader set it loads (`CarShader`; the default set `shaders_v16` when absent).
- **Runtime evidence**: the shader storage (`cache\shaders\shareable\4D530910.xsh`) of a BuildRoot holds every shader the runtime has loaded in that BuildRoot, across all its runs. Read for `I:\FM4-build2` (photo, photo view, career, career finish, Quick Race, DLC runs; 432 pixel shaders) and `I:\FM4-build-gtao` (cockpit, camera cycle, garage and showroom from GTAO M1; 294 pixel shaders). It shows that a hash ran, not in which mode.

## Tracks

No track archive contains a final combine, motion blur or car paint shader: 0 of the 32 disc track archives (24 track locations, six HomeSpace scenes, `UIThumBig`, `UIThumbnail`) and 0 of the two DLC track archives. These roles come only from default.xex and `Media\Shaders\Cars`. `TrackSettings.xml` changes their constants (`filmicTone`, `bloomScale`, `motionBlurAmount`, `dofFocal*`, cockpit exposure), not their shaders, and the mods read those constants. Coverage per track is therefore the coverage of the mode.

## Coverage per Mode

All hashes below have a mod in [work/fm4-rex/look](../../work/fm4-rex/look/) since 17 October 2026. "Added" marks the mods written that day from the offline analysis below. The runs of the same day that show which of them draw on screen, and how, are in [In-Game Checks](#in-game-checks-17-october-2026).

| Mode (render scenarios) | Car shader set | Body paint | Final combine | Motion blur |
|---|---|---|---|---|
| Race with 1, 8 or 12 cars, pre-race, 3-2-1 and post-race; Autocross, Bowling, Soccer, Multiplayer, split-screen 3-2-1 and post-race | `shaders_v16` | `B85EF04779ABD438`; `892947FF214FF4E8` added | `E97AA211B556A9B3`, `9CDA139F232A8A73` | `4746CAD32EF168CF` |
| Cockpit (same scenarios) | `shaders_v16` | as above | as above | as above |
| Replay (including the 16-car replay), photo mode | `shaders_v16` | as above | `30B67D8B86C37587`, `AE3B2E36FCA4958D` | `4746CAD32EF168CF`; photo variants added |
| 16-car race, pre-race, 3-2-1 and post-race | `shaders_16car_v16` | `4A15688ECE9E3657`, `E22EA1B051DFFD0C` added | as race | as race |
| Split-screen race | `shaders_splitscreen_v16` | `6DE1FEEA7DA50AC2`, `38F5DB57752FC2D9` added | as race | as race |
| Garage, car menus, HomeSpace (`UI_Car`, `UI_NoCar`, `UI_Homespace`) | `shaders_ui_v16` | `134B2725B75271DC` (ran), `5AFF2866B278F96D` added | `30B67D8B86C37587` | none seen |
| Showroom (`UI_Showroom`) | `shaders_slod_v16` | `9ED0E8D2A0AC30E0` (ran) added | `30B67D8B86C37587` | none seen |
| Saved photo or watermark (when the title sets a watermark texture) | | | four `_Watermark` variants added | |
| Not identified | | | `2F1766979C52704E` (`FinalCombine_NoPostEffects`, ran) added | |

- The body paint of each set is the first pixel shader in its container, as `B85EF04779ABD438` is in `shaders_v16`, and it is also the closest by constant names. Each set has a second, almost identical body paint technique.
- Final combine per mode comes from the [B4 research](b4-shader-research.md) and [GTAO M1](gtao-design.md) (garage and showroom draw `30B67D8B86C37587`). The cockpit has no shader variant of its own: its materials are other techniques of `shaders_v16`, and the GTAO BuildRoot, which includes the cockpit runs, loaded no final combine or motion blur hash without a mod.
- The paint techniques `C6A382D1784CEFCA` and `71C34CF563C167CA` of `shaders_v16` also ran (the false-color check found body color only on `B85EF04779ABD438`). Which car parts they draw is not identified.
- Technique names in default.xex sit in the same order as their containers. For the eight `FinalCombine_BloomTonemap*` variants this order matches the constant tables exactly. By the same order the four motion blur shaders are `PPMotionBlurDepthPhoto` (`8B1A70675335075E`), `...PhotoFast` (`560F69951C6D73D1`), `...PhotoComplete` (`1CA7A58B97F95E21`) and `...Replay` (`4746CAD32EF168CF`). This is not confirmed: `4746CAD32EF168CF` also ran in Quick Race and in photo mode.
- The watermark final combines and the three other motion blur shaders did not run in any BuildRoot, including runs that saved a photo and opened it again.

## Mods Added (17 October 2026)

Each shader's inputs were read without the game: the fetch instructions of its ucode (fetch constant and source register), and the constants that the first reads of each input register use. These were compared with `B85EF04779ABD438`, whose layout is known: interpolator 0 XY the paint UV, 1 the world normal, 2 the world position, 3 the shadow map coordinate, 4 the lightmap UV. Literal constants come from the container. They sit in the top registers and differ per shader: `B85EF04779ABD438` has the cube offset 1.5 in c255.x and the minimum shadow variance 0.001 in c254.w, while the UI shaders hold them in other registers. The new mods therefore use the numbers. `k` is the register of `filmicParams1` and `s` that of `colorGradingTexScale`, as in the tone curve table of the B4 research.

| Hash | Role | Mod | Basis |
|---|---|---|---|
| `B85EF04779ABD438`, `892947FF214FF4E8` | body paint, `shaders_v16` | `fm4_body_paint_env_cube` (moved from the B85E mod into `fm4-paint.hlsli`) with c254.w, c255.x and bool 128 | identical constant tables, literals and fetch layout |
| `134B2725B75271DC`, `5AFF2866B278F96D` | body paint, `shaders_ui_v16` | `fm4_body_paint_env_cube` with 0.001, 1.5 and the self shadow always on; the environment cubes decoded with `fm4_decode_env` (literals c251.w, c254.w, c251.x, c253.w; rows c253 to c255) | the same registers and fetch layout (shadow map from interpolator 3, lightmap from 4); no `doselfshadow` bool, the shadow map is always read. Unlike `B85EF04779ABD438`, the shader decodes encoded HDR cubes when bool 138 (dynamic cube) or 141 (static cube) is set |
| `4A15688ECE9E3657`, `E22EA1B051DFFD0C` | body paint, `shaders_16car_v16` | `fm4_body_paint_static_cube`: static cube in fetch 3, lightmap shadow only, `PaintColor` c82 | no dynamic cube and no shadow map; inputs 0-2 read as in B85E; the lightmap is read from register 4, and that it is still the interpolator is inferred |
| `6DE1FEEA7DA50AC2`, `38F5DB57752FC2D9` | body paint, `shaders_splitscreen_v16` | as above, with the static cube in fetch 4 | as above |
| `9ED0E8D2A0AC30E0` | body paint, `shaders_slod_v16` | only the coat's reflection of the environment cube (fetch 2, or the static fetch 3 with bool 130) at the cube offset c255.x, decoded with `fm4_decode_env` (literals c250.z, c253.w, c250.w, c252.w; rows c251, c252, c254); `PaintColor` c80; no sun highlight and no flakes | no sun direction and no lightmap in its constants; normal and position read as in B85E (inferred). The ucode decodes both cubes when bool 138 or 141 is set; fetch 1, the specular cube, is a scalar intensity, not a color |
| `26C973629F724A97` | `FinalCombine_BloomTonemap_Watermark` | as `E97AA211B556A9B3`, k 6, s 1 | scene (fetch 0) and bloom (fetch 2) from interpolator 0 and interpolator 1 read by ALU, as in the base variant; the watermark (fetch 3) is left as it is |
| `B606D6FA7A74981C` | `..._Vignette_Watermark` | as `9CDA139F232A8A73`, k 9, s 4, `vignetteColor` c2 | as above |
| `190CE76994929E32` | `..._DOF_Watermark` | as `30B67D8B86C37587`, k 20, s 3 | as above |
| `9186E11FABCF0163` | `..._DOF_Vignette_Watermark` | as `AE3B2E36FCA4958D`, k 23, s 6, `vignetteColor` c4 | as above |
| `2F1766979C52704E` | `FinalCombine_NoPostEffects` | `fm4_tonemap_output`: only the `tonemap` knob, applied to its output | it writes F(x) / F(white) linear (exposure from interpolator 1 W, F at white in Z, filmic in c0 and c1), with no bloom, LUT or gamma |
| `560F69951C6D73D1` | motion blur, fixed taps | `fm4_motion_blur`, as `4746CAD32EF168CF` | same constants; interpolator 0 is the pixel's coordinate (the depth is fetched at it) |
| `8B1A70675335075E`, `1CA7A58B97F95E21` | motion blur, sample loop | `fm4_motion_blur` on the output | interpolator 0 is the pixel's coordinate (depth and scene are fetched at it); at the default `motion_blur` 1 the blur the player sets in photo mode is unchanged |

Left without a mod, as proposed:

- `E5B1A71249D0CA9A` and its counterparts `7FDDAE0F0B88E28A` (UI), `1EFF1DFC0C5E5908` (16 cars) and `8603C675CB676DDD` (split screen). They have no environment samplers and no `PaintColor`. The offline guess was the car body in the environment cube render; the in-game check below shows instead that `E5B1A71249D0CA9A` draws the car lights and trim on screen (tail and brake lamp lenses, the window surrounds, the mirror housings). It is not body paint, so it stays without a clear coat.
- The custom paints `B79742D49E97B933` (Bentley), `B2B8852A04011EE4` (DeLorean), `E9D3D18F774121A1` (Reventon), `DCAE7A8A55FB872D` (Sagaris) and `DA3ACE8C256784B2` (matte). `CustomCarShaders.xml` loads them in UI scenes only (`uiload="true" inraceload="false"`). The matte paint should stay without a coat.

Verification without the game: `fm4_shader_mod_tests` compiles every mod under its synthetic contract, with and without the occlusion view, at the default knobs, with every look knob away from the original, and with each occlusion knob. All checks pass (388 after the live-knob checks of the shader mod iteration were merged). `override_fxc.py --mods` has not run against the real contracts dumped by the runs below.

## In-Game Checks (17 October 2026)

BuildRoot `I:\FM4-build-iter` (runtime with the shader mod iteration), 240 Hz, the AI driver, `--profile copy`, look `pc` unless named otherwise. A false-color look directory in the BuildRoot (not in the repository) tints the output of each paint hash with its own color. The 16-car and split-screen car shader sets are loaded in a normal Quick Race through `FM4_RACE_SETTINGS` (`CarShader=shaders_16car` or `CarShader=shaders_splitscreen`), because the `high` race quality removes `CarShader` and a real split-screen race needs a second controller. `dump_shaders` contracts do not show what a run drew, because the pipeline storage of the BuildRoot translates every shader it has seen before at startup.

| Check | Result |
|---|---|
| Race paint, Quick Race with a camera cycle (Bernese Alps, cockpit, hood, bumper, chase), and a career race with its replay (Tsukuba) | Every car body is `B85EF04779ABD438`, also in the mirrors and the replay. `892947FF214FF4E8` never showed. |
| Car menus and HomeSpace | The paint select screen and the career home menu draw `134B2725B75271DC`. `5AFF2866B278F96D` never showed. |
| Showroom | Autovista is the `UI_Showroom` scene: its car draws `9ED0E8D2A0AC30E0`. |
| 16-car set | Car bodies draw `4A15688ECE9E3657`. `E22EA1B051DFFD0C` never showed. |
| Split-screen set | Car bodies draw `6DE1FEEA7DA50AC2`. `38F5DB57752FC2D9` never showed. |
| `E5B1A71249D0CA9A` | Tinted alone at 2x, it shows on the tail and brake lamp lenses, the window surrounds, the roof rails and the mirror housings; the painted body and hood keep their color. These are the car lights of the [material atlas](material-atlas.md) (3.3% of draws). Its ucode blends two textures (fetch 0 and fetch 1 with a register LOD, plausibly the lamp off and on) by a factor from constants, lights them with the lightmap and the shadow map, and writes the constant alpha `c57.x`. Interpolators 0 to 5 are laid out as in `B85EF04779ABD438`. No clear coat is planned for it. |
| A/B with `clearcoat=1` at 2x (2560x1440), `shader_override_ab_ms=4000` | 16-car and split-screen sets: the coat adds the static cube's reflection, which lines up with the original's (16 cars) or adds one where the original shows almost none (split screen). That cube is coarse, so the reflection looks blocky at 2x. Garage and showroom: the first version turned the body green at grazing angles, because it read the environment cubes raw while these shaders store them as encoded HDR (and the showroom mod used the scalar specular cube). With `fm4_decode_env` the coat adds a clean reflection of the scene or the studio. |
| Default knobs (`--look clearcoat=0`), `shader_override_ab_ms=2000`, showroom | 16 of 16 on/off pairs bit-identical. The HomeSpace camera moves, so its pairs cannot be compared. |
| Photo mode | Y opens EFFECTS MODE; its first item, SHUTTER SPEED (default 50), sets the photo's motion blur. A photo at shutter speed 63, rendered and saved, drew `4746CAD32EF168CF` and no other motion blur shader. |

Still not seen in any run: `892947FF214FF4E8`, `5AFF2866B278F96D`, `E22EA1B051DFFD0C`, `38F5DB57752FC2D9`, the four watermark final combines, `2F1766979C52704E` and the motion blur shaders `8B1A70675335075E`, `560F69951C6D73D1` and `1CA7A58B97F95E21`. Their mods remain checked by compilation only. Not run: a real split-screen race with two controllers, a real 16-car race at `-Quality xbox`, photo mode at the highest shutter speed, and `motion_blur` 0.5 against 1 on a photo.

## Related

- [Material atlas](material-atlas.md): classes, samplers and registers of the car paint.
- [B4 shader research](b4-shader-research.md): the mod hooks and the final combine formula.
- [GTAO design](gtao-design.md): AO scope in the cockpit, garage and showroom.
