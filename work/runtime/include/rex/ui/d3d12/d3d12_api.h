/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#pragma once

// clang - format off
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <ObjBase.h>

#if defined(__MINGW32__)
// mingw-w64 has no DXProgrammableCapture.h: the PIX capture interface as the Windows SDK
// declares it (docs/analysis/mingw-toolchain.md).
struct IDXGraphicsAnalysis : public IUnknown {
  STDMETHOD_(void, BeginCapture)() PURE;
  STDMETHOD_(void, EndCapture)() PURE;
};
__CRT_UUID_DECL(IDXGraphicsAnalysis, 0x9f251514, 0x9d4d, 0x4902, 0x9d, 0x60, 0x18, 0x98, 0x8a,
                0xb7, 0xd4, 0xb5)
#else
#include <DXProgrammableCapture.h>
#endif
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_5.h>
#include <dxgidebug.h>
// For Microsoft::WRL::ComPtr.
#include <wrl/client.h>
#include <dxcapi.h>
#include "DxbcConverter.h"
#if defined(__MINGW32__)
// DXC's headers name their interfaces with __declspec(uuid), which mingw-w64's __uuidof (and so
// IID_PPV_ARGS) does not read: the GUIDs of the interfaces the runtime asks for, from the same
// headers (docs/analysis/mingw-toolchain.md 1.1).
__CRT_UUID_DECL(IDxcBlob, 0x8ba5fb08, 0x5195, 0x40e2, 0xac, 0x58, 0x0d, 0x98, 0x9c, 0x3a, 0x01, 0x02)
__CRT_UUID_DECL(IDxcBlobUtf8, 0x3da636c9, 0xba71, 0x4024, 0xa3, 0x01, 0x30, 0xcb, 0xf1, 0x25, 0x30, 0x5b)
__CRT_UUID_DECL(IDxcIncludeHandler, 0x7f61fc7d, 0x950d, 0x467f, 0xb3, 0xe3, 0x3c, 0x02, 0xfb, 0x49, 0x18, 0x7c)
__CRT_UUID_DECL(IDxcCompiler, 0x8c210bf3, 0x011f, 0x4422, 0x8d, 0x70, 0x6f, 0x9a, 0xcb, 0x8d, 0xb6, 0x17)
__CRT_UUID_DECL(IDxcCompiler3, 0x228b4687, 0x5a6a, 0x4730, 0x90, 0x0c, 0x97, 0x02, 0xb2, 0x20, 0x3f, 0x54)
__CRT_UUID_DECL(IDxcUtils, 0x4605c4cb, 0x2019, 0x492a, 0xad, 0xa4, 0x65, 0xf2, 0x0b, 0xb7, 0xd6, 0x7f)
__CRT_UUID_DECL(IDxcResult, 0x58346cda, 0xdde7, 0x4497, 0x94, 0x61, 0x6f, 0x87, 0xaf, 0x5e, 0x06, 0x59)
__CRT_UUID_DECL(IDxcVersionInfo2, 0xfb6904c4, 0x42f0, 0x4b62, 0x9c, 0x46, 0x98, 0x3a, 0xf7, 0xda, 0x7c, 0x83)
__CRT_UUID_DECL(IDxbcConverter, 0x5f956ed5, 0x78d1, 0x4b15, 0x82, 0x47, 0xf7, 0x18, 0x76, 0x14, 0xa0, 0x41)
#endif

// clang-format on
