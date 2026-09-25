# SPDX-License-Identifier: AGPL-3.0-or-later
"""tests/browser.py gives Chrome a short TMPDIR when the inherited one is too long (#H1BS).

Chrome's singleton socket lives at $TMPDIR/com.google.Chrome.XXXXXX/SingletonSocket and a Unix
socket path must fit in 108 bytes. A Relay pane's TMPDIR is its per-session scratch dir, long
enough that Chrome died before opening a page; and a terminated Chrome leaves its temp dirs
behind, so the dir is always private and removed. No Chrome is needed here: this pins the choice.
"""
from __future__ import annotations

import asyncio
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from tests import browser


class ChromeTmpdirTests(unittest.TestCase):
    def setUp(self):
        self.saved = tempfile.tempdir
        self.base = tempfile.mkdtemp(dir="/tmp")
        self.addCleanup(shutil.rmtree, self.base, True)

    def tearDown(self):
        tempfile.tempdir = self.saved

    def use_tmpdir(self, path: str) -> None:
        os.makedirs(path, exist_ok=True)
        tempfile.tempdir = path

    def test_short_tmpdir_gets_a_private_dir_inside_it(self):
        # Private even when TMPDIR fits: a terminated Chrome leaves com.google.Chrome.* behind,
        # and only a dir that stop() removes takes them with it.
        self.use_tmpdir(self.base)
        chosen = browser._chrome_tmpdir()
        self.assertEqual(os.path.dirname(chosen), self.base)
        self.assertTrue(os.path.basename(chosen).startswith("chrome-"))

    def test_long_tmpdir_gets_a_short_private_dir(self):
        # The shape that failed: <scratch home>/<36-char pane uuid>/tmp, padded past the limit.
        long_dir = os.path.join(self.base, "x" * 40, "7dbb2c54-9215-4e1f-a044-52c4f6c7dc1a", "tmp")
        self.use_tmpdir(long_dir)
        self.assertGreater(len(long_dir) + len(browser._SINGLETON_SUFFIX),
                           browser.SOCKET_PATH_LIMIT)
        first, second = browser._chrome_tmpdir(), browser._chrome_tmpdir()
        try:
            self.assertIsNotNone(first)
            self.assertNotEqual(first, second)
            for chosen in (first, second):
                self.assertTrue(os.path.isdir(chosen))
                self.assertFalse(chosen.startswith(long_dir))
                socket = chosen + "/com.google.Chrome.e7rpEL/SingletonSocket"
                self.assertLessEqual(len(socket), browser.SOCKET_PATH_LIMIT)
        finally:
            for chosen in (first, second):
                if chosen:
                    shutil.rmtree(chosen, ignore_errors=True)
        self.assertFalse(os.path.exists(first) or os.path.exists(second))

    def test_stop_removes_the_private_dir(self):
        b = browser.Browser(binary="/nonexistent")
        made = b.tmpdir = tempfile.mkdtemp(dir=self.base)
        asyncio.run(b.stop())
        self.assertFalse(os.path.exists(made))
        self.assertIsNone(b.tmpdir)

    def test_end_group_waits_for_children_that_outlive_the_leader(self):
        # A Chrome child that outlived the browser rewrote a profile stop() had just removed.
        leader = subprocess.Popen(["sh", "-c", "sleep 30 & sleep 30"], start_new_session=True)
        asyncio.run(browser._end_group(leader))
        with self.assertRaises(ProcessLookupError):
            os.killpg(leader.pid, 0)


if __name__ == "__main__":
    unittest.main()


class DiesWithItsTestTests(unittest.TestCase):
    """Card #XY13: a test killed outright takes its Chrome down with it."""

    def test_child_in_its_own_session_dies_when_the_test_is_killed(self):
        if not browser._die_with_parent([]):
            self.skipTest("no setpriv --pdeathsig here")
        self.assertFalse(_alive(self.orphan(guarded=True)), "the child outlived its killed test")
        # Without the launcher the same child is reparented and lives on: what the test covers.
        self.assertTrue(_alive(self.orphan(guarded=False)))

    def orphan(self, guarded: bool) -> int:
        """A stand-in for a test: starts a child the way Browser.start() starts Chrome, prints
        its pid, then is SIGKILLed — no finally, no atexit, nothing of ours runs. Returns the
        child's pid a moment later (the child is killed at cleanup)."""
        command = "browser._die_with_parent(['sleep', '60'])" if guarded else "['sleep', '60']"
        script = ("import os, signal, subprocess\n"
                  "from tests import browser\n"
                  f"child = subprocess.Popen({command}, start_new_session=True,"
                  " stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)\n"
                  "print(child.pid, flush=True)\n"
                  "os.kill(os.getpid(), signal.SIGKILL)\n")
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        test = subprocess.run([sys.executable, "-c", script], cwd=root,
                              capture_output=True, text=True, timeout=30)
        pid = int(test.stdout.split()[0])
        self.addCleanup(lambda: _alive(pid) and os.kill(pid, signal.SIGKILL))
        for _ in range(30):
            if not _alive(pid):
                break
            time.sleep(0.1)
        return pid

def _alive(pid: int) -> bool:
    try:
        with open(f"/proc/{pid}/stat") as handle:
            return handle.read().rsplit(")", 1)[1].split()[0] != "Z"
    except OSError:
        return False
