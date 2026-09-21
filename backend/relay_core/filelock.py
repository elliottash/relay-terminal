# SPDX-License-Identifier: AGPL-3.0-or-later
"""Blocking process locks: flock on Unix, a fixed byte range on Windows.

Directory locks retain the existing Unix inode lock. Windows uses a stable sibling
lock file inside the directory; never remove that file while writers may be alive.
"""
import os
from pathlib import Path

if os.name != "nt":
    from fcntl import flock, LOCK_EX, LOCK_UN
else:
    import ctypes
    from ctypes import wintypes
    import msvcrt

    LOCK_EX, LOCK_UN = 2, 8

    class _Overlapped(ctypes.Structure):
        _fields_ = [("Internal", ctypes.c_size_t), ("InternalHigh", ctypes.c_size_t),
                    ("Offset", wintypes.DWORD), ("OffsetHigh", wintypes.DWORD),
                    ("hEvent", wintypes.HANDLE)]

    _kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    _kernel.LockFileEx.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD,
                                  wintypes.DWORD, wintypes.DWORD, ctypes.POINTER(_Overlapped)]
    _kernel.LockFileEx.restype = wintypes.BOOL
    _kernel.UnlockFileEx.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD,
                                    wintypes.DWORD, ctypes.POINTER(_Overlapped)]
    _kernel.UnlockFileEx.restype = wintypes.BOOL

    def flock(file, operation):
        fd = file if isinstance(file, int) else file.fileno()
        handle = msvcrt.get_osfhandle(fd)
        overlapped = _Overlapped()
        if operation == LOCK_EX:
            ok = _kernel.LockFileEx(handle, 2, 0, 1, 0, ctypes.byref(overlapped))
        elif operation == LOCK_UN:
            ok = _kernel.UnlockFileEx(handle, 0, 1, 0, ctypes.byref(overlapped))
        else:
            raise ValueError("Unsupported file lock operation")
        if not ok:
            raise ctypes.WinError(ctypes.get_last_error())


def open_directory_lock(directory):
    if os.name == "nt":
        return os.open(Path(directory) / ".relay-directory.lock", os.O_CREAT | os.O_RDWR, 0o600)
    return os.open(directory, os.O_RDONLY)


def chmod_fd(fd, mode):
    """Apply Unix mode bits where supported; Windows inherits directory ACLs.

    Python 3.12 has no fchmod on Windows, and chmod only toggles read-only there;
    that is not an ACL/security substitute and can break atomic replacement.
    """
    if os.name != "nt":
        os.fchmod(fd, mode)
