#include <rex/system/native_records.h>

#include <memory>

namespace rex::system::native_records {
namespace {
std::atomic<bool> enabled{false};
std::atomic<uint64_t> next_sequence{0};
std::unique_ptr<Slot[]> slots;
std::atomic<uint32_t> kicks{0};
}  // namespace

void CountKick() { kicks.fetch_add(1, std::memory_order_release); }

uint32_t Kicks() { return kicks.load(std::memory_order_acquire); }

void SetEnabled(bool value) {
  if (value && !slots) {
    slots = std::make_unique<Slot[]>(kSlots);
  }
  enabled.store(value, std::memory_order_release);
}

bool Enabled() { return enabled.load(std::memory_order_acquire); }

Slot& Begin(uint64_t* sequence) {
  *sequence = next_sequence.fetch_add(1, std::memory_order_relaxed);
  Slot& slot = slots[*sequence & (kSlots - 1)];
  slot.sequence.store(kWriting, std::memory_order_relaxed);
  slot.record.runs.clear();
  slot.record.values.clear();
  return slot;
}

void Publish(Slot& slot, uint64_t sequence) {
  slot.sequence.store(sequence, std::memory_order_release);
}

Slot& SlotOf(uint32_t sequence) { return slots[sequence & (kSlots - 1)]; }

}  // namespace rex::system::native_records
