// The host render thread of the native path, stage N1 of
// docs/analysis/d3d-hle.md (section 14): with host_render_thread it replaces
// the command processor thread. The guest's kicks of the ring write pointer
// and CallInThread become records in one bounded queue, and this thread, the
// only one that drives the backend, executes them in order and makes every
// guest-visible write the command processor thread made (the consumed ring
// index after each record, and through the packet handlers the fences, the
// swap counter, occlusion reports, waits and the swap). Every PM4 writer of the
// title is still on the fallback, so the PM4 itself is unchanged.

#include <algorithm>
#include <chrono>

#include <rex/cvar.h>
#include <rex/graphics/command_processor.h>
#include <rex/graphics/host_render_queue.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/system/xthread.h>

#include "util/cp_sampler.h"
#include "util/perf_trace.h"

REXCVAR_DEFINE_BOOL(host_render_thread, false, "GPU",
                    "Stage N1 of the native path: a host render thread consumes the guest's "
                    "commands from a record queue instead of the command processor thread "
                    "polling the ring (docs/analysis/d3d-hle.md, section 14)");

REXCVAR_DEFINE_INT32(host_render_queue_capacity, 256, "GPU",
                     "Records the host render thread's queue holds before a new PM4 span "
                     "merges into the last one")
    .range(1, 65536);

namespace rex::graphics {

HostRenderQueue::HostRenderQueue(size_t capacity)
    : capacity_(std::max<size_t>(1, capacity)),
      event_(rex::thread::Event::CreateAutoResetEvent(false)) {}

void HostRenderQueue::ResetRing(uint32_t index) {
  std::lock_guard<std::mutex> lock(mutex_);
  span_end_ = index;
}

void HostRenderQueue::PushPm4Span(uint32_t end_index) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (end_index == span_end_) {
      return;
    }
    if (records_.size() >= capacity_ && records_.back().kind == Kind::kPm4Span) {
      // The guest never writes past the published read index, so the merged
      // span stays shorter than the ring.
      records_.back().end = end_index;
      perf_trace::CountHostMergedSpan();
    } else {
      Record& record = records_.emplace_back();
      record.sequence = next_sequence_++;
      record.kind = Kind::kPm4Span;
      record.begin = span_end_;
      record.end = end_index;
    }
    span_end_ = end_index;
  }
  event_->Set();
}

void HostRenderQueue::PushCall(std::function<void()> fn) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    Record& record = records_.emplace_back();
    record.sequence = next_sequence_++;
    record.kind = Kind::kCall;
    record.call = std::move(fn);
  }
  event_->Set();
}

bool HostRenderQueue::Pop(Record& record, size_t& depth) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (records_.empty()) {
    return false;
  }
  depth = records_.size();
  record = std::move(records_.front());
  records_.pop_front();
  return true;
}

bool HostRenderQueue::empty() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return records_.empty();
}

void HostRenderQueue::Wait(uint32_t timeout_ms) {
  rex::thread::Wait(event_.get(), false, std::chrono::milliseconds(timeout_ms));
}

void HostRenderQueue::Wake() { event_->Set(); }

bool CommandProcessor::HostRenderThreadEnabled() { return REXCVAR_GET(host_render_thread); }

void CommandProcessor::CreateHostRenderQueue() {
  host_render_queue_ =
      std::make_unique<HostRenderQueue>(size_t(REXCVAR_GET(host_render_queue_capacity)));
  perf_trace::host_render.store(true, std::memory_order_relaxed);
  REXGPU_INFO("Host render thread: on (stage N1), queue capacity {}",
              REXCVAR_GET(host_render_queue_capacity));
}

void CommandProcessor::HostRenderThreadMain() {
  cp_sampler::Start();
  if (!SetupContext()) {
    rex::FatalError("Unable to setup command processor internal state");
    return;
  }

  HostRenderQueue::Record record;
  size_t depth = 0;
  while (worker_running_) {
    if (!host_render_queue_->Pop(record, depth)) {
      // Nothing queued: wait on the queue's event instead of spinning. While
      // the backend has work the guest may be waiting for (occlusion
      // snapshots), poll it every millisecond.
      const uint64_t perf_start = perf_trace::Now();
      PrepareForWait();
      while (worker_running_ && host_render_queue_->empty()) {
        host_render_queue_->Wait(PollIdleWork() ? 1 : 100);
      }
      ReturnFromWait();
      perf_trace::AddWait(perf_start, perf_trace::Wait::kGuestIdle);
      continue;
    }
    perf_trace::CountHostRecord(depth);
    switch (record.kind) {
      case HostRenderQueue::Kind::kCall:
        record.call();
        record.call = nullptr;
        break;
      case HostRenderQueue::Kind::kPm4Span:
        read_ptr_index_ = ExecutePrimaryBuffer(record.begin, record.end);
        if (read_ptr_writeback_ptr_) {
          memory::store_and_swap<uint32_t>(memory_->TranslatePhysical(read_ptr_writeback_ptr_),
                                           read_ptr_index_);
        }
        break;
    }
  }

  ShutdownContext();
}

}  // namespace rex::graphics
