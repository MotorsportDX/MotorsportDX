# FM4 Title Updates (18 October 2026)

What the Forza Motorsport 4 title updates contain, what they change in the executables, what the runtime already supports, and what supporting one would take. Three packages were examined: Japan/Asia v1 (for media ID `633772A0`), and USA v1 and USA v4 (for `26FF1101`). The packages were read with an STFS reader, patched with the runtime's own XEX loader, and the resulting images compared through their PE section tables and function tables (`.pdata`). This note contains only counts, file names, header fields and short identifiers, not game data. The tools and the extracted files stayed outside the repository. Title updates are not supported by the workflow today; see [release comparison](releases.md) for the base releases.

## Summary

- A title update replaces the executable with a new link of the game, not a small fix. USA v1 changes about 72 functions, but almost every function moves: of 36,820 functions in the base `default.xex`, 2 keep their address and their bytes. Every address in the integration (function bounds, switch recipes, hooks, vtables and globals) therefore needs porting.
- Japan/Asia v1 and USA v1 patch the identical base images into identical images, for all three executables, and their asset files are identical. One v1 integration profile would serve both releases, as the base profile does.
- USA v4 changes more (about 257 functions) and moves the sections after `.data`. The v4 asset archive includes the v1 track fixes.
- The runtime already applies a `.xexp` patch that sits next to its `.xex`, both in codegen and at launch, and can mount a folder as `update:`. One defect stops the USA v4 patches of `SpeechFacade_default` and `XMediaFacade_default`. A one-line fix was verified in a scratch build (see [Runtime support](#runtime-support)).
- Supporting one title update is a second integration profile plus pipeline and runtime work, roughly a week of focused work (see [What support would take](#what-support-would-take)).

## Packages

All three are `LIVE` STFS packages of content type `000B0000` (title update) for title `4D530910`. The header names the media ID and disc version of the release they apply to, and that version is in the file name.

| Package | Media ID | Version in header | Display name | Size |
|---|---|---|---|---|
| `633772A0\tu0000000b_00000000` | `633772A0` | 11 | Forza Motorsport 4Title Update | 7,135,232 bytes |
| `26FF1101\tu00000008_00000000` (v1) | `26FF1101` | 8 | Forza Motorsport 4 Title Update #1 | 7,135,232 bytes |
| `26FF1101\tu00000008_00000000` (v4) | `26FF1101` | 8 | Forza Motorsport 4 Title Update | 8,245,248 bytes |

Files (sizes for v1, then v4):

| Path | v1 | v4 |
|---|---|---|
| `disc001\default.xexp` | 1,388,544 | 1,368,064 |
| `disc001\SpeechFacade_default.xexp` | 4,096 | 452,608 |
| `disc001\XMediaFacade_default.xexp` | 4,096 | 452,608 |
| `disc001\Media.zip` (v4: `media.zip`) | 716,308 | 941,852 |
| `disc001\Media\tracks\LeMans\patch_bin.zip` | 4,918,248 | 4,918,248 |
| `disc002\default.xexp` (the Disc 2 installer) | 4,096 | 4,096 |

- `Media.zip` and `patch_bin.zip` are identical in Japan/Asia v1 and USA v1, and `patch_bin.zip` is identical in all three. The `.xexp` files differ between the two v1 packages, because each delta patch is bound to its base executable (the SHA-1 of the base XEX signature, and its image key).
- Each `.xexp` is a delta patch (module flags `0x50`, delta compression) from the base version to version 1 or 4 (for example 0.0.0.8 to 0.0.4.8).
- `Media.zip` holds `tu_mount.xml`, which mounts `Media\tracks\LeMans\patch_bin.zip` at `game:\Media\tracks\LeMans\patch_bin\`. v1 adds two LeMans visibility patches (`.pvspatch`) and a LeMans track image. v4 has those too, plus three database patch scripts (`db\patch\*.sql`), a `titleupdate2` string table in 16 languages, car points of interest for the 1982 DeLorean DMC-12, a decal and Wiesmann logo textures. `patch_bin.zip` holds 226 LeMans track files.

## What the patches change

`default.xex`, as loaded (PE headers and sections), against the base image:

| | Base | v1 | v4 |
|---|---|---|---|
| Entry point | `82759E30` | `82758AB0` | `8275F9F0` |
| `.text` | 11,145,244 bytes | 11,163,204 | 11,189,244 |
| Functions in `.pdata` | 36,820 | 36,893 | 36,991 |
| Base functions at the same address with the same bytes | - | 2 | 0 |
| Base functions with no match in the patched image, ignoring relocations | - | 72 | 257 |
| Base functions found in order in the patched image, ignoring relocations | - | 35,925 (97.6%) | 35,730 (97.0%) |
| Sections after `.data` | - | same addresses | moved (`.reloc` from `82EC0200` to `82ED0200`) |

"Ignoring relocations" masks branch targets and 16-bit immediates, so a function that only moved, or only calls moved code, still matches. The functions with no match are those the update changed, split or removed.

`SpeechFacade_default` and `XMediaFacade_default`: v1 changes 6 words in `.rdata` and 1 word in `.text` (probably version data); every function keeps its address and bytes. v4 has the same code, and its images grow from `240000` to `2F0000` bytes and from `2B0000` to `360000` bytes: the added pages lie after the PE sections and hold compressed PNG data, not code. The one entry in `speech_bounds.toml` and the two in `xmedia_bounds.toml` stay valid for both updates.

## Runtime support

- **Patch loading**: `UserModule::LoadFromFile` ([user_module.cpp](../../work/runtime/src/system/user_module.cpp)) looks for `<path>p` next to every XEX it loads and applies it with `XexModule::ApplyPatch`. The codegen loads the executable the same way (`Runtime::LoadXexImage`), so a `default.xexp` next to `default.xex` in the game root is applied before codegen and at launch.
- **`update:`**: the `update_data_root` cvar mounts a host folder as `update:` ([runtime.cpp](../../work/runtime/src/system/runtime.cpp)). FM4 asks for `update:\media.zip` at start; every run today logs `ResolvePath(update:\media.zip) failed`.
- **Defect**: `ApplyPatch` takes the length of the header patch from its first entry (`info.compressed_len + 0xC`). The USA v4 module patches start their header patch with a copy entry (compressed length 1), so the entries after it, including the new image key, are skipped. The patch then fails with `XEX patch image key doesn't match original XEX!` (code 7). Relaxing that check only moves the failure to the block hash check (code 9), so the check is right. The length of the header patch is the size of the delta descriptor minus the offset of `info`. With that one-line change, all nine patches (three executables of each package) apply, and the seven that applied before give byte-identical images.

## What support would take

1. **Runtime**: the header patch length fix in `XexModule::ApplyPatch`. It is needed for USA v4 and changes nothing for the patches that already apply.
2. **Pipeline**: accept a title update package and check it against the Disc 1 release (title ID, content type, media ID and version, and the SHA-256 of its `.xexp` files). Extract the three `.xexp` files and the update files into BuildRoot. Give codegen and the launcher a game root where the `.xexp` files sit next to the Disc 1 executables (for example a BuildRoot folder of links to Disc 1 plus the patches), and mount the extracted `disc001` folder with `update_data_root`.
3. **Integration profile for the patched image**: a second set of `fm4_bounds.toml`, `fm4_callbacks.toml`, `fm4_initializers.toml`, `fm4_prior_cfg.toml` and `switch-recipes.json`, and new addresses for the 117 guest functions that `work/fm4-rex/src` names (106 `REX_HOOK_RAW` hooks and the functions they call) and for its data addresses (vtables and globals). Those addresses are constants in the source today, so they need a table per profile. Matching in order with an 8-word window maps most of them automatically; the rest need the analysis that produced the base files:

| Base entries | Count | Mapped to v1 | Mapped to v4 |
|---|---|---|---|
| Guest functions named in `work/fm4-rex/src` | 117 | 113 | 114 |
| `fm4_bounds.toml` | 16 | 11 | 15 |
| `fm4_callbacks.toml` | 117 | 85 | 79 |
| `fm4_initializers.toml` | 432 | 413 | 424 |
| `fm4_prior_cfg.toml` | 748 | 652 | 378 |
| Switch recipe sites | 265 | 228 | 186 |

4. **Verification**: the e2e gates on the title update build. The v4 database patches and strings may change menus and saves; a profile saved with an update may not load in the base game.

The port costs about the same for v1 and v4, because both relink everything. If one update is supported, v4 is the better target: it is the newest examined, and its assets include the v1 track fixes. Only the USA v4 package was available; whether the Japan/Asia v4 package patches to the same image, as v1 does, is not verified. Not tested either: a codegen run on a patched image, and the game itself with an update.
