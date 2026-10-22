// Read traps of resolve destinations (read_trace.h).

#include "read_trace.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>
#include <rex/system/xthread.h>
#include <rex/thread.h>

#include "perf_trace.h"

REXCVAR_DEFINE_BOOL(readback_read_trace, false, "GPU",
                    "Diagnostics with the perf trace: trap the guest pages of each resolve "
                    "readback until the next readback of the same destination and count who "
                    "reads them (docs/analysis/d3d-hle.md 16.4); the readback is unchanged");

namespace rex::graphics::read_trace {

namespace {

// How long an access waits for its readback before it gives up (with an
// error) and continues with the data guest memory has.
constexpr auto kDemandTimeout = std::chrono::seconds(5);
constexpr uint32_t kPhysicalPages = (512u << 20) >> 12;

// One destination (address and length of the resolve); its current arm runs
// from one readback (or deferred resolve) to the next.
struct Destination {
  uint32_t address = 0, length = 0;
  bool armed = false, read = false, written = false;
  // Readback on demand: the latest resolve's readback is pending; it was
  // requested; requests made and served (an access waits until served reaches
  // the request it joined).
  bool pending = false, requested = false;
  uint64_t wanted = 0, served = 0;
  Demand demand;
  // Cumulative over the run.
  uint64_t readbacks = 0, deferred = 0, demand_served = 0;
  uint64_t read_arms = 0, unread_arms = 0, written_arms = 0;
  uint64_t read_faults = 0, write_faults = 0, lost = 0;
  uint64_t guest_first = 0, host_first = 0;
  std::map<std::string, uint64_t> first_readers;
  double first_s = 0, last_s = 0;
};

// Per perf trace window.
struct Window {
  uint64_t readbacks = 0, readback_bytes = 0;
  uint64_t read_arms = 0, unread_arms = 0, written_arms = 0;
  uint64_t read_bytes = 0, unread_bytes = 0, written_bytes = 0;
  uint64_t read_faults = 0, write_faults = 0, guest_first = 0, host_first = 0;
  uint64_t armed_pages = 0, kept_pages = 0, lost = 0;
  // Readback on demand.
  uint64_t deferred = 0, deferred_bytes = 0, requests = 0, served = 0, served_bytes = 0;
  uint64_t forced = 0, timeouts = 0, cp_faults = 0, failed = 0, lost_pending = 0;
  uint64_t stall_ns = 0, stall_max_ns = 0, service_ns = 0;
};

struct Trace {
  // Taken by the fault handler under the global lock, never the other way.
  std::mutex mutex;
  std::condition_variable served_cv;
  std::unordered_map<uint64_t, uint32_t> tags;
  // A deque: waiting threads keep references while it grows.
  std::deque<Destination> destinations;
  std::vector<uint32_t> requests;
  Window window;
  uint32_t windows = 0;
  bool trace = false, demand = false;
  Servicer* servicer = nullptr;
  uint32_t cp_thread = 0;
  std::string path;
  // Pages of pending readbacks, for CountPhysicalRead (set by deferred
  // resolves, cleared by their service; approximate where ranges overlap).
  std::unique_ptr<std::atomic<uint64_t>[]> pending_pages;
  std::atomic<uint64_t> pending_physical_reads{0}, pending_physical_reads_total{0};
  std::atomic<uint64_t> invalidated_pending{0};
};

std::atomic<Trace*> trace{nullptr};

double Seconds() {
  perf_trace::State* state = perf_trace::Get();
  return state ? double(perf_trace::Now() - state->origin) / 1e9 : 0.0;
}

uint64_t NowNs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
}

std::string ReaderName() {
  rex::thread::Thread* thread = rex::thread::Thread::GetCurrentThread();
  if (thread && !thread->name().empty()) {
    return thread->name();
  }
  return fmt::format("thread {:X}", rex::thread::current_thread_id());
}

void SetPendingPages(Trace& t, uint32_t address, uint32_t length, bool pending) {
  if (!length) return;
  const uint32_t first = (address & 0x1FFFFFFF) >> 12;
  const uint32_t last = std::min(((address & 0x1FFFFFFF) + length - 1) >> 12, kPhysicalPages - 1);
  for (uint32_t page = first; page <= last; ++page) {
    const uint64_t bit = uint64_t(1) << (page & 63);
    if (pending) {
      t.pending_pages[page >> 6].fetch_or(bit, std::memory_order_relaxed);
    } else {
      t.pending_pages[page >> 6].fetch_and(~bit, std::memory_order_relaxed);
    }
  }
}

// The destination of a resolve, with the mutex held.
uint32_t TagOf(Trace& t, uint32_t address, uint32_t length) {
  const uint64_t key = (uint64_t(address) << 32) | length;
  auto [it, inserted] = t.tags.emplace(key, uint32_t(t.destinations.size()));
  if (inserted) {
    Destination& d = t.destinations.emplace_back();
    d.address = address;
    d.length = length;
    d.first_s = Seconds();
  }
  return it->second;
}

// Ends the current arm of a destination, with the mutex held.
void Close(Trace& t, Destination& d) {
  if (!d.armed) return;
  d.armed = false;
  if (d.read) {
    ++d.read_arms;
    ++t.window.read_arms;
    t.window.read_bytes += d.length;
  } else if (d.written) {
    ++d.written_arms;
    ++t.window.written_arms;
    t.window.written_bytes += d.length;
  } else {
    ++d.unread_arms;
    ++t.window.unread_arms;
    t.window.unread_bytes += d.length;
  }
  d.read = d.written = false;
}

// Joins or makes the request of a pending destination, with the mutex held.
// Returns the served count to wait for; wake is set if the command processor
// has to be asked.
uint64_t Request(Trace& t, uint32_t tag, bool& wake) {
  Destination& d = t.destinations[tag];
  if (!d.requested) {
    d.requested = true;
    d.wanted = d.served + 1;
    t.requests.push_back(tag);
    ++t.window.requests;
    wake = true;
  }
  return d.wanted;
}

// Waits until the destinations reach their targets, without the global lock,
// with the mutex held in lock. Returns false on a timeout.
bool Wait(Trace& t, std::unique_lock<std::mutex>& lock,
          const std::vector<std::pair<uint32_t, uint64_t>>& targets) {
  const uint64_t start = NowNs();
  const bool done = t.served_cv.wait_for(lock, kDemandTimeout, [&]() {
    if (!t.servicer) return true;
    for (const auto& [tag, target] : targets) {
      if (t.destinations[tag].served < target) return false;
    }
    return true;
  });
  const uint64_t stall = NowNs() - start;
  t.window.stall_ns += stall;
  t.window.stall_max_ns = std::max(t.window.stall_max_ns, stall);
  if (!done) {
    ++t.window.timeouts;
    for (const auto& [tag, target] : targets) {
      Destination& d = t.destinations[tag];
      if (d.served < target) {
        REXGPU_ERROR("Readback on demand of {:08X} ({} bytes) timed out; the guest reads stale data",
                     d.address, d.length);
        d.pending = false;
      }
    }
  }
  return done;
}

bool OnFault(void* context, std::unique_lock<std::recursive_mutex>& global_lock,
             uint32_t physical_address, uint32_t tag, memory::Memory::ReadTraceEvent event) {
  auto& t = *static_cast<Trace*>(context);
  std::unique_lock<std::mutex> lock(t.mutex);
  if (tag >= t.destinations.size()) return true;
  Destination& d = t.destinations[tag];
  if (event == memory::Memory::ReadTraceEvent::kLost) {
    ++d.lost;
    ++t.window.lost;
    t.window.lost_pending += d.pending;
    return true;
  }
  const bool is_read = event == memory::Memory::ReadTraceEvent::kRead;
  if (is_read) {
    ++d.read_faults;
    ++t.window.read_faults;
  } else {
    ++d.write_faults;
    ++t.window.write_faults;
  }
  if (d.armed && !d.read && !d.written) {
    if (!is_read) {
      d.written = true;
    } else {
      d.read = true;
      if (rex::system::XThread::IsInThread()) {
        ++d.guest_first;
        ++t.window.guest_first;
      } else {
        ++d.host_first;
        ++t.window.host_first;
      }
      ++d.first_readers[ReaderName()];
    }
  }
  if (!d.pending || !t.servicer) {
    return true;
  }
  if (rex::thread::current_thread_id() == t.cp_thread) {
    // Only the command processor can issue the readback: it must never fault
    // here (it reads guest memory through the physical view).
    if (!t.window.cp_faults++) {
      REXGPU_ERROR("Readback on demand: the command processor accessed {:08X}, pending since a "
                   "resolve; it keeps stale data",
                   physical_address);
    }
    return true;
  }
  bool wake = false;
  const uint64_t target = Request(t, tag, wake);
  Servicer* servicer = t.servicer;
  lock.unlock();
  if (wake) {
    servicer->RequestReadbackService();
  }
  // Never wait for the GPU with the global lock: the command processor takes it.
  global_lock.unlock();
  lock.lock();
  const bool done = Wait(t, lock, {{tag, target}});
  lock.unlock();
  global_lock.lock();
  // Served: the command processor restored the range. A page that a newer
  // resolve trapped again faults again, for its own readback.
  return !done;
}

void OnServe(void* context, uint32_t address, uint32_t length, bool discard) {
  auto& t = *static_cast<Trace*>(context);
  if (!length) return;
  if (discard) {
    // Released: what a pending readback would bring no longer matters (a
    // later readback skips the pages the guest's new data invalidates).
    SetPendingPages(t, address & 0x1FFFFFFF, length, false);
    return;
  }
  std::unique_lock<std::mutex> lock(t.mutex);
  if (!t.servicer) return;
  const uint32_t start = address & 0x1FFFFFFF;
  std::vector<std::pair<uint32_t, uint64_t>> targets;
  bool wake = false;
  for (uint32_t tag = 0; tag < t.destinations.size(); ++tag) {
    const Destination& d = t.destinations[tag];
    if (d.pending && d.address < start + length && start < d.address + d.length) {
      targets.emplace_back(tag, Request(t, tag, wake));
    }
  }
  if (targets.empty()) return;
  ++t.window.forced;
  if (rex::thread::current_thread_id() == t.cp_thread) {
    if (!t.window.cp_faults++) {
      REXGPU_ERROR("Readback on demand: the command processor wrote {:08X}, pending since a "
                   "resolve",
                   address);
    }
    return;
  }
  Servicer* servicer = t.servicer;
  lock.unlock();
  if (wake) {
    servicer->RequestReadbackService();
  }
  lock.lock();
  Wait(t, lock, targets);
}

void WriteDestinations(Trace& t, double seconds) {
  std::ofstream output(t.path, std::ios::trunc);
  output << "{\"seconds\":" << seconds << ",\"destinations\":[";
  for (size_t i = 0; i < t.destinations.size(); ++i) {
    const Destination& d = t.destinations[i];
    output << (i ? "," : "") << "\n{\"address\":" << d.address << ",\"length\":" << d.length
           << ",\"readbacks\":" << d.readbacks << ",\"deferred\":" << d.deferred
           << ",\"demand_served\":" << d.demand_served << ",\"read\":" << d.read_arms
           << ",\"unread\":" << d.unread_arms << ",\"written_first\":" << d.written_arms
           << ",\"read_faults\":" << d.read_faults << ",\"write_faults\":" << d.write_faults
           << ",\"guest_first\":" << d.guest_first << ",\"host_first\":" << d.host_first
           << ",\"lost\":" << d.lost << ",\"first_s\":" << d.first_s
           << ",\"last_s\":" << d.last_s << ",\"first_readers\":{";
    bool first = true;
    for (const auto& [name, count] : d.first_readers) {
      std::string escaped;
      for (char c : name) {
        if (c == '"' || c == '\\') escaped += '\\';
        escaped += (static_cast<unsigned char>(c) < 0x20) ? ' ' : c;
      }
      output << (first ? "" : ",") << '"' << escaped << "\":" << count;
      first = false;
    }
    output << "}}";
  }
  output << "]}\n";
}

}  // namespace

void Configure(memory::Memory& memory, Servicer* servicer) {
  const bool trace_on = REXCVAR_GET(readback_read_trace) && perf_trace::Get();
  Trace* t = trace.load(std::memory_order_acquire);
  if (!t) {
    if (!trace_on && !servicer) return;
    t = new Trace;
    t->pending_pages = std::make_unique<std::atomic<uint64_t>[]>(kPhysicalPages / 64);
    if (trace_on) {
      t->path = std::string(std::getenv("REX_PERF_TRACE_FILE")) + ".reads.json";
    }
    trace.store(t, std::memory_order_release);
    memory.SetReadTraceHandler(OnFault, OnServe, t);
  }
  std::lock_guard<std::mutex> lock(t->mutex);
  t->trace = trace_on;
  t->demand = t->demand || servicer;
  t->servicer = servicer;
  t->cp_thread = rex::thread::current_thread_id();
}

bool DemandEnabled() {
  Trace* t = trace.load(std::memory_order_acquire);
  return t && t->servicer;
}

void OnReadback(memory::Memory& memory, uint32_t address, uint32_t length) {
  Trace* t = trace.load(std::memory_order_acquire);
  if (!t || !t->trace || !length) return;
  uint32_t tag;
  {
    std::lock_guard<std::mutex> lock(t->mutex);
    tag = TagOf(*t, address, length);
    Destination& d = t->destinations[tag];
    Close(*t, d);
    ++d.readbacks;
    d.last_s = Seconds();
    ++t->window.readbacks;
    t->window.readback_bytes += length;
  }
  // Not under the mutex: arming takes the global lock, and the fault handler
  // takes the mutex under the global lock.
  const auto result = memory.ArmReadTrace(address, length, tag);
  std::lock_guard<std::mutex> lock(t->mutex);
  Destination& d = t->destinations[tag];
  d.armed = true;
  d.lost += result.lost;
  t->window.armed_pages += result.armed;
  t->window.kept_pages += result.kept;
  t->window.lost += result.lost;
}

void OnDeferredResolve(memory::Memory& memory, Demand demand) {
  Trace* t = trace.load(std::memory_order_acquire);
  if (!t || !t->servicer || !demand.length) return;
  {
    std::lock_guard<std::mutex> lock(t->mutex);
    t->cp_thread = rex::thread::current_thread_id();
    demand.tag = TagOf(*t, demand.address, demand.length);
    Destination& d = t->destinations[demand.tag];
    Close(*t, d);
    ++d.deferred;
    d.last_s = Seconds();
    d.demand = demand;
    // Before the trap, so an access finds the readback it needs.
    d.pending = true;
    ++t->window.deferred;
    t->window.deferred_bytes += demand.length;
  }
  SetPendingPages(*t, demand.address, demand.length, true);
  const auto result = memory.ArmReadTrace(demand.address, demand.length, demand.tag);
  std::lock_guard<std::mutex> lock(t->mutex);
  Destination& d = t->destinations[demand.tag];
  d.armed = true;
  d.lost += result.lost;
  t->window.armed_pages += result.armed;
  t->window.kept_pages += result.kept;
  t->window.lost += result.lost;
}

std::vector<Demand> TakeRequests() {
  std::vector<Demand> demands;
  Trace* t = trace.load(std::memory_order_acquire);
  if (!t) return demands;
  std::lock_guard<std::mutex> lock(t->mutex);
  bool notify = false;
  for (uint32_t tag : t->requests) {
    Destination& d = t->destinations[tag];
    if (d.pending) {
      demands.push_back(d.demand);
    } else {
      // Given up (a timeout): nothing to read back.
      d.requested = false;
      d.served = d.wanted;
      notify = true;
    }
  }
  t->requests.clear();
  if (notify) t->served_cv.notify_all();
  return demands;
}

void CompleteRequests(memory::Memory& memory, const std::vector<Demand>& demands, bool copied,
                      uint64_t stall_ns) {
  Trace* t = trace.load(std::memory_order_acquire);
  if (!t || demands.empty()) return;
  // Not under the mutex (the global lock).
  for (const Demand& demand : demands) {
    memory.DisarmReadTrace(demand.address, demand.length);
    SetPendingPages(*t, demand.address, demand.length, false);
  }
  {
    std::lock_guard<std::mutex> lock(t->mutex);
    for (const Demand& demand : demands) {
      Destination& d = t->destinations[demand.tag];
      d.pending = false;
      d.requested = false;
      d.served = d.wanted;
      ++d.demand_served;
      ++t->window.served;
      t->window.served_bytes += demand.length;
    }
    t->window.failed += copied ? 0 : demands.size();
    t->window.service_ns += stall_ns;
  }
  t->served_cv.notify_all();
}

void CountPhysicalRead(uint32_t address, uint32_t length) {
  Trace* t = trace.load(std::memory_order_acquire);
  if (!t || !t->demand || !length) return;
  const uint32_t first = (address & 0x1FFFFFFF) >> 12;
  const uint32_t last = std::min(((address & 0x1FFFFFFF) + length - 1) >> 12, kPhysicalPages - 1);
  for (uint32_t page = first; page <= last; ++page) {
    if (t->pending_pages[page >> 6].load(std::memory_order_relaxed) & (uint64_t(1) << (page & 63))) {
      if (t->pending_physical_reads_total.fetch_add(1, std::memory_order_relaxed) < 16) {
        REXGPU_INFO("Readback on demand: shared memory uploads [{:08X}, +{:X}), page {:08X} pending",
                    address, length, page << 12);
      }
      t->pending_physical_reads.fetch_add(1, std::memory_order_relaxed);
      return;
    }
  }
}

void NoteInvalidation(uint32_t address, uint32_t length) {
  Trace* t = trace.load(std::memory_order_acquire);
  if (!t || !t->demand || !length) return;
  static std::atomic<uint32_t> logged{0};
  const uint32_t first = (address & 0x1FFFFFFF) >> 12;
  const uint32_t last = std::min(((address & 0x1FFFFFFF) + length - 1) >> 12, kPhysicalPages - 1);
  for (uint32_t page = first; page <= last; ++page) {
    if (t->pending_pages[page >> 6].load(std::memory_order_relaxed) & (uint64_t(1) << (page & 63))) {
      t->invalidated_pending.fetch_add(1, std::memory_order_relaxed);
      if (logged.fetch_add(1, std::memory_order_relaxed) < 16) {
        REXGPU_INFO("Readback on demand: invalidation [{:08X}, +{:X}) over pending page {:08X} "
                    "on {} ({})",
                    address, length, page << 12, ReaderName(),
                    rex::system::XThread::IsInThread() ? "guest" : "host");
      }
      return;
    }
  }
}

void Shutdown(memory::Memory& memory) {
  Trace* t = trace.load(std::memory_order_acquire);
  if (!t) return;
  std::vector<std::pair<uint32_t, uint32_t>> ranges;
  {
    std::lock_guard<std::mutex> lock(t->mutex);
    t->servicer = nullptr;
    t->requests.clear();
    for (Destination& d : t->destinations) {
      if (d.pending || d.armed) {
        ranges.emplace_back(d.address, d.length);
      }
      d.pending = d.requested = false;
      d.served = d.wanted;
    }
  }
  t->served_cv.notify_all();
  for (const auto& [address, length] : ranges) {
    memory.DisarmReadTrace(address, length);
  }
}

void WriteWindow(std::ostream& output, double seconds) {
  Trace* t = trace.load(std::memory_order_acquire);
  if (!t) return;
  std::lock_guard<std::mutex> lock(t->mutex);
  const Window& w = t->window;
  if (t->trace) {
    output << "},\"read_trace\":{\"readbacks\":" << w.readbacks
           << ",\"readback_kb\":" << (w.readback_bytes >> 10) << ",\"read\":" << w.read_arms
           << ",\"unread\":" << w.unread_arms << ",\"written_first\":" << w.written_arms
           << ",\"read_kb\":" << (w.read_bytes >> 10) << ",\"unread_kb\":" << (w.unread_bytes >> 10)
           << ",\"written_kb\":" << (w.written_bytes >> 10) << ",\"read_faults\":" << w.read_faults
           << ",\"write_faults\":" << w.write_faults << ",\"guest_first\":" << w.guest_first
           << ",\"host_first\":" << w.host_first << ",\"armed_pages\":" << w.armed_pages
           << ",\"kept_pages\":" << w.kept_pages << ",\"lost\":" << w.lost;
  }
  if (t->demand) {
    output << "},\"demand_readback\":{\"deferred\":" << w.deferred
           << ",\"deferred_kb\":" << (w.deferred_bytes >> 10) << ",\"requests\":" << w.requests
           << ",\"served\":" << w.served << ",\"served_kb\":" << (w.served_bytes >> 10)
           << ",\"forced\":" << w.forced << ",\"stall_ms\":" << double(w.stall_ns) / 1e6
           << ",\"stall_max_ms\":" << double(w.stall_max_ns) / 1e6
           << ",\"service_ms\":" << double(w.service_ns) / 1e6 << ",\"timeouts\":" << w.timeouts
           << ",\"cp_faults\":" << w.cp_faults << ",\"failed\":" << w.failed
           << ",\"lost\":" << w.lost << ",\"lost_pending\":" << w.lost_pending
           << ",\"physical_reads_pending\":"
           << t->pending_physical_reads.exchange(0, std::memory_order_relaxed)
           << ",\"invalidated_pending\":"
           << t->invalidated_pending.exchange(0, std::memory_order_relaxed);
  }
  t->window = Window{};
  if (t->trace && ++t->windows % 10 == 0) {
    WriteDestinations(*t, seconds);
  }
}

}  // namespace rex::graphics::read_trace
