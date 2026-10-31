# The llvm-mingw toolchain: route (c) (31 October 2026)

Research for route (c) of the [builder installer](builder-installer.md#47-the-msvc-libraries-and-the-windows-sdk):
can Motorsport DX build with **llvm-mingw** (clang targeting mingw-w64, with libc++, mingw-w64's C
runtime and headers, and lld) instead of Microsoft's C and C++ libraries and the Windows SDK? The
owner chose to research this route first on 31 October 2026 ("Riset llvm-mingw dulu"), because the
Visual Studio Build Tools, Community and Windows SDK terms do not clearly cover a player who builds
the game only to play it.

This is a design note. Nothing was downloaded, built or run. The counts come from `grep` over the
repository at `4c9ca04` and over the development BuildRoot's generated code and vendored third-party
sources (`I:\FM4-build-usa`); the mingw-w64 and llvm-mingw facts come from their source trees and
the GitHub API, read on the web. Anything that only a build can settle is marked "not checked". This
note is not legal advice.

**Summary.** The port looks smaller than the 14 to 28 h guessed in the builder note, but its real
risks are in the ABI and only a build can find them. No site needs a rewrite. The runtime already
builds for Linux and macOS (with libc++ on macOS), its Windows code calls the Win32 API rather than
the MSVC runtime, and mingw-w64 has every D3D12, DXGI, WIC and D3DCompile declaration the project
uses, plus `wrl/client.h` and its own `dxcapi.h`. What needs real work: the 59 calls of D3D12
methods that return a struct (a known mingw ABI trap), the GameInput header for SDL, the symbol
export of `rexruntime.dll`, and the proof that the game behaves the same (setjmp and longjmp, the
exception handlers, performance). **Recommendation: a prototype first** (section 9), with one
download of 171 MB, before the owner decides between routes (c) and (a).

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
commit llvm-mingw 20250709 uses (section 6); none of it was compiled.

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
| C++20 and C++23 library: `std::filesystem` (579 uses), `std::expected`, `std::jthread`, `std::stop_token`, `std::format` (10), `std::from_chars` (integers; floats go through the runtime's own `portable_float_from_chars`), `std::chrono::clock_cast`, `std::byteswap`, one `std::regex` | spread | as-is with libc++ 20, as far as known; the runtime's POSIX branches already compile against libc++ on macOS upstream (not checked for this fork) |
| SEH `__try`/`__except` | 2 | [threading_win.cpp:75](../../work/runtime/src/core/threading_win.cpp) (the debugger's thread-name exception, only when a debugger is attached; `SetThreadDescription` names threads anyway); [exceptions.h:107](../../work/runtime/include/rex/platform/exceptions.h) (`SEH_TRY` macros) | **small shim**: `__try` needs `-fms-extensions` on a mingw target, and whether clang generates it there is not checked. The first site can go under mingw; the macros are emitted only for guest functions with SEH scopes ([function_graph.cpp:679](../../work/runtime/src/codegen/function_graph.cpp)), and the FM4 generated code has none (0 `SEH_TRY` in 368 files) |
| `#pragma comment(lib, "winmm.lib")` | 2 | [cp_sampler.h:18](../../work/runtime/src/graphics/util/cp_sampler.h), [thread_sampler.cpp:14](../../work/runtime/src/graphics/util/thread_sampler.cpp) | **small shim**: `target_link_libraries(... winmm)`; whether lld in mingw mode honours the pragma is not checked |
| Linker options in MSVC form | 4 | `LINKER:/MAP:` ([fm4-rex CMakeLists.txt:66](../../work/fm4-rex/CMakeLists.txt)); `/DELAYLOAD` with FSR only ([rexglue CMakeLists.txt:33](../../work/runtime/src/rexglue/CMakeLists.txt)); `-Xlinker /DEBUG` and `/opt:lldltojobs` in [pipeline.py](../../outputs/FM4-build/pipeline.py) | **small shim**: lld's MinGW driver takes `--pdb=`, `--thinlto-jobs=`, and `-Xlink=<option>` for lld-link options (the last not checked) |
| `wWinMain` | 1 | [windowed_app_main_sdl.cpp:107](../../work/runtime/src/ui/windowed_app_main_sdl.cpp) | **small shim**: `-municode` on the game's link |
| `WINDOWS_EXPORT_ALL_SYMBOLS` | 1 | [system/CMakeLists.txt:61](../../work/runtime/src/system/CMakeLists.txt) (`rexruntime.dll`, 10,647 exports today) | **small shim, to verify**: CMake implements it only for MSVC-style linkers; mingw needs `-Wl,--export-all-symbols` (the DLL has one `dllexport`, which turns mingw's automatic export off) and data imports go through mingw's auto-import with run-time pseudo relocations |
| DXC's API header | 1 header | the vendored `thirdparty/dxc/include/dxcapi.h` (DXC commit `6b6f402`, NCSA) declares its interfaces with `__declspec(uuid)` | **small shim**: mingw-w64 ships its own `dxcapi.h` with `__CRT_UUID_DECL` for every interface the runtime uses (`IDxcCompiler3`, `IDxcUtils`, `IDxcResult`, `IDxcBlobUtf8`, `IDxcVersionInfo2`); use it, or a newer DXC header with the same declarations |
| **D3D12 methods that return a struct** | **59 calls**: `GetCPUDescriptorHandleForHeapStart` 17, `GetGPUDescriptorHandleForHeapStart` 8, `GetDesc` 29, `GetDesc1` 3, `GetResourceAllocationInfo` 2 | the D3D12 back end, for example [command_processor.cpp:2183](../../work/runtime/src/graphics/d3d12/command_processor.cpp) | **real work, to verify**: Microsoft's x64 ABI returns a struct from a member function through a hidden pointer; the GNU C++ ABI on mingw does not, the classic mingw D3D12 crash ([GCC bug 64384](https://gcc.gnu.org/pipermail/gcc-bugs/2020-June/705246.html)). mingw-w64's `d3d12.h` carries both forms behind `WIDL_EXPLICIT_AGGREGATE_RETURNS` (16 places), and none of its headers defines that macro: the build must define it before `d3d12.h` and a test must show each call returns the right value. `dxgi1_6.h` and `d3d11.h` have no such method in use (DXGI's `GetDesc` returns an `HRESULT`) |
| setjmp and longjmp in the generated code | 2 generated files of FM4 (`fm4_bounds.toml` sets `setjmp_address` and `longjmp_address`: "Native setjmp/longjmp must unwind the host stack") | the template [pch_h.inja:269](../../work/runtime/resources/templates/codegen/pch_h.inja) | **compiles as-is; behaviour to verify**: with the Universal CRT on x64, mingw-w64's `setjmp` passes the frame address to `_setjmp`, so `longjmp` unwinds through the frames as under MSVC; the title's error path that uses it is not exercised by a short run |

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

**Gaps:** `gameinput.h` (the project brings the MIT one) and the `WIDL_EXPLICIT_AGGREGATE_RETURNS`
switch the build has to set. No D3D12 or DXGI interface the runtime uses is missing.

## 3. Third-party libraries under mingw

| Library | How the runtime builds it | Under mingw |
| --- | --- | --- |
| FFmpeg (`libavutil`, `libavcodec`, Xenia's fork `0604b46`) | the runtime's own CMake list with a checked-in `config_windows_x86_64.h` | made by `configure --toolchain=msvc` (`CC_IDENT` names MSVC 19.39): `HAVE_W32THREADS 1`, `HAVE_LIBC_MSVCRT 1`, `HAVE_ALIGNED_MALLOC 1`, `HAVE_IO_H 1`, `HAVE_UNISTD_H 0`, no inline assembly, no x86 assembly. Each holds under mingw with the Universal CRT, so it probably compiles; if not, a mingw config header (`configure --target-os=mingw32`, made once by a developer) replaces it. Not checked |
| SDL3 (static, GameInput patch) | SDL's own CMake, with the NuGet header put first for SDL's sources ([thirdparty CMakeLists.txt:318](../../work/runtime/thirdparty/CMakeLists.txt)) | SDL supports mingw. Its `HAVE_GAMEINPUT_H` check (`check_c_source_compiles` of `#include <gameinput.h>`) finds the Windows SDK's old header today; under mingw it finds none and the GameInput driver is left out unless the check is pointed at the NuGet folder (`CMAKE_REQUIRED_INCLUDES`). Whether `GameInput.cpp` and SDL's GameInput sources then compile is not checked. **Real work, small to medium**; without it SDL's other pad drivers take the pads |
| Dear ImGui, fmt, spdlog, simde, toml++, CLI11, inja, stb | header or plain sources | each has `__MINGW32__` branches or no compiler-specific code (counted in the vendored trees); expected as-is |
| libmspack (`lzxd.c`), xxHash, o1heap, utfcpp, tiny-AES, aes_128, the crypto and DXBC checksum files, RenderDoc's header | plain C and C++ | no `_MSC_VER` (xxHash 1 header) and no Windows code; as-is |
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
`libunwind.dll`; read in its documentation, not checked in the archive); the game's five modules would share them as they share the VC++ runtime DLLs
today, staged beside the game. Both are LLVM's (Apache-2.0 with LLVM exceptions), so even shipping
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
The unpacked size and the member list are not checked. The newest release, 20261020, carries LLVM
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
for the A/B build with Microsoft's libraries, and which members the builder keeps, are not checked.

## 7. Estimate

AI calendar time (1 person-week ≈ 3.5 h, range 2 to 7 h); the ranges are wide because the
compile errors are not counted until the prototype.

| Area | Work | Estimate |
| --- | --- | --- |
| Prototype (section 8) | the download, a CMake toolchain file, the runtime and the instruction tests | 2 to 3 h |
| Pipeline and toolchain | `toolchain.json` entry, `doctor` for mingw (no `INCLUDE`/`LIB`), the target triple, the PDB and map flags, staging `libc++.dll` and `libunwind.dll`, `test_pipeline.py` cases | 2 to 4 h |
| Runtime | the shims of 1.1, `WIDL_EXPLICIT_AGGREGATE_RETURNS` and a WARP test of the 59 calls, the DXC header, the exports, whatever the compiler reports | 3 to 7 h |
| Third party | SDL's GameInput, FFmpeg's config if needed | 1 to 3 h |
| Title code, tests, launcher | `-municode`, the map option, the static launcher, the WARP tests | 1 to 2 h |
| Tooling | the map parser or `/map`, the profiling build, PGO checked once | 1 to 2 h |
| Verification | the full gate, the perf A/B both ways, new baselines, docs and notices; the Ally X run is the owner's | 4 to 7 h |
| **Total** | | **14 to 28 h**, most likely about 18 h |

The total matches the builder note's guess, but the split is different: little porting, most of it
in proof. Against it stands route (a)'s cost inside B4 (small) and route (b)'s 7 h, both with the
licence question left open.

## 8. Prototype plan

The smallest step that proves or kills the route, on the development computer, with no GPU run:

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
| setjmp and longjmp across the generated frames | a title error path that misbehaves only under mingw | the same CRT `longjmp` with the frame address; a targeted test of the two generated users |
| SEH, VEH and C++ exceptions together | the write watch, the null-access capture and the MMIO handlers run in VEH, before any frame handler, so the C++ exception model should not matter; not shown | the e2e runs with the diagnostics on |
| Exports and auto-import of `rexruntime.dll` | link failures, or pseudo relocations in a module of 69 MB | `--export-all-symbols`; an export file if needed |
| Silent differences in libm or `memcpy` | small numeric or performance changes | the instruction tests; the perf A/B; the gate's captures |
| GameInput | pads through SDL's other drivers only, until fixed | the SDL check against the NuGet header |
| Two toolchains during the switch | gates that test something else than players run | one toolchain (section 5) |
| llvm-mingw is one maintainer's project | releases stop | the pin keeps working; its scripts can rebuild the same toolchain from LLVM's and mingw-w64's sources |
| FSR | a developer build without FSR, or kept on MSVC's libraries | FSR is off for players |

**Recommendation: a prototype first.** The route removes every Microsoft licence from the player's
build (the libraries, the Windows SDK and the VC++ redistributable), downloads 171 MB instead of
940 MB for the compiler plus Microsoft's packages, and needs no administrator rights. The code
reading finds no blocker, but the risks that matter (the ABI at the COM boundary, setjmp, exports)
show only in a build and a run. The prototype of section 8 costs 2 to 3 h and one download; after a
pass the route costs about 12 to 25 h more, after a fail route (a), Microsoft's installer, stays the
fallback with its open licence question for players.
