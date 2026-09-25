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
import subprocess
import tempfile
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
