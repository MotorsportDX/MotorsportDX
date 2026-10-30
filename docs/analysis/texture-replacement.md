# Texture replacement

The runtime can write the game's textures to DDS files and sample edited DDS files in their place. A texture is named by a hash of its contents, so a replacement keeps working across runs, builds and the Japan/Asia and USA releases, the way Dolphin texture packs and Content Shaders Patch textures do. This is the route for new skies, leaves, billboards and a later night sky: the shader mods can recolor those surfaces but cannot give them new images, and editing the track archives (`bin.zip`, LZX-compressed, with an undocumented `.bin`/`.bix` format, 32 tracks) is not practical.

Usage is in [running.md](../running-look.md#texture-replacement). For the current state see [status/current.md](../status/current.md).

## Summary

- **Switches**: `texture_dump_path=<directory>` writes each new texture once as `<hash>.dds` and a line about it to `textures.txt`. `texture_replace_path=<directory>` samples `<hash>.dds` from the directory or any subdirectory instead of the texture with that hash. Both are empty (off) by default and need a restart. The launcher sets them with `-TextureDump` and `-TextureReplace`.
- **Hash**: XXH3 64 over the hash version, the guest format, the width and the height, then the base level's blocks row by row after untiling and endian swapping. Mips are not hashed.
- **Replacements**: any 2D DDS file of a supported format, of any size and with any number of mips. They are decoded on the CPU, uploaded once per hash and shared by every guest texture with that hash.
- **Not covered**: render targets, cube maps, arrays, 3D textures, and textures that change all the time (video planes, for instance).
- **Code**: [content_hash.h](../../work/runtime/include/rex/graphics/pipeline/texture/content_hash.h) and [dds.h](../../work/runtime/include/rex/graphics/pipeline/texture/dds.h) (header only), [replacement.cpp](../../work/runtime/src/graphics/pipeline/texture/replacement.cpp) (the guest read, the file store and the dumper), and the D3D12 texture cache ([texture_cache.cpp](../../work/runtime/src/graphics/d3d12/texture_cache.cpp)). CPU tests: [texture-replacement-tests.cpp](../../work/fm4-rex/tests/texture-replacement-tests.cpp).

## Where it hooks in

The texture cache keys a guest texture by its fetch constant (`TextureKey`: base and mip page, size, pitch, format, tiling, endianness, mip count, signedness). It creates the host texture on the first fetch and loads it in `LoadTextureDataFromResidentMemoryImpl`: a compute shader untiles the guest data from the GPU copy of guest memory into a buffer, which is copied into the texture. A texture is loaded again when the CPU writes to its memory (a write watch marks its base or mips outdated).

With either switch on, `LoadTextureReplacement` runs before that load:

1. If only the mips are outdated, a replaced texture skips the load (the replacement has its own mips) and a normal one loads its mips as before.
2. Otherwise the base level is read on the CPU from the CPU copy of guest memory (`ReadGuestBaseLevel`): the level's whole extent is endian swapped in 32-bit words as the GPU fetches it, then each block is copied out of the tiled or linear layout (including the packed mip tail of small textures) into tightly packed rows.
3. The rows are hashed. A hash that is not in the dump directory yet is dumped.
4. If the replacement directory has the hash, the texture points at the shared replacement resource and nothing is loaded. If the texture was replaced before and no longer is, it goes back to its own resource and loads its mips as well, since those were skipped.

A replaced texture's `resource()` is the replacement, which stays in the shader resource states, so the cache's barriers on it are no-ops. Its SRV descriptors are keyed with the replacement's id as well, and use the replacement's format and mip count. The draw's SRV keys include the replacement ids of the bound textures, so a reload that switches a texture to or from a replacement rewrites the descriptors even though the `TextureKey` is the same.

## The hash

| Input | Why |
| --- | --- |
| Hash version (1) | Lets a later change of the hashed data keep old packs from matching the wrong textures. |
| Guest format (`xenos::TextureFormat`) | The same bytes in another format are another image. |
| Width, height in texels | The same bytes at another size are another image. |
| Base level blocks, untiled, endian swapped, without padding | The image itself. |

What it leaves out is what changes between runs and releases without changing the image: the guest address, the pitch and its padding, the tiling, and the fetch constant's endianness. The signed and unsigned versions of a texture hash the same.

XXH3 is a frozen format (the shader override hashes use it too), and the runtime uses the inline, scalar-or-SIMD implementation of `rex/hash.h`. The CPU test checks a fixed value, `0d2101ca995d4d48` for its 64x32 DXT1 pattern, built for x86-64, x86-64-v2 and x86-64-v3 and at -O0 and -O2.

The file name is the hash as 16 lowercase hex digits; the loader accepts either case.

## Dumps

`texture_dump_path` writes the base level of each new hash once, in the DDS format whose blocks are the guest's blocks as they are, and appends a line to `textures.txt`:

```text
<hash> <guest format> <width>x<height> mips <count> address 0x<base address>
```

| Guest format | DDS format |
| --- | --- |
| `k_DXT1` | BC1 (`DXT1`) |
| `k_DXT2_3` | BC2 (`DXT3`) |
| `k_DXT4_5` | BC3 (`DXT5`) |
| `k_DXT5A` | BC4 (`ATI1`) |
| `k_DXN` | BC5 (`ATI2`) |
| `k_8_8_8_8` | RGBA8 |
| `k_8`, `k_8_A` | R8 |
| `k_8_8` | RG8 (DX10 header) |
| `k_2_10_10_10` | R10G10B10A2 (DX10 header) |
| `k_16_16_16_16_FLOAT` | RGBA16F (DX10 header) |

The `_AS_16_16_16_16` variants map like their base formats. Other formats (packed 16-bit colors, YUV, CTX1, 16-bit normalized and 32-bit formats) get a `textures.txt` line marked `not-dumped-format`, but no file; they can still be replaced, by creating `<hash>.dds` from scratch. Formats with a legacy DDS header are written with one, so older tools open them.

Files already in the directory when the game starts are not written again, so a dump can be continued over several runs.

## Replacements

`texture_replace_path` is scanned once at start, recursively, for `<16 hex digits>.dds`. A file is read and decoded the first time its hash is loaded, uploaded to a new host texture, and then dropped from the CPU cache; the host texture is kept for the whole run and shared by every guest texture with the hash. A file that fails to parse or to create is logged once and not retried.

| Property | Rule |
| --- | --- |
| Formats | BC1 to BC7 (BC6H unsigned), RGBA8, BGRA8, BGRX8, R8, RG8, B5G6R5, R10G10B10A2, RGBA16F. |
| Headers | Legacy (FourCC `DXT1` to `DXT5`, `ATI1`, `ATI2`, `BC4U`, `BC5U`, and RGB masks) and DX10. |
| Size | Any; block-compressed files must be a multiple of 4 in width and height (a Direct3D 12 rule). |
| Mips | Any count. Fewer than the full chain limits sampling to those; none means no mips. |
| sRGB | sRGB formats are read as their UNORM equivalents: the guest shaders decode gamma (and the Xenos piecewise gamma) themselves, so the stored values must be the same encoding as the original's. |
| Channels | The view applies the original's swizzle: the fetch constant's swizzle combined with the original host format's. A replacement for an RGBA texture reads as RGBA; one for a one-channel texture (`k_8`, `k_DXT5A`) only uses red, and one for `k_DXN` only red and green. |
| Signedness | A signed fetch of a replaced texture reads the replacement's format as it is (no signed view). |
| Not supported | Cube maps, volume textures, arrays. |

Uploads go through a committed upload buffer per replacement, freed once its submission completes, and are recorded on the current command list before the texture's first use.

## Limits

- **Render targets** are not hashed: a texture whose memory a resolve has ever written (`IsRangeEverResolved`) is skipped, because those texels are only in the GPU copy of guest memory. That bit is not cleared when the CPU later reuses the page for a normal texture, so such a reused page stays unreplaceable. Memory exports from shaders (`memexport`) are not tracked; a texture written that way would hash stale CPU data, but FM4 does not sample such textures as far as is known.
- **Textures that change all the time** are hashed until they have been loaded 8 times in a row, each load within 250 ms of the one before, then skipped (and shown as the original) until they stay unchanged for 250 ms. Dumps of such textures stop there too. The first version counted consecutive frames only and missed the attract video, whose Y, U and V planes (`k_8`, 1280x720 and 640x360, double-buffered) reload every 2 to 4 frames: they made 1,012 of the 2,005 dumped textures of a Quick Race. With the time window, a run of the same scenario dumps 2 of them (969 textures in all).
- **Mips** are not part of the hash. Two textures with the same base level and different mips match the same file, and the replacement's mips are used for both.
- **Cube maps, arrays and 3D textures** are not hashed. A sky drawn from a cube map would need cube support in the hash, the dumper and the views.
- **Resolution-scaled textures** (`scaled_resolve`) are render targets and are skipped.
- **Cost**: off, nothing. On, each base level load reads and untiles the level on the CPU: one call per block, about 4 million for a 2048x2048 RGBA8 texture. Track loads take longer while dumping; the first use of a replacement reads its file on the command processor thread.
- **Vulkan**: only the D3D12 texture cache has it.

## Verification

- CPU: `fm4_texture_replacement_tests` (built and run by `pipeline.py build` with the other CPU tests) checks the fixed hash, that padding, pitch and row splitting do not change it, that format, size and one bit do, the hash file names, and DDS round trips for BC1, BC2, BC3, BC4, BC5, BC7, RGBA8, BGRA8, R8, RG8 and RGBA16F (legacy and DX10 headers, mips, a 5x3 BC1), sRGB, mip count handling and rejected files.
- In game, 19 October 2026: BuildRoot `I:\FM4-build-usa` (USA), this branch, launcher defaults, AI driver.
  - **Dump**: a Quick Race on the Bernese Alps (`quickrace` scenario, 20 s of race) wrote 2,005 textures (649 MB) with the first version of the dynamic texture check, 969 with the second. Between the two runs 813 of the hashes are the same; the rest are the cars and streamed textures of a race that is not deterministic.
  - **The sky**: `5de3b416781cfd97`, `k_DXT1` 8192x2048 without mips, a 360-degree photo panorama of the Alps with the sun and the mountains in it. Its hash is the same in both runs although its guest address differs (`0xa91f000`, `0xa91e000`).
  - **Replacement**: a test pattern of the same size as RGBA8 with 14 levels replaces it (a format change from BC1). In 2x captures (2560x1440) of the same camera, with and without the pack, the pattern shows where the sky and the mountains were, and in the car's window and exhaust reflections, which the game renders from the sky.
  - **Smoke gate** `texon` (both switches on, the test pack) against `texoff` (the same binary, switches off), 240 Hz: 3 of 3 cells passed in both. `career` PASS; `photo` and `photo_view` WARN on capture statistics (the replaced sky) and, in `photo`, the guest idle wait (8.4 to 27.8 ms/s). No new error keys. Single runs: race 207.0 against 172.3 fps (`photo`) and 204.1 against 148.4 fps (`career`), which is run-to-run noise, not a measurement.

## Offline extraction

[texture_extract.py](../../outputs/FM4-build/texture_extract.py) collects the textures stored in the track archives as a runtime dump writes them, so a pack can be built without driving every track:

```text
python outputs/FM4-build/texture_extract.py --build-root I:\FM4-build-usa [--track Alps] [--no-dump]
```

It reads every `Media\tracks\<track>\bin.zip` of Disc 1 (`disc1` in `local.json`), decompresses the entries with the runtime's `lzx_decompress` (as [material_atlas.py](../../outputs/FM4-build/e2e/material_atlas.py) does), and writes into `<BuildRoot>\texextract`:

| Output | Contents |
| --- | --- |
| `dds\<hash>.dds`, `dds\textures.txt` | The dump, exactly as `texture_dump_path` writes it (the guest address is 0). |
| `index.tsv` | Every texture record: track, archive entry, chunk, source file name, format, size, mip levels, `D3DFORMAT`, and the hash or why there is none (`cube`, `external`). |
| `panoramas.txt` | Per track, the sky candidates: textures without mips at least 1,024 wide, marked `wide` when at least twice as wide as high. |

The hashes and the files come from [texture-extract.cpp](../../outputs/FM4-build/texture-extract.cpp), a DLL the script builds with clang++ on first use. It compiles the BuildRoot's own `replacement.cpp` (`ReadGuestBaseLevel`, `HashGuestLevel`, `Dumper`) and links the installed `rexruntime` for the guest layout, the tiling and the format table, so the untiling, the endian swap, the hash and the DDS writer are the runtime's code, not a copy. Each texture is read as the game's Direct3D sets it up: the pitch rounded up to 32 texels, packed mips, and the format, endianness and tiling of its `D3DFORMAT`.

### The CAFF container

The `.bin` files of the archives that hold textures are `CAFF` containers (`CAFF21.11.05.0034`). The layout below was found in the files; the field names are guesses. All values are big-endian.

| Offset | Field |
| --- | --- |
| `0x18` | Chunk name count N. |
| `0x1c` | Chunk list length M. |
| `0x44` | Offset of the name table in `.data`. |
| `0x48` | Header size: `.data` starts here, `.gpu` right after `.data`. |
| `0x50` | Sections, `0x28` bytes each: name (8 bytes), alignment log2 at `+0x08`, size at `+0x10`. |

The name table is a blob size, N offsets and the names (the source paths, `...\scene\compiled\_0x00000005.bin`). After it come a zero word and the chunk list: M entries of 14 bytes (chunk id, offset, size, section 1 for `.data` or 2 for `.gpu`, alignment log2). A chunk id has a `.data` chunk, its record, and may have a `.gpu` chunk, its GPU data. A texture record is a `.data` chunk that starts with `texture` and `04.05.05.0032`:

| Offset in the record | Field |
| --- | --- |
| `+0x18` | Xbox 360 `D3DFORMAT`: bits 0 to 5 the `xenos::TextureFormat`, 6 and 7 the endianness, 8 tiled. `0x1A207F52` is a tiled DXT1, 8-in-16, with gamma. |
| `+0x1c` | 0 for a 2D texture, 2 for a cube map (6 faces of base level and mips). |
| `+0x24` | Width and height, 16 bits each. |
| `+0x28`, `+0x2c` | Base and mip offsets in the texture's GPU data; `0xffffffff` for the mips when there are none or they are packed into the base's tail. |
| `+0x30` | Mip levels (8 bits). |

Each track has single-texture files (`_0x00000005.bin` and the like: the skies and large billboards) and `<track>.stx.bin`, a container of several hundred texture records (789 on the Bernese Alps). Records without a `.gpu` chunk (`external` in the index) keep their data elsewhere, presumably in the streamed `.bix` files: their base offsets overlap other textures' data in the container. The `.bix` files (1,479 of the Bernese Alps ones start with `BIX1`, the others look like raw blocks) are not read yet.

### Results, 19 October 2026

USA Disc 1, BuildRoot `I:\FM4-build-usa`, the runtime of the texture replacement branch:

- **The sky matches**: the Bernese Alps `_0x00000005.bin` gives `5de3b416781cfd97`, and its DDS file is byte-identical to the runtime's dump of the sky (`texdump\5de3b416781cfd97.dds`, 8,388,736 bytes).
- **Other textures match too**: 103 of the 493 Bernese Alps hashes are also in the three runtime dumps of that track (4,074 distinct hashes, most of them cars, interface and streamed track textures). They include packed small textures (`k_DXT4_5` 16x32, `k_DXN` 4x4), so the packed mip path is right. The others were not loaded in the 20 s races of those dumps, or not seen; a texture the extractor hashes differently from the runtime would also be among them, and no such case is known.
- **All 32 archives**: 10,647 containers with textures, 28,827 texture records: 19,652 hashed (18,473 distinct, 559 MB of DDS files), 875 cube maps, 8,300 external. The formats are `k_DXT1` (11,253), `k_DXT4_5` (4,463), `k_DXT5A` (3,229), `k_DXN` (576), `k_8_8_8_8` (105) and a few others; 2 `k_5_6_5` textures get no file (not a dump format). A run takes about 30 s without the dump.
- **Skies**: every race track except Road America and Silverstone has a 360-degree panorama 8,192 texels wide without mips (8192x2048, 8192x1960 or 8192x1024, DXT1 or DXT5); seven have two (probably weather or time variants). Road America has five 1024x1024 and Silverstone four 2048x1024 textures without mips instead, probably a sky in tiles (not looked at). The HomeSpace scenes have one RGBA8 4096x2048 each. Crown Jewel also has 3508x872 and 2663x872 strips, the Bernese Alps three textures of about 1600x1024. Only the Bernese Alps sky was seen in game.

| Track folder | Sky panoramas |
| --- | --- |
| `Alps` | `5de3b416781cfd97`, `832fa222daf4e206` (DXT1 8192x2048) |
| `AmalfiGP` | `1336c1626e200a6e` (DXT1 8192x1960) |
| `Autopark` | `48160ab4c432091a` (DXT5 8192x2048) |
| `Catalunya` | `5bf7fbed71765cb1` (DXT5 8192x2048) |
| `CrownJewel` | `9b69285de138eff2`, `5b300e4508f0a85d` (DXT1 8192x1960) |
| `FujimiKaido` | `7cea8d31f0e79d87` (DXT1 8192x2048) |
| `Hockenheim` | `738d45786bb69133`, `75703d5271690893` (DXT1 8192x2048) |
| `Indi` | `c71963ec6156a6ff`, `a233139ce01ed0c0` (DXT1 8192x1024) |
| `Infin` | `d249083f4929356f`, `26b894d147e7e975` (DXT5 8192x1024) |
| `LagunaSeca` | `8bacc8d62f4d0ea5` (DXT5 8192x1024) |
| `LeMans` | `77200fb29104ec08` (DXT1 8192x1960) |
| `MapleValley` | `bdd3a7d4a544e93b` (DXT5 8192x2048) |
| `Motegi` | `e8d280e10e79b820` (DXT5 8192x1024) |
| `Mugello` | `651754d7b7662437` (DXT1 8192x1960) |
| `Nurburgring` | `ecd5ff85f66409ca` (DXT1 8192x1960) |
| `NurburgringF1` | `98362911b3672926` (DXT1 8192x1960) |
| `RoadAmerica` | Tiles: `249f1daf6b000007`, `d9ef131a9ca99e6f`, `4159bd573eae83f5`, `2ac79af79215a633`, `9d3b2cc934bd8e04` (DXT5 1024x1024) |
| `RoadAtlanta` | `9f8582dc8c337ae0` (DXT5 8192x2048) |
| `Sebring` | `c142cc270cc35eb4` (DXT1 8192x2048) |
| `Silverstone` | Tiles: `d1095ecf22a89b0a`, `3cbf372b64e39584`, `06bb81b43d89dcc9`, `bab0877680e86652` (DXT5 2048x1024) |
| `SunsetInfield` | `445d0c999d8c2326` (DXT1 8192x1960) |
| `Suzuka` | `9e8d2e08ff34edae`, `0290aa6703e61f34` (DXT5 8192x2048) |
| `TopGear` | `1b38999f7949efb1`, `c3ca8bd507a1f510` (DXT1 8192x2048) |
| `Tsukuba` | `6a05b8bbf978557f` (DXT1 8192x1024) |

**Not covered**: the external records (the `.bix` streaming files), cube maps (the runtime does not hash them either), DLC tracks (none are on Disc 1) and the other folders of `Media` (cars, interface).
