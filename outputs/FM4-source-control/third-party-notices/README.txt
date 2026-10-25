Third-party notices

The FM4-PC runtime contains, or is built with, the third-party code below. Each license file
in this folder is the upstream text. Build.ps1 -Action export copies this folder into the
export as third-party-notices.

ReXGlue-LICENSE.txt
  The ReXGlue SDK (BSD-3-Clause), with portions derived from Xenia: the runtime in
  work/runtime, in every build and export.

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

SDL-LICENSE.txt
  SDL 3 (Simple DirectMedia Layer), "Copyright (C) 1997-2026 Sam Lantinga", zlib license: the
  runtime's window, event and gamepad layer, built from source into the runtime from the
  pinned upstream SDK revision (SDL 3.5.0, revision 8bf3b7215). Altered: setup applies
  outputs/FM4-build/sdl3-gameinput-v3.patch (GameInput API v3 in SDL's GameInput driver), which
  marks what it changes. https://github.com/libsdl-org/SDL

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

The DirectX Shader Compiler (build time only, not shipped)
  The FidelityFX SDK's shader compiler driver, FidelityFX_SC.exe (part of the SDK), compiles
  the FSR shader permutations at build time with the Microsoft DirectX Shader Compiler
  binaries the SDK keeps beside it in sdk/tools/binary_store: dxcompiler.dll (file version
  1.8.2403.37) and dxil.dll (file version 101.8.2403.24), "(c) Microsoft Corporation". They
  stay in the BuildRoot's SDK checkout: they are not in the repository, not in the runtime
  and not in an export, so their licenses are not reproduced here. The SDK ships no license
  file for them in binary_store; they come under the terms of Microsoft's DirectX Shader
  Compiler releases (https://github.com/microsoft/DirectXShaderCompiler). The runtime embeds
  the compiler's output, the shader bytecode, as headers generated at build time.
