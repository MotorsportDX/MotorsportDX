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

#include <rex/graphics/d3d12/native_shader_contract.h>
#include <rex/graphics/d3d12/shader.h>
#include <rex/graphics/d3d12/shader_compile_queue.h>
#include <rex/graphics/pipeline/shader/dxbc_translator.h>
#include <rex/graphics/util/draw_scale.h>
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
// The post passes (post_pass.h) compile with the same compiler and cache, and
// take their defines and params from shader_mod_defines by name.
//
// On the native path, native_shader_contract reads <shader_mod_path>/shaders.toml
// (native_shader_contract.h, docs/analysis/shader-contract.md): a pixel shader
// with an entry there takes the entry's mod instead of its classic one, in the
// passes the entry lists, with the entry's knobs as constants and its draws'
// pass class in the host values.
//
// shader_override_async (on by default; docs/analysis/d3d-hle-n6.md 19.3.2,
// part 1): an override or mod that a draw meets and the cache does not hold is
// compiled on a worker of the pipeline cache's ShaderCompileQueue, not on the
// thread that issues the draw. The translation keeps its DXBC (the game's own
// look) until the pipeline cache swaps the compiled binary into it and its
// pipelines. The start-up's translations (InitializeShaderStorage) still
// compile before the title runs, and hot reload keeps compiling in place.
// Vertex shader keys (docs/analysis/shader-contract.md 9.3.1): the XDK writes
// the bound vertex layout's fetch instructions into a vertex shader's own
// microcode at the draw flush, so one shader of the title has a ucode key per
// layout. The fetch-blind key is the XXH3 of the microcode with the fields of
// every vertex fetch instruction that describe the layout zeroed (format,
// signedness, number format, signed mode, exponent adjust, stride, offset);
// the coarse key also zeroes the fetch constant, the prefetch count and both
// swizzles. Both are 0 for a shader without vertex fetch or not analyzed.
uint64_t VertexFetchBlindKey(const Shader& shader, bool coarse = false);
// native_shader_vertex_survey (9.4): logs each vertex shader once with its keys
// and fetches, each of its translations once with the host type and the
// modification, and for every key whose coarse key an earlier key had, the
// fetch instruction fields in which the two differ.
bool VertexSurveyEnabled();
void SurveyVertexTranslation(const D3D12Shader::D3D12Translation& translation);

class ShaderOverrides {
 public:
  struct HostConfig {
    bool bindless_resources_used;
    bool edram_rov_used;
    bool gamma_render_target_as_unorm8;
    bool msaa_2x_supported;
    uint32_t draw_resolution_scale_x;
    uint32_t draw_resolution_scale_y;
    // The scene's scale, for translations with the scene_scale modification
    // (docs/analysis/d3d-hle-n5.md 18.13).
    DrawScale scene_draw_scale;
    // The scene's input scale before the upscaler, with scene_input_scale too
    // (docs/analysis/fsr-plan.md 11).
    DrawScale scene_input_scale;
  };

  // Host values: one vector in the system constant buffer (b0) after
  // DxbcShaderTranslator::SystemConstants, which the translations don't read,
  // written for every draw while overrides are in use (16 bytes more than the
  // 592 of the system constants, within the same 768-byte placement). X: the
  // guest vblank rate in Hz (GraphicsSystem::guest_vblank_rate,
  // xe_guest_vblank_rate in the overrides); Y: the title's frame interval in
  // seconds (guest_frame_interval, xe_guest_frame_interval); Z: the frame rate
  // the title is designed to render its current scenario at
  // (guest_design_frame_rate, xe_guest_design_frame_rate); W: with
  // native_shader_contract, the pass class of the last draw of a pixel shader
  // with a manifest entry (xe_native_pass), else 0.
  static constexpr uint32_t kHostValuesOffset =
      uint32_t((sizeof(DxbcShaderTranslator::SystemConstants) + 15) & ~size_t(15));
  static constexpr uint32_t kHostValuesSize = 4 * uint32_t(sizeof(float));
  // Live knobs: floats after the host values. 64 of them take 256 bytes: with
  // the 608 before them, the buffer fits the 1024 bytes of four 256-byte
  // placement steps.
  static constexpr uint32_t kKnobCount = 64;
  static constexpr uint32_t kKnobsOffset = kHostValuesOffset + kHostValuesSize;
  static constexpr uint32_t kKnobsSize = kKnobCount * uint32_t(sizeof(float));

  // nullptr if shader_override_path is empty or the host path is unsupported.
  static std::unique_ptr<ShaderOverrides> Create(const HostConfig& config);
  ~ShaderOverrides();

  // Whether the guest pixel shader is overridden: by a file, by generated HLSL
  // (shader_override_generated), or by generated HLSL with a mod.
  bool HasPixelShader(uint64_t ucode_hash) const {
    return use_generated_ || pixel_shaders_.find(ucode_hash) != pixel_shaders_.end() ||
           mod_shaders_.find(ucode_hash) != mod_shaders_.end() ||
           (native_ && native_->FindPixel(ucode_hash));
  }
  // Whether shader_override_ab_ms currently selects the DXBC translations.
  static bool IsAbOffPhase();
  // Whether native_shader_contract_ab_ms currently selects the classic mods
  // over the manifest's entries.
  static bool IsNativeAbClassicPhase();

  // native_shader_contract: its manifest, nullptr when off.
  const native_shader_contract::Manifest* native_contract() const { return native_.get(); }
  // native_shader_pass_debug: entries tint their draws by pass class, and the
  // command processor logs the classes it gives.
  bool native_pass_debug() const { return native_pass_debug_; }
  // native_shader_contract_vertex_all: every vertex shader takes its generated
  // HLSL (docs/analysis/shader-contract.md 9.2.4).
  bool native_vertex_all() const { return native_vertex_all_; }

  // The translation of a native shader contract entry without the DXBC
  // translator (increment 2, docs/analysis/shader-contract.md): the game
  // shader's HLSL from HlslShaderTranslator, which also assigns the bindings
  // (stored in the shader if no translation stored any), with the entry's mod,
  // compiled with FXC through the override cache, its signatures checked
  // against those the DXBC translation would have. False if the translation is
  // not an entry's or any step failed: the caller translates with the DXBC
  // translator then.
  // With queue (shader_override_async, a draw meets the translation), an entry
  // that the cache does not hold is not compiled here: false is returned, the
  // caller's DXBC translation draws (the game's own look) and Apply queues the
  // entry's compile, which the pipeline cache swaps in as a mod's. The swap
  // keeps the pipelines' root signatures, which depend only on the shader's
  // bindings: the same for the entry's HLSL and the DXBC translation
  // (native_shader_contract_check compares them).
  bool TranslateEntry(D3D12Shader::D3D12Translation& translation,
                      ShaderCompileQueue* queue = nullptr);
  // native_shader_contract_check: whether to check the entries and the
  // bindings against DXBC translations.
  bool check() const { return check_; }
  // Every vertex shader translated, with vertex entries in the manifest: a
  // warning, once, for a key that the manifest does not list but whose
  // fetch-blind key (VertexFetchBlindKey) one of an entry's keys has, since the
  // XDK rewrote its fetches for a layout the entry does not list
  // (docs/analysis/shader-contract.md 9.3.1).
  void NoteVertexShader(const Shader& shader);
  // With check(): the bindings HlslShaderTranslator assigns to a pixel shader
  // against those its DXBC translation stored (every pixel shader translated
  // with the DXBC translator), and an entry's override against a DXBC
  // translation of the same modification (its signatures). Mismatches are
  // errors.
  void CheckBindings(const D3D12Shader::D3D12Translation& translation);
  void CheckEntry(const D3D12Shader::D3D12Translation& translation,
                  const std::vector<uint8_t>& dxbc);

  // After a successful DXBC translation: if there is an override for it,
  // compiles it and replaces the translated binary. Pixel shaders without an
  // override file get HLSL from HlslShaderTranslator, written to
  // shader_override_generate_path and, with shader_override_generated, used. With
  // dump_shaders, writes the contract of every translated pixel shader there.
  // With queue (shader_override_async, a draw meets the translation), a compile
  // the cache does not hold goes to the queue's workers, and the translation
  // keeps its DXBC until the pipeline cache swaps the compiled binary in.
  void Apply(D3D12Shader::D3D12Translation& translation, ShaderCompileQueue* queue = nullptr);

  // Whether the system constant buffer carries the knobs: with
  // shader_mod_live_knobs, or for the params of the manifest's entries.
  bool live_knobs() const { return live_knobs_ || entry_knobs_; }
  // Copies the kKnobCount knob values to values_out if they changed since
  // generation (which is updated); returns whether they did.
  bool UpdateKnobs(float* values_out, uint64_t& generation) const;

  // shader_mod_set: NAME=VALUE pairs (separated by ';', spaces or lines) over
  // shader_mod_defines while running. Live knobs and post pass names change at
  // once; other defines are recompiled at the next PollChanges with hot
  // reload, and rejected without it. Empty: logs the defines and the knob
  // values.
  void SetDefines(std::string_view text);

  // A file read by a compile: as the source includes it, where it was found,
  // and the XXH3 of its content.
  struct Dependency {
    std::string name;
    std::filesystem::path path;
    uint64_t hash;
  };
  struct FileState {
    bool exists = false;
    std::filesystem::file_time_type time;
    uintmax_t size = 0;
    uint64_t hash = 0;
  };
  // Whether the content of the file changed, or it appeared or disappeared,
  // since state (updated); its content in text_out if it changed and exists.
  static bool RefreshFileState(const std::filesystem::path& path, FileState& state,
                               std::string* text_out = nullptr);

  // Post passes (post_pass.cpp) share the compiler, the DXBC cache and the
  // defines of the shader mods.
  // Compiles source, named name, for target with FXC and the flags and defines
  // given (not shader_mod_defines): the include handler serves library as
  // library_name and other includes from directory. With
  // shader_override_cache_path, keeps the DXBC there. files_out lists the files
  // read, also of a failed compile.
  bool CompileProgram(const std::string& source, const std::string& name,
                      const std::string& entry, const char* target, uint32_t flags,
                      const std::vector<std::pair<std::string, std::string>>& defines,
                      const std::filesystem::path& directory, const char* library_name,
                      std::string_view library, std::vector<uint8_t>& binary_out,
                      std::vector<Dependency>& files_out) const;
  // The value of a define after shader_mod_set and shader_mod_defines.txt.
  bool GetDefine(const std::string& name, std::string& value_out) const;
  // Changes whenever a define's value changes.
  uint64_t defines_generation() const {
    return defines_generation_.load(std::memory_order_acquire);
  }
  // Names of post pass defines and params: shader_mod_set applies them at
  // once, and they are neither defines nor live knobs of the mods.
  void SetPassNames(std::vector<std::string> names);
  // The textures the post passes expose to mods, by name with their slot
  // (xe_post_texture): a native shader contract entry reads them by name
  // (srv = ["post:<name>"], xe_srv_<name>).
  void SetPostTextures(std::vector<std::pair<std::string, uint32_t>> slots);

  // The look directory's textures that the native shader contract's entries
  // read (srv = ["file:<name>"]): <name>.png decoded to 8-bit RGBA at start-up,
  // in the order of D3D12CommandProcessor's look file views (xe_look_file).
  // A file that is missing, too large or not an 8-bit RGB or RGBA PNG is one
  // texel of 0.5 gray, alpha 1, with an error in the log.
  struct LookFile {
    std::string name;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
  };
  const std::vector<LookFile>& look_files() const { return look_files_; }
  // Frees a look file's texels once the command processor has copied them into
  // its upload buffer; the name and the size stay for the contract.
  void ReleaseLookFileTexels(size_t index) {
    std::vector<uint8_t>().swap(look_files_[index].rgba);
  }

  // Hot reload (shader_mod_hot_reload), on the thread that creates pipelines
  // while no pipeline is being created and no shader translated: if PollChanges
  // (which checks at most every kPollInterval) is true, NeedsReload and Reload
  // for every translated pixel shader (Reload is true if its binary changed),
  // then EndReload.
  bool hot_reload() const { return hot_reload_; }
  // An override or mod file calls xe_post_mark_reactive: the post passes then bind and clear
  // their reactive mark texture every frame (otherwise it is neither bound nor cleared).
  bool uses_reactive_mark() const { return uses_reactive_mark_; }
  bool PollChanges();
  bool NeedsReload(const D3D12Shader::D3D12Translation& translation) const;
  bool Reload(D3D12Shader::D3D12Translation& translation);
  void EndReload();

 private:
  static constexpr std::chrono::milliseconds kPollInterval{250};
  // In shader_mod_path, with hot reload: defines over shader_mod_defines, '#'
  // to the end of a line a comment.
  static constexpr char kDefinesFile[] = "shader_mod_defines.txt";

  enum class Outcome {
    // No override for the translation.
    kNone,
    kCompiled,
    // An override, which didn't compile or match the translation.
    kFailed,
    // Queued for a compile worker (shader_override_async).
    kQueued,
  };
  // An FXC compile of an override or mod: its source and defines, the DXBC
  // translation whose signatures it must match (empty for an entry translated
  // without it: the shader's expected signatures), and its cache file.
  struct CompileJob {
    const Shader* shader = nullptr;
    uint64_t modification = 0;
    std::vector<uint8_t> dxbc;
    std::string source;
    std::string name;
    std::string mod;
    std::vector<std::pair<std::string, std::string>> defines;
    uint64_t key = 0;
    std::filesystem::path cache_path;
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
  struct PathHasher {
    size_t operator()(const std::filesystem::path& path) const {
      return std::filesystem::hash_value(path);
    }
  };

  ShaderOverrides(const HostConfig& config, std::filesystem::path directory);

  // The signature check of an override of the shader's modification: against
  // the DXBC translation, or with dxbc empty against the signatures it would
  // have. Empty if they match.
  std::string CheckSignatures(const Shader& shader, uint64_t modification,
                              const std::vector<uint8_t>& dxbc,
                              const std::vector<uint8_t>& binary) const;
  // entry: the manifest's entry the translation takes, or nullptr (for a
  // vertex shader, vertex_entry).
  std::string BuildContract(const D3D12Shader::D3D12Translation& translation,
                            const native_shader_contract::PixelEntry* entry,
                            const native_shader_contract::VertexEntry* vertex_entry = nullptr) const;
  std::string BuildVertexContract(const D3D12Shader::D3D12Translation& translation,
                                  uint32_t register_count) const;
  // Whether the translation has every input a replacement entry names (an
  // interpolator of its mask, a float constant its shader reads, a texture it
  // fetches): empty if so, else the first that it lacks.
  std::string CheckInputs(const D3D12Shader::D3D12Translation& translation,
                          const native_shader_contract::PixelEntry& entry) const;
  // The override of the translation (dxbc: its DXBC translation, empty for
  // an entry translated without it) from the current files, and the files read
  // for it.
  // With job_out, a compile the cache does not hold is returned there for a
  // worker (kQueued).
  Outcome Build(const D3D12Shader::D3D12Translation& translation,
                const std::vector<uint8_t>& dxbc, std::vector<uint8_t>& binary_out,
                std::vector<Dependency>& files_out,
                std::unique_ptr<CompileJob>* job_out = nullptr);
  // With job_out, a compile the cache does not hold is not run: its job is
  // returned there (false is returned, job_out set).
  bool Compile(const D3D12Shader::D3D12Translation& translation,
               const std::vector<uint8_t>& dxbc, const std::string& contract,
               const std::string& text, const std::string& name, const std::string& mod,
               const std::vector<std::string>* params,
               std::vector<uint8_t>& binary_out, std::vector<Dependency>& files_out,
               std::unique_ptr<CompileJob>* job_out = nullptr) const;
  // The manifest's entry a translation takes (not in the A/B's classic periods,
  // outside the entry's passes, or in the shader override A/B's off phase), or
  // nullptr.
  const native_shader_contract::PixelEntry* NativeEntry(
      const D3D12Shader::D3D12Translation& translation) const;
  // The same for a vertex shader's translation (not outside the entry's passes
  // or in the A/B's other periods).
  const native_shader_contract::VertexEntry* NativeVertexEntry(
      const D3D12Shader::D3D12Translation& translation) const;
  // Whether a vertex shader's translation takes its generated HLSL: a vertex
  // shader of the host vertex stage without memexport, with an entry or
  // native_shader_contract_vertex_all, not in native_plain or native_classic.
  bool GeneratesVertex(const D3D12Shader::D3D12Translation& translation) const;
  // Build for a vertex shader: its generated HLSL with the entry's hooks.
  Outcome BuildVertex(const D3D12Shader::D3D12Translation& translation,
                      const std::vector<uint8_t>& dxbc, std::vector<uint8_t>& binary_out,
                      std::vector<Dependency>& files_out,
                      std::unique_ptr<CompileJob>* job_out = nullptr);
  // FXC, the check against the signatures of dxbc and the cache store of a job
  // (on_worker: a compile worker runs it).
  bool RunCompile(const CompileJob& job, const std::vector<uint8_t>& dxbc,
                  std::vector<uint8_t>& binary_out, std::vector<Dependency>& files_out,
                  bool on_worker) const;

  // shader_override_cache_path.
  uint64_t CacheKey(const std::string& source,
                    const std::vector<std::pair<std::string, std::string>>& defines) const;
  std::filesystem::path CachePath(const D3D12Shader::D3D12Translation& translation,
                                  uint64_t key) const;
  // Includes resolve in directories, as OverrideInclude resolves them.
  bool LoadCached(const std::filesystem::path& path, uint64_t key,
                  const std::vector<std::filesystem::path>& directories,
                  std::vector<uint8_t>& binary_out, std::vector<Dependency>& files_out) const;
  void StoreCached(const std::filesystem::path& path, uint64_t key,
                   const std::vector<Dependency>& files, const std::vector<uint8_t>& binary) const;
  // The file an include of name resolves to, as OverrideInclude resolves it.
  static std::filesystem::path ResolveInclude(
      const std::string& name, const std::vector<std::filesystem::path>& directories);

  // The defines FXC gets: shader_mod_defines, with the live knobs as
  // xe_mod_knob(slot), and the knob slots of the entries' params. Needs
  // knob_mutex_.
  void RebuildCompileDefines();
  // Hot reload bookkeeping after Apply or Reload. Needs entries_mutex_.
  void Record(const D3D12Shader::D3D12Translation& translation, std::vector<uint8_t> dxbc,
              const std::vector<Dependency>& files, bool overridden);
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

  // native_shader_contract: the manifest, and whether its entries have params.
  std::unique_ptr<native_shader_contract::Manifest> native_;
  bool entry_knobs_ = false;
  // native_shader_pass_debug: entries tint their colour by pass class.
  bool native_pass_debug_ = false;
  bool native_vertex_all_ = false;
  // native_shader_contract_check, and its counts: translations whose
  // bindings were compared, the shaders among them (logged once each), the
  // translations whose bindings differ, those without HLSL (an unsupported
  // fetch or memexport), the entries checked.
  bool check_ = false;
  std::mutex check_mutex_;
  struct CheckCounts {
    std::unordered_set<uint64_t, rex::IdentityHasher<uint64_t>> shaders;
    uint32_t bindings = 0;
    uint32_t mismatches = 0;
    uint32_t without_hlsl = 0;
  };
  // Pixel shaders, then vertex shaders.
  CheckCounts checked_[2];
  // NoteVertexShader: the keys seen, the fetch-blind keys of the entries' keys
  // seen (with the key), and the unlisted keys by fetch-blind key.
  std::mutex vertex_keys_mutex_;
  std::unordered_set<uint64_t, rex::IdentityHasher<uint64_t>> vertex_keys_seen_;
  std::unordered_map<uint64_t, uint64_t> vertex_entry_blind_keys_;
  std::unordered_map<uint64_t, std::vector<uint64_t>> vertex_unlisted_keys_;
  std::atomic<uint32_t> checked_entries_{0};
  // Entries translated with the DXBC translator after all (TranslateEntry
  // failed), each logged with its reason.
  std::atomic<uint32_t> entry_fallbacks_{0};
  // Entries that a draw met before the cache held them (shader_override_async):
  // the DXBC translation until a worker's compile is swapped in.
  std::atomic<uint32_t> entry_placeholders_{0};
  // The look files' knob defaults (#ifndef NAME / #define NAME value), for the
  // params that shader_mod_defines leaves out.
  std::unordered_map<std::string, std::string> knob_defaults_;

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
  // Post pass names (SetPassNames).
  std::unordered_set<std::string> pass_names_;
  // The post passes' textures with a slot (SetPostTextures); empty without
  // post passes.
  std::unordered_map<std::string, uint32_t> post_texture_slots_;
  // The entries' look files (look_files), loaded with the manifest.
  std::vector<LookFile> look_files_;
  void LoadLookFiles();
  std::atomic<uint64_t> defines_generation_{1};

  // Hot reload.
  bool hot_reload_ = false;
  bool uses_reactive_mark_ = false;
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
