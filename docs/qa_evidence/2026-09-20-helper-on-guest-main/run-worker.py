#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Drive `backend/worker.py` over NDJSON with the guest-harness seam replaced by the test fake.

    python3 run-worker.py < messages.ndjson > events.ndjson

Nothing here starts a real claude or codex. `make_harness` is the single seam protocol 29.3 names
for exactly this, and it is pointed at `tests/guest_harness_fake.FakeHarness`: a `configure` that
*would* start a guest records a start on the fake instead of spawning anything, and the last line
on stderr says how many starts there were — which is the whole point of card #GH5T's first three
runs.
"""
import sys
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[3]          # the checkout
sys.path.insert(0, str(ROOT / "backend"))
sys.path.insert(0, str(ROOT / "tests"))

from relay_core import guest_harness_provider as ghp   # noqa: E402
from guest_harness_fake import FakeHarness              # noqa: E402

harness = FakeHarness([], session_id="drv-sess", model="claude-fake")

with mock.patch.object(ghp, "make_harness", return_value=harness), \
     mock.patch.object(ghp, "installations", return_value={
         "claude": {"installed": True, "binary": "/usr/bin/claude", "version": ""},
         "codex": {"installed": True, "binary": "/usr/bin/codex", "version": ""}}), \
     mock.patch.object(ghp, "adapter_available", lambda guest_id, refresh=False: True), \
     mock.patch.object(ghp, "_read_codex_catalog", return_value=[]):
    import worker
    worker.main()

sys.stderr.write("guest harness starts: %d\n" % len(harness.starts))
