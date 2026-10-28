/**
 * @file        graphics/d3d12/dxil_compiler.h
 * @brief       DXC for the native path's DXIL shaders (docs/analysis/d3d-hle-n6.md 19.3.9)
 *
 * The DirectX Shader Compiler is Microsoft's (docs/sources-and-licensing.md) and is not in the
 * repository: the runtime loads the release's dxcompiler.dll from beside the executable, where
 * setup or the launcher puts it with dxil.dll, which signs what it compiles.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rex/ui/d3d12/d3d12_api.h>

namespace rex::graphics::d3d12 {

class DxilCompiler {
 public:
  // dxcompiler.dll and dxil.dll from the executable's directory, or null (with
  // a log line) when either is missing or does not load.
  static std::unique_ptr<DxilCompiler> Load();
  ~DxilCompiler();

  DxilCompiler(const DxilCompiler&) = delete;
  DxilCompiler& operator=(const DxilCompiler&) = delete;

  // For cache keys: the version and the commit the release reports.
  uint64_t identity() const { return identity_; }
  const std::string& version() const { return version_; }

  // Compiles HLSL to signed DXIL for target (vs_6_0, ps_6_0) with the language
  // of 2018 and, with strict, IEEE strictness (-Gis), as FXC's flags compile
  // the shader overrides. include resolves every #include (FXC's interface, so
  // the overrides' include handler serves both compilers). Thread-safe: each
  // call creates its own compiler object.
  bool Compile(std::string_view source, const std::string& name, const char* target,
               const std::vector<std::pair<std::string, std::string>>& defines,
               ID3DInclude* include, bool strict, std::vector<uint8_t>& binary_out,
               std::string& messages_out) const;

 private:
  DxilCompiler() = default;

  HMODULE dxcompiler_ = nullptr;
  HMODULE dxil_ = nullptr;
  DxcCreateInstanceProc create_instance_ = nullptr;
  uint64_t identity_ = 0;
  std::string version_;
};

}  // namespace rex::graphics::d3d12
