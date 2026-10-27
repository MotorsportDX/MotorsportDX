# Native shader contract

Status: stage N5 item of the [native port](d3d-hle.md#82-shaders-on-the-native-path), 27 October 2026. The design was approved by the main session on 27 October 2026; increments 1 and 2 are implemented behind `native_shader_contract` (off by default). The other N5 items are in [d3d-hle-n5.md](d3d-hle-n5.md).

## Summary

- **Goal**: look mods stop depending on the DXBC translation of the title's own pixel shaders. Today a mod hooks into HLSL generated from the game shader's ucode, compiled against a contract read off the DXBC translation (section 1). The native path knows what the PM4 path does not (the surfaces a draw renders into, the post passes' final point, the vertex shader keys), and the contract uses that.
- **The contract**: `shaders.toml` in the look directory declares, per guest pixel shader (by its ucode hash), the mod it takes on the native path, the pass classes it applies in, and the knobs it reads as constants. Draws carry their pass class (`xe_native_pass()`). RTV and `-Rov` do not change.
- **Increment 1**: the manifest, the pass classes, the pass debug view, and the car windows (`E62CFDF09D758B6C`) moved onto the contract with the same picture (section 4).
- **Increment 2**: an entry's translation is made without the DXBC translator: the HLSL generator assigns the bindings as the DXBC translator does, and the signatures are checked against those derived from the modification and the ucode analysis. With an empty override cache the DXBC translation draws until a worker compiles the entry. The bindings of all 568 pixel shaders of a career and a photo run match their DXBC translations, and the entry draws byte for byte as before (section 5).
- **Increment 4**: the per-pass decisions. The car materials draw most of their race draws into the mirrors, which the manifest classes; the project owner kept the mirrors as the scene (section 6). The increments that follow are in section 3.

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
```

- **Key.** The pixel shader's ucode hash. An entry takes the place of the classic mod of its hash; hashes without an entry keep their classic mod.
- **Body.** `hooks`: a mod of the look directory, in the classic `.mod.hlsli` syntax, hooked into the game shader's generated HLSL. Replacement shaders (`shader`) and vertex shader entries are later increments; the parser refuses them.
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
