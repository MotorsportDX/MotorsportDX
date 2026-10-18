# Rain Feasibility: Wet Grip in the Physics (18 October 2026)

Whether Forza Motorsport 4's physics still applies a wet grip modifier, what would drive it, and how a wet race could change grip. This note covers the physics only; rain visuals (wet road shader mods, a screen-space rain pass) would use the existing shader mod and post pass infrastructure and are not discussed here.

Method: a static trace of the recompiled title code (the generated sources of BuildRoot `I:\FM4-build2`), plus read-only queries of the game database (Disc 1 `Media\db\gamedb.slt`) and a reading of `Media\physics\surfaceTypes.xml`. No game was run. The loaded images of the Japan/Asia and USA releases are byte-identical, so every address below is valid for both. Addresses are guest addresses of `default.xex`; struct offsets are decimal byte offsets. This note contains names, addresses, offsets and a few data values, not game code.

## Summary

- **There is no wetness input.** The tire compound fields `WetFrictionModFrictionScale` (0.8) and `WetFrictionModSlipScale` (1.5) are loaded from `List_TireCompound` and copied into each car's runtime compounds, and nothing reads them afterwards. The surface fields `OffRoadWetFrictionScale` and `OffRoadWetPeakSA` are parsed from `surfaceTypes.xml` and copied into the runtime surface table, and nothing reads them either.
- **Nothing drives them.** No variable, constant, track setting or race setting selects wet values. The tire model reads only the dry surface fields. Neither the image's strings nor the database has a rain, wetness or puddle setting; "weather" exists only as a loading screen card (an icon and a temperature).
- **Writing the wet fields does nothing**, because no code reads them. A per-race grip change is still possible: a hook on the function that builds the surface table at each track load can scale the surfaces' `FrictionScale` (the 0.8 friction part), and a mid-function hook in the tire force function can scale the peak slip (the 1.5 slip part).
- **The file overlay fallback works mechanically.** The title reads `surfaceTypes.xml` again at every track load, so an edited copy linked through `VirtualFileSystem::ResolveFileLink` takes effect. It is global for the session, it changes friction only, and it applies to the AI cars, whose driving data was built for dry grip. How the AI copes is not known without a game run.

## Tire compound fields

### Load

sub_828A2350 reads one `List_TireCompound` row (`SELECT * FROM List_TireCompound WHERE TireCompoundID=`) into a compound record:

| Offset | Content |
|---|---|
| +24 to +44 | the six `TorqueFree*` scales |
| +48 | lateral friction multi-curve (`FrictionMultiCurveLateralID`, from `List_TireFrictionMultiCurve`) |
| +884 | longitudinal friction multi-curve (`FrictionMultiCurveLongitudinalID`) |
| +1,720 | `WetFrictionModFrictionScale` (stored at 0x828A2E0C) |
| +1,724 | `WetFrictionModSlipScale` (stored at 0x828A2E48) |
| +1,728 to +2,828 | 12 affect curves of 100 bytes (camber, wear, pressure and temperature against friction and wear) |
| +2,928 to +3,072 | tire width and friction scale points, the thermal model, `HUDTemps0` to `HUDTemps3` |

All 9 compounds hold 0.8 and 1.5.

### Copy into the car

sub_8281EBA0 sets up a car's tires. It calls sub_82827108 twice (0x8281F988 and 0x8281F998), which builds a runtime compound for the front axle at car+10,384 (0x8281EFD4) and for the rear at car+13,444 (0x8281F048). sub_82827108 copies `WetFrictionModFrictionScale` to runtime +1,764 and `WetFrictionModSlipScale` to +1,768 (0x828273C0 to 0x828273D4). Runtime layout: +12 `TorqueFreeLatFrictionScale`, +36 the lateral curve, +900 the longitudinal curve, +1,764 and +1,768 the wet fields, +1,772 to +2,812 the affect curves, +2,916 onward the width and thermal fields.

### Use

Each wheel record (1,056 bytes, the first at car+6,160) holds a pointer to a runtime compound at +1,040. The tire functions of the car simulation read the compound through it:

- sub_8230AF28 (the tire force function, called from sub_8230B868): the lateral and longitudinal curves (+36, +900). Through a virtual call on the car (the vtable at car+2,400, slot +1,120: sub_8230B6E8) it also reads `TorqueFreeLatFrictionScale` (+12) of the first wheel's compound, and asserts that all four wheels agree ("FRONT/REAR FRICTION SCALES DON'T MATCH").
- sub_8230E0F8: the affect curves +1,772 to +2,500 and the thermal fields +2,932 to +3,040.
- sub_823021F0: the affect curves +2,604, +2,708 and +2,812.

No code reads +1,764 or +1,768 of a runtime compound, or +1,720 or +1,724 of a compound record after sub_82827108:

- Float loads at displacement 1,764 or 1,768 (any base register but the stack) occur only in sub_8234EDC0, which integrates a body's position from its velocity (its `this` has fields past +5,512, so it is not a compound).
- Displacements relative to the curve sub-objects (+864 and +868 from the longitudinal curve, +1,728 and +1,732 from the lateral curve), address computations of +1,764 and +1,768, vector loads at +1,760, and the absolute car offsets of the wet fields (12,148, 12,152, 15,208, 15,212) all belong to other objects (strings, GPU registers, rigid bodies).
- Float loads at +1,720 occur in sub_82827108 and in three other functions whose objects are not compound records (sub_828ECFD0, a tire size calculation that converts inches to metres, and two unrelated structures).
- A data flow scan of every register loaded from a wheel's +1,040 finds offsets 36, 900, 1,772 to 2,812 and 2,932 to 3,040, never 1,764 or 1,768.
- Check of the method: the same displacement search finds the readers of the used field `TireFricWidth0` (runtime +2,916) in sub_8281EBA0.

## Surface fields

### Load

The track loader sub_826DB438 calls sub_82460BB0 at each track load (0x826DBCB4). It opens `game:\media\Physics\SurfaceTypes.xml`, or `NatalSurfaceTypes.xml` when a flag of the caller is set (Natal was the Kinect code name), and sub_82455530 parses each surface into a 336-byte definition: the name at +0, `Category` at +68, then the `Friction` element.

sub_8245AE50 (called at 0x826DBCEC) then builds the runtime surface table: each entry is a 236-byte copy of a definition from +68 (0x8245AEB8 to 0x8245AEBC). Runtime offsets are therefore the definition offsets minus 68:

| Field | Definition | Runtime |
|---|---|---|
| `Category` | +68 | +0 |
| `FrictionScale` | +72 | +4 |
| `HeatTireScale` | +76 | +8 |
| `OffRoadness` | +80 | +12 |
| `OffRoadWetFrictionScale` | +84 | +16 |
| `OffRoadDryPeakSA` | +88 | +20 |
| `OffRoadWetPeakSA` | +92 | +24 |
| `VelDepFriction` (16 values) | +96 to +156 | +28 to +88 |
| `SteerTorque` (17 values) | +236 to +300 | +168 to +232 |

The loader then hands this table to the physics world through a virtual call (0x826DBD14).

### Use

sub_8230AF18 returns a runtime entry by index (the wheel's contact surface). Its 11 callers, all in the car simulation, read:

| Function | Runtime offsets read |
|---|---|
| sub_8230AF28 | +4, +12, +20 |
| sub_8230CAA8, sub_8230D188, sub_8230E9B0, sub_8230EB38 | +4 |
| sub_823091F0, sub_8230A938 | +0, +4, +8 |
| sub_8230E0F8 | +8 |
| sub_8230B868 | +28 to +88 |
| sub_823021F0 | +168 to +232 |
| sub_82822BD0 | none |

No code reads +16 or +24 of a runtime entry, or +84 or +92 of a definition.

In sub_8230AF28:

- Both friction limits are multiplied by `FrictionScale` (+4, read at 0x8230AFC4).
- `OffRoadDryPeakSA` (+20, 0x8230B13C, converted from degrees to radians) is divided by the lateral curve's peak slip angle at the wheel's load (0x8230B1B8). The ratio is blended with `OffRoadness` (+12, 0x8230B140) at 0x8230B1E0, and the result scales both the peak slip angle and the peak slip ratio (0x8230B1E4, 0x8230B1E8). With `OffRoadness` 0 (asphalt, concrete) the curves' own peaks stay.
- A second off-road factor multiplies `OffRoadness` by a literal 0.0 and adds 1.0 (0x8230B1B4), and then scales both friction limits (0x8230B1C4, 0x8230B1C8). It never changes them. Its shape, a blend whose other input is a compile-time constant, is the only trace in this function of a removed input; nothing shows that this input was wetness.

### Data

On all 61 surfaces of `surfaceTypes.xml`, `OffRoadWetPeakSA` equals `OffRoadDryPeakSA`. `OffRoadWetFrictionScale` is 1 on the main asphalt and concrete surfaces, 0.9 on dirt, grass, gravel and sand (whose `FrictionScale` is 0.75 to 0.8), and 0.6 to 1 elsewhere. `NatalSurfaceTypes.xml` has 58 surfaces of the same layout.

## Other weather data

- The image's strings have no rain, wetness, puddle or storm terms besides the four fields above. `NearWet`, `FarWet`, `WetDryMix` and the "wet mix" texts belong to the audio reverb and chorus effects.
- `TEXT_WEATHER_*` and `MATERIAL_WEATHER_*` belong to a loading screen card. It reads `Description`, `Icon` and `Temperature` from `EnvironmentTimeOfDay` for the race's track and `TimeOfDayIndex`. The icons are `WT_Weather_Icon_01` to `03`; the temperatures are 24 to 94, which read as degrees Fahrenheit. This trace did not follow `Temperature` into the tire thermal model.
- The database has 198 tables. No column besides the two compound fields concerns rain, wetness or weather.

## Setting wetness per race

There is no wetness value to set, and writing the runtime compound's +1,764 and +1,768 has no effect. A wet race needs a new effect on grip. Two hooks in the recompiled title would give the two parts that the compound fields describe:

1. **Friction (the 0.8 part).** A `REX_HOOK_RAW` on sub_8245AE50 calls the original and then multiplies `FrictionScale` (+4) of the chosen entries of the new runtime table (its second argument, a vector of 236-byte entries). The first argument holds the definitions in the same order, with the surface names at +0, so the hook can pick the road surfaces by name or `Category`. The loader hands the table to the physics world only after the call returns, so the edit reaches the entries that sub_8230AF18 returns. The table is rebuilt at every track load, so a value chosen for the race takes effect from the next load. The race's choice can come from the launcher (an environment variable, per track or for all tracks) or from the race itself; a hook on sub_826DB438 can record which track is loading. Every tire function that reads `FrictionScale` sees the change (the seven listed above; what the six besides sub_8230AF28 compute was not traced).
2. **Peak slip (the 1.5 part).** A mid-function hook after 0x8230B1E0 in sub_8230AF28 can multiply the peak slip factor that the next two instructions apply, which scales the peak slip angle and the peak slip ratio of every tire on every surface. The codegen supports `[[midasm_hook]]` entries (`work/runtime/src/codegen/config.cpp`); the FM4 configuration does not use any yet, so this would be the first, and it needs the title code regenerated. Without a code hook there is no exact equivalent: setting `OffRoadness` and `OffRoadDryPeakSA` in the table pulls every tire's peak slip angle toward one fixed angle, which drops the differences between compounds and loads.

Both changes apply to every car on the track. All the tire functions above run in the car simulation under sub_82822BD0 and its neighbors, and neither the surface table (one per track) nor the compound path distinguishes the player's car from the AI's.

## Fallback: lowering FrictionScale through a file overlay

The overlay would work like `FM4_TRACK_SETTINGS` (`FM4MountTrackSettings` in `fm4-render-overrides.cpp`): write an edited copy of `Media\physics\surfaceTypes.xml` to the cache, mount it as a device, and register a file-level symbolic link, which `VirtualFileSystem::OpenFile` resolves through `ResolveFileLink`. The edit multiplies `FrictionScale` of the road surfaces (the asphalt, concrete, cobblestone, bumpy, shoulder, blacktop and rumble strip entries) by 0.8, the compounds' wet friction factor. `NatalSurfaceTypes.xml` would need the same edit for its mode.

- **What it changes:** both friction limits of every tire on those surfaces, exactly as the hook in step 1 would. Peak slip does not change.
- **Scope:** the link is registered once, at startup, so every race of the session is wet. Per-race wetness needs the hook in step 1, or a link that is switched before each track load.
- **AI risk:** the AI drives from per-track data on Disc 1: `Media\AI\Tracks\<track>.seg`, `.geo`, `.irt` and `.pit`, and `Media\AI\Tracks\SegmentProfiles\<track>\Default.bin`. The title appends the literal `\Default.bin` (0x82184164), so each track has one segment profile and no wet variant. `Media\AI\PerfPrediction\carperfpred.bin` predicts car performance. This data was made for the dry surfaces (`FrictionScale` 1 on asphalt and concrete). If the AI takes its braking points and corner speeds from it, 20% less grip makes the AI brake late and run wide; if its controller corrects from the car's actual slip, it is only slower. Which one happens cannot be read from a static trace. Setting names suggest per-car friction scales exist (`MaxFrontTireFrictionScale` and `MaxRearTireFrictionScale` beside the AI difficulty settings, `TraitFrontFrictionScale` and `TraitRearFrictionScale` beside the event restriction fields); they were not traced.
- **Measuring it** needs a game run (not done): a Quick Race with AI opponents at `FrictionScale` 1 and 0.8 on the same track and car, comparing the AI's lap times, track departures, collisions and finishing order.

## Not covered

- What sub_8230CAA8, sub_8230D188, sub_8230E9B0, sub_8230EB38, sub_823091F0 and sub_8230A938 compute with `FrictionScale`.
- Whether `EnvironmentTimeOfDay.Temperature` reaches the tire thermal model.
- The AI's response to lower grip (needs a game run).
- Rain visuals.
