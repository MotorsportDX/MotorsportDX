#pragma once
// Diagnostic table shared by the title's hooks and the GPU plugin: PM4 packets
// that a guest hook saw being written, keyed by the guest physical address of
// the packet header, with a hash of the guest memory the packet points at taken
// at that moment. The command processor looks a packet up when it executes it
// and compares the hash with the memory it reads then (stage N0 of
// docs/analysis/d3d-hle.md). Nothing records into it unless a hook does, so it
// costs nothing in a normal run.

#include <cstdint>

namespace rex::system::pm4_emissions {

enum class Kind : uint32_t {
  kShader,     // IM_LOAD: microcode at the source address.
  kConstants,  // LOAD_ALU_CONSTANT: the constant block.
  kCount
};

struct Entry {
  uint64_t hash = 0;
  uint32_t length = 0;     // Bytes hashed.
  bool recorded = false;   // Written into a command buffer recording.
  bool executed = false;   // Taken by the command processor at the packet's first execution.
};

// True once anything was recorded.
bool Active();
void Record(Kind kind, uint32_t packet_address, const Entry& entry);
bool Find(Kind kind, uint32_t packet_address, Entry* entry);

}  // namespace rex::system::pm4_emissions
