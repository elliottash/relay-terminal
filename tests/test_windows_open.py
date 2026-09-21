"""relay-open transport checks; native named-pipe cases run on Windows CI."""
import ctypes
import importlib.machinery
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import threading
import time
import unittest
import uuid
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
loader = importlib.machinery.SourceFileLoader("relay_open", str(ROOT / "scripts" / "relay-open"))
spec = importlib.util.spec_from_loader(loader.name, loader)
relay_open = importlib.util.module_from_spec(spec)
loader.exec_module(relay_open)


class OpenDispatchTests(unittest.TestCase):
    def test_shell_address_wins(self):
        with mock.patch.dict(os.environ, RELAY_OPEN_SOCKET="relay-open-test"):
            self.assertEqual(relay_open.socket_address(), "relay-open-test")

    def test_windows_discovery_matches_qt_runtime_location(self):
        with tempfile.TemporaryDirectory() as directory:
            published = Path(directory) / "relay" / "open-socket"
            published.parent.mkdir()
            published.write_text("relay-open-test", encoding="utf-8")
            with mock.patch.dict(os.environ, RELAY_OPEN_SOCKET=""), \
                    mock.patch.object(os, "name", "nt"), \
                    mock.patch.object(os.path, "expanduser", return_value=directory):
                self.assertEqual(relay_open.socket_address(), "relay-open-test")

    def test_windows_default_app_fallback(self):
        with tempfile.NamedTemporaryFile() as handle:
            with mock.patch.object(relay_open, "send", return_value=False), \
                    mock.patch.object(os, "name", "nt"), \
                    mock.patch.object(os, "startfile", create=True) as startfile, \
                    mock.patch.object(relay_open.sys, "argv", ["relay-open", handle.name]):
                self.assertEqual(relay_open.main(), 0)
                startfile.assert_called_once_with(handle.name)


@unittest.skipUnless(os.name == "nt", "native Windows pipe transport")
class WindowsPipeTests(unittest.TestCase):
    def exchange(self, response, timeout=1.0):
        from ctypes import wintypes as W
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.CreateNamedPipeW.argtypes = [W.LPCWSTR, W.DWORD, W.DWORD, W.DWORD,
                                            W.DWORD, W.DWORD, W.DWORD, W.LPVOID]
        kernel.CreateNamedPipeW.restype = W.HANDLE
        kernel.ConnectNamedPipe.argtypes = [W.HANDLE, W.LPVOID]
        kernel.ConnectNamedPipe.restype = W.BOOL
        kernel.ReadFile.argtypes = [W.HANDLE, W.LPVOID, W.DWORD, ctypes.POINTER(W.DWORD), W.LPVOID]
        kernel.ReadFile.restype = W.BOOL
        kernel.WriteFile.argtypes = kernel.ReadFile.argtypes
        kernel.WriteFile.restype = W.BOOL
        kernel.FlushFileBuffers.argtypes = [W.HANDLE]
        kernel.FlushFileBuffers.restype = W.BOOL
        kernel.CloseHandle.argtypes = [W.HANDLE]
        kernel.CloseHandle.restype = W.BOOL
        address = "relay-open-test-" + uuid.uuid4().hex
        pipe = kernel.CreateNamedPipeW("\\\\.\\pipe\\" + address, 3, 0, 1, 4096, 4096, 0, None)
        self.assertNotEqual(pipe, ctypes.c_void_p(-1).value)
        received, errors = [], []

        def server():
            try:
                connected = kernel.ConnectNamedPipe(pipe, None)
                if not connected and ctypes.get_last_error() != 535:
                    raise ctypes.WinError(ctypes.get_last_error())
                data, count = ctypes.create_string_buffer(4096), W.DWORD()
                if not kernel.ReadFile(pipe, data, len(data), ctypes.byref(count), None):
                    raise ctypes.WinError(ctypes.get_last_error())
                received.append(json.loads(data.raw[:count.value]))
                if response is None:
                    time.sleep(timeout + 0.3)
                else:
                    for chunk in response:
                        buffer = ctypes.create_string_buffer(chunk)
                        if not kernel.WriteFile(pipe, buffer, len(chunk), ctypes.byref(count), None):
                            raise ctypes.WinError(ctypes.get_last_error())
                        time.sleep(0.01)
                    kernel.FlushFileBuffers(pipe)
            except Exception as error:
                errors.append(error)
            finally:
                kernel.CloseHandle(pipe)

        thread = threading.Thread(target=server, daemon=True)
        thread.start()
        start = time.monotonic()
        result = relay_open.send_windows(address, b'{"url":"relay://turn/test/1"}\n', timeout=timeout)
        elapsed = time.monotonic() - start
        thread.join(timeout + 2)
        self.assertFalse(thread.is_alive())
        self.assertFalse(errors, errors)
        self.assertEqual(received, [{"url": "relay://turn/test/1"}])
        return result, elapsed

    def test_fragmented_success(self):
        result, _ = self.exchange([b"o", b"k\n"])
        self.assertTrue(result)

    def test_server_rejection(self):
        result, _ = self.exchange([b"error\n"])
        self.assertFalse(result)

    def test_silent_server_times_out(self):
        result, elapsed = self.exchange(None, timeout=0.15)
        self.assertFalse(result)
        self.assertLess(elapsed, 0.8)

    def test_missing_server(self):
        self.assertFalse(relay_open.send_windows("relay-missing-" + uuid.uuid4().hex, b"{}\n"))


if __name__ == "__main__":
    unittest.main()
