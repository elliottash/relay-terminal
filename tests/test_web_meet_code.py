# SPDX-License-Identifier: AGPL-3.0-or-later
"""Joining a shared pane with a meeting code and a PIN: the browser's half (card #97EG).

Owner, 2026-09-18: "4 letter meeting code, 4 number pin code" — a friend types `BQRT` and `4829`
on the join page instead of opening a 141-character link.

Three layers, each against a desktop written from the card's wire spec rather than a stub of the
browser's own code:

* ``tests/meet_code_peer.mjs`` runs app/meet.js's code phase against a desktop in JavaScript:
  the right PIN opens the sealed fragment, a wrong one is caught by `tag_b` before `meet_confirm`
  is ever sent, and each of the desktop's refusals comes back as the right plain sentence.
* The same code phase over stdin/stdout, with the desktop in Python on remote/cpace.py: the two
  languages agree on CPace, the tags, the seal key and the AAD.
* The real page in headless Chrome, against the real rendezvous and hub: /join with no fragment
  offers the form, three wrong PINs burn the code, and the right one lands on the ordinary join
  screen, the ordinary knock with its five digits, and the guest's screen. The PIN is never in
  the address, history, storage or console. At 390x450 — a phone with its keyboard up — the form
  is on screen and usable.

Every Identity here is made in a temporary directory (the Harness does that), never the real one.
"""
import asyncio
import base64
import hashlib
import hmac
import json
import os
import shutil
import subprocess
import unittest
from pathlib import Path

from remote import ws
from remote import wire as wire_mod
from tests.browser import SCREENS_SHOWN, Browser, find_chrome, shown

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
NODE = shutil.which("node")
PEER = HERE / "meet_code_peer.mjs"

try:
    from remote import cpace
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
except ImportError:                       # the Python half is another session's, and may lag
    cpace = None

CODE, PIN, ROOM = "BQRT", "4829", "code-room-1"


def b64(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode()


def un64(text: str) -> bytes:
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def mac(key: bytes, message: bytes) -> bytes:
    return hmac.new(key, message, hashlib.sha256).digest()


class Desktop:
    """The responder's side of the code room, from the card, on remote/cpace.py.

    It counts failures the way the card says the desktop does — a bad tag, an invalid point, a
    close before `meet_confirm` — and after three the code is burned: the route stops resolving and
    a late attempt is answered `meet_error burned`.
    """

    def __init__(self, fragment: str, code: str = CODE, pin: str = PIN, room: str = ROOM):
        self.fragment, self.code, self.pin, self.room = fragment, code, pin, room
        self.failures = 0
        self.used = False
        self.frames: list[bytes | str] = []

    @property
    def burned(self) -> bool:
        return self.failures >= 3 or self.used

    async def attempt(self, receive, send) -> None:
        if self.burned:
            await send({"t": "meet_error", "error": "burned"})
            return
        try:
            first = json.loads(await receive())
            if first.get("t") != "meet_a":
                raise ValueError("expected meet_a")
            ya = un64(first["y"])
            me = cpace.CPace(self.pin.encode(), b"relay/meet/v1", self.room.encode(),
                             initiator=False, ad=b"desktop")
            isk = me.finish(ya, b"guest")
            yb = me.message
            await send({"t": "meet_b", "y": b64(yb),
                        "tag": b64(mac(isk, b"relay/meet/v1 desktop" + ya + yb))})
            confirm = json.loads(await receive())
            if confirm.get("t") != "meet_confirm" or not hmac.compare_digest(
                    un64(confirm.get("tag", "")), mac(isk, b"relay/meet/v1 guest" + ya + yb)):
                raise ValueError("bad tag")
        except (ValueError, KeyError, EOFError, ws.ConnectionClosed, cpace.CPaceError):
            self.failures += 1
            with _quiet():
                await send({"t": "meet_error", "error": "wrong_pin"})
            return
        nonce = os.urandom(12)
        sealed = AESGCM(mac(isk, b"relay/meet/v1 seal")).encrypt(
            nonce, self.fragment.encode(), self.code.encode())
        self.used = True
        await send({"t": "meet_invite", "nonce": b64(nonce), "sealed": b64(sealed)})


class _quiet:
    def __enter__(self):
        return self

    def __exit__(self, kind, value, traceback):
        return kind is not None and issubclass(kind, (ws.ConnectionClosed, BrokenPipeError,
                                                      ConnectionResetError))


# ---- 1: the code phase against a desktop in JavaScript -------------------------------------------

@unittest.skipUnless(NODE, "node is not installed")
class CodePhaseTests(unittest.TestCase):
    """app/meet.js, one scenario per case (tests/meet_code_peer.mjs)."""

    @classmethod
    def setUpClass(cls):
        done = subprocess.run([NODE, str(PEER), "cases"], capture_output=True, text=True,
                              cwd=str(ROOT), timeout=120)
        if done.returncode != 0:
            raise AssertionError(done.stderr)
        cls.cases = json.loads(done.stdout)

    def test_the_right_pin_opens_the_invite_fragment(self):
        right = self.cases["right"]
        self.assertTrue(right["ok"], right)
        self.assertEqual(right["fragment"], right["expected"])
        # meet_a, then meet_confirm, and the desktop checked the guest's tag.
        self.assertEqual([json.loads(frame)["t"] for frame in right["sent"]],
                         ["meet_a", "meet_confirm"])
        self.assertTrue(right["confirmed"])

    def test_a_wrong_pin_is_caught_before_the_guest_proves_anything(self):
        for name in ("wrongPin", "flippedTag"):
            case = self.cases[name]
            self.assertFalse(case["ok"], name)
            self.assertEqual(case["kind"], "wrong_pin", name)
            # tag_b did not check out, so meet_confirm was never sent: a server in the middle gets
            # nothing it could test a PIN guess against.
            self.assertEqual([json.loads(frame)["t"] for frame in case["sent"]], ["meet_a"], name)
            self.assertIn("after three", case["sentence"])
            self.assertIn("check the pin with the person", case["sentence"].lower())

    def test_the_room_id_is_part_of_the_key(self):
        self.assertEqual(self.cases["otherRoom"], {"ok": False, "kind": "wrong_pin"})

    def test_the_desktops_refusals_come_back_as_their_own_sentences(self):
        self.assertEqual(self.cases["burned"]["kind"], "burned")
        self.assertIn("stopped working", self.cases["burned"]["sentence"])
        self.assertEqual(self.cases["expired"]["kind"], "expired")
        self.assertIn("expired", self.cases["expired"]["sentence"])
        self.assertEqual(self.cases["desktopWrongPin"]["kind"], "wrong_pin")
        self.assertEqual(self.cases["silent"]["kind"], "no_answer")
        self.assertIn("not answering", self.cases["silent"]["sentence"])

    def test_an_invalid_point_or_a_seal_under_another_code_is_refused(self):
        self.assertEqual(self.cases["zeroPoint"]["kind"], "protocol")
        # AAD is the upper-case code: sealed under anything else, it does not open.
        self.assertEqual(self.cases["otherAad"]["kind"], "protocol")

    def test_what_is_typed_is_cleaned_to_the_alphabet(self):
        clean = self.cases["clean"]
        # Upper case, no I, L or O, four at most; digits only for the PIN.
        self.assertEqual(clean["code"], ["BQRT", "BQRT", "BQRT", "AB", "ABCD", "", ""])
        self.assertEqual(clean["pin"], ["4829", "4829", "4829", "4829", ""])
        self.assertEqual(self.cases["badInput"], ["bad_code", "bad_code", "bad_pin"])

    def test_the_lookup_route_and_its_failures(self):
        lookups = self.cases["lookups"]
        self.assertEqual(lookups["found"], {"room": "code-room-1"})
        self.assertEqual(lookups["missing"], {"kind": "unknown_code"})
        self.assertEqual(lookups["limited"], {"kind": "rate_limited"})
        self.assertEqual(lookups["broken"], {"kind": "unreachable"})
        self.assertEqual(lookups["empty"], {"kind": "unknown_code"})
        self.assertEqual(lookups["offline"], {"kind": "unreachable"})
        self.assertTrue(all(url == "https://relay.example/v1/codes/BQRT" for url in lookups["asked"]))

    def test_a_room_that_never_opens_gives_up_instead_of_hanging(self):
        """A socket nobody answers fires no event at all: `openCodeRoom` used to wait for ever, so
        the join sat on "Checking the code and PIN…" with the form disabled and nothing to press.
        It has a deadline of its own now (CONNECT_WAIT), it closes the socket on the way out, and
        the sentence it fails with is the one that says to try again — which app/guest.js shows
        with Join enabled behind it."""
        case = self.cases["connectTimeout"]
        self.assertFalse(case["ok"], case)
        self.assertEqual(case["kind"], "unreachable")
        self.assertIn("try again", case["sentence"])
        self.assertTrue(case["waited"], "it gave up before its own deadline")
        self.assertTrue(case["closed"], "the socket was left connecting")
        self.assertEqual(case["url"], "wss://relay.example/v1/connect?room=code-room-1")

    def test_no_sentence_is_empty(self):
        for kind, sentence in self.cases["sentences"].items():
            self.assertGreater(len(sentence), 20, kind)
        self.assertIn("ten minutes", self.cases["sentences"]["unknown_code"])


# ---- 2: the same code phase against a desktop in Python ------------------------------------------

@unittest.skipUnless(NODE, "node is not installed")
@unittest.skipUnless(cpace, "remote/cpace.py is not there yet")
class CrossLanguageTests(unittest.TestCase):
    """app/meet.js + app/cpace.js as the guest, remote/cpace.py as the desktop."""

    def exchange(self, pin: str, desktop: Desktop) -> dict:
        async def main():
            process = await asyncio.create_subprocess_exec(
                NODE, str(PEER), "wire", json.dumps({"code": CODE, "pin": pin, "room": ROOM}),
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, cwd=str(ROOT))
            result = {}

            async def receive():
                line = await process.stdout.readline()
                if not line:
                    raise EOFError
                message = json.loads(line)
                if "done" in message:
                    result.update(message["done"])
                    raise EOFError
                return line

            async def send(message):
                process.stdin.write(json.dumps(message).encode() + b"\n")
                await process.stdin.drain()

            await desktop.attempt(receive, send)
            while "ok" not in result:
                try:
                    await receive()
                except EOFError:
                    break
            process.stdin.close()
            await process.wait()
            return result
        return asyncio.run(asyncio.wait_for(main(), 60))

    def test_the_right_pin_opens_what_python_sealed(self):
        fragment = "v=1&d=" + b64(b"\x33" * 32) + "&i=" + b64(b"\x44" * 16) + "&r=room-9"
        desktop = Desktop(fragment)
        self.assertEqual(self.exchange(PIN, desktop), {"ok": True, "fragment": fragment})
        self.assertEqual(desktop.failures, 0)

    def test_a_wrong_pin_fails_on_both_sides_and_counts(self):
        desktop = Desktop("v=1")
        self.assertEqual(self.exchange("4828", desktop), {"ok": False, "kind": "wrong_pin"})
        self.assertEqual(desktop.failures, 1)


# ---- 3: the real page -----------------------------------------------------------------------------

def _harness():
    # Imported here: it pulls in the hub and the rendezvous, which the node tests do not need.
    from tests.test_remote_guest_browser import Harness
    return Harness


async def _type(browser, element_id: str, text: str) -> None:
    """Type as a keyboard does, one key at a time, through the page's own input handlers."""
    await browser.evaluate(f"document.getElementById({element_id!r}).focus()")
    for ch in text:
        await browser.call("Input.insertText", {"text": ch})


async def _clear(browser, element_id: str) -> None:
    await browser.evaluate(f"(() => {{ document.getElementById({element_id!r}).value = '';"
                           f" return true; }})()")


NOTE = "document.getElementById('meet-note').textContent"


@unittest.skipUnless(find_chrome(), "no Chrome or Chromium installed")
@unittest.skipUnless(cpace, "remote/cpace.py is not there yet")
class JoinPageTests(unittest.TestCase):
    """/join with no link: the code form, the code phase, then the ordinary knock."""

    def test_a_friend_joins_with_a_code_and_pin_and_knocks_as_a_link_would(self):
        async def main():
            async with _harness()(role=wire_mod.VIEWER) as harness:
                url = await harness.invite()
                desktop = Desktop(url.split("#", 1)[1])
                lookups = []

                # The two things the rendezvous and the desktop add for this card, standing in
                # here on the real rendezvous so everything after the code phase is the real one:
                # the lookup route, and a code room whose client is answered by the code handler.
                @harness.server.route("GET", f"/v1/codes/{CODE}")
                async def lookup(request, body):
                    lookups.append(request.path)
                    if desktop.burned:
                        return ws_json_error()
                    return json_reply({"room": ROOM})

                real_connect = harness.server.sockets["/v1/connect"]

                async def connect(socket):
                    query = socket.request.query if socket.request else {}
                    if query.get("room") != ROOM:
                        await real_connect(socket)
                        return

                    async def receive():
                        frame = await socket.recv()
                        desktop.frames.append(frame)
                        return frame

                    async def send(message):
                        await socket.send(json.dumps(message).encode())
                    await desktop.attempt(receive, send)
                harness.server.sockets["/v1/connect"] = connect

                browser = Browser()
                await browser.start()
                try:
                    await browser.call("Emulation.setDeviceMetricsOverride", {
                        "width": 390, "height": 844, "deviceScaleFactor": 2, "mobile": True})
                    await browser.navigate(harness.base + "/join")
                    await browser.wait_for(shown("screen-meet"), timeout=40)
                    self.assertEqual(await browser.evaluate(SCREENS_SHOWN), 1)
                    self.assertEqual(await browser.evaluate(
                        "document.getElementById('meet-pin').inputMode"), "numeric")
                    self.assertEqual(await browser.evaluate(
                        "document.getElementById('meet-pin').autocomplete"), "one-time-code")

                    # The code field upper-cases and keeps to the alphabet; the PIN to digits.
                    await _type(browser, "meet-code", "bq1ilo")
                    self.assertEqual(await browser.evaluate(
                        "document.getElementById('meet-code').value"), "BQ")
                    await _type(browser, "meet-code", "rtx")
                    self.assertEqual(await browser.evaluate(
                        "document.getElementById('meet-code').value"), "BQRT")
                    # Four letters in, the PIN field has the focus.
                    self.assertEqual(await browser.evaluate("document.activeElement.id"),
                                     "meet-pin")

                    # An unknown code is a sentence, not a hang.
                    await _clear(browser, "meet-code")
                    await _type(browser, "meet-code", "zzzz")
                    await _type(browser, "meet-pin", "4a8 29")
                    self.assertEqual(await browser.evaluate(
                        "document.getElementById('meet-pin').value"), "4829")
                    await browser.evaluate("document.getElementById('meet-join').click()")
                    said = await browser.wait_for(
                        f"/no meeting with that code/i.test({NOTE}) ? {NOTE} : ''")
                    self.assertIn("ten minutes", said)

                    # Two wrong PINs: each is a plain sentence saying it counts.
                    await _clear(browser, "meet-code")
                    await _type(browser, "meet-code", CODE)
                    for attempt in (1, 2):
                        await _clear(browser, "meet-pin")
                        await _type(browser, "meet-pin", "1111")
                        await browser.evaluate("document.getElementById('meet-join').click()")
                        said = await browser.wait_for(
                            f"/PIN is not right/.test({NOTE}) && "
                            f"document.getElementById('meet-pin').value === '' ? {NOTE} : ''")
                        self.assertIn("after three, this code stops working", said)
                        for _ in range(50):
                            if desktop.failures == attempt:
                                break
                            await asyncio.sleep(0.1)
                        self.assertEqual(desktop.failures, attempt)
                    # The frames reached the desktop as binary: the rendezvous drops text frames.
                    self.assertTrue(desktop.frames)
                    self.assertTrue(all(isinstance(f, bytes) for f in desktop.frames))

                    # At 390x450, a phone with its keyboard up, the form is still on screen.
                    await browser.call("Emulation.setDeviceMetricsOverride", {
                        "width": 390, "height": 450, "deviceScaleFactor": 2, "mobile": True})
                    await browser.evaluate("document.getElementById('meet-pin').focus()")
                    await asyncio.sleep(0.6)
                    for element in ("meet-code", "meet-pin", "meet-join", "meet-note"):
                        box = await browser.evaluate(
                            f"(() => {{ const r = document.getElementById({element!r})"
                            ".getBoundingClientRect(); const v = window.visualViewport;"
                            " return {top: r.top, bottom: r.bottom, left: r.left, right: r.right,"
                            " height: v ? v.height : innerHeight, width: v ? v.width : innerWidth};"
                            " })()")
                        self.assertGreaterEqual(box["top"], 0, (element, box))
                        self.assertLessEqual(box["bottom"], box["height"], (element, box))
                        self.assertGreaterEqual(box["left"], 0, (element, box))
                        self.assertLessEqual(box["right"], box["width"], (element, box))
                    # A thumb can hit it: the Join button is a full-width, finger-sized target.
                    height = await browser.evaluate(
                        "document.getElementById('meet-join').getBoundingClientRect().height")
                    self.assertGreaterEqual(height, 40)

                    # The right PIN: the ordinary join screen, with the desktop's own key.
                    await browser.call("Emulation.setDeviceMetricsOverride", {
                        "width": 390, "height": 844, "deviceScaleFactor": 2, "mobile": True})
                    await _clear(browser, "meet-pin")
                    await _type(browser, "meet-pin", PIN)
                    await browser.evaluate("document.getElementById('meet-join').click()")
                    await browser.wait_for(shown("screen-join"), timeout=40)
                    self.assertEqual(await browser.evaluate(SCREENS_SHOWN), 1)
                    printed = await browser.wait_for(
                        "document.getElementById('join-fingerprint').textContent !== '…'"
                        " ? document.getElementById('join-fingerprint').textContent : ''")
                    self.assertEqual(printed, harness.identity.fingerprint)
                    self.assertTrue(desktop.used)
                    self.assertEqual(desktop.failures, 2)

                    # The PIN is nowhere it could outlive this page.
                    leaked = await browser.evaluate(
                        "JSON.stringify([location.href, history.length, document.title,"
                        " {...localStorage}, {...sessionStorage},"
                        " document.getElementById('meet-pin').value])")
                    self.assertNotIn(PIN, leaked)
                    self.assertFalse(any(PIN in line for line in browser.console),
                                     browser.console)
                    self.assertEqual(await browser.evaluate("location.pathname + location.search"
                                                            " + location.hash"), "/join")

                    # Then the ordinary knock, five digits to compare and all, and in.
                    await browser.evaluate(
                        "(() => { document.getElementById('join-name').value = 'Bo';"
                        " return true; })()")
                    await browser.evaluate("document.getElementById('join-knock').click()")
                    code = await browser.wait_for(
                        "document.getElementById('knock-code')?.textContent?.match(/^\\d{5}$/)"
                        " ? document.getElementById('knock-code').textContent : ''", timeout=40)
                    self.assertRegex(code, r"^\d{5}$")
                    await browser.wait_for(shown("screen-guest"), timeout=60)
                    self.assertEqual([k.name for k in harness.knocks], ["Bo"])

                    # The code is spent: another visit is told so, and the lookup stops resolving.
                    await browser.evaluate("indexedDB.deleteDatabase('relay-remote'); true")
                    await browser.navigate(harness.base + "/join")
                    await browser.wait_for(shown("screen-meet"), timeout=40)
                    await _type(browser, "meet-code", CODE)
                    await _type(browser, "meet-pin", PIN)
                    await browser.evaluate("document.getElementById('meet-join').click()")
                    said = await browser.wait_for(
                        f"/no meeting with that code/i.test({NOTE}) ? {NOTE} : ''")
                    self.assertTrue(said)
                    self.assertFalse(any(PIN in line for line in browser.console))
                finally:
                    await browser.stop()
        asyncio.run(asyncio.wait_for(main(), 240))

    def test_three_wrong_pins_burn_the_code(self):
        async def main():
            async with _harness()(role=wire_mod.VIEWER) as harness:
                url = await harness.invite()
                desktop = Desktop(url.split("#", 1)[1])

                @harness.server.route("GET", f"/v1/codes/{CODE}")
                async def lookup(request, body):
                    # Still resolving after the burn, so the desktop's own `burned` is what the
                    # page hears: the server and the desktop learn of a burn at different times.
                    return json_reply({"room": ROOM})

                real_connect = harness.server.sockets["/v1/connect"]

                async def connect(socket):
                    query = socket.request.query if socket.request else {}
                    if query.get("room") != ROOM:
                        await real_connect(socket)
                        return

                    async def send(message):
                        await socket.send(json.dumps(message).encode())
                    await desktop.attempt(socket.recv, send)
                harness.server.sockets["/v1/connect"] = connect

                browser = Browser()
                await browser.start()
                try:
                    await browser.navigate(harness.base + "/join")
                    await browser.wait_for(shown("screen-meet"), timeout=40)
                    await _type(browser, "meet-code", CODE)
                    for attempt in (1, 2, 3):
                        await _type(browser, "meet-pin", "0000")
                        await browser.evaluate("document.getElementById('meet-join').click()")
                        await browser.wait_for(
                            f"/PIN is not right/.test({NOTE}) && "
                            "!document.getElementById('meet-join').disabled")
                        for _ in range(50):
                            if desktop.failures == attempt:
                                break
                            await asyncio.sleep(0.1)
                        self.assertEqual(desktop.failures, attempt)
                    # The right PIN, too late.
                    await _type(browser, "meet-pin", PIN)
                    await browser.evaluate("document.getElementById('meet-join').click()")
                    said = await browser.wait_for(
                        f"/stopped working/.test({NOTE}) ? {NOTE} : ''")
                    self.assertIn("new code", said)
                    self.assertFalse(desktop.used)
                    self.assertFalse(any(PIN in line or "0000" in line
                                         for line in browser.console), browser.console)
                finally:
                    await browser.stop()
        asyncio.run(asyncio.wait_for(main(), 180))


def json_reply(payload):
    from remote import httpd
    return httpd.Response.json(payload)


def ws_json_error():
    from remote import httpd
    return httpd.Response.error(404, "no such code.")


if __name__ == "__main__":
    unittest.main()
