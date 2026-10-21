// The records of converted PM4 writers, stage N1 increment 3 of
// docs/analysis/d3d-hle.md (section 14.9), with host_render_thread and
// host_render_native. A converted writer of the title leaves a NOP marker in
// its command buffer segment and its record under the marker's address
// (rex/system/native_records.h). When the host render thread reaches the marker
// it writes the record's registers the way the type-0 packets would have. A
// record stays under its address until a new marker replaces it, so a segment
// executed more than once (the tile bodies of a tiling bracket) applies it
// each time; a record whose address no longer holds a marker is stale.

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
  if (host_check_) {
    // A checked range ends before the record at its end is applied.
    HostCheckFinishAt(address);
  }
  if (host_records_) {
    host_packet_address_ = address;
    if (system::native_records::MayHave(address)) {
      HostApplyRecord(address);
    }
  }
  if (host_check_) {
    HostCheckTake(address);
  }
}

void CommandProcessor::HostApplyRecord(uint32_t address) {
  const std::shared_ptr<const system::native_records::Record> found =
      system::native_records::Find(address);
  if (!found) {
    return;
  }
  const system::native_records::Record& record = *found;
  const uint32_t* guest = memory_->TranslatePhysical<const uint32_t*>(address);
  if (memory::load_and_swap<uint32_t>(guest) != system::native_records::kMarkerHeader ||
      memory::load_and_swap<uint32_t>(guest + 1) != system::native_records::kMarkerTag) {
    // The segment memory holds other PM4 now: the record is stale.
    return;
  }
  size_t offset = 0;
  for (const auto& [first, count] : record.runs) {
    WriteRegistersFromMem(first, const_cast<uint32_t*>(record.values.data() + offset), count);
    offset += count;
  }
  host_record_applied_ = address;
  ++perf_trace::host_records_applied;
  perf_trace::host_record_registers += offset;
}

void CommandProcessor::HostRecordMissing() {
  if (host_record_applied_ == host_packet_address_) {
    return;
  }
  perf_trace::host_markers_missing.fetch_add(1, std::memory_order_relaxed);
  static uint32_t messages = 0;
  if (messages < 64) {
    ++messages;
    REXGPU_ERROR("Native record: marker without a record");
    REXGPU_WARN("Native record: marker at {:08X} without a record", host_packet_address_);
  }
}

}  // namespace rex::graphics
