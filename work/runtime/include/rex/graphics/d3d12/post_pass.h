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
 * (motion vectors). With the manifest's [jitter], the draws of the scene are
 * moved by a sub-pixel Halton (2, 3) sequence for temporal passes, in frames
 * after one in which the consuming pass ran, while the scene camera moves and
 * the frames come fast enough (post_pass_jitter.h).
 *
 * A pass of type "fsr" at the scene_color point runs AMD FSR 3.1's upscaler (fsr.h) on
 * textures of the manifest: srv = [color, depth, motion vectors, reactive, composition], the
 * last two optional, uav = [output]; its params FSR_SHARPNESS and FSR_DEBUG and the define
 * FSR_DEPTH_NEAR (fsr.hlsl prepares the inputs). It records into the replayed command list
 * through DeferredCommandList::D3DCallback. Without REX_FSR, or when its context cannot be
 * created, the param FSR stays 0 (the manifest's TAA runs instead).
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

#include <rex/graphics/d3d12/fsr.h>
#include <rex/graphics/d3d12/post_pass_camera.h>
#include <rex/graphics/d3d12/post_pass_jitter.h>
#include <rex/graphics/d3d12/post_pass_layout.h>
#include <rex/graphics/d3d12/shader_override.h>
#include <rex/graphics/pipeline/texture/cache.h>
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
  // vertex_float_constants: the bitmap of the float constants the vertex shader
  // reads (256 bits), for the fallback camera layout; nullptr if unknown.
  void OnDraw(const RegisterFile& regs, uint64_t vertex_shader_hash, uint64_t pixel_shader_hash,
              const uint64_t* vertex_float_constants = nullptr);
  // Before every resolve: runs a final point still pending.
  void OnBeforeResolve();
  // After every resolve (address and length of the written range): tracks the
  // scene depth and color, and runs the scene points when they are complete.
  void OnResolve(const RegisterFile& regs, uint32_t address, uint32_t length);
  // In the swap, in an open submission: clears the exposed outputs of the
  // frame, keeps or drops the history, picks the next frame's jitter, and
  // reloads changed files.
  void OnSwap();
  // A draw after its host viewport is known: the jitter of the scene draws of
  // this frame in host pixels (post_pass_jitter.h), if the draw is one of
  // them. rectangle: a rectangle list or a clear (a uniform draw).
  // Whether the frame's final point has run: the draws after it are the HUD's.
  bool IsAfterFinal() const;
  // The scene's width in guest pixels: its depth's pitch, or the logical width
  // where the scene is wider (native_logical_width, d3d-hle-n5.md 18.21).
  uint32_t GuestSceneWidth() const;
  // The same for the height (native_logical_height, 18.22).
  uint32_t GuestSceneHeight() const;
  // A scene-wide guest viewport in the logical width's guest pixels: shifted by
  // half the added width, so the camera's unpack maps the wider scene.
  post_pass::Viewport LogicalViewport(post_pass::Viewport viewport) const;
  bool DrawJitter(const RegisterFile& regs, uint64_t vertex_shader_hash,
                  const reg::RB_DEPTHCONTROL& depth_control, bool rectangle,
                  float jitter_out[2]) const;

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
    // previous:<texture> of a history texture: the one written in the
    // previous frame.
    bool previous = false;
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
    // Sized like the scene at its output scale: the scene's size, or while
    // upscaling the upscaler's output (docs/analysis/fsr-plan.md 11).
    bool output_size = false;
    int32_t slot = -1;
    bool clear = false;
    // Two textures swapped at the swap of a frame that wrote it: the passes
    // write this frame's and read the previous frame's as previous:<name>.
    bool history = false;
    // Written at the scene_color point, then copied into the texture cache's
    // host texture of the scene, which the game's next fetches of the scene
    // bind (the write-back; R10G10B10A2_UNORM, the scene's size).
    bool write_back = false;
    // reactive_mark: the texture shader overrides write with xe_post_mark_reactive
    // (R8_UNORM, the scene's size; one per manifest): bound in
    // kPostPassReactiveMarkUAV and cleared at every swap while an override or mod file
    // calls the function (ShaderOverrides::uses_reactive_mark), read by passes like any
    // texture (FSR's reactive mask, fsr_prepare).
    bool reactive_mark = false;
    // Usage by the passes.
    bool uav = false, rtv = false;
  };
  enum class Blend : uint32_t { kNone, kAlpha, kAdd, kMultiply };
  struct PassDesc {
    std::string name;
    Point point = Point::kSceneDepth;
    bool pixel = false;
    // type = "fsr": FSR's upscaler (no shader).
    bool fsr = false;
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
    // A camera entry with the hash *: the layout most vertex shaders of the
    // title share, for draws of vertex shaders not in camera_sources.
    bool has_camera_fallback = false;
    CameraSource camera_fallback = {};
    uint32_t min_width = 1024;
    std::vector<uint64_t> final_pixel_shaders;
    float cut_distance = 5.0f, cut_angle = 30.0f, cut_focal = 0.1f;
    // [motion]: the slot of a stencil value s is round(s / stencil_step) below
    // stencil_limit, else 0; slot_count from both (at most kMotionSlots).
    std::vector<MotionSource> motion_sources;
    // [motion] interior: the vertex shaders of an object's interior, drawn with
    // the object's stencil ID and then covered by a mask at the stored depth
    // interior_depth (no interior_sources: none).
    std::vector<MotionSource> interior_sources;
    float interior_depth = 1.0f;
    uint32_t motion_stencil_step = 1, motion_stencil_limit = 256;
    uint32_t motion_slot_count = 1;
    // [jitter]: the scene draws are jittered in a frame after one in which a
    // pass of jitter_consumers ran, while jitter_conditions hold; jitter_exclude
    // lists vertex shaders never jittered.
    bool jitter = false;
    uint32_t jitter_phases = 8;
    std::vector<Condition> jitter_conditions;
    // [jitter] every_frame_if: while it holds, the jitter of every frame, without the gate
    // (FSR accumulates still pictures itself and expects the jitter in every frame).
    std::vector<Condition> jitter_every_frame_conditions;
    std::vector<std::string> jitter_consumers;
    std::vector<uint64_t> jitter_exclude;
    // [jitter] min_motion and min_frame_rate: a number, or the param of that
    // name (its index, else UINT32_MAX); the camera's motion is measured at
    // clip W = jitter_motion_distance and held for jitter_motion_hold frames
    // (post_pass::JitterGate).
    double jitter_min_motion = 0.0, jitter_min_frame_rate = 0.0;
    uint32_t jitter_min_motion_param = UINT32_MAX, jitter_min_frame_rate_param = UINT32_MAX;
    // [jitter] still: above 0, the jitter also on a still camera, whose frames
    // the consumers accumulate (kSceneFlagCameraStill); a number or a param.
    double jitter_still = 0.0;
    uint32_t jitter_still_param = UINT32_MAX;
    double jitter_motion_distance = 10.0;
    uint32_t jitter_motion_hold = 8;
    // Name and the value of the manifest.
    std::vector<std::pair<std::string, std::string>> defines;
    std::vector<ParamDesc> params;
    std::vector<TextureDesc> textures;
    std::vector<PassDesc> passes;
    bool uses_point[size_t(Point::kCount)] = {};
    bool uses_scene_color = false, uses_previous_color = false, uses_previous_depth = false;
    bool uses_scene_stencil = false;
    bool uses_write_back = false;
    // The reactive_mark texture, or UINT32_MAX.
    uint32_t reactive_mark = UINT32_MAX;
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
    // The frame a pass last wrote it in.
    uint64_t written_frame = UINT64_MAX;
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
    // The camera sources that gave them: the index in camera_sources, or its
    // size for the fallback. A source earlier in the list replaces a later
    // one's matrices in the same frame.
    uint32_t projection_source = 0, view_source = 0;
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
    // The stencil slot of the interior's object, or kMotionNoInterior.
    uint32_t interior_slot = post_pass::kMotionNoInterior;
    // Per slot, the view position of the object's origin, w 1 if drawn.
    float object_origin[post_pass::kMotionSlots][4] = {};
    // With valid: the scene camera's motion in host pixels
    // (post_pass::CameraMotionPixels), for the jitter.
    double camera_pixels = -1.0;
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
  // source_index: the priority of the source (its index in camera_sources, or
  // their count for the fallback, the manifest's layout for any vertex shader).
  void CaptureCamera(const RegisterFile& regs, const CameraSource& source, uint32_t source_index);
  // The vertex shader reads every constant of the source's layout (its float
  // constant bitmap).
  static bool ReadsCameraConstants(const CameraSource& source, const uint64_t* float_constants);
  void CaptureObject(const RegisterFile& regs, uint32_t source_index);
  // A draw of an interior source ([motion] interior): the interior of the
  // object with its stencil reference.
  void CaptureInterior(const RegisterFile& regs, uint32_t source_index);
  // The slot and stencil reference of a draw of a motion or interior source,
  // if it writes its reference into the scene stencil.
  bool ObjectDraw(const RegisterFile& regs, uint32_t& slot_out, uint32_t& reference_out) const;
  // The world matrix of a draw of a source.
  post_pass::MatrixD ObjectWorld(const RegisterFile& regs, const MotionSource& source) const;
  // The motion slot of a stencil value (0: the static world).
  uint32_t MotionSlot(uint32_t stencil) const;
  void LogConstants(const RegisterFile& regs, uint64_t pixel_shader_hash);
  // Motion.
  void UpdateMotion();
  // An object's own view_to_previous_view, if it was captured in both frames
  // and did not jump.
  bool ObjectReprojection(const MotionObject& object, const MotionObject& previous,
                          const post_pass::MatrixD& view_to_world,
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
  // Conditions (a pass's if, [jitter] if) on the current params and defines.
  bool ConditionsHold(const std::vector<Condition>& conditions) const;
  bool PassEnabled(const PassDesc& pass) const { return ConditionsHold(pass.conditions); }
  // At the swap: the jitter of the next frame.
  void UpdateJitter();
  // The settings of the jitter gate from the manifest and the current params.
  post_pass::JitterGateSettings JitterSettings() const;
  // A param's current value (param_values_).
  double ParamValue(uint32_t index) const;
  void FillSceneConstants(post_pass::SceneConstants& constants) const;
  void RunPoint(Point point);
  enum class ExternalKind { kColor, kDepth, kStencil };
  bool RequestExternal(const ResolveGroup& group, ExternalKind kind, External& out);
  // False if the pass did not dispatch or draw (an input or output missing).
  bool RunPass(PassDesc& pass, D3D12_GPU_VIRTUAL_ADDRESS scene_constants);
  // A pass of type "fsr" (constants: the point's scene constants).
  bool RunFsr(PassDesc& pass, const post_pass::SceneConstants& constants);
  // The value of the float param or define of that name, else fallback.
  float NamedParam(const char* name, float fallback) const;
  float NamedDefine(const char* name, float fallback) const;
  bool BindingSize(const Binding& binding, uint32_t& width, uint32_t& height) const;
  void CopyHistory();
  // At the swap: binds and clears the reactive mark for the next frame's draws while an
  // override writes it, else leaves a null view in its slot.
  void UpdateReactiveMark();
  // Copies the write-back textures written at this frame's scene_color point
  // into the texture cache's host texture of the scene.
  void WriteBack();
  void LogRun(int32_t variant_index);
  // The write-back counts of the log line (empty without a write-back).
  std::string WriteBackSummary() const;

  // Resources.
  // The texture of a manifest index, or with previous its history texture's
  // copy of the previous frame.
  bool EnsureTexture(uint32_t index, bool previous = false);
  // The size of a texture of the manifest at the current scene and final
  // sizes (0 if unknown).
  void TextureSize(const TextureDesc& desc, uint32_t& width, uint32_t& height) const;
  // The previous frame's copies of the history textures are usable.
  bool HistoryTexturesValid() const;
  std::string HistoryTexturesSummary() const;
  // An enabled pass reads history texture texture as previous:<name>.
  bool HistoryTexturesReadThisFrame(size_t texture) const;
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
  // Per history texture of the manifest, the previous frame's, and whether it
  // was written in the frame before this one.
  std::vector<Texture> previous_textures_;
  std::vector<bool> previous_texture_ready_;
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
  // The scene at its output scale (native_draw_scale) while upscaling from
  // native_scene_input_scale (fsr-plan.md 11), else the scene's size.
  uint32_t output_width_ = 0, output_height_ = 0;
  // The final point: armed by a final combine draw, run before the next draw.
  bool final_pending_ = false;
  uint64_t final_frame_ = UINT64_MAX;
  // The final combine's guest viewport, and the final color in host pixels.
  uint32_t final_guest_width_ = 0, final_guest_height_ = 0;
  uint32_t final_width_ = 0, final_height_ = 0;
  bool warned_final_ = false;
  // The last final and scene sizes reported as differing.
  uint32_t mismatch_final_width_ = 0;
  uint32_t mismatch_final_height_ = 0;
  uint32_t mismatch_scene_width_ = 0;
  uint32_t mismatch_scene_height_ = 0;
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

  // The jitter of this frame's scene draws (UpdateJitter at the previous
  // swap), in host pixels; the phases counted so far; the last frame in which
  // a pass of the manifest's jitter_consumers ran.
  bool jitter_active_ = false;
  // The jitter is on because the camera is still (JitterGate::Still).
  bool jitter_still_ = false;
  float jitter_[2] = {};
  // The phase of jitter_ and the number of phases (0 with a fixed offset).
  uint32_t jitter_phase_ = 0;
  uint32_t jitter_phases_ = 0;
  uint64_t jitter_count_ = 0;
  uint64_t jitter_consumer_frame_ = UINT64_MAX;
  // The last frame in which a consumer of the jitter ran on a still camera
  // with a valid history, and the frames in a row up to it (SceneConstants
  // still_frames of the next frame).
  uint64_t still_consumer_frame_ = UINT64_MAX;
  uint32_t still_frames_ = 0;
  bool warned_jitter_ = false;
  // The camera motion and frame times that arm the jitter, and the time of the
  // last swap.
  post_pass::JitterGate jitter_gate_;
  std::chrono::steady_clock::time_point last_swap_time_ = {};

  // Motion objects by slot, of this frame and the previous one.
  std::array<MotionObject, post_pass::kMotionSlots> motion_objects_, previous_motion_objects_;
  // The interior of the frame and of the previous one (source: its index in
  // interior_sources).
  MotionObject interior_, previous_interior_;
  // Per slot and source, the first world matrix of the frame (for the log).
  std::vector<std::array<post_pass::Matrix, kMaxMotionSources>> motion_first_worlds_;
  Motion motion_;

  uint64_t runs_ = 0;
  bool warned_no_camera_ = false;
  // A scene depth not at a fractional scene scale (photo mode, 18.17), once.
  bool logged_unscaled_scene_ = false;
  bool warned_write_back_ = false;
  // The guest's use of the last complete write-back (until the one after it),
  // for the log.
  uint64_t write_back_frame_ = UINT64_MAX;
  TextureCache::WriteBackCounts write_back_counts_;
  bool warned_stencil_ = false;

  // FSR (passes of type "fsr"): the context of the current sizes, those replaced (released
  // once the GPU is past the submission), the last frame it ran in, and whether it failed
  // (FSR then stays 0).
  std::unique_ptr<FsrUpscaler> fsr_;
  std::vector<std::pair<uint64_t, std::unique_ptr<FsrUpscaler>>> retired_fsr_;
  uint64_t fsr_frame_ = UINT64_MAX;
  bool fsr_failed_ = false;
  bool warned_fsr_unavailable_ = false;
  // The time from the previous swap to the last one.
  double last_frame_seconds_ = 0.0;
  // The resource bound in kPostPassReactiveMarkUAV (nullptr: a null view).
  ID3D12Resource* reactive_mark_bound_ = nullptr;
};

}  // namespace rex::graphics::d3d12
