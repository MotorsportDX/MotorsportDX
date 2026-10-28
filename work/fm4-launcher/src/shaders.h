// The offline shader pass's side of the launcher (docs/analysis/d3d-hle-n6.md 19.3.9): the
// DirectX Shader Compiler release beside the game, fetched on this computer as setup's
// fetch_dxc does (never shipped, docs/sources-and-licensing.md), and the pass's progress.
#pragma once

#include <filesystem>
#include <string>

namespace fm4 {

// Whether dxcompiler.dll and dxil.dll are beside the game.
bool DxcPresent(const std::filesystem::path& game);

// The pinned release zip (pipeline.py DXC_URL and DXC_SHA256), from FM4_DXC_MIRROR when it is
// set (a local file or another URL), else from the release URL, checked against the pinned
// SHA-256, its two x64 DLLs and three license files extracted beside the game. work holds
// the download and the extraction until they are done. Returns an error, or empty.
std::string FetchDxc(const std::filesystem::path& game, const std::filesystem::path& work);

// The last line of the pass's shader_offline_pass_progress file.
struct PassProgress {
  std::string stage;
  long long done = 0;
  long long total = 0;
  long long errors = 0;
  bool finished = false;
};
bool ReadPassProgress(const std::filesystem::path& file, PassProgress& out);

}  // namespace fm4
