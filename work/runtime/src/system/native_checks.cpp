#include <rex/system/native_checks.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <unordered_map>

#include <rex/system/address_table.h>

namespace rex::system::native_checks {
namespace {
AddressTable<Entry> table;
std::atomic<bool> active{false};
std::atomic<uint64_t> overwritten{0};
std::atomic<uint64_t> dropped{0};
std::mutex late_mutex;
std::unordered_map<uint64_t, std::vector<std::string>> late_writes;
std::deque<uint64_t> late_order;
constexpr size_t kLateSequences = 8192;
}  // namespace

bool Active() { return active.load(std::memory_order_relaxed); }

void Record(uint32_t packet_address, Entry&& entry) {
  if (table.Put(packet_address, std::move(entry))) {
    overwritten.fetch_add(1, std::memory_order_relaxed);
  }
  active.store(true, std::memory_order_relaxed);
}

bool Take(uint32_t packet_address, Entry* entry) {
  return Active() && table.Take(packet_address, entry);
}

bool Pending(uint32_t packet_address, uint64_t sequence) {
  bool pending = false;
  table.Peek(packet_address, [&](const Entry& entry) { pending = entry.sequence == sequence; });
  return pending;
}

void NoteLateWrite(uint64_t sequence, std::string text) {
  std::lock_guard<std::mutex> lock(late_mutex);
  auto [it, inserted] = late_writes.try_emplace(sequence);
  if (inserted) {
    late_order.push_back(sequence);
    while (late_order.size() > kLateSequences) {
      late_writes.erase(late_order.front());
      late_order.pop_front();
    }
  }
  if (it->second.size() < 16) it->second.push_back(std::move(text));
}

std::vector<std::string> TakeLateWrites(uint64_t sequence) {
  std::lock_guard<std::mutex> lock(late_mutex);
  auto it = late_writes.find(sequence);
  if (it == late_writes.end()) return {};
  std::vector<std::string> notes = std::move(it->second);
  late_writes.erase(it);
  return notes;
}

uint64_t TakeOverwritten() { return overwritten.exchange(0, std::memory_order_relaxed); }

void CountDropped() { dropped.fetch_add(1, std::memory_order_relaxed); }

uint64_t TakeDropped() { return dropped.exchange(0, std::memory_order_relaxed); }

}  // namespace rex::system::native_checks
