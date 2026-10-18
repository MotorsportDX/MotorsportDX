# FM4 Releases: Japan/Asia and USA (18 October 2026)

How the Japan/Asia release (Play Disc media ID `633772A0`) and the USA release (`26FF1101`) of Forza Motorsport 4 differ, and why one set of integration files serves both. The comparison covers every file of both Disc 1 and Disc 2 extractions (SHA-256 per file), the XEX headers of the three executables, and their decrypted images. This note contains only file counts, header fields and short identifiers, not game data. The tools used (an XEX header reader and a decryptor for the retail key and basic compression) stayed outside the repository.

## Summary

- The game code is the same. The decrypted PE images of `default.xex`, `SpeechFacade_default.xex` and `XMediaFacade_default.xex` are identical between the releases, and so is the loaded image that `generate` writes (`loaded-image.bin`).
- The game assets are the same. Apart from the executables, every file of Disc 1 and Disc 2 is identical.
- Only the XEX headers differ: media ID, disc version, game region, ratings, LAN key, the multidisc media ID list, and the header digest and signature that cover them. Even the encrypted image data after the headers is byte for byte the same; 350 header bytes differ in each executable.
- The function bounds, initializer, callback and switch recipe files therefore apply to both releases unchanged, and `pipeline.py` accepts the executables of either release as a set (`RELEASES`).

## Disc contents

| Disc | Files per release | Identical | Different |
|---|---|---|---|
| Disc 1 (Play Disc) | 4,062 (8.1 GB) | 4,059: `Media`, `Database.xmplr`, `hash.manifest`, the Kinect speech and identity files and the rest | the three executables |
| Disc 2 (Content Install Disc) | 95 (3.2 GB) | 94, including the four content packages under `content\0000000000000000\4d530910\00000002` | `default.xex`, the console installer, which the workflow does not use |

Both releases have the same file names and folder layout. `hash.manifest` is identical on both discs, which agrees with the identical assets.

## Executable headers

The same fields differ in all three executables:

| Header field | Japan/Asia | USA |
|---|---|---|
| Execution info: media ID | `633772A0` | `26FF1101` |
| Execution info: version and base version | 11 | 8 |
| Security info: media ID (16 bytes) | ends in `633772A0` | ends in `26FF1101` |
| Security info: game region | `0000FD00` (Japan and the rest of Asia, not China) | `000000FF` (North America) |
| Game ratings | a CERO entry only | ESRB and Brazil entries |
| LAN key | differs | differs |
| Multidisc media IDs | the Japan/Asia Disc 1 and Disc 2 IDs | the USA Disc 1 and Disc 2 IDs |
| Header digest and RSA signature | differ | differ |

The other fields are identical: title ID `4D530910`, disc 1 of 2, entry point, image base, image size, the encrypted file key, the section digest, import libraries (with their versions and digests), checksum timestamp, original PE name, static libraries, TLS, stack size, system flags, title workspace size, the Xbox 360 logo, the alternate title IDs and the base file format (normal encryption, basic compression).

The title update file names follow the execution info: `633772A0\tu0000000b_00000000` for Japan/Asia and `26FF1101\tu00000008_00000000` for USA. Title updates are not supported for either release.

## What the runtime sees

- **Region and language**: the runtime does not read the header region. `XGetGameRegion` returns `FFFF` and `XGetLanguage` returns English for every title ([xam_info.cpp](../../work/runtime/src/kernel/xam/xam_info.cpp)), so both releases run with the same region and language.
- **Execution ID**: `XamGetExecutionId` returns the execution info of the executable that was loaded, so the game code sees the media ID and version of the release on Disc 1. The game code imports it in two places.
- **Content**: the title ID is the same, so the Disc 2 packages and downloadable content packages are the same for both releases. The content installer ([fm4-content-installer.cpp](../../work/fm4-rex/src/fm4-content-installer.cpp)) checks only the title ID.
- **Generated code**: the codegen and the switch tables read only the image, which is identical. The generated sources do not contain the Disc paths.

## Checked on a USA build

A new BuildRoot set up, generated and built from the USA discs on 18 October 2026 produced the same loaded image, switch tables and generated sources as the Japan/Asia discs. It imported the USA Disc 2 and the DLC packages, booted to the intro with a new profile, and passed the smoke gate with the `dlc` cell on a profile made by the Japan/Asia executable. The results are in [current status](../status/current.md#usa-release-18-october-2026).

Not compared: the European release, and the title updates of either release.
