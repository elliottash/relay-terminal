#!/usr/bin/env python3
"""The Relay Free gateway's contract on loopback, for driving the desktop without the real host.

Runs tests/test_hosted.py's FakeGateway (challenge → register → chat completions with quota headers,
GET /v1/quota) and writes its base URL to <dir>/gateway.url. Two knobs, read from files so drive.sh
can turn them mid-run:

  <dir>/exhausted     while this file exists every chat call is refused 429 quota_exhausted
  <dir>/used          the tokens used today, as a number (the fake never counts on its own)

    PYTHONPATH=backend:. python3 docs/qa_evidence/2026-09-18-relay-free/fake-gateway.py <dir>
"""
import sys
import time
from pathlib import Path

from tests.test_hosted import FakeGateway

control = Path(sys.argv[1])
control.mkdir(parents=True, exist_ok=True)
gateway = FakeGateway()
gateway.quota = {"limit": 250_000, "used": 1_200, "resets_at": int(time.time()) + 3 * 3600}
(control / "gateway.url").write_text(gateway.base + "\n")
print(gateway.base, flush=True)
try:
    while True:
        gateway.exhausted = (control / "exhausted").exists()
        used = control / "used"
        if used.exists():
            try:
                gateway.quota["used"] = int(used.read_text().strip() or 0)
            except ValueError:
                pass
        time.sleep(0.2)
except KeyboardInterrupt:
    pass
finally:
    gateway.close()
