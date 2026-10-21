#include <rex/system/native_checks.h>

#include <atomic>

#include <rex/system/address_table.h>

namespace rex::system::native_checks {
namespace {
AddressTable<Entry> table;
std::atomic<bool> active{false};
std::atomic<uint64_t> overwritten{0};
std::atomic<uint64_t> dropped{0};
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

uint64_t TakeOverwritten() { return overwritten.exchange(0, std::memory_order_relaxed); }

void CountDropped() { dropped.fetch_add(1, std::memory_order_relaxed); }

uint64_t TakeDropped() { return dropped.exchange(0, std::memory_order_relaxed); }

}  // namespace rex::system::native_checks
