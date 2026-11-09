# FM3 and FM2 career modes in FM4

Status: research, 9 November 2026. Nothing is built. This note records whether the career modes of Forza Motorsport 3 and 2 can run inside FM4: how the player picks a career, where the career's state lives in the save, how FM4's frontend is made, and how FM3's and FM2's career screens could be shown in it. The discs were read from `D:\Data` (FM4 USA, FM3 USA Disc 1, FM2 USA); every file was opened read-only, and the unpacked UI archives stayed in a scratch folder.

## 1. Summary

- A career is mostly data. FM4's events, races, restrictions, opponents and rewards are rows of `Media\db\gamedb.slt`, and official packs already add and change them with `merge.slt` and `execute.sql` ([DLC structure](dlc-structure.md#database-patch-format-observed)). FM3's database has nearly the same career tables; FM2's differs more.
- FM3's or FM2's career code cannot run in FM4. What can run is their content and structure on FM4's career code, with progression rules changed only where hooks in the recompiled code allow it.
- The career's progress lives in the profile save, keyed by event IDs, so each career needs its own save slot. The imported content (4.2 GB) stays shared.
- FM4's frontend is Anark Gameface: scene files in `Media\UI.zip`, bound by name to C++ screen classes (`ForzaUI::C*`). FM3 uses the same scene format and shares 86 of its 112 scenes with FM4. FM2 uses an older Anark format whose career logic is Lua that calls a game API FM4 no longer has.

## 2. Picking a career (method B)

The recommended choice is in the launcher, not in the game:

| Career | What Play sets up |
| --- | --- |
| FM4 (default) | the Disc 1 database and the FM4 save slot |
| FM3 style | the FM3 career database patch and the FM3 save slot |
| FM2 style | the FM2 career database patch and the FM2 save slot |

- The database is read at boot, so changing the career means leaving the game, changing the choice and pressing Play again.
- The choice is separate from the launcher's profiles: a profile is a set of run options (display, look, controls), and one profile should serve every career.
- The launcher's top bar shows the active career next to the profile.
- The database patch can use the existing mod disc (`outputs/FM4-build/mods.py`, [running mods](../running-mods.md)): it writes the files a mod replaces into `<BuildRoot>\mod-disc` without touching Disc 1.

A choice inside the game (a "FM3 Career" entry beside World Tour) needs the UI injection of section 5 and a database reload without a restart; it fits only with the progression rules of section 6.

## 3. The save

`<user-data>` of `I:\FM4-build-usa` on 9 November 2026:

| Part | Size | Contents | Per career |
| --- | --- | --- | --- |
| `0000000000000000\4D530910` | 4,256.6 MB | imported Disc 2 and content packages, shared by every profile | no |
| `B13EBABEBABEBABE\4D530910\00000001` and `Headers\00000001` | 7.4 MB | the player's profile save | yes |
| `achievements` | under 1 KB | achievements | no |
| `cache` | 0.7 MB | shader and render override caches | no |
| `*.import-complete` | bytes | import markers of the content packages | no |

`B13EBABEBABEBABE` is the runtime's fixed XUID (`work/runtime/src/system/xam/user_profile.cpp`). The profile folder holds `ForzaProfile\ForzaProfile` (20 KB), `ForzaProfile\PlayerDatabase` (6 MB), `GameplayLog`, `VersionFlags`, `Liveries`, `Thumbnails`, and the sibling folders `ghosts` and `marketplace`. Every save file starts with the magic `cmss` and is encrypted after its header, so the files are swapped whole, never edited.

- `ForzaProfile` follows `Media\ProfileSchema\ForzaProfile.sch`: the career's `Level`, `XP`, `Credits`, `TotalWinnings`, `CareerCar`, `HomespaceTrackId` and `AISkill`, and in the same file the in-game options (camera, assists, `ControllerAdvancedOptions`, `WheelAdvancedOptions`).
- `PlayerDatabase` is most likely an encrypted SQLite database (inferred): `gamedb.slt` holds empty template tables `NewProfile_Career_Garage`, `NewProfile_Career_EventResults (EventId, Place)`, `NewProfile_Career_RaceResults (RaceId, Place)`, `NewProfile_Career_PurchasedParts`, `NewProfile_Career_AIHistory`, `NewProfile_Career_Affinity`, `NewProfile_Career_CarGroups` and `NewProfile_Career_TrackUsage`, which match a garage and a career record keyed by event and race IDs.
- `Media\ProfileSchema\Calendar.cal` (7 KB, binary) is loaded by the title (`GAME:\Media\ProfileSchema\Calendar.cal`) beside the `CareerCalendar` strings; it is probably World Tour's calendar (inferred). An FM3-style career may need its own.

Consequences:

1. One save slot per career: the profile folder above, while content, cache and achievements stay shared. Separate `user-data` folders (the launcher's existing option) would import the 4.2 GB of content once per career.
2. The in-game options are per career too, because they share `ForzaProfile` with the career. The launcher's own pad, keyboard and wheel options are not affected.
3. The runtime should map the profile path to the career's slot (a setting such as a save slot name), rather than the launcher copying files before and after a run: a crash between the copies would lose or mix saves.

## 4. FM4's frontend

- `Media\UI.zip` (zip method 21, XMemCompress LZX; the runtime's decoder reads it, as `outputs/FM4-build/e2e/material_atlas.py` does) holds 141 scenes under `scenes/ui4/`, three files each: `.bgf` (magic `AnarkBGF`), `.bsg` (`FGBkranA`) and `.fbf` (object tree, version `0x3EF`). It also holds `respack.xml` (texture packages for `CUI4ResourceManager`), `eventnames.txt`, and five Lua 5.1 behaviours (`LuaQ` bytecode, three of them source) such as `EventFire.lua`.
- `Media\UI\Textures.zip` holds 4,544 `.xds` textures, among them `UI4/World_Tour` (229) and `UI4/Calendar` (9). `Media\UI` has fonts and videos only.
- Scenes are loaded from `GAME:\Media\UI\Scenes\UI4\%s.bgf`. A scene binds to code by name: `203_season_status` names a `UIContract` and controls such as `parent.Controller.Group.Group_DIALOG.SEASON_STATUS_LIST`; its texts are string table keys (`calendar:IDS_Season_Event_Name`).
- The code side is C++ in the namespace `ForzaUI` (400 RTTI classes), for example `CWorldTour`, `CWorldTourSeasonStatus`, `CWorldTourSeasonTransitionPopup`, `CCareerEventSelect`, `CCareerRaceSelect`, `CCalendarEndOfYear` and `CCalendarCarCheck`. Controls are made by the factory table at `0x82023C30` (123 `ForzaUI::CreateControl_<TYPE>`, [function catalog](function-catalog.md)), by type names such as `BUTTON_TEXT`, `GRID_VIEW` and `SCROLLING_LIST`.
- The scene registry built by `sub_824E2790` holds 249 entries of (factory, name); `sub_824B8EE0(registry, id, parent)` creates a scene, and the scene manager `sub_82987198` takes a scene description, not an id ([direct race launch](direct-race-launch.md#what-go-race-does)). Navigation comes from the scene data. The image names 132 scenes in the `<number>_<NAME>` form; some registry entries are named `NotImplemented` (seen in a career run's `events.txt`), probably slots of removed screens.
- The FM4 image has none of the Lua game API that FM2's behaviours call (`ForzaLib`, `DB.execute`, `RegisterDatasource`, `MakeDataBound`): FM4's screens take their data from C++ through the contract.

World Tour as played in the e2e career run creates `19_DYNAMIC_LOADING_WORLDTOUR`, `200_WORLD_TOUR`, `204_C_RACE_INFO`, the pre-race, race and post-race scenes, and `102_H_CAR_HOME`. The registry also names `201_SPECIAL_EVENT_DIALOG`, `202_WORLD_TOUR_START`, `203_SEASON_STATUS`, `207_SEASON_LIST_DIALOG`, `208_SEASON_END`, `242_C_CAL_GRID`, `245_C_CAL_GIFTCAR`, `909_LEVEL_PROGRESS` and `910_POST_RACE_LEVEL_REWARDS`.

## 5. FM3's and FM2's career screens

| | FM3 | FM2 |
| --- | --- | --- |
| UI archive | `Media\UI.zip`, LZX, 350 files | `Media\UI.zip`, deflate, 874 files |
| Scenes | 112 Anark BGF scenes in `scenes/ui4/`; the same `.bgf` and `.bsg` headers as FM4, `.fbf` version `0x3EE` | 173 `.xgf` scenes (an older Anark format) in `Scenes/<A,C,I,P,X>_Scenes/` |
| Navigation | scene data, as FM4 | 182 `Flows/*.xml` (for example `CareerEventSelect.xml`, `CareerMenu.xml`) naming scenes, packages and sub-flows |
| Logic | C++ behind the contract, as FM4 | 405 Lua 5.1 behaviours that run SQL and call `ProfileManager`, `Lobby.activePlayer.garage.homeRegion`, `DB.execute` and `ForzaLib` |
| Career scenes | `203_c_cal_main`, `205_d_cal_message`, `235_c_cal_register_event`, `242_c_cal_grid`, `243_c_cal_year_end`, `244_c_cal_eventresults`, `245_c_cal_giftcar`, `246_d_series_standings`, `909_level_progress`, `910_post_race_level_rewards` | `C_MYC_MyCareer`, `C_MYC_CareerDifficulty`, `C_TYP_RaceType`, `C_EVE_EventSelect`, `C_TRK_TrackSelect`, `C_BUY_RegionManufacturerSelect`, `C_WIN_Career_Trophy` |
| Shared with FM4 | 86 of 112 scene names | none |

FM3's calendar scenes that FM4 does not ship (`203_c_cal_main`, `243_c_cal_year_end`, `244_c_cal_eventresults`, `235_c_cal_register_event`) are also absent from FM4's registry. Some of their C++ classes remain (`CCalendarEndOfYear`, `CCalendarCarCheck`, `ICareerCalendarObserver`), so parts of FM3's calendar code survived in FM4.

### Ways to show them

1. **FM4's own World Tour screens with FM3 or FM2 data.** No UI change: events, seasons, restrictions and rewards come from the database patch, the texts from string tables. The look stays FM4's. This is the base for every other way.
2. **FM3 scenes loaded by FM4.** The scene format matches, so an FM3 scene can be placed in the mod disc's `UI.zip`. It needs a registry entry (appending to the 249 entries in `sub_824E2790`, or taking a `NotImplemented` slot) with a factory written on the host side, and a controller that fills the scene's contract (for `203_c_cal_main`: `parent.Layer.C_CAL_MAIN_LIST`, `TEXT_CALENDAR_DAY`, `TEXT_EVENT_TYPE`, `Material_Event_Poster` and so on). Writing that controller means knowing the layout of FM4's scene base class (the race event controller's base constructor is `sub_824E0608`) and how a contract binds controls. Whether FM4's loader accepts the `.fbf` version `0x3EE` is unknown.
3. **FM2 scenes.** Not loadable: the `.xgf` format is older and their logic is Lua against an API FM4 does not have. An FM2-style career is either FM4's or FM3's screens with FM2's data (way 1 or 2), or a host overlay (way 4).
4. **A host overlay.** A career hub drawn by the host (Dear ImGui is already in the launcher) over the title, with FM4's textures and fonts, taking the pad input while it shows. It is fully under our control and can show FM2's structure (regions, car level), but it must hand the chosen event to FM4's race flow, and no setter for the current event (`*(0x82DC36E8)`) is known yet ([direct race launch](direct-race-launch.md#what-a-direct-launch-needs)).
5. **Editing FM4's scenes.** Changing a scene's layout needs a BGF writer; Anark's authoring tools are not available. Texts can change through string tables without touching the scenes.

## 6. Layers and estimates

Estimates are AI calendar time.

| Layer | Contents | Estimate |
| --- | --- | --- |
| A | FM3 or FM2 events as FM4 rows (way 1), with cars and tracks that exist in FM4 | 1 to 3 days |
| B | the launcher's career choice, a save slot per career, the database patch on the mod disc | about 1 day after A |
| C | FM3 or FM2 progression rules (FM3's gift cars per level, FM2's prize money, car level and region unlocks) through hooks in FM4's career code | weeks |
| D | FM3 scenes in FM4 (way 2) | about a week for the first scene, if the loader accepts FM3's files |
| E | FM2's own tracks for an FM2 career | months; it waits for a track converter |

## 7. Open questions

- The format of `PlayerDatabase` behind `cmss` (encrypted SQLite is inferred), and whether `GameplayLog` or `VersionFlags` hold career state.
- What `Calendar.cal` holds.
- Whether FM4's career code accepts new `CareerEventTypes` rows, or only its own types.
- The string table key algorithm (`_&<number>`), needed for new event names ([DLC structure](dlc-structure.md#4-open-questions-for-fm3-and-fm2-conversion)).
- Whether FM4 loads an FM3 `.fbf` (`0x3EE`), and which registry entries are `NotImplemented`.

## 8. Next steps

1. **Clone event test**: one new event series in a `merge.slt` on the mod disc; pass if it shows in World Tour or the event list and can be raced.
2. **Save slot setting**: find where the runtime builds the profile path and add a slot name that maps `B13EBABEBABEBABE\4D530910\00000001` and its header to a per-career folder.
3. **FM3 career inventory**: FM3's `Events`, `Races`, `EventRestrictions`, `EventParticipants`, `ChampSeries` and `Rewards_DriverLevelv3` against FM4's columns, and which of their tracks and cars FM4 has.
4. **Scene probe**: replace FM4's `242_c_cal_grid` with FM3's (both games ship it) on the mod disc and record whether the title loads it; then dump the registry's 249 names to find the `NotImplemented` slots.
