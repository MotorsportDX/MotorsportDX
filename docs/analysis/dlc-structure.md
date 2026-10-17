# FM4 DLC Structure (17 October 2026)

How Forza Motorsport 4 (title `4D530910`) registers cars and tracks from content packages. It is the basis for packaging converted FM2 and FM3 assets as mod DLC. The findings come from the 12 packages imported with `-Content` (four Disc 2 `PIRS` packages and eight `LIVE` packs), their `.header` files, the Disc 1 `Media` folder, the runtime log of a Quick Race run with all 12 mounted, and the runtime content code. This note contains only file names, table names, column names and short identifiers, not game data. Tools used: Python `zipfile` and `sqlite3`, with zip method 21 decompressed by the runtime's `lzx_decompress` (as in [material_atlas.py](../../outputs/FM4-build/e2e/material_atlas.py)); the extracted files stay outside the repository.

Status labels: **observed** (seen in the files or the log), **code** (read in the runtime source), **inferred** (consistent with the evidence, not yet tested in game).

## 1. Package layout

### Common layout (observed)

An imported package is a plain directory under `user-data\0000000000000000\4D530910\00000002\<package name>\`. Every one of the 12 has the same skeleton:

```
<package>\
  Media\
    <PubOfferingID>.puboffer        3 bytes (" \r\n"); the name is the pack's PubOfferingID
    LicenseMasks                    Disc 2 only, empty file
    DLCZips\
      zipmount.xml                  zips to mount at fixed game: paths
      stringtables.xml              LIVE packs only: string tables the pack replaces
      <stem>_pri_<n>.zip            content archive, overlaid on game:\ (entries start with media/)
      <stem>_pri_<n>\Media\...      loose files of the same overlay (audio banks, track collision, AI)
      <name>_pri_<n>.zip            archives listed in zipmount.xml
      <name>_pri_<n>\               loose directories listed in zipmount.xml (pack 34 only)
  spa.bin                           pack 34 only
```

- `PubOfferingID` values: Disc 2 `1` to `4`; LIVE packs `21`, `23`, `25`, `30`, `32`, `34`, `35`, `39`. It matches `ContentOffers.PubOfferingID` in the database (see below).
- `zipmount.xml` entries have `Name`, `Mount` (a `game:\` path), optional `AltRootPath` (same path) and `ShouldCache="0"`. Mount targets seen: `game:\Media\StringTables\`, `game:\Media\cars\shared\cameras\`, `game:\Media\Audio\cars\CarModelTuning\`, `game:\Media\Tracks\<Track>\bin\`, `game:\Media\cars\<MediaName>\`, `game:\Media\audio\UI\`, `game:\Media\audio\VO\EN\`, `game:\Media\GameTunableSettings\`. A `Name` without `.zip` mounts a loose directory.
- The main archive `<stem>_pri_65.zip` is not listed in `zipmount.xml`. The title addresses it as `DLCRoot<k>:\media\dlczips\<stem>\...`, a root whose contents mirror `game:\` (entry names start with `media/`). The loose directory `<stem>_pri_65\Media\...` beside the zip holds files that are not in the zip (engine `.fsb` banks, track `.col` collision, `AI\Tracks`, track audio), so the overlay is the union of both (inferred).
- Zip entries use method 21 (XMemCompress LZX, 128 KB window) or method 0 (stored). No other method appears.
- String tables are binary `.str` files (magic `LSB2`), one per table and language (`EN`, `DE`, `JP`, ... 16 languages). Database text columns hold keys of the form `_&<number>`.

### Car pack (observed)

Example: the pack with `PubOfferingID` 25 (ten cars). Main archive `5c00_pri_65.zip`, 597 entries:

| Path in the archive | Contents |
|---|---|
| `media/cars/<MediaName>/` | `<medianame>.carbin`, `_lod0.carbin`, `_cockpit.carbin`, caliper and rotor `.carbin` per wheel, `damage*.xds`, `lights*.xds`, `nodamage*.xds`, `interior_lod0.xds`, `carattribs.xml`, `shadersettings.xml`, `physics/maxdata.xml`, `digitalgauge/` (`.bgf`, `.bsg`, `.fbf`, `.xds`), `liverymasks/` (`*.tga`, `masks.xml`), `versiondata.xml`, `buildnumber.txt` |
| `media/wheels/<MediaName>/` | `<medianame>.carbin`, `wheel*.xds`, `versiondata.xml` |
| `media/UI/Textures/` | `Thumbnails/Thumbnail_<CarId>.xds`, marketplace banner, part-maker logos |
| `media/audio/cars/` | engine sound banks (some also loose beside the zip) |
| `media/db/patch/<n>_merge.slt` | database patch (below) |

Other files in car packs: `cameras_pri_<n>.zip` (one `ThumbnailCamSettings.xml`), `CarModelTuning_pri_<n>.zip` (`<MediaName>_CMT.xml` per car), `StringTables_pri_<n>.zip`, and in one pack a whole car as its own zip (`PAG_ZondaCinque_09_SLOD_pri_173.zip`, mounted at `game:\Media\cars\PAG_ZondaCinque_09_SLOD\`). Pack 34 also has `media/expansion.sku`, `media/GameTunableSettings/BadgesAndTitles/<n>.xml`, `media/Livery/decals/*.carbin`, a per-pack string table (`media/stringtables/<lang>/Porsche.str`) and `<n>_execute.sql`.

The per-car file set is the same as a Disc 1 car (`Media\cars\<MediaName>.zip`, flat). Disc 1 cars are separate zips listed in `Media\zipmanifest.xml` (priority 80, with precomputed central directory offsets); DLC cars are folders inside the pack archive.

The four Disc 2 packages contain only `media/cars` and `media/wheels` (2,935 entries in package 1) and a `StringTables` zip. They have no database patch: their 188 cars are already in the Disc 1 database, mapped to offers with `PubOfferingID` 1 to 4.

### Track pack (observed)

The pack with `PubOfferingID` 21 adds two environments, `PhysicsTrack` (16 ribbons including reverse layouts) and `Soccer` (2 ribbons):

| Location | Contents |
|---|---|
| `0015_pri_65.zip` `media/Tracks/<Track>/` | `TrackSettings.xml` (and `TrackSettings1.xml`), `<Track>_ribbonNN.sh`, `staticcarcubemap.xpr`, `Ribbon_NN/` (`CollObjs.xml`, `<Track>_NN.pvs`, `<Track>_track_NN.crowd`, `TrackSegCams.xml`) |
| loose `0015_pri_65\Media\Tracks\<Track>\Ribbon_NN\` | `<Track>_track_NN.col` (collision) |
| loose `0015_pri_65\Media\AI\Tracks\` | `Level1..3\<Track>_NN[_R].bin`, `SegmentProfiles\<Track>_NN[_R]\` |
| loose `0015_pri_65\Media\Audio\Tracks\<Track>\Ribbon_NN\` | track audio |
| `0015_pri_65.zip` `media/UI/Textures/UI4/` | track logos, ribbon maps, track images, marketplace banners |
| `<Track>_pri_65.zip`, mounted at `game:\Media\Tracks\<Track>\bin\` | 3,808 entries: `_0x000000NN.bin`, `_0x000000NN.bix` / `_B.bix`, `VersionData.xml` (the equivalent of a Disc 1 `Media\tracks\<Track>\bin.zip`) |
| `0015_pri_65.zip` `media/db/patch/21000_merge.slt` | database patch |

Disc 1 tracks use the same folder layout under `Media\tracks\<Track>\` (`Ribbon_NN`, `*.sh`, `TrackSettings.xml`, `bin.zip`, `staticCarCubemap.xpr`).

### Database patch format (observed)

`media/db/patch/<n>_merge.slt` is an SQLite 3 database, like the Disc 1 database `Media\db\gamedb.slt` (198 tables). Each patch holds only tables that also exist in `gamedb.slt`, with identical column lists, and contains only the rows to add. Pack 34 also ships `100001_execute.sql`: plain SQL `UPDATE` statements on `CareerEventTypes` and `Events`. The numeric prefix probably orders the patches (inferred).

Most new row IDs do not exist in the base database. Some patches repeat base rows (for example `List_FamilyModel`, `List_PartManufacturer`, `Livery_Decals`, `GraffitiPoints`, and one `Events`/`Races` row), so the merge must tolerate or replace duplicates. The tables declare no primary keys, so the merge rule is the title's own (inferred: insert or replace by `Id`).

Tables that add a **car** (7 car patches; the counts are how many patches contain the table):

- Identity: `Data_Car` (`Id`, `MediaName`, `MakeID`, `ClassID`, `CarTypeID`, `FamilyModelID`, performance figures, ...), `Data_CarBody` (wheelbase, track width, ride height, bounding box), `Data_Engine`, `Data_Drivetrain` (all 7).
- Physics and upgrades, keyed by car: `List_AeroPhysics`, `List_AntiSwayPhysics`, `List_SpringDamperPhysics`, `List_TorqueCurve`, and 40 `List_Upgrade*` tables (engine parts, drivetrain, body, tires, rims, wings) (all 7, except `List_UpgradeEngineDSC` 6 and `List_UpgradeEngineRestrictorPlate` 4).
- Presentation: `List_Wheels` (stock wheel per car, `MediaName`), `CarPartPositions`, `CameraOverrides`, `CarTrackOffsets`, `CarExceptions` (no mirrors, no windows, ...), `Combo_Colors`, `Combo_Colors_Exceptions`, `StripeColors`, `Livery_Decals`, `Livery_DecalsSortOrder`.
- Lookups when the car brings a new make or model: `List_CarMake`, `List_FamilyModel`, `List_PartManufacturer`.
- Store and unlocks: `ContentOffers`, `ContentOffersMapping` (all 8 patches), `EventParticipants`, `List_BadgeAndTitleUnlocks`, `Rewards_DriverLevelv3`; events in some packs: `Events`, `Races`, `RacesSpecial`, `EventRestrictions`, `EventShowroomChallenges`, `CareerEventTypes`.

Tables that add a **track** (track pack): `Environments` (`Id`, `MediaName` = track folder, map position), `EnvironmentTimeOfDay`, `Tracks` (one row per layout and direction: `id`, `MediaName`, `RibbonIndex`, `ReverseIndex`, `IsReverse`, `Length`, `EnvironmentId`, `DefaultStartGridPositionsId`, logo and ribbon image paths, `IsInstalled`, `IsPurchased`, lap counts, ...), `StartGridPositions`, `SplitPoints`, `PittingProfiles`, `TrackStartingMerges`, `GraffitiPoints`, plus `ContentOffers` and `ContentOffersMapping`.

Store tables:

- `ContentOffers`: `ID` (offer ID as hex text, `4D530910xxxxxxxx`), `OfferId` (the same 64-bit value), `DisplayName`, `Description`, `PubOfferingID`, `LicenseLevel`, `LicenseMask`, `IsDLC`, `ImagePath`, `ThumbPath`, `IsInstalled`, `IsPurchased`, `IsVIP`, `Hidden`, `IsALaCarte`, `SeasonPassOffer`.
- `ContentOffersMapping`: `Id`, `OfferId`, `ContentId`, `ContentType`. `ContentType` 1 is a car (`ContentId` = `Data_Car.Id`), 3 is an environment (`ContentId` = `Environments.Id`); the base database also uses 2.
- A car pack has one bundle offer (mask `1`, mapped to all its cars) and one a-la-carte offer per car (masks `2`, `4`, ... `256`, `IsALaCarte` 1). The track pack has a hidden bundle (mask `3`) and one offer per environment (masks `1` and `2`). Disc 2 offers in the base database have `LicenseMask` 0 and `IsPurchased` 1.

String tables follow the database: `stringtables.xml` names the tables a pack replaces (for example `ContentOffers`, `Data_Car`, `List_CarMake`, `Tracks`, `Environments`), and the `StringTables_pri_<n>.zip` holds the full table, not a delta: the pack's `EN/Tracks.str` is larger than the Disc 1 one and the Disc 2 `EN/Data_Car.str` has the same size as the Disc 1 file. `ThumbnailCamSettings.xml` in `cameras_pri_<n>.zip` is also a whole file that grows with each pack.

## 2. Mount order, priority and license

### DLCRoot order (observed, code)

- The title enumerates marketplace content with `XamContentCreateEnumerator`. The runtime's `ContentManager::ListContent` returns every directory in `0000000000000000\4D530910\00000002\`, in host directory order (sorted by name on NTFS). The title then opens each package as `DLCRoot0`, `DLCRoot1`, ... in that order: in the run with 12 packages, `DLCRoot0..3` are the Disc 2 packages `4d53091000000001..4` and `DLCRoot4..11` follow the LIVE folder names alphabetically (`7C0F...`, `94C6...`, `9724...`, `B894...`, `C813...`, `D1DA...`, `DBA9...`, `E347...`).
- The root index therefore does not express priority. Priority comes from the `_pri_<n>` suffix of each archive.

### `_pri_` priority (observed, inferred)

- Disc 1 archives in `zipmanifest.xml` use priority 80 (813 archives: cars, string tables) and 95 (6 archives: UI, livery, spectators, audio tuning).
- DLC content archives use `_pri_65`, below the disc, which is safe because they only add new paths.
- Files that replace a disc file use higher values that grow with each release: `StringTables_pri_151..154` (Disc 2), `171`, `173`, `23702`, `25030..25039` (later packs); `cameras_pri_25030..25039`; `CarModelTuning_pri_25034/25035`; the single-car zip `_pri_173`.
- Inferred: for one path, the archive with the highest priority wins. Every pack therefore ships a cumulative copy of each replaced string table and of `ThumbnailCamSettings.xml`. A mod that replaces such a file must start from the highest-priority copy installed, add its own entries, and use a higher `_pri_` value. Two mods that replace the same table conflict.

### Probes in the log (observed)

At boot the title tries to open, per root:

- `DLCRoot<k>:\media\dlczips\stringtables.xml` (fails only for the Disc 2 roots, which have none);
- `game:\media\db\patch\` and `DLCRoot<k>:\media\dlczips\<stem>_pri_65\media\db\patch\` as loose directories (all fail: the patches are inside the zips);
- `DLCRoot<k>:\media\dlczips\<stem>_pri_65\Media\GameTunableSettings\BadgesAndTitles\` and `gametunablesettings_pri_25034\BadgesAndTitles\`.

Successful opens are not logged, so the log does not prove which `merge.slt` files were applied. `REX_FILE_READ_TRACE` ([traces](../e2e/traces.md#file-and-io)) records them.

### License mask (code, observed)

- `XamContentCreate(Ex)` returns a per-package license mask. The runtime reads it from the dword after the 0x148-byte `XCONTENT_AGGREGATE_DATA` in `Headers\00000002\<package>.header` (`ContentPackage::LoadPackageLicenseMask`); without that dword the mask is 0. The installer writes it as the OR of `license_bits` over the STFS header's license entries that have nonzero `license_flags`. The dword is written in host byte order (little-endian), unlike the big-endian `XCONTENT` fields.
- Values on the imported packages: Disc 2 headers are 328 bytes (no dword, mask 0); LIVE headers are 332 bytes with `0x00000001` (packs 25 and 35), `0x00000003` (track pack 21) and `0xFFFFFFFF` (packs 23, 30, 32, 34, 39).
- `XamContentGetLicenseMask` (the title's own license) returns the `license_mask` cvar, default 0.
- Inferred: the title compares a package's mask with `ContentOffers.LicenseMask` to set `IsPurchased` per offer: bundle bit 1 unlocks all cars of a car pack, mask 3 unlocks both environments of the track pack, `0xFFFFFFFF` unlocks everything. Not verified: DLC cars and tracks have not yet been checked in game menus.

### Signatures (code)

- The runtime's STFS reader (`StfsContainerDevice::ReadHeaderAndVerify`) checks only the magic (`CON `, `LIVE`, `PIRS`), not the RSA signature or the hash tables; the launcher and the installer add the title ID and content type checks. After import the package is a host directory without a signature.
- The title opens content through `XamContentCreate` and plain file I/O. No content verification call was seen; `XamMediaVerificationCreate` (disc media) is a stub. Zip entry CRCs must still be correct if the title checks them.

## 3. Minimal new package

A hand-made directory under `00000002` should work without STFS packing (inferred from the code path above; not yet tested):

- **Folder:** `user-data\0000000000000000\4D530910\00000002\<name>\`, name at most 42 characters (`XCONTENT_DATA.file_name`). Its alphabetical position sets its `DLCRoot` index only.
- **Header (optional):** `user-data\0000000000000000\4D530910\Headers\00000002\<name>.header`, 0x148 bytes of big-endian `XCONTENT_AGGREGATE_DATA` (device ID, content type `2`, UTF-16 display name, file name, XUID 0, title ID `4D530910`) followed by a little-endian license dword, for example `0xFFFFFFFF`. Without a header the runtime synthesizes the content data from the folder name and reports license 0, which may leave the content unpurchased.
- **`Media\<PubOfferingID>.puboffer`** with an unused `PubOfferingID`.
- **`Media\DLCZips\<stem>_pri_65.zip`** with:
  - `media/cars/<MediaName>/` and `media/wheels/<MediaName>/` (or the track folders), in FM4 formats;
  - `media/db/patch/<n>_merge.slt`: an SQLite file with the needed tables (schemas copied from `gamedb.slt`), new IDs for `Data_Car` and every keyed table, `ContentOffers` (new offer ID `4D530910xxxxxxxx`, `PubOfferingID`, `LicenseMask`, `IsDLC` 1) and `ContentOffersMapping`;
  - UI thumbnails (`Thumbnail_<CarId>.xds`).
  - Stored entries (method 0) appear in official packs, so an uncompressed zip is likely accepted; method 21 is the safe choice for large files. Deflate is untested.
- **`Media\DLCZips\zipmount.xml`** and, for new names, **`stringtables.xml`** with a `StringTables_pri_<n>.zip` holding full copies of the changed tables at a priority above all installed packs, plus a camera file if the car needs a thumbnail camera.

Launcher behaviour: `-Content` accepts only STFS files, so a directory mod is placed by hand (or a later launcher option copies it). The launcher does not delete unknown folders in `00000002`.

## 4. Open questions for FM3 and FM2 conversion

- **Model format:** FM4 cars are `.carbin` (body, `lod0`, cockpit, calipers, rotors, wheels) plus `.xds` textures. Which `.carbin` versions FM3 and FM2 use, how their vertex layouts, LODs and damage models differ, and whether a converter can write FM4 `.carbin` (including the cockpit and the separate wheel model).
- **Materials:** FM4 shader names come from `shadersettings.xml` and the `.fxobj` set ([material atlas](material-atlas.md)). FM3 and FM2 materials must be mapped onto existing FM4 shaders; FM2 likely needs the most remapping.
- **Textures:** Xbox 360 tiled formats in `.xds` (and `.xpr` for track cube maps); whether FM3 and FM2 containers convert one to one.
- **Database rows:** An FM4 car patch touches about 60 tables. Whether FM3's database (and FM2's, whose format is unknown here) carries enough physics data to fill `Data_Car`, `Data_Engine`, `Data_Drivetrain`, `List_TorqueCurve` and the upgrade tables, or whether rows are cloned from a similar FM4 car and edited.
- **Text keys:** database text is stored as `_&<number>` keys into `LSB2` string tables. The key algorithm (a hash of the string ID or an index) and whether literal text works in a text column are unknown. Writing `LSB2` files is required for new names.
- **Sound:** engine banks are named by engine type (`8N2_LS7_*`, ...) and tied to cars by `CarModelTuning` XML. Reusing FM4 banks first avoids converting FM2 and FM3 audio.
- **Tracks:** the hardest part. A track needs the `bin` archive (`_0x000000NN.bin/.bix` resources), per-ribbon collision (`.col`), visibility (`.pvs`), crowd, cameras, AI lines (`AI\Tracks\Level1..3`, `SegmentProfiles`), start grids, split points and pit profiles. The `.bix`/`.bin` format and the AI data must be understood before any FM3 or FM2 track can be converted; many FM3 tracks already exist in FM4.
- **IDs:** a reserved range for car, wheel, upgrade, environment, track, offer and mapping IDs that avoids the base database and all official packs.
- **Merge rule:** whether a patch row with an existing ID replaces the base row or is duplicated, and whether `_execute.sql` runs before or after the merges.

## Next experiments

1. Boot with `REX_FILE_READ_TRACE` and the 12 packages, and list the reads of `*.puboffer`, `zipmount.xml`, `stringtables.xml`, `*_merge.slt` and `*_execute.sql` per root and their order.
2. Check DLC cars and tracks in the car list, Autovista and the track list; then, on a test profile, change one pack's `.header` license dword (0, 1, missing) and record which offers become purchased.
3. Clone test: a hand-made folder with a header, a `.puboffer`, a stored zip that copies an existing FM4 car under a new `MediaName`, and a `merge.slt` with that car's rows under new IDs and a new offer. Pass if the car appears and drives.
4. Compression test: the same clone with method 0, method 21 and deflate entries.
5. String tables: find how `_&<number>` keys are derived (compare known string IDs and keys) and add a new name through a higher-priority `StringTables` zip.
6. Inventory the FM3 and FM2 discs (file lists, `.carbin` headers, database format) without copying assets, and record the differences against the FM4 layout above.
