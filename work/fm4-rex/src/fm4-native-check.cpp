// The producer side of the identity check of the native path, stage N1
// increment 2 of docs/analysis/d3d-hle.md (section 14.7), off by default. The
// hooks are in fm4-guest-hooks.cpp.
//
// FM4_NATIVE_CHECK=1: the hooked PM4 writers (the dirty state writers, the draw
// flush and the draw writers) note the device's ring write pointer (device+48,
// the last dword written) before the original body and copy the PM4 between it
// and the pointer after the body into rex/system/native_checks.h, keyed by the
// guest physical address of the first packet. Only the outermost hooked writer
// of a thread copies (the draw writers call the flush and the dirty writers).
// A segment overflow inside the writer (sub_822FEF08) ends the range before the
// overflow, and a new one starts after it. Nothing is copied while the device
// records a command buffer (device+13904): that PM4 is not in the ring. The
// runtime compares the copies with what it executes when host_render_check is
// set.
#include "fm4-guest-hooks.h"

#include <rex/memory.h>
#include <rex/system/kernel_state.h>
#include <rex/system/native_checks.h>

#include <atomic>
#include <cstdlib>
#include <cstring>

namespace fm4::hooks {
namespace {
uint32_t Load32(uint8_t* base, uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(base + address +
                                              (address >= 0xE0000000u ? 0x1000u : 0u));
}
// The XDK's virtual-to-physical conversion of command buffer pointers.
uint32_t Physical(uint32_t address) {
  return (address & 0x1FFFFFFF) + ((((address >> 20) + 0x200) & 0x1000));
}

bool Enabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("FM4_NATIVE_CHECK");
    return value && *value && *value != '0';
  }();
  return enabled;
}

// The device, from the draw flush's first argument.
std::atomic<uint32_t> device{0};
std::atomic<uint64_t> sequence{0};

struct Range {
  uint32_t depth = 0;
  uint32_t writer = 0;
  uint32_t start = 0;  // device+48 when the range started.
  bool active = false;
};
thread_local Range range;

void Copy(uint8_t* base, uint32_t start, uint32_t end) {
  if (end <= start || end - start > (1u << 20)) {
    return;
  }
  const uint32_t first = Physical(start + 4);
  const uint32_t dwords = (end - start) / 4;
  rex::system::native_checks::Entry entry;
  entry.writer = range.writer;
  entry.sequence = sequence.fetch_add(1, std::memory_order_relaxed);
  entry.dwords.resize(dwords);
  const uint8_t* source =
      rex::system::kernel_state()->memory()->TranslatePhysical<const uint8_t*>(first);
  std::memcpy(entry.dwords.data(), source, size_t(dwords) * 4);
  rex::system::native_checks::Record(first, std::move(entry));
}
}  // namespace

void NativeCheckDevice(uint32_t address) {
  if (Enabled()) device.store(address, std::memory_order_relaxed);
}

void NativeCheckPre(uint32_t writer, uint8_t* base) {
  if (!Enabled() || range.depth++) return;
  const uint32_t dev = device.load(std::memory_order_relaxed);
  range.writer = writer;
  range.active = dev && !Load32(base, dev + 13904);
  if (range.active) range.start = Load32(base, dev + 48);
}

void NativeCheckPost(uint8_t* base) {
  if (!Enabled() || --range.depth) return;
  if (range.active) {
    Copy(base, range.start, Load32(base, device.load(std::memory_order_relaxed) + 48));
    range.active = false;
  }
}

void NativeCheckOverflowPre(uint8_t* base) {
  if (!Enabled() || !range.depth || !range.active) return;
  Copy(base, range.start, Load32(base, device.load(std::memory_order_relaxed) + 48));
}

void NativeCheckOverflowPost(uint8_t* base) {
  if (!Enabled() || !range.depth || !range.active) return;
  range.start = Load32(base, device.load(std::memory_order_relaxed) + 48);
}

}  // namespace fm4::hooks
