#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <DbgHelp.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <cwchar>

namespace {
LONG CALLBACK FM4CaptureNullAccess(EXCEPTION_POINTERS* exception) {
  // Runtime memory-watch faults are handled by its earlier VEH. Record only
  // unmapped low guest addresses, without recovering the fault.
  if (exception->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
      exception->ExceptionRecord->NumberParameters < 2 ||
      exception->ExceptionRecord->ExceptionInformation[1] - 0x100000000ull >= 0x10000ull)
    return EXCEPTION_CONTINUE_SEARCH;
  const auto* context = exception->ContextRecord;
  FILE* output = nullptr;
  if (fopen_s(&output, "guest-null-crash.txt", "w") == 0 && output) {
    std::fprintf(output, "fault_address=%llx operation=%llx\n",
                 exception->ExceptionRecord->ExceptionInformation[1],
                 exception->ExceptionRecord->ExceptionInformation[0]);
    std::fprintf(output, "image_base=%p rip=%llx rsp=%llx rax=%llx rbx=%llx rcx=%llx rdx=%llx rsi=%llx rdi=%llx\n",
                 GetModuleHandleW(nullptr), context->Rip, context->Rsp, context->Rax,
                 context->Rbx, context->Rcx, context->Rdx, context->Rsi, context->Rdi);
    std::fprintf(output, "r8=%llx r9=%llx r10=%llx r11=%llx r12=%llx r13=%llx r14=%llx r15=%llx\n",
                 context->R8, context->R9, context->R10, context->R11,
                 context->R12, context->R13, context->R14, context->R15);
    std::fclose(output);
  }
  HANDLE dump = CreateFileW(L"guest-null-crash.dmp", GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (dump != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), exception, FALSE};
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump,
                     MiniDumpWithThreadInfo, &info, nullptr, nullptr);
    CloseHandle(dump);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

#if defined(__MINGW32__)
// llvm-mingw builds (docs/analysis/mingw-toolchain.md): the last exception no handler took, with
// the faulting address, the accessed address and the stack, each frame as module+offset for
// llvm-symbolizer, in <FM4_CAPTURE_DIR>\unhandled-crash.txt. These builds have no PDB.
void FM4PrintFrame(FILE* output, int index, DWORD64 address) {
  HMODULE module = nullptr;
  wchar_t name[MAX_PATH] = L"?";
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(address), &module))
    GetModuleFileNameW(module, name, MAX_PATH);
  const wchar_t* base_name = std::wcsrchr(name, L'\\');
  std::fprintf(output, "#%02d %016llx %ls+0x%llx\n", index, address,
               base_name ? base_name + 1 : name,
               module ? address - reinterpret_cast<DWORD64>(module) : 0ull);
}

LONG WINAPI FM4ReportUnhandled(EXCEPTION_POINTERS* exception) {
  const char* directory = std::getenv("FM4_CAPTURE_DIR");
  const std::string path = std::string(directory) + "\\unhandled-crash.txt";
  FILE* output = std::fopen(path.c_str(), "w");
  if (!output)
    return EXCEPTION_CONTINUE_SEARCH;
  const auto* record = exception->ExceptionRecord;
  std::fprintf(output, "code=%08lx thread=%lu address=%p", record->ExceptionCode,
               GetCurrentThreadId(), record->ExceptionAddress);
  for (DWORD i = 0; i < record->NumberParameters && i < 4; ++i)
    std::fprintf(output, " info%lu=%llx", i,
                 static_cast<unsigned long long>(record->ExceptionInformation[i]));
  std::fprintf(output, "\n");
  CONTEXT context = *exception->ContextRecord;
  std::fprintf(output, "rax=%llx rbx=%llx rcx=%llx rdx=%llx rsi=%llx rdi=%llx rbp=%llx rsp=%llx\n",
               context.Rax, context.Rbx, context.Rcx, context.Rdx, context.Rsi, context.Rdi,
               context.Rbp, context.Rsp);
  std::fprintf(output, "r8=%llx r9=%llx r10=%llx r11=%llx r12=%llx r13=%llx r14=%llx r15=%llx\n",
               context.R8, context.R9, context.R10, context.R11, context.R12, context.R13,
               context.R14, context.R15);
  for (int frame = 0; frame < 64 && context.Rip; ++frame) {
    FM4PrintFrame(output, frame, context.Rip);
    DWORD64 image_base = 0;
    PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &image_base, nullptr);
    if (!function) {
      // A leaf function: the return address is at the top of the stack.
      context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp);
      context.Rsp += 8;
      continue;
    }
    void* handler_data = nullptr;
    DWORD64 establisher = 0;
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, function, &context,
                     &handler_data, &establisher, nullptr);
  }
  std::fclose(output);
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif
}

void FM4InstallCrashDiagnostics() {
  if (std::getenv("FM4_CAPTURE_DIR")) {
    AddVectoredExceptionHandler(0, FM4CaptureNullAccess);
#if defined(__MINGW32__)
    SetUnhandledExceptionFilter(FM4ReportUnhandled);
#endif
  }
}
