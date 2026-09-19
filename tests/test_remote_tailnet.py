# SPDX-License-Identifier: AGPL-3.0-or-later
"""`tailscale serve`: the warning-free address the share dialog offers (remote/tailnet.py).

Nothing here talks to a real tailscale daemon. A shell script called `tailscale` goes on PATH and
answers `status --json`, `serve status`, `serve --bg` and `serve reset`, so the failures that matter
— not installed, not logged in, no MagicDNS name, the operator not set, Serve not enabled on the
tailnet, a CLI that hangs — are all reachable from a test, and each one has to come back as one
sentence somebody could act on.
"""
import asyncio
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from remote import gui_host, tailnet

RUNNING = {"BackendState": "Running", "Self": {"DNSName": "spark-dcc9.tail6fb70c.ts.net."}}


class Fake:
    """A `tailscale` on PATH that says exactly what a test wants it to say."""

    def __init__(self, directory: Path, *, status=RUNNING, status_rc=0, status_text=None,
                 serve_status_rc=0, serve_status_text="No serve config", serve_rc=0,
                 serve_text="", sleep=0.0):
        self.directory = directory
        self.log = directory / "calls.log"
        (directory / "status.json").write_text(
            status_text if status_text is not None else json.dumps(status))
        (directory / "serve-status.txt").write_text(serve_status_text)
        (directory / "serve.txt").write_text(serve_text)
        binary = directory / "tailscale"
        binary.write_text(f"""#!/bin/sh
printf '%s\\n' "$*" >> {self.log}
case "$1 $2" in
  "status --json") cat {directory}/status.json; exit {status_rc} ;;
  "serve status") cat {directory}/serve-status.txt; exit {serve_status_rc} ;;
  "serve --bg") cat {directory}/serve.txt >&2; sleep {sleep}; exit {serve_rc} ;;
  "serve reset") exit 0 ;;
esac
echo "unknown: $*" >&2
exit 1
""")
        binary.chmod(0o755)

    def calls(self) -> list[str]:
        if not self.log.is_file():
            return []
        return [line for line in self.log.read_text().splitlines() if line]

    def on_path(self):
        return mock.patch.dict(os.environ,
                               {"PATH": f"{self.directory}{os.pathsep}{os.environ['PATH']}"})


class ProbeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)

    def test_a_running_tailscale_offers_its_magicdns_name(self):
        fake = Fake(self.directory)
        with fake.on_path():
            found = tailnet.probe()
        self.assertTrue(found.ready)
        self.assertEqual(found.name, "spark-dcc9.tail6fb70c.ts.net", "the trailing dot is dropped")
        self.assertEqual(found.url, "https://spark-dcc9.tail6fb70c.ts.net")
        self.assertEqual(found.reason, "")

    def test_no_tailscale_at_all_says_so_in_one_sentence(self):
        with mock.patch.dict(os.environ, {"PATH": str(self.directory)}):
            found = tailnet.probe()
        self.assertFalse(found.ready)
        self.assertEqual(found.name, "")
        self.assertIn("not installed", found.reason)
        self.assertEqual(len(found.reason.splitlines()), 1, "one line, for a label")

    def test_not_logged_in_says_which_command_to_run(self):
        fake = Fake(self.directory, status={"BackendState": "NeedsLogin"})
        with fake.on_path():
            found = tailnet.probe()
        self.assertFalse(found.ready)
        self.assertIn("tailscale up", found.reason)

    def test_switched_off_is_not_the_same_as_logged_out(self):
        fake = Fake(self.directory, status={"BackendState": "Stopped"})
        with fake.on_path():
            found = tailnet.probe()
        self.assertFalse(found.ready)
        self.assertIn("switched off", found.reason)

    def test_no_magicdns_name_points_at_the_admin_console(self):
        fake = Fake(self.directory, status={"BackendState": "Running", "Self": {"DNSName": ""}})
        with fake.on_path():
            found = tailnet.probe()
        self.assertFalse(found.ready)
        self.assertIn("MagicDNS", found.reason)

    def test_the_operator_hint_carries_the_command_verbatim(self):
        """The one setting a person is most likely not to have, so the sentence is the command."""
        fake = Fake(self.directory, serve_status_rc=1,
                    serve_status_text="access denied: must be operator or root")
        with fake.on_path():
            found = tailnet.probe()
        self.assertFalse(found.ready)
        self.assertEqual(found.name, "spark-dcc9.tail6fb70c.ts.net",
                         "the name is known even when we may not drive serve")
        self.assertIn("sudo tailscale set --operator=$USER", found.reason)

    def test_somebody_elses_serve_configuration_is_left_alone(self):
        """`serve reset` takes the whole node's configuration down, so Relay does not join a
        configuration it did not write."""
        fake = Fake(self.directory,
                    serve_status_text="https://spark-dcc9.tail6fb70c.ts.net (tailnet only)\n"
                                      "|-- / proxy http://192.168.1.50:3000")
        with fake.on_path():
            found = tailnet.probe()
        self.assertFalse(found.ready)
        self.assertIn("already serving something else", found.reason)

    def test_a_loopback_route_is_taken_as_relays_own(self):
        """Relay only ever writes a route to loopback, so finding one is finding its own."""
        fake = Fake(self.directory,
                    serve_status_text="https://spark-dcc9.tail6fb70c.ts.net (tailnet only)\n"
                                      "|-- / proxy http://127.0.0.1:8787")
        with fake.on_path():
            found = tailnet.probe()
        self.assertTrue(found.ready)

    def test_a_daemon_that_says_nothing_useful_is_still_one_sentence(self):
        fake = Fake(self.directory, status_rc=1, status_text="failed to connect to local tailscaled")
        with fake.on_path():
            found = tailnet.probe()
        self.assertFalse(found.ready)
        self.assertIn("tailscaled", found.reason)

    def test_garbage_from_status_is_not_an_exception(self):
        fake = Fake(self.directory, status_text="not json at all")
        with fake.on_path():
            found = tailnet.probe()
        self.assertFalse(found.ready)
        self.assertIn("JSON", found.reason)


class PublishTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)

    def test_publishing_runs_serve_bg_on_the_plain_http_port(self):
        fake = Fake(self.directory)
        with fake.on_path():
            url, said = tailnet.publish(8787)
        self.assertEqual(url, "https://spark-dcc9.tail6fb70c.ts.net")
        self.assertIn("8787", said)
        self.assertIn("serve --bg 8787", fake.calls())

    def test_publishing_refuses_before_running_serve_when_detection_said_no(self):
        fake = Fake(self.directory, status={"BackendState": "NeedsLogin"})
        with fake.on_path():
            url, said = tailnet.publish(8787)
        self.assertIsNone(url)
        self.assertIn("tailscale up", said)
        self.assertNotIn("serve --bg 8787", fake.calls(), "nothing is published on a no")

    def test_serve_not_enabled_on_the_tailnet_comes_back_in_tailscales_own_words(self):
        """The case on this machine today: everything local is right and the tailnet says no."""
        fake = Fake(self.directory, serve_rc=1,
                    serve_text="Serve is not enabled on your tailnet.\n"
                               "To enable, visit:\n\n   https://login.tailscale.com/f/serve?node=x")
        with fake.on_path():
            url, said = tailnet.publish(8787)
        self.assertIsNone(url)
        self.assertIn("Serve is not enabled", said)
        self.assertIn("https://login.tailscale.com/f/serve?node=x", said,
                      "tailscale's own one-click link for this node, kept verbatim")
        self.assertEqual(len(said.splitlines()), 1, "folded into one line for a label")

    def test_a_certificate_failure_names_the_admin_console_setting(self):
        """No link to click in this one, so it says which console setting to go and find."""
        fake = Fake(self.directory, serve_rc=1, serve_text="cert: no HTTPS certificate available")
        with fake.on_path():
            url, said = tailnet.publish(8787)
        self.assertIsNone(url)
        self.assertIn("cert: no HTTPS certificate available", said, "tailscale's own words first")
        self.assertIn("admin console", said)

    def test_a_tailscale_that_hangs_is_a_failure_rather_than_a_wait(self):
        """`serve --bg` waits for somebody to click a link when Serve is off, so it has a deadline."""
        fake = Fake(self.directory, sleep=30, serve_text="Serve is not enabled on your tailnet.")
        with fake.on_path():
            url, said = tailnet.publish(8787, timeout=1.0)
        self.assertIsNone(url)
        self.assertIn("Serve is not enabled", said)

    def test_unpublish_runs_serve_reset(self):
        fake = Fake(self.directory)
        with fake.on_path():
            self.assertTrue(tailnet.unpublish())
        self.assertIn("serve reset", fake.calls())

    def test_unpublish_without_tailscale_is_a_quiet_no(self):
        with mock.patch.dict(os.environ, {"PATH": str(self.directory)}):
            self.assertFalse(tailnet.unpublish())


# ---- the sidecar's address list (remote/gui_host.py) ------------------------------------------

def run(coroutine, timeout=60):
    return asyncio.run(asyncio.wait_for(coroutine, timeout))


class SidecarAddressTests(unittest.TestCase):
    """What the share dialog is offered, and what it is told when there is nothing to offer."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)
        self.home = self.directory / "home"
        self.home.mkdir()
        # The sidecar's identity lives under XDG_DATA_HOME; keep it out of the real profile.
        patch = mock.patch.dict(os.environ, {"XDG_DATA_HOME": str(self.home)})
        patch.start()
        self.addCleanup(patch.stop)

    async def sidecar(self, fake: Fake):
        """A started sidecar with no self-signed listener: `tls` false isolates the tailnet half."""
        side = gui_host.Sidecar()
        side.out = []
        side.emit = side.out.append
        with fake.on_path():
            await side.start({"port": 0, "tls": False, "name": "test desktop"})
        return side

    def started(self, side) -> dict:
        return [message for message in side.out if message.get("t") == "started"][-1]

    def entry(self, side, kind: str) -> dict:
        return [item for item in self.started(side)["addresses"] if item["kind"] == kind][0]

    def test_the_tailnet_name_is_offered_first_and_is_what_the_qr_gets(self):
        fake = Fake(self.directory)

        async def main():
            side = await self.sidecar(fake)
            try:
                first = self.started(side)["addresses"][0]
                self.assertEqual(first["kind"], "tailscale")
                self.assertTrue(first["available"])
                self.assertEqual(first["value"], "spark-dcc9.tail6fb70c.ts.net")
                self.assertIn("no certificate warning", first["where"])
                self.assertTrue(first["current"], "and it is the one in use")
                self.assertEqual(self.started(side)["base"],
                                 "https://spark-dcc9.tail6fb70c.ts.net")
                self.assertEqual(side.host.app_base, "https://spark-dcc9.tail6fb70c.ts.net",
                                 "so the pairing and invite links use it too")
                self.assertIn("serve --bg", " ".join(fake.calls()))
            finally:
                with fake.on_path():
                    await side.stop()
        run(main())

    def test_without_tailscale_the_entry_says_why_not(self):
        empty = self.directory / "empty"
        empty.mkdir()

        async def main():
            side = gui_host.Sidecar()
            side.out = []
            side.emit = side.out.append
            with mock.patch.dict(os.environ, {"PATH": str(empty)}):
                await side.start({"port": 0, "tls": False, "name": "test desktop"})
            try:
                first = self.started(side)["addresses"][0]
                self.assertEqual(first["kind"], "tailscale")
                self.assertFalse(first["available"])
                self.assertEqual(first["value"], "")
                self.assertIn("not installed", first["reason"])
                self.assertTrue(self.started(side)["base"].startswith("http://127.0.0.1"),
                                "and nothing was published")
            finally:
                await side.stop()
        run(main())

    def test_a_serve_that_fails_leaves_the_reason_where_the_dialog_can_show_it(self):
        fake = Fake(self.directory, serve_rc=1,
                    serve_text="Serve is not enabled on your tailnet.")

        async def main():
            side = await self.sidecar(fake)
            try:
                first = self.started(side)["addresses"][0]
                self.assertFalse(first["available"])
                self.assertIn("Serve is not enabled", first["reason"])
                self.assertFalse(side.served_by_tailscale)
            finally:
                with fake.on_path():
                    await side.stop()
        run(main())

    def test_stopping_resets_the_serve_configuration(self):
        fake = Fake(self.directory)

        async def main():
            side = await self.sidecar(fake)
            self.assertTrue(side.served_by_tailscale)
            with fake.on_path():
                await side.stop()
            self.assertIn("serve reset", fake.calls())
            self.assertFalse(side.served_by_tailscale)
        run(main())

    def test_switching_to_a_local_address_takes_the_serve_route_down(self):
        """Two origins answering at once would be one more place the pairing link could go stale."""
        fake = Fake(self.directory)

        async def main():
            side = await self.sidecar(fake)
            try:
                side.tls_port = 8443                       # as if --tls had been asked for
                with mock.patch("remote.devtls.local_addresses", return_value=["192.168.1.9"]), \
                        fake.on_path():
                    await side.set_address("192.168.1.9")
                self.assertIn("serve reset", fake.calls())
                self.assertFalse(side.served_by_tailscale)
                self.assertEqual(side.base, "https://192.168.1.9:8443")
                self.assertEqual(side.host.app_base, "https://192.168.1.9:8443")
            finally:
                with fake.on_path():
                    await side.stop()
        run(main())

    def test_choosing_the_tailnet_name_again_publishes_it_again(self):
        fake = Fake(self.directory)

        async def main():
            side = await self.sidecar(fake)
            try:
                side.tls_port = 8443
                with mock.patch("remote.devtls.local_addresses", return_value=["192.168.1.9"]), \
                        fake.on_path():
                    await side.set_address("192.168.1.9")
                    await side.set_address("spark-dcc9.tail6fb70c.ts.net")
                self.assertTrue(side.served_by_tailscale)
                self.assertEqual(side.base, "https://spark-dcc9.tail6fb70c.ts.net")
                self.assertEqual(len([call for call in fake.calls()
                                      if call.startswith("serve --bg")]), 2)
            finally:
                with fake.on_path():
                    await side.stop()
        run(main())


if __name__ == "__main__":
    unittest.main()
