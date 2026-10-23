"""The game process's CPU time and the CPU package's power during an e2e run (Windows).

Every second a thread records the process's CPU time (GetProcessTimes) and, where Windows exposes
the CPU's energy counters (the "Energy Meter" performance counters, RAPL on Intel), the package's
and the cores' power. At each phase mark and at the end it also takes the CPU time of every thread
of the process by its name (SetThreadDescription), so a phase shows which threads used the CPU:
a thread that spins shows CPU time without the work. Without the counters the power stays None;
on another platform the sampler records nothing.

With an affinity mask it also confines the process to those logical processors, as a proxy for a
smaller CPU: at once and again each second where the mask changed (the runtime sets the
system's mask once at its start). It then also samples those processors' clock and busy share
(the "Processor Information" counters), since a power plan lowers the clock of lightly loaded
cores.
"""
import ctypes
import json
import threading
import time
from ctypes import wintypes

PACKAGE = r'\Energy Meter(RAPL_Package0_PKG)\Power'
CORES = r'\Energy Meter(RAPL_Package0_PP0)\Power'
TOP_THREADS = 12

try:
    kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
    pdh = ctypes.WinDLL('pdh')
except (AttributeError, OSError):
    kernel32 = pdh = None


class THREADENTRY32(ctypes.Structure):
    _fields_ = [('dwSize', wintypes.DWORD), ('cntUsage', wintypes.DWORD),
                ('th32ThreadID', wintypes.DWORD), ('th32OwnerProcessID', wintypes.DWORD),
                ('tpBasePri', wintypes.LONG), ('tpDeltaPri', wintypes.LONG), ('dwFlags', wintypes.DWORD)]


class PDH_FMT_COUNTERVALUE(ctypes.Structure):
    _fields_ = [('CStatus', wintypes.DWORD), ('doubleValue', ctypes.c_double)]


def _filetime_ms(value):
    return (value.dwHighDateTime << 32 | value.dwLowDateTime) / 10000


def _times(get, handle):
    """Kernel plus user time in ms of a process or thread handle, or None."""
    created, exited, kernel, user = (wintypes.FILETIME() for _ in range(4))
    if not get(handle, *(ctypes.byref(t) for t in (created, exited, kernel, user))):
        return None
    return _filetime_ms(kernel) + _filetime_ms(user)


def thread_times(pid):
    """{thread name: [CPU ms, threads]} of a process; unnamed threads count as 'thread'."""
    result = {}
    snapshot = kernel32.CreateToolhelp32Snapshot(0x4, 0)  # TH32CS_SNAPTHREAD
    if snapshot in (0, wintypes.HANDLE(-1).value):
        return result
    try:
        entry = THREADENTRY32()
        entry.dwSize = ctypes.sizeof(entry)
        more = kernel32.Thread32First(snapshot, ctypes.byref(entry))
        while more:
            if entry.th32OwnerProcessID == pid:
                handle = kernel32.OpenThread(0x0800, False, entry.th32ThreadID)  # QUERY_LIMITED
                if handle:
                    try:
                        cpu = _times(kernel32.GetThreadTimes, handle)
                        text = ctypes.c_wchar_p()
                        name = 'thread'
                        if kernel32.GetThreadDescription(handle, ctypes.byref(text)) >= 0 and text.value:
                            name = text.value
                        if text:
                            kernel32.LocalFree(text)
                        if cpu is not None:
                            slot = result.setdefault(name, [0.0, 0])
                            slot[0] += cpu
                            slot[1] += 1
                    finally:
                        kernel32.CloseHandle(handle)
            more = kernel32.Thread32Next(snapshot, ctypes.byref(entry))
    finally:
        kernel32.CloseHandle(snapshot)
    return result


class Counters:
    """The power counters through PDH; value() is None for a counter Windows does not have."""

    def __init__(self, paths):
        self.query = wintypes.HANDLE()
        self.counters = {}
        if pdh.PdhOpenQueryW(None, 0, ctypes.byref(self.query)):
            self.query = None
            return
        for path in paths:
            counter = wintypes.HANDLE()
            if not pdh.PdhAddEnglishCounterW(self.query, path, 0, ctypes.byref(counter)):
                self.counters[path] = counter
        pdh.PdhCollectQueryData(self.query)

    def collect(self):
        if self.query:
            pdh.PdhCollectQueryData(self.query)

    def value(self, path):
        counter = self.counters.get(path)
        if counter is None:
            return None
        value = PDH_FMT_COUNTERVALUE()
        if pdh.PdhGetFormattedCounterValue(counter, 0x200, None, ctypes.byref(value)):  # DOUBLE
            return None
        return value.doubleValue

    def close(self):
        if self.query:
            pdh.PdhCloseQuery(self.query)


class Sampler:
    """Samples a subprocess.Popen once a second until stop(); mark(name) starts a phase."""

    def __init__(self, process, path, affinity=None):
        self.process, self.path, self.affinity = process, path, affinity
        self.affinity_sets = 0
        self.samples = []  # (time, process CPU ms, package mW, cores mW, mean MHz, mean busy %)
        self.marks = []  # (name, time, {thread name: [CPU ms, threads]})
        self.stopping = threading.Event()
        self.thread = None
        if kernel32 is None or pdh is None:
            return
        kernel32.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
        kernel32.GetThreadTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
        kernel32.OpenThread.restype = wintypes.HANDLE
        kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
        kernel32.Thread32First.argtypes = kernel32.Thread32Next.argtypes = [wintypes.HANDLE, ctypes.c_void_p]
        kernel32.GetThreadDescription.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_wchar_p)]
        kernel32.GetThreadDescription.restype = ctypes.c_long
        kernel32.LocalFree.argtypes = [ctypes.c_void_p]
        kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
        pdh.PdhAddEnglishCounterW.argtypes = [wintypes.HANDLE, wintypes.LPCWSTR, ctypes.c_size_t,
                                              ctypes.POINTER(wintypes.HANDLE)]
        pdh.PdhGetFormattedCounterValue.argtypes = [wintypes.HANDLE, wintypes.DWORD, ctypes.c_void_p,
                                                    ctypes.POINTER(PDH_FMT_COUNTERVALUE)]
        kernel32.GetProcessAffinityMask.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_size_t),
                                                    ctypes.POINTER(ctypes.c_size_t)]
        kernel32.SetProcessAffinityMask.argtypes = [wintypes.HANDLE, ctypes.c_size_t]
        self._apply_affinity()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def _apply_affinity(self):
        if not self.affinity:
            return
        handle = wintypes.HANDLE(int(self.process._handle))
        current, system = ctypes.c_size_t(), ctypes.c_size_t()
        if kernel32.GetProcessAffinityMask(handle, ctypes.byref(current), ctypes.byref(system)) and                 current.value != self.affinity and kernel32.SetProcessAffinityMask(handle, self.affinity):
            self.affinity_sets += 1

    def _process_ms(self):
        return _times(kernel32.GetProcessTimes, wintypes.HANDLE(int(self.process._handle)))

    def _run(self):
        processors = [i for i in range(64) if self.affinity and self.affinity >> i & 1]
        clocks = [rf'\Processor Information(0,{i})\Actual Frequency' for i in processors]
        busy = [rf'\Processor Information(0,{i})\% Processor Time' for i in processors]
        counters = Counters([PACKAGE, CORES] + clocks + busy)

        def mean(paths):
            values = [counters.value(path) for path in paths]
            values = [value for value in values if value is not None]
            return sum(values) / len(values) if values else None
        try:
            with open(self.path, 'w') as output:
                while not self.stopping.wait(1.0) and self.process.poll() is None:
                    self._apply_affinity()
                    counters.collect()
                    sample = (time.time(), self._process_ms(), counters.value(PACKAGE), counters.value(CORES),
                              mean(clocks), mean(busy))
                    self.samples.append(sample)
                    output.write(json.dumps({'t': round(sample[0], 3), 'process_cpu_ms': sample[1],
                                             'package_mw': sample[2], 'cores_mw': sample[3],
                                             'affinity_mhz': sample[4], 'affinity_busy_pct': sample[5]}) + '\n')
        finally:
            counters.close()

    def mark(self, name):
        if self.thread is not None and self.process.poll() is None:
            self.marks.append((name, time.time(), thread_times(self.process.pid)))

    def stop(self):
        """Ends the sampling while the process still runs (its last thread times)."""
        if self.thread is None:
            return
        self.mark('end')
        self.stopping.set()
        self.thread.join(timeout=5)

    def summary(self):
        """Per phase (from its mark to the next): seconds, the process's CPU ms per second, the
        package's and the cores' mean power in W, and the threads with the most CPU time as
        {name: [CPU ms per second, threads]}."""
        result = {}
        for (name, start, threads), (_, stop, end_threads) in zip(self.marks, self.marks[1:]):
            inside = [s for s in self.samples if start <= s[0] <= stop and s[1] is not None]
            if name == 'end' or len(inside) < 2 or stop <= start:
                continue
            seconds = inside[-1][0] - inside[0][0]
            mean = lambda values: round(sum(values) / len(values) / 1000, 2) if values else None
            per_thread = {thread: [(cpu - threads.get(thread, [0, 0])[0]) / (stop - start), count]
                          for thread, (cpu, count) in end_threads.items()}
            top = sorted(per_thread.items(), key=lambda item: -item[1][0])[:TOP_THREADS]
            result[name] = {
                'seconds': round(seconds, 1),
                'process_cpu_ms_per_s': round((inside[-1][1] - inside[0][1]) / seconds, 1),
                'package_w': mean([s[2] for s in inside if s[2] is not None]),
                'cores_w': mean([s[3] for s in inside if s[3] is not None]),
                # With an affinity mask: its processors' mean clock (MHz) and busy share (%).
                'affinity_mhz': (lambda v: round(sum(v) / len(v)) if v else None)(
                    [s[4] for s in inside if len(s) > 4 and s[4] is not None]),
                'affinity_busy_pct': (lambda v: round(sum(v) / len(v), 1) if v else None)(
                    [s[5] for s in inside if len(s) > 5 and s[5] is not None]),
                'threads': {thread: [round(ms, 1), count] for thread, (ms, count) in top},
            }
        return result
