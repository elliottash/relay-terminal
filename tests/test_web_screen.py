# SPDX-License-Identifier: AGPL-3.0-or-later
"""What the terminal view costs a phone, measured in a real browser.

Card #3H5T, findings 3 and 4, measured on the owner's Pixel 8 against a LAN share
(`docs/qa_evidence/2026-09-20-perf-fixes/phone/RESULTS.md`): a 20 000-character reply made the
phone send **188 `history_get` in about five seconds**, 121 of them refused `rate_limited` — and
the refusal was drawn under the composer, in red, while the agent was merely typing. `fit()` was
the top JS function of the whole profile at 22 % of non-idle JS, because it read `clientWidth` and
`getComputedStyle` on every frame the desktop sent.

Both are costs, not behaviours, so they are asserted as counts: how many requests a streamed reply
provokes, and how many times the view makes the browser resolve layout and style. `tests/
screen_harness.html` is the bench — the real `ScreenView` with this page standing in for the
desktop; `tests/test_remote_browser.py` drives the same view against a real host for the
behaviour, and `test_a_refused_page_is_never_the_readers_problem` below does the end-to-end half
of this one, against a desktop whose budget has been turned down until it refuses.

Skipped when Chrome is not installed.
"""
import asyncio
import json
import unittest

from remote import host as host_mod
from remote import wire
from tests.browser import Browser, find_chrome
from tests.test_pane_view import ROOT, serve
from tests.test_remote_browser import Harness, ScrollbackSource

# The frames one streamed reply is, and how fast they arrive. The Pixel measured 5.4 screen
# snapshots a second over a 30-second reply; this is the same shape in a fifth of the time, which
# is what keeps the test honest about a rate limit measured per minute.
FRAMES = 200
FRAME_MS = 10


@unittest.skipUnless(find_chrome(), "Chrome is not installed")
class ScreenCostTests(unittest.TestCase):
    """app/screen.js on the bench, with the desktop played by tests/screen_harness.html."""

    @classmethod
    def setUpClass(cls):
        cls.server, cls.origin = serve(ROOT)

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def drive(self, coroutine, timeout=120):
        return asyncio.run(asyncio.wait_for(coroutine, timeout))

    async def bench(self, browser: Browser, **options) -> None:
        """A view watching a pane, with the first page of scrollback already in."""
        await browser.navigate(f"{self.origin}/tests/screen_harness.html?count=1")
        await browser.wait_for("document.body && document.body.dataset.harnessReady === '1'")
        await browser.evaluate(f"screenHarness.open({json.dumps(options)})")
        await browser.wait_for("screenHarness.report().held >= 80", timeout=20)
        await browser.evaluate("screenHarness.zero()")

    def test_a_streamed_reply_does_not_ask_for_scrollback_once_a_frame(self):
        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.bench(browser)
                # Two seconds of output at the live end. Every frame pushes a row off the screen
                # into the scrollback, so the seam between the held rows and the live block
                # reopens 200 times; before the fix that was 200 `history_get`.
                await browser.evaluate(f"screenHarness.stream({FRAMES}, {FRAME_MS})", timeout=60)
                report = await browser.evaluate("screenHarness.report()")
                self.assertLessEqual(
                    report["asks"], 8,
                    f"{report['asks']} history_get for {FRAMES} frames of output; the desktop's "
                    f"budget is 120 a minute")
                self.assertTrue(report["atBottom"])

                # And the gap left open while the reader watched the live end closes when they
                # move off it — in one request for the whole gap, not a page of it at a time.
                gap = report["gap"]
                self.assertGreater(gap, 0)
                await browser.evaluate("screenHarness.zero()")
                await browser.wait_for(
                    "(() => { if (screenHarness.report().gap === 0) return true;"
                    " const w = document.getElementById('screen-wrap');"
                    " w.scrollTop = w.scrollHeight - w.clientHeight - 300;"
                    " return false; })()", timeout=30)
                after = await browser.evaluate("screenHarness.report()")
                self.assertLessEqual(after["asks"], 1 + gap // 200,
                                     f"a {gap}-row gap took {after['asks']} requests")
                self.assertEqual(max(after["wanted"]), min(gap, 200),
                                 "the gap was fetched a page at a time")

                # Cheaper had better still be right: the column is one run of numbers, no hole
                # where the buffer meets the live block and nothing painted twice.
                column = after["column"]
                self.assertEqual(column, list(range(column[0], column[0] + len(column))),
                                 "the column has a hole or a repeat in it")
                self.assertEqual(await browser.evaluate(
                    "(() => { const w = document.getElementById('screen-wrap');"
                    " return w.scrollWidth - w.clientWidth; })()"), 0,
                    "the grid is wider than its container")
            finally:
                await browser.stop()
        self.drive(main(), 180)

    def test_a_url_in_the_grid_is_a_real_link(self):
        """The wire's cells are text only, so the view finds http(s) URLs itself at paint time —
        live rows and scrollback both — and a tap opens one (a phone's request, 2026-09-22)."""
        async def main():
            browser = Browser()
            await browser.start()
            try:
                await browser.navigate(f"{self.origin}/tests/screen_harness.html")
                await browser.wait_for("document.body && document.body.dataset.harnessReady === '1'")
                await browser.evaluate("""
                  screenHarness.open({});
                  screenHarness.state.view.apply({t: 'screen_snapshot', rows: 4, cols: 60, alt: false,
                    cursor: {row: 0, col: 0, visible: false, shape: 0},
                    lines: [{row: 1, segs: [['see https://example.com/x_(1), ok', 0, 0, 0]]}]});
                  screenHarness.state.view.applyHistory({
                    t: 'history', from_row: 100, total: 101, more: false,
                    lines: [{row: 100, segs: [['older www.example.com/a?b=1 line', 0, 0, 0]]}]});
                """)
                live = await browser.evaluate("""
                  JSON.stringify((() => {
                    const a = document.querySelector('.screen-grid > .screen-row a.screen-link');
                    return a ? [a.href, a.textContent, a.target] : null; })())
                """)
                self.assertEqual(json.loads(live),
                                 ["https://example.com/x_(1)", "https://example.com/x_(1)", "_blank"],
                                 "the trailing comma and space are not the link's")
                back = await browser.evaluate("""
                  JSON.stringify((() => {
                    const a = document.querySelector('.screen-history .screen-row a.screen-link');
                    return a ? [a.href, a.textContent] : null; })())
                """)
                self.assertEqual(json.loads(back),
                                 ["https://www.example.com/a?b=1", "www.example.com/a?b=1"])
            finally:
                await browser.stop()
        self.drive(main(), 120)

    def test_fit_measures_when_the_layout_moves_not_when_a_frame_arrives(self):
        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.bench(browser)
                # Wide enough that the fit is above the 12px floor (app/screen.js): the whole
                # point below is watching the size move, and the harness's own window sits low
                # enough that the floor would pin it.
                await browser.evaluate(
                    "document.getElementById('terminal-pane').style.width = '1000px'")
                sized = (await browser.evaluate("screenHarness.report()"))["fontSize"]
                # The observer that caught the width change fires on the next rendered frame, and
                # an idle page renders none — let one happen here, so the zeroed counters below
                # measure the stream and not this.
                await browser.evaluate(
                    "new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r)))")
                # The resize's own refit has now happened; count from here.
                await browser.evaluate("screenHarness.zero()")
                self.assertTrue(sized.endswith("px"), sized)
                await browser.evaluate(f"screenHarness.stream({FRAMES}, 5)", timeout=60)
                report = await browser.evaluate("screenHarness.report()")
                # Nothing moved, so nothing was measured: no style resolution and no layout
                # flush in the whole stream. Before the fix it was one of each per frame.
                self.assertEqual(report["style"], 0, "fit() resolved style while streaming")
                self.assertEqual(report["clientWidth"], 0, "fit() flushed layout while streaming")
                self.assertEqual(report["fontSize"], sized, "the grid was refitted for nothing")

                # A resize is measured, and the grid still fits the width it now has.
                await browser.evaluate(
                    "(() => { document.getElementById('terminal-pane').style.width = '240px';"
                    " window.dispatchEvent(new Event('resize')); })()")
                narrow = await browser.evaluate("screenHarness.report()")
                self.assertGreater(narrow["style"], 0, "a resize did not re-measure")
                self.assertNotEqual(narrow["fontSize"], sized)
                # The fit is a floor now, not a clamp (app/screen.js, 2026-09-22): a narrow pane
                # keeps a readable font and scrolls sideways instead of drawing six-pixel text.
                # So the grid may be wider here — what must never happen is clipping.
                self.assertGreaterEqual(float(narrow["fontSize"].rstrip("px")), 12,
                                        "the font fell below the readable floor")
                self.assertGreaterEqual(await browser.evaluate(
                    "(() => { const w = document.getElementById('screen-wrap');"
                    " return w.scrollWidth; })()"),
                    await browser.evaluate(
                        "(() => { const w = document.getElementById('screen-wrap');"
                        " return w.clientWidth; })()"),
                    "the grid was clipped instead of scrolling")

                # And so is a container that changes size with the window standing still, which
                # is what app/viewport.js does for an on-screen keyboard. Only the observer
                # catches that one.
                # Wide enough to fit again past the floor (80 cols at 800px is ~16px), so the
                # fit is measured and the font grows back.
                await browser.evaluate("screenHarness.zero();"
                                       " document.getElementById('terminal-pane').style.width ="
                                       " '800px'")
                # As above: the observer fires on the next rendered frame; give it one.
                await browser.evaluate(
                    "new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r)))")
                await browser.wait_for(
                    f"screenHarness.report().fontSize !== {narrow['fontSize']!r}", timeout=10)
            finally:
                await browser.stop()
        self.drive(main(), 180)

    def test_a_rate_limit_is_backed_off_and_is_not_the_end_of_the_scrollback(self):
        """`rate_limited` says this client asked too fast; `more` is about the desktop's rows."""
        async def main():
            browser = Browser()
            await browser.start()
            try:
                await browser.navigate(f"{self.origin}/tests/screen_harness.html?count=1")
                await browser.wait_for("document.body.dataset.harnessReady === '1'")
                await browser.evaluate('screenHarness.open({"refuse": "rate_limited"})')
                await browser.wait_for("screenHarness.report().asks >= 1", timeout=10)
                await browser.wait_for("screenHarness.report().pending === false", timeout=10)
                refused = await browser.evaluate("screenHarness.report()")
                self.assertTrue(refused["more"], "a rate limit was read as the end of the history")
                # And it is not asked again straight away: that is what the desktop complained of.
                await asyncio.sleep(1)
                self.assertEqual((await browser.evaluate("screenHarness.report()"))["asks"],
                                 refused["asks"], "it asked again inside the back-off")

                # Any other refusal does mean the page is not coming.
                await browser.evaluate('screenHarness.open({"refuse": "not_permitted"})')
                await browser.wait_for("screenHarness.report().more === false", timeout=10)
            finally:
                await browser.stop()
        self.drive(main(), 120)


@unittest.skipUnless(find_chrome(), "Chrome is not installed")
class RefusedPageTests(unittest.TestCase):
    """The real client against a real desktop, with the budget turned down until it refuses."""

    def test_a_refused_page_is_never_the_readers_problem(self):
        # One page, and every `history_get` after it is refused: the desktop end of the red line
        # in evidence/phone-rate-limited-toast.png, forced rather than raced for.
        original = host_mod.LIMITS["history_get"]
        host_mod.LIMITS["history_get"] = (1, 60)
        self.addCleanup(host_mod.LIMITS.__setitem__, "history_get", original)

        async def main():
            async with Harness(capability=wire.VIEW, source=ScrollbackSource) as harness:
                # Every `history_get` that reached the desktop, answered or refused, counted where
                # the budget is spent rather than where a page is made.
                tried: list[bool] = []
                allow = harness.host.limiter.allow

                def counted(who, kind):
                    ok = allow(who, kind)
                    if kind == "history_get":
                        tried.append(ok)
                    return ok

                harness.host.limiter.allow = counted
                url, _ = await harness.host.open_pairing()
                browser = Browser()
                await browser.start()
                try:
                    await browser.navigate(url)
                    await browser.wait_for("document.getElementById('screen-inbox')"
                                           " && !document.getElementById('screen-inbox').hidden",
                                           timeout=40)
                    await browser.evaluate(
                        "document.querySelector('[data-pane-id=\"pane-1\"]').click()")
                    await browser.wait_for(
                        "[...document.querySelectorAll('#screen-wrap .screen-history"
                        " .screen-row')].length >= 80", timeout=30)
                    # Reading what scrolled away, which is where the seam is in front of somebody.
                    await browser.evaluate(
                        "(() => { const w = document.getElementById('screen-wrap');"
                        " w.scrollTop = w.scrollHeight - w.clientHeight - 300; })()")
                    tried.clear()

                    # A reply streaming at the live end: a row of output at a time, which is what
                    # reopens the seam on every frame. Every request now comes back refused.
                    for _ in range(FRAMES):
                        harness.source.advance("pane-1", 1)
                        await asyncio.sleep(FRAME_MS / 1000)
                    await asyncio.sleep(1.5)

                    self.assertLessEqual(len(tried), 12,
                                         f"{len(tried)} history_get for {FRAMES} frames of output")
                    self.assertIn(False, tried, "the desktop never had to refuse one")
                    self.assertEqual(
                        await browser.evaluate(
                            "document.getElementById('thread-note').textContent"), "",
                        "the desktop's refusal was drawn under the reader's composer")
                    self.assertTrue([line for line in browser.console
                                     if "history_get refused" in line],
                                    f"the refusal went nowhere at all: {browser.console[-6:]}")

                    # And a refusal is not the end of the scrollback: with the budget back, the
                    # rows that left the live screen page in and the column joins up again.
                    host_mod.LIMITS["history_get"] = original
                    numbers = ("(() => [...document.querySelectorAll("
                               "'.screen-history .screen-row, .screen-grid > .screen-row')]"
                               ".map(n => parseInt(n.textContent.trim().split('-')[1], 10)))()")
                    await browser.wait_for(
                        f"(() => {{ const n = {numbers};"
                        " if (n.every((v, i) => i === 0 || v === n[i - 1] + 1)) return true;"
                        " const w = document.getElementById('screen-wrap');"
                        " w.scrollTop = w.scrollHeight - w.clientHeight - 300;"
                        " return false; })()", timeout=40)
                finally:
                    await browser.stop()
        asyncio.run(asyncio.wait_for(main(), 240))


if __name__ == "__main__":
    unittest.main()
