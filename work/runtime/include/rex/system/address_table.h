#pragma once
// A table of entries keyed by a guest physical address, shared by the title's
// hooks (producers, any thread) and the host render thread (the consumer, which
// takes an entry when it reaches its address), for the native path of
// docs/analysis/d3d-hle.md (sections 14.7 and 14.9). A filter of one bit per
// dword of the 512 MB physical space (16 MB, allocated with the first entry)
// lets the consumer test an address before every packet without a lock.

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace rex::system {

template <typename T>
class AddressTable {
 public:
  // True when an entry exists at the address, tested without a lock (the
  // filter has one bit per dword, and an address holds one entry).
  bool MayHave(uint32_t address) const {
    const std::atomic<uint64_t>* filter = filter_.load(std::memory_order_acquire);
    if (!filter) return false;
    const uint32_t dword = (address & 0x1FFFFFFF) >> 2;
    return (filter[dword >> 6].load(std::memory_order_relaxed) >> (dword & 63)) & 1;
  }

  // Returns true when an entry at the address was replaced.
  bool Put(uint32_t address, T&& value) {
    std::call_once(allocate_, [this] {
      filter_.store(new std::atomic<uint64_t>[kFilterWords](), std::memory_order_release);
    });
    Shard& shard = ShardOf(address);
    std::lock_guard<std::mutex> lock(shard.mutex);
    auto [it, inserted] = shard.entries.try_emplace(address);
    it->second = std::move(value);
    const uint32_t dword = (address & 0x1FFFFFFF) >> 2;
    filter_.load(std::memory_order_relaxed)[dword >> 6].fetch_or(uint64_t(1) << (dword & 63),
                                                                std::memory_order_release);
    return !inserted;
  }

  bool Take(uint32_t address, T* value) {
    if (!MayHave(address)) return false;
    Shard& shard = ShardOf(address);
    std::lock_guard<std::mutex> lock(shard.mutex);
    auto it = shard.entries.find(address);
    if (it == shard.entries.end()) return false;
    *value = std::move(it->second);
    shard.entries.erase(it);
    const uint32_t dword = (address & 0x1FFFFFFF) >> 2;
    filter_.load(std::memory_order_relaxed)[dword >> 6].fetch_and(~(uint64_t(1) << (dword & 63)),
                                                                 std::memory_order_relaxed);
    return true;
  }

  bool Allocated() const { return filter_.load(std::memory_order_relaxed) != nullptr; }

 private:
  static constexpr uint32_t kShards = 16;
  static constexpr size_t kFilterWords = (size_t(512) << 20) / 4 / 64;
  struct Shard {
    std::mutex mutex;
    std::unordered_map<uint32_t, T> entries;
  };
  Shard& ShardOf(uint32_t address) { return shards_[(address >> 2) % kShards]; }

  Shard shards_[kShards];
  std::once_flag allocate_;
  std::atomic<std::atomic<uint64_t>*> filter_{nullptr};
};

}  // namespace rex::system
