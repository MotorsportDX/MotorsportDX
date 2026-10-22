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
// Increment 1 (d3d12_draw_back_end=1): every draw writes a kDrawBackEnd command
// into the deferred command list with the context registers that changed since
// the previous capture in the list, the fetch constants its samplers read, and
// the front half's results that changed; the thread that replays the list
// keeps a copy of the registers. With d3d12_draw_back_end_check the command also
// carries the back end's results computed at the draw, and the replay computes
// them again from its copy and compares. The back end itself still runs at the
// draw.

#include <cstdint>
#include <cstring>

#include <rex/graphics/pipeline/shader/dxbc_translator.h>
#include <rex/graphics/register_file.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/util/draw.h>
#include <rex/graphics/xenos.h>

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

// The kDrawBackEnd command. After the header: the masks of the context
// register words present in context_words (one uint64_t each), the context
// registers set in them (one dword each, in register order), with fetch_slots
// the 16-byte slots of fetch constants (from 0x4800, 48 slots) set in it, the
// front half's groups present in groups (in Group order, each only when it
// changed since the previous command of the list), and with kCheck the
// back end's results at the draw (Expected, then the sampler parameters).
struct Header {
  uint16_t context_words;
  uint16_t groups;
  uint16_t check;
  uint16_t texture_count;
  uint32_t context_count;
  uint32_t fetch_count;
  uint64_t fetch_slots;
};
constexpr uint16_t kCheck = 1;
// With kCheck: the pipeline description's comparison is changed on purpose
// (d3d12_draw_back_end_check_perturb).
constexpr uint16_t kCheckPerturb = 2;

enum Group : uint16_t {
  kGroupShaders,
  kGroupPrimitive,
  kGroupDraw,
  kGroupTargets,
  kGroupFlags,
  kGroupTextures,
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
// The draw call: the vertex or index count and the index buffer.
struct GroupDraw {
  uint64_t index_buffer_gpu_address;
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
struct GroupFlags {
  uint32_t flags;
  uint16_t draw_resolution_scale_x;
  uint16_t draw_resolution_scale_y;
  float jitter[2];
};
// The texture cache's answers for the used textures: the used mask, the
// resolution scaled mask, then per used texture its swizzled signs (one byte
// each, padded to 4) and its integer scale bits (4 bytes each).
struct GroupTexturesHeader {
  uint32_t used_texture_mask;
  uint32_t textures_resolution_scaled;
};

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
  uint32_t padding;
  DxbcShaderTranslator::SystemConstants system_constants;
};

// What the replay of a list keeps between its kDrawBackEnd commands.
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
  kMismatchCount
};

}  // namespace rex::graphics::d3d12::draw_back_end
