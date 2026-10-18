# Look Mod Coverage (17 October 2026)

Look mods are keyed by the ucode hash of a pixel shader. A mode or track that draws the same role with a different ucode gets the original shader without any warning. This note lists, per track and per mode, which final combine, motion blur and car paint hashes occur, and the mods added on 17 October 2026 for the ones that had none. The car glass, chrome and rims, which got mods on 18 October 2026, are in [their own section](#glass-chrome-and-rims-18-october-2026), and so are the sky and vegetation of the tracks ([Sky and Vegetation](#sky-and-vegetation-18-october-2026)) and the road surface ([Road Surface](#road-surface-18-october-2026)). It contains hashes, technique names and constant names only, no shader code.

## Method

```
outputs/FM4-build/e2e/look_coverage.py --build-root <BuildRoot> [--dump <dump_shaders dir> ...] [--json <BuildRoot>\atlas\look-coverage.json]
```

([look_coverage.py](../../outputs/FM4-build/e2e/look_coverage.py))

- **Hash**: the runtime keys shaders by XXH3_64 of the big-endian ucode (`PipelineCache::LoadShader`). The script computes it offline with a Python XXH3 that reads the secret from the runtime's xxHash header. It matches xxHash on 1,104 random lengths and on all 260 containers of default.xex, and it finds the six modded hashes and `E5B1A71249D0CA9A` in the expected containers.
- **Sources**: the same containers as the [material atlas](material-atlas.md): 171 `.fxobj` files in `Media\Shaders`, the 32 track archives (`Media\tracks\<track>\bin.zip`, LZX through the runtime), the shader files in the DLC archives of the BuildRoot user data (the Launch Track Pack: `PhysicsTrack` and `Soccer`, 82 files), and default.xex. 1,076 unique pixel shaders.
- **Roles**, from the constant names: final combine (`filmicParams1`; the variant from `vignetteColor`, `dofFocalParams` and `watermarkLayout`), motion blur (`invCurrentVPThenPrevVP`), and the paint family (`PaintColor`, `StripeColor` and `TwoToneColor`), plus the five custom paint techniques. Since 18 October 2026 also chrome, glass and rim (see [their section](#glass-chrome-and-rims-18-october-2026)).
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

Verification without the game: `fm4_shader_mod_tests` compiles every mod under its synthetic contract, with and without the occlusion view, at the default knobs, with every look knob away from the original, and with each occlusion knob. All checks passed on 17 October 2026 (388 after the live-knob checks of the shader mod iteration were merged). On 18 October 2026 these mods were also compiled against the real contracts of the runs below, with `override_fxc.py --stub` ([results](#verification-without-the-game)).

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

Follow-up runs on 18 October 2026 (same BuildRoot and settings), with `REX_GPU_INVENTORY` as the record of what drew:

| Check | Result |
|---|---|
| 16-car render scenario at `-Quality xbox` | Free Play Quick Race has no grid size option, and its grids are 8 or 12 cars, so the 16-car scenarios of the title (`16-Car_PreRace`, `16-Car_Race`, `16-Car_321_and_PostRace`) were reproduced on a 12-car Quick Race: `--quality xbox` (the title's own settings) plus `FM4_RACE_SETTINGS` with exactly the elements that differ from the 12-car files (`CarShader=shaders_16car`, `CarDamage=0`, `CarDamageTextures=0`, `CrowdLoad=0`, `EnvMapFrequencyScale=0.0`, `HalfRateBloom=1`, `CollidablesInMirror=0`). Car bodies drew `4A15688ECE9E3657` (140,210 draws in 4,274 frames); `E22EA1B051DFFD0C` never drew and was never translated. The final combines and `4746CAD32EF168CF` are the race's. No clear-gray frames. |
| Photo mode at the shutter speed limits | Holding right or left on SHUTTER SPEED stops at 100 and at 0. A photo at 100 and a photo at 0, each rendered and saved (the inventory covers 12,000 of the run's 12,134 and 12,094 frames, including the photo render), drew `4746CAD32EF168CF` only, with the same draw count as a race without photo mode (258 to 270). `8B1A70675335075E`, `560F69951C6D73D1` and `1CA7A58B97F95E21` never drew and were never translated. |
| Photo mode from a replay | The career race (Tsukuba) is quit into its replay; the camera icon of the replay toolbar (TAKE PHOTO, the ninth icon) opens the same photo mode. A photo at shutter speed 100, rendered and saved, again drew `4746CAD32EF168CF` only, and the three photo variants were never translated. The replay draws `4746CAD32EF168CF` much more often: 2,809 frames in a career run with 40 s of racing and 30 s of replay, against 258 to 270 frames in the Quick Race runs without a replay, which fits its reading as `PPMotionBlurDepthReplay`. |

Still not seen in any run: `892947FF214FF4E8`, `5AFF2866B278F96D`, `E22EA1B051DFFD0C`, `38F5DB57752FC2D9`, the four watermark final combines, `2F1766979C52704E` and the motion blur shaders `8B1A70675335075E`, `560F69951C6D73D1` and `1CA7A58B97F95E21`. Their mods remain checked by compilation only. The photo motion blur variants do not draw in a photo taken during a race at shutter speed 0, 63 or 100, nor in a photo taken from a replay at 100. Not run: a real split-screen race with two controllers, a race with a real 16-car grid (only the 16-car render scenarios were reproduced, on 12 cars), and `motion_blur` 0.5 against 1 on a photo (no photo variant draws to compare).

## Glass, Chrome and Rims (18 October 2026)

Car paint was the only material with a mod. The glass, chrome and rim shaders of the five car shader sets now have mods too: [fm4-car.hlsli](../../work/fm4-rex/look/fm4-car.hlsli) and one `shader_<hash>.mod.hlsli` per hash, with the knobs `glass`, `chrome` and `rims` ([running.md](../running.md)). They were checked offline and then in the game: false color and an A/B at 2x in a race, in HomeSpace and with the 16-car set, and a smoke gate ([In-Game Checks](#in-game-checks-18-october-2026)).

### Roles

`look_coverage.py` has three more roles, from the constant names:

- **chrome**: a `Chrome*` constant and no `Cockpit*` one. The variant is rim with `blurRimGeomTexBlend` (the geometry rim, whose alpha fades out as the blurred wheel of a spinning rim fades in), trim otherwise, and spinning blur with `blurRimSampler1`.
- **glass**: `GlassColor` or `DamageGlassSampler`. The variant is lamp cover with `LightsSampler` (glass over a lights texture), window otherwise.
- **rim**: `TintColor` and `blurRimGeomTexBlend`; geometry or spinning blur.

Not in these roles: the lamp lenses (head, tail, brake and reverse lights: `GlassMaxOpacity` without `GlassColor`, the "car lights" of the [material atlas](material-atlas.md)), the cockpit chrome (`ChromeAmbientScale` with `CockpitStaticCubeMapSampler`), and the 145 material shaders of `CustomCarShaders.xml` (`chrome`, `rim_chrome`, `lights_glass` and the others). Every one of those 145 is `uiload="true" inraceload="false"`, like the custom paints, and none has a mod.

### Coverage per Car Shader Set

Hashes with a mod, and the ones left without, per set (the 16-car and split-screen sets share their chrome and rim shaders):

| Car shader set | Glass | Chrome | Rims |
|---|---|---|---|
| `shaders_v16` (race, replay, photo) | windows `E62CFDF09D758B6C`; `82C947AB2E156A6A` (no glass color or opacity, its alpha is the fade alone); lamp covers `AD10B610621DEFE5`, `00AF2783DCAB9C75`. Without: `6F36125B9CA1EFC4` | trim `AF6AA26032BC613D`; rims `F8CDBAFAD23FD784`, `4CC589D8F9554881`. Without: blur `71D965254A685B9E`; `5041DE5E586B9F3B`, `F1948F8CB9236067` | `36DFBBD4669AF5E9`, `75B334519A1568F6`, `0412EE51F7B00389`. Without: blur `10BE203F234535A7` |
| `shaders_16car_v16` | windows `228F44353D897139`; lamp covers `075A39C371224577`, `EC8CC6E764252B8A`. Without: `593DE871390B661D` | trim `C6A76669106153D9`; rims `A0A849C534AE715A`, `B6BA81437B77FDF4`. Without: blur `E8EC40DC79F22B62`; `2045A6483F67B2B2`, `6F41BD14D3587427` | `E1DC29154298F7F8`, `039EAAF6D690B619`, `76A83E90F52E0739`. Without: blur `06E8D763F02B1463` |
| `shaders_splitscreen_v16` | windows `F0BC67CC35A4DC6B`; lamp covers `DE909751EF928890`, `BA6FE2E0F5DAE8F7`. Without: `6F36125B9CA1EFC4`, `DEA935DF60211BC8` | as the 16-car set | as the 16-car set |
| `shaders_ui_v16` (garage, car menus, HomeSpace) | windows `8B983E466B264D2A`; lamp covers `DA5D7A7C839A64C1`, `A6D8049525F6D88A`. Without: `593DE871390B661D` | trim `C2296ADC601D1072`; rims `5F76CA7F8910C7EC`, `2178DB71F6E701EC`. Without: blur `BF2852F1DC60E55A`; `D1ACA733E42F4EB8`, `CEFB393179FEEEED` | `806961C873069679`, `FF6CFFF501187440`, `C1F9FF2D0BB6E707`. Without: blur `E4946F30032BD511` |
| `shaders_slod_v16` (showroom) | none of 5 | none of 8 | none of 4 |

31 of the 62 hashes of these roles have a mod: 13 glass, 9 chrome and 9 rims. The others are left without, for these reasons:

- **Spinning-wheel blur** (`71D965254A685B9E`, `10BE203F234535A7`, `E8EC40DC79F22B62`, `06E8D763F02B1463`, `BF2852F1DC60E55A`, `E4946F30032BD511`): they draw the blurred textures of a spinning wheel (`blurRimSampler1` to `4`) and fade in as the geometry rim fades out. A sharp highlight would stand still on a wheel that they show spinning, so they keep the game's look.
- **Never translated in any run**, so their inputs were not read: the chrome with a normal map (`ChromeNormalSampler` or `BumpScale1`: `5041DE5E586B9F3B`, `F1948F8CB9236067`, `2045A6483F67B2B2`, `6F41BD14D3587427`, `D1ACA733E42F4EB8`, `CEFB393179FEEEED`) and the split-screen glass `DEA935DF60211BC8`. Two hashes never translated do have a mod, because their ucode is a translated one's except for the last instructions, which write the alpha: `B6BA81437B77FDF4` (1 of 144 dwords differ from `C6A76669106153D9`) and `2178DB71F6E701EC` (1 of 384 from `C2296ADC601D1072`).
- **No environment**: `6F36125B9CA1EFC4` and `593DE871390B661D` (30 and 36 dwords) read no normal, position or cube, only a lights texture and `GlassColor`.
- **Showroom** (`shaders_slod_v16`): of its 17 glass, chrome and rim hashes only `7226D929481D4274` (a window with a windshield reflection matrix and its own reflection texture) and `12D649304EAB2DA8` (chrome with an engine normal map) were ever translated, and neither appears in a draw inventory. Left for a run that shows which of them the showroom draws.

### Inputs

Read from the ucode dumps of the runs below (`dump_shaders`, `I:\FM4-build-iter\e2e\cov-*-dump`) and from the containers, compared with `B85EF04779ABD438`:

- **Interpolators**: as the body paint. 1 the world normal, 2 the world position (eye `c59`), 3 the shadow map coordinate, 4 XY the track lightmap UV (fetch 10). The windows of `shaders_v16` and `shaders_ui_v16` take the UV of the 2D background map (fetch 1, bool 133) from 4 ZW. Glass reads its damage or lights textures at interpolator 0.
- **Environment cubes**, gamma 2 (squared):

  | Shaders | Dynamic cube | Static cube (bool 130) | Encoded HDR |
  |---|---|---|---|
  | glass `E62CFDF09D758B6C`, `AD10B610621DEFE5`, `00AF2783DCAB9C75` | fetch 2 | fetch 3 | no |
  | glass `82C947AB2E156A6A`; chrome and rims of `shaders_v16` | fetch 1 | fetch 2 | no |
  | glass `228F44353D897139`, `F0BC67CC35A4DC6B`; chrome and rims of the 16-car and split-screen sets | none | fetch 1 | no |
  | glass `075A39C371224577`, `EC8CC6E764252B8A`, `DE909751EF928890`, `BA6FE2E0F5DAE8F7` | none | fetch 2 | no |
  | glass of `shaders_ui_v16`; rims `806961C873069679` | fetch 2 | fetch 3 | bools 138 and 141 |
  | chrome `C2296ADC601D1072`, `5F76CA7F8910C7EC`, `2178DB71F6E701EC`; rims `FF6CFFF501187440`, `C1F9FF2D0BB6E707` | fetch 1 | fetch 2 | bools 138 and 141 |

  `806961C873069679` also has a specular cube in fetch 1, a scalar intensity. Every one of these shaders holds the cube coordinate offset 1.5 in its literals (in `c253.x` to `c255.w`, depending on the shader). Every shader of `shaders_ui_v16` holds the same encoded HDR decode in its literals: the exponent constants 127, 255 and 0.5, the three matrix rows and the minimum 0, in different registers per shader. The mods use these numbers.
- **Shadow**: the chrome and rims of `shaders_v16` read the variance shadow map (fetch 13) with bool 128, those of `shaders_ui_v16` always, with the minimum variance 0.001 (literal) and the darkening `c80.x` (`ShadowDarkeningRamp`). The 16-car and split-screen sets have no shadow map. Chrome and rims square the lightmap texel before they map it through `c60` (`trackLightMapParams`); the body paint `B85EF04779ABD438` does not square it. The glass shaders have no `c60`: they scale their highlight by the texel as it is.
- **Sun**: chrome and rims read the sun color `c42`. The glass shaders read no sun color; their highlight is `SpecularAmount` (`c80.x`, or `c81.x` on the lamp covers) times `SpecularColor` (`c82`, absent on the lamp covers). A real translation defines only the constants its shader reads, so the glass mods cannot use `c42`.
- **Output**: the square root of the linear color (gamma 2), with the fog `c47` applied before it. The alpha is the car's fade `c57.x`, times the glass opacity on the glass and times `blurRimGeomTexBlend` (`c40.x`) on the geometry rims. The `REX_GPU_INVENTORY` files of these BuildRoots record the blend control `0x01060706` (color: source alpha, add, one minus source alpha) for every draw of the 22 modded hashes that appear in them.

### Mods

- **Glass** (`fm4_glass`): the environment's reflection at grazing angles above the head-on 4% that the game's own reflection shows (Schlick, F0 0.04) and a sharp GGX highlight of the sun (`glass_roughness`, default 0.08), soft limited by `clearcoat_glint`. The alpha stays the game's. The blend scales the output by the alpha on gamma 2 values, so over a dark background the screen gets the alpha squared times the output's linear light: the added light is divided by the square of the glass's opacity (the alpha without `c57.x`, at least 1/8). That is exact over the cabin behind a window and stronger than physical over a bright background. Seen from inside the car (the normal facing away), the glass stays as it is, because the cube shows the outside, not the cabin.
- **Chrome** (`fm4_chrome`): the game gives chrome no highlight of the sun. The mod adds an energy normalized GGX highlight (`chrome_roughness`, default 0.1) with chromium's head-on reflectance 0.6, and the Fresnel rise of the environment above it. Both are in the pixel's shadow, and the reflection keeps a share of 0.35 in shadow, as the clear coat does.
- **Rims** (`fm4_rims`): a clear lacquer over the game's metal, like the body's clear coat without flakes: a GGX highlight (`rims_roughness`, default 0.25) and the Fresnel rise of the environment, F0 0.04.

Every knob defaults to the original look. `fm4-car.hlsli` takes the shared helpers of `fm4-paint.hlsli` (`FM4_PAINT_HELPERS_ONLY` leaves out the body paint functions, which read constants that these shaders lack). The variance shadow of `fm4_paint_shadow` is split out as `fm4_variance_shadow`; the body paint mods compile to the same DXBC as before.

**The `pc` preset**: unchanged. No preset sets `glass`, `chrome` or `rims`. The in-game checks below show them right where they were seen, but not yet a sun highlight on glass or chrome (none of the scenes faced the sun), the split-screen set, or another track and time of day. The glass highlight scales with the game's `SpecularAmount`, whose values have not been read, so a sun-facing scene is the open risk of a bloom blob.

### Verification Without the Game

- `fm4_shader_mod_tests` (53 mods): 1,021 checks pass. A new check requires every mod's DXBC to change when the look knobs move away from the original, so a mod whose code its knobs do not reach fails. With the defines of the default look, the 22 mods that existed before compile to the same DXBC fingerprints as before this change. The look files have 21 knobs of the 32 live knob slots.
- `override_fxc.py --stub` against the real contracts: 55 contracts of 37 modded hashes, taken from 37 dump directories of `I:\FM4-build-iter`, `I:\FM4-build-gtao` and `I:\FM4-build2`. Every mod (paint, post-processing and the new ones) compiles with and without the defines of the `pc` look plus `glass`, `chrome` and `rims` at 1 and `motion_blur` at 0.5, and every mod's DXBC changes with them. This was the first compile of the mods against real contracts. A glass mod that reads `c42` fails on the contract of `AD10B610621DEFE5` with `undeclared identifier 'XE_FLOAT_CONSTANT_42'`, which the synthetic contract of `fm4_shader_mod_tests` defines. No contract was found for 16 mods: the 12 earlier ones that never drew (see [In-Game Checks](#in-game-checks-17-october-2026)) and `2178DB71F6E701EC`, `5F76CA7F8910C7EC`, `B6BA81437B77FDF4` and `C1F9FF2D0BB6E707`.
- Size, in FXC instruction slots with live knobs, on the test's synthetic override (1,396 slots with the smallest mod): glass 1,615 to 1,826; chrome and rims of the 16-car and split-screen sets 1,627 to 1,628; chrome and rims of `shaders_v16` and `shaders_ui_v16` 2,535 to 2,595, as the body paint (2,595), most of it the 9 taps of the variance shadow.

### In-Game Checks (18 October 2026)

BuildRoot `I:\FM4-build-iter` (the runtime of the shader mod iteration, which this change does not touch), 240 Hz, the AI driver, `--profile copy`, at 2x (2560x1440) with `--keep-ppm`. The A/B runs set only `glass=1,chrome=1,rims=1` (`--look glass=1,chrome=1,rims=1`, every other knob at the original look) with `shader_override_ab_ms=4000`, so their off phase is the game's own DXBC and their on phase differs only by these three mods. A race scene is frozen with Y (rewind) for 20 s on each camera; the steps press Y until OCR reads `RESUME` or `REWINDING`, since a Y sent once was sometimes not taken. Photo mode is no use for an A/B: with its camera at rest it does not draw the scene again, so its on and off captures were identical.

| Check | Result |
|---|---|
| Runtime compile | With the knobs at 1, the runtime compiled 27 of the 31 new mods into the generated HLSL at the start (the pipeline storage translates every shader seen before), with no error; the only FXC warning is the known X4000 from `xe_cube`. The 4 others were never translated. |
| False color, Quick Race (`shaders_v16`) | `E62CFDF09D758B6C` draws the windows of every car, and the glass over the gauges in the cockpit (not the windshield seen from inside). `AD10B610621DEFE5` and `00AF2783DCAB9C75` draw the tail lamp lenses. `AF6AA26032BC613D` draws the window surrounds, door handles, mirror trim and exhaust tips. `75B334519A1568F6` draws the rim, and the spinning-wheel blur `10BE203F234535A7` covers its center at speed, as expected. `36DFBBD4669AF5E9` and `0412EE51F7B00389` drew 848,000 and 170,000 times in the inventory. |
| False color, career home (HomeSpace, `shaders_ui_v16`) | `8B983E466B264D2A` draws the windows and the inner lenses of the headlights and fog lamps (the outer headlight cover is another shader), `C2296ADC601D1072` the door handles, `806961C873069679` the rims. `A6D8049525F6D88A` and `FF6CFFF501187440` also drew; `DA5D7A7C839A64C1` drew in the menus of the race run. |
| A/B, race | Five of six cameras froze (cockpit, hood, bumper and the two chase cameras). The glass of other cars reflects the sky and the trackside banners more at grazing angles (the side windows of a Nissan show the TAG Heuer banner), the chrome exhaust tips are brighter, and the tail lamp lenses gain a small highlight. The rims change hardly at all. The gauge glass in the cockpit does not change. No blob, no wrong color. |
| A/B, HomeSpace | The windshield of the Ford Ka reflects the clouds clearly, most at its edges; the headlights look the same. The encoded HDR cubes decode correctly (no green). The camera moves, so the pairs are 2 s apart. |
| A/B, 16-car set (`FM4_RACE_SETTINGS` with `CarShader=shaders_16car`) | `228F44353D897139`, `075A39C371224577`, `EC8CC6E764252B8A`, `C6A76669106153D9`, `E1DC29154298F7F8`, `039EAAF6D690B619` and `76A83E90F52E0739` drew. Brighter chrome exhaust tips and lamp highlights, little on the glass: the static cube is coarse and dim. No blob. |
| Smoke gate `gcr` (`pc` plus `glass`, `chrome` and `rims` at 1) against `gcrbase` (`pc`), run one after the other | 3 of 3 cells passed, no FAIL, no clear-gray frames; WARN on capture statistics only. Race GPU per frame: `photo` 3.09 to 3.19 ms, `career` 3.24 to 3.24 ms. Replay GPU 4.42 to 4.73 ms, photo mode GPU 3.82 to 4.49 ms. Race fps 185.3 to 170.7 (`photo`) and 169.7 to 182.7 (`career`): it follows the draws per frame of the race (2,575 to 3,021 and 2,767 to 2,482), as in the earlier gates. One run per cell, at 1x. |

Not checked: a sun highlight on glass or chrome (no frozen scene faced the sun), the split-screen set, the chrome rim shaders on screen (`F8CDBAFAD23FD784` drew 87,000 times in a photo-mode run, but not on a frozen camera), another track or time of day, and the cost at 2x.

The A/B left nothing to tune: no default was changed with `--shader-dev`. Before a preset gets these mods: a sun-facing scene (a replay camera or another track) for the highlights, and the split-screen set.

## Sky and Vegetation (18 October 2026)

The track's sky dome, trees, tree cards, bushes and grass have mods too: [fm4-nature.hlsli](../../work/fm4-rex/look/fm4-nature.hlsli) and one `shader_<hash>.mod.hlsli` per hash (15 hashes), with the knobs `sky_saturation`, `sky_contrast`, `sky_warmth`, `sky_horizon`, `foliage_hue`, `foliage_saturation` and `foliage_brightness` ([running.md](../running.md)). The track knob `sky`, which scales `skyScale` in the track lighting files, stays as it is: it sets the sky's brightness, these knobs its color.

### Hashes

From the [material atlas](material-atlas.md) of `I:\FM4-build2`: the draw counts of its photo sample (a Quick Race on the Bernese Alps), and the techniques and track archives whose shader files compile to each hash. The atlas classes sky, tree, tree card, bush and grass hold the first 16 of these pixel shaders; the last one is in the atlas class track object, and false color found it drawing trees:

| Hash | Techniques | Track archives | Draws | Mod |
|---|---|---|---|---|
| `BB5743878E0E85BC` | `SKY_DIFF_1`, `SKY_DIFF_1_NoClamp` | all 24 race locations | 5,295 | sky |
| `091B9C98D6DF357A` | `SKY_DIFF_1` with the sun glow (`sunDir`, `SunGainScale`) | all 24 | 6 | sky |
| `19596BCE89F6A0AF`, `758C560B3431D662` | `HS_SKY_DIFF_HDRI_SHAD_1` and `_ANISO_1` | HomeSpace scenes 02, 03, 07 and 20 | 1,962 and 460 | none (below) |
| `9C8D86C330784DD7` | `TREE_DIFF_OPAC_DIRLIGHT_2_2SD` | all 24, HomeSpace07 | 103,552 | foliage |
| `62A70440B0169EB2` | `TREE_DIFF_OPAC_DIRLIGHT_TRACK_2_2SD` | `Alps` | 41,841 | foliage |
| `8EB93A26C6795BF8` | as above, with `Diffuse_Texture_uvOS` and `SkyScale` | `Alps` | 378 | foliage |
| `BFD6CD588F9C96AC` | `TREE_DIFF_OPAC_DIRLIGHT_2_2SD` with `SkyScale` | all 24, HomeSpace07 | 0 | foliage |
| `4C9BF8D6E00CE335` | `TREE_DIFF_OPAC_2_2SD`, `BUSH_DIFF_OPAC_2_2SD_FADE_VCLR_ANIM` | 10 | 0 | foliage |
| `79C21B34BB497059` | as above, with `Diffuse_Texture_uvOS` and `SkyScale` | 10 | 0 | foliage |
| `09226241FE1C9EDB` | `TREECARD_DIFF_OPAC_2_2SD`, `TREECARD_DIFF_OPAC_2_ANISO` | 12 | 0 | foliage |
| `8647ED32C49921A5` | `TREECARD_DIFF_OPAC_2_VERT` | `CrownJewel`, `FujimiKaido`, `NurburgringF1` | 0 | foliage |
| `7DC6FD96091632D3` | `BUSH_DIFF_OPAC_2_2SD` | 7 | 0 | foliage |
| `DCCD09C3413E36DD` | `GRASS_DIFF_OPAC_2_2SD` | all 24, HomeSpace07 | 3,372 | foliage |
| `D00FA3CA03AC0C29` | `GRASS_DIFF_OPAC_2_2SD_noCARSHAD`, `TREECARD_DIFF_OPAC_TANFADE_2_2SD` | `Alps`, `AmalfiGP`, `MapleValley` | 1,903 | foliage |
| `0667025D0DA05D28` | `GRASS_DIFF_OPAC_2_2SD`, `TREECARD_DIFF_OPAC_TANFADE_2_2SD`, `CHAIN_DIFF_OPAC_2_2SD` (chain-link fences) | all 24, HomeSpace07 | 0 | foliage |
| `2FABDDDB86C5AA15` | `DIFF_OPAC_2_2SD` | 22 | 21,311 | foliage |

The HomeSpace skies are left without a mod: they draw only behind the garage and the career home, read a grayscale shadow map (fetch 11, `shad_color`), and `758C560B3431D662` decodes an encoded HDR texture (`bDoDynamicCubeLogLuv`). `DIFF_TREEBEND_1` (`DBAAD2C2B5ADEC50`, 29,630 draws, atlas class track object) has no mod: false color shows it drawing tree trunks and branches, whose bark the foliage knobs would leave as it is, and its specular mask is the texel's red ([In-Game Checks](#in-game-checks-sky-and-vegetation-18-october-2026)).

### Inputs

Read from the ucode dumps of `I:\FM4-build-iter` (`cov-ab-16car-dump`), which hold the first 16 hashes, and of the false color run `nat-fc-alps-dump` (`2FABDDDB86C5AA15`):

- **Sky**: `BB5743878E0E85BC` fetches the dome texture (fetch 0) at interpolator 1 XY, scaled by `c0` and `Diffuse_Texture_uvOS` (`c120`), and writes the square root of its color times `SkyScale` (`c222.x`) plus `TimeGain` (`c249.y`) times its alpha; the alpha is the texel's. It loads interpolator 0 but does not read it. Its vertex shader `0D37C78343C4EE99` writes the world position of the dome (`world`, `c140`) into interpolator 0 and the UV into interpolator 1. `091B9C98D6DF357A`, drawn with the same vertex shader, normalizes interpolator 0 and takes its dot product with `sunDir` (`c250`) for the sun glow, so interpolator 0 is the direction of the view from the camera, and its Y the height above the horizon (checked with false color below). It also desaturates the texture by `c222.y`.
- **Vegetation**: every one of the 13 shaders fetches its diffuse texture in fetch 0, multiplies its color by a light that blends `V2LightmapColor1` and `2` (`c247`, `c248`) by a vertex value (the `DIRLIGHT` trees) or by the lightmap in fetch 7, fogs it towards `FogColor` (`c242`) by an interpolator (0 X on most of them), and writes the square root (gamma 2). The texel is linear. Its alpha is the cut-out of the leaves (times interpolator 0 Y on most of them). `DCCD09C3413E36DD` also darkens the grass towards `shad_color` (`c240`) by three taps of the grayscale shadow map (fetch 11); several multiply the result by `c222.w`. `2FABDDDB86C5AA15` is a general cut-out shader: it also multiplies by the vertex color (interpolator 5) and adds a sun highlight (`sunDir` `c250`, `sunColor` `c251`) masked by the texel's red, so turning green leaves towards yellow makes that highlight a little stronger on them.

### Mods

All of them hook the texture fetch of fetch 0 and change the texel's color, so the game's lighting, shadow, fog and cut-out apply to the new color. The alpha is the game's. At the default knobs each mod returns the texel as it is.

- **Sky** (`fm4_sky`): `sky_contrast` applies the S-curve of the global `contrast` to the texel's luminance in gamma 2 and keeps its hue; `sky_warmth` multiplies it by a white balance of 1.2, 1 and 0.8 (at 1; red, green, blue, normalized to the same luminance); `sky_saturation` scales its saturation. `sky_horizon` takes the height above the horizon from interpolator 0 (`XE_HOOK_INPUT`): at 1 the horizon is 0.6 times as saturated and a quarter stop brighter, the zenith 1.4 times as saturated and half a stop darker, the square root of the height rising fast above the horizon. The HDR gain in the alpha stays as it is.
- **Vegetation** (`fm4_foliage`): the knobs act on green only, by a weight from the texel's hue and saturation: in full from yellow green to blue green (60 to 170 degrees), not at all below 35 or above 195 degrees, and not on gray texels (saturation below 0.08, in full from 0.3). Bark, branches, flowers and the gray chain-link fences that share the grass shader keep their color. `foliage_hue` turns that green about the gray axis by its angle times the weight and restores the texel's luminance; `foliage_saturation` and `foliage_brightness` scale its saturation and brightness by the weight.

### Verification Without the Game

- `fm4_shader_mod_tests` (68 mods): 1,306 checks pass. With the defines of the `pc` look, which leave these knobs at their defaults, the 15 new mods compile to the DXBC of the library without a mod (fingerprint `9F4285BB9A8C0E9A`, 1,283 instruction slots); with every knob away from the original, each mod's DXBC changes. The look files have 28 knobs of the 32 live knob slots. With live knobs the new mods take 1,353 to 1,357 slots.
- `override_fxc.py --stub` against the real contracts: all 15 contracts of the 14 hashes in `I:\FM4-build-iter\e2e\cov-ab-16car-dump`, and the contracts of the false color run `I:\FM4-build2\e2e\nat-fc-alps-dump` (50 contracts of 39 modded hashes, among them `2FABDDDB86C5AA15`), compile with and without the seven knobs moved, and every new mod's DXBC changes with them. The stub override now fetches every 2D texture of the contract through `XE_HOOK_TFETCH`, as a generated override does; before, it fetched nothing, so a mod that only hooks a fetch could not change its DXBC.

### In-Game Checks: Sky and Vegetation (18 October 2026)

BuildRoot `I:\FM4-build2`, 240 Hz, the AI driver, `--profile copy`, quality `high`, at 2x (2560x1440) with `--keep-ppm`, a Quick Race from Free Play with the default car on the Bernese Alps (Festival Circuit) and on Maple Valley Raceway (its location picked on the TRACKS screen), then six race cameras. `REX_GPU_INVENTORY` records what drew. The false color runs use `--look original` and a look directory in the BuildRoot (not in the repository) that writes one flat color per hash, and for the sky 10-degree bands of the height that `fm4_sky` reads from interpolator 0 (red at the horizon to blue at the zenith, black below it).

| Check | Result |
|---|---|
| Sky, both tracks | `BB5743878E0E85BC` draws the whole sky: in every camera, in the rear-view mirror, and in the environment cube that the car paint reflects (the hood reflects the false color). The bands lie level with the horizon and rise towards the zenith, so interpolator 0 is the direction of the view and Y is up. The snowy mountains of the Alps are geometry, not part of the dome. `091B9C98D6DF357A` drew 12 times per run. Draws: 38,294 (Alps) and 37,863 (Maple Valley). |
| Trees, Alps | `9C8D86C330784DD7` (310,870 draws) draws conifers and the hedges along the fences, `62A70440B0169EB2` (218,259) and `8EB93A26C6795BF8` (4,200) most of the other conifers (their purple and magenta come out alike after the tone curve). Dark green tree lines kept their color; a second and a third run with candidate hashes found them in `2FABDDDB86C5AA15` (`DIFF_OPAC_2_2SD`, 66,411 to 83,922 draws), which now has a mod. The other candidates draw something else: `32F9BD5E1092DC31` (`DIFF_OPAC_VLIT_1`) the railings of the stands, `B5F9FC78D040A846` (`DIFF_OPAC_2_noLM`) the cable car pylons. With `2FABDDDB86C5AA15`, no tree on screen kept its color. |
| Trees, Maple Valley | `9C8D86C330784DD7` draws the broadleaf canopies (1,545,422 draws), `D00FA3CA03AC0C29` the distant tree line (1,066,995), `09226241FE1C9EDB` tree cards (779,721; its red false color cannot be told from the red sky at the horizon), `BFD6CD588F9C96AC` 67,658 draws. `DIFF_TREEBEND_1` (`DBAAD2C2B5ADEC50`, 750,057 draws) draws the trunks and branches. |
| Grass | `DCCD09C3413E36DD` draws the grass tufts beside the track on both tracks (8,525 and 30,199 draws), `D00FA3CA03AC0C29` also draws on the Alps (12,929). The ground itself is terrain and shoulder shaders, which have no mod. |
| Never drew | `4C9BF8D6E00CE335`, `79C21B34BB497059`, `7DC6FD96091632D3`, `8647ED32C49921A5` and `0667025D0DA05D28` (their track archives are other tracks), and `62A70440B0169EB2` and `8EB93A26C6795BF8` on Maple Valley (Alps only). |
| HomeSpace skies | `19596BCE89F6A0AF` and `758C560B3431D662` drew in every run (about 3,800 and 480 draws, in the menus), without a mod. |

The capture check flagged one Alps false color frame with 1.2% clear gray pixels; they are all on the asphalt, whose color matched (102, 102, 102), and the frame shows the whole road.

The A/B runs set only the nature knobs (every other knob at the original look) with `shader_override_ab_ms=4000`, so their off phase is the game's own DXBC. Five race cameras each are frozen with Y (rewind) for 22 s. A candidate set, `sky_saturation=1.15`, `sky_contrast=0.3`, `sky_horizon=0.5` and `foliage_saturation=1.15`, ran on both tracks, and an autumn set, `foliage_hue=-45`, `foliage_saturation=1.2` and `sky_warmth=0.5`, on Maple Valley. Colors below are 8-bit means of patches of the 2x captures, off then on.

| Check | Result |
|---|---|
| Frozen pairs | Maple Valley: all five cameras froze (same-phase pairs identical; on/off pairs differ by a mean of 2.3 to 6.6 on 0.9 to 9.8 million bytes), both sets. Alps: two of five (the others kept moving through the rewind), with on/off means of 3.0 and 3.5. |
| Sky, candidate set | The upper sky is a deeper blue: (184, 222, 242) to (172, 223, 244) on the Alps, (144, 184, 216) to (126, 178, 216) on Maple Valley. Clouds stand out a little more. The Alps mountains, which are geometry, do not change. No banding at the horizon, no seam on the dome. |
| Vegetation, candidate set | Barely visible. The dark Alps conifers keep their brightness (a patch mean of (19.9, 26.5, 17.3) to (19.3, 26.8, 16.2)) and get a little greener. Maple Valley is an autumn track: its green trees get a little greener, and its red, orange and yellow trees do not change, since the weight leaves those hues alone. |
| Autumn set | The green and olive trees of Maple Valley turn a rusty orange brown; the yellow and red ones keep their color; the cut-out edges of the leaves stay as they are. The ground stays green (terrain shaders, no mod). `sky_warmth=0.5` makes the sky a little paler and warmer. |
| Artifacts | None seen: no blocks, halos or wrong colors on bark, fences, banners or cars. |
| Smoke gate `natc1` (`pc` plus the candidate set) against `pcsky07` (`pc`, the same BuildRoot, run earlier the same day), 240 Hz, 1x | 3 of 3 cells passed, no FAIL, no new error keys, no clear-gray frames; WARN on capture statistics only. Race GPU per frame 3.01 to 2.93 ms (`photo`) and 3.14 to 3.13 ms (`career`); photo mode 4.62 to 4.00 ms; replay 4.58 to 4.42 ms. The race fps follows the draws per frame: 204.6 to 213.7 at 2,406 to 2,143 draws (`photo`), 192.0 to 180.8 at 2,458 to 2,751 draws (`career`). One run per cell. |

**The `pc` preset**: unchanged. The candidate set is an improvement only in the sky, and a small one, seen on two tracks at one time of day, against the game's look rather than against `pc`, which already has `saturation=1.1` and `sky=0.7`. No preset sets the nature knobs.

## Road Surface (18 October 2026)

The road, its painted lines, its edges and the paved shoulders beside it have mods too: [fm4-road.hlsli](../../work/fm4-rex/look/fm4-road.hlsli) and one `shader_<hash>.mod.hlsli` per hash (25 hashes), with the knobs `road_brightness`, `road_contrast`, `road_specular`, `road_sheen`, `road_roughness` and the debug views `road_debug` ([running.md](../running.md)). The track knob `road_gloss`, which scales `RoadSpecular` and `RoadSpecular2` in the track lighting files, stays as it is.

### Hashes

From the [material atlas](material-atlas.md) of `I:\FM4-build2` (draws of its Bernese Alps sample), the techniques and track archives of each hash, and what the false color runs below showed it drawing:

| Hash | Techniques | Track archives | Draws | Mod | On screen |
|---|---|---|---|---|---|
| `5E33CD06E3AF5B04` | `ROAD_BLND_2`, `ROAD_BLND_TRILIN_2` | 17 | 23,179 | lit, debug views | the asphalt of the Bernese Alps, the brick of its pit lane, parts of Sebring and Top Gear; also in the mirror |
| `29A611FD8CFB545D` | as above, unlit | the same 17 | 7,246 | tone | draws in the main scene in a third of the race frames, not seen in the frozen cameras |
| `719181479E75D490` | `ROAD_2BLND_3`, `ROAD_2BLND_TRILIN_3` | `Nurburgring`, `RoadAtlanta`, `Sebring`, `SunsetInfield` | 0 | lit | the concrete of the Sebring start straight |
| `AB06B72B8FC00531` | as above, unlit, and `TERR_ROAD_2BLND_3` | the same 4 | 0 | tone | as `29A611FD8CFB545D` |
| `E2DECBB50FE1F549` | `ROAD_BLND_DIFF_SPEC_3`, `_TRILIN_3` | `CrownJewel`, `TopGear` | 0 | lit | the asphalt of Top Gear |
| `B456A6A80C358BE8` | `ROAD_BLND_DIFF_LOCALSPEC_3` | `CrownJewel` | 0 | lit | not run |
| `6FAC53A84493D9C8` | the three above, unlit | `CrownJewel`, `TopGear` | 0 | tone | as `29A611FD8CFB545D` |
| `2AD313C93939CC4E` | `ROAD_BLND_REFL_NORM_NOCUBE_4` | `Indi`, `Infin` | 0 | lit | not run |
| `F54ECAB765C4EF80` | `ROAD_BLND_NORM_ANI3_4` | `Tsukuba` | 0 | lit | the asphalt of Tsukuba |
| `68BFCEB3128D4243` | `ROAD_BLND_NORM_ANI3_4`, `ROAD_BLND_REFL_NORM_4`, `_NOCUBE_4`, unlit | `Indi`, `Infin`, `Tsukuba` | 0 | tone | as `29A611FD8CFB545D` |
| `3659E347F2BC25C4` | `ROAD_BLND_REFL_NORM_4` | `Tsukuba` | 0 | tone | drew on Tsukuba (102,933 draws), not identified |
| `23820B0944BFEA99` | `ROAD_BLND_REFL_NORM_ANI3_VCLR_4` | `Tsukuba` | 0 | tone | 3,017 draws on Tsukuba, not identified |
| `FDB6220AC64BFAA7` | as above, unlit | `Tsukuba` | 0 | tone | never drew |
| `49E644093DA8F52F` | `RDLINE_BLND_SPEC_OPAC_3` | 17 | 47,376 | plain | the white and yellow lines and the arrows |
| `38BB332354AB004E` | `RDLINE_BLND_DIFF_OPAC_2` | 7 | 0 | plain | lines of Sebring and Top Gear |
| `B45FDB6F029E956C` | `RDLINE_DIFF_OPAC_2` | 15 | 0 | plain | lines of Tsukuba, Sebring and Top Gear |
| `2721A94BE9720ED6` | `RDEDG_BLND_DIFF_SPEC_BUMP_5` | `Alps` | 6,475 | lit (fetch 1) | not seen in the frozen cameras |
| `511B7ED1E9C15776` | `RDEDG_BLND_DIFF_SPEC_BUMP_4` | `Alps`, `CrownJewel`, `TopGear` | 543 | lit (fetch 1) | the road edge of Top Gear |
| `E6504978C414C01D`, `29F5E60933DDD558` | the two above, unlit | as above | 349, 265 | tone (fetch 1) | not seen |
| `21CE6E592CD9B840` | `SHLDR_BLND_SPEC_2` | `Alps`, `Autopark`, `Catalunya`, `Hockenheim`, `LeMans` | 36,374 | plain | the paved shoulders and the pit lane of the Bernese Alps |
| `61BDABDE43ED4AA5` | `SHLDR_DIFF_SPEC_DECAL_MULT_NORM_5` | `Alps`, `TopGear` | 11,835 | plain | the paved run-off beside the road (Alps, Top Gear) |
| `22F2AAD7D46B3D62` | `SHLDR_BLND_SPEC_3` | 15 | 6,328 | plain | a paved strip beside the road (Alps) |
| `7A1AD82DFA7556E8` | `SHLDR_4CLR_BLND_MASK_2` | `Alps`, `CrownJewel` | 10,650 | plain | not seen |
| `E7723323FB183E69` | `SHLDR_DIFF_SPEC_MULT_NORM_4` | `Alps` | 908 | plain | not seen |

Mod kinds: **lit** gives the asphalt texture the tone knobs (fetch 0, or fetch 1, the `RoadSampler` of the edges, whose fetch 0 is the shoulder texture), scales the game's sun highlight (`road_specular`) and adds the sheen; **plain** has no asphalt texture, so only the highlight knobs; **tone** is a road shader without a sun highlight, so only the tone knobs.

Left without a mod:

- `A61C8E704FB1CCDB` (`SHLDR_DIFF_SPEC_1`, also `SIGN_DIFF_SPEC_SHAD_1`, 23 tracks): false color shows it drawing the walls, banners and signs along the track, not the ground.
- `AC785C2DA14FB0F4` (`SHLDR_BLND_SKIDSPEC_2`, 3 tracks): its sun shadow comes from other interpolators and from a projected track lightmap (fetch 13), so the shared shadow of the sheen does not fit it; it keeps the game's look.
- The unlit shoulders `471771339781970C`, `AFE1EDF44F357517`, `429AE74455107F56` have no highlight to change.
- The shoulders of Tsukuba (`C9656A1E9EF64D28`, the painted run-off, 755,315 draws; `4267DC196B990989`, the grass verges, 509,710 draws; `33AE5A6528F5D1B2`, `A775803D3CCB7C56`, `C8F59933FE159062`, `4BF1AB3CEA368C88`) and the other `SHLDR_*`, `RDEDG_DIFF_OPAC_2` and `RDDET_*` hashes of the atlas: not decoded yet.
- `3659E347F2BC25C4` and `23820B0944BFEA99` read no sun direction: their highlight is a cube map reflection times the W of `c249`, which `road_specular` would remove with no sheen to replace it, so only the tone knobs apply.
- The roads of Amalfi, Fujimi Kaido and Maple Valley are not road shaders at all (no `ROAD_*` technique in their archives): they are drawn by terrain, shoulder and track object shaders.

### Inputs

Read from the ucode dumps of `I:\FM4-build-iter\e2e\cov-ab-16car-dump` (all hashes above but four, which only the runs below translated) with a symbolic listing of each shader, and the literal constants from the containers (`c255` = (1, 0, 0, 0) in `5E33CD06E3AF5B04`; other shaders keep 0 and 1 in other components). `5E33CD06E3AF5B04`:

- Interpolator 0 the normal, 1 the direction to the eye, 2 X the fog, 2 ZW and 4 ZW the cascade selection of the shadow map, 3 XY, 3 ZW and 4 XY its three taps, 5 XY the UV of the asphalt, 6 XY the UV of the blend value and the lightmap.
- The asphalt color: the texel of `Blend_B` (fetch 0) times `c0` blended to the texel plus `c2` by the blend value (`Blend_Value`, fetch 1, Y).
- The diffuse: that color times the lower, per channel, of two lights blended from `V2LightmapColor1` (`c247`) to `V2LightmapColor2` (`c248`): one by the lightmap texel (fetch 7, RGB), one by the cascade shadow through `shad_color` (`c240`).
- The sun highlight: `pow(N.H, power)` times `sunColor` (`c251`), the shadow and the specular amount, times the W of `TimeGain` (`c249`). The shadow is the lower of the three taps (fetch 11, Y, Z and X) and the lightmap's Y mapped from `c224.z` to `c224.w`, raised from the W of `SunGainScale` (`c214`) to 1. The amount is `c223.x` to `c223.y` by the asphalt texel's alpha, plus `c223.w` to `c223.z` by the blend value, and the power is `c224.x` to `c224.y` by the amount.
- `RoadSpecularParams1` (`c223`) and `RoadSpecularParams2` (`c224`) hold the eight values of the track's `RoadSpecular` element: `RoadSpecular` and `RoadSpecular2` in `c223` X and Y (the `road_gloss` knob scales those, and only the aggregate's sparkle changed with it), the rubber values `RoadSkid` and `RoadSkid2` in Z and W, `RoadGlossiness` and `RoadGlossiness2` in `c224` X and Y (25 and 25 on the Bernese Alps, so the power is 25), and `RoadLightmapDarkPoint` and `RoadLightmapLightPoint` in Z and W (0.42 and 0.86). This assignment is inferred from how each value is used.
- The output: the fog towards `FogColor` (`c242`) and the square root (gamma 2), alpha 1.

Every lit shader of the table reads the same shadow (three taps at the same interpolators, the same cascade selection and the lightmap mapping of `c224`), the normal in interpolator 0, the direction to the eye in 1 and the fog in 2 X, and adds its highlight with the W of `c249` before the fog towards `c242`. The variants differ in the color (two blend textures, normal maps, skids) and in the amount. The unlit variants (`29A611FD8CFB545D` and the others marked tone) light the same asphalt color by the lightmap alone, with no shadow map and no highlight, and multiply the result by the W of `c222`.

### The Bright Sheet

[Current status](../status/current.md#display) recorded a near-white sheet on sunlit asphalt toward the sun on the Bernese Alps, which `road_gloss` did not remove. The debug views of the `5E33CD06E3AF5B04` mod split the shader's result in the game: a Quick Race grid start without the AI driver, the second camera turned to the side with the right stick, at 2x, the views switched every 4.5 s through `shader_mod_defines.txt` (`defines:` steps, [e2e README](../e2e/README.md#scenario-steps)) in the same frame. The Quick Race put the car in the same grid slot in this run and in the `CameraExposureRange` run, and the crop is the 640x300 one of the `road_gloss` checks:

| Setting | Crop mean (8-bit) | Pixels at 230 or more | At 250 or more |
|---|---|---|---|
| `pc` | 225 | 51.7% | 0.2% |
| `pc`, `road_specular=0` (no game highlight) | 116 to 123 | 0.0% | 0.0% |
| `pc`, `road_specular=0.5` | 214 | 22.2% | 0.0% |
| `pc`, `exposure=-1` | 195 | 2.6% | 0.0% |
| `pc` with `CameraExposureRange` `maxval` 1 instead of 6 (another run) | 225 | 51.7% | 0.2% |

- View 1 colors the sheet orange to red: the sun highlight is 70 to 80% of the light there. View 2 (without the highlight) has no sheet; view 3 (the highlight alone) is the sheet. View 4 shows the lightmap deciding the diffuse light everywhere but in the car's own shadow, where the dynamic shadow does.
- View 5 shows the specular amount mostly from the blend value of the rubber (`RoadSkid`, `RoadSkid2`), not from the texture's alpha. So the sheet is the game's highlight, a Blinn-Phong lobe of power 25 with no Fresnel and no N.L, whose amount is a smooth mask: `road_gloss` scales only the alpha's part, the sparkle of the aggregate.
- `CameraExposureRange` does not take part: with `maxval` 1 the track settings copy was mounted (60 changes) and the frame is the same, so the auto exposure there lies within the range. A lower exposure only darkens the whole picture.

A look mod is therefore the place for it, not the exposure range.

### Mods

- **Tone** (`fm4_road_asphalt`): `road_brightness` scales the asphalt texel; `road_contrast` raises the ratio of each texel's luminance to the texture's local mean (the same fetch four mip levels coarser) to the power 1 + contrast, so the aggregate stands out while the mean tone stays. The alpha (the specular mask) is the game's. Measured on the grid: `road_contrast=0.5` raises the standard deviation of a road crop's green from 41.8 to 49.2 at the same mean.
- **Specular** (`fm4_road_constant`): `road_specular` scales the W of `c249`, which these shaders read for the sun highlight only.
- **Sheen** (`fm4_road_sheen`): an energy normalized GGX highlight with Schlick Fresnel (F0 0.04, roughness `road_roughness`, default 0.5), the height correlated Smith visibility and N.L, times pi as the clear coat, in the sun's share of the diffuse light (`c248 - c247`) and the shared shadow, soft limited at 4 times that light, before the fog. The road shaders `5E33CD06E3AF5B04` and `719181479E75D490` weight it by the game's own specular amount (in full from 0.5); the others by a uniform 0.5. The normal is interpolator 0, without the normal maps of the variants.
- **Debug** (`fm4_road_debug`, `5E33CD06E3AF5B04` only): the views above. Views 1, 4 and 5 keep the luminance of the game's output, so the auto exposure stays.

At the default knobs each mod compiles to the DXBC of the library without a mod.

An earlier version, which weighted the sheen by the texel's brightness against the mean instead, gave the sheet back: 55.8% of the crop at 230 or more with `road_specular=0`, a flat gradient with no aggregate. Weighted by the amount it gives 5.4% (mean 187), the aggregate visible through a smaller glare.

### Verification Without the Game

- `fm4_shader_mod_tests` (93 mods): 1,781 checks pass, also when built against the runtime headers of the rebuilt `I:\FM4-build-usa` (`Build.ps1 -Action build`, which also passed the instruction, post pass and track settings tests). The synthetic override now also fetches fetch 1 through `XE_HOOK_TFETCH`, for the asphalt of the road edges, so every fingerprint differs from the earlier ones: with the `pc` defines the 25 new mods compile to the DXBC of the library without a mod (fingerprint `BF287DBE415AA801`, 1,432 instruction slots), and with every knob away from the original each changes. The look files have 34 knobs, and the live knob slots are raised from 32 to 64 (`ShaderOverrides::kKnobCount`, 848 bytes of system constants with live knobs). With live knobs the lit road mods take 1,844 to 1,961 instruction slots, the plain ones 1,708, the tone ones 1,569.
- `override_fxc.py --stub` against the real contracts: all 99 contracts of 76 modded hashes in the dumps of the runs below and of `I:\FM4-build-iter`, among them 24 of the 25 new hashes, compile with and without the road knobs moved, and each road mod's DXBC changes with them. `2AD313C93939CC4E` (Indianapolis, Infineon) has no contract and compiled only on the synthetic one.
- The same stub compiled with the `pc` defines and the mod, and with no mod, gives byte-identical DXBC on all 29 contracts of the 24 road mods that have one: at their defaults the road knobs leave nothing in the shader. A hash with a mod file still runs as the HLSL that the runtime generates from its ucode, compiled by FXC, instead of the DXBC translation (`ShaderOverrides`, a mod turns the generated HLSL on), so adding these 25 files moves the road shaders to that path at the default look too. `roadbase` below ran with them; the default look with and without the road files was not compared in the game.

### In-Game Checks: Road Surface (18 October 2026)

BuildRoot `I:\FM4-build-usa`, 240 Hz, `--profile copy`, quality `high`, at 2x (2560x1440) with `--keep-ppm`. The false color runs use `--look original` and a look directory in the BuildRoot (not in the repository) with every mod of the repository plus a tint of each listed hash, its luminance kept, in groups of up to eight that a live define switches through `defines:` steps while a race camera is frozen (rewind, Y); `REX_GPU_INVENTORY` records what drew. Tracks: the Bernese Alps (Quick Race), and Tsukuba, Sebring and Top Gear (Quick Race, the location picked on the TRACKS screen), with the AI driver and five race cameras each. The A/B runs switch the road knobs through `defines:` steps with `--shader-dev` in one frozen frame, on top of `pc`.

| Check | Result |
|---|---|
| False color, Bernese Alps | `5E33CD06E3AF5B04` draws the whole road, the brick of the pit lane and the road in the mirror; `49E644093DA8F52F` the white and yellow lines and the arrows; `21CE6E592CD9B840` the pit lane and paved shoulders; `61BDABDE43ED4AA5` the paved run-off beside the road; `22F2AAD7D46B3D62` a paved strip; `A61C8E704FB1CCDB` the walls and banners. `29A611FD8CFB545D` drew 76,593 times in 12,726 of 36,063 frames, only in the main scene, not in the frozen cameras; so did the unlit variants on the other tracks. Kerbs are other shaders. |
| False color, Tsukuba | `F54ECAB765C4EF80` draws the whole road, `49E644093DA8F52F` its lines. `3659E347F2BC25C4` (102,933 draws) and `23820B0944BFEA99` (3,017) drew but were not seen; `FDB6220AC64BFAA7` never drew. The painted run-off is `C9656A1E9EF64D28`, the grass verges `4267DC196B990989` (no mods). |
| False color, Sebring | `719181479E75D490` draws the concrete of the start straight; `5E33CD06E3AF5B04`, `38BB332354AB004E` and `B45FDB6F029E956C` drew (267,650, 501,912 and 155,062 draws). |
| False color, Top Gear | `E2DECBB50FE1F549` draws the runway asphalt, `511B7ED1E9C15776` the road edge, `61BDABDE43ED4AA5` the paved apron, `5E33CD06E3AF5B04` a part of the track. In the game's look its hood view shows the same white glare towards the sun on `E2DECBB50FE1F549`. |
| Debug views and `CameraExposureRange` | See [The Bright Sheet](#the-bright-sheet). |
| A/B, sheet view (Bernese Alps grid) | The crop of [The Bright Sheet](#the-bright-sheet), 1:1 at 2x: `pc` a milky sheet, the aggregate barely visible; `road_specular=0` dark asphalt with sharp aggregate and no glare; `road_specular=0,road_sheen=1` a smaller glare towards the sun with the aggregate through it (5.4% at 230 or more, mean 187); `road_roughness=0.35` a hot spot (5.9% at 250 or more), 0.7 almost no glare. `road_contrast=0.5` alone leaves the sheet as it is. |
| A/B, another grid slot (a moderate sun angle) | An 800x380 crop over the asphalt and a painted arrow: `pc` mean (175, 190, 180), 2.1% at 230 or more, a soft glare over asphalt and arrow alike; `road_specular=0,road_sheen=1` (122, 147, 134), 0%, no glare, sharp aggregate; `road_specular=0.5,road_sheen=1` (162, 179, 168), 0.8%; `road_specular=0,road_sheen=1,road_roughness=0.4` (138, 161, 148), 0.4%. The arrow (`49E644093DA8F52F`) and the asphalt change together in every set: no seam. |
| A/B, five race cameras (cockpit, hood, bumper, two chase cameras), none facing the sun | Road crops change by 1 or 2 of 255 between `pc` and every set; `road_contrast=0.3` adds a little grain to the aggregate. The skid marks, the apron and the lines look the same. |
| Smoke gate `roadc` (`pc` plus `road_specular=0,road_sheen=1`) against `roadbase` (`pc`, run just before it), 240 Hz, 1x | 3 of 3 cells passed, the same error keys, no clear-gray frames. `photo` and `photo_view` WARN on capture statistics only. `career` FAIL on fps: race 213.7 to 189.5 fps, but that race drew 2,585 instead of 2,007 draws per frame (`photo`: 215.4 to 199.2 fps at 2,154 to 2,521), and the race is limited by the command processor. Race GPU per frame 3.01 to 3.08 ms (`photo`) and 3.08 to 3.20 ms (`career`), replay 4.49 to 4.70 ms, photo mode 4.34 to 4.20 ms. One run per cell. |
| `-ShaderDev` on the rebuilt runtime (`e2e.py --shader-dev`, `boot`) | The runtime reports 93 mods and 34 live knobs, with no "more than N live knobs" warning, and compiles 24 of the 25 road mods with every knob live (not `2AD313C93939CC4E`, never translated in this BuildRoot), with no FXC error or warning. |
| `RoadSkid` and `RoadSkid2` set to 0 (track settings) | The copies mounted (90 changes), but the Quick Race put the car in another grid slot, with no view into the sun, so the run does not show whether the sheet goes with them. |

**The `pc` preset**: unchanged. `road_specular=0,road_sheen=1` removes the sheet, but also the soft glare that the game's highlight gives at moderate sun angles, and the views away from the sun do not change; `road_specular=0.5,road_sheen=1` halves the game's term instead. Which reads better is a matter of taste, seen on one track at one time of day, and the race GPU time rose by 0.07 to 0.12 ms per frame. No preset sets the road knobs; try `-Look "pc,road_specular=0,road_sheen=1"` or `-Look "pc,road_specular=0.5,road_sheen=1"`.

Not checked: a sun-facing view with the mods on another track (the Top Gear hood view showed the glare in the game's look only), Indianapolis and Infineon (`2AD313C93939CC4E`), Crown Jewel (`B456A6A80C358BE8`), other times of day, replays and photo mode, and the cost at 2x.

## Related

- [Material atlas](material-atlas.md): classes, samplers and registers of the car paint.
- [B4 shader research](b4-shader-research.md): the mod hooks and the final combine formula.
- [GTAO design](gtao-design.md): AO scope in the cockpit, garage and showroom.
