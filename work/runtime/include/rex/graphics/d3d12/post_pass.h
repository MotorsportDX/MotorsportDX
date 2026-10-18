/**
 * @file        graphics/d3d12/post_pass.h
 * @brief       Post passes: screen passes injected into the guest frame, from
 *              HLSL files and a manifest in post_pass_path
 *
 * <post_pass_path>/passes.toml lists the passes, their textures and their
 * params; each pass is an entry point of an HLSL file there, compiled with FXC
 * (compute or pixel) against the library post_pass.hlsli. The passes run at
 * injection points of the guest frame:
 * - scene_depth: right after the guest resolves the last tile of its scene
 *   depth, before its post-processing;
 * - scene_color: right after it resolves the last tile of its scene color;
 * - final: after the draws of the final combine pixel shaders of the manifest,
 *   before the next draw (the HUD), on the render target they drew into.
 * Inputs are the scene depth, its stencil and the scene color, the previous
 * frame's scene color and depth (kept only if a pass reads them, dropped on
 * camera cuts, resolution changes and frames without a scene), the final color,
 * and the textures of earlier passes. Textures with a slot are bound in the
 * bindless heap at SystemBindlessView::kPostPassViewsStart + slot for shader
 * mods (xe_post_texture in shader_override.hlsli). The scene camera (the
 * projection and, where the vertex shader carries it, the view) comes from the
 * constants of the vertex shaders the manifest names; so do the world matrices
 * of the objects whose draws write an object ID into the stencil ([motion]),
 * from which the passes get each object's reprojection into the previous frame
 * (motion vectors).
 *
 * Params and defines come from shader_mod_defines by name, and shader_mod_set
 * changes them while running (params at once, defines with a recompile). With
 * shader_mod_hot_reload, a saved manifest or pass file is reloaded. Off unless
 * post_pass_path is set; RTV path with bindless resources only. See
 * docs/analysis/post-pass-framework.md.
 */

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <rex/graphics/d3d12/post_pass_camera.h>
#include <rex/graphics/d3d12/post_pass_layout.h>
#include <rex/graphics/d3d12/shader_override.h>
#include <rex/graphics/register_file.h>
#include <rex/graphics/xenos.h>
#include <rex/ui/d3d12/d3d12_api.h>

namespace rex::graphics::d3d12 {

class D3D12CommandProcessor;

class PostPasses {
 public:
  // nullptr unless post_pass_path is set and the host can run passes (bindless
  // resources, host render targets, shader overrides for FXC).
  static std::unique_ptr<PostPasses> Create(D3D12CommandProcessor& command_processor);
  ~PostPasses();

  // Every draw, before its render targets are bound: keeps the scene camera and
  // the world matrices of the motion objects, logs post_pass_log_constants and
  // post_pass_motion_check, and runs the final point at the first draw after
  // the final combine.
  void OnDraw(const RegisterFile& regs, uint64_t vertex_shader_hash, uint64_t pixel_shader_hash);
  // Before every resolve: runs a final point still pending.
  void OnBeforeResolve();
  // After every resolve (address and length of the written range): tracks the
  // scene depth and color, and runs the scene points when they are complete.
  void OnResolve(const RegisterFile& regs, uint32_t address, uint32_t length);
  // In the swap, in an open submission: clears the exposed outputs of the
  // frame, keeps or drops the history, and reloads changed files.
  void OnSwap();

 private:
  enum class Point : uint32_t { kSceneDepth, kSceneColor, kFinal, kCount };
  // Inputs and outputs that are not textures of the manifest.
  enum class Builtin : uint32_t {
    kNone,
    kSceneDepth,
    // The stencil byte of the scene depth resolve, an R8G8B8A8_UINT view of it
    // (the stencil in R).
    kSceneStencil,
    kSceneColor,
    kPreviousColor,
    kPreviousDepth,
    kFinalColor,
  };
  static constexpr uint32_t kMaxMips = 8;
  struct Binding {
    Builtin builtin = Builtin::kNone;
    uint32_t texture = UINT32_MAX;
    // A single mip, or -1 for all of them (SRVs) or mip 0.
    int32_t mip = -1;
  };
  struct Condition {
    std::string name;
    enum class Op { kEqual, kNotEqual, kLess, kLessEqual, kGreater, kGreaterEqual } op;
    double value;
  };
  struct ParamDesc {
    std::string name;
    bool is_uint = false;
    double value = 0.0;
    double min = -1.0e30, max = 1.0e30;
    // A view length in host pixels at W = 1: that param times the focal
    // length of the scene.
    uint32_t focal_length_of = UINT32_MAX;
  };
  struct TextureDesc {
    std::string name;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    uint32_t mips = 1;
    float scale = 1.0f;
    // Sized like the final color rather than the scene depth.
    bool final_size = false;
    int32_t slot = -1;
    bool clear = false;
    // Usage by the passes.
    bool uav = false, rtv = false;
  };
  enum class Blend : uint32_t { kNone, kAlpha, kAdd, kMultiply };
  struct PassDesc {
    std::string name;
    Point point = Point::kSceneDepth;
    bool pixel = false;
    std::string shader, entry;
    std::vector<std::pair<std::string, std::string>> defines;
    std::vector<Binding> srvs, uavs, rtvs;
    uint32_t group[2] = {8, 8};
    Binding size;
    std::vector<Condition> conditions;
    Blend blend = Blend::kNone;
    std::vector<uint8_t> bytecode;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    // Pixel passes: the render target formats of pipeline.
    std::array<DXGI_FORMAT, post_pass::kMaxRenderTargets> pipeline_formats = {};
  };
  struct CameraSource {
    uint64_t vertex_shader_hash;
    post_pass::CameraLayout layout;
    uint32_t constants[3];
  };
  // A vertex shader of the motion objects ([motion] objects): where its world
  // matrix is. Its index in the list is its priority (0 first).
  struct MotionSource {
    uint64_t vertex_shader_hash;
    post_pass::WorldLayout layout;
    uint32_t constant;
  };
  static constexpr uint32_t kMaxMotionSources = 32;
  struct Manifest {
    std::vector<CameraSource> camera_sources;
    uint32_t min_width = 1024;
    std::vector<uint64_t> final_pixel_shaders;
    float cut_distance = 5.0f, cut_angle = 30.0f, cut_focal = 0.1f;
    // [motion]: the slot of a stencil value s is round(s / stencil_step) below
    // stencil_limit, else 0; slot_count from both (at most kMotionSlots).
    std::vector<MotionSource> motion_sources;
    uint32_t motion_stencil_step = 1, motion_stencil_limit = 256;
    uint32_t motion_slot_count = 1;
    // Name and the value of the manifest.
    std::vector<std::pair<std::string, std::string>> defines;
    std::vector<ParamDesc> params;
    std::vector<TextureDesc> textures;
    std::vector<PassDesc> passes;
    bool uses_point[size_t(Point::kCount)] = {};
    bool uses_scene_color = false, uses_previous_color = false, uses_previous_depth = false;
    bool uses_scene_stencil = false;
    // The defines as compiled (after shader_mod_defines).
    std::vector<std::pair<std::string, std::string>> compiled_defines;
    std::vector<uint8_t> vertex_shader;
    // The manifest and every file compiled (hot reload).
    std::vector<ShaderOverrides::Dependency> files;
  };
  struct Texture {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    uint32_t width = 0, height = 0;
    // Bindless views: all mips, and each mip.
    uint32_t srv = UINT32_MAX;
    std::array<uint32_t, kMaxMips> srv_mips, uavs;
    // In rtv_heap_ and cpu_heap_ (for clearing).
    std::array<uint32_t, kMaxMips> rtvs;
    uint32_t clear_uav = UINT32_MAX;
    bool written = false;
    Texture() {
      srv_mips.fill(UINT32_MAX);
      uavs.fill(UINT32_MAX);
      rtvs.fill(UINT32_MAX);
    }
  };
  // The scene camera of a frame.
  struct Camera {
    bool valid = false;
    uint32_t depth_edram_base = 0;
    uint64_t frame = 0;
    post_pass::Unpack unpack = {};
    float guest_origin = 0.0f;
    // For the log: ndc_z = q - q_near / view z of a Direct3D style projection.
    float q = 0.0f, q_near = 0.0f;
    // Clip = projection * view position (post_pass_motion_check).
    post_pass::Matrix projection = {};
    bool view_known = false;
    uint64_t view_frame = UINT64_MAX;
    post_pass::Matrix view_to_world = {};
  };
  // A motion object of a frame: the world matrix of the draw of the highest
  // priority source with its stencil reference.
  struct MotionObject {
    uint64_t frame = UINT64_MAX;
    uint32_t source = UINT32_MAX;
    uint32_t reference = 0;
    post_pass::MatrixD world = {};
    // The sources that drew the object in the frame (their first world matrix
    // is in motion_first_worlds_, for the log).
    uint32_t sources_drawn = 0;
  };
  // The motion constants of a frame (SceneConstants::motion_*), computed at
  // its first point.
  struct Motion {
    uint64_t frame = UINT64_MAX;
    bool valid = false;
    uint32_t slots_own = 0;
    uint32_t slots_drawn = 0;
    float view_to_previous_view[post_pass::kMotionSlots][3][4] = {};
  };
  // post_pass_motion_check: a guest pixel shader holding its own reprojection
  // matrices, logged next to the host's.
  struct MotionCheck {
    uint64_t pixel_shader_hash;
    uint32_t constant, slots;
    uint64_t next_frame;
  };
  // A pixel shader float constant to log (post_pass_log_constants).
  struct LoggedConstant {
    uint64_t pixel_shader_hash;
    uint32_t constant;
    uint64_t next_frame;
  };
  // One texture written by contiguous resolves in a frame (the scene's tiles).
  struct ResolveGroup {
    uint64_t frame = UINT64_MAX;
    uint32_t base = 0, end = 0;
    uint32_t pitch = 0, height = 0;
    uint32_t edram_base = 0;
    bool done = false;
    // Depth: kD24FS8 or kD24S8; color: the destination's texture format.
    uint32_t format = 0;
    xenos::Endian128 endianness = xenos::Endian128::k8in32;
  };
  // A guest texture read through the texture cache, for one point run.
  struct External {
    ID3D12Resource* resource = nullptr;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    uint32_t width = 0, height = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE srv = {};
  };
  struct History {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    uint32_t srv = UINT32_MAX;
  };
  // Descriptors of released textures, freed once the GPU is past them.
  enum class DescriptorKind : uint32_t { kBindless, kRtv, kCpu };
  struct PendingDescriptor {
    DescriptorKind kind;
    uint32_t index;
    uint64_t submission;
  };

  explicit PostPasses(D3D12CommandProcessor& command_processor, ShaderOverrides& overrides);
  bool Initialize();

  // Manifest.
  bool LoadManifest(Manifest& manifest_out);
  bool CompileManifest(Manifest& manifest);
  std::vector<std::pair<std::string, std::string>> ResolveDefines(const Manifest& manifest) const;
  void Reload(const char* reason);
  void PollFiles();

  // Scene tracking.
  bool IsSceneDepthBuffer(const RegisterFile& regs, uint32_t& depth_edram_base_out) const;
  void CaptureCamera(const RegisterFile& regs, const CameraSource& source);
  void CaptureObject(const RegisterFile& regs, uint32_t source_index);
  // The motion slot of a stencil value (0: the static world).
  uint32_t MotionSlot(uint32_t stencil) const;
  void LogConstants(const RegisterFile& regs, uint64_t pixel_shader_hash);
  // Motion.
  void UpdateMotion();
  // An object's own view_to_previous_view, if it was captured in both frames
  // and did not jump.
  bool ObjectReprojection(uint32_t slot, const post_pass::MatrixD& view_to_world,
                          const post_pass::MatrixD& previous_view_to_world,
                          post_pass::MatrixD& out) const;
  void LogMotionCheck(const RegisterFile& regs, MotionCheck& check);
  std::string MotionSummary() const;
  bool TrackGroup(ResolveGroup& group, uint32_t address, uint32_t length, uint32_t pitch,
                  uint32_t height, uint32_t edram_base, uint64_t frame);
  // The scene color of the frame is complete, after its scene depth.
  void OnSceneColor();
  bool IsCameraCut() const;

  // Runs.
  bool IsSkipped(int32_t* variant_index_out);
  void UpdateParams(int32_t variant_index);
  // The pass's conditions on the current params and defines.
  bool PassEnabled(const PassDesc& pass) const;
  void FillSceneConstants(post_pass::SceneConstants& constants) const;
  void RunPoint(Point point);
  enum class ExternalKind { kColor, kDepth, kStencil };
  bool RequestExternal(const ResolveGroup& group, ExternalKind kind, External& out);
  void RunPass(PassDesc& pass, D3D12_GPU_VIRTUAL_ADDRESS scene_constants);
  bool BindingSize(const Binding& binding, uint32_t& width, uint32_t& height) const;
  void CopyHistory();
  void LogRun(int32_t variant_index);

  // Resources.
  bool EnsureTexture(uint32_t index);
  void ReleaseTexture(Texture& texture, const TextureDesc* desc);
  void ReleaseTextures();
  bool EnsureHistory(History& history, ID3D12Resource* source,
                     const D3D12_SHADER_RESOURCE_VIEW_DESC& srv_desc);
  void ReleaseHistory(History& history);
  void Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES& state,
                  D3D12_RESOURCE_STATES new_state);
  D3D12_GPU_DESCRIPTOR_HANDLE Gpu(uint32_t descriptor) const;
  // A view of the bindless heap, and of rtv_heap_ or cpu_heap_.
  D3D12_CPU_DESCRIPTOR_HANDLE Cpu(uint32_t descriptor) const;
  D3D12_CPU_DESCRIPTOR_HANDLE LocalCpu(DescriptorKind kind, uint32_t descriptor) const;
  uint32_t AllocateLocal(DescriptorKind kind);
  void ReleaseDescriptor(DescriptorKind kind, uint32_t& index);
  void ReleasePendingDescriptors(bool all);
  void WriteSlotView(int32_t slot, ID3D12Resource* resource, DXGI_FORMAT format);
  ID3D12PipelineState* PixelPipeline(PassDesc& pass,
                                     const std::array<DXGI_FORMAT, post_pass::kMaxRenderTargets>&
                                         formats);

  D3D12CommandProcessor& command_processor_;
  ShaderOverrides& overrides_;
  std::filesystem::path directory_;
  std::unique_ptr<Manifest> manifest_;
  bool hot_reload_ = false;
  std::chrono::steady_clock::time_point last_poll_, start_time_;
  ShaderOverrides::FileState manifest_file_;
  std::vector<std::pair<std::filesystem::path, ShaderOverrides::FileState>> watched_;
  uint64_t defines_generation_ = 0;
  std::vector<LoggedConstant> logged_constants_;
  std::vector<MotionCheck> motion_checks_;

  Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
  // Pipelines replaced by a reload, released once the GPU is past them.
  std::vector<std::pair<uint64_t, Microsoft::WRL::ComPtr<ID3D12PipelineState>>> retired_;

  // Local descriptor heaps: render target views, and non-shader-visible views
  // for ClearUnorderedAccessViewUint.
  static constexpr uint32_t kLocalDescriptorCount = 128;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_, cpu_heap_;
  uint32_t rtv_increment_ = 0, cpu_increment_ = 0;
  std::vector<uint32_t> free_rtvs_, free_cpu_;
  std::vector<PendingDescriptor> pending_descriptors_;

  std::vector<Texture> textures_;
  // Per param, the value in the pass constants (b1).
  std::array<uint32_t, post_pass::kMaxParams> param_values_ = {};
  bool params_valid_ = false;
  uint64_t params_generation_ = 0;
  int32_t params_variant_ = -2;

  // The frame being drawn.
  Camera camera_;
  ResolveGroup depth_group_, color_group_;
  // The scene of this frame: its depth and camera are known (scene_depth ran
  // or was skipped).
  uint64_t scene_frame_ = UINT64_MAX;
  uint64_t scene_color_frame_ = UINT64_MAX;
  uint32_t scene_width_ = 0, scene_height_ = 0;
  // The final point: armed by a final combine draw, run before the next draw.
  bool final_pending_ = false;
  uint64_t final_frame_ = UINT64_MAX;
  // The final combine's guest viewport, and the final color in host pixels.
  uint32_t final_guest_width_ = 0, final_guest_height_ = 0;
  uint32_t final_width_ = 0, final_height_ = 0;
  bool warned_final_ = false;
  // The passes' inputs at the current run; the stencil is requested by the
  // first pass that reads it.
  External scene_depth_, scene_color_, scene_stencil_;
  bool scene_stencil_requested_ = false;
  struct {
    ID3D12Resource* resource = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    D3D12_GPU_DESCRIPTOR_HANDLE srv = {};
  } final_color_;
  bool skipped_ = false;

  // History: written at the scene color, read the next frame.
  History history_color_[2], history_depth_[2];
  uint32_t history_write_ = 0;
  bool history_written_ = false;
  bool previous_valid_ = false;
  uint32_t previous_width_ = 0, previous_height_ = 0;
  // The previous frame's camera and motion objects, kept from every frame with
  // a scene (also without history textures).
  bool previous_scene_ = false;
  Camera previous_camera_;

  // Motion objects by slot, of this frame and the previous one.
  std::array<MotionObject, post_pass::kMotionSlots> motion_objects_, previous_motion_objects_;
  // Per slot and source, the first world matrix of the frame (for the log).
  std::vector<std::array<post_pass::Matrix, kMaxMotionSources>> motion_first_worlds_;
  Motion motion_;

  uint64_t runs_ = 0;
  bool warned_no_camera_ = false;
  bool warned_stencil_ = false;
};

}  // namespace rex::graphics::d3d12
