# SPDX-License-Identifier: AGPL-3.0-or-later
"""#FR1C task 4, driven once against the real hosted rendezvous (drive.sh runs this).

Two halves, one command, one jail:

* **Steps 14-19**, this card's own path, on a fresh profile with remote control off: the plug
  menu's "Pair a phone…" turns the service on and shows the code; the phone opens
  `https://join.relay-terminal.ai/` and *types* it; a burned code; iOS install-first; the
  notifications offer on first arrival; and the plug menu's own switch, off and on again.
* Then the profile is wiped and **#PH0N's thirteen steps** run again on the same build, because
  this card changed the desktop entry point their third step uses and the harness is shared.

Everything under the two halves — the isolation, the fake model, the xdotool desktop, the
headless phones, every helper — is #PH0N's `run.py`, loaded here by path rather than copied, so
there is one harness to keep right. What this file adds is the six new steps and the wipe.

Every step writes what it saw to notes.txt with PASS/FAIL/NOTE in front of it, and a step that
throws is recorded and the run carries on: one run should surface every problem, not the first.
"""
from __future__ import annotations

import asyncio
import importlib.util
import json
import os
import re
import shutil
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))

# The #PH0N harness, by path: its module-level code reads the RELAY_QA_* variables drive.sh sets,
# so every helper in it writes into *this* folder.
PH0N = Path(os.environ.get("RELAY_QA_PH0N",
                           ROOT / "docs/qa_evidence/2026-09-21-ph0n-hosted-drive")) / "run.py"
_spec = importlib.util.spec_from_file_location("ph0n_drive", PH0N)
ph0n = importlib.util.module_from_spec(_spec)
sys.modules["ph0n_drive"] = ph0n
_spec.loader.exec_module(ph0n)

from tests.browser import SCREENS_SHOWN, Browser, shown                  # noqa: E402

OUT, JAIL, HOSTED = ph0n.OUT, ph0n.JAIL, ph0n.HOSTED
note, desk, share_shot, root_shot = ph0n.note, ph0n.desk, ph0n.share_shot, ph0n.root_shot
browser_shot, open_phone, press, step = ph0n.browser_shot, ph0n.open_phone, ph0n.press, ph0n.step
key, xdo, click, focus_relay = ph0n.key, ph0n.xdo, ph0n.click, ph0n.focus_relay

# The plug, in the window chrome at the top right, with the window at 1600x1000 (start_relay).
PLUG = (1481, 26)
# An iPhone's Safari, and the half of `installedApp()` a headless Chrome cannot reach on its own.
# Copied from tests/test_remote_browser.py, which is where the install-first behaviour is pinned.
IPHONE_UA = ("Mozilla/5.0 (iPhone; CPU iPhone OS 17_5 like Mac OS X) AppleWebKit/605.1.15 "
             "(KHTML, like Gecko) Version/17.5 Mobile/15E148 Safari/604.1")
STANDALONE = ("Object.defineProperty(navigator, 'standalone',"
              " { configurable: true, get: () => true });")
# What Safari itself answers on an iPhone whose Relay is *not* on the Home Screen: the property
# exists and is false. (Chrome has no such property at all; the page must not pair on either.)
NOT_STANDALONE = ("Object.defineProperty(navigator, 'standalone',"
                  " { configurable: true, get: () => false });")

WELCOME_NOTE = "document.getElementById('welcome-code-note').textContent"
FIVE_DIGITS = ("(() => { const n = document.getElementById('pair-code');"
               " return n && /^\\d{5}$/.test(n.textContent) ? n.textContent : ''; })()")


async def type_in(browser: Browser, element_id: str, text: str) -> None:
    """Type as a thumb does, one key at a time, through the page's own input handlers."""
    await browser.evaluate(f"document.getElementById({element_id!r}).focus()")
    for character in text:
        await browser.call("Input.insertText", {"text": character})


async def clear(browser: Browser, element_id: str) -> None:
    await browser.evaluate(f"(() => {{ document.getElementById({element_id!r}).value = '';"
                           f" return true; }})()")


def plug_menu(row: int) -> None:
    """Open the plug menu and choose the `row`-th *choosable* row (1 = the first item).

    A QMenu's arrow keys skip the disabled status line and the separators, so "first item" is
    literally one Down — which is the claim step 14 makes, driven rather than read off a picture.
    """
    focus_relay()
    click(*PLUG)
    time.sleep(1.5)
    key(*(["Down"] * row))
    time.sleep(0.4)


def open_plug_menu_for_a_shot(what: str) -> str:
    focus_relay()
    click(*PLUG)
    time.sleep(1.5)
    name = root_shot(what)
    key("Escape")
    time.sleep(1)
    return name


async def allow_typing() -> None:
    """"Allow typing" in the share window's approval row (Refuse holds the focus; third is Allow)."""
    xdo("windowfocus", ph0n.share_window())
    await asyncio.sleep(1)
    key("Tab", "Tab", "space")


def audit_text() -> str:
    """Every audit line the desktop has written so far, as drive.sh renders them at the end."""
    lines = []
    for path in sorted((JAIL / "data/relay/remote").glob("audit-*.jsonl")):
        lines.append(path.read_text(errors="replace"))
    return "".join(lines)


def audit_rows(kind: str, **match) -> list[dict]:
    """The desktop's audit lines of one kind (and with these fields), oldest first."""
    rows = []
    for line in audit_text().splitlines():
        try:
            row = json.loads(line)
        except ValueError:
            continue
        if row.get("kind") == kind and all(row.get(k) == v for k, v in match.items()):
            rows.append(row)
    return rows


def new_code_button() -> tuple[int, int] | None:
    """Where the dialog's "New code" button is, or None when the code row is not offering one.

    Read off a photograph like every other row of this window (its rows move with the wrapped
    text above them). Down the column at x=250 the theme's widget colour is, top to bottom: the
    address list, then — only when a code has ended — "New code", then the invite row's expiry
    list some 450 px further down. So a second band close under the first is the button.
    """
    bands = ph0n._bands(250, ph0n.WIDGET)
    if len(bands) >= 2:
        first, second = bands[0], bands[1]
        if (second[0] + second[1]) // 2 - (first[0] + first[1]) // 2 < 300:
            return 286, (second[0] + second[1]) // 2
    return None


def conf_text() -> str:
    path = JAIL / "config/RelayTerminal/relay.conf"
    return path.read_text() if path.is_file() else ""


class Drive(ph0n.Run):
    """#PH0N's run, with this card's six steps in front of it."""

    def __init__(self) -> None:
        super().__init__()
        self.code = ""              # "ABCD 4829" as the dialog shows it
        self.pin = ""
        self.letters = ""
        self.seen_urls: list[str] = []

    # -- 14. one entry point: the plug menu ----------------------------------------------------
    async def plug_menu_pairs(self) -> None:
        self.desktops_before = ph0n.desktops_online()
        ph0n.start_relay("")
        await asyncio.sleep(8)
        off_at_start = not ph0n.sidecar_running()
        no_remote_section = "[remote]" not in conf_text()
        desk("step14a-a-fresh-profile-remote-control-off")
        # The menu itself: the status line, then "Pair a phone…" and "Remote control: off".
        open_plug_menu_for_a_shot("step14b-the-plug-menu-with-remote-control-off")
        # And the first item, chosen with one Down — which is what "first item" means.
        plug_menu(1)
        root_shot("step14c-the-first-item-under-the-status-line")
        key("Return")
        for _ in range(60):
            if ph0n.share_window(required=False):
                break
            await asyncio.sleep(1)
        opened = bool(ph0n.share_window(required=False))
        ph0n.park_share_window()
        await asyncio.sleep(3)
        share_shot("step14d-the-pairing-dialog-the-code-the-qr-and-copy")
        up = ph0n.sidecar_running()
        registered = ph0n.wait_desktops(self.desktops_before + 1, timeout=90)
        line = ph0n.read_url("pair-code.txt")
        url = ph0n.read_url("pair-url.txt")
        self.pair_url = url
        self.seen_urls.append(url)
        if line and len(line.split()) == 2:
            self.letters, self.pin = line.split()
            self.code = line
        shaped = bool(re.fullmatch(r"[A-Z]{4} \d{4}", line or ""))
        conf = conf_text()
        remembered = re.search(r"\[remote\][^\[]*alwaysOn=true", conf, re.S) is not None
        hosted = bool(re.search(r"\[remote\][^\[]*address=relay-terminal\.ai", conf, re.S))
        # The plug menu again, now that the one click has turned it on: the same menu says so.
        open_plug_menu_for_a_shot("step14e-the-plug-menu-now-says-on")
        note("PASS" if (off_at_start and no_remote_section and opened and up
                        and registered == self.desktops_before + 1 and shaped and remembered
                        and hosted and url.startswith(f"{HOSTED}/pair#")) else "FAIL",
             f"a fresh profile starts with remote control off ({off_at_start}, no [remote] section:"
             f" {no_remote_section}); the plug menu's first item opens the pairing dialog"
             f" ({opened}), which turns the service on by itself: the sidecar runs ({up}), the"
             f" desktop registers at join.relay-terminal.ai (health desktops"
             f" {self.desktops_before} → {registered}), relay.conf remembers alwaysOn=true"
             f" ({remembered}) with address=relay-terminal.ai ({hosted}), the dialog shows a code"
             f" shaped {'ABCD 4829' if shaped else line!r} ({shaped}) and the link is hosted"
             f" ({url.split('#')[0]}#…, {len(url)} chars) (shots step14a-e)")
        if not self.code:
            raise RuntimeError("the dialog never showed a pairing code")

    # -- 15. the phone types the code ----------------------------------------------------------
    async def typed_code_pairs(self) -> None:
        phone = await open_phone(f"{HOSTED}/", track_sockets=True, stub_push=True)
        self.phone = phone
        await phone.wait_for("(e => !!e && !e.disabled)"
                             "(document.getElementById('welcome-code-pair'))", timeout=90)
        lead = await phone.evaluate("document.getElementById('welcome-code-lead').textContent")
        await browser_shot(phone, "phone", "step15a-the-welcome-screen-leads-with-the-code")
        await type_in(phone, "welcome-code", self.letters)
        await type_in(phone, "welcome-pin", self.pin)
        await phone.evaluate("document.getElementById('welcome-code-pair').click()")
        five = ""
        try:
            five = await phone.wait_for(FIVE_DIGITS, timeout=90)
        except AssertionError:
            five = ""
        await browser_shot(phone, "phone", "step15b-the-five-digits-on-the-phone")
        await asyncio.sleep(2)
        share_shot("step15c-the-same-five-digits-on-the-desktop")
        await allow_typing()
        arrived = False
        try:
            await phone.wait_for(shown('screen-inbox'), timeout=120)
            arrived = True
        except AssertionError:
            pass
        capability = await phone.evaluate("document.getElementById('capability').textContent")
        link = await phone.evaluate(ph0n.LINK)
        one_screen = await phone.evaluate(SCREENS_SHOWN)
        offered = await phone.evaluate(shown('notify-offer'))
        await browser_shot(phone, "phone", "step15d-the-phone-is-in-the-inbox")
        await asyncio.sleep(2)
        share_shot("step15e-the-dialog-says-the-code-was-used")
        # Nothing the rendezvous or the desktop's own audit holds may carry the PIN or the
        # pairing secret: the PIN is never sent anywhere, and the fragment is the phone's alone.
        secret = self.pair_url.split("s=", 1)[1][:16] if "s=" in self.pair_url else "n/a"
        health = json.dumps(ph0n.health())
        audit = audit_text()
        clean = (self.pin not in health and self.pin not in audit
                 and secret not in health and secret not in audit)
        leaked_console = [line for line in phone.console if self.pin in line]
        used = audit_rows("code_used", code=self.letters)
        used_once = len(used) == 1 and used[0].get("failures") == 0
        offered_button = new_code_button() is not None
        note("PASS" if (arrived and capability == "full" and lead ==
                        "Enter the code from your desktop" and five and one_screen == 1
                        and clean and not leaked_console and used_once
                        and offered_button) else "FAIL",
             f"the welcome screen at {HOSTED}/ leads with {lead!r}; the eight characters from the"
             f" dialog pair this phone: the same five digits show on both sides ({five!r} — shot"
             f" step15c is the desktop's), Allow typing lands it in the inbox ({arrived}, one"
             f" screen drawn: {one_screen == 1}) as {capability!r}, link {link!r}; the"
             f" rendezvous's /v1/health and the desktop's audit carry neither the PIN nor the"
             f" pairing secret ({clean}), and the PIN is in no console line"
             f" ({not leaked_console}). The desktop's audit has code_used for {self.letters}"
             f" exactly once with no failed try ({used_once}), and the dialog's code row ended:"
             f" it offers \"New code\" ({offered_button}; shot step15e reads \"Used\")"
             f" (shots step15a-e)")
        note("NOTE", f"the inbox led with the notifications offer on arrival: {offered}"
                     " (step 18 taps it)")

    # -- 16. three wrong PINs burn a code ------------------------------------------------------
    async def wrong_pins_burn(self) -> None:
        old = self.code
        burned_code = await self.new_code("step16a-a-fresh-code-for-the-burn")
        wrong = "1111" if self.pin != "1111" else "2222"
        browser = await open_phone(f"{HOSTED}/")
        said: list[str] = []
        refused, still_welcome = "", False
        try:
            await browser.wait_for("(e => !!e && !e.disabled)"
                                   "(document.getElementById('welcome-code-pair'))", timeout=90)
            for attempt in range(3):
                await clear(browser, "welcome-code")
                await clear(browser, "welcome-pin")
                await type_in(browser, "welcome-code", self.letters)
                await type_in(browser, "welcome-pin", wrong)
                await browser.evaluate("document.getElementById('welcome-code-pair').click()")
                try:
                    said.append(await browser.wait_for(
                        f"/not right|stopped working|no meeting/.test({WELCOME_NOTE})"
                        f" ? {WELCOME_NOTE} : ''", timeout=60))
                except AssertionError:
                    said.append(await browser.evaluate(WELCOME_NOTE))
                await asyncio.sleep(2)
            await asyncio.sleep(3)
            share_shot("step16b-the-burned-code-and-new-code-on-the-desktop")
            # The right PIN now, on the same code: it is dead, and the page says so rather than
            # pairing. This is what "burned" means, read from the phone rather than from a widget.
            await clear(browser, "welcome-pin")
            await type_in(browser, "welcome-pin", self.pin)
            await browser.evaluate("document.getElementById('welcome-code-pair').click()")
            try:
                refused = await browser.wait_for(
                    f"/stopped working|no pairing code|no meeting/.test({WELCOME_NOTE})"
                    f" ? {WELCOME_NOTE} : ''", timeout=60)
            except AssertionError:
                refused = await browser.evaluate(WELCOME_NOTE)
            still_welcome = await browser.evaluate(shown('screen-welcome'))
            await browser_shot(browser, "phone", "step16c-the-burned-code-on-the-phone")
        finally:
            await browser.stop()
        burned = audit_rows("code_burned", code=burned_code.split()[0])
        burned_by_three = len(burned) == 1 and burned[0].get("failures") == 3
        # And a new code works — minted by the button the burned row offers, which is what the
        # person looking at "Closed" will press: a second phone, the same two fields, Allow typing.
        fresh, by_button = await self.new_code_from_the_dialog("step16d-a-new-code-after-the-burn")
        second = await open_phone(f"{HOSTED}/")
        paired = False
        try:
            await second.wait_for("(e => !!e && !e.disabled)"
                                  "(document.getElementById('welcome-code-pair'))", timeout=90)
            await type_in(second, "welcome-code", self.letters)
            await type_in(second, "welcome-pin", self.pin)
            await second.evaluate("document.getElementById('welcome-code-pair').click()")
            try:
                await second.wait_for(FIVE_DIGITS, timeout=90)
                await allow_typing()
                await second.wait_for(shown('screen-inbox'), timeout=120)
                paired = True
            except AssertionError:
                pass
            await browser_shot(second, "phone", "step16e-the-new-code-paired-a-second-device")
        finally:
            await second.stop()
        first_two_counted = sum(1 for text in said[:2] if "not right" in text)
        # The third wrong PIN is the one that burns the code. The page this drive first met said
        # "…then try again" about it; app/meet.js now counts its own wrong PINs and says the code
        # has stopped working. Which sentence shows depends on what the rendezvous is serving, so
        # it is recorded rather than required — provenance.txt says whether meet.js is the repo's.
        third = said[2] if len(said) > 2 else ""
        note("NOTE", "the third wrong PIN was answered with "
                     + ("the code-has-stopped-working sentence (the served meet.js has the fix)"
                        if "stopped working" in third else
                        "'That PIN is not right … then try again' — the served meet.js predates the"
                        " fix in the repo, which makes it say the code has stopped working; it"
                        " needs the redeploy"))
        # A burned code and a code that never existed are the same answer on purpose: the
        # rendezvous stops resolving the route, and telling the two apart would say whether a
        # code existed. So either sentence is a refusal — what matters is that the *right* PIN
        # no longer pairs, and that what the phone is told points at "New code".
        dead = any(word in refused for word in ("stopped working", "no pairing code", "no meeting"))
        note("PASS" if (first_two_counted == 2 and dead and burned_by_three and by_button
                        and still_welcome and paired) else "FAIL",
             f"three wrong PINs on a fresh code ({burned_code.split()[0]}) burn it: the page counts"
             f" them ({said!r}), and the *right* PIN afterwards is refused — {refused!r} — with the"
             f" phone still on the welcome screen ({still_welcome}). The desktop's audit has"
             f" code_burned with failures=3 ({burned_by_three}) and the dialog's row reads"
             f" \"Closed\" with a \"New code\" button (shot step16b). That button"
             f" ({'pressed' if by_button else 'NOT FOUND — the window was reopened instead'})"
             f" mints {fresh.split()[0]}, which pairs a second device ({paired})"
             f" (shots step16a-e; the old code was {old.split()[0]})")

    async def new_code_from_the_dialog(self, what: str) -> tuple[str, bool]:
        """Press the ended code row's own "New code". Falls back to reopening the window, and
        says which it was: the button is the claim, the fallback only keeps the run going."""
        spot = new_code_button()
        if spot is None:
            return await self.new_code(what), False
        before = self.code
        xdo("windowfocus", ph0n.share_window())
        await asyncio.sleep(0.5)
        ph0n.share_click(*spot)
        await asyncio.sleep(3)
        line = ph0n.read_url("pair-code.txt", since=before)
        if not line or line == before or len(line.split()) != 2:
            return await self.new_code(what), False
        self.code = line
        self.letters, self.pin = line.split()
        share_shot(what)
        return self.code, True

    async def new_code(self, what: str) -> str:
        """Close the pairing window and open it from the plug menu again: a code every time."""
        ph0n.close_share_window()
        await asyncio.sleep(2)
        plug_menu(1)                       # "Pair a phone…", with the switch already on
        key("Return")
        for _ in range(60):
            if ph0n.share_window(required=False):
                break
            await asyncio.sleep(1)
        ph0n.park_share_window()
        await asyncio.sleep(3)
        line = ph0n.read_url("pair-code.txt", since=self.code)
        url = ph0n.read_url("pair-url.txt", since=self.pair_url)
        if line and len(line.split()) == 2:
            self.code = line
            self.letters, self.pin = line.split()
        if url:
            self.pair_url = url
            self.seen_urls.append(url)
        share_shot(what)
        return self.code

    # -- 17. an iPhone that is not installed ---------------------------------------------------
    async def ios_installs_first(self) -> None:
        url = await self.fresh_link()
        pairs_before = len(audit_rows("pair"))
        safari = Browser()
        await safari.start()
        seen: dict = {}
        shown_card = False
        where = ""
        try:
            await safari.call("Emulation.setDeviceMetricsOverride", ph0n.PHONE)
            await safari.call("Emulation.setUserAgentOverride",
                              {"userAgent": IPHONE_UA, "platform": "iPhone"})
            await safari.call("Page.addScriptToEvaluateOnNewDocument", {"source": NOT_STANDALONE})
            await safari.navigate(url)
            try:
                await safari.wait_for(shown("install-card"), timeout=60)
                shown_card = True
            except AssertionError:
                pass
            # Ten seconds is long enough for a pairing to have reached the desktop if it were
            # going to: the five digits show within two on this machine.
            await asyncio.sleep(10)
            seen = json.loads(await safari.evaluate("""JSON.stringify({
              welcome: getComputedStyle(document.getElementById('screen-welcome')).display !== 'none',
              pairing: getComputedStyle(document.getElementById('screen-pair')).display !== 'none',
              screens: [...document.querySelectorAll('.screen')]
                .filter(e => getComputedStyle(e).display !== 'none').map(e => e.id),
              lead: (document.getElementById('install-lead') || {}).textContent || '',
              copy: !!document.getElementById('install-copy'),
              link: (document.getElementById('install-link') || {}).textContent || '',
              linkRow: (e => !!e && getComputedStyle(e).display !== 'none')
                (document.getElementById('install-link-row')),
            })"""))
            await browser_shot(safari, "phone", "step17a-ios-safari-is-told-to-install-first")
        finally:
            await safari.stop()
        share_shot("step17b-the-desktop-was-never-asked")
        pairs_after_safari = len(audit_rows("pair"))
        # The same link, unspent, in the "installed" app: it pairs. That it still works is the
        # proof the Safari tab never spent it — and so that the desktop was never asked.
        installed = Browser()
        await installed.start()
        paired = False
        try:
            await installed.call("Emulation.setDeviceMetricsOverride", ph0n.PHONE)
            await installed.call("Emulation.setUserAgentOverride",
                                 {"userAgent": IPHONE_UA, "platform": "iPhone"})
            await installed.call("Page.addScriptToEvaluateOnNewDocument", {"source": STANDALONE})
            await installed.navigate(url)
            try:
                await installed.wait_for(FIVE_DIGITS, timeout=90)
                await allow_typing()
                await installed.wait_for(shown('screen-inbox'), timeout=120)
                paired = True
            except AssertionError:
                pass
            where = await installed.evaluate("location.href")
            await browser_shot(installed, "phone", "step17c-the-same-link-in-the-installed-app")
        finally:
            await installed.stop()
        pairs_after_app = len(audit_rows("pair"))
        note("PASS" if (shown_card and seen.get("welcome") and not seen.get("pairing")
                        and seen.get("screens") == ["screen-welcome"] and seen.get("linkRow")
                        and seen.get("link") == url and paired
                        and pairs_after_safari == pairs_before
                        and pairs_after_app == pairs_before + 1) else "FAIL",
             f"a pairing link opened in Safari on an iPhone that is not installed shows the install"
             f" card ({shown_card}) and hands the link over instead of pairing: screens"
             f" {seen.get('screens')}, the copy row is up ({seen.get('linkRow')}) with the link"
             f" itself ({seen.get('link') == url}), and ten seconds later nothing has paired"
             f" ({not seen.get('pairing')}) — the desktop was never asked: its audit has"
             f" {pairs_before} pairing(s) before the tab and {pairs_after_safari} after, and shot"
             f" step17b is the dialog with no approval row. The same link in an"
             f" installed app pairs ({paired}, left at {where!r}; the audit now has"
             f" {pairs_after_app}), which is the proof the tab never"
             f" spent it (shots step17a-c). Lead: {seen.get('lead', '')[:80]!r}")

    async def fresh_link(self) -> str:
        """A pairing link the run has not used: the dialog writes one every time it opens."""
        before = self.pair_url
        await self.new_code("step17z-a-fresh-link-for-the-ios-check")
        if self.pair_url == before:
            note("NOTE", "the dialog did not write a new pairing link; the old one is reused")
        return self.pair_url

    # -- 18. the notifications offer on first arrival ------------------------------------------
    async def first_arrival_offer(self) -> None:
        phone = self.phone
        await phone.wait_for(shown('screen-inbox'), timeout=60)
        before = json.loads(await phone.evaluate("""JSON.stringify({
          top: (e => !!e && getComputedStyle(e).display !== 'none')(document.getElementById('inbox-top')),
          offer: (e => !!e && getComputedStyle(e).display !== 'none')(document.getElementById('notify-offer')),
          text: (document.getElementById('notify-offer') || {}).textContent || '',
          first: (() => { const top = document.getElementById('inbox-top');
                          const list = document.getElementById('pane-list');
                          return !!top && !!list && !!(top.compareDocumentPosition(list)
                            & Node.DOCUMENT_POSITION_FOLLOWING); })(),
          permission: window.Notification ? Notification.permission : 'none',
        })"""))
        await browser_shot(phone, "phone", "step18a-the-inbox-leads-with-turn-on-notifications")
        origin = await phone.evaluate("location.origin")
        await phone.call("Browser.grantPermissions",
                         {"origin": origin, "permissions": ["notifications"]})
        await press(phone, "notify-offer")
        switches = []
        try:
            switches = await phone.wait_for("""
                (() => { const boxes = [...document.querySelectorAll('#notify-kinds input')];
                         return boxes.length ? boxes.map(b => [b.id, b.checked]) : null; })()
            """, timeout=60)
        except AssertionError:
            pass
        await asyncio.sleep(2)
        after = json.loads(await phone.evaluate("""JSON.stringify({
          offer: (e => !!e && getComputedStyle(e).display !== 'none')(document.getElementById('notify-offer')),
          note: (document.getElementById('notify-offer-note') || {}).textContent || '',
          row: (document.getElementById('notify') || {}).textContent || '',
          kinds: (e => !!e && getComputedStyle(e).display !== 'none')(document.getElementById('notify-kinds')),
        })"""))
        await browser_shot(phone, "phone", "step18b-both-switches-on")
        both_on = bool(switches) and all(state for _, state in switches)
        note("PASS" if (before["offer"] and before["first"]
                        and before["text"].strip() == "Turn on notifications"
                        and both_on and not after["offer"]) else "FAIL",
             f"the first arrival after pairing leads with one button: {before!r}; one tap turns"
             f" both switches on ({switches}) and the offer collapses ({not after['offer']}),"
             f" leaving the settings row ({after['row']!r}) where it is changed afterwards"
             f" (shots step18a, step18b). The subscription itself is the browser's one stub"
             f" (pushManager.subscribe); everything on the desktop's side of it is real.")

    # -- 19. the plug menu's own switch --------------------------------------------------------
    async def plug_switch(self) -> None:
        phone = self.phone
        open_plug_menu_for_a_shot("step19a-the-plug-menu-with-remote-control-on")
        plug_menu(2)                       # the switch, under "Pair a phone…"
        key("Return")
        await asyncio.sleep(2)
        desk("step19b-the-desktop-after-the-switch-off")
        gone = ph0n.wait_desktops(self.desktops_before, timeout=90)
        try:
            link_off = await phone.wait_for(
                f"['offline', 'dropped'].includes({ph0n.LINK}) ? {ph0n.LINK} : ''", timeout=60)
        except AssertionError:
            link_off = await phone.evaluate(ph0n.LINK)
        await browser_shot(phone, "phone", "step19c-the-phone-says-offline")
        conf_off = re.search(r"\[remote\][^\[]*alwaysOn=false", conf_text(), re.S) is not None
        # And on again, from the same menu.
        plug_menu(2)
        key("Return")
        await asyncio.sleep(2)
        back = ph0n.wait_desktops(self.desktops_before + 1, timeout=90)
        reconnected = False
        try:
            await phone.wait_for(f"{ph0n.LINK} === 'connected' ? 1 : 0", timeout=120)
            reconnected = True
        except AssertionError:
            pass
        nudged = ""
        if not reconnected:
            # A phone that has been asleep gets a `pageshow`; this is the same nudge, and it says
            # in notes.txt that the socket did not come back on its own.
            await phone.evaluate(
                "(() => { document.dispatchEvent(new Event('visibilitychange'));"
                " window.dispatchEvent(new Event('pageshow')); return true; })()")
            try:
                await phone.wait_for(f"{ph0n.LINK} === 'connected' ? 1 : 0", timeout=60)
                nudged = "after a pageshow"
            except AssertionError:
                nudged = "not at all"
        screens = await phone.evaluate(
            "[...document.querySelectorAll('.screen')]"
            ".filter(e => getComputedStyle(e).display !== 'none').map(e => e.id)")
        rows = await phone.evaluate("document.querySelectorAll('.pane-row').length")
        conf_on = re.search(r"\[remote\][^\[]*alwaysOn=true", conf_text(), re.S) is not None
        await browser_shot(phone, "phone", "step19d-the-phone-is-back-with-no-pairing")
        open_plug_menu_for_a_shot("step19e-the-plug-menu-says-on-again")
        no_pairing = screens == ["screen-inbox"]
        note("PASS" if (gone == self.desktops_before and link_off in ("offline", "dropped")
                        and conf_off and back == self.desktops_before + 1
                        and (reconnected or nudged == "after a pageshow") and no_pairing
                        and conf_on) else "FAIL",
             f"the plug menu's switch turns remote control off: the rendezvous drops the desktop"
             f" (desktops {gone}), the phone says {link_off!r}, alwaysOn=false is written"
             f" ({conf_off}). Turned on again from the same menu the desktop is back (desktops"
             f" {back}, alwaysOn=true: {conf_on}) and the phone reconnects"
             f" ({'on its own' if reconnected else nudged}) straight into the inbox with {rows}"
             f" pane(s) and no pairing ({no_pairing}, screens {screens}) (shots step19a-e)")

    async def go_new_steps(self) -> None:
        await step("14 the plug menu pairs", self.plug_menu_pairs())
        await step("15 the typed code", self.typed_code_pairs())
        await step("16 a burned code", self.wrong_pins_burn())
        await step("17 iOS installs first", self.ios_installs_first())
        await step("18 notifications offered", self.first_arrival_offer())
        await step("19 the plug switch", self.plug_switch())
        # One code per look at the dialog. The first recorded run of this drive found two: the
        # dialog asked on the first `started`, a second or two before the sidecar had moved to
        # join.relay-terminal.ai, the move ended that code as `code_expired` and the dialog was
        # handed another — under the eyes of someone already reading the first one out. Fixed in
        # remote/gui_host.py (`pair_code` waits for the move; the re-ask gets the same code), so
        # here it is a check: nothing in this half lives ten minutes, so nothing may expire.
        short_lived = [row["code"] for row in audit_rows("code_expired")]
        minted = [row["code"] for row in audit_rows("code_create")]
        note("PASS" if not short_lived else "FAIL",
             f"every code the dialog showed stayed the code: {len(minted)} minted ({minted}), none"
             f" ended by the service's move to join.relay-terminal.ai (code_expired: {short_lived})"
             f" — see audit-steps-14-19.txt")

def wipe_profile() -> None:
    """Everything the first half wrote, gone: the second half is a fresh desktop again.

    The audit and the stderr of the first half are kept first — they are evidence, and the
    profile they live in is about to go.
    """
    audit = audit_text()
    if audit:
        rendered = []
        for line in audit.splitlines():
            row = json.loads(line)
            kind = row.pop("kind", "?")
            row.pop("at", None)
            rendered.append(f"{kind:20} " + " ".join(f"{k}={v!r}" for k, v in row.items()))
        (OUT / "audit-steps-14-19.txt").write_text("\n".join(rendered) + "\n")
    stderr = OUT / "relay-stderr.log"
    if stderr.is_file():
        stderr.replace(OUT / "relay-stderr-steps-14-19.log")
    for path in ("config/RelayTerminal", "data/relay", "work", "cache"):
        shutil.rmtree(JAIL / path, ignore_errors=True)
    (JAIL / "work").mkdir(parents=True, exist_ok=True)
    (JAIL / "cache").mkdir(parents=True, exist_ok=True)
    (JAIL / "config/RelayTerminal").mkdir(parents=True, exist_ok=True)
    shutil.copy(JAIL / "relay.conf.template", JAIL / "config/RelayTerminal/relay.conf")
    note("NOTE", "the profile was wiped between the two halves: the thirteen steps below run on a"
                 " fresh desktop identity, with the same build, jail and fake model")


async def main() -> int:
    drive = Drive()
    try:
        await drive.go_new_steps()
    finally:
        await drive.close()
    wipe_profile()
    # #PH0N's thirteen, unchanged, on the same build: this card touched the entry point their
    # third step uses, so they are the regression half of this drive.
    thirteen = ph0n.Run()
    try:
        await thirteen.go()
    finally:
        await thirteen.close()
    failures = [line for line in ph0n.NOTES if line.startswith("FAIL")]
    print(f"\n{len(ph0n.NOTES)} notes, {len(failures)} failures", flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
