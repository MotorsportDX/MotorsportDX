// The decoded replays of stage N2 of docs/analysis/d3d-hle.md (section 15),
// with host_render_thread and host_render_oplists. A replay is an indirect
// buffer that a command buffer segment calls (depth two). At its first replay
// the host render thread decodes its PM4 into an op list: runs of register
// writes (type 0, SET_CONSTANT, SET_CONSTANT2, SET_SHADER_CONSTANTS, not
// predicated) written straight from guest memory through
// WriteRegistersFromMem, and blocks of every other packet executed as PM4
// through ExecutePacket. The op list stays valid while a write watch on the
// buffer's pages holds; a write by the CPU drops it, and the next replay
// decodes again.

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <rex/cvar.h>
#include <rex/hash.h>
#include <rex/graphics/command_processor.h>
#include <rex/graphics/xenos.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/memory/ring_buffer.h>

#include "util/perf_trace.h"

REXCVAR_DEFINE_BOOL(host_render_oplists_verify, false, "GPU",
                    "Diagnostics of host_render_oplists: hash each replayed buffer and report an "
                    "op list whose buffer changed while its write watch still held");

REXCVAR_DEFINE_BOOL(host_render_oplists, false, "GPU",
                    "With host_render_thread: replays of recorded command buffers run from op "
                    "lists decoded once (docs/analysis/d3d-hle.md, section 15)");

namespace rex::graphics {

namespace {
constexpr uint32_t kPageShift = 12;

struct Op {
  enum Kind : uint32_t { kRegisters, kRaw };
  Kind kind;
  uint32_t offset;  // Dwords from the start of the buffer.
  uint32_t count;   // Dwords.
  uint32_t first_register;
};

struct OpList {
  std::vector<Op> ops;
  std::atomic<bool> valid{false};
  bool fallback = false;  // The PM4 did not decode (a packet ran past the end).
  uint64_t hash = 0;      // host_render_oplists_verify: the PM4's hash at the decode.
};
}  // namespace

struct CommandProcessor::HostOpLists {
  std::unordered_map<uint64_t, std::unique_ptr<OpList>> lists;  // Host render thread.
  // The watched pages and the op lists on them, guarded by the mutex: the
  // invalidation callback runs on the thread that wrote.
  std::mutex mutex;
  std::unordered_map<uint32_t, std::vector<OpList*>> pages;
  void* callback = nullptr;
};

std::pair<uint32_t, uint32_t> CommandProcessor::HostOpListsInvalidated(void* context,
                                                                        uint32_t start,
                                                                        uint32_t length,
                                                                        bool exact_range) {
  auto* state = static_cast<HostOpLists*>(context);
  if (!length) return {0, UINT32_MAX};
  const uint32_t first = start >> kPageShift, last = (start + length - 1) >> kPageShift;
  std::lock_guard<std::mutex> lock(state->mutex);
  for (uint32_t page = first; page <= last; ++page) {
    auto it = state->pages.find(page);
    if (it == state->pages.end()) continue;
    for (OpList* list : it->second) {
      if (list->valid.exchange(false, std::memory_order_acq_rel)) {
        perf_trace::host_oplists_invalidated.fetch_add(1, std::memory_order_relaxed);
      }
    }
    // The watch is one-shot: the page is watched again when an op list on it
    // is decoded again.
    state->pages.erase(it);
  }
  // Only the triggered pages may be unprotected: a wider range (the "does not
  // matter" answer) would unwatch the pages of other op lists without telling
  // them, and their buffers would change unnoticed.
  return {start, length};
}

namespace {
// Decodes the PM4 of a replayed buffer into ops; false when a packet runs past
// the end.
bool Decode(const uint32_t* dwords, uint32_t count, std::vector<Op>& ops) {
  using namespace xenos;
  const auto word = [&](uint32_t i) { return memory::load_and_swap<uint32_t>(dwords + i); };
  const auto raw = [&](uint32_t offset, uint32_t length) {
    if (!ops.empty() && ops.back().kind == Op::kRaw &&
        ops.back().offset + ops.back().count == offset) {
      ops.back().count += length;
    } else {
      ops.push_back({Op::kRaw, offset, length, 0});
    }
  };
  const auto registers = [&](uint32_t first, uint32_t offset, uint32_t length) {
    if (length) ops.push_back({Op::kRegisters, offset, length, first});
  };
  static const uint32_t kConstantBases[] = {0x4000, 0x4800, 0x4900, 0x4908, 0x2000};
  uint32_t at = 0;
  while (at < count) {
    const uint32_t header = word(at);
    const uint32_t type = header >> 30;
    if (header == 0 || type == 2) {
      ++at;
      continue;
    }
    if (type == 1) {
      if (at + 3 > count) return false;
      raw(at, 3);
      at += 3;
      continue;
    }
    const uint32_t body = ((header >> 16) & 0x3FFF) + 1;
    if (at + 1 + body > count) return false;
    if (type == 0) {
      if ((header >> 15) & 1) {
        raw(at, 1 + body);  // One register written repeatedly.
      } else {
        registers(header & 0x7FFF, at + 1, body);
      }
    } else {
      const uint32_t opcode = (header >> 8) & 0x7F;
      const bool predicated = header & 1;
      if (!predicated && opcode == PM4_SET_CONSTANT && body >= 1 &&
          ((word(at + 1) >> 16) & 0xFF) < 5) {
        const uint32_t offset_type = word(at + 1);
        registers(kConstantBases[(offset_type >> 16) & 0xFF] + (offset_type & 0x7FF), at + 2,
                  body - 1);
      } else if (!predicated &&
                 (opcode == PM4_SET_CONSTANT2 || opcode == PM4_SET_SHADER_CONSTANTS) &&
                 body >= 1) {
        registers(word(at + 1) & 0xFFFF, at + 2, body - 1);
      } else {
        raw(at, 1 + body);
      }
    }
    at += 1 + body;
  }
  return true;
}
}  // namespace

void CommandProcessor::InitializeHostOpLists() {
  host_oplists_ = host_render_queue_ && REXCVAR_GET(host_render_oplists);
  if (!host_oplists_) {
    return;
  }
  host_oplists_state_ = std::make_shared<HostOpLists>();
  host_oplists_state_->callback =
      memory_->RegisterPhysicalMemoryInvalidationCallback(HostOpListsInvalidated,
                                                          host_oplists_state_.get());
  perf_trace::host_oplists.store(true, std::memory_order_relaxed);
  REXGPU_INFO("Host render op lists: on (replays run from decoded op lists)");
}

bool CommandProcessor::HostReplay(uint32_t ptr, uint32_t count) {
  HostOpLists& state = *host_oplists_state_;
  const uint64_t key = uint64_t(ptr) << 32 | count;
  std::unique_ptr<OpList>& slot = state.lists[key];
  if (!slot) {
    slot = std::make_unique<OpList>();
  }
  OpList& list = *slot;
  if (!list.valid.load(std::memory_order_acquire)) {
    if (list.fallback) {
      return false;
    }
    // Watch first, then decode: a write during the decode drops the list again.
    list.valid.store(true, std::memory_order_release);
    {
      std::lock_guard<std::mutex> lock(state.mutex);
      const uint32_t first = ptr >> kPageShift, last = (ptr + count * 4 - 1) >> kPageShift;
      for (uint32_t page = first; page <= last; ++page) {
        state.pages[page].push_back(&list);
      }
    }
    memory_->EnablePhysicalMemoryAccessCallbacks(ptr, count * 4, true, false);
    list.ops.clear();
    if (!Decode(memory_->TranslatePhysical<const uint32_t*>(ptr), count, list.ops)) {
      list.fallback = true;
      list.valid.store(false, std::memory_order_release);
      return false;
    }
    ++perf_trace::host_oplists_decoded;
    if (REXCVAR_GET(host_render_oplists_verify)) {
      list.hash = XXH3_64bits(memory_->TranslatePhysical<const uint8_t*>(ptr), count * 4);
    }
  } else if (REXCVAR_GET(host_render_oplists_verify) &&
             XXH3_64bits(memory_->TranslatePhysical<const uint8_t*>(ptr), count * 4) !=
                 list.hash) {
    static uint32_t messages = 0;
    if (messages < 32) {
      ++messages;
      REXGPU_WARN("Op lists: buffer {:08X} ({} dwords) changed without a watch callback: {}",
                  ptr, count, memory_->DescribePhysicalPage(ptr));
    }
    perf_trace::host_oplists_stale.fetch_add(1, std::memory_order_relaxed);
    list.valid.store(false, std::memory_order_release);
    return false;
  }

  const bool split = perf_trace::SplitEnabled();
  const uint64_t replay_start = split ? __rdtsc() : 0;
  ++perf_trace::indirect_depth;
  uint32_t* base = memory_->TranslatePhysical<uint32_t*>(ptr);
  for (const Op& op : list.ops) {
    if (op.kind == Op::kRegisters) {
      perf_trace::SplitScope scope(perf_trace::Split::kRegisters);
      WriteRegistersFromMem(op.first_register, base + op.offset, op.count);
      continue;
    }
    memory::RingBuffer reader(reinterpret_cast<uint8_t*>(base + op.offset),
                              op.count * sizeof(uint32_t));
    reader.set_write_offset(op.count * sizeof(uint32_t));
    do {
      if (!ExecutePacket(&reader)) {
        REXGPU_ERROR("**** OP LIST: Failed to execute packet.");
        break;
      }
    } while (reader.read_count());
  }
  --perf_trace::indirect_depth;
  if (replay_start) {
    perf_trace::State* trace = perf_trace::Get();
    trace->replay_tsc += __rdtsc() - replay_start;
    ++trace->replays;
    trace->replay_dwords += count;
  }
  ++perf_trace::host_oplist_replays;
  return true;
}

}  // namespace rex::graphics
