# SPDX-License-Identifier: GPL-3.0-or-later
"""The guest's web client, in a real browser, against the real rendezvous and hub (section 10).

A guest is the one participant in this system who was never at the desktop: everything they can
do arrives over the wire and is judged there, so a stub of the hub would be testing the stub.
These drive the real `app/` in headless Chrome against `remote/host.py`, with the owner standing
in as the three approver seams the GUI fills — `knock_approver`, `control_approver` and
`prompt_approver` — which is exactly where the desktop's own UI plugs in.

Visibility is asserted with ``shown()``, never with the `hidden` property: an author `display`
rule beats the browser's own [hidden] rule, and a whole class of bug once hid behind that.

Skipped when Chrome is not installed.
"""
import asyncio
import contextlib
import inspect
import tempfile
import unittest
import urllib.request
from pathlib import Path

from remote import guests as guests_mod
from remote import host as host_mod
from remote import identity as identity_mod
from remote import wire
from rendezvous.server import Store, build
from tests.browser import SCREENS_SHOWN, Browser, find_chrome, shown
from tests.test_remote_browser import APP_DIR, ScrollbackSource


def run(coroutine, timeout=180):
    return asyncio.run(asyncio.wait_for(coroutine, timeout))


# The control handoff of 10.3 and the prompt approval of 10.4 reach the hub through two seams
# shaped like `knock_approver`. Everything a guest does with them is tested here against the real
# ones, so this module says plainly when a hub does not have them yet rather than erroring on a
# keyword argument: it skips, and starts running the moment they land.
_HOST_SEAMS = set(inspect.signature(host_mod.Host.__init__).parameters)
HANDOFF = {"control_approver", "prompt_approver"} <= _HOST_SEAMS
NO_HANDOFF = "this hub has no control_approver/prompt_approver seam yet (protocol 10.3, 10.4)"


class GuestSource(ScrollbackSource):
    """The demo desktop with a screen and scrollback, plus somewhere for typing to land.

    ``ScrollbackSource`` is the screen the paired-phone tests already use, so a guest is watching
    the same painter over the same wire; what is added here is only the three write seams, which
    record rather than run, so a test can ask whether the bytes actually left the browser.
    """

    def __init__(self, *arguments, **keywords):
        super().__init__(*arguments, **keywords)
        self.typed: list[bytes] = []
        self.lines: list[str] = []
        self.pasted: list[str] = []
        self.composed: list[dict] = []

    async def send_keys(self, pane: str, data: bytes, *, device=None) -> None:
        self.typed.append(data)

    async def send_line(self, pane: str, text: str, *, device=None) -> None:
        self.lines.append(text)

    async def paste(self, pane: str, text: str, *, device=None) -> None:
        self.pasted.append(text)

    async def compose(self, pane: str, text: str, *, to_agent: bool, when: str, origin: str,
                      origin_name: str = "") -> None:
        self.composed.append({"pane": pane, "text": text, "to_agent": to_agent,
                              "origin": origin, "name": origin_name})
        await super().compose(pane, text, to_agent=to_agent, when=when, origin=origin,
                              origin_name=origin_name)


class Harness:
    """A rendezvous serving the app, a desktop with two panes, and an owner who answers."""

    def __init__(self, *, admit=True, role=wire.VIEWER, grant=True, approve_prompt=True,
                 share=None):
        self.admit = admit
        self.role = role
        self.grant = grant
        self.approve_prompt = approve_prompt
        # A second desktop on the **same** rendezvous, and so the same app origin. That is how
        # two desktops actually look to one browser, and it is the only way to ask whether an
        # owner record and a guest record coexist: one origin, one IndexedDB.
        self.share = share
        self.knocks: list[host_mod.KnockRequest] = []
        self.control_asks: list[host_mod.ControlRequest] = []
        self.prompt_asks: list[host_mod.PromptRequest] = []

    async def __aenter__(self):
        self.temporary = tempfile.TemporaryDirectory()
        directory = Path(self.temporary.name)
        if self.share is not None:
            self.store, self.server, self.base = (self.share.store, self.share.server,
                                                  self.share.base)
        else:
            self.store = Store(":memory:")
            self.server = build(self.store, static_root=APP_DIR)
            await self.server.start("127.0.0.1", 0)
            self.base = f"http://127.0.0.1:{self.server.port}"

        self.identity = identity_mod.Identity.create(directory)
        self.devices = identity_mod.DeviceStore(directory)
        self.guests = guests_mod.GuestStore(directory, devices=self.devices)
        self.source = GuestSource()

        async def approver(request):
            return True, wire.AGENT

        async def knock_approver(request):
            self.knocks.append(request)
            return self.admit, self.role

        async def control_approver(request):
            self.control_asks.append(request)
            return self.grant

        async def prompt_approver(request):
            self.prompt_asks.append(request)
            return self.approve_prompt

        seams = {"control_approver": control_approver,
                 "prompt_approver": prompt_approver} if HANDOFF else {}
        self.host = host_mod.Host(self.identity, self.devices, self.source, app_base=self.base,
                                  approver=approver, knock_approver=knock_approver,
                                  guests=self.guests, name="Ada's desktop", **seams)
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
        with contextlib.suppress(asyncio.CancelledError):
            await self.serving
        if self.share is None:
            await self.server.close()
            self.store.close()
        self.temporary.cleanup()

    async def invite(self, panes=("pane-1",), role=None, **keywords):
        invite, url = await self.host.invite_create(list(panes), role or self.role, **keywords)
        return url

    def participant(self):
        live = self.guests.live()
        return live[0] if live else None


# What a guest must never be handed, whatever else is on screen. Each is an element that exists in
# the owner's half of the app; asserting it is not *drawn* is the check that catches a stylesheet
# mistake, and the guest client never wiring its handler is what makes it true.
OWNER_ONLY_ELEMENTS = ("secret-row", "secret-text", "composer", "composer-mic", "composer-text",
                       "notify", "notify-kinds", "forget", "pane-list", "term-take",
                       "term-keys", "screen-inbox", "screen-thread")


async def join_as_guest(browser, harness, url, name="Bo"):
    """The whole join, from opening the link to watching the pane."""
    await browser.navigate(url)
    await browser.wait_for(shown('screen-join'), timeout=40)
    await browser.evaluate(
        f"(() => {{ document.getElementById('join-name').value = {name!r}; return true; }})()")
    await browser.evaluate("document.getElementById('join-knock').click()")
    code = await browser.wait_for(
        "document.getElementById('knock-code')?.textContent?.match(/^\\d{5}$/)"
        " ? document.getElementById('knock-code').textContent : ''", timeout=40)
    await browser.wait_for(shown('screen-guest'), timeout=60)
    return code


async def phone(browser):
    """A phone-sized viewport, so the layout under test is the one a phone gets."""
    await browser.call("Emulation.setDeviceMetricsOverride", {
        "width": 390, "height": 844, "deviceScaleFactor": 2, "mobile": True})


def no_console_errors(test, browser):
    problems = [line for line in browser.console
                if "EXCEPTION" in line or "error:" in line.lower()]
    test.assertEqual(problems, [], f"console errors: {problems}")


class JoinRouteTests(unittest.TestCase):
    """/join is a client-side route on the app shell, served exactly as /pair is."""

    def test_the_join_route_serves_the_app_shell_under_the_same_csp(self):
        async def main():
            async with Harness() as harness:
                def fetch(path):
                    request = urllib.request.Request(harness.base + path)
                    with urllib.request.urlopen(request, timeout=10) as reply:
                        return reply.status, dict(reply.headers), reply.read().decode()

                status, headers, body = await asyncio.to_thread(fetch, "/join")
                self.assertEqual(status, 200)
                self.assertIn("<title>Relay</title>", body)
                self.assertIn('id="screen-join"', body)
                self.assertIn("text/html", headers["Content-Type"])
                # The app's own CSP, unchanged: no third-party script and no inline script, which
                # is the answer to "whoever serves the JavaScript can serve a key-stealing one".
                csp = headers["Content-Security-Policy"]
                self.assertIn("script-src 'self'", csp)
                self.assertIn("frame-ancestors 'none'", csp)
                self.assertEqual(headers["X-Content-Type-Options"], "nosniff")
                # Byte for byte the same shell /pair gets: one app, two client-side routes.
                _, _, paired = await asyncio.to_thread(fetch, "/pair")
                self.assertEqual(body, paired)

            # And the route does not become a way out of the static root.
            async with Harness() as harness:
                def escape():
                    try:
                        with urllib.request.urlopen(harness.base + "/../remote/host.py",
                                                    timeout=10) as reply:
                            return reply.status
                    except urllib.error.HTTPError as error:
                        return error.code
                self.assertIn(await asyncio.to_thread(escape), (400, 403, 404))
        run(main(), 60)


@unittest.skipUnless(find_chrome(), "no Chrome or Chromium installed")
class GuestJoinTests(unittest.TestCase):
    def test_a_viewer_joins_and_watches_the_pane_with_nothing_to_type_into(self):
        async def main():
            async with Harness(role=wire.VIEWER) as harness:
                url = await harness.invite()
                browser = Browser()
                await browser.start()
                try:
                    await phone(browser)
                    # The join screen says who this is before anything is sent: the key is all
                    # the link carries, because nothing in it has been authenticated by anybody.
                    await browser.navigate(url)
                    await browser.wait_for(shown('screen-join'), timeout=40)
                    self.assertEqual(await browser.evaluate(SCREENS_SHOWN), 1)
                    printed = await browser.wait_for(
                        "document.getElementById('join-fingerprint').textContent !== '…'"
                        " ? document.getElementById('join-fingerprint').textContent : ''")
                    self.assertEqual(printed, harness.identity.fingerprint)

                    # The secret is out of the address bar before the first keystroke.
                    self.assertEqual(await browser.evaluate("location.hash"), "")

                    # A name is required: knocking without one asks for it rather than knocking.
                    await browser.evaluate("document.getElementById('join-knock').click()")
                    await asyncio.sleep(0.3)
                    self.assertIn("name", await browser.evaluate(
                        "document.getElementById('join-note').textContent"))
                    self.assertEqual(harness.knocks, [])

                    await browser.evaluate(
                        "(() => { document.getElementById('join-name').value = 'Bo';"
                        " document.getElementById('join-knock').click(); return true; })()")

                    # Waiting: the five digits, and the same five the hub derived.
                    code = await browser.wait_for(
                        "document.getElementById('knock-code')?.textContent?.match(/^\\d{5}$/)"
                        " ? document.getElementById('knock-code').textContent : ''", timeout=40)
                    await browser.wait_for(shown('screen-guest'), timeout=60)
                    self.assertEqual(len(harness.knocks), 1)
                    self.assertEqual(code, harness.knocks[0].code)
                    self.assertEqual(harness.knocks[0].name, "Bo")

                    # Whose computer this is, and on what terms, in the bar.
                    self.assertEqual(await browser.evaluate(
                        "document.getElementById('guest-desktop').textContent"), "Ada's desktop")
                    self.assertIn("guest · viewer", await browser.evaluate(
                        "document.getElementById('guest-role').textContent"))
                    self.assertIn("ends in", await browser.evaluate(
                        "document.getElementById('guest-role').textContent"))

                    # The pane is live, painted by the screen the owner's phone uses.
                    await browser.wait_for(
                        "[...document.querySelectorAll('#guest-screen-wrap .screen-row')]"
                        ".some(n => n.textContent.includes('row-'))", timeout=30)

                    # A viewer has nothing to type into and nothing to type with — asserted by
                    # what is drawn, not by the `hidden` property.
                    for element in ("guest-composer", "guest-drive-bar", "guest-ask",
                                    "guest-keys", "guest-line-row", "guest-capture"):
                        self.assertFalse(await browser.evaluate(shown(element)), element)
                    self.assertEqual(await browser.evaluate(
                        "document.querySelectorAll('#guest-keys button').length"), 0,
                        "a viewer's session built the extra-keys row")
                    for element in OWNER_ONLY_ELEMENTS:
                        self.assertFalse(await browser.evaluate(shown(element)), element)
                    self.assertEqual(await browser.evaluate(SCREENS_SHOWN), 1)

                    # Scrollback is reading, and reading is a viewer's: dragging the terminal
                    # down pages in what scrolled away, through the same painter and the same
                    # `history_get` the owner's own phone uses.
                    rows = "[...document.querySelectorAll('.screen-history .screen-row')]"
                    older = await browser.wait_for(
                        f"(() => {{ const w = document.getElementById('guest-screen-wrap');"
                        f" const t = {rows}.map(n => n.textContent);"
                        " if (t.length >= 120) return t;"
                        " w.scrollTop = 0; return null; })()", timeout=30)
                    numbers = [int(text.split("-")[1]) for text in older]
                    self.assertEqual(numbers, list(range(numbers[0], numbers[0] + len(numbers))),
                                     "the guest's pages joined with a gap or a repeat")

                    # Presence: the owner and this guest, marked as themselves.
                    who = await browser.wait_for(
                        "(() => { const n = [...document.querySelectorAll('#guest-presence .who')]"
                        ".map(e => e.textContent); return n.length >= 2 ? n : null; })()",
                        timeout=30)
                    self.assertTrue(any("Ada's desktop" in text for text in who), who)
                    self.assertTrue(any("Bo (you)" in text for text in who), who)

                    # The record stored is a guest's: a participant and a role, no device id and
                    # no capability, and it is not under the paired-device key.
                    stored = await browser.evaluate(GUEST_RECORD)
                    self.assertEqual(stored["participant"], harness.participant().participant_id)
                    self.assertEqual(stored["role"], "viewer")
                    self.assertNotIn("deviceId", stored)
                    self.assertNotIn("capability", stored)
                    self.assertIsNone(await browser.evaluate(DEVICE_RECORD))
                    no_console_errors(self, browser)
                finally:
                    await browser.stop()
        run(main())

    def test_a_refused_knock_says_so_and_stores_nothing(self):
        async def main():
            async with Harness(admit=False) as harness:
                url = await harness.invite()
                browser = Browser()
                await browser.start()
                try:
                    await phone(browser)
                    await browser.navigate(url)
                    await browser.wait_for(shown('screen-join'), timeout=40)
                    await browser.evaluate(
                        "(() => { document.getElementById('join-name').value = 'Bo';"
                        " document.getElementById('join-knock').click(); return true; })()")
                    note = await browser.wait_for(
                        "document.getElementById('join-note').textContent || ''", timeout=60)
                    self.assertIn("did not let you in", note)
                    # A plain sentence and a way forward, not a stack trace.
                    self.assertNotIn("Error", note)
                    self.assertTrue(await browser.evaluate(shown('join-knock')))
                    self.assertIsNone(await browser.evaluate(GUEST_RECORD))
                    self.assertEqual(harness.guests.live(), [])
                    no_console_errors(self, browser)
                finally:
                    await browser.stop()
        run(main())

    def test_a_damaged_link_says_what_to_do_and_offers_no_knock(self):
        async def main():
            async with Harness() as harness:
                browser = Browser()
                await browser.start()
                try:
                    await phone(browser)
                    await browser.navigate(harness.base + "/join#v=1&d=short&i=xx&r=room")
                    await browser.wait_for(shown('screen-join'), timeout=40)
                    note = await browser.wait_for(
                        "document.getElementById('join-note').textContent || ''", timeout=20)
                    self.assertIn("invitation link is damaged", note)
                    self.assertFalse(await browser.evaluate(shown('join-knock')))
                    # And a link that arrived with its fragment stripped altogether.
                    await browser.navigate(harness.base + "/join")
                    await browser.wait_for(
                        "document.getElementById('join-note').textContent.includes('without its')",
                        timeout=20)
                    no_console_errors(self, browser)
                finally:
                    await browser.stop()
        run(main())


@unittest.skipUnless(find_chrome(), "no Chrome or Chromium installed")
@unittest.skipUnless(HANDOFF, NO_HANDOFF)
class GuestEditorTests(unittest.TestCase):
    def test_an_editor_asks_to_type_is_granted_types_and_the_owner_takes_it_back(self):
        async def main():
            async with Harness(role=wire.EDITOR) as harness:
                url = await harness.invite(role=wire.EDITOR)
                browser = Browser()
                await browser.start()
                try:
                    await phone(browser)
                    await join_as_guest(browser, harness, url)
                    self.assertIn("guest · editor", await browser.evaluate(
                        "document.getElementById('guest-role').textContent"))

                    # An editor is still watching until they ask. The keyboard is one per pane.
                    await browser.wait_for(shown('guest-ask'), timeout=20)
                    self.assertFalse(await browser.evaluate(shown('guest-keys')))
                    self.assertFalse(await browser.evaluate(shown('guest-line-row')))
                    # The prompt box is theirs from the start, and says what it is.
                    self.assertTrue(await browser.evaluate(shown('guest-composer')))
                    self.assertIn("Ask their agent", await browser.evaluate(
                        "document.getElementById('guest-prompt-text').placeholder"))
                    self.assertIn("they approve it first", await browser.evaluate(
                        "document.getElementById('guest-composer').textContent"))

                    await browser.evaluate("document.getElementById('guest-ask').click()")
                    await browser.wait_for(shown('guest-hand-back'), timeout=30)
                    self.assertEqual(len(harness.control_asks), 1)
                    self.assertEqual(harness.control_asks[0].name, "Bo")
                    self.assertEqual(await browser.evaluate(
                        "document.getElementById('guest-drive-mode').textContent"),
                        "You have the keyboard")
                    self.assertTrue(await browser.evaluate(shown('guest-keys')))
                    self.assertTrue(await browser.evaluate(shown('guest-line-row')))

                    # Typing a line, and one of the extra keys, reaches the pane.
                    await browser.evaluate(
                        "(() => { document.getElementById('guest-line').value = 'ls -l';"
                        " document.getElementById('guest-line-send').click(); return true; })()")
                    await browser.wait_for("true")
                    for _ in range(100):
                        if harness.source.lines:
                            break
                        await asyncio.sleep(0.05)
                    self.assertEqual(harness.source.lines, ["ls -l"])
                    self.assertEqual(await browser.evaluate(
                        "document.getElementById('guest-line').value"), "")
                    await browser.evaluate(
                        "[...document.querySelectorAll('#guest-keys button')]"
                        ".find(b => b.textContent === '^C').click()")
                    for _ in range(100):
                        if harness.source.typed:
                            break
                        await asyncio.sleep(0.05)
                    self.assertEqual(harness.source.typed, [b"\x03"])

                    # Half a command, typed but not sent, while the owner takes the keyboard back
                    # with their own keystroke. The change is immediate and the line survives.
                    await browser.evaluate(
                        "(() => { document.getElementById('guest-line').value = 'rm -r bui';"
                        " return true; })()")
                    self.assertTrue(harness.host.take_control("pane-1"))
                    await browser.wait_for(f"!({shown('guest-hand-back')})", timeout=30)
                    self.assertTrue(await browser.evaluate(shown('guest-ask')))
                    self.assertFalse(await browser.evaluate(shown('guest-keys')))
                    self.assertEqual(await browser.evaluate(
                        "document.getElementById('guest-line').value"), "rm -r bui",
                        "the half-typed line was thrown away when control went back")
                    self.assertIn("took the keyboard back", await browser.evaluate(
                        "document.getElementById('guest-note').textContent"))

                    # The box stays on screen holding it — hiding it would look like losing it —
                    # but the send really is off, not merely out of sight.
                    self.assertTrue(await browser.evaluate(shown('guest-line-send')))
                    self.assertTrue(await browser.evaluate(
                        "document.getElementById('guest-line-send').disabled"))
                    self.assertTrue(await browser.evaluate(
                        "document.getElementById('guest-line').disabled"))
                    before = list(harness.source.lines)
                    await browser.evaluate(
                        "document.getElementById('guest-line-send').click()")
                    await asyncio.sleep(0.5)
                    self.assertEqual(harness.source.lines, before)
                    self.assertEqual(await browser.evaluate(
                        "document.getElementById('guest-line').value"), "rm -r bui")
                    no_console_errors(self, browser)
                finally:
                    await browser.stop()
        run(main())

    def test_a_refused_request_to_type_says_so_and_offers_to_ask_again(self):
        async def main():
            async with Harness(role=wire.EDITOR, grant=False) as harness:
                url = await harness.invite(role=wire.EDITOR)
                browser = Browser()
                await browser.start()
                try:
                    await phone(browser)
                    await join_as_guest(browser, harness, url)
                    await browser.wait_for(shown('guest-ask'), timeout=20)
                    await browser.evaluate("document.getElementById('guest-ask').click()")
                    note = await browser.wait_for(
                        "document.getElementById('guest-note').textContent.includes('said no')"
                        " ? document.getElementById('guest-note').textContent : ''", timeout=30)
                    self.assertIn("ask again", note)
                    self.assertTrue(await browser.evaluate(shown('guest-ask')))
                    self.assertFalse(await browser.evaluate(shown('guest-keys')))
                    no_console_errors(self, browser)
                finally:
                    await browser.stop()
        run(main())


@unittest.skipUnless(find_chrome(), "no Chrome or Chromium installed")
@unittest.skipUnless(HANDOFF, NO_HANDOFF)
class GuestPromptTests(unittest.TestCase):
    def test_a_prompt_waits_for_the_owner_and_then_runs(self):
        async def main():
            async with Harness(role=wire.EDITOR, approve_prompt=True) as harness:
                url = await harness.invite(role=wire.EDITOR)
                browser = Browser()
                await browser.start()
                try:
                    await phone(browser)
                    await join_as_guest(browser, harness, url)
                    await browser.wait_for(shown('guest-composer'), timeout=20)
                    await browser.evaluate(
                        "(() => { document.getElementById('guest-prompt-text').value ="
                        " 'what is failing in the test suite?';"
                        " document.getElementById('guest-prompt-send').click(); return true; })()")
                    state = await browser.wait_for(
                        "document.querySelector('.guest-prompt.is-approved .guest-prompt-state')"
                        "?.textContent || ''", timeout=40)
                    self.assertIn("approved", state)
                    self.assertEqual(len(harness.prompt_asks), 1)
                    self.assertEqual(harness.prompt_asks[0].text,
                                     "what is failing in the test suite?")
                    self.assertEqual(harness.prompt_asks[0].name, "Bo")
                    # Approved, and it went to the **agent** with the guest's own origin.
                    sent = [item for item in harness.source.composed
                            if item["origin"].startswith("guest:")]
                    self.assertEqual(len(sent), 1, harness.source.composed)
                    self.assertTrue(sent[0]["to_agent"])
                    self.assertEqual(sent[0]["name"], "Bo")
                    # The box is empty again and the text is on screen as a row of its own.
                    self.assertEqual(await browser.evaluate(
                        "document.getElementById('guest-prompt-text').value"), "")
                    self.assertIn("test suite", await browser.evaluate(
                        "document.querySelector('.guest-prompt-text').textContent"))
                    no_console_errors(self, browser)
                finally:
                    await browser.stop()
        run(main())

    def test_a_declined_prompt_says_who_declined_it_and_never_runs(self):
        async def main():
            async with Harness(role=wire.EDITOR) as harness:
                # The owner takes a moment to say no, so the row is caught waiting on the way
                # there: pending → declined is the sequence, not a single jump.
                answer = asyncio.Event()

                async def declines(request):
                    harness.prompt_asks.append(request)
                    await answer.wait()
                    return False

                harness.host.prompt_approver = declines
                url = await harness.invite(role=wire.EDITOR)
                browser = Browser()
                await browser.start()
                try:
                    await phone(browser)
                    await join_as_guest(browser, harness, url)
                    await browser.wait_for(shown('guest-composer'), timeout=20)
                    await browser.evaluate(
                        "(() => { document.getElementById('guest-prompt-text').value = 'rm -rf /';"
                        " document.getElementById('guest-prompt-send').click(); return true; })()")
                    waiting = await browser.wait_for(
                        "(document.querySelector('.guest-prompt .guest-prompt-state')"
                        "?.textContent || '').includes('waiting')"
                        " ? document.querySelector('.guest-prompt-state').textContent : ''",
                        timeout=30)
                    self.assertIn("waiting for Ada's desktop", waiting)
                    answer.set()
                    state = await browser.wait_for(
                        "document.querySelector('.guest-prompt.is-declined .guest-prompt-state')"
                        "?.textContent || ''", timeout=40)
                    self.assertIn("declined", state)
                    self.assertEqual([item for item in harness.source.composed
                                      if item["origin"].startswith("guest:")], [])
                    no_console_errors(self, browser)
                finally:
                    answer.set()
                    await browser.stop()
        run(main())

    def test_three_waiting_prompts_are_as_many_as_the_box_will_take(self):
        async def main():
            # An approver that never answers, so all three stay pending (section 10.4).
            async with Harness(role=wire.EDITOR) as harness:
                waiting = asyncio.Event()

                async def never(request):
                    harness.prompt_asks.append(request)
                    await waiting.wait()
                    return False

                harness.host.prompt_approver = never
                url = await harness.invite(role=wire.EDITOR)
                browser = Browser()
                await browser.start()
                try:
                    await phone(browser)
                    await join_as_guest(browser, harness, url)
                    await browser.wait_for(shown('guest-composer'), timeout=20)
                    for index in range(3):
                        await browser.evaluate(
                            "(() => { document.getElementById('guest-prompt-text').value ="
                            f" 'question {index}';"
                            " document.getElementById('guest-prompt-send').click();"
                            " return true; })()")
                        await browser.wait_for(
                            "document.querySelectorAll('.guest-prompt').length"
                            f" === {index + 1}", timeout=30)
                    await browser.wait_for(
                        "document.querySelectorAll('.guest-prompt .guest-prompt-state')"
                        ".length === 3 && [...document.querySelectorAll('.guest-prompt-state')]"
                        ".every(n => n.textContent.includes('waiting'))", timeout=40)
                    # The fourth is not sent: the box says why rather than being refused later.
                    self.assertTrue(await browser.evaluate(
                        "document.getElementById('guest-prompt-send').disabled"))
                    self.assertIn("already waiting", await browser.evaluate(
                        "document.getElementById('guest-prompt-text').placeholder"))
                    self.assertIn("they approve it first", await browser.evaluate(
                        "document.getElementById('guest-composer').textContent"))
                    self.assertEqual(len(harness.prompt_asks), 3)
                    waiting.set()
                    no_console_errors(self, browser)
                finally:
                    waiting.set()
                    await browser.stop()
        run(main())


@unittest.skipUnless(find_chrome(), "no Chrome or Chromium installed")
@unittest.skipUnless(HANDOFF, NO_HANDOFF)
class GuestSessionTests(unittest.TestCase):
    def test_a_reload_reconnects_with_the_stored_record(self):
        async def main():
            async with Harness(role=wire.VIEWER) as harness:
                url = await harness.invite()
                browser = Browser()
                await browser.start()
                try:
                    await phone(browser)
                    await join_as_guest(browser, harness, url)
                    participant = harness.participant().participant_id

                    # Reload on /join with no fragment at all: the stored record is the session.
                    await browser.navigate(harness.base + "/join")
                    await browser.wait_for(shown('screen-guest'), timeout=60)
                    await browser.wait_for(
                        "document.getElementById('guest-status').textContent === 'connected'",
                        timeout=30)
                    await browser.wait_for(
                        "[...document.querySelectorAll('#guest-screen-wrap .screen-row')]"
                        ".some(n => n.textContent.includes('row-'))", timeout=30)
                    # The same person, not a second one: knocking happened once.
                    self.assertEqual(len(harness.knocks), 1)
                    self.assertEqual(harness.participant().participant_id, participant)
                    self.assertEqual(await browser.evaluate(SCREENS_SHOWN), 1)
                    no_console_errors(self, browser)
                finally:
                    await browser.stop()
        run(main())

    def test_a_role_change_reaches_the_guest_without_a_reload(self):
        async def main():
            async with Harness(role=wire.VIEWER) as harness:
                url = await harness.invite(role=wire.EDITOR)
                browser = Browser()
                await browser.start()
                try:
                    await phone(browser)
                    # Admitted below what the link offered: the owner's answer, not the link's.
                    await join_as_guest(browser, harness, url)
                    self.assertFalse(await browser.evaluate(shown('guest-composer')))

                    participant = harness.participant().participant_id
                    await harness.host.role_set(participant, wire.EDITOR)
                    await browser.wait_for(shown('guest-composer'), timeout=30)
                    self.assertIn("guest · editor", await browser.evaluate(
                        "document.getElementById('guest-role').textContent"))
                    self.assertTrue(await browser.evaluate(shown('guest-ask')))

                    # And back down again: the composer goes with no reload.
                    await harness.host.role_set(participant, wire.VIEWER)
                    await browser.wait_for(f"!({shown('guest-composer')})", timeout=30)
                    self.assertIn("guest · viewer", await browser.evaluate(
                        "document.getElementById('guest-role').textContent"))
                    no_console_errors(self, browser)
                finally:
                    await browser.stop()
        run(main())

    def test_pausing_guests_says_why_typing_stopped(self):
        async def main():
            async with Harness(role=wire.EDITOR) as harness:
                url = await harness.invite(role=wire.EDITOR)
                browser = Browser()
                await browser.start()
                try:
                    await phone(browser)
                    await join_as_guest(browser, harness, url)
                    await browser.wait_for(shown('guest-composer'), timeout=20)

                    harness.host.share_pause("pane-1", True)
                    await browser.wait_for(
                        "document.getElementById('guest-note').textContent.includes('paused')",
                        timeout=30)
                    self.assertIn("The owner paused guests", await browser.evaluate(
                        "document.getElementById('guest-note').textContent"))
                    self.assertTrue(await browser.evaluate(
                        "document.getElementById('guest-prompt-send').disabled"))
                    self.assertFalse(await browser.evaluate(shown('guest-ask')))
                    self.assertEqual(await browser.evaluate(
                        "document.getElementById('guest-drive-mode').textContent"), "Paused")
                    # The screen keeps streaming while typing is off.
                    self.assertTrue(await browser.evaluate(shown('guest-screen-wrap')))

                    harness.host.share_pause("pane-1", False)
                    await browser.wait_for(shown('guest-ask'), timeout=30)
                    self.assertFalse(await browser.evaluate(
                        "document.getElementById('guest-prompt-send').disabled"))
                    no_console_errors(self, browser)
                finally:
                    await browser.stop()
        run(main())

    def test_removing_a_guest_ends_the_session_and_clears_its_record(self):
        async def main():
            async with Harness(role=wire.EDITOR) as harness:
                url = await harness.invite(role=wire.EDITOR)
                browser = Browser()
                await browser.start()
                try:
                    await phone(browser)
                    await join_as_guest(browser, harness, url)
                    participant = harness.participant().participant_id

                    await harness.host.participant_remove(participant)
                    await browser.wait_for(shown('screen-ended'), timeout=40)
                    self.assertIn("ended", await browser.evaluate(
                        "document.getElementById('ended-text').textContent"))
                    # The record is discarded, so a reload offers nothing to reconnect with.
                    self.assertIsNone(await browser.evaluate(GUEST_RECORD))
                    self.assertFalse(await browser.evaluate(shown('guest-screen-wrap')))
                    self.assertEqual(await browser.evaluate(SCREENS_SHOWN), 1)

                    await browser.navigate(harness.base + "/join")
                    await browser.wait_for(shown('screen-join'), timeout=30)
                    self.assertIn("without its code", await browser.evaluate(
                        "document.getElementById('join-note').textContent"))
                    no_console_errors(self, browser)
                finally:
                    await browser.stop()
        run(main())

    def test_an_owner_record_and_a_guest_record_live_side_by_side(self):
        """One browser, two desktops: the owner of one, somebody's guest on another.

        Neither flow may read, overwrite or be confused by the other's record — which is the
        client-side half of the rule `remote/guests.py` makes structural on the desktop.
        """
        async def main():
            async with Harness(role=wire.EDITOR) as mine, \
                    Harness(role=wire.VIEWER, share=mine) as theirs:
                pairing_url, _ = await mine.host.open_pairing()
                invite_url = await theirs.invite()
                browser = Browser()
                await browser.start()
                try:
                    await phone(browser)
                    # First, pair this browser with a desktop of its own.
                    await browser.navigate(pairing_url)
                    await browser.wait_for(shown('screen-inbox'), timeout=60)
                    self.assertEqual(len(mine.devices.live()), 1)

                    # Then follow somebody else's invite, in the same browser.
                    await join_as_guest(browser, theirs, invite_url, name="Bo")
                    device = await browser.evaluate(DEVICE_RECORD)
                    stored = await browser.evaluate(GUEST_RECORD)
                    self.assertEqual(device["deviceId"], mine.devices.live()[0].device_id)
                    self.assertEqual(stored["participant"],
                                     theirs.participant().participant_id)
                    # The shapes cannot be mistaken for one another, in either direction.
                    self.assertNotIn("participant", device)
                    self.assertNotIn("deviceId", stored)
                    self.assertEqual(device["capability"], "agent")
                    self.assertEqual(stored["role"], "viewer")

                    # And the owner's own session is untouched: / still opens their inbox.
                    await browser.navigate(mine.base + "/")
                    await browser.wait_for(shown('screen-inbox'), timeout=60)
                    self.assertEqual(await browser.evaluate(
                        "document.getElementById('capability').textContent"), "agent")
                    self.assertFalse(await browser.evaluate(shown('screen-guest')))
                    # Their guest record survived being the owner somewhere else.
                    self.assertEqual((await browser.evaluate(GUEST_RECORD))["participant"],
                                     theirs.participant().participant_id)
                    no_console_errors(self, browser)
                finally:
                    await browser.stop()
        run(main(), 240)


# Reading a record straight out of IndexedDB, which is where both of them live.
def _record(key):
    return """
        (async () => {
          const db = await new Promise((res, rej) => {
            const r = indexedDB.open('relay-remote', 1);
            r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error);
          });
          if (!db.objectStoreNames.contains('device')) return null;
          const row = await new Promise((res, rej) => {
            const r = db.transaction('device').objectStore('device').get('%s');
            r.onsuccess = () => res(r.result || null); r.onerror = () => rej(r.error);
          });
          if (!row) return null;
          // The key itself is a non-extractable CryptoKey and will not survive returnByValue.
          const { devicePrivate, devicePublic, desktopPublic, ...rest } = row;
          return rest;
        })()
    """ % key


GUEST_RECORD = _record("guest")
DEVICE_RECORD = _record("paired")


if __name__ == "__main__":
    unittest.main()
