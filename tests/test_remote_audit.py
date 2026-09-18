# SPDX-License-Identifier: GPL-3.0-or-later
"""The remote-share audit log (remote/audit.py): capped parts, nothing rotated away."""
import json
import stat
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from remote import audit


class AuditLogTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.directory = Path(self.tmp.name)
        self.log = audit.AuditLog(self.directory)

    def tearDown(self):
        self.tmp.cleanup()

    def lines(self):
        return [json.loads(line) for path in sorted(self.directory.glob("audit-*.jsonl"),
                                                     key=lambda p: (len(p.name), p.name))
                for line in path.read_text().splitlines()]

    def test_one_month_under_the_cap_is_one_file(self):
        for n in range(5):
            self.log.record("line", n=n)
        files = list(self.directory.glob("audit-*.jsonl"))
        self.assertEqual(len(files), 1)
        self.assertRegex(files[0].name, r"^audit-\d{4}-\d{2}\.jsonl$")
        self.assertEqual(stat.S_IMODE(files[0].stat().st_mode), 0o600)

    def test_past_the_cap_it_continues_in_numbered_parts(self):
        with mock.patch.object(audit, "MAX_BYTES", 200):
            for n in range(40):
                self.log.record("line", text="x" * 40, n=n)
        names = sorted(p.name for p in self.directory.glob("audit-*.jsonl"))
        stamp = names[0][len("audit-"):len("audit-YYYY-MM")]
        self.assertIn(f"audit-{stamp}.2.jsonl", names)
        for path in self.directory.glob("audit-*.jsonl"):
            # A part stops taking lines once it reaches the cap, so it overshoots by one line at most.
            self.assertLess(path.stat().st_size, 200 + 100)
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
        # Split, never trimmed: every record is still there, in order.
        self.assertEqual([line["n"] for line in self.lines()], list(range(40)))

    def test_a_full_month_does_not_make_a_file_per_second(self):
        # The first version named the overflow after time.time(), so once the month was full every
        # second that saw a record made a new file. Here each record lands in a different second.
        clock = iter(range(1_800_000_000, 1_800_000_000 + 1000))
        with mock.patch.object(audit, "MAX_BYTES", 200), \
             mock.patch.object(audit.time, "time", lambda: float(next(clock))):
            for n in range(40):
                self.log.record("line", text="x" * 40, n=n)
        files = list(self.directory.glob("audit-*.jsonl"))
        # About 40 lines of ~70 bytes in parts of 200: a dozen or so parts, not 40 files.
        self.assertLess(len(files), 20)
        self.assertEqual(len(self.lines()), 40)


if __name__ == "__main__":
    unittest.main()
