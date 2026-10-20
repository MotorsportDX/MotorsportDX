# Render path of default.xex and draws that a game patch can cut

Status: static analysis, 20 October 2026. Nothing here is implemented or measured in a game run. Addresses are the USA `default.xex` (SHA-256 `3618e85a…`); the Japan/Asia executable has the same code ([release comparison](releases.md)).

## Question

At 240 Hz and 1x the race is limited by the single command processor (CP) thread, which translates 2,400 to 3,050 draws per frame ([CP draw offload](cp-draw-offload.md)). Every 10% fewer draws gives roughly 8 to 10% more race fps. This note maps how the title builds its frame and lists the draws that a patch of the title (a hook in `work/fm4-rex/src` or a render scenario setting) can remove or run less often.

## 1. Setup

Everything lives outside the repository under `I:\FM4-re`. Ghidra projects, decompiled output, the copy of `default.xex` and the exported tables never enter the repository; only this note does.

| Item | Version or path |
|---|---|
| Ghidra | 12.1.3 portable, `I:\FM4-re\ghidra_12.1.3_PUBLIC` |
| JDK | Temurin 21.0.12.1+1, `I:\FM4-re\jdk-21.0.12.1+1` |
| Loader | XEXLoaderWV 12.1.3, in `Ghidra\Extensions\XEXLoaderWV` |
| GUI | `I:\FM4-re\launch-ghidra.cmd` (sets `JAVA_HOME`) |
| Input | `I:\FM4-re\input\default.xex`, a copy of the disc file: `analyzeHeadless.bat` fails on the parentheses in the disc path |
| Project | `I:\FM4-re\project`, project `fm4v2` (`fm4` is the first import, superseded) |
| Import | `I:\FM4-re\import.cmd` (headless import and auto-analysis, about 4 minutes) |
| Scripts | `I:\FM4-re\scripts`: `run.cmd <Script.java> <args>` runs a script read-only, `run-write.cmd` saves changes |
| Output | `I:\FM4-re\out` (tables and decompiled functions), `I:\FM4-re\logs` |

The scripts:

- `DisableNoReturn.java` (pre-analysis): turns off "Non-Returning Functions - Discovered". That analyzer marks the register save and restore helpers (`__savegprlr_*` at `0x82760450` to `0x827604E0`) as non-returning, which cuts almost every function after its prologue.
- `SeedFunctions.java` (pre-analysis): disassembles and creates a function at every function start of the recompiler (`out\recomp-funcs.txt`, from `<BuildRoot>\project\generated\default\fm4_rex_funcs.*.h`).
- `Summary.java`, `FindStrings.java`, `Xrefs.java`, `Export.java` (decompile with callers and callees).
- Python helpers that read the recompiler's generated C++ (which decodes VMX128): `recomp_callgraph.py`, `cg.py` (callers and callees), `pm4_scan.py`, `id_calls.py`, `key_calls.py`, `vtables.py` (RTTI vtables in `<BuildRoot>\loaded-image.bin`), `fg_breakdown.py` and `fg_frames.py` (draws per pass kind from `REX_FRAME_GRAPH` traces).

### Import result

- The loader is picked up as `XEXLoaderWVLoader`. It maps 14 sections (`.text` `0x822A0000`, 11,145,244 bytes), 393 import references and 36,820 function symbols from `.pdata`.
- Language: `PowerPC:BE:64:A2ALT-32addr`. Stock Ghidra has no VMX128. 2,722 places decode as bad instructions (about 1 per 420 instructions), and the decompiler truncates a function at the first one, for example the environment map pass `sub_822E4118` and the view pre-render `sub_822EDDC8`. The 841 vector mnemonics it shows are mostly mis-decoded VMX128. For those functions the generated C++ of the recompiler is the reference.
- Function starts: 49,968 Ghidra functions. The recompiler emits 49,616 functions (`fm4_rex_funcs.h`; the numbered headers only declare the 28,753 that other translation units call). 49,590 of them are Ghidra function entries at the same address and 26 lie inside another function, so the names in `work/fm4-rex/src` (for example `sub_82442600` and `sub_82438A38` in `fm4-vblank-rate.cpp`, `sub_82935F80` and `sub_82882C78` in `fm4-streaming-fixes.cpp`) map one to one. Without the pre-analysis scripts, about 7,000 of them were missing. The [function catalog](function-catalog.md) names them.
- RTTI is present: 2,637 type names (`out\rtti-names.txt`), so vtables can be named, but there are no function symbols. Identical code folding merges small methods of different classes (for example `lbz r3,356(r3); blr`), so a one-line getter does not identify its class.

## 2. Render path

### Threads and frame

| Function | Role |
|---|---|
| `sub_822F7A00` | Main game loop (already named in `fm4-dev-features.cpp`). Per frame it also calls `sub_822B99C8`, the digital gauge texture updates. |
| `sub_822A90B0` | Render thread entry: `sub_82457E60` (render thread init: copies `CForzaCommandLineParameters` flags from `sub_8237D468`), then `while (flag) sub_822DB318(this)`. |
| `sub_822DB318` | One render thread frame (34 callees). |
| `sub_82454FD0`, `sub_82384E30` | End of frame: tiling scenario (`sub_82391638`, `sub_8244EA68`) and present. |
| `sub_822AF998`, `sub_822AF9D8` | Swap wrappers. |
| `sub_8237FA28` | D3D `Swap`: `VdGetSystemCommandBuffer`, `VdSwap`. |
| `sub_826E4E28` | Ring buffer init (`VdInitializeRingBuffer`), reached from `sub_826F48B8` and `sub_826F4D00`. |

`CRenderThread` (vtable `0x8200726C`, singleton `0x82DFDDC8` from `sub_822D8490`) executes the commands that the game thread queues through `CRenderThreadLink` and `CPresentationDeferred` (each `IRenderThread*` and `IPresentation*` call is a `CParams` record with a thunk). Examples: `PushDynamicRenderSettings` `sub_82442600` queues `CRenderThread` slot 57 (`sub_8243F9E0`); `PopDynamicRenderSettings` `sub_82438A38` queues slot 58 (`sub_8243FA50`); `CPresentationDeferred::RenderMirror` `sub_82437618` queues the thunk `0x82437600`, which runs `CPresentation::RenderMirror` `sub_828D19E0` (vtable `0x8214D91C`, slot 59).

### Scene, views and passes

| Class (vtable) | Function | Role |
|---|---|---|
| `CPresentation` (`0x8214D91C`) | slot 5 `sub_828D87F8` | Frame setup; by their arguments, its callees `sub_828D15B0`, `sub_828D2050`, `sub_828D4398` and `sub_828D2360` set up render targets. |
| | slot 48 `sub_828D6DA8` | Second caller of the environment map pass `sub_82370890`. |
| | slot 59 `sub_828D19E0` | Rear-view mirror render (render thread). |
| `CPresentationView` (`0x82151C64`) | slot 9 `sub_822EDDC8` | `PreRender` (named by its deferred call record), once per view and frame: the setup that reads SkipShadowMapUnlessCockpit `sub_823548C8`, environment map `sub_82370890` → `sub_822E4118`, crowd `sub_82396C10` (through `sub_82371ED0`), the half-rate mirror gate and the mirror call. |
| | | Car draw `sub_823134A8` (reads CarDrawDriver, CarMinWheelLOD, CarDamage) → `sub_828F60E0` → `sub_828F5F20` → `VIZ_QUERY` writers `sub_826E8C80` and `sub_826E8D60`. |
| `CTrackPresentation` (`0x82152D94`), `CCrowdPresentation` (`0x8214BECC`), `CParticlePresentation` (`0x8214BF7C`), `CMirrorRenderer` (`0x821594C4`) | | Per-subsystem presentations (vtables only; not traced further). |

The PM4 writers of the statically linked D3D layer (`0x822A7E08` to `0x8239DF28` and `0x826E1550` to `0x826F7FB0`) are listed in `fm4-d3d-trace.cpp`. `sub_826E6D70`, `sub_826E75C0`, `sub_826E7718` and `sub_826EEBC0` build `DRAW_INDX_2` (auto-indexed draws: clears and screen passes). The writer of the indexed `DRAW_INDX` that carries the scene draws was not found: no function builds its header as an immediate (no `0xC0nn2200` constant in code or data), so it is assembled at run time, probably inline in the game's draw loops. `FM4_D3D_TRACE` can find it in a run.

### Render scenario settings

`CDynamicRenderSettings` (vtable `0x8214EB48`, constructor `sub_828E60F8`) holds 36 controls. Each control is an object of 8 bytes with its value at +4. A global manager (`0x82DC3750`) keeps a stack of settings per thread (push `sub_828E7340`, pop `sub_828E3CB0`). Every setting has a getter `Get(manager, thread)` that returns the topmost value whose bit is set in the mask at +0x60, using a table of control offsets at `0x82D59FF0 + 4 * ID` (the value is at the control offset + 4). Getters and callers (`out\setting-getters.txt`):

| ID | Setting | Value offset | Getter | Callers |
|---|---|---|---|---|
| 0 | FrameRate | +48 | none (table read in `sub_828DAFA0`, `sub_828DF158`, `sub_828DF1D0`) | |
| 1 | TilingScenario | +56 | `sub_82391638` | `sub_822D78F0`, `sub_82384E30`, `sub_82457E60` |
| 2 | UITilingScenario | +64 | `sub_8244EA68` | `sub_82384E30`, `sub_82450DE0`, `sub_82450E98`, `sub_82457E60` |
| 3 | MotionBlur | +72 | none: no code reads it | |
| 4 | DepthOfField | +80 | none: no code reads it | |
| 5 | CarDrawDriver | +88 | `sub_8234F860` | `sub_823134A8` |
| 6 | CarShader | string | `sub_828F67E8` | `sub_8290CAD8`, `sub_8290D348` |
| 7 to 10 | CarLODThresholds, CarFocusLODMinMax, CarNonFocusLODMinMax, CarLODTransDist | | `sub_82910BB0`, `sub_822C1DE0`, `sub_823429B8`, `sub_822FF2E8` | `sub_823416D0`, `sub_823352F8`, `sub_82302B98` |
| 11, 12 | CarKeepLOD0, CarForceNoLOD0 | +184, +192 | `sub_823C1BB8`, `sub_823C1C78` | `sub_823D3D48`, `sub_82407E20`, `sub_826D8528` |
| 13 | CarMinWheelLOD | +200 | `sub_82948218` | `sub_823134A8` |
| 14, 15 | CarDamage, CarDamageTextures | +208, +216 | `sub_822FA850`, `sub_823FFDC8` | car presentation and damage code |
| 16 | CarDigitalGaugeFrequency | +224 | `sub_826CE300` | `sub_822B99C8` |
| 17, 18, 19 | CollidablesRender, CollidablesInMirror, CollidableShadows | +232, +240, +248 | `sub_82910D70`, `sub_822EE828`, `sub_822C54B8` | `sub_822EE8E8`, `sub_82314D30`, `sub_82366798`, `sub_823680D8` |
| 20, 21 | CrowdLoad, CrowdLoadModels | +256, +264 | `sub_82561D28`, `sub_829482D8` | `sub_82396C10`, `sub_825884D0`, `sub_826DAF28`, `sub_826DDA88`, `sub_82950E98` |
| 22 to 27 | CrowdDraw, CrowdDrawModels, CrowdDrawSprites, CrowdDrawShadows, CrowdSpriteDistScale, CrowdLODCountScales | +272 to +304 | `sub_82910E30`, `sub_82948398`, `sub_82948458`, `sub_82948518`, `sub_829485D8`, `sub_829486A0` | `sub_82396C10`, `sub_82948920`, `sub_8294F5D8`, `sub_8294A4C8`, `sub_82948BC8` |
| 28 | ParticleRateScale | +332 | `sub_822B14F0` | `sub_822B7930` |
| 29 | EnvMapFrequencyScale | +340 | `sub_822BA1C0` | `sub_822B6498`, `sub_822BA148`, `sub_82370890` |
| 30 | MaxCarsInMirror | +348 | `sub_82910CB0` | `sub_823416D0`, `sub_82360360` |
| 31 | HalfRateMirror | +356 | `sub_826CE3C0` | `sub_822B99C8`, `sub_822EDDC8` |
| 32 | HalfRateBloom | +364 | `sub_8244EB28` | `sub_8237CC08` |
| 33 | SoftParticles | +372 | `sub_82CEA0F8` | `sub_82367820` |
| 34 | SkipShadowMapUnlessCockpit | +380 | `sub_828D5C58` | `sub_82314D30`, `sub_823548C8` |
| 35 | ExposureShiftScale | +388 | `sub_8244E908` | `sub_822A1230` |

Defaults that the controls set when a scenario leaves them out (slot 4 of each control vtable): MaxCarsInMirror 7, HalfRateMirror 0, CarMinWheelLOD 0, EnvMapFrequencyScale 1.0, and 1 for most switches (MotionBlur, DepthOfField, CarDrawDriver, CollidablesRender, CollidablesInMirror, CrowdLoad, CrowdDraw). `MotionBlur` and `DepthOfField` parse but nothing reads them: motion blur and depth of field are switched by `IRenderThreadEnableMotionBlur` and `IRenderThreadEnableDepthOfField` instead. `CrowdLoadModels`, `CarForceNoLOD0`, `CollidablesRender`, `CarDigitalGaugeFrequency` and `ExposureShiftScale` are read, but no scenario file sets them, so they are free knobs for `FM4_RACE_SETTINGS`.

Two consumers decide per frame:

- **Environment map faces** (`sub_822B6498`): faces updated per frame = `EnvMapFrequencyScale` × the track's face count (+0x160 of the track object, 6 by default), clamped to 1 to 6. A scale of exactly 0 (`sub_822BA148`, constant at `0x82000F40`) freezes the map. The update pass is `sub_822E4118`, reached from the view pre-render through `sub_82370890`. The update count is per frame, not per unit of time, so at 240 fps the cube is refreshed four times as often as at 60.
- **Half-rate mirror** (`sub_822EDDC8`, around `0x822EE578` to `0x822EE63C`): when HalfRateMirror is set, a flag at +463 of the view ("mirror rendered last frame") alternates, and the mirror is rendered through the presentation's slot 59 only when the flag was clear. The same flag style (+0x26 of each car's gauge record) halves the digital gauge texture updates in `sub_822B99C8`, which additionally staggers the other cars' gauges by CarDigitalGaugeFrequency.

## 3. Where the draws go

From the existing frame graph `I:\FM4-build-usa\e2e\mvA` (photo scenario: 12-car Quick Race on the Bernese Alps, `-Quality high`, 60 Hz, `REX_FRAME_GRAPH_EVERY=5`, 18 October 2026), race frames 6,000 to 8,999 with at least 1,500 draws (522 samples, mean 2,517 draws per frame; the 240 Hz gates `sflp` and `sflc` show 2,400 to 3,050):

| Pass (identified by pitch, MSAA and EDRAM layout) | Draws per frame, mean | Share |
|---|---|---|
| Main scene, three predicated tiles (4x MSAA, offsets 0, 256, 512) | 1,286 | 51% |
| Rear-view mirror (pitch 400, 2x MSAA, depth at EDRAM 80, resolved to 384x128): 1,108 draws when rendered, every second frame | 660 (about 554 at exactly half rate) | 22 to 26% |
| Post chain, HUD and overlays (1x) | 212 | 8% |
| Passes of 24 draws without a target (likely the cars' visibility queries: 12 cars, 2 draws each), 4 to 5 per frame | 124 | 5% |
| Shadow map 512x512 depth (pitch 560) | 114 | 5% |
| Environment map face and its blur chain (pitch 320, 256x256 down to 32x32) | 67 | 3% |
| Shadow map 512x512, 4x MSAA, 16_16 target (pitch 520) | 54 | 2% |

Every car in a view is a block of a 1 or 2 draw stencil pass and a 26 to 55 draw color pass. These blocks repeat in each of the three tiles and in the mirror, also in tiles where the car has no pixels (`0.000 ms` GPU time, draws still counted). The 6-face cube at 4x MSAA (pitch 280, about 245 draws) appears in photo mode only, where it is updated every frame.

## 4. Patch candidates

Ranked by draws per frame removed in a 12-car race at 240 Hz. The fps estimates use the 8 to 10% per 10% of draws above and need a gate to confirm.

| # | Candidate | Where | Draws per frame affected | Estimate | Risk |
|---|---|---|---|---|---|
| 1 | Render the mirror at most at 60 Hz, as the title does on the console | Hook `sub_82437618` (`CPresentationDeferred::RenderMirror`, game thread) and return without queueing on frames where fewer than 1/60 s have passed since the last mirror; the mirror texture keeps its last image, as on the frames that HalfRateMirror already skips | 12 and 16 cars: about 554 to 277 (−277); 8 cars and `ultra` (mirror every frame): 1,108 to 277 (−831) | +9% (12 cars) to +25% (8 cars) | Low. The title already skips every second mirror frame in 12 and 16 car races. Check that `sub_822EDDC8` keeps its +463 flag consistent (it sets the flag after the call whether or not the call queued work). |
| 1a | Scenario-only part of 1: `HalfRateMirror=1` for 8-car races, and a lower `MaxCarsInMirror` (7 by default; each car is about 40 draws) | `FM4_RACE_SETTINGS` | 8 cars: −554; each mirror car fewer: −40 per mirror render | +4 to +20% | None for code; the mirror shows fewer cars or updates at 120 Hz. |
| 2 | Fewer tiles (existing switch, listed for scale) | `TilingScenario` 1 (`-Quality xbox`) or 2 (`-SingleTile`) | The main scene is 51% of the frame in three tiles. Measured: 2,418 to 1,859 draws per frame for `high` to single tile with TAA at 2x ([TAA, M1](taa.md)) | +15 to +25% | Visual: less MSAA (owner's choice, already documented). |
| 3 | Update the visibility queries at most at 60 Hz | The car draw path `sub_823134A8` → `sub_828F60E0` → `sub_828F5F20` (writers `sub_826E8C80`, `sub_826E8D60`); hook `sub_828F5F20` and keep the previous result on skipped frames | 124 to about 31 (−93), if the 24-draw passes are these queries (to verify with `REX_IB_TRACE` or `FM4_D3D_TRACE`) | +3% | Medium: occlusion results three frames old; at 240 fps that is the age of one console frame. |
| 4 | Update the shadow maps every second frame above 120 fps | `sub_823548C8`, the per-view setup that reads SkipShadowMapUnlessCockpit (which of its callees `sub_822BA478`, `sub_823795E8`, `sub_8238BDC0` renders the shadow map is not traced yet) | 168 to 84 (−84) | +3% | Medium: car shadows trail by one frame (4.2 ms at 240 Hz). |
| 5 | Update the environment map at most at 60 Hz | Hook `sub_82370890` (the environment map pass, called from the view pre-render and from `CPresentation` slot 48) and skip it on frames in between; the cube keeps its faces, as with `EnvMapFrequencyScale` 0 | 67 to about 17 (−50) | +2% | Low: reflections refresh at the console rate. `EnvMapFrequencyScale` alone cannot do this, because the face count is clamped to at least 1 per frame. |
| 6 | Crowds in 8 and 12 car races (the scenarios leave `CrowdLoad` at its default 1; 1 and 16 car races set 0) | `CrowdDrawModels=0`, `CrowdLODCountScales`, `CrowdSpriteDistScale` through `FM4_RACE_SETTINGS`; consumers `sub_8294F5D8`, `sub_82948BC8` | Not measured: the crowd draws are inside the main tiles | unknown | Visual: crowd density. |
| 7 | Runtime, not a game patch: skip draws on the CP whose bins miss the current tile | The title predicates each draw per tile (`SET_BIN_MASK` and `SET_BIN_SELECT` writers `sub_82310530`, `sub_82310818`, `sub_826E8E30`, `sub_826E8FD8`); the car blocks in tiles without pixels are executed and counted | Part of the 51% main scene | unknown | Must match the predication semantics exactly; check first whether the runtime already does this (`bin_selects` in `perf.jsonl`). |

Candidates 1, 3, 4 and 5 share one mechanism: a function-level hook (as in `fm4-vblank-rate.cpp`) that lets the call through once per 1/60 s of guest time and returns early otherwise. All four places are per frame, not per unit of time, so they cost four times as much at 240 Hz as the title was built for. Together they remove about 500 draws per frame in a 12-car race (about 20%) and about 1,050 in an 8-car race.

## 5. Next steps

1. Implement candidate 1 behind a variable (for example `FM4_MIRROR_RATE=60`), then run the `perf` gate at 240 Hz against the same binary without it. Read the mirror's draws per frame from `REX_FRAME_GRAPH` (pitch 400, EDRAM depth 80) and look at the mirror in a capture.
2. Confirm that the 24-draw passes are visibility queries (`FM4_D3D_TRACE` callers of `sub_826E8C80` and `sub_826E8D60` per frame) before candidate 3.
3. Measure the crowd's share with a `-Quality high` run that sets `CrowdDraw=0` (candidate 6), only as a measurement.
4. Find the `DRAW_INDX` writer with `FM4_D3D_TRACE` to complete the device layer map.
5. A VMX128 language module for Ghidra would remove the truncated functions; until then, read those functions in the recompiler's generated C++.
