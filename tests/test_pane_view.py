# SPDX-License-Identifier: AGPL-3.0-or-later
"""The pane view (app/pane.js) in a real browser, against the fixtures the desktop would send.

The view draws `pane_state` (docs/REMOTE-PROTOCOL.md section 16) and sends back actions. What is
worth asserting is exactly the contract between the two, because everything else about the pane
is the desktop's business:

* every label the message carries is drawn as it arrived, character for character (the words on the
  view's own controls — its sheets, its hints, the QUEUE heading — are the view's own, and fixed);
* a row offers the actions the desktop listed for it, and no others;
* a click or a key sends the protocol's message, naming the row by the desktop's own id;
* a state that has been overtaken (a lower `seq`) is ignored rather than drawn;
* a `view` device, which the hub sends no `actions` and no `model.choices`, gets no controls.

`app/pane-demo.html` is the harness: it mounts the view on a fixture and records what it sends.
Skipped when Chrome is not installed.
"""
import asyncio
import json
import shutil
import subprocess
import threading
import unittest
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from tests.browser import Browser, find_chrome

ROOT = Path(__file__).resolve().parent.parent
FIXTURES = ROOT / "tests" / "fixtures" / "pane_state"


class Quiet(SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def serve(directory: Path):
    """A static server over the repository, so the page reaches app/ and the fixtures."""
    server = ThreadingHTTPServer(("127.0.0.1", 0), partial(Quiet, directory=str(directory)))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server, f"http://127.0.0.1:{server.server_address[1]}"


def js(value) -> str:
    return json.dumps(value)


def fixture(name: str) -> dict:
    return json.loads((FIXTURES / f"{name}.json").read_text())


@unittest.skipUnless(find_chrome(), "Chrome is not installed")
class PaneViewTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server, cls.origin = serve(ROOT)

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def drive(self, coroutine, timeout=120):
        return asyncio.run(asyncio.wait_for(coroutine, timeout))

    async def open(self, browser: Browser, name: str, *, query: str = "") -> None:
        await browser.navigate(f"{self.origin}/app/pane-demo.html?fixture={name}&bare=1{query}")
        await browser.wait_for("document.body && document.body.dataset.demoReady === '1'"
                               " && !!document.querySelector('.relay-pane')")
        await browser.wait_for(f"document.body && document.body.dataset.demoSeq === '{fixture(name)['seq']}'")

    async def sent(self, browser: Browser) -> list:
        return await browser.evaluate("JSON.stringify(window.paneDemo.sent)") and json.loads(
            await browser.evaluate("JSON.stringify(window.paneDemo.sent)"))

    def test_every_label_is_the_desktops_and_actions_are_only_what_it_offered(self):
        state = fixture("busy_queue")

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.open(browser, "busy_queue")
                labels = json.loads(await browser.evaluate(
                    "JSON.stringify([...document.querySelectorAll('.rp-row .rp-row-label')]"
                    ".map(e => e.textContent))"))
                self.assertEqual(labels, [row["label"] for row in state["queue"]["rows"]])
                ids = json.loads(await browser.evaluate(
                    "JSON.stringify([...document.querySelectorAll('.rp-row')].map(e => e.dataset.rowId))"))
                self.assertEqual(ids, [row["id"] for row in state["queue"]["rows"]])
                # The × is drawn for exactly the rows whose actions include "remove".
                crosses = json.loads(await browser.evaluate(
                    "JSON.stringify([...document.querySelectorAll('.rp-row')]"
                    ".map(e => [e.dataset.rowId, !!e.querySelector('.rp-row-x')]))"))
                self.assertEqual(crosses,
                                 [[row["id"], "remove" in row["actions"]] for row in state["queue"]["rows"]])
                # The running row and the queue's hint are the desktop's words too.
                self.assertIn(state["queue"]["running"]["label"], await browser.evaluate(
                    "document.querySelector('.rp-queue-running').textContent"))
                self.assertIn(state["queue"]["hint"], await browser.evaluate(
                    "document.querySelector('.rp-queue-hint').textContent"))
                self.assertEqual(browser.console, [])
            finally:
                await browser.stop()

        self.drive(main())

    def test_the_allowance_chip_is_the_desktops_words(self):
        state = fixture("allowance")

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.open(browser, "allowance")
                chip = await browser.evaluate(
                    "(() => { const c = document.querySelector('.rp-allowance');"
                    " return c && {text: c.textContent, title: c.title, warn: c.dataset.warn,"
                    " hidden: c.hidden}; })()")
                # The desktop's words, character for character, with its detail as the title and
                # the warn style at 10% and below (section 16: the view writes none of them).
                self.assertEqual(chip["text"], state["allowance"]["label"])
                self.assertEqual(chip["title"], state["allowance"]["detail"])
                self.assertEqual(chip["warn"], "true")
                self.assertFalse(chip["hidden"])
                for percent, warn in ((10, "true"), (11, "false")):
                    low = json.dumps({**state, "seq": state["seq"] + percent,
                                      "allowance": {**state["allowance"], "percent_left": percent,
                                                    "warn": percent <= 10}})
                    self.assertTrue(await browser.evaluate(f"window.paneDemo.update({low})"))
                    self.assertEqual(await browser.evaluate("document.querySelector('.rp-allowance').dataset.warn"),
                                     warn)
                # A state that stops carrying one — the pane moved to a provider with a key —
                # hides the chip again.
                without = {**state, "seq": state["seq"] + 40}
                without.pop("allowance")
                self.assertTrue(await browser.evaluate(f"window.paneDemo.update({json.dumps(without)})"))
                self.assertTrue(await browser.evaluate("document.querySelector('.rp-allowance').hidden"))
                self.assertEqual(browser.console, [])
            finally:
                await browser.stop()

        self.drive(main())

    def test_the_view_takes_the_desktops_theme(self):
        """The phone's terminal is the colour the desktop's is (owner, 2026-09-19): the state's
        `theme` reaches the pane's data-theme, and the generated block for it is what paints. An id
        the view has no colours for leaves it on the theme it is showing."""
        state = fixture("busy_queue")

        async def main():
            browser = Browser()
            await browser.start()
            try:
                # The demo mounts on Dark Copper, the desktop's default; the state moves it.
                await self.open(browser, "busy_queue", query="&theme=dark-copper")
                self.assertEqual(await browser.evaluate("document.querySelector('.relay-pane').dataset.theme"),
                                 "dark-copper")
                dark = await browser.evaluate(
                    "getComputedStyle(document.querySelector('.relay-pane')).getPropertyValue('--rt-bg').trim()")

                seq = state["seq"]

                async def push(theme):
                    nonlocal seq
                    seq += 1
                    message = {**state, "seq": seq, "theme": theme}
                    self.assertTrue(await browser.evaluate(f"window.paneDemo.update({json.dumps(message)})"))
                    return await browser.evaluate(
                        "(() => { const p = document.querySelector('.relay-pane');"
                        " return {theme: p.dataset.theme || '',"
                        " bg: getComputedStyle(p).getPropertyValue('--rt-bg').trim()}; })()")

                light = await push("relay-light")
                self.assertEqual(light["theme"], "relay-light")
                self.assertNotEqual(light["bg"], dark)
                # Every shipped theme the desktop can be on is one the view can follow.
                for theme in ("gruvbox-dark", "ibm-beige", "relay-dark", "dark-copper"):
                    applied = await push(theme)
                    self.assertEqual(applied["theme"], theme, theme)
                # A theme of the person's own: the pane keeps the colours it has rather than
                # dropping to the default (which is what an unknown data-theme would paint).
                kept = await push("mine-own-theme")
                self.assertEqual(kept["theme"], "dark-copper")
                self.assertEqual(kept["bg"], dark)
                # A state with no theme at all — an older desktop — changes nothing either.
                seq += 1
                message = {**state, "seq": seq}
                message.pop("theme", None)
                self.assertTrue(await browser.evaluate(f"window.paneDemo.update({json.dumps(message)})"))
                self.assertEqual(await browser.evaluate("document.querySelector('.relay-pane').dataset.theme"),
                                 "dark-copper")
                self.assertEqual(browser.console, [])
            finally:
                await browser.stop()

        self.drive(main())

    def test_the_cross_sends_queue_remove_naming_the_desktops_row(self):
        state = fixture("busy_queue")
        steer = next(row for row in state["queue"]["rows"] if row["kind"] == "steer")

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.open(browser, "busy_queue")
                await browser.evaluate(
                    "document.querySelector('.rp-row[data-row-id=\"%s\"] .rp-row-x').click()" % steer["id"])
                await browser.wait_for("window.paneDemo.sent.length > 0")
                self.assertEqual(await self.sent(browser),
                                 [{"t": "queue_remove", "pane": state["pane"], "row": steer["id"]}])
            finally:
                await browser.stop()

        self.drive(main())

    def test_a_state_that_was_overtaken_is_ignored(self):
        state = fixture("busy_queue")

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.open(browser, "busy_queue")
                older = json.dumps({**state, "seq": state["seq"] - 1,
                                    "queue": {**state["queue"], "rows": [], "hint": "stale"}})
                self.assertFalse(await browser.evaluate(f"window.paneDemo.update({older})"))
                rows = await browser.evaluate("document.querySelectorAll('.rp-row').length")
                self.assertEqual(rows, len(state["queue"]["rows"]))
                newer = json.dumps({**state, "seq": state["seq"] + 1,
                                    "queue": {**state["queue"], "rows": [], "hint": "drawn"}})
                self.assertTrue(await browser.evaluate(f"window.paneDemo.update({newer})"))
                self.assertEqual(await browser.evaluate("document.querySelectorAll('.rp-row').length"), 0)
            finally:
                await browser.stop()

        self.drive(main())

    def test_a_view_only_device_is_given_no_controls(self):
        state = fixture("view_only")
        self.assertFalse(any(row.get("actions") for row in state["queue"]["rows"]),
                         "the fixture is the hub's view of a `view` device: no actions")
        # The hub does not drop `composer` for a `view` device, it empties its `modes`
        # (remote/pane_state.py `for_capability`, protocol § 16). The fixture had no `composer` at
        # all, so this test passed over a client that happily drew a prompt box and an enabled Send
        # for a viewer whose every `compose` came back `not_permitted`. Pin what the hub sends.
        self.assertIn("composer", state)
        self.assertEqual(state["composer"]["modes"], [])

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.open(browser, "view_only")
                self.assertEqual(await browser.evaluate("document.querySelectorAll('.rp-row-x').length"), 0)
                # Its rows are still drawn, and still say what the desktop said.
                labels = json.loads(await browser.evaluate(
                    "JSON.stringify([...document.querySelectorAll('.rp-row .rp-row-label')]"
                    ".map(e => e.textContent))"))
                self.assertEqual(labels, [row["label"] for row in state["queue"]["rows"]])
                # Nothing it can press. The composer is not drawn at all (its `modes` is empty),
                # the model chip offers its label and no choice, and there is no "new
                # conversation". Drawn, not merely present: a hidden ancestor counts.
                drawn = ("(s => [...document.querySelectorAll(s)]"
                         ".filter(e => e.getClientRects().length > 0).length)")
                for selector in ('.rp-send', '.rp-input', '.rp-composer'):
                    self.assertEqual(await browser.evaluate(f"{drawn}('{selector}')"), 0,
                                     f"{selector} is drawn for a view-only device")
                self.assertEqual(
                    await browser.evaluate("document.querySelectorAll('.rp-model option:not([disabled])').length"),
                    0, "a view-only device is offered a model to switch to")
                self.assertEqual(await browser.evaluate(f"{drawn}('.rp-sessions-new')"), 0)
                # The owner's other conversations are not part of what a viewer is sent, so the
                # button that opens the list is not drawn either (owner's three levels).
                self.assertNotIn("sessions", state)
                self.assertEqual(await browser.evaluate(f"{drawn}('.rp-sessions-button')"), 0)
                self.assertEqual(await self.sent(browser), [])
            finally:
                await browser.stop()

        self.drive(main())

    def test_the_text_of_a_row_taken_back_lands_in_the_prompt_box(self):
        state = fixture("busy_queue")
        row = next(r for r in state["queue"]["rows"] if "edit" in r["actions"])

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.open(browser, "busy_queue")
                await browser.evaluate(
                    "window.paneDemo.editText(%s)" % json.dumps(
                        {"t": "queue_edit_text", "pane": state["pane"], "row": row["id"],
                         "text": "the words that were in the row"}))
                self.assertEqual(await browser.evaluate("document.querySelector('.rp-input').value"),
                                 "the words that were in the row")
            finally:
                await browser.stop()

        self.drive(main())


    def test_a_chip_too_long_for_the_strip_ellipsizes_instead_of_being_cut(self):
        """A chip is a flex box, and a flex box never ellipsizes text of its own: the words go in
        as an anonymous item, which `text-overflow` does not reach, so at 330 px the clock and the
        folder chip were cut straight through a glyph. The words are in a block child with
        `min-width: 0` now — the one box that can shrink and can end in an ellipsis."""
        state = fixture("busy_queue")

        async def main():
            browser = Browser()
            await browser.start()
            try:
                # A tablet's width — the folder chip is the desktop's and a phone does not draw it
                # (pane.css) — with labels longer than any chip can hold at any size.
                await browser.call("Emulation.setDeviceMetricsOverride",
                                   {"width": 700, "height": 700, "deviceScaleFactor": 1, "mobile": False})
                await self.open(browser, "busy_queue")
                long_state = {
                    **state, "seq": state["seq"] + 1,
                    "turn": {**state.get("turn", {}),
                             "clock": "running · 1 min 12 s · step 4/256 · Esc stops · and a turn "
                                      "clock long enough that no strip on any screen could hold "
                                      "the whole of it in one chip"},
                    "folder": {"label": "~/repos/relay-terminal/backend/relay_core/providers/openrouter"},
                }
                self.assertTrue(await browser.evaluate(f"window.paneDemo.update({json.dumps(long_state)})"))
                for selector, label in ((".rp-clock", long_state["turn"]["clock"]),
                                        (".rp-folder", long_state["folder"]["label"])):
                    with self.subTest(chip=selector):
                        seen = json.loads(await browser.evaluate(
                            "JSON.stringify((c => { const t = c.querySelector('.rp-chip-text');"
                            " if (!t) return {text: c.textContent, present: false};"
                            " const s = getComputedStyle(t); return {text: c.textContent,"
                            " present: true, overflowing: t.scrollWidth > t.clientWidth,"
                            " fits: t.getBoundingClientRect().right <= c.getBoundingClientRect().right + 1,"
                            " ellipsis: s.textOverflow, minWidth: s.minWidth};})"
                            f"(document.querySelector('{selector}')))"))
                        # The desktop's label, character for character, is still what it holds.
                        self.assertEqual(seen["text"], label)
                        self.assertTrue(seen["present"],
                                        f"{selector}'s words are the flex box's own, which cannot ellipsize")
                        # It is too long for the chip — which is the case that used to be cut —
                        # and the box that overflows is the one with the ellipsis on it.
                        self.assertTrue(seen["overflowing"], f"{selector} is not the narrow case")
                        self.assertTrue(seen["fits"], f"{selector}'s words run outside the chip")
                        self.assertEqual(seen["ellipsis"], "ellipsis")
                        self.assertEqual(seen["minWidth"], "0px")
                # And the strip itself does not run off the side of the pane.
                self.assertTrue(await browser.evaluate(
                    "(s => s.scrollWidth <= s.clientWidth + 1)(document.querySelector('.rp-strip'))"))
                self.assertEqual(browser.console, [])
            finally:
                await browser.stop()

        self.drive(main())

    def test_a_phones_strip_is_two_rows_whatever_the_turn_is_doing(self):
        """At 390 px a running turn wrapped the strip onto four rows of bordered 44 px chips. A
        phone lays it out on purpose: the conversation, the model and the level on one row; the
        turn's readings as plain words, then ⋯, Stop and Send on the other. Neither row wraps."""
        state = fixture("busy_queue")
        busy = {**state, "seq": state["seq"] + 1,
                "turn": {**state.get("turn", {}),
                         "clock": "running · 1 min 12 s · step 4/256 · Esc stops"},
                "model": {**state["model"], "effort": "high", "efforts": ["low", "medium", "high"]}}

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await browser.call("Emulation.setDeviceMetricsOverride",
                                   {"width": 390, "height": 844, "deviceScaleFactor": 1, "mobile": True})
                await self.open(browser, "busy_queue")
                self.assertTrue(await browser.evaluate(f"window.paneDemo.update({json.dumps(busy)})"))
                seen = json.loads(await browser.evaluate("""JSON.stringify((() => {
                  const mid = e => (r => Math.round((r.top + r.bottom) / 2))(e.getBoundingClientRect());
                  const top = q => mid(document.querySelector(q));
                  const strip = document.querySelector('.rp-strip');
                  const shown = [...strip.children].filter(e => e.getBoundingClientRect().height);
                  return {rows: new Set(shown.map(mid)).size,
                          sessions: top('.rp-sessions-button'), model: top('.rp-model'),
                          effort: top('.rp-effort'), clock: top('.rp-clock'), more: top('.rp-more'),
                          stop: top('.rp-stop'), send: top('.rp-send'),
                          overflows: strip.scrollWidth > strip.clientWidth + 1,
                          sendRight: document.querySelector('.rp-send-group').getBoundingClientRect().right,
                          stripRight: strip.getBoundingClientRect().right,
                          clockBorder: getComputedStyle(document.querySelector('.rp-clock')).borderTopWidth,
                          clockText: document.querySelector('.rp-clock').textContent};
                })())"""))
                self.assertEqual(seen["rows"], 2, seen)
                self.assertEqual({seen["sessions"], seen["model"], seen["effort"]}, {seen["sessions"]})
                self.assertEqual({seen["clock"], seen["more"], seen["stop"]}, {seen["send"]})
                self.assertGreater(seen["send"], seen["sessions"], "Send is not on the second row")
                self.assertFalse(seen["overflows"])
                self.assertLessEqual(seen["sendRight"], seen["stripRight"] + 1)
                self.assertEqual(seen["clockBorder"], "0px", "a reading is drawn as a button")
                # The desktop's words, whole, even where the row ends them in an ellipsis.
                self.assertEqual(seen["clockText"], busy["turn"]["clock"])
                self.assertEqual(browser.console, [])
            finally:
                await browser.stop()

        self.drive(main())

    def test_a_refused_edit_drops_what_was_typed_on_the_row_and_says_so(self):
        """`queue_edit` carries an id, and the desktop's refusal comes back with it (§6.1).

        The keys typed on a selected row are held for the text the desktop is about to send back.
        When it refuses instead, nothing ever cleared them, so the next row taken back arrived with
        a stray letter in front of its text. The refusal is shown the way the view shows anything:
        the toast over the terminal."""
        state = fixture("busy_queue")
        # The first row the list selects when it takes focus, and another one to be given back.
        row = next(r for r in state["queue"]["rows"] if r["actions"])
        self.assertIn("edit", row["actions"])
        other = next(r for r in state["queue"]["rows"] if "edit" in r["actions"] and r["id"] != row["id"])

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.open(browser, "busy_queue")
                # ↑ on an empty prompt box selects the first row (the one below), and typing on a
                # selected row asks for it back and holds the letter for its text.
                await browser.evaluate(
                    "document.querySelector('.rp-input').dispatchEvent("
                    "new KeyboardEvent('keydown', {key: 'ArrowUp', bubbles: true, cancelable: true}))")
                await browser.evaluate(
                    "document.querySelector('.rp-rows').dispatchEvent("
                    "new KeyboardEvent('keydown', {key: 'y', bubbles: true, cancelable: true}))")
                await browser.wait_for("window.paneDemo.sent.length > 0")
                sent = await self.sent(browser)
                self.assertEqual(sent[-1]["t"], "queue_edit")
                self.assertEqual(sent[-1]["row"], row["id"])
                edit_id = sent[-1]["id"]
                self.assertTrue(edit_id, "queue_edit carries no id, so its refusal cannot be told")
                # An error for something else is not this view's to swallow.
                self.assertFalse(await browser.evaluate(
                    "window.paneDemo.refuse({code: 'internal', message: 'elsewhere'})"))
                self.assertTrue(await browser.evaluate(
                    "window.paneDemo.refuse(%s)" % json.dumps(
                        {"t": "error", "code": "not_permitted", "id": edit_id,
                         "message": "that row is running now."})))
                self.assertEqual(await browser.evaluate("window.paneDemo.toast()"),
                                 "that row is running now.")
                # The letter is gone: the next row the desktop does give back arrives clean.
                await browser.evaluate(
                    "window.paneDemo.editText(%s)" % json.dumps(
                        {"t": "queue_edit_text", "pane": state["pane"], "row": other["id"],
                         "text": "the words that were in the row"}))
                self.assertEqual(await browser.evaluate("document.querySelector('.rp-input').value"),
                                 "the words that were in the row")
                self.assertEqual(browser.console, [])
            finally:
                await browser.stop()

        self.drive(main())

    def test_an_owner_opens_a_past_conversation_by_the_desktops_token(self):
        state = fixture("sessions_50")
        row = next(r for r in state["sessions"]["rows"] if not r.get("current"))

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.open(browser, "sessions_50")
                await browser.evaluate("document.querySelector('.rp-sessions-button').click()")
                await browser.wait_for("!!document.querySelector('.rp-session-list')")
                # The conversation the pane is already on is a line, not a button.
                self.assertEqual(await browser.evaluate(
                    "document.querySelectorAll('.rp-session-row.rp-current .rp-session-open').length"), 0)
                await browser.evaluate(
                    "document.querySelector('.rp-session-row[data-session-id=\"%s\"] .rp-session-open').click()"
                    % row["id"])
                await browser.wait_for("window.paneDemo.sent.length > 0")
                self.assertEqual(await self.sent(browser),
                                 [{"t": "conversation_open", "pane": state["pane"], "session": row["id"]}])
            finally:
                await browser.stop()

        self.drive(main())

    def test_a_session_row_fills_its_line_and_the_sheet_is_not_squashed(self):
        """The open button spans the row, not the 8px dot column it used to land in: on a phone
        the Conversations rows were eight pixels wide and could neither be read nor tapped."""
        state = fixture("sessions_50")

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await browser.call("Emulation.setDeviceMetricsOverride",
                                   {"width": 390, "height": 844, "deviceScaleFactor": 1, "mobile": True})
                await self.open(browser, "sessions_50")
                await browser.evaluate("document.querySelector('.rp-sessions-button').click()")
                await browser.wait_for("!!document.querySelector('.rp-session-list')")
                widths = await browser.evaluate(
                    "JSON.stringify([...document.querySelectorAll('.rp-session-open')]"
                    ".slice(0, 3).map((b) => Math.round(b.getBoundingClientRect().width)))")
                for width in json.loads(widths):
                    self.assertGreater(width, 200, f"an open button only {width}px wide")
            finally:
                await browser.stop()

        self.drive(main())

    def test_copy_id_asks_by_the_token_and_copies_the_desktops_answer(self):
        state = fixture("sessions_50")
        row = state["sessions"]["rows"][0]
        conversation = "9f2c7a1e-4b3d-4e5f-8a90-1b2c3d4e5f60"

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.open(browser, "sessions_50")
                await browser.evaluate("document.querySelector('.rp-sessions-button').click()")
                await browser.wait_for("!!document.querySelector('.rp-session-list')")
                await browser.evaluate("""
                  (() => { let copied = '';
                    Object.defineProperty(navigator, 'clipboard',
                      { value: { writeText: (text) => { copied = text; return Promise.resolve(); } },
                        configurable: true });
                    window.paneDemo.copied = () => copied; })()
                """)
                await browser.evaluate(
                    "document.querySelector('.rp-session-row[data-session-id=\\\"%s\\\"] "
                    ".rp-session-copy').click()" % row["id"])
                await browser.wait_for("window.paneDemo.sent.length > 0")
                sent = (await self.sent(browser))[-1]
                self.assertEqual(sent["t"], "conversation_id")
                self.assertEqual(sent["session"], row["id"])
                self.assertEqual(sent["pane"], state["pane"])
                self.assertTrue(sent["id"], "the view mints an id for the ask")
                await browser.evaluate(
                    "window.paneDemo.conversationId({t: 'conversation_id_text', pane: %s,"
                    " session: %s, conversation: %s, id: %s})"
                    % (js(state["pane"]), js(row["id"]), js(conversation), js(sent["id"])))
                self.assertEqual(await browser.evaluate("window.paneDemo.copied()"), conversation)
                self.assertIn("copied", await browser.evaluate("window.paneDemo.toast()"))
                # An answer to an id this view never asked reaches nobody's clipboard.
                await browser.evaluate(
                    "window.paneDemo.conversationId({t: 'conversation_id_text', pane: %s,"
                    " session: %s, conversation: 'nope', id: 'other'})"
                    % (js(state["pane"]), js(row["id"])))
                self.assertEqual(await browser.evaluate("window.paneDemo.copied()"), conversation)
            finally:
                await browser.stop()

        self.drive(main())

    CLIPBOARD = """
      (() => { let copied = ''; let refuse = %s;
        Object.defineProperty(navigator, 'clipboard',
          { value: { writeText: (text) => { if (refuse) return Promise.reject(new Error('no'));
                                            copied = text; return Promise.resolve(); } },
            configurable: true });
        window.paneDemo.copied = () => copied; })()
    """

    async def open_sessions(self, browser, *, query="&input=touch"):
        await self.open(browser, "sessions_50", query=query)
        await browser.evaluate("document.querySelector('.rp-sessions-button').click()")
        await browser.wait_for("!!document.querySelector('.rp-session-list')")

    def test_the_id_is_asked_for_on_the_press_so_the_tap_writes_it_itself(self):
        """#CPY4: `navigator.clipboard.writeText()` ran in the answer handler, after a wire round
        trip, with no user activation left — which on iOS is a refusal every time. The press asks;
        the tap writes what the press brought back."""
        state = fixture("sessions_50")
        row = state["sessions"]["rows"][0]
        conversation = "9f2c7a1e-4b3d-4e5f-8a90-1b2c3d4e5f60"
        copy = (".rp-session-row[data-session-id=\\\"%s\\\"] .rp-session-copy" % row["id"])

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.open_sessions(browser)
                await browser.evaluate(self.CLIPBOARD % "false")
                # The finger goes down: that is where the ask is sent from.
                await browser.evaluate(
                    "document.querySelector('%s').dispatchEvent(new PointerEvent('pointerdown',"
                    " {bubbles: true, pointerType: 'touch'}))" % copy)
                await browser.wait_for("window.paneDemo.sent.length > 0")
                asked = (await self.sent(browser))[-1]
                self.assertEqual(asked["t"], "conversation_id")
                self.assertEqual(asked["session"], row["id"])
                self.assertTrue(asked["id"])
                # The desktop answers while the finger is still down. Nothing is written yet.
                await browser.evaluate(
                    "window.paneDemo.conversationId({t: 'conversation_id_text', pane: %s,"
                    " session: %s, conversation: %s, id: %s})"
                    % (js(state["pane"]), js(row["id"]), js(conversation), js(asked["id"])))
                self.assertEqual(await browser.evaluate("window.paneDemo.copied()"), "")
                # The finger lifts: the write is the first thing in the tap, from what was kept.
                await browser.evaluate("document.querySelector('%s').click()" % copy)
                self.assertEqual(await browser.evaluate("window.paneDemo.copied()"), conversation)
                self.assertIn("copied", await browser.evaluate("window.paneDemo.toast()"))
                # And the tap closed the sheet, so the toast is not under it.
                self.assertTrue(await browser.evaluate("document.querySelector('.rp-layer').hidden"))
                # One press, one ask: the id is kept, so a second tap sends nothing.
                before = len(await self.sent(browser))
                await browser.evaluate("document.querySelector('.rp-sessions-button').click()")
                await browser.wait_for("!!document.querySelector('.rp-session-list')")
                await browser.evaluate(
                    "document.querySelector('%s').dispatchEvent(new PointerEvent('pointerdown',"
                    " {bubbles: true, pointerType: 'touch'}))" % copy)
                await browser.evaluate("document.querySelector('%s').click()" % copy)
                self.assertEqual(len(await self.sent(browser)), before,
                                 "an id already answered for was asked for again")
                self.assertEqual(browser.console, [])
            finally:
                await browser.stop()

        self.drive(main())

    def test_a_copy_id_outcome_is_the_topmost_thing_on_the_pane(self):
        """#CPY4: the toast lives in `.rp-term-wrap`, which has no z-index, under a sheet layer at
        `z-index: 10` with a 70%-opaque backdrop. Measured before this: `elementFromPoint` at the
        toast's centre returned `rp-session-when`, a row of the sheet."""
        state = fixture("sessions_50")
        row = state["sessions"]["rows"][0]
        conversation = "9f2c7a1e-4b3d-4e5f-8a90-1b2c3d4e5f60"
        copy = (".rp-session-row[data-session-id=\\\"%s\\\"] .rp-session-copy" % row["id"])
        # The measurement the card's "Done means" names: the sheet's row used to be the answer.
        topmost = """
          (() => { const t = document.querySelector('.rp-toast');
            if (t.hidden) return 'the toast is not up';
            const b = t.getBoundingClientRect();
            const at = document.elementFromPoint(b.left + b.width / 2, b.top + b.height / 2);
            if (!at) return 'nothing';
            return (at === t || t.contains(at)) ? 'the toast' : at.className; })()
        """

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await browser.call("Emulation.setDeviceMetricsOverride",
                                   {"width": 390, "height": 844, "deviceScaleFactor": 1,
                                    "mobile": True})
                await self.open_sessions(browser)
                # The clipboard refuses, which is the iOS path: the id itself goes in the toast.
                await browser.evaluate(self.CLIPBOARD % "true")
                await browser.evaluate("document.querySelector('%s').click()" % copy)
                await browser.wait_for("window.paneDemo.sent.length > 0")
                asked = (await self.sent(browser))[-1]
                # The answer lands after the sheet has been re-opened over the terminal.
                await browser.evaluate("document.querySelector('.rp-sessions-button').click()")
                await browser.wait_for("!!document.querySelector('.rp-session-list')")
                await browser.evaluate(
                    "window.paneDemo.conversationId({t: 'conversation_id_text', pane: %s,"
                    " session: %s, conversation: %s, id: %s})"
                    % (js(state["pane"]), js(row["id"]), js(conversation), js(asked["id"])))
                await browser.wait_for("!document.querySelector('.rp-toast').hidden")
                self.assertFalse(await browser.evaluate("document.querySelector('.rp-layer').hidden"),
                                 "the sheet must be up for this to measure anything")
                self.assertEqual(await browser.evaluate(topmost), "the toast")
                # The refusal says the id, as selectable text rather than a line in some sheet.
                self.assertIn(conversation, await browser.evaluate("window.paneDemo.toast()"))
                self.assertEqual(await browser.evaluate(
                    "document.querySelector('.rp-toast .rp-session-id').textContent"), conversation)
                self.assertEqual(await browser.evaluate(
                    "document.querySelectorAll('.rp-sheet .rp-session-id').length"), 0,
                    "the id was appended to whatever sheet happened to be in the DOM")
            finally:
                await browser.stop()

        self.drive(main())

    def test_a_minute_tick_leaves_the_conversations_where_the_reader_left_them(self):
        """#CPY4: the rebuild was gated on a signature of the whole `sessions` block, which
        includes each row's `when` — a relative time the desktop recomputes on every publish.
        Measured before this: `scrollTop` 2010 → 0, focus back on "New conversation"."""
        state = fixture("sessions_50")

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await browser.call("Emulation.setDeviceMetricsOverride",
                                   {"width": 390, "height": 844, "deviceScaleFactor": 1,
                                    "mobile": True})
                await self.open_sessions(browser)
                await browser.evaluate("document.querySelector('.rp-sheet').scrollTop = 2010")
                scrolled = await browser.evaluate("document.querySelector('.rp-sheet').scrollTop")
                self.assertGreater(scrolled, 0, "the sheet did not scroll, so this proves nothing")
                node = "document.querySelectorAll('.rp-session-row')[3]"
                await browser.evaluate(f"{node}.dataset.tickMark = '1'")

                ticked = json.loads(json.dumps(state))
                ticked["seq"] += 1
                for r in ticked["sessions"]["rows"]:
                    r["when"] = "one minute later"
                self.assertTrue(await browser.evaluate(
                    f"window.paneDemo.update({json.dumps(ticked)})"))
                self.assertEqual(await browser.evaluate(
                    "document.querySelector('.rp-sheet').scrollTop"), scrolled)
                self.assertTrue(await browser.evaluate(
                    f"!!{node} && {node}.dataset.tickMark === '1'"),
                    "the rows were rebuilt, so a finger already down on one is on a dead node")
                # The clock still moved: it is patched in, not left stale.
                whens = json.loads(await browser.evaluate(
                    "JSON.stringify([...document.querySelectorAll('.rp-session-when')]"
                    ".map((e) => e.textContent))"))
                self.assertEqual(set(whens), {"one minute later"})

                # A row that really changed still rebuilds the sheet.
                renamed = json.loads(json.dumps(ticked))
                renamed["seq"] += 1
                renamed["sessions"]["rows"][3]["title"] = "a different conversation"
                self.assertTrue(await browser.evaluate(
                    f"window.paneDemo.update({json.dumps(renamed)})"))
                self.assertFalse(await browser.evaluate(f"{node}.dataset.tickMark === '1'"))
                self.assertEqual(browser.console, [])
            finally:
                await browser.stop()

        self.drive(main())

    def test_the_effort_picker_shows_the_desktops_levels_and_sends_one(self):
        """The model's levels (section 3), drawn as they arrived, and a pick sent back by name."""
        state = fixture("idle")

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.open(browser, "idle")
                levels = await browser.evaluate(
                    "JSON.stringify([...document.querySelectorAll('.rp-effort option')]"
                    ".map((o) => [o.value, o.textContent]))")
                # The model menu's shape: a disabled placeholder carrying the plain current level,
                # then the levels with the ✓ on the one the pane is on (#EFT9).
                self.assertEqual(json.loads(levels),
                                 [["", "high"], ["low", "low"], ["medium", "medium"],
                                  ["high", "✓ high"]])
                await browser.evaluate(
                    "(() => { const e = document.querySelector('.rp-effort');"
                    " e.value = 'low'; e.dispatchEvent(new Event('change')); })()")
                await browser.wait_for("window.paneDemo.sent.length > 0")
                self.assertEqual((await self.sent(browser))[-1],
                                 {"t": "effort_pick", "pane": state["pane"], "effort": "low"})
                # A model that takes no level draws no picker (a `view` fixture has none at all).
                await self.open(browser, "view_only")
                self.assertEqual(await browser.evaluate(
                    "getComputedStyle(document.querySelector('.rp-effort')).display"), "none")
            finally:
                await browser.stop()

        self.drive(main())

    # The prompt box, typed into the way a person does: the value and the `input` event the view
    # sizes and enables Send from.
    TYPE = ("(() => { const b = document.querySelector('.rp-input'); b.focus(); b.value = %s;"
            " b.dispatchEvent(new Event('input', {bubbles: true})); })()")
    PRESS = ("document.activeElement.dispatchEvent(new KeyboardEvent('keydown',"
             " {key: %s, bubbles: true, cancelable: true}))")

    def test_send_puts_the_keyboard_down_on_a_thumb_and_leaves_the_focus_on_a_laptop(self):
        """#KBD7: the Send button called `box.focus()` on the statement after `compose()` blurred,
        so the tap a phone actually uses put the on-screen keyboard straight back up. The blur
        cannot be unconditional either: `enter()` is reachable only from the box's own keydown, so
        a laptop that lost the focus lost Enter's escalation with it."""

        async def main():
            browser = Browser()
            await browser.start()
            try:
                # A touch device: sent is sent, and the keyboard comes down.
                await self.open(browser, "busy_queue", query="&input=touch")
                await browser.evaluate(self.TYPE % js("plan the next step"))
                self.assertTrue(await browser.evaluate(
                    "document.activeElement === document.querySelector('.rp-input')"))
                await browser.evaluate("document.querySelector('.rp-send').click()")
                await browser.wait_for("window.paneDemo.sent.length > 0")
                self.assertFalse(await browser.evaluate(
                    "document.activeElement === document.querySelector('.rp-input')"),
                    "a tap on Send left the box focused, so the keyboard came back up")
                self.assertEqual(await browser.evaluate("document.querySelector('.rp-input').value"), "")

                # A device with a physical keyboard: the box keeps the focus, and the second Enter
                # on the now-empty box still makes the queued prompt a steer.
                await self.open(browser, "busy_queue", query="&input=mouse")
                await browser.evaluate(self.TYPE % js("plan the next step"))
                await browser.evaluate("document.querySelector('.rp-send').click()")
                await browser.wait_for("window.paneDemo.sent.length > 0")
                self.assertTrue(await browser.evaluate(
                    "document.activeElement === document.querySelector('.rp-input')"),
                    "the box lost the focus Enter's escalation needs")
                sent = await self.sent(browser)
                self.assertEqual(sent[0]["t"], "compose")
                self.assertEqual(sent[0]["when"], "queue")
                await browser.evaluate(self.PRESS % js("Enter"))
                await browser.wait_for("window.paneDemo.sent.length > 1")
                sent = await self.sent(browser)
                self.assertEqual([sent[1]["t"], sent[1]["when"], sent[1]["text"]],
                                 ["compose", "steer", "plan the next step"])
                self.assertEqual(browser.console, [])
            finally:
                await browser.stop()

        self.drive(main())

    def test_answering_the_ask_empties_the_box_the_way_an_ordinary_send_does(self):
        """#KBD7: the ask branch of `compose()` returned without clearing the box, so the typed
        answer to question 1 was still there, with Send enabled, and the next tap sent it again as
        the answer to question 2."""
        state = fixture("idle")
        wrapped = {"t": "agent", "pane": state["pane"], "event": self.QUESTION}

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.open(browser, "idle", query="&input=touch")
                self.assertTrue(await browser.evaluate(
                    "window.paneDemo.agentEvent(%s)" % json.dumps(wrapped)))
                await browser.evaluate(self.TYPE % js("this file only, please"))
                self.assertFalse(await browser.evaluate("document.querySelector('.rp-send').disabled"))
                await browser.evaluate(self.PRESS % js("Enter"))
                await browser.wait_for("window.paneDemo.sent.length > 0")
                self.assertEqual(await browser.evaluate("document.querySelector('.rp-input').value"), "",
                                 "the answer stayed in the box and the next tap would send it again")
                self.assertTrue(await browser.evaluate("document.querySelector('.rp-send').disabled"))
                # And the view has stepped to question 2, which the empty box now answers.
                self.assertEqual(await browser.evaluate(
                    "document.querySelector('.rp-ask-header').textContent"), "Anything else")
                self.assertEqual(browser.console, [])
            finally:
                await browser.stop()

        self.drive(main())

    def test_a_level_only_state_repaints_the_chip_and_the_closed_chip_has_no_tick(self):
        """#EFT9: the level is in neither half of the model's signature, so a `pane_state` whose
        model did not move used to be dropped and the chip kept the level the pane had left. And
        the closed control read `✓▾high`: the tick was baked into the option's own text and the
        model's chevron was anchored to the level's right edge."""
        state = fixture("idle")

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.open(browser, "idle")
                closed = await browser.evaluate(
                    "(() => { const e = document.querySelector('.rp-effort');"
                    " return JSON.stringify([e.value, e.options[e.selectedIndex].textContent]); })()")
                self.assertEqual(json.loads(closed), ["", "high"],
                                 "the closed chip says the level, with no ✓ and no value to send")
                # The chevrons: one each, and neither one over the other control's words.
                boxes = json.loads(await browser.evaluate("""
                  JSON.stringify({
                    model: document.querySelector('.rp-model').getBoundingClientRect().toJSON(),
                    modelChevron: document.querySelector('.rp-model-box .rp-model-chevron')
                      .getBoundingClientRect().toJSON(),
                    effort: document.querySelector('.rp-effort').getBoundingClientRect().toJSON(),
                    effortChevron: document.querySelector('.rp-effort-chevron')
                      .getBoundingClientRect().toJSON()})
                """))
                self.assertLessEqual(boxes["modelChevron"]["right"], boxes["model"]["right"] + 1)
                self.assertGreaterEqual(boxes["modelChevron"]["left"], boxes["model"]["left"])
                self.assertLessEqual(boxes["effortChevron"]["right"], boxes["effort"]["right"] + 1)
                self.assertGreaterEqual(boxes["effortChevron"]["left"], boxes["effort"]["left"])

                # The same model, one level lower: the chip follows it.
                lower = dict(state, seq=state["seq"] + 1,
                             model=dict(state["model"], effort="low"))
                self.assertTrue(await browser.evaluate(
                    f"window.paneDemo.update({json.dumps(lower)})"))
                after = await browser.evaluate(
                    "(() => { const e = document.querySelector('.rp-effort');"
                    " return JSON.stringify([e.options[e.selectedIndex].textContent,"
                    " [...e.options].map((o) => o.textContent)]); })()")
                self.assertEqual(json.loads(after),
                                 ["low", ["low", "✓ low", "medium", "high"]])
                self.assertEqual(browser.console, [])
            finally:
                await browser.stop()

        self.drive(main())

    def test_a_model_whose_level_is_fixed_draws_a_chip_that_cannot_be_picked_from(self):
        """#EFT9: `effort_fixed` on the model block (Relay Free is exactly this case). Untested
        against a real desktop until the wire carries the field."""
        state = fixture("idle")

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.open(browser, "idle")
                self.assertFalse(await browser.evaluate(
                    "document.querySelector('.rp-effort').disabled"))
                fixed = dict(state, seq=state["seq"] + 1,
                             model=dict(state["model"], effort_fixed=True))
                self.assertTrue(await browser.evaluate(
                    f"window.paneDemo.update({json.dumps(fixed)})"))
                self.assertTrue(await browser.evaluate(
                    "document.querySelector('.rp-effort').disabled"))
                self.assertEqual(await browser.evaluate(
                    "getComputedStyle(document.querySelector('.rp-effort-chevron')).display"),
                    "none")
            finally:
                await browser.stop()

        self.drive(main())

    def test_a_refused_id_ask_is_a_toast_on_the_pane(self):
        state = fixture("sessions_50")
        row = state["sessions"]["rows"][0]

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.open(browser, "sessions_50")
                await browser.evaluate("document.querySelector('.rp-sessions-button').click()")
                await browser.wait_for("!!document.querySelector('.rp-session-list')")
                await browser.evaluate(
                    "document.querySelector('.rp-session-row[data-session-id=\\\"%s\\\"] "
                    ".rp-session-copy').click()" % row["id"])
                await browser.wait_for("window.paneDemo.sent.length > 0")
                sent = (await self.sent(browser))[-1]
                self.assertTrue(await browser.evaluate(
                    "window.paneDemo.refuse({id: %s, message: 'not part of a share'})"
                    % js(sent["id"])))
                self.assertIn("refused", await browser.evaluate("window.paneDemo.toast()"))
            finally:
                await browser.stop()

        self.drive(main())


    # ---- Stop, Recap, the agent's ask and the owner's decisions (card #PH0N, phase 2.6) --------

    DRAWN = ("(s => [...document.querySelectorAll(s)]"
             ".filter(e => e.getClientRects().length > 0).length)")

    def test_stop_is_drawn_only_while_a_turn_runs_and_sends_agent_stop(self):
        busy = fixture("busy_queue")
        self.assertTrue(busy["turn"]["busy"])
        idle = fixture("idle")
        self.assertFalse(idle["turn"]["busy"])

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.open(browser, "idle")
                self.assertEqual(await browser.evaluate(f"{self.DRAWN}('.rp-stop')"), 0,
                                 "Stop is drawn with nothing to stop")
                await self.open(browser, "busy_queue")
                self.assertEqual(await browser.evaluate(f"{self.DRAWN}('.rp-stop')"), 1)
                await browser.evaluate("document.querySelector('.rp-stop').click()")
                await browser.wait_for("window.paneDemo.sent.length > 0")
                self.assertEqual(await self.sent(browser), [{"t": "agent_stop", "pane": busy["pane"]}])
                # The desktop's `agent_stopped` takes `turn.busy` down and the button with it.
                later = dict(busy, seq=busy["seq"] + 1, turn={"phase": "idle", "clock": "", "busy": False})
                await browser.evaluate("window.paneDemo.update(%s)" % json.dumps(later))
                self.assertEqual(await browser.evaluate(f"{self.DRAWN}('.rp-stop')"), 0)
                # A view-only device has nothing to stop with, busy or not.
                await self.open(browser, "view_only")
                self.assertEqual(await browser.evaluate(f"{self.DRAWN}('.rp-stop')"), 0)
                self.assertEqual(await browser.evaluate(f"{self.DRAWN}('.rp-more')"), 0)
            finally:
                await browser.stop()

        self.drive(main())

    def test_recap_is_under_the_pane_menu_and_sends_recap_request(self):
        state = fixture("idle")

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.open(browser, "idle")
                self.assertEqual(await browser.evaluate(f"{self.DRAWN}('.rp-more')"), 1)
                await browser.evaluate("document.querySelector('.rp-more').click()")
                await browser.wait_for("!!document.querySelector('.rp-sheet-item[data-action=\"recap\"]')")
                await browser.evaluate("document.querySelector('.rp-sheet-item[data-action=\"recap\"]').click()")
                await browser.wait_for("window.paneDemo.sent.length > 0")
                self.assertEqual(await self.sent(browser), [{"t": "recap_request", "pane": state["pane"]}])
            finally:
                await browser.stop()

        self.drive(main())

    QUESTION = {
        "event": "question", "id": "q-1", "turn_id": "t-1",
        "questions": [
            {"header": "Which files", "question": "Apply the rename to which files?",
             "options": [{"label": "This file only", "description": "just Pane.h", "recommended": True},
                         {"label": "git status", "description": "a label that looks like a command"}],
             "multiple": False},
            {"header": "Anything else", "question": "Anything I should know before I start?",
             "options": [], "multiple": False},
        ],
    }

    def test_the_agents_question_is_drawn_and_a_tap_answers_it_agent_bound(self):
        state = fixture("idle")            # a `full` device: composer modes auto/shell/agent
        self.assertIn("shell", state["composer"]["modes"])
        wrapped = {"t": "agent", "pane": state["pane"], "seq": 7, "event": self.QUESTION}

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.open(browser, "idle")
                self.assertEqual(await browser.evaluate(f"{self.DRAWN}('.rp-ask')"), 0)
                self.assertTrue(await browser.evaluate("window.paneDemo.agentEvent(%s)" % json.dumps(wrapped)))
                self.assertEqual(await browser.evaluate(f"{self.DRAWN}('.rp-ask')"), 1)
                # The worker's own words, character for character; the step count is the view's.
                self.assertEqual(await browser.evaluate("document.querySelector('.rp-ask-header').textContent"),
                                 "Which files")
                self.assertEqual(await browser.evaluate("document.querySelector('.rp-ask-text').textContent"),
                                 "Apply the rename to which files?")
                self.assertEqual(await browser.evaluate("document.querySelector('.rp-ask-step').textContent"),
                                 "1 of 2")
                labels = json.loads(await browser.evaluate(
                    "JSON.stringify([...document.querySelectorAll('.rp-ask-choice')].map(e => e.textContent))"))
                self.assertEqual(labels, ["This file only", "git status"])
                self.assertEqual(await browser.evaluate(
                    "document.querySelector('.rp-ask-choice.rp-recommended').textContent"), "This file only")
                # A choice that looks like a command is still an answer: sent agent-bound (no
                # `agent: false`), so the desktop's ask takes it rather than the shell.
                await browser.evaluate("document.querySelectorAll('.rp-ask-choice')[1].click()")
                await browser.wait_for("window.paneDemo.sent.length > 0")
                sent = await self.sent(browser)
                self.assertEqual(sent[0]["t"], "compose")
                self.assertEqual(sent[0]["pane"], state["pane"])
                self.assertEqual(sent[0]["text"], "git status")
                self.assertEqual(sent[0]["when"], "now")
                self.assertNotIn("agent", sent[0])
                self.assertTrue(sent[0]["msg_id"])
                # The next question: open, so no choices, only Skip — and the prompt box answers it.
                self.assertEqual(await browser.evaluate("document.querySelector('.rp-ask-header').textContent"),
                                 "Anything else")
                self.assertEqual(await browser.evaluate("document.querySelectorAll('.rp-ask-choice').length"), 0)
                self.assertEqual(await browser.evaluate(f"{self.DRAWN}('.rp-ask-skip')"), 1)
                await browser.evaluate("document.querySelector('.rp-input').value = 'ls -la first please'")
                await browser.evaluate(
                    "document.querySelector('.rp-input').dispatchEvent("
                    "new KeyboardEvent('keydown', {key: 'Enter', bubbles: true, cancelable: true}))")
                await browser.wait_for("window.paneDemo.sent.length > 1")
                sent = await self.sent(browser)
                self.assertEqual(sent[1]["text"], "ls -la first please")
                self.assertNotIn("agent", sent[1], "an answer typed under the ask is agent-bound too")
                # Both answered: the ask is gone, and the box routes as before.
                self.assertEqual(await browser.evaluate(f"{self.DRAWN}('.rp-ask')"), 0)
                await browser.evaluate("document.querySelector('.rp-input').value = 'git status'")
                await browser.evaluate(
                    "document.querySelector('.rp-input').dispatchEvent("
                    "new KeyboardEvent('keydown', {key: 'Enter', bubbles: true, cancelable: true}))")
                await browser.wait_for("window.paneDemo.sent.length > 2")
                sent = await self.sent(browser)
                self.assertIs(sent[2]["agent"], False)
                self.assertEqual(browser.console, [])
            finally:
                await browser.stop()

        self.drive(main())

    def test_a_finger_on_an_ask_option_or_a_queue_row_survives_the_states_under_it(self):
        """#PKT5 item 4: `renderAsk` cleared `askChoices` and `renderRows` cleared `rows` on every
        `draw()`, unguarded, while a `pane_state` arrives about ten times a second. A finger down
        on an option when one landed lifted onto a node that was no longer in the document, so no
        `click` fired and the tap did nothing."""
        state = fixture("busy_queue")
        wrapped = {"t": "agent", "pane": state["pane"], "event": self.QUESTION}

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.open(browser, "busy_queue", query="&input=touch")
                self.assertTrue(await browser.evaluate(
                    "window.paneDemo.agentEvent(%s)" % json.dumps(wrapped)))
                # The nodes a finger is already down on, marked so a rebuild can be seen.
                await browser.evaluate("""
                  (() => { window.paneDemo.held = {
                    choice: document.querySelector('.rp-ask-choice'),
                    row: document.querySelector('.rp-row') }; })()
                """)
                # Ten states, the way a running turn publishes them: the clock moves, nothing else.
                for n in range(10):
                    later = dict(state, seq=state["seq"] + 1 + n,
                                 turn=dict(state["turn"], clock=f"0:{n:02d}"))
                    self.assertTrue(await browser.evaluate(
                        f"window.paneDemo.update({json.dumps(later)})"))
                self.assertTrue(await browser.evaluate(
                    "window.paneDemo.held.choice === document.querySelector('.rp-ask-choice')"
                    " && window.paneDemo.held.choice.isConnected"),
                    "the ask options were rebuilt under the finger")
                self.assertTrue(await browser.evaluate(
                    "window.paneDemo.held.row === document.querySelector('.rp-row')"
                    " && window.paneDemo.held.row.isConnected"),
                    "the queue rows were rebuilt under the finger")
                # And the tap still does what it was going to do.
                await browser.evaluate("window.paneDemo.held.choice.click()")
                await browser.wait_for("window.paneDemo.sent.length > 0")
                sent = (await self.sent(browser))[-1]
                self.assertEqual([sent["t"], sent["text"]], ["compose", "This file only"])

                # A queue that really moved is still redrawn.
                moved = json.loads(json.dumps(state))
                moved["seq"] += 20
                moved["queue"]["rows"][0]["label"] = "✦ something else entirely"
                self.assertTrue(await browser.evaluate(
                    f"window.paneDemo.update({json.dumps(moved)})"))
                self.assertFalse(await browser.evaluate(
                    "window.paneDemo.held.row === document.querySelector('.rp-row')"))
                self.assertEqual(await browser.evaluate(
                    "document.querySelector('.rp-row .rp-row-text').textContent"),
                    " something else entirely")
                self.assertEqual(browser.console, [])
            finally:
                await browser.stop()

        self.drive(main())

    def test_the_queue_scrolls_to_a_row_when_the_selection_moves_and_not_otherwise(self):
        """#PKT5 item 4: `scrollIntoView` ran on every rebuild, which at ten states a second is
        the list scrolling itself out from under whoever is reading it."""
        state = fixture("busy_queue")

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.open(browser, "busy_queue")
                calls = """
                  (() => { window.paneDemo.scrolls = 0;
                    const proto = Element.prototype;
                    const real = proto.scrollIntoView;
                    proto.scrollIntoView = function (...args) {
                      if (this.classList.contains('rp-row')) window.paneDemo.scrolls += 1;
                      return real.apply(this, args); }; })()
                """
                await browser.evaluate(calls)
                # A selection: one scroll.
                await browser.evaluate("document.querySelector('.rp-rows').focus()")
                await browser.evaluate(
                    "document.querySelector('.rp-rows').dispatchEvent(new KeyboardEvent('keydown',"
                    " {key: 'ArrowUp', bubbles: true, cancelable: true}))")
                self.assertEqual(await browser.evaluate("window.paneDemo.scrolls"), 1)
                # Ten states that change the clock and nothing else: no more.
                for n in range(10):
                    later = dict(state, seq=state["seq"] + 1 + n,
                                 turn=dict(state["turn"], clock=f"0:{n:02d}"))
                    await browser.evaluate(f"window.paneDemo.update({json.dumps(later)})")
                self.assertEqual(await browser.evaluate("window.paneDemo.scrolls"), 1)
                self.assertEqual(browser.console, [])
            finally:
                await browser.stop()

        self.drive(main())

    def test_a_long_queue_line_opens_to_its_whole_text_and_stays_open(self):
        """Card #JDN4: ▾ on a queued row and on the running line shows the whole message — past
        the label's 400 characters, to its real last words — wrapped, in place. It neither selects
        the row nor opens its sheet, a state tick does not fold it, and ▴ folds it back."""
        state = fixture("busy_queue")
        whole = "✦ " + " ".join(f"word{n}" for n in range(330)) + "\nTHE REAL END"
        state["queue"]["rows"][2]["label"] = whole.replace("\n", " ")[:399] + "…"
        state["queue"]["rows"][2]["full"] = whole
        state["queue"]["running"] = {"label": "✦ first line second line",
                                     "full": "✦ first line\nsecond line"}
        row_id = state["queue"]["rows"][2]["id"]
        row = f"document.querySelector('.rp-row[data-row-id=\"{row_id}\"]')"

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.open(browser, "busy_queue", query="&input=touch")
                self.assertTrue(await browser.evaluate(f"window.paneDemo.update({json.dumps(state)})"))
                # Folded: the label as sent, one line, with the control offered.
                self.assertTrue((await browser.evaluate(f"{row}.querySelector('.rp-row-label').textContent"))
                                .endswith("…"))
                self.assertFalse(await browser.evaluate(f"{row}.querySelector('.rp-row-more').hidden"))
                self.assertEqual(await browser.evaluate(
                    f"getComputedStyle({row}.querySelector('.rp-row-label')).whiteSpace"), "pre")
                # A short row that fits has no control.
                self.assertTrue(await browser.evaluate(
                    "document.querySelector('.rp-row[data-row-id=\"entry:9\"] .rp-row-more').hidden"))
                selected = await browser.evaluate(
                    "[...document.querySelectorAll('.rp-row')].map(e => e.getAttribute('aria-selected')).join()")
                await browser.evaluate(f"{row}.querySelector('.rp-row-more').click()")
                label = f"{row}.querySelector('.rp-row-label')"
                self.assertTrue((await browser.evaluate(f"{label}.textContent")).endswith("\nTHE REAL END"))
                self.assertEqual(await browser.evaluate(f"getComputedStyle({label}).whiteSpace"), "pre-wrap")
                self.assertEqual(await browser.evaluate(f"{row}.querySelector('.rp-row-more').getAttribute('aria-expanded')"),
                                 "true")
                self.assertEqual(await browser.evaluate(
                    "[...document.querySelectorAll('.rp-row')].map(e => e.getAttribute('aria-selected')).join()"),
                    selected, "expanding a row selected it")
                self.assertFalse(await browser.evaluate("!document.querySelector('.rp-sheet').parentElement.hidden"),
                                 "expanding a row opened its action sheet")
                self.assertEqual(await self.sent(browser), [], "expanding a row sent something")
                # Ticks at the running turn's rate leave it open.
                for n in range(5):
                    later = dict(state, seq=state["seq"] + 1 + n, turn=dict(state["turn"], clock=f"0:{n:02d}"))
                    await browser.evaluate(f"window.paneDemo.update({json.dumps(later)})")
                self.assertTrue((await browser.evaluate(f"{label}.textContent")).endswith("THE REAL END"))
                # The running line opens the same way.
                self.assertFalse(await browser.evaluate("document.querySelector('.rp-queue-running .rp-row-more').hidden"))
                await browser.evaluate("document.querySelector('.rp-queue-running .rp-row-more').click()")
                self.assertEqual(await browser.evaluate("document.querySelector('.rp-running-label').textContent"),
                                 "✦ first line\nsecond line")
                # ▴ folds both back to the label.
                await browser.evaluate(f"{row}.querySelector('.rp-row-more').click()")
                await browser.evaluate("document.querySelector('.rp-queue-running .rp-row-more').click()")
                self.assertTrue((await browser.evaluate(f"{label}.textContent")).endswith("…"))
                self.assertEqual(await browser.evaluate("document.querySelector('.rp-running-label').textContent"),
                                 "✦ first line second line")
                # And tapping the row body still selects it.
                await browser.evaluate(f"{row}.click()")
                self.assertEqual(await browser.evaluate(f"{row}.getAttribute('aria-selected')"), "true")
                self.assertEqual(browser.console, [])
            finally:
                await browser.stop()

        self.drive(main())

    def test_question_closed_and_the_end_of_the_turn_take_the_ask_away(self):
        state = fixture("busy_queue")
        wrapped = {"t": "agent", "pane": state["pane"], "event": self.QUESTION}

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.open(browser, "busy_queue")
                await browser.evaluate("window.paneDemo.agentEvent(%s)" % json.dumps(wrapped))
                self.assertEqual(await browser.evaluate(f"{self.DRAWN}('.rp-ask')"), 1)
                # Somebody else's closing is not this ask's.
                self.assertFalse(await browser.evaluate("window.paneDemo.agentEvent(%s)" % json.dumps(
                    {"t": "agent", "pane": state["pane"], "event": {"event": "question_closed", "id": "q-9"}})))
                self.assertEqual(await browser.evaluate(f"{self.DRAWN}('.rp-ask')"), 1)
                self.assertTrue(await browser.evaluate("window.paneDemo.agentEvent(%s)" % json.dumps(
                    {"t": "agent", "pane": state["pane"], "event": {"event": "question_closed", "id": "q-1", "reason": "stopped"}})))
                self.assertEqual(await browser.evaluate(f"{self.DRAWN}('.rp-ask')"), 0)
                # Skip is the desk's `/skip`, and the turn ending closes what is left.
                await browser.evaluate("window.paneDemo.agentEvent(%s)" % json.dumps(wrapped))
                await browser.evaluate("document.querySelector('.rp-ask-skip').click()")
                await browser.wait_for("window.paneDemo.sent.length > 0")
                sent = await self.sent(browser)
                self.assertEqual(sent[0]["text"], "/skip")
                self.assertEqual(sent[0]["when"], "queue", "a turn is running: the answer waits in it")
                self.assertEqual(await browser.evaluate("document.querySelector('.rp-ask-step').textContent"), "2 of 2")
                self.assertTrue(await browser.evaluate("window.paneDemo.agentEvent(%s)" % json.dumps(
                    {"t": "agent", "pane": state["pane"], "event": {"event": "agent_finished"}})))
                self.assertEqual(await browser.evaluate(f"{self.DRAWN}('.rp-ask')"), 0)
                self.assertEqual(browser.console, [])
            finally:
                await browser.stop()

        self.drive(main())

    def test_a_view_only_device_reads_the_question_and_cannot_answer_it(self):
        state = fixture("view_only")
        wrapped = {"t": "agent", "pane": state["pane"], "event": self.QUESTION}

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.open(browser, "view_only")
                await browser.evaluate("window.paneDemo.agentEvent(%s)" % json.dumps(wrapped))
                self.assertEqual(await browser.evaluate(f"{self.DRAWN}('.rp-ask')"), 1)
                self.assertEqual(await browser.evaluate("document.querySelector('.rp-ask-text').textContent"),
                                 "Apply the rename to which files?")
                self.assertEqual(await browser.evaluate("document.querySelectorAll('.rp-ask button').length"), 0)
                self.assertEqual(await self.sent(browser), [])
            finally:
                await browser.stop()

        self.drive(main())

    def test_the_owners_decisions_are_rows_with_two_buttons_for_a_full_device(self):
        state = fixture("idle")            # `sessions` present: the owner's level
        self.assertIn("sessions", state)
        pane = state["pane"]
        asks = {"t": "owner_asks", "items": [
            {"kind": "knock", "id": "p-alice", "name": "alice", "platform": "Chrome",
             "role": "editor", "code": "48213", "pane": pane},
            {"kind": "prompt", "id": "pr-1", "pane": pane, "name": "alice", "text": "run the tests"},
            {"kind": "control", "id": "p-alice", "pane": pane, "name": "alice"},
            {"kind": "prompt", "id": "pr-2", "pane": "another-pane", "name": "bob", "text": "elsewhere"},
        ]}

        async def main():
            browser = Browser()
            await browser.start()
            try:
                await self.open(browser, "idle")
                self.assertEqual(await browser.evaluate(f"{self.DRAWN}('.rp-asks')"), 0)
                self.assertTrue(await browser.evaluate("window.paneDemo.ownerAsks(%s)" % json.dumps(asks)))
                rows = json.loads(await browser.evaluate(
                    "JSON.stringify([...document.querySelectorAll('.rp-ask-row')]"
                    ".map(e => [e.dataset.kind, e.querySelector('.rp-ask-row-text').textContent,"
                    " [...e.querySelectorAll('button')].map(b => b.textContent)]))"))
                # A knock is about the desktop and shows on any pane; a prompt or a control request
                # for another pane belongs to that pane. The knock row carries the five digits.
                self.assertEqual(rows, [
                    ["knock", "alice wants to join as editor · code 48213", ["Admit", "Refuse"]],
                    ["prompt", "alice: run the tests", ["Run", "Refuse"]],
                    ["control", "alice asks to type", ["Allow", "Deny"]],
                ])
                await browser.evaluate("document.querySelector('.rp-ask-row[data-kind=\"knock\"] .rp-ask-yes').click()")
                await browser.evaluate("document.querySelector('.rp-ask-row[data-kind=\"prompt\"] .rp-ask-no').click()")
                await browser.evaluate("document.querySelector('.rp-ask-row[data-kind=\"control\"] .rp-ask-yes').click()")
                await browser.wait_for("window.paneDemo.sent.length > 2")
                self.assertEqual(await self.sent(browser), [
                    {"t": "knock_answer", "participant": "p-alice", "admit": True, "role": "editor"},
                    {"t": "prompt_answer", "id": "pr-1", "approve": False},
                    {"t": "control_answer", "pane": pane, "participant": "p-alice", "grant": True},
                ])
                # Each row went as it was answered; the hub's next list is what brings one back.
                self.assertEqual(await browser.evaluate(f"{self.DRAWN}('.rp-asks')"), 0)
                await browser.evaluate("window.paneDemo.ownerAsks(%s)" % json.dumps(
                    {"t": "owner_asks", "items": asks["items"][:1]}))
                self.assertEqual(await browser.evaluate("document.querySelectorAll('.rp-ask-row').length"), 1)
                await browser.evaluate("window.paneDemo.ownerAsks(%s)" % json.dumps({"t": "owner_asks", "items": []}))
                self.assertEqual(await browser.evaluate(f"{self.DRAWN}('.rp-asks')"), 0)
                self.assertEqual(browser.console, [])
            finally:
                await browser.stop()

        self.drive(main())


@unittest.skipUnless(shutil.which("node"), "node is not installed")
class OutboxTests(unittest.TestCase):
    """The transport the view is handed (``app/outbox.js``, protocol section 7).

    The view knows nothing about the link being down: it calls ``send`` and that is the end of its
    involvement. So the rules about what happens next — what may wait for the link, what may
    never, and that a replay is one send and not two — belong here rather than in the view, and
    they are worth checking without a browser because each of them is a single wrong ``if`` away
    from being silently untrue. ``tests/test_remote_browser.py`` drives the same code over a real
    drop; this is the part that is cheap to run.
    """

    @classmethod
    def setUpClass(cls):
        done = subprocess.run([shutil.which("node"), str(ROOT / "tests" / "outbox_peer.mjs")],
                              capture_output=True, text=True, cwd=str(ROOT))
        assert done.returncode == 0, done.stderr
        cls.out = json.loads(done.stdout)

    def test_a_prompt_sent_while_the_link_is_down_is_kept_and_then_sent_unchanged(self):
        case = self.out["queued_then_sent"]
        self.assertEqual(case["first"], "queued")
        self.assertEqual(case["line"], "Sends when back online · 1 message")
        self.assertEqual(case["flushed"], 1)
        self.assertEqual(case["sent"],
                         [{"t": "compose", "pane": "p1", "text": "hello", "msg_id": "m1"}])
        self.assertEqual(case["pendingAfter"], 0)

    def test_two_flushes_in_the_same_tick_send_it_once(self):
        # A phone coming out of a pocket fires `pageshow` and `online` together, and the
        # reconnect's own flush lands on top of them.
        case = self.out["a_double_flush_sends_once"]
        self.assertEqual(case["sent"], ["m1", "m2"])
        self.assertEqual(case["pendingAfter"], 0)

    def test_re_sending_the_same_message_while_offline_is_not_two_prompts(self):
        self.assertEqual(self.out["a_repeat_is_not_two"]["pending"], 1)

    def test_a_password_or_a_keystroke_is_never_queued(self):
        # Section 6.6 and 6.7: a password answered into a prompt that has since ended, or a
        # control byte replayed into whatever is in the foreground twenty minutes later, are both
        # worse than a failure the person can see.
        case = self.out["never_queued"]
        self.assertEqual(case["queueable"], ["agent_stop", "compose"])
        self.assertEqual(case["pending"], 0)
        for kind in ("secret_input", "keys", "line", "pane_focus"):
            self.assertEqual(case["refused"][kind], "not connected.")

    def test_anything_that_waits_carries_an_id_so_it_lands_at_most_once(self):
        case = self.out["an_id_is_minted"]
        self.assertEqual(case["msg_id"], "string")
        self.assertGreaterEqual(case["length"], 12)

    def test_a_flush_that_cannot_get_the_first_one_out_keeps_the_order(self):
        case = self.out["order_is_kept"]
        self.assertEqual(case["flushed"], 0)
        self.assertEqual(case["later"], 2)
        self.assertEqual(case["sent"], ["one", "two"])

    def test_the_line_under_the_prompt_box_counts_what_is_waiting(self):
        self.assertEqual(self.out["lines"], {
            "none": "",
            "one": "Sends when back online · 1 message",
            "many": "Sends when back online · 3 messages",
            "mixed": "Sends when back online · 2 messages, 1 Stop",
        })


if __name__ == "__main__":
    unittest.main()
