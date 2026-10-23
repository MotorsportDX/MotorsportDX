#pragma once
// The publications of the guest-visible ring read pointer (stage N4 of
// docs/analysis/d3d-hle-n4.md, section 17.16), shared by the GPU plugin and the
// title's hooks. The command processor, or the host render thread, counts one
// after each write-back of the read pointer to guest memory. A guest wait on the
// read pointer (the title's ring index wait polls it in a loop without sleeping)
// can then sleep until the next publication instead of spinning on its core.

#include <cstdint>

namespace rex::system::gpu_progress {

// Counts a publication and wakes the waiters. Called after the write-back.
void PublishReadPointer();

// The number of publications so far.
uint32_t ReadPointerPublications();

// Sleeps until a publication after `seen` (from ReadPointerPublications), or at
// most `timeout_us`. Returns false on the timeout.
bool WaitReadPointerPublication(uint32_t seen, uint32_t timeout_us);

// guest_ring_wait_sleep: whether the title's ring index wait sleeps between its
// polls.
bool GuestRingWaitSleep();

// The sleeps of WaitReadPointerPublication since the last call, for the perf
// trace: their count, their time and how many ended at the timeout.
struct WaitStats {
  uint32_t waits = 0;
  uint32_t timeouts = 0;
  uint64_t ns = 0;
};
WaitStats TakeWaitStats();

}  // namespace rex::system::gpu_progress
