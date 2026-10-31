/**
 * @file        ppc.h
 * @brief       PPC recompilation support -- umbrella header
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#pragma once

#include <rex/hook.h>
#include <rex/types.h>

#include <rex/ppc/context.h>
#include <rex/ppc/function.h>
#include <rex/ppc/intrinsics.h>
#include <rex/ppc/stack.h>

// Consumer-facing using declarations
using rex::ppc::FindPPCFuncByName;
using rex::ppc::GetPPCFuncRegistry;
using rex::ppc::GuestToHostFunction;
using rex::ppc::HostToGuestFunction;
using rex::ppc::ImportFunction;

// The setjmp buffers of the generated code (pch_h.inja's get_jmp_buf_map). The Universal CRT's
// x64 setjmp stores xmm6-xmm15 with movdqa, so each buffer must be 16-byte aligned; mingw-w64 puts
// that alignment on the SETJMP_FLOAT128 typedef rather than on the struct, which clang drops
// inside std::pair, so llvm-mingw builds wrap the buffer (docs/analysis/mingw-toolchain.md).
// Defined here, at the end, so the template keeps its line count and the MSVC build its code.
#if defined(__MINGW32__)
#include <csetjmp>
namespace rex::ppc {
struct alignas(16) JmpBuf {
  jmp_buf buf;
};
}  // namespace rex::ppc
#define REX_PPC_JMP_BUF_TYPE ::rex::ppc::JmpBuf
#define REX_PPC_JMP_BUF(entry) ((entry).buf)
#else
#define REX_PPC_JMP_BUF_TYPE jmp_buf
#define REX_PPC_JMP_BUF(entry) (entry)
#endif
