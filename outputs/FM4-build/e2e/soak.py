"""Soak telemetry of the game process during an e2e run (Windows): soak.jsonl.

Every 30 s (--soak-interval) a thread records the process's private bytes, working set, page
file use, handle, thread, GDI and USER object counts, and its own GPU memory: the "GPU Process
Memory" performance counters of its process id (dedicated, shared and committed bytes, summed
over the adapters it uses; the counters DXGI's QueryVideoMemoryInfo reports inside the process).
A label set by the scenario (set_label) says where the run was, so a report can line the samples
up with the races. soak_report.py analyses the file. On another platform the sampler records
nothing.
"""
import ctypes
import json
import threading
import time
from ctypes import wintypes

try:
    kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
    psapi = ctypes.WinDLL('psapi')
    user32 = ctypes.WinDLL('user32')
    pdh = ctypes.WinDLL('pdh')
except (AttributeError, OSError):
    kernel32 = psapi = user32 = pdh = None

GPU_COUNTERS = ('Dedicated Usage', 'Shared Usage', 'Total Committed')


class PROCESS_MEMORY_COUNTERS_EX(ctypes.Structure):
    _fields_ = [('cb', wintypes.DWORD), ('PageFaultCount', wintypes.DWORD),
                ('PeakWorkingSetSize', ctypes.c_size_t), ('WorkingSetSize', ctypes.c_size_t),
                ('QuotaPeakPagedPoolUsage', ctypes.c_size_t), ('QuotaPagedPoolUsage', ctypes.c_size_t),
                ('QuotaPeakNonPagedPoolUsage', ctypes.c_size_t), ('QuotaNonPagedPoolUsage', ctypes.c_size_t),
                ('PagefileUsage', ctypes.c_size_t), ('PeakPagefileUsage', ctypes.c_size_t),
                ('PrivateUsage', ctypes.c_size_t)]


class THREADENTRY32(ctypes.Structure):
    _fields_ = [('dwSize', wintypes.DWORD), ('cntUsage', wintypes.DWORD),
                ('th32ThreadID', wintypes.DWORD), ('th32OwnerProcessID', wintypes.DWORD),
                ('tpBasePri', wintypes.LONG), ('tpDeltaPri', wintypes.LONG), ('dwFlags', wintypes.DWORD)]


class PDH_FMT_COUNTERVALUE(ctypes.Structure):
    _fields_ = [('CStatus', wintypes.DWORD), ('doubleValue', ctypes.c_double)]


class PDH_FMT_COUNTERVALUE_ITEM_W(ctypes.Structure):
    _fields_ = [('szName', wintypes.LPWSTR), ('FmtValue', PDH_FMT_COUNTERVALUE)]


def _declare():
    kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel32.Thread32First.argtypes = kernel32.Thread32Next.argtypes = [wintypes.HANDLE, ctypes.c_void_p]
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel32.GetProcessHandleCount.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD]
    user32.GetGuiResources.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    user32.GetGuiResources.restype = wintypes.DWORD
    pdh.PdhAddEnglishCounterW.argtypes = [wintypes.HANDLE, wintypes.LPCWSTR, ctypes.c_size_t,
                                          ctypes.POINTER(wintypes.HANDLE)]
    pdh.PdhGetFormattedCounterArrayW.argtypes = [wintypes.HANDLE, wintypes.DWORD,
                                                 ctypes.POINTER(wintypes.DWORD),
                                                 ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]


def thread_count(pid):
    snapshot = kernel32.CreateToolhelp32Snapshot(0x4, 0)  # TH32CS_SNAPTHREAD
    if snapshot in (0, None, wintypes.HANDLE(-1).value):
        return None
    count = 0
    try:
        entry = THREADENTRY32()
        entry.dwSize = ctypes.sizeof(entry)
        more = kernel32.Thread32First(snapshot, ctypes.byref(entry))
        while more:
            count += entry.th32OwnerProcessID == pid
            more = kernel32.Thread32Next(snapshot, ctypes.byref(entry))
    finally:
        kernel32.CloseHandle(snapshot)
    return count


def process_counters(handle, pid):
    """The process's memory (bytes) and object counts, or {} when it has exited."""
    memory = PROCESS_MEMORY_COUNTERS_EX()
    memory.cb = ctypes.sizeof(memory)
    if not psapi.GetProcessMemoryInfo(handle, ctypes.byref(memory), memory.cb):
        return {}
    handles = wintypes.DWORD()
    kernel32.GetProcessHandleCount(handle, ctypes.byref(handles))
    return {'private_bytes': memory.PrivateUsage, 'working_set': memory.WorkingSetSize,
            'peak_working_set': memory.PeakWorkingSetSize, 'pagefile': memory.PagefileUsage,
            'handles': handles.value, 'threads': thread_count(pid),
            'gdi': user32.GetGuiResources(handle, 0), 'user': user32.GetGuiResources(handle, 1)}


class GpuMemory:
    """The "GPU Process Memory" counters of one process id, summed over its instances (one per
    adapter and memory segment group); values are None where Windows has no such counters."""

    def __init__(self, pid):
        self.query = wintypes.HANDLE()
        self.counters = {}
        if pdh.PdhOpenQueryW(None, 0, ctypes.byref(self.query)):
            self.query = None
            return
        for name in GPU_COUNTERS:
            counter = wintypes.HANDLE()
            path = rf'\GPU Process Memory(pid_{pid}_*)\{name}'
            if not pdh.PdhAddEnglishCounterW(self.query, path, 0, ctypes.byref(counter)):
                self.counters[name] = counter
        pdh.PdhCollectQueryData(self.query)

    def sample(self):
        if not self.query:
            return {}
        pdh.PdhCollectQueryData(self.query)
        result = {}
        for name, counter in self.counters.items():
            size, count = wintypes.DWORD(0), wintypes.DWORD(0)
            # PDH_MORE_DATA (0x800007D2) with the buffer size first, then the items.
            pdh.PdhGetFormattedCounterArrayW(counter, 0x200, ctypes.byref(size), ctypes.byref(count), None)
            if not size.value:
                result[name] = None
                continue
            buffer = ctypes.create_string_buffer(size.value)
            if pdh.PdhGetFormattedCounterArrayW(counter, 0x200, ctypes.byref(size), ctypes.byref(count),
                                                buffer):
                result[name] = None
                continue
            items = ctypes.cast(buffer, ctypes.POINTER(PDH_FMT_COUNTERVALUE_ITEM_W))
            values = [items[i].FmtValue.doubleValue for i in range(count.value)
                      if items[i].FmtValue.CStatus in (0, 1)]  # PDH_CSTATUS_VALID_DATA, NEW_DATA
            result[name] = int(sum(values)) if values else None
        return {'gpu_dedicated': result.get('Dedicated Usage'), 'gpu_shared': result.get('Shared Usage'),
                'gpu_committed': result.get('Total Committed')}

    def close(self):
        if self.query:
            pdh.PdhCloseQuery(self.query)


class SoakSampler:
    """Samples a subprocess.Popen every `interval` seconds into `path` until stop()."""

    def __init__(self, process, path, interval=30.0):
        self.process, self.path, self.interval = process, path, interval
        self.label = None
        self.samples = 0
        self.stopping = threading.Event()
        self.thread = None
        if kernel32 is None or pdh is None:
            return
        _declare()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def set_label(self, label):
        self.label = label

    def _run(self):
        handle = wintypes.HANDLE(int(self.process._handle))
        gpu = GpuMemory(self.process.pid)
        started = time.time()
        try:
            with open(self.path, 'w', encoding='utf-8') as output:
                # The first sample after 5 s (the process has its window), then every interval.
                wait = min(5.0, self.interval)
                while not self.stopping.wait(wait) and self.process.poll() is None:
                    wait = self.interval
                    record = {'t': round(time.time(), 1), 'uptime_s': round(time.time() - started, 1),
                              'label': self.label, **process_counters(handle, self.process.pid),
                              **gpu.sample()}
                    output.write(json.dumps(record) + '\n')
                    output.flush()
                    self.samples += 1
        finally:
            gpu.close()

    def stop(self):
        if self.thread is None:
            return
        self.stopping.set()
        self.thread.join(timeout=10)
