# SPDX-License-Identifier: GPL-3.0-or-later
"""The pane view (app/pane.js) in a real browser, against the fixtures the desktop would send.

The view draws `pane_state` (docs/REMOTE-PROTOCOL.md section 16) and sends back actions. What is
worth asserting is exactly the contract between the two, because everything else about the pane
is the desktop's business:

* every label it shows is the desktop's, character for character — the view writes none of them;
* a row offers the actions the desktop listed for it, and no others;
* a click or a key sends the protocol's message, naming the row by the desktop's own id;
* a state that has been overtaken (a lower `seq`) is ignored rather than drawn;
* a `view` device, which the hub sends no `actions` and no `model.choices`, gets no controls.

`app/pane-demo.html` is the harness: it mounts the view on a fixture and records what it sends.
Skipped when Chrome is not installed.
"""
import asyncio
import json
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
        await browser.wait_for("document.body.dataset.demoReady === '1'"
                               " && !!document.querySelector('.relay-pane')")
        await browser.wait_for(f"document.body.dataset.demoSeq === '{fixture(name)['seq']}'")

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
                # Nothing it can press. The composer is not drawn at all (the hub sends a `view`
                # device no `composer`), the model chip offers its label and no choice, and there
                # is no "new conversation". Drawn, not merely present: a hidden ancestor counts.
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


if __name__ == "__main__":
    unittest.main()
