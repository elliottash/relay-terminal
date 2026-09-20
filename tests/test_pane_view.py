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
