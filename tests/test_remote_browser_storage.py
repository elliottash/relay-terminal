# SPDX-License-Identifier: AGPL-3.0-or-later
"""A paired phone whose storage is slow or gone is never shown the pairing form as if new (#SAW4).

The owner's iPhone was paired thirteen times in a week. After each session the phone never dialled
the desktop again: the app's default screen was the pairing form, and it only left it once the
device record had been read from IndexedDB. WebKit can leave that read unanswered after iOS
cold-starts a suspended Home Screen app, so a phone whose pairing was fine sat on the form and was
paired again.

Here the real app, in a real browser, against the real rendezvous and host:

* with IndexedDB stalled, it shows "this phone is paired with <desktop>" and retries, and once
  storage answers it lands in the inbox with no second pairing asked for;
* with the record really gone, it says the pairing key was lost rather than presenting the form
  as a first pairing.

Skipped when Chrome is not installed.
"""
import asyncio
import unittest

from tests.browser import Browser, find_chrome, shown
from tests.test_remote_browser import Harness

# Installed before any page script: while `localStorage.stallIdb` is set, `indexedDB.open` returns
# a request that never fires either callback — what a cold-started iOS web app can get.
STALL = """
(() => {
  const real = indexedDB.open.bind(indexedDB);
  indexedDB.open = (...args) => (localStorage.getItem('stallIdb') ? {} : real(...args));
})();
"""


@unittest.skipUnless(find_chrome(), "Chrome is not installed")
class StorageTests(unittest.TestCase):
    async def _paired(self, harness, browser):
        url, _ = await harness.host.open_pairing()
        await browser.call("Page.addScriptToEvaluateOnNewDocument", {"source": STALL})
        await browser.navigate(url)
        await browser.wait_for(shown("screen-inbox"), timeout=40)
        return url.split("/pair#", 1)[0] + "/"

    def test_a_stalled_storage_says_paired_and_recovers_without_pairing_again(self):
        async def main():
            async with Harness() as harness:
                browser = Browser()
                await browser.start()
                try:
                    root = await self._paired(harness, browser)
                    await browser.evaluate("localStorage.setItem('stallIdb', '1')")
                    await browser.navigate(root)
                    # Before storage has answered: never the pairing form.
                    await asyncio.sleep(1.0)
                    self.assertTrue(await browser.evaluate(shown("screen-start")))
                    self.assertFalse(await browser.evaluate(shown("screen-welcome")))
                    # Both bounded opens time out, and the app says what is going on.
                    await browser.wait_for(
                        "document.getElementById('start-note').textContent"
                        ".includes('paired with test desktop')", timeout=20)
                    self.assertFalse(await browser.evaluate(shown("screen-welcome")))
                    self.assertTrue(await browser.evaluate(shown("start-retry")))
                    # Storage comes back: the app's own retry reaches the inbox.
                    await browser.evaluate("localStorage.removeItem('stallIdb')")
                    await browser.wait_for(shown("screen-inbox"), timeout=40)
                    self.assertEqual(len(harness.requests), 1, "no second pairing was asked for")
                finally:
                    await browser.stop()
        asyncio.run(asyncio.wait_for(main(), 180))

    def test_a_record_that_is_really_gone_is_named_as_lost(self):
        async def main():
            async with Harness() as harness:
                browser = Browser()
                await browser.start()
                try:
                    root = await self._paired(harness, browser)
                    # iOS cleared the database; the localStorage note survived.
                    await browser.evaluate("""new Promise((resolve) => {
                        const r = indexedDB.open('relay-remote', 1);
                        r.onsuccess = () => {
                          const tx = r.result.transaction('device', 'readwrite');
                          tx.objectStore('device').delete('paired');
                          tx.oncomplete = () => { r.result.close(); resolve(true); };
                        };
                    })""")
                    await browser.navigate(root)
                    await browser.wait_for(shown("screen-welcome"), timeout=40)
                    note = await browser.evaluate(
                        "document.getElementById('welcome-note').textContent")
                    self.assertIn("was paired with test desktop", note)
                    self.assertIn("no longer in this app's storage", note)
                finally:
                    await browser.stop()
        asyncio.run(asyncio.wait_for(main(), 180))

    def test_a_first_visit_still_reaches_the_pairing_form(self):
        async def main():
            async with Harness() as harness:
                browser = Browser()
                await browser.start()
                try:
                    await browser.navigate(harness.base + "/")
                    await browser.wait_for(shown("screen-welcome"), timeout=40)
                    note = await browser.evaluate(
                        "document.getElementById('welcome-note').textContent")
                    self.assertNotIn("was paired", note)
                finally:
                    await browser.stop()
        asyncio.run(asyncio.wait_for(main(), 180))


if __name__ == "__main__":
    unittest.main()
