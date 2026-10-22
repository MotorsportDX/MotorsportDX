// Read trace of resolve readbacks (read_trace.h).

#include "read_trace.h"

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
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

// One destination (address and length of the resolve); its current arm runs
// from one readback to the next.
struct Destination {
  uint32_t address = 0, length = 0;
  bool armed = false, read = false, written = false;
  // Cumulative over the run.
  uint64_t readbacks = 0, read_arms = 0, unread_arms = 0, written_arms = 0;
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
};

struct Trace {
  // Taken by the fault handler under the global lock, never the other way.
  std::mutex mutex;
  std::unordered_map<uint64_t, uint32_t> tags;
  std::vector<Destination> destinations;
  Window window;
  uint32_t windows = 0;
  std::string path;
};

std::atomic<Trace*> trace{nullptr};

double Seconds() {
  perf_trace::State* state = perf_trace::Get();
  return state ? double(perf_trace::Now() - state->origin) / 1e9 : 0.0;
}

std::string ReaderName() {
  rex::thread::Thread* thread = rex::thread::Thread::GetCurrentThread();
  if (thread && !thread->name().empty()) {
    return thread->name();
  }
  return fmt::format("thread {:X}", rex::thread::current_thread_id());
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

void OnFault(void* context, uint32_t physical_address, uint32_t tag,
             memory::Memory::ReadTraceEvent event) {
  auto& t = *static_cast<Trace*>(context);
  std::lock_guard<std::mutex> lock(t.mutex);
  if (tag >= t.destinations.size()) return;
  Destination& d = t.destinations[tag];
  if (event == memory::Memory::ReadTraceEvent::kLost) {
    ++d.lost;
    ++t.window.lost;
    return;
  }
  const bool is_read = event == memory::Memory::ReadTraceEvent::kRead;
  if (is_read) {
    ++d.read_faults;
    ++t.window.read_faults;
  } else {
    ++d.write_faults;
    ++t.window.write_faults;
  }
  if (!d.armed || d.read || d.written) return;
  if (!is_read) {
    d.written = true;
    return;
  }
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

void WriteDestinations(Trace& t, double seconds) {
  std::ofstream output(t.path, std::ios::trunc);
  output << "{\"seconds\":" << seconds << ",\"destinations\":[";
  for (size_t i = 0; i < t.destinations.size(); ++i) {
    const Destination& d = t.destinations[i];
    output << (i ? "," : "") << "\n{\"address\":" << d.address << ",\"length\":" << d.length
           << ",\"readbacks\":" << d.readbacks << ",\"read\":" << d.read_arms
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

bool Enabled(memory::Memory& memory) {
  static const bool enabled = [&memory]() {
    if (!REXCVAR_GET(readback_read_trace) || !perf_trace::Get()) return false;
    auto* t = new Trace;
    t->path = std::string(std::getenv("REX_PERF_TRACE_FILE")) + ".reads.json";
    trace.store(t, std::memory_order_release);
    memory.SetReadTraceHandler(OnFault, t);
    return true;
  }();
  return enabled;
}

void OnReadback(memory::Memory& memory, uint32_t address, uint32_t length) {
  Trace* t = trace.load(std::memory_order_acquire);
  if (!t || !length) return;
  uint32_t tag;
  {
    std::lock_guard<std::mutex> lock(t->mutex);
    const uint64_t key = (uint64_t(address) << 32) | length;
    auto [it, inserted] = t->tags.emplace(key, uint32_t(t->destinations.size()));
    if (inserted) {
      Destination& d = t->destinations.emplace_back();
      d.address = address;
      d.length = length;
      d.first_s = Seconds();
    }
    tag = it->second;
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

void WriteWindow(std::ostream& output, double seconds) {
  Trace* t = trace.load(std::memory_order_acquire);
  if (!t) return;
  std::lock_guard<std::mutex> lock(t->mutex);
  const Window& w = t->window;
  output << "},\"read_trace\":{\"readbacks\":" << w.readbacks
         << ",\"readback_kb\":" << (w.readback_bytes >> 10) << ",\"read\":" << w.read_arms
         << ",\"unread\":" << w.unread_arms << ",\"written_first\":" << w.written_arms
         << ",\"read_kb\":" << (w.read_bytes >> 10) << ",\"unread_kb\":" << (w.unread_bytes >> 10)
         << ",\"written_kb\":" << (w.written_bytes >> 10) << ",\"read_faults\":" << w.read_faults
         << ",\"write_faults\":" << w.write_faults << ",\"guest_first\":" << w.guest_first
         << ",\"host_first\":" << w.host_first << ",\"armed_pages\":" << w.armed_pages
         << ",\"kept_pages\":" << w.kept_pages << ",\"lost\":" << w.lost;
  t->window = Window{};
  if (++t->windows % 10 == 0) {
    WriteDestinations(*t, seconds);
  }
}

}  // namespace rex::graphics::read_trace
