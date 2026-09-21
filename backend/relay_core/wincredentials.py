# SPDX-License-Identifier: AGPL-3.0-or-later
"""Windows Credential Manager generic credentials; no file fallback.

Loaded only on Windows. Credentials persist for this user on this computer.
"""
import ctypes
from ctypes import wintypes


class _Credential(ctypes.Structure):
    _fields_ = [("Flags", wintypes.DWORD), ("Type", wintypes.DWORD),
                ("TargetName", wintypes.LPWSTR), ("Comment", wintypes.LPWSTR),
                ("LastWritten", wintypes.FILETIME), ("CredentialBlobSize", wintypes.DWORD),
                ("CredentialBlob", ctypes.POINTER(wintypes.BYTE)), ("Persist", wintypes.DWORD),
                ("AttributeCount", wintypes.DWORD), ("Attributes", ctypes.c_void_p),
                ("TargetAlias", wintypes.LPWSTR), ("UserName", wintypes.LPWSTR)]


_api = ctypes.WinDLL("advapi32", use_last_error=True)
_api.CredReadW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                         ctypes.POINTER(ctypes.POINTER(_Credential))]
_api.CredReadW.restype = wintypes.BOOL
_api.CredWriteW.argtypes = [ctypes.POINTER(_Credential), wintypes.DWORD]
_api.CredWriteW.restype = wintypes.BOOL
_api.CredDeleteW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD]
_api.CredDeleteW.restype = wintypes.BOOL
_api.CredFree.argtypes = [ctypes.c_void_p]
_api.CredFree.restype = None


def lookup(target):
    pointer = ctypes.POINTER(_Credential)()
    if not _api.CredReadW(target, 1, 0, ctypes.byref(pointer)):
        error = ctypes.get_last_error()
        if error == 1168:  # ERROR_NOT_FOUND
            return ""
        raise ctypes.WinError(error)
    try:
        item = pointer.contents
        return ctypes.string_at(item.CredentialBlob, item.CredentialBlobSize).decode("utf-8")
    finally:
        _api.CredFree(pointer)


def store(target, value):
    data = value.encode("utf-8")
    if len(data) > 2560:  # CRED_MAX_CREDENTIAL_BLOB_SIZE
        raise ValueError("API key exceeds Windows Credential Manager's 2560-byte limit.")
    blob = (wintypes.BYTE * len(data)).from_buffer_copy(data)
    item = _Credential(Type=1, TargetName=target, CredentialBlobSize=len(data),
                       CredentialBlob=blob, Persist=2, UserName="Relay")
    if not _api.CredWriteW(ctypes.byref(item), 0):
        raise ctypes.WinError(ctypes.get_last_error())


def remove(target):
    if _api.CredDeleteW(target, 1, 0):
        return True
    error = ctypes.get_last_error()
    if error == 1168:
        return False
    raise ctypes.WinError(error)
