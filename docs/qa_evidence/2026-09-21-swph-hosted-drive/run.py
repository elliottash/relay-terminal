# SPDX-License-Identifier: AGPL-3.0-or-later
"""#SWPH task 4, driven once against the real hosted rendezvous (drive.sh runs this).

Two halves, one command, one jail, one Relay, one paired phone:

* **#FR1C's steps 14-19** first, unchanged, on the same build: they are how this run gets a phone
  paired *by typed code* at `full`, notifications switched on (the one browser stub,
  `pushManager.subscribe`), and a desktop whose remote control has been off and on again.
* then **this card's thirteen steps** on that same phone: the inbox row, the board, a card and its
  thread, Answer, a note, a move, a new card, search, Discuss / Stop / Plan, Execute and Verify,
  the `card_waiting` push, the offline queue, what a guest and a `view` device are refused, that
  no path of this machine reaches the phone, and the iPad and landscape layouts.

The harness underneath — the isolation, the xdotool desktop, the headless phones, every helper —
is #PH0N's `run.py` and #FR1C's, loaded by path rather than copied. What this file adds is the
steps, a frame tap in each page (what the page *decrypted*, which is the only place a Noise
session's contents can be read), and the reading of the sidecar tap's log (`sidecar_tap.py`).

Every step writes what it saw to notes.txt with PASS/FAIL/NOTE in front of it, and a step that
throws is recorded and the run carries on: one run should surface every problem, not the first.
"""
from __future__ import annotations

import asyncio
import base64
import importlib.util
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))

FR1C = Path(os.environ.get("RELAY_QA_FR1C",
                           ROOT / "docs/qa_evidence/2026-09-21-fr1c-hosted-drive")) / "run.py"
_spec = importlib.util.spec_from_file_location("fr1c_drive", FR1C)
fr1c = importlib.util.module_from_spec(_spec)
sys.modules["fr1c_drive"] = fr1c
_spec.loader.exec_module(fr1c)           # which loads #PH0N's run.py in turn
ph0n = fr1c.ph0n

from remote import push                                                   # noqa: E402
from tests.browser import SCREENS_SHOWN, Browser, shown                   # noqa: E402
from tests.test_remote_push import receive, subscription_keypair          # noqa: E402

# The board as the *desktop's* code reads it, from the tree the binary was built from.
SOURCE = Path(os.environ.get("RELAY_QA_SOURCE", ROOT))
sys.path.insert(0, str(SOURCE / "backend"))
from relay_core import board as B                                         # noqa: E402

OUT, JAIL, WORK, HOSTED = ph0n.OUT, ph0n.JAIL, ph0n.WORK, ph0n.HOSTED
TAP = Path(os.environ["SWPH_TAP_LOG"])
note, share_shot, root_shot = ph0n.note, ph0n.share_shot, ph0n.root_shot
browser_shot, press, step = ph0n.browser_shot, ph0n.press, ph0n.step
key, xdo, click, focus_relay = ph0n.key, ph0n.xdo, ph0n.click, ph0n.focus_relay
LINK = ph0n.LINK

IPAD = {"width": 1180, "height": 820, "deviceScaleFactor": 2, "mobile": True}
LANDSCAPE_KEYBOARD = {"width": 844, "height": 185, "deviceScaleFactor": 2, "mobile": True}

# ---- what each page is given before its document loads ------------------------------------------

# Every message the page's `Rrp` hands the app (app/rrp.js `emit`): the decrypted wire, on the far
# side of the Noise session. Board events, errors and the welcome are kept whole; anything else —
# screen frames above all — is kept as its type, its size and whether a path of this machine is in
# it, because step 12 asks that of *everything* and a whole run of screen frames is megabytes.
# The `Rrp` itself is kept too: step 11 sends a `board_request` from a guest's and a `view`
# device's own session, which no button on their pages will.
FRAME_TAP = """
  (() => {
    const real = EventTarget.prototype.dispatchEvent;
    const frames = [];
    const NEEDLES = %s;
    Object.defineProperty(window, '__relayFrames', { value: frames });
    EventTarget.prototype.dispatchEvent = function (event) {
      try {
        if (event instanceof CustomEvent && event.detail && typeof event.detail === 'object'
            && typeof this.send === 'function' && typeof this.resume === 'function') {
          window.__relayRrp = this;
          const text = JSON.stringify(event.detail);
          const whole = ['board_event', 'error', 'welcome', 'push_state'].includes(event.type);
          const hits = NEEDLES.filter((needle) => text.includes(needle));
          const at = hits.length ? text.indexOf(hits[hits.length - 1]) : -1;
          frames.push({ at: Date.now(), t: event.type, size: text.length, hits,
                        where: at < 0 ? undefined : text.slice(Math.max(0, at - 60), at + 80),
                        sub: event.detail.event && event.detail.event.event,
                        detail: whole ? JSON.parse(text) : undefined });
        }
      } catch { /* a tap must never break the page */ }
      return real.call(this, event);
    };
    // The count on the app icon (app/app.js `updateBadge`), which a headless page cannot show.
    const badge = [];
    Object.defineProperty(window, '__relayBadge', { value: badge });
    const set = navigator.setAppBadge ? navigator.setAppBadge.bind(navigator) : null;
    const clear = navigator.clearAppBadge ? navigator.clearAppBadge.bind(navigator) : null;
    navigator.setAppBadge = (count) => { badge.push(count);
      try { return set ? set(count).catch(() => {}) : Promise.resolve(); } catch { return Promise.resolve(); } };
    navigator.clearAppBadge = () => { badge.push(0);
      try { return clear ? clear().catch(() => {}) : Promise.resolve(); } catch { return Promise.resolve(); } };
  })();
""" % json.dumps([str(WORK), str(JAIL), "/home/", "/tmp/"])

SUBSCRIPTION: dict = {}          # the stub subscription's private half, kept so the push can be opened
FRAMES: dict[str, list[dict]] = {}


async def install_push_stub(browser: Browser) -> None:
    """#PH0N's stub of `pushManager.subscribe`, with the private key kept (step 9 opens the push)."""
    if not SUBSCRIPTION:
        private, public, auth = subscription_keypair()
        SUBSCRIPTION.update(private=private, public=public, auth=auth)
    b64 = lambda raw: base64.urlsafe_b64encode(raw).decode().rstrip("=")   # noqa: E731
    script = """
      (() => {
        const endpoint = %s, p256dh = %s, auth = %s;
        const un64 = (text) => Uint8Array.from(
          atob(text.replace(/-/g, '+').replace(/_/g, '/')), (c) => c.charCodeAt(0));
        const fake = {
          endpoint,
          getKey: (name) => (name === 'p256dh' ? un64(p256dh) : un64(auth)).buffer,
          unsubscribe: async () => true,
          toJSON: () => ({ endpoint }),
        };
        let live = null;
        PushManager.prototype.subscribe = async function () { live = fake; return fake; };
        PushManager.prototype.getSubscription = async function () { return live; };
      })();
    """ % (json.dumps("https://push.invalid/swph-drive/subscription"),
           json.dumps(b64(SUBSCRIPTION["public"])), json.dumps(b64(SUBSCRIPTION["auth"])))
    await browser.call("Page.addScriptToEvaluateOnNewDocument", {"source": script})


async def open_phone(url: str, *, size: dict | None = None, track_sockets: bool = False,
                     stub_push: bool = False) -> Browser:
    """#PH0N's `open_phone`, with the frame tap in every page this run opens."""
    browser = Browser()
    await browser.start()
    await browser.call("Emulation.setDeviceMetricsOverride", size or ph0n.PHONE)
    await browser.call("Page.addScriptToEvaluateOnNewDocument", {"source": FRAME_TAP})
    if track_sockets:
        await browser.call("Page.addScriptToEvaluateOnNewDocument", {"source": ph0n.SOCKET_TRACKER})
    if stub_push:
        await install_push_stub(browser)
    await browser.navigate(url)
    return browser


ph0n.open_phone = open_phone
ph0n.install_push_stub = install_push_stub
fr1c.open_phone = open_phone


async def harvest(browser: Browser | None, who: str) -> None:
    """Move what the page's tap has caught into this process. Before every navigation (a new
    document starts a new array) and at the end."""
    if browser is None:
        return
    try:
        caught = await browser.evaluate(
            "(() => { const f = window.__relayFrames || []; return f.splice(0, f.length); })()")
    except Exception:                                                     # noqa: BLE001
        return
    FRAMES.setdefault(who, []).extend(caught or [])


# ---- small things ---------------------------------------------------------------------------------

def relay_alive() -> bool:
    return ph0n.RELAY is not None and ph0n.RELAY.poll() is None


def desk(what: str) -> str:
    """#PH0N's `desk`, which waits for ever on a window that has gone: this run's fourth attempt
    found a crash that way (README, "What it found"), and sat on it for twenty minutes."""
    name = ph0n.shot_name("desktop", what)
    if relay_alive():
        try:
            subprocess.run(["import", "-window", ph0n.WIN, str(OUT / name)], check=False, timeout=20)
        except subprocess.TimeoutExpired:
            pass
    print("shot", name, flush=True)
    return name


def board() -> B.Board:
    return B.Board(WORK / B.DEFAULT_BOARD_FOLDER, repo=WORK)


def card_on_disk(card_id: str) -> B.Card | None:
    return board().card_by_id(card_id)


def thread_on_disk(card_id: str) -> list:
    return board().thread(card_id)


def tap_rows(**match) -> list[dict]:
    rows = []
    for line in TAP.read_text().splitlines():
        try:
            row = json.loads(line)
        except ValueError:
            continue
        if all(row.get(k) == v for k, v in match.items()):
            rows.append(row)
    return rows


def ocr(crop: tuple[int, int, int, int] | None = None, psm: str = "6") -> str:
    """The Relay window (or a strip of it) read by tesseract: the status line is a QStatusBar
    message, which nothing but a screen reader or a photograph can see."""
    from PIL import Image, ImageOps

    path = OUT / ".ocr.png"
    if not relay_alive():
        return ""
    try:
        subprocess.run(["import", "-window", ph0n.WIN, str(path)], check=False, timeout=20)
    except subprocess.TimeoutExpired:
        return ""
    try:
        image = Image.open(path).convert("L")
    except OSError:
        return ""
    if crop:
        image = image.crop(crop)
    image = ImageOps.invert(image).resize((image.width * 3, image.height * 3))
    image.save(path)
    return subprocess.run(["tesseract", str(path), "stdout", "--psm", psm], capture_output=True,
                          text=True).stdout


def desktop_counts() -> dict[str, int]:
    """The desktop Switchboard pane's section headers — "▸ PLANNED 2" — read off the window."""
    found: dict[str, int] = {}
    for line in ocr().splitlines():
        match = re.match(r"^\W*(INBOX|DISCUSSING|PLANNING|PLANNED|EXECUTING|NEEDS VERIFICATION|NEEDS QA"
                         r"|VERIFIED|DONE)\s+(\d+)\s*$", line.strip())
        if match:
            found[match.group(1).lower().replace(" ", "-")] = int(match.group(2))
    return found


def status_line(pattern: str, timeout: float = 8.0) -> str:
    """The window's status line, once it matches. It is shown for nine seconds."""
    deadline = time.monotonic() + timeout
    seen = ""
    while time.monotonic() < deadline:
        seen = " ".join(ocr(crop=(0, 960, 1600, 1000), psm="7").split())
        if re.search(pattern, seen, re.I):
            return seen
        time.sleep(0.4)
    return seen


async def type_into(browser: Browser, selector: str, text: str) -> None:
    """Focus, then the keyboard's own path (`Input.insertText` fires the page's input handlers)."""
    found = await browser.evaluate(
        f"(() => {{ const n = document.querySelector({json.dumps(selector)});"
        f" if (!n) return false; n.focus(); return true; }})()")
    if not found:
        raise RuntimeError(f"nothing to type into at {selector}")
    await browser.call("Input.insertText", {"text": text})


async def tap(browser: Browser, selector: str) -> None:
    result = await ph0n.click_first(browser, selector)
    if result:
        raise RuntimeError(f"tapping {selector}: {result}")


async def soon(browser: Browser, expression: str, timeout: float = 30):
    try:
        return await browser.wait_for(expression, timeout=timeout)
    except AssertionError:
        return None


BOARD = """
  (() => {
    const text = (n) => (n ? n.textContent : '');
    const seen = (n) => !!n && !n.hidden && n.getClientRects().length > 0;
    return {
      shown: getComputedStyle(document.getElementById('screen-board')).display !== 'none',
      device: (document.querySelector('.rb') || { dataset: {} }).dataset.device || '',
      name: text(document.querySelector('.rb-bar-sub')),
      tabs: [...document.querySelectorAll('.rb-tab')].map(text),
      sections: [...document.querySelectorAll('.rb-list .rb-section')].map((n) =>
        [n.dataset.section, text(n.querySelector('.rb-section-title')), +text(n.querySelector('.rb-section-count'))]),
      rows: [...document.querySelectorAll('.rb-list .rb-row')].map((n) => [n.dataset.group, n.dataset.cardId]),
      busy: [...document.querySelectorAll('.rb-list .rb-row')].filter((n) => !n.querySelector('.rb-row-busy').hidden)
        .map((n) => n.dataset.cardId),
      line: text(document.querySelector('.rb-list-col .rb-line')),
      // The strip lives above the list, which a phone with a card open has slid away: its words
      // are read whether or not it is on screen, `hidden` being what says it has nothing to say.
      offline: (document.querySelector('.rb-offline') || { hidden: true }).hidden ? '' : text(document.querySelector('.rb-offline')),
    };
  })()
"""

CARD = """
  (() => {
    const text = (n) => (n ? n.textContent : '');
    const seen = (n) => !!n && !n.hidden && n.getClientRects().length > 0;
    const scroll = document.querySelector('.rb-card-scroll');
    const style = (n) => { if (!n) return ''; const s = getComputedStyle(n);
      return `${s.borderLeftWidth} ${s.borderLeftColor}`; };
    return {
      ref: text(document.querySelector('.rb-card-ref')),
      title: text(document.querySelector('.rb-card-title')),
      chips: [...document.querySelectorAll('.rb-card-chips .rb-chip')].map(text),
      headings: [...document.querySelectorAll('.rb-card-scroll .rb-body-heading')].map(text),
      sections: [...document.querySelectorAll('.rb-card-scroll .rb-section-body')].map((n) =>
        [text(n.querySelector('.rb-body-heading')), text(n)]),
      markers: /<!--|-->|t:[a-z0-9]{2}\\b/.test(text(scroll)),
      entries: [...document.querySelectorAll('.rb-thread .rb-entry')].map((n) => ({
        kind: n.dataset.kind || '', author: n.dataset.author || '', pending: n.dataset.pending || '',
        live: n.classList.contains('rb-entry-live'), text: text(n).slice(0, 400) })),
      questionStyle: style(document.querySelector('.rb-thread .rb-entry-question')),
      commentStyle: style(document.querySelector('.rb-thread .rb-entry:not(.rb-entry-question)')),
      options: [...document.querySelectorAll('.rb-option')].map(text),
      send: text(document.querySelector('.rb-send-comment')),
      reply: (document.querySelector('.rb-reply-text') || {}).value || '',
      stop: seen(document.querySelector('.rb-stop')),
      actions: [...document.querySelectorAll('.rb-actions .rb-action')].map((n) => [text(n), n.disabled]),
      line: text(document.querySelector('.rb-card-line .rb-line-text')),
      lineAction: text(document.querySelector('.rb-card-line .rb-line-action')),
    };
  })()
"""

INBOX = """
  (() => {
    const row = document.getElementById('board-row');
    const seen = (n) => !!n && !n.hidden && n.getClientRects().length > 0;
    const first = document.querySelector('.pane-row');
    return {
      row: seen(row),
      chip: seen(row && row.querySelector('.rb-inbox-chip')) ? row.querySelector('.rb-inbox-chip').textContent : '',
      sub: row ? row.querySelector('.rb-inbox-sub').textContent : '',
      leads: seen(row) && !!first && row.getBoundingClientRect().top < first.getBoundingClientRect().top,
      panes: document.querySelectorAll('.pane-row').length,
      capability: (document.getElementById('capability') || {}).textContent || '',
      badge: (window.__relayBadge || []).slice(-1)[0],
      needsYou: [...document.querySelectorAll('[data-pane-chip]')].map((n) => n.textContent),
    };
  })()
"""


def expected_sections() -> list[tuple[str, int]]:
    """The desktop's sections and their counts, from the files, by the desktop's own rules: the
    configured columns with what each collects (`board.column_statuses_of`), then Verified and
    Done, which `Model::sections()` always puts last — a closed card somebody else verified is
    Verified, every other closed card is Done."""
    b = board()
    config = b.config()
    cards = [c for c in b.cards() if c.type == "work"]
    closed = [c for c in cards if c.status in ("done", "dropped")]
    out = []
    for column in config.get("columns") or []:
        statuses = [s for s in B.column_statuses_of(config, column) if s not in ("done", "dropped")]
        if statuses:
            out.append((column, sum(1 for c in cards if c.status in statuses)))
    verified = [c for c in closed if c.status == "done" and str(c.front.get("verified_by") or "").strip()
                and str(c.front.get("verified_by")).strip() != str(c.front.get("implemented_by") or "").strip()]
    out.append(("verified", len(verified)))
    out.append(("done", len(closed) - len(verified)))
    return out


class Swph(fr1c.Drive):
    """#FR1C's six steps, then the Switchboard's thirteen on the phone they paired."""

    def __init__(self) -> None:
        super().__init__()
        self.viewer: Browser | None = None
        self.viewer_id = ""
        self.new_card = ""
        self.device_name = ""
        self.board_pane_open = False

    # ---- helpers on the phone -------------------------------------------------------------------
    async def to_inbox(self) -> None:
        phone = self.phone
        if await phone.evaluate(shown("screen-board")):
            await tap(phone, ".rb-bar .rb-back")
        elif not await phone.evaluate(shown("screen-inbox")):
            await ph0n.back_to_inbox(phone)
        await phone.wait_for(shown("screen-inbox"), timeout=30)

    async def to_board(self) -> None:
        phone = self.phone
        if not await phone.evaluate(shown("screen-board")):
            await self.to_inbox()
            await tap(phone, "#board-row")
        await phone.wait_for(f"{shown('screen-board')} && document.querySelectorAll('.rb-list .rb-row').length > 0",
                             timeout=40)

    async def to_list(self) -> None:
        phone = self.phone
        await self.to_board()
        if await phone.evaluate("(document.querySelector('.rb') || {dataset: {}}).dataset.card === 'open'"):
            await tap(phone, ".rb-card-back")
            await asyncio.sleep(0.4)

    async def open_card(self, card_id: str, browser: Browser | None = None) -> dict:
        phone = browser or self.phone
        if browser is None:
            await self.to_list()
        await tap(phone, f".rb-list .rb-row[data-card-id=\"{card_id}\"]")
        await phone.wait_for(
            f"(document.querySelector('.rb-card-ref') || {{}}).textContent === '#{card_id}'"
            " && document.querySelectorAll('.rb-card-scroll .rb-section-body, .rb-card-scroll .rb-thread').length > 0",
            timeout=40)
        await asyncio.sleep(0.6)
        return await phone.evaluate(CARD)

    async def card(self) -> dict:
        return await self.phone.evaluate(CARD)

    # -- 1. the inbox row -----------------------------------------------------------------------------
    async def inbox_row(self) -> None:
        phone = self.phone
        await self.to_inbox()
        await soon(phone, "(() => { const r = document.getElementById('board-row');"
                          " return r && !r.hidden && /open card/.test(r.textContent); })()", timeout=60)
        seen = await phone.evaluate(INBOX)
        await browser_shot(phone, "phone", "sw01a-the-inbox-leads-with-the-switchboard")
        waiting = [c.id for c in board().cards() if str(c.front.get("waiting_on") or "") == "owner"]
        features = [f["detail"].get("features") for f in await self.frames_of(phone, "phone", "welcome")]
        note("PASS" if (seen["row"] and seen["leads"] and seen["chip"] == "1 waiting on you"
                        and seen["badge"] == len(waiting) + sum(1 for c in seen["needsYou"] if "wait" in c)
                        and seen["capability"] == "full") else "FAIL",
             f"the inbox of the phone paired by typed code ({seen['capability']!r}) leads with the"
             f" Switchboard row, above its {seen['panes']} pane(s) ({seen['leads']}): it says"
             f" {seen['chip']!r} and {seen['sub']!r}; on disk {waiting} wait on the owner. The app"
             f" badge was last set to {seen['badge']!r} (app.js counts the panes that need you plus"
             f" the cards that do). The welcome's features: {features[-1] if features else None}"
             " (shot sw01a)")

        # The second device, paired for viewing only before step 19 (`pair_viewer`): no row.
        viewer = self.viewer
        if viewer is None:
            raise RuntimeError("no view-only device was paired")
        await viewer.wait_for(f"{shown('screen-inbox')} && {LINK} === 'connected'", timeout=60)
        lesser = await viewer.evaluate(INBOX)
        await browser_shot(viewer, "phone", "sw01c-a-view-only-device-has-no-row")
        theirs = [f["detail"].get("features") for f in await self.frames_of(viewer, "viewer", "welcome")]
        events = await self.frames_of(viewer, "viewer", "board_event")
        note("PASS" if (lesser["capability"] == "view" and not lesser["row"] and lesser["panes"] >= 1
                        and theirs and all("board" not in (f or []) for f in theirs) and not events) else "FAIL",
             f"the second device, paired with Allow viewing ({lesser['capability']!r}), sees the"
             f" {lesser['panes']} pane(s) and no Switchboard row ({not lesser['row']}): the features of"
             f" its {len(theirs)} welcome(s) — it has reconnected through step 19 — are"
             f" {theirs[-1] if theirs else None} (no \"board\"), and it has been sent"
             f" {len(events)} board_event(s) (shot sw01c)")

    async def pair_viewer(self) -> None:
        """A second device of the owner's, paired by the same typed code with **Allow viewing**.

        Between #FR1C's steps 18 and 19, with the code the dialog is still showing from step 17 —
        that step paired by the *link*, so the code is unspent. Not a new code: the hosted
        rendezvous allows a desktop twenty rooms an hour, #FR1C's six steps ask for sixteen of
        them, and a look at the pairing dialog costs two to four.
        """
        if not ph0n.share_window(required=False):
            await self.new_code("sw00a-the-code-a-view-only-device-will-type")
        else:
            share_shot("sw00a-the-code-a-view-only-device-will-type")
        viewer = await open_phone(f"{HOSTED}/", track_sockets=True)
        self.viewer = viewer
        await viewer.wait_for("(e => !!e && !e.disabled)(document.getElementById('welcome-code-pair'))",
                              timeout=90)
        await fr1c.type_in(viewer, "welcome-code", self.letters)
        await fr1c.type_in(viewer, "welcome-pin", self.pin)
        await viewer.evaluate("document.getElementById('welcome-code-pair').click()")
        five = await soon(viewer, fr1c.FIVE_DIGITS, timeout=60)
        if not five:
            said = await viewer.evaluate(fr1c.WELCOME_NOTE)
            note("NOTE", f"the code the dialog was still showing ({self.letters}) did not pair a second"
                         f" device ({said!r}); a new one is minted")
            await self.new_code("sw00a-the-code-a-view-only-device-will-type")
            await fr1c.clear(viewer, "welcome-code")
            await fr1c.clear(viewer, "welcome-pin")
            await fr1c.type_in(viewer, "welcome-code", self.letters)
            await fr1c.type_in(viewer, "welcome-pin", self.pin)
            await viewer.evaluate("document.getElementById('welcome-code-pair').click()")
            five = await viewer.wait_for(fr1c.FIVE_DIGITS, timeout=90)
        await asyncio.sleep(2)
        xdo("windowfocus", ph0n.share_window())
        await asyncio.sleep(1)
        key("Tab", "space")                     # Refuse holds the focus; the second is Allow viewing
        await viewer.wait_for(shown("screen-inbox"), timeout=120)
        capability = await viewer.evaluate("document.getElementById('capability').textContent")
        await browser_shot(viewer, "phone", "sw00b-paired-for-viewing-only")
        note("PASS" if capability == "view" else "FAIL",
             f"a second device typed the same kind of code ({five!r} on both sides) and was let in"
             f" with Allow viewing: it reads {capability!r} (shots sw00a, sw00b)")

    async def frames_of(self, browser: Browser, who: str, kind: str) -> list[dict]:
        await harvest(browser, who)
        return [f for f in FRAMES.get(who, []) if f.get("t") == kind]

    # -- 2. the board -----------------------------------------------------------------------------------
    async def the_board(self) -> None:
        phone = self.phone
        await self.to_board()
        await asyncio.sleep(1)
        seen = await phone.evaluate(BOARD)
        await browser_shot(phone, "phone", "sw02a-the-board-waiting-on-you-first")
        # The desktop's own board, for the eye: Ctrl+Shift+S opens the Switchboard pane.
        focus_relay()
        click(*ph0n.PROMPT_BOX)
        await asyncio.sleep(0.4)
        key("ctrl+shift+s")
        await asyncio.sleep(5)
        desk("sw02b-the-desktops-own-board")
        on_desktop = desktop_counts()
        expected = expected_sections()
        stages = [(sid, count) for sid, _, count in seen["sections"] if sid != "waiting-on-you"]
        first = seen["sections"][0] if seen["sections"] else ["", "", 0]
        want_tabs = [str(t["id"]) for t in board().tabs()]
        tabs_ok = [t.lower() for t in seen["tabs"][1:]] == [t.lower() for t in want_tabs]
        note("PASS" if (seen["shown"] and first[0] == "waiting-on-you" and first[1] == "Waiting on you"
                        and first[2] == 1 and stages == expected and tabs_ok
                        and seen["rows"][0] == ["waiting-on-you", "W8TQ"]) else "FAIL",
             f"the row opens the board ({seen['name']!r}, laid out for a {seen['device']}): tabs"
             f" {seen['tabs']} (board.yaml's: {want_tabs}); \"Waiting on you\" is pinned first with"
             f" {first[2]} card ({seen['rows'][0] if seen['rows'] else None}); then the stages in the"
             f" desktop's order with its counts — the phone {stages}, the files by the desktop's own"
             f" column rules {expected} (shots sw02a, sw02b; Done starts folded, as on the desktop)")
        note("PASS" if (on_desktop and on_desktop == dict(expected)) else "NOTE",
             f"the desktop's own Switchboard pane, opened beside the terminal with Ctrl+Shift+S and"
             f" read off the window by OCR, counts {on_desktop} (shot sw02b)")

    # -- 3. the waiting card, and the answer -----------------------------------------------------------
    async def the_answer(self) -> None:
        phone = self.phone
        seen = await self.open_card("W8TQ")
        await browser_shot(phone, "phone", "sw03a-the-waiting-card-body-and-thread")
        highlighted = bool(seen["questionStyle"]) and seen["questionStyle"] != seen["commentStyle"]
        kinds = [(e["author"], e["kind"]) for e in seen["entries"]]
        note("PASS" if ("Issue" in seen["headings"] and not seen["markers"] and highlighted
                        and ("agent", "question") in kinds and len(seen["options"]) == 3
                        and seen["send"] == "Answer") else "FAIL",
             f"#W8TQ opens as {seen['title']!r} {seen['chips']}: the body by section"
             f" {seen['headings']}, no comment marker in the text ({not seen['markers']}), the thread"
             f" {kinds} with the question set apart (its left edge {seen['questionStyle']!r} against a"
             f" comment's {seen['commentStyle']!r}), its three options as buttons"
             f" ({[o[:28] for o in seen['options']]}) and the send button reading {seen['send']!r}"
             " (shot sw03a)")
        await tap(phone, ".rb-option[data-option=\"1\"]")
        prefilled = (await self.card())["reply"]
        await phone.call("Input.insertText", {"text": "Above the map, and keep it to three lines."})
        await browser_shot(phone, "phone", "sw03b-an-option-tapped-the-reply-prefilled")
        before = len(thread_on_disk("W8TQ"))
        await tap(phone, ".rb-send-comment")
        said = status_line(r"Comment on .?W8TQ from")
        desk("sw03c-the-desktop-status-line-says-who")
        await soon(phone, "[...document.querySelectorAll('.rb-thread .rb-entry')]"
                          ".some(n => n.dataset.kind === 'decision' && !n.dataset.pending)", timeout=30)
        after = await self.card()
        await browser_shot(phone, "phone", "sw03d-the-decision-in-the-thread")
        thread = thread_on_disk("W8TQ")
        last = thread[-1] if thread else None
        on_disk = (last is not None and last.kind == "decision" and "Above the map" in last.text
                   and len(thread) == before + 1)
        on_phone = [e for e in after["entries"] if e["kind"] == "decision" and "Above the map" in e["text"]]
        quoted = last is not None and last.author == "owner" and "“" in last.text
        self.device_name = (re.search(r"from (.+?):", last.text) or [None, ""])[1] if last is not None else ""
        note("PASS" if (prefilled == "1. " and on_phone and on_disk and quoted
                        and re.search(r"Comment on .?W8TQ from", said, re.I)) else "FAIL",
             f"tapping option 1 prefills the reply ({prefilled!r}); Answer puts a decision in the"
             f" thread on the phone ({bool(on_phone)}) and in .switchboard/threads/W8TQ.md ({on_disk}):"
             f" author={getattr(last, 'author', None)!r} kind={getattr(last, 'kind', None)!r}"
             f" text={getattr(last, 'text', '')[:120]!r} — the owner's words, quoted ({quoted}). The"
             f" desktop's status line, read off the window: {said!r} (shots sw03b-d)")

    # -- 4. a note, and a move --------------------------------------------------------------------------
    async def note_and_move(self) -> None:
        phone = self.phone
        await self.open_card("N2BX")
        await type_into(phone, ".rb-reply-text", "A5, two to a sheet, so the chandlery can cut them.")
        send = (await self.card())["send"]
        await tap(phone, ".rb-send-comment")
        await soon(phone, "[...document.querySelectorAll('.rb-thread .rb-entry')]"
                          ".some(n => /two to a sheet/.test(n.textContent) && !n.dataset.pending)", timeout=30)
        await browser_shot(phone, "phone", "sw04a-a-note-on-another-card")
        notes = [e for e in thread_on_disk("N2BX") if "two to a sheet" in e.text]
        note("PASS" if (send == "Comment" and len(notes) == 1 and notes[0].kind == "note") else "FAIL",
             f"on a card with no question the button reads {send!r}; the comment is in"
             f" threads/N2BX.md once ({len(notes)}) as kind={notes[0].kind if notes else None!r}"
             f" author={notes[0].author if notes else None!r} (shot sw04a)")

        await self.open_card("D4MV")
        was = card_on_disk("D4MV").status
        await tap(phone, ".rb-action-move")
        await phone.wait_for("!!document.querySelector('.rb-sheet[data-kind=\"move\"]')", timeout=10)
        await type_into(phone, ".rb-move-reason", "agreed on the quay this morning")
        await browser_shot(phone, "phone", "sw04b-the-move-sheet")
        await tap(phone, ".rb-stage[data-status=\"planned\"]")
        said = status_line(r"D4MV")
        await soon(phone, "/Moved to Planned/.test((document.querySelector('.rb-card-line') || {}).textContent || '')",
                   timeout=30)
        line = (await self.card())["line"]
        await self.to_list()
        await asyncio.sleep(1)
        listed = await phone.evaluate(BOARD)
        await browser_shot(phone, "phone", "sw04c-the-list-updated-in-place")
        await asyncio.sleep(2)
        desk("sw04d-the-desktops-board-shows-the-move")
        now = card_on_disk("D4MV")
        reasons = [e.text for e in thread_on_disk("D4MV") if "quay" in e.text]
        where = [group for group, cid in listed["rows"] if cid == "D4MV"]
        counts = desktop_counts()
        mine = dict((sid, n) for sid, _, n in listed["sections"])
        agree = bool(counts) and all(mine.get(sid) == n for sid, n in counts.items())
        toast = re.search(r"D4MV.{0,6}Planning.{0,6}Planned", " ".join(ocr().split())) is not None
        note("PASS" if (was == "planning" and now.status == "planned" and reasons and where == ["planned"]
                        and "Moved to Planned" in line and re.search(r"D4MV moved to Planned from", said)) else "FAIL",
             f"Move… with a reason: the file's front matter went {was!r} → {now.status!r}, the reason"
             f" is in the thread ({reasons[:1]}), the phone said {line!r} and its list has #D4MV under"
             f" {where} without a reload; the desktop's status line read {said!r} (shots sw04b-d)")
        note("PASS" if (agree and counts.get("planned") == 2) else "NOTE",
             f"the desktop's own Switchboard pane, open beside the terminal and read off the window by"
             f" OCR, counts {counts} — the phone's list {mine} — and its toast names the move"
             f" ({toast}); shot sw04d is the picture")
        # …and the pane is closed again, so that from here on nothing on the desktop is watching
        # the board but the bridge (step 9 leans on that).
        focus_relay()
        click(1569, 58)                                 # the Switchboard pane's own ×
        await asyncio.sleep(3)
        self.board_pane_open = "NEEDS VERIFICATION" in " ".join(ocr().split()).upper()
        if self.board_pane_open:
            note("NOTE", "the desktop's Switchboard pane did not close on its ×")
        desk("sw04e-the-desktops-switchboard-pane-closed-again")

    # -- 5. a new card ------------------------------------------------------------------------------------
    async def new_card_from_the_phone(self) -> None:
        phone = self.phone
        await self.to_list()
        words = "the tide table  should say BST or GMT,\nnobody knows which it is in October"
        known = {c.id for c in board().cards()}
        await tap(phone, ".rb-add")
        await phone.wait_for("!!document.querySelector('.rb-sheet[data-kind=\"create\"]')", timeout=10)
        await type_into(phone, ".rb-create-title", "Say which clock the tide table uses")
        await type_into(phone, ".rb-create-text", words)
        await browser_shot(phone, "phone", "sw05a-the-new-card-sheet")
        await tap(phone, ".rb-create-send")
        said = status_line(r"New card")
        ref = await soon(phone, "(() => { const t = (document.querySelector('.rb-card-ref') || {}).textContent || '';"
                                " return /^#[0-9A-Z]{4}$/.test(t) && document.querySelector('.rb').dataset.card === 'open' ? t : ''; })()",
                         timeout=40)
        await asyncio.sleep(1.5)
        await browser_shot(phone, "phone", "sw05b-the-phone-opened-the-new-card")
        made = [c for c in board().cards() if c.id not in known]
        card = made[0] if made else None
        self.new_card = card.id if card else ""
        body = card.body if card else ""
        verbatim = f"## Issue\n{words}\n" in body or f"## Issue\n\n{words}\n" in body
        note("PASS" if (card is not None and len(made) == 1 and card.status == "inbox" and verbatim
                        and ref == f"#{card.id}") else "FAIL",
             f"+ with a title and the request: one new file ({card.path.name if card else None}),"
             f" status {card.status if card else None!r}, the words verbatim under ## Issue — double"
             f" space, line break and all ({verbatim}); the phone opened it ({ref!r}); the desktop's"
             f" status line read {said!r} (shots sw05a, sw05b)")

    # -- 6. search ------------------------------------------------------------------------------------------
    async def search(self) -> None:
        phone = self.phone
        await self.to_list()
        await type_into(phone, ".rb-search", "zeppelin")
        found = await soon(phone, "(() => { const r = [...document.querySelectorAll('.rb-list .rb-row')]"
                                  ".map(n => n.dataset.cardId); return r.length ? r : null; })()", timeout=30)
        await asyncio.sleep(1)
        await browser_shot(phone, "phone", "sw06a-search-finds-a-word-in-a-body")
        titles = [c.title for c in board().cards() if "zeppelin" in c.title.lower()]
        note("PASS" if found == ["S3ZP"] and not titles else "FAIL",
             f"\"zeppelin\" is in no card's title ({titles}) and in #S3ZP's body: the search lists"
             f" {found} (shot sw06a)")
        await phone.evaluate("(() => { const s = document.querySelector('.rb-search'); s.value = '';"
                             " s.dispatchEvent(new Event('input', { bubbles: true })); return true; })()")
        await asyncio.sleep(1)

    # -- 7. Discuss, Stop, Plan --------------------------------------------------------------------------
    async def discuss_stop_plan(self) -> None:
        phone = self.phone
        await self.open_card("Q2HK")
        await type_into(phone, ".rb-reply-text", "DISCUSSIT only our three ports, or the whole coast?")
        await tap(phone, ".rb-send-discuss")
        busy = await soon(phone, "(() => { const s = document.querySelector('.rb-stop');"
                                 " return s && !s.hidden && !!document.querySelector('.rb-entry-live'); })()",
                          timeout=20)
        lamp = (await phone.evaluate(BOARD))["busy"]
        await browser_shot(phone, "phone", "sw07a-discuss-the-busy-mark")
        desk("sw07b-the-desktop-while-the-card-turn-runs")
        answered = await soon(phone, "[...document.querySelectorAll('.rb-thread .rb-entry')]"
                                     ".some(n => n.dataset.author === 'agent' && /DISCUSSED-BY-FAKE/.test(n.textContent))",
                              timeout=120)
        after = await self.card()
        await browser_shot(phone, "phone", "sw07c-the-agents-reply-as-a-thread-entry")
        disk = [e for e in thread_on_disk("Q2HK") if "DISCUSSED-BY-FAKE" in e.text]
        note("PASS" if (busy and "Q2HK" in lamp and answered and len(disk) == 1
                        and disk[0].author == "agent" and not after["stop"]) else "FAIL",
             f"Discuss with the fake model: while it runs the card shows Stop and a working line"
             f" ({bool(busy)}) and the list's row its lamp ({lamp}); the answer arrives as an agent's"
             f" thread entry on the phone ({bool(answered)}) and in threads/Q2HK.md ({len(disk)}), and"
             f" Stop goes away ({not after['stop']}) (shots sw07a-c)")

        await type_into(phone, ".rb-reply-text", "SLOWCARD take all the time you like")
        await tap(phone, ".rb-send-discuss")
        running = await soon(phone, "(() => { const s = document.querySelector('.rb-stop'); return s && !s.hidden; })()",
                             timeout=20)
        await asyncio.sleep(5)
        await browser_shot(phone, "phone", "sw07d-a-long-turn-with-stop")
        started = time.monotonic()
        await tap(phone, ".rb-stop")
        stopped = await soon(phone, "(() => { const s = document.querySelector('.rb-stop'); return !s || s.hidden; })()",
                             timeout=30)
        took = time.monotonic() - started
        line = (await self.card())["line"]
        await browser_shot(phone, "phone", "sw07e-stopped")
        slow = sum(1 for row in (OUT / "requests.jsonl").read_text().splitlines()
                   if json.loads(row).get("scene") == "SLOWCARD")
        note("PASS" if (running and stopped and "Stopped" in line and slow == 1) else "FAIL",
             f"Stop on a turn the model would have kept up for two minutes (the fake model saw it"
             f" {slow} time): the phone said {line!r} {took:.1f} s after the tap and the busy mark went"
             f" ({bool(stopped)}) (shots sw07d, sw07e)")

        seen = await self.open_card("P7AN")
        plan_before = next((t for h, t in seen["sections"] if h == "Plan"), "")
        file_before = card_on_disk("P7AN").body
        await type_into(phone, ".rb-reply-text", "PLANIT P7AN say what happens when the feed is down")
        await tap(phone, ".rb-send-plan")
        await soon(phone, "(() => { const s = document.querySelector('.rb-stop'); return s && !s.hidden; })()",
                   timeout=20)
        await browser_shot(phone, "phone", "sw07f-plan-running")
        changed = await soon(phone, "[...document.querySelectorAll('.rb-card-scroll .rb-section-body')]"
                                    ".some(n => /PLANNED-BY-FAKE/.test(n.textContent))", timeout=120)
        await soon(phone, "(() => { const s = document.querySelector('.rb-stop'); return !s || s.hidden; })()",
                   timeout=90)
        after = await self.card()
        plan_after = next((t for h, t in after["sections"] if h == "Plan"), "")
        await browser_shot(phone, "phone", "sw07g-the-plan-section-changed")
        file_after = card_on_disk("P7AN").body
        note("PASS" if (changed and "PLANNED-BY-FAKE" in file_after and "PLANNED-BY-FAKE" not in file_before
                        and "Serve the stale entry" in plan_after and plan_after != plan_before) else "FAIL",
             f"Plan on #P7AN: the card's ## Plan changed on the phone ({bool(changed)}; it now ends"
             f" {plan_after[-60:]!r}) and in the file ({'PLANNED-BY-FAKE' in file_after}); the card is"
             f" now {card_on_disk('P7AN').status!r} (shots sw07f, sw07g)")

    # -- 8. Execute and Verify ---------------------------------------------------------------------------
    async def execute_and_verify(self) -> None:
        phone = self.phone
        panes_before = (await self.inbox_panes())
        seen = await self.open_card("P7AN")
        await tap(phone, ".rb-action-execute")
        said = status_line(r"Execute on .?P7AN from")
        result = await soon(phone, "(() => { const t = (document.querySelector('.rb-card-line .rb-line-text') || {}).textContent || '';"
                                   " return /pane/i.test(t) && !/Asking/.test(t) ? t : ''; })()", timeout=60)
        after = await self.card()
        await browser_shot(phone, "phone", "sw08a-executing-in-a-new-pane")
        await asyncio.sleep(6)
        desk("sw08b-the-new-pane-on-the-desktop")
        card = card_on_disk("P7AN")
        session = str(card.front.get("session") or "")
        progress = [e.text for e in thread_on_disk("P7AN") if "Execute pressed" in e.text]
        await tap(phone, ".rb-card-line .rb-line-action")
        opened = await soon(phone, shown("terminal-pane"), timeout=40)
        handed = await ph0n.terminal_has(phone, "EXECUTING-BY-FAKE P7AN", timeout=90)
        await browser_shot(phone, "phone", "sw08c-the-link-opened-that-pane")
        panes_after = await self.inbox_panes()
        scenes = [json.loads(r).get("scene") for r in (OUT / "requests.jsonl").read_text().splitlines()]
        note("PASS" if (result and "executing in a new pane" in result.lower() and after["lineAction"] == "Open pane"
                        and card.status == "executing" and session and progress and opened and handed
                        and "EXECUTE:P7AN" in scenes and panes_after == panes_before + 1
                        and re.search(r"Execute on .?P7AN from", said, re.I)) else "FAIL",
             f"Execute from the phone (the button row was {seen['actions']}): the phone says"
             f" {result!r} with an {after['lineAction']!r} link; the desktop's status line read"
             f" {said!r}; the file is now status={card.status!r} session={session[:8]!r}… with"
             f" {progress[:1]} in its thread; the inbox went {panes_before} → {panes_after} panes; the"
             f" link opened that pane on the phone ({bool(opened)}), where its agent had been handed"
             f" the card and answered for it ({handed}; the fake model's scenes include EXECUTE:P7AN:"
             f" {'EXECUTE:P7AN' in scenes}) (shots sw08a-c)")

        seen = await self.open_card("V6RF")
        offered = [name for name, _ in seen["actions"]]
        await tap(phone, ".rb-action-verify")
        result = await soon(phone, "(() => { const t = (document.querySelector('.rb-card-line .rb-line-text') || {}).textContent || '';"
                                   " return t && !/Asking/.test(t) ? t : ''; })()", timeout=60)
        after = await self.card()
        await browser_shot(phone, "phone", "sw08d-verify-from-the-phone")
        await asyncio.sleep(4)
        desk("sw08e-the-desktop-after-verify")
        if after["lineAction"] == "Open pane":
            scenes = [json.loads(r).get("scene") for r in (OUT / "requests.jsonl").read_text().splitlines()]
            await asyncio.sleep(8)
            scenes = [json.loads(r).get("scene") for r in (OUT / "requests.jsonl").read_text().splitlines()]
            note("PASS" if "VERIFY:V6RF" in scenes else "FAIL",
                 f"Verify on #V6RF (needs-verification; the row offered {offered}): {result!r}, and the"
                 f" verifier's pane was handed the card ({'VERIFY:V6RF' in scenes}) (shots sw08d, sw08e)")
        else:
            qa = subprocess.run([sys.executable, str(SOURCE / "scripts/relay-board.py"), "verifier", "V6RF"],
                                cwd=WORK, capture_output=True, text=True).stdout.strip().splitlines()
            note("PASS" if (result and "Verify" in offered) else "FAIL",
                 f"Verify on #V6RF (needs-verification; the row offered {offered}) reached the desktop"
                 f" and came back refused in a line, not a spinner: {result!r}. No verifier can be"
                 f" configured in this throwaway profile: #V6RF was implemented by openai/…, the only"
                 f" model here is the fake local endpoint, PATH holds no claude or codex on purpose"
                 f" (drive.sh) and no provider key exists. relay-board.py verifier says: {qa[:1]}"
                 " (shots sw08d, sw08e). A pane opening for Verify is the owner's to see, with his keys")

    async def inbox_panes(self) -> int:
        await self.to_inbox()
        await asyncio.sleep(1)
        return int(await self.phone.evaluate("document.querySelectorAll('.pane-row').length"))

    # -- 9. card_waiting ------------------------------------------------------------------------------------
    async def card_waiting(self) -> None:
        phone = self.phone
        # The desktop's own Switchboard pane was closed at the end of step 4: what tells the hub
        # about a file an agent wrote is then the bridge's own watch (src/BoardRemote.cpp), which
        # is the harder case and the one a desktop left alone all day is in.
        await self.to_inbox()
        before = await phone.evaluate(INBOX)
        raw = await phone.evaluate("""
            (async () => {
              const db = await new Promise((res, rej) => {
                const r = indexedDB.open('relay-remote', 1);
                r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error);
              });
              const key = await new Promise((res) => {
                const r = db.transaction('device').objectStore('device').get('push-key');
                r.onsuccess = () => res(r.result); r.onerror = () => res(null);
              });
              if (!(key instanceof CryptoKey)) return '';
              const bytes = new Uint8Array(await crypto.subtle.exportKey('raw', key));
              return btoa(String.fromCharCode(...bytes));
            })()
        """)
        seal_key = base64.b64decode(raw) if raw else b""
        # Presence: a second X client takes the focus, which is what an alt-tab does.
        away = subprocess.Popen(["xmessage", "-geometry", "320x90+2000+1150", "-name", "away",
                                 "Relay is not the window you are looking at"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            await asyncio.sleep(3)
            for window in xdo("search", "--name", "away").split():
                xdo("windowraise", window)
                xdo("windowfocus", window)
            await asyncio.sleep(3)
            focused = xdo("getwindowfocus")
            pushes_before = len(tap_rows(dir="push"))
            # The way an agent does it: the card's front matter, and its question in the thread.
            b = board()
            card = b.card_by_id("R4SK")
            card.set("waiting_on", "owner")
            b.save(card)
            b.append_thread("R4SK", "Is the feed in UTC or in local time? I cannot tell from one day of data.",
                            author="agent", kind="question", model="fake")
            sent = []
            for _ in range(80):
                sent = tap_rows(dir="push")[pushes_before:]
                if sent:
                    break
                await asyncio.sleep(0.5)
            counted = await soon(phone, "(() => { const c = document.querySelector('#board-row .rb-inbox-chip');"
                                        f" return c && !c.hidden && c.textContent !== {json.dumps(before['chip'])}"
                                        " ? c.textContent : ''; })()", timeout=40)
            after = await phone.evaluate(INBOX)
            await browser_shot(phone, "phone", "sw09a-the-row-count-went-up")
        finally:
            away.terminate()
            focus_relay()
        body = None
        if sent and seal_key:
            try:
                body = push.open_sealed(seal_key, receive(SUBSCRIPTION["private"], SUBSCRIPTION["auth"],
                                                          base64.b64decode(sent[0]["payload"])))
            except Exception as error:                                    # noqa: BLE001
                note("NOTE", f"the push payload did not open: {type(error).__name__}: {error}")
        if body is not None:
            (OUT / "push-body.json").write_text(json.dumps(body, indent=2) + "\n")
        title = card_on_disk("R4SK").title
        words = json.dumps(body or {})
        leaked = [w for w in (title, "UTC", "feed", str(WORK)) if w in words]
        n_before = int((re.match(r"\d+", before["chip"] or "0") or [0])[0])
        n_after = int((re.match(r"\d+", counted or "0") or [0])[0])
        note("PASS" if (body and body.get("kind") == "card_waiting" and body.get("card") == "R4SK"
                        and body.get("pane") == "" and not leaked and len(sent) == 1
                        and n_after == n_before + 1 and focused != ph0n.WIN) else "FAIL",
             f"with another window focused ({focused} is not Relay's {ph0n.WIN}) and the desktop's"
             f" Switchboard pane {'closed' if not self.board_pane_open else 'OPEN (it did not close in step 4)'}, #R4SK was made to wait on the owner on disk the way an agent does it. The"
             f" hub handed the push service {len(sent)} push; opened with the stub subscription's key"
             f" and the page's seal key it is {body} — the kind, the card's id, no title and none of"
             f" the question (leaked: {leaked}). The phone's row went {before['chip']!r} →"
             f" {counted!r} and the badge {before['badge']!r} → {after['badge']!r} (shot sw09a)")
        reply = (sent[0].get("reply") or sent[0].get("error")) if sent else None
        note("NOTE", f"the push went to the real rendezvous's /v1/push/send, which answered {reply!r}: the"
                     " stub subscription's endpoint (push.invalid) is not a Web Push service, and the"
                     " hosted rendezvous posts to nothing else. Delivery to a lock screen is the real"
                     " phone's to show; the body above is what it would be handed")

        # The tap, both ways in (app/sw.js `notificationclick`): a page that is open is posted the
        # card's id; one that is not is opened at `?card=`.
        await phone.evaluate("(() => { navigator.serviceWorker.dispatchEvent(new MessageEvent('message',"
                             " { data: { t: 'open_card', card: 'R4SK' } })); return true; })()")
        posted = await soon(phone, f"{shown('screen-board')} && (document.querySelector('.rb-card-ref') || {{}}).textContent === '#R4SK'",
                            timeout=30)
        await asyncio.sleep(1.5)
        await browser_shot(phone, "phone", "sw09b-the-tap-opened-that-card")
        await harvest(phone, "phone")
        await phone.navigate(f"{HOSTED}/?card=R4SK")
        cold = await soon(phone, f"{shown('screen-board')} && (document.querySelector('.rb-card-ref') || {{}}).textContent === '#R4SK'"
                                 " && document.querySelectorAll('.rb-thread .rb-entry').length > 0", timeout=90)
        query = await phone.evaluate("location.search")
        await asyncio.sleep(1)
        await browser_shot(phone, "phone", "sw09c-opened-cold-at-the-card")
        note("PASS" if (posted and cold and query == "") else "FAIL",
             f"tapping the notification opens that card: posted to the open page as the service"
             f" worker posts it ({bool(posted)}), and opened cold at ?card=R4SK ({bool(cold)}, the"
             f" query dropped again: {query == ''}) (shots sw09b, sw09c). The tap itself is the one"
             " thing stubbed here: a headless page has no notification to tap")

    # -- 10. offline ------------------------------------------------------------------------------------------
    async def offline(self) -> None:
        phone = self.phone
        await self.open_card("N2BX")
        await phone.call("Network.enable")
        await phone.call("Network.emulateNetworkConditions",
                         {"offline": True, "latency": 0, "downloadThroughput": -1, "uploadThroughput": -1})
        cut = await phone.evaluate("""
            (() => { const live = window.__relaySockets.filter(s => s.readyState === WebSocket.OPEN);
                     live.forEach(s => s.close(4000, 'tunnel'));
                     return live.length; })()
        """)
        link = await soon(phone, f"['offline', 'dropped'].includes({LINK}) ? {LINK} : ''", timeout=20)
        await type_into(phone, ".rb-reply-text", "OFFLINE-NOTE laminate them, the quay is wet")
        await tap(phone, ".rb-send-comment")
        await asyncio.sleep(1.5)
        during = await self.card()
        strip = (await phone.evaluate(BOARD))["offline"]
        await browser_shot(phone, "phone", "sw10a-written-offline-sends-when-back-online")
        on_disk_during = sum(1 for e in thread_on_disk("N2BX") if "OFFLINE-NOTE" in e.text)
        await phone.call("Network.emulateNetworkConditions",
                         {"offline": False, "latency": 0, "downloadThroughput": -1, "uploadThroughput": -1})
        back = await soon(phone, f"{LINK} === 'connected' ? 'connected' : ''", timeout=90)
        for _ in range(60):
            if any("OFFLINE-NOTE" in e.text for e in thread_on_disk("N2BX")):
                break
            await asyncio.sleep(0.5)
        await asyncio.sleep(8)                          # room for a second delivery to show up
        after = await self.card()
        strip_after = (await phone.evaluate(BOARD))["offline"]
        await browser_shot(phone, "phone", "sw10b-back-online-and-landed-once")
        landed = sum(1 for e in thread_on_disk("N2BX") if "OFFLINE-NOTE" in e.text)
        pending = [e for e in during["entries"] if e["pending"] and "OFFLINE-NOTE" in e["text"]]
        shown_after = [e for e in after["entries"] if "OFFLINE-NOTE" in e["text"]]
        note("PASS" if (cut and "sends when back online" in during["line"] and "sends when back online" in strip
                        and pending and on_disk_during == 0 and back == "connected" and landed == 1
                        and len(shown_after) == 1 and not shown_after[0]["pending"] and not strip_after) else "FAIL",
             f"{cut} socket cut and the page taken offline (link {link!r}): a comment written then"
             f" reads {during['line']!r}, the strip {strip!r}, and it waits in the thread marked as"
             f" pending ({bool(pending)}) with nothing on disk ({on_disk_during}); back online"
             f" ({back!r}) it is in threads/N2BX.md exactly once ({landed}) and once on the phone"
             f" ({len(shown_after)}, no longer pending), the strip cleared ({strip_after!r})"
             " (shots sw10a, sw10b)")

    # -- 11. a guest and a view device ----------------------------------------------------------------------
    async def guests_get_nothing(self) -> None:
        phone = self.phone
        viewer = self.viewer
        await self.to_inbox()
        await ph0n.open_pane_row(phone, 0)
        # The share window, from the plug menu: with four panes across the window the palette's
        # "Share this pane" is not where #PH0N's helper clicks for it.
        if not ph0n.share_window(required=False):
            fr1c.plug_menu(1)
            key("Return")
            for _ in range(60):
                if ph0n.share_window(required=False):
                    break
                await asyncio.sleep(1)
        ph0n.park_share_window()
        await asyncio.sleep(3)
        rows = ph0n.share_rows()
        if len(rows) < 3:
            raise RuntimeError(f"the share window's rows did not read: {rows}")
        invite_row = rows[1]
        ph0n.share_click(65, invite_row)
        await asyncio.sleep(1)
        key("Down", "Return")                           # Viewer → Editor: the most a guest can be
        await asyncio.sleep(1)
        ph0n.share_click(441, invite_row)               # Make a link
        await asyncio.sleep(6)
        share_shot("sw11a-an-editor-invite-to-one-pane")
        invite = ph0n.read_url("invite-url.txt")
        if not invite:
            raise RuntimeError("no invite url was written")
        ph0n.close_share_window()
        guest = await open_phone(invite, size={"width": 430, "height": 900, "deviceScaleFactor": 2, "mobile": True})
        self.guest = guest
        await guest.wait_for(shown("screen-join"), timeout=90)
        await guest.wait_for("document.getElementById('join-fingerprint').textContent !== '…'", timeout=30)
        await guest.evaluate("(() => { document.getElementById('join-name').value = 'alice'; return true; })()")
        await press(guest, "join-knock")
        await guest.wait_for("document.getElementById('knock-code')?.textContent?.match(/^\\d{5}$/) ? 1 : 0",
                             timeout=120)
        found = ""
        panes = int(await phone.evaluate("document.querySelectorAll('.pane-row').length")) or 3
        for index in range(max(panes, 1)):
            found = await soon(phone, "(() => { const r = document.querySelector('.rp-ask-row[data-kind=\"knock\"]');"
                                      " return r ? r.textContent : ''; })()", timeout=10) or ""
            if found:
                break
            await ph0n.back_to_inbox(phone)
            await ph0n.open_pane_row(phone, (index + 1) % max(panes, 1))
        await ph0n.click_first(phone, '.rp-ask-row[data-kind="knock"] .rp-ask-yes')
        admitted = await soon(guest, shown("screen-guest"), timeout=60)
        await asyncio.sleep(3)
        await browser_shot(guest, "guest", "sw11b-alice-admitted-to-one-pane")

        # Each asks for the board from inside its own session: no button on either page would.
        ask = ("(async () => { try { await window.__relayRrp.send({ t: 'board_request', rid: 7101, id: 7101,"
               " request: { type: 'board_comment', id: 'W8TQ', text: 'SNOOP-TEXT let me in' } }); return 'sent'; }"
               " catch (error) { return 'threw ' + error.message; } })()")
        asked = {"guest": await guest.evaluate(ask), "viewer": await viewer.evaluate(ask)}
        await asyncio.sleep(3)
        # …and the board changes while both are connected: a note from the owner's phone.
        await self.open_card("Q2HK")
        await type_into(phone, ".rb-reply-text", "Ours only. Decided.")
        await tap(phone, ".rb-send-comment")
        await soon(phone, "[...document.querySelectorAll('.rb-thread .rb-entry')]"
                          ".some(n => /Ours only/.test(n.textContent) && !n.dataset.pending)", timeout=30)
        await asyncio.sleep(4)
        await browser_shot(guest, "guest", "sw11c-the-guest-after-the-board-changed")
        await browser_shot(viewer, "phone", "sw11d-the-view-device-after-the-board-changed")

        verdicts = {}
        for who, browser in (("guest", guest), ("viewer", viewer)):
            await harvest(browser, who)
            frames = FRAMES.get(who, [])
            events = [f for f in frames if f["t"] == "board_event"]
            refusals = [f["detail"] for f in frames if f["t"] == "error"
                        and f["detail"].get("code") == "not_permitted"]
            verdicts[who] = (len(events), [r.get("message") for r in refusals][-1:])
        tapped = [r for r in tap_rows(dir="out", t="board_event") if r.get("sent")]
        wrong = [r for r in tapped if r.get("kind") != "device" or r.get("cap") != "full"]
        asked_rows = [(r["kind"], r["cap"], r["request"]) for r in tap_rows(dir="in") if r.get("rid") == 7101]
        audit = fr1c.audit_rows("board_refused")
        gate = [r for r in audit if r.get("code") == "not_permitted"]
        leak = "SNOOP-TEXT" in fr1c.audit_text() or "W8TQ" in json.dumps(gate)
        by_guest = [r for r in gate if r.get("participant")]
        by_view = [r for r in gate if r.get("device")]
        note("PASS" if (admitted and asked == {"guest": "sent", "viewer": "sent"}
                        and verdicts["guest"][0] == 0 and verdicts["viewer"][0] == 0
                        and verdicts["guest"][1] and verdicts["viewer"][1] and tapped and not wrong
                        and len(asked_rows) == 2 and by_guest and by_view and not leak) else "FAIL",
             f"a guest admitted to one pane as an editor ({bool(admitted)}) and the view-only device"
             f" each sent board_request {{board_comment}} from its own session ({asked}; the hub's"
             f" tap saw {asked_rows}). On the wire each was answered with an error and nothing else —"
             f" guest: {verdicts['guest'][1]}, view: {verdicts['viewer'][1]} — and while the owner's"
             f" phone then wrote a note, the guest's page decrypted {verdicts['guest'][0]}"
             f" board_event(s) and the view device's {verdicts['viewer'][0]}; of the {len(tapped)}"
             f" board_events the hub sent in this run, {len(wrong)} went to anything but a full"
             f" device. The audit log has board_refused for both"
             f" ({[{k: v for k, v in r.items() if k != 'at'} for r in gate]}) with the type and no"
             f" text or card ({not leak}) (shots sw11a-d)")

    def rooms_note(self) -> None:
        posts = [r for r in tap_rows(dir="post") if r.get("path") == "/v1/rooms"]
        refused = [r for r in posts if r.get("error")]
        note("PASS" if not refused else "FAIL",
             f"the run asked the hosted rendezvous for {len(posts)} room(s) — a pairing link, a pairing"
             f" code and an invite are one each — of the twenty an hour it allows a desktop; refused:"
             f" {[r['error'][:80] for r in refused]}")

    # -- 12. no machine path --------------------------------------------------------------------------------
    async def no_paths(self) -> None:
        await harvest(self.phone, "phone")
        await harvest(self.viewer, "viewer")
        await harvest(self.guest, "guest")
        frames = FRAMES.get("phone", [])
        by_type: dict[str, list[int]] = {}
        for frame in frames:
            row = by_type.setdefault(frame["t"], [0, 0, 0])
            row[0] += 1
            row[1] += frame["size"]
            row[2] += 1 if frame["hits"] else 0
        # What this card added to the wire is `board_event` (and the errors that answer a
        # `board_request`): that is what must never name a file. The pane protocol is older and
        # says otherwise on purpose — `panes` carries each pane's `cwd` (section 6.3, the inbox's
        # right-hand label), and the terminal's own picture is the owner's shell, prompt and all.
        strict = {"board_event", "error", "welcome", "push_state"}
        dirty = {t: row for t, row in by_type.items() if row[2] and t in strict}
        board_frames = [f for f in frames if f["t"] == "board_event"]
        board_hits = [f["hits"] for f in board_frames if f["hits"]]
        with open(OUT / "phone-board-frames.jsonl", "w", encoding="utf-8") as out:
            for frame in board_frames:
                out.write(json.dumps({"at": frame["at"], **frame["detail"]}, ensure_ascii=False) + "\n")
        summary = "\n".join(f"{t:24} {row[0]:6} frames {row[1]:10} bytes  {row[2]:4} with a path"
                            for t, row in sorted(by_type.items()))
        elsewhere: dict[str, str] = {}
        for frame in frames:
            if frame["hits"] and frame["t"] not in strict and frame["t"] != "message":
                name = frame["t"] + (f" {frame['sub']}" if frame.get("sub") else "")
                elsewhere.setdefault(name, " ".join(str(frame.get("where") or "").split()))
        (OUT / "phone-frames-summary.txt").write_text(
            f"every message the phone's page decrypted in this run, by type (`message` is rrp.js's\n"
            f"catch-all: every frame again); a path is any of the project's ({WORK}), the jail's,\n"
            f"\"/home/\" or \"/tmp/\"\n\n{summary}\n\nwhere a path was, outside the Switchboard's messages"
            f" (one sample per type):\n" + "".join(f"  {t}: …{w}…\n" for t, w in sorted(elsewhere.items())))
        keys = set()
        for frame in board_frames:
            keys.update(re.findall(r'"(path|paths|root|folder|file|files|workspace|cwd|dir|directory|repo|project)":',
                                   json.dumps(frame["detail"])))
        kinds = sorted({f["detail"].get("event", {}).get("event", "?") for f in board_frames})
        note("PASS" if (board_frames and not board_hits and not dirty and not keys) else "FAIL",
             f"the phone's page decrypted {len(frames)} messages in this run, {len(board_frames)} of"
             f" them board_events ({sum(f['size'] for f in board_frames)} bytes, kept whole in"
             f" phone-board-frames.jsonl; kinds {kinds}): none holds the project's path, the jail's,"
             f" \"/home/\" or \"/tmp/\" ({len(board_hits)} hits), none has a path-named key"
             f" ({sorted(keys)}), and the errors, the welcome and push_state have none either"
             f" ({dirty or 'none'})")
        if elsewhere:
            note("NOTE", f"outside the Switchboard's messages the pane protocol does name this machine's"
                         f" paths, as it did before this card: {sorted(elsewhere)} —"
                         " `panes[].cwd` is section 6.3's (the inbox's right-hand label) and the rest is"
                         " the terminal's own picture and the pane agent's events, which a `full`"
                         " device of the owner's is paired to see. phone-frames-summary.txt has a"
                         " sample of each")

    # -- 13. the iPad, and a phone on its side with the keyboard up ---------------------------------
    async def layouts(self) -> None:
        phone = self.phone
        await self.to_list()
        await phone.call("Emulation.setDeviceMetricsOverride", IPAD)
        await asyncio.sleep(1.5)
        await tap(phone, ".rb-list .rb-row[data-card-id=\"W8TQ\"]")
        await phone.wait_for("(document.querySelector('.rb-card-ref') || {}).textContent === '#W8TQ'", timeout=30)
        await asyncio.sleep(1.5)
        ipad = await phone.evaluate("""
            (() => { const box = (s) => { const n = document.querySelector(s);
                       const r = n ? n.getBoundingClientRect() : null;
                       return r && r.width > 0 && r.height > 0 ? [Math.round(r.left), Math.round(r.right)] : null; };
                     return { device: document.querySelector('.rb').dataset.device,
                              list: box('.rb-list-col'), card: box('.rb-card-col'),
                              rows: document.querySelectorAll('.rb-list .rb-row').length,
                              overflow: document.documentElement.scrollWidth > innerWidth }; })()
        """)
        await browser_shot(phone, "phone", "sw13a-ipad-1180x820-list-and-card-side-by-side")
        side_by_side = bool(ipad["list"] and ipad["card"] and ipad["list"][1] <= ipad["card"][0] + 1)
        note("PASS" if (ipad["device"] == "tablet" and side_by_side and not ipad["overflow"]) else "FAIL",
             f"at 1180×820 the board lays itself out for a {ipad['device']}: the list at x"
             f" {ipad['list']} and the open card at x {ipad['card']}, side by side ({side_by_side}),"
             f" nothing wider than the screen ({not ipad['overflow']}) (shot sw13a)")

        await phone.call("Emulation.setDeviceMetricsOverride", LANDSCAPE_KEYBOARD)
        await asyncio.sleep(1)
        await phone.evaluate("(() => { document.querySelector('.rb-reply-text').focus(); return true; })()")
        await asyncio.sleep(1.5)
        fits = await phone.evaluate("""
            (() => { const inside = (s) => { const n = document.querySelector(s);
                       if (!n || n.hidden) return null; const r = n.getBoundingClientRect();
                       return r.width > 0 && r.top >= 0 && r.bottom <= innerHeight + 0.5
                         && r.left >= 0 && r.right <= innerWidth + 0.5; };
                     return { box: inside('.rb-reply-text'), send: inside('.rb-send-comment'),
                              discuss: inside('.rb-send-discuss'), plan: inside('.rb-send-plan'),
                              height: innerHeight }; })()
        """)
        await browser_shot(phone, "phone", "sw13b-landscape-with-the-keyboard-844x185")
        note("PASS" if (fits["box"] and fits["send"]) else "FAIL",
             f"at 844×{fits['height']} — a phone on its side with the keyboard up — the reply box"
             f" ({fits['box']}) and its send ({fits['send']}) are wholly on screen; Discuss"
             f" ({fits['discuss']}) and Plan ({fits['plan']}) too (shot sw13b)")
        await phone.call("Emulation.setDeviceMetricsOverride", ph0n.PHONE)
        await asyncio.sleep(1)

    async def go_pairing(self) -> None:
        """#FR1C's `go_new_steps`, step for step, with one thing between 18 and 19: the second,
        view-only device pairs while the dialog's code is still good (`pair_viewer`), so that it
        too is a device that goes through step 19's off and on."""
        await step("14 the plug menu pairs", self.plug_menu_pairs())
        await step("15 the typed code", self.typed_code_pairs())
        await step("16 a burned code", self.wrong_pins_burn())
        await step("17 iOS installs first", self.ios_installs_first())
        await step("18 notifications offered", self.first_arrival_offer())
        await step("sw0 a view-only device", self.pair_viewer())
        await step("19 the plug switch", self.plug_switch())
        short_lived = [row["code"] for row in fr1c.audit_rows("code_expired")]
        minted = [row["code"] for row in fr1c.audit_rows("code_create")]
        note("PASS" if not short_lived else "FAIL",
             f"every code the dialog showed stayed the code: {len(minted)} minted ({minted}), none"
             f" ended by the service's move to join.relay-terminal.ai (code_expired: {short_lived})")

    async def go_switchboard(self) -> None:
        for name, run in (("sw1 the inbox row", self.inbox_row), ("sw2 the board", self.the_board),
                          ("sw3 the answer", self.the_answer), ("sw4 a note and a move", self.note_and_move),
                          ("sw5 a new card", self.new_card_from_the_phone), ("sw6 search", self.search),
                          ("sw7 discuss, stop, plan", self.discuss_stop_plan),
                          ("sw8 execute and verify", self.execute_and_verify),
                          ("sw9 card_waiting", self.card_waiting), ("sw10 offline", self.offline),
                          ("sw11 a guest and a view device", self.guests_get_nothing),
                          ("sw12 no machine path", self.no_paths), ("sw13 layouts", self.layouts)):
            if not relay_alive():
                crashes = (OUT / "relay-stderr.log").read_text(errors="replace").count("gui_crash")
                note("FAIL", f"Relay is no longer running before {name!r} (gui_crash lines in its"
                             f" stderr: {crashes}); the steps after it were not walked")
                break
            await step(name, run())
        self.rooms_note()

    async def close(self) -> None:
        if self.viewer is not None:
            await self.viewer.stop()
        # What the hub handed each channel, for the record (never a text: sidecar_tap.py).
        rows = [r for r in tap_rows() if r.get("dir") in ("in", "out", "push", "post")]
        for row in rows:
            if row.get("dir") == "push":
                row["payload"] = f"<{len(row.get('payload') or '')} base64 characters of ciphertext>"
        (OUT / "hub-tap.jsonl").write_text("".join(json.dumps(r) + "\n" for r in rows))
        board_dir = OUT / "board-after"
        board_dir.mkdir(exist_ok=True)
        for old in board_dir.glob("*.md"):
            old.unlink()
        for card_id in ("W8TQ", "N2BX", "D4MV", "Q2HK", "P7AN", "R4SK", self.new_card):
            card = card_on_disk(card_id) if card_id else None
            if card is None:
                continue
            (board_dir / f"{card_id}.md").write_text(card.path.read_text())
            thread = board().thread_path(card_id)
            if thread.exists():
                (board_dir / f"{card_id}.thread.md").write_text(thread.read_text())
        await super().close()


async def main() -> int:
    drive = Swph()
    try:
        await drive.go_pairing()                        # #FR1C's 14-19: the pairing this run stands on
        await drive.go_switchboard()
    finally:
        await drive.close()
    failures = [line for line in ph0n.NOTES if line.startswith("FAIL")]
    print(f"\n{len(ph0n.NOTES)} notes, {len(failures)} failures", flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
