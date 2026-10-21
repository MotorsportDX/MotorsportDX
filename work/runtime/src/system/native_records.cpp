#include <rex/system/native_records.h>

#include <atomic>

#include <rex/system/address_table.h>

namespace rex::system::native_records {
namespace {
AddressTable<Record> table;
std::atomic<bool> enabled{false};
std::atomic<uint64_t> overwritten{0};
}  // namespace

void SetEnabled(bool value) { enabled.store(value, std::memory_order_relaxed); }

bool Enabled() { return enabled.load(std::memory_order_relaxed); }

void Put(uint32_t marker_address, Record&& record) {
  if (table.Put(marker_address, std::move(record))) {
    overwritten.fetch_add(1, std::memory_order_relaxed);
  }
}

bool MayHave(uint32_t marker_address) { return table.MayHave(marker_address); }

bool Take(uint32_t marker_address, Record* record) { return table.Take(marker_address, record); }

uint64_t TakeOverwritten() { return overwritten.exchange(0, std::memory_order_relaxed); }

}  // namespace rex::system::native_records
