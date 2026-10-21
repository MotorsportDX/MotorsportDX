// The identity check of the native path, stage N1 increment 2 of
// docs/analysis/d3d-hle.md (section 14.7), with host_render_thread and
// host_render_check. The title's hooks (FM4_NATIVE_CHECK) copy the PM4 that a
// writer put into the ring (rex/system/native_checks.h). When the host render
// thread reaches the first packet of such a range it takes the copy, decodes it
// (util/pm4_decode.h) and records what the command processor does with the
// range: the type-3 packets it runs or skips by predication and the draws it
// issues. At the end of the range it compares the copy with the PM4 it read,
// the decoded register writes with the register file, and the packets and
// draws with the decoded ones. A difference is a REXGPU_ERROR, so it shows as
// a new error key in the gates.

#include <algorithm>
#include <map>
#include <unordered_map>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/graphics/command_processor.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/system/native_checks.h>

#include "util/perf_trace.h"
#include "util/pm4_decode.h"

REXCVAR_DEFINE_BOOL(host_render_check, false, "GPU",
                    "With host_render_thread: compare the PM4 ranges that the title's hooks "
                    "copied (FM4_NATIVE_CHECK) with what the host render thread executes "
                    "(docs/analysis/d3d-hle.md, section 14.7)");

REXCVAR_DEFINE_INT32(host_render_check_perturb, 0, "GPU",
                     "Test of host_render_check: change the decoded copy of every Nth range "
                     "before the comparison, so that each of them must be reported (0: off)")
    .range(0, 1000000);

namespace rex::graphics {

struct CommandProcessor::HostCheckState {
  // The range being executed.
  bool capturing = false;
  uint32_t begin = 0, end = 0;
  system::native_checks::Entry entry;
  pm4_decode::Decoded decoded;
  bool bytes_changed = false;
  uint32_t first_changed = 0;
  std::vector<pm4_decode::Type3> type3;
  std::vector<pm4_decode::Draw> draws;
  uint64_t compared = 0;
  uint32_t perturb = 0;
  uint32_t messages = 0;
};

namespace {
constexpr uint32_t kMaxMessages = 64;
}  // namespace

void CommandProcessor::InitializeHostCheck() {
  host_check_ = host_render_queue_ && REXCVAR_GET(host_render_check);
  if (!host_check_) {
    return;
  }
  host_check_state_ = std::make_shared<HostCheckState>();
  host_check_state_->perturb = uint32_t(REXCVAR_GET(host_render_check_perturb));
  perf_trace::host_check.store(true, std::memory_order_relaxed);
  REXGPU_INFO("Host render check: on, perturbing every {} range(s)",
              host_check_state_->perturb);
}

void CommandProcessor::HostCheckBeforePacket(const memory::RingBuffer* reader) {
  HostCheckState& state = *host_check_state_;
  const uint32_t address = uint32_t(reader->buffer() + reader->read_offset() -
                                    memory_->TranslatePhysical<const uint8_t*>(0));
  if (state.capturing && address == state.end) {
    HostCheckFinish();
  }
  if (system::native_checks::Active()) {
    system::native_checks::Entry entry;
    if (system::native_checks::Take(address, &entry)) {
      if (state.capturing) {
        // A range that never reached its end: not compared.
        perf_trace::host_check_unmatched.fetch_add(1, std::memory_order_relaxed);
      }
      state.capturing = true;
      state.begin = address;
      state.end = address + uint32_t(entry.dwords.size() * sizeof(uint32_t));
      // The PM4 the command processor reads now against the copy taken when the
      // writer returned.
      const uint32_t* guest = memory_->TranslatePhysical<const uint32_t*>(address);
      state.bytes_changed = false;
      for (size_t i = 0; i < entry.dwords.size(); ++i) {
        if (guest[i] != entry.dwords[i]) {
          state.bytes_changed = true;
          state.first_changed = uint32_t(i);
          break;
        }
      }
      state.decoded =
          pm4_decode::Decode(entry.dwords.data(), entry.dwords.size(), bin_select_, bin_mask_);
      state.entry = std::move(entry);
      state.type3.clear();
      state.draws.clear();
    }
  }
  host_check_in_range_ = state.capturing && address >= state.begin && address < state.end;
}

void CommandProcessor::HostCheckType3(uint32_t opcode, bool runs) {
  HostCheckState& state = *host_check_state_;
  pm4_decode::Type3 packet;
  packet.opcode = opcode;
  packet.runs = runs;
  state.type3.push_back(packet);
}

void CommandProcessor::HostCheckDraw(uint32_t initiator, uint32_t dma_base, uint32_t dma_size) {
  host_check_state_->draws.push_back({initiator, dma_base, dma_size});
}

void CommandProcessor::HostCheckFinish() {
  HostCheckState& state = *host_check_state_;
  state.capturing = false;
  host_check_in_range_ = false;
  const pm4_decode::Decoded& decoded = state.decoded;
  const uint32_t writer = state.entry.writer;
  ++perf_trace::host_checks[writer];
  perf_trace::host_check_unmatched.fetch_add(system::native_checks::TakeOverwritten(),
                                             std::memory_order_relaxed);
  std::string difference;
  if (state.bytes_changed) {
    difference = fmt::format("the PM4 changed after the writer returned (dword {})",
                             state.first_changed);
  }
  // Every Nth range is changed on purpose (host_render_check_perturb).
  const bool perturb = state.perturb && ++state.compared % state.perturb == 0;
  bool perturbed_registers = false;
  if (difference.empty() && !decoded.indirect && !decoded.unknown_writes) {
    std::unordered_map<uint32_t, uint32_t> last;
    std::vector<uint32_t> order;
    for (const auto& [index, value] : decoded.registers) {
      if (last.emplace(index, value).second) {
        order.push_back(index);
      } else {
        last[index] = value;
      }
    }
    const auto loaded = [&](uint32_t index) {
      return std::any_of(decoded.loaded.begin(), decoded.loaded.end(), [&](const auto& range) {
        return index >= range.first && index < range.first + range.second;
      });
    };
    order.erase(std::remove_if(order.begin(), order.end(),
                               [&](uint32_t index) {
                                 return index >= RegisterFile::kRegisterCount || loaded(index);
                               }),
                order.end());
    if (perturb && !order.empty()) {
      last[order.front()] ^= 1;
      perturbed_registers = true;
      perf_trace::host_check_perturbed.fetch_add(1, std::memory_order_relaxed);
    }
    for (uint32_t index : order) {
      const uint32_t value = register_file_->values[index];
      if (value != last[index]) {
        difference = fmt::format("register {:04X} is {:08X}, the record has {:08X}", index,
                                 value, last[index]);
        break;
      }
    }
  }
  if (difference.empty() && !decoded.indirect) {
    std::vector<pm4_decode::Draw> expected_draws = decoded.draws;
    if (perturb && !perturbed_registers && !expected_draws.empty()) {
      expected_draws.front().initiator += 1u << 16;
      perf_trace::host_check_perturbed.fetch_add(1, std::memory_order_relaxed);
    }
    if (state.type3.size() != decoded.type3.size()) {
      difference = fmt::format("{} type-3 packets executed, the record has {}",
                               state.type3.size(), decoded.type3.size());
    }
    for (size_t i = 0; difference.empty() && i < state.type3.size(); ++i) {
      const pm4_decode::Type3& a = state.type3[i];
      const pm4_decode::Type3& b = decoded.type3[i];
      if (a.opcode != b.opcode || a.runs != b.runs) {
        difference = fmt::format("type-3 packet {} is opcode {:02X} {}, the record has {:02X} {}",
                                 i, a.opcode, a.runs ? "run" : "skipped", b.opcode,
                                 b.runs ? "run" : "skipped");
      }
    }
    if (difference.empty() && state.draws.size() != expected_draws.size()) {
      difference = fmt::format("{} draws issued, the record has {}", state.draws.size(),
                               expected_draws.size());
    }
    for (size_t i = 0; difference.empty() && i < state.draws.size(); ++i) {
      const pm4_decode::Draw& a = state.draws[i];
      const pm4_decode::Draw& b = expected_draws[i];
      if (!(a == b)) {
        difference = fmt::format(
            "draw {} has initiator {:08X} base {:08X} size {:08X}, the record has {:08X} {:08X} "
            "{:08X}",
            i, a.initiator, a.dma_base, a.dma_size, b.initiator, b.dma_base, b.dma_size);
      }
    }
  }
  if (!difference.empty()) {
    perf_trace::host_check_mismatches.fetch_add(1, std::memory_order_relaxed);
    if (state.messages < kMaxMessages) {
      ++state.messages;
      REXGPU_ERROR("Native check: writer {:08X}, range at {:08X} ({} dwords, record {}): {}",
                   writer, state.begin, state.entry.dwords.size(), state.entry.sequence,
                   difference);
    }
  }
}

}  // namespace rex::graphics
