// The records of converted PM4 writers, stage N1 increment 3 of
// docs/analysis/d3d-hle.md (section 14.9), with host_render_thread and
// host_render_native. A converted writer of the title fills a record in a slot
// of a fixed pool and leaves a NOP marker holding the record's sequence in its
// command buffer segment (rex/system/native_records.h). When the host render
// thread executes the marker it writes the record's registers the way the
// type-0 packets would have. A segment executed more than once (the tile bodies
// of a tiling bracket) applies its records each time, until the pool wraps.

#include <rex/cvar.h>
#include <rex/graphics/command_processor.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/system/native_records.h>

#include "util/perf_trace.h"

REXCVAR_DEFINE_BOOL(host_render_native, false, "GPU",
                    "With host_render_thread: the title's converted PM4 writers leave a marker "
                    "and a record instead of their PM4 (docs/analysis/d3d-hle.md, section 14.9)");

namespace rex::graphics {

void CommandProcessor::InitializeHostRecords() {
  host_records_ = host_render_queue_ && REXCVAR_GET(host_render_native);
  system::native_records::SetEnabled(host_records_);
  if (host_records_) {
    perf_trace::host_records.store(true, std::memory_order_relaxed);
    REXGPU_INFO("Host render records: on (converted writers leave markers)");
  }
}

void CommandProcessor::HostBeforePacket(const memory::RingBuffer* reader) {
  const uint32_t address = uint32_t(reader->buffer() + reader->read_offset() -
                                    memory_->TranslatePhysical<const uint8_t*>(0));
  // A checked range ends before the next packet runs, so before a marker's
  // record is applied.
  HostCheckFinishAt(address);
  HostCheckTake(address);
}

void CommandProcessor::HostApplyMarker(uint32_t sequence) {
  system::native_records::Slot& slot = system::native_records::SlotOf(sequence);
  const uint64_t stored = slot.sequence.load(std::memory_order_acquire);
  if (stored >= system::native_records::kWriting || uint32_t(stored) != sequence) {
    // The record was never published, or the pool wrapped around since.
    HostRecordMissing(sequence, stored);
    return;
  }
  const system::native_records::Record& record = slot.record;
  size_t offset = 0;
  for (const auto& [first, count] : record.runs) {
    WriteRegistersFromMem(first, const_cast<uint32_t*>(record.values.data() + offset), count);
    offset += count;
  }
  if (slot.sequence.load(std::memory_order_acquire) != stored) {
    // A producer reused the slot while it was applied.
    HostRecordMissing(sequence, slot.sequence.load(std::memory_order_relaxed));
    return;
  }
  ++perf_trace::host_records_applied;
  perf_trace::host_record_registers += offset;
}

void CommandProcessor::HostRecordMissing(uint32_t sequence, uint64_t stored) {
  perf_trace::host_markers_missing.fetch_add(1, std::memory_order_relaxed);
  static uint32_t messages = 0;
  if (messages < 64) {
    ++messages;
    REXGPU_ERROR("Native record: marker without a record");
    REXGPU_WARN("Native record: marker of record {} finds slot holding {:X}", sequence, stored);
  }
}

}  // namespace rex::graphics
