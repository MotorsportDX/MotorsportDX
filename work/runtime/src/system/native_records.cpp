#include <rex/system/native_records.h>

#include <atomic>

#include <rex/system/address_table.h>

namespace rex::system::native_records {
namespace {
AddressTable<std::shared_ptr<const Record>> table;
std::atomic<bool> enabled{false};
}  // namespace

void SetEnabled(bool value) { enabled.store(value, std::memory_order_relaxed); }

bool Enabled() { return enabled.load(std::memory_order_relaxed); }

void Put(uint32_t marker_address, Record&& record) {
  table.Put(marker_address, std::make_shared<const Record>(std::move(record)));
}

bool MayHave(uint32_t marker_address) { return table.MayHave(marker_address); }

std::shared_ptr<const Record> Find(uint32_t marker_address) {
  std::shared_ptr<const Record> record;
  table.Find(marker_address, &record);
  return record;
}

}  // namespace rex::system::native_records
