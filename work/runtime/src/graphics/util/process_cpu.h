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

// perf_trace_heap_seconds: how often the heaps are walked (0 = never).
int HeapSeconds();

// Writes one JSON line: per heap of the process, its busy blocks and bytes, its
// free bytes, and the 40 block sizes with the most busy bytes as [size, blocks,
// bytes]. It walks each heap under its lock. On other platforms it writes nothing.
void WriteHeaps(std::ostream& output, double seconds);

// perf_trace_d3d12_objects: hooks the creating methods of the device (an
// ID3D12Device) so that WriteHeaps also writes the Direct3D 12 objects created
// and alive by their two calling frames. Call it before the device creates
// anything to be counted. With perf_trace_driver_alloc_sizes it also patches
// the driver's heap imports to count its blocks of those sizes by caller. On
// other platforms, or with the cvars off, it does nothing.
void TrackD3D12Objects(void* device);

// With perf_trace_driver_alloc_sizes: the pipeline that the calling thread is
// binding (nullptr after the bind), to count the driver's blocks by pipeline.
void SetBindingPipeline(void* pipeline);

}  // namespace rex::graphics::perf_trace
