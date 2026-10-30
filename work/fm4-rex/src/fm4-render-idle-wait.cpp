// The title render thread's wait for its next frame (fm4_render_thread_idle_wait,
// docs/analysis/career-season-soak.md, section 12).
//
// CRenderThread::ThreadProc (sub_822A90B0) calls CRenderThread::RunFrame
// (sub_822DB318) in a loop. RunFrame first recomputes the gamma ramp
// (UpdateCombineFade, sub_822A71A0, through SetGammaRamp and pow), then polls
// the deferred command queue at CRenderThread+2472 (sub_822C0840, under the
// queue's lock at +64) and runs a frame only when the game's main thread has
// queued one. With the queue empty it returns, and ThreadProc calls it again
// at once: between two frames the thread spins, 18 to 92 turns a frame.
//
// The main thread queues a frame in sub_82379E90: under the same lock it
// inserts the recorded buffer into the queue's list at +28 (sub_82940438, the
// call returning to 0x8237A1BC). The hooks here count a publication after that
// insert, note before each of RunFrame's polls how many there were and whether
// the poll found the queue empty, and with the switch let the next RunFrame
// first sleep until the next publication, at most 1 ms. The sleep is outside
// every lock (ThreadProc holds none between two RunFrame calls); the poll hook
// itself never waits, as it runs under the queue's lock, which the main thread
// needs to queue. A turn that did not poll (the loading frames' path) or found
// a frame does not wait.
#include <rex/hook.h>
#include <rex/system/gpu_progress.h>

#include <cstdint>

namespace {

namespace progress = rex::system::gpu_progress;

// RunFrame's two polls of the queue: their return addresses.
constexpr uint32_t kRunFramePolls[] = {0x822DB85C, 0x822DB95C};
// The main thread's insert of a frame into the queue's list (in sub_82379E90).
constexpr uint32_t kQueueInsert = 0x8237A1BC;
constexpr uint32_t kMaxWaitUs = 1000;

// The render thread's state between two RunFrame calls.
thread_local bool found_empty = false;
thread_local uint32_t seen_before_poll = 0;

bool IsRunFramePoll(uint32_t lr) {
  return lr == kRunFramePolls[0] || lr == kRunFramePolls[1];
}

}  // namespace

// Called by the RunFrame hook (fm4-guest-hooks.cpp) before the title's RunFrame.
void FM4RenderIdleWait() {
  if (found_empty && progress::RenderThreadIdleWait()) {
    progress::WaitFrameQueued(seen_before_poll, kMaxWaitUs);
  }
  found_empty = false;
}

REX_EXTERN(__imp__sub_822C0840);
REX_HOOK_RAW(sub_822C0840) {  // The render queue's poll: true when a frame is ready.
  const uint32_t lr = uint32_t(ctx.lr);
  // Read before the poll takes the queue's lock: an insert the poll does not see
  // comes after it and counts.
  const uint32_t seen = progress::FrameQueuePublications();
  __imp__sub_822C0840(ctx, base);
  if (IsRunFramePoll(lr)) {
    found_empty = (ctx.r3.u32 & 0xFF) == 0;
    seen_before_poll = seen;
  }
}

REX_EXTERN(__imp__sub_82940438);
REX_HOOK_RAW(sub_82940438) {  // A list insert; from kQueueInsert, a frame queued.
  const uint32_t lr = uint32_t(ctx.lr);
  __imp__sub_82940438(ctx, base);
  if (lr == kQueueInsert) progress::PublishFrameQueued();
}
