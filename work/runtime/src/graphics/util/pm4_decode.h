#pragma once
// A decoder of PM4 into the state it sets, for the identity check of the native
// path (docs/analysis/d3d-hle.md, section 14.7): the register writes, the
// registers loaded from memory (not known from the PM4 alone), the type-3
// packets with their predicate decision, and the draws. It reads a copy of the
// PM4 and touches no guest memory, so the check can compare it with what the
// command processor executes.

#include <cstdint>
#include <utility>
#include <vector>

#include <rex/graphics/xenos.h>
#include <rex/memory.h>

namespace rex::graphics::pm4_decode {

struct Type3 {
  uint32_t offset = 0;  // Dwords from the start of the decoded PM4.
  uint32_t opcode = 0;
  bool runs = true;     // Not skipped by bin predication.
};

struct Draw {
  uint32_t initiator = 0;  // VGT_DRAW_INITIATOR.
  uint32_t dma_base = 0;   // VGT_DMA_BASE and VGT_DMA_SIZE for indexed draws, else 0.
  uint32_t dma_size = 0;
  bool operator==(const Draw& other) const {
    return initiator == other.initiator && dma_base == other.dma_base &&
           dma_size == other.dma_size;
  }
};

struct Decoded {
  std::vector<std::pair<uint32_t, uint32_t>> registers;  // Index and value, in order.
  std::vector<std::pair<uint32_t, uint32_t>> loaded;     // First index and count.
  std::vector<Type3> type3;
  std::vector<Draw> draws;  // Draws that run, in order.
  bool indirect = false;    // Contains an INDIRECT_BUFFER (its PM4 is not decoded).
  bool unknown_writes = false;  // Contains a packet that may write any register.
  bool complete = true;     // Every packet fit in the PM4.
};

// dwords: PM4 in guest (big-endian) byte order. bin_select and bin_mask: the
// predication state where the PM4 starts.
inline Decoded Decode(const uint32_t* dwords, size_t count, uint64_t bin_select,
                      uint64_t bin_mask) {
  using namespace xenos;
  Decoded out;
  const auto word = [&](size_t i) { return memory::load_and_swap<uint32_t>(dwords + i); };
  size_t at = 0;
  while (at < count) {
    const uint32_t header = word(at);
    const uint32_t type = header >> 30;
    if (header == 0 || type == 2) {
      ++at;
      continue;
    }
    if (type == 1) {
      if (at + 3 > count) {
        out.complete = false;
        break;
      }
      out.registers.emplace_back(header & 0x7FF, word(at + 1));
      out.registers.emplace_back((header >> 11) & 0x7FF, word(at + 2));
      at += 3;
      continue;
    }
    const uint32_t body = ((header >> 16) & 0x3FFF) + 1;
    if (at + 1 + body > count) {
      out.complete = false;
      break;
    }
    const size_t data = at + 1;
    if (type == 0) {
      const uint32_t base = header & 0x7FFF;
      const bool one_register = (header >> 15) & 1;
      for (uint32_t i = 0; i < body; ++i) {
        out.registers.emplace_back(one_register ? base : base + i, word(data + i));
      }
      at = data + body;
      continue;
    }
    // Type 3.
    const uint32_t opcode = (header >> 8) & 0x7F;
    Type3 packet;
    packet.offset = uint32_t(at);
    packet.opcode = opcode;
    packet.runs = !(header & 1) || ((bin_select & bin_mask) != 0 && opcode != PM4_XE_SWAP);
    out.type3.push_back(packet);
    if (packet.runs) {
      switch (opcode) {
        case PM4_SET_CONSTANT: {
          const uint32_t offset_type = word(data);
          const uint32_t index = offset_type & 0x7FF;
          static const uint32_t kBases[] = {0x4000, 0x4800, 0x4900, 0x4908, 0x2000};
          const uint32_t kind = (offset_type >> 16) & 0xFF;
          if (kind < 5) {
            for (uint32_t i = 1; i < body; ++i) {
              out.registers.emplace_back(kBases[kind] + index + i - 1, word(data + i));
            }
          }
        } break;
        case PM4_SET_CONSTANT2:
        case PM4_SET_SHADER_CONSTANTS: {
          const uint32_t index = word(data) & 0xFFFF;
          for (uint32_t i = 1; i < body; ++i) {
            out.registers.emplace_back(index + i - 1, word(data + i));
          }
        } break;
        case PM4_LOAD_ALU_CONSTANT: {
          // The values come from guest memory.
          if (body >= 3) {
            const uint32_t offset_type = word(data + 1);
            const uint32_t index = offset_type & 0x7FF;
            static const uint32_t kBases[] = {0x4000, 0x4800, 0x4900, 0x4908, 0x2000};
            const uint32_t kind = (offset_type >> 16) & 0xFF;
            if (kind < 5) {
              out.loaded.emplace_back(kBases[kind] + index, word(data + 2) & 0xFFF);
            }
          }
        } break;
        case PM4_INDIRECT_BUFFER:
        case PM4_INDIRECT_BUFFER_PFD:
          out.indirect = true;
          break;
        case PM4_REG_RMW:
          // The value depends on the register file.
          out.loaded.emplace_back(word(data) & 0x1FFF, 1);
          break;
        case PM4_COND_WRITE:
          // May write a register; which one and whether it does depend on the
          // condition at execution.
          out.unknown_writes = true;
          break;
        case PM4_DRAW_INDX:
        case PM4_DRAW_INDX_2: {
          size_t i = data + (opcode == PM4_DRAW_INDX ? 1 : 0);
          if (i < data + body) {
            Draw draw;
            draw.initiator = word(i++);
            reg::VGT_DRAW_INITIATOR initiator;
            initiator.value = draw.initiator;
            out.registers.emplace_back(XE_GPU_REG_VGT_DRAW_INITIATOR, draw.initiator);
            if (initiator.source_select == SourceSelect::kDMA && i + 2 <= data + body) {
              draw.dma_base = word(i);
              draw.dma_size = word(i + 1);
              out.registers.emplace_back(XE_GPU_REG_VGT_DMA_BASE, draw.dma_base);
              out.registers.emplace_back(XE_GPU_REG_VGT_DMA_SIZE, draw.dma_size);
            }
            out.draws.push_back(draw);
          }
        } break;
        case PM4_SET_BIN_MASK_LO:
          bin_mask = (bin_mask & 0xFFFFFFFF00000000ull) | word(data);
          break;
        case PM4_SET_BIN_MASK_HI:
          bin_mask = (bin_mask & 0xFFFFFFFFull) | (uint64_t(word(data)) << 32);
          break;
        case PM4_SET_BIN_SELECT_LO:
          bin_select = (bin_select & 0xFFFFFFFF00000000ull) | word(data);
          break;
        case PM4_SET_BIN_SELECT_HI:
          bin_select = (bin_select & 0xFFFFFFFFull) | (uint64_t(word(data)) << 32);
          break;
        case PM4_SET_BIN_MASK:
          if (body >= 2) bin_mask = (uint64_t(word(data)) << 32) | word(data + 1);
          break;
        case PM4_SET_BIN_SELECT:
          if (body >= 2) bin_select = (uint64_t(word(data)) << 32) | word(data + 1);
          break;
        case PM4_EVENT_WRITE:
        case PM4_EVENT_WRITE_SHD:
        case PM4_EVENT_WRITE_EXT:
        case PM4_EVENT_WRITE_ZPD:
          // The command processor writes the event's initiator.
          out.loaded.emplace_back(XE_GPU_REG_VGT_EVENT_INITIATOR, 1);
          break;
        case PM4_VIZ_QUERY:
          out.loaded.emplace_back(XE_GPU_REG_VGT_EVENT_INITIATOR, 1);
          out.loaded.emplace_back(XE_GPU_REG_PA_SC_VIZ_QUERY_STATUS_0, 1);
          out.loaded.emplace_back(XE_GPU_REG_PA_SC_VIZ_QUERY_STATUS_1, 1);
          break;
        case PM4_ME_INIT:
        case PM4_NOP:
        case PM4_INTERRUPT:
        case PM4_XE_SWAP:
        case PM4_WAIT_REG_MEM:
        case PM4_WAIT_FOR_IDLE:
        case PM4_MEM_WRITE:
        case PM4_REG_TO_MEM:
        case PM4_IM_LOAD:
        case PM4_IM_LOAD_IMMEDIATE:
        case PM4_INVALIDATE_STATE:
        case PM4_CONTEXT_UPDATE:
          // No register write.
          break;
        default:
          // A packet this decoder does not model may write any register.
          out.unknown_writes = true;
          break;
      }
    }
    at = data + body;
  }
  return out;
}

}  // namespace rex::graphics::pm4_decode
