#pragma once

// The game process's CPU time and its threads', by name, in the perf trace's
// windows (perf_trace_cpu, stage N4 of docs/analysis/d3d-hle-n4.md, 17.19): the
// harness's cpu_power.py measures the same from outside, but the GUI launcher of
// an export has no Python. A thread that spins shows CPU time without the work.

#include <ostream>

namespace rex::graphics::perf_trace {

// Whether perf_trace_cpu is on.
bool ProcessCpuEnabled();

// Writes ,"process_cpu":{"ms":...,"threads":{"<name>":[ms,threads],...}}: the
// process's CPU time and the twelve thread names with the most, over the last
// second that a sampler thread of its own (below normal priority, started by the
// first call) measured, nothing before its first second. The sampler lists the
// threads again every tenth second (a thread snapshot of the system takes tens
// of milliseconds); between, it keeps their handles. On other platforms it
// writes nothing.
void WriteProcessCpu(std::ostream& output);

}  // namespace rex::graphics::perf_trace
