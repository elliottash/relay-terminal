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
from tests.browser import SCREENS_SHOWN, Browser, find_chrome, shown

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
                    await browser.wait_for(shown('screen-inbox'),
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

                    # Exactly one screen is drawn. A stylesheet `display` rule once beat the
                    # [hidden] attribute and drew every screen at once on a real phone.
                    self.assertEqual(await browser.evaluate(SCREENS_SHOWN), 1)

                    # The inbox shows the panes the desktop published.
                    titles = await browser.wait_for(
                        "[...document.querySelectorAll('.pane-title')].map(n => n.textContent)")
                    self.assertIn("relay-terminal", titles)

                    # Open a pane and send a prompt; the agent's answer streams back.
                    await browser.evaluate(
                        "document.querySelectorAll('.pane-row')[0].click()")
                    await browser.wait_for(shown('screen-thread'))
                    self.assertEqual(await browser.evaluate(SCREENS_SHOWN), 1)
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
                    # One concise line per tool call (protocol section 23), not the tool's name and
                    # a slab of its arguments: the two reads fold into one line, the command that
                    # exited 1 is marked, and the short diff prints under its line without a tap.
                    tools = await browser.wait_for(
                        "(() => { const lines = [...document.querySelectorAll('.tool-line')]"
                        ".map(n => n.textContent);"
                        " return lines.some(l => l.startsWith('edited')) ? lines : null; })()",
                        timeout=40)
                    self.assertIn("read 2 files · 412 lines", tools)
                    self.assertIn("✗ ran pytest · 12 lines · exit 1 · 2.1 s", tools)
                    self.assertIn("edited router.py · +2 −1", tools)
                    self.assertNotIn("read_file", "".join(tools))
                    diff = await browser.evaluate(
                        "[...document.querySelectorAll('.tool-diff .diff-line')]"
                        ".map(n => n.className + ':' + n.textContent)")
                    self.assertTrue(any(line.startswith("diff-line add:+") for line in diff), diff)
                    self.assertTrue(any(line.startswith("diff-line del:-") for line in diff), diff)
                    # The detail is behind the disclosure, not printed into the transcript.
                    self.assertTrue(await browser.evaluate(
                        "[...document.querySelectorAll('details.tool')].every(d => !d.open)"))
                    self.assertIn("pytest -q tests/test_router.py", await browser.evaluate(
                        "[...document.querySelectorAll('.tool-detail')].map(n => n.textContent).join('\\n')"))
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

    def test_a_model_switch_mid_turn_shows_where_it_lands(self):
        # Issue 3ES1: the phone says what the desktop says - the switch accepted mid-turn, where it
        # took over, and a refusal - and its model indicator follows, back to the old model when a
        # switch is refused.
        async def main():
            async with Harness() as harness:
                url, _ = await harness.host.open_pairing()
                browser = Browser()
                await browser.start()
                try:
                    await browser.navigate(url)
                    await browser.wait_for(shown('screen-inbox'), timeout=60)
                    await browser.wait_for("document.querySelectorAll('.pane-row').length > 0")
                    await browser.evaluate("""[...document.querySelectorAll('.pane-row')].find(
                        row => row.querySelector('.pane-title')?.textContent === 'relay-terminal').click()""")
                    await browser.wait_for(shown('screen-thread'))
                    pane = "pane-1"             # DemoPaneSource's relay-terminal pane
                    lines = "[...document.querySelectorAll('.model-line')].map(n => n.textContent)"
                    indicator = "document.getElementById('thread-model')"

                    harness.source._emit(pane, {"event": "model_changed", "model": "kimi-k3",
                                                "applies": "next_step", "in_flight_model": "glm-5.3",
                                                "will_compact": True})
                    await browser.wait_for(f"{lines}.length === 1")
                    self.assertEqual(await browser.evaluate(lines), [
                        "↻ kimi-k3 takes over at the next step · glm-5.3 is not interrupted · will compact to fit"])
                    self.assertEqual(await browser.evaluate(f"{indicator}.textContent"),
                                     "kimi-k3 · glm-5.3 finishing the current step")
                    self.assertTrue(await browser.evaluate(f"{indicator}.classList.contains('waiting')"))
                    self.assertFalse(await browser.evaluate(f"{indicator}.hidden"))

                    harness.source._emit(pane, {"event": "model_applied", "model": "kimi-k3", "at": "step",
                                                "from_model": "glm-5.3", "history_converted": True,
                                                "compacted": True})
                    await browser.wait_for(f"{lines}.length === 2")
                    self.assertEqual((await browser.evaluate(lines))[1],
                                     "→ now on kimi-k3 · conversation converted from glm-5.3"
                                     " · compacted to fit its window")
                    self.assertEqual(await browser.evaluate(f"{indicator}.textContent"), "kimi-k3")
                    self.assertFalse(await browser.evaluate(f"{indicator}.classList.contains('waiting')"))

                    harness.source._emit(pane, {"event": "model_changed", "model": "tiny", "applies": "next_step",
                                                "in_flight_model": "kimi-k3"})
                    harness.source._emit(pane, {"event": "model_switch_refused", "model": "tiny", "at": "step",
                                                "current_model": "kimi-k3",
                                                "reason": "tiny cannot take over. Staying on kimi-k3."})
                    await browser.wait_for(f"{lines}.length === 4")
                    self.assertEqual((await browser.evaluate(lines))[3],
                                     "✗ tiny cannot take over. Staying on kimi-k3.")
                    self.assertEqual(await browser.evaluate(f"{indicator}.textContent"), "kimi-k3")
                    self.assertTrue(await browser.evaluate(
                        "document.querySelectorAll('.model-line')[3].classList.contains('error')"))
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
                        shown('pair-retry'), timeout=60)
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
