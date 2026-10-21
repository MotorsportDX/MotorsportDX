#pragma once
// Records of the identity check of the native path (stage N1, increment 2 of
// docs/analysis/d3d-hle.md, section 14.7), shared by the title's hooks and the
// GPU plugin. A hook on a PM4 writer copies the PM4 the writer put into the
// ring, keyed by the guest physical address of its first packet; the host
// render thread takes the entry when it reaches that address, decodes the copy
// and compares it with what the command processor executes. Nothing records
// into it unless the title's check is on (FM4_NATIVE_CHECK), so it costs
// nothing in a normal run.

#include <cstdint>
#include <vector>

namespace rex::system::native_checks {

struct Entry {
  uint32_t writer = 0;    // Guest address of the hooked writer.
  uint64_t sequence = 0;  // Production order.
  std::vector<uint32_t> dwords;  // The PM4 as written, in guest byte order.
};

// True once anything was recorded.
bool Active();
void Record(uint32_t packet_address, Entry&& entry);
// Takes the entry of a packet address, false without one.
bool Take(uint32_t packet_address, Entry* entry);
// Entries replaced by a newer one at the same address before they were taken,
// since the last call.
uint64_t TakeOverwritten();

}  // namespace rex::system::native_checks
