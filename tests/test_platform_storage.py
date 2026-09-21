"""Cross-platform storage checks, including native APIs on the Windows runner."""
import json
import subprocess
import sys
import multiprocessing
import os
from pathlib import Path
import tempfile
import unittest
import uuid
from unittest import mock

from relay_core import board, filelock, keystore


def _append(directory, worker):
    path = Path(directory) / "thread.md"
    for index in range(12):
        board.append_to_thread(path, lambda body: (f"worker={worker}:{index}\n", None))


class PlatformStorageTests(unittest.TestCase):
    def test_atomic_thread_updates_exclude_other_processes(self):
        with tempfile.TemporaryDirectory() as directory:
            context = multiprocessing.get_context("spawn")
            workers = [context.Process(target=_append, args=(directory, n)) for n in range(4)]
            for worker in workers:
                worker.start()
            for worker in workers:
                worker.join(30)
                if worker.is_alive():
                    worker.terminate()
                    worker.join()
                    self.fail("Thread writer deadlocked")
                self.assertEqual(worker.exitcode, 0)
            lines = (Path(directory) / "thread.md").read_text().splitlines()
            self.assertEqual(sorted(line for line in lines if line),
                             sorted(f"worker={n}:{i}" for n in range(4) for i in range(12)))

    def test_lock_released_after_close(self):
        with tempfile.TemporaryDirectory() as directory:
            fd = filelock.open_directory_lock(directory)
            filelock.flock(fd, filelock.LOCK_EX)
            filelock.flock(fd, filelock.LOCK_UN)
            os.close(fd)
            fd = filelock.open_directory_lock(directory)
            filelock.flock(fd, filelock.LOCK_EX)
            os.close(fd)

    def test_worker_starts_with_isolated_profile(self):
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            env = dict(os.environ, HOME=directory, USERPROFILE=directory,
                       XDG_DATA_HOME=directory, XDG_CONFIG_HOME=directory,
                       RELAY_KEYRING="off", RELAY_INDEX="off")
            result = subprocess.run([sys.executable, str(root / "backend" / "worker.py")],
                                    input="", text=True, capture_output=True, env=env, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            events = [json.loads(line) for line in result.stdout.splitlines()]
            self.assertIn("ready", [event.get("event") for event in events])

    def test_settings_and_session_atomic_save(self):
        from relay_core.keybindings import KeybindingCatalog
        from relay_core.sessions import SessionStore
        with tempfile.TemporaryDirectory() as directory:
            settings = KeybindingCatalog(str(Path(directory) / "keybindings.json"), [])
            settings._write({"bindings": {"pane.splitRight": ["Ctrl+Shift+R"]}})
            self.assertEqual(settings._read()["bindings"]["pane.splitRight"], ["Ctrl+Shift+R"])
            store = SessionStore(directory, index=False)
            identity = uuid.uuid4().hex
            data = {"id": identity, "title": "Windows roundtrip", "messages": [], "turns": 0}
            store.save(data)
            self.assertEqual(json.loads(store.path(identity).read_text())["title"], data["title"])

    def test_windows_remote_input_fails_closed(self):
        root = str(Path(__file__).resolve().parents[1])
        if root not in sys.path:
            sys.path.insert(0, root)
        from remote import terminal, gui_host, wire
        source = object.__new__(gui_host.GuiPaneSource)
        with mock.patch.object(os, "name", "nt"):
            self.assertIsNone(source.secret_state("pane"))
            with self.assertRaises(wire.WireError):
                source.secret_prompt("pane")
            with self.assertRaises(wire.WireError):
                terminal.secret_prompt(123)

    @unittest.skipUnless(os.name == "nt", "native Windows Credential Manager")
    def test_native_credential_roundtrip(self):
        from relay_core import wincredentials
        target = f"org.relayterminal.Relay/test/{uuid.uuid4()}"
        try:
            self.assertEqual(wincredentials.lookup(target), "")
            wincredentials.store(target, "test-key-é")
            self.assertEqual(wincredentials.lookup(target), "test-key-é")
            wincredentials.store(target, "replacement")
            self.assertEqual(wincredentials.lookup(target), "replacement")
            self.assertTrue(wincredentials.remove(target))
            self.assertFalse(wincredentials.remove(target))
            self.assertEqual(wincredentials.lookup(target), "")
        finally:
            wincredentials.remove(target)

    def test_environment_precedes_disabled_keyring(self):
        with mock.patch.dict(os.environ, RELAY_OPENAI_API_KEY="test-env-key", RELAY_KEYRING="off"):
            self.assertEqual(keystore.lookup("openai"), "test-env-key")
            self.assertEqual(keystore.key_source("openai"), "env")


if __name__ == "__main__":
    unittest.main()
