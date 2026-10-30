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
REXCVAR_DEFINE_BOOL(fm4_render_thread_idle_wait, false, "GPU",
                    "Let the title's render thread wait for the next queued frame, at most 1 ms, "
                    "when its frame loop found the queue empty, instead of turning the loop again "
                    "at once (docs/analysis/career-season-soak.md, section 12)");

namespace rex::system::gpu_progress {
namespace {
// A count that one side publishes and the other can sleep on. Waiters register
// before they check the count under the mutex, so a publication either is seen
// by the check or finds a waiter to wake.
struct Signal {
  std::atomic<uint32_t> publications{0};
  std::atomic<uint32_t> waiters{0};
  std::mutex mutex;
  std::condition_variable published;
  std::atomic<uint32_t> stat_waits{0}, stat_timeouts{0};
  std::atomic<uint64_t> stat_ns{0};
  std::atomic<uint32_t> taken_publications{0};

  void Publish() {
    publications.fetch_add(1, std::memory_order_seq_cst);
    if (waiters.load(std::memory_order_seq_cst)) {
      { std::lock_guard<std::mutex> lock(mutex); }
      published.notify_all();
    }
  }

  uint32_t Count() const { return publications.load(std::memory_order_seq_cst); }

  bool Wait(uint32_t seen, uint32_t timeout_us) {
    const auto start = std::chrono::steady_clock::now();
    waiters.fetch_add(1, std::memory_order_seq_cst);
    bool changed;
    {
      std::unique_lock<std::mutex> lock(mutex);
      changed = published.wait_for(lock, std::chrono::microseconds(timeout_us), [this, seen] {
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

  WaitStats TakeStats() {
    WaitStats stats;
    stats.waits = stat_waits.exchange(0, std::memory_order_relaxed);
    stats.timeouts = stat_timeouts.exchange(0, std::memory_order_relaxed);
    stats.ns = stat_ns.exchange(0, std::memory_order_relaxed);
    const uint32_t now = Count();
    stats.publications = now - taken_publications.exchange(now, std::memory_order_relaxed);
    return stats;
  }
};

Signal read_pointer;
Signal frame_queue;
std::atomic<uint32_t> branch_queue_empty{0}, branch_frame_index{0};
}  // namespace

void PublishReadPointer() { read_pointer.Publish(); }

uint32_t ReadPointerPublications() { return read_pointer.Count(); }

bool WaitReadPointerPublication(uint32_t seen, uint32_t timeout_us) {
  return read_pointer.Wait(seen, timeout_us);
}

bool GuestRingWaitSleep() { return REXCVAR_GET(guest_ring_wait_sleep); }

WaitStats TakeWaitStats() { return read_pointer.TakeStats(); }

void PublishFrameQueued() { frame_queue.Publish(); }

uint32_t FrameQueuePublications() { return frame_queue.Count(); }

bool WaitFrameQueued(uint32_t seen, uint32_t timeout_us) {
  return frame_queue.Wait(seen, timeout_us);
}

bool RenderThreadIdleWait() { return REXCVAR_GET(fm4_render_thread_idle_wait); }

WaitStats TakeIdleWaitStats() { return frame_queue.TakeStats(); }

void NoteRenderIdleBranch(bool queue_empty) {
  (queue_empty ? branch_queue_empty : branch_frame_index).fetch_add(1, std::memory_order_relaxed);
}

IdleBranchStats TakeIdleBranchStats() {
  IdleBranchStats stats;
  stats.queue_empty = branch_queue_empty.exchange(0, std::memory_order_relaxed);
  stats.frame_index = branch_frame_index.exchange(0, std::memory_order_relaxed);
  return stats;
}

}  // namespace rex::system::gpu_progress
