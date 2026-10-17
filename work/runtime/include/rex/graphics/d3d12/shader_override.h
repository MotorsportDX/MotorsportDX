/**
 * @file        graphics/d3d12/shader_override.h
 * @brief       Hand-written HLSL replacing the DXBC translation of guest shaders
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <rex/graphics/d3d12/shader.h>
#include <rex/graphics/pipeline/shader/dxbc_translator.h>
#include <rex/hash.h>

namespace rex::graphics::d3d12 {

// <shader_override_path>/shader_<ucode hash>[_<modification>].ps.hlsl replaces
// the DXBC translation of a guest pixel shader. The file defines
//   void xe_pixel_shader(inout XePixel p)
// and is compiled with FXC (ps_5_1, the translator's bindless root signature)
// after a contract describing the translation it replaces: its constants,
// descriptor slots, interpolators and outputs (see shader_override.hlsli). On
// any failure, or if its signatures don't match the translation's, the DXBC
// translation stays. Only the bindless host render target path is supported.
//
// Iteration aids, all off by default (docs/analysis/shader-mod-iteration.md):
// - shader_override_cache_path keeps the compiled DXBC on disk, keyed on the
//   source FXC sees (contract, library, override or generated HLSL, defines)
//   and checked against the content of every file it included.
// - shader_mod_hot_reload watches the files compiled into the overrides and
//   the override and mod directories, recompiles what changed and rebuilds only
//   the pipelines of those translations, without a restart. It also applies
//   <shader_mod_path>/shader_mod_defines.txt over shader_mod_defines, at the
//   start and whenever it is saved, like shader_mod_set.
// - shader_mod_live_knobs compiles the numeric shader_mod_defines as constants
//   after the system constants (xe_mod_knob), which shader_mod_set changes
//   while running without a recompile.
class ShaderOverrides {
 public:
  struct HostConfig {
    bool bindless_resources_used;
    bool edram_rov_used;
    bool gamma_render_target_as_unorm8;
    bool msaa_2x_supported;
    uint32_t draw_resolution_scale_x;
    uint32_t draw_resolution_scale_y;
  };

  // Live knobs: floats in the system constant buffer (b0) after
  // DxbcShaderTranslator::SystemConstants, which the translations don't read.
  static constexpr uint32_t kKnobCount = 32;
  static constexpr uint32_t kKnobsOffset =
      uint32_t((sizeof(DxbcShaderTranslator::SystemConstants) + 15) & ~size_t(15));
  static constexpr uint32_t kKnobsSize = kKnobCount * uint32_t(sizeof(float));

  // nullptr if shader_override_path is empty or the host path is unsupported.
  static std::unique_ptr<ShaderOverrides> Create(const HostConfig& config);
  ~ShaderOverrides();

  // Whether the guest pixel shader is overridden: by a file, by generated HLSL
  // (shader_override_generated), or by generated HLSL with a mod.
  bool HasPixelShader(uint64_t ucode_hash) const {
    return use_generated_ || pixel_shaders_.find(ucode_hash) != pixel_shaders_.end() ||
           mod_shaders_.find(ucode_hash) != mod_shaders_.end();
  }
  // Whether shader_override_ab_ms currently selects the DXBC translations.
  static bool IsAbOffPhase();

  // After a successful DXBC translation: if there is an override for it,
  // compiles it and replaces the translated binary. Pixel shaders without an
  // override file get HLSL from HlslShaderTranslator, written to
  // shader_override_generate_path and, with shader_override_generated, used. With
  // dump_shaders, writes the contract of every translated pixel shader there.
  void Apply(D3D12Shader::D3D12Translation& translation);

  // shader_mod_live_knobs: whether the system constant buffer carries the knobs.
  bool live_knobs() const { return live_knobs_; }
  // Copies the kKnobCount knob values to values_out if they changed since
  // generation (which is updated); returns whether they did.
  bool UpdateKnobs(float* values_out, uint64_t& generation) const;

  // shader_mod_set: NAME=VALUE pairs (separated by ';', spaces or lines) over
  // shader_mod_defines while running. Live knobs change at once; other defines
  // are recompiled at the next PollChanges with hot reload, and rejected
  // without it. Empty: logs the defines and the knob values.
  void SetDefines(std::string_view text);

  // Hot reload (shader_mod_hot_reload), on the thread that creates pipelines
  // while no pipeline is being created and no shader translated: if PollChanges
  // (which checks at most every kPollInterval) is true, NeedsReload and Reload
  // for every translated pixel shader (Reload is true if its binary changed),
  // then EndReload.
  bool hot_reload() const { return hot_reload_; }
  bool PollChanges();
  bool NeedsReload(const D3D12Shader::D3D12Translation& translation) const;
  bool Reload(D3D12Shader::D3D12Translation& translation);
  void EndReload();

 private:
  static constexpr std::chrono::milliseconds kPollInterval{250};
  // In shader_mod_path, with hot reload: defines over shader_mod_defines, '#'
  // to the end of a line a comment.
  static constexpr char kDefinesFile[] = "shader_mod_defines.txt";

  // A file read by a compile: as the source includes it, where it was found,
  // and the XXH3 of its content.
  struct Dependency {
    std::string name;
    std::filesystem::path path;
    uint64_t hash;
  };
  enum class Outcome {
    // No override for the translation.
    kNone,
    kCompiled,
    // An override, which didn't compile or match the translation.
    kFailed,
  };
  // What hot reload knows about a translation that has, or had, an override.
  struct Entry {
    // Its DXBC translation.
    std::vector<uint8_t> dxbc;
    // The files its last compile read (override file, mod, their includes).
    std::vector<std::filesystem::path> files;
    // Whether the binary is the override.
    bool overridden = false;
  };
  struct FileState {
    bool exists = false;
    std::filesystem::file_time_type time;
    uintmax_t size = 0;
    uint64_t hash = 0;
  };
  struct PathHasher {
    size_t operator()(const std::filesystem::path& path) const {
      return std::filesystem::hash_value(path);
    }
  };

  ShaderOverrides(const HostConfig& config, std::filesystem::path directory);

  std::string BuildContract(const D3D12Shader::D3D12Translation& translation) const;
  // The override of the translation (dxbc: its DXBC translation) from the
  // current files, and the files read for it.
  Outcome Build(const D3D12Shader::D3D12Translation& translation,
                const std::vector<uint8_t>& dxbc, std::vector<uint8_t>& binary_out,
                std::vector<Dependency>& files_out);
  bool Compile(const D3D12Shader::D3D12Translation& translation,
               const std::vector<uint8_t>& dxbc, const std::string& contract,
               const std::string& text, const std::string& name, const std::string& mod,
               std::vector<uint8_t>& binary_out, std::vector<Dependency>& files_out) const;

  // shader_override_cache_path.
  uint64_t CacheKey(const std::string& source,
                    const std::vector<std::pair<std::string, std::string>>& defines) const;
  std::filesystem::path CachePath(const D3D12Shader::D3D12Translation& translation,
                                  uint64_t key) const;
  bool LoadCached(const std::filesystem::path& path, uint64_t key,
                  std::vector<uint8_t>& binary_out, std::vector<Dependency>& files_out) const;
  void StoreCached(const std::filesystem::path& path, uint64_t key,
                   const std::vector<Dependency>& files, const std::vector<uint8_t>& binary) const;
  // The file an include of name resolves to, as OverrideInclude resolves it.
  std::filesystem::path ResolveInclude(const std::string& name) const;

  // The defines FXC gets: shader_mod_defines, with the live knobs as
  // xe_mod_knob(slot). Needs knob_mutex_.
  void RebuildCompileDefines();
  // Hot reload bookkeeping after Apply or Reload. Needs entries_mutex_.
  void Record(const D3D12Shader::D3D12Translation& translation, std::vector<uint8_t> dxbc,
              const std::vector<Dependency>& files, bool overridden);
  // Whether the content of the file changed, or it appeared or disappeared,
  // since state (updated); its content in text_out if it changed and exists.
  static bool RefreshFileState(const std::filesystem::path& path, FileState& state,
                               std::string* text_out = nullptr);

  HostConfig config_;
  std::filesystem::path directory_;
  std::string generate_path_;
  bool use_generated_ = false;
  // Changed only by PollChanges, while nothing is translated.
  std::unordered_set<uint64_t, rex::IdentityHasher<uint64_t>> pixel_shaders_;
  // Shader mods (shader_mod_path): shader_<hash>.mod.hlsli.
  std::filesystem::path mod_directory_;
  std::unordered_set<uint64_t, rex::IdentityHasher<uint64_t>> mod_shaders_;
  void* d3dcompiler_ = nullptr;
  void* d3d_compile_ = nullptr;

  // shader_override_cache_path; the cache key covers the library, and the
  // compiler by the size and time of its DLL.
  std::filesystem::path cache_directory_;
  uint64_t library_hash_ = 0;
  uint64_t compiler_id_ = 0;

  // Defines and live knobs.
  bool live_knobs_ = false;
  mutable std::mutex knob_mutex_;
  std::vector<std::pair<std::string, std::string>> mod_defines_;
  std::vector<std::pair<std::string, std::string>> compile_defines_;
  std::unordered_map<std::string, uint32_t> knob_slots_;
  float knob_values_[kKnobCount] = {};
  std::atomic<uint64_t> knob_generation_{1};
  // shader_mod_set defines waiting for a recompile.
  std::vector<std::pair<std::string, std::string>> pending_defines_;

  // Hot reload.
  bool hot_reload_ = false;
  std::chrono::steady_clock::time_point last_poll_;
  mutable std::mutex entries_mutex_;
  std::unordered_map<const D3D12Shader::D3D12Translation*, Entry> entries_;
  std::unordered_map<std::filesystem::path, FileState, PathHasher> files_;
  // Found by the last PollChanges.
  std::unordered_set<uint64_t, rex::IdentityHasher<uint64_t>> changed_shaders_;
  std::unordered_set<std::filesystem::path, PathHasher> changed_files_;
  bool defines_changed_ = false;
  FileState defines_file_;
};

}  // namespace rex::graphics::d3d12
