#pragma once
// Records of the converted PM4 writers of the native path (stage N1, increment
// 3 of docs/analysis/d3d-hle.md, section 14.9), shared by the title's hooks and
// the GPU plugin. A converted writer writes a two-dword type-3 NOP marker into
// its command buffer segment instead of its PM4 and stores its record under the
// marker's guest physical address; the host render thread applies the record
// when it reaches the marker. The GPU plugin enables the conversion
// (host_render_native with host_render_thread); the title's hooks convert only
// while it is enabled.

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace rex::system::native_records {

// The marker: type 3, NOP, one data dword (in host byte order; guest memory
// holds it byte-swapped).
constexpr uint32_t kMarkerHeader = 0xC0001000;
constexpr uint32_t kMarkerTag = 0x4E41544B;

struct Record {
  uint32_t writer = 0;    // Guest address of the converted writer.
  uint64_t sequence = 0;  // Production order.
  // Register writes in the order of the PM4: first register and count, with
  // the values in guest (big-endian) byte order, consecutive in values.
  std::vector<std::pair<uint32_t, uint32_t>> runs;
  std::vector<uint32_t> values;
};

void SetEnabled(bool enabled);
bool Enabled();
// A record stays under its marker's address until a new marker replaces it:
// the same segment can be executed more than once (the tile bodies of a tiling
// bracket, run once per tile).
void Put(uint32_t marker_address, Record&& record);
// Tested without a lock before every packet.
bool MayHave(uint32_t marker_address);
std::shared_ptr<const Record> Find(uint32_t marker_address);

}  // namespace rex::system::native_records
