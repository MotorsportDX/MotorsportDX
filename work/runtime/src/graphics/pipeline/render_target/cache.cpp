/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <iterator>
#include <tuple>
#include <unordered_set>
#include <utility>

#include <rex/assert.h>
#include <rex/cvar.h>
#include <rex/graphics/flags.h>
#include <rex/graphics/pipeline/render_target/cache.h>
#include <rex/graphics/register_file.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/util/draw.h>
#include <rex/graphics/xenos.h>
#include <rex/logging.h>
#include <rex/math.h>

#include "../../util/perf_trace.h"

REXCVAR_DEFINE_BOOL(mrt_edram_used_range_clamp_to_min, true, "GPU",
                    "Clamp MRT EDRAM used range to minimum");

REXCVAR_DEFINE_BOOL(edram_single_rt_no_wrap, false, "GPU",
                    "When a draw binds a single render target, do not let its estimated EDRAM "
                    "range wrap past the end of EDRAM. Titles using predicated tiling (FM4) "
                    "draw depth-only passes of one tile whose estimated full-screen height "
                    "would otherwise wrap over the color tile at base 0 and take its ownership "
                    "on the host render target path");

REXCVAR_DEFINE_BOOL(execute_unclipped_draw_vs_on_cpu_for_psi_render_backend, true, "GPU",
                    "Execute unclipped draw VS on CPU for PSI render backend");

REXCVAR_DEFINE_BOOL(snorm16_render_target_full_range, true, "GPU",
                    "Use full range for SNORM16 render targets");

REXCVAR_DEFINE_INT32(native_rt_surfaces, 0, "GPU",
                     "Surface-keyed host render targets (docs/analysis/d3d-hle.md 16.9), bits: 1 "
                     "count whether the tiles of each ownership transfer are read before they "
                     "are overwritten (perf trace), 2 resolve from the host render target "
                     "instead of its dump into the EDRAM buffer, 4 defer ownership transfers "
                     "until a draw, a resolve or a post pass reads the tiles (16.12), 8 size "
                     "the host render targets to the tile rows their surfaces use (16.15), 16 "
                     "with native_one_pass_msaa, give every 1280-wide surface of the races "
                     "(the scene, the post passes' colour and the final depth) a private host "
                     "target (docs/analysis/d3d-hle-n5.md 18.11). RTV path only")
    .range(0, 31)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_INT32(native_rt_surfaces_memory_cap_mb, 0, "GPU",
                     "With native_rt_surfaces bit 8: above this many MB of host render "
                     "targets (0: no cap), destroy the least recently bound ones that own no "
                     "EDRAM tile")
    .range(0, 65536)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_INT32(native_one_pass_msaa, 0, "GPU",
                     "With the title's one-pass MSAA races (FM4_ONE_PASS_MSAA): their sample "
                     "count (2 or 4; 0 off). The scene's depth (2x: EDRAM base 1024, pitch 16 "
                     "tiles; 4x: base 2880, pitch 32) and at 4x its colour (base 0) then have "
                     "private host render targets outside the EDRAM tile map "
                     "(docs/analysis/d3d-hle.md 16.17). RTV path only")
    .range(0, 4)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(native_rt_surfaces_grow_stress, false, "GPU",
                    "With native_rt_surfaces bit 8: create every host render target with 8 tile "
                    "rows and grow it only to the rows asked for, so that every surface grows "
                    "(a check of the growth, docs/analysis/d3d-hle.md 16.16)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(native_rt_surfaces_lazy_check, false, "GPU",
                    "With native_rt_surfaces bit 4: after each flush of deferred transfers, "
                    "look for deferred tiles left in what the reader reads, and for deferred "
                    "tiles that their holder may draw into without claiming them (perf trace "
                    "lazy_transfers violations)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_INT32(native_rt_surfaces_lazy_perturb, 0, "GPU",
                     "With native_rt_surfaces_lazy_check: skip the copy of every N-th deferred "
                     "tile that a draw reads (0: never), to prove that the check reports it")
    .range(0, 1000000)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(direct_host_resolve, true, "GPU",
                    "Resolve from host render targets directly to shared memory when possible")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace rex::graphics {

namespace {
// REX_SURFACE_PROBE=<path> (docs/analysis/d3d-hle.md 16.17): one line for
// every new combination of a draw's render targets - the raw RB_SURFACE_INFO
// (pitch, sample count, HiZ pitch), the raw RB_DEPTH_INFO and RB_COLOR_INFO
// of the bound targets with their 12-bit EDRAM bases, the window offset and
// the window and screen scissors - with the update it first appeared in.
struct SurfaceProbe {
  std::ofstream output;
  std::map<std::string, uint64_t> counts;
};
SurfaceProbe* GetSurfaceProbe() {
  static SurfaceProbe* probe = []() -> SurfaceProbe* {
    const char* path = std::getenv("REX_SURFACE_PROBE");
    if (!path || !*path) {
      return nullptr;
    }
    auto* result = new SurfaceProbe;
    result->output.open(path);
    return result->output ? result : nullptr;
  }();
  return probe;
}
}  // namespace

void RenderTargetCache::GetPSIColorFormatInfo(xenos::ColorRenderTargetFormat format,
                                              uint32_t write_mask, float& clamp_rgb_low,
                                              float& clamp_alpha_low, float& clamp_rgb_high,
                                              float& clamp_alpha_high, uint32_t& keep_mask_low,
                                              uint32_t& keep_mask_high) {
  keep_mask_low = keep_mask_high = 0;
  switch (format) {
    case xenos::ColorRenderTargetFormat::k_8_8_8_8:
    case xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA: {
      clamp_rgb_low = clamp_alpha_low = 0.0f;
      clamp_rgb_high = clamp_alpha_high = 1.0f;
      for (uint32_t i = 0; i < 4; ++i) {
        if (!(write_mask & (1 << i))) {
          keep_mask_low |= uint32_t(0xFF) << (i * 8);
        }
      }
    } break;
    case xenos::ColorRenderTargetFormat::k_2_10_10_10:
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10: {
      clamp_rgb_low = clamp_alpha_low = 0.0f;
      clamp_rgb_high = clamp_alpha_high = 1.0f;
      for (uint32_t i = 0; i < 3; ++i) {
        if (!(write_mask & (1 << i))) {
          keep_mask_low |= uint32_t(0x3FF) << (i * 10);
        }
      }
      if (!(write_mask & 0b1000)) {
        keep_mask_low |= uint32_t(3) << 30;
      }
    } break;
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16: {
      clamp_rgb_low = clamp_alpha_low = 0.0f;
      clamp_rgb_high = 31.875f;
      clamp_alpha_high = 1.0f;
      for (uint32_t i = 0; i < 3; ++i) {
        if (!(write_mask & (1 << i))) {
          keep_mask_low |= uint32_t(0x3FF) << (i * 10);
        }
      }
      if (!(write_mask & 0b1000)) {
        keep_mask_low |= uint32_t(3) << 30;
      }
    } break;
    case xenos::ColorRenderTargetFormat::k_16_16:
    case xenos::ColorRenderTargetFormat::k_16_16_16_16:
      // Alpha clamping affects blending source, so it's non-zero for alpha for
      // k_16_16 (the render target is fixed-point). There's one deviation from
      // how Direct3D 11.3 functional specification defines SNorm conversion
      // (NaN should be 0, not the lowest negative number), and that needs to be
      // handled separately.
      clamp_rgb_low = clamp_alpha_low = -32.0f;
      clamp_rgb_high = clamp_alpha_high = 32.0f;
      if (!(write_mask & 0b0001)) {
        keep_mask_low |= 0xFFFFu;
      }
      if (!(write_mask & 0b0010)) {
        keep_mask_low |= 0xFFFF0000u;
      }
      if (format == xenos::ColorRenderTargetFormat::k_16_16_16_16) {
        if (!(write_mask & 0b0100)) {
          keep_mask_high |= 0xFFFFu;
        }
        if (!(write_mask & 0b1000)) {
          keep_mask_high |= 0xFFFF0000u;
        }
      } else {
        write_mask &= 0b0011;
      }
      break;
    case xenos::ColorRenderTargetFormat::k_16_16_FLOAT:
    case xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
      // No NaNs on the Xbox 360 GPU, though can't use the extended range with
      // Direct3D and Vulkan conversions.
      // TODO(Triang3l): Use the extended-range encoding in all implementations.
      clamp_rgb_low = clamp_alpha_low = -65504.0f;
      clamp_rgb_high = clamp_alpha_high = 65504.0f;
      if (!(write_mask & 0b0001)) {
        keep_mask_low |= 0xFFFFu;
      }
      if (!(write_mask & 0b0010)) {
        keep_mask_low |= 0xFFFF0000u;
      }
      if (format == xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT) {
        if (!(write_mask & 0b0100)) {
          keep_mask_high |= 0xFFFFu;
        }
        if (!(write_mask & 0b1000)) {
          keep_mask_high |= 0xFFFF0000u;
        }
      } else {
        write_mask &= 0b0011;
      }
      break;
    case xenos::ColorRenderTargetFormat::k_32_FLOAT:
      // No clamping - let min/max always pick the original value.
      clamp_rgb_low = clamp_alpha_low = clamp_rgb_high = clamp_alpha_high = std::nanf("");
      write_mask &= 0b0001;
      if (!(write_mask & 0b0001)) {
        keep_mask_low = ~uint32_t(0);
      }
      break;
    case xenos::ColorRenderTargetFormat::k_32_32_FLOAT:
      // No clamping - let min/max always pick the original value.
      clamp_rgb_low = clamp_alpha_low = clamp_rgb_high = clamp_alpha_high = std::nanf("");
      write_mask &= 0b0011;
      if (!(write_mask & 0b0001)) {
        keep_mask_low = ~uint32_t(0);
      }
      if (!(write_mask & 0b0010)) {
        keep_mask_high = ~uint32_t(0);
      }
      break;
    default:
      assert_unhandled_case(format);
      // Disable invalid render targets.
      write_mask = 0;
      break;
  }
  // Special case handled in the shaders for empty write mask to completely skip
  // a disabled render target: all keep bits are set.
  if (!write_mask) {
    keep_mask_low = keep_mask_high = ~uint32_t(0);
  }
}

uint32_t RenderTargetCache::Transfer::GetRangeRectangles(uint32_t start_tiles, uint32_t end_tiles,
                                                         uint32_t base_tiles, uint32_t pitch_tiles,
                                                         xenos::MsaaSamples msaa_samples,
                                                         bool is_64bpp, Rectangle* rectangles_out,
                                                         const Rectangle* cutout) {
  // EDRAM addressing wrapping must be handled by doing GetRangeRectangles for
  // two clamped ranges, in this case start_tiles == end_tiles will also
  // unambiguously mean an empty range rather than the entire EDRAM.
  assert_true(start_tiles < xenos::kEdramTileCount);
  assert_true(end_tiles <= xenos::kEdramTileCount);
  assert_true(start_tiles <= end_tiles);
  // If start_tiles < base_tiles, this is the tail after EDRAM addressing
  // wrapping.
  assert_true(start_tiles >= base_tiles || end_tiles <= base_tiles);
  assert_not_zero(pitch_tiles);
  if (start_tiles == end_tiles) {
    return 0;
  }
  uint32_t tile_width = xenos::kEdramTileWidthSamples >>
                        (uint32_t(msaa_samples >= xenos::MsaaSamples::k4X) + uint32_t(is_64bpp));
  uint32_t tile_height =
      xenos::kEdramTileHeightSamples >> uint32_t(msaa_samples >= xenos::MsaaSamples::k2X);
  // If the first and / or the last rows have the same X spans as the middle
  // part, merge them with it.
  uint32_t rectangle_count = 0;
  // If start_tiles < base_tiles, this is the tail after EDRAM addressing
  // wrapping.
  uint32_t local_offset = start_tiles < base_tiles ? xenos::kEdramTileCount : 0;
  uint32_t local_start = local_offset + start_tiles - base_tiles;
  uint32_t local_end = local_offset + end_tiles - base_tiles;
  // Inclusive.
  uint32_t rows_start = local_start / pitch_tiles;
  // Exclusive.
  uint32_t rows_end = (local_end + (pitch_tiles - 1)) / pitch_tiles;
  uint32_t row_first_start = local_start - rows_start * pitch_tiles;
  uint32_t row_last_end = pitch_tiles - (rows_end * pitch_tiles - local_end);
  uint32_t rows = rows_end - rows_start;
  if (rows == 1 || row_first_start) {
    Rectangle rectangle_first;
    rectangle_first.x_pixels = row_first_start * tile_width;
    rectangle_first.y_pixels = rows_start * tile_height;
    rectangle_first.width_pixels =
        ((rows == 1 ? row_last_end : pitch_tiles) - row_first_start) * tile_width;
    rectangle_first.height_pixels = tile_height;
    rectangle_count += AddRectangle(
        rectangle_first, rectangles_out ? rectangles_out + rectangle_count : nullptr, cutout);
    if (rows == 1) {
      return rectangle_count;
    }
  }
  uint32_t mid_rows_start = rows_start + 1;
  uint32_t mid_rows = rows - 2;
  if (!row_first_start) {
    --mid_rows_start;
    ++mid_rows;
  }
  if (row_last_end == pitch_tiles) {
    ++mid_rows;
  }
  if (mid_rows) {
    Rectangle rectangle_mid;
    rectangle_mid.x_pixels = 0;
    rectangle_mid.y_pixels = mid_rows_start * tile_height;
    rectangle_mid.width_pixels = pitch_tiles * tile_width;
    rectangle_mid.height_pixels = mid_rows * tile_height;
    rectangle_count += AddRectangle(
        rectangle_mid, rectangles_out ? rectangles_out + rectangle_count : nullptr, cutout);
  }
  if (row_last_end != pitch_tiles) {
    Rectangle rectangle_last;
    rectangle_last.x_pixels = 0;
    rectangle_last.y_pixels = (rows_end - 1) * tile_height;
    rectangle_last.width_pixels = row_last_end * tile_width;
    rectangle_last.height_pixels = tile_height;
    rectangle_count += AddRectangle(
        rectangle_last, rectangles_out ? rectangles_out + rectangle_count : nullptr, cutout);
  }
  assert_true(rectangle_count <= (cutout ? kMaxRectanglesWithCutout : kMaxRectanglesWithoutCutout));
  return rectangle_count;
}

uint32_t RenderTargetCache::Transfer::AddRectangle(const Rectangle& rectangle,
                                                   Rectangle* rectangles_out,
                                                   const Rectangle* cutout) {
  uint32_t rectangle_right = rectangle.x_pixels + rectangle.width_pixels;
  uint32_t rectangle_bottom = rectangle.y_pixels + rectangle.height_pixels;
  // If nothing to cut out (no region specified, or no intersection - if the
  // cutout region is in the middle on Y, but completely to the left / right on
  // X, don't split), add the whole rectangle.
  if (!cutout || !cutout->width_pixels || !cutout->height_pixels ||
      cutout->x_pixels >= rectangle_right ||
      cutout->x_pixels + cutout->width_pixels <= rectangle.x_pixels ||
      cutout->y_pixels >= rectangle_bottom ||
      cutout->y_pixels + cutout->height_pixels <= rectangle.y_pixels) {
    if (rectangles_out) {
      rectangles_out[0] = rectangle;
    }
    return 1;
  }
  uint32_t rectangle_count = 0;
  uint32_t cutout_right = cutout->x_pixels + cutout->width_pixels;
  uint32_t cutout_bottom = cutout->y_pixels + cutout->height_pixels;
  // Upper part after cutout.
  if (cutout->y_pixels > rectangle.y_pixels) {
    // The completely outside case has already been checked.
    assert_true(cutout->y_pixels < rectangle_bottom);
    if (rectangles_out) {
      Rectangle& rectangle_upper = rectangles_out[rectangle_count];
      rectangle_upper.x_pixels = rectangle.x_pixels;
      rectangle_upper.y_pixels = rectangle.y_pixels;
      rectangle_upper.width_pixels = rectangle.width_pixels;
      // cutout->y_pixels is already known to be < rectangle_bottom, no need for
      // min(cutout->y_pixels - rectangle.y_pixels, rectangle.height_pixels).
      rectangle_upper.height_pixels = cutout->y_pixels - rectangle.y_pixels;
    }
    ++rectangle_count;
  }
  // Middle part after cutout.
  uint32_t middle_top = std::max(cutout->y_pixels, rectangle.y_pixels);
  uint32_t middle_height = std::min(cutout_bottom, rectangle_bottom) - middle_top;
  // Middle left.
  if (cutout->x_pixels > rectangle.x_pixels) {
    assert_true(cutout->x_pixels < rectangle_right);
    if (rectangles_out) {
      Rectangle& rectangle_middle_left = rectangles_out[rectangle_count];
      rectangle_middle_left.x_pixels = rectangle.x_pixels;
      rectangle_middle_left.y_pixels = middle_top;
      rectangle_middle_left.width_pixels = cutout->x_pixels - rectangle.x_pixels;
      rectangle_middle_left.height_pixels = middle_height;
    }
    ++rectangle_count;
  }
  // Middle right.
  if (cutout_right < rectangle_right) {
    assert_true(cutout_right > rectangle.x_pixels);
    if (rectangles_out) {
      Rectangle& rectangle_middle_right = rectangles_out[rectangle_count];
      rectangle_middle_right.x_pixels = cutout_right;
      rectangle_middle_right.y_pixels = middle_top;
      rectangle_middle_right.width_pixels = rectangle_right - cutout_right;
      rectangle_middle_right.height_pixels = middle_height;
    }
    ++rectangle_count;
  }
  // Lower part after cutout.
  if (cutout_bottom < rectangle_bottom) {
    assert_true(cutout_bottom > rectangle.y_pixels);
    if (rectangles_out) {
      Rectangle& rectangle_upper = rectangles_out[rectangle_count];
      rectangle_upper.x_pixels = rectangle.x_pixels;
      rectangle_upper.y_pixels = cutout_bottom;
      rectangle_upper.width_pixels = rectangle.width_pixels;
      rectangle_upper.height_pixels = rectangle_bottom - cutout_bottom;
    }
    ++rectangle_count;
  }
  assert_true(rectangle_count <= kMaxCutoutBorderRectangles);
  return rectangle_count;
}

RenderTargetCache::~RenderTargetCache() {
  ShutdownCommon();
}

bool RenderTargetCache::IsLastUpdateRepeatable() const {
  if (GetPath() != Path::kHostRenderTargets || (REXCVAR_GET(native_rt_surfaces) & 1) ||
      lazy_held_tiles_ || pending_uniform_draw_signature_) {
    return false;
  }
  const auto is_plain = [](const UniformDraw& uniform_draw) {
    return !uniform_draw.signature && !uniform_draw.overwritten_slots &&
           uniform_draw.scale_x == 1 && uniform_draw.scale_y == 1 && !uniform_draw.bounds_valid;
  };
  if (!is_plain(uniform_draw_) || !is_plain(last_update_uniform_draw_)) {
    return false;
  }
  for (const std::vector<Transfer>& transfers : last_update_transfers_) {
    if (!transfers.empty()) {
      return false;
    }
  }
  return true;
}

void RenderTargetCache::InitializeCommon() {
  BumpUpdateEpoch();
  assert_true(ownership_ranges_.empty());
  ownership_ranges_.emplace(std::piecewise_construct, std::forward_as_tuple(uint32_t(0)),
                            std::forward_as_tuple(xenos::kEdramTileCount, RenderTargetKey(),
                                                  RenderTargetKey(), RenderTargetKey()));
}

void RenderTargetCache::DestroyAllRenderTargets(bool shutting_down) {
  BumpUpdateEpoch();
  ownership_ranges_.clear();
  if (!shutting_down) {
    ownership_ranges_.emplace(std::piecewise_construct, std::forward_as_tuple(uint32_t(0)),
                              std::forward_as_tuple(xenos::kEdramTileCount, RenderTargetKey(),
                                                    RenderTargetKey(), RenderTargetKey()));
  }

  for (const auto& render_target_pair : render_targets_) {
    if (render_target_pair.second) {
      delete render_target_pair.second;
    }
  }
  render_targets_.clear();
}

void RenderTargetCache::ShutdownCommon() {
  DestroyAllRenderTargets(true);
}

void RenderTargetCache::ClearCache() {
  BumpUpdateEpoch();
  // Keep only render targets currently owning any EDRAM data.
  if (!render_targets_.empty()) {
    std::unordered_set<RenderTargetKey, RenderTargetKey::Hasher> used_render_targets;
    for (const auto& ownership_range_pair : ownership_ranges_) {
      const OwnershipRange& ownership_range = ownership_range_pair.second;
      if (!ownership_range.render_target.IsEmpty()) {
        used_render_targets.emplace(ownership_range.render_target);
      }
      if (!ownership_range.host_depth_render_target_unorm24.IsEmpty()) {
        used_render_targets.emplace(ownership_range.host_depth_render_target_unorm24);
      }
      if (!ownership_range.host_depth_render_target_float24.IsEmpty()) {
        used_render_targets.emplace(ownership_range.host_depth_render_target_float24);
      }
    }
    // The holders of deferred transfers (native_rt_surfaces bit 4), and the
    // private scene depth (16.17).
    for (RenderTarget* holder : lazy_holders_) {
      if (holder) {
        used_render_targets.emplace(holder->key());
      }
    }
    for (const auto& render_target_pair : render_targets_) {
      if (IsPrivateKey(render_target_pair.first)) {
        used_render_targets.emplace(render_target_pair.first);
      }
    }
    if (render_targets_.size() != used_render_targets.size()) {
      typename decltype(render_targets_)::iterator it_next;
      for (auto it = render_targets_.begin(); it != render_targets_.end(); it = it_next) {
        it_next = std::next(it);
        if (!it->second) {
          render_targets_.erase(it);
          continue;
        }
        if (used_render_targets.find(it->second->key()) == used_render_targets.end()) {
          render_target_memory_ -= it->second->memory_bytes_;
          delete it->second;
          render_targets_.erase(it);
        }
      }
    }
  }
}

std::string RenderTargetCache::DescribeLastUpdateUniformDraw() const {
  const UniformDraw& uniform_draw = last_update_uniform_draw_;
  if (!uniform_draw.bounds_valid) {
    return std::string();
  }
  const Transfer::Rectangle& cutout = last_update_overwrite_cutout_;
  return fmt::format("s{:x}:{},{},{},{}>c{:x}:{},{},{},{}", uniform_draw.overwritten_slots,
                     uniform_draw.bounds[0], uniform_draw.bounds[1], uniform_draw.bounds[2],
                     uniform_draw.bounds[3], last_update_overwrite_cutout_slots_, cutout.x_pixels,
                     cutout.y_pixels, cutout.width_pixels, cutout.height_pixels);
}

uint32_t RenderTargetCache::AppendLastUpdateTransfers(std::string& out) const {
  if (GetPath() != Path::kHostRenderTargets)
    return 0;
  uint32_t count = 0;
  for (uint32_t slot = 0; slot < 1 + xenos::kMaxColorRenderTargets; ++slot) {
    for (const Transfer& transfer : last_update_transfers_[slot]) {
      out += fmt::format("{}[{},{},{},{}]", out.empty() ? "" : ",", slot, transfer.start_tiles,
                         transfer.end_tiles, transfer.source ? transfer.source->key().key : 0);
      ++count;
    }
  }
  return count;
}

void RenderTargetCache::BeginFrame() {
  ResetAccumulatedRenderTargets();
}

bool RenderTargetCache::Update(bool is_rasterization_done,
                               reg::RB_DEPTHCONTROL normalized_depth_control,
                               uint32_t normalized_color_mask, const Shader& vertex_shader) {
  const RegisterFile& regs = register_file();
  bool interlock_barrier_only = GetPath() == Path::kPixelShaderInterlock;
  // For this update only.
  const UniformDraw uniform_draw = uniform_draw_;
  uniform_draw_ = UniformDraw();
  ++render_target_update_clock_;
  last_update_uniform_draw_ = uniform_draw;
  last_update_overwrite_cutout_ = {};
  last_update_overwrite_cutout_slots_ = 0;

  auto rb_surface_info = regs.Get<reg::RB_SURFACE_INFO>();
  xenos::MsaaSamples msaa_samples = rb_surface_info.msaa_samples;
  assert_true(msaa_samples <= xenos::MsaaSamples::k4X);
  if (msaa_samples > xenos::MsaaSamples::k4X) {
    // Safety check because a lot of code assumes up to 4x.
    assert_always();
    REXGPU_ERROR("{}x MSAA requested by the guest, Xenos only supports up to 4x",
                 uint32_t(1) << uint32_t(msaa_samples));
    return false;
  }
  uint32_t msaa_samples_x_log2 = uint32_t(msaa_samples >= xenos::MsaaSamples::k4X);
  uint32_t pitch_pixels = rb_surface_info.surface_pitch;
  // surface_pitch 0 should be handled in disabling rasterization (hopefully
  // it's safe to assume that).
  assert_true(pitch_pixels || !is_rasterization_done);
  if (!pitch_pixels) {
    is_rasterization_done = false;
  } else if (pitch_pixels > xenos::kTexture2DCubeMaxWidthHeight) {
    REXGPU_ERROR(
        "Surface pitch {} larger than the maximum texture width {} specified "
        "by the guest",
        pitch_pixels, xenos::kTexture2DCubeMaxWidthHeight);
    return false;
  }
  uint32_t pitch_tiles_at_32bpp =
      ((pitch_pixels << msaa_samples_x_log2) + (xenos::kEdramTileWidthSamples - 1)) /
      xenos::kEdramTileWidthSamples;
  if (!interlock_barrier_only) {
    uint32_t pitch_pixels_tile_aligned_scaled =
        pitch_tiles_at_32bpp * (xenos::kEdramTileWidthSamples >> msaa_samples_x_log2) *
        draw_resolution_scale_x();
    uint32_t max_render_target_width = GetMaxRenderTargetWidth();
    if (pitch_pixels_tile_aligned_scaled > max_render_target_width) {
      // TODO(Triang3l): If really needed for some game on some device, clamp
      // the pitch and generate multiple ranges (each for every row of tiles)
      // with gaps for padding. Very few PowerVR GPUs have 4096, not 8192, as
      // the limit, though with 8192 (on Mali) the actual limit for Xenia is
      // 8160 because tile padding is stored - but 8192 should be extremely rare
      // anyway.
      REXGPU_ERROR(
          "Surface pitch aligned to EDRAM tiles and resolution-scaled {} "
          "larger than the maximum host render target width {}",
          pitch_pixels_tile_aligned_scaled, max_render_target_width);
      return false;
    }
  }

  // Get used render targets.
  // [0] is depth / stencil where relevant, [1...4] is color.
  // Depth / stencil testing / writing is before color in the pipeline.
  uint32_t depth_and_color_rts_used_bits = 0;
  // depth_and_color_rts_used_bits -> EDRAM base.
  uint32_t edram_bases[1 + xenos::kMaxColorRenderTargets];
  uint32_t resource_formats[1 + xenos::kMaxColorRenderTargets];
  uint32_t rts_are_64bpp = 0;
  if (is_rasterization_done) {
    if (normalized_depth_control.z_enable || normalized_depth_control.stencil_enable) {
      depth_and_color_rts_used_bits |= 1;
      auto rb_depth_info = regs.Get<reg::RB_DEPTH_INFO>();
      edram_bases[0] = rb_depth_info.depth_base;
      // With pixel shader interlock, always the same addressing disregarding
      // the format.
      resource_formats[0] = interlock_barrier_only ? 0 : uint32_t(rb_depth_info.depth_format);
    }
    for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
      if (!(normalized_color_mask & (uint32_t(0b1111) << (4 * i)))) {
        continue;
      }
      auto color_info = regs.Get<reg::RB_COLOR_INFO>(reg::RB_COLOR_INFO::rt_register_indices[i]);
      uint32_t rt_bit_index = 1 + i;
      depth_and_color_rts_used_bits |= uint32_t(1) << rt_bit_index;
      edram_bases[rt_bit_index] = color_info.color_base;
      xenos::ColorRenderTargetFormat color_format =
          regs.Get<reg::RB_COLOR_INFO>(reg::RB_COLOR_INFO::rt_register_indices[i]).color_format;
      bool is_64bpp = xenos::IsColorRenderTargetFormat64bpp(color_format);
      if (is_64bpp) {
        rts_are_64bpp |= uint32_t(1) << rt_bit_index;
      }
      xenos::ColorRenderTargetFormat color_resource_format;
      if (interlock_barrier_only) {
        // Only changes in mapping between coordinates and addresses are
        // interesting (along with access overlap between draw calls), thus only
        // pixel size is relevant.
        color_resource_format = is_64bpp ? xenos::ColorRenderTargetFormat::k_16_16_16_16
                                         : xenos::ColorRenderTargetFormat::k_8_8_8_8;
      } else {
        color_resource_format = GetColorResourceFormat(xenos::GetStorageColorFormat(color_format));
      }
      resource_formats[rt_bit_index] = uint32_t(color_resource_format);
    }
  }

  uint32_t rts_remaining;
  uint32_t rt_index;

  // Eliminate other bound render targets if their EDRAM base conflicts with
  // another render target - it's an error in most host implementations to bind
  // the same render target into multiple slots, also the behavior would be
  // unpredictable if that happens.
  // Depth is considered the least important as it's earlier in the pipeline
  // (issues caused by color and depth render target collisions haven't been
  // found yet), but render targets with smaller index are considered more
  // important - specifically, because of the usage in the lighting pass of
  // 4D5307E6, which can be checked in the vertical look calibration sequence in
  // the beginning of the game: if render target 0 is removed in favor of 1, the
  // characters and the world will be too dark, like fully in shadow -
  // especially prominent on the helmet. This happens because the shader picks
  // between two render targets to write dynamically (though with a static, bool
  // constant condition), but all other state is set up in a way that implies
  // the same render target being bound twice. On Direct3D 9, if you don't write
  // to a color pixel shader output on the control flow that was taken, the
  // render target will not be written to. However, this has been relaxed in
  // Direct3D 10, where if the shader declares an output, it's assumed to be
  // always written (or with an undefined value otherwise).
  rts_remaining = depth_and_color_rts_used_bits & ~(uint32_t(1));
  while (rex::bit_scan_forward(rts_remaining, &rt_index)) {
    rts_remaining &= ~(uint32_t(1) << rt_index);
    uint32_t edram_base = edram_bases[rt_index];
    uint32_t rts_other_remaining =
        depth_and_color_rts_used_bits & (~((uint32_t(1) << (rt_index + 1)) - 1) | uint32_t(1));
    uint32_t rt_other_index;
    while (rex::bit_scan_forward(rts_other_remaining, &rt_other_index)) {
      rts_other_remaining &= ~(uint32_t(1) << rt_other_index);
      if (edram_bases[rt_other_index] == edram_base) {
        depth_and_color_rts_used_bits &= ~(uint32_t(1) << rt_other_index);
      }
    }
  }

  for (auto& range : last_update_ranges_)
    range = {};
  // Clear ownership transfers before adding any.
  if (!interlock_barrier_only) {
    for (size_t i = 0; i < rex::countof(last_update_transfers_); ++i) {
      last_update_transfers_[i].clear();
    }
  }

  if (!depth_and_color_rts_used_bits) {
    // Nothing to bind, don't waste time on things like memexport-only draws -
    // just check if old bindings can still be used.
    std::memset(last_update_used_render_targets_, 0, sizeof(last_update_used_render_targets_));
    if (are_accumulated_render_targets_valid_) {
      for (size_t i = 0; i < rex::countof(last_update_accumulated_render_targets_); ++i) {
        const RenderTarget* render_target = last_update_accumulated_render_targets_[i];
        if (!render_target) {
          continue;
        }
        RenderTargetKey rt_key = render_target->key();
        if (rt_key.pitch_tiles_at_32bpp != pitch_tiles_at_32bpp ||
            rt_key.msaa_samples != msaa_samples) {
          are_accumulated_render_targets_valid_ = false;
          break;
        }
      }
    }
    if (!are_accumulated_render_targets_valid_) {
      std::memset(last_update_accumulated_render_targets_, 0,
                  sizeof(last_update_accumulated_render_targets_));
    }
    return true;
  }

  // native_one_pass_msaa: the one-pass scene's private surfaces (16.17) are
  // bound but claim no EDRAM tiles, and do not shorten the other targets'
  // ranges.
  uint32_t private_rts = 0;
  if (!interlock_barrier_only) {
    if ((depth_and_color_rts_used_bits & 1) &&
        IsPrivateDepth(edram_bases[0], pitch_tiles_at_32bpp, msaa_samples)) {
      private_rts |= 1;
    }
    for (uint32_t i = 1; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
      if (((depth_and_color_rts_used_bits >> i) & 1) &&
          IsPrivateColor(edram_bases[i], pitch_tiles_at_32bpp, msaa_samples,
                         resource_formats[i])) {
        private_rts |= uint32_t(1) << i;
      }
    }
  }

  // Estimate height used by render targets (for color for writes, for depth /
  // stencil for both reads and writes) from various sources.
  uint32_t height_used =
      std::min(GetSurfaceHeight(pitch_tiles_at_32bpp, msaa_samples, private_rts != 0),
               draw_extent_estimator_.EstimateMaxY(
                   interlock_barrier_only
                       ? REXCVAR_GET(execute_unclipped_draw_vs_on_cpu_for_psi_render_backend)
                       : true,
                   vertex_shader) *
                   uniform_draw.scale_y);

  // Sorted by EDRAM base and then by index in the pipeline - for simplicity,
  // treat render targets placed closer to the end of the EDRAM as truncating
  // the previous one (and in case multiple render targets are placed at the
  // same EDRAM base, though normally this shouldn't happen, treat the color
  // ones as more important than the depth one, which may be not needed and just
  // a leftover if the draw, for instance, has depth / stencil happening to be
  // always passing and never writing with the current state, and also because
  // depth testing has to happen before the color is written). Overall it's
  // normal for estimated EDRAM ranges of render targets to intersect if drawing
  // without a viewport (as there's nothing to clamp the estimated height) and
  // multiple render targets are bound.
  std::pair<uint32_t, uint32_t> edram_bases_sorted[1 + xenos::kMaxColorRenderTargets];
  uint32_t edram_bases_sorted_count = 0;
  rts_remaining = depth_and_color_rts_used_bits & ~private_rts;
  while (rex::bit_scan_forward(rts_remaining, &rt_index)) {
    rts_remaining &= ~(uint32_t(1) << rt_index);
    edram_bases_sorted[edram_bases_sorted_count++] =
        std::make_pair(edram_bases[rt_index], rt_index);
  }
  std::sort(edram_bases_sorted, edram_bases_sorted + edram_bases_sorted_count);
  // "As if it was 64bpp" (contribution of 32bpp render targets multiplied by 2,
  // and clamping for 32bpp render targets divides this by 2) because 32bpp
  // render targets can be combined with twice as long 64bpp render targets. An
  // example is the 4541099D menu background (1-sample 1152x720, or 1200x720
  // after rounding to tiles, with a 32bpp depth buffer at 0 requiring 675
  // tiles, and a 64bpp color buffer at 675 requiring 1350 tiles, but the
  // smallest distance between two render target bases is 675 tiles).
  uint32_t rt_max_distance_tiles_at_64bpp = xenos::kEdramTileCount * 2;
  if (REXCVAR_GET(mrt_edram_used_range_clamp_to_min) && edram_bases_sorted_count >= 2) {
    for (uint32_t i = 1; i < edram_bases_sorted_count; ++i) {
      const std::pair<uint32_t, uint32_t>& rt_base_prev = edram_bases_sorted[i - 1];
      rt_max_distance_tiles_at_64bpp =
          std::min(rt_max_distance_tiles_at_64bpp,
                   (edram_bases_sorted[i].first - rt_base_prev.first)
                       << (((rts_are_64bpp >> rt_base_prev.second) & 1) ^ 1));
    }
    // Clamp to the distance from the last render target to the first with
    // EDRAM addressing wrapping.
    const std::pair<uint32_t, uint32_t>& rt_base_last =
        edram_bases_sorted[edram_bases_sorted_count - 1];
    rt_max_distance_tiles_at_64bpp =
        std::min(rt_max_distance_tiles_at_64bpp,
                 (xenos::kEdramTileCount + edram_bases_sorted[0].first - rt_base_last.first)
                     << (((rts_are_64bpp >> rt_base_last.second) & 1) ^ 1));
  }

  // Make sure all the needed render targets are created, and gather lengths of
  // ranges used by each render target.
  RenderTargetKey rt_keys[1 + xenos::kMaxColorRenderTargets];
  RenderTarget* rts[1 + xenos::kMaxColorRenderTargets];
  uint32_t rt_lengths_tiles[1 + xenos::kMaxColorRenderTargets];
  uint32_t length_used_tiles_at_32bpp =
      ((height_used << uint32_t(msaa_samples >= xenos::MsaaSamples::k2X)) +
       (xenos::kEdramTileHeightSamples - 1)) /
      xenos::kEdramTileHeightSamples * pitch_tiles_at_32bpp;
  rts_remaining = private_rts;
  while (rex::bit_scan_forward(rts_remaining, &rt_index)) {
    rts_remaining &= ~(uint32_t(1) << rt_index);
    RenderTargetKey& rt_key = rt_keys[rt_index];
    rt_key.base_tiles = edram_bases[rt_index];
    rt_key.pitch_tiles_at_32bpp = pitch_tiles_at_32bpp;
    rt_key.msaa_samples = msaa_samples;
    rt_key.is_depth = rt_index == 0;
    rt_key.resource_format = resource_formats[rt_index];
    RenderTarget* render_target = GetOrCreateRenderTarget(
        rt_key, ((height_used << uint32_t(msaa_samples >= xenos::MsaaSamples::k2X)) +
                 (xenos::kEdramTileHeightSamples - 1)) /
                    xenos::kEdramTileHeightSamples);
    if (!render_target) {
      return false;
    }
    rts[rt_index] = render_target;
    last_update_ranges_[rt_index] = {uint32_t(rt_key.base_tiles), 0};
  }
  if (private_rts) {
    perf_trace::CountOnePass(perf_trace::one_pass_draws);
  }
  for (uint32_t i = 0; i < edram_bases_sorted_count; ++i) {
    const std::pair<uint32_t, uint32_t>& rt_base_index = edram_bases_sorted[i];
    uint32_t rt_base = rt_base_index.first;
    uint32_t rt_bit_index = rt_base_index.second;
    RenderTargetKey& rt_key = rt_keys[rt_bit_index];
    rt_key.base_tiles = rt_base;
    rt_key.pitch_tiles_at_32bpp = pitch_tiles_at_32bpp;
    rt_key.msaa_samples = msaa_samples;
    rt_key.is_depth = rt_bit_index == 0;
    rt_key.resource_format = resource_formats[rt_bit_index];
    if (!interlock_barrier_only) {
      // With native_rt_surfaces bit 8, every bound target has the draw's rows:
      // the render area is the intersection of the bound views.
      RenderTarget* render_target = GetOrCreateRenderTarget(
          rt_key, ((height_used << uint32_t(msaa_samples >= xenos::MsaaSamples::k2X)) +
                   (xenos::kEdramTileHeightSamples - 1)) /
                      xenos::kEdramTileHeightSamples);
      if (!render_target) {
        return false;
      }
      rts[rt_bit_index] = render_target;
    }
    uint32_t rt_is_64bpp = (rts_are_64bpp >> rt_bit_index) & 1;
    // The last render target can occupy the EDRAM until the base of the first
    // render target (itself in case of 1 render target) with EDRAM addressing
    // wrapping.
    rt_lengths_tiles[i] = std::min(std::min(length_used_tiles_at_32bpp << rt_is_64bpp,
                                            rt_max_distance_tiles_at_64bpp >> (rt_is_64bpp ^ 1)),
                                   ((i + 1 < edram_bases_sorted_count)
                                        ? edram_bases_sorted[i + 1].first
                                    : (edram_bases_sorted_count == 1 &&
                                       REXCVAR_GET(edram_single_rt_no_wrap))
                                        ? xenos::kEdramTileCount
                                        : (xenos::kEdramTileCount + edram_bases_sorted[0].first)) -
                                       rt_base);
  }

  if (interlock_barrier_only) {
    // Because a full pixel shader interlock barrier may clear the ownership map
    // (since it flushes all previous writes, and there's no need for another
    // barrier if an overlap is encountered later between pre-barrier and
    // post-barrier usages), check if any overlap requiring a barrier happens,
    // and then insert the barrier if needed.
    bool interlock_barrier_needed = false;
    for (uint32_t i = 0; i < edram_bases_sorted_count; ++i) {
      const std::pair<uint32_t, uint32_t>& rt_base_index = edram_bases_sorted[i];
      if (WouldOwnershipChangeRequireTransfers(rt_keys[rt_base_index.second], 0,
                                               rt_lengths_tiles[i])) {
        interlock_barrier_needed = true;
        break;
      }
    }
    if (interlock_barrier_needed) {
      RequestPixelShaderInterlockBarrier();
    }
  }

  // From now on ownership transfers should succeed for simplicity and
  // consistency, even if they fail in the implementation (just ignore that and
  // draw with whatever contents currently are in the render target in this
  // case).

  // The render target pixels that a uniform draw covers entirely, for the slots
  // it overwrites: transfers of tile ranges within them would be overwritten.
  Transfer::Rectangle overwrite_cutout = {};
  uint32_t overwrite_cutout_slots = 0;
  if (!interlock_barrier_only && uniform_draw.bounds_valid &&
      (uniform_draw.overwritten_slots & depth_and_color_rts_used_bits)) {
    draw_util::Scissor scissor;
    draw_util::GetScissor(regs, scissor, false);
    // In render target pixels: the rectangle and the scissor, scaled from guest
    // pixels of the draw, and the pixels with all samples inside - with one
    // sample, the pixel center by the top-left rule, [ceil(x0 - 0.5),
    // ceil(x1 - 0.5)), otherwise the whole pixel, [ceil(x0), floor(x1)).
    float scale[] = {float(uniform_draw.scale_x), float(uniform_draw.scale_y)};
    int32_t covered[4];
    for (uint32_t i = 0; i < 2; ++i) {
      float low = uniform_draw.bounds[i] * scale[i];
      float high = uniform_draw.bounds[2 + i] * scale[i];
      if (msaa_samples == xenos::MsaaSamples::k1X) {
        covered[i] = int32_t(std::ceil(low - 0.5f));
        covered[2 + i] = int32_t(std::ceil(high - 0.5f));
      } else {
        covered[i] = int32_t(std::ceil(low));
        covered[2 + i] = int32_t(std::floor(high));
      }
      int32_t scissor_scale = int32_t(i ? uniform_draw.scale_y : uniform_draw.scale_x);
      covered[i] = std::max(std::max(covered[i], int32_t(scissor.offset[i]) * scissor_scale),
                            int32_t(0));
      covered[2 + i] = std::min(covered[2 + i],
                                int32_t(scissor.offset[i] + scissor.extent[i]) * scissor_scale);
    }
    if (covered[2] > covered[0] && covered[3] > covered[1]) {
      overwrite_cutout.x_pixels = uint32_t(covered[0]);
      overwrite_cutout.y_pixels = uint32_t(covered[1]);
      overwrite_cutout.width_pixels = uint32_t(covered[2] - covered[0]);
      overwrite_cutout.height_pixels = uint32_t(covered[3] - covered[1]);
      overwrite_cutout_slots = uniform_draw.overwritten_slots;
    }
  }
  last_update_overwrite_cutout_ = overwrite_cutout;
  last_update_overwrite_cutout_slots_ = overwrite_cutout_slots;
  if (SurfaceProbe* probe = GetSurfaceProbe(); probe && !interlock_barrier_only) {
    std::string line = fmt::format("surface {:08X}", regs[XE_GPU_REG_RB_SURFACE_INFO]);
    if (depth_and_color_rts_used_bits & 1) {
      const auto depth_info = regs.Get<reg::RB_DEPTH_INFO>();
      line += fmt::format(" depth {:08X} base {}", depth_info.value,
                          depth_info.depth_base | (depth_info.depth_base_bit_11 << 11));
    }
    for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
      if ((depth_and_color_rts_used_bits >> (1 + i)) & 1) {
        const auto color_info =
            regs.Get<reg::RB_COLOR_INFO>(reg::RB_COLOR_INFO::rt_register_indices[i]);
        line += fmt::format(" color{} {:08X} base {}", i, color_info.value,
                            color_info.color_base | (color_info.color_base_bit_11 << 11));
      }
    }
    line += fmt::format(" height {} window {:08X} scissor {:08X} {:08X} screen {:08X}", height_used,
                        regs[XE_GPU_REG_PA_SC_WINDOW_OFFSET],
                        regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL],
                        regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR],
                        regs[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR]);
    if (++probe->counts[line] == 1) {
      probe->output << "update " << render_target_update_clock_ << ' ' << line << std::endl;
    }
  }
  // native_rt_surfaces bit 4 (d3d-hle.md 16.12): the deferred tiles that change
  // their owner leave their holder before the claims, and the render target
  // pixels the draw may touch - its scissor, and with a covered rectangle only
  // the pixels the rectangle reaches.
  const bool lazy_transfers = !interlock_barrier_only && GetPath() == Path::kHostRenderTargets &&
                              (lazy_held_tiles_ || IsLazyTransfersEnabled());
  Transfer::Rectangle lazy_touched = {};
  if (lazy_transfers) {
    for (uint32_t i = 0; i < edram_bases_sorted_count; ++i) {
      DetachLazyHolders(rt_keys[edram_bases_sorted[i].second], 0, rt_lengths_tiles[i]);
    }
    if (is_rasterization_done) {
      draw_util::Scissor scissor;
      draw_util::GetScissor(regs, scissor, false);
      const float scale[] = {float(uniform_draw.scale_x), float(uniform_draw.scale_y)};
      int32_t low[2], high[2];
      for (uint32_t i = 0; i < 2; ++i) {
        const int32_t scissor_scale = int32_t(i ? uniform_draw.scale_y : uniform_draw.scale_x);
        low[i] = int32_t(scissor.offset[i]) * scissor_scale;
        high[i] = int32_t(scissor.offset[i] + scissor.extent[i]) * scissor_scale;
        if (uniform_draw.bounds_valid) {
          low[i] = std::max(low[i], int32_t(std::floor(uniform_draw.bounds[i] * scale[i])));
          high[i] = std::min(high[i], int32_t(std::ceil(uniform_draw.bounds[2 + i] * scale[i])));
        }
        low[i] = std::max(low[i], int32_t(0));
      }
      if (high[0] > low[0] && high[1] > low[1]) {
        lazy_touched.x_pixels = uint32_t(low[0]);
        lazy_touched.y_pixels = uint32_t(low[1]);
        lazy_touched.width_pixels = uint32_t(high[0] - low[0]);
        lazy_touched.height_pixels = uint32_t(high[1] - low[1]);
      }
    }
  }
  for (uint32_t i = 0; i < edram_bases_sorted_count; ++i) {
    const std::pair<uint32_t, uint32_t>& rt_base_index = edram_bases_sorted[i];
    uint32_t rt_bit_index = rt_base_index.second;
    last_update_ranges_[rt_bit_index] = {uint32_t(rt_keys[rt_bit_index].base_tiles),
                                         rt_lengths_tiles[i]};
    ChangeOwnership(rt_keys[rt_bit_index], 0, rt_lengths_tiles[i],
                    interlock_barrier_only ? nullptr : &last_update_transfers_[rt_bit_index],
                    (overwrite_cutout_slots >> rt_bit_index) & 1 ? &overwrite_cutout : nullptr);
  }
  if (lazy_transfers) {
    DeferTransfers(rt_keys, rts, depth_and_color_rts_used_bits, lazy_touched, overwrite_cutout,
                   overwrite_cutout_slots);
  }
  if (!interlock_barrier_only && (REXCVAR_GET(native_rt_surfaces) & 1)) {
    CountStaleTilesForDraw(rt_keys, depth_and_color_rts_used_bits, overwrite_cutout,
                           overwrite_cutout_slots, is_rasterization_done);
  }

  // Remember the sample count that the render targets of a uniform draw are
  // next used with (keys compared without the sample count).
  if (!interlock_barrier_only) {
    auto without_msaa = [](RenderTargetKey key) {
      key.msaa_samples = xenos::MsaaSamples::k1X;
      return key;
    };
    if (uniform_draw.signature) {
      pending_uniform_draw_signature_ = uniform_draw.signature;
      for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
        pending_uniform_draw_keys_[i] = (depth_and_color_rts_used_bits >> i) & 1
                                            ? without_msaa(rt_keys[i])
                                            : RenderTargetKey();
      }
    } else if (pending_uniform_draw_signature_) {
      for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
        if (!((depth_and_color_rts_used_bits >> i) & 1)) {
          continue;
        }
        RenderTargetKey key = without_msaa(rt_keys[i]);
        bool matches = false;
        for (const RenderTargetKey& pending_key : pending_uniform_draw_keys_) {
          matches |= !pending_key.IsEmpty() && pending_key == key;
        }
        if (matches) {
          uniform_draw_next_msaa_[pending_uniform_draw_signature_] = rt_keys[i].msaa_samples;
          pending_uniform_draw_signature_ = 0;
          break;
        }
      }
    }
  }

  if (interlock_barrier_only) {
    // No copying transfers or render target bindings - only needed the barrier.
    return true;
  }

  // If everything succeeded, update the used render targets.
  for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
    last_update_used_render_targets_[i] =
        (depth_and_color_rts_used_bits & (uint32_t(1) << i)) ? rts[i] : nullptr;
  }
  if (are_accumulated_render_targets_valid_) {
    // Check if the only re-enabling a previously bound render target.
    for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
      RenderTarget* current_rt =
          (depth_and_color_rts_used_bits & (uint32_t(1) << i)) ? rts[i] : nullptr;
      const RenderTarget* accumulated_rt = last_update_accumulated_render_targets_[i];
      if (!accumulated_rt) {
        if (current_rt) {
          // Binding a totally new render target - won't keep the existing
          // render pass anyway, no much need to try to re-enable previously
          // disabled render targets in other slots as well, even though that
          // would be valid.
          are_accumulated_render_targets_valid_ = false;
          break;
        }
        // Append the new render target.
        last_update_accumulated_render_targets_[i] = current_rt;
        continue;
      }
      if (current_rt) {
        if (current_rt != accumulated_rt) {
          // Changing a render target in a slot.
          are_accumulated_render_targets_valid_ = false;
          break;
        }
      } else {
        RenderTargetKey accumulated_rt_key = accumulated_rt->key();
        if (accumulated_rt_key.pitch_tiles_at_32bpp != pitch_tiles_at_32bpp ||
            accumulated_rt_key.msaa_samples != msaa_samples) {
          // The previously bound render target is incompatible with the
          // current surface info.
          are_accumulated_render_targets_valid_ = false;
          break;
        }
      }
    }
    // Make sure the same render target isn't bound into two different slots
    // over time.
    for (uint32_t i = 1;
         are_accumulated_render_targets_valid_ && i < 1 + xenos::kMaxColorRenderTargets; ++i) {
      const RenderTarget* render_target = last_update_accumulated_render_targets_[i];
      if (!render_target) {
        continue;
      }
      for (uint32_t j = 0; j < i; ++j) {
        if (last_update_accumulated_render_targets_[j] == render_target) {
          are_accumulated_render_targets_valid_ = false;
          break;
        }
      }
    }
  }
  if (!are_accumulated_render_targets_valid_) {
    std::memcpy(last_update_accumulated_render_targets_, last_update_used_render_targets_,
                sizeof(last_update_accumulated_render_targets_));
    are_accumulated_render_targets_valid_ = true;
  }

  return true;
}

uint32_t RenderTargetCache::GetLastUpdateBoundRenderTargets(
    uint32_t* depth_and_color_formats_out) const {
  if (GetPath() != Path::kHostRenderTargets) {
    if (depth_and_color_formats_out) {
      std::memset(depth_and_color_formats_out, 0,
                  sizeof(uint32_t) * (1 + xenos::kMaxColorRenderTargets));
    }
    return 0;
  }
  uint32_t rts_used = 0;
  for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
    const RenderTarget* render_target = last_update_accumulated_render_targets_[i];
    if (!render_target) {
      if (depth_and_color_formats_out) {
        depth_and_color_formats_out[i] = 0;
      }
      continue;
    }
    rts_used |= uint32_t(1) << i;
    if (depth_and_color_formats_out) {
      depth_and_color_formats_out[i] = render_target->key().resource_format;
    }
  }
  return rts_used;
}

void RenderTargetCache::EndStaleTile(uint32_t tile, uint32_t outcome) {
  uint32_t& transfer = stale_tile_transfers_[tile];
  if (!transfer) {
    return;
  }
  static perf_trace::StaleTileOutcome kOutcomes[] = {
      perf_trace::StaleTileOutcome::kDropped, perf_trace::StaleTileOutcome::kOverwritten,
      perf_trace::StaleTileOutcome::kReadByDraw, perf_trace::StaleTileOutcome::kReadByResolve};
  perf_trace::CountStaleTile(kOutcomes[outcome]);
  auto it = stale_transfer_pending_.find(transfer);
  if (it != stale_transfer_pending_.end()) {
    // The high bit: some tile of the transfer was read.
    if (outcome >= 2) {
      it->second |= UINT32_C(1) << 31;
    }
    if (!(--it->second & ~(UINT32_C(1) << 31))) {
      perf_trace::CountStaleTransferEnd((it->second >> 31) != 0);
      stale_transfer_pending_.erase(it);
    }
  }
  transfer = 0;
}

void RenderTargetCache::CountStaleTilesForDraw(const RenderTargetKey* keys, uint32_t used_bits,
                                               const Transfer::Rectangle& overwrite_cutout,
                                               uint32_t overwrite_cutout_slots,
                                               bool is_rasterization_done) {
  if (stale_tile_transfers_.empty()) {
    stale_tile_transfers_.resize(xenos::kEdramTileCount);
  }
  draw_util::Scissor scissor;
  draw_util::GetScissor(register_file(), scissor);
  for (uint32_t slot = 0; slot < 1 + xenos::kMaxColorRenderTargets; ++slot) {
    if (!((used_bits >> slot) & 1)) {
      continue;
    }
    const RenderTargetKey key = keys[slot];
    const uint32_t pitch_tiles = key.GetPitchTiles();
    if (!pitch_tiles) {
      continue;
    }
    // The tiles that this update's transfers gave the render target, minus
    // those wholly inside the cutout, which the transfer leaves out.
    const uint32_t tile_width =
        (xenos::kEdramTileWidthSamples >> uint32_t(key.Is64bpp())) >>
        uint32_t(key.msaa_samples >= xenos::MsaaSamples::k4X);
    const uint32_t tile_height =
        xenos::kEdramTileHeightSamples >> uint32_t(key.msaa_samples >= xenos::MsaaSamples::k2X);
    auto tile_inside = [&](uint32_t relative_tile, const Transfer::Rectangle& rect) {
      const uint32_t x = (relative_tile % pitch_tiles) * tile_width;
      const uint32_t y = (relative_tile / pitch_tiles) * tile_height;
      return x >= rect.x_pixels && y >= rect.y_pixels &&
             x + tile_width <= rect.x_pixels + rect.width_pixels &&
             y + tile_height <= rect.y_pixels + rect.height_pixels;
    };
    const bool overwrites = (overwrite_cutout_slots >> slot) & 1;
    for (const Transfer& transfer : last_update_transfers_[slot]) {
      const uint32_t id = stale_next_transfer_++;
      uint32_t pending = 0;
      for (uint32_t tile = transfer.start_tiles; tile < transfer.end_tiles; ++tile) {
        const uint32_t edram_tile = tile & (xenos::kEdramTileCount - 1);
        // The previous owner's data that nobody read.
        EndStaleTile(edram_tile, 0);
        const uint32_t relative_tile =
            (edram_tile - key.base_tiles) & (xenos::kEdramTileCount - 1);
        if (overwrites && tile_inside(relative_tile, overwrite_cutout)) {
          continue;
        }
        stale_tile_transfers_[edram_tile] = id;
        ++pending;
      }
      perf_trace::CountStaleTransfer(transfer.end_tiles - transfer.start_tiles, pending);
      if (pending) {
        stale_transfer_pending_[id] = pending;
      }
    }
    if (!is_rasterization_done) {
      continue;
    }
    // The draw: overwrites the tiles wholly inside its rectangle, or may read
    // every tile its scissor touches.
    const std::pair<uint32_t, uint32_t> range = last_update_ranges_[slot];
    for (uint32_t relative_tile = 0; relative_tile < range.second; ++relative_tile) {
      const uint32_t edram_tile = (key.base_tiles + relative_tile) & (xenos::kEdramTileCount - 1);
      if (!stale_tile_transfers_[edram_tile]) {
        continue;
      }
      if (overwrites) {
        if (tile_inside(relative_tile, overwrite_cutout)) {
          EndStaleTile(edram_tile, 1);
        }
        continue;
      }
      const uint32_t x = (relative_tile % pitch_tiles) * tile_width;
      const uint32_t y = (relative_tile / pitch_tiles) * tile_height;
      if (x < scissor.offset[0] + scissor.extent[0] && x + tile_width > scissor.offset[0] &&
          y < scissor.offset[1] + scissor.extent[1] && y + tile_height > scissor.offset[1]) {
        EndStaleTile(edram_tile, 2);
      }
    }
  }
}

void RenderTargetCache::CountStaleTilesForResolve(uint32_t base_tiles, uint32_t row_length_used,
                                                  uint32_t rows, uint32_t pitch_tiles,
                                                  bool clears) {
  if (stale_tile_transfers_.empty() || !(REXCVAR_GET(native_rt_surfaces) & 1)) {
    return;
  }
  for (uint32_t row = 0; row < rows; ++row) {
    for (uint32_t column = 0; column < row_length_used; ++column) {
      EndStaleTile((base_tiles + row * pitch_tiles + column) & (xenos::kEdramTileCount - 1),
                   clears ? 1 : 3);
    }
  }
}

bool RenderTargetCache::IsLazyTransfersEnabled() const {
  return GetPath() == Path::kHostRenderTargets && (REXCVAR_GET(native_rt_surfaces) & 4);
}

void RenderTargetCache::SetLazyHolder(uint32_t tile, RenderTarget* owner, RenderTarget* holder) {
  RenderTarget*& entry = lazy_holders_[tile];
  if (entry) {
    --lazy_held_tiles_;
    if (owner) {
      --owner->lazy_owned_held_tiles_;
    }
  }
  entry = holder;
  if (holder) {
    ++lazy_held_tiles_;
    if (owner) {
      ++owner->lazy_owned_held_tiles_;
    }
  }
  perf_trace::lazy_pending_tiles.store(lazy_held_tiles_, std::memory_order_relaxed);
}

void RenderTargetCache::SortAndMergeTransfers(std::vector<Transfer>& transfers) {
  std::sort(transfers.begin(), transfers.end(), [](const Transfer& a, const Transfer& b) {
    return std::tie(a.source, a.host_depth_source, a.start_tiles) <
           std::tie(b.source, b.host_depth_source, b.start_tiles);
  });
  size_t merged = 0;
  for (size_t i = 0; i < transfers.size(); ++i) {
    if (merged && transfers[merged - 1].AreSourcesSame(transfers[i]) &&
        transfers[merged - 1].end_tiles == transfers[i].start_tiles) {
      transfers[merged - 1].end_tiles = transfers[i].end_tiles;
    } else {
      transfers[merged++] = transfers[i];
    }
  }
  transfers.erase(transfers.begin() + merged, transfers.end());
}

void RenderTargetCache::DetachLazyHolders(RenderTargetKey dest, uint32_t start_tiles_base_relative,
                                          uint32_t length_tiles) {
  if (!lazy_held_tiles_ || !length_tiles) {
    return;
  }
  if (lazy_detached_.empty()) {
    lazy_detached_.resize(xenos::kEdramTileCount);
  }
  // The claimed extent, split where it wraps around the EDRAM.
  const uint32_t start = (dest.base_tiles + start_tiles_base_relative) & (xenos::kEdramTileCount - 1);
  const uint32_t length = std::min(length_tiles, xenos::kEdramTileCount);
  const uint32_t extents[2][2] = {
      {start, std::min(start + length, xenos::kEdramTileCount)},
      {0, start + length > xenos::kEdramTileCount ? start + length - xenos::kEdramTileCount : 0}};
  for (const auto& extent : extents) {
    uint32_t tile = extent[0];
    while (tile < extent[1]) {
      // The ownership range of the tile: skipped whole when the claiming render
      // target owns it already or its owner has no deferred tile.
      auto range_it = ownership_ranges_.upper_bound(tile);
      assert_true(range_it != ownership_ranges_.begin());
      --range_it;
      const uint32_t run_end = std::min(range_it->second.end_tiles, extent[1]);
      const RenderTargetKey owner_key = range_it->second.render_target;
      RenderTarget* owner = nullptr;
      if (owner_key != dest && !owner_key.IsEmpty()) {
        auto owner_it = render_targets_.find(owner_key);
        if (owner_it != render_targets_.end()) {
          owner = owner_it->second;
        }
      }
      if (owner && owner->lazy_owned_held_tiles_) {
        for (; tile < run_end; ++tile) {
          if (RenderTarget* holder = lazy_holders_[tile]) {
            SetLazyHolder(tile, owner, nullptr);
            lazy_detached_[tile] = holder;
            lazy_detached_tiles_.push_back(tile);
          }
        }
      }
      tile = run_end;
    }
  }
}

void RenderTargetCache::SubstituteDetachedHolders(RenderTarget* dest,
                                                  std::vector<Transfer>& transfers) {
  if (lazy_detached_tiles_.empty() || !dest) {
    return;
  }
  lazy_transfers_scratch_.clear();
  uint32_t copied = 0, claimed_back = 0;
  for (const Transfer& transfer : transfers) {
    for (uint32_t tile = transfer.start_tiles; tile < transfer.end_tiles; ++tile) {
      const uint32_t edram_tile = tile & (xenos::kEdramTileCount - 1);
      RenderTarget* source = transfer.source;
      RenderTarget* host_depth_source = transfer.host_depth_source;
      if (RenderTarget* holder = lazy_detached_[edram_tile]) {
        lazy_detached_[edram_tile] = nullptr;
        source = holder;
        if (host_depth_source == source) {
          host_depth_source = nullptr;
        }
        if (source == dest) {
          ++claimed_back;
          continue;
        }
        ++copied;
      }
      lazy_transfers_scratch_.emplace_back(tile, tile + 1, source, host_depth_source);
    }
  }
  SortAndMergeTransfers(lazy_transfers_scratch_);
  transfers.swap(lazy_transfers_scratch_);
  perf_trace::CountLazyTiles(perf_trace::LazyTileOutcome::kFlushedClear, copied);
  perf_trace::CountLazyTiles(perf_trace::LazyTileOutcome::kClaimedBack, claimed_back);
}

void RenderTargetCache::EndDetachedHolders() {
  // Detached but in no transfer: inside an overwrite cutout or a clear, whose
  // pixels replace the deferred data.
  uint32_t overwritten = 0;
  for (uint32_t tile : lazy_detached_tiles_) {
    if (lazy_detached_[tile]) {
      lazy_detached_[tile] = nullptr;
      ++overwritten;
    }
  }
  lazy_detached_tiles_.clear();
  perf_trace::CountLazyTiles(perf_trace::LazyTileOutcome::kOverwritten, overwritten);
}

void RenderTargetCache::DeferTransfers(const RenderTargetKey* keys, RenderTarget* const* rts,
                                       uint32_t used_bits, const Transfer::Rectangle& touched,
                                       const Transfer::Rectangle& overwrite_cutout,
                                       uint32_t overwrite_cutout_slots) {
  if (lazy_holders_.empty()) {
    lazy_holders_.resize(xenos::kEdramTileCount);
  }
  if (lazy_detached_.empty()) {
    lazy_detached_.resize(xenos::kEdramTileCount);
  }
  const bool defer = IsLazyTransfersEnabled();
  const bool check = REXCVAR_GET(native_rt_surfaces_lazy_check);
  const uint32_t perturb_every =
      check ? uint32_t(std::max(REXCVAR_GET(native_rt_surfaces_lazy_perturb), int32_t(0))) : 0;
  for (uint32_t slot = 0; slot < 1 + xenos::kMaxColorRenderTargets; ++slot) {
    if (!((used_bits >> slot) & 1)) {
      continue;
    }
    const RenderTargetKey key = keys[slot];
    RenderTarget* dest = rts[slot];
    const uint32_t pitch_tiles = key.GetPitchTiles();
    if (!dest || !pitch_tiles) {
      continue;
    }
    const uint32_t tile_width =
        (xenos::kEdramTileWidthSamples >> uint32_t(key.Is64bpp())) >>
        uint32_t(key.msaa_samples >= xenos::MsaaSamples::k4X);
    const uint32_t tile_height =
        xenos::kEdramTileHeightSamples >> uint32_t(key.msaa_samples >= xenos::MsaaSamples::k2X);
    // The tile columns and rows relative to the render target that the draw
    // may touch: it reads and writes nothing outside them.
    uint32_t columns[2] = {}, rows[2] = {};
    if (touched.width_pixels && touched.height_pixels) {
      columns[0] = touched.x_pixels / tile_width;
      columns[1] = std::min((touched.x_pixels + touched.width_pixels + tile_width - 1) / tile_width,
                            pitch_tiles);
      rows[0] = touched.y_pixels / tile_height;
      rows[1] = (touched.y_pixels + touched.height_pixels + tile_height - 1) / tile_height;
    }
    auto read_by_draw = [&](uint32_t relative_tile) {
      const uint32_t column = relative_tile % pitch_tiles, row = relative_tile / pitch_tiles;
      return column >= columns[0] && column < columns[1] && row >= rows[0] && row < rows[1];
    };
    // Wholly inside the overwrite cutout: the copy is dropped (counted only).
    const bool overwrites = (overwrite_cutout_slots >> slot) & 1;
    auto overwritten = [&](uint32_t relative_tile) {
      if (!overwrites) {
        return false;
      }
      const uint32_t x = (relative_tile % pitch_tiles) * tile_width;
      const uint32_t y = (relative_tile / pitch_tiles) * tile_height;
      return x >= overwrite_cutout.x_pixels && y >= overwrite_cutout.y_pixels &&
             x + tile_width <= overwrite_cutout.x_pixels + overwrite_cutout.width_pixels &&
             y + tile_height <= overwrite_cutout.y_pixels + overwrite_cutout.height_pixels;
    };
    const bool dest_host_depth =
        key.is_depth && IsHostDepthEncodingDifferent(key.GetDepthFormat());
    std::vector<Transfer>& transfers = last_update_transfers_[slot];
    lazy_transfers_scratch_.clear();
    auto emit = [&](uint32_t tile, RenderTarget* source, RenderTarget* host_depth_source) {
      if (!lazy_transfers_scratch_.empty()) {
        Transfer& last = lazy_transfers_scratch_.back();
        if (last.end_tiles == tile && last.source == source &&
            last.host_depth_source == host_depth_source) {
          ++last.end_tiles;
          return;
        }
      }
      lazy_transfers_scratch_.emplace_back(tile, tile + 1, source, host_depth_source);
    };
    // The tiles that change their owner: from the holder detached before the
    // claim if there is one, none when the holder takes them back, deferred
    // unless the draw touches them.
    uint32_t copied = 0, deferred = 0, claimed_back = 0, redeferred = 0, chain_copied = 0,
             overwritten_tiles = 0;
    for (const Transfer& transfer : transfers) {
      for (uint32_t tile = transfer.start_tiles; tile < transfer.end_tiles; ++tile) {
        const uint32_t edram_tile = tile & (xenos::kEdramTileCount - 1);
        RenderTarget* source = transfer.source;
        RenderTarget* host_depth_source = transfer.host_depth_source;
        const bool held = lazy_detached_[edram_tile] != nullptr;
        if (held) {
          source = lazy_detached_[edram_tile];
          lazy_detached_[edram_tile] = nullptr;
          if (host_depth_source == source) {
            host_depth_source = nullptr;
          }
        }
        if (source == dest) {
          ++claimed_back;
          continue;
        }
        const uint32_t relative_tile =
            (edram_tile - key.base_tiles) & (xenos::kEdramTileCount - 1);
        if (defer && !host_depth_source && !dest_host_depth && !read_by_draw(relative_tile)) {
          SetLazyHolder(edram_tile, dest, source);
          ++(held ? redeferred : deferred);
          continue;
        }
        emit(tile, source, host_depth_source);
        if (!held) {
          ++copied;
        } else if (overwritten(relative_tile)) {
          ++overwritten_tiles;
        } else {
          ++chain_copied;
        }
      }
    }
    // The tiles the render target already owned, deferred before, that the draw
    // touches now.
    uint32_t flushed = 0;
    const uint32_t range_length = last_update_ranges_[slot].second;
    if (dest->lazy_owned_held_tiles_ && columns[1] > columns[0]) {
      for (uint32_t row = rows[0]; row < rows[1]; ++row) {
        for (uint32_t column = columns[0]; column < columns[1]; ++column) {
          const uint32_t relative_tile = row * pitch_tiles + column;
          if (relative_tile >= range_length) {
            break;
          }
          const uint32_t edram_tile =
              (key.base_tiles + relative_tile) & (xenos::kEdramTileCount - 1);
          RenderTarget* holder = lazy_holders_[edram_tile];
          if (!holder) {
            continue;
          }
          if (perturb_every && ++lazy_flushes_ % perturb_every == 0) {
            perf_trace::CountLazyTiles(perf_trace::LazyTileOutcome::kPerturbed, 1);
            continue;
          }
          SetLazyHolder(edram_tile, dest, nullptr);
          emit(edram_tile, holder, nullptr);
          ++(overwritten(relative_tile) ? overwritten_tiles : flushed);
        }
      }
    }
    if (check && columns[1] > columns[0]) {
      // No deferred tile left where the draw touches the range it claimed
      // (violations), and none held by this render target where it may touch
      // tiles beyond that range (hazards: it would draw into data it holds for
      // another owner).
      uint32_t violations = 0, hazards = 0;
      for (uint32_t row = rows[0]; row < rows[1]; ++row) {
        for (uint32_t column = columns[0]; column < columns[1]; ++column) {
          const uint32_t relative_tile = row * pitch_tiles + column;
          const uint32_t edram_tile =
              (key.base_tiles + relative_tile) & (xenos::kEdramTileCount - 1);
          RenderTarget* holder = lazy_holders_[edram_tile];
          if (relative_tile < range_length) {
            violations += holder != nullptr;
          } else {
            hazards += holder == dest;
          }
        }
      }
      perf_trace::CountLazyTiles(perf_trace::LazyTileOutcome::kViolation, violations);
      perf_trace::CountLazyTiles(perf_trace::LazyTileOutcome::kHazard, hazards);
    }
    perf_trace::CountLazyTiles(perf_trace::LazyTileOutcome::kCopied, copied);
    perf_trace::CountLazyTiles(perf_trace::LazyTileOutcome::kDeferred, deferred);
    perf_trace::CountLazyTiles(perf_trace::LazyTileOutcome::kClaimedBack, claimed_back);
    perf_trace::CountLazyTiles(perf_trace::LazyTileOutcome::kFlushedDraw, flushed);
    perf_trace::CountLazyTiles(perf_trace::LazyTileOutcome::kRedeferred, redeferred);
    perf_trace::CountLazyTiles(perf_trace::LazyTileOutcome::kChainCopied, chain_copied);
    perf_trace::CountLazyTiles(perf_trace::LazyTileOutcome::kOverwritten, overwritten_tiles);
    if (deferred || claimed_back || flushed || copied || redeferred || chain_copied ||
        overwritten_tiles) {
      SortAndMergeTransfers(lazy_transfers_scratch_);
      transfers.swap(lazy_transfers_scratch_);
    }
  }
  EndDetachedHolders();
}

void RenderTargetCache::TakeDeferredTransfers(
    uint32_t base_tiles, uint32_t row_length_used, uint32_t rows, uint32_t pitch_tiles,
    const RenderTarget* owner, uint32_t flush_outcome,
    std::vector<std::pair<RenderTarget*, std::vector<Transfer>>>& out,
    const RenderTarget* holder_filter) {
  if (!lazy_held_tiles_ || (owner && !owner->lazy_owned_held_tiles_)) {
    return;
  }
  uint32_t flushed = 0;
  auto take = [&](uint32_t tile) {
    RenderTarget* holder = lazy_holders_[tile];
    if (!holder || (holder_filter && holder != holder_filter)) {
      return;
    }
    auto range_it = ownership_ranges_.upper_bound(tile);
    assert_true(range_it != ownership_ranges_.begin());
    --range_it;
    auto tile_owner_it = render_targets_.find(range_it->second.render_target);
    RenderTarget* tile_owner =
        tile_owner_it != render_targets_.end() ? tile_owner_it->second : nullptr;
    if (owner && tile_owner != owner) {
      return;
    }
    SetLazyHolder(tile, tile_owner, nullptr);
    if (!tile_owner) {
      return;
    }
    auto group_it = std::find_if(out.begin(), out.end(),
                                 [&](const auto& group) { return group.first == tile_owner; });
    if (group_it == out.end()) {
      out.emplace_back(tile_owner, std::vector<Transfer>());
      group_it = std::prev(out.end());
    }
    group_it->second.emplace_back(tile, tile + 1, holder, nullptr);
    ++flushed;
  };
  if (owner || holder_filter) {
    for (uint32_t tile = 0; tile < xenos::kEdramTileCount; ++tile) {
      take(tile);
    }
  } else {
    for (uint32_t row = 0; row < rows; ++row) {
      for (uint32_t column = 0; column < row_length_used; ++column) {
        take((base_tiles + row * pitch_tiles + column) & (xenos::kEdramTileCount - 1));
      }
    }
  }
  for (auto& group : out) {
    SortAndMergeTransfers(group.second);
  }
  perf_trace::CountLazyTiles(perf_trace::LazyTileOutcome(flush_outcome), flushed);
}

uint32_t RenderTargetCache::GetRenderTargetHeight(uint32_t pitch_tiles_at_32bpp,
                                                  xenos::MsaaSamples msaa_samples) const {
  if (!pitch_tiles_at_32bpp) {
    return 0;
  }
  // Down to the beginning of the render target in the next 11-bit EDRAM
  // addressing period.
  uint32_t tile_rows = (xenos::kEdramTileCount + (pitch_tiles_at_32bpp - 1)) / pitch_tiles_at_32bpp;
  // Clamp to the guest limit (tile padding should exceed it) and to the host
  // limit (tile padding mustn't exceed it).
  static_assert(!(xenos::kTexture2DCubeMaxWidthHeight % xenos::kEdramTileHeightSamples),
                "Maximum guest render target height is assumed to always be a multiple "
                "of an EDRAM tile height");
  uint32_t max_height_scaled = std::min(
      xenos::kTexture2DCubeMaxWidthHeight * draw_resolution_scale_y(), GetMaxRenderTargetHeight());
  uint32_t msaa_samples_y_log2 = uint32_t(msaa_samples >= xenos::MsaaSamples::k2X);
  uint32_t tile_height_samples_scaled = xenos::kEdramTileHeightSamples * draw_resolution_scale_y();
  tile_rows =
      std::min(tile_rows, (max_height_scaled << msaa_samples_y_log2) / tile_height_samples_scaled);
  assert_not_zero(tile_rows);
  return tile_rows * (xenos::kEdramTileHeightSamples >> msaa_samples_y_log2);
}

void RenderTargetCache::GetHostDepthStoreRectangleInfo(
    const Transfer::Rectangle& transfer_rectangle, xenos::MsaaSamples msaa_samples,
    HostDepthStoreRectangleConstant& rectangle_constant_out, uint32_t& group_count_x_out,
    uint32_t& group_count_y_out) const {
  // Initialize to all bits zeroed.
  HostDepthStoreRectangleConstant rectangle_constant;
  // 8 pixels is the resolve granularity, both clearing and tile size are
  // aligned to 8.
  assert_zero(transfer_rectangle.x_pixels & 7);
  assert_zero(transfer_rectangle.y_pixels & 7);
  assert_zero(transfer_rectangle.width_pixels & 7);
  assert_zero(transfer_rectangle.height_pixels & 7);
  assert_not_zero(transfer_rectangle.width_pixels);
  rectangle_constant.x_pixels_div_8 = transfer_rectangle.x_pixels >> 3;
  rectangle_constant.y_pixels_div_8 = transfer_rectangle.y_pixels >> 3;
  rectangle_constant.width_pixels_div_8_minus_1 = (transfer_rectangle.width_pixels >> 3) - 1;
  rectangle_constant_out = rectangle_constant;
  // 1 thread group = 64x8 host samples.
  uint32_t pixel_size_x = draw_resolution_scale_x()
                          << uint32_t(msaa_samples >= xenos::MsaaSamples::k4X);
  uint32_t pixel_size_y = draw_resolution_scale_y()
                          << uint32_t(msaa_samples >= xenos::MsaaSamples::k2X);
  group_count_x_out = (transfer_rectangle.width_pixels * pixel_size_x + 63) >> 6;
  group_count_y_out = (transfer_rectangle.height_pixels * pixel_size_y) >> 3;
}

void RenderTargetCache::GetResolveCopyRectanglesToDump(
    uint32_t base, uint32_t row_length, uint32_t rows, uint32_t pitch,
    std::vector<ResolveCopyDumpRectangle>& rectangles_out) const {
  rectangles_out.clear();
  assert_true(row_length <= pitch);
  row_length = std::min(row_length, pitch);
  if (!row_length || !rows) {
    return;
  }
  auto get_rectangles_in_extent = [&](uint32_t extent_start, uint32_t extent_end,
                                      uint32_t range_local_offset) {
    // Collect render targets owning ranges within the specified rectangle. The
    // first render target in the range may be before the lower_bound, only
    // being in the range with its tail.
    auto it = ownership_ranges_.lower_bound(extent_start);
    if (it != ownership_ranges_.cbegin()) {
      auto it_pre = std::prev(it);
      if (it_pre->second.end_tiles > extent_start) {
        it = it_pre;
      }
    }
    for (; it != ownership_ranges_.cend(); ++it) {
      uint32_t range_global_start = std::max(it->first, extent_start);
      if (range_global_start >= extent_end) {
        break;
      }
      RenderTargetKey rt_key = it->second.render_target;
      if (rt_key.IsEmpty()) {
        continue;
      }
      // Merge with other render target ranges with the same current ownership,
      // but different depth ownership, since it's not relevant to resolving.
      while (it != ownership_ranges_.cend()) {
        auto it_next = std::next(it);
        if (it_next == ownership_ranges_.cend() || it_next->first >= extent_end ||
            it_next->second.render_target != rt_key) {
          break;
        }
        it = it_next;
      }

      uint32_t range_local_start =
          range_local_offset + std::max(range_global_start, extent_start) - base;
      uint32_t range_local_end =
          range_local_offset + std::min(it->second.end_tiles, extent_end) - base;
      assert_true(range_local_start < range_local_end);

      uint32_t rows_start = range_local_start / pitch;
      uint32_t rows_end = (range_local_end + (pitch - 1)) / pitch;
      uint32_t row_first_start = range_local_start - rows_start * pitch;
      if (row_first_start >= row_length) {
        // The first row starts within the pitch padding.
        if (rows_start + 1 < rows_end) {
          // Multiple rows - start at the second.
          ++rows_start;
          row_first_start = 0;
        } else {
          // Single row - nothing to dump.
          continue;
        }
      }

      auto it_rt = render_targets_.find(rt_key);
      assert_true(it_rt != render_targets_.cend());
      assert_not_null(it_rt->second);
      // Don't include pitch padding in the last row.
      rectangles_out.emplace_back(
          it_rt->second, rows_start, rows_end - rows_start, row_first_start,
          std::min(pitch - (rows_end * pitch - range_local_end), row_length));
    }
  };
  uint32_t resolve_area_end = base + (rows - 1) * pitch + row_length;
  get_rectangles_in_extent(base, std::min(resolve_area_end, xenos::kEdramTileCount), 0);
  if (resolve_area_end > xenos::kEdramTileCount) {
    // The resolve area goes to the next EDRAM addressing period.
    get_rectangles_in_extent(0, std::min(resolve_area_end & (xenos::kEdramTileCount - 1), base),
                             xenos::kEdramTileCount);
  }
}

void RenderTargetCache::GetResolveCopyDispatchesToDump(
    uint32_t base, uint32_t row_length, uint32_t rows, uint32_t pitch,
    std::vector<ResolveCopyDumpRectangle>& rectangles_out,
    std::vector<ResolveCopyDispatch>& dispatches_out) const {
  GetResolveCopyRectanglesToDump(base, row_length, rows, pitch, rectangles_out);
  dispatches_out.clear();
  for (uint32_t rectangle_index = 0; rectangle_index < uint32_t(rectangles_out.size());
       ++rectangle_index) {
    const ResolveCopyDumpRectangle& rectangle = rectangles_out[rectangle_index];
    ResolveCopyDumpRectangle::Dispatch
        rectangle_dispatches[ResolveCopyDumpRectangle::kMaxDispatches];
    uint32_t dispatch_count = rectangle.GetDispatches(pitch, row_length, rectangle_dispatches);
    for (uint32_t i = 0; i < dispatch_count; ++i) {
      dispatches_out.emplace_back(rectangle_index, rectangle_dispatches[i]);
    }
  }
}

bool RenderTargetCache::PrepareHostRenderTargetsResolveClear(
    const draw_util::ResolveInfo& resolve_info, Transfer::Rectangle& clear_rectangle_out,
    RenderTarget*& depth_render_target_out, std::vector<Transfer>& depth_transfers_out,
    RenderTarget*& color_render_target_out, std::vector<Transfer>& color_transfers_out) {
  assert_true(GetPath() == Path::kHostRenderTargets);

  uint32_t pitch_tiles_at_32bpp;
  uint32_t base_offset_tiles_at_32bpp;
  xenos::MsaaSamples msaa_samples;
  if (resolve_info.IsClearingDepth()) {
    pitch_tiles_at_32bpp = resolve_info.depth_edram_info.pitch_tiles;
    base_offset_tiles_at_32bpp =
        resolve_info.depth_edram_info.base_tiles - resolve_info.depth_original_base;
    msaa_samples = resolve_info.depth_edram_info.msaa_samples;
  } else if (resolve_info.IsClearingColor()) {
    pitch_tiles_at_32bpp = resolve_info.color_edram_info.pitch_tiles;
    base_offset_tiles_at_32bpp =
        resolve_info.color_edram_info.base_tiles - resolve_info.color_original_base;
    if (resolve_info.color_edram_info.format_is_64bpp) {
      assert_zero(pitch_tiles_at_32bpp & 1);
      pitch_tiles_at_32bpp >>= 1;
      assert_zero(base_offset_tiles_at_32bpp & 1);
      base_offset_tiles_at_32bpp >>= 1;
    }
    msaa_samples = resolve_info.color_edram_info.msaa_samples;
  } else {
    return false;
  }
  assert_true(msaa_samples <= xenos::MsaaSamples::k4X);
  if (!pitch_tiles_at_32bpp) {
    return false;
  }
  uint32_t msaa_samples_x_log2 = uint32_t(msaa_samples >= xenos::MsaaSamples::k4X);
  uint32_t msaa_samples_y_log2 = uint32_t(msaa_samples >= xenos::MsaaSamples::k2X);
  if (pitch_tiles_at_32bpp > ((xenos::kTexture2DCubeMaxWidthHeight << msaa_samples_x_log2) +
                              (xenos::kEdramTileWidthSamples - 1)) /
                                 xenos::kEdramTileWidthSamples) {
    REXGPU_ERROR(
        "Surface pitch in 80-sample groups {} at {}x MSAA larger than the "
        "maximum texture width {} specified by the guest in a resolve",
        pitch_tiles_at_32bpp, uint32_t(1) << uint32_t(msaa_samples),
        xenos::kTexture2DCubeMaxWidthHeight);
    return false;
  }
  uint32_t pitch_pixels =
      pitch_tiles_at_32bpp * (xenos::kEdramTileWidthSamples >> msaa_samples_x_log2);
  uint32_t pitch_pixels_scaled = pitch_pixels * draw_resolution_scale_x();
  uint32_t max_render_target_width = GetMaxRenderTargetWidth();
  if (pitch_pixels_scaled > max_render_target_width) {
    // TODO(Triang3l): If really needed for some game on some device, clamp the
    // pitch the same way as explained in the comment in Update.
    REXGPU_ERROR(
        "Surface pitch aligned to EDRAM tiles and resolution-scaled {} larger "
        "than the maximum host render target width {} in a resolve",
        pitch_pixels_scaled, max_render_target_width);
    return false;
  }

  // native_one_pass_msaa: the private scene surfaces share no tile with the
  // EDRAM's targets (16.17).
  const bool private_depth_clear =
      resolve_info.IsClearingDepth() &&
      IsPrivateDepth(resolve_info.depth_original_base, pitch_tiles_at_32bpp, msaa_samples);
  const bool private_color_clear =
      resolve_info.IsClearingColor() && !resolve_info.color_edram_info.format_is_64bpp &&
      IsPrivateColor(resolve_info.color_original_base, pitch_tiles_at_32bpp, msaa_samples,
                     uint32_t(GetColorResourceFormat(
                         xenos::ColorRenderTargetFormat(resolve_info.color_edram_info.format))));
  uint32_t render_target_height_pixels = GetSurfaceHeight(
      pitch_tiles_at_32bpp, msaa_samples, private_depth_clear || private_color_clear);
  uint32_t base_offset_rows_at_32bpp = base_offset_tiles_at_32bpp / pitch_tiles_at_32bpp;
  Transfer::Rectangle clear_rectangle;
  clear_rectangle.x_pixels =
      std::min((base_offset_tiles_at_32bpp - base_offset_rows_at_32bpp * pitch_tiles_at_32bpp) *
                       (xenos::kEdramTileWidthSamples >> msaa_samples_x_log2) +
                   (uint32_t(resolve_info.coordinate_info.edram_offset_x_div_8) << 3),
               pitch_pixels);
  clear_rectangle.y_pixels =
      std::min(base_offset_rows_at_32bpp * (xenos::kEdramTileHeightSamples >> msaa_samples_y_log2) +
                   (uint32_t(resolve_info.coordinate_info.edram_offset_y_div_8) << 3),
               render_target_height_pixels);
  clear_rectangle.width_pixels = std::min(uint32_t(resolve_info.coordinate_info.width_div_8) << 3,
                                          pitch_pixels - clear_rectangle.x_pixels);
  clear_rectangle.height_pixels = std::min(uint32_t(resolve_info.height_div_8) << 3,
                                           render_target_height_pixels - clear_rectangle.y_pixels);
  if (!clear_rectangle.width_pixels || !clear_rectangle.height_pixels) {
    // Outside the pitch / height (or initially specified as 0).
    return false;
  }

  // Change ownership of the tiles containing the area to be cleared, so the
  // up-to-date host render target for the cleared range will be the cleared
  // one.
  uint32_t clear_start_tiles_at_32bpp =
      ((clear_rectangle.y_pixels << msaa_samples_y_log2) / xenos::kEdramTileHeightSamples) *
          pitch_tiles_at_32bpp +
      (clear_rectangle.x_pixels << msaa_samples_x_log2) / xenos::kEdramTileWidthSamples;
  uint32_t clear_length_tiles_at_32bpp =
      (((clear_rectangle.y_pixels + clear_rectangle.height_pixels - 1) << msaa_samples_y_log2) /
       xenos::kEdramTileHeightSamples) *
          pitch_tiles_at_32bpp +
      ((clear_rectangle.x_pixels + clear_rectangle.width_pixels - 1) << msaa_samples_x_log2) /
          xenos::kEdramTileWidthSamples +
      1 - clear_start_tiles_at_32bpp;
  // Up to the range from the base in the current 11 tile index bits to the base
  // in the next 11 tile index bits after wrapping can be cleared.
  uint32_t depth_clear_start_tiles_base_relative = 0;
  uint32_t depth_clear_length_tiles = 0;
  if (resolve_info.IsClearingDepth()) {
    depth_clear_start_tiles_base_relative =
        std::min(clear_start_tiles_at_32bpp, xenos::kEdramTileCount);
    depth_clear_length_tiles =
        std::min(clear_start_tiles_at_32bpp + clear_length_tiles_at_32bpp, xenos::kEdramTileCount) -
        depth_clear_start_tiles_base_relative;
  }
  uint32_t color_clear_start_tiles_base_relative = 0;
  uint32_t color_clear_length_tiles = 0;
  if (resolve_info.IsClearingColor()) {
    color_clear_start_tiles_base_relative =
        std::min(clear_start_tiles_at_32bpp << resolve_info.color_edram_info.format_is_64bpp,
                 xenos::kEdramTileCount);
    color_clear_length_tiles = std::min((clear_start_tiles_at_32bpp + clear_length_tiles_at_32bpp)
                                            << resolve_info.color_edram_info.format_is_64bpp,
                                        xenos::kEdramTileCount) -
                               color_clear_start_tiles_base_relative;
  }
  if (depth_clear_length_tiles && color_clear_length_tiles && !private_depth_clear &&
      !private_color_clear) {
    // Prevent overlap - clear the depth only until the color, the color only
    // until the depth, in the current or the next 11 bits of the tile index.
    uint32_t depth_clear_start_tiles_wrapped =
        (resolve_info.depth_original_base + depth_clear_start_tiles_base_relative) &
        (xenos::kEdramTileCount - 1);
    uint32_t color_clear_start_tiles_wrapped =
        (resolve_info.color_original_base + color_clear_start_tiles_base_relative) &
        (xenos::kEdramTileCount - 1);
    depth_clear_length_tiles =
        std::min(depth_clear_length_tiles,
                 ((color_clear_start_tiles_wrapped < depth_clear_start_tiles_wrapped)
                      ? xenos::kEdramTileCount
                      : 0) +
                     color_clear_start_tiles_wrapped - depth_clear_start_tiles_wrapped);
    color_clear_length_tiles =
        std::min(color_clear_length_tiles,
                 ((depth_clear_start_tiles_wrapped < color_clear_start_tiles_wrapped)
                      ? xenos::kEdramTileCount
                      : 0) +
                     depth_clear_start_tiles_wrapped - color_clear_start_tiles_wrapped);
  }

  // The tile rows the clear reaches (native_rt_surfaces bit 8).
  const uint32_t clear_tile_rows =
      (((clear_rectangle.y_pixels + clear_rectangle.height_pixels) << msaa_samples_y_log2) +
       (xenos::kEdramTileHeightSamples - 1)) /
      xenos::kEdramTileHeightSamples;
  RenderTargetKey depth_render_target_key;
  RenderTarget* depth_render_target = nullptr;
  if (depth_clear_length_tiles) {
    depth_render_target_key.base_tiles = resolve_info.depth_original_base;
    depth_render_target_key.pitch_tiles_at_32bpp = pitch_tiles_at_32bpp;
    depth_render_target_key.msaa_samples = msaa_samples;
    depth_render_target_key.is_depth = 1;
    depth_render_target_key.resource_format = resolve_info.depth_edram_info.format;
    depth_render_target = GetOrCreateRenderTarget(depth_render_target_key, clear_tile_rows);
    if (!depth_render_target) {
      // Failed to create the depth render target, don't clear it.
      depth_render_target_key = RenderTargetKey();
      depth_clear_length_tiles = 0;
    }
  }
  RenderTargetKey color_render_target_key;
  RenderTarget* color_render_target = nullptr;
  if (color_clear_length_tiles) {
    color_render_target_key.base_tiles = resolve_info.color_original_base;
    color_render_target_key.pitch_tiles_at_32bpp = pitch_tiles_at_32bpp;
    color_render_target_key.msaa_samples = msaa_samples;
    color_render_target_key.is_depth = 0;
    color_render_target_key.resource_format = uint32_t(GetColorResourceFormat(
        xenos::ColorRenderTargetFormat(resolve_info.color_edram_info.format)));
    color_render_target = GetOrCreateRenderTarget(color_render_target_key, clear_tile_rows);
    if (!color_render_target) {
      // Failed to create the color render target, don't clear it.
      color_render_target_key = RenderTargetKey();
      color_clear_length_tiles = 0;
    }
  }
  if (!depth_clear_length_tiles && !color_clear_length_tiles) {
    // Complete overlap, or failed to create all the render targets.
    return false;
  }

  clear_rectangle_out = clear_rectangle;
  // native_rt_surfaces bit 4: the deferred tiles the clears claim leave their
  // holder, which their transfers then copy from (d3d-hle.md 16.12).
  if (depth_render_target && !private_depth_clear) {
    DetachLazyHolders(depth_render_target_key, depth_clear_start_tiles_base_relative,
                      depth_clear_length_tiles);
  }
  if (color_render_target && !private_color_clear) {
    DetachLazyHolders(color_render_target_key, color_clear_start_tiles_base_relative,
                      color_clear_length_tiles);
  }
  depth_render_target_out = depth_render_target;
  depth_transfers_out.clear();
  if (depth_render_target && private_depth_clear) {
    perf_trace::CountOnePass(perf_trace::one_pass_clears);
  } else if (depth_render_target) {
    ChangeOwnership(depth_render_target_key, depth_clear_start_tiles_base_relative,
                    depth_clear_length_tiles, &depth_transfers_out, &clear_rectangle);
    SubstituteDetachedHolders(depth_render_target, depth_transfers_out);
  }
  color_render_target_out = color_render_target;
  color_transfers_out.clear();
  if (color_render_target && private_color_clear) {
    perf_trace::CountOnePass(perf_trace::one_pass_clears);
  } else if (color_render_target) {
    ChangeOwnership(color_render_target_key, color_clear_start_tiles_base_relative,
                    color_clear_length_tiles, &color_transfers_out, &clear_rectangle);
    SubstituteDetachedHolders(color_render_target, color_transfers_out);
  }
  if (!lazy_detached_tiles_.empty()) {
    EndDetachedHolders();
  }
  return true;
}

void RenderTargetCache::PixelShaderInterlockFullEdramBarrierPlaced() {
  BumpUpdateEpoch();
  assert_true(GetPath() == Path::kPixelShaderInterlock);
  // Clear ownership - any overlap of data written before the barrier is safe.
  OwnershipRange empty_range(xenos::kEdramTileCount, RenderTargetKey(), RenderTargetKey(),
                             RenderTargetKey());
  if (ownership_ranges_.size() == 1) {
    // Do not reallocate map elements if not needed (either nothing drawn since
    // the last barrier, or all of the EDRAM is owned by one render target).
    // The ownership map contains no gaps - the first element should always be
    // at 0.
    assert_true(!ownership_ranges_.begin()->first);
    OwnershipRange& all_edram_range = ownership_ranges_.begin()->second;
    assert_true(all_edram_range.end_tiles == xenos::kEdramTileCount);
    all_edram_range = empty_range;
    return;
  }
  ownership_ranges_.clear();
  ownership_ranges_.emplace(0, empty_range);
}

bool RenderTargetCache::IsPrivateSceneWide() const {
  return GetPath() == Path::kHostRenderTargets && REXCVAR_GET(native_one_pass_msaa) &&
         (REXCVAR_GET(native_rt_surfaces) & 16);
}

bool RenderTargetCache::IsPrivateDepth(uint32_t base_tiles, uint32_t pitch_tiles_at_32bpp,
                                       xenos::MsaaSamples msaa_samples) const {
  if (GetPath() != Path::kHostRenderTargets) {
    return false;
  }
  // The final picture's depth (base 720, 1280 wide, one sample).
  if (IsPrivateSceneWide() && base_tiles == 720 && pitch_tiles_at_32bpp == 16 &&
      msaa_samples == xenos::MsaaSamples::k1X) {
    return true;
  }
  switch (REXCVAR_GET(native_one_pass_msaa)) {
    case 2:
      return base_tiles == 1024 && pitch_tiles_at_32bpp == 16 &&
             msaa_samples == xenos::MsaaSamples::k2X;
    case 4:
      // 2880, after the colour's 2,880 tiles, without bit 11.
      return base_tiles == 832 && pitch_tiles_at_32bpp == 32 &&
             msaa_samples == xenos::MsaaSamples::k4X;
    default:
      return false;
  }
}

bool RenderTargetCache::IsPrivateColor(uint32_t base_tiles, uint32_t pitch_tiles_at_32bpp,
                                       xenos::MsaaSamples msaa_samples,
                                       uint32_t resource_format) const {
  const uint32_t float_format =
      uint32_t(GetColorResourceFormat(xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT));
  if (REXCVAR_GET(native_one_pass_msaa) == 4 && GetPath() == Path::kHostRenderTargets &&
      base_tiles == 0 && pitch_tiles_at_32bpp == 32 &&
      msaa_samples == xenos::MsaaSamples::k4X && resource_format == float_format) {
    return true;
  }
  // native_rt_surfaces bit 16: the 2x scene colour, and the post passes'
  // one-sample colour at base 0 in the float format and in the final
  // picture's 2_10_10_10.
  if (!IsPrivateSceneWide() || base_tiles != 0 || pitch_tiles_at_32bpp != 16) {
    return false;
  }
  if (msaa_samples == xenos::MsaaSamples::k2X) {
    return REXCVAR_GET(native_one_pass_msaa) == 2 && resource_format == float_format;
  }
  return msaa_samples == xenos::MsaaSamples::k1X &&
         (resource_format == float_format ||
          resource_format ==
              uint32_t(GetColorResourceFormat(xenos::ColorRenderTargetFormat::k_2_10_10_10)));
}

uint32_t RenderTargetCache::GetSurfaceHeight(uint32_t pitch_tiles_at_32bpp,
                                             xenos::MsaaSamples msaa_samples,
                                             bool is_private) const {
  uint32_t height = GetRenderTargetHeight(pitch_tiles_at_32bpp, msaa_samples);
  if (is_private && msaa_samples >= xenos::MsaaSamples::k4X) {
    height = std::min(height * 2, xenos::kTexture2DCubeMaxWidthHeight);
  }
  return height;
}

bool RenderTargetCache::IsSizingEnabled() const {
  return GetPath() == Path::kHostRenderTargets && (REXCVAR_GET(native_rt_surfaces) & 8);
}

uint32_t RenderTargetCache::GetFullTileRows(RenderTargetKey key) const {
  const uint32_t tile_height = xenos::kEdramTileHeightSamples >>
                               uint32_t(key.msaa_samples >= xenos::MsaaSamples::k2X);
  return GetSurfaceHeight(key.pitch_tiles_at_32bpp, key.msaa_samples, IsPrivateKey(key)) /
         tile_height;
}

RenderTargetCache::RenderTarget* RenderTargetCache::CreateSizedRenderTarget(RenderTargetKey key,
                                                                          uint32_t tile_rows) {
  RenderTarget* render_target = CreateRenderTarget(key, tile_rows);
  if (render_target) {
    render_target->tile_rows_ = tile_rows;
    render_target_memory_ += render_target->memory_bytes_;
    render_target_memory_peak_ = std::max(render_target_memory_peak_, render_target_memory_);
    perf_trace::SetRenderTargetMemory(render_target_memory_, render_target_memory_peak_,
                                      uint32_t(render_targets_.size()) + 1);
  }
  return render_target;
}

void RenderTargetCache::ReplaceRenderTarget(RenderTarget* old_rt, RenderTarget* new_rt) {
  BumpUpdateEpoch();
  render_targets_[old_rt->key()] = new_rt;
  new_rt->lazy_owned_held_tiles_ = old_rt->lazy_owned_held_tiles_;
  new_rt->last_bound_ = old_rt->last_bound_;
  for (RenderTarget*& holder : lazy_holders_) {
    if (holder == old_rt) {
      holder = new_rt;
    }
  }
  for (RenderTarget*& render_target : last_update_used_render_targets_) {
    if (render_target == old_rt) {
      render_target = new_rt;
    }
  }
  for (RenderTarget*& render_target : last_update_accumulated_render_targets_) {
    if (render_target == old_rt) {
      render_target = new_rt;
    }
  }
  for (std::vector<Transfer>& transfers : last_update_transfers_) {
    for (Transfer& transfer : transfers) {
      if (transfer.source == old_rt) {
        transfer.source = new_rt;
      }
      if (transfer.host_depth_source == old_rt) {
        transfer.host_depth_source = new_rt;
      }
    }
  }
  render_target_memory_ -= old_rt->memory_bytes_;
  perf_trace::SetRenderTargetMemory(render_target_memory_, render_target_memory_peak_,
                                    uint32_t(render_targets_.size()));
  RetireRenderTarget(old_rt);
}

bool RenderTargetCache::IsOverMemoryCap() const {
  const int32_t cap_mb = REXCVAR_GET(native_rt_surfaces_memory_cap_mb);
  return cap_mb > 0 && IsSizingEnabled() && render_target_memory_ > (uint64_t(cap_mb) << 20);
}

void RenderTargetCache::EvictRenderTargets() {
  BumpUpdateEpoch();
  if (!IsOverMemoryCap()) {
    return;
  }
  // Targets that own a tile, or keep the host depth of one, stay.
  std::unordered_set<RenderTargetKey, RenderTargetKey::Hasher> used;
  for (const auto& ownership_range_pair : ownership_ranges_) {
    const OwnershipRange& ownership_range = ownership_range_pair.second;
    used.emplace(ownership_range.render_target);
    used.emplace(ownership_range.host_depth_render_target_unorm24);
    used.emplace(ownership_range.host_depth_render_target_float24);
  }
  for (const RenderTarget* render_target : last_update_accumulated_render_targets_) {
    if (render_target) {
      used.emplace(render_target->key());
    }
  }
  std::vector<RenderTarget*> candidates;
  for (const auto& render_target_pair : render_targets_) {
    if (render_target_pair.second && used.find(render_target_pair.first) == used.end() &&
        !IsPrivateKey(render_target_pair.first)) {
      candidates.push_back(render_target_pair.second);
    }
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const RenderTarget* a, const RenderTarget* b) {
              return a->last_bound_ < b->last_bound_;
            });
  uint32_t evicted = 0;
  for (RenderTarget* render_target : candidates) {
    if (!IsOverMemoryCap()) {
      break;
    }
    FlushLazyHolder(render_target);
    render_targets_.erase(render_target->key());
    render_target_memory_ -= render_target->memory_bytes_;
    RetireRenderTarget(render_target);
    ++evicted;
  }
  perf_trace::SetRenderTargetMemory(render_target_memory_, render_target_memory_peak_,
                                    uint32_t(render_targets_.size()));
  perf_trace::CountRenderTargetEvictions(evicted);
}

RenderTargetCache::RenderTarget* RenderTargetCache::GetOrCreateRenderTarget(RenderTargetKey key,
                                                                          uint32_t tile_rows) {
  assert_true(GetPath() == Path::kHostRenderTargets);
  const uint32_t full_rows = GetFullTileRows(key);
  // native_rt_surfaces bit 8: the rows asked for, rounded up to 8, at most the
  // EDRAM's.
  const bool sized = IsSizingEnabled() && tile_rows;
  const uint32_t needed_rows = std::min(tile_rows, full_rows);
  const uint32_t rows =
      sized ? std::min(full_rows, (needed_rows + 7) & ~uint32_t(7)) : full_rows;
  const uint32_t tile_height = xenos::kEdramTileHeightSamples >>
                               uint32_t(key.msaa_samples >= xenos::MsaaSamples::k2X);
  // The growth check: every target starts at 8 rows and grows only to what
  // is asked for.
  const bool grow_stress = sized && REXCVAR_GET(native_rt_surfaces_grow_stress);
  auto it_rt = render_targets_.find(key);
  RenderTarget* render_target;
  if (it_rt == render_targets_.end() && grow_stress) {
    render_target = CreateSizedRenderTarget(key, std::min(rows, uint32_t(8)));
    render_targets_.emplace(key, render_target);
    BumpUpdateEpoch();
    it_rt = render_targets_.find(key);
  }
  if (it_rt != render_targets_.end()) {
    render_target = it_rt->second;
    if (render_target && sized && render_target->tile_rows_ < needed_rows) {
      // Grown by at least a half, so that a surface that grows row by row is
      // not copied each time.
      const uint32_t grown_rows =
          grow_stress ? rows
                      : std::min(full_rows, std::max(rows, (render_target->tile_rows_ * 3 / 2 + 7) &
                                                               ~uint32_t(7)));
      if (RenderTarget* larger = ResizeRenderTarget(render_target, grown_rows)) {
        REXGPU_DEBUG("Grew the {} from {} to {} lines", key.GetDebugName(),
                     render_target->tile_rows_ * tile_height, grown_rows * tile_height);
        ReplaceRenderTarget(render_target, larger);
        render_target = larger;
        perf_trace::CountRenderTargetResize();
      }
    }
  } else {
    render_target = CreateSizedRenderTarget(key, rows);
    uint32_t width = key.GetWidth();
    uint32_t height = rows * tile_height;
    if (render_target) {
      REXGPU_DEBUG(
          "Created a {}x{} {}xMSAA {} render target with guest format {} at "
          "EDRAM base {}",
          width, height, uint32_t(1) << uint32_t(key.msaa_samples),
          key.is_depth ? "depth" : "color", static_cast<uint32_t>(key.resource_format),
          static_cast<uint32_t>(key.base_tiles));
    } else {
      REXGPU_ERROR(
          "Failed to create a {}x{} {}xMSAA {} render target with guest format "
          "{} at EDRAM base {}",
          width, height, uint32_t(1) << uint32_t(key.msaa_samples),
          key.is_depth ? "depth" : "color", static_cast<uint32_t>(key.resource_format),
          static_cast<uint32_t>(key.base_tiles));
    }
    // Insert even if failed to create, not to try to create again.
    render_targets_.emplace(key, render_target);
    BumpUpdateEpoch();
  }
  if (render_target) {
    render_target->last_bound_ = render_target_update_clock_;
  }
  return render_target;
}

bool RenderTargetCache::WouldOwnershipChangeRequireTransfers(RenderTargetKey dest,
                                                             uint32_t start_tiles_base_relative,
                                                             uint32_t length_tiles) const {
  // xenos::kEdramTileCount with length 0 is fine if both the start and the end
  // are clamped to xenos::kEdramTileCount.
  assert_true(start_tiles_base_relative <= (xenos::kEdramTileCount - uint32_t(length_tiles != 0)));
  assert_true(length_tiles <= xenos::kEdramTileCount);
  if (length_tiles == 0) {
    return false;
  }
  bool host_depth_encoding_different = dest.is_depth && GetPath() == Path::kHostRenderTargets &&
                                       IsHostDepthEncodingDifferent(dest.GetDepthFormat());
  auto would_require_transfers_in_extent = [&](uint32_t extent_start, uint32_t extent_end) -> bool {
    // The map contains consecutive ranges, merged if the adjacent ones are the
    // same. Find the range starting at >= the start. A portion of the range
    // preceding it may be intersecting the render target's range (or even fully
    // contain it).
    auto it = ownership_ranges_.lower_bound(extent_start);
    if (it != ownership_ranges_.begin()) {
      auto it_pre = std::prev(it);
      if (it_pre->second.end_tiles > extent_start) {
        it = it_pre;
      }
    }
    for (; it != ownership_ranges_.end(); ++it) {
      if (it->first >= extent_end) {
        // Outside the touched extent already.
        break;
      }
      if (it->second.IsOwnedBy(dest, host_depth_encoding_different)) {
        // Already owned by the needed render target - no need to transfer
        // anything.
        continue;
      }
      RenderTargetKey transfer_source = it->second.render_target;
      // Only perform the transfer when actually changing the latest owner, not
      // just the latest host depth owner - the transfer source is expected to
      // be different than the destination.
      if (!transfer_source.IsEmpty() && transfer_source != dest) {
        return true;
      }
    }
    return false;
  };
  // start_tiles_base_relative may already be in the next 11 bits - wrap the
  // start tile index to use the same code as if that was not the case.
  uint32_t start_tiles =
      (dest.base_tiles + start_tiles_base_relative) & (xenos::kEdramTileCount - 1);
  uint32_t end_tiles = start_tiles + length_tiles;
  if (would_require_transfers_in_extent(start_tiles, std::min(end_tiles, xenos::kEdramTileCount))) {
    return true;
  }
  if (end_tiles > xenos::kEdramTileCount) {
    // The check extent goes to the next EDRAM addressing period.
    if (would_require_transfers_in_extent(
            0, std::min(end_tiles & (xenos::kEdramTileCount - 1), start_tiles))) {
      return true;
    }
  }
  return false;
}

void RenderTargetCache::ChangeOwnership(RenderTargetKey dest, uint32_t start_tiles_base_relative,
                                        uint32_t length_tiles,
                                        std::vector<Transfer>* transfers_append_out,
                                        const Transfer::Rectangle* resolve_clear_cutout) {
  // xenos::kEdramTileCount with length 0 is fine if both the start and the end
  // are clamped to xenos::kEdramTileCount.
  assert_true(start_tiles_base_relative <= (xenos::kEdramTileCount - uint32_t(length_tiles != 0)));
  assert_true(length_tiles <= xenos::kEdramTileCount);
  if (length_tiles == 0) {
    return;
  }
  if (IsPrivateKey(dest)) {
    // The private scene surfaces are outside the tile map (16.17).
    perf_trace::CountOnePass(perf_trace::one_pass_violations);
    return;
  }
  uint32_t dest_pitch_tiles = dest.GetPitchTiles();
  bool dest_is_64bpp = dest.Is64bpp();
  bool host_depth_encoding_different = dest.is_depth && GetPath() == Path::kHostRenderTargets &&
                                       IsHostDepthEncodingDifferent(dest.GetDepthFormat());
  auto change_ownership_in_extent = [&](uint32_t extent_start, uint32_t extent_end) {
    // The map contains consecutive ranges, merged if the adjacent ones are the
    // same. Find the range starting at >= the start. A portion of the range
    // preceding it may be intersecting the render target's range (or even fully
    // contain it) - split it into the untouched head and the claimed tail if
    // needed.
    auto it = ownership_ranges_.lower_bound(extent_start);
    if (it != ownership_ranges_.begin()) {
      auto it_pre = std::prev(it);
      if (it_pre->second.end_tiles > extent_start &&
          !it_pre->second.IsOwnedBy(dest, host_depth_encoding_different)) {
        // Different render target overlapping the range - split the head.
        BumpUpdateEpoch();
        ownership_ranges_.emplace(extent_start, it_pre->second);
        it_pre->second.end_tiles = extent_start;
        // Let the next loop do the transfer and needed merging and splitting
        // starting from the added tail.
        it = std::next(it_pre);
      }
    }
    while (it != ownership_ranges_.end()) {
      if (it->first >= extent_end) {
        // Outside the touched extent already.
        break;
      }
      if (it->second.IsOwnedBy(dest, host_depth_encoding_different)) {
        // Already owned by the needed render target - no need to transfer
        // anything.
        ++it;
        continue;
      }
      // Take over the current range. Handle the tail - may be outside the range
      // (split in this case) or within it.
      BumpUpdateEpoch();
      if (it->second.end_tiles > extent_end) {
        // Split the tail.
        ownership_ranges_.emplace(extent_end, it->second);
        it->second.end_tiles = extent_end;
      }
      if (transfers_append_out) {
        RenderTargetKey transfer_source = it->second.render_target;
        // Only perform the copying when actually changing the latest owner, not
        // just the latest host depth owner - the transfer source is expected to
        // be different than the destination.
        if (!transfer_source.IsEmpty() && transfer_source != dest) {
          uint32_t transfer_end_tiles = std::min(it->second.end_tiles, extent_end);
          if (!resolve_clear_cutout ||
              Transfer::GetRangeRectangles(it->first, transfer_end_tiles, dest.base_tiles,
                                           dest_pitch_tiles, dest.msaa_samples, dest_is_64bpp,
                                           nullptr, resolve_clear_cutout)) {
            RenderTargetKey transfer_host_depth_source =
                host_depth_encoding_different
                    ? it->second.GetHostDepthRenderTarget(dest.GetDepthFormat())
                    : RenderTargetKey();
            if (transfer_host_depth_source == transfer_source) {
              // Same render target, don't provide a separate host depth source.
              transfer_host_depth_source = RenderTargetKey();
            }
            if (!transfers_append_out->empty() &&
                transfers_append_out->back().end_tiles == it->first &&
                transfers_append_out->back().source->key() == transfer_source &&
                ((transfers_append_out->back().host_depth_source == nullptr) ==
                 transfer_host_depth_source.IsEmpty()) &&
                (transfer_host_depth_source.IsEmpty() ||
                 transfers_append_out->back().host_depth_source->key() ==
                     transfer_host_depth_source)) {
              // Extend the last transfer if, for example, transferring color,
              // but host depth is different.
              transfers_append_out->back().end_tiles = transfer_end_tiles;
            } else {
              auto transfer_source_rt_it = render_targets_.find(transfer_source);
              if (transfer_source_rt_it != render_targets_.end()) {
                assert_not_null(transfer_source_rt_it->second);
                auto transfer_host_depth_source_rt_it =
                    !transfer_host_depth_source.IsEmpty()
                        ? render_targets_.find(transfer_host_depth_source)
                        : render_targets_.end();
                if (transfer_host_depth_source.IsEmpty() ||
                    transfer_host_depth_source_rt_it != render_targets_.end()) {
                  assert_false(transfer_host_depth_source_rt_it != render_targets_.end() &&
                               !transfer_host_depth_source_rt_it->second);
                  transfers_append_out->emplace_back(
                      it->first, transfer_end_tiles, transfer_source_rt_it->second,
                      transfer_host_depth_source_rt_it != render_targets_.end()
                          ? transfer_host_depth_source_rt_it->second
                          : nullptr);
                }
              }
            }
          }
        }
      }
      // Claim the current range.
      it->second.render_target = dest;
      if (host_depth_encoding_different) {
        it->second.GetHostDepthRenderTarget(dest.GetDepthFormat()) = dest;
      }
      // Check if can merge with the next range after claiming.
      std::map<uint32_t, OwnershipRange>::iterator it_next;
      if (it != ownership_ranges_.end()) {
        it_next = std::next(it);
        if (it_next != ownership_ranges_.end() && it_next->second.AreOwnersSame(it->second)) {
          // Merge with the next range.
          it->second.end_tiles = it_next->second.end_tiles;
          auto it_after = std::next(it_next);
          ownership_ranges_.erase(it_next);
          it_next = it_after;
        }
      } else {
        it_next = ownership_ranges_.end();
      }
      // Check if can merge with the previous range after claiming and merging
      // with the next (thus obtaining the correct end pointer).
      if (it != ownership_ranges_.begin()) {
        auto it_prev = std::prev(it);
        if (it_prev->second.AreOwnersSame(it->second)) {
          it_prev->second.end_tiles = it->second.end_tiles;
          ownership_ranges_.erase(it);
        }
      }
      it = it_next;
    }
  };
  // start_tiles_base_relative may already be in the next 11 bits - wrap the
  // start tile index to use the same code as if that was not the case.
  uint32_t start_tiles =
      (dest.base_tiles + start_tiles_base_relative) & (xenos::kEdramTileCount - 1);
  uint32_t end_tiles = start_tiles + length_tiles;
  change_ownership_in_extent(start_tiles, std::min(end_tiles, xenos::kEdramTileCount));
  if (end_tiles > xenos::kEdramTileCount) {
    // The ownership change extent goes to the next EDRAM addressing period.
    change_ownership_in_extent(0, std::min(end_tiles & (xenos::kEdramTileCount - 1), start_tiles));
  }
}

}  // namespace rex::graphics
