// Deferred constant buffers, stage N4 step 1 of docs/analysis/d3d-hle.md
// (section 17): the float, bool and loop, and fetch constant buffers of a draw
// are filled while the deferred command list is replayed (on the submit thread
// with d3d12_submit_thread), from the constant registers captured at the draw.
// See rex/graphics/d3d12/deferred_constants.h.

#include <algorithm>
#include <atomic>
#include <cstring>

#include <rex/cvar.h>
#include <rex/graphics/d3d12/command_processor.h>
#include <rex/graphics/d3d12/deferred_constants.h>
#include <rex/logging.h>

#include "../util/perf_trace.h"

REXCVAR_DEFINE_BOOL(d3d12_deferred_constants, false, "GPU/D3D12",
                    "Fill the float, bool and loop, and fetch constant buffers of the draws "
                    "while the deferred command list is replayed (on the submit thread with "
                    "d3d12_submit_thread) instead of at the draw (N4 step 1 of "
                    "docs/analysis/d3d-hle.md, section 17)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(d3d12_deferred_constants_check, false, "GPU/D3D12",
                    "With d3d12_deferred_constants: also write the inline path's bytes of every "
                    "filled buffer into the command, and compare them with the replay's")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_INT32(d3d12_deferred_constants_check_perturb, 0, "GPU/D3D12",
                     "Test of d3d12_deferred_constants_check: change the comparison of every "
                     "Nth command so that it must be reported (0: off)")
    .range(0, 1000000)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rex::graphics::d3d12 {

namespace deferred_constants {

namespace {

constexpr uint32_t kBoolLoopBytes = kBoolLoopSlotCount * 16;
constexpr uint32_t kFetchBytes = kFetchSlotCount * 16;
constexpr const char* kFillNames[kFillCount] = {"float vertex", "float pixel", "bool and loop",
                                                "fetch"};

uint32_t Popcount(const uint64_t* words, uint32_t count) {
  uint32_t result = 0;
  for (uint32_t i = 0; i < count; ++i) {
    result += uint32_t(__builtin_popcountll(words[i]));
  }
  return result;
}

// Writes the buffer of one fill from 16-byte slots (the register file or the
// replay's copy, both starting at kFirstRegister).
void FillBuffer(Fill fill, const uint32_t* slots, const uint64_t (&float_maps)[2][4],
                uint8_t* dst) {
  switch (fill) {
    case kFloatVertex:
      PackFloatConstants(slots, float_maps[0], dst);
      break;
    case kFloatPixel:
      PackFloatConstants(slots + 256 * 4, float_maps[1], dst);
      break;
    case kBoolLoop:
      std::memcpy(dst, slots + kBoolLoopFirstSlot * 4, kBoolLoopBytes);
      break;
    default:
      std::memcpy(dst, slots + kFetchFirstSlot * 4, kFetchBytes);
      break;
  }
}

}  // namespace

void Replay(const uint8_t* arguments, uint32_t* shadow) {
  const uint64_t start = __rdtsc();
  const Header& header = *reinterpret_cast<const Header*>(arguments);
  const uint8_t* values = arguments + sizeof(Header);
  for (uint32_t word = 0; word < kMaskWords; ++word) {
    uint64_t bits = header.captured[word];
    while (bits) {
      const uint32_t slot = (word << 6) + uint32_t(__builtin_ctzll(bits));
      bits &= bits - 1;
      std::memcpy(shadow + slot * 4, values, 16);
      values += 16;
    }
  }
  uint32_t checks = 0, mismatches = 0, perturbed = 0;
  if (!(header.check & kCheck)) {
    for (uint32_t fill = 0; fill < kFillCount; ++fill) {
      if (header.destinations[fill]) {
        FillBuffer(Fill(fill), shadow, header.float_maps, header.destinations[fill]);
      }
    }
  } else {
    // Fill a local copy first: the buffers are in write-combined memory, too
    // slow to read back.
    const uint8_t* inline_bytes = values;
    bool perturb = (header.check & kCheckPerturb) != 0;
    for (uint32_t fill = 0; fill < kFillCount; ++fill) {
      if (!header.destinations[fill]) {
        continue;
      }
      alignas(16) uint8_t local[256 * 16];
      const uint32_t size = header.sizes[fill];
      FillBuffer(Fill(fill), shadow, header.float_maps, local);
      std::memcpy(header.destinations[fill], local, size);
      ++checks;
      bool differs;
      if (perturb && size) {
        perturb = false;
        ++perturbed;
        differs = local[0] != uint8_t(inline_bytes[0] ^ 1) ||
                  std::memcmp(local + 1, inline_bytes + 1, size - 1) != 0;
      } else {
        differs = std::memcmp(local, inline_bytes, size) != 0;
        if (differs) {
          static std::atomic<uint32_t> logged{0};
          if (logged.fetch_add(1, std::memory_order_relaxed) < 16) {
            uint32_t offset = 0;
            while (offset < size && local[offset] == inline_bytes[offset]) ++offset;
            REXGPU_ERROR("Deferred constants: the {} buffer differs from the inline bytes at {}",
                         kFillNames[fill], offset);
          }
        }
      }
      mismatches += differs;
      inline_bytes += size;
    }
  }
  perf_trace::AddDeferredConstantReplay(__rdtsc() - start, checks, mismatches, perturbed);
}

}  // namespace deferred_constants

void D3D12CommandProcessor::InitializeDeferredConstants() {
  deferred_constants_ = REXCVAR_GET(d3d12_deferred_constants);
  deferred_constants_check_ = deferred_constants_ && REXCVAR_GET(d3d12_deferred_constants_check);
  deferred_constants_perturb_ =
      deferred_constants_check_ ? uint32_t(REXCVAR_GET(d3d12_deferred_constants_check_perturb)) : 0;
  deferred_constants_commands_ = 0;
  pending_constants_.SetAll();
  if (deferred_constants_) {
    REXGPU_INFO("Deferred constants: on{}{}", deferred_constants_check_ ? ", checked" : "",
                deferred_constants_perturb_ ? ", perturbed" : "");
  }
}

void D3D12CommandProcessor::CountContextRegisterDelta() {
  if (!perf_trace::RegisterDeltaEnabled()) {
    return;
  }
  constexpr uint32_t kFirst = 0x2000, kCount = 0x400;
  const uint32_t* registers = register_file_->values + kFirst;
  if (context_register_snapshot_.empty()) {
    context_register_snapshot_.assign(registers, registers + kCount);
    return;
  }
  uint32_t dwords = 0, granules = 0;
  for (uint32_t granule = 0; granule < kCount; granule += 4) {
    uint32_t changed = 0;
    for (uint32_t i = granule; i < granule + 4; ++i) {
      changed += registers[i] != context_register_snapshot_[i];
    }
    dwords += changed;
    granules += changed != 0;
  }
  std::memcpy(context_register_snapshot_.data(), registers, kCount * sizeof(uint32_t));
  perf_trace::CountRegisterDelta(dwords, granules);
}

bool D3D12CommandProcessor::UpdateConstantBuffersDeferred(
    const D3D12Shader* vertex_shader, const D3D12Shader* pixel_shader,
    uint32_t root_parameter_float_constants_vertex, uint32_t root_parameter_float_constants_pixel,
    uint32_t root_parameter_bool_loop_constants, uint32_t root_parameter_fetch_constants) {
  namespace dc = deferred_constants;
  static constexpr uint64_t kNoMap[4] = {};
  const Shader::ConstantRegisterMap& constant_map_vertex = vertex_shader->constant_register_map();
  const uint64_t* map_vertex = constant_map_vertex.float_bitmap;
  const uint64_t* map_pixel =
      pixel_shader ? pixel_shader->constant_register_map().float_bitmap : kNoMap;

  // The same buffers, sizes and order as the inline path.
  uint8_t* destinations[dc::kFillCount] = {};
  uint32_t sizes[dc::kFillCount] = {};
  uint64_t needed[dc::kMaskWords] = {};
  if (!cbuffer_binding_float_vertex_.up_to_date) {
    destinations[dc::kFloatVertex] = constant_buffer_pool_->Request(
        frame_current_, sizeof(float) * 4 * std::max(constant_map_vertex.float_count, uint32_t(1)),
        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, nullptr, nullptr,
        &cbuffer_binding_float_vertex_.address);
    if (!destinations[dc::kFloatVertex]) {
      return false;
    }
    sizes[dc::kFloatVertex] = 16 * dc::Popcount(map_vertex, 4);
    for (uint32_t i = 0; i < 4; ++i) {
      needed[i] |= map_vertex[i];
    }
    cbuffer_binding_float_vertex_.up_to_date = true;
    current_graphics_root_up_to_date_ &= ~(1u << root_parameter_float_constants_vertex);
  }
  if (!cbuffer_binding_float_pixel_.up_to_date) {
    const uint32_t float_count_pixel =
        pixel_shader ? pixel_shader->constant_register_map().float_count : 0;
    destinations[dc::kFloatPixel] = constant_buffer_pool_->Request(
        frame_current_, sizeof(float) * 4 * std::max(float_count_pixel, uint32_t(1)),
        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, nullptr, nullptr,
        &cbuffer_binding_float_pixel_.address);
    if (!destinations[dc::kFloatPixel]) {
      return false;
    }
    sizes[dc::kFloatPixel] = 16 * dc::Popcount(map_pixel, 4);
    for (uint32_t i = 0; i < 4; ++i) {
      needed[4 + i] |= map_pixel[i];
    }
    cbuffer_binding_float_pixel_.up_to_date = true;
    current_graphics_root_up_to_date_ &= ~(1u << root_parameter_float_constants_pixel);
  }
  if (!cbuffer_binding_bool_loop_.up_to_date) {
    destinations[dc::kBoolLoop] = constant_buffer_pool_->Request(
        frame_current_, dc::kBoolLoopSlotCount * 16, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT,
        nullptr, nullptr, &cbuffer_binding_bool_loop_.address);
    if (!destinations[dc::kBoolLoop]) {
      return false;
    }
    sizes[dc::kBoolLoop] = dc::kBoolLoopSlotCount * 16;
    dc::SetSlots(needed, dc::kBoolLoopFirstSlot,
                 dc::kBoolLoopFirstSlot + dc::kBoolLoopSlotCount - 1);
    cbuffer_binding_bool_loop_.up_to_date = true;
    current_graphics_root_up_to_date_ &= ~(1u << root_parameter_bool_loop_constants);
  }
  if (!cbuffer_binding_fetch_.up_to_date) {
    destinations[dc::kFetch] = constant_buffer_pool_->Request(
        frame_current_, dc::kFetchSlotCount * 16, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT,
        nullptr, nullptr, &cbuffer_binding_fetch_.address);
    if (!destinations[dc::kFetch]) {
      return false;
    }
    sizes[dc::kFetch] = dc::kFetchSlotCount * 16;
    dc::SetSlots(needed, dc::kFetchFirstSlot, dc::kFetchFirstSlot + dc::kFetchSlotCount - 1);
    cbuffer_binding_fetch_.up_to_date = true;
    current_graphics_root_up_to_date_ &= ~(1u << root_parameter_fetch_constants);
  }
  uint32_t fill_count = 0, fill_bytes = 0;
  for (uint32_t fill = 0; fill < dc::kFillCount; ++fill) {
    fill_count += destinations[fill] != nullptr;
    fill_bytes += sizes[fill];
  }
  if (!fill_count) {
    return true;
  }

  // Capture the registers the fills read that were written since they were
  // last captured into this list.
  uint64_t captured[dc::kMaskWords];
  uint32_t captured_count = 0;
  for (uint32_t word = 0; word < dc::kMaskWords; ++word) {
    captured[word] = pending_constants_.slots[word] & needed[word];
    pending_constants_.slots[word] &= ~captured[word];
    captured_count += uint32_t(__builtin_popcountll(captured[word]));
  }
  uint32_t check = 0;
  if (deferred_constants_check_) {
    check = dc::kCheck;
    if (deferred_constants_perturb_ && fill_bytes &&
        ++deferred_constants_commands_ % deferred_constants_perturb_ == 0) {
      check |= dc::kCheckPerturb;
    }
  }
  auto* arguments = static_cast<uint8_t*>(deferred_command_list_.DeferredConstants(
      sizeof(dc::Header) + size_t(captured_count) * 16 + (check ? fill_bytes : 0)));
  dc::Header& header = *reinterpret_cast<dc::Header*>(arguments);
  std::memcpy(header.captured, captured, sizeof(captured));
  std::memcpy(header.destinations, destinations, sizeof(destinations));
  std::memcpy(header.sizes, sizes, sizeof(sizes));
  std::memcpy(header.float_maps[0], map_vertex, sizeof(header.float_maps[0]));
  std::memcpy(header.float_maps[1], map_pixel, sizeof(header.float_maps[1]));
  header.captured_count = captured_count;
  header.check = check;
  const uint32_t* registers = register_file_->values + dc::kFirstRegister;
  uint8_t* values = arguments + sizeof(dc::Header);
  for (uint32_t word = 0; word < dc::kMaskWords; ++word) {
    uint64_t bits = captured[word];
    while (bits) {
      const uint32_t slot = (word << 6) + uint32_t(__builtin_ctzll(bits));
      bits &= bits - 1;
      std::memcpy(values, registers + slot * 4, 16);
      values += 16;
    }
  }
  if (check) {
    // The inline path's bytes, as UpdateBindings writes them.
    if (destinations[dc::kFloatVertex]) {
      values += dc::PackFloatConstants(registers, map_vertex, values);
    }
    if (destinations[dc::kFloatPixel]) {
      values += dc::PackFloatConstants(registers + 256 * 4, map_pixel, values);
    }
    if (destinations[dc::kBoolLoop]) {
      std::memcpy(values, registers + dc::kBoolLoopFirstSlot * 4, dc::kBoolLoopSlotCount * 16);
      values += dc::kBoolLoopSlotCount * 16;
    }
    if (destinations[dc::kFetch]) {
      std::memcpy(values, registers + dc::kFetchFirstSlot * 4, dc::kFetchSlotCount * 16);
    }
  }
  perf_trace::CountConstantFills(fill_count, fill_bytes >> 4, captured_count);
  return true;
}

}  // namespace rex::graphics::d3d12
