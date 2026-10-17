# Look Mod Coverage (17 October 2026)

Look mods are keyed by the ucode hash of a pixel shader. A mode or track that draws the same role with a different ucode gets the original shader without any warning. This note lists, per track and per mode, which final combine, motion blur and car paint hashes occur, and the mods added on 17 October 2026 for the ones that had none. The car glass, chrome and rims, which got mods on 18 October 2026, are in [their own section](#glass-chrome-and-rims-18-october-2026). It contains hashes, technique names and constant names only, no shader code.

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

## Related

- [Material atlas](material-atlas.md): classes, samplers and registers of the car paint.
- [B4 shader research](b4-shader-research.md): the mod hooks and the final combine formula.
- [GTAO design](gtao-design.md): AO scope in the cockpit, garage and showroom.
