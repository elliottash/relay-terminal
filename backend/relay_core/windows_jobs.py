# SPDX-License-Identifier: AGPL-3.0-or-later
"""Windows command trees, attached while suspended so no child can escape the job."""
import ctypes
from ctypes import wintypes as w
import threading

k32 = ctypes.WinDLL("kernel32", use_last_error=True)

def _api(name, result, *arguments):
    fn = getattr(k32, name)
    fn.restype, fn.argtypes = result, arguments
    return fn

_create = _api("CreateJobObjectW", w.HANDLE, w.LPVOID, w.LPCWSTR)
_assign = _api("AssignProcessToJobObject", w.BOOL, w.HANDLE, w.HANDLE)
_terminate = _api("TerminateJobObject", w.BOOL, w.HANDLE, w.UINT)
_close = _api("CloseHandle", w.BOOL, w.HANDLE)
_snapshot = _api("CreateToolhelp32Snapshot", w.HANDLE, w.DWORD, w.DWORD)
_open_thread = _api("OpenThread", w.HANDLE, w.DWORD, w.BOOL, w.DWORD)
_resume = _api("ResumeThread", w.DWORD, w.HANDLE)

_set_info = _api("SetInformationJobObject", w.BOOL, w.HANDLE, ctypes.c_int, w.LPVOID, w.DWORD)

class BasicLimits(ctypes.Structure):
    _fields_ = [("process_time", ctypes.c_int64), ("job_time", ctypes.c_int64),
                ("flags", w.DWORD), ("min_ws", ctypes.c_size_t), ("max_ws", ctypes.c_size_t),
                ("active", w.DWORD), ("affinity", ctypes.c_size_t),
                ("priority", w.DWORD), ("scheduling", w.DWORD)]

class ExtendedLimits(ctypes.Structure):
    _fields_ = [("basic", BasicLimits), ("io", ctypes.c_uint64 * 6),
                ("process_memory", ctypes.c_size_t), ("job_memory", ctypes.c_size_t),
                ("peak_process", ctypes.c_size_t), ("peak_job", ctypes.c_size_t)]

class ThreadEntry(ctypes.Structure):
    _fields_ = [("size", w.DWORD), ("usage", w.DWORD), ("tid", w.DWORD),
                ("pid", w.DWORD), ("base", w.LONG), ("delta", w.LONG), ("flags", w.DWORD)]

_first = _api("Thread32First", w.BOOL, w.HANDLE, ctypes.POINTER(ThreadEntry))
_next = _api("Thread32Next", w.BOOL, w.HANDLE, ctypes.POINTER(ThreadEntry))

class ProcessTree:
    def __init__(self, process):
        self.lock = threading.Lock()
        self.handle = _create(None, None)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            limits = ExtendedLimits()
            limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
            if not _set_info(self.handle, 9, ctypes.byref(limits), ctypes.sizeof(limits)):
                raise ctypes.WinError(ctypes.get_last_error())
            if not _assign(self.handle, w.HANDLE(int(process._handle))):
                raise ctypes.WinError(ctypes.get_last_error())
            # Popen closes the primary thread handle. Open that suspended thread by id,
            # using documented Toolhelp APIs; it cannot spawn children before ResumeThread.
            snapshot = _snapshot(4, 0)  # TH32CS_SNAPTHREAD
            if snapshot == ctypes.c_void_p(-1).value:
                raise ctypes.WinError(ctypes.get_last_error())
            resumed = False
            try:
                entry = ThreadEntry(size=ctypes.sizeof(ThreadEntry))
                found = _first(snapshot, ctypes.byref(entry))
                while found:
                    if entry.pid == process.pid:
                        thread = _open_thread(2, False, entry.tid)  # THREAD_SUSPEND_RESUME
                        if not thread:
                            raise ctypes.WinError(ctypes.get_last_error())
                        try:
                            if _resume(thread) == 0xFFFFFFFF:
                                raise ctypes.WinError(ctypes.get_last_error())
                            resumed = True
                        finally:
                            _close(thread)
                    found = _next(snapshot, ctypes.byref(entry))
            finally:
                _close(snapshot)
            if not resumed:
                raise OSError("The command's suspended thread could not be found")
        except BaseException:
            self.stop()
            raise

    def stop(self):
        with self.lock:
            if self.handle:
                _terminate(self.handle, 1)
                _close(self.handle)
                self.handle = None


def attach(process):
    try:
        process._relay_tree = ProcessTree(process)
    except BaseException:
        process.kill()
        process.wait()
        if process.stdout:
            process.stdout.close()
        raise
