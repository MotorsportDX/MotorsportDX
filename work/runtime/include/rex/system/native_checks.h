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
#include <string>
#include <vector>

namespace rex::system::native_checks {

struct Entry {
  uint32_t writer = 0;    // Guest address of the hooked writer.
  uint64_t sequence = 0;  // Production order.
  std::vector<uint32_t> dwords;  // The PM4 as written, in guest byte order.
  // The PM4 of a converted writer, written into a scratch segment; the stream
  // holds its marker (rex/system/native_records.h) instead.
  bool marker = false;
};

// True once anything was recorded.
bool Active();
void Record(uint32_t packet_address, Entry&& entry);
// Takes the entry of a packet address, false without one.
bool Take(uint32_t packet_address, Entry* entry);
// Whether the entry of this sequence still waits at the packet address: the
// host render thread has not reached the range yet. An entry whose range never
// runs also waits, until a newer one at its address replaces it.
bool Pending(uint32_t packet_address, uint64_t sequence);
// The late write trace (FM4_LATE_WRITE_TRACE): a hook's note that a function
// rewrote a dword of the pending range of this sequence. The identity check
// prints the notes of a range whose PM4 changed and drops the others; the
// latest 8,192 sequences with notes are kept.
void NoteLateWrite(uint64_t sequence, std::string text);
std::vector<std::string> TakeLateWrites(uint64_t sequence);
// Entries replaced by a newer one at the same address before they were taken,
// since the last call.
uint64_t TakeOverwritten();
// Ranges a hook could not copy (a write pointer that went backwards or jumped
// more than 1 MB, or an allocation inside a writer that passes its own pointer).
void CountDropped();
uint64_t TakeDropped();

}  // namespace rex::system::native_checks
