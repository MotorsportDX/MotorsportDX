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
// trace: their count, their time, how many ended at the timeout, and the
// publications since the last call.
struct WaitStats {
  uint32_t waits = 0;
  uint32_t timeouts = 0;
  uint64_t ns = 0;
  uint32_t publications = 0;
};
WaitStats TakeWaitStats();

// The title render thread's frame queue (docs/analysis/career-season-soak.md,
// section 12). Between two frames the title's render thread turns its frame loop
// without waiting: it polls the queue that the game's main thread fills with
// each frame's commands, and recomputes the gamma ramp on every turn. The
// title's hooks count a publication after each push into that queue, and with
// fm4_render_thread_idle_wait a turn that found the queue empty first sleeps
// until the next push, at most a given time.
void PublishFrameQueued();
uint32_t FrameQueuePublications();
// Sleeps until a push after `seen` (from FrameQueuePublications), or at most
// `timeout_us`. Returns false on the timeout.
bool WaitFrameQueued(uint32_t seen, uint32_t timeout_us);
// fm4_render_thread_idle_wait.
bool RenderThreadIdleWait();
// The sleeps of WaitFrameQueued and the pushes since the last call.
WaitStats TakeIdleWaitStats();
// RunFrame's path without a poll: counts a turn of its queue-empty branch (true)
// or of its frame-index branch (false), with the switch on or off.
void NoteRenderIdleBranch(bool queue_empty);
// The counts of NoteRenderIdleBranch since the last call.
struct IdleBranchStats {
  uint32_t queue_empty = 0;
  uint32_t frame_index = 0;
};
IdleBranchStats TakeIdleBranchStats();

}  // namespace rex::system::gpu_progress
