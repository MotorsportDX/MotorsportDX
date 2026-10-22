#pragma once

// Read traps of resolve destinations, for stage N3 of
// docs/analysis/d3d-hle.md: the read trace (readback_read_trace, 16.4) and the
// readback on demand (readback_resolve_on_demand, 16.5).
//
// Read trace: with the perf trace, every resolve readback published to guest
// memory traps its destination pages (Memory::ArmReadTrace) until the next
// readback of the same destination. The first access to a trapped page
// restores its protection and is counted with the thread that made it;
// nothing waits, and the readback itself is unchanged.
//
// Readback on demand: a resolve skips its readback, its data staying on the
// GPU, and traps its destination with a pending readback of the latest resolve
// to it. The first guest access, read or write, asks the command processor for
// that readback and waits for it (without the global memory lock), so the data
// is in guest memory before the access continues; the command processor then
// restores the range's protection. The runtime's own writes into guest memory
// through the physical view (file reads) serve the pending readbacks of their
// range first (Memory::ServeReadTrace).
//
// Only the guest virtual views of physical memory are trapped: readers through
// Memory::TranslatePhysical (the runtime's uploads into shared memory, and the
// readback copy itself) are not seen.

#include <cstdint>
#include <ostream>
#include <vector>

namespace rex::memory {
class Memory;
}

namespace rex::graphics::read_trace {

// A resolve whose readback waits for the first access.
struct Demand {
  uint32_t tag = 0;
  uint32_t address = 0, length = 0;
  uint32_t pixel_size_log2 = 0;
  // With a draw resolution scale: the scaled resolve range the resolve made
  // current (unscaled start and length).
  uint32_t scaled_start = 0, scaled_length = 0;
};

// The command processor's side of the readback on demand.
class Servicer {
 public:
  // From any thread: run the requested readbacks soon (TakeRequests, then
  // CompleteRequests), on the command processor's thread.
  virtual void RequestReadbackService() = 0;
};

// On the command processor's thread at its setup: the read trace when
// readback_read_trace and the perf trace are on, the readback on demand when
// servicer is not null. Installs the fault handler, which stays for the life
// of the process.
void Configure(memory::Memory& memory, Servicer* servicer);
bool DemandEnabled();
// After a readback of [address, address + length) was published to guest
// memory, on the command processor's thread (read trace).
void OnReadback(memory::Memory& memory, uint32_t address, uint32_t length);
// A resolve whose readback is left to the first access (readback on demand),
// on the command processor's thread.
void OnDeferredResolve(memory::Memory& memory, Demand demand);
// On the command processor's thread: the requested readbacks, and after their
// data is in guest memory (copied false if it could not be), the end of their
// requests: their ranges get their protection back and the waiting threads
// continue.
std::vector<Demand> TakeRequests();
void CompleteRequests(memory::Memory& memory, const std::vector<Demand>& demands, bool copied,
                      uint64_t stall_ns);
// A reader of the physical view on the command processor's thread (shared
// memory uploads): counts reads of ranges whose readback is still pending,
// which would get stale data (expected none).
void CountPhysicalRead(uint32_t address, uint32_t length);
// Diagnostics: logs the first invalidations of shared memory over pending pages.
void NoteInvalidation(uint32_t address, uint32_t length);
// At the command processor's shutdown: drops the pending readbacks, restores
// every trapped range and lets waiting threads continue.
void Shutdown(memory::Memory& memory);
// The "read_trace" and "demand_readback" objects of a perf trace window (the
// caller has an object open and closes it), and every tenth window the
// destinations file.
void WriteWindow(std::ostream& output, double seconds);

}  // namespace rex::graphics::read_trace
