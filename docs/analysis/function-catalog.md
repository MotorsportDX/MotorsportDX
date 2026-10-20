# Function catalog of the title's executables

Status: static analysis, 20 October 2026. The catalog, the Ghidra projects and every decompiled function stay outside the repository under `I:\FM4-re`. This note describes how the catalog is built, what it covers, and how to use it. Setup and the render path are in [Ghidra render path](ghidra-render-path.md).

## Scope

"Recognized" is measured in four levels for every function the recompiler emits:

| Level | Meaning |
|---|---|
| Module | The library or code base the function belongs to (Forza game code, FMOD, SQLite, XDK D3D, and so on). |
| Subsystem | For game code: UI, rendering, network, audio, and so on. |
| Class | The C++ class whose method it is, where one is known. |
| Name | A real method name, a class-scoped name (`Class::vf12`, `Class::fn_<address>`), a role (`get_u32_8`, `thunk_to_…`, `dynamic_initializer_…`) or a string hint (`str_<text>`), with a confidence from 0 to 1 and the evidence. |

Every function has a module, and every game function also has a subsystem. Names are only as good as the evidence: the executables carry no symbols. A real name comes from RTTI, the title's deferred calls, a string, or a human check. Function bodies without any of these keep a class-scoped or generic name.

## Universe

| Executable | Functions (recompiler) | Ghidra project |
|---|---|---|
| `default.xex` | 49,616 (`fm4_rex_funcs.h`; 49,590 are Ghidra function entries, 26 lie inside another function) | `fm4v2:/default.xex` |
| `XMediaFacade_default.xex` | 2,917 | `fm4v2:/XMediaFacade_default.xex` |
| `SpeechFacade_default.xex` | 6,357 | `fm4v2:/SpeechFacade_default.xex` |

The numbered headers `fm4_rex_funcs.N.h` only declare the functions that other translation units call (28,753 for `default.xex`); the complete list is in `fm4_rex_funcs.h`.

## Pipeline

All scripts are in `I:\FM4-re\scripts`; outputs in `I:\FM4-re\out`.

1. **Import** (`import.cmd`, `import-facades.cmd`): XEXLoaderWV, auto-analysis with "Non-Returning Functions - Discovered" off (`DisableNoReturn.java`), and a function at every recompiler start (`SeedFunctions.java`).
2. **Evidence export**: `ExportDB.java` (per function: callees, data references, instruction count; all strings; code pointers), `DumpMemory.java` (flat image for the facades; `default.xex` uses `<BuildRoot>\loaded-image.bin`), `rtti.py` (type descriptors, complete object locators, class hierarchy with base offsets, vtables and slots), `recomp_asm.py` (the PPC instructions of every recompiled function, VMX128 included), `pair_tables.py` (data tables of a string and a function pointer).
3. **Catalog** (`catalog.py`, with `catalog_virtuals.py`, `catalog_names.py`, `catalog_subsystems.py`; `FM4RE_MODULE=xmedia|speech` for the facades). Layers, in order:
   - manual names (`I:\FM4-re\names-manual.tsv`: hooks and comments of `work/fm4-rex/src`, the render path analysis, the most called utilities);
   - vtable slots: the class that introduced a slot (`Class::vfN`); slots shared by unrelated classes (identical code folding) are marked as shared;
   - constructors and destructors: functions that store a vtable;
   - deferred calls: every `CXxxDeferred::CParamsN<Interface><Method>` record names the queuing wrapper, its executor thunk (`lwz rX,K(rY); mtctr; bctr` gives slot K/4) and that slot in every class implementing the interface;
   - source paths, library string fingerprints (D3D, libpng, zlib, CRT, NUI, Xls web services, ASF/WMA, FMOD, Vorbis, SQLite, DirectPlay, XAM), kernel import families (XAudio/XMA, LDI, NetDll, XeCrypt);
   - modules in link order: runs between two anchors of one module take that module; other runs are split once where call-graph votes (callees count twice) agree best, after a provisional propagation through the call graph; game runs without one game anchor go to the neighbouring library; class anchors need at least 6 instructions (tiny functions are folded across unrelated code);
   - deleting destructors (`clrlwi. rX,rFLAGS,31` then `operator delete`), base-class slot names propagated to overrides (`AddRef`, `Release`, deferred interface methods), the UI control factory table at `0x82023C30` (123 `ForzaUI::CreateControl_<TYPE>`);
   - trivial functions by their instructions (getters, setters, constants, thunks), names from strings (`Class::Method` text, thread names), string hints, static initializers (`0x82D00000` onwards);
   - class membership: a function between two methods of one class (link order) or called only by one class;
   - game subsystems from class names, then link order and the call graph.
4. **Apply** (`ApplyCatalog.java` through `run-apply.cmd`, `PROG=<program>`): functions with a confidence of at least 0.5 get the catalog name, with the class as a Ghidra namespace; every function gets a plate comment with module, subsystem, class, catalog name, confidence and evidence. Names of an earlier run that no longer hold go back to the default.

Output: `I:\FM4-re\out\catalog\functions.tsv`, `functions-xmedia.tsv`, `functions-speech.tsv` (address, module, subsystem, class, name, confidence, evidence).

## Coverage

### default.xex

| Category | Functions | Share |
|---|---|---|
| Real name | 2,977 | 6.0% |
| Class-scoped name | 15,084 | 30.4% |
| Role (trivial, initializer, shared virtual) | 7,664 | 15.4% |
| String hint only | 2,504 | 5.0% |
| Module (and subsystem) only | 21,387 | 43.1% |

Every function has a module: 12,508 from direct evidence (source path, library strings, imports, RTTI class, manual), 37,108 from link order and the call graph. Game code is 28,432 functions; 17,421 of them (61%) have a class.

| Module | Functions | Real | Class-scoped | Role | Hint | Module only |
|---|---|---|---|---|---|---|
| Forza game | 28,432 | 2,712 | 14,105 | 1,769 | 1,186 | 8,660 |
| Static initializers | 5,278 | 0 | 4 | 5,274 | 0 | 0 |
| Xls web services | 2,408 | 56 | 107 | 119 | 256 | 1,870 |
| XDK media (ASF/WMA) | 2,334 | 1 | 2 | 32 | 18 | 2,281 |
| FMOD | 2,149 | 0 | 13 | 64 | 356 | 1,716 |
| XDK NUI (Kinect) | 1,186 | 0 | 1 | 41 | 23 | 1,121 |
| UserInterface4 | 966 | 121 | 408 | 59 | 28 | 350 |
| XDK audio (XAudio2/XMA) | 856 | 0 | 0 | 19 | 0 | 837 |
| SQLite | 849 | 0 | 9 | 14 | 153 | 673 |
| DirectPlay 8 (dpnet) | 843 | 0 | 1 | 18 | 20 | 804 |
| Granny | 794 | 2 | 0 | 101 | 133 | 558 |
| XDK XAM/XAPI | 735 | 7 | 0 | 53 | 21 | 654 |
| libpng (with libjpeg) | 593 | 0 | 1 | 18 | 62 | 512 |
| STL | 516 | 27 | 376 | 22 | 2 | 89 |
| XDK D3D | 346 | 38 | 0 | 10 | 16 | 282 |
| MSVC CRT | 339 | 8 | 0 | 10 | 9 | 312 |
| XDK xgraphics (shader compiler) | 283 | 0 | 0 | 23 | 104 | 156 |
| R1Font | 191 | 3 | 53 | 8 | 31 | 96 |
| Anark | 167 | 1 | 3 | 9 | 57 | 97 |
| zlib | 143 | 0 | 0 | 0 | 24 | 119 |
| XDK XeCrypt | 100 | 0 | 0 | 1 | 0 | 99 |
| Other (unidentified XDK, XNet, RAD, xcompress, Vorbis) | 108 | 1 | 1 | 0 | 5 | 101 |

Game subsystems (functions): UI 8,678; rendering 7,221; other 2,693; network and online 2,425; IO and resources 1,525; audio 1,248; cars and simulation 1,229; core and scripting 689; camera 468; AI 399; database 335; gameplay and progression 324; livery and paint 267; photo and video 267; memory 263; replay and ghosts 238; input and Kinect 98; track 65.

### Facades

| Executable | Modules | Named or labelled |
|---|---|---|
| `XMediaFacade_default.xex` | XDK WMV9/VC-1 video decoder (`core\private\del\codecs\wmv`, DXVA shaders) 2,396; MSVC CRT 425; ASF/WMA 96 | 107 (no RTTI) |
| `SpeechFacade_default.xex` | Kinect speech recognition (`NUISPEECH`, SR engine, CFG/SISR grammars, 95 RTTI classes) 5,306; XDK NUI 566; MSVC CRT 409; others 76 | 1,446 |

## Address layout of default.xex

The linker places game code first and the static libraries after it, with link-time code generation moving some game functions into library areas (for example methods of `Presentation_Unified::CPresentationType` among the FMOD functions at `0x82B8F850`).

| Range | Module |
|---|---|
| `0x822A0000`–`0x826E14D0` | Forza game |
| `0x826E1550`–`0x826F9B58` | XDK D3D (runtime, PIX, GPU hang diagnostics) |
| `0x826F9F88`–`0x82737FA0` | libpng and libjpeg, zlib |
| `0x827383D0`–`0x8275E7D0` | XDK XAM/XAPI, XNet |
| `0x8275E9A8`–`0x82772090` | MSVC CRT, STL |
| `0x827720C8`–`0x8279DB18` | Xls web services (`Xls.WebServices`, `Forza.WebServices`) |
| `0x8279DB88`–`0x82808480` | XDK XAM/XAPI, XDK NUI (Kinect) |
| `0x82809458`–`0x829AEDE0` | Forza game (with UserInterface4 around `0x8298`) |
| `0x829AEDE0`–`0x82A50FE0` | XDK media (ASF/WMA, metadata objects) |
| `0x82A51160`–`0x82AA4148` | Forza game |
| `0x82AA5F18`–`0x82AE5878` | Anark, Granny, zlib, libpng (second copy) |
| `0x82AED108`–`0x82BA2210` | FMOD (with some game code) |
| `0x82BA2838`–`0x82C1B2B8` | Vorbis area, SQLite |
| `0x82C1B3F0`–`0x82C2EF18` | XDK xgraphics (shader compiler) |
| `0x82C2EF40`–`0x82C80CF0` | XDK audio (XAudio2/XMA), XeCrypt, XAPI fibers |
| `0x82C80DB8`–`0x82C9A9F8` | Xls web services (notifications, file upload) |
| `0x82CA6C18`–`0x82CE7858` | R1Font, DirectPlay 8 |
| `0x82CE7968`–`0x82CFFDA0` | Forza game, Anark |
| `0x82D00160`–`0x82D41DA8` | Static initializers of globals |

## Checks

Names that were derived independently agree with the manual analysis of the render path: `CPresentation::RenderMirror` (`sub_828D19E0`), `CPresentationDeferred::RenderMirror` (`sub_82437618`), `CRenderThread::PushDynamicRenderSettingsInternal` (`sub_8243F9E0`) and its queue side (`sub_82442600`, the hook in `fm4-vblank-rate.cpp`). The deferred layer named `sub_822EDDC8` as `CPresentationView::PreRender`, which corrects "view render" in the render path note.

## Rendering subsystem: manual pass

Started 20 October 2026: functions read in their decompiled code and named in `names-manual.tsv` with a confidence (1.0 checked, 0.5 to 0.85 inferred from the code). 454 manual names so far, 266 of them inferred, 125 in the Rendering subsystem; the rest are the utilities, containers and the XDK D3D layer that the render path calls. Worklists: the direct-call tree of the render thread frame and the presentation methods (`worklist.py`, 645 functions, 563 without a real name before the pass), then the unnamed non-trivial vtable slots of the presentation classes (206).

What the pass established, by function:

| Area | Functions |
|---|---|
| Per-draw state flush of the D3D layer (the PM4 the CP translates for every draw) | `sub_826F6E18` `D3DDevice_FlushDirtyState` writes the dirty register groups (`0x2000`, `0x2100`, `0x2180`, `0x2200`, `0x2280`) through `sub_823114F8`, the fetch constants through `sub_8230FA40` (6 dwords each, base `0x4800`) and the vertex and pixel float constants through `sub_82310E78` (`0x4000`, `0x4400`) |
| Device state setters | `D3DDevice_SetTexture` `sub_8233A9A8`, `SetStreamSource` `sub_823445A0`, `SetRenderTarget` `sub_823563B8`, `SetDepthStencilSurface` `sub_822D9968`, `SetVertexShader` `sub_82351A00`, `SetPixelShader` `sub_82351BD0`, `SetViewport` `sub_822FEAA8`, `SetScissorRect` `sub_822FF050`, `SetShaderGPRAllocation` `sub_822BA060`, `Clear` `sub_8236D840` |
| Command buffer | `BeginCommand` `sub_823913D0`, overflow `sub_822FEF08`, kick `sub_822FED80`, ring allocation `sub_8234D738`, indirect buffer submission `sub_826E4848`, `BlockUntilIdle` `sub_826E4D20` |
| Predicated tiling | `D3DDevice_BeginTiling` `sub_82371B80`, `EndTiling` `sub_8236FA20` (one resolve per tile under a bin mask), `CTileBuffer::SetScenario` `sub_82883948`, `Begin` `sub_82882600`, `BeginTiling` `sub_823813B0`, `End` `sub_82376F38`; the tiling scenario table at `0x82D883C8` (stride `0x4D0`, tile count at +8, tile rectangles at +0x18) |
| Render targets of the presentation | mirror 384x128 `CPresentation::CreateMirrorRenderTargets` `sub_828CE598`, environment map faces `sub_828D2050` and cube targets `sub_828D1080`, shadow targets `sub_828D4398`, scene targets `sub_828D15B0` |
| Track | `CTrackPresentation::ComputeVisibility` `sub_82925860` (clears the visible lists, three culling passes, then adds the visible groups), `AddVisibleGroup` `sub_82921F20`, `AddVisibleObject` `sub_82921E30`, `CheckTrackReady` `sub_822F9070` |
| Shadows | `ShadowMapGenerator_Unified::Configure` `sub_82945230` (singleton at `0x82E01504`, three cascades released by `sub_828832D0`) |
| Runtime support | memory (`Memory::CHeapManager`, heap ids per thread through `CScopedHeapOverride`), timers (`CTimer`), FXL effects (`FXLEffect::GetParameterHandle`, `SetFloat`, `CommitParameters`), screen quads (`CRenderEngine::DrawScreenQuad` `sub_8233A3A0`) |

Most presentation methods that render (for example the car and track render slots) use VMX128 and are truncated in Ghidra; their names wait for a reading of the recompiler's C++.

## Limits

- Real names exist only where the binary carries them: RTTI classes, deferred call records, strings, data tables. 43% of `default.xex` (8,660 game functions and most library internals) has a module and, for game code, a subsystem, but no name.
- Library internals (SQLite, zlib, libpng, FMOD, Granny) are recognized at module level. Their function names could be matched against the public sources by hand; that was not done.
- Link order is approximate: link-time code generation interleaves game and library code, so module boundaries between two libraries can be off by a few functions. The evidence column says which rule decided.
- Ghidra cannot decode VMX128; for the 2,722 truncated places, use the recompiler's generated C++.

## Next steps

1. Name the remaining game functions subsystem by subsystem, starting with rendering (7,221) for the CP draw work, by reading their decompiled code; add the verified names to `names-manual.tsv` and rerun the catalog.
2. Match library internals to their public sources where it matters (zlib, libpng, SQLite).
3. Use the catalog names in hook comments in `work/fm4-rex/src`.
