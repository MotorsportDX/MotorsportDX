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
    float point_screen_diameter_to_ndc_radius_x =
        (/* 0.5f * 2.0f * */ float(draw_resolution_scale_x)) /
        std::max(viewport_info.xy_extent[0], uint32_t(1));
    float point_screen_diameter_to_ndc_radius_y =
        (/* 0.5f * 2.0f * */ float(draw_resolution_scale_y)) /
        std::max(viewport_info.xy_extent[1], uint32_t(1));
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
    float poly_offset_scale_factor = xenos::kPolygonOffsetScaleSubpixelUnit *
                                     std::max(draw_resolution_scale_x, draw_resolution_scale_y);
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

}  // namespace rex::graphics::d3d12::draw_back_end

namespace rex::graphics::d3d12 {

namespace {

constexpr const char* kMismatchNames[draw_back_end::kMismatchCount] = {
    "pipeline description", "viewport", "scissor", "blend factor or stencil reference",
    "system constants", "sampler parameters"};

constexpr uint32_t kGroupSizes[draw_back_end::kGroupCount] = {
    sizeof(draw_back_end::GroupShaders),  sizeof(draw_back_end::GroupPrimitive),
    sizeof(draw_back_end::GroupDraw),     sizeof(draw_back_end::GroupTargets),
    sizeof(draw_back_end::GroupFlags),    0};

uint32_t TexturesGroupSize(uint32_t texture_count) {
  return uint32_t(sizeof(draw_back_end::GroupTexturesHeader)) + ((texture_count + 3) & ~3u) +
         texture_count * 4;
}

}  // namespace

void D3D12CommandProcessor::InitializeDrawBackEnd() {
  draw_back_end_ = uint32_t(REXCVAR_GET(d3d12_draw_back_end));
  draw_back_end_check_ = draw_back_end_ && REXCVAR_GET(d3d12_draw_back_end_check);
  draw_back_end_perturb_ =
      draw_back_end_check_ ? uint32_t(REXCVAR_GET(d3d12_draw_back_end_check_perturb)) : 0;
  draw_back_end_checks_ = 0;
  track_constants_ = deferred_constants_ || draw_back_end_;
  track_register_writes_ = draw_back_end_ != 0;
  back_end_list_captured_ = false;
  if (draw_back_end_) {
    REXGPU_INFO("Draw back end: capture{}{}", draw_back_end_check_ ? ", checked" : "",
                draw_back_end_perturb_ ? ", perturbed" : "");
  }
}

void D3D12CommandProcessor::CaptureDrawBackEnd(const DrawBackEndCapture& capture) {
  namespace be = draw_back_end;
  const uint64_t start = __rdtsc();
  const RegisterFile& regs = *register_file_;
  const uint32_t* context = regs.values + be::kContextFirst;
  constexpr uint32_t kFirstWord = be::kContextFirst >> 6;

  // The context registers the back end reads that changed since they were
  // last captured into this list; all of them at the list's first capture.
  uint64_t context_masks[be::kContextWords];
  uint16_t context_words = 0;
  uint32_t context_count = 0;
  uint32_t written_count = 0;
  if (!back_end_list_captured_) {
    std::memcpy(back_end_context_, context, sizeof(back_end_context_));
    for (uint32_t word = 0; word < be::kContextWords; ++word) {
      context_masks[word] = be::kBackEndRegisters[word];
      written_registers_[kFirstWord + word] = 0;
      if (context_masks[word]) {
        context_words |= uint16_t(1u << word);
        context_count += uint32_t(__builtin_popcountll(context_masks[word]));
      }
    }
  } else {
    for (uint32_t word = 0; word < be::kContextWords; ++word) {
      uint64_t written = written_registers_[kFirstWord + word];
      context_masks[word] = 0;
      if (!written) {
        continue;
      }
      written_registers_[kFirstWord + word] = 0;
      written_count += uint32_t(__builtin_popcountll(written));
      written &= be::kBackEndRegisters[word];
      uint64_t changed = 0;
      while (written) {
        const uint32_t bit = uint32_t(__builtin_ctzll(written));
        written &= written - 1;
        const uint32_t index = (word << 6) + bit;
        if (context[index] != back_end_context_[index]) {
          back_end_context_[index] = context[index];
          changed |= uint64_t(1) << bit;
        }
      }
      context_masks[word] = changed;
      if (changed) {
        context_words |= uint16_t(1u << word);
        context_count += uint32_t(__builtin_popcountll(changed));
      }
    }
  }

  // The fetch constants the samplers read, written since they were last
  // captured into this list (the constant registers' pending mask is shared
  // with d3d12_deferred_constants, whose commands fill the same copy).
  const auto& vertex_shader = static_cast<const D3D12Shader&>(capture.vertex_translation->shader());
  const D3D12Shader* pixel_shader =
      capture.pixel_translation
          ? &static_cast<const D3D12Shader&>(capture.pixel_translation->shader())
          : nullptr;
  uint64_t fetch_needed = 0;
  const auto add_samplers = [&fetch_needed](const D3D12Shader& shader) {
    for (const D3D12Shader::SamplerBinding& binding : shader.GetSamplerBindingsAfterTranslation()) {
      const uint32_t first = (binding.fetch_constant * 6) >> 2;
      const uint32_t last = (binding.fetch_constant * 6 + 5) >> 2;
      fetch_needed |= ((uint64_t(1) << (last - first + 1)) - 1) << first;
    }
  };
  add_samplers(vertex_shader);
  if (pixel_shader) {
    add_samplers(*pixel_shader);
  }
  constexpr uint32_t kFetchWord = deferred_constants::kFetchFirstSlot >> 6;
  const uint64_t fetch_slots = pending_constants_.slots[kFetchWord] & fetch_needed;
  pending_constants_.slots[kFetchWord] &= ~fetch_slots;
  const uint32_t fetch_count = uint32_t(__builtin_popcountll(fetch_slots));

  // The front half's groups.
  be::ListState current = {};
  current.shaders.vertex = capture.vertex_translation;
  current.shaders.pixel = capture.pixel_translation;
  const PrimitiveProcessor::ProcessingResult& primitive = *capture.primitive;
  current.primitive.host_primitive_type = uint8_t(primitive.host_primitive_type);
  current.primitive.host_vertex_shader_type = uint8_t(primitive.host_vertex_shader_type);
  current.primitive.tessellation_mode = uint8_t(primitive.tessellation_mode);
  current.primitive.host_index_format = uint8_t(primitive.host_index_format);
  current.primitive.host_primitive_reset_enabled = primitive.host_primitive_reset_enabled;
  current.primitive.host_shader_index_endian = uint8_t(primitive.host_shader_index_endian);
  current.primitive.index_buffer_type = uint8_t(primitive.index_buffer_type);
  current.primitive.line_loop_closing_index = primitive.line_loop_closing_index;
  // The index buffer's address is resolved later in IssueDraw; increment 1
  // captures the guest index base in its place, with the same size.
  current.draw.index_buffer_gpu_address = primitive.guest_index_base;
  current.draw.host_draw_vertex_count = primitive.host_draw_vertex_count;
  current.targets.bits = capture.bound_render_target_bits;
  for (uint32_t i = 0; i < 5; ++i) {
    current.targets.formats[i] =
        (capture.bound_render_target_bits >> i) & 1 ? capture.bound_render_target_formats[i] : 0;
  }
  current.flags.flags = capture.flags;
  current.flags.draw_resolution_scale_x = uint16_t(texture_cache_->draw_resolution_scale_x());
  current.flags.draw_resolution_scale_y = uint16_t(texture_cache_->draw_resolution_scale_y());
  if (capture.flags & be::kFlagJitter) {
    current.flags.jitter[0] = capture.jitter[0];
    current.flags.jitter[1] = capture.jitter[1];
  }
  const be::SystemConstantInputs& inputs = system_constant_inputs_;
  current.used_texture_mask = inputs.used_texture_mask;
  current.textures_resolution_scaled = inputs.textures_resolution_scaled;
  const uint32_t texture_count = uint32_t(__builtin_popcount(inputs.used_texture_mask));
  {
    uint32_t remaining = inputs.used_texture_mask;
    while (remaining) {
      const uint32_t index = uint32_t(__builtin_ctz(remaining));
      remaining &= remaining - 1;
      current.texture_swizzled_signs[index] = inputs.texture_swizzled_signs[index];
      current.texture_integer_scale_bits[index] = inputs.texture_integer_scale_bits[index];
    }
  }
  uint16_t groups = 0;
  const bool all_groups = !back_end_list_captured_;
  const auto group_changed = [all_groups](const void* a, const void* b, size_t size) {
    return all_groups || std::memcmp(a, b, size) != 0;
  };
  if (group_changed(&current.shaders, &back_end_front_.shaders, sizeof(current.shaders))) {
    groups |= 1 << be::kGroupShaders;
  }
  if (group_changed(&current.primitive, &back_end_front_.primitive, sizeof(current.primitive))) {
    groups |= 1 << be::kGroupPrimitive;
  }
  if (group_changed(&current.draw, &back_end_front_.draw, sizeof(current.draw))) {
    groups |= 1 << be::kGroupDraw;
  }
  if (group_changed(&current.targets, &back_end_front_.targets, sizeof(current.targets))) {
    groups |= 1 << be::kGroupTargets;
  }
  if (group_changed(&current.flags, &back_end_front_.flags, sizeof(current.flags))) {
    groups |= 1 << be::kGroupFlags;
  }
  if (all_groups || current.used_texture_mask != back_end_front_.used_texture_mask ||
      current.textures_resolution_scaled != back_end_front_.textures_resolution_scaled ||
      std::memcmp(current.texture_swizzled_signs, back_end_front_.texture_swizzled_signs,
                  sizeof(current.texture_swizzled_signs)) ||
      std::memcmp(current.texture_integer_scale_bits, back_end_front_.texture_integer_scale_bits,
                  sizeof(current.texture_integer_scale_bits))) {
    groups |= 1 << be::kGroupTextures;
  }
  uint32_t group_bytes = 0;
  for (uint32_t group = 0; group < be::kGroupCount; ++group) {
    if (groups & (1 << group)) {
      group_bytes +=
          group == be::kGroupTextures ? TexturesGroupSize(texture_count) : kGroupSizes[group];
    }
  }

  // The back end's results at the draw, for the check.
  uint16_t check = 0;
  uint32_t sampler_count_vertex = 0, sampler_count_pixel = 0;
  uint32_t check_bytes = 0;
  if (draw_back_end_check_ &&
      pipeline_cache_->GetPipelineDescriptionSize() <= sizeof(be::Expected::pipeline_description)) {
    check = be::kCheck;
    if (draw_back_end_perturb_ && ++draw_back_end_checks_ % draw_back_end_perturb_ == 0) {
      check |= be::kCheckPerturb;
    }
    sampler_count_vertex = uint32_t(vertex_shader.GetSamplerBindingsAfterTranslation().size());
    sampler_count_pixel =
        pixel_shader ? uint32_t(pixel_shader->GetSamplerBindingsAfterTranslation().size()) : 0;
    check_bytes = uint32_t(sizeof(be::Expected)) + 4 * (sampler_count_vertex + sampler_count_pixel);
  }

  const uint32_t context_word_count = uint32_t(__builtin_popcount(context_words));
  const uint32_t bytes = uint32_t(sizeof(be::Header)) + context_word_count * 8 + context_count * 4 +
                         fetch_count * 16 + group_bytes;
  auto* arguments =
      static_cast<uint8_t*>(deferred_command_list_.DrawBackEnd(bytes + check_bytes));
  be::Header header;
  header.context_words = context_words;
  header.groups = groups;
  header.check = check;
  header.texture_count = uint16_t(texture_count);
  header.context_count = context_count;
  header.fetch_count = fetch_count;
  header.fetch_slots = fetch_slots;
  uint8_t* out = arguments;
  std::memcpy(out, &header, sizeof(header));
  out += sizeof(header);
  for (uint32_t word = 0; word < be::kContextWords; ++word) {
    if (context_words & (1u << word)) {
      std::memcpy(out, &context_masks[word], 8);
      out += 8;
    }
  }
  for (uint32_t word = 0; word < be::kContextWords; ++word) {
    uint64_t bits = context_masks[word];
    if (!(context_words & (1u << word))) {
      continue;
    }
    if (bits == ~uint64_t(0)) {
      std::memcpy(out, back_end_context_ + (word << 6), 64 * 4);
      out += 64 * 4;
      continue;
    }
    while (bits) {
      const uint32_t bit = uint32_t(__builtin_ctzll(bits));
      bits &= bits - 1;
      std::memcpy(out, back_end_context_ + (word << 6) + bit, 4);
      out += 4;
    }
  }
  {
    uint64_t bits = fetch_slots;
    while (bits) {
      const uint32_t slot = uint32_t(__builtin_ctzll(bits));
      bits &= bits - 1;
      std::memcpy(out, regs.values + XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + slot * 4, 16);
      out += 16;
    }
  }
  if (groups & (1 << be::kGroupShaders)) {
    std::memcpy(out, &current.shaders, sizeof(current.shaders));
    out += sizeof(current.shaders);
  }
  if (groups & (1 << be::kGroupPrimitive)) {
    std::memcpy(out, &current.primitive, sizeof(current.primitive));
    out += sizeof(current.primitive);
  }
  if (groups & (1 << be::kGroupDraw)) {
    std::memcpy(out, &current.draw, sizeof(current.draw));
    out += sizeof(current.draw);
  }
  if (groups & (1 << be::kGroupTargets)) {
    std::memcpy(out, &current.targets, sizeof(current.targets));
    out += sizeof(current.targets);
  }
  if (groups & (1 << be::kGroupFlags)) {
    std::memcpy(out, &current.flags, sizeof(current.flags));
    out += sizeof(current.flags);
  }
  if (groups & (1 << be::kGroupTextures)) {
    be::GroupTexturesHeader textures_header = {current.used_texture_mask,
                                               current.textures_resolution_scaled};
    std::memcpy(out, &textures_header, sizeof(textures_header));
    out += sizeof(textures_header);
    uint8_t* signs = out;
    out += (texture_count + 3) & ~3u;
    std::memset(signs, 0, out - signs);
    uint32_t remaining = current.used_texture_mask;
    while (remaining) {
      const uint32_t index = uint32_t(__builtin_ctz(remaining));
      remaining &= remaining - 1;
      *signs++ = current.texture_swizzled_signs[index];
      std::memcpy(out, &current.texture_integer_scale_bits[index], 4);
      out += 4;
    }
  }
  if (check) {
    be::Expected expected;
    std::memset(&expected, 0, sizeof(expected));
    std::memcpy(expected.pipeline_description,
                pipeline_cache_->GetPipelineDescription(capture.pipeline_handle),
                pipeline_cache_->GetPipelineDescriptionSize());
    expected.viewport = *capture.viewport;
    expected.scissor = *capture.scissor;
    std::memcpy(expected.blend_factor, ff_blend_factor_, sizeof(expected.blend_factor));
    expected.stencil_reference = ff_stencil_ref_;
    expected.sampler_count_vertex = sampler_count_vertex;
    expected.sampler_count_pixel = sampler_count_pixel;
    expected.system_constants = system_constants_;
    std::memcpy(out, &expected, sizeof(expected));
    out += sizeof(expected);
    for (uint32_t i = 0; i < sampler_count_vertex; ++i) {
      std::memcpy(out, &current_samplers_vertex_[i].value, 4);
      out += 4;
    }
    for (uint32_t i = 0; i < sampler_count_pixel; ++i) {
      std::memcpy(out, &current_samplers_pixel_[i].value, 4);
      out += 4;
    }
  }
  assert_true(out == arguments + bytes + check_bytes);

  back_end_front_ = current;
  back_end_list_captured_ = true;
  perf_trace::CountBackEndCapture(bytes, context_count, written_count, fetch_count, group_bytes,
                                  __rdtsc() - start);
}

void D3D12CommandProcessor::ReplayDrawBackEnd(const uint8_t* arguments, RegisterFile& shadow,
                                              draw_back_end::ListState& list) const {
  namespace be = draw_back_end;
  const uint64_t start = __rdtsc();
  be::Header header;
  std::memcpy(&header, arguments, sizeof(header));
  const uint8_t* in = arguments + sizeof(header);
  uint64_t context_masks[be::kContextWords] = {};
  for (uint32_t word = 0; word < be::kContextWords; ++word) {
    if (header.context_words & (1u << word)) {
      std::memcpy(&context_masks[word], in, 8);
      in += 8;
    }
  }
  uint32_t* context = shadow.values + be::kContextFirst;
  for (uint32_t word = 0; word < be::kContextWords; ++word) {
    uint64_t bits = context_masks[word];
    if (bits == ~uint64_t(0)) {
      std::memcpy(context + (word << 6), in, 64 * 4);
      in += 64 * 4;
      continue;
    }
    while (bits) {
      const uint32_t bit = uint32_t(__builtin_ctzll(bits));
      bits &= bits - 1;
      std::memcpy(context + (word << 6) + bit, in, 4);
      in += 4;
    }
  }
  {
    uint64_t bits = header.fetch_slots;
    while (bits) {
      const uint32_t slot = uint32_t(__builtin_ctzll(bits));
      bits &= bits - 1;
      std::memcpy(shadow.values + XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + slot * 4, in, 16);
      in += 16;
    }
  }
  if (header.groups & (1 << be::kGroupShaders)) {
    std::memcpy(&list.shaders, in, sizeof(list.shaders));
    in += sizeof(list.shaders);
  }
  if (header.groups & (1 << be::kGroupPrimitive)) {
    std::memcpy(&list.primitive, in, sizeof(list.primitive));
    in += sizeof(list.primitive);
  }
  if (header.groups & (1 << be::kGroupDraw)) {
    std::memcpy(&list.draw, in, sizeof(list.draw));
    in += sizeof(list.draw);
  }
  if (header.groups & (1 << be::kGroupTargets)) {
    std::memcpy(&list.targets, in, sizeof(list.targets));
    in += sizeof(list.targets);
  }
  if (header.groups & (1 << be::kGroupFlags)) {
    std::memcpy(&list.flags, in, sizeof(list.flags));
    in += sizeof(list.flags);
  }
  if (header.groups & (1 << be::kGroupTextures)) {
    be::GroupTexturesHeader textures_header;
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
  const uint64_t replayed = __rdtsc();
  if (perf_trace::Get()) {
    perf_trace::back_end_replay.replays.fetch_add(1, std::memory_order_relaxed);
    perf_trace::back_end_replay.tsc.fetch_add(replayed - start, std::memory_order_relaxed);
  }
  if (!(header.check & be::kCheck)) {
    return;
  }

  // The check: the back end's results again, from the copy of the registers.
  be::Expected expected;
  std::memcpy(&expected, in, sizeof(expected));
  in += sizeof(expected);
  const bool perturb = (header.check & be::kCheckPerturb) != 0;
  if (perturb) {
    expected.pipeline_description[0] ^= 1;
  }
  bool mismatches[be::kMismatchCount] = {};
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
  uint8_t description[sizeof(be::Expected::pipeline_description)] = {};
  if (!pipeline_cache_->ComputePipelineDescription(
          regs, vertex_translation, pixel_translation, primitive, depth_control, color_mask,
          list.targets.bits, list.targets.formats, description)) {
    mismatches[be::kMismatchPipeline] = true;
  } else {
    mismatches[be::kMismatchPipeline] =
        std::memcmp(description, expected.pipeline_description,
                    pipeline_cache_->GetPipelineDescriptionSize()) != 0;
  }

  const uint32_t uniform_scale_x = (flags >> be::kFlagUniformScaleShift) & 3;
  const uint32_t uniform_scale_y = (flags >> (be::kFlagUniformScaleShift + 2)) & 3;
  const uint32_t scale_x = list.flags.draw_resolution_scale_x * uniform_scale_x;
  const uint32_t scale_y = list.flags.draw_resolution_scale_y * uniform_scale_y;
  draw_util::ViewportInfo viewport;
  draw_util::GetHostViewportInfo(regs, scale_x, scale_y, true, D3D12_VIEWPORT_BOUNDS_MAX,
                                 D3D12_VIEWPORT_BOUNDS_MAX, false, depth_control,
                                 (flags & be::kFlagConvertZToFloat24) != 0,
                                 (flags & be::kFlagHostRenderTargets) != 0,
                                 pixel_shader && pixel_shader->writes_depth(), viewport);
  if (flags & be::kFlagJitter) {
    post_pass::AddJitterToNdcOffset(list.flags.jitter, viewport.xy_extent, viewport.ndc_offset);
  }
  mismatches[be::kMismatchViewport] =
      std::memcmp(&viewport, &expected.viewport, sizeof(viewport)) != 0;

  draw_util::Scissor scissor;
  draw_util::GetScissor(regs, scissor);
  scissor.offset[0] *= scale_x;
  scissor.offset[1] *= scale_y;
  scissor.extent[0] *= scale_x;
  scissor.extent[1] *= scale_y;
  mismatches[be::kMismatchScissor] = std::memcmp(&scissor, &expected.scissor, sizeof(scissor)) != 0;

  if (flags & be::kFlagHostRenderTargets) {
    float blend_factor[4];
    uint32_t stencil_reference;
    be::ComputeBlendFactorAndStencilReference(regs, polygonal, depth_control, blend_factor,
                                              stencil_reference);
    mismatches[be::kMismatchFixedFunction] =
        std::memcmp(blend_factor, expected.blend_factor, sizeof(blend_factor)) != 0 ||
        stencil_reference != expected.stencil_reference;
  }

  be::SystemConstantInputs inputs = {};
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
  std::memcpy(inputs.texture_swizzled_signs, list.texture_swizzled_signs,
              sizeof(inputs.texture_swizzled_signs));
  std::memcpy(inputs.texture_integer_scale_bits, list.texture_integer_scale_bits,
              sizeof(inputs.texture_integer_scale_bits));
  inputs.textures_resolution_scaled = list.textures_resolution_scaled;
  be::ComputeSystemConstants(regs, viewport, inputs, back_end_replay_constants_);
  mismatches[be::kMismatchSystemConstants] =
      std::memcmp(&back_end_replay_constants_, &expected.system_constants,
                  sizeof(back_end_replay_constants_)) != 0;

  const auto& samplers_vertex = vertex_shader.GetSamplerBindingsAfterTranslation();
  const size_t pixel_sampler_count =
      pixel_shader ? pixel_shader->GetSamplerBindingsAfterTranslation().size() : 0;
  if (samplers_vertex.size() != expected.sampler_count_vertex ||
      pixel_sampler_count != expected.sampler_count_pixel) {
    mismatches[be::kMismatchSamplers] = true;
  } else {
    for (uint32_t stage = 0; stage < 2; ++stage) {
      if (stage && !pixel_shader) {
        break;
      }
      const auto& bindings =
          stage ? pixel_shader->GetSamplerBindingsAfterTranslation() : samplers_vertex;
      for (const D3D12Shader::SamplerBinding& binding : bindings) {
        uint32_t value;
        std::memcpy(&value, in, 4);
        in += 4;
        if (texture_cache_->GetSamplerParameters(regs, binding).value != value) {
          mismatches[be::kMismatchSamplers] = true;
        }
      }
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
    if (perturb && kind == be::kMismatchPipeline) {
      continue;
    }
    static std::atomic<uint32_t> logged{0};
    if (logged.fetch_add(1, std::memory_order_relaxed) < 16) {
      REXGPU_ERROR("Draw back end: the {} differ from the draw's (vertex shader {:016X})",
                   kMismatchNames[kind], vertex_shader.ucode_data_hash());
    }
  }
}

}  // namespace rex::graphics::d3d12
