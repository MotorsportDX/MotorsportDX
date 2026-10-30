# Native shader contract

Status: stage N5 item of the [native port](d3d-hle.md#82-shaders-on-the-native-path), 28 October 2026. The design was approved by the main session on 27 October 2026; increments 1, 2, 4, 3, 5 and 6 are implemented behind `native_shader_contract` (off by default); increment 6 (vertex shader hooks) passed its close gate on 28 October 2026 (section 9.9). The other N5 items are in [d3d-hle-n5.md](d3d-hle-n5.md).

## Summary

- **Goal**: look mods stop depending on the DXBC translation of the title's own pixel shaders. Today a mod hooks into HLSL generated from the game shader's ucode, compiled against a contract read off the DXBC translation (section 1). The native path knows what the PM4 path does not (the surfaces a draw renders into, the post passes' final point, the vertex shader keys), and the contract uses that.
- **The contract**: `shaders.toml` in the look directory declares, per guest pixel shader (by its ucode hash), the mod it takes on the native path, the pass classes it applies in, and the knobs it reads as constants. Draws carry their pass class (`xe_native_pass()`). RTV and `-Rov` do not change.
- **Increment 1**: the manifest, the pass classes, the pass debug view, and the car windows (`E62CFDF09D758B6C`) moved onto the contract with the same picture (section 4).
- **Increment 2**: an entry's translation is made without the DXBC translator: the HLSL generator assigns the bindings as the DXBC translator does, and the signatures are checked against those derived from the modification and the ucode analysis. With an empty override cache the DXBC translation draws until a worker compiles the entry. The bindings of all 568 pixel shaders of a career and a photo run match their DXBC translations, and the entry draws byte for byte as before (section 5).
- **Increment 4**: the per-pass decisions. The car materials draw most of their race draws into the mirrors, which the manifest classes; the project owner kept the mirrors as the scene (section 6).
- **Increment 3**: declared resources. An entry reads the post passes' textures by name (`srv = ["post:<name>"]`) and PNG files of the look directory (`"file:<name>"`, at most 8, each an 8-bit RGB or RGBA PNG up to 4,096 by 4,096). The race's final combine became an entry that reads the occlusion by name and draws byte for byte as its classic mod; a test pattern from a look file reaches the screen byte for byte as the same values computed in the shader (section 7).
- **Increment 5**: replacement shaders. An entry's `shader` takes the place of the game shader's HLSL and reads the game's inputs by names the manifest gives to registers (`[pixel.inputs]`). A probe proved the names, packed indices, bindings and signatures byte for byte; the race's final combine is now the project's own tone mapping, written from the documented formula (the project owner's decision of 27 October 2026), and matches the game at the game's knob values within 1 level in 1 pixel of a 2x frame (section 8). The increments are in section 3.
- **Increment 6** (done, 28 October 2026): vertex shader hooks through a vertex HLSL generator that mirrors the DXBC translator, keyed by the ucode keys of the native path, proven byte for byte with every vertex shader of a frame through the generator (section 9).

## 1. What ties a look mod to the DXBC translation

Read from [shader_override.cpp](../../work/runtime/src/graphics/d3d12/shader_override.cpp), [shader_override.hlsli](../../work/runtime/src/graphics/d3d12/shader_override.hlsli), [hlsl_translator.cpp](../../work/runtime/src/graphics/pipeline/shader/hlsl_translator.cpp), [pipeline_cache.cpp](../../work/runtime/src/graphics/d3d12/pipeline_cache.cpp), [draw_back_end.cpp](../../work/runtime/src/graphics/d3d12/draw_back_end.cpp), the 144 files of [work/fm4-rex/look](../../work/fm4-rex/look) and the [post pass framework](post-pass-framework.md).

- **The build.** `PipelineCache::TranslateAnalyzedShader` runs the DXBC translator first; `ShaderOverrides::Apply` then replaces the binary. With a `.mod.hlsli`, `HlslShaderTranslator` writes the game shader's ucode as HLSL, operation by operation as the DXBC translator does ("keep both in sync"), the mod's hooks are included, FXC compiles it, and `CompareSignatures` requires the result's input and output signatures to match the DXBC translation's. A modded shader is translated twice; the generated HLSL is made on the user's machine.
- **The contract** (`BuildContract`) is read off the DXBC translation: the packed index of each float constant the game shader reads (`XE_C(n)`; a mod cannot read a constant the shader does not read), the bindless descriptor slot of each texture and sampler binding (`XE_TEXTURE<fetch>_2D_U` and so on, numbered by the DXBC translator), the register count, the interpolator mask and centroid bits, param gen, the colour targets and the depth write, and the root signature's `b0` to `b4`.
- **Xenos-shaped inputs.** Interpolators as `p.r[i]`, bool constants by number (`xe_bool_constant(133u)`), textures by fetch slot (`XE_TFETCHCUBE(2, ...)`), signs, exponent bias and integer scale applied in `xe_texture_finish`.
- **Output rules.** `xe_ps_epilogue`: alpha test, alpha to coverage, the exponent bias per target, the piecewise linear gamma of `k_8_8_8_8_GAMMA` as 8-bit, the float24 depth halving.
- **Side channels.** The look's knobs are defines (every look compiles its own binaries; `shader_mod_live_knobs` makes them constants for development). The host values (vblank rate, frame interval, design frame rate) sit after the system constants. The post passes' textures are bindless views by number (`xe_post_texture(slot, uv)`).
- **No pass and no vertex shaders.** A mod cannot tell the scene from the mirror, the environment cube or the shadow maps. `HlslShaderTranslator` refuses vertex fetch, so vertex shaders have no mods.

The draw list of 19 October 2026 (`taaj1seam-drawlist.txt`, RTV, 31 sampled race frames) split by depth surface shows that the modded car shaders draw a large share of their draws into the rear-view mirror's depth (EDRAM base 80) beside the scene's (1024):

| Mod (pixel shader) | Draws | Into the mirror |
|---|---|---|
| `E62CFDF09D758B6C` car windows | 2,522 | 1,282 (51%) |
| `B85EF04779ABD438` car body paint (clear coat, flakes) | 1,210 | 499 (41%) |
| `AF6AA26032BC613D` chrome trim | 1,208 | 674 (56%) |
| `75B334519A1568F6` rims | 520 | 252 (48%) |
| `5E33CD06E3AF5B04` road | 494 | 129 (26%) |
| `BB5743878E0E85BC` sky | 111 | 39 (35%) |

## 2. The contract

### 2.1 The manifest and the key

`shaders.toml` in `shader_mod_path` ([the FM4 look's](../../work/fm4-rex/look/shaders.toml)), read on the native path with `native_shader_contract=true` (refused with an error otherwise; the classic mods then apply). The launcher copies it with the `.hlsli` files into the run's and the export's `look` directories.

```toml
[[pass]]                    # the title's pass classes, after the built-in ones
name = "scene"
depth_base = 1024           # EDRAM tiles of the depth surface, one or a list
min_viewport_width = 1024   # optional, guest pixels; also max_viewport_width

[[pixel]]
hash = "E62CFDF09D758B6C"   # the runtime's ucode key (XXH3 of the microcode)
hooks = "shader_E62CFDF09D758B6C.mod.hlsli"
passes = ["scene", "mirror"]   # optional: elsewhere the plain translation runs
params = ["FM4_LOOK_GLASS"]    # optional: knobs read as constants
srv = ["post:gtao_occlusion"]  # optional: resources by name (section 7)
```

- **Key.** The pixel shader's ucode hash. An entry takes the place of the classic mod of its hash; hashes without an entry keep their classic mod.
- **Body.** `hooks`: a mod of the look directory, in the classic `.mod.hlsli` syntax, hooked into the game shader's generated HLSL; or `shader`: a replacement of the look directory that takes the place of the game shader's HLSL and reads its inputs by name (`[pixel.inputs]`, section 8). Vertex shader entries are a later increment; the parser refuses them.
- **Resources.** `srv`: the post passes' textures and the look directory's PNG files the entry reads by name (section 7).
- The parser refuses unknown keys, bad names, a hash with two entries, a hooks file outside the look directory, and more than 13 title passes.

### 2.2 Pass classes

A draw of a pixel shader with an entry gets a pass class before its translation is chosen (`D3D12CommandProcessor::IssueDraw`):

1. depth or stencil testing on, and the first `[[pass]]` rule that lists the depth surface's EDRAM base and whose viewport width limits hold (the width in guest pixels, twice `PA_CL_VPORT_XSCALE`, 0 without viewport scaling): that class, also after the final point;
2. depth or stencil testing on, no rule: `other` (0);
3. neither tested, before the post passes' final point: `post` (1);
4. neither tested, after it: `hud` (2).

The FM4 manifest names `scene` (base 1024, at least 1024 guest pixels wide, as the post passes' camera rule takes the scene) and `mirror` (base 80). The log of `native_shader_pass_debug` on the native path in one pass (`n5sc-dbg2`, 27 October 2026) has exactly two kinds of draws of the car windows: base 1024 in a viewport 1280 wide, and base 80 in a viewport 384 wide, the rear-view mirror, as in the RTV tiles of section 1. The native draw list sampled every 600th frame shows only base 1024: the mirror renders at half rate and the sampled frames were all without it. Other render modes (one tile without one pass, the tiles) are not checked against these rules; a draw no rule matches is `other`. With `native_shader_pass_debug`, the command processor logs every kind of draw an entry's shader gets (its pass, depth base, viewport width, depth test and final point), once each. The class reaches the shader as the host values' W (`xe_native_pass()`, with `XE_NATIVE_PASS_<NAME>` defined for every class), written by the command processor and, with the draw back end on the replay, carried in the draw's flags (`kFlagNativePassShift`). A draw outside an entry's `passes` takes the `native_plain` modification: the DXBC translation without a mod. Only draws of shaders with an entry are classified.

### 2.3 Params

The defines an entry lists in `params` take knob slots after the host values (the slots of `shader_mod_live_knobs`, 64 in all): the entry's compile defines each as `xe_mod_knob(<slot>)`, its value from `shader_mod_defines` or, when the look leaves the knob out, from the `#ifndef`/`#define` default of the look files (as the launcher reads them). `shader_mod_set` changes them at once, and every look shares one binary per entry. Classic mods keep their defines folded.

### 2.4 The debug view and the A/B

- `native_shader_pass_debug=true`: the entries' draws write the colour of their pass class into colour target 0, at the brightness of its brightest channel (at least 0.5), alpha kept: `other` gray, `post` blue, `hud` yellow, then the manifest's classes in order green, red, magenta, cyan, orange (FM4: `scene` green, `mirror` red).
- `native_shader_contract_ab_ms=N`: the classic mod instead of the entry in every other period of N ms of the steady clock (the `native_classic` modification, whose pipelines are not stored), to compare both in one frozen frame.

### 2.5 Outputs and formats

A mod writes values in the guest's units into `XePixel.color`, and the runtime's epilogue applies the alpha test, coverage, exponent bias and gamma of the bound target. A change of the render target formats (the float `k_16_16` targets of N5) changes the epilogue's factor, not the mods.

### 2.6 What stays out of the repository

- In: the manifest, the mods and replacement shaders the project writes, the library, original data files the project makes.
- Out: the generated HLSL of the game's shaders (made at run time; `shader_override_generate_path` writes only into BuildRoot), the DXBC and DXIL caches, shader dumps, the material atlas's full output, and any translation of the game's shaders: all derived from game data and made on the user's machine. A replacement shader must not be a transcription of a game shader's ucode or of its generated HLSL.

## 3. Increments

Effort in person-weeks for one developer with an AI agent, inferred. Each behind `native_shader_contract`, each with step gates and a close gate. The order is the main session's of 27 October 2026: 1, 2, 4, then 3, 5 and 6.

| # | Scope | Gate | Effort |
|---|---|---|---|
| 1 | The manifest, the pass classes with `xe_native_pass()`, the pass debug view, and the car windows `E62CFDF09D758B6C` moved with their knobs as params; the A/B cvar | the in-run A/B on a frozen frame at 2x byte-identical; the pass view at 1:1; a param changed while running without a compile; freeze parity; RTV and `-Rov` no change (smoke, and the DXBC fingerprints of `fm4_shader_mod_tests`); the GPU time a race frame at 2x within about ±0.05 ms of the folded defines (3 runs), and the render thread's time; smoke on the native default | 1 to 1.5 |
| 2 | No DXBC translation for entries: `HlslShaderTranslator` assigns the texture and sampler bindings as the DXBC translator does (in its own code, checked against the DXBC translator, section 5), the contract and the expected signature come from the ucode analysis, and an entry's translation is the FXC result alone, cacheable per ucode hash (stage N6's shader precompilation builds on it) | identical bindings and descriptor tables (the back end's binding check exact), the perturbation proof, byte-identical A/B captures, the shader and pipeline storages reload them | 1.5 to 2.5 |
| 4 | Per-pass look decisions: the car family (glass, chrome, rims, paint flakes) and the road's sheen in the mirror and the panel, an A/B of the scene's shading against a mirror rule (no flakes, rougher highlights); each change goes to the project owner with 1:1 crops in motion, since it makes the native path differ from RTV | per decision | 0.5 to 1 |
| 3 | Declared resources: `post:<texture>` by the post manifest's name, `file:` textures from the look directory in a bindless range, `scene_depth` and `previous_color` with the native anchors, an entry's own constant buffer at `b0, space10` | the data at 1:1; no change for entries without them | 1 to 1.5 |
| 5 | Replacement shaders with declared game inputs: interpolators, float and bool constants and textures by name (the constants uploaded packed per translation), the project's own code only | signature and binding checks; the shader at 1:1 | 2 to 3 |
| 6 | Vertex shader hooks: a vertex HLSL generator (vertex fetch from shared memory with the endian swaps, the exports, the position's NDC transform), hooks on the exported position and interpolators, keyed by the native path's vertex shader keys (after the XDK's vertex fetch rewrite; several keys per shader) | the generated shaders against the DXBC translation byte-identical without hooks; the hook's effect at 1:1 | 3 to 5 |
| 7 | Moved to stage N6 (the main session's decision of 27 October 2026, [section 19](d3d-hle-n6.md)): the offline translation of every shader in the user's shader storage, so no shader compiles during play; it builds on increments 1 and 2 | | |

New passes at D3D-level points (8.2's fourth bullet) belong with the post passes on native surfaces; they take declared resources the same way.

## 4. Increment 1

### 4.1 Implementation

- [native_shader_contract.h](../../work/runtime/include/rex/graphics/d3d12/native_shader_contract.h): the manifest, the pass classes and `Classify` (header only); [native_shader_contract.cpp](../../work/runtime/src/graphics/d3d12/native_shader_contract.cpp): the parser (toml++).
- [shader_override.cpp](../../work/runtime/src/graphics/d3d12/shader_override.cpp): `native_shader_contract`, `native_shader_contract_ab_ms` and `native_shader_pass_debug`; the manifest is loaded with the mods; an entry's translation takes the entry's hooks, its contract adds `XE_NATIVE_CONTRACT`, `XE_SYS_NATIVE_PASS`, the pass names and the debug define, and its params compile as knob slots. `xe_native_pass()` and the debug view are in [shader_override.hlsli](../../work/runtime/src/graphics/d3d12/shader_override.hlsli), compiled only for entries: the classic mods' DXBC does not change.
- The pixel shader modification gains `native_classic` and `native_plain` (unused by the translator, like `hlsl_override_disabled`); the pipelines of `native_classic` are not stored.
- [command_processor.cpp](../../work/runtime/src/graphics/d3d12/command_processor.cpp) classifies the draws of entries and writes the host values' W; [draw_back_end.cpp](../../work/runtime/src/graphics/d3d12/draw_back_end.cpp) takes the class from the draw's flags on the replay.
- Tests: `fm4_native_shader_contract_tests` ([native-shader-contract-tests.cpp](../../work/fm4-rex/tests/native-shader-contract-tests.cpp)) parses the look's manifest and refused ones and checks the classes; `fm4_shader_mod_tests` compiles the library with the contract (the same DXBC without a pass read, another with the debug view) and every look mod as an entry with live knobs and the debug view; `test_pipeline` checks that the run's and the export's look directories get `shaders.toml`.

### 4.2 Results, 27 October 2026

BuildRoot `I:\FM4-build-usa`, the native path (`--native`, one pass with 2x MSAA, the e2e defaults of this laptop's panel), at 240 Hz for races and 60 Hz for `freeze`; the switch on with `--arg native_shader_contract=true`. Builds: `8bdcc7c` (the first runs) and `ae84adf`, whose runtime and title code are those of `99e3be6`, the commit of the increment (`ae84adf` changed only the manifest, the test and this note against `ddc6836`, and main changed no runtime code before the rebase).

| Check | Result |
|---|---|
| CPU tests | `fm4_native_shader_contract_tests` passes (the look's manifest, 18 refused manifests, the classes). `fm4_shader_mod_tests` passes: the 414 DXBC fingerprints of the classic mods are the same as main's (`595fd04`), so the library's new code changes no classic mod; the library with the contract gives the same DXBC without a pass read and another with the debug view; all 138 mods compile as entries with live knobs and the debug view. `test_pipeline.py` passes (28, one new) |
| The pass classes (`n5sc-dbg3`, the log of `native_shader_pass_debug`) | two kinds of draws of the car windows: depth base 1024 in a viewport 1280 wide (`scene`) and depth base 80 in a viewport 384 wide (`mirror`) |
| The debug view with the A/B (`n5sc-dbg3`, 2x, `aces`, `native_shader_contract_ab_ms=3000`), 1:1 crops | in the entry's periods the car ahead's windows are green and the windows of the cars in the rear-view mirror red; in the classic periods both are as the classic mod draws them |
| In-run A/B on a frozen frame at 2x, byte for byte (`n5sc-ab2`: `aces,taa=0,ao=0,windshield=0`, `FM4_TILING_SCENARIO=2`) | 10 captures, 7 in the entry's periods and 3 in the classic mod's: 0 pixels differ; the run compiled both translations (the classic one with `native_classic`) |
| The same A/B with `aces` (`n5sc-ab`, the build of `8bdcc7c`) | not byte-stable on its own: TAA and GTAO move 1.5% of the pixels by 1 to 3 levels from capture to capture, as much within one period as across the two |
| A param while running (`n5sc-live`, `shader_mod_hot_reload` and `FM4_LOOK_GLASS=0` written into `shader_mod_defines.txt` 9 s into the frozen phase) | "shader_mod_set: FM4_LOOK_GLASS=0 (live)", no compile in the whole run (every binary from the cache); the captures after it differ from those before around the car ahead by up to 81 levels, where two captures before it differ by at most 3 |
| Combined checks with perturbation, `career` at 2x (`n5sc-check`) | the draw back end's check: 28,932,921 draws, mismatches only the 28,932 perturbed (pipeline); bindings 26,046,293 and the render target update guard 25,082,418, mismatches only the perturbed; the resolve byte check 16,454 resolves, 0 mismatches |
| Perf at 2x, three runs each (`n5sc-p2x-off`, `n5sc-p2x-on`, the `photo` race at 240 Hz) | race 184.4 against 190.3 fps, p99 7.83 against 7.42 ms, GPU 5.30 against 5.12 ms a frame, at 2,029 to 2,228 against 1,600 to 2,355 draws a frame (the AI's race). A fit over the six runs, GPU = 4.46 ms + 0.393 ms per 1,000 draws, gives the switch −0.027 ms; the render thread (0.62 ms + 1.42 ms per 1,000 draws) +0.021 ms; no run is more than 0.025 ms off the fit. So the params as constants cost nothing measurable, within the ±0.05 ms the main session set |
| A manifest the binary cannot read (`n5sc-on`, by accident: the run's look was copied from a newer manifest) | one error, "unknown key 'min_viewport_width'; off", and the classic mods applied |
| The identity check with perturbation, `career` at 2x with `FM4_NATIVE_CHECK=1` (`n5sc-check2`) | 12,280,794 ranges compared, mismatches only the 10,953 perturbed; in the same run the draw back end's check 18,711,357 draws, the bindings' 17,164,695 and the update guard's 16,192,137, mismatches only the perturbed |
| Freeze parity, the switch off against on (`n5sc-off3`, `n5sc-on3`, `aces` at 2x) | PASS: draws −0.0%, predicated packets −0.1%, 33.6% of the image above 4 (the frozen moment's spread between runs). An earlier pair (`n5sc-off2`, `n5sc-on2`) failed on draws, 2,058.7 against 2,234.5 a frame: `n5sc-off2` froze at another moment, while the four other freezes of the day, with the switch on and off, had 2,234.5 to 2,236.9. At 1:1 the car ahead's windows and the rear-view mirror of `n5sc-on3` and `n5sc-off3` show the same picture |
| Smoke on the native path with the switch at 2x (`n5sc-nat2x` against `ndff-nat`) | 3 of 3 cells reached their goal; races 197.1 and 193.2 fps against 174.2 and 170.4, GPU 4.97 and 5.10 ms a frame against 5.59 and 5.70; no new error key. The WARNs are capture statistics and the guest's waits: this gate ran the menus route, its baseline the fast route |
| RTV and `-Rov` smokes (`n5sc-rtv2x` at 2x against `ndff-rtv`, `n5sc-rov1x` at 1x with `original,taa=0` against `ndff-rov`) | 6 of 6 cells reached their goal, no new error key. RTV races 173.7 and 167.1 fps against 170.5 and 170.1, `-Rov` races 117.8 and 110.0 against 125.4 and 94.1. The `photo` cells FAIL on the overall fps (77.2 against 90.8, 61.4 against 71.8): the menus route's longer menus at 30 fps against the fast route of the baselines, with the race phases within the limits; `-Rov`'s `career` PASS |
| Freeze parity of RTV and `-Rov` (`n5sc-fzrtv` at 2x, `n5sc-fzrov` at 1x, `original,taa=0`, against `ndff-fz-rtv2` and `ndff-fz-rov`) | PASS: draws +0.2% each, 25.5% and 28.5% of the image above 4 |

## 5. Increment 2: entries without the DXBC translator

### 5.1 Design (the main session's go of 27 October 2026)

An entry's translation is made from the ucode alone, so it can be cached per ucode hash and modification without a DXBC pass (stage N6's offline pass builds on it, [section 19](d3d-hle-n6.md)):

- **The bindings.** `HlslShaderTranslator` assigns the texture and sampler bindings itself, as `DxbcShaderTranslator::FindOrAddTextureBinding` and `FindOrAddSamplerBinding` do with bindless resources: per texture fetch the sampler first (its filters from the instruction, anisotropy only with a computed LOD, linear filters under anisotropy), then the unsigned and the signed texture of its dimension (1D as 2D), and for a 3D fetch those of the stacked 2D array too, each bindless descriptor index the count of both kinds with it (the DXBC translator counts after it adds the binding, so the first has index 1; the first check run found the HLSL one lower in every shader with a texture, section 5.2). The bindings are a property of the shader, not of the modification: `DxbcShader::SetBindingsOnce` stores them if no translation stored any. The design note had one helper shared with the DXBC translator; the code is mirrored instead, so the DXBC translator's output, which RTV and `-Rov` use, does not change (the main session was told on 27 October 2026). The DXBC translator's `PostTranslation` now logs an error when a shader's stored bindings differ from its own.
- **The signatures.** Without a DXBC translation to compare with, the override's signatures are checked against those the DXBC translator would write, from the modification and the ucode analysis: the interpolators of the mask as `TEXCOORD0` and up in registers 0 and up, the point coordinates, `SV_Position` and `SV_IsFrontFace` after them; `SV_Target<n>` in register n for the colour targets written, `SV_Coverage` for target 0 without the early depth hint, `SV_Depth` for a shader that writes depth.
- **The translation.** `ShaderOverrides::TranslateEntry` runs `HlslShaderTranslator` on the translation itself, stores the bindings, builds the contract and compiles the entry's mod through the override cache (the same source, defines and cache key as increment 1's), and replaces the binary with FXC's. The DXBC translator runs only when that fails: a fetch the generator does not support (getWeights, getCompTexLOD, getBCF), memexport, the float24 depth modes, or a failed compile. Hot reload recompiles such an entry as before; an entry that loses its mod keeps its override until a restart, since there is no DXBC translation to go back to.
- **Compiles off the draw path** (stage N6's increment 1.1, `shader_override_async`, [d3d-hle-n6.md 19.3.7](d3d-hle-n6.md#1937-increments-10-and-11-27-october-2026)). The direct path above runs when the override cache holds the entry's binary, at start-up (the shader storage's translations), and without the queue (hot reload, `async_shader_compilation`). When a draw meets an entry that the cache does not hold, `TranslateEntry` compiles nothing: the DXBC translation draws, the game's own look for the first frames (the main session's decision of 27 October 2026), and `Apply` queues the entry's compile with that DXBC for the signature check, as for a classic mod. `PipelineCache::SwapCompiledMods` then puts FXC's binary into the translation and recreates its pipelines with the same root signature. That is safe because the root signature follows the shader's bindings, which the generator assigns as the DXBC translator does (the check below). The worker's cache file is the direct path's: the contract and the HLSL come from the shader's analysis and bindings, not from the DXBC translation, so the next session takes the direct path.
- **The check.** `native_shader_contract_check=true` compares the bindings the generator assigns with those of the DXBC translation of every pixel shader the DXBC translator translates, and translates every entry once more with the DXBC translator into a scratch translation: its `PostTranslation` compares the bindings the entry stored, and the signatures of the entry's override, of the DXBC translation and of the expected ones are compared. Every mismatch is an error.

### 5.2 Results, 27 October 2026

BuildRoot `I:\FM4-build-usa`, the native path, the look `aces` unless noted, at 240 Hz for races and 60 Hz for `freeze`. Two builds: `5321a8d` (the increment on local main `d2efb01`, before the descriptor fix) and `6b2b8db` (on local main `f3c50f8` with stage N6's increment 1.1, the descriptor fix `2bb5c03` and the compiles off the draw path). The manifest's only entry is the car windows' `E62CFDF09D758B6C`.

| Check | Result |
|---|---|
| The first check runs (`n5sc2-check`, `career` at 2x, and `n5sc2-checkphoto`, `photo` at 2x; `5321a8d`) | 1,217 binding mismatches in 554 of the 568 pixel shaders: every shader with a texture, while the 14 without matched. The generator numbered the bindless descriptors from 0, the DXBC translator from 1 (it counts after it adds the binding). The entry was consistent with itself, since its translation stored the bindings first (the byte A/B `n5sc2-ab`: 0 pixels differ in 10 captures), but a DXBC translation of the same shader, such as its draws outside the entry's passes, would have read its descriptors one slot off. The four that the entry check made logged the DXBC translator's error, while the entry check reported a match. `2bb5c03` numbers them as the DXBC translator does; the check's error names the first difference, and the entry check reports the DXBC translator's mismatches |
| The check runs on `6b2b8db` (`n5sc3-check`, `career` at 2x with `FM4_NATIVE_CHECK=1` and the other checks perturbed; `n5sc3-checkphoto`, `photo` at 2x) | 568 pixel shaders, 1,247 and 1,243 translations compared: 0 binding mismatches, none without HLSL (no unsupported fetch). Each run checked the entry's 4 modifications against a DXBC translation: the signatures and the bindings match, and the DXBC translator logged no mismatch. 0 DXBC fallbacks. Every translation came from the shader storage at start-up, which holds every pixel shader that runs on this BuildRoot met; no pixel shader was new in play |
| The other checks in `n5sc3-check` | the identity check 13,930,369 ranges, mismatches only the 12,418 perturbed; the draw back end's 19,313,066 draws, 19,313 perturbed; the bindings' 16,395,895 and the update guard's 16,493,859, mismatches only the perturbed |
| In-run A/B on a frozen frame at 2x, byte for byte (`n5sc3-ab`: `aces,taa=0,ao=0,windshield=0`, `FM4_TILING_SCENARIO=2`) | 10 captures, 4 in the entry's periods and 6 in the classic mod's: 0 pixels differ (18 comparisons). The classic mod's output is the one increment 1's entry matched byte for byte (`n5sc-ab2`) |
| Empty caches, `career` at 1x (`n5sc3-cold` against `n6k12`, `f3c50f8`'s code with the contract) | no mod compiled on the draw path; 64 on the workers, 84 swaps. The entry met a draw once before the cache held it (one placeholder) and compiled on a worker in 511 ms. Frames over 50, 100 and 250 ms 16, 10 and 3 against 9, 5 and 1, and pipelines over 4 ms 181 against 12: the driver's cache, as in [d3d-hle-n6.md 19.3.7](d3d-hle-n6.md#1937-increments-10-and-11-27-october-2026) (`n6k12` ran right after `n6c12` on the same shaders; `n6c12`, on empty caches without the contract, had 17, 10 and 3, and 184). The race 237.7 fps, p99 5.04 against 5.27 ms; `compare.py` fails the whole run's p99 (33.17 against 17.09 ms, the menus), so the cell passes by the per-phase rule |
| Empty caches, the race at 2x with the full captures (`n5sc3-cold2x`; `n5sc3-warm2x` on the BuildRoot's caches for the same moments) | no mod compiled on the draw path; 66 on the workers, 111 swaps. The entry met its first draw at the start of the 3-2-1 (one placeholder), waited 3.2 s in the queue, compiled in 522 ms and was swapped in at once with 12 pipelines to recreate. At 1:1, 3.4 s into the 3-2-1 and 0.45 s before the swap, the player's car's rear window is the game's own glass beside the warm run's entry, and the interior behind it differs too, with 19 other mods still to come from the workers. 3.8 s after the swap the car ahead's rear window has the entry's glass (the warm run has another car there, so that frame has no warm pair) |
| Smoke on the native path with the contract (`n5sc3-nat` against `n6s12`, the contract off) | 3 of 3 cells; races 238.1 and 237.1 fps against 235.7 and 237.2, p99 4.92 and 5.08 ms; `photo` and `photo_view` PASS, `career` FAIL on the whole run's p99 (27.14 against 17.30 ms, the menus) with its phases within the limits; the same error keys as main |
| Freeze parity at 1x with `original,taa=0` (`n5sc3-fz1`, native, against `n6y11`; `n5sc3-fzrov`, `-Rov`, against `n6v12`) | PASS: the native path's draws +0.0% in both runs (WARN only on the guest's idle wait, 1.05 to 58 ms/s); `-Rov`'s −0.0% and +0.0% |
| Freeze parity at 2x, the switch off against on (`n5sc2-off`, `n5sc2-on`; `5321a8d`) | PASS |
| Perf at 2x, three runs each (`n5sc2-p2x-off`, `n5sc2-p2x-on`, the `photo` race; `5321a8d`, whose work the descriptor fix does not change) | the race's GPU time 5.18 against 5.10 ms a frame, the render thread's 3.46 against 3.53 ms; a fit with the draws gives the switch −0.075 ms of GPU and +0.094 ms of render thread, where one side's runs spread over 0.73 ms |
| CPU tests (`6b2b8db`'s build) | `fm4_shader_mod_tests` passes, its 414 DXBC fingerprints those of `595fd04`; `fm4_native_shader_contract_tests` passes; `test_pipeline.py` (28) and `test_e2e_tools.py` (44) pass |

Not covered: an entry with a fetch the generator does not support (none in the manifest), hot reload of an entry, RTV (out of the gates since `d2efb01`; the contract is native only), 60 and 120 Hz.

## 6. Increment 4: per-pass decisions

### 6.1 Where the car materials draw, 27 October 2026

A race at 1x on the native path with the draw list (`REX_DRAW_LIST_TRACE`, every 7th frame, 2,201 frames; `n5sc4-survey` on main `55ee1cb`'s runtime) split the modded pixel shaders by depth surface. The car materials of `shaders_v16` draw most of their race draws into the mirror pass (depth base 80) besides the scene (1024): the car windows `E62CFDF09D758B6C` 74,194 against 19,305, the lamp covers `00AF2783DCAB9C75` and `AD10B610621DEFE5` 12,107 against 1,616, the body paint `B85EF04779ABD438` 41,883 against 10,999, the chrome trim `AF6AA26032BC613D` and chrome rims `F8CDBAFAD23FD784` 43,347 against 6,461, the rims `36DFBBD4669AF5E9`, `75B334519A1568F6` and `0412EE51F7B00389` 45,928 against 8,156. The lit road `5E33CD06E3AF5B04` (6,300 against 19,156), its edges and shoulders draw there too. The other depth surfaces are the front-end's 3D car (2880, in the menus only) and the intro's unlit road `29A611FD8CFB545D` (224, in the intro and the 3-2-1 only): a race has no "mirror panel" pass. The cockpit's side mirror draws in the mirror pass with the rear-view mirror.

The manifest has entries for the nine car materials (`a066fdf`, their knobs as params). The pass log of `native_shader_pass_debug` (`n5sc4-dbg`, a frozen race at 2x) shows two kinds of draws for each of them: the scene (depth base 1024, 1280 wide) and the mirror (depth base 80, 384 wide).

### 6.2 The A/B and the project owner's decisions, 27 October 2026

The rule under test: in the mirror pass, the sun highlights no sharper than roughness 0.3 (GGX), and the flakes off. Today's roughness: glass 0.08, chrome 0.1, rims 0.25, the clear coat 0.3, the road's sheen 0.5, so the floor could change only glass, chrome and rims. One frozen race frame at 2x with `aces` (cockpit camera, the Bernese Alps grid; `n5sc4-crops`) took the knob states live while it stayed frozen, so each state shows the same moment: the game's own car materials and road, the preset, the preset with the rule, the flakes on, and the flakes on but off in the mirror. Captures of one state differ by at most 3 levels.

| Decision | What the 1:1 crops at 2x show | The owner's decision |
|---|---|---|
| Glass, chrome and rims rougher in the mirrors | the rule softens a few glints on the window frames, windscreen edges and chrome trim of the cars behind: 673 of the rear-view mirror's 84,600 pixels change by more than 4 levels (up to 74), 1,114 of the side mirror's 63,000 (up to 103); nothing changes outside the mirrors beyond the run's exposure drift (13 levels). Whether the sharp glints shimmer in motion, which the rule is for, a still frame cannot show, and TAA steadies them on the native path | no rule: the mirrors keep the scene's roughness |
| The paint's flakes off in the mirrors | no preset turns flakes on; with `flakes=1`, turning them off in the mirrors changes no more than the capture noise (14 pixels above 4 levels, at most 7): the flakes already fade where a flake cell is smaller than a pixel | no rule |
| The road's sheen in the mirrors | its roughness is above the floor, so the rule changes nothing on the road; against the game, the mirror shows the sheen in place of the game's highlight, as the scene does | unchanged |
| The clear coat in the mirrors | its roughness equals the floor: unchanged | unchanged |

So the mirrors draw as the scene, as on RTV, and the two knobs of the test (`FM4_LOOK_MIRROR_ROUGHNESS`, `FM4_LOOK_MIRROR_FLAKES`) are gone (`592b825`); the mirror pass class and the nine entries stay for later increments.

### 6.3 Close gate, 27 October 2026

BuildRoot `I:\FM4-build-usa`, the build of `47ea6ba` (the nine entries without the mirror knobs; every cell's snapshot names it), the native path, the look `aces` unless noted; against main `55ee1cb`'s cells of increment 2 (`n5sc3-*`, one entry). A Windows restart cut the gate after the smoke; the perf and freeze cells ran again on the same head, rebuilt under the GPU queue's lock after another session's build.

| Check | Result |
|---|---|
| CPU tests | `fm4_shader_mod_tests` passes, its 414 DXBC fingerprints those of main; `fm4_native_shader_contract_tests` passes with the nine entries |
| The pass log (`n5sc4-dbg`, a frozen race at 2x with `native_shader_pass_debug`, the look of `a066fdf` on main's runtime) | every entry draws in the scene (depth base 1024, 1280 wide) and in the mirror pass (80, 384 wide) and nowhere else |
| The checks, `career` at 2x with `native_shader_contract_check` (`n5sc4c-check`) | the nine entries checked in their 4 modifications each, 36 checks: the signatures and bindings of their DXBC translations match; the other 560 pixel shaders (1,212 translations) have the generator's bindings; 0 mismatches, 0 DXBC fallbacks |
| In-run A/B at 2x, byte for byte (`n5sc4c-ab`: `aces,taa=0,ao=0,windshield=0`, `FM4_TILING_SCENARIO=2`) | 10 captures, 6 in the entries' periods and 4 in the classic mods': 0 pixels differ (18 comparisons). An earlier run (`n5sc4-ab2`) does not count: it ran on another session's build |
| Empty caches, `career` at 1x (`n5sc4c-cold` against `n5sc3-cold`) | no mod compiled on the draw path; 62 on the workers, 81 swaps; 7 entry translations met a draw before the cache held them and drew their DXBC translation until the swap. WARN: pipelines over 4 ms 182 against 181, frames over 100 ms 11 against 10 (the driver's cache) |
| Smoke on the native path with the contract (`n5sc4c-nat` against `n5sc3-nat`) | 3 of 3 cells; races 235.2 and 237.7 fps (p99 5.26 and 5.07 ms) against 238.1 and 237.1; `photo` and `photo_view` PASS, `career` FAIL on the whole run's p99 (33.16 against 27.14 ms, the menus) with its phases within the limits |
| Perf at 2x, three runs each (`n5sc4c-p2x-off`, `n5sc4c-p2x-on`, the `photo` race) | the race's GPU time 5.14 against 4.97 ms a frame and the render thread's 3.33 against 2.73 ms, at 1,851 to 2,137 against 1,502 to 1,720 draws a frame (the AI's race). A fit with the draws, GPU = 4.34 ms + 0.397 ms per 1,000 draws, gives the switch −0.007 ms; the render thread 0.000 ms. The nine entries' knobs as constants cost nothing measurable |
| Freeze parity at 2x, the contract off against on (`n5sc4c-off`, `n5sc4c-on`) | PASS: draws −0.5% |
| `-Rov`, freeze parity at 1x (`n5sc4c-fzrov` against `n5sc3-fzrov`) | PASS: draws −0.1% and +0.1% |

Not covered: RTV (out of the gates; the contract is native only), 60 and 120 Hz, the mirrors in motion.

## 7. Increment 3: declared resources

### 7.1 Design (the main session's go of 27 October 2026)

- **The key.** An entry lists what it reads besides the game shader's own inputs: `srv = ["post:<name>", "file:<name>"]`, a string or a list. The parser refuses another kind, an empty or upper-case name, a name twice in one entry, a post texture and a file of one name (they would share the define), a file name with a suffix, and more than 8 look files over the manifest (a file that several entries read counts once).
- **Post textures by name.** `post:<name>` is a texture of the post pass manifest with a `slot` ([post pass framework](post-pass-framework.md)). The entry's contract defines `XE_SRV_<NAME>` and `xe_srv_<name>(uv)`, which reads the texture through the slot the post passes gave it (`xe_post_texture`); a name with no slot reads 0, with a warning when post passes are loaded. The textures hold the frame's data where the passes before the final point have written them, so in the final combine and after it.
- **The final combine as an entry.** The race's final combine `E97AA211B556A9B3` (scene and bloom, the filmic curve, the grading LUT) reads the occlusion through `fm4_post_ao(uv)` of [fm4-look.hlsli](../../work/fm4-rex/look/fm4-look.hlsli), which takes `xe_srv_gtao_occlusion` where the entry declares it and `xe_post_ao` (slot 0) elsewhere. The manifest gives it an entry with `srv = ["post:gtao_occlusion"]` and no `passes`, so it applies in every pass class. Its variants with the vignette and with depth of field keep their classic mods.
- **Look files.** `file:<name>` is `<name>.png` in the look directory: an 8-bit RGB or RGBA PNG of at most 16 MiB and 4,096 by 4,096 texels. The header is checked before stb_image decodes the file (`rex::ui::DecodeImageRGBA`). The runtime loads the files with the manifest at start-up, and the command processor uploads them at its first submission into eight system bindless views after the post passes' (`kLookFileViewsStart`), as RGBA8 2D arrays of one layer. `xe_srv_<name>(uv)` reads the texel at `uv` without a filter, `xe_srv_<name>_linear(uv)` filters four texels bilinearly, both clamped at the edges, and `uv` (0, 0) is the PNG's top left. A file that is missing, too large, of another kind or that does not decode reads one 0.5 gray texel, with an error in the log: no draw reads an unwritten view. The memory follows the texels: at the limits, 8 files of 4,096 by 4,096 take 512 MiB of video memory (64 MiB each), and as much again in the upload heap until the first submission completes; the decoded texels in system memory are freed after the upload (`f0b8f02`). Look files should be as small as their use allows. The launcher and the export copy the PNG files that `shaders.toml` names and no other image of the look directory.
- **Deferred** (the main session's decision of 27 October 2026): `previous_color` and `previous_depth`, the post passes' history, until a mod needs them; `scene_depth` not at all, since no mod needs the scene's depth beyond GTAO, which reads it; an entry's own constant buffer at `b0, space10` with increment 5, whose replacement shaders take declared constants.
- **The shader override A/B's off phase.** A translation with `hlsl_override_disabled` (`shader_override_ab_ms`) is not an entry's draw: it takes the DXBC translation as before. With the final combine as an entry, such a pipeline that a build before the contract had stored, loaded at start-up, counted as an entry's DXBC fallback in the first close gate's runs (one each); `bfa30cd` keeps it out of the entries.
- **No change without them.** The contract of every mod defines `XE_LOOK_FILE_VIEWS`, which no code reads unless a mod reads a look file, and entries without `srv` get nothing else: the classic mods' DXBC fingerprints are main's.
- Tests: `fm4_native_shader_contract_tests` parses the final combine's entry and 8 refused `srv` lists, takes 8 look files and refuses 9, and checks the PNG header rules (RGB and RGBA at 8 bits, 1x1 and 4,096 by 4,096 taken; 16 bits, gray, a palette, too wide, no rows, another signature, a cut header and a file over 16 MiB refused); `test_pipeline.py` checks that a look directory's PNG files go with it only where `shaders.toml` reads them.

### 7.2 Close gate, 27 October 2026

BuildRoot `I:\FM4-build-usa`, the native path with `native_shader_contract=true`, the look `aces` unless noted; against main `3c76f51`'s cells of increment 4 (`n5sc4c-*`, the build of `47ea6ba`). The gate ran on `f0027a7`, whose code is that of `43f19ea` (the increment's two commits); the check, the look file run and the perf bracket ran on `f0b8f02`, with the A/B's off phase kept out of the entries (`bfa30cd`) and the texels freed after the upload. Every cell's snapshot names its build.

| Check | Result |
|---|---|
| CPU tests (the builds of `f0027a7` and `f0b8f02`) | `fm4_native_shader_contract_tests` passes; `fm4_shader_mod_tests` passes, its 414 DXBC fingerprints those of `595fd04`, so `fm4_post_ao` changes no classic mod; `test_pipeline.py` passes (29, one new) |
| The checks, `career` at 2x with `native_shader_contract_check` (`n5sc6-check`, `f0b8f02`) | the ten entries checked in 38 translations: the signatures and bindings of their DXBC translations match; the other 560 pixel shaders (1,213 translations) have the generator's bindings; 0 mismatches, none without HLSL, 0 DXBC fallbacks. On `f0027a7` (`n5sc5-check`) the same with one fallback, the final combine's pipeline of the shader override A/B's off phase from the pipeline storage (section 7.1) |
| In-run A/B at 2x, the ten entries against their classic mods, with the final combine's entry reading the occlusion by name and its classic mod by slot (`n5sc5-ab`: `aces,taa=0,windshield=0`, `FM4_TILING_SCENARIO=2`; `n5sc5-ab0`: the same with `ao=0`) | with the occlusion 10 captures, 7 in the entries' periods and 3 in the classic mods': 0 pixels differ (18 comparisons); without it 10 captures, 3 and 7: 0 pixels differ. The frozen frame with GTAO and without TAA is byte-stable |
| The look files (`n5sc6-file`, `f0b8f02`): a gate look outside the repository, the look with the final combine also reading `gate_pattern.png` (64x64, RGB, asymmetric) and `gate_missing` (no such file), frozen at 2x with `aces,taa=0,ao=0,windshield=0,sharpen=0` | the log: "look file gate_pattern.png (64x64)" and one error, "gate_missing.png cannot be read; xe_srv_gate_missing reads 0.5 gray". The final combine's hooks write the pattern 1:1 in guest pixels beside the same values computed in the shader, and the missing file's texel beside 0.5 gray, so both pass the same output path (the 10-bit target, the HUD, the game's gamma ramp). In all 28 captures from the race's start through the frozen phase both blocks equal their references byte for byte, the checker's blue right at every edge; at 1:1 the four blocks show the same picture. Before the race the 3-2-1 draws the final combine's other variants, which the gate look leaves alone. The first run (`n5sc5-file`, `f0027a7`) drew the pattern at the screen's centre without a reference: each texel value came out as one value after the game's gamma ramp ([HDR output](hdr-output.md)), red and green alike, blue exact, but the blocks covered the rewind's text and the run stopped at that step |
| Empty caches, `career` at 1x (`n5sc5-cold` against `n5sc4c-cold`) | no mod compiled on the draw path; 62 on the workers, 81 swaps; 8 entry translations drew their DXBC translation until the swap (7 before; the final combine's is new). Frames over 50, 100 and 250 ms 16, 9 and 3 against 18, 11 and 4. PASS |
| Smoke on the native path (`n5sc5-nat` against `n5sc4c-nat`) | 3 of 3 cells; races 238.7 and 237.8 fps (p99 4.75 and 4.87 ms) against 235.2 and 237.7; `career` PASS, `photo` WARN on the guest's waits in photo mode (its scene drew 1,975 draws a frame against 4,874), `photo_view` WARN on capture statistics |
| Perf at 2x, three runs a cell (the `photo` race and photo mode at 240 Hz) | against main's cell of increment 4 (`n5sc5-p2x-on` against `n5sc4c-p2x-on`) a fit with the draws gave −0.165 ms of GPU a race frame and +0.340 ms a photo mode frame, the photo mode shift beyond the ±0.15 ms of the earlier pairs. So `f0b8f02` ran between two runs of main's build (`47ea6ba`'s snapshot with main's look; `n5sc7-main1`, `n5sc7-inc3`, `n5sc7-main2`): GPU = 4.43 ms + 0.277 ms per 1,000 draws in the race, the increment −0.064 ms; in photo mode +0.064 ms; the render thread −0.033 and −0.096 ms. Main's two cells differ by −0.096 and +0.151 ms between themselves, and its photo mode took 7.52 to 8.06 ms a frame against 7.37 to 7.69 in `n5sc4c-p2x-on`: the first shift was the machine's state, and the increment costs nothing measurable |
| Freeze parity at 2x on the native path without the contract (`n5sc5-off` against `n5sc4c-off`) | PASS: draws −0.5%. The system views and the post passes' code are shared |
| `-Rov`: smoke at 1x with `original,taa=0` (`n5sc5-rov` against `n6o11`) and freeze parity at 1x (`n5sc5-fzrov` against `n5sc4c-fzrov`) | smoke 3 of 3 cells, no look file or contract line in the logs; `photo` FAIL on its race's fps in one run (104.4 against 134.6, while `career`'s race had 106.3 against 93.3), `photo_view` WARN on capture statistics. The parity PASS: draws +0.0%, the frozen frame's GPU time 12.09 against 12.06 ms |

Not covered: RTV (out of the gates; the contract is native only), 60 and 120 Hz, `xe_srv_<name>_linear` on the GPU (it compiles with every contract, but no gate look reads it), and a look that declares more than one post texture.

## 8. Increment 5: replacement shaders

The design, read from the code of `b445ace`. The main session took decisions 1 to 5 and 7 as recommended below, and step 5a of decision 6, on 27 October 2026; the project owner approved step 5b the same day (8.2.6). Steps 5a and 5b are implemented behind `native_shader_contract` (8.4 and 8.5), and the increment's close gate passed on 28 October 2026 (8.6).

### 8.1 What the design starts from

- **The classic override.** `shader_<hash>[_<modification>].ps.hlsl` in `shader_override_path` ([B4 research](b4-shader-research.md#b4-step-1-per-hash-hlsl-override-16-october-2026)) replaces a pixel shader's whole translation, but under the Xenos-shaped contract of section 1: registers, `XE_C(n)` by packed index, fetch slots by number. It is not in the manifest, and the faithful rewrite of the final combine made with it stayed in BuildRoot, since it is a transcription. The manifest's parser refuses `shader` today.
- **The root signature does not follow the bindings.** With bindless resources, the only mode the mods run in, `D3D12CommandProcessor::GetRootSignature` gives every pipeline the same root signature. What follows a shader's bindings is the table of descriptor indices (`b4`) that the command processor writes for its draws, from the bindings stored in the shader (`GetTextureBindingsAfterTranslation`, `GetTextureBindingLayoutUserUID`). All translations of the shader share them: the entry's, the `native_plain` one outside the entry's passes, the DXBC translation that draws until a worker's compile is swapped in, and the A/B's classic one.
- **Constants.** The float constants (`b1`) are uploaded packed by the pixel shader's float constant map (`constant_register_map`, from the ucode analysis, one per shader); the bool and loop constants (`b2`) and the fetch constants (`b3`) are uploaded whole. The knobs of `params` and the host values follow the system constants in `b0`.
- **Interpolators** reach a translation by its modification's mask (what the vertex shader writes and the pixel shader reads), and the signature check of increment 2 holds every override to them.

### 8.2 The decisions

| # | Decision | Taken (27 October 2026) | Cost (person-days) |
|---|---|---|---|
| 1 | Manifest syntax | `shader = "<file>.hlsli"` instead of `hooks`; `passes`, `params` and `srv` as for hooks; the inputs in a `[pixel.inputs]` table | 1 |
| 2 | How inputs get names | in the manifest, by register (`i<n>`, `c<n>`, `b<n>`, `tf<n>`, with a swizzle), not from the title's shader containers | 2 to 3 |
| 3 | Bindings and root signature | the shader's own bindings; no change to bindings or the root signature | 0 in the runtime |
| 4 | Constants | the float constants the game shader reads, by name; any bool constant; knobs through `params`; the entry's own buffer at `b0, space10` deferred again | 0.5 |
| 5 | Signatures and output rules | keep the expected signatures and the epilogue | 0.5 |
| 6 | First replacement and its gate | 5a: the mechanism, proven by a probe byte for byte against a classic probe mod; 5b, approved by the owner (8.2.6): the race's final combine `E97AA211B556A9B3` as the project's own tone mapping | 5a 1 to 2, 5b 3 to 5, the gates 3 to 4 |
| 7 | Fallback | the DXBC translation with an error, per modification, as for entries | 0.5 |

With the CPU tests (1.5) and the library's entry point for replacements, the increment takes about 15 person-days, the upper end of section 3's 2 to 3 person-weeks.

#### 8.2.1 Manifest syntax

```toml
[[pixel]]
hash = "E97AA211B556A9B3"
shader = "fm4-final-combine.ps.hlsli"  # instead of hooks
srv = ["post:gtao_occlusion"]

[pixel.inputs]          # the game shader's inputs the replacement reads, by name
scene = "tf0"           # the registers below are those the classic mod reads
bloom = "tf2"
lut = "tf7"
white = "i1.z"          # F at white
exposure = "i1.w"
filmic_ab = "c5"
filmic_cdef = "c6"
lut_scale = "c0.x"
lut_offset = "c1.x"
# the scene coordinate and the colour transform: read off the shader's contract
# (dump_shaders) when the entry is written
```

- `shader` and `hooks` exclude each other, and an entry has one of them. `shader` names a file of the look directory with the `.hlsli` suffix: the launcher and the export already copy those (`LOOK_SUFFIXES` in `pipeline.py`), and the compile includes it as it includes a mod, so the override cache and hot reload track it through the include handler as they track mods.
- `passes` works as for hooks: outside them the plain translation (`native_plain`). One body per hash: a replacement in one pass and hooks in another would take two entries of one hash with disjoint `passes`, which the parser refuses today and no look needs while the mirrors draw as the scene (section 6.2).
- `inputs` only with `shader`: no hook mod needs names, so the contracts of today's entries stay as they are.
- One file can serve several entries: the four final combines share their structure at other registers ([B4 research, step 4](b4-shader-research.md#b4-step-4-tone-curves-16-october-2026)), so each entry maps its own registers to the same names, and `#ifdef XE_IN_<NAME>` lets the file take an optional input such as the vignette's colour.
- The parser refuses both `shader` and `hooks` or neither, `inputs` without `shader`, a bad name or register, and a name twice.

Why: the smallest change to the manifest of increments 1 to 4, and the launcher copies the files without a change. Cost: 1 day with its tests.

#### 8.2.2 How inputs get names

Recommendation: names declared in the manifest, by register:

- `i<n>`, interpolator n (0 to 15), as the translation loads it into register n (the param gen register too), with a swizzle (`.x` to `.xyzw`: a float to a float4);
- `c<n>`, float constant n (0 to 255), with a swizzle;
- `b<n>`, bool constant n (0 to 255);
- `tf<n>`, texture fetch constant n (0 to 31), a texture the game shader fetches;
- `position` (`SV_Position`, in host pixels) and `front_face`.

Names are lower case (`[a-z][a-z0-9_]*`). The contract of a replacement defines `struct XeInputs` with a member per input that is not a texture, filled after the prologue (`XE_LOAD_INPUTS`), `XE_IN_<NAME>` for each input, and per texture `xe_tex_<name>(coord)`: the fetch at the dimension, signedness and filters of the game shader's binding for it, finished as any generated fetch (`xe_texture_finish`: signs, exponent bias, integer scale), and `XE_TEX_<NAME>`, its fetch constant, for the library's helpers such as `xe_texture_size_2d`. The packed `XE_FLOAT_CONSTANT_<n>` macros are not defined for a replacement, so the manifest lists every constant and interpolator it reads from the game.

The names are checked when the entry is translated, since the interpolators depend on the modification: an `i` outside the interpolator mask, a `c` outside the shader's float constant map or a `tf` without a texture binding is an error naming the entry, the input and the modification, and that modification falls back (8.2.7).

Why: the ucode has registers only. The manifest's names are the project's, the register numbers are observations such as the look's comments already carry ("c43 the direction to the sun" in `fm4-car.hlsli`), and one replacement serves shaders with the same inputs at other registers. No game data is read at run time beyond today's.

Not recommended: names from the constant tables of the title's shader containers. They cover 166 of 276 pixel shaders, name no interpolators, are the game's own names, and need `Media\Shaders` and the track archives read at run time on the user's machine (the LZX reader of stage N6's increment 1.3, [d3d-hle-n6.md 19.3.2](d3d-hle-n6.md#1932-design)).

Cost: 2 to 3 days (the parser, the contract, the check, their tests).

#### 8.2.3 Bindings and root signature

Recommendation: a replacement keeps the shader's bindings. `TranslateEntry` still runs the HLSL generator for them and drops its HLSL, so a replacement reads only the fetches the game shader makes, at their dimension and signedness. New textures come through `srv` (the post passes' textures and the look files), which are system views, not bindings. A fetch the replacement leaves unread costs one descriptor index.

Why: it changes nothing in the command processor, the draw back end or the native draw records, and the swap off the draw path stays as safe as for entries (section 5.1).

Limit: a shader the generator cannot translate (getWeights, getCompTexLOD, getBCF; none in the manifest) cannot take a replacement. A run of the generator that only assigns bindings would lift that later (about a day).

Not recommended: bindings per replacement. The binding layout is per shader and shared by all its translations (8.1); bindings per translation would change the draw path's binding cache, the draw back end's binding check and the native draw records.

Cost: none in the runtime.

#### 8.2.4 Constants

Recommendation: a replacement reads

- the float constants its game shader reads, by name, at their packed index;
- any bool constant (the whole of `b2` is uploaded); loop constants are not exposed, since no replacement needs them;
- the look's knobs through `params` (the live knob slots) and the host values (`xe_guest_*`, `xe_native_pass()`).

A constant the game shader does not read is an error at translation (8.2.2). The entry's own constant buffer at `b0, space10`, deferred to this increment by increment 3, is deferred again until a replacement needs a game constant its shader does not read, or data beyond the 64 knob slots. Two routes then: widen the shader's float constant map from the manifest when its ucode is analysed, so that all its translations, the DXBC ones too, take the wider packed layout (about 1 to 2 days, no root signature change), or the `b0, space10` buffer, a root parameter in the bindless root signature that `-Rov` shares and a copy per draw (about 3 to 5 days, with a `-Rov` smoke).

Why: the first replacement needs nothing more, and the deferral keeps the root signature and `-Rov` untouched. Cost: half a day, within the contract code of 8.2.2.

#### 8.2.5 Signatures and output rules

Recommendation: keep both. The replacement's signatures are checked against the expected ones of increment 2 (its inputs the translation's interpolators, its outputs the colour targets the game shader writes, with coverage and depth as the DXBC translation writes them), and its outputs pass the epilogue (`xe_ps_epilogue`: the alpha test, alpha to coverage, the exponent bias and the gamma of the bound target). The replacement writes guest units into `XePixel.color` for the targets of `XE_COLOR_TARGETS_WRITTEN`, and `XePixel.depth` when `XE_WRITES_DEPTH` is 1. It may `discard`, except where `XE_FORCE_EARLY_DEPTH_STENCIL` is 1: the early depth hint writes the depth of a discarded pixel (a documented rule, not checked).

The library's entry point for a replacement: the prologue, the inputs (`XeInputs`), `void xe_replacement(XeInputs inputs, inout XePixel p)` from the file, the pass debug view, the epilogue.

Why: the pipeline around the shader is the game's (the vertex shader's outputs, the target formats, blending, the write masks), and the swap off the draw path keeps the pipelines. The alpha test and the exponent bias are the draw's render state, not the shader's. N5's float targets change the epilogue's factor, not the replacements (section 2.5).

Not recommended: a replacement that owns its output. Each would carry the alpha test, the exponent bias and the gamma per target format, and would break at every change of the target formats.

Cost: half a day.

#### 8.2.6 The first replacement and its gate

Recommendation: two steps.

- **5a, the mechanism, proven by a probe.** A gate look outside the repository, as increment 3's look file gate (section 7.2): `E97AA211B556A9B3` as a replacement that writes its named inputs (the three fetches, the constants, the two interpolator values) into test blocks at the top of the screen, where they leave the rewind's text readable, and the scene fetch as it is everywhere else; beside it a classic mod of the same hash that writes the same values read by register (`p.r`, `XE_C`, the generated fetches through `XE_HOOK_TFETCH`). Neither holds game code. The in-run A/B (`native_shader_contract_ab_ms`) on a frozen frame at 2x must be byte-identical in every race frame: the names, the packed indices, the bindings and the signatures proven at once.
- **5b, the race's final combine** as `fm4-final-combine.ps.hlsli`, the project's own tone mapping over the functions of `fm4-look.hlsli`: x from the scene and bloom fetches and the exposure, the look's tone curve applied to x directly (where the hooks invert F from the LUT coordinate), the track's grading LUT and colour transform, gamma 2, then saturation, contrast, the occlusion, the sharpening and the HDR probe as today. The variants with the vignette and with depth of field keep their classic mods; the same file can take them later with their own inputs.

Why the final combine: one draw a frame (no measurable cost either way), the most modded shader, its formula in the [B4 research](b4-shader-research.md#finalcombine-formula) and its inputs already named in `fm4-look.hlsli`, and the place where [HDR output](hdr-output.md) needs the project's own code.

The gate of 5b, beside the usual cells (the check, empty caches, smoke, perf, freeze parity with the contract off against on, `-Rov` freeze parity):

- the in-run A/B of the replacement against the classic `aces` mod on a frozen frame at 2x: not byte-identical; the difference expected within 2 levels (the float error of the hooks' inversion of F), anything larger examined at 1:1;
- a look with every knob at the game's value and the mods loaded: the replacement against the DXBC translation, the game's own picture, within a few levels;
- 1:1 crops at 2x of a race with `aces`, the replacement beside the classic mod, and the native capture beside the `-Rov` reference.

A question for the project owner (section 2.6): at the game's knob values, 5b computes what the game shader computes, written from the documented formula in the project's own code, not from the ucode or the generated HLSL. The rule forbids a transcription; whether a re-implementation of a game shader's function may be committed is the owner's reading. If it may not, the increment ends at 5a, or 5b takes a shader that the project's effect replaces outright, for example the motion blur `4746CAD32EF168CF` as the project's own blur from the post passes' motion vectors (`post:motion_vectors`), which changes the picture and so is a look decision too.

**The project owner's decision (27 October 2026):** 5b is approved: the final combine `E97AA211B556A9B3` becomes the project's own tone mapping. Section 2.6 is read as allowing the reimplementation of a game shader's function written from documented formulas (the [B4 research](b4-shader-research.md)), never a transcription of its ucode or generated HLSL. The replacement states its provenance: the formulas it implements and the sections of the documentation they come from. Its gate: a match against the game at the default knobs at 1:1 (crops at 2x), and the look's intended change at 1:1.

Cost: 5a 1 to 2 days, 5b 3 to 5 days, the step and close gates 3 to 4 days.

#### 8.2.7 Fallback

Recommendation: the DXBC translation, as for entries. A replacement that does not compile, fails its signature check or names an input the translation lacks logs an error naming the entry, the modification and the reason, the check run counts it, and the shader draws the game's own look until the files are fixed (hot reload recompiles). It is per modification: a modification whose interpolator mask lacks an input falls back alone. While a worker compiles a replacement, its DXBC translation draws, as for entries.

Not recommended: the classic mod of the hash. It would hide a broken replacement behind a picture close to the intended one, compile a second body per modification, and tie the replacement to the mod it is meant to retire.

Cost: half a day.

### 8.3 What stays the same

- RTV, `-Rov`, the classic mods and the hook entries: an entry without `shader` gets the contract it gets today, so the 414 DXBC fingerprints of `fm4_shader_mod_tests` stay main's.
- The classic `.ps.hlsl` overrides of `shader_override_path`, a development tool.
- Replacements are cached per ucode hash and modification as entries are, so stage N6's offline pass ([d3d-hle-n6.md 19.3.2](d3d-hle-n6.md#1932-design), part 2) takes them as it takes entries.
- Left out: vertex shaders (increment 6), shaders the generator cannot translate, a replacement and hooks in different passes of one shader, and the entry's own constant buffer.

### 8.4 Step 5a: implementation

- [native_shader_contract.h](../../work/runtime/include/rex/graphics/d3d12/native_shader_contract.h) and [its parser](../../work/runtime/src/graphics/d3d12/native_shader_contract.cpp): `shader`, `[pixel.inputs]` and `ParseInput`, with the refusals of 8.2.1 and 8.2.2.
- [shader_override.cpp](../../work/runtime/src/graphics/d3d12/shader_override.cpp): for a replacement, `BuildContract` leaves out the packed `XE_FLOAT_CONSTANT_<n>` macros and adds `XE_REPLACEMENT`, `XeInputs`, `XE_LOAD_INPUTS`, `XE_IN_<NAME>`, and `xe_tex_<name>(coord)` with the instruction defaults of a fetch (computed LOD, no offsets) at the dimension of the shader's binding (3D with its stacked 2D array, else cube, else 2D) and with its first sampler binding of that fetch constant, and `XE_TEX_<NAME>`, the fetch constant (FXC takes no macro with an empty parameter list); `CheckInputs` checks the inputs against the translation; `Build` compiles the contract, the library, the replacement file as an include and the library's main, without the generated HLSL and without a hand-written override of the hash.
- [shader_override.hlsli](../../work/runtime/src/graphics/d3d12/shader_override.hlsli): with `XE_REPLACEMENT` the main runs the prologue, `XE_LOAD_INPUTS`, `xe_replacement`, the pass debug view and the epilogue, no hooks.
- Tests: `fm4_native_shader_contract_tests` parses a replacement with every kind of input and one without inputs, and refuses hooks with `shader`, inputs with hooks, a file without the `.hlsli` suffix or outside the look directory, inputs that are not a table, 14 bad registers, an upper-case name and a value that is not a string; `fm4_shader_mod_tests` compiles a replacement that reads every kind of input by name, with live knobs, with and without the pass debug view.
- Step gate, 28 October 2026 (on `b445ace`, before the rebase onto `ee2dbba`): `fm4_native_shader_contract_tests` and `fm4_shader_mod_tests` pass, the 414 DXBC fingerprints those of `f0b8f02`. A probe look outside the repository gave the final combine a replacement that writes its named inputs (the three fetches, the constants, the interpolator values) into test blocks at the top of the screen, and a classic mod of the same hash that writes the same values read by register. With the probe, the check (`n5sc8-check`, `career` at 2x) matched the signatures and bindings of the replacement's two modifications (`0000400000000003` and `0002400000000003`) with their DXBC translations; 560 shaders and 1,206 translations had the generator's bindings, 0 mismatches, none without HLSL, no DXBC fallback. The in-run A/B of the replacement against the classic probe on a frozen frame at 2x (`n5sc8-probe`, `n5sc8-probe2`): 0 pixels differ in all 18 comparisons of each run, so the names, the packed indices, the bindings and the signatures hold. The repository's look, whose entries are all hooks then, A/B byte-identical as on main (`n5sc8-ab`).

### 8.5 Step 5b: the final combine as the project's own tone mapping

- [fm4-final-combine.ps.hlsli](../../work/fm4-rex/look/fm4-final-combine.ps.hlsli), the project's own code, written from the formulas of the [B4 research](b4-shader-research.md): x = (scene² + bloom²) · 2^exposure, the filmic curve f(x) = (x(Ax + CB) + DE) / (x(Ax + B) + DF) − E/F, the grading LUT at f(x) · scale / f(white) + offset, the colour transform's three rows, and gamma 2 ([FinalCombine Formula](b4-shader-research.md#finalcombine-formula)), and the curves of [step 4](b4-shader-research.md#b4-step-4-tone-curves-16-october-2026). The registers of its inputs are observations of this computer's runs (the shader's contract and a look that wrote the values out), recorded with the formula in the B4 research; no ucode or generated HLSL was read into it. Its header states the same.
- With the look's tone curve off it computes the game's function. With a curve (`aces` and the other presets), it applies the curve to x itself where the classic mod inverts f from the LUT coordinate: `fm4_tonemap_curve` of [fm4-look.hlsli](../../work/fm4-rex/look/fm4-look.hlsli), which the classic mods' `fm4_tonemap_lut_coord` now calls too (their DXBC fingerprints are unchanged). Then saturation, contrast, the occlusion by name, the sharpening, the bloom and the HDR probe as in the classic mod.
- [shaders.toml](../../work/fm4-rex/look/shaders.toml): `E97AA211B556A9B3` takes `shader = "fm4-final-combine.ps.hlsli"` with 13 inputs: `scene` `tf0`, `bloom` `tf2`, `lut` `tf7`, `uv` `i0.xy` (both fetches read at it), `white` `i1.z`, `exposure` `i1.w`, `filmic_ab` `c5`, `filmic_cdef` `c6`, `lut_scale` `c0.x`, `lut_offset` `c1.x`, and the colour transform's rows `c2` to `c4`. The classic mod `shader_E97AA211B556A9B3.mod.hlsli` stays for RTV and runs without the switch.
- Tests: `fm4_shader_mod_tests` compiles the replacement against a contract of its 13 inputs at the look's defaults, with a tone curve (the code must differ), with `FM4_LOOK_AO=1` (it reads the occlusion) and with live knobs and the pass debug view; `fm4_native_shader_contract_tests` reads the look's manifest and finds the final combine a replacement of 13 inputs.
- Step gate, 28 October 2026 (on `b445ace`, before the rebase), at 2x with `native_shader_contract=true`:

| Check | Result |
|---|---|
| CPU tests | `fm4_native_shader_contract_tests` and `fm4_shader_mod_tests` pass; the 414 DXBC fingerprints those of `f0b8f02` |
| The check, `career` at 2x (`n5sc9-check`) | the ten entries' 38 checks match their DXBC translations' signatures and bindings, the replacement's two modifications among them; 560 shaders and 1,212 translations with the generator's bindings, 0 mismatches, none without HLSL, no DXBC fallback |
| The match against the game: the in-run A/B on a frozen frame, the look `saturation=1,taa=0,windshield=0` (every knob of the final combine at the game's value, so the classic mod's periods draw the game's result), `FM4_TILING_SCENARIO=2` (`n5sc9-game`) | 10 captures: the replacement's and the classic mod's differ in 1 pixel of 3,686,400 by 1 level of 255 in every pair; captures of one side are equal. At 1:1 the car and the sky at 2x show the same picture |
| The look's intended change: the same with `aces,taa=0,windshield=0` (`n5sc9-ab`) | 10 captures: the replacement and the classic `aces` mod differ in 36 pixels by 1 level, none by more: the tone curve applied to x and the classic mod's inversion of f agree within float error. At 1:1 `aces` against the game's look shows the curve's intended change, and the native capture beside the `-Rov` reference the same frame |

### 8.6 Close gate, 28 October 2026

BuildRoot `I:\FM4-build-usa`, the native path with `native_shader_contract=true`, the look `aces` unless noted; against the cells of increment 3 on main `b445ace` (`n5sc5-*`, `n5sc7-inc3`). The gate ran on the increment's code on `b445ace`. Local main had moved on to `ee2dbba` (N6 item 5 step (a), no shared files); on the rebased build, `5854933`, the CPU tests, the check, smoke and the match against the game ran again (last row).

| Check | Result |
|---|---|
| Empty caches, `career` at 1x (`n5sc9-cold` against `n5sc5-cold`) | no mod compiled on the draw path; 62 on the workers, 81 swaps; frames over 50, 100 and 250 ms 17, 11 and 3, the longest 468 ms. WARN: the guest's idle waits 9.3 against 2.9 ms a second, pipelines over 4 ms 183 against 181, frames over 100 ms 11 against 9 |
| Smoke on the native path (`n5sc9-nat` against `n5sc5-nat`) | 3 of 3 cells; races 233.1 and 232.9 fps (p99 5.81 and 6.03 ms) against 239.1 and 239.1, at 2.27 and 2.33 ms of GPU a frame against 2.24 and 2.32. WARN on the guest's idle waits and the races' p99, `photo_view` on capture statistics. The GPU time is unchanged and the time went into the CPU's waits; main's build at 3/2 in another session's cell at 03:00 (`n6b1m32`) ran its races at 220 and 227 fps against 238 earlier in the night: the machine's state |
| Perf at 2x, three runs (`n5sc9-p2x` against `n5sc7-inc3`) | a fit with the draws: GPU = 4.22 ms + 0.353 ms per 1,000 draws in the race, the increment +0.117 ms; in photo mode −0.133 ms; within the ±0.15 ms of main's own spread (7.2), so no bracket. The render thread +0.22 and +0.75 ms, within the runs' spread (photo mode 5.71 to 7.23 ms a frame on main's cell, 6.32 to 7.82 here); the replacement is one draw a frame and adds no work on the render thread |
| Freeze parity at 2x without the contract (`n5sc9-off` against `n5sc5-off`) and with it (`n5sc9-on` against `n5sc9-off`) | PASS and PASS. The classic mods share `fm4-look.hlsli`'s `fm4_tonemap_curve` |
| `-Rov`, freeze parity at 1x with `original,taa=0` (`n5sc9-fzrov` against `n5sc5-fzrov`) | PASS |
| After the rebase onto `ee2dbba` (`5854933`: `n5sc10-check`, `n5sc10-nat` against `n5sc9-nat`, `n5sc10-game`) | the CPU tests pass, the DXBC fingerprints unchanged; the check's 38 entry checks match, 560 shaders and 1,212 translations with the generator's bindings, 0 mismatches, no DXBC fallback; smoke 3 of 3 cells, races 235.2 and 236.5 fps (p99 5.67 and 5.33 ms), `career` FAIL on the whole run's p99 (33.12 against 17.38 ms, a menu window) with its phases within the limits, `photo` and `photo_view` WARN on the guest's register waits and capture statistics; the match against the game: 10 captures, 0 pixels differ in all 18 comparisons |

Not covered: RTV (out of the gates; the contract is native only), 60 and 120 Hz, the final combine's variants with the vignette and depth of field (classic mods), a replacement in some passes only, and `b`, `position` and `front_face` inputs on the GPU (the CPU tests compile them; the final combine reads none).

## 9. Increment 6: vertex shader hooks

The design, read from the code of `bb922c6`. The main session took decisions 1 to 6 as recommended below on 28 October 2026, while the project owner was away; the owner may overturn them. They are in 9.3, each with its alternatives.

### 9.1 What the design starts from

- **Vertex shaders have no mods today.** `HlslShaderTranslator::StartTranslation` refuses every shader that is not a pixel shader, and `ProcessVertexFetchInstruction` refuses vertex fetch. Every vertex shader draws its DXBC translation, on every path.
- **The DXBC translator's vertex path** ([dxbc_translator.cpp](../../work/runtime/src/graphics/pipeline/shader/dxbc_translator.cpp), [dxbc_translator_fetch.cpp](../../work/runtime/src/graphics/pipeline/shader/dxbc_translator_fetch.cpp)):
  - the prologue (`StartVertexOrDomainShader`, `StartVertexShader_LoadVertexIndex`): every register and every interpolator output zeroed; `SV_VertexID` into `r0.x`, zeroed for the closing vertex of a line loop, endian-swapped by `vertex_index_endian`, offset by `vertex_index_offset`, masked to 24 bits, clamped to `vertex_index_min` and `vertex_index_max`, converted to float. The domain shader types of tessellation take other prologues;
  - vertex fetch (`ProcessVertexFetchInstruction`): the element's byte address from the fetch constant and the index (floored, or rounded with `is_index_rounded`, times the stride), kept for the `vfetch_mini` instructions after a `vfetch_full`; the needed words loaded raw from shared memory (the SRV `t0`, or the UAV `u0` when `kSysFlag_SharedMemoryIsUAV` says the draw has memexport); the endian swap from the fetch constant (8-in-16, 8-in-32, 16-in-32); the unpacking of the format (the packed 8, 10, 11 and 16-bit formats, the half and full floats, 32-bit integers), signed and unsigned, normalized with the signed mode of `signed_rf_mode` or integer, the exponent adjust, and the destination swizzle with constant 0 and 1;
  - the exports: `oPos` into a system temporary, `o0` to `o15` into the interpolator outputs of the modification's mask (packed, in register order), and `oPts` (point size, edge flag, vertex kill);
  - the epilogue (`CompleteVertexOrDomainShader`): W from 1/W unless `kSysFlag_WNotReciprocal`, XY and Z multiplied back by W where the guest returned them divided (`kSysFlag_XYDividedByW`, `kSysFlag_ZDividedByW`, all from `PA_CL_VTE_CNTL`), the user clip planes as clip or cull distances, the guest-to-host clip space transform (`ndc_scale`, `ndc_offset` times W; the native path's jitter and the logical width's horizontal scale already sit in those constants), the vertex kill (a cull distance with the AND rule, else W set to NaN), and the point size.
- **The signatures** (`WriteInputSignature`, `WriteOutputSignature`): the input is `SV_VertexID`; the outputs are `TEXCOORD0` and up for the interpolators of the mask, `SV_Position`, the clip and cull distances of the modification, and `XEPSIZE` where the point size is written. The pixel shader and the geometry shaders of points and rectangle lists read these.
- **The vertex shader keys.** The runtime keys a vertex shader by XXH3 over its microcode, and the XDK's vertex fetch rewrite at the draw flush writes the bound vertex layout's fetch instructions into the shader's own microcode ([d3d-hle.md 2.6](d3d-hle.md#26-shaders-and-their-hash-keys), [13.3](d3d-hle.md#133-shader-keys-and-constant-blocks)). One shader of the title therefore has several keys, one per vertex layout it has been drawn with, and a key exists only once the title has drawn with it. The native path keeps these keys (`Shader::ucode_data_hash`, the native draw records' translations). `passes.toml` already lists such siblings by hand: the car paint's motion source is both `8D05A46C3584CA9E` and `BBD01C26B1C45733`, and the glass, lights and cockpit have two keys each.
- **What increments 1 to 5 give.** The manifest and its parser (only `[[pass]]` and `[[pixel]]`), the pass classes (`Classify`, the host values' W), the live knob slots in `b0`, the bindings assigned by the generator as the DXBC translator does (5.1), the expected signatures and their check, the override cache per ucode hash and modification, the compile off the draw path with the swap (`SwapCompiledMods`), the check mode (`native_shader_contract_check`) and the in-run A/B (`native_shader_contract_ab_ms`). Stage N6's offline pass ([d3d-hle-n6.md 19.3.2](d3d-hle-n6.md#1932-design), part 2) waits for this increment to take vertex shaders through the generator; until then it takes their DXBC translations.

### 9.2 The design

#### 9.2.1 The vertex HLSL generator

`HlslShaderTranslator` takes vertex shaders of the host type `kVertex`, as `xe_vertex_shader(inout XeVertex v)`, mirroring the DXBC translator operation by operation as the pixel path does ("keep both in sync"):

- **The prologue** in the library (`xe_vs_prologue`): the registers and the 16 export registers zeroed, the vertex index as the DXBC translator loads it, from the same system constants.
- **Vertex fetch** in the library (`xe_vfetch_address`, `xe_vfetch_load`, `xe_vfetch_unpack_<format>`): the generator writes per instruction the address (kept in a variable for the `vfetch_mini` that follow), the load of the needed words through `ByteAddressBuffer xe_shared_memory : register(t0)` or `RWByteAddressBuffer xe_shared_memory_uav : register(u0)` behind the same flag test, the endian swap with the fetch constant's endianness, and the unpacking with the instruction's format, signedness, signed mode, integer flag and exponent adjust as literals. Every format the DXBC translator unpacks is generated; one it refuses stays refused (a translation error, so the fallback).
- **The ALU and control flow** of the pixel path, unchanged; `StoreResult` gains the vertex targets: `oPos` (`v.position`), `o<n>` (`v.o[n]`), `oPts` (`v.point_size_edge_flag_kill`). Memexport stays refused, as in the pixel path.
- **Vertex texture fetch** through the pixel path's fetch code; the vertex shader's bindings are assigned by the same mirrored code (5.1), stored with `SetBindingsOnce`, and compared by the check mode.
- **The epilogue** in the library (`xe_vs_epilogue`): the W, XY and Z conventions, the user clip planes, the guest-to-host transform, the vertex kill and the point size, as `CompleteVertexOrDomainShader`, then the packing of the export registers of the mask into `TEXCOORD0` and up.
- **Exactness.** The generated code keeps the DXBC translator's operations and their order: `precise` on every temporary, `mad()` where the DXBC translator writes `mad`, a division where it divides (`1/W`), `asfloat` literals for NaN, so that FXC cannot fuse, reorder or fold them. The pixel path reaches byte-identical pictures this way (5.2); the vertex path's position goes into rasterization, where one unit in the last place can move an edge pixel, so 9.2.4 measures it on every vertex shader of a frame, not only on entries.
- **Out of scope**, a translation error and so the fallback (9.2.6): the domain shader types of tessellation, memexport, and an unsupported fetch. A survey (9.4, step 6a) counts which vertex shaders of a career and a photo run fall back.

The generated HLSL is made at run time and stays out of the repository (2.6), as for pixel shaders.

#### 9.2.2 Hooks

A vertex entry's hooks file is included into the generated shader as a pixel entry's is, with defaults that change nothing:

- `XE_HOOK_VFETCH(fetch, offset, value)`: the unpacked result of each vertex fetch (its fetch constant and word offset as literals), before the destination swizzle;
- `XE_HOOK_CONSTANT(n, value)`: float constant c<n>, as for pixel shaders (`XE_C(n)` by the vertex shader's packed index);
- `XE_HOOK_VERTEX_OUTPUT(v, input)`: the exports after the ucode and before the epilogue's guest-to-host transform. `v.position` is the guest's clip space position with the W, XY and Z conventions already applied, so a hook reads and writes an ordinary homogeneous position, before the jitter, the logical width's scale and the viewport conversion; `v.o[n]` are the guest's export registers `o0` to `o15` (those outside the modification's mask are dropped by the epilogue); `v.point_size_edge_flag_kill` is `oPts`; `input` is the vertex after the prologue (the index in `r0.x`).

The library defines `XE_VERTEX_SHADER`, `XE_INTERPOLATOR_MASK` (the modification's mask, so a hook can test whether an export reaches the pixel shader), the pass class (`xe_native_pass()`, 2.2) and the knobs of `params` as for pixel entries.

#### 9.2.3 The manifest

```toml
[[vertex]]
hash = ["8D05A46C3584CA9E", "BBD01C26B1C45733"]  # the car paint: one per vertex layout
hooks = "vs-car-paint.mod.hlsli"
passes = ["scene", "mirror"]   # optional, as for pixel entries
params = ["FM4_LOOK_EXAMPLE"]  # optional
```

- `hash` is a key or a list of keys (decision 1); a key in two entries is refused, and a key may have a pixel and a vertex entry, since the two kinds of keys are separate.
- `hooks` only: vertex replacements (a `shader` with named inputs and outputs) are a later increment; the parser refuses `shader`, `inputs` and `srv` in a vertex entry.
- `passes`: the draw's class (2.2), now also computed for draws whose vertex shader has an entry; outside the entry's passes the draw takes the DXBC translation (a vertex `native_plain` bit in the modification's second word, which the translator does not read, as for pixel shaders).
- The hooks file follows the look directory's rules and is named so that the classic mods' lookup (`shader_<hash>.mod.hlsli`) never takes it as a pixel mod: anything but the `shader_` prefix.

#### 9.2.4 The checks and the gate's byte-identical proof

- **Signatures and bindings.** `native_shader_contract_check` compares, for every vertex shader the DXBC translator translates, the bindings the generator assigns with the DXBC translation's, and for every vertex entry the override's input and output signatures with the DXBC translation's and with those expected from the modification (9.1). Every mismatch is an error, as in increment 2.
- **Every vertex shader through the generator** (`native_shader_contract_vertex_all`, a development switch, off by default): every vertex shader of the host type `kVertex` draws its generated HLSL without hooks, entry or not. With the in-run A/B (`native_shader_contract_ab_ms`), the periods alternate between the generated vertex shaders and their DXBC translations (the vertex `native_plain` bit), and the pixel shaders do not change. The gate's byte-identical proof is this A/B on a frozen frame at 2x (`taa=0,windshield=0`, `FM4_TILING_SCENARIO=2`): 0 pixels differ in every comparison, over all vertex shaders of the frame at once: the scene, the mirrors, the shadow maps, the environment cube, the post chain's quads and the HUD.
- **A regression fingerprint.** `fm4_shader_mod_tests` compiles the library's vertex main against a synthetic contract (every export, the epilogue's flags) and records its DXBC fingerprint beside the 414 of the pixel mods, so a later library change that alters vertex code shows in the CPU tests.

#### 9.2.5 The first hooks

- **Without an effect (6a).** A gate look outside the repository with an identity hook entry (`XE_HOOK_VERTEX_OUTPUT` that writes back what it reads) for the car paint's keys: the A/B of the entry against the DXBC translation byte-identical, the pass log showing the entry's draws in `scene` and `mirror`, and the check matching its signatures.
- **With a visible effect (6b).** A probe in the same gate look: the car paint's hook moves the position by a fixed offset in guest clip space (`v.position.xy += k * v.position.w`, k chosen for 16 host pixels at 2x) and zeroes one export register that the paint's pixel shader reads. At 1:1, in the entry's periods of the A/B the car body is shifted by exactly 16 pixels against the rest of the car (glass, lights, rims, which are other vertex shaders), and the zeroed interpolator shows as the paint's change; in the other periods the frame is the reference. Neither hook goes into the repository: they prove the mechanism, as 5a's probe did.
- **A hook in the repository** comes with a look decision of the project owner, as 5b did. No look needs one today: the widescreen projection that [d3d-hle.md 8.2](d3d-hle.md#82-shaders-on-the-native-path) gives as the example is done by the logical width in the guest-to-host transform. A candidate is the cockpit camera's field of view as a knob; it would need the post passes' camera (`passes.toml`'s `camera`, read from the vertex shaders' constants) to see the same projection, or the motion vectors, TAA and GTAO would disagree with the picture, so it is a design of its own.

#### 9.2.6 Fallback and what stays the same

- The DXBC translation, with an error naming the entry, the key and the modification, per modification: a host type other than `kVertex`, memexport, an unsupported fetch, a failed compile or a signature mismatch. While a worker compiles an entry, its DXBC translation draws, and `SwapCompiledMods` swaps the vertex translation's pipelines as it does a pixel translation's; the root signature follows the vertex shader's bindings, which the check holds to the DXBC translator's.
- RTV and `-Rov`: no vertex entry, no generated vertex shader (the contract and the switch are native only). The DXBC translator does not change, so its DXBC is main's, and the 414 fingerprints of the pixel mods stay main's.
- The override cache keys a vertex entry's binary by its key, modification, generated HLSL, library and hooks, as for pixel entries, so stage N6's offline pass takes vertex entries, and with the switch every vertex shader, the same way.

### 9.3 The decisions

The main session's decisions of 28 October 2026, taken while the project owner was away and open to the owner's review: 1 to 6 as recommended in the table. Beside them:

- Decision 1: the fetch-blind key (B) is considered only if the survey shows a hooked shader under layouts that the gate runs do not meet.
- Decision 3: the A/B with `native_shader_contract_vertex_all` runs at 3/2 as well as at 2x, both with 0 pixels different: at 3/2 `ndc_scale` and `ndc_offset` are fractional, where a fusion by FXC that integer scales hide would show.
- Decision 4: the cockpit camera's field of view stays a candidate for the project owner, with the post passes' camera to redesign (9.2.5); it is not part of the increment.
- Decision 6: the survey counts the domain shaders; if FM4 draws any in a race or photo mode, the next report names them, and the scope stays `kVertex`.

| # | Decision | Recommendation (taken) | Cost (person-days) |
|---|---|---|---|
| 1 | The manifest's key for vertex entries | the ucode key, one or a list, with a warning for keys that look like an entry's under another layout | 1 |
| 2 | What hooks see | the guest's exports by register, the position in the guest's clip space before the guest-to-host transform | 1 |
| 3 | How the byte-identical gate is measured | the in-run A/B with every vertex shader through the generator, plus the signature and binding check | 1.5 |
| 4 | The first hooks | 6a an identity hook, 6b a probe moving the car paint and zeroing an export, both in a gate look outside the repository; no repository hook without a look decision | 1 |
| 5 | `passes` for vertex entries | yes, with a vertex `native_plain` bit | 1 |
| 6 | The generator's scope | `kVertex` only; tessellation and memexport fall back | 0 |

#### 9.3.1 The key

- **A (recommended): the ucode key, a string or a list of keys.** The same key the runtime, the native draw records and `passes.toml` use. The survey (9.4) logs, per vertex shader, its key and a fetch-blind key: XXH3 over the microcode with the fields of every vertex fetch instruction that the rewrite writes (format, stride, offset, signedness, number format, exponent adjust) zeroed. A run that meets a key whose fetch-blind key equals one of an entry's keys' but that is not listed logs a warning once ("vertex shader <key> looks like entry <first key> under another vertex layout; not hooked"), so a new car, track or layout cannot lose a hook without a line in the log. Cost: 1 day with the survey's log.
- **B: the fetch-blind key itself as the manifest's key** (`family = "<key>"`). It covers layouts that no run has met, which A cannot. It needs the survey first: the rewrite has to change only the fields that are zeroed, which today is an observation of `sub_826EC0C0` (2.6) and not a measurement, and two shaders of the title that differ only in their fetch formats would share a hook (they are then the same shader under two layouts, which is what a hook wants). Cost: 1.5 days, and B can be added to A later without a change to A's entries.
- **C: the guest's shader object** (the address at `SetVertexShader`): not stable across runs (heap addresses) and not known on a recorded command buffer's replay. Not recommended.

Why A: it is the key every tool of the project already reads, the hook works on the exports and not on the fetches, and the warning closes A's gap at the cost of a log line. If the survey shows that a hooked shader meets layouts in play that the gate runs do not, B follows.

#### 9.3.2 What hooks see

- **A (recommended): the guest's exports by register**, `v.position` in the guest's clip space with the W, XY and Z conventions applied, `v.o[n]`, `v.point_size_edge_flag_kill`, the registers in `input`, at one point before the guest-to-host transform (9.2.2). As the pixel hooks read `p.r[i]` and `XE_C(n)`, the vertex hooks read registers; the meaning of a register is an observation written into the hooks file, as the look's comments do today.
- **B: named outputs** from the manifest (`[vertex.outputs]`, `position = "oPos"`, `uv = "o0.xy"`), as replacements name their inputs. That is the contract of vertex replacements, a later increment; for hooks it adds a parser and a check for no gain, since a hook writes back into the same registers.
- **C: the raw `oPos`**, before the W, XY and Z conventions: every hook would carry the three `PA_CL_VTE_CNTL` cases. Not recommended.
- **D: the position after the guest-to-host transform** (host clip space): a hook would see the jitter, the logical width's scale and the half-pixel offset and could undo them by accident. Not recommended; a hook that needs host pixels reads `ndc_scale` and `ndc_offset` through the library.

#### 9.3.3 How the byte-identical gate is measured

- **A (recommended): the in-run A/B with `native_shader_contract_vertex_all`** on a frozen frame at 2x, every vertex shader of the frame generated in one period and DXBC in the other, 0 pixels differ; with the signature and binding check of every vertex shader translated in a career at 2x. It covers every vertex shader a frame draws, uses the cells and tools of increments 1 to 5 (`ab_compare.py`, `ab_levels.py`), and needs only the switch (1 day with the A/B's vertex bit). Its limit: an output that no pixel reads (an export outside every mask, a clipped vertex) is not seen, and a difference shows as edge pixels whose source the A/B does not name.
- **B: the vertex outputs compared per draw** (stream output of both translations into a buffer for sampled draws, compared by dword). It names the shader and the export that differs and sees what the picture does not, but it adds stream-output pipelines next to every sampled pipeline and a readback path, 3 to 4 days of code used by the gate only. Recommended only as the diagnostic if A finds a difference that its crops do not explain.
- **C: DXBC fingerprints of the generated shaders against the DXBC translator's**: the two translators write different code for the same function, so the fingerprints never match; they serve as a regression check of the library (9.2.4), not as the proof.

#### 9.3.4 The first hooks

As 9.2.5: 6a, an identity hook on the car paint's keys, byte-identical in the A/B; 6b, a probe in a gate look outside the repository with an exact, measurable effect (a shift of 16 host pixels at 2x and a zeroed interpolator) at 1:1. Alternatives: a repository hook with a visible effect now (the cockpit's field of view), which is a look decision for the project owner with the post passes' camera to redesign (9.2.5), or only 6a, which proves nothing a hook changes. The recommendation keeps the repository's look unchanged in this increment.

#### 9.3.5 `passes` for vertex entries

- **A (recommended): yes**, the draw's class for entries of either kind; outside an entry's passes the DXBC translation (a vertex `native_plain` bit). The classification exists and costs one lookup per draw; a hook that should leave the shadow pass or the mirror alone needs it.
- **B: no `passes` in this increment**, every draw of a hooked shader hooked: a day less, and the first hook in the repository would wait for it.

#### 9.3.6 The generator's scope

- **A (recommended): `kVertex` only.** The domain shader types are the tessellated draws; whether FM4 draws any is a question for the survey (9.4), and they fall back to the DXBC translation with an error if one has an entry. Memexport falls back as in the pixel path.
- **B: the domain shaders too**: four more prologues and the hull shader's interplay, about 3 days, for shaders the title may not have.

### 9.4 Steps, gates and effort

- **6a: the generator and the mechanism.** The survey first (a log line per vertex shader: the key, the fetch-blind key, the host types and modifications met, the fetch formats, whether it would fall back); then the generator, the library's vertex main, prologue, fetch and epilogue, the manifest's `[[vertex]]`, the passes, the hooks, the expected signatures, the check, the switch and the A/B bit, the override cache and the swap for vertex translations, and the CPU tests (the parser's refusals, the library compiled with every hook, the fingerprint). Step gate: the CPU tests with the 414 fingerprints unchanged; the check in a career at 2x (every vertex shader's bindings, every entry's signatures, 0 mismatches, the fallbacks counted); the A/B with the switch byte-identical on a frozen frame at 2x; the identity hook's A/B byte-identical with its pass log.
- **6b: the probe.** The gate look's probe at 1:1 (crops at 2x), the A/B's periods against each other.
- **The close gate**, as increment 5's: empty caches (the vertex shaders' compiles on the workers, none on the draw path), smoke on the native path with the contract and with the switch, perf at 2x (three runs; with the switch the generated vertex shaders' GPU time a frame against their DXBC translations', judged per draw), freeze parity off against on, `-Rov` freeze parity at 1x, and a native capture against the `-Rov` reference at 1:1.

| Part | Person-days |
|---|---|
| The survey | 0.5 to 1 |
| The generator: prologue, vertex fetch with every format and the endian swaps, the exports, the epilogue | 6 to 8 |
| The library's vertex main, `XeVertex`, the hooks | 1 to 1.5 |
| The manifest, the passes for vertex entries, the warning of decision 1 | 2 |
| The expected signatures and the check; the switch and the A/B bit | 2 to 2.5 |
| The override cache, the compile off the draw path and the swap for vertex translations | 2 to 3 |
| The CPU tests | 1.5 to 2 |
| 6a's step gate, 6b's probe, the close gate | 5 to 6 |
| **Total** | **20 to 26** |

That is 4 to 5 person-weeks, inside section 3's 3 to 5.

### 9.5 Risks

1. **Byte-identical positions.** FXC compiles the generated HLSL, while the DXBC translator writes its instructions directly; a fused multiply-add, a reciprocal for a division or a folded constant changes a position by one unit in the last place, which moves the coverage of edge pixels. The pixel path shows that `precise` and the explicit operations reach byte identity, but a position difference shows as scattered edge pixels whose shader the A/B does not name. Mitigation: the switch takes all vertex shaders, so a difference shows in the first A/B; the per-draw output comparison (9.3.3 B) is the diagnostic; a vertex shader that cannot be made exact stays on its DXBC translation and cannot take hooks.
2. **Keys that the gate runs never meet.** A hooked shader under a vertex layout that no gate run drew has a key the manifest does not list; decision 1's warning names it, and B would close it.
3. **The swap of vertex translations.** The compile off the draw path swaps pipelines by translation; a vertex translation sits in more pipelines than a pixel one (every pixel shader it pairs with), so the swap recreates more pipelines at once, and the root signature must hold for all of them (the check of the vertex bindings).
4. **Shared code with `-Rov`.** The library and the DXBC translator are shared; the vertex code is new and compiled only for the native path's generated vertex shaders, and the pixel fingerprints and the `-Rov` freeze parity prove that nothing else changed.

### 9.6 Step 6a: implementation

- **The survey** (`native_shader_vertex_survey`, [shader_override.cpp](../../work/runtime/src/graphics/d3d12/shader_override.cpp)): every vertex shader translated is logged once with its key, its fetch-blind key, a coarse key (also the fetch constant, the prefetch count and both swizzles zeroed), its fetches (format, signedness, stride, offset), textures, memexport and dynamic addressing; every translation once with its host type, interpolator mask, clip and cull distances and point size; and every key whose coarse key an earlier key had, with the fetch instruction fields in which the two differ and whether their fetch-blind keys agree. The analysis keeps the vertex fetch instructions' indices (`Shader::vertex_fetch_instructions`).
- **The generator** ([hlsl_translator.cpp](../../work/runtime/src/graphics/pipeline/shader/hlsl_translator.cpp)): vertex shaders of the host type `kVertex` as `xe_vertex_shader(inout XeVertex p)`; vertex fetch as the DXBC translator does it (the address kept for `vfetch_mini`, the words through `xe_shared_memory_word`, `xe_vfetch_swap`, the unpacking of every format with its constants as exact bit patterns, the exponent adjust, `XE_HOOK_VFETCH`); the exports `p.position`, `p.o[n]` and `p.point_size_edge_flag_kill` (the point size clamped as the DXBC translator clamps it). Kill and getGradients in a vertex shader are translation errors.
- **The library** ([shader_override.hlsli](../../work/runtime/src/graphics/d3d12/shader_override.hlsli), `XE_VERTEX_SHADER`): shared memory as `t0` and `u0` of space 0, the prologue (the vertex index), `xe_vs_normalize_position` (the W, XY and Z conventions), `XE_HOOK_VERTEX_OUTPUT`, and the epilogue (the user clip planes, the guest-to-host transform as a multiply and a `mad`, the vertex kill, the outputs). The derivatives of the texture fetch helpers compile in pixel shaders only, so the pixel shaders' DXBC does not change.
- **The contract** (`BuildContract`, `BuildVertexContract`): the system constants and flags of the vertex path, the registers, the interpolator mask, the distances, the float constants and descriptor slots, `XeVsInput`, `XeVsOutput` with the DXBC translation's register layout (the clip and cull distances of one register share it, the point size in its own), and `XE_STORE_OUTPUTS`. The expected signatures (`ExpectedVertexSignatures`) check every compile; the target is `vs_5_1`; the cache files are named `vertex_<key>_<modification>_<hash>.dxbc`.
- **The manifest** ([native_shader_contract.cpp](../../work/runtime/src/graphics/d3d12/native_shader_contract.cpp)): `[[vertex]]` with `hash` (a key or a list), `hooks`, `passes` and `params`; refused are a missing or empty key list, a bad key, a key twice, missing hooks, hooks outside the look directory or named `shader_*`, `shader`, `inputs`, `srv` and any other key.
- **The draws** ([command_processor.cpp](../../work/runtime/src/graphics/d3d12/command_processor.cpp)): a draw is classified when its pixel or its vertex shader has an entry; outside a vertex entry's passes the vertex modification's `native_plain` bit keeps the DXBC translation, and in the A/B's other periods `native_classic` does, for entries and with `native_shader_contract_vertex_all`. Pipelines of `native_classic` vertex translations are not stored.
- **The translation** (`TranslateEntry`, `Apply`, `BuildVertex`): as for pixel entries, a vertex shader that takes its generated HLSL (`GeneratesVertex`: an entry or the switch, `kVertex`, no memexport, neither bit) is translated without the DXBC translator when the override cache holds it, else its DXBC translation draws until a worker's compile is swapped in; `SwapCompiledMods` recreates the pipelines of a swapped vertex translation too. Hot reload stays with pixel shaders.
- **The checks**: `native_shader_contract_check` compares the generator's bindings with the DXBC translation's for every vertex shader of the host type `kVertex` too, counted apart from the pixel shaders, and each generated vertex shader's signatures with its DXBC translation's and the expected ones. With vertex entries, `NoteVertexShader` warns once for a key that the manifest does not list but whose fetch-blind key an entry's key has.
- **Tests**: `fm4_native_shader_contract_tests` parses vertex entries and 13 refused ones; `fm4_shader_mod_tests` compiles the library's vertex main with a vertex shader in the generator's form: identity hooks give the same DXBC, moving hooks another, the native contract's pass class without a read the same, and the distances and the point size compile; it prints the vertex shaders' fingerprints apart from the 414 of the pixel mods.
- **The survey's result**, a career and photo mode at 2x, 28 October 2026: 349 vertex shaders in 408 translations, all of the host type `kVertex` (no domain shader, so decision 6's scope holds), none with clip or cull distances or a point size, 7 with texture fetches, none without HLSL and no DXBC fallback. 346 fetch-blind keys, one of them with two keys: one rewrite by the XDK, which changes the stride and no other field. Decision 1's key A stands, and B (9.3.1) has nothing to cover today.

### 9.7 The A/B's reference: precise DXBC translations

The vertex A/B with `native_shader_contract_vertex_all` (9.2.4) does not reach 0 pixels on every run, and the cause is its reference, not the generator:

- **The runs.** On a frozen frame, the generated vertex shaders and their DXBC translations differ in 0 or 1 pixel at an edge by 1 level of 255: at 2x 0 pixels on `c72b1a6` and 1 on `ed6c985`; at 3/2 1 pixel on `c72b1a6` and 0 on `ed6c985`. Captures of one side are always equal. The frozen frame differs from run to run (2,583,452 pixels at 2x, 516,883 at 3/2 between two runs of one build), so each run samples other edges.
- **The precise flags.** FXC compiles the generated HLSL with `D3DCOMPILE_IEEE_STRICTNESS` and marks every instruction that writes a register precise; the generator writes a guest `mad` as a multiply and an add, and `dp#` as sequential adds, the DXBC translator's order. The DXBC translator writes the same operations without precise flags, and its own comment on `dp#` ([dxbc_translator_alu.cpp](../../work/runtime/src/graphics/pipeline/shader/dxbc_translator_alu.cpp)) notes that the driver may fuse a multiply and an add where they are not precise. This GPU (an RTX 5070 Ti Laptop GPU) does: with the flags cleared from the generated vertex shaders (`112f062`, not kept), the A/B differs in 537 pixels at 2x and 9 at 3/2. Without the flags on both sides, the DXBC translation is not a deterministic reference for exact code, and the edge differences of at most 1 pixel are rounding noise of the reference, not a defect of the generator.
- **Decision A** (the main session's decision of 28 October 2026, open to the project owner's review): `native_shader_contract_ab_precise`, a development switch of the native path, off by default, marks the DXBC translations of vertex and pixel shaders precise as FXC marks the generated HLSL, before `Apply`, and signs the container again (`MarkPrecise` in [shader_override.cpp](../../work/runtime/src/graphics/d3d12/shader_override.cpp)). Both sides of the A/B are then exact code, and its byte-identical proof is strict. The rule (every instruction that writes a register, not the declarations or flow control; a null first destination takes the second; a texture load or sample on four components) gives FXC's own flags back on 5,793 FXC binaries whose flags were cleared, except for `resinfo`'s component mask, which rounds nothing. `-Rov` and RTV do not change: the switch is native only, and without it the DXBC translations are main's.
- **B, not taken**: the gate's criterion as at most 1 pixel by 1 level at an edge per comparison, as increment 5's match against the game. No code, but a weaker proof: a generator defect of that size would pass.
- **C, not taken**: the vertex outputs compared per draw by stream output (9.3.3 B), 3 to 4 days of gate-only code.
- **For N6's increment 1.2** (the offline pass with DXC, [D3D-level HLE, N6](d3d-hle-n6.md)): the generator compiles with IEEE strictness, so all of its code is precise. Once every shader goes through the generator and DXC in the offline pass, that gives up the fusion of a multiply and an add that the DXBC path gets from the driver today. 1.2 measures that cost per draw and decides between precise only where the title needs it and strictness everywhere.

### 9.8 Step gate of 6a and 6b, 28 October 2026

BuildRoot `I:\FM4-build-usa`, `848b52a` on main `c4607b8`, the native path with `native_shader_contract=true`. Every cell ran from the snapshot of the first one (`snapshots\n5sc16-ab2x1`). The vertex A/Bs use a gate look outside the repository whose `[[vertex]]` entries hook nothing (`look-v0`), with `native_shader_contract_vertex_all` and `native_shader_contract_ab_precise`. Freeze cells use `aces,taa=0,windshield=0` and `FM4_TILING_SCENARIO=2`. The 6a and 6b rows ran earlier, on `c72b1a6` and `ed6c985`, the increment's code before the switch.

| Check | Result |
|---|---|
| CPU tests | `fm4_native_shader_contract_tests` and `fm4_shader_mod_tests` pass; the 414 DXBC fingerprints of the pixel mods unchanged |
| The check, `career` at 2x with the switch (`n5sc16-check`) | 446 vertex checks (408 translations and the 38 entry checks), 560 shaders and 1,216 pixel translations with the generator's bindings, 0 mismatches, none without HLSL |
| The vertex A/B at 3/2, three runs (`n5sc16-ab321` to `n5sc16-ab323`) | 0 pixels differ in every comparison of all three runs |
| The vertex A/B at 2x, three runs (`n5sc16-ab2x1` to `n5sc16-ab2x3`) | 0, 4 and 2 pixels of 3,686,400 differ, the same in every comparison of a run: single pixels, 1 to 3 levels in one channel, in the interior mirror's picture and on the edge of a trackside board. At 1:1 nothing shows |
| The pixel A/B at 3/2 with the repository's look (`n5sc16-px32`) | 9 pixels differ by 1 level in one channel, spread over the screen: the final combine's replacement against the classic `aces` mod, within 5b's criterion of 2 levels (8.2, the inversion of F in the hooks). The classic mods are FXC's code too, so the switch does not touch this cell |
| 6a, the identity hook on the car paint's keys (`n5sc11-vid`, `n5sc14-vid`; the pass log `n5sc11-vdbg`) | the A/B 0 pixels in both runs; the pass log shows the entry's draws in `scene`, `mirror` and `other` |
| 6b, the probe (`n5sc11-vmove`) | in the entry's periods the car body sits exactly 16 host pixels from the rest of the car, with `o0` zeroed; the other periods are the reference |

**The 2x residual** (the main session's decision of 28 October 2026, open to the project owner's review):

- **A, taken:** the residual is accepted as documented. It is at most 4 single pixels and at most 3 levels in one channel. At 3/2 it is 0 with the switch, and the check has 0 mismatches. Generated vertex shaders draw only for manifest entries or with the development switch, and no entry of the repository hooks a vertex shader yet.
- **B, not taken:** hold the increment and find the cause first.

The cause is open. Both sides are precise, so it is not a fusion by the driver. Three sources are ruled out:

- **The guest-to-host transform:** the same multiply by `ndc_scale` and `mad` of `ndc_offset` in both translators.
- **The guest's `mad` and `dp#`:** a multiply followed by adds in both.
- **The texture offsets' scale revert:** a factor of 1/2 at 2x, which is exact.

At a whole scale, many edges pass through sample positions exactly, so one unit in the last place of a position decides a pixel's coverage more often than at 3/2. The close gate's bisect cells (9.9) run the 2x A/B without the scene draws' path at a whole scale (`native_draw_scale_variant=false`), with 122 and with 90 surfaces.

### 9.9 Close gate, 28 October 2026

BuildRoot `I:\FM4-build-usa`, the native path. The gate ran on `848b52a` (main `c4607b8`), from the snapshot of the step gate (`snapshots\n5sc16-ab2x1`), against the same day's cells of main (`n6es2x`, `n6ep32`). It follows the gate rules of 28 October 2026: RTV and `-Rov` are out of all tests, so the image proof is the native path against main's own runs. Image-only cells ran two at once, each with its own run folder, profile copy and cold caches. The vertex A/Bs use the gate look `look-v0`, `native_shader_contract_vertex_all` and `native_shader_contract_ab_precise`, as in 9.8.

| Check | Result |
|---|---|
| Smoke at 2x, the default path (`n5sc17-s2x` against `n6es2x`) | 3 of 3 cells; `career` PASS, its race 186.2 against 187.9 fps; `photo` and `photo_view` WARN on capture statistics only (other race moments; at 1:1 the same rendering), the photo phase's GPU time 8.19 against 8.77 ms |
| Empty caches, `career` at 2x with the contract and `native_shader_contract_vertex_all` (`n5sc17-cold`) | no mod compiled on the draw path; 219 on the workers, 367 swaps, every vertex shader among them; frames over 50, 100 and 250 ms 10, 5 and 1, none over 50 ms with a mod compile, the longest 698 ms |
| Freeze parity at 3/2 without the contract (`n5sc17-p32` against `n6ep32`) and with it (`n5sc17-p32c` against `n5sc17-p32`) | parity PASS and PASS (`n5sc17-p32` WARN only on the guest's idle waits, 62.85 against 1.35 ms a second, with the other cell running beside it). A native capture at 1:1 beside main's shows the same picture |
| The 2x bisect: the vertex A/B without the scene draws' path at a whole scale (`n5sc17-bv`, `native_draw_scale_variant=false`, 122 surfaces) | 2 pixels of 3,686,400 differ by 1 level in every comparison: the residual stays without the scene draws' path |
| The same with 90 surfaces (`n5sc18-bs90`, `native_rt_surfaces=90`, no scene draws' path), alone | 1 pixel differs by 2 levels in every comparison. The paired run of this cell (`n5sc17-bs90`) stopped at its start: both cells of the pair wrote the BuildRoot's look directory (`I:\FM4-build-usa\look`) at once |
| After the rebase onto main `1803b36` (`2f745f4`: `n5sc18-s2x` against `n6es2x`, `n5sc18-check`) | the CPU tests pass, the 276 DXBC fingerprints of `f0b8f02`'s log unchanged; smoke 3 of 3 cells, `career` PASS (race 192.7 fps), `photo` and `photo_view` WARN on capture statistics; the check's 446 vertex checks and 1,211 pixel translations, 0 mismatches, none without HLSL |

**The bisect's result.** The residual at 2x stays without the scene draws' path and with either number of surfaces, so it is not tied to that path. It stays an open 2x rasterization sensitivity of the vertex A/B, accepted by decision A of 9.8. Where to look next, if a vertex entry in the repository ever needs strict byte identity at 2x: the vertex A/B of one shader at a time, by a manifest entry instead of `native_shader_contract_vertex_all`, on the mirror's and the trackside boards' draws, where the pixels sit; then the per-draw output comparison (9.3.3 B) for that shader.

**Two cells at once.** The freeze parity pair (`n5sc17-p32`, `n5sc17-p32c`, the look `original,taa=0`) ran in 6.8 minutes of wall time instead of 13.6, and both cells passed. A pair of cells with a look that has mods conflicts: `pipeline.look_cvars` deletes and writes `<BuildRoot>\look` for each run, so the second cell of the pair stopped when the first removed a file under it. Such cells run one at a time until the harness gives each run its own look directory.

Not covered: RTV and `-Rov` (out of all tests since 28 October 2026; the contract is native only), 60 and 120 Hz, perf at 2x with the switch (the generated vertex shaders draw only with the development switch or an entry, and the repository has no vertex entry), a vertex entry in the repository's look, and the domain shader types (the survey found none).
