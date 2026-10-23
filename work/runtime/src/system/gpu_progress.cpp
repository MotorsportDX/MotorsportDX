#include <rex/system/gpu_progress.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(guest_ring_wait_sleep, false, "GPU",
                    "Let the title's ring index wait sleep until the command processor publishes "
                    "the read pointer again, instead of polling it on its core (N4 of "
                    "docs/analysis/d3d-hle-n4.md, section 17.16)");

namespace rex::system::gpu_progress {
namespace {
std::atomic<uint32_t> publications{0};
// Waiters register before they check the count under the mutex, so a
// publication either is seen by the check or finds a waiter to wake.
std::atomic<uint32_t> waiters{0};
std::mutex mutex;
std::condition_variable published;
std::atomic<uint32_t> stat_waits{0}, stat_timeouts{0};
std::atomic<uint64_t> stat_ns{0};
}  // namespace

void PublishReadPointer() {
  publications.fetch_add(1, std::memory_order_seq_cst);
  if (waiters.load(std::memory_order_seq_cst)) {
    { std::lock_guard<std::mutex> lock(mutex); }
    published.notify_all();
  }
}

uint32_t ReadPointerPublications() { return publications.load(std::memory_order_seq_cst); }

bool WaitReadPointerPublication(uint32_t seen, uint32_t timeout_us) {
  const auto start = std::chrono::steady_clock::now();
  waiters.fetch_add(1, std::memory_order_seq_cst);
  bool changed;
  {
    std::unique_lock<std::mutex> lock(mutex);
    changed = published.wait_for(lock, std::chrono::microseconds(timeout_us), [seen] {
      return publications.load(std::memory_order_seq_cst) != seen;
    });
  }
  waiters.fetch_sub(1, std::memory_order_seq_cst);
  stat_waits.fetch_add(1, std::memory_order_relaxed);
  stat_timeouts.fetch_add(changed ? 0 : 1, std::memory_order_relaxed);
  stat_ns.fetch_add(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::steady_clock::now() - start)
                                 .count()),
                    std::memory_order_relaxed);
  return changed;
}

bool GuestRingWaitSleep() { return REXCVAR_GET(guest_ring_wait_sleep); }

WaitStats TakeWaitStats() {
  WaitStats stats;
  stats.waits = stat_waits.exchange(0, std::memory_order_relaxed);
  stats.timeouts = stat_timeouts.exchange(0, std::memory_order_relaxed);
  stats.ns = stat_ns.exchange(0, std::memory_order_relaxed);
  return stats;
}

}  // namespace rex::system::gpu_progress
