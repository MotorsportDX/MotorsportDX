# Texture replacement

The runtime can write the game's textures to DDS files and sample edited DDS files in their place. A texture is named by a hash of its contents, so a replacement keeps working across runs, builds and the Japan/Asia and USA releases, the way Dolphin texture packs and Content Shaders Patch textures do. This is the route for new skies, leaves, billboards and a later night sky: the shader mods can recolor those surfaces but cannot give them new images, and editing the track archives (`bin.zip`, LZX-compressed, with an undocumented `.bin`/`.bix` format, 32 tracks) is not practical.

Usage is in [running.md](../running.md#texture-replacement). For the current state see [status/current.md](../status/current.md).

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

A probe on 19 October 2026 found the sky in the track archive, so textures can be collected without driving every track:

- `Media\tracks\Alps\bin.zip` has 4,387 entries (4,376 LZX, 11 stored); all decompress with the runtime's `lzx_decompress` and match their CRC.
- 368 distinctive blocks of the dumped sky are all in `_0x00000005.bin` at file offset 672 plus their tiled offset, swapped 8-in-16: the file holds the texture exactly as guest memory does. `_0x00000006.bin`, the same size, shares 184 of them (a second sky with the same lower half, not checked further).
- These files are `CAFF` containers (`CAFF21.11.05.0034`, sections `.data` and `.gpu`). A `texture` record (`04.05.05.0032`) holds what looks like the Xbox 360 `D3DFORMAT` (`0x1A207F52` here, `0x1A200152` and `0x1A200154` in other files; `0x52` and `0x54` match DXT1 and DXT5) and the width and height as big-endian 16-bit values; the `.gpu` data (8 MB for the sky) starts after the header.
- Only 60 of the 4,387 entries are such single-texture files. Where the other track textures are stored (the `.bix` files, the ribbon files) is not known yet.

An offline extractor would untile and swap these files as the runtime does and name them by the same hash, so a pack built from them matches `texture_replace_path`. Not written yet.
