# SPDX-License-Identifier: AGPL-3.0-or-later
"""Models › Sources: each login's email, and a usage refresh on demand (card #EQH0).

Offline: logins are fixture files in a temporary directory, and the worker runs with an empty
home, so neither poll has a token to send anywhere.
"""
import base64
import json
import os
import select
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

from relay_core import guest_harness_provider as ghp

WORKER = Path(__file__).resolve().parents[1] / "backend" / "worker.py"


def id_token(claims):
    part = lambda obj: base64.urlsafe_b64encode(json.dumps(obj).encode()).decode().rstrip("=")
    return f"{part({'alg': 'RS256'})}.{part(claims)}.sig"


class LoginEmailTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.dir = Path(self.temp.name)
        ghp._emails.clear()

    def test_codex_reads_the_id_tokens_email_claim(self):
        (self.dir / "auth.json").write_text(json.dumps(
            {"tokens": {"id_token": id_token({"email": "ashe@ethz.ch"}), "access_token": "secret"}}))
        self.assertEqual(ghp.login_email("codex", str(self.dir)), "ashe@ethz.ch")

    def test_claude_reads_oauth_account_in_its_config_dir(self):
        (self.dir / ".claude.json").write_text(json.dumps({"oauthAccount": {"emailAddress": "a@b.org"}}))
        self.assertEqual(ghp.login_email("claude", str(self.dir)), "a@b.org")

    def test_the_default_logins_follow_the_environment(self):
        (self.dir / ".claude.json").write_text(json.dumps({"oauthAccount": {"emailAddress": "c@d.org"}}))
        (self.dir / "auth.json").write_text(json.dumps({"tokens": {"id_token": id_token({"email": "e@f.org"})}}))
        with mock.patch.dict(os.environ, {"CLAUDE_CONFIG_DIR": str(self.dir), "CODEX_HOME": str(self.dir)}):
            self.assertEqual((ghp.login_email("claude"), ghp.login_email("codex")), ("c@d.org", "e@f.org"))

    def test_a_sign_in_that_rewrites_the_file_is_read_again(self):
        path = self.dir / "auth.json"
        path.write_text(json.dumps({"tokens": {"id_token": id_token({"email": "old@x.org"})}}))
        self.assertEqual(ghp.login_email("codex", str(self.dir)), "old@x.org")
        path.write_text(json.dumps({"tokens": {"id_token": id_token({"email": "new@x.org"})}}))
        os.utime(path, (time.time() + 5, time.time() + 5))
        self.assertEqual(ghp.login_email("codex", str(self.dir)), "new@x.org")

    def test_missing_malformed_or_non_address_values_are_empty(self):
        self.assertEqual(ghp.login_email("codex", str(self.dir)), "")            # no file
        (self.dir / "auth.json").write_text("{not json")
        self.assertEqual(ghp.login_email("codex", str(self.dir)), "")
        (self.dir / ".claude.json").write_text(json.dumps({"oauthAccount": {"emailAddress": "no address"}}))
        self.assertEqual(ghp.login_email("claude", str(self.dir)), "")
        self.assertEqual(ghp.login_email("gemini", str(self.dir)), "")


class UsageRefreshWorkerTests(unittest.TestCase):
    def test_the_worker_answers_usage_refresh(self):
        with tempfile.TemporaryDirectory() as home:
            env = dict(os.environ, XDG_DATA_HOME=home, XDG_CONFIG_HOME=home, XDG_STATE_HOME=home,
                       HOME=home, RELAY_KEYRING="off", RELAY_INDEX="off")
            for name in ("CLAUDE_CONFIG_DIR", "CODEX_HOME"):
                env.pop(name, None)
            worker = subprocess.Popen([sys.executable, "-u", str(WORKER)], stdin=subprocess.PIPE,
                                      stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=env)
            try:
                worker.stdin.write(b'{"type": "usage_refresh", "id": "r1"}\n')
                worker.stdin.flush()
                deadline, answer = time.monotonic() + 30, None
                while answer is None and time.monotonic() < deadline:
                    ready, _, _ = select.select([worker.stdout], [], [], 1)
                    if ready:
                        event = json.loads(worker.stdout.readline())
                        if event.get("event") == "usage_refreshed":
                            answer = event
            finally:
                worker.stdin.close()
                worker.wait(timeout=30)
        self.assertIsNotNone(answer)
        self.assertEqual(answer["id"], "r1")
        self.assertIsInstance(answer["at"], int)


if __name__ == "__main__":
    unittest.main()
