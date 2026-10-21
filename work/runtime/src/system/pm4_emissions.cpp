#include <rex/system/pm4_emissions.h>

#include <atomic>
#include <mutex>
#include <unordered_map>

namespace rex::system::pm4_emissions {
namespace {
constexpr uint32_t kShards = 16;
struct Shard {
  std::mutex mutex;
  std::unordered_map<uint32_t, Entry> entries;
};
Shard shards[uint32_t(Kind::kCount)][kShards];
std::atomic<bool> active{false};

Shard& ShardOf(Kind kind, uint32_t packet_address) {
  return shards[uint32_t(kind)][(packet_address >> 2) % kShards];
}
}  // namespace

bool Active() { return active.load(std::memory_order_relaxed); }

void Record(Kind kind, uint32_t packet_address, const Entry& entry) {
  if (kind >= Kind::kCount) return;
  Shard& shard = ShardOf(kind, packet_address);
  {
    std::lock_guard<std::mutex> lock(shard.mutex);
    shard.entries[packet_address] = entry;
  }
  active.store(true, std::memory_order_relaxed);
}

bool Find(Kind kind, uint32_t packet_address, Entry* entry) {
  if (kind >= Kind::kCount || !Active()) return false;
  Shard& shard = ShardOf(kind, packet_address);
  std::lock_guard<std::mutex> lock(shard.mutex);
  auto it = shard.entries.find(packet_address);
  if (it == shard.entries.end()) return false;
  *entry = it->second;
  return true;
}

}  // namespace rex::system::pm4_emissions
