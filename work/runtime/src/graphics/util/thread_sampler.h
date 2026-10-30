#pragma once

// Optional sampling profiler for the busy threads of the process (diagnostics
// only). When REX_THREAD_PROFILE=<path> is set, a thread of its own samples the
// stacks of every thread that used at least a fifth of a core in the last
// 100 ms, every REX_THREAD_PROFILE_PERIOD_MS (default 2), and of the other
// threads ten times less often, so a wait shows where it waits. It writes
//   T <thread id> <name>                       a thread, once
//   S <stack id> <module>+0x<offset> ...       a stack, once, innermost frame first
//   <ms> <thread id> <stack id>                a sample
// with <ms> on the perf trace's clock (REX_PERF_TRACE_FILE's 't', in ms), so a
// sample can be matched to a window's long frames. Offsets into the game's
// executable resolve with its linker map (outputs/FM4-build/e2e/thread_profile.py);
// the runtime's DLLs by module. Does nothing without the variable, and on other
// platforms.

namespace rex::graphics::thread_sampler {

void Start();

}  // namespace rex::graphics::thread_sampler
