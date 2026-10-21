// The converted PM4 writers of the native path, stage N1 increment 3 of
// docs/analysis/d3d-hle.md (section 14.9). The hooks are in
// fm4-guest-hooks.cpp. While the GPU plugin enables the records
// (host_render_native with host_render_thread, rex/system/native_records.h), a
// dirty state writer builds its register writes from its arguments, writes a
// two-dword NOP marker at the device's write pointer instead of its PM4, and
// stores the record under the marker's guest physical address; the host render
// thread applies it at the marker. The original body runs instead while the
// device records a command buffer (that PM4 is replayed later) or when the
// segment has no room for the marker.
//
// With the identity check (FM4_NATIVE_CHECK, fm4-native-check.cpp) the
// original body also runs into a scratch segment of the thread, and its PM4
// goes to the check under the marker's address, so the runtime compares the
// record with the PM4 it replaces.
#include "fm4-guest-hooks.h"

#include <rex/memory.h>
#include <rex/system/kernel_state.h>
#include <rex/system/native_checks.h>
#include <rex/system/native_records.h>
#include <rex/system/xmemory.h>

#include <atomic>
#include <cstring>

namespace fm4::hooks {
namespace {
uint8_t* Host(uint8_t* base, uint32_t address) {
  return base + address + (address >= 0xE0000000u ? 0x1000u : 0u);
}
uint32_t Load32(uint8_t* base, uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(Host(base, address));
}
void Store32(uint8_t* base, uint32_t address, uint32_t value) {
  rex::memory::store_and_swap<uint32_t>(Host(base, address), value);
}
// The XDK's virtual-to-physical conversion of command buffer pointers.
uint32_t Physical(uint32_t address) {
  return (address & 0x1FFFFFFF) + ((((address >> 20) + 0x200) & 0x1000));
}

std::atomic<uint64_t> sequence{0};

// Register writes of a dirty mask (the most significant bit first): each set
// bit i writes `per_bit` registers from first + per_bit * i, read from source
// + 4 * per_bit * i, in guest byte order; runs of set bits stay together, as
// the writers' type-0 packets do.
void AddMask(rex::system::native_records::Record& record, uint8_t* base, uint64_t mask,
             uint32_t first, uint32_t source, uint32_t per_bit) {
  uint32_t bit = 0;
  while (mask) {
    const uint32_t skip = uint32_t(__builtin_clzll(mask));
    bit += skip;
    mask <<= skip;
    const uint32_t run = ~mask ? uint32_t(__builtin_clzll(~mask)) : 64;
    const uint32_t count = run * per_bit;
    record.runs.emplace_back(first + bit * per_bit, count);
    const size_t at = record.values.size();
    record.values.resize(at + count);
    std::memcpy(record.values.data() + at, Host(base, source + bit * per_bit * 4),
                size_t(count) * 4);
    bit += run;
    mask = run < 64 ? mask << run : 0;
  }
}

// The scratch segment of the thread for the check: 64 KB of guest memory, so
// any dirty mask fits and the writer never calls the chunked writer or the
// segment overflow.
constexpr uint32_t kScratchBytes = 64 * 1024;
uint32_t Scratch() {
  thread_local uint32_t scratch = 0;
  if (!scratch) {
    scratch = rex::system::kernel_state()->memory()->SystemHeapAlloc(kScratchBytes, 0x100);
  }
  return scratch;
}
}  // namespace

bool NativeConvert(NativeWriter writer, PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  namespace records = rex::system::native_records;
  if (!records::Enabled()) return false;
  const uint32_t device = ctx.r3.u32;
  if (Load32(base, device + 13904)) return false;  // Recording: keep the PM4.
  const uint32_t pointer = Load32(base, device + 48);
  const uint32_t end = Load32(base, device + 52);
  // The writers write while the next dword is below the segment end.
  if (pointer + 8 >= end) return false;

  records::Record record;
  const uint64_t mask = ctx.r4.u64;
  uint32_t address = 0;
  switch (writer) {
    case NativeWriter::kRegisters:
      address = 0x823114F8;
      AddMask(record, base, mask, ctx.r5.u32, ctx.r6.u32, 1);
      break;
    case NativeWriter::kFloatConstants:
      address = 0x82310E78;
      AddMask(record, base, mask, ctx.r5.u32, ctx.r6.u32, 16);
      break;
    case NativeWriter::kFetchConstants: {
      address = 0x8230FA40;
      AddMask(record, base, mask, 0x4800, device + 1152, 6);
      // The type-0 write that ends the fetch constants.
      record.runs.emplace_back(0x5000, 3);
      for (uint32_t value : {0u, 0x25000u, 0u}) {
        uint32_t swapped;
        rex::memory::store_and_swap<uint32_t>(&swapped, value);
        record.values.push_back(swapped);
      }
    } break;
  }
  record.writer = address;
  record.sequence = sequence.fetch_add(1, std::memory_order_relaxed);

  // The PM4 for the check: the original body into the scratch segment.
  const bool check = NativeCheckEnabled();
  if (check) {
    const uint32_t scratch = Scratch();
    if (scratch) {
      Store32(base, device + 48, scratch);
      Store32(base, device + 52, scratch + kScratchBytes);
      original(ctx, base);
      const uint32_t written = Load32(base, device + 48);
      Store32(base, device + 48, pointer);
      Store32(base, device + 52, end);
      rex::system::native_checks::Entry entry;
      entry.writer = address;
      entry.sequence = record.sequence;
      entry.marker = true;
      if (written > scratch && written - scratch < kScratchBytes) {
        entry.dwords.resize((written - scratch) / 4);
        std::memcpy(entry.dwords.data(), Host(base, scratch + 4), entry.dwords.size() * 4);
      }
      rex::system::native_checks::Record(Physical(pointer + 4), std::move(entry));
    }
  }

  // The checked range of an outer writer ends before the marker and goes on
  // after it.
  NativeCheckSplitPre(base);
  Store32(base, pointer + 4, records::kMarkerHeader);
  Store32(base, pointer + 8, records::kMarkerTag);
  Store32(base, device + 48, pointer + 8);
  records::Put(Physical(pointer + 4), std::move(record));
  NativeCheckSplitPost(base);
  return true;
}

}  // namespace fm4::hooks
