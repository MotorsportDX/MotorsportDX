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
//
// RunFrame has a second empty-queue path that does not poll: at 0x822DB70C it
// branches to loc_822DB9E4 when its frame index r22 is not past [this+2356] (the
// frame-index branch), or when [this+2892] is 2 (the race) and the queue's count
// (+128) is 0 (the queue-empty branch). That path calls Sleep(0, alertable)
// (sub_8238C778, returning to 0x822DBA08) and returns. With the switch, the
// queue-empty branch first sleeps until a frame is queued after the turn began,
// at most 1 ms, outside every lock (RunFrame left its critical section and the
// queue's lock before the branch); the frame-index branch, which waits on other
// progress, keeps the title's Sleep(0) alone. Both are counted, with the switch
// on or off.
#include <rex/hook.h>
#include <rex/memory.h>
#include <rex/system/gpu_progress.h>

#include <cstdint>

namespace {

namespace progress = rex::system::gpu_progress;

// RunFrame's two polls of the queue: their return addresses.
constexpr uint32_t kRunFramePolls[] = {0x822DB85C, 0x822DB95C};
// The main thread's insert of a frame into the queue's list (in sub_82379E90).
constexpr uint32_t kQueueInsert = 0x8237A1BC;
// RunFrame's Sleep(0) on its path without a poll.
constexpr uint32_t kRunFrameSleep = 0x822DBA08;
// CRenderThread: the queue, its count, and the frame index of the frame-index branch.
constexpr uint32_t kQueue = 2472;
constexpr uint32_t kQueueCount = kQueue + 128;
constexpr uint32_t kFrameIndex = 2356;
constexpr uint32_t kMaxWaitUs = 1000;

// The render thread's state between two RunFrame calls, and in the current one.
thread_local bool found_empty = false;
thread_local uint32_t seen_before_poll = 0;
thread_local uint32_t render_thread = 0;
thread_local uint32_t seen_at_turn = 0;

bool IsRunFramePoll(uint32_t lr) {
  return lr == kRunFramePolls[0] || lr == kRunFramePolls[1];
}

}  // namespace

// Called by the RunFrame hook (fm4-guest-hooks.cpp) before the title's RunFrame,
// with its CRenderThread (r3).
void FM4RenderIdleWait(uint32_t this_ptr) {
  if (found_empty && progress::RenderThreadIdleWait()) {
    progress::WaitFrameQueued(seen_before_poll, kMaxWaitUs);
  }
  found_empty = false;
  render_thread = this_ptr;
  seen_at_turn = progress::FrameQueuePublications();
}

REX_EXTERN(__imp__sub_8238C778);
REX_HOOK_RAW(sub_8238C778) {  // SleepEx(ms, alertable)
  if (uint32_t(ctx.lr) == kRunFrameSleep && render_thread) {
    // r22 is non-volatile: still RunFrame's frame index.
    const uint32_t index =
        rex::memory::load_and_swap<uint32_t>(base + render_thread + kFrameIndex);
    if (ctx.r22.u32 <= index) {
      progress::NoteRenderIdleBranch(false);
    } else {
      progress::NoteRenderIdleBranch(true);
      // A frame queued since the branch read the count needs no wait.
      if (progress::RenderThreadIdleWait() &&
          rex::memory::load_and_swap<uint32_t>(base + render_thread + kQueueCount) == 0) {
        progress::WaitFrameQueued(seen_at_turn, kMaxWaitUs);
      }
    }
  }
  __imp__sub_8238C778(ctx, base);
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
