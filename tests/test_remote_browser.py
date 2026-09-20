# SPDX-License-Identifier: AGPL-3.0-or-later
"""The real web client, in a real browser, against the real rendezvous and host.

This is the check that the thing a phone runs actually works: WebCrypto's X25519 and AES-GCM, a
non-extractable key in IndexedDB, the Noise handshake against remote/noise.py, and the pairing,
inbox and thread screens. ``http://127.0.0.1`` is a secure context, so WebCrypto is available
without a certificate.

Skipped when Chrome is not installed.
"""
import asyncio
import tempfile
import time
import unittest
from pathlib import Path

from remote import host as host_mod
from remote import identity as identity_mod
from remote import panes as panes_mod
from remote import wire
from rendezvous.server import Store, build
from tests.browser import SCREENS_SHOWN, Browser, find_chrome, shown

APP_DIR = Path(__file__).resolve().parent.parent / "app"


class ScrollbackSource(panes_mod.DemoPaneSource):
    """The demo desktop with a screen and a scrollback behind it.

    One numbered stream: absolute row K reads `row-K`, the scrollback is rows [0, total) and the
    live block is the `rows` rows starting at `total`. That is what lets a test assert the thing
    that matters — that the column the phone shows is every number once, with no hole where the
    buffered history meets the live block. A real shell would do as well, and
    `tests/test_remote_terminal.py` drives one, but it will not hold still while output arrives.
    """

    scrollback = True

    def __init__(self, rows=14, cols=60, history=600):
        super().__init__()
        self.rows, self.cols, self.total = rows, cols, history
        self._screen_callbacks: list = []
        self.typed: list[tuple] = []

    # -- the seams the hub looks for when a source has a screen ---------------------------------

    def on_screen(self, callback) -> None:
        self._screen_callbacks.append(callback)

    def release(self, pane: str, device: str) -> None:
        return

    def release_device(self, device: str) -> None:
        return

    # -- taking over. `control_request` writes an empty `keys` to claim the pane, so a source with
    # a screen needs these three even when the test only cares about who is driving.

    async def send_keys(self, pane: str, data: bytes, *, device: str) -> None:
        self.typed.append(("keys", pane, data, device))

    async def send_line(self, pane: str, text: str, *, device: str) -> None:
        self.typed.append(("line", pane, text, device))

    async def paste(self, pane: str, text: str, *, device: str) -> None:
        self.typed.append(("paste", pane, text, device))

    @classmethod
    def _row(cls, screen_row: int, absolute: int) -> dict:
        # Every fifth row bold and red, by absolute row, so history and the live block are styled
        # by the same rule and the browser can be asked whether history is painted like a screen.
        red, bold = ((2 << 24) | 0xFF5F5F, 1) if absolute % 5 == 0 else (0, 0)
        return {"row": screen_row, "segs": [[f"row-{absolute}", red, 0, bold]]}

    def _cursor(self) -> dict:
        return {"row": self.rows - 1, "col": 0, "visible": True, "shape": 0}

    def _live(self) -> list[dict]:
        return [self._row(n, self.total + n) for n in range(self.rows)]

    def screen_snapshot(self, pane: str) -> dict:
        return {"t": "screen_snapshot", "pane": pane, "rows": self.rows, "cols": self.cols,
                "alt": False, "cursor": self._cursor(), "base": self.total,
                "history": self.total, "lines": self._live()}

    def redraw(self, pane: str) -> None:
        """A full frame with the geometry unchanged: the desktop redrew, nothing else."""
        for callback in list(self._screen_callbacks):
            callback(pane, self.screen_snapshot(pane))

    def advance(self, pane: str, count: int) -> int:
        """`count` more lines of output: the screen scrolls and scrollback grows by that much."""
        self.total += count
        message = {"t": "screen_diff", "pane": pane, "cursor": self._cursor(),
                   "base": self.total, "history": self.total, "lines": self._live()}
        for callback in list(self._screen_callbacks):
            callback(pane, message)
        return self.total

    def scroll(self, pane: str, count: int) -> int:
        """The same `count` lines of output, described the way a real desktop describes it.

        Rows `[0, rows)` moved up by `count` and only the rows that entered at the bottom go on
        the wire — `advance` above is the old shape, every row every time (#3H5T, section 6.5).
        """
        self.total += count
        message = {"t": "screen_diff", "pane": pane, "cursor": self._cursor(),
                   "base": self.total, "history": self.total,
                   "scroll": {"top": 0, "bottom": self.rows, "by": count},
                   "lines": [self._row(n, self.total + n)
                             for n in range(self.rows - count, self.rows)]}
        for callback in list(self._screen_callbacks):
            callback(pane, message)
        return self.total

    async def history(self, pane: str, before_row: int, count: int) -> dict:
        end = self.total if before_row < 0 else max(0, min(before_row, self.total))
        want = min(count, end)
        start = end - want
        return {"from_row": start, "total": self.total, "more": start > 0,
                "lines": [self._row(row, row) for row in range(start, end)]}


class SixPanes(panes_mod.DemoPaneSource):
    """Six panes, one in each status of section 6.3, in an order that is not the inbox's.

    Two of them are guest-agent panes — a Claude Code pane and a Codex pane. They are ordinary
    panes to every part of this: the desktop publishes them like any other terminal with a screen,
    and nothing in the phone's inbox asks whether a pane has one of Relay's own agent workers
    behind it. That is the point of including them: the test fails if anything starts to.
    """

    def __init__(self):
        super().__init__()
        now = time.time()
        self._panes = [
            {"id": "p-idle", "window": 1, "tab": "notes", "title": "notes", "cwd": "~/notes",
             "program": "", "control": "human", "status": "idle", "unread": 0, "queue": 0,
             "updated": now - 900},
            # A guest agent, three minutes into a turn: "running · 3m" comes from `updated`.
            {"id": "p-claude", "window": 1, "tab": "relay", "title": "claude code",
             "cwd": "~/repos/relay-terminal", "program": "claude", "control": "agent",
             "status": "running", "unread": 0, "queue": 2, "updated": now - 185},
            {"id": "p-failed", "window": 1, "tab": "build", "title": "build", "cwd": "~/build",
             "program": "ninja", "control": "human", "status": "failed", "unread": 0, "queue": 0,
             "updated": now - 30},
            {"id": "p-codex", "window": 1, "tab": "codex", "title": "codex", "cwd": "~/repos/x",
             "program": "codex", "control": "agent", "status": "finished", "unread": 0,
             "queue": 0, "updated": now - 60},
            {"id": "p-waiting", "window": 1, "tab": "deploy", "title": "deploy", "cwd": "~/ops",
             "program": "ansible", "control": "human", "status": "waiting_input", "unread": 0,
             "queue": 0, "updated": now - 10},
            {"id": "p-password", "window": 1, "tab": "ssh", "title": "ssh prod-db", "cwd": "~",
             "program": "ssh", "control": "human", "status": "password", "unread": 0, "queue": 0,
             "updated": now - 5},
        ]

    def republish(self) -> None:
        """Send the list again unchanged, so a test can assert on a render it set up for."""
        self._changed()


class RecordingSource(panes_mod.DemoPaneSource):
    """The demo desktop with the scripted turn taken out: what was composed, and nothing else.

    The queue-and-replay test is about how many times a prompt reaches the desktop, so a source
    that answers with six seconds of scripted agent output is noise it would have to wait for.
    """

    def __init__(self):
        super().__init__()
        self.composed: list[tuple] = []
        self.stops: list[str] = []

    async def compose(self, pane, text, *, to_agent, when, origin, origin_name="") -> None:
        self.composed.append((pane, text, when, origin))
        # The turn starts, and says so, because that is what puts Stop on the phone's screen.
        self._emit(pane, {"event": "agent_started"})
        self.set_status(pane, "running")

    async def agent_stop(self, pane) -> None:
        self.stops.append(pane)


class Harness:
    def __init__(self, capability=wire.AGENT, approve=True, source=None):
        self.capability, self.approve = capability, approve
        self.source_factory = source or panes_mod.DemoPaneSource
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
        self.source = self.source_factory()

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
                        "document.querySelector('[data-pane-id=\"pane-1\"]').click()")
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

    def test_a_voice_clip_is_recorded_here_and_transcribed_on_the_desktop(self):
        # Issue W5N2: the phone records, the desktop transcribes with the key it already has, and
        # the words come back into the prompt box for the person to read before sending. Chrome's
        # fake capture device stands in for a microphone; everything else is the real path.
        async def main():
            async with Harness() as harness:
                url, _ = await harness.host.open_pairing()
                browser = Browser(microphone=True)
                await browser.start()
                try:
                    await browser.navigate(url)
                    await browser.wait_for(shown('screen-inbox'), timeout=60)
                    await browser.wait_for("document.querySelectorAll('.pane-row').length > 0")
                    await browser.evaluate("""[...document.querySelectorAll('.pane-row')].find(
                        row => row.querySelector('.pane-title')?.textContent === 'relay-terminal').click()""")
                    await browser.wait_for(shown('screen-thread'))

                    # An `agent` device that can record sees the button; a `view` device would not
                    # get a composer at all.
                    await browser.wait_for(shown('composer-mic'), timeout=20)

                    # Something already typed must survive the transcript.
                    await browser.evaluate(
                        "(() => { document.getElementById('composer-text').value = 'note:'; "
                        "return true; })()")

                    await browser.evaluate("document.getElementById('composer-mic').click()")
                    await browser.wait_for(
                        "document.getElementById('composer-mic').classList.contains('recording')",
                        timeout=20)
                    await asyncio.sleep(1.0)            # let the fake device produce a clip
                    await browser.evaluate("document.getElementById('composer-mic').click()")

                    # The clip crossed the session, DemoPaneSource transcribed it, and the words
                    # were appended to what was already in the box rather than replacing it.
                    text = await browser.wait_for(
                        "document.getElementById('composer-text').value.includes('demo transcript')"
                        " ? document.getElementById('composer-text').value : ''", timeout=40)
                    self.assertTrue(text.startswith("note: "), text)
                    self.assertIn("of webm]", text)     # the container Chrome recorded

                    # And the button is idle again, ready for the next clip.
                    await browser.wait_for(
                        "!document.getElementById('composer-mic').classList.contains('busy')"
                        " && !document.getElementById('composer-mic').disabled", timeout=20)
                    problems = [line for line in browser.console
                                if "EXCEPTION" in line or "error:" in line.lower()]
                    self.assertEqual(problems, [], f"console errors: {problems}")
                finally:
                    await browser.stop()
        asyncio.run(asyncio.wait_for(main(), 180))

    def test_scrollback_pages_in_when_you_drag_the_terminal_down(self):
        # Issue W5N2: a phone could watch a shared pane but not read what had scrolled away.
        # Paired as `view`, because reading back is watching: a device that may not type still
        # gets the scrollback.
        async def main():
            async with Harness(capability=wire.VIEW, source=ScrollbackSource) as harness:
                url, _ = await harness.host.open_pairing()
                browser = Browser()
                await browser.start()
                # Every row on screen, history then live, in the order they are painted.
                column = ("(() => [...document.querySelectorAll("
                          "'.screen-history .screen-row, .screen-grid > .screen-row')]"
                          ".map(n => n.textContent.trim()))()")
                numbers = ("(() => [...document.querySelectorAll("
                           "'.screen-history .screen-row, .screen-grid > .screen-row')]"
                           ".map(n => parseInt(n.textContent.trim().split('-')[1], 10)))()")
                rows = "[...document.querySelectorAll('.screen-history .screen-row')]"

                def contiguous(values):
                    return values == list(range(values[0], values[0] + len(values)))

                try:
                    await browser.navigate(url)
                    await browser.wait_for(shown('screen-inbox'), timeout=40)
                    await browser.evaluate("document.querySelector('[data-pane-id=\"pane-1\"]').click()")
                    await browser.wait_for(shown('screen-thread'))
                    await browser.wait_for(shown('terminal-pane'), timeout=20)
                    await browser.wait_for(
                        "[...document.querySelectorAll('#screen-wrap .screen-row')]"
                        ".some(n => n.textContent.includes('row-'))", timeout=20)

                    # A page is fetched before the finger is there, so a drag upward lands in it,
                    # and it joins the live block: the whole column is one run of numbers.
                    await browser.wait_for(f"{rows}.length >= 80", timeout=20)
                    self.assertTrue(contiguous(await browser.evaluate(numbers)),
                                    await browser.evaluate(column))

                    # Drag it down, repeatedly. The poll nudges the scroll back to the top the way
                    # a finger would keep dragging.
                    await browser.wait_for(
                        f"(() => {{ const w = document.getElementById('screen-wrap');"
                        f" if ({rows}.length >= 300) return true;"
                        " w.scrollTop = 0; return false; })()", timeout=30)
                    deeper = await browser.evaluate(numbers)
                    self.assertTrue(contiguous(deeper),
                                    f"the pages must join with no gap and no repeat: {deeper}")

                    # Painted by the run painter the live screen uses, not as plain text: the
                    # bold red rows are bold and red, the plain ones are not.
                    style = await browser.evaluate(
                        f"(() => {{ const of = (text) => {{ const row = {rows}"
                        ".find(n => n.textContent.trim() === text);"
                        " const css = getComputedStyle(row.firstElementChild);"
                        " return [css.fontWeight, css.color]; };"
                        " return { bold: of('row-400'), plain: of('row-401') }; })()")
                    self.assertEqual(style["bold"][0], "700")
                    self.assertEqual(style["bold"][1], "rgb(255, 95, 95)")
                    self.assertNotEqual(style["plain"][0], "700")

                    # Output arrives while somebody is reading up here. Nothing may move, and the
                    # rows that left the live screen must land between the buffer and the live
                    # block rather than falling down the hole between them.
                    await browser.evaluate(
                        "(() => { const w = document.getElementById('screen-wrap');"
                        " w.scrollTop = w.scrollHeight - w.clientHeight - 400; })()")
                    before = await browser.wait_for(
                        "(() => { const w = document.getElementById('screen-wrap');"
                        " const was = window.__settled; window.__settled = w.scrollTop;"
                        " return was === w.scrollTop ? w.scrollTop : null; })()", timeout=20)
                    base = harness.source.advance("pane-1", 6)
                    newest = base + harness.source.rows - 1
                    await browser.wait_for(
                        f"{column}.includes('row-{newest}')", timeout=20)
                    after = await browser.evaluate(
                        "document.getElementById('screen-wrap').scrollTop")
                    self.assertEqual(before, after, "new output dragged the reader to the bottom")
                    self.assertTrue(await browser.evaluate(shown('term-new-output')),
                                    "there was no way back to the live screen")
                    seam = await browser.wait_for(
                        f"(() => {{ const n = {numbers};"
                        " return n.every((v, i) => i === 0 || v === n[i - 1] + 1) ? n : null;"
                        " })()", timeout=20)
                    self.assertIn(base - 1, seam, "the rows that left the screen were dropped")
                    self.assertEqual(seam[-1], newest)
                    self.assertEqual(len(seam), len(set(seam)), "a row was painted twice")

                    # A big burst, too large to close eagerly, fills in as the reader comes down.
                    base = harness.source.advance("pane-1", 400)
                    newest = base + harness.source.rows - 1
                    await browser.wait_for(f"{column}.includes('row-{newest}')", timeout=20)
                    await browser.wait_for(
                        f"(() => {{ const w = document.getElementById('screen-wrap');"
                        f" const n = {numbers};"
                        " if (n.every((v, i) => i === 0 || v === n[i - 1] + 1)"
                        f"     && n[n.length - 1] === {newest}) return true;"
                        " w.scrollTop = w.scrollHeight; return false; })()", timeout=40)

                    # A full repaint is not a reason to throw the reader's scrollback away.
                    # Live, a redraw on the desktop dropped the phone back to the live screen.
                    await browser.evaluate(
                        "(() => { const w = document.getElementById('screen-wrap');"
                        " w.scrollTop = w.scrollHeight - w.clientHeight - 400; })()")
                    settled = await browser.wait_for(
                        "(() => { const w = document.getElementById('screen-wrap');"
                        " const was = window.__again; window.__again = w.scrollTop;"
                        " return was === w.scrollTop ? w.scrollTop : null; })()", timeout=20)
                    held = await browser.evaluate(f"{rows}.length")
                    harness.source.redraw("pane-1")
                    await asyncio.sleep(1)
                    self.assertEqual(await browser.evaluate(f"{rows}.length"), held,
                                     "a full frame discarded the scrollback")
                    self.assertEqual(
                        await browser.evaluate(
                            "document.getElementById('screen-wrap').scrollTop"),
                        settled, "a full frame moved the reader")

                    # And the way back works: tapping it returns to the newest output.
                    await browser.evaluate(
                        "document.getElementById('term-new-output').click()")
                    await browser.wait_for(
                        "(() => { const w = document.getElementById('screen-wrap');"
                        " return w.scrollHeight - w.scrollTop - w.clientHeight <= 4; })()",
                        timeout=20)
                    self.assertFalse(await browser.evaluate(shown('term-new-output')))

                    # No horizontal scrollbar: the grid is sized for the width it actually has.
                    self.assertEqual(await browser.evaluate(
                        "(() => { const w = document.getElementById('screen-wrap');"
                        " return w.scrollWidth - w.clientWidth; })()"), 0,
                        "the terminal grid is wider than its container")

                    problems = [line for line in browser.console
                                if "EXCEPTION" in line or "error:" in line.lower()]
                    self.assertEqual(problems, [], f"console errors: {problems}")
                finally:
                    await browser.stop()
        asyncio.run(asyncio.wait_for(main(), 180))

    def test_a_scrolled_screen_is_painted_exactly_as_a_snapshot_would_paint_it(self):
        """#3H5T: a scroll reaches the phone as a shift, and the shift has to be free of charge.

        The one thing that could go wrong is the arithmetic: a delta that disagrees with the
        desktop leaves the phone showing rows that are not there. So the same output is sent both
        ways — as the shift, then as the whole screen — and the painted rows are compared. They
        must be identical, and the shift must not have cost a snapshot's worth of bytes.
        """
        async def main():
            async with Harness(capability=wire.VIEW, source=ScrollbackSource) as harness:
                url, _ = await harness.host.open_pairing()
                browser = Browser()
                await browser.start()
                live = ("(() => [...document.querySelectorAll('.screen-grid > .screen-row')]"
                        ".map(n => n.textContent.trim()))()")
                try:
                    await browser.navigate(url)
                    await browser.wait_for(shown('screen-inbox'), timeout=40)
                    await browser.evaluate(
                        "document.querySelector('[data-pane-id=\"pane-1\"]').click()")
                    await browser.wait_for(shown('screen-thread'))
                    await browser.wait_for(shown('terminal-pane'), timeout=20)
                    await browser.wait_for(
                        "[...document.querySelectorAll('#screen-wrap .screen-row')]"
                        ".some(n => n.textContent.includes('row-'))", timeout=20)

                    # Output, as shifts: a line at a time, then several at once, then a scroll
                    # that turns over all but one row.
                    for count in (1, 1, 1, 5, 13):
                        harness.source.scroll("pane-1", count)
                    await browser.wait_for(
                        f"{live}.includes('row-{harness.source.total + harness.source.rows - 1}')",
                        timeout=20)
                    shifted = await browser.evaluate(live)

                    # The same screen, sent whole. A snapshot rebuilds the grid, so the arrival is
                    # watched for as a new first row node rather than as a change of text — there
                    # must not be one.
                    await browser.evaluate(
                        "window.__firstRow = document.querySelector('.screen-grid > .screen-row')")
                    harness.source.redraw("pane-1")
                    await browser.wait_for(
                        "document.querySelector('.screen-grid > .screen-row') !== window.__firstRow",
                        timeout=20)
                    self.assertEqual(await browser.evaluate(live), shifted)
                    self.assertEqual(
                        shifted,
                        [f"row-{harness.source.total + n}" for n in range(harness.source.rows)])

                    # And the shift really was cheaper: the hub forwarded one row per line of
                    # output, not fourteen.
                    sent = [m for m in harness.host.streams["screen:pane-1"].ring
                            if m.get("scroll")]
                    self.assertEqual([len(m["lines"]) for m in sent], [1, 1, 1, 5, 13])

                    problems = [line for line in browser.console
                                if "EXCEPTION" in line or "error:" in line.lower()]
                    self.assertEqual(problems, [], f"console errors: {problems}")
                finally:
                    await browser.stop()
        asyncio.run(asyncio.wait_for(main(), 180))

    def test_the_phone_follows_the_control_token(self):
        """#W5N2, shots 13a and 13b: the owner types at the desktop while the phone holds the
        keyboard. One holder per pane (section 10.3), so the phone has to hear about it — flip to
        Watching, say who has it, and keep the half-typed line, which is what a guest's client
        already does. Before this it went on saying "You have the keyboard"."""
        async def main():
            async with Harness(capability=wire.FULL, source=ScrollbackSource) as harness:
                url, _ = await harness.host.open_pairing()
                browser = Browser()
                await browser.start()
                mode = "document.getElementById('term-mode').textContent"
                note = "document.getElementById('term-note').textContent"
                typed = "document.getElementById('composer-text').value"
                try:
                    await browser.navigate(url)
                    await browser.wait_for(shown('screen-inbox'), timeout=40)
                    await browser.evaluate("document.querySelector('[data-pane-id=\"pane-1\"]').click()")
                    await browser.wait_for(shown('screen-thread'))
                    await browser.wait_for(shown('terminal-pane'), timeout=20)
                    self.assertEqual(await browser.evaluate(mode), "Watching")

                    # Take over, and half-type a line into the box.
                    await browser.evaluate("document.getElementById('term-take').click()")
                    await browser.wait_for(f"{mode} === 'You have the keyboard'", timeout=20)
                    # The chip flips as soon as the request is away; the token is the hub's
                    # answer, so wait for the book rather than assuming the two are one step.
                    pane = ""
                    for _ in range(100):
                        await asyncio.sleep(0.05)
                        held = [name for name, who in harness.host.control.holders.items()
                                if who.who == harness.devices.live()[0].device_id]
                        if held:
                            pane = held[0]
                            break
                    self.assertTrue(pane, "taking over claims the one control token")
                    await browser.evaluate(
                        "(() => { document.getElementById('composer-text').value = 'rm -r bui';"
                        " return true; })()")

                    # The owner types in the pane at the desktop: `control_take` (10.5).
                    self.assertTrue(harness.host.take_control(pane))
                    await browser.wait_for(f"{mode} === 'Watching'", timeout=20)
                    self.assertEqual(await browser.evaluate(note),
                                     "Read only until you take over.")
                    self.assertEqual(await browser.evaluate(typed), "rm -r bui",
                                     "losing the keyboard must not lose what was typed")
                    # Take over is offered again; Hand back is not.
                    self.assertFalse(await browser.evaluate(
                        "document.getElementById('term-take').hidden"))
                    self.assertTrue(await browser.evaluate(
                        "document.getElementById('term-release').hidden"))
                    self.assertIn("still here", await browser.evaluate(
                        "document.getElementById('thread-note').textContent"))

                    # And one tap gets it back, because the owner's phone is the owner.
                    await browser.evaluate("document.getElementById('term-take').click()")
                    await browser.wait_for(f"{mode} === 'You have the keyboard'", timeout=20)

                    problems = [line for line in browser.console
                                if "EXCEPTION" in line or "error:" in line.lower()]
                    self.assertEqual(problems, [], f"console errors: {problems}")
                finally:
                    await browser.stop()
        asyncio.run(asyncio.wait_for(main(), 180))

    def test_the_phone_shows_who_else_is_on_the_pane(self):
        """Section 10.3 fans `participants` out to everyone on the pane, the owner's own devices
        included — so the phone can say who is here (#W5N2)."""
        async def main():
            async with Harness(capability=wire.FULL, source=ScrollbackSource) as harness:
                url, _ = await harness.host.open_pairing()
                browser = Browser()
                await browser.start()
                presence = "document.getElementById('term-presence').textContent"
                try:
                    await browser.navigate(url)
                    await browser.wait_for(shown('screen-inbox'), timeout=40)
                    await browser.evaluate("document.querySelector('[data-pane-id=\"pane-1\"]').click()")
                    await browser.wait_for(shown('screen-thread'))
                    await browser.wait_for(shown('terminal-pane'), timeout=20)
                    self.assertTrue(await browser.evaluate(
                        "document.getElementById('term-presence').hidden"),
                        "nobody else is here yet")

                    # Somebody is let in to the pane. The invite and the record are the real
                    # ones; only her socket is missing, which is what `online: false` means and
                    # what the row says.
                    invite, _ = await harness.host.invite_create(["pane-1"], wire.EDITOR)
                    guest = harness.host.guests.admit(invite, bytes(range(32)), "alice",
                                                      "Chrome", wire.EDITOR)
                    harness.host.send_participants("pane-1")
                    await browser.wait_for(f"{presence}.includes('alice')", timeout=20)
                    self.assertEqual(await browser.evaluate(presence), "alice is away")
                    self.assertFalse(await browser.evaluate(
                        "document.getElementById('term-presence').hidden"))

                    # And every handoff re-sends the list, so the line follows the one token.
                    self.assertTrue(harness.host.grant_control("pane-1", guest.participant_id))
                    await browser.wait_for(f"{presence} === 'alice is typing'", timeout=20)

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


@unittest.skipUnless(find_chrome(), "no Chrome or Chromium installed")
class InboxTests(unittest.TestCase):
    """The first screen of the day (#PH0N phase 2.5).

    The question the inbox answers is "what wants me?", not "what is there?". So: the rows that
    need the owner come first whatever else is going on, a running pane says how long it has been
    running, and the number of rows that want him is on the app icon before he has opened
    anything at all.
    """

    ROWS = ("[...document.querySelectorAll('.pane-row')].map(r => ["
            "r.dataset.paneId,"
            " r.querySelector('.chip').textContent,"
            " r.querySelector('.pane-title').textContent])")

    def test_needs_you_comes_first_and_every_row_says_what_it_is_doing(self):
        async def main():
            async with Harness(source=SixPanes) as harness:
                url, _ = await harness.host.open_pairing()
                browser = Browser()
                await browser.start()
                try:
                    await browser.navigate(url)
                    await browser.wait_for(shown('screen-inbox'), timeout=40)
                    rows = await browser.wait_for(
                        f"(() => {{ const r = {self.ROWS}; return r.length === 6 ? r : null; }})()",
                        timeout=20)

                    # Three bands: what needs you, what is working, the rest. Inside a band the
                    # desktop's own order stands — these are the panes on a screen the owner
                    # knows, and re-sorting them by "most recently changed" makes rows swap
                    # places under a thumb.
                    self.assertEqual([row[0] for row in rows],
                                     ["p-failed", "p-waiting", "p-password",
                                      "p-claude", "p-idle", "p-codex"])
                    self.assertEqual([row[1] for row in rows[:3]],
                                     ["failed", "waiting for you", "password"])
                    # The running pane says how long. `updated` put its turn three minutes back.
                    self.assertEqual(rows[3][1], "running · 3m")
                    self.assertEqual([row[1] for row in rows[4:]], ["idle", "finished"])
                    # A guest-agent pane is an ordinary pane: its own title, in its own band,
                    # with nothing the others do not have.
                    self.assertEqual([row[2] for row in rows],
                                     ["build", "deploy", "ssh prod-db", "claude code", "notes",
                                      "codex"])
                    self.assertIn("2 queued", await browser.evaluate(
                        "document.querySelector('[data-pane-id=\"p-claude\"] .pane-queue')"
                        ".textContent"))

                    problems = [line for line in browser.console
                                if "EXCEPTION" in line or "error:" in line.lower()]
                    self.assertEqual(problems, [], f"console errors: {problems}")
                finally:
                    await browser.stop()
        asyncio.run(asyncio.wait_for(main(), 180))

    def test_the_app_badge_counts_what_needs_you_and_clears_at_zero(self):
        async def main():
            async with Harness(source=SixPanes) as harness:
                url, _ = await harness.host.open_pairing()
                browser = Browser()
                await browser.start()
                try:
                    await browser.navigate(url)
                    await browser.wait_for(shown('screen-inbox'), timeout=40)
                    # The Badging API is an installed PWA's, and headless Chrome has none, so the
                    # two calls are recorded here instead. The guard around them is the product
                    # code's (`updateBadge`): a browser without them must not throw on every list.
                    await browser.evaluate("""
                        (() => {
                          window.__badge = null;
                          navigator.setAppBadge = (n) => { window.__badge = n; return Promise.resolve(); };
                          navigator.clearAppBadge = () => { window.__badge = 0; return Promise.resolve(); };
                          return true;
                        })()
                    """)
                    harness.source.republish()
                    self.assertEqual(await browser.wait_for(
                        "window.__badge === null ? null : window.__badge", timeout=20), 3)

                    # Answered, all three: the badge goes away rather than sticking at the last
                    # number, which is the failure people actually notice.
                    for pane in ("p-failed", "p-waiting", "p-password"):
                        harness.source.set_status(pane, "idle")
                    # `wait_for` polls until the expression is truthy, so the zero is reported as
                    # a word rather than as the number it is waiting to stop seeing.
                    self.assertEqual(await browser.wait_for(
                        "window.__badge === 0 ? 'cleared' : null", timeout=20), "cleared")
                finally:
                    await browser.stop()
        asyncio.run(asyncio.wait_for(main(), 180))


@unittest.skipUnless(find_chrome(), "no Chrome or Chromium installed")
class OfflineQueueTests(unittest.TestCase):
    """Queue and replay across a real drop (#PH0N phase 2.7, protocol section 7).

    iOS closes the socket within seconds of the app leaving the foreground, so a prompt typed on
    a bus is normally typed with no link. What used to happen is that `rrp.send` rejected, every
    caller swallowed it, and the prompt simply never happened — after the person had watched
    themselves send it. The drop here is a real one: the desktop closes the channel, the
    rendezvous closes the phone's socket, and the client reconnects on its own.
    """

    async def drop(self, harness) -> None:
        for channel in list(harness.host.channels.values()):
            await channel.close("test drop")

    def test_a_prompt_typed_while_the_link_is_down_is_kept_and_sent_once(self):
        async def main():
            async with Harness(source=RecordingSource) as harness:
                url, _ = await harness.host.open_pairing()
                browser = Browser()
                await browser.start()
                try:
                    await browser.navigate(url)
                    await browser.wait_for(shown('screen-inbox'), timeout=40)
                    await browser.evaluate("document.querySelector('[data-pane-id=\"pane-1\"]').click()")
                    await browser.wait_for(shown('screen-thread'), timeout=20)

                    await self.drop(harness)
                    await browser.wait_for(
                        "document.getElementById('link-status').textContent === 'offline'"
                        " ? 'offline' : null", timeout=20)

                    await browser.evaluate("""
                        (() => {
                          document.getElementById('composer-text').value = 'run the tests';
                          document.getElementById('composer-send').click();
                          return true;
                        })()
                    """)
                    # A line under the box, not a sheet: the person is mid-sentence and a modal
                    # over the prompt box takes the sentence with it.
                    note = await browser.wait_for("""
                        (() => {
                          const n = document.getElementById('outbox-note');
                          return n && !n.hidden ? n.textContent : null;
                        })()
                    """, timeout=20)
                    self.assertEqual(note, "Sends when back online · 1 message")
                    # And the box is empty: it was taken, not refused.
                    self.assertEqual(await browser.evaluate(
                        "document.getElementById('composer-text').value"), "")
                    self.assertEqual(harness.source.composed, [])

                    # The client reconnects by itself, resumes its streams, and only then sends
                    # what it kept.
                    await browser.wait_for(
                        "document.getElementById('link-status').textContent === 'connected'"
                        " ? 'connected' : null", timeout=30)
                    for _ in range(100):
                        await asyncio.sleep(0.1)
                        if harness.source.composed:
                            break
                    self.assertEqual(len(harness.source.composed), 1, harness.source.composed)
                    pane, text, when, origin = harness.source.composed[0]
                    self.assertEqual(text, "run the tests")
                    self.assertTrue(origin.startswith("remote:"), origin)

                    # The line goes away once it has gone out, and nothing arrives twice — a
                    # second drop and reconnect must not replay a queue that is already empty.
                    self.assertEqual(await browser.wait_for(
                        "document.getElementById('outbox-note').hidden ? 'gone' : null",
                        timeout=20), "gone")

                    # A Stop waits the same way. It is also still *there* to tap: hiding it when
                    # the socket dropped meant that seconds after the app left the foreground,
                    # the one button whose purpose is "stop it now" was the one that had gone.
                    await browser.wait_for(
                        "document.getElementById('composer-stop').hidden ? null : 'shown'",
                        timeout=20)
                    await self.drop(harness)
                    await browser.wait_for(
                        "document.getElementById('link-status').textContent.startsWith('offline')"
                        " ? 'offline' : null", timeout=20)
                    self.assertFalse(await browser.evaluate(
                        "document.getElementById('composer-stop').hidden"),
                        "Stop disappeared when the link did")
                    await browser.evaluate("document.getElementById('composer-stop').click()")
                    # And the header says so, because the inbox does not show the line.
                    self.assertEqual(await browser.wait_for(
                        "document.getElementById('link-status').textContent.includes('waiting')"
                        " ? document.getElementById('link-status').textContent : null",
                        timeout=20), "offline · 1 waiting")
                    await browser.wait_for(
                        "document.getElementById('link-status').textContent === 'connected'"
                        " ? 'connected' : null", timeout=30)
                    for _ in range(100):
                        await asyncio.sleep(0.1)
                        if harness.source.stops:
                            break
                    self.assertEqual(harness.source.stops, [pane])

                    await self.drop(harness)
                    await browser.wait_for(
                        "document.getElementById('link-status').textContent === 'connected'"
                        " ? 'connected' : null", timeout=30)
                    await asyncio.sleep(1.0)
                    self.assertEqual(len(harness.source.composed), 1,
                                     "the prompt was replayed a second time")

                    problems = [line for line in browser.console
                                if "EXCEPTION" in line or "error:" in line.lower()]
                    self.assertEqual(problems, [], f"console errors: {problems}")
                finally:
                    await browser.stop()
        asyncio.run(asyncio.wait_for(main(), 240))

    def test_two_prompts_queued_while_down_arrive_in_order(self):
        async def main():
            async with Harness(source=RecordingSource) as harness:
                url, _ = await harness.host.open_pairing()
                browser = Browser()
                await browser.start()
                try:
                    await browser.navigate(url)
                    await browser.wait_for(shown('screen-inbox'), timeout=40)
                    await browser.evaluate("document.querySelector('[data-pane-id=\"pane-1\"]').click()")
                    await browser.wait_for(shown('screen-thread'), timeout=20)

                    await self.drop(harness)
                    await browser.wait_for(
                        "document.getElementById('link-status').textContent === 'offline'"
                        " ? 'offline' : null", timeout=20)
                    for text in ("first", "second"):
                        await browser.evaluate(f"""
                            (() => {{
                              document.getElementById('composer-text').value = '{text}';
                              document.getElementById('composer-send').click();
                              return true;
                            }})()
                        """)
                    self.assertEqual(await browser.wait_for("""
                        (() => {
                          const n = document.getElementById('outbox-note');
                          return n && !n.hidden && n.textContent.includes('2 messages')
                            ? n.textContent : null;
                        })()
                    """, timeout=20), "Sends when back online · 2 messages")

                    await browser.wait_for(
                        "document.getElementById('link-status').textContent === 'connected'"
                        " ? 'connected' : null", timeout=30)
                    for _ in range(100):
                        await asyncio.sleep(0.1)
                        if len(harness.source.composed) == 2:
                            break
                    self.assertEqual([row[1] for row in harness.source.composed],
                                     ["first", "second"])
                finally:
                    await browser.stop()
        asyncio.run(asyncio.wait_for(main(), 240))


if __name__ == "__main__":
    unittest.main()
