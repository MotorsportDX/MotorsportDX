// The draw back end, stage N4 step 2 of docs/analysis/d3d-hle.md (17.6): the
// parts of the back half of IssueDraw that read registers, taking the register
// file to read. See rex/graphics/d3d12/draw_back_end.h.
//
// ComputeSystemConstants is D3D12CommandProcessor::UpdateSystemConstantValues
// of the upstream code (Copyright 2022 Ben Vanik, BSD license, see LICENSE),
// with the render target cache's and the texture cache's answers passed in.

#include <atomic>
#include <cstring>

#include <rex/assert.h>
#include <rex/cvar.h>
#include <rex/graphics/d3d12/command_processor.h>
#include <rex/graphics/d3d12/draw_back_end.h>
#include <rex/graphics/d3d12/post_pass_jitter.h>
#include <rex/graphics/d3d12/shader_override.h>
#include <rex/graphics/pipeline/render_target/cache.h>
#include <rex/logging.h>
#include <rex/math.h>
#include <rex/memory/utils.h>

#include "../util/perf_trace.h"

REXCVAR_DEFINE_INT32(d3d12_draw_back_end, 0, "GPU/D3D12",
                     "The draw back end of N4 step 2 (docs/analysis/d3d-hle.md, 17.6). 0: off. "
                     "1: every draw also captures the back end's inputs into the deferred "
                     "command list, and the thread that replays the list keeps a copy of the "
                     "registers; the back end still runs at the draw")
    .range(0, 1)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(d3d12_draw_back_end_check, false, "GPU/D3D12",
                    "With d3d12_draw_back_end: also write the back end's results at the draw "
                    "into the command, and compute and compare them again from the replay's "
                    "copy of the registers")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_INT32(d3d12_draw_back_end_check_perturb, 0, "GPU/D3D12",
                     "Test of d3d12_draw_back_end_check: change the pipeline description's "
                     "comparison of every Nth checked draw so that it must be reported (0: off)")
    .range(0, 1000000)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rex::graphics::d3d12::draw_back_end {

bool ComputeSystemConstants(const RegisterFile& regs, const draw_util::ViewportInfo& viewport_info,
                            const SystemConstantInputs& inputs,
                            DxbcShaderTranslator::SystemConstants& constants) {
  const bool shared_memory_is_uav = inputs.shared_memory_is_uav;
  const bool primitive_polygonal = inputs.primitive_polygonal;
  const uint32_t line_loop_closing_index = inputs.line_loop_closing_index;
  const xenos::Endian index_endian = inputs.index_endian;
  const uint32_t used_texture_mask = inputs.used_texture_mask;
  const reg::RB_DEPTHCONTROL normalized_depth_control = inputs.normalized_depth_control;
  const uint32_t normalized_color_mask = inputs.normalized_color_mask;
  auto pa_cl_clip_cntl = regs.Get<reg::PA_CL_CLIP_CNTL>();
  auto pa_cl_vte_cntl = regs.Get<reg::PA_CL_VTE_CNTL>();
  auto pa_su_sc_mode_cntl = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
  auto rb_alpha_ref = regs.Get<float>(XE_GPU_REG_RB_ALPHA_REF);
  auto rb_colorcontrol = regs.Get<reg::RB_COLORCONTROL>();
  auto rb_depth_info = regs.Get<reg::RB_DEPTH_INFO>();
  auto rb_stencilrefmask = regs.Get<reg::RB_STENCILREFMASK>();
  auto rb_stencilrefmask_bf = regs.Get<reg::RB_STENCILREFMASK>(XE_GPU_REG_RB_STENCILREFMASK_BF);
  auto rb_surface_info = regs.Get<reg::RB_SURFACE_INFO>();
  auto sq_context_misc = regs.Get<reg::SQ_CONTEXT_MISC>();
  auto sq_program_cntl = regs.Get<reg::SQ_PROGRAM_CNTL>();
  auto vgt_draw_initiator = regs.Get<reg::VGT_DRAW_INITIATOR>();
  uint32_t vgt_indx_offset = regs.Get<reg::VGT_INDX_OFFSET>().indx_offset;
  uint32_t vgt_max_vtx_indx = regs.Get<reg::VGT_MAX_VTX_INDX>().max_indx;
  uint32_t vgt_min_vtx_indx = regs.Get<reg::VGT_MIN_VTX_INDX>().min_indx;

  bool edram_rov_used = inputs.edram_rov_used;
  uint32_t draw_resolution_scale_x = inputs.draw_resolution_scale_x;
  uint32_t draw_resolution_scale_y = inputs.draw_resolution_scale_y;

  // Get the color info register values for each render target. Also, for ROV,
  // exclude components that don't exist in the format from the write mask.
  // Don't exclude fully overlapping render targets, however - two render
  // targets with the same base address are used in the lighting pass of
  // 4D5307E6, for example, with the needed one picked with dynamic control
  // flow.
  reg::RB_COLOR_INFO color_infos[4];
  float rt_clamp[4][4];
  // Two UINT32_MAX if no components actually existing in the RT are written.
  uint32_t rt_keep_masks[4][2];
  for (uint32_t i = 0; i < 4; ++i) {
    auto color_info = regs.Get<reg::RB_COLOR_INFO>(reg::RB_COLOR_INFO::rt_register_indices[i]);
    color_infos[i] = color_info;
    if (edram_rov_used) {
      RenderTargetCache::GetPSIColorFormatInfo(
          color_info.color_format, (normalized_color_mask >> (i * 4)) & 0b1111, rt_clamp[i][0],
          rt_clamp[i][1], rt_clamp[i][2], rt_clamp[i][3], rt_keep_masks[i][0], rt_keep_masks[i][1]);
    }
  }

  // Disable depth and stencil if it aliases a color render target (for
  // instance, during the XBLA logo in 58410954, though depth writing is already
  // disabled there).
  bool depth_stencil_enabled =
      normalized_depth_control.stencil_enable || normalized_depth_control.z_enable;
  if (edram_rov_used && depth_stencil_enabled) {
    for (uint32_t i = 0; i < 4; ++i) {
      if (rb_depth_info.depth_base == color_infos[i].color_base &&
          (rt_keep_masks[i][0] != UINT32_MAX || rt_keep_masks[i][1] != UINT32_MAX)) {
        depth_stencil_enabled = false;
        break;
      }
    }
  }

  bool dirty = false;

  // Flags.
  uint32_t flags = 0;
  if (inputs.rov_occlusion_query) {
    flags |= DxbcShaderTranslator::kSysFlag_ROVOcclusionQuery;
  }
  // Whether shared memory is an SRV or a UAV. Because a resource can't be in a
  // read-write (UAV) and a read-only (SRV, IBV) state at once, if any shader in
  // the pipeline uses memexport, the shared memory buffer must be a UAV.
  if (shared_memory_is_uav) {
    flags |= DxbcShaderTranslator::kSysFlag_SharedMemoryIsUAV;
  }
  // W0 division control.
  // http://www.x.org/docs/AMD/old/evergreen_3D_registers_v2.pdf
  // 8: VTX_XY_FMT = true: the incoming XY have already been multiplied by 1/W0.
  //               = false: multiply the X, Y coordinates by 1/W0.
  // 9: VTX_Z_FMT = true: the incoming Z has already been multiplied by 1/W0.
  //              = false: multiply the Z coordinate by 1/W0.
  // 10: VTX_W0_FMT = true: the incoming W0 is not 1/W0. Perform the reciprocal
  //                        to get 1/W0.
  if (pa_cl_vte_cntl.vtx_xy_fmt) {
    flags |= DxbcShaderTranslator::kSysFlag_XYDividedByW;
  }
  if (pa_cl_vte_cntl.vtx_z_fmt) {
    flags |= DxbcShaderTranslator::kSysFlag_ZDividedByW;
  }
  if (pa_cl_vte_cntl.vtx_w0_fmt) {
    flags |= DxbcShaderTranslator::kSysFlag_WNotReciprocal;
  }
  // Whether the primitive is polygonal and SV_IsFrontFace matters.
  if (primitive_polygonal) {
    flags |= DxbcShaderTranslator::kSysFlag_PrimitivePolygonal;
  }
  // Primitive type.
  if (draw_util::IsPrimitiveLine(regs)) {
    flags |= DxbcShaderTranslator::kSysFlag_PrimitiveLine;
  }
  // Depth format.
  if (rb_depth_info.depth_format == xenos::DepthRenderTargetFormat::kD24FS8) {
    flags |= DxbcShaderTranslator::kSysFlag_DepthFloat24;
  }
  // Alpha test.
  xenos::CompareFunction alpha_test_function = rb_colorcontrol.alpha_test_enable
                                                   ? rb_colorcontrol.alpha_func
                                                   : xenos::CompareFunction::kAlways;
  flags |= uint32_t(alpha_test_function) << DxbcShaderTranslator::kSysFlag_AlphaPassIfLess_Shift;
  // Gamma writing.
  if (!inputs.gamma_render_target_as_unorm16) {
    for (uint32_t i = 0; i < 4; ++i) {
      if (color_infos[i].color_format == xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA) {
        flags |= DxbcShaderTranslator::kSysFlag_ConvertColor0ToGamma << i;
      }
    }
  }
  if (edram_rov_used && depth_stencil_enabled) {
    flags |= DxbcShaderTranslator::kSysFlag_ROVDepthStencil;
    if (normalized_depth_control.z_enable) {
      flags |= uint32_t(normalized_depth_control.zfunc)
               << DxbcShaderTranslator::kSysFlag_ROVDepthPassIfLess_Shift;
      if (normalized_depth_control.z_write_enable) {
        flags |= DxbcShaderTranslator::kSysFlag_ROVDepthWrite;
      }
    } else {
      // In case stencil is used without depth testing - always pass, and
      // don't modify the stored depth.
      flags |= DxbcShaderTranslator::kSysFlag_ROVDepthPassIfLess |
               DxbcShaderTranslator::kSysFlag_ROVDepthPassIfEqual |
               DxbcShaderTranslator::kSysFlag_ROVDepthPassIfGreater;
    }
    if (normalized_depth_control.stencil_enable) {
      flags |= DxbcShaderTranslator::kSysFlag_ROVStencilTest;
    }
    // Hint - if not applicable to the shader, will not have effect.
    if (alpha_test_function == xenos::CompareFunction::kAlways &&
        !rb_colorcontrol.alpha_to_mask_enable) {
      flags |= DxbcShaderTranslator::kSysFlag_ROVDepthStencilEarlyWrite;
    }
  }
  dirty |= constants.flags != flags;
  constants.flags = flags;

  // Tessellation factor range, plus 1.0 according to the images in
  // https://www.slideshare.net/blackdevilvikas/next-generation-graphics-programming-on-xbox-360
  float tessellation_factor_min = regs.Get<float>(XE_GPU_REG_VGT_HOS_MIN_TESS_LEVEL) + 1.0f;
  float tessellation_factor_max = regs.Get<float>(XE_GPU_REG_VGT_HOS_MAX_TESS_LEVEL) + 1.0f;
  dirty |= constants.tessellation_factor_range_min != tessellation_factor_min;
  constants.tessellation_factor_range_min = tessellation_factor_min;
  dirty |= constants.tessellation_factor_range_max != tessellation_factor_max;
  constants.tessellation_factor_range_max = tessellation_factor_max;

  // Line loop closing index (or 0 when drawing other primitives or using an
  // index buffer).
  dirty |= constants.line_loop_closing_index != line_loop_closing_index;
  constants.line_loop_closing_index = line_loop_closing_index;

  // Index or tessellation edge factor buffer endianness.
  dirty |= constants.vertex_index_endian != index_endian;
  constants.vertex_index_endian = index_endian;

  // Vertex index offset.
  dirty |= constants.vertex_index_offset != vgt_indx_offset;
  constants.vertex_index_offset = vgt_indx_offset;

  // Vertex index range.
  dirty |= constants.vertex_index_min != vgt_min_vtx_indx;
  dirty |= constants.vertex_index_max != vgt_max_vtx_indx;
  constants.vertex_index_min = vgt_min_vtx_indx;
  constants.vertex_index_max = vgt_max_vtx_indx;

  // User clip planes (UCP_ENA_#), when not CLIP_DISABLE.
  // The shader knows only the total count - tightly packing the user clip
  // planes that are actually used.
  if (!pa_cl_clip_cntl.clip_disable) {
    float* user_clip_plane_write_ptr = constants.user_clip_planes[0];
    uint32_t user_clip_planes_remaining = pa_cl_clip_cntl.ucp_ena;
    uint32_t user_clip_plane_index;
    while (rex::bit_scan_forward(user_clip_planes_remaining, &user_clip_plane_index)) {
      user_clip_planes_remaining &= ~(UINT32_C(1) << user_clip_plane_index);
      const void* user_clip_plane_regs =
          &regs[XE_GPU_REG_PA_CL_UCP_0_X + user_clip_plane_index * 4];
      if (std::memcmp(user_clip_plane_write_ptr, user_clip_plane_regs, 4 * sizeof(float))) {
        dirty = true;
        std::memcpy(user_clip_plane_write_ptr, user_clip_plane_regs, 4 * sizeof(float));
      }
      user_clip_plane_write_ptr += 4;
    }
  }

  // Conversion to Direct3D 12 normalized device coordinates.
  for (uint32_t i = 0; i < 3; ++i) {
    dirty |= constants.ndc_scale[i] != viewport_info.ndc_scale[i];
    dirty |= constants.ndc_offset[i] != viewport_info.ndc_offset[i];
    constants.ndc_scale[i] = viewport_info.ndc_scale[i];
    constants.ndc_offset[i] = viewport_info.ndc_offset[i];
  }

  // Point size.
  if (vgt_draw_initiator.prim_type == xenos::PrimitiveType::kPointList) {
    auto pa_su_point_minmax = regs.Get<reg::PA_SU_POINT_MINMAX>();
    auto pa_su_point_size = regs.Get<reg::PA_SU_POINT_SIZE>();
    float point_vertex_diameter_min = float(pa_su_point_minmax.min_size) * (2.0f / 16.0f);
    float point_vertex_diameter_max = float(pa_su_point_minmax.max_size) * (2.0f / 16.0f);
    float point_constant_diameter_x = float(pa_su_point_size.width) * (2.0f / 16.0f);
    float point_constant_diameter_y = float(pa_su_point_size.height) * (2.0f / 16.0f);
    dirty |= constants.point_vertex_diameter_min != point_vertex_diameter_min;
    dirty |= constants.point_vertex_diameter_max != point_vertex_diameter_max;
    dirty |= constants.point_constant_diameter[0] != point_constant_diameter_x;
    dirty |= constants.point_constant_diameter[1] != point_constant_diameter_y;
    constants.point_vertex_diameter_min = point_vertex_diameter_min;
    constants.point_vertex_diameter_max = point_vertex_diameter_max;
    constants.point_constant_diameter[0] = point_constant_diameter_x;
    constants.point_constant_diameter[1] = point_constant_diameter_y;
    // 2 because 1 in the NDC is half of the viewport's axis, 0.5 for diameter
    // to radius conversion to avoid multiplying the per-vertex diameter by an
    // additional constant in the shader.
    const float point_scale_x =
        inputs.scene_draw ? inputs.scene_scale.ToFloat() : float(draw_resolution_scale_x);
    const float point_scale_y =
        inputs.scene_draw ? inputs.scene_scale.ToFloat() : float(draw_resolution_scale_y);
    float point_screen_diameter_to_ndc_radius_x =
        (/* 0.5f * 2.0f * */ point_scale_x) / std::max(viewport_info.xy_extent[0], uint32_t(1));
    float point_screen_diameter_to_ndc_radius_y =
        (/* 0.5f * 2.0f * */ point_scale_y) / std::max(viewport_info.xy_extent[1], uint32_t(1));
    dirty |= constants.point_screen_diameter_to_ndc_radius[0] !=
             point_screen_diameter_to_ndc_radius_x;
    dirty |= constants.point_screen_diameter_to_ndc_radius[1] !=
             point_screen_diameter_to_ndc_radius_y;
    constants.point_screen_diameter_to_ndc_radius[0] =
        point_screen_diameter_to_ndc_radius_x;
    constants.point_screen_diameter_to_ndc_radius[1] =
        point_screen_diameter_to_ndc_radius_y;
  }

  // Texture signedness / gamma.
  uint32_t textures_resolution_scaled = 0;
  uint32_t textures_remaining = used_texture_mask;
  uint32_t texture_index;
  while (rex::bit_scan_forward(textures_remaining, &texture_index)) {
    textures_remaining &= ~(uint32_t(1) << texture_index);
    uint32_t& texture_signs_uint = constants.texture_swizzled_signs[texture_index >> 2];
    uint32_t texture_signs_shift = (texture_index & 3) * 8;
    uint8_t texture_signs = inputs.texture_swizzled_signs[texture_index];
    uint32_t texture_signs_shifted = uint32_t(texture_signs) << texture_signs_shift;
    uint32_t texture_signs_mask = uint32_t(0b11111111) << texture_signs_shift;
    dirty |= (texture_signs_uint & texture_signs_mask) != texture_signs_shifted;
    texture_signs_uint = (texture_signs_uint & ~texture_signs_mask) | texture_signs_shifted;
    uint32_t integer_scale_bits = inputs.texture_integer_scale_bits[texture_index];
    dirty |= constants.texture_integer_scale_bits[texture_index] != integer_scale_bits;
    constants.texture_integer_scale_bits[texture_index] = integer_scale_bits;
    textures_resolution_scaled |= inputs.textures_resolution_scaled & (uint32_t(1) << texture_index);
  }
  dirty |= constants.textures_resolution_scaled != textures_resolution_scaled;
  constants.textures_resolution_scaled = textures_resolution_scaled;

  // Log2 of sample count, for alpha to mask and with ROV, for EDRAM address
  // calculation with MSAA.
  uint32_t sample_count_log2_x = rb_surface_info.msaa_samples >= xenos::MsaaSamples::k4X ? 1 : 0;
  uint32_t sample_count_log2_y = rb_surface_info.msaa_samples >= xenos::MsaaSamples::k2X ? 1 : 0;
  dirty |= constants.sample_count_log2[0] != sample_count_log2_x;
  dirty |= constants.sample_count_log2[1] != sample_count_log2_y;
  constants.sample_count_log2[0] = sample_count_log2_x;
  constants.sample_count_log2[1] = sample_count_log2_y;

  // Alpha test and alpha to coverage.
  dirty |= constants.alpha_test_reference != rb_alpha_ref;
  constants.alpha_test_reference = rb_alpha_ref;
  uint32_t alpha_to_mask =
      rb_colorcontrol.alpha_to_mask_enable ? (rb_colorcontrol.value >> 24) | (1 << 8) : 0;
  dirty |= constants.alpha_to_mask != alpha_to_mask;
  constants.alpha_to_mask = alpha_to_mask;

  uint32_t edram_tile_dwords_scaled = xenos::kEdramTileWidthSamples *
                                      xenos::kEdramTileHeightSamples *
                                      (draw_resolution_scale_x * draw_resolution_scale_y);

  // EDRAM pitch for ROV writing.
  if (edram_rov_used) {
    // Align, then multiply by 32bpp tile size in dwords.
    uint32_t edram_32bpp_tile_pitch_dwords_scaled =
        ((rb_surface_info.surface_pitch *
          (rb_surface_info.msaa_samples >= xenos::MsaaSamples::k4X ? 2 : 1)) +
         (xenos::kEdramTileWidthSamples - 1)) /
        xenos::kEdramTileWidthSamples * edram_tile_dwords_scaled;
    dirty |= constants.edram_32bpp_tile_pitch_dwords_scaled !=
             edram_32bpp_tile_pitch_dwords_scaled;
    constants.edram_32bpp_tile_pitch_dwords_scaled = edram_32bpp_tile_pitch_dwords_scaled;
  }

  // Color exponent bias and ROV render target writing.
  for (uint32_t i = 0; i < 4; ++i) {
    reg::RB_COLOR_INFO color_info = color_infos[i];
    // Exponent bias is in bits 20:25 of RB_COLOR_INFO.
    int32_t color_exp_bias = color_info.color_exp_bias;
    if (color_info.color_format == xenos::ColorRenderTargetFormat::k_16_16 ||
        color_info.color_format == xenos::ColorRenderTargetFormat::k_16_16_16_16) {
      if (inputs.host_render_targets_used && !inputs.fixed16_truncated_to_minus_1_to_1) {
        // Remap from -32...32 to -1...1 by dividing the output values by 32,
        // losing blending correctness, but getting the full range.
        color_exp_bias -= 5;
      }
    }
    auto color_exp_bias_scale =
        rex::memory::Reinterpret<float>(int32_t(0x3F800000 + (color_exp_bias << 23)));
    dirty |= constants.color_exp_bias[i] != color_exp_bias_scale;
    constants.color_exp_bias[i] = color_exp_bias_scale;
    if (edram_rov_used) {
      dirty |= constants.edram_rt_keep_mask[i][0] != rt_keep_masks[i][0];
      constants.edram_rt_keep_mask[i][0] = rt_keep_masks[i][0];
      dirty |= constants.edram_rt_keep_mask[i][1] != rt_keep_masks[i][1];
      constants.edram_rt_keep_mask[i][1] = rt_keep_masks[i][1];
      if (rt_keep_masks[i][0] != UINT32_MAX || rt_keep_masks[i][1] != UINT32_MAX) {
        uint32_t rt_base_dwords_scaled = color_info.color_base * edram_tile_dwords_scaled;
        dirty |= constants.edram_rt_base_dwords_scaled[i] != rt_base_dwords_scaled;
        constants.edram_rt_base_dwords_scaled[i] = rt_base_dwords_scaled;
        uint32_t format_flags = RenderTargetCache::AddPSIColorFormatFlags(color_info.color_format);
        dirty |= constants.edram_rt_format_flags[i] != format_flags;
        constants.edram_rt_format_flags[i] = format_flags;
        // Can't do float comparisons here because NaNs would result in always
        // setting the dirty flag.
        dirty |=
            std::memcmp(constants.edram_rt_clamp[i], rt_clamp[i], 4 * sizeof(float)) != 0;
        std::memcpy(constants.edram_rt_clamp[i], rt_clamp[i], 4 * sizeof(float));
        uint32_t blend_factors_ops =
            regs[reg::RB_BLENDCONTROL::rt_register_indices[i]] & 0x1FFF1FFF;
        dirty |= constants.edram_rt_blend_factors_ops[i] != blend_factors_ops;
        constants.edram_rt_blend_factors_ops[i] = blend_factors_ops;
      }
    }
  }

  if (edram_rov_used) {
    uint32_t depth_base_dwords_scaled = rb_depth_info.depth_base * edram_tile_dwords_scaled;
    dirty |= constants.edram_depth_base_dwords_scaled != depth_base_dwords_scaled;
    constants.edram_depth_base_dwords_scaled = depth_base_dwords_scaled;

    // For non-polygons, front polygon offset is used, and it's enabled if
    // POLY_OFFSET_PARA_ENABLED is set, for polygons, separate front and back
    // are used.
    float poly_offset_front_scale = 0.0f, poly_offset_front_offset = 0.0f;
    float poly_offset_back_scale = 0.0f, poly_offset_back_offset = 0.0f;
    if (primitive_polygonal) {
      if (pa_su_sc_mode_cntl.poly_offset_front_enable) {
        poly_offset_front_scale = regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_SCALE);
        poly_offset_front_offset = regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_OFFSET);
      }
      if (pa_su_sc_mode_cntl.poly_offset_back_enable) {
        poly_offset_back_scale = regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_BACK_SCALE);
        poly_offset_back_offset = regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_BACK_OFFSET);
      }
    } else {
      if (pa_su_sc_mode_cntl.poly_offset_para_enable) {
        poly_offset_front_scale = regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_SCALE);
        poly_offset_front_offset = regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_OFFSET);
        poly_offset_back_scale = poly_offset_front_scale;
        poly_offset_back_offset = poly_offset_front_offset;
      }
    }
    // With non-square resolution scaling, make sure the worst-case impact is
    // reverted (slope only along the scaled axis), thus max. More bias is
    // better than less bias, because less bias means Z fighting with the
    // background is more likely.
    float poly_offset_scale_factor =
        xenos::kPolygonOffsetScaleSubpixelUnit *
        (inputs.scene_draw ? inputs.scene_scale.ToFloat()
                           : float(std::max(draw_resolution_scale_x, draw_resolution_scale_y)));
    poly_offset_front_scale *= poly_offset_scale_factor;
    poly_offset_back_scale *= poly_offset_scale_factor;
    dirty |= constants.edram_poly_offset_front_scale != poly_offset_front_scale;
    constants.edram_poly_offset_front_scale = poly_offset_front_scale;
    dirty |= constants.edram_poly_offset_front_offset != poly_offset_front_offset;
    constants.edram_poly_offset_front_offset = poly_offset_front_offset;
    dirty |= constants.edram_poly_offset_back_scale != poly_offset_back_scale;
    constants.edram_poly_offset_back_scale = poly_offset_back_scale;
    dirty |= constants.edram_poly_offset_back_offset != poly_offset_back_offset;
    constants.edram_poly_offset_back_offset = poly_offset_back_offset;

    if (depth_stencil_enabled && normalized_depth_control.stencil_enable) {
      dirty |= constants.edram_stencil_front_reference != rb_stencilrefmask.stencilref;
      constants.edram_stencil_front_reference = rb_stencilrefmask.stencilref;
      dirty |= constants.edram_stencil_front_read_mask != rb_stencilrefmask.stencilmask;
      constants.edram_stencil_front_read_mask = rb_stencilrefmask.stencilmask;
      dirty |=
          constants.edram_stencil_front_write_mask != rb_stencilrefmask.stencilwritemask;
      constants.edram_stencil_front_write_mask = rb_stencilrefmask.stencilwritemask;
      uint32_t stencil_func_ops = (normalized_depth_control.value >> 8) & ((1 << 12) - 1);
      dirty |= constants.edram_stencil_front_func_ops != stencil_func_ops;
      constants.edram_stencil_front_func_ops = stencil_func_ops;

      if (primitive_polygonal && normalized_depth_control.backface_enable) {
        dirty |= constants.edram_stencil_back_reference != rb_stencilrefmask_bf.stencilref;
        constants.edram_stencil_back_reference = rb_stencilrefmask_bf.stencilref;
        dirty |= constants.edram_stencil_back_read_mask != rb_stencilrefmask_bf.stencilmask;
        constants.edram_stencil_back_read_mask = rb_stencilrefmask_bf.stencilmask;
        dirty |= constants.edram_stencil_back_write_mask !=
                 rb_stencilrefmask_bf.stencilwritemask;
        constants.edram_stencil_back_write_mask = rb_stencilrefmask_bf.stencilwritemask;
        uint32_t stencil_func_ops_bf = (normalized_depth_control.value >> 20) & ((1 << 12) - 1);
        dirty |= constants.edram_stencil_back_func_ops != stencil_func_ops_bf;
        constants.edram_stencil_back_func_ops = stencil_func_ops_bf;
      } else {
        dirty |= std::memcmp(constants.edram_stencil_back,
                             constants.edram_stencil_front, 4 * sizeof(uint32_t)) != 0;
        std::memcpy(constants.edram_stencil_back, constants.edram_stencil_front,
                    4 * sizeof(uint32_t));
      }
    }

    dirty |= constants.edram_blend_constant[0] != regs.Get<float>(XE_GPU_REG_RB_BLEND_RED);
    constants.edram_blend_constant[0] = regs.Get<float>(XE_GPU_REG_RB_BLEND_RED);
    dirty |=
        constants.edram_blend_constant[1] != regs.Get<float>(XE_GPU_REG_RB_BLEND_GREEN);
    constants.edram_blend_constant[1] = regs.Get<float>(XE_GPU_REG_RB_BLEND_GREEN);
    dirty |= constants.edram_blend_constant[2] != regs.Get<float>(XE_GPU_REG_RB_BLEND_BLUE);
    constants.edram_blend_constant[2] = regs.Get<float>(XE_GPU_REG_RB_BLEND_BLUE);
    dirty |=
        constants.edram_blend_constant[3] != regs.Get<float>(XE_GPU_REG_RB_BLEND_ALPHA);
    constants.edram_blend_constant[3] = regs.Get<float>(XE_GPU_REG_RB_BLEND_ALPHA);
  }

  return dirty;
}

void ComputeBlendFactorAndStencilReference(const RegisterFile& regs, bool primitive_polygonal,
                                           reg::RB_DEPTHCONTROL normalized_depth_control,
                                           float blend_factor_out[4],
                                           uint32_t& stencil_reference_out) {
  blend_factor_out[0] = regs.Get<float>(XE_GPU_REG_RB_BLEND_RED);
  blend_factor_out[1] = regs.Get<float>(XE_GPU_REG_RB_BLEND_GREEN);
  blend_factor_out[2] = regs.Get<float>(XE_GPU_REG_RB_BLEND_BLUE);
  blend_factor_out[3] = regs.Get<float>(XE_GPU_REG_RB_BLEND_ALPHA);
  // Per-face reference not supported by Direct3D 12, choose the back face one
  // only if drawing only back faces.
  Register stencil_ref_mask_reg;
  auto pa_su_sc_mode_cntl = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
  if (primitive_polygonal && normalized_depth_control.backface_enable &&
      pa_su_sc_mode_cntl.cull_front && !pa_su_sc_mode_cntl.cull_back) {
    stencil_ref_mask_reg = XE_GPU_REG_RB_STENCILREFMASK_BF;
  } else {
    stencil_ref_mask_reg = XE_GPU_REG_RB_STENCILREFMASK;
  }
  stencil_reference_out = regs.Get<reg::RB_STENCILREFMASK>(stencil_ref_mask_reg).stencilref;
}

void ComputeViewportAndScissor(const RegisterFile& regs, uint32_t flags, uint32_t scale_x,
                               uint32_t scale_y, DrawScale scene_scale,
                               reg::RB_DEPTHCONTROL normalized_depth_control,
                               bool pixel_shader_writes_depth, draw_util::ViewportInfo& viewport,
                               draw_util::Scissor& scissor) {
  if (flags & kFlagSceneScale) {
    draw_util::GetHostViewportInfo(regs, scene_scale, true, D3D12_VIEWPORT_BOUNDS_MAX,
                                   D3D12_VIEWPORT_BOUNDS_MAX, false, normalized_depth_control,
                                   (flags & kFlagConvertZToFloat24) != 0,
                                   (flags & kFlagHostRenderTargets) != 0,
                                   pixel_shader_writes_depth, viewport);
    draw_util::GetScissor(regs, scissor);
    draw_util::ScaleScissor(scene_scale, scissor);
    return;
  }
  draw_util::GetHostViewportInfo(regs, scale_x, scale_y, true, D3D12_VIEWPORT_BOUNDS_MAX,
                                 D3D12_VIEWPORT_BOUNDS_MAX, false, normalized_depth_control,
                                 (flags & kFlagConvertZToFloat24) != 0,
                                 (flags & kFlagHostRenderTargets) != 0, pixel_shader_writes_depth,
                                 viewport);
  draw_util::GetScissor(regs, scissor);
  scissor.offset[0] *= scale_x;
  scissor.offset[1] *= scale_y;
  scissor.extent[0] *= scale_x;
  scissor.extent[1] *= scale_y;
}

}  // namespace rex::graphics::d3d12::draw_back_end

namespace rex::graphics::d3d12 {

namespace draw_back_end {

constexpr uint32_t kGroupFixedSizes[kGroupCount] = {
    sizeof(GroupShaders), sizeof(GroupPrimitive), sizeof(GroupDraw), sizeof(GroupTargets),
    sizeof(GroupFlags),   0,                      0,                 0,
    kKnobCount * 4};

uint32_t TexturesGroupSize(uint32_t texture_count) {
  return uint32_t(sizeof(GroupTexturesHeader)) + ((texture_count + 3) & ~3u) + texture_count * 4;
}

void Decode(const uint8_t* arguments, RegisterFile& shadow, ListState& list, Decoded& decoded) {
  std::memcpy(&decoded.header, arguments, sizeof(Header));
  const Header& header = decoded.header;
  const uint8_t* in = arguments + sizeof(Header);
  uint64_t context_masks[kContextWords] = {};
  for (uint32_t word = 0; word < kContextWords; ++word) {
    if (header.context_words & (1u << word)) {
      std::memcpy(&context_masks[word], in, 8);
      in += 8;
    }
  }
  std::memset(decoded.constant_slots, 0, sizeof(decoded.constant_slots));
  for (uint32_t word = 0; word < deferred_constants::kMaskWords; ++word) {
    if (header.constant_words & (1u << word)) {
      std::memcpy(&decoded.constant_slots[word], in, 8);
      in += 8;
    }
  }
  uint32_t* context = shadow.values + kContextFirst;
  for (uint32_t word = 0; word < kContextWords; ++word) {
    uint64_t bits = context_masks[word];
    while (bits) {
      const uint32_t bit = uint32_t(__builtin_ctzll(bits));
      bits &= bits - 1;
      std::memcpy(context + (word << 6) + bit, in, 4);
      in += 4;
    }
  }
  uint32_t* constants = shadow.values + deferred_constants::kFirstRegister;
  for (uint32_t word = 0; word < deferred_constants::kMaskWords; ++word) {
    uint64_t bits = decoded.constant_slots[word];
    while (bits) {
      const uint32_t slot = (word << 6) + uint32_t(__builtin_ctzll(bits));
      bits &= bits - 1;
      std::memcpy(constants + slot * 4, in, 16);
      in += 16;
    }
  }
  const auto group = [&](Group index, void* out) {
    if (header.groups & (1u << index)) {
      std::memcpy(out, in, kGroupFixedSizes[index]);
      in += kGroupFixedSizes[index];
    }
  };
  group(kGroupShaders, &list.shaders);
  group(kGroupPrimitive, &list.primitive);
  group(kGroupDraw, &list.draw);
  group(kGroupTargets, &list.targets);
  group(kGroupFlags, &list.flags);
  if (header.groups & (1u << kGroupTextures)) {
    GroupTexturesHeader textures_header;
    std::memcpy(&textures_header, in, sizeof(textures_header));
    in += sizeof(textures_header);
    list.used_texture_mask = textures_header.used_texture_mask;
    list.textures_resolution_scaled = textures_header.textures_resolution_scaled;
    const uint8_t* signs = in;
    in += (header.texture_count + 3) & ~3u;
    uint32_t remaining = list.used_texture_mask;
    while (remaining) {
      const uint32_t index = uint32_t(__builtin_ctz(remaining));
      remaining &= remaining - 1;
      list.texture_swizzled_signs[index] = *signs++;
      std::memcpy(&list.texture_integer_scale_bits[index], in, 4);
      in += 4;
    }
  }
  for (uint32_t stage = 0; stage < 2; ++stage) {
    decoded.texture_indices[stage] = nullptr;
    if (header.groups & (1u << (kGroupTextureIndicesVertex + stage))) {
      decoded.texture_indices[stage] = reinterpret_cast<const uint32_t*>(in);
      in += header.texture_index_counts[stage] * 4;
    }
  }
  decoded.knobs = nullptr;
  if (header.groups & (1u << kGroupKnobs)) {
    decoded.knobs = reinterpret_cast<const float*>(in);
    in += kKnobCount * 4;
  }
  decoded.check = in;
}

}  // namespace draw_back_end

namespace {

constexpr const char* kMismatchNames[draw_back_end::kMismatchCount] = {
    "pipeline description", "viewport", "scissor", "blend factor or stencil reference",
    "system constants", "sampler parameters", "constant buffers"};

void LogMismatch(draw_back_end::Mismatch kind, uint64_t vertex_shader_hash) {
  static std::atomic<uint32_t> logged{0};
  if (logged.fetch_add(1, std::memory_order_relaxed) < 16) {
    REXGPU_ERROR("Draw back end: the {} differ from the draw's (vertex shader {:016X})",
                 kMismatchNames[kind], vertex_shader_hash);
  }
}

// The fetch constant slots a shader reads (bit n: slot kFetchFirstSlot + n):
// its vertex fetch constants and the texture fetch constants of its textures
// and samplers.
uint64_t ShaderFetchSlots(const D3D12Shader& shader) {
  uint64_t slots = 0;
  const auto add = [&slots](uint32_t first_dword, uint32_t dword_count) {
    const uint32_t first = first_dword >> 2;
    const uint32_t last = (first_dword + dword_count - 1) >> 2;
    slots |= ((uint64_t(1) << (last - first + 1)) - 1) << first;
  };
  const auto& map = shader.constant_register_map();
  for (uint32_t i = 0; i < 3; ++i) {
    uint32_t bits = map.vertex_fetch_bitmap[i];
    while (bits) {
      add((i * 32 + uint32_t(__builtin_ctz(bits))) * 2, 2);
      bits &= bits - 1;
    }
  }
  for (const D3D12Shader::TextureBinding& binding : shader.GetTextureBindingsAfterTranslation()) {
    add(binding.fetch_constant * 6, 6);
  }
  for (const D3D12Shader::SamplerBinding& binding : shader.GetSamplerBindingsAfterTranslation()) {
    add(binding.fetch_constant * 6, 6);
  }
  return slots;
}

// ShaderFetchSlots, computed at the shader's first capture (its bindings are
// known after its translation) and kept in the shader.
uint64_t BackEndFetchSlots(D3D12Shader& shader) {
  if (!shader.back_end_fetch_slots_known()) {
    shader.SetBackEndFetchSlots(ShaderFetchSlots(shader));
  }
  return shader.back_end_fetch_slots();
}

// The float constants of a stage's map, packed from the register file.
uint32_t PackFloatConstants(const RegisterFile& regs, uint32_t stage, const uint64_t map[4],
                            uint8_t* out) {
  return deferred_constants::PackFloatConstants(
      regs.values + XE_GPU_REG_SHADER_CONSTANT_000_X + stage * 256 * 4, map, out);
}

}  // namespace

void D3D12CommandProcessor::InitializeDrawBackEnd() {
  draw_back_end_ = uint32_t(REXCVAR_GET(d3d12_draw_back_end));
  if (draw_back_end_ == 2 &&
      (!bindless_resources_used_ ||
       render_target_cache_->GetPath() != RenderTargetCache::Path::kHostRenderTargets ||
       REXCVAR_GET(async_shader_compilation))) {
    REXGPU_INFO(
        "Draw back end: the replay needs bindless resources, host render targets and "
        "async_shader_compilation off; off");
    draw_back_end_ = 0;
  }
  draw_back_end_check_ = draw_back_end_ && REXCVAR_GET(d3d12_draw_back_end_check);
  draw_back_end_perturb_ =
      draw_back_end_check_ ? uint32_t(REXCVAR_GET(d3d12_draw_back_end_check_perturb)) : 0;
  draw_back_end_checks_ = 0;
  track_constants_ = deferred_constants_ || draw_back_end_;
  track_register_writes_ = draw_back_end_ != 0;
  back_end_list_captured_ = false;
  if (draw_back_end_ == 2) {
    back_end_constant_pool_ = std::make_unique<ui::d3d12::D3D12UploadBufferPool>(
        GetD3D12Provider(),
        std::max(ui::d3d12::D3D12UploadBufferPool::kDefaultPageSize,
                 sizeof(float) * 4 * D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT));
  }
  if (draw_back_end_) {
    REXGPU_INFO("Draw back end: {}{}{}", draw_back_end_ == 2 ? "on the replay" : "capture",
                draw_back_end_check_ ? ", checked" : "",
                draw_back_end_perturb_ ? ", perturbed" : "");
  }
}

void D3D12CommandProcessor::PrepareDrawBackEndInputs(const D3D12Shader* vertex_shader,
                                                     const D3D12Shader* pixel_shader) {
  // The texture SRV indices of each stage, when the texture cache's bindings
  // of its textures changed (as UpdateBindings decides for the bindless
  // descriptor index buffer), and always at a list's first draw.
  for (uint32_t stage = 0; stage < 2; ++stage) {
    const D3D12Shader* shader = stage ? pixel_shader : vertex_shader;
    std::vector<uint32_t>& indices = back_end_texture_indices_[stage];
    const size_t layout_uid =
        shader ? shader->GetTextureBindingLayoutUserUID() : PipelineCache::kLayoutUIDEmpty;
    size_t& current_layout_uid =
        stage ? current_texture_layout_uid_pixel_ : current_texture_layout_uid_vertex_;
    std::vector<D3D12TextureCache::TextureSRVKey>& current_keys =
        stage ? current_texture_srv_keys_pixel_ : current_texture_srv_keys_vertex_;
    if (!shader) {
      if (!back_end_list_captured_ || !indices.empty()) {
        indices.clear();
        back_end_texture_indices_changed_[stage] = true;
      }
      current_layout_uid = layout_uid;
      continue;
    }
    const std::vector<D3D12Shader::TextureBinding>& textures =
        shader->GetTextureBindingsAfterTranslation();
    const size_t texture_count = textures.size();
    if (back_end_list_captured_ && current_layout_uid == layout_uid &&
        indices.size() == texture_count &&
        (!texture_count || texture_cache_->AreActiveTextureSRVKeysUpToDate(
                               current_keys.data(), textures.data(), texture_count))) {
      continue;
    }
    indices.resize(texture_count);
    for (size_t i = 0; i < texture_count; ++i) {
      indices[i] = texture_cache_->GetActiveTextureBindlessSRVIndex(textures[i]) -
                   uint32_t(SystemBindlessView::kUnboundedSRVsStart);
    }
    current_layout_uid = layout_uid;
    if (texture_count) {
      current_keys.resize(std::max(current_keys.size(), texture_count));
      texture_cache_->WriteActiveTextureSRVKeys(current_keys.data(), textures.data(),
                                                texture_count);
    }
    back_end_texture_indices_changed_[stage] = true;
  }
  // The shader mods' live knobs. The change flags stay set until a capture
  // takes them (a draw may end before its capture).
  if (shader_mod_knobs_source_ &&
      shader_mod_knobs_source_->UpdateKnobs(shader_mod_knobs_, shader_mod_knobs_generation_)) {
    back_end_knobs_changed_ = true;
  }
}

void D3D12CommandProcessor::InvalidateStateAfterDrawBackEnd() {
  // The replay set the graphics state of this draw; whatever the front half
  // records next sets its own again.
  current_graphics_root_signature_ = nullptr;
  current_graphics_root_up_to_date_ = 0;
  current_guest_pipeline_ = nullptr;
  current_external_pipeline_ = nullptr;
  primitive_topology_ = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
  ff_viewport_update_needed_ = true;
  ff_scissor_update_needed_ = true;
  ff_blend_factor_update_needed_ = true;
  ff_stencil_ref_update_needed_ = true;
}

void D3D12CommandProcessor::CaptureDrawBackEnd(const DrawBackEndCapture& capture) {
  namespace be = draw_back_end;
  // The trace's capture time: every 16th capture, its ticks counted 16 times
  // (the time stamps of every capture would cost about a tenth of it), and
  // every capture with REX_N4_CAPTURE_SPLIT, which also times each part.
  const bool traced = perf_trace::Get() != nullptr;
  const bool split = perf_trace::CaptureSplitEnabled();
  const uint32_t time_scale =
      split ? 1 : (traced && (++back_end_capture_samples_ & 15) == 0 ? 16 : 0);
  const uint64_t start = time_scale ? __rdtsc() : 0;
  uint64_t split_tsc[perf_trace::kCaptureSplitParts] = {};
  uint64_t split_last = start;
  const auto split_part = [&](uint32_t part) {
    if (split) {
      const uint64_t now = __rdtsc();
      split_tsc[part] = now - split_last;
      split_last = now;
    }
  };
  const bool replay_draw = draw_back_end_ == 2;
  const RegisterFile& regs = *register_file_;
  const uint32_t* context = regs.values + be::kContextFirst;
  constexpr uint32_t kFirstWord = be::kContextFirst >> 6;
  const bool list_start = !back_end_list_captured_;

  // The context registers the back end reads that changed since they were
  // last captured into this list; all of them at the list's first capture.
  uint64_t context_masks[be::kContextWords];
  uint16_t context_words = 0;
  uint32_t context_count = 0;
  uint32_t written_count = 0;
  for (uint32_t word = 0; word < be::kContextWords; ++word) {
    uint64_t written = written_registers_[kFirstWord + word];
    written_registers_[kFirstWord + word] = 0;
    if (traced) {
      written_count += uint32_t(__builtin_popcountll(written));
    }
    uint64_t changed;
    if (list_start) {
      changed = be::kBackEndRegisters[word];
      std::memcpy(back_end_context_ + (word << 6), context + (word << 6), 64 * 4);
    } else {
      written &= be::kBackEndRegisters[word];
      changed = 0;
      while (written) {
        // Without a branch on the comparison: few of the written registers
        // change.
        const uint32_t bit = uint32_t(__builtin_ctzll(written));
        written &= written - 1;
        const uint32_t index = (word << 6) + bit;
        const uint32_t value = context[index];
        changed |= uint64_t(value != back_end_context_[index]) << bit;
        back_end_context_[index] = value;
      }
    }
    context_masks[word] = changed;
    if (changed) {
      context_words |= uint16_t(1u << word);
      context_count += uint32_t(__builtin_popcountll(changed));
    }
  }
  split_part(0);

  // The constant registers the back end reads, written since they were last
  // captured into this list (the pending mask is shared with
  // d3d12_deferred_constants, whose commands fill the same copy): with the
  // replay, the float constants of both shaders' maps, the fetch constants
  // both shaders read (the others stay pending until a draw reads them) and
  // the bool and loop constants; for increment 1's check, the fetch constants
  // of the samplers.
  auto& vertex_shader = static_cast<D3D12Shader&>(capture.vertex_translation->shader());
  D3D12Shader* pixel_shader =
      capture.pixel_translation ? &static_cast<D3D12Shader&>(capture.pixel_translation->shader())
                                : nullptr;
  constexpr uint32_t kFetchWord = deferred_constants::kFetchFirstSlot >> 6;
  static_assert(deferred_constants::kFetchFirstSlot % 64 == 0 &&
                deferred_constants::kFetchSlotCount <= 64);
  uint64_t constants_needed[deferred_constants::kMaskWords] = {};
  if (replay_draw) {
    const uint64_t* map_vertex = vertex_shader.constant_register_map().float_bitmap;
    for (uint32_t i = 0; i < 4; ++i) {
      constants_needed[i] = map_vertex[i];
    }
    if (pixel_shader) {
      const uint64_t* map_pixel = pixel_shader->constant_register_map().float_bitmap;
      for (uint32_t i = 0; i < 4; ++i) {
        constants_needed[4 + i] = map_pixel[i];
      }
    }
    constants_needed[kFetchWord] =
        BackEndFetchSlots(vertex_shader) | (pixel_shader ? BackEndFetchSlots(*pixel_shader) : 0);
    deferred_constants::SetSlots(
        constants_needed, deferred_constants::kBoolLoopFirstSlot,
        deferred_constants::kBoolLoopFirstSlot + deferred_constants::kBoolLoopSlotCount - 1);
  } else {
    uint64_t fetch_needed = 0;
    const auto add_samplers = [&fetch_needed](const D3D12Shader& shader) {
      for (const D3D12Shader::SamplerBinding& binding :
           shader.GetSamplerBindingsAfterTranslation()) {
        const uint32_t first = (binding.fetch_constant * 6) >> 2;
        const uint32_t last = (binding.fetch_constant * 6 + 5) >> 2;
        fetch_needed |= ((uint64_t(1) << (last - first + 1)) - 1) << first;
      }
    };
    add_samplers(vertex_shader);
    if (pixel_shader) {
      add_samplers(*pixel_shader);
    }
    constants_needed[kFetchWord] = fetch_needed;
  }
  uint64_t constant_masks[deferred_constants::kMaskWords];
  uint16_t constant_words = 0;
  uint32_t constant_count = 0;
  for (uint32_t word = 0; word < deferred_constants::kMaskWords; ++word) {
    constant_masks[word] = pending_constants_.slots[word] & constants_needed[word];
    pending_constants_.slots[word] &= ~constant_masks[word];
    if (constant_masks[word]) {
      constant_words |= uint16_t(1u << word);
      constant_count += uint32_t(__builtin_popcountll(constant_masks[word]));
    }
  }
  split_part(1);

  // The front half's groups that changed since the list's previous capture,
  // compared with the list's state and written into it in place.
  be::ListState& front = back_end_front_;
  uint16_t groups = 0;
  uint32_t group_bytes = 0;
  const auto update_group = [&](be::Group index, auto& stored, const auto& value) {
    static_assert(sizeof(stored) == sizeof(value));
    if (list_start || std::memcmp(&stored, &value, sizeof(value))) {
      std::memcpy(&stored, &value, sizeof(value));
      groups |= uint16_t(1u << index);
      group_bytes += uint32_t(sizeof(value));
    }
  };
  const be::GroupShaders shaders = {capture.vertex_translation, capture.pixel_translation};
  update_group(be::kGroupShaders, front.shaders, shaders);
  const PrimitiveProcessor::ProcessingResult& primitive = *capture.primitive;
  be::GroupPrimitive primitive_group = {};
  primitive_group.host_primitive_type = uint8_t(primitive.host_primitive_type);
  primitive_group.host_vertex_shader_type = uint8_t(primitive.host_vertex_shader_type);
  primitive_group.tessellation_mode = uint8_t(primitive.tessellation_mode);
  primitive_group.host_index_format = uint8_t(primitive.host_index_format);
  primitive_group.host_primitive_reset_enabled = primitive.host_primitive_reset_enabled;
  primitive_group.host_shader_index_endian = uint8_t(primitive.host_shader_index_endian);
  primitive_group.index_buffer_type = uint8_t(primitive.index_buffer_type);
  primitive_group.line_loop_closing_index = primitive.line_loop_closing_index;
  update_group(be::kGroupPrimitive, front.primitive, primitive_group);
  be::GroupDraw draw = {};
  if (replay_draw) {
    draw.index_buffer_gpu_address = capture.index_buffer.BufferLocation;
    draw.index_buffer_size = capture.index_buffer.SizeInBytes;
    draw.index_buffer_format = uint32_t(capture.index_buffer.Format);
  } else {
    // Increment 1 captures the guest index base in place of the address.
    draw.index_buffer_gpu_address = primitive.guest_index_base;
  }
  draw.host_draw_vertex_count = primitive.host_draw_vertex_count;
  update_group(be::kGroupDraw, front.draw, draw);
  be::GroupTargets targets = {};
  targets.bits = capture.bound_render_target_bits;
  for (uint32_t i = 0; i < 5; ++i) {
    targets.formats[i] =
        (capture.bound_render_target_bits >> i) & 1 ? capture.bound_render_target_formats[i] : 0;
  }
  update_group(be::kGroupTargets, front.targets, targets);
  be::GroupFlags flags_group = {};
  flags_group.flags = capture.flags | (shader_mod_knobs_source_ ? be::kFlagKnobs : 0);
  flags_group.draw_resolution_scale_x = uint16_t(texture_cache_->draw_resolution_scale_x());
  flags_group.draw_resolution_scale_y = uint16_t(texture_cache_->draw_resolution_scale_y());
  if (capture.flags & be::kFlagJitter) {
    flags_group.jitter[0] = capture.jitter[0];
    flags_group.jitter[1] = capture.jitter[1];
  }
  if (replay_draw) {
    flags_group.frame = frame_current_;
    flags_group.frame_completed = frame_completed_;
    flags_group.submission = submission_current_;
  }
  update_group(be::kGroupFlags, front.flags, flags_group);
  // The texture cache's answers: the entries of the used textures only (the
  // decoder keeps the others as they were too).
  const be::SystemConstantInputs& inputs = system_constant_inputs_;
  const uint32_t used_texture_mask = inputs.used_texture_mask;
  const uint32_t texture_count = uint32_t(__builtin_popcount(used_texture_mask));
  bool textures_changed = list_start || front.used_texture_mask != used_texture_mask ||
                          front.textures_resolution_scaled != inputs.textures_resolution_scaled;
  {
    uint32_t remaining = used_texture_mask;
    while (remaining) {
      const uint32_t index = uint32_t(__builtin_ctz(remaining));
      remaining &= remaining - 1;
      if (front.texture_swizzled_signs[index] != inputs.texture_swizzled_signs[index] ||
          front.texture_integer_scale_bits[index] != inputs.texture_integer_scale_bits[index]) {
        front.texture_swizzled_signs[index] = inputs.texture_swizzled_signs[index];
        front.texture_integer_scale_bits[index] = inputs.texture_integer_scale_bits[index];
        textures_changed = true;
      }
    }
  }
  if (textures_changed) {
    front.used_texture_mask = used_texture_mask;
    front.textures_resolution_scaled = inputs.textures_resolution_scaled;
    groups |= 1 << be::kGroupTextures;
    group_bytes += be::TexturesGroupSize(texture_count);
  }
  uint16_t texture_index_counts[2] = {};
  if (replay_draw) {
    for (uint32_t stage = 0; stage < 2; ++stage) {
      if (back_end_texture_indices_changed_[stage]) {
        groups |= 1 << (be::kGroupTextureIndicesVertex + stage);
        texture_index_counts[stage] = uint16_t(back_end_texture_indices_[stage].size());
        group_bytes += texture_index_counts[stage] * 4;
      }
    }
    if (shader_mod_knobs_source_ && (list_start || back_end_knobs_changed_)) {
      groups |= 1 << be::kGroupKnobs;
      group_bytes += be::kKnobCount * 4;
    }
  }
  split_part(2);

  // The check: the back end's results at the draw.
  uint16_t flags = (list_start ? be::kListStart : 0) | (replay_draw ? be::kReplayDraw : 0);
  uint32_t sampler_count_vertex = 0, sampler_count_pixel = 0;
  uint32_t check_bytes = 0;
  uint32_t constant_bytes[4] = {};
  if (draw_back_end_check_ &&
      pipeline_cache_->GetPipelineDescriptionSize() <= sizeof(be::Expected::pipeline_description)) {
    flags |= be::kCheck;
    if (draw_back_end_perturb_ && ++draw_back_end_checks_ % draw_back_end_perturb_ == 0) {
      flags |= be::kCheckPerturb;
    }
    sampler_count_vertex = uint32_t(vertex_shader.GetSamplerBindingsAfterTranslation().size());
    sampler_count_pixel =
        pixel_shader ? uint32_t(pixel_shader->GetSamplerBindingsAfterTranslation().size()) : 0;
    check_bytes = uint32_t(sizeof(be::Expected)) + 4 * (sampler_count_vertex + sampler_count_pixel);
    if (replay_draw) {
      constant_bytes[0] =
          16 * uint32_t(vertex_shader.constant_register_map().float_count);
      constant_bytes[1] =
          pixel_shader ? 16 * uint32_t(pixel_shader->constant_register_map().float_count) : 0;
      constant_bytes[2] = deferred_constants::kBoolLoopSlotCount * 16;
      constant_bytes[3] = deferred_constants::kFetchSlotCount * 16;
      check_bytes += constant_bytes[0] + constant_bytes[1] + constant_bytes[2] + constant_bytes[3];
    }
  }

  // The record: the header, the masks, the changed context registers, the
  // constant slots, the changed groups, then the check's part.
  const uint32_t bytes = uint32_t(sizeof(be::Header)) +
                         uint32_t(__builtin_popcount(context_words)) * 8 +
                         uint32_t(__builtin_popcount(constant_words)) * 8 + context_count * 4 +
                         constant_count * 16 + group_bytes;
  auto* arguments =
      static_cast<uint8_t*>(deferred_command_list_.DrawBackEnd(bytes + check_bytes));
  split_part(3);
  be::Header header = {};
  header.context_words = context_words;
  header.constant_words = constant_words;
  header.groups = groups;
  header.flags = flags;
  header.context_count = context_count;
  header.constant_count = constant_count;
  header.texture_count = uint16_t(texture_count);
  header.texture_index_counts[0] = texture_index_counts[0];
  header.texture_index_counts[1] = texture_index_counts[1];
  uint8_t* out = arguments;
  std::memcpy(out, &header, sizeof(header));
  out += sizeof(header);
  for (uint32_t word = 0; word < be::kContextWords; ++word) {
    if (context_words & (1u << word)) {
      std::memcpy(out, &context_masks[word], 8);
      out += 8;
    }
  }
  for (uint32_t word = 0; word < deferred_constants::kMaskWords; ++word) {
    if (constant_words & (1u << word)) {
      std::memcpy(out, &constant_masks[word], 8);
      out += 8;
    }
  }
  for (uint32_t word = 0; word < be::kContextWords; ++word) {
    uint64_t bits = context_masks[word];
    while (bits) {
      const uint32_t bit = uint32_t(__builtin_ctzll(bits));
      bits &= bits - 1;
      std::memcpy(out, back_end_context_ + (word << 6) + bit, 4);
      out += 4;
    }
  }
  const uint32_t* constants = regs.values + deferred_constants::kFirstRegister;
  for (uint32_t word = 0; word < deferred_constants::kMaskWords; ++word) {
    uint64_t bits = constant_masks[word];
    while (bits) {
      const uint32_t slot = (word << 6) + uint32_t(__builtin_ctzll(bits));
      bits &= bits - 1;
      std::memcpy(out, constants + slot * 4, 16);
      out += 16;
    }
  }
  split_part(4);
  const auto write_group = [&](be::Group index, const auto& stored) {
    if (groups & (1u << index)) {
      std::memcpy(out, &stored, sizeof(stored));
      out += sizeof(stored);
    }
  };
  write_group(be::kGroupShaders, front.shaders);
  write_group(be::kGroupPrimitive, front.primitive);
  write_group(be::kGroupDraw, front.draw);
  write_group(be::kGroupTargets, front.targets);
  write_group(be::kGroupFlags, front.flags);
  if (groups & (1 << be::kGroupTextures)) {
    be::GroupTexturesHeader textures_header = {front.used_texture_mask,
                                               front.textures_resolution_scaled};
    std::memcpy(out, &textures_header, sizeof(textures_header));
    out += sizeof(textures_header);
    uint8_t* signs = out;
    out += (texture_count + 3) & ~3u;
    std::memset(signs, 0, out - signs);
    uint32_t remaining = front.used_texture_mask;
    while (remaining) {
      const uint32_t index = uint32_t(__builtin_ctz(remaining));
      remaining &= remaining - 1;
      *signs++ = front.texture_swizzled_signs[index];
      std::memcpy(out, &front.texture_integer_scale_bits[index], 4);
      out += 4;
    }
  }
  for (uint32_t stage = 0; stage < 2; ++stage) {
    if (groups & (1 << (be::kGroupTextureIndicesVertex + stage))) {
      std::memcpy(out, back_end_texture_indices_[stage].data(), texture_index_counts[stage] * 4);
      out += texture_index_counts[stage] * 4;
    }
  }
  if (groups & (1 << be::kGroupKnobs)) {
    static_assert(ShaderOverrides::kKnobCount == be::kKnobCount);
    std::memcpy(out, shader_mod_knobs_, be::kKnobCount * 4);
    out += be::kKnobCount * 4;
  }
  if (flags & be::kCheck) {
    be::Expected expected;
    std::memset(&expected, 0, sizeof(expected));
    expected.sampler_count_vertex = sampler_count_vertex;
    expected.sampler_count_pixel = sampler_count_pixel;
    std::memcpy(expected.constant_bytes, constant_bytes, sizeof(constant_bytes));
    if (!replay_draw) {
      // Increment 1: the results of the back end that ran at this draw.
      std::memcpy(expected.pipeline_description,
                  pipeline_cache_->GetPipelineDescription(capture.pipeline_handle),
                  pipeline_cache_->GetPipelineDescriptionSize());
      expected.viewport = *capture.viewport;
      expected.scissor = *capture.scissor;
      std::memcpy(expected.blend_factor, ff_blend_factor_, sizeof(expected.blend_factor));
      expected.stencil_reference = ff_stencil_ref_;
      expected.system_constants = system_constants_;
    } else {
      // Increment 2: the same functions on the live registers at the draw.
      const reg::RB_DEPTHCONTROL depth_control = draw_util::GetNormalizedDepthControl(regs);
      const uint32_t color_mask =
          pixel_shader ? draw_util::GetNormalizedColorMask(regs, pixel_shader->writes_color_targets())
                       : 0;
      const bool polygonal = draw_util::IsPrimitivePolygonal(regs);
      pipeline_cache_->ComputePipelineDescription(
          regs, capture.vertex_translation, capture.pixel_translation, primitive, depth_control,
          color_mask, front.targets.bits, front.targets.formats, expected.pipeline_description);
      const uint32_t uniform_scale_x = (capture.flags >> be::kFlagUniformScaleShift) & 3;
      const uint32_t uniform_scale_y = (capture.flags >> (be::kFlagUniformScaleShift + 2)) & 3;
      const uint32_t scale_x = front.flags.draw_resolution_scale_x * uniform_scale_x;
      const uint32_t scale_y = front.flags.draw_resolution_scale_y * uniform_scale_y;
      be::ComputeViewportAndScissor(regs, capture.flags, scale_x, scale_y,
                                    render_target_cache_->scene_draw_scale(), depth_control,
                                    pixel_shader && pixel_shader->writes_depth(),
                                    expected.viewport, expected.scissor);
      if (capture.flags & be::kFlagJitter) {
        post_pass::AddJitterToNdcOffset(capture.jitter, expected.viewport.xy_extent,
                                        expected.viewport.ndc_offset);
      }
      be::ComputeBlendFactorAndStencilReference(regs, polygonal, depth_control,
                                                expected.blend_factor, expected.stencil_reference);
      be::ComputeSystemConstants(regs, expected.viewport, inputs, system_constants_);
      expected.system_constants = system_constants_;
    }
    std::memcpy(out, &expected, sizeof(expected));
    out += sizeof(expected);
    for (uint32_t stage = 0; stage < 2; ++stage) {
      if (stage && !pixel_shader) {
        break;
      }
      const D3D12Shader& shader = stage ? *pixel_shader : vertex_shader;
      const auto& samplers = shader.GetSamplerBindingsAfterTranslation();
      for (size_t i = 0; i < samplers.size(); ++i) {
        const uint32_t value =
            replay_draw ? texture_cache_->GetSamplerParameters(regs, samplers[i]).value
                        : (stage ? current_samplers_pixel_[i] : current_samplers_vertex_[i]).value;
        std::memcpy(out, &value, 4);
        out += 4;
      }
    }
    if (replay_draw) {
      std::memset(out, 0, constant_bytes[0]);
      PackFloatConstants(regs, 0, vertex_shader.constant_register_map().float_bitmap, out);
      out += constant_bytes[0];
      if (pixel_shader) {
        std::memset(out, 0, constant_bytes[1]);
        PackFloatConstants(regs, 1, pixel_shader->constant_register_map().float_bitmap, out);
        out += constant_bytes[1];
      }
      std::memcpy(out, regs.values + XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031, constant_bytes[2]);
      out += constant_bytes[2];
      std::memcpy(out, regs.values + XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0, constant_bytes[3]);
      out += constant_bytes[3];
    }
  }
  assert_true(out == arguments + bytes + check_bytes);

  back_end_list_captured_ = true;
  if (replay_draw) {
    back_end_texture_indices_changed_[0] = back_end_texture_indices_changed_[1] = false;
    back_end_knobs_changed_ = false;
  }
  split_part(5);
  perf_trace::CountBackEndCapture(bytes, context_count, written_count, constant_count, group_bytes,
                                  time_scale ? (__rdtsc() - start) * time_scale : 0);
  if (split) {
    perf_trace::CaptureSplitCounts& counts = perf_trace::capture_split;
    for (uint32_t part = 0; part < perf_trace::kCaptureSplitParts; ++part) {
      counts.tsc[part] += split_tsc[part];
    }
    constexpr uint32_t kBoolLoopWord = deferred_constants::kBoolLoopFirstSlot >> 6;
    static_assert(deferred_constants::kBoolLoopFirstSlot % 64 == 0);
    for (uint32_t word = 0; word < kFetchWord; ++word) {
      counts.float_slots += uint32_t(__builtin_popcountll(constant_masks[word]));
    }
    counts.fetch_slots += uint32_t(__builtin_popcountll(constant_masks[kFetchWord]));
    counts.bool_loop_slots += uint32_t(__builtin_popcountll(constant_masks[kBoolLoopWord]));
  }
}

void D3D12CommandProcessor::ReplayDrawBackEnd(const uint8_t* arguments, RegisterFile& shadow,
                                              draw_back_end::ReplayList& replay,
                                              ID3D12GraphicsCommandList* command_list) {
  namespace be = draw_back_end;
  const uint64_t start = __rdtsc();
  be::Decoded decoded;
  be::Decode(arguments, shadow, replay.list, decoded);
  const be::Header& header = decoded.header;
  if (!(header.flags & (be::kReplayDraw | be::kCheck))) {
    // Increment 1 without the check: only the copy.
    if (perf_trace::Get()) {
      perf_trace::back_end_replay.replays.fetch_add(1, std::memory_order_relaxed);
      perf_trace::back_end_replay.tsc.fetch_add(__rdtsc() - start, std::memory_order_relaxed);
    }
    return;
  }
  const be::ListState& list = replay.list;
  const RegisterFile& regs = shadow;
  auto* vertex_translation = static_cast<D3D12Shader::D3D12Translation*>(list.shaders.vertex);
  auto* pixel_translation = static_cast<D3D12Shader::D3D12Translation*>(list.shaders.pixel);
  const auto& vertex_shader = static_cast<const D3D12Shader&>(vertex_translation->shader());
  const D3D12Shader* pixel_shader =
      pixel_translation ? &static_cast<const D3D12Shader&>(pixel_translation->shader()) : nullptr;
  const uint32_t flags = list.flags.flags;
  const reg::RB_DEPTHCONTROL depth_control = draw_util::GetNormalizedDepthControl(regs);
  const uint32_t color_mask =
      pixel_shader ? draw_util::GetNormalizedColorMask(regs, pixel_shader->writes_color_targets())
                   : 0;
  const bool polygonal = draw_util::IsPrimitivePolygonal(regs);
  PrimitiveProcessor::ProcessingResult primitive = {};
  primitive.host_primitive_type = xenos::PrimitiveType(list.primitive.host_primitive_type);
  primitive.host_vertex_shader_type =
      Shader::HostVertexShaderType(list.primitive.host_vertex_shader_type);
  primitive.tessellation_mode = xenos::TessellationMode(list.primitive.tessellation_mode);
  primitive.host_index_format = xenos::IndexFormat(list.primitive.host_index_format);
  primitive.host_primitive_reset_enabled = list.primitive.host_primitive_reset_enabled != 0;
  primitive.host_shader_index_endian = xenos::Endian(list.primitive.host_shader_index_endian);
  primitive.line_loop_closing_index = list.primitive.line_loop_closing_index;
  primitive.host_draw_vertex_count = list.draw.host_draw_vertex_count;
  const uint32_t uniform_scale_x = (flags >> be::kFlagUniformScaleShift) & 3;
  const uint32_t uniform_scale_y = (flags >> (be::kFlagUniformScaleShift + 2)) & 3;
  const uint32_t scale_x = list.flags.draw_resolution_scale_x * uniform_scale_x;
  const uint32_t scale_y = list.flags.draw_resolution_scale_y * uniform_scale_y;
  be::SystemConstantInputs inputs;
  inputs.edram_rov_used = (flags & be::kFlagEdramRov) != 0;
  inputs.rov_occlusion_query = (flags & be::kFlagRovOcclusionQuery) != 0;
  inputs.host_render_targets_used = (flags & be::kFlagHostRenderTargets) != 0;
  inputs.gamma_render_target_as_unorm16 = (flags & be::kFlagGammaAsUnorm16) != 0;
  inputs.fixed16_truncated_to_minus_1_to_1 = (flags & be::kFlagFixed16Truncated) != 0;
  inputs.shared_memory_is_uav = (flags & be::kFlagMemexport) != 0;
  inputs.primitive_polygonal = polygonal;
  inputs.index_endian = primitive.host_shader_index_endian;
  inputs.line_loop_closing_index = primitive.line_loop_closing_index;
  inputs.used_texture_mask = list.used_texture_mask;
  inputs.normalized_color_mask = color_mask;
  inputs.normalized_depth_control = depth_control;
  inputs.draw_resolution_scale_x = list.flags.draw_resolution_scale_x;
  inputs.draw_resolution_scale_y = list.flags.draw_resolution_scale_y;
  inputs.scene_draw = (flags & be::kFlagSceneScale) != 0;
  inputs.scene_scale = render_target_cache_->scene_draw_scale();
  std::memcpy(inputs.texture_swizzled_signs, list.texture_swizzled_signs,
              sizeof(inputs.texture_swizzled_signs));
  std::memcpy(inputs.texture_integer_scale_bits, list.texture_integer_scale_bits,
              sizeof(inputs.texture_integer_scale_bits));
  inputs.textures_resolution_scaled = list.textures_resolution_scaled;

  const bool check = (header.flags & be::kCheck) != 0;
  const bool perturb = (header.flags & be::kCheckPerturb) != 0;
  be::Expected expected;
  const uint8_t* check_in = decoded.check;
  if (check) {
    std::memcpy(&expected, check_in, sizeof(expected));
    check_in += sizeof(expected);
    if (perturb) {
      expected.pipeline_description[0] ^= 1;
    }
  }
  bool mismatches[be::kMismatchCount] = {};

  if (!(header.flags & be::kReplayDraw)) {
    // Increment 1: only the check, from the copy.
    if (perf_trace::Get()) {
      perf_trace::back_end_replay.replays.fetch_add(1, std::memory_order_relaxed);
      perf_trace::back_end_replay.tsc.fetch_add(__rdtsc() - start, std::memory_order_relaxed);
    }
    if (!check) {
      return;
    }
    uint8_t description[sizeof(be::Expected::pipeline_description)] = {};
    mismatches[be::kMismatchPipeline] =
        !pipeline_cache_->ComputePipelineDescription(regs, vertex_translation, pixel_translation,
                                                     primitive, depth_control, color_mask,
                                                     list.targets.bits, list.targets.formats,
                                                     description) ||
        std::memcmp(description, expected.pipeline_description,
                    pipeline_cache_->GetPipelineDescriptionSize()) != 0;
    draw_util::ViewportInfo viewport;
    draw_util::Scissor scissor;
    be::ComputeViewportAndScissor(regs, flags, scale_x, scale_y,
                                  render_target_cache_->scene_draw_scale(), depth_control,
                                  pixel_shader && pixel_shader->writes_depth(), viewport, scissor);
    if (flags & be::kFlagJitter) {
      post_pass::AddJitterToNdcOffset(list.flags.jitter, viewport.xy_extent, viewport.ndc_offset);
    }
    mismatches[be::kMismatchViewport] =
        std::memcmp(&viewport, &expected.viewport, sizeof(viewport)) != 0;
    mismatches[be::kMismatchScissor] =
        std::memcmp(&scissor, &expected.scissor, sizeof(scissor)) != 0;
    if (flags & be::kFlagHostRenderTargets) {
      float blend_factor[4];
      uint32_t stencil_reference;
      be::ComputeBlendFactorAndStencilReference(regs, polygonal, depth_control, blend_factor,
                                                stencil_reference);
      mismatches[be::kMismatchFixedFunction] =
          std::memcmp(blend_factor, expected.blend_factor, sizeof(blend_factor)) != 0 ||
          stencil_reference != expected.stencil_reference;
    }
    be::ComputeSystemConstants(regs, viewport, inputs, back_end_replay_constants_);
    mismatches[be::kMismatchSystemConstants] =
        std::memcmp(&back_end_replay_constants_, &expected.system_constants,
                    sizeof(back_end_replay_constants_)) != 0;
    for (uint32_t stage = 0; stage < 2; ++stage) {
      const uint32_t expected_count =
          stage ? expected.sampler_count_pixel : expected.sampler_count_vertex;
      const D3D12Shader* shader = stage ? pixel_shader : &vertex_shader;
      const size_t count = shader ? shader->GetSamplerBindingsAfterTranslation().size() : 0;
      if (count != expected_count) {
        mismatches[be::kMismatchSamplers] = true;
        break;
      }
      for (size_t i = 0; i < count; ++i) {
        uint32_t value;
        std::memcpy(&value, check_in, 4);
        check_in += 4;
        if (texture_cache_->GetSamplerParameters(regs, shader->GetSamplerBindingsAfterTranslation()[i])
                .value != value) {
          mismatches[be::kMismatchSamplers] = true;
        }
      }
    }
  } else {
    ReplayDrawBackEndDraw(decoded, regs, replay, command_list, vertex_shader, pixel_shader,
                          primitive, depth_control, color_mask, polygonal, scale_x, scale_y,
                          inputs, check ? &expected : nullptr, check_in, mismatches);
    if (perf_trace::Get()) {
      perf_trace::back_end_replay.replays.fetch_add(1, std::memory_order_relaxed);
      perf_trace::back_end_replay.tsc.fetch_add(__rdtsc() - start, std::memory_order_relaxed);
    }
    if (!check) {
      return;
    }
  }

  perf_trace::back_end_replay.checks.fetch_add(1, std::memory_order_relaxed);
  if (perturb) {
    perf_trace::back_end_replay.perturbed.fetch_add(1, std::memory_order_relaxed);
  }
  for (uint32_t kind = 0; kind < be::kMismatchCount; ++kind) {
    if (!mismatches[kind]) {
      continue;
    }
    perf_trace::back_end_replay.mismatches[kind].fetch_add(1, std::memory_order_relaxed);
    if (!(perturb && kind == be::kMismatchPipeline)) {
      LogMismatch(be::Mismatch(kind), vertex_shader.ucode_data_hash());
    }
  }
}

void D3D12CommandProcessor::ReplayDrawBackEndDraw(
    const draw_back_end::Decoded& decoded, const RegisterFile& regs,
    draw_back_end::ReplayList& replay, ID3D12GraphicsCommandList* command_list,
    const D3D12Shader& vertex_shader, const D3D12Shader* pixel_shader,
    const PrimitiveProcessor::ProcessingResult& primitive, reg::RB_DEPTHCONTROL depth_control,
    uint32_t color_mask, bool polygonal, uint32_t scale_x, uint32_t scale_y,
    const draw_back_end::SystemConstantInputs& inputs, const draw_back_end::Expected* expected,
    const uint8_t* check_in, bool* mismatches) {
  namespace be = draw_back_end;
  const be::Header& header = decoded.header;
  const be::ListState& list = replay.list;
  be::ReplayState& d3d = replay.d3d;
  be::ReplayBindings& bindings = replay.bindings;
  const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
  const uint32_t flags = list.flags.flags;
  auto* vertex_translation = static_cast<D3D12Shader::D3D12Translation*>(list.shaders.vertex);
  auto* pixel_translation = static_cast<D3D12Shader::D3D12Translation*>(list.shaders.pixel);

  // The front half's texture indices and knobs, and the buffers holding
  // registers that changed.
  for (uint32_t stage = 0; stage < 2; ++stage) {
    if (decoded.texture_indices[stage]) {
      replay.texture_indices[stage].assign(
          decoded.texture_indices[stage],
          decoded.texture_indices[stage] + header.texture_index_counts[stage]);
      bindings.indices_valid[stage] = false;
    }
  }
  if (decoded.knobs) {
    std::memcpy(replay.knobs, decoded.knobs, sizeof(replay.knobs));
    bindings.system_valid = false;
  }
  for (uint32_t i = 0; i < 4; ++i) {
    if (decoded.constant_slots[i] & bindings.float_maps[0][i]) {
      bindings.float_valid[0] = false;
    }
    if (decoded.constant_slots[4 + i] & bindings.float_maps[1][i]) {
      bindings.float_valid[1] = false;
    }
  }
  if (decoded.constant_slots[deferred_constants::kFetchFirstSlot >> 6] &
      ((uint64_t(1) << deferred_constants::kFetchSlotCount) - 1)) {
    bindings.fetch_valid = false;
  }
  if (decoded.constant_slots[deferred_constants::kBoolLoopFirstSlot >> 6] &
      (((uint64_t(1) << deferred_constants::kBoolLoopSlotCount) - 1)
       << (deferred_constants::kBoolLoopFirstSlot & 63))) {
    bindings.bool_loop_valid = false;
  }

  // The constant buffer pool, reclaimed by frame.
  if (list.flags.frame != back_end_pool_frame_) {
    back_end_constant_pool_->Reclaim(list.flags.frame_completed);
    back_end_pool_frame_ = list.flags.frame;
  }
  const auto request = [&](size_t size, uint64_t& address) -> uint8_t* {
    D3D12_GPU_VIRTUAL_ADDRESS gpu_address;
    uint8_t* mapping = back_end_constant_pool_->Request(
        list.flags.frame, size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, nullptr, nullptr,
        &gpu_address);
    address = gpu_address;
    return mapping;
  };

  // The pipeline.
  void* pipeline_handle;
  ID3D12RootSignature* root_signature;
  bool created = false;
  const uint64_t pipeline_start = __rdtsc();
  if (!pipeline_cache_->ConfigurePipelineDescribed(
          regs, vertex_translation, pixel_translation, primitive, depth_control, color_mask,
          list.targets.bits, list.targets.formats, false, &pipeline_handle, &root_signature,
          &created)) {
    perf_trace::back_end_replay.dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (created) {
    perf_trace::back_end_replay.pipelines.fetch_add(1, std::memory_order_relaxed);
    perf_trace::back_end_replay.pipeline_tsc.fetch_add(__rdtsc() - pipeline_start,
                                                       std::memory_order_relaxed);
  }
  ID3D12PipelineState* pipeline = pipeline_cache_->GetD3D12PipelineByHandle(pipeline_handle);
  if (pipeline && pipeline != d3d.pipeline) {
    command_list->SetPipelineState(pipeline);
    d3d.pipeline = pipeline;
  }

  // Viewport, scissor, blend factor and stencil reference.
  draw_util::ViewportInfo viewport_info;
  draw_util::Scissor scissor;
  be::ComputeViewportAndScissor(regs, flags, scale_x, scale_y,
                                render_target_cache_->scene_draw_scale(), depth_control,
                                pixel_shader && pixel_shader->writes_depth(), viewport_info,
                                scissor);
  if (flags & be::kFlagJitter) {
    post_pass::AddJitterToNdcOffset(list.flags.jitter, viewport_info.xy_extent,
                                    viewport_info.ndc_offset);
  }
  D3D12_VIEWPORT viewport;
  viewport.TopLeftX = float(viewport_info.xy_offset[0]);
  viewport.TopLeftY = float(viewport_info.xy_offset[1]);
  viewport.Width = float(viewport_info.xy_extent[0]);
  viewport.Height = float(viewport_info.xy_extent[1]);
  viewport.MinDepth = viewport_info.z_min;
  viewport.MaxDepth = viewport_info.z_max;
  if (!d3d.viewport_set || std::memcmp(&viewport, &d3d.viewport, sizeof(viewport))) {
    command_list->RSSetViewports(1, &viewport);
    d3d.viewport = viewport;
    d3d.viewport_set = true;
  }
  D3D12_RECT scissor_rect;
  scissor_rect.left = LONG(scissor.offset[0]);
  scissor_rect.top = LONG(scissor.offset[1]);
  scissor_rect.right = LONG(scissor.offset[0] + scissor.extent[0]);
  scissor_rect.bottom = LONG(scissor.offset[1] + scissor.extent[1]);
  if (!d3d.scissor_set || std::memcmp(&scissor_rect, &d3d.scissor, sizeof(scissor_rect))) {
    command_list->RSSetScissorRects(1, &scissor_rect);
    d3d.scissor = scissor_rect;
    d3d.scissor_set = true;
  }
  float blend_factor[4];
  uint32_t stencil_reference;
  be::ComputeBlendFactorAndStencilReference(regs, polygonal, depth_control, blend_factor,
                                            stencil_reference);
  if (!d3d.blend_factor_set || std::memcmp(blend_factor, d3d.blend_factor, sizeof(blend_factor))) {
    command_list->OMSetBlendFactor(blend_factor);
    std::memcpy(d3d.blend_factor, blend_factor, sizeof(blend_factor));
    d3d.blend_factor_set = true;
  }
  if (!d3d.stencil_reference_set || stencil_reference != d3d.stencil_reference) {
    command_list->OMSetStencilRef(stencil_reference);
    d3d.stencil_reference = stencil_reference;
    d3d.stencil_reference_set = true;
  }

  // The system constants.
  if (be::ComputeSystemConstants(regs, viewport_info, inputs, back_end_replay_constants_)) {
    bindings.system_valid = false;
  }
  const bool knobs = (flags & be::kFlagKnobs) != 0;
  if (!bindings.system_valid) {
    const size_t size = knobs ? ShaderOverrides::kKnobsOffset + ShaderOverrides::kKnobsSize
                              : sizeof(back_end_replay_constants_);
    uint8_t* mapping = request(size, bindings.system);
    if (!mapping) {
      perf_trace::back_end_replay.dropped.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    std::memcpy(mapping, &back_end_replay_constants_, sizeof(back_end_replay_constants_));
    if (knobs) {
      std::memset(mapping + sizeof(back_end_replay_constants_), 0,
                  ShaderOverrides::kKnobsOffset - sizeof(back_end_replay_constants_));
      std::memcpy(mapping + ShaderOverrides::kKnobsOffset, replay.knobs,
                  ShaderOverrides::kKnobsSize);
    }
    bindings.system_valid = true;
  }

  // The float, bool and loop, and fetch constant buffers, filled from the copy.
  // The check's part: Expected, the sampler parameters, the constant bytes.
  const uint8_t* expected_samplers = expected ? check_in : nullptr;
  const uint8_t* expected_constants =
      expected ? check_in + 4 * (expected->sampler_count_vertex + expected->sampler_count_pixel)
               : nullptr;
  alignas(16) uint8_t local[256 * 16];
  uint32_t filled = 0;
  for (uint32_t stage = 0; stage < 2; ++stage) {
    static constexpr uint64_t kNoMap[4] = {};
    const D3D12Shader* shader = stage ? pixel_shader : &vertex_shader;
    const uint64_t* map = shader ? shader->constant_register_map().float_bitmap : kNoMap;
    if (std::memcmp(map, bindings.float_maps[stage], sizeof(bindings.float_maps[stage]))) {
      std::memcpy(bindings.float_maps[stage], map, sizeof(bindings.float_maps[stage]));
      bindings.float_valid[stage] = false;
    }
    if (bindings.float_valid[stage]) {
      continue;
    }
    const uint32_t count = shader ? shader->constant_register_map().float_count : 0;
    uint8_t* mapping = request(16 * std::max(count, uint32_t(1)), bindings.float_buffers[stage]);
    if (!mapping) {
      perf_trace::back_end_replay.dropped.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    const uint32_t size = PackFloatConstants(regs, stage, map, expected ? local : mapping);
    if (expected) {
      std::memcpy(mapping, local, size);
      if (size != expected->constant_bytes[stage] ||
          std::memcmp(local, expected_constants + (stage ? expected->constant_bytes[0] : 0),
                      size)) {
        mismatches[be::kMismatchConstantBuffers] = true;
      }
    }
    bindings.float_valid[stage] = true;
    filled |= 1u << stage;
  }
  const uint8_t* expected_bool_loop =
      expected ? expected_constants + expected->constant_bytes[0] + expected->constant_bytes[1]
               : nullptr;
  if (!bindings.bool_loop_valid) {
    uint8_t* mapping = request(deferred_constants::kBoolLoopSlotCount * 16, bindings.bool_loop);
    if (!mapping) {
      perf_trace::back_end_replay.dropped.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    const uint32_t* source = regs.values + XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031;
    std::memcpy(mapping, source, deferred_constants::kBoolLoopSlotCount * 16);
    if (expected && std::memcmp(source, expected_bool_loop, expected->constant_bytes[2])) {
      mismatches[be::kMismatchConstantBuffers] = true;
    }
    bindings.bool_loop_valid = true;
  }
  if (!bindings.fetch_valid) {
    uint8_t* mapping = request(deferred_constants::kFetchSlotCount * 16, bindings.fetch);
    if (!mapping) {
      perf_trace::back_end_replay.dropped.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    const uint32_t* source = regs.values + XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0;
    std::memcpy(mapping, source, deferred_constants::kFetchSlotCount * 16);
    if (expected) {
      // Only the fetch constants the shaders read: the copy's others may hold
      // values of earlier draws, their writes pending until a draw reads them.
      const uint8_t* expected_fetch = expected_bool_loop + expected->constant_bytes[2];
      uint64_t read = vertex_shader.back_end_fetch_slots() |
                      (pixel_shader ? pixel_shader->back_end_fetch_slots() : 0);
      while (read) {
        const uint32_t slot = uint32_t(__builtin_ctzll(read));
        read &= read - 1;
        if (std::memcmp(source + slot * 4, expected_fetch + slot * 16, 16)) {
          mismatches[be::kMismatchConstantBuffers] = true;
        }
      }
    }
    bindings.fetch_valid = true;
  }
  (void)filled;

  // Samplers: their parameters from the copy's fetch constants, their
  // descriptors in the bindless sampler heap the replay owns.
  bool samplers_changed[2] = {};
  for (uint32_t stage = 0; stage < 2; ++stage) {
    const D3D12Shader* shader = stage ? pixel_shader : &vertex_shader;
    const size_t layout_uid =
        shader ? shader->GetSamplerBindingLayoutUserUID() : PipelineCache::kLayoutUIDEmpty;
    std::vector<uint32_t>& current = bindings.samplers[stage];
    const std::vector<D3D12Shader::SamplerBinding>* samplers =
        shader ? &shader->GetSamplerBindingsAfterTranslation() : nullptr;
    const size_t count = samplers ? samplers->size() : 0;
    if (bindings.sampler_layout_uid[stage] != layout_uid || current.size() != count) {
      bindings.sampler_layout_uid[stage] = layout_uid;
      current.assign(count, UINT32_MAX);
      samplers_changed[stage] = true;
    }
    for (size_t i = 0; i < count; ++i) {
      const uint32_t value = texture_cache_->GetSamplerParameters(regs, (*samplers)[i]).value;
      if (current[i] != value) {
        current[i] = value;
        samplers_changed[stage] = true;
      }
      if (expected_samplers) {
        uint32_t expected_value;
        std::memcpy(&expected_value,
                    expected_samplers + 4 * (stage ? expected->sampler_count_vertex + i : i), 4);
        if (value != expected_value) {
          mismatches[be::kMismatchSamplers] = true;
        }
      }
    }
    if (expected && count != (stage ? expected->sampler_count_pixel
                                    : expected->sampler_count_vertex)) {
      mismatches[be::kMismatchSamplers] = true;
    }
    if (samplers_changed[stage]) {
      bindings.indices_valid[stage] = false;
    }
  }
  for (uint32_t attempt = 0; attempt < 2; ++attempt) {
    bool overflowed = false;
    for (uint32_t stage = 0; stage < 2 && !overflowed; ++stage) {
      if (bindings.indices_valid[stage]) {
        continue;
      }
      std::vector<uint32_t>& indices = bindings.sampler_indices[stage];
      indices.resize(bindings.samplers[stage].size());
      for (size_t i = 0; i < indices.size(); ++i) {
        D3D12TextureCache::SamplerParameters parameters;
        parameters.value = bindings.samplers[stage][i];
        auto it = texture_cache_bindless_sampler_map_.find(parameters.value);
        if (it != texture_cache_bindless_sampler_map_.end()) {
          indices[i] = it->second;
          continue;
        }
        if (sampler_bindless_heap_allocated_ >= kSamplerHeapSize) {
          overflowed = true;
          break;
        }
        const uint32_t index = sampler_bindless_heap_allocated_++;
        texture_cache_->WriteSampler(
            parameters, provider.OffsetSamplerDescriptor(sampler_bindless_heap_cpu_start_, index));
        texture_cache_bindless_sampler_map_.emplace(parameters.value, index);
        indices[i] = index;
      }
    }
    if (!overflowed) {
      break;
    }
    // A new sampler heap, as UpdateBindings switches after an overflow.
    ID3D12DescriptorHeap* sampler_heap_new;
    const uint64_t completed = submission_fence_->GetCompletedValue();
    if (!sampler_bindless_heaps_overflowed_.empty() &&
        sampler_bindless_heaps_overflowed_.front().second <= completed) {
      sampler_heap_new = sampler_bindless_heaps_overflowed_.front().first;
      sampler_bindless_heaps_overflowed_.pop_front();
    } else {
      D3D12_DESCRIPTOR_HEAP_DESC desc;
      desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
      desc.NumDescriptors = kSamplerHeapSize;
      desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
      desc.NodeMask = 0;
      if (FAILED(provider.GetDevice()->CreateDescriptorHeap(&desc,
                                                            IID_PPV_ARGS(&sampler_heap_new)))) {
        REXGPU_ERROR("Draw back end: failed to create a sampler heap after an overflow");
        perf_trace::back_end_replay.dropped.fetch_add(1, std::memory_order_relaxed);
        return;
      }
    }
    sampler_bindless_heaps_overflowed_.push_back(
        std::make_pair(sampler_bindless_heap_current_, list.flags.submission));
    sampler_bindless_heap_current_ = sampler_heap_new;
    sampler_bindless_heap_cpu_start_ = sampler_heap_new->GetCPUDescriptorHandleForHeapStart();
    sampler_bindless_heap_gpu_start_ = sampler_heap_new->GetGPUDescriptorHandleForHeapStart();
    sampler_bindless_heap_allocated_ = 0;
    texture_cache_bindless_sampler_map_.clear();
    ID3D12DescriptorHeap* heaps[] = {view_bindless_heap_, sampler_bindless_heap_current_};
    command_list->SetDescriptorHeaps(2, heaps);
    d3d.view_heap = view_bindless_heap_;
    d3d.sampler_heap = sampler_bindless_heap_current_;
    d3d.root_parameters_set &= ~((1u << kRootParameter_Bindless_SamplerHeap) |
                                 (1u << kRootParameter_Bindless_ViewHeap) |
                                 (1u << kRootParameter_Bindless_SharedMemory));
    bindings.indices_valid[0] = bindings.indices_valid[1] = false;
  }
  for (uint32_t stage = 0; stage < 2; ++stage) {
    if (bindings.indices_valid[stage]) {
      continue;
    }
    const D3D12Shader* shader = stage ? pixel_shader : &vertex_shader;
    const size_t texture_count = shader ? shader->GetTextureBindingsAfterTranslation().size() : 0;
    const size_t sampler_count = bindings.samplers[stage].size();
    auto* mapping = reinterpret_cast<uint32_t*>(
        request(std::max(texture_count + sampler_count, size_t(1)) * 4, bindings.indices[stage]));
    if (!mapping) {
      perf_trace::back_end_replay.dropped.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    if (replay.texture_indices[stage].size() != texture_count) {
      REXGPU_ERROR("Draw back end: {} texture indices for {} textures",
                   replay.texture_indices[stage].size(), texture_count);
      perf_trace::back_end_replay.dropped.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    if (shader) {
      const auto& textures = shader->GetTextureBindingsAfterTranslation();
      for (size_t i = 0; i < texture_count; ++i) {
        mapping[textures[i].bindless_descriptor_index] = replay.texture_indices[stage][i];
      }
      const auto& samplers = shader->GetSamplerBindingsAfterTranslation();
      for (size_t i = 0; i < sampler_count; ++i) {
        mapping[samplers[i].bindless_descriptor_index] = bindings.sampler_indices[stage][i];
      }
    }
    bindings.indices_valid[stage] = true;
  }

  // The root signature and the root parameters.
  if (d3d.root_signature != root_signature) {
    command_list->SetGraphicsRootSignature(root_signature);
    d3d.root_signature = root_signature;
    d3d.root_parameters_set = 0;
  }
  const auto set_buffer = [&](uint32_t index, uint64_t address) {
    if (!(d3d.root_parameters_set & (1u << index)) || d3d.root_parameters[index] != address) {
      command_list->SetGraphicsRootConstantBufferView(index, address);
      d3d.root_parameters[index] = address;
      d3d.root_parameters_set |= 1u << index;
    }
  };
  const auto set_table = [&](uint32_t index, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
    if (!(d3d.root_parameters_set & (1u << index)) || d3d.root_parameters[index] != handle.ptr) {
      command_list->SetGraphicsRootDescriptorTable(index, handle);
      d3d.root_parameters[index] = handle.ptr;
      d3d.root_parameters_set |= 1u << index;
    }
  };
  set_buffer(kRootParameter_Bindless_FetchConstants, bindings.fetch);
  set_buffer(kRootParameter_Bindless_FloatConstantsVertex, bindings.float_buffers[0]);
  set_buffer(kRootParameter_Bindless_FloatConstantsPixel, bindings.float_buffers[1]);
  set_buffer(kRootParameter_Bindless_SystemConstants, bindings.system);
  set_buffer(kRootParameter_Bindless_BoolLoopConstants, bindings.bool_loop);
  set_table(kRootParameter_Bindless_SharedMemory,
            provider.OffsetViewDescriptor(
                view_bindless_heap_gpu_start_,
                uint32_t((flags & be::kFlagMemexport)
                             ? SystemBindlessView::kNullRawSRVAndSharedMemoryRawUAVStart
                             : SystemBindlessView::kSharedMemoryRawSRVAndNullRawUAVStart)));
  set_buffer(kRootParameter_Bindless_DescriptorIndicesPixel, bindings.indices[1]);
  set_buffer(kRootParameter_Bindless_DescriptorIndicesVertex, bindings.indices[0]);
  set_table(kRootParameter_Bindless_SamplerHeap, sampler_bindless_heap_gpu_start_);
  set_table(kRootParameter_Bindless_ViewHeap, view_bindless_heap_gpu_start_);

  // The topology and the draw.
  D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
  if (primitive.IsTessellated()) {
    const bool adaptive =
        regs.Get<reg::VGT_HOS_CNTL>().tess_mode == xenos::TessellationMode::kAdaptive;
    switch (primitive.host_primitive_type) {
      case xenos::PrimitiveType::kTriangleList:
        topology = D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
        break;
      case xenos::PrimitiveType::kQuadList:
        topology = D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST;
        break;
      case xenos::PrimitiveType::kTrianglePatch:
        topology = adaptive ? D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST
                            : D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST;
        break;
      case xenos::PrimitiveType::kQuadPatch:
        topology = adaptive ? D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST
                            : D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST;
        break;
      default:
        break;
    }
  } else {
    switch (primitive.host_primitive_type) {
      case xenos::PrimitiveType::kPointList:
        topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        break;
      case xenos::PrimitiveType::kLineList:
        topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        break;
      case xenos::PrimitiveType::kLineStrip:
        topology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        break;
      case xenos::PrimitiveType::kTriangleList:
      case xenos::PrimitiveType::kRectangleList:
        topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        break;
      case xenos::PrimitiveType::kTriangleStrip:
        topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        break;
      case xenos::PrimitiveType::kQuadList:
        topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
        break;
      default:
        break;
    }
  }
  if (topology == D3D_PRIMITIVE_TOPOLOGY_UNDEFINED) {
    perf_trace::back_end_replay.dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (d3d.topology != topology) {
    command_list->IASetPrimitiveTopology(topology);
    d3d.topology = topology;
  }
  if (list.draw.index_buffer_format != uint32_t(DXGI_FORMAT_UNKNOWN)) {
    D3D12_INDEX_BUFFER_VIEW index_buffer;
    index_buffer.BufferLocation = list.draw.index_buffer_gpu_address;
    index_buffer.SizeInBytes = list.draw.index_buffer_size;
    index_buffer.Format = DXGI_FORMAT(list.draw.index_buffer_format);
    if (!d3d.index_buffer_set ||
        std::memcmp(&index_buffer, &d3d.index_buffer, sizeof(index_buffer))) {
      command_list->IASetIndexBuffer(&index_buffer);
      d3d.index_buffer = index_buffer;
      d3d.index_buffer_set = true;
    }
    command_list->DrawIndexedInstanced(list.draw.host_draw_vertex_count, 1, 0, 0, 0);
  } else {
    command_list->DrawInstanced(list.draw.host_draw_vertex_count, 1, 0, 0);
  }

  // The rest of the check: what this replay computed against the draw's.
  if (!expected) {
    return;
  }
  mismatches[be::kMismatchPipeline] =
      std::memcmp(pipeline_cache_->GetPipelineDescription(pipeline_handle),
                  expected->pipeline_description,
                  pipeline_cache_->GetPipelineDescriptionSize()) != 0;
  mismatches[be::kMismatchViewport] =
      std::memcmp(&viewport_info, &expected->viewport, sizeof(viewport_info)) != 0;
  if (mismatches[be::kMismatchViewport]) {
    static uint32_t viewport_messages = 0;
    if (viewport_messages < 4) {
      ++viewport_messages;
      const draw_util::ViewportInfo& e = expected->viewport;
      REXGPU_WARN(
          "Draw back end: viewport flags {:X}: replay {},{} {}x{} z {}..{} ndc {} {} {} / {} {} "
          "{}, draw {},{} {}x{} z {}..{} ndc {} {} {} / {} {} {}",
          flags, viewport_info.xy_offset[0], viewport_info.xy_offset[1],
          viewport_info.xy_extent[0], viewport_info.xy_extent[1], viewport_info.z_min,
          viewport_info.z_max, viewport_info.ndc_scale[0], viewport_info.ndc_scale[1],
          viewport_info.ndc_scale[2], viewport_info.ndc_offset[0], viewport_info.ndc_offset[1],
          viewport_info.ndc_offset[2], e.xy_offset[0], e.xy_offset[1], e.xy_extent[0],
          e.xy_extent[1], e.z_min, e.z_max, e.ndc_scale[0], e.ndc_scale[1], e.ndc_scale[2],
          e.ndc_offset[0], e.ndc_offset[1], e.ndc_offset[2]);
    }
  }
  mismatches[be::kMismatchScissor] =
      std::memcmp(&scissor, &expected->scissor, sizeof(scissor)) != 0;
  mismatches[be::kMismatchFixedFunction] =
      std::memcmp(blend_factor, expected->blend_factor, sizeof(blend_factor)) != 0 ||
      stencil_reference != expected->stencil_reference;
  mismatches[be::kMismatchSystemConstants] =
      std::memcmp(&back_end_replay_constants_, &expected->system_constants,
                  sizeof(back_end_replay_constants_)) != 0;
}

}  // namespace rex::graphics::d3d12
