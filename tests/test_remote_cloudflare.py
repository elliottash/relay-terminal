# SPDX-License-Identifier: GPL-3.0-or-later
"""The public link: a cloudflared quick tunnel (remote/cloudflare.py).

Nothing here opens a real tunnel. A shell script called `cloudflared` goes on PATH and prints what
a test wants it to print, so the cases that matter are reachable: it is not installed, it prints
the URL, it prints nothing before the deadline, it dies on start, and it has to be killed. The
things worth pinning are the ones a public address makes expensive to get wrong — only a real
`https://<name>.trycloudflare.com` is ever taken for a tunnel, the URL never reaches a log, one
tunnel exists at a time, and `unpublish()` kills the child it started and nobody else's.
"""
import os
import subprocess
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

from remote import cloudflare


class Fake:
    """A `cloudflared` on PATH that prints a script and then waits to be killed."""

    def __init__(self, directory: Path, *, lines: str, linger: bool = True, exit_code: int = 0):
        self.directory = directory
        self.log = directory / "calls.log"
        (directory / "say.txt").write_text(lines)
        binary = directory / "cloudflared"
        wait = 'while true; do sleep 0.2; done' if linger else f"exit {exit_code}"
        binary.write_text(f"""#!/bin/sh
printf '%s\\n' "$*" >> {self.log}
cat {directory}/say.txt
{wait}
""")
        binary.chmod(0o755)

    def calls(self) -> list[str]:
        return self.log.read_text().splitlines() if self.log.exists() else []


class TunnelTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.path = mock.patch.dict(os.environ, {"PATH": f"{self.directory}{os.pathsep}{os.environ['PATH']}"})
        self.path.start()
        self.addCleanup(self.path.stop)
        self.addCleanup(self.temp.cleanup)
        self.addCleanup(cloudflare.unpublish)

    # ---- what is offered -----------------------------------------------------------------------
    def test_without_cloudflared_there_is_a_sentence_not_a_silence(self):
        with mock.patch.dict(os.environ, {"PATH": "/nonexistent"}):
            found = cloudflare.probe()
            self.assertFalse(found.ready)
            self.assertIn("cloudflared is not installed", found.reason)
            url, said = cloudflare.publish(9999)
            self.assertIsNone(url)
            self.assertIn("cloudflared is not installed", said)

    def test_installed_means_offered_with_no_reason_to_show(self):
        Fake(self.directory, lines="")
        found = cloudflare.probe()
        self.assertEqual(found.reason, "")
        self.assertFalse(found.ready, "probing starts nothing; only publish() opens a tunnel")

    # ---- opening one ---------------------------------------------------------------------------
    def test_the_url_is_read_from_what_cloudflared_prints(self):
        fake = Fake(self.directory, lines=(
            "2026-09-18T20:00:00Z INF Requesting new quick Tunnel on trycloudflare.com...\n"
            "+-----------------------------------------+\n"
            "|  https://tidy-otter-rides-again.trycloudflare.com  |\n"
            "+-----------------------------------------+\n"))
        url, said = cloudflare.publish(18499, timeout=10)
        self.assertEqual(url, "https://tidy-otter-rides-again.trycloudflare.com")
        self.assertIn("--url http://127.0.0.1:18499", " ".join(fake.calls()))
        # The message tells the person the two things a quick tunnel makes true.
        self.assertIn("anyone with it", said)
        self.assertIn("stops working when you stop sharing", said)
        self.assertNotIn(url, said, "the link itself is not repeated into a message that gets logged")

    def test_only_a_real_tunnel_name_is_taken_for_one(self):
        Fake(self.directory, lines=(
            "INF see https://developers.cloudflare.com/trycloudflare.com/ for the docs\n"
            "INF connect to https://evil.example.com/trycloudflare.com now\n"
            "INF https://not-a-tunnel.trycloudflare.com.attacker.test/\n"), linger=False)
        url, said = cloudflare.publish(18499, timeout=3)
        self.assertIsNone(url, f"took something that is not a quick tunnel: {url}")
        self.assertIn("did not open a tunnel", said)

    def test_a_tunnel_that_never_comes_up_gives_cloudflareds_own_words(self):
        Fake(self.directory, lines="ERR failed to dial to edge: connection refused\n")
        url, said = cloudflare.publish(18499, timeout=3)
        self.assertIsNone(url)
        self.assertIn("failed to dial to edge", said)

    def test_a_binary_that_dies_on_start_is_reported_not_awaited(self):
        Fake(self.directory, lines="", linger=False, exit_code=1)
        started = time.monotonic()
        url, said = cloudflare.publish(18499, timeout=20)
        self.assertIsNone(url)
        self.assertLess(time.monotonic() - started, 10, "waited for a process that had already gone")

    # ---- one at a time, and taken down -----------------------------------------------------------
    def test_one_tunnel_at_a_time(self):
        Fake(self.directory, lines="https://first-one.trycloudflare.com\n")
        first, _ = cloudflare.publish(18499, timeout=10)
        again, said = cloudflare.publish(18499, timeout=10)
        self.assertEqual((first, again), ("https://first-one.trycloudflare.com",) * 2)
        self.assertIn("already open", said)
        running = cloudflare.current()
        self.assertIsNotNone(running)
        self.assertEqual(running.port, 18499)

    def test_unpublish_kills_the_child_it_started(self):
        Fake(self.directory, lines="https://going-away.trycloudflare.com\n")
        url, _ = cloudflare.publish(18499, timeout=10)
        self.assertTrue(url)
        child = cloudflare.current().process
        self.assertIsNone(child.poll(), "the fake should still be running")
        self.assertTrue(cloudflare.unpublish())
        self.assertIsNotNone(child.poll(), "the child outlived unpublish()")
        self.assertIsNone(cloudflare.current())
        self.assertFalse(cloudflare.unpublish(), "nothing to take down twice")

    def test_a_tunnel_this_process_did_not_start_is_not_ours_to_close(self):
        """`cloudflared` may be running for something else on this machine — another Relay, or the
        owner's own tunnel. unpublish() closes what this process opened and nothing else."""
        other = subprocess.Popen(["sleep", "30"])
        self.addCleanup(other.kill)
        self.assertFalse(cloudflare.unpublish())
        self.assertIsNone(other.poll(), "killed a process this module never started")


if __name__ == "__main__":
    unittest.main()
