#pragma once
// The record queue of the host render thread (stage N1 of the native path,
// docs/analysis/d3d-hle.md section 14). Guest producers append records in
// order; the host render thread, the only thread that drives the backend,
// consumes them. In the first increment every PM4 writer of the title is on the
// fallback, so the records are spans of the PM4 ring (from one kick of the write
// pointer to the next) and calls (CallInThread).

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

#include <rex/thread.h>

namespace rex::graphics {

class HostRenderQueue {
 public:
  enum class Kind : uint32_t {
    kPm4Span,  // Ring indices [begin, end), wrapping at the end of the ring.
    kCall,
  };
  struct Record {
    uint64_t sequence = 0;
    Kind kind = Kind::kPm4Span;
    uint32_t begin = 0;
    uint32_t end = 0;
    std::function<void()> call;
  };
  // capacity: the queue length above which a new PM4 span merges into the last
  // record, when that is a span, instead of being appended. Calls are not
  // bounded.
  explicit HostRenderQueue(size_t capacity);

  // The ring was (re)initialized: the next span starts at this index.
  void ResetRing(uint32_t index);
  // The guest kicked the write pointer. Never blocks: the guest may be waiting
  // for a ring index that only the consumer publishes.
  void PushPm4Span(uint32_t end_index);
  void PushCall(std::function<void()> fn);

  // Takes the oldest record, false when the queue is empty. Consecutive PM4
  // spans are taken as one span. depth: the records queued before this one
  // was taken, this one included.
  bool Pop(Record& record, size_t& depth);
  bool empty() const;
  // Waits until a record is pushed, Wake is called or the timeout passes.
  void Wait(uint32_t timeout_ms);
  void Wake();

 private:
  const size_t capacity_;
  mutable std::mutex mutex_;
  std::deque<Record> records_;
  std::unique_ptr<rex::thread::Event> event_;
  uint64_t next_sequence_ = 0;
  uint32_t span_end_ = 0;
};

}  // namespace rex::graphics
