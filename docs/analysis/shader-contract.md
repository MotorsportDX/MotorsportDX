# Native shader contract

Status: stage N5 item of the [native port](d3d-hle.md#82-shaders-on-the-native-path), 27 October 2026. The design was approved by the main session on 27 October 2026; increments 1, 2, 4 and 3 are implemented behind `native_shader_contract` (off by default). The other N5 items are in [d3d-hle-n5.md](d3d-hle-n5.md).

## Summary

- **Goal**: look mods stop depending on the DXBC translation of the title's own pixel shaders. Today a mod hooks into HLSL generated from the game shader's ucode, compiled against a contract read off the DXBC translation (section 1). The native path knows what the PM4 path does not (the surfaces a draw renders into, the post passes' final point, the vertex shader keys), and the contract uses that.
- **The contract**: `shaders.toml` in the look directory declares, per guest pixel shader (by its ucode hash), the mod it takes on the native path, the pass classes it applies in, and the knobs it reads as constants. Draws carry their pass class (`xe_native_pass()`). RTV and `-Rov` do not change.
- **Increment 1**: the manifest, the pass classes, the pass debug view, and the car windows (`E62CFDF09D758B6C`) moved onto the contract with the same picture (section 4).
- **Increment 2**: an entry's translation is made without the DXBC translator: the HLSL generator assigns the bindings as the DXBC translator does, and the signatures are checked against those derived from the modification and the ucode analysis. With an empty override cache the DXBC translation draws until a worker compiles the entry. The bindings of all 568 pixel shaders of a career and a photo run match their DXBC translations, and the entry draws byte for byte as before (section 5).
- **Increment 4**: the per-pass decisions. The car materials draw most of their race draws into the mirrors, which the manifest classes; the project owner kept the mirrors as the scene (section 6).
- **Increment 3**: declared resources. An entry reads the post passes' textures by name (`srv = ["post:<name>"]`) and PNG files of the look directory (`"file:<name>"`, at most 8, each an 8-bit RGB or RGBA PNG up to 4,096 by 4,096). The race's final combine became an entry that reads the occlusion by name and draws byte for byte as its classic mod; a test pattern from a look file reaches the screen byte for byte as the same values computed in the shader (section 7). The increments that follow are in section 3.

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

The design, read from the code of `b445ace`. The main session took decisions 1 to 5 and 7 as recommended below, and step 5a of decision 6, on 27 October 2026; the project owner approved step 5b the same day (8.2.6). Step 5a is implemented behind `native_shader_contract` (8.4); 5b follows it.

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

Names are lower case (`[a-z][a-z0-9_]*`). The contract of a replacement defines `struct XeInputs` with a member per input that is not a texture, filled after the prologue (`XE_LOAD_INPUTS`), `XE_IN_<NAME>` for each input, and per texture `xe_tex_<name>(coord)`: the fetch at the dimension, signedness and filters of the game shader's binding for it, finished as any generated fetch (`xe_texture_finish`: signs, exponent bias, integer scale), and `xe_tex_<name>_size()` for a 2D texture. The packed `XE_FLOAT_CONSTANT_<n>` macros are not defined for a replacement, so the manifest lists every constant and interpolator it reads from the game.

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
- [shader_override.cpp](../../work/runtime/src/graphics/d3d12/shader_override.cpp): for a replacement, `BuildContract` leaves out the packed `XE_FLOAT_CONSTANT_<n>` macros and adds `XE_REPLACEMENT`, `XeInputs`, `XE_LOAD_INPUTS`, `XE_IN_<NAME>`, and `xe_tex_<name>(coord)` with the instruction defaults of a fetch (computed LOD, no offsets) at the dimension of the shader's binding (3D with its stacked 2D array, else cube, else 2D) and with its first sampler binding of that fetch constant, and `xe_tex_<name>_size()` for 2D; `CheckInputs` checks the inputs against the translation; `Build` compiles the contract, the library, the replacement file as an include and the library's main, without the generated HLSL and without a hand-written override of the hash.
- [shader_override.hlsli](../../work/runtime/src/graphics/d3d12/shader_override.hlsli): with `XE_REPLACEMENT` the main runs the prologue, `XE_LOAD_INPUTS`, `xe_replacement`, the pass debug view and the epilogue, no hooks.
- Tests: `fm4_native_shader_contract_tests` parses a replacement with every kind of input and one without inputs, and refuses hooks with `shader`, inputs with hooks, a file without the `.hlsli` suffix or outside the look directory, inputs that are not a table, 14 bad registers, an upper-case name and a value that is not a string; `fm4_shader_mod_tests` compiles a replacement that reads every kind of input by name, with live knobs, with and without the pass debug view.
