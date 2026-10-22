#pragma once

// Deferred constant buffers, stage N4 step 1 of docs/analysis/d3d-hle.md
// (section 17), with d3d12_deferred_constants.
//
// The draw path keeps every decision on the thread that runs it (the command
// processor or the host render thread): which constant buffers are out of date,
// their allocation in the constant buffer pool and the root parameters. Instead
// of copying the float, bool and loop, and fetch constants into those buffers,
// it writes a kDeferredConstants command into the deferred command list with the
// constant registers written since they were last captured in the list, and
// the buffers to fill. The thread that replays the list into the D3D12 command
// list (the submit thread with d3d12_submit_thread) keeps its own copy of the
// constant registers, applies the captured values and fills the buffers before
// the list is executed, so the GPU reads them only after they are written.
//
// Every submission's list starts with all registers pending, so a list never
// depends on another list's captures.

#include <cstdint>
#include <cstring>

namespace rex::graphics::d3d12::deferred_constants {

// The constant registers in 16-byte slots, from SHADER_CONSTANT_000_X (0x4000)
// to SHADER_CONSTANT_LOOP_31 (0x4927): the float constants in slots 0 to 511
// (vertex 0 to 255, pixel 256 to 511), the fetch constants in 512 to 559, a gap,
// and the bool and loop constants in 576 to 585.
constexpr uint32_t kFirstRegister = 0x4000;
constexpr uint32_t kLastRegister = 0x4927;
constexpr uint32_t kSlotCount = ((kLastRegister - kFirstRegister) >> 2) + 1;
constexpr uint32_t kMaskWords = (kSlotCount + 63) / 64;
constexpr uint32_t kFetchFirstSlot = 512;
constexpr uint32_t kFetchSlotCount = 48;
constexpr uint32_t kBoolLoopFirstSlot = 576;
constexpr uint32_t kBoolLoopSlotCount = 10;

enum Fill : uint32_t { kFloatVertex, kFloatPixel, kBoolLoop, kFetch, kFillCount };

// Header::check bits.
constexpr uint32_t kCheck = 1;
// With kCheck: the comparison of the first non-empty buffer is changed on
// purpose, so it must be reported (d3d12_deferred_constants_check_perturb).
constexpr uint32_t kCheckPerturb = 2;

// The arguments of a kDeferredConstants command. Followed by the captured
// values (16 bytes per set bit of captured, in slot order), then, with the
// check, the bytes the inline path writes into each filled buffer, in Fill
// order.
struct Header {
  uint64_t captured[kMaskWords];
  // nullptr: the buffer is not filled at this draw.
  uint8_t* destinations[kFillCount];
  // Bytes written into each buffer (the inline path's: 16 per used float
  // constant, 160 for bool and loop, 768 for fetch).
  uint32_t sizes[kFillCount];
  // The float constants used by the vertex and the pixel shader.
  uint64_t float_maps[2][4];
  uint32_t captured_count;
  uint32_t check;
};

// Sets the bits of the slots first to last in a mask of kMaskWords.
inline void SetSlots(uint64_t* mask, uint32_t first, uint32_t last) {
  const uint32_t first_word = first >> 6, last_word = last >> 6;
  const uint64_t first_bits = ~uint64_t(0) << (first & 63);
  const uint64_t last_bits = ~uint64_t(0) >> (63 - (last & 63));
  if (first_word == last_word) {
    mask[first_word] |= first_bits & last_bits;
    return;
  }
  mask[first_word] |= first_bits;
  for (uint32_t word = first_word + 1; word < last_word; ++word) {
    mask[word] = ~uint64_t(0);
  }
  mask[last_word] |= last_bits;
}

// The registers written since they were last captured into the current list,
// kept by the thread that runs the draws.
struct Pending {
  uint64_t slots[kMaskWords];

  void SetAll() { std::memset(slots, 0xFF, sizeof(slots)); }
  // Any register range; the part outside the constant registers is ignored.
  void MarkWritten(uint32_t first_register, uint32_t last_register) {
    if (last_register < kFirstRegister || first_register > kLastRegister) {
      return;
    }
    if (first_register < kFirstRegister) first_register = kFirstRegister;
    if (last_register > kLastRegister) last_register = kLastRegister;
    SetSlots(slots, (first_register - kFirstRegister) >> 2, (last_register - kFirstRegister) >> 2);
  }
};

// Copies the float constants of a usage map from 16-byte slots (the register
// file or the replay's copy, starting at the first slot of the stage) into dst
// in map order. Returns the bytes written.
inline uint32_t PackFloatConstants(const uint32_t* slots, const uint64_t map[4], uint8_t* dst) {
  uint8_t* const start = dst;
  for (uint32_t i = 0; i < 4; ++i) {
    uint64_t bits = map[i];
    while (bits) {
      const uint32_t index = uint32_t(__builtin_ctzll(bits));
      bits &= bits - 1;
      std::memcpy(dst, slots + ((i << 6) + index) * 4, 16);
      dst += 16;
    }
  }
  return uint32_t(dst - start);
}

// The replay of a kDeferredConstants command: applies the captured values to
// shadow (kSlotCount * 4 dwords) and fills the buffers; with the check, compares
// each filled buffer with the inline bytes.
void Replay(const uint8_t* arguments, uint32_t* shadow);

}  // namespace rex::graphics::d3d12::deferred_constants
