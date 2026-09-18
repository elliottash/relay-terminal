# SPDX-License-Identifier: GPL-3.0-or-later
"""A real shell, shared to a real browser: the screen stream and take-over end to end.

These run a `relay-screen-bridge` (Relay's own emulator around a PTY), share it through the
rendezvous, and drive it from the web client in headless Chrome. Skipped when the bridge is not
built or Chrome is missing, because both are optional parts of a checkout.
"""
import asyncio
import contextlib
import os
import tempfile
import unittest
from pathlib import Path

from remote import client as client_mod
from remote import host as host_mod
from remote import identity as identity_mod
from remote import terminal as terminal_mod
from remote import wire
from rendezvous.server import Store, build
from tests.browser import SCREENS_SHOWN, Browser, find_chrome, shown

APP_DIR = Path(__file__).resolve().parent.parent / "app"
BRIDGE = terminal_mod.find_bridge()

SHELL_ENV = {"PATH": "/usr/bin:/bin", "TERM": "xterm-256color", "HOME": "/tmp",
             "PS1": "$ ", "SHELL": "/bin/bash"}


class Harness:
    def __init__(self, capability=wire.FULL):
        self.capability = capability
        self.requests = []

    async def __aenter__(self):
        self.temporary = tempfile.TemporaryDirectory()
        directory = Path(self.temporary.name)
        self.store = Store(":memory:")
        self.server = build(self.store, static_root=APP_DIR)
        await self.server.start("127.0.0.1", 0)
        self.base = f"http://127.0.0.1:{self.server.port}"

        self.identity = identity_mod.Identity.create(directory)
        self.devices = identity_mod.DeviceStore(directory)
        self.source = terminal_mod.TerminalPaneSource(BRIDGE, rows=12, cols=60,
                                                      shell="/bin/bash")

        async def approver(request):
            self.requests.append(request)
            return True, self.capability

        self.host = host_mod.Host(self.identity, self.devices, self.source, app_base=self.base,
                                  approver=approver, name="test desktop")
        await self.host.register(self.base)
        self.serving = asyncio.create_task(self.host.serve())
        for _ in range(100):
            await asyncio.sleep(0.02)
            if self.host.socket is not None:
                break
        self.pane = await self.source.open(title="shared", cwd="/tmp")
        await asyncio.sleep(0.8)          # let bash draw its first prompt
        return self

    async def __aexit__(self, *exc):
        await self.source.close_all()
        await self.host.stop()
        self.serving.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await self.serving
        await self.server.close()
        self.store.close()
        self.temporary.cleanup()

    async def paired_client(self):
        url, _ = await self.host.open_pairing()
        pairing = client_mod.Client(self.base)
        record = await pairing.pair(url, name="Pixel 9", platform="Chrome")
        await pairing.close()
        client = client_mod.Client(self.base)
        await client.connect(record)
        await client.expect("panes")
        return client, record

    def screen_text(self) -> str:
        pane = self.source.pane(self.pane.id)
        rows = []
        for row in range(pane.rows):
            rows.append("".join(segment[0] for segment in pane.lines.get(row, [])))
        return "\n".join(rows)


@unittest.skipUnless(BRIDGE, "relay-screen-bridge is not built")
class ScreenStreamTests(unittest.TestCase):
    def test_a_command_reaches_the_screen_stream(self):
        async def main():
            async with Harness() as harness:
                client, _ = await harness.paired_client()
                await client.send({"t": "pane_focus", "pane": harness.pane.id})
                snapshot = await client.expect("screen_snapshot")
                self.assertEqual(snapshot["rows"], 12)
                self.assertEqual(snapshot["cols"], 60)

                await client.send({"t": "line", "pane": harness.pane.id,
                                   "text": "echo remote-works-1234"})
                deadline = asyncio.get_event_loop().time() + 15
                seen = ""
                while "remote-works-1234" not in seen:
                    if asyncio.get_event_loop().time() > deadline:
                        self.fail(f"never saw the output; screen was:\n{harness.screen_text()}")
                    message = await asyncio.wait_for(client.inbox.get(), 10)
                    if message["t"] in ("screen_diff", "screen_snapshot"):
                        for line in message["lines"]:
                            seen += "".join(segment[0] for segment in line["segs"])
                await client.close()
        asyncio.run(asyncio.wait_for(main(), 120))

    def test_the_phone_cannot_resize_the_host(self):
        """There is no resize message in RRP/1; the host stays authoritative."""
        async def main():
            async with Harness() as harness:
                client, _ = await harness.paired_client()
                await client.send({"t": "resize", "pane": harness.pane.id, "rows": 4, "cols": 20})
                error = await client.expect("error")
                self.assertEqual(error["code"], "unknown_type")
                self.assertEqual(harness.source.pane(harness.pane.id).cols, 60)
                await client.close()
        asyncio.run(asyncio.wait_for(main(), 120))

    def test_a_view_device_cannot_type(self):
        async def main():
            async with Harness(capability=wire.VIEW) as harness:
                client, _ = await harness.paired_client()
                await client.send({"t": "pane_focus", "pane": harness.pane.id})
                await client.expect("screen_snapshot")
                await client.send({"t": "line", "pane": harness.pane.id, "text": "whoami"})
                error = await client.expect("error")
                self.assertEqual(error["code"], "not_permitted")
                await client.close()
        asyncio.run(asyncio.wait_for(main(), 120))

    def test_input_is_refused_at_a_password_prompt(self):
        """The rule that makes §6.7 worth having: `line` must not become a shell command."""
        async def main():
            async with Harness() as harness:
                client, _ = await harness.paired_client()
                await client.send({"t": "pane_focus", "pane": harness.pane.id})
                await client.expect("screen_snapshot")

                # `read -s` puts the tty in canonical mode with echo off, exactly as sudo does.
                await client.send({"t": "line", "pane": harness.pane.id,
                                   "text": "read -s -p 'Password: ' secret"})
                for _ in range(60):
                    await asyncio.sleep(0.1)
                    if terminal_mod.secret_prompt(harness.source.pane(harness.pane.id).shell_pid):
                        break
                else:
                    self.skipTest("the shell never entered a password prompt")

                self.assertEqual(harness.source.pane(harness.pane.id).status, "password")
                await client.send({"t": "line", "pane": harness.pane.id, "text": "hunter2"})
                error = await client.expect("error")
                self.assertEqual(error["code"], "not_permitted")
                self.assertNotIn("hunter2", harness.screen_text())
                await client.close()
        asyncio.run(asyncio.wait_for(main(), 120))

    def test_revoking_drops_the_keyboard(self):
        async def main():
            async with Harness() as harness:
                client, record = await harness.paired_client()
                await client.send({"t": "pane_focus", "pane": harness.pane.id})
                await client.expect("screen_snapshot")
                await client.send({"t": "control_request", "pane": harness.pane.id})
                await asyncio.sleep(0.3)
                self.assertEqual(harness.source.pane(harness.pane.id).driver, record.device_id)

                harness.devices.revoke(record.device_id)
                await asyncio.sleep(0.4)
                self.assertIsNone(harness.source.pane(harness.pane.id).driver)
                await client.close()
        asyncio.run(asyncio.wait_for(main(), 120))


@unittest.skipUnless(BRIDGE and find_chrome(), "needs the screen bridge and Chrome")
class BrowserTerminalTests(unittest.TestCase):
    def test_watch_and_type_from_the_browser(self):
        async def main():
            async with Harness() as harness:
                url, _ = await harness.host.open_pairing()
                browser = Browser()
                await browser.start()
                try:
                    await browser.navigate(url)
                    await browser.wait_for(shown('screen-inbox'),
                                           timeout=40)
                    await browser.evaluate("document.querySelectorAll('.pane-row')[0].click()")
                    # The Terminal tab is chosen automatically when the desktop offers a screen.
                    await browser.wait_for(shown('terminal-pane'),
                                           timeout=20)
                    await browser.wait_for(
                        "document.querySelectorAll('.screen-row').length > 1", timeout=30)

                    # One screen, one prompt box. The box is always there — it routes what you
                    # type — but the program's own keys wait until you take over.
                    self.assertEqual(await browser.evaluate(SCREENS_SHOWN), 1)
                    self.assertTrue(await browser.evaluate(shown('composer')))
                    self.assertFalse(await browser.evaluate(shown('term-keys')))
                    self.assertEqual(
                        await browser.evaluate(
                            "document.getElementById('composer-text').placeholder"),
                        "Ask or run…")

                    await browser.evaluate("document.getElementById('term-take').click()")
                    await browser.wait_for(shown('term-keys'), timeout=20)
                    # Taking over turns the same box into the program's line.
                    self.assertEqual(
                        await browser.evaluate(
                            "document.getElementById('composer-text').placeholder"),
                        "Type a line for the program…")

                    await browser.evaluate("""
                        (() => {
                          const box = document.getElementById('composer-text');
                          box.value = 'echo browser-typed-9876';
                          document.getElementById('composer-send').click();
                          return true;
                        })()
                    """)
                    await browser.wait_for(
                        "document.querySelector('.screen-grid').textContent"
                        ".includes('browser-typed-9876')", timeout=30)

                    # And the desktop's own view of the shell agrees.
                    deadline = asyncio.get_event_loop().time() + 15
                    while "browser-typed-9876" not in harness.screen_text():
                        if asyncio.get_event_loop().time() > deadline:
                            self.fail("the host never saw the command")
                        await asyncio.sleep(0.2)

                    problems = [line for line in browser.console if "EXCEPTION" in line]
                    self.assertEqual(problems, [], f"console errors: {problems}")
                finally:
                    await browser.stop()
        asyncio.run(asyncio.wait_for(main(), 240))


@unittest.skipUnless(BRIDGE and find_chrome(), "needs the screen bridge and Chrome")
class ViewOnlyTests(unittest.TestCase):
    """Allowed for viewing: the screen arrives, the keyboard is never offered."""

    def test_a_view_device_watches_but_is_not_offered_the_keyboard(self):
        async def main():
            async with Harness(capability=wire.VIEW) as harness:
                url, _ = await harness.host.open_pairing()
                browser = Browser()
                await browser.start()
                try:
                    await browser.navigate(url)
                    await browser.wait_for(shown('screen-inbox'), timeout=40)
                    await browser.evaluate("document.querySelectorAll('.pane-row')[0].click()")
                    await browser.wait_for(shown('terminal-pane'), timeout=20)
                    await browser.wait_for("document.querySelectorAll('.screen-row').length > 1",
                                           timeout=30)

                    # Watching works: what the desktop runs shows up.
                    await harness.source.send_line(
                        harness.pane.id, "echo view-only-7788", device="owner")
                    await browser.wait_for(
                        "document.querySelector('.screen-grid').textContent"
                        ".includes('view-only-7788')", timeout=30)

                    # Typing is never offered, and the reason is on screen.
                    self.assertFalse(await browser.evaluate(shown('term-take')))
                    self.assertFalse(await browser.evaluate(shown('composer')))
                    self.assertFalse(await browser.evaluate(shown('term-keys')))
                    note = await browser.evaluate(
                        "document.getElementById('term-note').textContent")
                    self.assertIn("viewing only", note.lower())
                    self.assertEqual(
                        await browser.evaluate("document.getElementById('capability').textContent"),
                        "view")

                    problems = [line for line in browser.console if "EXCEPTION" in line]
                    self.assertEqual(problems, [], f"console errors: {problems}")
                finally:
                    await browser.stop()
        asyncio.run(asyncio.wait_for(main(), 240))


@unittest.skipUnless(BRIDGE and find_chrome(), "needs the screen bridge and Chrome")
class DirectTypingTests(unittest.TestCase):
    """The tablet path: a real keyboard, every key straight through to the program."""

    def test_keys_go_through_one_at_a_time(self):
        async def main():
            async with Harness() as harness:
                url, _ = await harness.host.open_pairing()
                browser = Browser()
                await browser.start()
                try:
                    await browser.navigate(url)
                    await browser.wait_for(shown('screen-inbox'), timeout=40)
                    await browser.evaluate("document.querySelectorAll('.pane-row')[0].click()")
                    await browser.wait_for(shown('terminal-pane'), timeout=20)
                    await browser.wait_for("document.querySelectorAll('.screen-row').length > 1",
                                           timeout=30)
                    await browser.evaluate("document.getElementById('term-take').click()")
                    await browser.wait_for(shown('composer'), timeout=20)

                    # Turn on direct typing: the line box goes, the keyboard target arrives.
                    await browser.evaluate("document.getElementById('term-direct').click()")
                    await browser.wait_for("!" + shown('composer'), timeout=10)
                    self.assertEqual(
                        await browser.evaluate("document.activeElement.id"), "term-capture")

                    # Type it a key at a time, the way a keyboard does.
                    await browser.evaluate("""
                        (() => {
                          const target = document.getElementById('term-capture');
                          const send = (key, init = {}) => target.dispatchEvent(
                            new KeyboardEvent('keydown', {key, bubbles: true, cancelable: true,
                                                          ...init}));
                          for (const key of 'echo direct-keys-4242') send(key);
                          send('Enter');
                          return true;
                        })()
                    """)
                    deadline = asyncio.get_event_loop().time() + 20
                    while "direct-keys-4242" not in harness.screen_text():
                        if asyncio.get_event_loop().time() > deadline:
                            self.fail(f"never arrived; screen was:\n{harness.screen_text()}")
                        await asyncio.sleep(0.2)

                    # Ctrl and the arrow keys are encoded, not sent as letters.
                    await browser.evaluate("""
                        (() => {
                          const target = document.getElementById('term-capture');
                          target.dispatchEvent(new KeyboardEvent('keydown',
                            {key: 'c', ctrlKey: true, bubbles: true, cancelable: true}));
                          return true;
                        })()
                    """)
                    await asyncio.sleep(1)
                    self.assertNotIn("direct-keys-4242c", harness.screen_text())

                    problems = [line for line in browser.console if "EXCEPTION" in line]
                    self.assertEqual(problems, [], f"console errors: {problems}")
                finally:
                    await browser.stop()
        asyncio.run(asyncio.wait_for(main(), 240))


@unittest.skipUnless(BRIDGE, "relay-screen-bridge is not built")
class BridgeTests(unittest.TestCase):
    def test_the_bridge_reports_a_password_prompt(self):
        async def main():
            source = terminal_mod.TerminalPaneSource(BRIDGE, rows=10, cols=40, shell="/bin/bash")
            pane = await source.open(cwd="/tmp")
            try:
                await asyncio.sleep(0.8)
                self.assertIn(pane.status, ("idle", "running"))
                await source.send_line(pane.id, "read -s -p 'Password: ' x", device="test")
                for _ in range(60):
                    await asyncio.sleep(0.1)
                    if pane.status == "password":
                        break
                self.assertEqual(pane.status, "password")
                with self.assertRaises(wire.WireError):
                    await source.send_line(pane.id, "hunter2", device="test")
            finally:
                await source.close_all()
        asyncio.run(asyncio.wait_for(main(), 60))

    def test_colours_and_styles_survive_the_trip(self):
        async def main():
            source = terminal_mod.TerminalPaneSource(BRIDGE, rows=10, cols=60, shell="/bin/bash")
            pane = await source.open(cwd="/tmp")
            try:
                await asyncio.sleep(0.6)
                await source.send_line(
                    pane.id, r"printf '\033[1;31mRED\033[0m plain\n'", device="test")
                await asyncio.sleep(1.2)
                bold_red = None
                for segments in pane.lines.values():
                    for text, fg, bg, attrs in segments:
                        if "RED" in text and attrs & 1:
                            bold_red = (fg, attrs)
                self.assertIsNotNone(bold_red, "the styled run never arrived")
                self.assertEqual(bold_red[0] >> 24, 2, "expected an RGB colour from the palette")
            finally:
                await source.close_all()
        asyncio.run(asyncio.wait_for(main(), 60))


if __name__ == "__main__":
    unittest.main()
