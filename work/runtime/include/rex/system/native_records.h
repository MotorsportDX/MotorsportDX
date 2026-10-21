#pragma once
// Records of the converted PM4 writers of the native path (stage N1, increment
// 3 of docs/analysis/d3d-hle.md, section 14.9), shared by the title's hooks and
// the GPU plugin. A converted writer fills a record in a slot of a fixed pool
// and writes a three-dword type-3 NOP marker into its command buffer segment
// instead of its PM4; the marker holds the record's sequence, which also names
// its slot. The host render thread applies the record when it executes the
// marker. A record stays in its slot until the pool wraps around, so a segment
// executed more than once (the tile bodies of a tiling bracket, run once per
// tile) applies it each time. The GPU plugin enables the conversion
// (host_render_native with host_render_thread); the title's hooks convert only
// while it is enabled.

#include <atomic>
#include <cstdint>
#include <utility>
#include <vector>

namespace rex::system::native_records {

// The marker: type 3, NOP, two data dwords, the tag and the low 32 bits of the
// record's sequence (in host byte order; guest memory holds them byte-swapped).
constexpr uint32_t kMarkerHeader = 0xC0011000;
constexpr uint32_t kMarkerTag = 0x4E41544B;
constexpr uint32_t kMarkerDwords = 3;

// 65,536 records: at about 2,000 converted calls a frame, a slot is reused
// after about 30 frames, long after the host render thread executed its marker
// (the queue holds about one frame).
constexpr uint32_t kSlots = 1u << 16;

struct Record {
  uint32_t writer = 0;  // Guest address of the converted writer.
  // Register writes in the order of the PM4: first register and count, with
  // the values in guest (big-endian) byte order, consecutive in values.
  std::vector<std::pair<uint32_t, uint32_t>> runs;
  std::vector<uint32_t> values;
};

struct Slot {
  // The sequence of the record in the slot; kWriting while it is filled.
  std::atomic<uint64_t> sequence{UINT64_MAX};
  Record record;
};
constexpr uint64_t kWriting = UINT64_MAX - 1;

void SetEnabled(bool enabled);
bool Enabled();
// Producer: takes the next sequence and its slot, cleared for filling (the
// vectors keep their capacity); Publish makes it visible before the marker is
// written.
Slot& Begin(uint64_t* sequence);
void Publish(Slot& slot, uint64_t sequence);
// Consumer: the slot of a marker's sequence (its low 32 bits).
Slot& SlotOf(uint32_t sequence);
// The kicks of the ring write pointer so far: a converted call joins the
// record of the thread's last marker only while no kick happened since it was
// written (a kick does not move the segment's write pointer).
void CountKick();
uint32_t Kicks();

}  // namespace rex::system::native_records
