#pragma once

// Read trace of resolve readbacks, a diagnostic of stage N3 of
// docs/analysis/d3d-hle.md (16.4). With readback_read_trace (off by default)
// and the perf trace, every resolve readback published to guest memory traps
// its destination pages (Memory::ArmReadTrace) until the next readback of the
// same destination. The first access to a trapped page restores its
// protection and is counted with the thread that made it; nothing waits, and
// the readback itself is unchanged, so the guest always reads the right data.
// It measures how many readback bytes nobody reads: the ceiling of a readback
// on demand.
//
// Only the guest virtual views of physical memory are trapped: readers through
// Memory::TranslatePhysical (the runtime's uploads into shared memory, and the
// readback copy itself) are not seen.

#include <cstdint>
#include <ostream>

namespace rex::memory {
class Memory;
}

namespace rex::graphics::read_trace {

// Whether the cvar and the perf trace are on. The first call that returns true
// installs the fault handler, which stays for the life of the process, because
// a page may stay trapped until its next access.
bool Enabled(memory::Memory& memory);
// After a readback of [address, address + length) was published to guest
// memory, on the command processor thread.
void OnReadback(memory::Memory& memory, uint32_t address, uint32_t length);
// The "read_trace" object of a perf trace window (the caller has an object
// open and closes it), and every tenth window the destinations file.
void WriteWindow(std::ostream& output, double seconds);

}  // namespace rex::graphics::read_trace
