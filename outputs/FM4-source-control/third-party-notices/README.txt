Motorsport DX: third-party notices

Motorsport DX is built from the project's own code, the ReXGlue SDK fork in work/runtime, and the
third-party code below. None of the third-party libraries is stored in this repository:
setup fetches them into the BuildRoot from their pinned upstream revisions. Each license file
in this folder is the upstream text as it stands at that revision. Build.ps1 -Action build and
-Action export copy this folder beside MotorsportDX.exe as third-party-notices, with
CREDITS.txt, which thanks the projects that made Motorsport DX possible; the launcher's About
page opens both.

The binaries a build produces, and what each one contains:
  rexruntime.dll    the runtime: ReXGlue and Xenia code, SDL3 (with the GameInput API v3
                    header and loader in its GameInput driver), Dear ImGui, fmt, spdlog, FFmpeg,
                    libmspack, toml++, xxHash, SIMDe, CLI11, utfcpp, o1heap and the vendored
                    files of Vendored-code-NOTICES.txt
  rexgpu-xenos.dll  the GPU plugin: ReXGlue and Xenia code, toml++, xxHash, DXBCChecksum, and
                    with REX_FSR (Build.ps1 -Fsr on) AMD FSR 3.1 and Microsoft's d3dx12.h
  fm4_rex.exe, fm4_rex_XMediaFacade_default.dll, fm4_rex_SpeechFacade_default.dll
                    code generated from the user's own discs, the project's integration code,
                    fmt and spdlog (linked statically), and header code of toml++ and SIMDe;
                    they call the rest of the runtime in rexruntime.dll
  MotorsportDX.exe  the GUI launcher: the project's code, SDL3, Dear ImGui and nlohmann/json
An export holds these binaries. The tools below stay in the BuildRoot and are never exported:
  rexglue.exe       the recompiler (build time only): ReXGlue code, inja, nlohmann/json, CLI11,
                    the GNU binutils PowerPC disassembler, toml++, fmt, spdlog, xxHash, SIMDe
  the test programs of build (fm4_instruction_tests and the others), FidelityFX_SC.exe and the
                    DirectX Shader Compiler of the FidelityFX SDK (with -Fsr on)

ReXGlue-LICENSE.txt
  The ReXGlue SDK, https://github.com/rexglue/rexglue-sdk (BSD-3-Clause, "Copyright (c) 2026,
  Tom Clay"), with portions derived from Xenia, https://xenia.jp ("Copyright (c) 2022, Ben
  Vanik and Xenia project contributors"): the runtime fork in work/runtime (from commit
  f5337cdc947ff6d4c4196737e2c807a48f2a1fc2), in every build and export. The resolve shaders in
  work/runtime/src/graphics/shaders/fm4-resolve keep Xenia's license in LICENSE.xenia there.

XenonRecomp-LICENSE.txt
  XenonRecomp, https://github.com/hedge-dev/XenonRecomp, MIT, "Copyright (c) 2025 hedge-dev
  and contributors" (its LICENSE.md, read on 26 October 2026). The ReXGlue SDK marks four files
  of the runtime fork as based on XenonRecomp and Unleashed Recompiled: include/rex/ppc/context.h,
  function.h and intrinsics.h (the PPC context, the function wrappers and the SIMD patterns,
  compiled into the runtime and the game) and src/codegen/config.cpp (in rexglue.exe). Those
  files carry only ReXGlue's notice; XenonRecomp's is kept here. Unleashed Recompiled is under
  the GPL 3.0, an open question for the ReXGlue SDK (docs/sources-and-licensing.md).

Libraries the runtime builds (the upstream SDK's submodules at that commit)
  SDL-LICENSE.txt              SDL 3 (3.5.0 development, commit 8bf3b72), zlib license.
                               Altered: setup applies outputs/FM4-build/sdl3-gameinput-v3.patch
                               (GameInput API v3 in SDL's GameInput driver), which marks what
                               it changes. https://github.com/libsdl-org/SDL
  DearImGui-LICENSE.txt        Dear ImGui 1.92.5 (commit 6d910d5), MIT, with its SDL3 and
                               DirectX 11 backends in the GUI launcher.
                               https://github.com/ocornut/imgui
  fmt-LICENSE.txt              {fmt} 12.1.0, MIT with an exception for object code.
                               https://github.com/fmtlib/fmt
  spdlog-LICENSE.txt           spdlog (commit 79524dd), MIT. https://github.com/gabime/spdlog
  FFmpeg-LICENSE.txt and FFmpeg-COPYING.LGPLv2.1.txt
                               FFmpeg's libavcodec and libavutil, from Xenia's fork
                               https://github.com/wmarti/FFmpeg (commit 0604b46, branch
                               xenia-ffmpeg-canary-full), configured for LGPL version 2.1 or
                               later (CONFIG_GPL 0: only the decoders mp3, mp3float, wmav2 and
                               xmaframes), linked statically into rexruntime.dll. See "LGPL"
                               below.
  libmspack-COPYING.LIB.txt    libmspack 1.11 (commit 3059077), only mspack/lzxd.c (LZX
                               decompression), "(C) 2003-2023 Stuart Caie", LGPL version 2.1,
                               linked statically into rexruntime.dll. See "LGPL" below.
                               https://github.com/kyz/libmspack
  tomlplusplus-LICENSE.txt     toml++ 3.4.0, MIT. https://github.com/marzer/tomlplusplus
  xxHash-LICENSE.txt           xxHash 0.8.3, BSD-2-Clause. https://github.com/Cyan4973/xxHash
  simde-COPYING.txt            SIMD Everywhere 0.8.2, MIT.
                               https://github.com/simd-everywhere/simde
  CLI11-LICENSE.txt            CLI11 (commit bfffd37), BSD-3-Clause style.
                               https://github.com/CLIUtils/CLI11
  inja-LICENSE.txt             inja 3.5.0, MIT; rexglue.exe only. https://github.com/pantor/inja
  nlohmann-json-LICENSE.txt    JSON for Modern C++ 3.11.3, MIT, from inja's third_party folder;
                               in MotorsportDX.exe and rexglue.exe.
                               https://github.com/nlohmann/json
  utfcpp-LICENSE.txt           UTF8-CPP (commit 63d64de), Boost Software License 1.0.
                               https://github.com/nemtrif/utfcpp
  o1heap-LICENSE.txt           o1heap 3.0.0, MIT. https://github.com/pavel-kirienko/o1heap

Files the SDK vendors in its thirdparty folder (mostly from Xenia)
  aes_128-LICENSE.txt          aes_128, "Copyright (c) 2017 LuoPeng", MIT.
                               https://github.com/openluopworld/aes_128
  des-LICENSE.txt              DES, "Copyright (c) 2014 Faraz Fallahi", MIT (crypto/des).
  stb-LICENSE.txt              stb_image 2.29, Sean Barrett, MIT or public domain (the
                               runtime's icon decoding). https://github.com/nothings/stb
  Vendored-code-NOTICES.txt    the notices of TinySHA1 (ISC style), SHA-256 by Stephan Brumme
                               (zlib), the Rijndael reference code (public domain),
                               DXBCChecksum (AMD; derived from the RSA Data Security, Inc. MD5
                               Message-Digest Algorithm), renderdoc_app.h (MIT), the DirectX
                               Shader Compiler API headers (University of Illinois/NCSA),
                               pe_image.h (Microsoft), disruptorplus (MIT), tiny-AES-c (The
                               Unlicense; not linked), the ProggyTiny font (MIT) and the AMD
                               FidelityFX CAS and FSR 1 shader bytecode of the presenter (MIT).

Microsoft.GameInput, in SDL's GameInput driver
  DirectXShaderCompiler-LICENSE-MS.txt, DirectXShaderCompiler-LICENSE-LLVM.txt,
  DirectXShaderCompiler-LICENCE-MIT.txt
    The DirectX Shader Compiler, release v1.9.2609 of 29 September 2026
    (https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.9.2609,
    dxc_2026_09_29.zip, SHA-256 ad31b1fc...a4a7e7f1): Microsoft's software license terms for
    the DirectX Shader Compiler, the University of Illinois/NCSA Open Source License of LLVM
    ("Copyright (c) 2003-2015 University of Illinois at Urbana-Champaign") and MIT ("Copyright
    (c) Microsoft Corporation"), the release's three license files. Its x64 dxcompiler.dll and
    dxil.dll compile the native path's DXIL shaders with native_shader_dxil
    (docs/analysis/d3d-hle-n6.md 19.3.9). They are never distributed by the project: setup
    takes them from the release zip in the BuildRoot (fetched from its URL only when it is
    missing) and the GUI launcher fetches the same release on the user's computer; the three
    license files stay beside the DLLs. No export carries any file of the release. The
    runtime loads them from beside the executable and compiles with FXC without them.

  Microsoft-GameInput-LICENSE.txt
    The Microsoft.GameInput package, version 3.5.283 (NuGet, SHA-512 59083faf...fe77705),
    "Copyright (c) Microsoft Corporation", MIT: its public header native/include/GameInput.h
    (GameInput API v3) and its loader source native/src/GameInput.cpp, the two files of the
    package that carry the MIT license in their header (README: "Updated public header and
    static library (lib) license to MIT", version 3.5). Setup fetches the package into the
    BuildRoot and extracts only these two files; the header is compiled into SDL's GameInput
    driver and the loader into SDL, which loads the GameInput runtime DLL that Windows or the
    GameInput redistributable installs. Nothing else of the package is used or shipped: not its
    older v0 to v2 headers, its static library, its redistributable installer
    (GameInputRedist.msi) or its LICENSE.txt, which are under Microsoft's software license terms
    for the GameInput redistributable. https://aka.ms/gameinput

AMD FidelityFX SDK, with REX_FSR (Build.ps1 -Fsr on)
  AMD-FidelityFX-SDK-LICENSE.txt
    The AMD FidelityFX SDK, v1.1.4 (commit c6efa6bf7f2027b3ec94f28578bb5965eabb9e55), MIT,
    "Copyright (C) 2024 Advanced Micro Devices, Inc." (the SDK's LICENSE.txt and
    sdk/LICENSE.txt, the same file; src/shared/ffx_message.cpp carries the year 2025).
    https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK
    Used: AMD FidelityFX Super Resolution 3.1 (the FSR 3 upscaler, version 3.1.4) and the SDK's
    DX12 backend, built from source into the runtime's rexgpu-xenos when the runtime is built
    with REX_FSR (Build.ps1 -Fsr on). No prebuilt AMD DLL is used.
  Microsoft-d3dx12-LICENSE.txt
    d3dx12.h, "Copyright (c) Microsoft Corporation", MIT: a header of the FidelityFX SDK's DX12
    backend (sdk/include/FidelityFX/host/backends/dx12/d3dx12.h, with its license.txt), compiled
    into the backend with REX_FSR.

Build-time tools that are not shipped
  The DirectX Shader Compiler of the FidelityFX SDK
    The FidelityFX SDK's shader compiler driver, FidelityFX_SC.exe (part of the SDK), compiles
    the FSR shader permutations at build time with the Microsoft DirectX Shader Compiler
    binaries the SDK keeps beside it in sdk/tools/binary_store: dxcompiler.dll (file version
    1.8.2403.37) and dxil.dll (file version 101.8.2403.24), "(c) Microsoft Corporation". They
    stay in the BuildRoot's SDK checkout: they are not in the repository, not in the runtime
    and not in an export, so their licenses are not reproduced here. The SDK ships no license
    file for them in binary_store; they come under the terms of Microsoft's DirectX Shader
    Compiler releases (https://github.com/microsoft/DirectXShaderCompiler). The runtime embeds
    the compiler's output, the shader bytecode, as headers generated at build time.
  The GNU binutils PowerPC disassembler
    thirdparty/disasm (ppc-dis.c and ppc.h, "Copyright 1994-2007 Free Software Foundation,
    Inc.", GNU General Public License version 2 or later) is linked into rexglue.exe, the
    recompiler that generate and build run in the BuildRoot. No binary of an export contains
    it. rexglue.exe must not be given to others without the GPL's terms (its source and the
    license text).

LGPL (FFmpeg and libmspack)
  rexruntime.dll links FFmpeg and libmspack statically. Whoever passes such a binary to
  someone else must, under the LGPL version 2.1, pass on this notice and the license text,
  offer the source of these libraries as built (the pinned revisions above, unmodified), and
  let the recipient relink the runtime with a changed library, for example by offering the
  runtime's source and build scripts (this repository and docs/setup.md). The project
  publishes source only and every user builds the binaries; an export contains code generated
  from the user's game discs and is not to be distributed in any case
  (docs/sources-and-licensing.md#distribution).
