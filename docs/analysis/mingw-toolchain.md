# The llvm-mingw toolchain: route (c) (31 October 2026)

Research for route (c) of the [builder installer](builder-installer.md#47-the-msvc-libraries-and-the-windows-sdk):
can Motorsport DX build with **llvm-mingw** (clang targeting mingw-w64, with libc++, mingw-w64's C
runtime and headers, and lld) instead of Microsoft's C and C++ libraries and the Windows SDK? The
owner chose to research this route first on 31 October 2026 ("Riset llvm-mingw dulu"), because the
Visual Studio Build Tools, Community and Windows SDK terms do not clearly cover a player who builds
the game only to play it.

Sections 1 to 9 are the design research, made before anything was downloaded, built or run. The
counts come from `grep` over the repository at `4c9ca04` and over the development BuildRoot's
generated code and vendored third-party sources (`I:\FM4-build-usa`); the mingw-w64 and llvm-mingw
facts come from their source trees and the GitHub API, read on the web. Section 10 records the
prototype of section 8, built on 31 October 2026 with the owner's approval of its one download, and
corrects the verdicts of section 1.1 that the build proved wrong. This note is not legal advice.

**Summary.** The prototype passed every kill criterion of section 8 in about 1 h. With llvm-mingw
20250709 and no Microsoft library, header or SDK, the whole player build (`all --tests player`)
passes: the runtime, `rexglue.exe`, the game and its two modules, the launcher, the instruction
tests (295,650 checks, the same output as the MSVC build), the ten CPU tests and the launcher tests
(558 plans). `rexglue.exe` built with mingw generates the same code byte for byte. A D3D12 probe on
WARP gets the same struct returns as an MSVC-library build of it; without mingw-w64's aggregate
return switch it crashes. The port needed 16 small shims, 14 of them guarded for mingw, and no
rewrite (section 10.2); the build with Microsoft's libraries keeps every instruction (section 10.6).
On the GPU (section 10.7), the first mingw runs crashed at every race load: the Universal CRT's
setjmp needs a 16-byte aligned buffer, and mingw-w64's `jmp_buf` loses that alignment inside a
`std::pair`. A guarded wrapper fixed it, and the smoke A/B then passed with both builds at about the
same fps. The frozen-frame parity gate still fails on draws, because the mingw build freezes
another moment of the same race (cause not found). Not verified: performance, SDL's GameInput
driver (left out under mingw). **Recommendation:** find why the frozen moment differs (section
10.8; the switch is blocked until then), then the steps of section 5 (section 10.9).

## 1. MSVC-specific code

Areas searched: the runtime (`work/runtime/src`, `work/runtime/include`; 864 source files with the
title and launcher, 297,563 lines), the title code and its tests (`work/fm4-rex`), the launcher
(`work/fm4-launcher`) and the generated code of the USA build (`I:\FM4-build-usa\project\generated`,
368 files, 194 MB in `default`).

Today's builds already use clang's GNU-style driver (`clang++`, `-fuse-ld=lld-link`), not
`clang-cl`: the runtime's CMake refuses any compiler but Clang
([work/runtime/CMakeLists.txt:89](../../work/runtime/CMakeLists.txt)), and CMake's `MSVC` variable is
false. The code therefore has no `cl`-only syntax, and `_MSC_VER` appears only in two fallback
branches that clang never takes ([platform.h:67](../../work/runtime/include/rex/platform.h),
[o1heap_config.h:18](../../work/runtime/include/rex/o1heap_config.h)). Under mingw `_WIN32` is
defined, so the same `REX_PLATFORM_WIN32` branches compile (112 lines in 46 files), against
mingw-w64's headers instead of the Windows SDK's.

### 1.1 The sites, classified

"As-is" means mingw-w64's headers or clang provide the same name, read in mingw-w64's source at the
commit llvm-mingw 20250709 uses (section 6); none of it was compiled for this table. The prototype's
build corrected five verdicts: `std::chrono::clock_cast` is missing in libc++, `#pragma comment(lib)`
works with lld, the export setting needs more than `--export-all-symbols`, DXC's header needs GUID
declarations rather than mingw-w64's own header, and `WIDL_EXPLICIT_AGGREGATE_RETURNS` is set by
mingw-w64 itself (section 10.2).

| Class | Sites | Where | Verdict |
| --- | --- | --- | --- |
| `<intrin.h>` intrinsics: `_Interlocked*` | 9 | [atomic_win.cpp](../../work/runtime/src/core/atomic_win.cpp) | as-is: mingw-w64's `intrin.h` and `psdk_inc/intrin-impl.h` define them |
| `_BitScanForward/Reverse(64)` | 10 (+1 in a branch clang skips) | [math_msvc.cpp](../../work/runtime/src/core/math_msvc.cpp) | as-is (and `math_gcc.cpp` exists beside it) |
| `__rdtsc` | 27 lines in 7 files | the perf trace, the command processor, the D3D12 back end | as-is |
| `__debugbreak`, `_byteswap_ulong` | 1, 2 | [dbg_win.cpp:24](../../work/runtime/src/core/dbg_win.cpp); two title diagnostics | as-is |
| `__declspec(dllexport)` | 3 + 2 generated | [graphics_system.cpp:92](../../work/runtime/src/graphics/graphics_system.cpp) (`NvOptimusEnablement`), [gpu_plugin.h:24](../../work/runtime/include/rex/system/gpu_plugin.h), each module's `fm4_rex_register.cpp` | as-is: clang accepts `__declspec` on every Windows target |
| `__stdcall` | 6 | the `ID3DInclude` handlers of [shader_override.cpp:267](../../work/runtime/src/graphics/d3d12/shader_override.cpp) and two tests | as-is |
| `#pragma warning` | 14 | `xma_context.cpp`, `xma_decoder.cpp`, `xam_locale.cpp` | as-is (an unknown-pragma warning at most) |
| CRT functions | 33 calls of 22 functions (`_stricmp`, `_fileno`, `localtime_s`, `_wfopen_s`, `_fseeki64`, `_chsize_s`, `_mkgmtime`, `_aligned_malloc` and others) | spread | as-is: all are Universal CRT functions, and llvm-mingw links the Universal CRT; each declaration not checked one by one |
| Win32 exception and unwind API: `AddVectoredExceptionHandler` (3), `SetUnhandledExceptionFilter` (1), `RtlLookupFunctionEntry`/`RtlVirtualUnwind` (3 files), `MiniDumpWriteDump` (1) | 8 | [exception_handler_win.cpp:106](../../work/runtime/src/core/exception_handler_win.cpp), [fm4-crash-diagnostics.cpp:35](../../work/fm4-rex/src/fm4-crash-diagnostics.cpp), [fm4-write-watch.cpp:88](../../work/fm4-rex/src/fm4-write-watch.cpp), the thread samplers | as-is: Windows' API, independent of the compiler; clang emits `.pdata` and `.xdata` unwind data for x86-64 mingw too |
| Fibers (`CreateFiber`, `SwitchToFiber`, `ConvertThreadToFiber`) | 27 | [fiber_win32.cpp](../../work/runtime/src/core/fiber_win32.cpp), `fm4-fiber-hooks.cpp` | as-is: Windows' API |
| WRL `ComPtr` | 167 lines in 24 files; `Get`, `Reset`, `ReleaseAndGetAddressOf`, `Detach`, `Attach` | the D3D12 code | as-is: mingw-w64 has `wrl/client.h` with `ComPtr`, `ComPtrRef` and these members |
| `IID_PPV_ARGS` / `__uuidof` | 145 / 1 | the D3D12, DXGI, D3D11 and WIC code | as-is for the interfaces of mingw-w64's headers: `IID_PPV_ARGS` expands to `__mingw_uuidof` through `__CRT_UUID_DECL`, and `wrl/client.h` has the `ComPtrRef` overload of `IID_PPV_ARGS_Helper` (124 calls pass `&com_ptr`) |
| `thread_local` | 72 | spread | as-is: llvm-mingw uses Windows' native TLS |
| `__int128` (emitted for `mulhd`) | 1 builder | [arithmetic.cpp:194](../../work/runtime/src/codegen/builders/arithmetic.cpp) | as-is: clang supports it on x86-64 mingw |
| C++20 and C++23 library: `std::filesystem` (579 uses), `std::expected`, `std::jthread`, `std::stop_token`, `std::format` (10), `std::from_chars` (integers; floats go through the runtime's own `portable_float_from_chars`), `std::chrono::clock_cast`, `std::byteswap`, one `std::regex` | spread | as-is with libc++ 20, except `clock_cast` and `clock_time_conversion`, which libc++ lacks (38 files failed on them): the runtime's existing Apple fallback in [chrono.h](../../work/runtime/include/rex/chrono/chrono.h) now covers any libc++, and the launcher uses `file_clock::to_sys` (prototype, section 10.2) |
| SEH `__try`/`__except` | 2 | [threading_win.cpp:75](../../work/runtime/src/core/threading_win.cpp) (the debugger's thread-name exception, only when a debugger is attached; `SetThreadDescription` names threads anyway); [exceptions.h:107](../../work/runtime/include/rex/platform/exceptions.h) (`SEH_TRY` macros) | **small shim**: clang rejects `__try` on a mingw target. The first site is left out under mingw (prototype, section 10.2); the macros are emitted only for guest functions with SEH scopes ([function_graph.cpp:679](../../work/runtime/src/codegen/function_graph.cpp)), and the FM4 generated code has none (0 `SEH_TRY` in 368 files) |
| `#pragma comment(lib, "winmm.lib")` | 2 | [cp_sampler.h:18](../../work/runtime/src/graphics/util/cp_sampler.h), [thread_sampler.cpp:14](../../work/runtime/src/graphics/util/thread_sampler.cpp) | as-is: lld in mingw mode honours the pragma (`rexgpu-xenos.dll` imports `WINMM.dll`; prototype) |
| Linker options in MSVC form | 4 | `LINKER:/MAP:` ([fm4-rex CMakeLists.txt:66](../../work/fm4-rex/CMakeLists.txt)); `/DELAYLOAD` with FSR only ([rexglue CMakeLists.txt:33](../../work/runtime/src/rexglue/CMakeLists.txt)); `-Xlinker /DEBUG` and `/opt:lldltojobs` in [pipeline.py](../../outputs/FM4-build/pipeline.py) | **small shim**: lld's MinGW driver takes `--pdb=`, `--thinlto-jobs=`, and `-Xlink=<option>` for lld-link options (the last not checked) |
| `wWinMain` | 1 | [windowed_app_main_sdl.cpp:107](../../work/runtime/src/ui/windowed_app_main_sdl.cpp) | **small shim**: `-municode` on the game's link |
| `WINDOWS_EXPORT_ALL_SYMBOLS` | 1 | [system/CMakeLists.txt:61](../../work/runtime/src/system/CMakeLists.txt) (`rexruntime.dll`, 10,647 exports today) | **small shim**: CMake implements it only for MSVC-style linkers; mingw needs `-Wl,--export-all-symbols`, and lld's option leaves out every name that begins with `__imp_`, which all 2,866 guest import thunks have: they get a `dllexport` declaration under mingw in [hook.h](../../work/runtime/include/rex/hook.h) (prototype, section 10.2). Data imports go through mingw's auto-import with run-time pseudo relocations |
| DXC's API header | 1 header | the vendored `thirdparty/dxc/include/dxcapi.h` (DXC commit `6b6f402`, NCSA) declares its interfaces with `__declspec(uuid)` | **small shim**: the vendored header compiles, but mingw-w64's `__uuidof` does not read `__declspec(uuid)`; the runtime declares the GUIDs of the nine DXC and DXBC interfaces it asks for with `__CRT_UUID_DECL` under mingw (prototype, section 10.2), and keeps the vendored header, since `DxbcConverter.h` is not in mingw-w64's |
| **D3D12 methods that return a struct** | **59 calls**: `GetCPUDescriptorHandleForHeapStart` 17, `GetGPUDescriptorHandleForHeapStart` 8, `GetDesc` 29, `GetDesc1` 3, `GetResourceAllocationInfo` 2 | the D3D12 back end, for example [command_processor.cpp:2183](../../work/runtime/src/graphics/d3d12/command_processor.cpp) | **real work, to verify**: Microsoft's x64 ABI returns a struct from a member function through a hidden pointer; the GNU C++ ABI on mingw does not, the classic mingw D3D12 crash ([GCC bug 64384](https://gcc.gnu.org/pipermail/gcc-bugs/2020-June/705246.html)). mingw-w64's `d3d12.h` carries both forms behind `WIDL_EXPLICIT_AGGREGATE_RETURNS` (16 places), and its `_mingw_mac.h` defines that macro for every GCC and clang C++ build (this note said none did; the prototype found it). The WARP probe of section 10.4 shows the calls return the MSVC build's values with it and crash without it; the runtime's CMake sets it explicitly as well. `dxgi1_6.h` and `d3d11.h` have no such method in use (DXGI's `GetDesc` returns an `HRESULT`) |
| setjmp and longjmp in the generated code | 2 generated files of FM4 (`fm4_bounds.toml` sets `setjmp_address` and `longjmp_address`: "Native setjmp/longjmp must unwind the host stack") | the template [pch_h.inja:269](../../work/runtime/resources/templates/codegen/pch_h.inja) | **compiles as-is; crashed at run time**: the Universal CRT's setjmp stores `xmm6` to `xmm15` with `movdqa`, and mingw-w64's `jmp_buf` loses its 16-byte alignment inside the template's `std::pair`, so every race load crashed; a guarded `alignas(16)` wrapper in [rex/ppc.h](../../work/runtime/include/rex/ppc.h) fixes it (section 10.7). `longjmp` through the frames passed a small test; the title's error path that uses it is not exercised by a short run |

**Count by class:** about 530 lines compile as-is (49 intrinsics, 5 `__declspec`, 6 `__stdcall`,
14 pragmas, 33 CRT calls, 35 Win32 exception and fiber calls, 312 WRL and `IID_PPV_ARGS` lines,
72 `thread_local`, not counting the `std::` uses);
**10 sites and 1 header need a small shim** (2 SEH, 2 `#pragma comment`, 4 linker options,
`wWinMain`, the export setting, DXC's header); **3 areas need real work or proof** (the 59
struct-returning calls, setjmp and longjmp, and GameInput in section 3). The generated code has no
host MSVC-specific code: its only hits are guest import names such as `__imp___snprintf`, plus the
two `dllexport`s and the two `ppc_setjmp` users.

**ABI assumptions of the recompiled code.** The generated functions take `(PPCContext& ctx,
uint8_t* base)` and use only `__builtin_bswap*`, `__builtin_assume`, `__int128` and plain loads
through `base` ([ppc/function.h](../../work/runtime/include/rex/ppc/function.h),
[ppc/stack.h](../../work/runtime/include/rex/ppc/stack.h)); nothing depends on the C++ ABI. mingw
x86-64 is LLP64 like MSVC (`long` 32-bit, `wchar_t` 16-bit) and calls C functions with the same
Microsoft x64 convention. The differences are the C++ ABI (Itanium mangling and vtables, which COM
interfaces survive except for the struct returns above), `long double` (80-bit; 0 uses), and C++
exceptions (SEH-based unwinding through libunwind). Every module is built by the same toolchain, so
no C++ object crosses between two ABIs.

## 2. What mingw-w64 provides

Read in mingw-w64's tree at commit `a0e69f7bc83a29b40e68355aac3e0c39b113b20f`, the one
llvm-mingw 20250709 builds (its `build-mingw-w64.sh`).

| Need | The project uses | mingw-w64 |
| --- | --- | --- |
| D3D12 | `ID3D12Device` up to `ID3D12Device8` (one feature check), `ID3D12GraphicsCommandList` and `...List1`, `D3D12_FEATURE_D3D12_OPTIONS2`, `7` and `8`, `ID3D12PipelineLibrary`, DRED (`ID3D12DeviceRemovedExtendedData`, `...Settings`), `ID3D12InfoQueue`, `ID3D12Debug`; no enhanced barriers, no Agility SDK exports (`D3D12SDKVersion`) | `d3d12.h` (1.2 MB) declares all of them, up to `ID3D12GraphicsCommandList7` and the enhanced barrier types; `d3d12sdklayers.h` the debug interfaces; `libd3d12.a` |
| DXGI | `IDXGIFactory1`, `2`, `4`, `5`, `IDXGIAdapter1`, `IDXGISwapChain1` and `3`, `IDXGIOutput`, `DXGI_FEATURE_PRESENT_ALLOW_TEARING`, `IDXGIDebug` and `IDXGIInfoQueue` through `DXGIGetDebugInterface1` (loaded at run time) | `dxgi1_6.h` (to `IDXGIFactory7` and `IDXGIOutput6`), `dxgi1_5.h`, `dxgidebug.h`; `libdxgi.a` |
| D3DCompile | the runtime's look mods ([shader_override.cpp](../../work/runtime/src/graphics/d3d12/shader_override.cpp)), the WARP tests, the launcher; the pipeline calls Windows' `d3dcompiler_47.dll` through `ctypes`, which no toolchain affects | `d3dcompiler.h` with `D3DCompile`, `D3DStripShader`, `D3DCOMPILE_OPTIMIZATION_LEVEL3`, `D3D_COMPILE_STANDARD_FILE_INCLUDE`; `libd3dcompiler.a` maps to `d3dcompiler_47.dll` |
| DXC | `dxcapi.h` (the runtime loads `dxcompiler.dll` at run time) | its own `dxcapi.h` (see 1.1); the vendored header is DXC's, under the NCSA licence (its README in `thirdparty/dxc`) |
| D3D11 | the launcher (Dear ImGui's DX11 back end) and the WARP tests | `d3d11.h`; `libd3d11.a` |
| WIC | the launcher's screenshots ([screenshot.cpp](../../work/fm4-launcher/src/screenshot.cpp)) | `wincodec.h`; `libwindowscodecs.a` |
| GameInput | SDL's GameInput driver and the runtime's `GameInputInitialize` call ([sdl_input_driver.cpp:507](../../work/runtime/src/input/sdl/sdl_input_driver.cpp)), from the NuGet package's MIT header and loader | **no `gameinput.h`**: the gap. The NuGet header's declarations are plain COM; under mingw `DECLARE_INTERFACE_IID_` drops the UUID ([combaseapi.h:118](https://github.com/mingw-w64/mingw-w64/blob/a0e69f7bc83a29b40e68355aac3e0c39b113b20f/mingw-w64-headers/include/combaseapi.h)), which the code does not need (it passes `IID_IGameInput`); every SAL annotation it uses is defined by mingw-w64's `sal.h` |
| XAudio2, XInput, Media Foundation | not used (audio goes through SDL; no `IXAudio2`, `IMF*` or XInput calls in the project's code) | `xaudio2.h`, `xinput.h`, `mfapi.h` exist anyway |
| Others linked | `dbghelp`, `winmm`, `bcrypt`, `shcore`, `urlmon`, `ole32`, `shell32`, `advapi32`, `user32`, `ws2_32`, `dxguid` | each has a `.def` or source in `mingw-w64-crt` (`ws2_32` not looked up) |
| `d3dx12.h` | not used by the runtime; FidelityFX brings its own copy | none |

**Gaps:** `gameinput.h` (the project brings the MIT one). The prototype found three more missing
headers: `DXProgrammableCapture.h` (the PIX capture interface `IDXGraphicsAnalysis`, declared under
mingw in [d3d12_api.h](../../work/runtime/include/rex/ui/d3d12/d3d12_api.h)), `crtversion.h` (for
FFmpeg) and `endian.h` (for the crypto), and that mingw-w64's headers default to Windows 7
(`_WIN32_WINNT` 0x0601), which hides `SetProcessInformation`, `RRF_SUBKEY_WOW6432KEY`,
`GetDpiForMonitor` and `SetProcessDpiAwarenessContext`: the build sets 0x0A00, as the Windows SDK
does by default. No D3D12 or DXGI interface the runtime uses is missing.

## 3. Third-party libraries under mingw

| Library | How the runtime builds it | Under mingw |
| --- | --- | --- |
| FFmpeg (`libavutil`, `libavcodec`, Xenia's fork `0604b46`) | the runtime's own CMake list with a checked-in `config_windows_x86_64.h` | made by `configure --toolchain=msvc` (`CC_IDENT` names MSVC 19.39): `HAVE_W32THREADS 1`, `HAVE_LIBC_MSVCRT 1`, `HAVE_ALIGNED_MALLOC 1`, `HAVE_IO_H 1`, `HAVE_UNISTD_H 0`, no inline assembly, no x86 assembly. Each holds under mingw with the Universal CRT. The prototype found one gap: `HAVE_LIBC_MSVCRT` includes `crtversion.h`, which mingw-w64 lacks (151 files failed); a two-line stub with the Universal CRT's major version 14 keeps FFmpeg's code that of the MSVC build. No other change |
| SDL3 (static, GameInput patch) | SDL's own CMake, with the NuGet header put first for SDL's sources ([thirdparty CMakeLists.txt:318](../../work/runtime/thirdparty/CMakeLists.txt)) | SDL supports mingw. Its `HAVE_GAMEINPUT_H` check (`check_c_source_compiles` of `#include <gameinput.h>`) finds the Windows SDK's old header today; under mingw it finds none and the GameInput driver is left out unless the check is pointed at the NuGet folder (`CMAKE_REQUIRED_INCLUDES`). The prototype confirmed it: under mingw `HAVE_GAMEINPUT_H` is empty and SDL's GameInput driver is left out, while the runtime's `GameInput.cpp` compiles (with `_WIN32_WINNT` 0x0A00). **Real work, small to medium, not done**; without it SDL's other pad drivers take the pads |
| Dear ImGui, fmt, spdlog, simde, toml++, CLI11, inja, stb | header or plain sources | each has `__MINGW32__` branches or no compiler-specific code (counted in the vendored trees); expected as-is |
| libmspack (`lzxd.c`), xxHash, o1heap, utfcpp, tiny-AES, aes_128, the crypto and DXBC checksum files, RenderDoc's header | plain C and C++ | no Windows code; as-is, except the crypto's `sha256.cpp`, which reads `<endian.h>` on every compiler but MSVC (a stub under mingw, section 10.2); `_MSC_VER` also in xxHash (1 header) |
| The PowerPC disassembler of `rexglue.exe` (`thirdparty/disasm`, from binutils) | C | no compiler-specific code; as-is |
| FidelityFX FSR 3.1 | built from source with clang when `-Fsr` is on ([rex_fsr.cmake](../../work/runtime/cmake/rex_fsr.cmake)) | **not checked**, likely work (its own D3D12 back end, PIX header, `/DELAYLOAD`). Off for players; a developer-only build keeps it on MSVC's libraries or drops it |

**Blockers:** none found that a shim cannot settle; GameInput is the one with real work, and FSR
is outside the player build.

## 4. Debugging and the project's tooling

| Tool | Today | Under llvm-mingw |
| --- | --- | --- |
| PDB | the profiling build passes `-gline-tables-only -gcodeview` and `-Xlinker /DEBUG` ([pipeline.py](../../outputs/FM4-build/pipeline.py), the `prof` runtime build) | supported: llvm-mingw's README documents `-gcodeview` with `-Wl,--pdb=`; only the flags change |
| The link map | `/MAP:fm4_rex.map`, read by [thread_profile.py](../../outputs/FM4-build/e2e/thread_profile.py) and [soak_report.py](../../outputs/FM4-build/e2e/soak_report.py) in MSVC's map format | lld's MinGW driver writes lld's own map format with `-Map`; the MSVC format needs lld-link's `/map` through `-Xlink` (not checked) or a second parser. Small |
| Symbols for the perf tools | [cp_profile.py](../../outputs/FM4-build/e2e/cp_profile.py) runs `llvm-symbolizer` on the PDBs | unchanged with PDBs; llvm-mingw's bundle of `llvm-symbolizer` not checked (LLVM's own copy works) |
| Crash dumps and stacks | `MiniDumpWriteDump` (`dbghelp`), `RtlVirtualUnwind` walks in the samplers and the write watch | Windows' `dbghelp.dll` and API; the dumps open in WinDbg with the PDBs. The walks need `.pdata`, which clang emits for x86-64 mingw |
| PGO (`pgo-gen`, `pgo-use`) | `clang_rt.profile-x86_64.lib` (kept by B3) | llvm-mingw builds compiler-rt's runtimes with the sanitizers' pass (its `build-compiler-rt.sh`), whose default includes the profile library (`libclang_rt.profile-x86_64.a`); not checked in the archive. PGO is an opt-in experiment, not a player build |
| The e2e harness | runs the built game and reads its logs and captures | nothing toolchain-specific beyond the map and PDBs above; `matrix.py` copies `.exe`, `.dll`, `.pdb` and `.map`, and the two new DLLs (below) must be staged too |

**Run-time files.** llvm-mingw links libc++ and libunwind as DLLs by default (`libc++.dll`,
1,843,712 bytes, and `libunwind.dll`, 92,160 bytes, in the archive's `x86_64-w64-mingw32in`;
the prototype's modules import both); the game's five modules share them as they share the VC++
runtime DLLs today, staged beside the game. Both are LLVM's (Apache-2.0 with LLVM exceptions), so even shipping
them would be allowed. The Universal CRT is part of Windows 10 and 11. **This removes the VC++
redistributable from the player's computer** and the open licence question of fetching its DLLs
([builder note 4.10](builder-installer.md#410-what-runs-on-the-players-computer)). The shipped
launcher would link libc++ statically (`-static`) and contain no Microsoft code at all, where a
launcher linked with MSVC's static runtime carries Microsoft's library code.

## 5. One toolchain or two

If players build with llvm-mingw while the gates build with Microsoft's libraries, every player
runs binaries no gate tested: another C++ library, another ABI at the COM boundary, another export
mechanism. The ABI items of section 1 fail at run time, not at compile time, so a "builds on both"
check would not catch them.

**Recommendation: one toolchain.** If the prototype passes, developers switch too, and the MSVC
build stays only as the A/B reference for the switch, then goes. The cost, once:

- **A full gate on the native path** with the mingw build against the previous main's MSVC build,
  captures at 1:1 beside each other ([gate image check](../../AGENTS.md#render-target-paths)). Under
  the tiered gates of 29 October 2026 this fits the one full gate at the end of Level 1, or is a
  gate of its own.
- **Performance A/B** on the development laptop: the same commit built both ways, run in both
  orders to cancel package power drift. The hot paths are the generated code, which has no `std::`
  container but the `jmp_buf` map, and the D3D12 back end (`std::vector`, `std::unordered_map`,
  `std::mutex`). libc++'s Windows threading support builds its mutex and condition variable on
  Windows' SRW locks and condition variables, as MSVC's STL does, and both allocate through the
  Universal CRT's `malloc`. The
  `memcpy` the code calls may differ (MSVC's `vcruntime140.dll` against the Universal CRT's).
  Expected within noise; **not measured**.
- **New baselines**: the e2e and perf baselines are per binary, so they are taken again from the
  mingw build; older runs stay comparable only through the A/B above.
- **The ROG Xbox Ally X** run through the export, by the owner.
- **The instruction tests** (295,650 checks of the compiler on the title's arithmetic) and the
  launcher tests, which run in every build anyway.

## 6. The toolchain

**The release to pin: llvm-mingw 20250709**, the last release with **LLVM 20.1.8**, the clang the
project already validates, so the prototype changes the libraries and the ABI and not the compiler.
Read from the GitHub API on 31 October 2026:

| Asset | Bytes | SHA-256 (GitHub's digest) |
| --- | --- | --- |
| `llvm-mingw-20250709-ucrt-x86_64.zip` (Windows x86-64 host, Universal CRT) | 171,028,561 | `82babcd6aae4dc3606e8e0471d816989c384b4bf86a139f184a8b4a1b2c2758d` |

URL: `https://github.com/mstorsjo/llvm-mingw/releases/download/20250709/llvm-mingw-20250709-ucrt-x86_64.zip`
(GitHub redirects release downloads to its asset host). The `ucrt` build is the one to take: the
Universal CRT is in every Windows 10 and 11, and the `msvcrt` build targets the old `msvcrt.dll`.
Unpacked by the pipeline's own `unpack_tool` (prototype): 9,353 members, all below the top folder
`llvm-mingw-20250709-ucrt-x86_64`, no link and no refused path; 9,104 files, 671,790,153 bytes, in
3.9 s. The archive holds the toolchains for four targets (x86-64, i686, ARMv7, ARM64); which members
the builder keeps is not decided. The newest release, 20261020, carries LLVM
23.1.4 (`llvm-mingw-20261020-ucrt-x86_64.zip`, 190,612,575 bytes); moving to it is a separate step,
like any compiler update.

**Licences** (read in each project's tree; not legal advice):

| Part | Licence |
| --- | --- |
| llvm-mingw's scripts and wrappers | ISC ("only the components provided by llvm-mingw directly"; the toolchain is under its parts' licences) |
| clang, lld, the LLVM tools, libc++, libc++abi, libunwind, compiler-rt | Apache-2.0 with LLVM exceptions |
| mingw-w64's C runtime and headers | the Zope Public License 2.1, except parts marked public domain, BSD or LGPL (`COPYING`); `COPYING.MinGW-w64-runtime.txt` lists the notices to carry when the runtime is linked into a distributed binary. Many headers are public domain (`wrl/client.h`); the D3D12 headers are generated from IDL files under the LGPL 2.1 taken from Wine (`d3d12.idl`: "Copyright 2016 Józef Kucia for CodeWeavers") |
| winpthreads | MIT, with parts from the POSIX threads library for Windows |

None of these needs a licence acceptance from the player, and all allow fetching and shipping
with their notices. The game is built on the player's computer and never distributed, so the
runtime's notices matter only for what the project ships: the launcher, which would carry the
mingw-w64 runtime notice and LLVM's. The LGPL IDL-derived headers contribute declarations and
small inline wrappers to code that is not distributed; whether that needs anything for a shipped
launcher (which uses `d3d11.h`, not `d3d12.h`) is not checked.

**It replaces the B3 LLVM download.** llvm-mingw's clang is a full clang 20.1.8 with lld, `llvm-ar`
and `llvm-rc` (as `llvm-windres`), so it takes the place of LLVM's 939.5 MB `.tar.xz` with a
171 MB zip; Python, CMake and Ninja stay. Whether its clang can still target `x86_64-pc-windows-msvc`
for the A/B build with Microsoft's libraries is checked only as far as emitting LLVM IR for that
target (section 10.4); a link with it is not checked.

## 7. Estimate

AI calendar time (1 person-week ≈ 3.5 h, range 2 to 7 h). Before the prototype this note estimated
14 to 28 h, most likely 18 h, with 2 to 3 h for the prototype and 3 to 7 h for the runtime's port.
The prototype took about 1.5 h with this note's update and did most of the porting (section 10),
so the estimate of what is left is:

| Area | Work | Estimate |
| --- | --- | --- |
| Prototype (section 8) | done on 31 October 2026 (section 10) | done |
| Pipeline and toolchain | a `toolchain.json` entry for llvm-mingw with a keep list (the archive holds four targets), the toolchain file in `pipeline.py` itself instead of the prototype's wrapper, `doctor` without `INCLUDE` and `LIB`, `libc++.dll` and `libunwind.dll` staged beside the game and the tests, DWARF stripped or moved to a PDB (51 MB in `fm4_rex.exe`), `test_pipeline.py` cases | 2 to 4 h |
| Runtime | the shims are in (section 10.2); left: the runtime's own struct-returning calls checked on WARP in the CPU tests, and the auto-import of data at run time | 0.5 to 1.5 h |
| Third party | SDL's GameInput driver under mingw | 1 to 2 h |
| Title code, tests, launcher | done, but for the map format | 0.5 h |
| Tooling | the map parser for lld's format or lld-link's `/map`, the profiling build (`-gcodeview`, `--pdb`), PGO checked once | 1 to 2 h |
| Verification | the smoke run, the full gate, the perf A/B both ways, new baselines, docs and notices; the Ally X run is the owner's | 4 to 7 h |
| **Total left** | | **9 to 17 h**, most likely about 12 h |

Most of what is left is proof, as before. Against it stands route (a)'s cost inside B4 (small) and
route (b)'s 7 h, both with the licence question left open.

## 8. Prototype plan

The smallest step that proves or kills the route, on the development computer, with no GPU run.
**Done on 31 October 2026; it passed (section 10).**

1. **Download** `llvm-mingw-20250709-ucrt-x86_64.zip` (171,028,561 bytes, from `github.com`, pinned
   by the digest above) into a new folder, for example `I:\FM4-toolchain-mingw`. **The only
   download; it needs the owner's approval first.**
2. **The runtime alone:** configure a copy of the BuildRoot's `runtime-source` with a CMake
   toolchain file for `x86_64-w64-mingw32` (the unpacked `bin` first on `PATH`, Vulkan, tests and
   Tracy off), build it with `ninja -k 0`, and record every error by class against section 1.1.
   Fix only the shims of 1.1, then build `rexruntime.dll`, `rexgpu-xenos.dll` and `rexglue.exe`.
3. **The instruction tests:** `generate` with the mingw `rexglue.exe` into a scratch BuildRoot
   (the generated code must equal the MSVC build's byte for byte, since `rexglue` only writes text),
   then build `fm4_instruction_tests.exe` and run it: 295,650 checks of the title's arithmetic
   under the new library and ABI, on the CPU.
4. **A D3D12 ABI probe on WARP:** a small test that creates a WARP device and checks the struct
   returns of section 1.1 (`GetCPUDescriptorHandleForHeapStart`, `GetGPUDescriptorHandleForHeapStart`,
   `GetDesc`, `GetResourceAllocationInfo`) against the values the MSVC build gets, with and without
   `WIDL_EXPLICIT_AGGREGATE_RETURNS`. WARP runs on the CPU, as the shader-mod tests do.

**Kill criteria:** an error class outside section 1.1 that needs a rewrite of shared runtime code;
a struct return that stays wrong with the macro set; an instruction test that differs from the MSVC
build; or a link of `rexruntime.dll` that the export and auto-import options cannot make work.

**After a pass:** the game build and a short smoke run on the native path (the main session's call
under the GPU rules), then the steps of section 5. The prototype's result goes into this note.

## 9. Risks and recommendation

| Risk | Effect | Mitigation |
| --- | --- | --- |
| The COM struct-return ABI | wrong descriptor handles or descriptions: corruption or a device removal, only at run time | `WIDL_EXPLICIT_AGGREGATE_RETURNS`, the WARP probe (8.4), and the full gate |
| setjmp and longjmp across the generated frames | the buffers' alignment crashed every race load (fixed, section 10.7); a title error path that misbehaves only under mingw | the aligned wrapper; a targeted test of the two generated users |
| SEH, VEH and C++ exceptions together | the write watch, the null-access capture and the MMIO handlers run in VEH, before any frame handler, so the C++ exception model should not matter; not shown | the e2e runs with the diagnostics on |
| Exports and auto-import of `rexruntime.dll` | link failures, or pseudo relocations in a module of 69 MB | `--export-all-symbols` and the `__imp_` exports of `hook.h` link (section 10.2); the pseudo relocations at run time are not shown |
| Silent differences in libm or `memcpy` | small numeric or performance changes | the instruction tests; the perf A/B; the gate's captures |
| GameInput | pads through SDL's other drivers only, until fixed | the SDL check against the NuGet header |
| Two toolchains during the switch | gates that test something else than players run | one toolchain (section 5) |
| llvm-mingw is one maintainer's project | releases stop | the pin keeps working; its scripts can rebuild the same toolchain from LLVM's and mingw-w64's sources |
| FSR | a developer build without FSR, or kept on MSVC's libraries | FSR is off for players |

**Recommendation, after the prototype: route (c).** The route removes every Microsoft licence from
the player's build (the libraries, the Windows SDK and the VC++ redistributable), downloads 171 MB
instead of 940 MB for the compiler plus Microsoft's packages, and needs no administrator rights. The
prototype passed every kill criterion (section 10). The game runs on the GPU after one more shim
(section 10.7). The open run-time question is the frozen-frame parity, and the next step is its
cause (sections 10.8 and 10.9); until it is explained the switch is blocked. The route costs
about 9 to 17 h more (section 7); route (a), Microsoft's installer, stays the fallback with its
open licence question for players.

## 10. Prototype results (31 October 2026)

The prototype of section 8, approved by the owner with its one download on 31 October 2026 ("saya
ijinkan"), on the development laptop, with no GPU run. Code: `5c4b25f` on the builder branch.

**Setup.** The archive came once with `curl` (13 s) into the new, empty folder
`I:\FM4-toolchain-mingw\downloads`: 171,028,561 bytes with the SHA-256 of section 6. It was
unpacked by the pipeline's `unpack_tool` under `python -I` into
`I:\FM4-toolchain-mingw\llvm-mingw-20250709-ucrt-x86_64` (section 6). Its `clang++` is clang
20.1.8 with the default target `x86_64-w64-windows-gnu`. Nothing else came from the network: the
15 third-party archives were copies of `I:\FM4-build-b3\downloads`, GameInput and DXC came through
`GAMEINPUT_MIRROR` and `DXC_MIRROR` from `I:\FM4-build-usa`, and Python 3.11.9, CMake 3.31.6 and
Ninja 1.12.1 were B3's portable copies in `I:\FM4-toolchain-b3`.

**How it builds.** Three files in [outputs/FM4-build/mingw](../../outputs/FM4-build/mingw):

- [x86_64-w64-mingw32.cmake](../../outputs/FM4-build/mingw/x86_64-w64-mingw32.cmake), the CMake
  toolchain file: llvm-mingw's `x86_64-w64-mingw32-clang`, `-clang++`, `-windres`, `llvm-ar` and
  `llvm-ranlib`, from `FM4_MINGW_ROOT`;
- [prototype.py](../../outputs/FM4-build/mingw/prototype.py), which runs `pipeline.py`'s own
  `setup`, `generate` and `build` stages:
  - it wraps the pipeline's `run()` so that every CMake configure (the runtime, the loader, the
    game, the tests and the launcher) takes the toolchain file instead of `clang`;
  - `--keep-going` adds `ninja -k 0`;
  - `PATH` holds only llvm-mingw's `bin`, the portable CMake, Ninja and Python, `System32` and
    `Windows`, and `INCLUDE` and `LIB` are removed;
  - after `setup` it copies `libc++.dll` and `libunwind.dll` into `runtime-install\bin`, which
    `build` stages beside the game;
  - `pipeline.py` itself is unchanged;
- [d3d12_abi_probe.cpp](../../outputs/FM4-build/mingw/d3d12_abi_probe.cpp), the ABI probe of
  section 10.4.

The iterations ran in the scratch BuildRoot `I:\FM4-build-mingw`, the final clean run in
`I:\FM4-build-mingw2`. Every build went through the build queue.

### 10.1 Errors by class

Each pass built with `ninja -k 0`, so every error of a pass was seen before a fix.

| Pass | Class | Files (errors) | In 1.1? | Fix (10.2) |
| --- | --- | --- | --- | --- |
| Runtime, pass 1: 203 of 787 steps failed | FFmpeg's MSVC config includes `crtversion.h`, which mingw-w64 lacks | 151 | no (section 3 named FFmpeg's config as a risk) | stub |
| | libc++ has no `clock_time_conversion` and `clock_cast` | 38, and 2 calls in `threading_win.cpp` | yes, verdict "as-is": wrong | the Apple fallback for any libc++ |
| | `DXProgrammableCapture.h` missing | 24 | no | declaration |
| | mingw-w64's headers default to Windows 7: `SetProcessInformation`, `RRF_SUBKEY_WOW6432KEY` (GameInput's loader) | 2 | no | `_WIN32_WINNT` 0x0A00 |
| | `__try` | 1 | yes | left out under mingw |
| Runtime, pass 2: 3 failed | the crypto's `sha256.cpp` includes `<endian.h>` on any compiler but MSVC | 1 | no | stub |
| | `_uuidof` with one underscore, an MSVC alias of `__uuidof` | 1 | no | `__uuidof` |
| | `FARPROC` assigned to `void*`, which MSVC mode allows and GNU mode rejects | 1 (6) | no | explicit cast |
| Runtime, pass 3: the link of `rexgpu-xenos.dll` | `__mingw_uuidof<T>()` undefined for nine DXC and DXBC interfaces | 9 symbols | yes (DXC's header) | GUID declarations |
| Runtime, pass 4 | the DLLs named `librexruntime.dll` and `librexgpu-xenos.dll`, which the GPU plugin loader would not find | not an error | no | no prefix |
| Game | both module DLLs: every guest import thunk undefined (`__imp__KeTlsAlloc` and the rest); lld's `--export-all-symbols` leaves out names that begin with `__imp_`, and the MSVC build exports 2,866 of them | 2 links | yes (the export setting) | `dllexport` in `REX_EXPORT` |
| Launcher | `GetDpiForMonitor`, `SetProcessDpiAwarenessContext` (Windows 7 default); `clock_cast`; libc++'s file clock counts in `__int128` nanoseconds; `WinMain` undefined | 4 | partly (`wWinMain` for the game only) | `_WIN32_WINNT`, `file_clock::to_sys`, `-municode` |

Nothing needed a rewrite of shared runtime code. The classes outside section 1.1 are four missing
headers, the Windows version default, the DLL prefix and two MSVC-mode spellings. Warnings only:
`-fasync-exceptions` is unused on the mingw target (the generated code has no SEH scopes), and the
runtime's usual `-Wunused` warnings.

### 10.2 The shims

All in [work/runtime](../../work/runtime), [work/fm4-rex](../../work/fm4-rex) and
[work/fm4-launcher](../../work/fm4-launcher), commits `5c4b25f` and, for the setjmp buffers, `f1292ab`
(section 10.7). Sixteen in all: fourteen under
CMake's `MINGW`, `__MINGW32__` or `_LIBCPP_VERSION`, and two spellings that need no guard.

| Shim | Where | Guard |
| --- | --- | --- |
| `_WIN32_WINNT` and `WINVER` 0x0A00, `WIDL_EXPLICIT_AGGREGATE_RETURNS`, for the runtime and, through `rexruntime`'s public definitions, the game and its tests | [CMakeLists.txt](../../work/runtime/CMakeLists.txt), [system/CMakeLists.txt](../../work/runtime/src/system/CMakeLists.txt) | `MINGW` |
| DLLs without the `lib` prefix | the runtime's and the game's `CMakeLists.txt` | `MINGW` |
| `-Wl,--export-all-symbols` for `rexruntime.dll` | [system/CMakeLists.txt](../../work/runtime/src/system/CMakeLists.txt) | `MINGW` |
| `REX_EXPORT_DECL`: a `dllexport` declaration of each name in `REX_EXPORT`, `REX_EXPORT_STUB`, `REX_EXPORT_STUB_RETURN` and `REX_HOOK_RAW`; these macros define all 2,866 `__imp_` names | [hook.h](../../work/runtime/include/rex/hook.h) | `__MINGW32__` |
| `crtversion.h` with `_VC_CRT_MAJOR_VERSION` 14, for libavutil and libavcodec | [thirdparty/CMakeLists.txt](../../work/runtime/thirdparty/CMakeLists.txt) | `MINGW` |
| `endian.h` (little-endian) for `xboxkrnl_crypt.cpp`, which includes `sha256.cpp` | [kernel/CMakeLists.txt](../../work/runtime/src/kernel/CMakeLists.txt) | `MINGW` |
| `clock_time_conversion` and `clock_cast`: the existing Apple fallback, now for any libc++ | [chrono.h](../../work/runtime/include/rex/chrono/chrono.h) | `__APPLE__` or `_LIBCPP_VERSION` |
| `IDXGraphicsAnalysis` declared as the Windows SDK declares it | [d3d12_api.h](../../work/runtime/include/rex/ui/d3d12/d3d12_api.h) | `__MINGW32__` |
| `__CRT_UUID_DECL` for `IDxcBlob`, `IDxcBlobUtf8`, `IDxcIncludeHandler`, `IDxcCompiler`, `IDxcCompiler3`, `IDxcUtils`, `IDxcResult`, `IDxcVersionInfo2`, `IDxbcConverter`, with the GUIDs of the vendored headers | [d3d12_api.h](../../work/runtime/include/rex/ui/d3d12/d3d12_api.h) | `__MINGW32__` |
| The generated code's setjmp buffers: `rex::ppc::JmpBuf`, an `alignas(16)` wrapper, through `REX_PPC_JMP_BUF_TYPE` and `REX_PPC_JMP_BUF` in the two templates (section 10.7) | [rex/ppc.h](../../work/runtime/include/rex/ppc.h), [pch_h.inja](../../work/runtime/resources/templates/codegen/pch_h.inja) | `__MINGW32__` |
| No `__try` around the debugger's thread-name exception | [threading_win.cpp](../../work/runtime/src/core/threading_win.cpp) | `__MINGW32__` |
| `-Wl,-Map=` instead of `/MAP:`, and `-municode` for `fm4_rex.exe` | [fm4-rex/CMakeLists.txt](../../work/fm4-rex/CMakeLists.txt) | `MINGW` |
| The launcher: `_WIN32_WINNT` 0x0A00, `-municode`, `-static` (libc++ linked into the shipped program) | [fm4-launcher/CMakeLists.txt](../../work/fm4-launcher/CMakeLists.txt) | `MINGW` |
| The launcher's build date: `file_clock::to_sys`, cast to the system clock's duration | [app.cpp](../../work/fm4-launcher/src/app.cpp) | `_LIBCPP_VERSION` |
| `__uuidof(ID3D12Device)` for `_uuidof(...)` | [d3d12_provider.cpp:259](../../work/runtime/src/ui/d3d12/d3d12_provider.cpp) | none: one keyword in clang's MSVC mode |
| `reinterpret_cast<void*>` for six `FARPROC` assignments | [xinput_input_driver.cpp](../../work/runtime/src/input/xinput/xinput_input_driver.cpp) | none: the conversion MSVC mode made implicitly |

Corrections to the research: `#pragma comment(lib, ...)` needs no shim (lld honours it), and
mingw-w64's `_mingw_mac.h` already defines `WIDL_EXPLICIT_AGGREGATE_RETURNS` for every GCC and clang
C++ build, so the CMake definition only makes it explicit. SDL's GameInput driver is left out under
mingw (`HAVE_GAMEINPUT_H` empty), as section 3 expected; the fix is not part of the prototype.

### 10.3 Generate and the tests

- **Generate.** `rexglue.exe` built with mingw ran `generate` with USA Disc 1. In the clean
  BuildRoot its 511 files in `project\generated` (269,459,918 bytes) equal those of the B3 build
  (`I:\FM4-build-b3`, `rexglue.exe` with Microsoft's libraries) byte for byte, but for
  `codegen.d`, which names the BuildRoot. `loaded-image.bin`, `fm4_switches.toml` (271 switch
  tables) and the manifest are equal too.
- **The codegen stamps.** In the iteration BuildRoot the three `codegen.stamp` files and
  `codegen.build.stamp` held another fingerprint than B3's. It comes from that folder, not from the
  toolchain: the same mingw `rexglue.exe` gives B3's fingerprint on a copy of that project elsewhere,
  and B3's `rexglue.exe` gives it there too. The stamps are the codegen's cache key; what in that
  folder changes the key was not found.
- **The instruction tests.** `fm4_instruction_tests.exe` built with mingw: 295,650 checks, 0
  failures, and the same output line as B3's (`half_checks` 68,121, `vector_checks` 227,529,
  `old_fma_differences` 264, `old_dot_differences` 84), in 96 ms.
- **The player tests.** The ten CPU tests passed, among them the wheel host test against SDL's
  virtual wheel and the native shader contract's parser. So did the launcher tests: 558 plans, the
  discs and Disc 2's four packages.

### 10.4 The D3D12 ABI probe

The probe makes a WARP device and calls the struct-returning methods of section 1.1:

- `GetCPUDescriptorHandleForHeapStart`, on a shader-visible CBV/SRV/UAV heap and on an RTV heap;
- `GetGPUDescriptorHandleForHeapStart`;
- the heaps', a texture's and a copy queue's `GetDesc`;
- `GetResourceAllocationInfo` for a texture and a buffer;
- `GetAdapterLuid`, against DXGI's adapter description.

It writes a null SRV through the start handle and reads the device's removed reason. It was built
three ways from the same source:

- with LLVM 20.1.8 against Microsoft's libraries (B3's `clang++`, Visual Studio's `INCLUDE` and
  `LIB`);
- with llvm-mingw;
- with llvm-mingw and `PROBE_GNU_RETURNS`, which undefines the macro after `_mingw_mac.h`.

| Line | Microsoft's libraries | llvm-mingw | llvm-mingw, GNU returns |
| --- | --- | --- | --- |
| heap start handles set (CPU, aligned; GPU; RTV CPU) | 1, 1, 1, 1 | the same | crashed before its first line (exit 139) |
| `heap.desc` (type, count, flags, node mask) | 0, 1234, 1, 1 | the same | |
| `rtv.desc` | 2, 7, 0, 1 | the same | |
| increment | 64 | the same | |
| removed reason after the write | 0x00000000 | the same | |
| `allocation` (texture 1280 x 720 x 3, 4 mips, RGBA16F) | 29,425,664, 65,536 | the same | |
| `allocation.buffer` (196,608 bytes) | 196,608, 65,536 | the same | |
| `resource.desc` | 3, 65,536, 1280, 720, 3, 4, 10, 1, 0, 0, 1 | the same | |
| `queue.desc` | 3, 100, 0, 1 | the same | |
| LUID equal to DXGI's | 1 | the same | |
| failures | 0 | 0 | |

The two working builds print the same 14 lines. clang's IR for a minimal COM interface shows why the
third crashes: on `x86_64-w64-windows-gnu` an 8-byte struct comes back in a register and a 16-byte
one through a hidden pointer passed before `this`, while `x86_64-pc-windows-msvc` passes the hidden
pointer after `this` for both. The kill criterion "a struct return that stays wrong with the macro"
is not met.

### 10.5 Times and sizes

| Step | Time |
| --- | --- |
| Download | 13 s |
| Unpack (9,104 files) | 3.9 s |
| Runtime, pass 1 with `-k 0` (setup with the archives' unpack) | 188 s |
| Runtime, passes 2 to 5 (incremental) | 12 to 58 s each |
| `generate` (iteration BuildRoot) | 35 s |
| Game, pass 1 with `-k 0` | 146 s |
| The ABI probe, three builds and runs | 3 s |

The clean run, `prototype.py all --tests player --jobs 8` into the new BuildRoot
`I:\FM4-build-mingw2`, against B3's run with Microsoft's libraries:

| Stage | llvm-mingw | B3 (MSVC libraries) | Largest steps (llvm-mingw) |
| --- | --- | --- | --- |
| `setup` | 202.9 s | 210.5 s | unpacking the archives 57.9 s, the runtime's configure 54.2 s and build 56.6 s |
| `generate` | 35.2 s | 40.3 s | `codegen` 23.4 s |
| `build --tests player` | 181.8 s | 191.5 s | the game 132.2 s, the wheel host test 8.5 s, the launcher 6.6 s |
| `all` | 420 s | 443 s | |

| Size | llvm-mingw | MSVC libraries (B3) |
| --- | --- | --- |
| The toolchain, unpacked (all of it) | 671,790,153 bytes, 9,104 files | LLVM's kept 304,219,141 bytes, plus Visual Studio and the Windows SDK |
| The BuildRoot after the clean run | 1,771,208,764 bytes, 16,973 files | 1,719,104,970 bytes, 16,984 files |
| The iteration BuildRoot `I:\FM4-build-mingw` | 2,067,428,992 bytes | |
| `fm4_rex.exe` | 120,898,048 bytes (`.text` 63,255,062; DWARF 51,312,671) | 68,908,032 bytes (`.text` 66,430,150; no debug data) |
| `fm4_rex_XMediaFacade_default.dll`, `fm4_rex_SpeechFacade_default.dll` | 21,559,808, 15,301,632 | 12,391,424, 8,992,768 |
| `rexruntime.dll` | 13,777,920 bytes, 17,655 exports | 11,006,464 bytes, 10,647 exports |
| `rexgpu-xenos.dll` | 8,420,864 | 7,491,584 |
| `MotorsportDX.exe` (static) | 6,651,904 | 4,763,136 |
| Staged C++ runtime | `libc++.dll` 1,843,712, `libunwind.dll` 92,160 | the VC++ redistributable |

The generated code's `-gline-tables-only` stays in the mingw binaries as DWARF, where lld-link drops
it without `/DEBUG`; the code itself is 5 % smaller. The extra exports come from
`--export-all-symbols`, which also exports inline and template functions. No mingw module imports a
Microsoft C++ runtime DLL: they import the Universal CRT's `api-ms-win-crt-*` sets, `libc++.dll`,
`libunwind.dll` and Windows' own DLLs, and the launcher only Windows' own DLLs. Run-time pseudo
relocations of mingw's auto-import: 121 in `fm4_rex.exe`, 4 in each module, 1,150 in
`rexgpu-xenos.dll`.

### 10.6 The MSVC path

The shims leave the build with Microsoft's libraries as it was. Two builds with the current
toolchain show it, both made as B3's was: B3's portable Python 3.11.9, CMake, Ninja and LLVM 20.1.8,
Visual Studio's `INCLUDE` and `LIB`, `PATH` set to `System32` and `Windows` only, the archives
copied into each BuildRoot's `downloads`, and `all --tests player --jobs 8` with USA Disc 1 and
Disc 2.

- **The branch.** A `snapshot` of `5c4b25f` in `I:\FM4-src-mingwcheck` was built into
  `I:\FM4-build-msvccheck`. `doctor` passed. The stages took 212.4 s for `setup`, 38.6 s for
  `generate` and 193.0 s for `build` (444 s in all). The instruction tests printed the same line
  as in section 10.3, and the ten CPU tests and the launcher tests (558 plans) passed.
  `build-result.json` names `5c4b25f`, with `dirty` false.
- **The previous main.** A `snapshot` of `cc4e7f9` (the code of B3) in `I:\FM4-src-mingwbase1`
  was built into `I:\FM4-build-msvcbase1` (441 s, passed). Its folder names have the same lengths
  as the branch's, since the source paths land in the binaries as strings.

The two builds, section by section:

- `.text` is equal in `fm4_rex.exe`, both modules, `rexgpu-xenos.dll`, `MotorsportDX.exe` and
  `fm4_instruction_tests.exe`. Their `.rdata` and `.data` differ by the path strings and the build
  timestamp.
- In `rexruntime.dll`, `.text` has the same size and differs in 48 bytes at 39 places. 36 of them
  decode as instructions that differ only in a RIP-relative displacement to a string that moved
  within `.rdata`. The other 3 are decodes that start inside an instruction, and their bytes show
  the same shift of 0x45.

No instruction of the MSVC build changed.

### 10.7 The game runs and the setjmp crash

The main session asked for a smoke A/B on the GPU after section 10.6. Both runs used the native
path (`--native`), the launcher's default look, `FM4_FIXED_SEED=1`, and `matrix.py`'s `smoke` tier,
then its `parity` tier. They ran under one perf hold of the queue. Both BuildRoots got a copy of
`I:\FM4-build-usa`'s played profile (`B13EBABEBABEBABE`, `achievements`), and `e2e.py` imported
Disc 2 into each before its first launch; the mingw build's import mode worked. The game's `PATH`
held only `C:\Windows\System32`, `C:\Windows` and Windows PowerShell's folder, no toolchain
folder. The mingw game therefore loaded the `libc++.dll` and `libunwind.dll` staged beside it.

**The first A/B.** The MSVC build of `cc4e7f9` (`I:\FM4-build-msvcbase1`) passed every cell. The
mingw build of `5c4b25f` (`I:\FM4-build-mingw2`) booted and rendered:

- the splash videos, the title screen and the main menu;
- the Free Play and Career menus;
- the career car screen.

It then crashed with exit code 0xC0000005 every time a race loaded:

- after `17_DYNAMIC_LOADING_FREEPLAY` in `photo` and both `freeze` runs;
- after `push 8-Car_PreRace` in `career`.

Nothing else showed the crash: no error key that the MSVC run lacks, no device removal, no event
in Windows' Application log, and no dump (the title's crash dump is only for guest null
accesses).

**The stack.** The unpacked llvm-mingw has `lldb.exe`, but the runtime takes access violations
on purpose (the MMIO and write-watch handlers), so a debugger would stop at each. Instead,
[fm4-crash-diagnostics.cpp](../../work/fm4-rex/src/fm4-crash-diagnostics.cpp) got an
unhandled-exception filter, under `__MINGW32__` and only in harness runs (`FM4_CAPTURE_DIR`). It
writes the exception, the registers, and the stack unwound from the exception's context with
`RtlVirtualUnwind`, as module+offset, to `<capture>\unhandled-crash.txt`.
`llvm-symbolizer` reads the mingw binaries' DWARF line tables. The crash (run `mgdiag1`):

| Frame | Where |
| --- | --- |
| #00 | `ucrtbase.dll+0xed52b`: `__intrinsic_setjmpex+0x3b`, `movdqa %xmm6, 0x60(%rcx)`, with `rcx` = `0x1cfefbe0c28` (8 bytes past a 16-byte boundary) |
| #01 | `fm4_rex.exe`: `ppc_setjmp` in `sub_826FEEC8` ([generated](../../work/runtime/resources/templates/codegen/pch_h.inja) `fm4_rex_recomp.113.cpp`) |
| #02 to #10 | eight more generated functions (`sub_82701A90`, `sub_826FB6E0`, ... `sub_8275F320`) |
| #11 to #13 | `rex::system::XThread::Execute`, its `std::function`, `rex::thread::ThreadStartRoutine` |

**The cause** (the setjmp row of section 1.1; that row said "compiles as-is; behaviour to verify"):

- The generated code keeps one `jmp_buf` per guest buffer in a
  `std::unordered_map<u32, jmp_buf>`.
- The Universal CRT's x64 setjmp stores `xmm6` to `xmm15` with `movdqa`, which needs a 16-byte
  aligned buffer.
- Microsoft's `setjmp.h` puts the 16-byte alignment on the `_SETJMP_FLOAT128` struct.
  mingw-w64's puts it on the `SETJMP_FLOAT128` typedef (`typedef _CRT_ALIGN(16) struct ...`).
- clang then reports `alignof(jmp_buf)` = 16 but `alignof(std::pair<const uint32_t, jmp_buf>)` = 8,
  so the map's buffers sat 8 bytes off a 16-byte boundary (measured in a small mingw program).

Nothing of the ABI cases listed in the brief (a COM struct return, auto-import, SEH) was involved.

**The fix**, `d13cea5` and then `f1292ab`:

- Under `__MINGW32__`, [rex/ppc.h](../../work/runtime/include/rex/ppc.h) defines an `alignas(16)`
  wrapper, `rex::ppc::JmpBuf`, at its end.
- The two templates ([pch_h.inja](../../work/runtime/resources/templates/codegen/pch_h.inja) and
  the test template) use it through `REX_PPC_JMP_BUF_TYPE` and `REX_PPC_JMP_BUF`. Outside
  llvm-mingw these expand to `jmp_buf` and the buffer itself.
- `d13cea5` first put the wrapper into the template itself. That added 14 lines, and the generated
  code stores `__LINE__` values of the precompiled header as immediates (0x149 became 0x157), so
  the MSVC build's modules changed although their code did not. `f1292ab` keeps the template's
  line count.
- A small mingw program with the wrapper: all 64 buffers 16-byte aligned, and 1,000 setjmp and
  longjmp pairs across 21 frames returned correctly.

**The second A/B** (`mgabms2` and `mgabmp2` against `mgabgs2` and `mgabgp2`, on `d13cea5`; the same
cells):

| Cell | MSVC (`cc4e7f9`) | llvm-mingw |
| --- | --- | --- |
| `photo` | pass: race 237.11 fps, p99 4.84 ms; photo mode 29.95 fps | pass: race 233.88 fps, p99 5.18 ms; photo mode 29.98 fps |
| `career` | pass: race 234.21 fps, p99 5.35 ms; replay 118.46 fps | pass: race 236.29 fps, p99 5.09 ms; replay 117.65 fps |
| `photo_view` | pass, 56.98 fps | pass, 57.02 fps |
| `freeze` (2 runs, 2x) | pass, 59.95 fps | pass, 60.0 fps |

One run each, so the fps is not a performance verdict. One `career` run of the final build
`f1292ab` passed too: race 237.02 fps, p99 4.94 ms.

**Parity of the frozen frame: FAIL on draws.**

- `parity.py` against the MSVC cell:
  - draws per frame: 2,266 and 2,263 against 2,053 and 2,055 (+10 %; the limit is 3 %);
  - predicated runs: +19 %;
  - image: 41.6 % and 42.3 % of the pixels differ by more than 4 (the limit is 60 %).
- The two MSVC gates against each other passed: draws within 0.7 %, image 35.6 %.
- A third pair with `FM4_FIXED_FRAME_TIME=1` (`mgfzm`, `mgfzg`, one run each) gave the same picture:
  draws +11 %, image 43.0 %.
- At 1:1 the frozen frames show the same race at another moment, and both are complete renders.
  Race frames from the same second (the HUD at 00:01.5 and 00:01.8) show the same place (5/12),
  the same cars and the same grid. The MSVC frame shows the car past the grandstand with one car
  ahead. The mingw frame shows it at the start line with three cars ahead in view, which accounts
  for the extra draws.
- [frozen-frames.md](../e2e/frozen-frames.md) records that the moment is not pinned, since the
  race advances by wall time. Here the mingw build lands on another moment in all three runs, while
  the MSVC runs land on one moment. Why the mingw build's rewind reaches another moment is not
  found.

Files:

- **The 2x captures** of the second A/B:
  - `I:\FM4-build-msvcbase1\e2e\mgabmp2-60-freeze\run1\capture` and `run2`;
  - `I:\FM4-build-mingw2\e2e\mgabgp2-60-freeze\run1\capture` and `run2`;
  - the frozen frames compared are the last of the phase (`frame-087.ppm` and `frame-088.ppm` in
    run 1).
- **Race captures at 1x**: the `photo` and `career` cells, `mgabms2-240-*` and `mgabgs2-240-*`.
- **The parity reports and crops**: `I:\FM4-build-mingw2\e2e\mgabgp2-parity` and `mgfzg-parity`.

**The MSVC path after the fix.**

- A `snapshot` of `f1292ab` in `I:\FM4-src-mingwchek3` was built into `I:\FM4-build-msvcchek3`
  as in section 10.6 (445 s, the instruction and launcher tests passed).
- Against the `cc4e7f9` build of section 10.6:
  - `.text` is equal in `fm4_rex.exe`, both modules, `rexgpu-xenos.dll`, `MotorsportDX.exe` and
    `fm4_instruction_tests.exe`;
  - `rexruntime.dll`'s `.text` differs only in 176 RIP-relative displacements to moved strings
    (and three decodes that start inside an instruction).
- The crash reporter changes nothing in the MSVC build: it is under `__MINGW32__`, and
  `fm4_rex.exe`'s `.text` is equal.

### 10.8 The other frozen moment: what is ruled out

The main session compared the frozen frames at 1:1: the rendering is the same, and only the game
state differs (the needles, the cars' places, the place on the track). Each build repeats itself,
and `FM4_FIXED_FRAME_TIME=1` does not remove the difference. Players must not get another
simulation, so this blocks the switch until it is explained. A search of about 2 h on
31 October 2026 ruled out the following and did not find the cause.

**Where the difference shows.**

- The race is the same until the rewind:
  - the same grid (5th of 12, the same cars) at 00:01.5;
  - the same Y presses: 110 guest polls with Y and 94 converted, from the same packet numbers
    on, in all four runs;
  - the rewind shows 12.03 s after the race scenario in both builds.
- Both rewinds reach their oldest frame: REWIND FURTHER is greyed at 1:1 in both frozen frames.
- The MSVC build's oldest frame lies about 6 to 10 s into the race, as an older MSVC run on the
  development BuildRoot shows too (`l1c-ab32-60-freeze`). The mingw build's lies at the start line,
  about 1 s in. **The mingw build's rewind keeps a longer history.**

**Ruled out.**

| Lead | Check | Result |
| --- | --- | --- |
| The floating-point environment | the runtime keeps the FPSCR in MXCSR through simde (`fpscr.h`); no `fesetround`, `_controlfp` or `FE_*` use in the runtime, the title code or the templates | the same code in both builds; x87 is not used |
| Host libm | the imports of both `fm4_rex.exe` | both take `fma`, `fmaf`, `log2f`, `sqrt` and `sqrtf` from the Universal CRT, the only math the generated code calls (11,046 `std::fma`, 319 `sqrt`, 8 `log2f`); MSVC also imports `_dsign` |
| `long double` (80-bit under mingw, 64-bit under MSVC) | the runtime, the title code and the templates | not used |
| Thread timing | the MSVC build with its process confined to 4 performance cores (`--affinity 0xC3`, `mgaff-m`) | it still freezes the MSVC moment: draws 2,017 against 2,017 (pass) and −10 % against mingw |
| Frame pacing | the frame times of the race (`perf.jsonl`) | the same distribution, 16.2 to 17.2 ms |
| The guest's memory | `REX_GUEST_MEM_TRACE` in a `boot` run of each build (`mgmem-m`, `mgmem-g`) | identical: the title's two pools (329,973,760 and 58,720,256 bytes at the same addresses), the same heaps to the byte |
| The race start | session times with the fixed frame time: the 3-2-1 starts 60 updates after the placing in both | the same; mingw's pre-race has 37 more session updates of loading, which also varies within one build |
| Duplicated cvar storage | the `FLAGS_*_storage_` accessors in `fm4_rex.exe` | lld's import thunks, one instance in `rexruntime.dll` as under MSVC |
| `CallFrame`'s registers that are not initialized | its users | not used by the title or the runtime |

**Not checked, in the order the next search would take them.**

1. A value that the host hands to the guest and that depends on the C++ library: iteration order
   of `std::unordered_map` or `std::unordered_set`, or equal elements after `std::sort` (libc++
   and MSVC's STL order them differently, each the same in every run). Examples: enumerations
   of files, content, handles or objects that the kernel or XAM functions return.
2. The guest's timebase and time source during the rewind (`sub_822D92D8`, `mftb`): how the rewind
   measures its history. If it measures in time rather than frames, a difference in what the
   guest reads there would change it.
3. A probe that dumps the rewind's own state (its buffer's head and tail) at the freeze in both
   builds, to tell a longer buffer from a different reading of the same buffer.

### 10.9 Not verified, and the next step

Not verified:

- **The frozen frame at the same moment** (10.7, 10.8): the parity gate fails on draws because the
  mingw build's rewind keeps a longer history and freezes another moment of the same race; the
  cause is not found.
- **Longer play**: one smoke and one parity gate, and three single career runs. Not exercised:
  the auto-import pseudo relocations beyond these runs, a longjmp of the title's error path,
  the vectored handlers under faults other than the runtime's own, audio and input beyond the
  scenarios.
- Performance (section 5's A/B): one run per cell only.
- SDL's GameInput driver (left out).
- lld's map format for `thread_profile.py` and `soak_report.py`; PDBs; the profiling, flavor, PGO
  and FSR builds.
- The launcher beyond `--test`.
- A computer without Visual Studio (the mingw target never looks for it, and `INCLUDE` and `LIB`
  were unset), Windows 10.
- llvm-mingw's `clang` targeting `x86_64-pc-windows-msvc`, beyond emitting IR.

**Next step, the main session's call:** the cause of the other frozen moment, along the list of
10.8. It decides whether the parity gate can compare the two toolchains at all, or whether the mingw
build changes the title's behaviour; until then the switch is blocked. Then the steps of section 5: the pipeline's own switch to llvm-mingw
(section 7), the full gate and the performance A/B both ways.
