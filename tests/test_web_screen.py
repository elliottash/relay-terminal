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

# Every painted piece of every live row, in order: the row's text as the reader sees it, and each
# anchor with the offset it sits at. What #NK73 is about is the relation between the two.
PAINTED_ROWS = """
  [...document.querySelectorAll('.screen-grid > .screen-row')].map((node) => {
    let text = '';
    const links = [];
    for (const child of node.childNodes) {
      const piece = child.textContent;
      if (child.tagName === 'A') links.push({at: text.length, text: piece, href: child.href});
      text += piece;
    }
    return {text, links};
  })
"""


def same_url(href: str, label: str) -> bool:
    """Does the anchor go where its visible text says? A browser normalises an href — it lower-
    cases the host and gives a bare host a path — so the comparison has to."""
    want = label if label.lower().startswith(("http://", "https://")) else f"https://{label}"
    return href.lower() in (want.lower(), f"{want.lower()}/")


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

    def test_a_row_with_more_than_one_url_is_painted_exactly_as_it_was_sent(self):
        """#NK73. What the reader sees is what the terminal printed, whatever is on the row.

        The painter cuts a row into pieces and marks the URLs in it, so the one thing that must
        always hold is that the pieces sum to the row: two URLs on a line used to lose the word
        between them, draw the first one twice, and give the second anchor a visible address that
        was not the one it opened. `git remote -v`, an `npm audit` advisory and a `curl -v` trace
        all put two on a row."""
        rows = [
            # text, the labels expected to be links, in order
            ("see https://a.example.com and https://b.example.com now",
             ["https://a.example.com", "https://b.example.com"]),
            ("https://a.example.com https://b.example.com https://c.example.com",
             ["https://a.example.com", "https://b.example.com", "https://c.example.com"]),
            # An uppercase host is still a host: a case-sensitive scheme test made this a
            # *relative* href into the client's own origin.
            ("banner: WWW.EXAMPLE.COM is the site", ["WWW.EXAMPLE.COM"]),
            # A shell's quotes and an RFC's angle brackets are not part of the address.
            ('run curl "https://api.example.com/v1" twice', ["https://api.example.com/v1"]),
            ("see <https://example.com/a> in the RFC", ["https://example.com/a"]),
            # A path that merely contains `www.` is not a host at all.
            ("path /var/www.old/index.html is served", []),
            # And the rule that was already right stays right.
            ("one https://example.com/x_(1), ok", ["https://example.com/x_(1)"]),
        ]

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await browser.navigate(f"{self.origin}/tests/screen_harness.html")
                await browser.wait_for("document.body && document.body.dataset.harnessReady === '1'")
                lines = [{"row": n, "segs": [[text, 0, 0, 0]]}
                         for n, (text, _) in enumerate(rows)]
                await browser.evaluate(f"""
                  screenHarness.open({{}});
                  screenHarness.state.view.apply({{t: 'screen_snapshot', rows: {len(rows)},
                    cols: 100, alt: false, cursor: {{row: 0, col: 0, visible: false}},
                    lines: {json.dumps(lines)}}});
                """)
                painted = await browser.evaluate(PAINTED_ROWS)
                for (text, labels), row in zip(rows, painted):
                    self.assertEqual(row["text"], text,
                                     "the row was painted as something the terminal never sent")
                    self.assertEqual([link["text"] for link in row["links"]], labels, text)
                    for link in row["links"]:
                        at = link["at"]
                        self.assertEqual(text[at:at + len(link["text"])], link["text"],
                                         f"the anchor's text is not the row's at {at}: {text!r}")
                        self.assertTrue(
                            same_url(link["href"], link["text"]),
                            f"{link['text']!r} is a link to {link['href']!r}")
            finally:
                await browser.stop()
        self.drive(main(), 120)

    def test_a_url_that_runs_to_the_last_column_is_not_linked_to_its_prefix(self):
        """#NK73. The terminal wraps; a row does not say so. A URL that reaches the last column
        may be half an address, and an href that is a truncated one is a 404 or — signed — a
        request silently for something else. The only safe reading is that it is not a link."""
        wrapped = "open https://example.com/a/very/long/signed/path"

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await browser.navigate(f"{self.origin}/tests/screen_harness.html")
                await browser.wait_for("document.body && document.body.dataset.harnessReady === '1'")
                # Two rows on a screen exactly as wide as the first one: the same URL, once
                # running to the last column and once with a space after it.
                lines = [{"row": 0, "segs": [[wrapped, 0, 0, 0]]},
                         {"row": 1, "segs": [[wrapped[5:] + " .", 0, 0, 0]]}]
                await browser.evaluate(f"""
                  screenHarness.open({{}});
                  screenHarness.state.view.apply({{t: 'screen_snapshot', rows: 2,
                    cols: {len(wrapped)}, alt: false, cursor: {{row: 0, col: 0, visible: false}},
                    lines: {json.dumps(lines)}}});
                """)
                painted = await browser.evaluate(PAINTED_ROWS)
                self.assertEqual(painted[0]["text"], wrapped)
                self.assertEqual(painted[0]["links"], [],
                                 "a URL that may have wrapped was linked to its prefix")
                self.assertEqual([link["text"] for link in painted[1]["links"]],
                                 [wrapped[5:]], "a URL that plainly ended was not linked")
            finally:
                await browser.stop()
        self.drive(main(), 120)

    def test_a_link_keeps_its_colour_and_a_concealed_one_stays_concealed(self):
        """#NK73. `link()` threw away the `fg, bg, attrs` the painter had carried onto every
        piece, so a URL in a red error line painted default, one in a reverse-video run lost its
        inversion — and one under CONCEAL, which `span` blanks, was printed in full and made
        tappable: a hidden token readable on the phone and nowhere else."""
        red = (1 << 24) | 1
        bold, reverse, conceal = 1 << 0, 1 << 6, 1 << 7

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await browser.navigate(f"{self.origin}/tests/screen_harness.html")
                await browser.wait_for("document.body && document.body.dataset.harnessReady === '1'")
                lines = [
                    {"row": 0, "segs": [["error: ", red, 0, 0],
                                        ["https://a.example.com", red, 0, bold],
                                        [" failed", red, 0, 0]]},
                    {"row": 1, "segs": [["see https://b.example.com now", red, 0, reverse]]},
                    {"row": 2, "segs": [["token https://secret.example.com/t", 0, 0, conceal]]},
                ]
                await browser.evaluate(f"""
                  screenHarness.open({{}});
                  screenHarness.state.view.apply({{t: 'screen_snapshot', rows: 3, cols: 100,
                    alt: false, cursor: {{row: 0, col: 0, visible: false}},
                    lines: {json.dumps(lines)}}});
                """)
                styled = await browser.evaluate("""
                  [...document.querySelectorAll('.screen-grid > .screen-row')].map((node) => {
                    const anchor = node.querySelector('a.screen-link');
                    const span = anchor && anchor.firstElementChild;
                    return {
                      text: node.textContent,
                      linked: !!anchor,
                      color: span ? span.style.color : null,
                      background: span ? span.style.background : null,
                      weight: span ? span.style.fontWeight : null,
                    };
                  })
                """)
                self.assertTrue(styled[0]["linked"])
                self.assertIn("--rt-ansi-1", styled[0]["color"],
                              "the link painted in the default foreground, not the row's red")
                self.assertEqual(styled[0]["weight"], "700", "the link lost the run's bold")
                self.assertTrue(styled[1]["linked"])
                self.assertIn("--rt-ansi-1", styled[1]["background"],
                              "the link in a reverse-video run lost its inversion")
                self.assertFalse(styled[2]["linked"],
                                 "a concealed URL was linked, which is to print it")
                self.assertEqual(styled[2]["text"].strip(), "",
                                 "a concealed URL was painted in full")
                self.assertEqual(len(styled[2]["text"]), len("token https://secret.example.com/t"))
            finally:
                await browser.stop()
        self.drive(main(), 120)

    def test_the_font_buttons_work_for_the_session_when_storage_throws(self):
        """#NK73. `fontFloor()` read storage and `setFontFloor()` wrote it, with nothing in
        memory, so where storage throws — iOS private browsing, a sandboxed iframe, `file://` —
        A−/A+ were dead rather than session-only: `refit()` re-read and got the default back."""
        async def main():
            browser = Browser()
            await browser.start()
            try:
                await browser.navigate(f"{self.origin}/tests/screen_harness.html")
                await browser.wait_for("document.body && document.body.dataset.harnessReady === '1'")
                await browser.evaluate("""
                  Object.defineProperty(window, 'localStorage', {configurable: true,
                    get() { throw new DOMException('denied', 'SecurityError'); }});
                  screenHarness.open({});
                """)
                self.assertEqual(await browser.evaluate("screenHarness.state.view.fontFloor()"), 12,
                                 "the default floor did not stand")
                moved = await browser.evaluate("""
                  (() => {
                    const view = screenHarness.state.view;
                    view.setFontFloor(view.fontFloor() + 2);   // A+
                    view.setFontFloor(view.fontFloor() + 2);   // A+
                    const after = view.grid.style.fontSize;
                    view.refit();          // the resize that used to undo it
                    return [view.fontFloor(), after, view.grid.style.fontSize];
                  })()
                """)
                self.assertEqual(moved[0], 16, "A+ did not move the floor at all")
                self.assertEqual(float(moved[1].rstrip("px")), 16, "A+ did not grow the text")
                self.assertEqual(moved[2], moved[1], "a refit undid the reader's choice")
            finally:
                await browser.stop()
        self.drive(main(), 120)

    def test_the_cursor_stays_in_view_on_a_grid_wider_than_the_screen(self):
        """#NK73. The fit is a floor, so the grid scrolls sideways — and nothing ever scrolled it:
        with direct keys on at 12px the cursor left the right edge around column 30 of 80 and the
        reader typed blind. It follows the cursor at the live end only, and by arithmetic: the
        counters below are the same ones finding 4 of #3H5T put here."""
        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.bench(browser)
                wide = "".join(str(n % 10) for n in range(80))
                frame = ("(col) => screenHarness.state.view.apply({t: 'screen_snapshot',"
                         " rows: 24, cols: 80, alt: false,"
                         " cursor: {row: 23, col, visible: true},"
                         f" lines: [{{row: 23, segs: [[{json.dumps(wide)}, 0, 0, 0]]}}]}})")
                await browser.evaluate(f"window.typeAt = {frame}; typeAt(70)")
                seen = await browser.evaluate("""
                  (() => {
                    const wrap = document.getElementById('screen-wrap');
                    const cell = wrap.querySelector('.cursor').getBoundingClientRect();
                    const box = wrap.getBoundingClientRect();
                    return {left: wrap.scrollLeft, over: wrap.scrollWidth - wrap.clientWidth,
                            cellLeft: cell.left, cellRight: cell.right,
                            boxLeft: box.left, boxRight: box.right};
                  })()
                """)
                self.assertGreater(seen["over"], 0, "the grid was not wider than the screen")
                self.assertGreater(seen["left"], 0, "the cursor was left off the right edge")
                self.assertLessEqual(seen["cellRight"], seen["boxRight"] + 1,
                                     "the cursor cell is past the right edge")
                self.assertGreaterEqual(seen["cellLeft"], seen["boxLeft"] - 1,
                                        "the cursor cell is past the left edge")

                # Typing along the row costs no measurement: the cell width and the visible width
                # come from the last fit(), and where it last scrolled to is remembered.
                await browser.evaluate("screenHarness.zero()")
                await browser.evaluate(
                    "(async () => { for (let col = 0; col < 80; col += 4) { typeAt(col);"
                    " await new Promise((r) => setTimeout(r, 5)); } })()", timeout=30)
                report = await browser.evaluate("screenHarness.report()")
                self.assertEqual(report["style"], 0, "following the cursor resolved style")
                self.assertEqual(report["clientWidth"], 0, "following the cursor flushed layout")

                # And it never drags somebody who is reading what scrolled away.
                await browser.evaluate(
                    "(() => { const w = document.getElementById('screen-wrap');"
                    " w.scrollTop = 0; w.scrollLeft = 120; })()")
                await browser.evaluate("new Promise((r) => setTimeout(r, 60))")
                self.assertFalse((await browser.evaluate("screenHarness.report()"))["atBottom"])
                await browser.evaluate("typeAt(79)")
                self.assertEqual(
                    await browser.evaluate("document.getElementById('screen-wrap').scrollLeft"),
                    120, "the reader was dragged sideways while reading back")
            finally:
                await browser.stop()
        self.drive(main(), 180)

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
