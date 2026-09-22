# SPDX-License-Identifier: AGPL-3.0-or-later
"""The Board on the phone (app/board.js, card #SWPH) in a real browser.

The page under test is the real client — app/index.html, app/app.js, the outbox, the viewport —
with one file swapped: the static server answers ``/app/rrp.js`` with
``tests/fixtures/board/fake_rrp.js``, a transport that is "connected" without a socket, records
what the client would have put on the wire, and lets the test play the hub. What is worth
asserting is the contract on the card, because everything else about a board is the desktop's:

* the Board row is in the inbox only when ``welcome.features`` has ``board`` (and the device
  is ``full``), it counts the cards waiting on the owner, and they count on the app badge;
* the stages come in the desktop's section order, "Waiting on you" first, closed stages folded;
* a card's Markdown is drawn as nodes: no element a card asked for exists, no HTML comment shows,
  and a link opens only from a sheet that shows its whole address;
* every action sends exactly the contract's ``board_request``, and nothing else;
* a ``board_changed`` repaints the list in place;
* a write made while the link is down is kept once, says so, and goes out once on the resume;
* a phone in portrait, a phone in landscape with its keyboard up, and an iPad in two columns.

The fixtures are the shapes of docs/AGENT-SESSIONS-PROTOCOL.md section 19.2 as they reach a device
through ``remote/board_state.py`` (no ``path``, ``root`` or ``folder``; ``board_name`` for the
project). Skipped when Chrome is not installed.
"""
import asyncio
import base64
import json
import os
import shutil
import subprocess
import threading
import unittest
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from remote import board_state
from tests.browser import Browser, find_chrome, shown, SCREENS_SHOWN

ROOT = Path(__file__).resolve().parent.parent
FIXTURES = ROOT / "tests" / "fixtures" / "board"
# Set to a directory to keep a screenshot of each layout (docs/qa_evidence/…-swph-board-view/).
SHOTS = os.environ.get("RELAY_BOARD_SHOTS", "")

BOARD = json.loads((FIXTURES / "board.json").read_text())
CARD = json.loads((FIXTURES / "card.json").read_text())
CARD_ID = CARD["card_id"]

PHONE = (390, 844)
PHONE_LANDSCAPE_KEYBOARD = (844, 185)
IPAD = (1180, 820)


class Swapped(SimpleHTTPRequestHandler):
    """The repository, with the transport swapped for the fake one."""

    def log_message(self, *args):
        pass

    def translate_path(self, path):
        clean = path.split("?", 1)[0].split("#", 1)[0]
        if clean == "/app/rrp.js":
            return str(FIXTURES / "fake_rrp.js")
        if clean == "/app/rrp-real.js":
            return str(ROOT / "app" / "rrp.js")
        return super().translate_path(path)


def serve():
    server = ThreadingHTTPServer(("127.0.0.1", 0), partial(Swapped, directory=str(ROOT)))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, f"http://127.0.0.1:{server.server_address[1]}"


def frames(n: int = 3) -> str:
    return ("new Promise(r => { let k = %d; const f = () => (--k ? requestAnimationFrame(f) : r(1));"
            " requestAnimationFrame(f); })" % n)


def js(value) -> str:
    return json.dumps(value)


@unittest.skipUnless(find_chrome(), "Chrome is not installed")
class BoardViewTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server, cls.origin = serve()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def drive(self, coroutine, timeout=180):
        return asyncio.run(asyncio.wait_for(coroutine, timeout))

    # ---- the hub's side, played by the test ------------------------------------------------------

    async def window(self, browser, width, height):
        await browser.call("Emulation.setDeviceMetricsOverride",
                           {"width": width, "height": height, "deviceScaleFactor": 1, "mobile": False})

    async def start(self, browser, *, features="panes,agent,compose,pane_state,board",
                    capability="full", size=PHONE, query=""):
        await self.window(browser, *size)
        await browser.navigate(f"{self.origin}/app/index.html?features={features}"
                               f"&capability={capability}{query}")
        await browser.wait_for("!!window.fakeRrp && !!window.fakeRrp.session && " + shown("screen-inbox"))

    async def requests(self, browser) -> list:
        return json.loads(await browser.evaluate("JSON.stringify(window.fakeRrp.boardRequests())"))

    async def last_request(self, browser, kind: str) -> dict:
        found = [m for m in await self.requests(browser) if m["request"]["type"] == kind]
        self.assertTrue(found, f"no {kind} was sent: {await self.requests(browser)}")
        return found[-1]

    async def answer_open(self, browser, board=None) -> dict:
        """Answer the client's `board_open` with the fixture board, as the hub would."""
        await browser.wait_for("window.fakeRrp.boardRequests().some(m => m.request.type === 'board_open')")
        asked = await self.last_request(browser, "board_open")
        await browser.evaluate(f"window.fakeRrp.board({asked['rid']}, {js(board or BOARD)})")
        return asked

    async def open_board(self, browser):
        await self.answer_open(browser)
        await browser.evaluate("document.getElementById('board-row').click()")
        await browser.wait_for(shown("screen-board") + " && document.querySelectorAll('.rb-row').length > 0")

    async def open_card(self, browser, card=None):
        card = card or CARD
        await browser.evaluate(
            f"document.querySelector('.rb-row[data-card-id=\"{card['card_id']}\"]').click()")
        await browser.wait_for(
            "window.fakeRrp.boardRequests().some(m => m.request.type === 'board_card_get')")
        asked = await self.last_request(browser, "board_card_get")
        await browser.evaluate(f"window.fakeRrp.board({asked['rid']}, {js(card)})")
        await browser.wait_for("!!document.querySelector('.rb-thread .rb-entry')")
        return asked

    def passes_the_hub(self, messages: list) -> None:
        """Every request is one the hub takes whole: `remote/board_state.py` rebuilds a request
        from its allow-list, so a request equal to its rebuilt form carried nothing else, and one
        it refuses raises here rather than on the owner's phone."""
        for message in messages:
            self.assertEqual(set(message) - {"msg_id"}, {"t", "rid", "request"})
            self.assertEqual(board_state.rid_of(message), message["rid"])
            self.assertEqual(board_state.clean_request(message["request"]), message["request"])

    async def shot(self, browser, name: str):
        if not SHOTS:
            return
        Path(SHOTS).mkdir(parents=True, exist_ok=True)
        data = await browser.call("Page.captureScreenshot", {"format": "png"})
        (Path(SHOTS) / f"{name}.png").write_bytes(base64.b64decode(data["data"]))

    def clean(self, browser):
        """No exception and no refused resource: the CSP the rendezvous serves is strict."""
        # Chrome's own chatter (a password field outside a form, the install banner) is not the
        # page's; an exception, an error or a warning is.
        noise = [line for line in browser.console
                 if line.startswith(("EXCEPTION", "error:", "warning:"))
                 and "favicon" not in line and "manifest" not in line
                 and "sw.js" not in line and "ServiceWorker" not in line]
        self.assertEqual(noise, [])

    # ---- the inbox row -----------------------------------------------------------------------------

    def test_the_row_is_in_the_inbox_only_when_the_desktop_offers_the_board(self):
        async def main():
            browser = Browser()
            await browser.start()
            try:
                # Without the feature: no row, and nothing about a board is ever sent.
                await self.start(browser, features="panes,agent,compose,pane_state")
                self.assertFalse(await browser.evaluate(shown("board-row")))
                self.assertEqual(await self.requests(browser), [])

                # Offered, but to a device that is not `full`: the hub would refuse it, so no row.
                await self.start(browser, capability="agent")
                self.assertFalse(await browser.evaluate(shown("board-row")))
                self.assertEqual(await self.requests(browser), [])

                # Offered to a `full` device: the row leads the inbox, and the board is asked for
                # at once so the row can say how many cards are waiting.
                await self.start(browser)
                self.assertTrue(await browser.evaluate(shown("board-row")))
                self.assertTrue(await browser.evaluate(
                    "document.getElementById('board-row-slot').compareDocumentPosition("
                    "document.getElementById('pane-list')) & Node.DOCUMENT_POSITION_FOLLOWING"))
                asked = await self.answer_open(browser)
                self.assertEqual(asked, {"t": "board_request", "rid": asked["rid"],
                                         "request": {"type": "board_open"}})
                self.assertIsInstance(asked["rid"], int)
                waiting = [row for row in BOARD["cards"] if row["waiting_on"] == "owner"]
                self.assertEqual(len(waiting), 3)
                await browser.wait_for("!document.querySelector('.rb-inbox-chip').hidden")
                self.assertEqual(await browser.evaluate(
                    "document.querySelector('.rb-inbox-chip').textContent"), "3 waiting on you")
                self.assertIn("relay-terminal", await browser.evaluate(
                    "document.querySelector('#board-row .rb-inbox-sub').textContent"))
                # Exactly one screen, and a tap opens the board.
                await browser.evaluate("document.getElementById('board-row').click()")
                await browser.wait_for(shown("screen-board"))
                self.assertEqual(await browser.evaluate(SCREENS_SHOWN), 1)
                await browser.evaluate("document.querySelector('.rb-bar .rb-back').click()")
                await browser.wait_for(shown("screen-inbox"))
                self.clean(browser)
            finally:
                await browser.stop()

        self.drive(main())

    def test_cards_waiting_on_you_count_on_the_app_badge(self):
        async def main():
            browser = Browser()
            await browser.start()
            try:
                await browser.call("Page.addScriptToEvaluateOnNewDocument", {"source":
                    "window.badges = []; navigator.setAppBadge = (n) => { window.badges.push(n); return Promise.resolve(); };"
                    "navigator.clearAppBadge = () => { window.badges.push(0); return Promise.resolve(); };"
                    "window.fakePanes = [{id: 'p1', title: 'build', cwd: '~/relay', status: 'waiting_input'},"
                    " {id: 'p2', title: 'idle', cwd: '~', status: 'idle'}];"})
                await self.start(browser)
                await self.answer_open(browser)
                # One pane wants you, and three cards do.
                await browser.wait_for("window.badges[window.badges.length - 1] === 4")
                # A card stops waiting: the badge follows the `board_changed`.
                row = next(r for r in BOARD["cards"] if r["waiting_on"] == "owner")
                change = {"event": "board_changed", "rev": BOARD["rev"] + 1, "removed": [], "problems": [],
                          "upserts": [{**row, "waiting_on": None}]}
                await browser.evaluate(f"window.fakeRrp.board(null, {js(change)})")
                await browser.wait_for("window.badges[window.badges.length - 1] === 3")
                self.assertEqual(await browser.evaluate(
                    "document.querySelector('.rb-inbox-chip').textContent"), "2 waiting on you")
            finally:
                await browser.stop()

        self.drive(main())

    def test_a_project_with_no_board_says_so_where_the_cards_would_be(self):
        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.start(browser)
                await browser.wait_for("window.fakeRrp.boardRequests().some(m => m.request.type === 'board_open')")
                asked = await self.last_request(browser, "board_open")
                refusal = {"event": "error", "code": "board_not_found", "request": "board_open",
                           "text": "This project has no board."}
                await browser.evaluate(f"window.fakeRrp.board({asked['rid']}, {js(refusal)})")
                await browser.wait_for("document.querySelector('#board-row .rb-inbox-sub').textContent.includes('no board')")
                await browser.evaluate("document.getElementById('board-row').click()")
                await browser.wait_for(shown("screen-board"))
                self.assertIn("no board yet", await browser.evaluate("document.querySelector('.rb-empty').textContent"))
                self.assertEqual(await browser.evaluate("document.querySelectorAll('.rb-row').length"), 0)
            finally:
                await browser.stop()

        self.drive(main())

    # ---- the list ------------------------------------------------------------------------------------

    def test_stages_are_in_the_desktops_order_with_waiting_on_you_first(self):
        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.start(browser)
                await self.open_board(browser)
                sections = json.loads(await browser.evaluate(
                    "JSON.stringify([...document.querySelectorAll('.rb-section')].map(e => "
                    "[e.dataset.section, e.querySelector('.rb-section-title').textContent,"
                    " Number(e.querySelector('.rb-section-count').textContent), e.getAttribute('aria-expanded')]))"))
                # "Waiting on you" is pinned first; then board.yaml's columns in their order with
                # the board's own titles (Checks, Research); then a section for each status no
                # column collects (ready, deferred — the desktop's extra-status order); then
                # Verified and Done, which are always last and start folded.
                self.assertEqual(sections, [
                    ["waiting-on-you", "Waiting on you", 3, "true"],
                    ["inbox", "Inbox", 5, "true"],
                    ["discussing", "Discussing", 5, "true"],
                    ["planning", "Planning", 2, "true"],
                    ["planned", "Planned", 3, "true"],
                    ["executing", "Executing", 4, "true"],
                    ["needs-verification", "Needs verification", 3, "true"],
                    ["needs-qa", "Checks", 4, "true"],
                    ["research", "Research", 1, "true"],
                    ["ready", "Ready to start", 2, "true"],
                    ["deferred", "Deferred", 2, "true"],
                    ["verified", "Verified", 2, "false"],
                    ["done", "Done", 7, "false"],
                ])
                # The pinned group leads the list, and its rows are the three that wait on the owner.
                first = json.loads(await browser.evaluate(
                    "JSON.stringify([...document.querySelector('.rb-list').children].slice(0, 4)"
                    ".map(e => e.dataset.section || e.dataset.cardId))"))
                waiting = {row["id"] for row in BOARD["cards"] if row["waiting_on"] == "owner"}
                self.assertEqual(first[0], "waiting-on-you")
                self.assertEqual(set(first[1:]), waiting)
                # A row: the id, the title on at most two lines, the chips, the task progress and
                # the thread count.
                row = json.loads(await browser.evaluate(
                    f"(() => {{ const r = document.querySelector('.rb-row[data-group=\"discussing\"][data-card-id=\"{CARD_ID}\"]');"
                    " const t = r.querySelector('.rb-row-title');"
                    " return JSON.stringify({id: r.querySelector('.rb-row-id').textContent, title: t.textContent,"
                    " lines: Math.round(t.getBoundingClientRect().height / parseFloat(getComputedStyle(t).lineHeight)),"
                    " chips: [...r.querySelectorAll('.rb-chip')].map(c => c.textContent),"
                    " busy: !r.querySelector('.rb-row-busy').hidden}); })()"))
                self.assertEqual(row["id"], f"#{CARD_ID}")
                self.assertEqual(row["title"], "Scrollback survives a resume")
                self.assertLessEqual(row["lines"], 2)
                self.assertIn("waiting on you", row["chips"])
                self.assertIn("☑ 2/5", row["chips"])
                self.assertIn("✎ 6", row["chips"])
                self.assertFalse(row["busy"])
                # Every row and every header a thumb can hit is at least 44 px tall.
                short = json.loads(await browser.evaluate(
                    "JSON.stringify([...document.querySelectorAll('.rb-row, .rb-section, .rb-tab, .rb-search,"
                    " .rb-bar button')].filter(e => e.getBoundingClientRect().height < 44)"
                    ".map(e => e.className))"))
                self.assertEqual(short, [])

                # Folding: Done opens on a tap, and the cards the agent closed itself stay behind
                # one row ("3 closed by the agent"), as on the desktop.
                await browser.evaluate("document.querySelector('.rb-section[data-section=\"done\"]').click()")
                await browser.wait_for("!!document.querySelector('.rb-fold')")
                self.assertEqual(await browser.evaluate("document.querySelector('.rb-fold').textContent"),
                                 "▸ 3 closed by the agent")
                self.assertEqual(await browser.evaluate(
                    "document.querySelectorAll('.rb-row[data-group=\"done\"]').length"), 4)

                # Tabs: a folder tab is its cards; a filter tab is what its filter says.
                tabs = json.loads(await browser.evaluate(
                    "JSON.stringify([...document.querySelectorAll('.rb-tab')].map(e => e.textContent))"))
                self.assertEqual(tabs, ["All", "features", "bugs", "design", "deferred", "done"])
                await browser.evaluate("document.querySelector('.rb-tab[data-tab=\"bugs\"]').click()")
                ids = set(json.loads(await browser.evaluate(
                    "JSON.stringify([...new Set([...document.querySelectorAll('.rb-row')].map(e => e.dataset.cardId))])")))
                expected = {r["id"] for r in BOARD["cards"] if r["tab"] == "bugs"
                            and r["status"] not in ("done", "dropped")} | {
                    r["id"] for r in BOARD["cards"] if r["tab"] == "bugs" and r["status"] in ("done", "dropped")
                    and not (r["verified_by"] and r["verified_by"] == r["implemented_by"])
                    and not (r["status"] == "done" and r["verified_by"])}
                self.assertEqual(ids, expected)
                await self.shot(browser, "phone-390x844-board-bugs-tab")
                self.clean(browser)
            finally:
                await browser.stop()

        self.drive(main())

    def test_search_asks_the_desktop_and_shows_what_it_answers(self):
        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.start(browser)
                await self.open_board(browser)
                await browser.evaluate(
                    "(() => { const s = document.querySelector('.rb-search'); s.value = 'sqlite';"
                    " s.dispatchEvent(new Event('input', {bubbles: true})); })()")
                # No row's own fields hold the word, so until the desktop answers nothing matches.
                await browser.wait_for("!!document.querySelector('.rb-empty')")
                await browser.wait_for("window.fakeRrp.boardRequests().some(m => m.request.type === 'board_search')")
                asked = await self.last_request(browser, "board_search")
                self.assertEqual(asked["request"], {"type": "board_search", "query": "sqlite"})
                self.passes_the_hub(await self.requests(browser))
                answer = {"event": "board_search", "query": "sqlite", "ids": [CARD_ID]}
                await browser.evaluate(f"window.fakeRrp.board({asked['rid']}, {js(answer)})")
                await browser.wait_for("document.querySelectorAll('.rb-row').length > 0")
                ids = json.loads(await browser.evaluate(
                    "JSON.stringify([...new Set([...document.querySelectorAll('.rb-row')].map(e => e.dataset.cardId))])"))
                self.assertEqual(ids, [CARD_ID])
                # Nothing is folded and no empty section is shown while a search is on.
                sections = json.loads(await browser.evaluate(
                    "JSON.stringify([...document.querySelectorAll('.rb-section')].map(e => e.dataset.section))"))
                self.assertEqual(sections, ["waiting-on-you", "discussing"])
            finally:
                await browser.stop()

        self.drive(main())

    def test_a_board_changed_updates_the_list_in_place(self):
        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.start(browser)
                await self.open_board(browser)
                moved = next(r for r in BOARD["cards"] if r["status"] == "inbox")
                await browser.evaluate(
                    f"window.keptNode = document.querySelector('.rb-row[data-card-id=\"{moved['id']}\"]');"
                    "document.querySelector('.rb-list').scrollTop = 120;")
                gone = next(r for r in BOARD["cards"] if r["status"] == "planned" and not r["section"])
                # The two forms the bridge forwards, in the order it sends them: the write's own
                # notice (bare ids, no `rev`), then the worker's diff with the rows and the config.
                notice = {"event": "board_changed", "removed": [], "upserts": [moved["id"]], "write_id": "w-9"}
                await browser.evaluate(f"window.fakeRrp.board(null, {js(notice)})")
                self.assertEqual(await browser.evaluate(
                    f"document.querySelector('.rb-row[data-card-id=\"{moved['id']}\"] .rb-row-title').textContent"),
                    moved["title"])
                # Revisions skip on a device (the bridge drops a refresh that found nothing), so a
                # gap is not a reason to load the board again.
                change = {"event": "board_changed", "rev": BOARD["rev"] + 3, "problems": [], "write_id": "w-9",
                          "config": {**BOARD["config"], "column_titles": {**BOARD["config"]["column_titles"],
                                                                         "inbox": "Triage"}},
                          "removed": [gone["id"]],
                          "upserts": [{**moved, "title": "Retitled on the desktop", "waiting_on": "owner"}]}
                await browser.evaluate(f"window.fakeRrp.board(null, {js(change)})")
                await browser.wait_for(
                    f"document.querySelector('.rb-row[data-group=\"inbox\"][data-card-id=\"{moved['id']}\"] .rb-row-title')"
                    ".textContent === 'Retitled on the desktop'")
                # The same node, repainted; the list did not jump; nothing was asked for again.
                self.assertTrue(await browser.evaluate(
                    f"window.keptNode === document.querySelector('.rb-row[data-group=\"inbox\"][data-card-id=\"{moved['id']}\"]')"))
                self.assertEqual(await browser.evaluate("document.querySelector('.rb-list').scrollTop"), 120)
                self.assertEqual([m["request"]["type"] for m in await self.requests(browser)], ["board_open"])
                self.assertEqual(await browser.evaluate(
                    "document.querySelector('.rb-section[data-section=\"waiting-on-you\"] .rb-section-count').textContent"), "4")
                self.assertEqual(await browser.evaluate(
                    f"document.querySelectorAll('.rb-row[data-card-id=\"{gone['id']}\"]').length"), 0)
                self.assertEqual(await browser.evaluate(
                    "document.querySelector('.rb-section[data-section=\"planned\"] .rb-section-count').textContent"), "2")
                # The board's own config rides on the diff: a section renamed on the desktop is renamed here.
                self.assertEqual(await browser.evaluate(
                    "document.querySelector('.rb-section[data-section=\"inbox\"] .rb-section-title').textContent"), "Triage")
                self.assertEqual([m["request"]["type"] for m in await self.requests(browser)], ["board_open"])
                self.clean(browser)
            finally:
                await browser.stop()

        self.drive(main())

    def test_pull_to_refresh_and_the_refresh_button_send_board_refresh(self):
        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.start(browser)
                await self.open_board(browser)
                await browser.call("Emulation.setTouchEmulationEnabled", {"enabled": True})
                box = json.loads(await browser.evaluate(
                    "JSON.stringify((r => [r.left + r.width / 2, r.top + 30])"
                    "(document.querySelector('.rb-list').getBoundingClientRect()))"))
                x, y = box
                await browser.call("Input.dispatchTouchEvent", {"type": "touchStart", "touchPoints": [{"x": x, "y": y}]})
                for step in (30, 60, 90, 120):
                    await browser.call("Input.dispatchTouchEvent",
                                       {"type": "touchMove", "touchPoints": [{"x": x, "y": y + step}]})
                self.assertEqual(await browser.evaluate("document.querySelector('.rb-pull').textContent"),
                                 "Release to refresh")
                await browser.call("Input.dispatchTouchEvent", {"type": "touchEnd", "touchPoints": []})
                await browser.wait_for("window.fakeRrp.boardRequests().some(m => m.request.type === 'board_refresh')")
                asked = await self.last_request(browser, "board_refresh")
                self.assertEqual(asked["request"], {"type": "board_refresh"})
                # A short drag is a scroll that changed its mind, not a refresh.
                await browser.call("Input.dispatchTouchEvent", {"type": "touchStart", "touchPoints": [{"x": x, "y": y}]})
                await browser.call("Input.dispatchTouchEvent", {"type": "touchMove", "touchPoints": [{"x": x, "y": y + 30}]})
                await browser.call("Input.dispatchTouchEvent", {"type": "touchEnd", "touchPoints": []})
                await browser.evaluate("document.querySelector('.rb-refresh').click()")
                await browser.evaluate(frames(), timeout=10)
                kinds = [m["request"]["type"] for m in await self.requests(browser)]
                self.assertEqual(kinds.count("board_refresh"), 2)
            finally:
                await browser.stop()

        self.drive(main())

    # ---- the card page ---------------------------------------------------------------------------------

    def test_a_card_renders_as_nodes_with_nothing_injected_and_no_comment_markers(self):
        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.start(browser)
                await self.open_board(browser)
                asked = await self.open_card(browser)
                self.assertEqual(asked["request"], {"type": "board_card_get", "id": CARD_ID})
                page = json.loads(await browser.evaluate(
                    "(() => { const c = document.querySelector('.rb-card-col');"
                    " return JSON.stringify({"
                    "  injected: c.querySelectorAll('script, img, iframe, object, embed, a, style, link, b, form').length,"
                    "  handlers: [...c.querySelectorAll('*')].filter(e => [...e.attributes].some(a => a.name.startsWith('on'))).length,"
                    "  pwned: window.pwned === undefined,"
                    "  text: c.textContent,"
                    "  title: c.querySelector('.rb-card-title').textContent,"
                    "  headings: [...c.querySelectorAll('.rb-body-heading')].map(e => e.textContent),"
                    "  strong: [...c.querySelectorAll('.rb-section-body strong')].map(e => e.textContent),"
                    "  em: [...c.querySelectorAll('.rb-section-body em')].map(e => e.textContent),"
                    "  del: [...c.querySelectorAll('.rb-section-body del')].map(e => e.textContent),"
                    "  code: [...c.querySelectorAll('.rb-md-code')].map(e => e.textContent),"
                    "  pre: [...c.querySelectorAll('.rb-md-pre')].map(e => e.textContent),"
                    "  ordered: [...c.querySelectorAll('.rb-section-body ol > li')].length,"
                    "  nested: [...c.querySelectorAll('.rb-section-body ol ul > li')].length,"
                    "  quote: c.querySelector('.rb-md-quote').textContent,"
                    "  rules: c.querySelectorAll('.rb-md-rule').length,"
                    "  table: [...c.querySelectorAll('.rb-md-table tr')].map(r => [...r.children].map(x => x.textContent)),"
                    "  subheads: [...c.querySelectorAll('.rb-md-h')].map(e => e.textContent),"
                    "  tasks: [...c.querySelectorAll('.rb-tasks .rb-task')].map(t => [t.querySelector('input').checked,"
                    "     t.querySelector('input').disabled, t.querySelector('.rb-task-text').textContent]),"
                    "  boxes: [...c.querySelectorAll('.rb-md-task input')].map(i => [i.checked, i.disabled]),"
                    "  links: [...c.querySelectorAll('.rb-md-link')].map(e => e.dataset.url),"
                    "  refs: [...c.querySelectorAll('.rb-md-ref')].map(e => e.textContent),"
                    "  chips: [...c.querySelectorAll('.rb-card-chips .rb-chip')].map(e => e.textContent),"
                    " }); })()"))
                # The injection attempts are characters on the page, not elements in it.
                self.assertEqual(page["injected"], 0)
                self.assertEqual(page["handlers"], 0)
                self.assertTrue(page["pwned"])
                self.assertIn("<script>window.pwned = 1</script>", page["text"])
                self.assertIn('<img src=x onerror="window.pwned = 2">', page["text"])
                self.assertIn('<b onmouseover="window.pwned=6">not bold</b>', page["text"])
                # The board's own bookkeeping is never shown — in the body, in a task, in a thread
                # entry — except inside a code fence, where it is code.
                for marker in ("relay:entry", "relay:note", "hidden-marker", "t:a1", "t:a3", "t:zz", "s=dropped"):
                    self.assertNotIn(marker, page["text"].replace(page["pre"][0], ""))
                self.assertIn("<!-- kept: this is code -->", page["pre"][0])
                self.assertNotIn("-->", page["text"].replace(page["pre"][0], ""))
                # The body, by section, with the H1 dropped for the page's own title.
                self.assertEqual(page["title"], "Scrollback survives a resume")
                self.assertEqual(page["headings"], ["Acceptance", "Issue", "Decisions (owner, 2026-09-20)",
                                                    "Planning notes", "Plan", "Tasks", "QA checklist", "Thread · the last 6 of 9"])
                self.assertIn("kept as typed", page["text"])
                # Every construct.
                self.assertEqual(page["strong"][:2], ["Bold", "also bold"])
                self.assertEqual(page["em"][:2], ["italic", "also italic"])
                self.assertEqual(page["del"], ["struck"])
                self.assertIn("code span with <b>tags</b>", page["code"])
                self.assertIn("snake_case_name stays whole", page["text"])
                self.assertIn("*not emphasis*", page["text"])
                self.assertEqual(page["ordered"], 3)
                self.assertEqual(page["nested"], 2)
                self.assertIn("A quote, with bold inside.", page["quote"])
                self.assertEqual(page["rules"], 1)
                self.assertEqual(page["table"], [["Option", "Cost", "Verdict"], ["plain text", "0", "yes"],
                                                 ["sqlite", "high", "no | never"]])
                self.assertEqual(page["subheads"], ["Step one", "Deeper"])
                # The tasks come from the card's parsed checklist: read-only boxes.
                self.assertEqual(page["tasks"], [[True, True, "Save on close"], [True, True, "Replay on resume"],
                                                 [False, True, "Rewind's auxiliary file"],
                                                 [False, True, "Cap it at 512 KiB"],
                                                 [True, True, "Compress with sqlite"]])
                self.assertEqual(page["boxes"], [[False, True], [True, True]])
                # Only web addresses are links; the repository path and the script are text.
                self.assertEqual(page["links"], ["https://relay-terminal.ai/docs/scrollback", "https://example.org/auto",
                                                 "https://example.com/bare", "https://example.org/d.png"])
                self.assertIn("the pane (src/Pane.h)", page["text"])
                self.assertIn("script link (javascript:window.pwned=4)", page["text"])
                self.assertEqual(page["refs"], [f"#{BOARD['cards'][0]['id']}"])
                self.assertEqual(page["chips"][:3], ["Discussing", "waiting on you", "✦ agent"])

                # The card opened on the question it is waiting on; the body is a scroll away.
                self.assertGreater(await browser.evaluate("document.querySelector('.rb-card-scroll').scrollTop"), 0)
                await browser.evaluate("document.querySelector('.rb-card-scroll').scrollTop = 0")
                await self.shot(browser, "phone-390x844-card-body")

                # A link is text until it is confirmed: the sheet shows the whole address first.
                await browser.evaluate("window.opened = []; window.open = (...a) => { window.opened.push(a); return null; };")
                await browser.evaluate("document.querySelector('.rb-md-link').click()")
                await browser.wait_for("!!document.querySelector('.rb-sheet[data-kind=\"link\"]')")
                self.assertEqual(await browser.evaluate("window.opened.length"), 0)
                self.assertEqual(await browser.evaluate("document.querySelector('.rb-link-url').textContent"),
                                 "https://relay-terminal.ai/docs/scrollback")
                await browser.evaluate("document.querySelector('.rb-link-open').click()")
                self.assertEqual(json.loads(await browser.evaluate("JSON.stringify(window.opened)")),
                                 [["https://relay-terminal.ai/docs/scrollback", "_blank", "noopener,noreferrer"]])
                self.assertFalse(await browser.evaluate("!!document.querySelector('.rb-sheet')"))
                self.clean(browser)
            finally:
                await browser.stop()

        self.drive(main())

    def test_the_thread_highlights_a_question_and_its_options_prefill_the_reply(self):
        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.start(browser)
                await self.open_board(browser)
                await self.open_card(browser)
                thread = json.loads(await browser.evaluate(
                    "JSON.stringify([...document.querySelectorAll('.rb-thread .rb-entry')].map(e => ({"
                    " kind: e.dataset.kind, author: e.dataset.author,"
                    " head: (e.querySelector('.rb-entry-author') || {}).textContent || '',"
                    " meta: (e.querySelector('.rb-entry-meta') || {}).textContent || '',"
                    " options: [...e.querySelectorAll('.rb-option')].map(o => o.dataset.option)})))"))
                self.assertEqual([e["kind"] for e in thread],
                                 ["comment", "event", "comment", "decision", "progress", "question"])
                self.assertEqual(thread[0]["head"], "owner")
                self.assertTrue(thread[0]["meta"].startswith("Discuss · "))
                self.assertEqual(thread[2]["head"], "✦ agent")
                self.assertIn("kimi-k3", thread[2]["meta"])
                self.assertEqual(thread[5]["options"], ["1", "2"])
                self.assertEqual([e["options"] for e in thread if e["kind"] != "question"], [[]] * 5)
                # The question is the amber one.
                colours = json.loads(await browser.evaluate(
                    "JSON.stringify([...document.querySelectorAll('.rb-thread .rb-entry')].map(e => "
                    "getComputedStyle(e).borderLeftColor))"))
                self.assertNotEqual(colours[5], colours[2])
                self.assertEqual(colours[4], colours[2])
                # …and it is what the reply box is for: the primary send reads Answer.
                self.assertEqual(await browser.evaluate("document.querySelector('.rb-reply-mode').options[2].textContent"), "Answer only")
                await browser.evaluate("document.querySelector('.rb-option[data-option=\"2\"]').click()")
                self.assertEqual(await browser.evaluate("document.querySelector('.rb-reply-text').value"), "2. ")
                self.assertTrue(await browser.evaluate(
                    "document.activeElement === document.querySelector('.rb-reply-text')"))
                await self.shot(browser, "phone-390x844-card-thread")
            finally:
                await browser.stop()

        self.drive(main())

    # ---- the contract: what each action sends ---------------------------------------------------------

    def test_each_action_sends_exactly_the_contracts_message(self):
        async def main():
            browser = Browser()
            await browser.start()
            try:
                await browser.call("Page.addScriptToEvaluateOnNewDocument", {"source":
                    "window.fakePanes = [{id: 'p-other', title: 'build', cwd: '~', status: 'idle'},"
                    " {id: 'd6f19588-fa87-4ed5-a67e-a03b68951a0a', title: 'execute #card', cwd: '~/relay', status: 'running'}];"})
                await self.start(browser)
                await self.open_board(browser)
                await self.open_card(browser)
                before = len(await self.requests(browser))

                async def typed(text):
                    await browser.evaluate(
                        f"(() => {{ const b = document.querySelector('.rb-reply-text'); b.value = {js(text)};"
                        " b.dispatchEvent(new Event('input', {bubbles: true})); })()")

                async def newest():
                    await browser.evaluate(frames(2), timeout=10)
                    return (await self.requests(browser))[-1]

                # The thread ends on the agent's question, so the reply is an answer: `decision`,
                # which the desktop writes down as the owner's own words, quoted.
                await typed("1. beside the session file is fine")
                await browser.evaluate("document.querySelector('.rb-reply-mode').value = 'comment'; document.querySelector('.rb-reply-mode').dispatchEvent(new Event('change')); document.querySelector('.rb-reply-send').click()")
                answer = await newest()
                self.assertEqual(answer, {"t": "board_request", "rid": answer["rid"], "msg_id": answer["msg_id"],
                                          "request": {"type": "board_comment", "id": CARD_ID, "kind": "decision",
                                                      "text": "1. beside the session file is fine"}})
                self.assertEqual(await browser.evaluate("document.querySelector('.rb-reply-text').value"), "")
                written = {"event": "board_written", "id": "remote-3", "kind": "board_comment", "card_id": CARD_ID,
                           "entry_id": "20260921T024029Z-5k", "write_id": "w-1"}
                await browser.evaluate(f"window.fakeRrp.board({answer['rid']}, {js(written)})")
                await browser.wait_for("document.querySelector('.rb-card-line').textContent.includes('Answer recorded')")
                # …and the card is read again, so the thread shows what the desktop wrote.
                reread = await newest()
                self.assertEqual(reread["request"], {"type": "board_card_get", "id": CARD_ID})
                answered = {**CARD, "thread": CARD["thread"] + [
                    {"attrs": {}, "author": "owner", "entry_id": "20260921T024029Z-5k", "kind": "decision",
                     "text": "owner, from Elliott's iPhone: “1. beside the session file is fine”"}]}
                await browser.evaluate(f"window.fakeRrp.board({reread['rid']}, {js(answered)})")
                await browser.wait_for("document.querySelector('.rb-reply-mode').options[2].textContent === 'Comment only'")

                # With nothing to answer it is a comment: `note`, the kind the desktop's own reply
                # box sends (protocol 19.3).
                await typed("and keep the cap at 512 KiB")
                await browser.evaluate("document.querySelector('.rb-reply-mode').value = 'comment'; document.querySelector('.rb-reply-mode').dispatchEvent(new Event('change')); document.querySelector('.rb-reply-send').click()")
                comment = await newest()
                self.assertEqual(comment, {"t": "board_request", "rid": comment["rid"], "msg_id": comment["msg_id"],
                                           "request": {"type": "board_comment", "id": CARD_ID, "kind": "note",
                                                       "text": "and keep the cap at 512 KiB"}})
                await browser.evaluate(f"window.fakeRrp.board({comment['rid']}, {js({**written, 'id': 'remote-4'})})")
                await browser.wait_for("document.querySelector('.rb-card-line').textContent.includes('Comment added')")

                # Discuss and Plan: `board_ask` with the mode. A Plan may go without words.
                await typed("what about guests?")
                await browser.evaluate("document.querySelector('.rb-reply-mode').value = 'discuss'; document.querySelector('.rb-reply-mode').dispatchEvent(new Event('change')); document.querySelector('.rb-reply-send').click()")
                discuss = await newest()
                self.assertEqual(discuss, {"t": "board_request", "rid": discuss["rid"],
                                           "request": {"type": "board_ask", "id": CARD_ID, "mode": "discuss",
                                                       "text": "what about guests?"}})
                # The turn runs: the lamp is lit on the row, Stop is offered, a second ask is not.
                await browser.wait_for("!document.querySelector('.rb-stop').hidden")
                self.assertTrue(await browser.evaluate("document.querySelector('.rb-reply-send').disabled"))
                self.assertTrue(await browser.evaluate("document.querySelector('.rb-action-execute').disabled"))
                await browser.evaluate("document.querySelector('.rb-stop').click()")
                stop = await newest()
                self.assertEqual(stop, {"t": "board_request", "rid": stop["rid"],
                                        "request": {"type": "board_cancel", "id": CARD_ID}})
                # `cards` still names the stopped card: what a worker sent while that turn's
                # thread unwound (the hosted drive's first Stop). The lamp goes out regardless.
                cancelled = {"event": "board_cancelled", "card_id": CARD_ID, "stopped": True,
                             "cards": [CARD_ID]}
                await browser.evaluate(f"window.fakeRrp.board({stop['rid']}, {js(cancelled)})")
                await browser.wait_for("document.querySelector('.rb-stop').hidden")

                await browser.evaluate("document.querySelector('.rb-reply-mode').value = 'plan'; document.querySelector('.rb-reply-mode').dispatchEvent(new Event('change')); document.querySelector('.rb-reply-send').click()")
                plan = await newest()
                self.assertEqual(plan["request"], {"type": "board_ask", "id": CARD_ID, "mode": "plan", "text": ""})
                # The agent's answer lands on the thread and the lamp goes out.
                answer = {"event": "board_thread_appended", "card_id": CARD_ID, "author": "agent", "kind": "comment",
                          "text": "Planned. See **Plan**.", "turn_id": "t-9", "mode": "plan"}
                await browser.evaluate(f"window.fakeRrp.board(null, {js(answer)})")
                await browser.wait_for("document.querySelector('.rb-stop').hidden")
                self.assertIn("Planned. See Plan.", await browser.evaluate(
                    "document.querySelector('.rb-thread').textContent"))

                # Move…: a sheet of the board's statuses, with a reason.
                await browser.evaluate("document.querySelector('.rb-action-move').click()")
                await browser.wait_for("!!document.querySelector('.rb-sheet[data-kind=\"move\"]')")
                stages = json.loads(await browser.evaluate(
                    "JSON.stringify([...document.querySelectorAll('.rb-stage')].map(e => [e.dataset.status, e.disabled]))"))
                self.assertEqual([s for s, _ in stages], BOARD["config"]["statuses"]["work"])
                self.assertEqual([s for s, off in stages if off], ["discussing"])
                await browser.evaluate(
                    "(() => { const r = document.querySelector('.rb-move-reason'); r.value = '  agreed   on the phone ';"
                    " document.querySelector('.rb-stage[data-status=\"planned\"]').click(); })()")
                move = await newest()
                self.assertEqual(move, {"t": "board_request", "rid": move["rid"], "msg_id": move["msg_id"],
                                        "request": {"type": "board_move", "id": CARD_ID, "status": "planned",
                                                    "reason": "agreed on the phone"}})
                self.assertFalse(await browser.evaluate("!!document.querySelector('.rb-sheet')"))

                # Execute: the GUI-level action, and its result is a line with a way to the pane.
                await browser.evaluate("document.querySelector('.rb-action-execute').click()")
                execute = await newest()
                self.assertEqual(execute, {"t": "board_request", "rid": execute["rid"],
                                           "request": {"type": "board_action", "id": CARD_ID, "action": "execute"}})
                # (`pane` is the new pane's session token, cut by the hub to its first eight characters.)
                result = {"event": "board_action_result", "id": CARD_ID, "action": "execute", "ok": True,
                          "pane": "d6f19588", "message": f"#{CARD_ID} is executing in a new pane."}
                await browser.evaluate(f"window.fakeRrp.board({execute['rid']}, {js(result)})")
                await browser.wait_for("!!document.querySelector('.rb-card-line .rb-line-action')")
                self.assertIn("is executing in a new pane.", await browser.evaluate(
                    "document.querySelector('.rb-card-line').textContent"))
                self.assertFalse(await browser.evaluate("!!document.querySelector('.rb-sheet')"))   # a line, never a modal

                # A card with neither a plan nor an acceptance asks once before it goes, on the card
                # and in the desktop's own words; the second tap sends.
                bare = {**CARD, "sections": ["Issue"], "front": {k: v for k, v in CARD["front"].items() if k != "acceptance"}}
                await browser.evaluate(f"window.fakeRrp.board(null, {js(bare)})")
                await browser.evaluate(frames(2), timeout=10)
                sent = len(await self.requests(browser))
                await browser.evaluate("document.querySelector('.rb-action-execute').click()")
                await browser.wait_for("document.querySelector('.rb-card-line').textContent.includes('no plan and no acceptance')")
                self.assertEqual(len(await self.requests(browser)), sent)
                await browser.evaluate("document.querySelector('.rb-action-execute').click()")
                self.assertEqual((await newest())["request"], {"type": "board_action", "id": CARD_ID, "action": "execute"})

                # Verify is offered in the verify lanes instead of Execute.
                verifying = {**CARD, "status": "needs-verification",
                             "front": {**CARD["front"], "status": "needs-verification"}}
                await browser.evaluate(f"window.fakeRrp.board(null, {js(verifying)})")
                await browser.wait_for("!!document.querySelector('.rb-action-verify')")
                self.assertFalse(await browser.evaluate("!!document.querySelector('.rb-action-execute')"))
                await browser.evaluate("document.querySelector('.rb-action-verify').click()")
                verify = await newest()
                self.assertEqual(verify["request"], {"type": "board_action", "id": CARD_ID, "action": "verify"})
                # A refusal is a line too, in the desktop's words (`text`) or the hub's (`message`).
                refused = {"event": "error", "code": "board_refused", "request": "board_action",
                           "text": "No verifier is available for this card."}
                await browser.evaluate(f"window.fakeRrp.board({verify['rid']}, {js(refused)})")
                await browser.wait_for("document.querySelector('.rb-card-line').textContent"
                                       ".includes('No verifier is available')")
                self.assertTrue(await browser.evaluate("document.querySelector('.rb-card-line').classList.contains('rb-error')"))
                await browser.evaluate("document.querySelector('.rb-action-verify').click()")
                again = await newest()
                wedged = {"event": "error", "code": "busy", "source": "hub",
                          "message": "the desktop did not answer in time."}
                await browser.evaluate(f"window.fakeRrp.board({again['rid']}, {js(wedged)})")
                await browser.wait_for("document.querySelector('.rb-card-line').textContent"
                                       ".includes('the desktop did not answer in time.')")

                # The link in an action's line goes to that pane in the inbox.
                await browser.evaluate(f"window.fakeRrp.board({execute['rid']}, {js(result)})")
                await browser.wait_for("!!document.querySelector('.rb-card-line .rb-line-action')")
                await browser.evaluate("document.querySelector('.rb-card-line .rb-line-action').click()")
                await browser.wait_for(shown("screen-thread"))
                self.assertEqual(await browser.evaluate("document.getElementById('thread-title').textContent"),
                                 "execute #card")

                # Nothing but the contract's eleven request types ever went out, and only
                # `board_request` messages carry them (`board_resume` joined with card #7JD1).
                kinds = {m["request"]["type"] for m in await self.requests(browser)}
                self.assertLessEqual(kinds, {"board_open", "board_refresh", "board_card_get", "board_search",
                                             "board_comment", "board_move", "board_create", "board_ask",
                                             "board_cancel", "board_resume", "board_action"})
                self.assertGreater(len(await self.requests(browser)), before)
                self.passes_the_hub(await self.requests(browser))
                self.clean(browser)
            finally:
                await browser.stop()

        self.drive(main())

    def test_a_conflict_and_a_busy_refusal_keep_the_words_and_say_so_in_a_line(self):
        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.start(browser)
                await self.open_board(browser)
                await self.open_card(browser)
                await browser.evaluate(
                    "(() => { const b = document.querySelector('.rb-reply-text'); b.value = 'plan it with guests';"
                    " document.querySelector('.rb-reply-mode').value = 'plan'; document.querySelector('.rb-reply-mode').dispatchEvent(new Event('change')); document.querySelector('.rb-reply-send').click(); })()")
                await browser.evaluate(frames(2), timeout=10)
                ask = (await self.requests(browser))[-1]
                other = BOARD["cards"][3]["id"]
                busy = {"event": "error", "code": "board_busy", "card_id": CARD_ID, "cards": [other],
                        "text": f"busy with a turn on #{other} — wait for it, then start the plan."}
                await browser.evaluate(f"window.fakeRrp.board({ask['rid']}, {js(busy)})")
                await browser.wait_for("document.querySelector('.rb-card-line').textContent.includes('busy with a turn')")
                # The words are back in the box, this card's lamp is out, the other card's is lit.
                self.assertEqual(await browser.evaluate("document.querySelector('.rb-reply-text').value"),
                                 "plan it with guests")
                self.assertTrue(await browser.evaluate("document.querySelector('.rb-stop').hidden"))
                self.assertFalse(await browser.evaluate("!!document.querySelector('.rb-sheet')"))
                await browser.evaluate("document.querySelector('.rb-card-back').click()")
                await browser.wait_for(f"!document.querySelector('.rb-row[data-card-id=\"{other}\"] .rb-row-busy').hidden")

                # A conflict: the card is read again, and the line says why.
                await self.open_card(browser)
                await browser.evaluate(
                    "(() => { const b = document.querySelector('.rb-reply-text'); b.value = 'a note';"
                    " document.querySelector('.rb-reply-mode').value = 'comment'; document.querySelector('.rb-reply-mode').dispatchEvent(new Event('change')); document.querySelector('.rb-reply-send').click(); })()")
                await browser.evaluate(frames(2), timeout=10)
                note = (await self.requests(browser))[-1]
                conflict = {"event": "board_conflict", "card_id": CARD_ID, "current_hash": "ffff"}
                await browser.evaluate(f"window.fakeRrp.board({note['rid']}, {js(conflict)})")
                await browser.wait_for("document.querySelector('.rb-card-line').textContent.includes('changed on the desktop')")
                self.assertEqual(await browser.evaluate("document.querySelector('.rb-reply-text').value"), "a note")
                self.assertEqual((await self.requests(browser))[-1]["request"], {"type": "board_card_get", "id": CARD_ID})
            finally:
                await browser.stop()

        self.drive(main())

    def test_the_fields_stop_at_the_hubs_caps_and_a_cut_card_says_so(self):
        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.start(browser)
                await self.open_board(browser)
                # Over-long text is refused by the hub, not cut (REMOTE-PROTOCOL.md 17.1), so each
                # field stops taking letters at its cap.
                cut = {**CARD, "truncated": True}
                await self.open_card(browser, cut)
                self.assertIn("too long to send", await browser.evaluate(
                    "document.querySelector('.rb-truncated').textContent"))
                await browser.evaluate("document.querySelector('.rb-action-move').click()")
                await browser.evaluate("document.querySelector('.rb-sheet-cancel').click()")
                await browser.evaluate("document.querySelector('.rb-card-back').click()")
                await browser.evaluate("document.querySelector('.rb-add').click()")
                caps = json.loads(await browser.evaluate(
                    "JSON.stringify({reply: document.querySelector('.rb-reply-text').maxLength,"
                    " search: document.querySelector('.rb-search').maxLength,"
                    " title: document.querySelector('.rb-create-title').maxLength,"
                    " text: document.querySelector('.rb-create-text').maxLength})"))
                self.assertEqual(caps, {"reply": 8000, "search": 200, "title": 200, "text": 8000})
                # A label the hub would refuse is refused here, in a sentence, and nothing is sent.
                sent = len(await self.requests(browser))
                await browser.evaluate(
                    "(() => { document.querySelector('.rb-create-text').value = 'a card';"
                    " document.querySelector('.rb-create-labels').value = 'ok, <script>';"
                    " document.querySelector('.rb-create-send').click(); })()")
                self.assertIn("cannot be a label", await browser.evaluate(
                    "document.querySelector('.rb-sheet-note').textContent"))
                self.assertEqual(len(await self.requests(browser)), sent)
                await browser.evaluate("document.querySelector('.rb-sheet-cancel').click()")

                # An agent's answer heard twice — by its rid and fanned out — is one entry.
                await self.open_card(browser)
                before = await browser.evaluate("document.querySelectorAll('.rb-thread .rb-entry').length")
                answer = {"event": "board_thread_appended", "card_id": CARD_ID, "author": "agent", "kind": "comment",
                          "text": "Both recommendations taken.", "turn_id": "t-9", "mode": "discuss"}
                await browser.evaluate(f"window.fakeRrp.board(3, {js(answer)}); window.fakeRrp.board(null, {js(answer)});")
                await browser.evaluate(frames(2), timeout=10)
                self.assertEqual(await browser.evaluate("document.querySelectorAll('.rb-thread .rb-entry').length"),
                                 before + 1)
            finally:
                await browser.stop()

        self.drive(main())

    def test_a_new_card_sends_board_create_with_the_words_verbatim_and_opens_it(self):
        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.start(browser)
                await self.open_board(browser)
                await browser.evaluate("document.querySelector('.rb-tab[data-tab=\"bugs\"]').click()")
                await browser.evaluate("document.querySelector('.rb-add').click()")
                await browser.wait_for("!!document.querySelector('.rb-sheet[data-kind=\"create\"]')")
                # Only folders take a card: the two filter tabs are not offered, and the tab that is
                # open is the one chosen.
                self.assertEqual(json.loads(await browser.evaluate(
                    "JSON.stringify([...document.querySelectorAll('.rb-create-tab option')].map(o => o.value))")),
                    ["features", "bugs", "design"])
                self.assertEqual(await browser.evaluate("document.querySelector('.rb-create-tab').value"), "bugs")
                words = "the  phone's back button\n   goes to the inbox, not the card list\n\ni want it to go back one step"
                await browser.evaluate(
                    f"(() => {{ document.querySelector('.rb-create-title').value = ' Back goes one step ';"
                    f" document.querySelector('.rb-create-text').value = {js(words)};"
                    " document.querySelector('.rb-create-labels').value = 'phone, ux';"
                    " document.querySelector('.rb-create-send').click(); })()")
                await browser.evaluate(frames(2), timeout=10)
                create = (await self.requests(browser))[-1]
                self.assertEqual(create, {"t": "board_request", "rid": create["rid"], "msg_id": create["msg_id"],
                                          "request": {"type": "board_create", "tab": "bugs", "title": "Back goes one step",
                                                      "request": words, "labels": ["phone", "ux"]}})
                # The desktop writes it; the list learns of it; the new card opens.
                row = {**BOARD["cards"][0], "id": "N3W1", "title": "Back goes one step", "tab": "bugs",
                       "status": "inbox", "rank": "00a", "thread_entries": 0, "tasks_total": 0, "tasks_done": 0}
                await browser.evaluate(f"window.fakeRrp.board({create['rid']}, "
                                       f"{js({'event': 'board_written', 'kind': 'board_create', 'card_id': 'N3W1', 'write_id': 'w-2'})})")
                await browser.evaluate(f"window.fakeRrp.board(null, "
                                       f"{js({'event': 'board_changed', 'rev': BOARD['rev'] + 1, 'upserts': [row], 'removed': [], 'problems': []})})")
                await browser.wait_for("document.querySelector('.rb-card-ref').textContent === '#N3W1'")
                self.assertEqual((await self.requests(browser))[-1]["request"], {"type": "board_card_get", "id": "N3W1"})
                self.passes_the_hub(await self.requests(browser))
                # Nothing to say is not a card.
                await browser.evaluate("document.querySelector('.rb-card-back').click()")
                await browser.evaluate("document.querySelector('.rb-add').click()")
                await browser.wait_for("!!document.querySelector('.rb-sheet[data-kind=\"create\"]')")
                sent = len(await self.requests(browser))
                await browser.evaluate("document.querySelector('.rb-create-send').click()")
                self.assertEqual(len(await self.requests(browser)), sent)
                self.assertTrue(await browser.evaluate("!!document.querySelector('.rb-sheet[data-kind=\"create\"]')"))
                await browser.evaluate(
                    "(() => { document.querySelector('.rb-sheet-note').textContent = '';"
                    " document.querySelector('.rb-create-text').value = 'the back button goes to the inbox, not the card list'; })()")
                await asyncio.sleep(0.4)          # the sheet's 140 ms rise
                await self.shot(browser, "phone-390x844-new-card")
                self.clean(browser)
            finally:
                await browser.stop()

        self.drive(main())

    def test_the_microphone_dictates_a_new_card_through_the_desktops_transcription(self):
        async def main():
            browser = Browser(microphone=True)
            await browser.start()
            try:
                await browser.call("Page.addScriptToEvaluateOnNewDocument", {"source":
                    "window.fakePanes = [{id: 'p1', title: 'build', cwd: '~/relay', status: 'idle'}];"})
                await self.start(browser, features="panes,agent,compose,pane_state,voice,board")
                await self.open_board(browser)
                await browser.evaluate("document.querySelector('.rb-add').click()")
                await browser.wait_for("!!document.querySelector('.rb-create-mic') && !document.querySelector('.rb-create-mic').hidden")
                await browser.evaluate("document.querySelector('.rb-create-text').value = 'typed first.'")
                await browser.evaluate("document.querySelector('.rb-create-mic').click()")
                await browser.wait_for("document.querySelector('.rb-create-mic').classList.contains('rb-recording')")
                await asyncio.sleep(0.6)
                await browser.evaluate("document.querySelector('.rb-create-mic').click()")
                # The clip goes to the desktop by the app's own voice path, addressed to a pane that
                # only lends its worker for the round trip; no board request carries audio.
                await browser.wait_for("window.fakeRrp.sent.some(m => m.t === 'voice')")
                clip = json.loads(await browser.evaluate(
                    "JSON.stringify((m => ({t: m.t, pane: m.pane, format: m.format, id: m.id, bytes: m.data.length}))"
                    "(window.fakeRrp.sent.find(m => m.t === 'voice')))"))
                self.assertEqual(clip["pane"], "p1")
                self.assertIn(clip["format"], ("webm", "ogg", "m4a"))
                self.assertGreater(clip["bytes"], 100)
                said = {"t": "agent", "pane": "p1", "id": clip["id"],
                        "event": {"event": "transcribed", "text": "and then dictated."}}
                await browser.evaluate(f"window.fakeRrp.emit('agent', {js(said)})")
                await browser.wait_for("document.querySelector('.rb-create-text').value === 'typed first. and then dictated.'")
                self.assertIn("check it", await browser.evaluate("document.querySelector('.rb-sheet-note').textContent"))
                self.clean(browser)
            finally:
                await browser.stop()

        self.drive(main())

    def test_with_no_pane_to_transcribe_the_sheet_points_to_the_keyboards_dictation(self):
        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.start(browser, features="panes,agent,compose,pane_state,voice,board")
                await self.open_board(browser)
                await browser.evaluate("document.querySelector('.rb-add').click()")
                await browser.wait_for("!!document.querySelector('.rb-sheet[data-kind=\"create\"]')")
                self.assertTrue(await browser.evaluate("document.querySelector('.rb-create-mic').hidden"))
                self.assertEqual(await browser.evaluate("document.querySelector('.rb-sheet-note').textContent"),
                                 "To dictate, use the microphone on your keyboard.")
            finally:
                await browser.stop()

        self.drive(main())

    # ---- offline -----------------------------------------------------------------------------------------

    def test_a_write_made_offline_is_queued_once_and_a_read_shows_the_last_board(self):
        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.start(browser)
                await self.open_board(browser)
                await self.open_card(browser)
                rows_before = await browser.evaluate("document.querySelectorAll('.rb-row').length")
                await browser.evaluate("window.fakeRrp.drop()")
                sent_before = len(await self.requests(browser))

                # A comment typed on the bus: kept, shown as waiting, and said in a line.
                await browser.evaluate(
                    "(() => { const b = document.querySelector('.rb-reply-text'); b.value = 'go, both recommendations';"
                    " document.querySelector('.rb-reply-mode').value = 'comment'; document.querySelector('.rb-reply-mode').dispatchEvent(new Event('change')); document.querySelector('.rb-reply-send').click(); })()")
                await browser.wait_for("!!document.querySelector('.rb-entry[data-pending]')")
                self.assertIn("sends when back online", await browser.evaluate(
                    "document.querySelector('.rb-entry[data-pending] .rb-entry-meta').textContent"))
                self.assertIn("sends when back online", await browser.evaluate(
                    "document.querySelector('.rb-card-line').textContent"))
                # A turn is not something to find running later: refused in a line, words kept.
                await browser.evaluate(
                    "(() => { const b = document.querySelector('.rb-reply-text'); b.value = 'and plan it';"
                    " document.querySelector('.rb-reply-mode').value = 'plan'; document.querySelector('.rb-reply-mode').dispatchEvent(new Event('change')); document.querySelector('.rb-reply-send').click(); })()")
                await browser.wait_for("document.querySelector('.rb-card-line').textContent.includes('Offline')")
                self.assertEqual(await browser.evaluate("document.querySelector('.rb-reply-text').value"), "and plan it")
                await browser.evaluate("document.querySelector('.rb-action-execute').click()")
                # Nothing reached the wire, and exactly one message is waiting.
                self.assertEqual(len(await self.requests(browser)), sent_before)
                self.assertEqual(await browser.evaluate("document.getElementById('link-status').textContent"),
                                 "offline · 1 waiting")

                # The list is the last board, with a line saying so.
                await browser.evaluate("document.querySelector('.rb-card-back').click()")
                self.assertEqual(await browser.evaluate("document.querySelectorAll('.rb-row').length"), rows_before)
                offline = await browser.evaluate("document.querySelector('.rb-offline').textContent")
                self.assertIn("Offline · the board as of", offline)
                self.assertIn("1 card change · sends when back online", offline)
                await self.shot(browser, "phone-390x844-offline")

                # Back online: the comment goes out once, with the id that makes a replay harmless,
                # and the board is asked for again.
                await browser.evaluate("window.dispatchEvent(new Event('online'))")
                await browser.wait_for("window.fakeRrp.boardRequests().some(m => m.request.type === 'board_comment')")
                await browser.evaluate("window.dispatchEvent(new Event('online')); window.dispatchEvent(new Event('online'));")
                await browser.evaluate(frames(4), timeout=10)
                comments = [m for m in await self.requests(browser) if m["request"]["type"] == "board_comment"]
                self.assertEqual(len(comments), 1)
                self.assertEqual(comments[0]["request"], {"type": "board_comment", "id": CARD_ID, "kind": "decision",
                                                          "text": "go, both recommendations"})
                self.assertGreaterEqual(len(comments[0]["msg_id"]), 12)
                opens = [m for m in await self.requests(browser) if m["request"]["type"] == "board_open"]
                self.assertEqual(len(opens), 2)
                await browser.evaluate(f"window.fakeRrp.board({opens[-1]['rid']}, {js(BOARD)})")
                written = {"event": "board_written", "kind": "board_comment", "card_id": CARD_ID, "write_id": "w-3"}
                await browser.evaluate(f"window.fakeRrp.board({comments[0]['rid']}, {js(written)})")
                await browser.wait_for("document.querySelector('.rb-offline').hidden")
                self.assertEqual(await browser.evaluate("document.getElementById('link-status').textContent"), "connected")
                self.clean(browser)
            finally:
                await browser.stop()

        self.drive(main())

    # ---- notifications -------------------------------------------------------------------------------

    def test_a_card_notification_opens_the_board_on_that_card(self):
        async def main():
            browser = Browser()
            await browser.start()
            try:
                # The app was not running: the service worker opened `?card=…` (app/sw.js).
                await self.start(browser, query=f"&card={CARD_ID}")
                self.assertNotIn("card=", await browser.evaluate("location.search"))
                await self.answer_open(browser)
                await browser.wait_for(shown("screen-board"))
                await browser.wait_for("window.fakeRrp.boardRequests().some(m => m.request.type === 'board_card_get')")
                self.assertEqual((await self.last_request(browser, "board_card_get"))["request"],
                                 {"type": "board_card_get", "id": CARD_ID})
                self.assertEqual(await browser.evaluate("document.querySelector('.rb-card-ref').textContent"),
                                 f"#{CARD_ID}")
            finally:
                await browser.stop()

        self.drive(main())

    # ---- layouts -----------------------------------------------------------------------------------------

    def test_composer_space_growth_and_desktop_keyboard_actions(self):
        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.start(browser, size=PHONE)
                await self.open_board(browser)
                await self.open_card(browser)
                self.assertEqual(await browser.evaluate("document.querySelector('.rb-reply-mode').value"), "discuss")
                for width, height in (PHONE, (390, 430), IPAD):
                    await self.window(browser, width, height)
                    await browser.evaluate(frames(), timeout=10)
                    dimensions = json.loads(await browser.evaluate(
                        "JSON.stringify((() => { const b = document.querySelector('.rb-reply-text').getBoundingClientRect();"
                        " const c = document.querySelector('.rb-card-col').getBoundingClientRect();"
                        " return [b.height, b.width / c.width]; })())"))
                    self.assertGreaterEqual(dimensions[0], 72 if height < 520 else 112)
                    self.assertGreater(dimensions[1], 0.9)
                await self.window(browser, *PHONE)
                await browser.evaluate("(() => { const b = document.querySelector('.rb-reply-text');"
                                       " b.value = ('A long sentence to edit before sending.\\n').repeat(30);"
                                       " b.dispatchEvent(new Event('input')); })()")
                self.assertGreater(await browser.evaluate(
                    "document.querySelector('.rb-reply-text').getBoundingClientRect().height"), 112)
                await self.shot(browser, "phone-expanded-prompt")
                for modifiers, kind, mode in [({}, 'board_ask', 'discuss'),
                                               ({'ctrlKey': True}, 'board_ask', 'plan'),
                                               ({'ctrlKey': True, 'shiftKey': True}, 'board_comment', None)]:
                    await browser.evaluate(f"window.fakeRrp.board(null, {{event: 'board_cancelled', card_id: {js(CARD_ID)}}})")
                    await browser.evaluate("(() => { const b = document.querySelector('.rb-reply-text'); b.value = 'keyboard reply';"
                                           f" b.dispatchEvent(new KeyboardEvent('keydown', {{key: 'Enter', bubbles: true, ...{js(modifiers)}}})); }})()")
                    await browser.evaluate(frames(), timeout=10)
                    request = (await self.requests(browser))[-1]['request']
                    self.assertEqual(request['type'], kind)
                    self.assertEqual(request['text'], 'keyboard reply')
                    if mode:
                        self.assertEqual(request['mode'], mode)
                count = len(await self.requests(browser))
                for modifiers in ({'shiftKey': True}, {'isComposing': True}):
                    await browser.evaluate("document.querySelector('.rb-reply-text').dispatchEvent("
                                           f"new KeyboardEvent('keydown', {{key: 'Enter', bubbles: true, ...{js(modifiers)}}}))")
                self.assertEqual(len(await self.requests(browser)), count)
            finally:
                await browser.stop()
        self.drive(main())

    def test_a_phone_in_portrait_shows_one_column_and_the_reply_box_at_the_bottom(self):
        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.start(browser, size=PHONE)
                await self.open_board(browser)
                await self.shot(browser, "phone-390x844-board")
                self.assertFalse(await browser.evaluate(
                    "getComputedStyle(document.querySelector('.rb-card-col')).display !== 'none'"))
                self.assertFalse(await browser.evaluate(
                    "document.documentElement.scrollWidth > document.documentElement.clientWidth"))
                await self.open_card(browser)
                await browser.evaluate(frames(), timeout=10)
                layout = json.loads(await browser.evaluate(
                    "JSON.stringify({list: getComputedStyle(document.querySelector('.rb-list-col')).display,"
                    " reply: (r => [r.left, r.top, r.right, r.bottom])(document.querySelector('.rb-reply').getBoundingClientRect()),"
                    " sends: [...document.querySelectorAll('.rb-reply button:not([hidden])')].map(b => (r => [b.textContent, r.left, r.right, r.top, r.bottom, r.height])(b.getBoundingClientRect())),"
                    " wide: document.querySelector('.rb-card-scroll').scrollWidth > document.querySelector('.rb-card-scroll').clientWidth + 1,"
                    " actions: [...document.querySelectorAll('.rb-actions button')].map(b => b.getBoundingClientRect().height)})"))
                self.assertEqual(layout["list"], "none")
                self.assertEqual(round(layout["reply"][3]), PHONE[1])          # pinned to the bottom
                self.assertEqual([s[0] for s in layout["sends"]], ["Send"])
                for name, left, right, top, bottom, height in layout["sends"]:
                    self.assertGreaterEqual(left, 0, name)
                    self.assertLessEqual(right, PHONE[0], name)
                    self.assertLessEqual(bottom, PHONE[1], name)
                    self.assertGreaterEqual(height, 44, name)
                self.assertFalse(layout["wide"])
                self.assertTrue(all(h >= 44 for h in layout["actions"]))
                await self.shot(browser, "phone-390x844-card")
            finally:
                await browser.stop()

        self.drive(main())

    def test_a_phone_in_landscape_with_its_keyboard_keeps_the_reply_box_and_its_sends_on_screen(self):
        from tests.test_web_viewport import FAKE_VIEWPORT
        width, gap = PHONE_LANDSCAPE_KEYBOARD

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await browser.call("Page.addScriptToEvaluateOnNewDocument", {"source": FAKE_VIEWPORT})
                # Safari's way: the window keeps its height, the visual viewport is the strip.
                await self.start(browser, size=(width, 390))
                await browser.evaluate(f"visualViewport.width = {width}; visualViewport.move({gap}, 0)")
                await browser.wait_for("document.documentElement.classList.contains('tiny-viewport')")
                await self.open_board(browser)
                await self.open_card(browser)
                await browser.evaluate("document.querySelector('.rb-reply-text').focus()")
                await browser.evaluate(frames(), timeout=10)
                layout = json.loads(await browser.evaluate(
                    "JSON.stringify({box: (r => [r.left, r.top, r.right, r.bottom, r.height])(document.querySelector('.rb-reply-text').getBoundingClientRect()),"
                    " sends: [...document.querySelectorAll('.rb-reply button:not([hidden])')].filter(b => getComputedStyle(b).display !== 'none')"
                    "   .map(b => (r => [b.textContent, r.left, r.right, r.top, r.bottom, r.height])(b.getBoundingClientRect())),"
                    " thread: document.querySelector('.rb-card-scroll').getBoundingClientRect().height})"))
                left, top, right, bottom, height = layout["box"]
                self.assertGreaterEqual(top, 0)
                self.assertLessEqual(bottom, gap)
                self.assertGreaterEqual(right - left, 300)       # a box wide enough to type a sentence in
                self.assertEqual([s[0] for s in layout["sends"]], ["Send"])
                for name, s_left, s_right, s_top, s_bottom, s_height in layout["sends"]:
                    self.assertGreaterEqual(s_top, 0, name)
                    self.assertLessEqual(s_bottom, gap, name)
                    self.assertLessEqual(s_right, width, name)
                    self.assertGreaterEqual(s_height, 44, name)
                # …and there is still thread above it to read what is being answered.
                self.assertGreaterEqual(layout["thread"], 60)
                await self.shot(browser, "phone-landscape-keyboard-844x185-card")
            finally:
                await browser.stop()

        self.drive(main())

    def test_an_ipad_shows_the_list_and_the_card_side_by_side(self):
        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.start(browser, size=IPAD)
                await self.open_board(browser)
                await browser.evaluate(frames(), timeout=10)
                # Before a card is picked the right column says so rather than being blank.
                self.assertTrue(await browser.evaluate(
                    "getComputedStyle(document.querySelector('.rb-card-empty')).display !== 'none'"))
                await self.open_card(browser)
                await browser.evaluate(frames(), timeout=10)
                layout = json.loads(await browser.evaluate(
                    "JSON.stringify({list: (r => [r.left, r.right, r.height])(document.querySelector('.rb-list-col').getBoundingClientRect()),"
                    " card: (r => [r.left, r.right, r.height])(document.querySelector('.rb-card-col').getBoundingClientRect()),"
                    " back: getComputedStyle(document.querySelector('.rb-card-back')).display,"
                    " open: document.querySelector('.rb-row.rb-row-open').dataset.cardId,"
                    " reply: (r => [r.left, r.right, r.bottom])(document.querySelector('.rb-reply').getBoundingClientRect())})"))
                self.assertGreaterEqual(layout["list"][1] - layout["list"][0], 340)
                self.assertLessEqual(layout["list"][1], layout["card"][0] + 1)        # side by side, not stacked
                self.assertGreaterEqual(layout["card"][1] - layout["card"][0], 600)
                self.assertEqual(round(layout["card"][1]), IPAD[0])
                self.assertEqual(layout["back"], "none")
                self.assertEqual(layout["open"], CARD_ID)
                self.assertGreaterEqual(layout["reply"][0], layout["card"][0])        # the reply box is the card's
                self.assertEqual(round(layout["reply"][2]), IPAD[1])
                # iOS zooms the page into any field whose text is under 16 px, on an iPad as on a phone.
                sizes = json.loads(await browser.evaluate(
                    "JSON.stringify(['.rb-search', '.rb-reply-text'].map(q => parseFloat(getComputedStyle(document.querySelector(q)).fontSize)))"))
                self.assertTrue(all(size >= 16 for size in sizes), sizes)
                # The section header is the board's enamel label: mono, upper case.
                self.assertIn("mono", await browser.evaluate(
                    "getComputedStyle(document.querySelector('.rb-section')).fontFamily.toLowerCase()"))
                await self.shot(browser, "ipad-1180x820-board-and-card")
                # Picking another card swaps the right column and leaves the list where it was.
                other = next(r for r in BOARD["cards"] if r["status"] == "executing")
                await browser.evaluate(f"document.querySelector('.rb-row[data-card-id=\"{other['id']}\"]').click()")
                await browser.wait_for(f"document.querySelector('.rb-card-ref').textContent === '#{other['id']}'")
                self.assertTrue(await browser.evaluate(
                    "getComputedStyle(document.querySelector('.rb-list-col')).display !== 'none'"))
            finally:
                await browser.stop()

        self.drive(main())


@unittest.skipUnless(find_chrome(), "Chrome is not installed")
class ThroughTheHubTests(unittest.TestCase):
    """The three halves met: the real client, paired to the real hub, with the test as the GUI.

    Everything above plays the hub. Here nothing is swapped: the client's `board_request` crosses
    the Noise session, `remote/host.py` checks the device and `remote/board_state.py` rebuilds the
    request and hands it to the GUI's pipe; the test answers as `src/BoardRemote.cpp` does — with
    the worker's own payload, paths and all — and what reaches the phone has been through the
    scrubber. So this is where a disagreement about a field's name would show."""

    def test_the_board_a_card_and_an_answer_cross_the_real_hub(self):
        from tests.test_remote_pane_state import Harness
        from remote import wire

        def from_worker(event: dict, **extra) -> dict:
            # What the desktop's worker emits, before anything is stripped.
            return {**event, **extra}

        async def main():
            async with Harness(capability=wire.FULL) as harness:
                url, _ = await harness.host.open_pairing()
                browser = Browser()
                await browser.start()
                try:
                    await browser.call("Emulation.setDeviceMetricsOverride",
                                       {"width": PHONE[0], "height": PHONE[1], "deviceScaleFactor": 1, "mobile": False})
                    await browser.navigate(url)
                    await browser.wait_for(shown("screen-inbox"), timeout=40)

                    async def asked(kind: str) -> dict:
                        for _ in range(200):
                            found = [m for m in harness.to_gui if m.get("t") == "board_request"
                                     and m["request"]["type"] == kind]
                            if found:
                                return found[-1]
                            await asyncio.sleep(0.05)
                        raise AssertionError(f"no {kind} reached the GUI: {harness.to_gui[-5:]}")

                    # `welcome.features` has `board` for this `full` device, so the client asks at once.
                    opened = await asked("board_open")
                    self.assertEqual(opened["request"], {"type": "board_open"})
                    self.assertIsInstance(opened["device"], str)
                    rows = [{**card, "path": f"issues/features/{card['id']}.md"} for card in BOARD["cards"]]
                    board = from_worker({k: v for k, v in BOARD.items() if k != "board_name"},
                                        cards=rows, root="/home/elliott/repos/relay-terminal/issues",
                                        workspace="/home/elliott/repos/relay-terminal",
                                        project="/home/elliott/repos/relay-terminal")
                    harness.host.board_event_from_gui({"t": "board_event", "rid": opened["rid"], "event": board})
                    await browser.wait_for("!document.querySelector('.rb-inbox-chip').hidden", timeout=20)
                    self.assertEqual(await browser.evaluate("document.querySelector('.rb-inbox-chip').textContent"),
                                     "3 waiting on you")
                    # The project's path stayed on the desktop; its name is the heading.
                    self.assertEqual(await browser.evaluate("document.querySelector('#board-row .rb-inbox-sub').textContent"),
                                     "relay-terminal · 31 open cards")

                    await browser.evaluate("document.getElementById('board-row').click()")
                    await browser.wait_for(shown("screen-board") + " && document.querySelectorAll('.rb-row').length > 0")
                    await browser.evaluate(f"document.querySelector('.rb-row[data-card-id=\"{CARD_ID}\"]').click()")
                    got = await asked("board_card_get")
                    self.assertEqual(got["request"], {"type": "board_card_get", "id": CARD_ID})
                    card = from_worker(CARD, path=f"issues/features/{CARD_ID}.md")
                    harness.host.board_event_from_gui({"t": "board_event", "rid": got["rid"], "event": card})
                    await browser.wait_for("!!document.querySelector('.rb-thread .rb-entry-question')", timeout=20)
                    page = await browser.evaluate("document.querySelector('.rb-card-col').textContent")
                    self.assertNotIn("issues/features", page)
                    self.assertIn("Two choices before this is built", page)

                    # The answer to the agent's question reaches the GUI as the contract's message.
                    await browser.evaluate(
                        "(() => { const b = document.querySelector('.rb-reply-text'); b.value = 'go, both';"
                        " document.querySelector('.rb-reply-mode').value = 'comment'; document.querySelector('.rb-reply-mode').dispatchEvent(new Event('change')); document.querySelector('.rb-reply-send').click(); })()")
                    comment = await asked("board_comment")
                    self.assertEqual(comment["request"], {"type": "board_comment", "id": CARD_ID,
                                                          "kind": "decision", "text": "go, both"})
                    written = {"event": "board_written", "id": "remote-3", "kind": "board_comment",
                               "card_id": CARD_ID, "entry_id": "20260921T024029Z-5k", "write_id": "w-1"}
                    harness.host.board_event_from_gui({"t": "board_event", "rid": comment["rid"], "event": written})
                    await browser.wait_for("document.querySelector('.rb-card-line').textContent.includes('Answer recorded')",
                                           timeout=20)
                finally:
                    await browser.stop()

        asyncio.run(asyncio.wait_for(main(), 180))


@unittest.skipUnless(shutil.which("node"), "node is not installed")
class CardNotificationTapTests(unittest.TestCase):
    """A `card_waiting` push (REMOTE-PROTOCOL.md 17.5) tapped: the whole path, as
    tests/test_remote_push.py drives it for a pane — `remote/notify.py` builds the body,
    `remote/push.py` seals it, `app/sw.js` opens it, shows it and is tapped."""

    def tapped(self, blob: bytes, mode: str) -> dict:
        key = bytes(range(32))
        done = subprocess.run(
            [shutil.which("node"), str(ROOT / "tests" / "sw_click_peer.mjs"),
             base64.b64encode(key).decode(), base64.b64encode(blob).decode(), mode],
            capture_output=True, text=True, cwd=str(ROOT))
        self.assertEqual(done.returncode, 0, done.stderr)
        return json.loads(done.stdout)

    def test_a_tap_opens_the_board_on_that_card(self):
        from remote import notify, push
        body = notify.Notifier.card_body("K7Q2")
        self.assertEqual((body["kind"], body["card"], body["pane"]), ("card_waiting", "K7Q2", ""))
        blob = push.seal(bytes(range(32)), body)
        focused = self.tapped(blob, "focus")
        self.assertEqual(focused["posted"], [{"t": "open_card", "card": "K7Q2"}])
        self.assertIsNone(focused["opened"])
        self.assertEqual(self.tapped(blob, "open")["opened"], "./?card=K7Q2")

    def test_a_card_that_is_not_a_card_id_opens_nothing_but_the_app(self):
        from remote import push
        blob = push.seal(bytes(range(32)), {"v": 1, "kind": "card_waiting", "pane": "",
                                            "card": "../../x?y=1", "title": "t", "body": "b"})
        self.assertEqual(self.tapped(blob, "open")["opened"], "./")
        self.assertEqual(self.tapped(blob, "focus")["posted"], [])


class BoardStyleTests(unittest.TestCase):
    """app/board.css is held to app/pane.css's rule (tests/test_web_theme.py): it draws with the
    theme generated from the desktop and nothing else, so a theme change cannot miss the board."""

    def test_board_css_uses_only_the_generated_theme(self):
        import re
        css = re.sub(r"/\*.*?\*/", "", (ROOT / "app" / "board.css").read_text(), flags=re.S)
        defined = set(re.findall(r"(--[\w-]+)\s*:", (ROOT / "app" / "pane-theme.css").read_text()
                                 + (ROOT / "app" / "pane.css").read_text()
                                 + (ROOT / "app" / "style.css").read_text() + css))
        for used in sorted(set(re.findall(r"var\((--[\w-]+)", css))):
            self.assertIn(used, defined, f"app/board.css reads {used}, which nothing defines")
        self.assertIsNone(re.search(r"#[0-9a-fA-F]{3,8}\b", css), "app/board.css has a literal hex colour")
        self.assertIsNone(re.search(r"\b(?:rgb|rgba|hsl|hsla)\(", css), "app/board.css has a literal colour function")

    def test_the_view_never_parses_html(self):
        """Card text is the desktop's, and a model wrote much of it: nodes and textContent only."""
        import re
        for name in ("board.js", "boardmd.js"):
            code = re.sub(r"//.*", "", (ROOT / "app" / name).read_text())
            for banned in ("innerHTML", "outerHTML", "insertAdjacentHTML", "DOMParser", "document.write",
                           "createContextualFragment", "srcdoc"):
                self.assertNotIn(banned, code, f"app/{name} uses {banned}")


if __name__ == "__main__":
    unittest.main()
