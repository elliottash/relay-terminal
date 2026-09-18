# SPDX-License-Identifier: GPL-3.0-or-later
"""The real web client, in a real browser, against the real rendezvous and host.

This is the check that the thing a phone runs actually works: WebCrypto's X25519 and AES-GCM, a
non-extractable key in IndexedDB, the Noise handshake against remote/noise.py, and the pairing,
inbox and thread screens. ``http://127.0.0.1`` is a secure context, so WebCrypto is available
without a certificate.

Skipped when Chrome is not installed.
"""
import asyncio
import tempfile
import unittest
from pathlib import Path

from remote import host as host_mod
from remote import identity as identity_mod
from remote import panes as panes_mod
from remote import wire
from rendezvous.server import Store, build
from tests.browser import Browser, find_chrome

APP_DIR = Path(__file__).resolve().parent.parent / "app"


class Harness:
    def __init__(self, capability=wire.AGENT, approve=True):
        self.capability, self.approve = capability, approve
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
        self.source = panes_mod.DemoPaneSource()

        async def approver(request):
            self.requests.append(request)
            return self.approve, self.capability

        self.host = host_mod.Host(self.identity, self.devices, self.source, app_base=self.base,
                                  approver=approver, name="test desktop")
        await self.host.register(self.base)
        self.serving = asyncio.create_task(self.host.serve())
        for _ in range(100):
            await asyncio.sleep(0.02)
            if self.host.socket is not None:
                break
        return self

    async def __aexit__(self, *exc):
        await self.host.stop()
        self.serving.cancel()
        import contextlib
        with contextlib.suppress(asyncio.CancelledError):
            await self.serving
        await self.server.close()
        self.store.close()
        self.temporary.cleanup()


@unittest.skipUnless(find_chrome(), "no Chrome or Chromium installed")
class BrowserClientTests(unittest.TestCase):
    def test_pair_and_drive_a_pane_from_the_browser(self):
        async def main():
            async with Harness() as harness:
                url, _ = await harness.host.open_pairing()
                browser = Browser()
                await browser.start()
                try:
                    await browser.navigate(url)

                    # Pairing screen, with a confirmation code both ends derived independently.
                    await browser.wait_for(
                        "document.getElementById('pair-code')?.textContent?.match(/^\\d{5}$/) "
                        "? document.getElementById('pair-code').textContent : ''", timeout=40)
                    code = await browser.evaluate(
                        "document.getElementById('pair-code').textContent")
                    await browser.wait_for("!document.getElementById('screen-inbox').hidden",
                                           timeout=40)
                    self.assertEqual(len(harness.requests), 1)
                    self.assertEqual(code, harness.requests[0].code)
                    self.assertEqual(len(harness.devices.live()), 1)

                    # The device key is in IndexedDB and is not extractable.
                    extractable = await browser.evaluate("""
                        (async () => {
                          const db = await new Promise((res, rej) => {
                            const r = indexedDB.open('relay-remote', 1);
                            r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error);
                          });
                          const record = await new Promise((res, rej) => {
                            const r = db.transaction('device').objectStore('device').get('paired');
                            r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error);
                          });
                          return record.devicePrivate.extractable;
                        })()
                    """)
                    self.assertFalse(extractable, "the device key must not be extractable")

                    # The inbox shows the panes the desktop published.
                    titles = await browser.wait_for(
                        "[...document.querySelectorAll('.pane-title')].map(n => n.textContent)")
                    self.assertIn("relay-terminal", titles)

                    # Open a pane and send a prompt; the agent's answer streams back.
                    await browser.evaluate(
                        "document.querySelectorAll('.pane-row')[0].click()")
                    await browser.wait_for("!document.getElementById('screen-thread').hidden")
                    await browser.evaluate("""
                        (() => {
                          const box = document.getElementById('composer-text');
                          box.value = 'why does the router send this to the shell?';
                          document.getElementById('composer-send').click();
                          return true;
                        })()
                    """)
                    answer = await browser.wait_for(
                        "document.querySelector('.answer')?.textContent || ''", timeout=40)
                    self.assertIn("router", answer)
                    tools = await browser.evaluate(
                        "[...document.querySelectorAll('.tool-name')].map(n => n.textContent)")
                    self.assertIn("read_file", tools)
                    prompt = await browser.evaluate(
                        "document.querySelector('.prompt-text')?.textContent || ''")
                    self.assertIn("router", prompt)
                    origin = await browser.evaluate(
                        "document.querySelector('.prompt-origin')?.textContent || ''")
                    self.assertTrue(origin.startswith("remote:"), origin)

                    # A plan card arrives and executes by id, never by path.
                    await browser.wait_for("document.querySelector('.plan-title') !== null",
                                           timeout=40)

                    problems = [line for line in browser.console
                                if "EXCEPTION" in line or "error:" in line.lower()]
                    self.assertEqual(problems, [], f"console errors: {problems}")
                finally:
                    await browser.stop()
        asyncio.run(asyncio.wait_for(main(), 180))

    def test_a_refused_device_shows_the_reason_and_stores_nothing(self):
        async def main():
            async with Harness(approve=False) as harness:
                url, _ = await harness.host.open_pairing()
                browser = Browser()
                await browser.start()
                try:
                    await browser.navigate(url)
                    await browser.wait_for(
                        "document.getElementById('pair-retry') && "
                        "!document.getElementById('pair-retry').hidden", timeout=60)
                    stored = await browser.evaluate("""
                        (async () => {
                          const db = await new Promise((res, rej) => {
                            const r = indexedDB.open('relay-remote', 1);
                            r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error);
                          });
                          if (!db.objectStoreNames.contains('device')) return null;
                          return await new Promise((res, rej) => {
                            const r = db.transaction('device').objectStore('device').get('paired');
                            r.onsuccess = () => res(r.result || null); r.onerror = () => rej(r.error);
                          });
                        })()
                    """)
                    self.assertIsNone(stored)
                    self.assertEqual(harness.devices.live(), [])
                finally:
                    await browser.stop()
        asyncio.run(asyncio.wait_for(main(), 180))


if __name__ == "__main__":
    unittest.main()
