#include <rex/system/native_checks.h>

#include <atomic>
#include <mutex>
#include <unordered_map>

namespace rex::system::native_checks {
namespace {
constexpr uint32_t kShards = 16;
struct Shard {
  std::mutex mutex;
  std::unordered_map<uint32_t, Entry> entries;
};
Shard shards[kShards];
std::atomic<bool> active{false};
std::atomic<uint64_t> overwritten{0};
std::atomic<uint64_t> dropped{0};

Shard& ShardOf(uint32_t packet_address) { return shards[(packet_address >> 2) % kShards]; }
}  // namespace

bool Active() { return active.load(std::memory_order_relaxed); }

void Record(uint32_t packet_address, Entry&& entry) {
  Shard& shard = ShardOf(packet_address);
  {
    std::lock_guard<std::mutex> lock(shard.mutex);
    auto [it, inserted] = shard.entries.try_emplace(packet_address);
    if (!inserted) {
      overwritten.fetch_add(1, std::memory_order_relaxed);
    }
    it->second = std::move(entry);
  }
  active.store(true, std::memory_order_relaxed);
}

bool Take(uint32_t packet_address, Entry* entry) {
  if (!Active()) return false;
  Shard& shard = ShardOf(packet_address);
  std::lock_guard<std::mutex> lock(shard.mutex);
  auto it = shard.entries.find(packet_address);
  if (it == shard.entries.end()) return false;
  *entry = std::move(it->second);
  shard.entries.erase(it);
  return true;
}

uint64_t TakeOverwritten() { return overwritten.exchange(0, std::memory_order_relaxed); }

void CountDropped() { dropped.fetch_add(1, std::memory_order_relaxed); }

uint64_t TakeDropped() { return dropped.exchange(0, std::memory_order_relaxed); }

}  // namespace rex::system::native_checks
