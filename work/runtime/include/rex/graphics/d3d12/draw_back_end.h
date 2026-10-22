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

}  // namespace rex::graphics::d3d12::draw_back_end
