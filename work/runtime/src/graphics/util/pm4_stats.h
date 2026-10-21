#pragma once
// Optional PM4 statistics of the command processor (REX_PM4_STATS=<file>), for
// stage N0 of docs/analysis/d3d-hle.md. One JSON line per perf trace window
// (REX_PERF_TRACE_FILE; otherwise per second), with the steady clock in ms,
// the swaps, the packets executed by type, the type-3 packets executed and
// skipped by predication per opcode, the indirect buffers, and the emission
// checks: an IM_LOAD or LOAD_ALU_CONSTANT that a title hook recorded when it was
// written (rex/system/pm4_emissions.h, FM4_N0_KEYCHECK in the title) is hashed
// again when the command processor executes it. "same" means the memory it reads
// is the memory it pointed at when written, "changed" that the guest rewrote it
// in between, "unknown" that no hook recorded the packet; each split by packets
// written into a command buffer recording and packets written into the ring.
// Changed packets are listed in <file>.changes ("<kind> <packet> <length>
// <recorded> <hash at emission> <hash at execution>", hexadecimal, at most
// 4,096 lines). Without the variable every helper returns at once.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_set>

#include <rex/hash.h>
#include <rex/system/pm4_emissions.h>

#include "perf_trace.h"

namespace rex::graphics::pm4_stats {

struct Check {
  uint64_t same[2]{}, changed[2]{};  // [ring, recording]
  uint64_t unknown = 0;
};

struct State {
  std::ofstream output, changes;
  uint64_t origin = 0, window_start = 0;
  uint32_t swaps = 0, change_lines = 0;
  uint64_t packets[4]{};
  uint64_t executed[128]{}, skipped[128]{};
  uint64_t indirect_buffers = 0, indirect_dwords = 0;
  Check checks[uint32_t(system::pm4_emissions::Kind::kCount)];
  // Distinct (packet, execution hash) pairs that changed, over the whole run.
  std::unordered_set<uint64_t> changed_shaders;
};

inline State* Get() {
  static State* state = []() -> State* {
    const char* path = std::getenv("REX_PM4_STATS");
    if (!path || !*path) return nullptr;
    auto* result = new State;
    result->output.open(path, std::ios::app);
    if (!result->output.is_open()) return nullptr;
    result->changes.open(std::string(path) + ".changes", std::ios::app);
    result->origin = result->window_start = perf_trace::Now();
    return result;
  }();
  return state;
}

inline void CountPacket(uint32_t type) {
  if (State* state = Get()) ++state->packets[type & 3];
}
inline void CountType3(uint32_t opcode, bool skipped) {
  if (State* state = Get()) ++(skipped ? state->skipped : state->executed)[opcode & 127];
}
inline void CountIndirectBuffer(uint32_t dwords) {
  if (State* state = Get()) {
    ++state->indirect_buffers;
    state->indirect_dwords += dwords;
  }
}

// packet_address: guest physical address of the type-3 header; data: the memory
// the packet reads now.
inline void CheckEmission(system::pm4_emissions::Kind kind, uint32_t packet_address,
                          const void* data, uint32_t length) {
  State* state = Get();
  if (!state || !data) return;
  Check& check = state->checks[uint32_t(kind)];
  system::pm4_emissions::Entry entry;
  if (!system::pm4_emissions::Find(kind, packet_address, &entry)) {
    ++check.unknown;
    return;
  }
  const uint32_t hashed = entry.length && entry.length <= length ? entry.length : length;
  const uint64_t hash = XXH3_64bits(data, hashed);
  if (hash == entry.hash) {
    ++check.same[entry.recorded];
    return;
  }
  ++check.changed[entry.recorded];
  if (kind == system::pm4_emissions::Kind::kShader) {
    state->changed_shaders.insert(uint64_t(packet_address) << 32 ^ hash);
  }
  if (state->change_lines < 4096) {
    ++state->change_lines;
    state->changes << uint32_t(kind) << ' ' << std::hex << packet_address << ' ' << hashed << ' '
                   << unsigned(entry.recorded) << ' ' << entry.hash << ' ' << hash << std::dec
                   << '\n';
  }
}

inline void OnSwap() {
  State* state = Get();
  if (!state) return;
  ++state->swaps;
  const uint64_t now = perf_trace::Now();
  const uint64_t elapsed = now - state->window_start;
  // Called right after perf_trace::OnSwap: close the window together with the
  // perf trace's, so the lines match perf.jsonl (and its phases) one to one.
  if (const perf_trace::State* perf = perf_trace::Get()) {
    if (perf->window_start <= state->window_start) return;
  } else if (elapsed < 1000000000ull) {
    return;
  }
  auto& out = state->output;
  out << "{\"t\":" << double(now - state->origin) / 1e9 << ",\"steady_ms\":" << now / 1000000
      << ",\"window_ms\":" << double(elapsed) / 1e6
      << ",\"swaps\":" << state->swaps << ",\"packets\":[" << state->packets[0] << ','
      << state->packets[1] << ',' << state->packets[2] << ',' << state->packets[3]
      << "],\"type3\":{";
  bool first = true;
  for (uint32_t i = 0; i < 128; ++i) {
    if (!state->executed[i] && !state->skipped[i]) continue;
    char name[8];
    std::snprintf(name, sizeof(name), "%02X", i);
    out << (first ? "" : ",") << '"' << name << "\":[" << state->executed[i] << ','
        << state->skipped[i] << ']';
    first = false;
  }
  out << "},\"indirect_buffers\":" << state->indirect_buffers
      << ",\"indirect_dwords\":" << state->indirect_dwords;
  static const char* const kKinds[] = {"im_load", "load_alu_constant"};
  for (uint32_t k = 0; k < uint32_t(system::pm4_emissions::Kind::kCount); ++k) {
    const Check& c = state->checks[k];
    out << ",\"" << kKinds[k] << "\":{\"same_ring\":" << c.same[0]
        << ",\"same_recorded\":" << c.same[1] << ",\"changed_ring\":" << c.changed[0]
        << ",\"changed_recorded\":" << c.changed[1] << ",\"unknown\":" << c.unknown << '}';
  }
  out << ",\"changed_shader_pairs\":" << state->changed_shaders.size() << "}\n";
  out.flush();
  state->changes.flush();
  state->window_start = now;
  state->swaps = 0;
  for (auto& v : state->packets) v = 0;
  for (auto& v : state->executed) v = 0;
  for (auto& v : state->skipped) v = 0;
  state->indirect_buffers = state->indirect_dwords = 0;
  for (auto& c : state->checks) c = Check{};
}

}  // namespace rex::graphics::pm4_stats
