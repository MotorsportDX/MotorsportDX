#pragma once

// The draw back end, stage N4 step 2 of docs/analysis/d3d-hle.md (17.6).
//
// The back half of IssueDraw (pipeline description, fixed function state,
// system constants, sampler parameters, constant buffers, root parameters and
// the draw) reads only the context registers 0x2000 to 0x23FF, the constant
// registers, and results of the front half (the primitive processor, the
// render target cache, the texture cache). Its register-reading functions take
// the register file to read, so they can run on a copy.
//
// Every draw writes a kDrawBackEnd command into the deferred command list with
// the context registers that changed since the previous capture in the list,
// the constant registers it reads that were written since, and the front half's
// results that changed; the thread that replays the list keeps a copy of the
// registers. d3d12_draw_back_end=1 (increment 1): the back end still runs at the
// draw. d3d12_draw_back_end=2 (increment 2): the replay runs the back end from
// the copy and records the draw. With d3d12_draw_back_end_check the command
// also carries the back end's results computed at the draw from the live
// registers, and the replay compares its own with them.

#include <cstdint>
#include <cstring>
#include <vector>

#include <rex/graphics/d3d12/deferred_constants.h>
#include <rex/graphics/pipeline/shader/dxbc_translator.h>
#include <rex/graphics/register_file.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/util/draw.h>
#include <rex/graphics/xenos.h>
#include <rex/ui/d3d12/d3d12_api.h>

namespace rex::graphics::d3d12::draw_back_end {

// The context registers.
constexpr uint32_t kContextFirst = 0x2000;
constexpr uint32_t kContextCount = 0x400;
constexpr uint32_t kContextWords = kContextCount / 64;
// The 76 context registers the back end reads (the register names in its
// functions and the draw_util functions they call, d3d-hle.md 17.7), one bit
// each from kContextFirst; the check shows a register missing here as a
// mismatch.
constexpr uint64_t kBackEndRegisters[kContextWords] = {
    UINT64_C(0x000000000000C03F), UINT64_C(0x0000000000000000),
    UINT64_C(0x0000000000000007), UINT64_C(0x0000000000000000),
    UINT64_C(0x00000000001FF1F7), UINT64_C(0x0000000000000000),
    UINT64_C(0x0000000000000003), UINT64_C(0x1000000000000000),
    UINT64_C(0x0000000000000F77), UINT64_C(0x0000000000000000),
    UINT64_C(0x00000000000000F3), UINT64_C(0x0000000000000000),
    UINT64_C(0x0000000000000004), UINT64_C(0x0000000000000000),
    UINT64_C(0x00000000FFFFFF0F), UINT64_C(0x0000000000000000),
};

// What UpdateSystemConstantValues reads besides the registers and the
// viewport.
struct SystemConstantInputs {
  bool edram_rov_used;
  // The ROV occlusion query flag (ROV, initialized and occlusion_query_enable).
  bool rov_occlusion_query;
  bool host_render_targets_used;
  bool gamma_render_target_as_unorm16;
  bool fixed16_truncated_to_minus_1_to_1;
  bool shared_memory_is_uav;
  bool primitive_polygonal;
  xenos::Endian index_endian;
  uint32_t line_loop_closing_index;
  uint32_t used_texture_mask;
  uint32_t normalized_color_mask;
  reg::RB_DEPTHCONTROL normalized_depth_control;
  uint32_t draw_resolution_scale_x;
  uint32_t draw_resolution_scale_y;
  // A scene draw at the scene's scale (18.13): the point size and the polygon
  // offset use it.
  bool scene_draw;
  DrawScale scene_scale;
  // Per texture fetch constant, valid for the used textures: the texture
  // cache's swizzled signs and integer scale bits, and the resolution scaled
  // textures as a mask.
  uint8_t texture_swizzled_signs[32];
  uint32_t texture_integer_scale_bits[32];
  uint32_t textures_resolution_scaled;
};

// Updates the system constants for a draw; returns whether any changed.
bool ComputeSystemConstants(const RegisterFile& regs, const draw_util::ViewportInfo& viewport_info,
                            const SystemConstantInputs& inputs,
                            DxbcShaderTranslator::SystemConstants& constants);

// The blend factor and the stencil reference of the host render target path.
void ComputeBlendFactorAndStencilReference(const RegisterFile& regs, bool primitive_polygonal,
                                           reg::RB_DEPTHCONTROL normalized_depth_control,
                                           float blend_factor_out[4],
                                           uint32_t& stencil_reference_out);

// The host viewport and scissor of a draw: at the whole scale times the
// uniform draw's, or with kFlagSceneScale at the scene's scale
// (docs/analysis/d3d-hle-n5.md 18.13).
void ComputeViewportAndScissor(const RegisterFile& regs, uint32_t flags, uint32_t scale_x,
                               uint32_t scale_y, DrawScale scene_scale,
                               reg::RB_DEPTHCONTROL normalized_depth_control,
                               bool pixel_shader_writes_depth, draw_util::ViewportInfo& viewport,
                               draw_util::Scissor& scissor);

// The scene's logical width in guest pixels (1280 unless native_logical_width
// sets another, d3d-hle-n5.md 18.21), fixed for the run.
void SetSceneLogicalWidth(uint32_t width);
uint32_t SceneLogicalWidth();
// The scene's logical height in guest pixels at 1280 wide (720 unless
// native_logical_height sets another, Vert+, d3d-hle-n5.md 18.22).
void SetSceneLogicalHeight(uint32_t height);
uint32_t SceneLogicalHeight();
// A scene draw's full-width viewport and scissor widened to the logical width:
// with kFlagLogicalCamera the clip x scaled to keep the pixels per degree,
// with kFlagLogicalHud kept at 1280 in the centre, otherwise stretched. A tile
// of predicated tiling (its window offset in regs) widens as its part of the
// full screen.
void ApplyLogicalWidth(const RegisterFile& regs, uint32_t flags, DrawScale scene_scale,
                       draw_util::ViewportInfo& viewport, draw_util::Scissor& scissor);
// With kFlagLogicalCrop: a full-screen viewport at the whole scale scale_x by
// scale_y widened to the logical size, centred and cut at the target's edge.
void ApplyLogicalCrop(uint32_t flags, uint32_t scale_x, uint32_t scale_y,
                      draw_util::ViewportInfo& viewport);

// The kDrawBackEnd command, written by Encode and read by Decode only, so the
// format can change without the back end. After the header: the masks of the
// context register words present in context_words (one uint64_t each), the
// masks of the constant register slot words (deferred_constants' 16-byte
// slots) present in constant_words, the context registers set in them (one
// dword each, in register order), the constant slots set in them (16 bytes
// each), the front half's groups present in groups (in Group order, each only
// when it changed since the previous command of the list), and with kCheck the
// back end's results at the draw (Expected, the sampler parameters, and with
// kReplayDraw the constant buffers' bytes).
struct Header {
  uint16_t context_words;
  uint16_t constant_words;
  uint16_t groups;
  uint16_t flags;
  uint32_t context_count;
  uint32_t constant_count;
  uint16_t texture_count;
  uint16_t texture_index_counts[2];
  uint16_t padding;
};
constexpr uint16_t kCheck = 1;
// With kCheck: the pipeline description's comparison is changed on purpose
// (d3d12_draw_back_end_check_perturb).
constexpr uint16_t kCheckPerturb = 2;
// The first command of the list: the replay's bindings start over.
constexpr uint16_t kListStart = 4;
// d3d12_draw_back_end=2: the replay runs the back end and records the draw.
constexpr uint16_t kReplayDraw = 8;

enum Group : uint16_t {
  kGroupShaders,
  kGroupPrimitive,
  kGroupDraw,
  kGroupTargets,
  kGroupFlags,
  kGroupTextures,
  kGroupTextureIndicesVertex,
  kGroupTextureIndicesPixel,
  kGroupKnobs,
  kGroupCount
};

// The shader translations (D3D12Shader::D3D12Translation*).
struct GroupShaders {
  void* vertex;
  void* pixel;
};
// The primitive processing result's parts that the pipeline and the system
// constants read.
struct GroupPrimitive {
  uint8_t host_primitive_type;
  uint8_t host_vertex_shader_type;
  uint8_t tessellation_mode;
  uint8_t host_index_format;
  uint8_t host_primitive_reset_enabled;
  uint8_t host_shader_index_endian;
  uint8_t index_buffer_type;
  uint8_t padding;
  uint32_t line_loop_closing_index;
  uint32_t padding_2;
};
// The draw call: the index buffer view (format DXGI_FORMAT_UNKNOWN without an
// index buffer) and the vertex or index count.
struct GroupDraw {
  uint64_t index_buffer_gpu_address;
  uint32_t index_buffer_size;
  uint32_t index_buffer_format;
  uint32_t host_draw_vertex_count;
  uint32_t padding;
};
// The render targets the render target cache bound.
struct GroupTargets {
  uint32_t bits;
  uint32_t formats[5];
};
constexpr uint32_t kFlagMemexport = 1 << 0;
constexpr uint32_t kFlagConvertZToFloat24 = 1 << 1;
constexpr uint32_t kFlagHostRenderTargets = 1 << 2;
constexpr uint32_t kFlagEdramRov = 1 << 3;
constexpr uint32_t kFlagGammaAsUnorm16 = 1 << 4;
constexpr uint32_t kFlagFixed16Truncated = 1 << 5;
constexpr uint32_t kFlagRovOcclusionQuery = 1 << 6;
constexpr uint32_t kFlagJitter = 1 << 7;
constexpr uint32_t kFlagUniformScaleShift = 8;  // 2 bits x, 2 bits y.
constexpr uint32_t kFlagKnobs = 1 << 12;
// A scene draw: into the private surfaces at the scene's scale (18.13).
constexpr uint32_t kFlagSceneScale = 1 << 13;
// At a logical width other than 1280 (native_logical_width, d3d-hle-n5.md
// 18.21): a scene draw through the camera (depth or stencil on), whose clip x
// is scaled so the wider viewport shows more (Hor+), and a draw of the HUD
// (after the post passes' final point), which stays in the centred 1280.
constexpr uint32_t kFlagLogicalCamera = 1 << 14;
constexpr uint32_t kFlagLogicalHud = 1 << 15;
// With kFlagSceneScale while upscaling (docs/analysis/fsr-plan.md 11): into the
// MSAA private surfaces at the scene's input scale.
constexpr uint32_t kFlagSceneInput = 1 << 16;
// At a logical size, a draw outside the scene's scale after the post passes'
// final point that samples a full-screen scene texture (d3d-hle-n5.md 18.28):
// its full-screen viewport
// covers the logical size centred on the target, so it shows the texture's
// centred 1280x720.
constexpr uint32_t kFlagLogicalCrop = 1 << 17;
// The scale of a scene draw with the flags.
inline DrawScale FlagsSceneScale(uint32_t flags, DrawScale scene_scale, DrawScale input_scale) {
  return (flags & kFlagSceneInput) ? input_scale : scene_scale;
}
struct GroupFlags {
  uint32_t flags;
  uint16_t draw_resolution_scale_x;
  uint16_t draw_resolution_scale_y;
  float jitter[2];
  // The frame and the submission the draw belongs to, and the last frame the
  // GPU completed (the replay's constant buffer pool reclaims by them).
  uint64_t frame;
  uint64_t frame_completed;
  uint64_t submission;
};
// The texture cache's answers for the used textures: the used mask, the
// resolution scaled mask, then per used texture its swizzled signs (one byte
// each, padded to 4) and its integer scale bits (4 bytes each).
struct GroupTexturesHeader {
  uint32_t used_texture_mask;
  uint32_t textures_resolution_scaled;
};
// The texture groups of a stage: the bindless SRV index of each texture
// binding, as the descriptor index buffer holds it (4 bytes each).
// The knobs group: the shader mods' live knob values.
constexpr uint32_t kKnobCount = 64;

// The back end's results at the draw, with kCheck.
struct Expected {
  // The pipeline description (up to 128 bytes, PipelineCache's size).
  uint8_t pipeline_description[128];
  draw_util::ViewportInfo viewport;
  draw_util::Scissor scissor;
  float blend_factor[4];
  uint32_t stencil_reference;
  uint32_t sampler_count_vertex;
  uint32_t sampler_count_pixel;
  // With kReplayDraw: the bytes of the float vertex, float pixel, bool and
  // loop, and fetch constant buffers that follow the sampler parameters.
  uint32_t constant_bytes[4];
  uint32_t padding;
  DxbcShaderTranslator::SystemConstants system_constants;
};

// What the decoder keeps between the commands of a list.
struct ListState {
  GroupShaders shaders;
  GroupPrimitive primitive;
  GroupDraw draw;
  GroupTargets targets;
  GroupFlags flags;
  uint32_t used_texture_mask;
  uint32_t textures_resolution_scaled;
  uint8_t texture_swizzled_signs[32];
  uint32_t texture_integer_scale_bits[32];
};

// Comparison kinds of the check.
enum Mismatch : uint32_t {
  kMismatchPipeline,
  kMismatchViewport,
  kMismatchScissor,
  kMismatchFixedFunction,
  kMismatchSystemConstants,
  kMismatchSamplers,
  kMismatchConstantBuffers,
  kMismatchCount
};

// A decoded command: the list state (updated in place) and where the parts of
// the command that are not kept start.
struct Decoded {
  Header header;
  // The constant slots captured in this command (kSlotCount bits).
  uint64_t constant_slots[deferred_constants::kMaskWords];
  // The texture indices of the stages present in this command, the knobs if
  // present, and the check's part.
  const uint32_t* texture_indices[2];
  const float* knobs;
  const uint8_t* check;
};
// Applies a command to the register copy and the list state.
void Decode(const uint8_t* arguments, RegisterFile& shadow, ListState& list, Decoded& decoded);

// The replay's D3D12 state of the command list it records: what the list's
// own commands set, and what the back end bound (d3d12_draw_back_end=2).
struct ReplayState {
  ID3D12RootSignature* root_signature;
  // Root parameters the back end set since the root signature, and their
  // values.
  uint32_t root_parameters_set;
  uint64_t root_parameters[16];
  ID3D12PipelineState* pipeline;
  D3D12_PRIMITIVE_TOPOLOGY topology;
  bool viewport_set;
  bool scissor_set;
  bool blend_factor_set;
  bool stencil_reference_set;
  bool index_buffer_set;
  D3D12_VIEWPORT viewport;
  D3D12_RECT scissor;
  float blend_factor[4];
  uint32_t stencil_reference;
  D3D12_INDEX_BUFFER_VIEW index_buffer;
  ID3D12DescriptorHeap* view_heap;
  ID3D12DescriptorHeap* sampler_heap;
};

// The back end's buffers in the list being recorded (d3d12_draw_back_end=2):
// valid until the list ends or a register they hold changes.
struct ReplayBindings {
  bool system_valid;
  bool float_valid[2];
  bool bool_loop_valid;
  bool fetch_valid;
  bool indices_valid[2];
  uint64_t system;
  uint64_t float_buffers[2];
  uint64_t bool_loop;
  uint64_t fetch;
  uint64_t indices[2];
  uint64_t float_maps[2][4];
  size_t sampler_layout_uid[2];
  std::vector<uint32_t> samplers[2];
  std::vector<uint32_t> sampler_indices[2];
};

// What the replay of one list keeps (a member of DeferredCommandList).
struct ReplayList {
  ListState list;
  ReplayState d3d;
  ReplayBindings bindings;
  std::vector<uint32_t> texture_indices[2];
  float knobs[kKnobCount];
  // The shader overrides' host values (the guest vblank rate, the title's
  // frame interval and its design frame rate).
  float host_values[4] = {};

  // At the start of each Execute: a new D3D12 command list.
  void BeginList() {
    std::memset(&d3d, 0, sizeof(d3d));
    d3d.topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    bindings.system_valid = false;
    bindings.float_valid[0] = bindings.float_valid[1] = false;
    bindings.bool_loop_valid = bindings.fetch_valid = false;
    bindings.indices_valid[0] = bindings.indices_valid[1] = false;
    std::memset(bindings.float_maps, 0, sizeof(bindings.float_maps));
    bindings.sampler_layout_uid[0] = bindings.sampler_layout_uid[1] = SIZE_MAX;
    for (uint32_t stage = 0; stage < 2; ++stage) {
      bindings.samplers[stage].clear();
      bindings.sampler_indices[stage].clear();
    }
  }
};

}  // namespace rex::graphics::d3d12::draw_back_end
