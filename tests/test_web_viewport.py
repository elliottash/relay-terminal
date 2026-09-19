# SPDX-License-Identifier: GPL-3.0-or-later
"""The web client fits the part of the screen that is visible, on-screen keyboard or not.

Owner, 2026-09-18, on an iPad: "in portrait mode without on screen keyboard it looks good, but once
you have the on screen keyboard, it looks good in portrait but not landscape, it goes off screen."

Safari does not shrink the layout viewport for its keyboard; it lays the keyboard over it and
reports the smaller visible part only through `window.visualViewport`. Chrome can be made to do
exactly that: a fake `visualViewport` installed before any script runs, which the tests then move
the way Safari moves the real one. So the two things asserted are the two Safari needs —

* app/viewport.js pins the page to the visual viewport: the body is its height and sits at its
  offset, a pinch-zoom is not taken for a keyboard, and a short strip turns on the compact layout;
* in a strip the height of an iPad's landscape keyboard gap, the pane view (app/pane.js) keeps the
  terminal, the queue and the prompt box on screen instead of pushing the box off the bottom — in
  the demo page, and in the real client paired to a hub whose pane publishes `pane_state`, with a
  mouse and with an iPad's 44 px touch targets. The real client is the case that mattered: there
  the terminal's whole 24-row grid used to set the pane's height, and the prompt box sat at 916 px.

Skipped when Chrome is not installed.
"""
import asyncio
import json
import unittest

from tests.browser import Browser, find_chrome, shown
from tests.test_pane_view import ROOT, fixture, serve
from tests.test_remote_pane_state import Harness, state

# A fake visual viewport, as Safari's behaves with its keyboard up: smaller than the window, and
# able to scroll (offsetTop) inside it. Installed before the page's own scripts, like the real one.
FAKE_VIEWPORT = """
(() => {
  const fake = new EventTarget();
  fake.height = 330; fake.width = 1180; fake.offsetTop = 0; fake.offsetLeft = 0; fake.scale = 1;
  fake.move = (height, offsetTop, scale = 1) => {
    fake.height = height; fake.offsetTop = offsetTop; fake.scale = scale;
    fake.dispatchEvent(new Event('resize'));
  };
  Object.defineProperty(window, 'visualViewport', { value: fake, configurable: true });
})();
"""

# An iPad Pro 11" in landscape, and what its on-screen keyboard (with the QuickType bar) leaves.
IPAD_LANDSCAPE = (1194, 834)
KEYBOARD_GAP = 330
# The same iPad in portrait, keyboard down and up (owner: "same on portrait actually"), and an
# iPhone in portrait with its keyboard up, which is under the short-viewport line like landscape.
IPAD_PORTRAIT = (820, 1180)
PORTRAIT_KEYBOARD_GAP = 740
IPHONE_KEYBOARD_GAP = (390, 450)
# An iPhone in landscape with its keyboard up: 844x390 less a 205 px keyboard (relay-terminal-71's
# end-to-end matrix found send off screen here after the first fix).
IPHONE_LANDSCAPE_KEYBOARD_GAP = (844, 185)


def frames(n: int = 3) -> str:
    """JavaScript that resolves after n animation frames: viewport.js applies on the next one."""
    return ("new Promise(r => { let k = %d; const f = () => (--k ? requestAnimationFrame(f) : r(1));"
            " requestAnimationFrame(f); })" % n)


@unittest.skipUnless(find_chrome(), "Chrome is not installed")
class ViewportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server, cls.origin = serve(ROOT)

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def drive(self, coroutine, timeout=120):
        return asyncio.run(asyncio.wait_for(coroutine, timeout))

    async def window(self, browser: Browser, width: int, height: int) -> None:
        await browser.call("Emulation.setDeviceMetricsOverride",
                           {"width": width, "height": height, "deviceScaleFactor": 1, "mobile": False})

    def test_the_app_is_pinned_to_the_visible_part_of_the_screen(self):
        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.window(browser, *IPAD_LANDSCAPE)
                await browser.call("Page.addScriptToEvaluateOnNewDocument", {"source": FAKE_VIEWPORT})
                await browser.navigate(f"{self.origin}/app/index.html")
                await browser.wait_for("getComputedStyle(document.documentElement)"
                                       ".getPropertyValue('--app-height') === '330px'")

                async def body():
                    return json.loads(await browser.evaluate(
                        "JSON.stringify((r => [r.top, r.height])(document.body.getBoundingClientRect()))"))

                # The keyboard is up: the page is the strip above it, not the whole window.
                self.assertEqual(await body(), [0, KEYBOARD_GAP])
                self.assertTrue(await browser.evaluate(
                    "document.documentElement.classList.contains('short-viewport')"))

                # Safari scrolls the visual viewport to a focused box: the page follows it.
                await browser.evaluate("visualViewport.move(300, 40)")
                await browser.evaluate(frames(), timeout=10)
                self.assertEqual(await body(), [40, 300])

                # A pinch-zoom shrinks the visual viewport too, and must not squash the layout.
                await browser.evaluate("visualViewport.move(150, 0, 2)")
                await browser.evaluate(frames(), timeout=10)
                self.assertEqual(await body(), [40, 300])

                # The keyboard goes away: the whole window again, and the compact layout goes too.
                await browser.evaluate("visualViewport.move(%d, 0)" % IPAD_LANDSCAPE[1])
                await browser.evaluate(frames(), timeout=10)
                self.assertEqual(await body(), [0, IPAD_LANDSCAPE[1]])
                self.assertFalse(await browser.evaluate(
                    "document.documentElement.classList.contains('short-viewport')"))
            finally:
                await browser.stop()

        self.drive(main())

    def test_in_the_landscape_keyboard_gap_the_prompt_box_stays_on_screen(self):
        """The busiest fixture — reasoning, five queued rows — in the strip an iPad leaves."""
        state = fixture("busy_queue")
        self.assertGreaterEqual(len(state["queue"]["rows"]), 5)

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.window(browser, IPAD_LANDSCAPE[0], KEYBOARD_GAP)
                await browser.navigate(f"{self.origin}/app/pane-demo.html?fixture=busy_queue&bare=1")
                await browser.wait_for("document.body && document.body.dataset.demoReady === '1'"
                                       " && !!document.querySelector('.relay-pane textarea')")
                await browser.evaluate(frames(), timeout=10)
                # The whole composer — the box and the strip under it, where the send button is —
                # not only the box: a box on screen with its send button under the keyboard is the
                # same complaint.
                for part in (".relay-pane textarea", ".rp-composer"):
                    top, bottom = json.loads(await browser.evaluate(
                        "JSON.stringify((r => [r.top, r.bottom])("
                        f"document.querySelector('{part}').getBoundingClientRect()))"))
                    self.assertGreaterEqual(top, 0, part)
                    self.assertLessEqual(bottom, KEYBOARD_GAP, f"{part} ends at {bottom}px")
                # The queue gives way first: two rows' height and a scrollbar, not five rows.
                rows = json.loads(await browser.evaluate(
                    "JSON.stringify((l => [l.getBoundingClientRect().height, l.scrollHeight])("
                    "document.querySelector('.rp-rows')))"))
                self.assertLess(rows[0], rows[1])
                self.assertLess(rows[0], KEYBOARD_GAP / 3)
                # And the terminal still has room to be read.
                slot = await browser.evaluate(
                    "document.querySelector('.relay-pane').firstElementChild.getBoundingClientRect().height")
                self.assertGreater(slot, 40)
            finally:
                await browser.stop()

        self.drive(main())

    def paired_pane_at(self, height: int, *, touch: bool, width: int = IPAD_LANDSCAPE[0]) -> dict:
        """Pair the real client with a hub, open a pane that publishes pane_state, and measure."""
        async def main():
            async with Harness() as harness:
                url, _ = await harness.host.open_pairing()
                browser = Browser()
                await browser.start()
                try:
                    await browser.call("Emulation.setDeviceMetricsOverride",
                                       {"width": width, "height": height,
                                        "deviceScaleFactor": 1, "mobile": touch})
                    if touch:
                        await browser.call("Emulation.setTouchEmulationEnabled",
                                           {"enabled": True, "maxTouchPoints": 5})
                    await browser.navigate(url)
                    await browser.wait_for(shown("screen-inbox"), timeout=40)
                    harness.host.pane_state_from_gui(state())
                    await browser.evaluate("document.querySelectorAll('.pane-row')[0].click()")
                    await browser.wait_for("!!document.querySelector('#pane-view .rp-composer')", timeout=20)
                    await browser.evaluate(frames(), timeout=10)
                    return json.loads(await browser.evaluate("""JSON.stringify({
                      input: document.querySelector('.relay-pane').dataset.input,
                      parts: Object.fromEntries(['.rp-term-wrap', '.rp-queue', '.rp-composer', '#thread-bar',
                                                 '.rp-send']
                        .map(q => [q, (r => [r.top, r.bottom])(document.querySelector(q).getBoundingClientRect())])),
                    })"""))
                finally:
                    await browser.stop()

        return self.drive(main(), timeout=150)

    def test_the_real_client_fits_the_landscape_keyboard_gap(self):
        for touch in (False, True):
            with self.subTest(touch=touch):
                seen = self.paired_pane_at(KEYBOARD_GAP, touch=touch)
                self.assertEqual(seen["input"], "touch" if touch else "mouse")
                parts = seen["parts"]
                top, bottom = parts[".rp-composer"]
                self.assertGreaterEqual(top, 0)
                self.assertLessEqual(bottom, KEYBOARD_GAP, f"the prompt box ends at {bottom}px")
                self.assertGreaterEqual(parts[".rp-queue"][0], parts[".rp-term-wrap"][1])
                self.assertGreater(parts[".rp-term-wrap"][1] - parts[".rp-term-wrap"][0], 25)
                self.assertGreaterEqual(parts["#thread-bar"][0], 0)

    def test_the_real_client_fits_portrait_and_a_phone_with_the_keyboard_up(self):
        """Owner, 2026-09-18: "same on portrait actually, test it with the on screen keyboard"."""
        for width, height in ((IPAD_PORTRAIT[0], PORTRAIT_KEYBOARD_GAP), IPHONE_KEYBOARD_GAP,
                              IPAD_PORTRAIT, IPHONE_LANDSCAPE_KEYBOARD_GAP):
            with self.subTest(size=f"{width}x{height}"):
                parts = self.paired_pane_at(height, touch=True, width=width)["parts"]
                top, bottom = parts[".rp-composer"]
                self.assertGreaterEqual(top, 0)
                self.assertLessEqual(bottom, height, f"the prompt box ends at {bottom}px")
                # The send button, not only the box: it is the one that went off at 185 px.
                send_bottom = parts[".rp-send"][1]
                self.assertLessEqual(send_bottom, height, f"send ends at {send_bottom}px")
                if height >= 260:   # under that, the thread bar steps aside until the keyboard goes
                    self.assertGreaterEqual(parts["#thread-bar"][0], 0)
                self.assertGreater(parts[".rp-term-wrap"][1] - parts[".rp-term-wrap"][0], 12)

    def test_the_real_client_fits_landscape_without_a_keyboard(self):
        """834 px: nothing gives way here, and the pane must still end on the screen."""
        parts = self.paired_pane_at(IPAD_LANDSCAPE[1], touch=True)["parts"]
        self.assertLessEqual(parts[".rp-composer"][1], IPAD_LANDSCAPE[1])
        # With room, the terminal grows back past the two lines it is held to in the keyboard gap:
        # ten rows at least, beside a queue of six 44 px touch rows and the prompt box.
        self.assertGreater(parts[".rp-term-wrap"][1] - parts[".rp-term-wrap"][0], 150)


if __name__ == "__main__":
    unittest.main()
