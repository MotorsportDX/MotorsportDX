// Optional call counts of the guest functions that build PM4 packets
// (FM4_D3D_TRACE=<path>), for the Stage 3.1 inventory of the title's D3D layer.
// The list comes from a static scan of the generated code for type-3 packet
// headers (addresses and packet names only). Every 5 seconds the file is
// rewritten with "<function> <packets> <calls> <caller>:<calls> ..." lines,
// callers being guest return addresses (top 8). The hooks are in
// fm4-guest-hooks.cpp.
#include "fm4-guest-hooks.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
struct Function {
  uint32_t address;
  const char* packets;
};
constexpr Function kFunctions[] = {
    {0x822A7E08, "EVENT_WRITE_ZPD,SET_BIN_MASK_HI,SET_BIN_MASK_LO,SET_CONSTANT2(pred)"},
    {0x822A7FD0, "EVENT_WRITE,EVENT_WRITE_EXT,MEM_WRITE,WAIT_REG_MEM"},
    {0x822ACC80, "WAIT_REG_MEM"},
    {0x822B2FB8, "WAIT_REG_MEM"},
    {0x822BF380, "WAIT_REG_MEM"},
    {0x822C22A8, "WAIT_REG_MEM"},
    {0x822DADA8, "INDIRECT_BUFFER,INDIRECT_BUFFER_PFD"},
    {0x822DB108, "WAIT_REG_MEM"},
    {0x822EBE20, "IM_LOAD_IMMEDIATE,INVALIDATE_STATE,SET_BIN_MASK_LO"},
    {0x82310530, "SET_BIN_MASK_HI,SET_BIN_MASK_LO,SET_CONSTANT2(pred)"},
    {0x82310818, "EVENT_WRITE(pred),EVENT_WRITE_EXT(pred),SET_BIN_MASK_HI,SET_BIN_MASK_LO,SET_BIN_SELECT_HI"},
    {0x82311080, "DRAW_INDX(pred),EVENT_WRITE(pred),EVENT_WRITE_EXT(pred),SET_BIN_MASK_LO"},
    {0x82333538, "SET_BIN_MASK_LO"},
    {0x8233AC60, "DRAW_INDX(pred),EVENT_WRITE(pred),EVENT_WRITE_EXT(pred),SET_BIN_MASK_LO"},
    {0x82343608, "LOAD_ALU_CONSTANT"},
    {0x823436B8, "IM_LOAD,IM_LOAD(pred),SET_BIN_MASK_HI,SET_BIN_MASK_LO,SET_CONSTANT2(pred),WAIT_FOR_IDLE(pred)"},
    {0x82344038, "EVENT_WRITE"},
    {0x82348DC8, "EVENT_WRITE,IM_LOAD_IMMEDIATE,INVALIDATE_STATE,SET_BIN_MASK_HI,SET_BIN_MASK_LO,SET_CONSTANT2(pred),WAIT_REG_MEM"},
    {0x8234D278, "DRAW_INDX(pred),EVENT_WRITE(pred),EVENT_WRITE_EXT(pred),SET_BIN_MASK_LO"},
    {0x8234DAE8, "DRAW_INDX(pred),EVENT_WRITE(pred),EVENT_WRITE_EXT(pred),SET_BIN_MASK_LO"},
    {0x823619B0, "SET_BIN_MASK_HI,SET_BIN_MASK_LO,SET_CONSTANT2(pred)"},
    {0x82361D40, "SET_BIN_MASK_LO"},
    {0x8236F6F8, "EVENT_WRITE_SHD,INVALIDATE_STATE,NOP"},
    {0x82370370, "DRAW_INDX_2(pred)"},
    {0x823712F8, "EVENT_WRITE(pred),EVENT_WRITE_EXT(pred),SET_BIN_MASK_HI,SET_BIN_MASK_LO,SET_BIN_SELECT_HI"},
    {0x82374660, "EVENT_WRITE"},
    {0x8237FA28, "NOP,REG_RMW"},
    {0x82381318, "EVENT_WRITE"},
    {0x82382A70, "SET_BIN_MASK_HI,SET_BIN_MASK_LO,SET_BIN_SELECT_HI,SET_BIN_SELECT_LO"},
    {0x82386C48, "WAIT_REG_MEM"},
    {0x82392800, "NOP"},
    {0x82395A48, "EVENT_WRITE_SHD"},
    {0x8239DF28, "REG_RMW"},
    {0x826E1550, "SET_BIN_MASK_HI,SET_BIN_MASK_LO"},
    {0x826E35F8, "EVENT_WRITE(pred),EVENT_WRITE_EXT(pred),SET_BIN_MASK_LO,WAIT_REG_MEM"},
    {0x826E3CA8, "SET_BIN_SELECT_HI"},
    {0x826E3E60, "SET_BIN_SELECT_HI"},
    {0x826E4C90, "EVENT_WRITE_SHD(pred)"},
    {0x826E4E28, "ME_INIT"},
    {0x826E5598, "IM_LOAD_IMMEDIATE,INVALIDATE_STATE"},
    {0x826E5C20, "WAIT_REG_MEM"},
    {0x826E6490, "IM_LOAD_IMMEDIATE,INVALIDATE_STATE"},
    {0x826E6D70, "DRAW_INDX_2"},
    {0x826E7220, "WAIT_REG_MEM"},
    {0x826E75C0, "DRAW_INDX_2,IM_LOAD_IMMEDIATE,INVALIDATE_STATE"},
    {0x826E7718, "DRAW_INDX_2,IM_LOAD_IMMEDIATE,INVALIDATE_STATE"},
    {0x826E7AA0, "EVENT_WRITE"},
    {0x826E7B98, "EVENT_WRITE"},
    {0x826E80A8, "WAIT_REG_MEM"},
    {0x826E8928, "DRAW_INDX(pred),IM_LOAD_IMMEDIATE,INVALIDATE_STATE"},
    {0x826E8C80, "VIZ_QUERY"},
    {0x826E8D60, "VIZ_QUERY"},
    {0x826E8E30, "SET_BIN_MASK_HI,SET_BIN_MASK_LO"},
    {0x826E8FD8, "SET_BIN_MASK_HI,SET_BIN_MASK_LO"},
    {0x826E92A0, "REG_RMW"},
    {0x826E9CF0, "WAIT_FOR_IDLE"},
    {0x826EBBC0, "COND_WRITE"},
    {0x826EBCB0, "COND_WRITE"},
    {0x826EC9C0, "IM_LOAD_IMMEDIATE,INVALIDATE_STATE"},
    {0x826ECF30, "SET_BIN_SELECT_LO"},
    {0x826ED400, "EVENT_WRITE_SHD"},
    {0x826EE8E8, "INVALIDATE_STATE"},
    {0x826EE940, "IM_LOAD_IMMEDIATE"},
    {0x826EEBC0, "DRAW_INDX_2"},
    {0x826F1390, "EVENT_WRITE_SHD"},
    {0x826F3228, "INDIRECT_BUFFER"},
    {0x826F3660, "EVENT_WRITE_SHD"},
    {0x826F36C8, "SET_BIN_SELECT_HI"},
    {0x826F3858, "MEM_WRITE"},
    {0x826F64A8, "DRAW_INDX_2(pred),EVENT_WRITE,IM_LOAD_IMMEDIATE,INVALIDATE_STATE"},
    {0x826F7FB0, "REG_RMW"},
};
constexpr size_t kCount = sizeof(kFunctions) / sizeof(kFunctions[0]);

struct Trace {
  std::string path;
  std::array<std::atomic<uint64_t>, kCount> calls{};
  std::mutex mutex;
  std::map<uint64_t, uint64_t> callers;  // function index << 32 | return address
};

void Write(Trace& trace, double seconds) {
  std::map<uint64_t, uint64_t> callers;
  {
    std::lock_guard<std::mutex> lock(trace.mutex);
    callers = trace.callers;
  }
  std::ofstream output(trace.path + ".tmp");
  output << "# seconds " << seconds << "\n";
  for (size_t i = 0; i < kCount; ++i) {
    const uint64_t calls = trace.calls[i].load(std::memory_order_relaxed);
    if (!calls) continue;
    std::vector<std::pair<uint64_t, uint32_t>> top;
    for (auto it = callers.lower_bound(uint64_t(i) << 32);
         it != callers.end() && (it->first >> 32) == i; ++it) {
      top.emplace_back(it->second, uint32_t(it->first));
    }
    std::sort(top.rbegin(), top.rend());
    char head[256];
    std::snprintf(head, sizeof(head), "sub_%08X %s %llu", kFunctions[i].address, kFunctions[i].packets,
                  static_cast<unsigned long long>(calls));
    output << head;
    for (size_t k = 0; k < top.size() && k < 8; ++k) {
      char caller[48];
      std::snprintf(caller, sizeof(caller), " %08X:%llu", top[k].second,
                    static_cast<unsigned long long>(top[k].first));
      output << caller;
    }
    output << "\n";
  }
  output.close();
  std::error_code error;
  std::filesystem::rename(trace.path + ".tmp", trace.path, error);
}

Trace* GetTrace() {
  static Trace* trace = [] {
    const char* path = std::getenv("FM4_D3D_TRACE");
    if (!path || !*path) return static_cast<Trace*>(nullptr);
    auto* result = new Trace;
    result->path = path;
    std::thread([result] {
      const auto start = std::chrono::steady_clock::now();
      for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        Write(*result, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
      }
    }).detach();
    return result;
  }();
  return trace;
}

}  // namespace

void fm4::hooks::D3DTraceCount(uint32_t address, const PPCContext& ctx) {
  Trace* trace = GetTrace();
  if (!trace) return;
  const Function* found = std::lower_bound(
      std::begin(kFunctions), std::end(kFunctions), address,
      [](const Function& function, uint32_t value) { return function.address < value; });
  if (found == std::end(kFunctions) || found->address != address) return;
  const size_t index = size_t(found - std::begin(kFunctions));
  trace->calls[index].fetch_add(1, std::memory_order_relaxed);
  std::lock_guard<std::mutex> lock(trace->mutex);
  ++trace->callers[uint64_t(index) << 32 | uint32_t(ctx.lr)];
}
