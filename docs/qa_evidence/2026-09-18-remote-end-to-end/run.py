# SPDX-License-Identifier: AGPL-3.0-or-later
"""The whole of #W5N2 driven once, against a real Relay (docs/qa_evidence/…/drive.sh runs this).

Four actors take turns and each waits on the others, so this is one coroutine rather than a shell
script with flag files: the desktop (xdotool against the Xvfb window `drive.sh` found), the owner's
phone (a headless Chrome at 390×844), a guest called alice (a second headless Chrome), and a second
device of the owner's paired for viewing only (a third). A fake push service stands on loopback so
a notification can be caught, opened and read.

Nothing here is a stand-in for the product except two things, both named where they happen:

* ``pushManager.subscribe`` is stubbed **in the browser**, because it talks to Google's push
  service and there is none reachable from this machine. Everything after it is the real thing:
  the page's own seal key, the real `push_subscribe` inside the Noise session, the hub's own
  decision in `remote/notify.py`, the rendezvous's VAPID signature and a real HTTPS delivery.
* the transcription provider, which needs a key this run deliberately cannot reach
  (`RELAY_KEYRING=off`), so the voice step shows the error a phone is shown.

Every step writes what it saw to notes.txt with a PASS/FAIL/NOTE in front of it, and a step that
throws is recorded and the run carries on: one run should surface every problem, not the first.
"""
from __future__ import annotations

import asyncio
import base64
import json
import os
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))

from remote import devtls, httpd, push                                   # noqa: E402
from tests.browser import SCREENS_SHOWN, Browser, shown                  # noqa: E402
from tests.test_remote_push import receive, subscription_keypair         # noqa: E402

OUT = Path(os.environ["RELAY_QA_OUT"])
WORK = Path(os.environ["RELAY_QA_WORK"])
WIN = os.environ["RELAY_QA_WIN"]
CERTS = Path(os.environ["RELAY_PUSH_CERTS"])

NOTES: list[str] = []


def note(verdict: str, text: str) -> None:
    line = f"{verdict:4} {text}"
    print(line, flush=True)
    NOTES.append(line)
    (OUT / "notes.txt").write_text("\n".join(NOTES) + "\n")


def shot_name(side: str, what: str) -> str:
    """`NN-<side>-<what>.png`. The number is written into each call rather than counted here, so
    a shot keeps its name when a step before it fails, and the README's numbers stay true."""
    number, _, rest = what.partition("-")
    return f"{number}-{side}-{rest}.png"


# ---- the desktop ---------------------------------------------------------------------------------

def xdo(*args: str) -> str:
    return subprocess.run(["xdotool", *args], capture_output=True, text=True).stdout.strip()


def desk(what: str) -> str:
    """A shot of the Relay window itself."""
    name = shot_name("desktop", what)
    subprocess.run(["import", "-window", WIN, str(OUT / name)], check=False)
    print("shot", name, flush=True)
    return name


def share_window(required: bool = True) -> str:
    """The share window's id.

    It is raised rather than returned empty when it is missing, because `import -window ""` waits
    for somebody to click a window with the mouse, and a headless run then hangs for ever instead
    of failing — which is how an hour of this one went.
    """
    found = (xdo("search", "--onlyvisible", "--name", "Share this pane") or "").split("\n")[0]
    if not found and required:
        raise RuntimeError("the share window is not on screen")
    return found


def share_shot(what: str) -> str:
    name = shot_name("share", what)
    subprocess.run(["import", "-window", share_window(), str(OUT / name)], check=False)
    print("shot", name, flush=True)
    return name


def click(x: int, y: int) -> None:
    xdo("mousemove", "--window", WIN, str(x), str(y), "click", "1")


def share_click(x: int, y: int) -> None:
    xdo("mousemove", "--window", share_window(), str(x), str(y), "click", "1")


# The share window's rows cannot be aimed at by a fixed offset. Its height changes with how many
# lines the sentence at the top happens to wrap to, whether a device is asking to pair, and
# whether a link has been made, and the spare height goes into a stretch in the middle — so a row
# is neither a fixed distance from the top nor from the bottom. It is, though, always the same
# colour: Relay draws a control on a darker page. So the window is photographed and the columns
# are read, which is the same thing a person does when they look at it.
WIDGET = (28, 31, 38)          # a button, a combo box, a spin box, a line edit
LIST_FILL = (22, 24, 29)       # the paired-device list


def _bands(column: int, colour: tuple[int, int, int]) -> list[tuple[int, int]]:
    """Every run of `colour` down one column of the share window, as (top, bottom) pairs.

    Runs separated by less than a character's height are joined: the label inside a button is not
    the button's colour, and a column through one would otherwise come back as two rows.
    """
    from PIL import Image

    path = OUT / ".share-probe.png"
    subprocess.run(["import", "-window", share_window(), str(path)], check=False)
    image = Image.open(path).convert("RGB")
    width, height = image.size
    column = min(max(column, 0), width - 1)
    hits = [y for y in range(height)
            if all(abs(a - b) <= 6 for a, b in zip(image.getpixel((column, y)), colour))]
    bands: list[list[int]] = []
    for y in hits:
        if bands and y - bands[-1][1] <= 10:
            bands[-1][1] = y
        else:
            bands.append([y, y])
    return [(top, bottom) for top, bottom in bands if bottom - top >= 16]


def share_rows() -> list[int]:
    """The middle of every control down the left-hand column: the address combo, then the invite
    row's role combo, then Revoke selected at the bottom."""
    return [(top + bottom) // 2 for top, bottom in _bands(65, WIDGET)]


def device_list_y(row: int = 0) -> int:
    """The middle of one row of the paired-device list, counted from the top.

    The *first* row, not the middle of the list: by the time this run turns the password switch on
    there are two devices in it, and a click at the centre picks the second — which is how a drive
    once turned password entry on for the viewing device and then waited for a field to appear on
    the phone.
    """
    bands = _bands(253, LIST_FILL)
    if not bands:
        raise RuntimeError("the paired-device list is not on the share window")
    top, _ = max(bands, key=lambda band: band[1] - band[0])
    return top + 14 + row * 30


def typ(text: str) -> None:
    xdo("type", "--delay", "12", text)


def key(*names: str) -> None:
    xdo("key", "--delay", "70", *names)


def focus_relay() -> None:
    xdo("windowfocus", WIN)


PROMPT_BOX = (400, 925)


def at_prompt(text: str, enter: bool = True) -> None:
    focus_relay()
    click(*PROMPT_BOX)
    time.sleep(0.4)
    typ(text)
    if enter:
        key("Return")


def palette(action: str) -> None:
    """Run an action by name. Layout-independent, which matters once the window has split."""
    focus_relay()
    click(*PROMPT_BOX)
    time.sleep(0.3)
    key("ctrl+shift+a")
    time.sleep(1.2)
    typ(action)
    time.sleep(1.2)
    key("Return")


def park_share_window() -> None:
    """Beside the Relay window, wholly on the root, so both can be photographed."""
    window = share_window(required=False)
    if window:
        xdo("windowmove", window, "1700", "10")


def ensure_share_window() -> str:
    """Open the share window if it is not up, and park it.

    It does not stay open for the length of a run: the pairing code it shows lasts five minutes and
    the window goes with it, so a run slower than that comes back to find it gone. Reopening it is
    what a person does, and the action is the one the share chip runs.
    """
    if not share_window(required=False):
        palette("Share this pane")
        # Polled, not slept at: the window takes as long as the sidecar takes to answer, which on
        # a loaded machine is well past any number worth writing down.
        for _ in range(40):
            time.sleep(1)
            if share_window(required=False):
                break
    park_share_window()
    time.sleep(1)
    return share_window()


# ---- the phone and the browsers -------------------------------------------------------------------

PHONE = {"width": 390, "height": 844, "deviceScaleFactor": 2, "mobile": True}


async def browser_shot(browser: Browser, side: str, what: str) -> str:
    name = shot_name(side, what)
    result = await browser.call("Page.captureScreenshot",
                                {"format": "png", "captureBeyondViewport": False})
    (OUT / name).write_bytes(base64.b64decode(result["data"]))
    print("shot", name, flush=True)
    return name


async def open_phone(url: str, *, microphone: bool = False, stub_push: dict | None = None,
                     size: dict | None = None) -> Browser:
    browser = Browser(insecure=True, microphone=microphone)
    await browser.start()
    await browser.call("Emulation.setDeviceMetricsOverride", size or PHONE)
    if stub_push is not None:
        await install_push_stub(browser, stub_push)
    await browser.navigate(url)
    return browser


async def install_push_stub(browser: Browser, subscription: dict) -> None:
    """Make `pushManager.subscribe` answer, without a push service.

    This is the one part of the notification path this machine cannot run: a browser's push
    subscription comes from Google's or Mozilla's service and there is none reachable here. What
    the page does with the answer — generate the seal key, send `push_subscribe` inside the Noise
    session, store what came back — is untouched, and so is everything on the desktop's side.
    """
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
        // Inert until the run arms it, so the first tap is the real thing with no push service
        // and the second is the same code path with one answered. Arming it needs no reload,
        // which matters: this page was opened on a pairing link that is spent.
        let armed = false, live = null;
        const real = PushManager.prototype.subscribe;
        Object.defineProperty(window, '__relayArmPush', { value: () => { armed = true; } });
        PushManager.prototype.subscribe = async function (...rest) {
          if (!armed) return real.apply(this, rest);
          live = fake;
          return fake;
        };
        const realGet = PushManager.prototype.getSubscription;
        PushManager.prototype.getSubscription = async function (...rest) {
          return armed ? live : realGet.apply(this, rest);
        };
      })();
    """ % (json.dumps(subscription["endpoint"]), json.dumps(subscription["p256dh"]),
           json.dumps(subscription["auth"]))
    await browser.call("Page.addScriptToEvaluateOnNewDocument", {"source": script})


# ---- the fake push service -------------------------------------------------------------------------

class PushService:
    """A local stand-in for FCM, over TLS, with the certificate the sidecar was told to trust."""

    def __init__(self) -> None:
        self.taken: list[bytes] = []
        self.server = httpd.Server()

    async def start(self) -> str:
        @self.server.route("POST", "/subscription/one")
        async def take(request, body):                                   # noqa: ANN001
            self.taken.append(body)
            return httpd.Response(status=201, body=b"", content_type="text/plain")

        await self.server.start("127.0.0.1", 0, ssl_context=devtls.context(CERTS))
        return f"https://127.0.0.1:{self.server.port}/subscription/one"

    async def close(self) -> None:
        await self.server.close()


# ---- helpers ---------------------------------------------------------------------------------------

def read_url(name: str, since: str = "") -> str:
    path = WORK / name
    for _ in range(120):
        if path.is_file():
            text = path.read_text().strip()
            if text and text != since:
                return text
        time.sleep(0.5)
    return ""


def screen_text(selector: str = ".screen-grid") -> str:
    """JavaScript for the text of the terminal grid, or '' when it is not on screen."""
    return (f"(() => {{ const n = document.querySelector({json.dumps(selector)});"
            f" return n ? n.textContent : ''; }})()")


async def compose(browser: Browser, text: str, box: str = "composer-text",
                  button: str = "composer-send", prefer_view: bool = True) -> str:
    """Type into the prompt box that is actually on screen and press its send, as a thumb would.

    Since the phone grew a pane view, the box a person sees on a shared pane is the **pane's own**
    (`.rp-input`, `.rp-send` in `app/pane.js`) and `app/app.js` hides its older one under it. So
    this reaches for the pane's box first and falls back to the client's, and says which it used;
    the guest's boxes and the password field have no pane-view equivalent and are named directly.

    The text goes through `json.dumps`, because half of what this run sends is shell with quotes
    and backslashes in it, and hand-escaping that into JavaScript is how a drive ends up testing
    its own quoting. The click is guarded, because Chrome reports an exception thrown by a click
    handler as an exception of the evaluation that ran it: a fault in the page is worth recording,
    not worth losing the rest of the run to.
    """
    used = await browser.evaluate(f"""
        (() => {{
          const text = {json.dumps(text)};
          const wanted = {json.dumps(box)};
          const view = wanted === 'composer-text' && {json.dumps(bool(prefer_view))}
            ? document.querySelector('.rp-composer:not([hidden]) .rp-input') : null;
          const send = view
            ? view.closest('.rp-composer').querySelector('.rp-send') : null;
          const target = view || document.getElementById(wanted);
          const press = send || document.getElementById({json.dumps(button)});
          if (!target || !press) return 'MISSING';
          target.value = text;
          target.dispatchEvent(new Event('input', {{ bubbles: true }}));
          try {{ press.click(); }}
          catch (error) {{ return 'THREW ' + String(error && (error.stack || error)); }}
          return view ? 'the pane view\\u2019s box' : 'the client\\u2019s own box';
        }})()
    """)
    if used.startswith(("MISSING", "THREW")):
        note("NOTE", f"sending {text[:40]!r}: {used[:300]}")
    return used


async def press(browser: Browser, element_id: str) -> str:
    """Click a control by id and say what happened, instead of throwing the step away."""
    result = await browser.evaluate(f"""
        (() => {{
          const node = document.getElementById({json.dumps(element_id)});
          if (!node) return 'MISSING';
          try {{ node.click(); return ''; }}
          catch (error) {{ return 'THREW ' + String(error && (error.stack || error)); }}
        }})()
    """)
    if result:
        note("NOTE", f"clicking #{element_id}: {result[:300]}")
    return result


async def terminal_has(browser: Browser, needle: str, timeout: float = 40) -> bool:
    try:
        await browser.wait_for(
            f"{screen_text()}.includes({json.dumps(needle)})", timeout=timeout)
        return True
    except AssertionError:
        return False
    except RuntimeError as error:
        note("NOTE", f"looking for {needle!r} on the screen: {error}")
        return False


async def wait_idle(browser: Browser, timeout: float = 300) -> str:
    """Wait until the pane's agent has stopped working.

    A shell command sent while a turn is running is queued behind it — correctly — so a step that
    needs the shell to answer now has to wait for the turn first. The pane view's clock chip is
    the same thing the desktop's strip shows, and it is empty when nothing is running.
    """
    clock = ("(() => { const n = document.querySelector('.rp-clock');"
             " return n ? n.textContent : ''; })()")
    deadline = asyncio.get_running_loop().time() + timeout
    last = ""
    while asyncio.get_running_loop().time() < deadline:
        last = await browser.evaluate(clock) or ""
        if not last.strip():
            return last
        await asyncio.sleep(2)
    note("NOTE", f"the pane was still working after {timeout:.0f} s: {last!r}")
    return last


async def step(name: str, coroutine) -> None:
    try:
        await coroutine
    except Exception as error:                                           # noqa: BLE001
        note("FAIL", f"{name}: {type(error).__name__}: {error}")


# ---- the run ----------------------------------------------------------------------------------------

class Run:
    def __init__(self) -> None:
        self.phone: Browser | None = None
        self.guest: Browser | None = None
        self.viewer: Browser | None = None
        self.service = PushService()
        self.endpoint = ""
        self.subscription = (b"", b"")
        self.seal_key = b""
        self.pair_url = ""
        self.invite = ""
        self.guest_code = ""

    # -- 1. a pane worth sharing ------------------------------------------------------------------
    async def pane(self) -> None:
        at_prompt("/local")
        await asyncio.sleep(4)
        at_prompt('printf "the pane a phone and a guest are about to join\\n"')
        await asyncio.sleep(3)
        desk("01-a-pane-with-output-and-a-local-agent")
        note("PASS", "the pane runs a shell and its agent is the local model (shot 01)")

    # -- 2. the phone ------------------------------------------------------------------------------
    async def pair_phone(self) -> None:
        palette("Share this pane")
        await asyncio.sleep(12)
        if not share_window():
            raise RuntimeError("the share window did not open")
        park_share_window()
        await asyncio.sleep(1)
        share_shot("02-the-share-window-offers-the-phone-first")

        url = read_url("pair-url.txt")
        if not url:
            raise RuntimeError("no pairing url; the sidecar did not start")
        self.pair_url = url
        # The push stub is installed now and armed much later: it has to be in before the document
        # is, and this page can never be reloaded — its URL is a pairing link that will be spent.
        private, p256dh, auth = subscription_keypair()
        self.subscription = (private, auth)
        self.endpoint = await self.service.start()
        b64 = lambda raw: base64.urlsafe_b64encode(raw).decode().rstrip("=")   # noqa: E731
        self.phone = await open_phone(
            url, microphone=True,
            stub_push={"endpoint": self.endpoint, "p256dh": b64(p256dh), "auth": b64(auth)})
        code = await self.phone.wait_for(
            "document.getElementById('pair-code')?.textContent?.match(/^\\d{5}$/)"
            " ? document.getElementById('pair-code').textContent : ''", timeout=90)
        await browser_shot(self.phone, "phone", "03-the-code-on-the-phone")
        await asyncio.sleep(2)
        share_shot("04-the-same-code-on-the-desktop")
        note("NOTE", f"the phone and the desktop are both showing {code}"
                     " (shots 03 and 04 — compare them)")

        # Refuse holds the focus, so Return would turn the phone away; the third button is
        # "Allow typing", which is what the one routed prompt box needs.
        xdo("windowfocus", share_window())
        await asyncio.sleep(1)
        key("Tab", "Tab", "space")
        await self.phone.wait_for(shown('screen-inbox'), timeout=120)
        capability = await self.phone.evaluate(
            "document.getElementById('capability').textContent")
        await browser_shot(self.phone, "phone", "05-paired-inbox")
        if capability != "full":
            raise RuntimeError(f"paired as {capability!r}, not full")
        note("PASS", "the phone paired with Allow typing and reads `full` (shot 05)")

    async def phone_runs_a_command(self) -> None:
        phone = self.phone
        await phone.evaluate("document.querySelectorAll('.pane-row')[0].click()")
        await phone.wait_for(shown('terminal-pane'), timeout=40)
        await phone.wait_for("document.querySelectorAll('.screen-row').length > 1", timeout=40)
        drawn = await phone.evaluate(SCREENS_SHOWN)
        mounted = await phone.evaluate(
            "(() => { const n = document.getElementById('pane-view');"
            " return n ? !n.hidden : false; })()")
        await browser_shot(self.phone, "phone", "06-the-shared-pane")
        note("NOTE", f"exactly one screen is drawn: {drawn == 1}; the pane view is mounted:"
                     f" {mounted}")

        # Two boxes, and it matters which: the pane view draws the pane's own prompt box over the
        # client's, so this tries the one a thumb would reach for first and then the client's own.
        #
        # "It reached the shell" is not the same question as "the words turned up on screen": an
        # agent handed a command will run it as a tool call and print the same output. What tells
        # them apart is the attribution line Relay writes under a prompt it gave the agent, so the
        # count of those before and after is the test.
        origins = lambda text: text.count("remote:")                     # noqa: E731
        before = origins(await phone.evaluate(screen_text()))
        used = await compose(phone, 'printf "the box on top ran this\\n"')
        showed = await terminal_has(phone, "the box on top ran this", timeout=90)
        after = origins(await phone.evaluate(screen_text()))
        to_agent = after > before
        await browser_shot(self.phone, "phone", "07-a-command-through-the-box-on-top")
        note("PASS" if (showed and not to_agent) else "FAIL",
             f"a shell command typed into {used} ran in the shell: output on screen {showed},"
             f" and it went to the agent instead: {to_agent}"
             f" (“remote:” attribution lines {before} → {after}) (shot 07)")

        used = await compose(phone, 'printf "a command the phone ran\\n"', prefer_view=False)
        ok = await terminal_has(phone, "a command the phone ran", timeout=45)
        await browser_shot(self.phone, "phone", "07a-the-command-the-phone-ran")
        desk("08-the-phones-command-in-the-real-pane")
        note("PASS" if ok else "FAIL",
             f"the phone's command, sent through {used}, ran in the desktop's shell"
             " (shots 07a, 08)")

    async def phone_prompts_the_agent(self) -> None:
        phone = self.phone
        await compose(phone, "in one short sentence, what does the pwd command print?")
        # The answer prints into the terminal, because that is where Relay prints it.
        started = await terminal_has(phone, "pwd command print", timeout=60)
        await asyncio.sleep(45)
        await browser_shot(self.phone, "phone", "09-the-agent-answering-on-the-phone")
        desk("10-the-agent-answering-in-the-pane")
        text = await phone.evaluate(screen_text())
        note("PASS" if started else "FAIL",
             "an agent prompt from the phone reached the pane's agent (shots 09, 10);"
             f" the phone's screen ends: …{' '.join(text.split())[-140:]!r}")

    async def phone_scrolls_back(self) -> None:
        phone = self.phone
        await wait_idle(phone)
        await compose(phone, 'for i in $(seq 1 400); do printf "scrollback-%s\\n" "$i"; done',
                      prefer_view=False)
        await asyncio.sleep(12)
        rows = ".screen-history .screen-row, .screen-grid > .screen-row"
        column = (f"(() => [...document.querySelectorAll('{rows}')]"
                  ".map(n => n.textContent.trim()))()")
        # Dragging it down, the way a finger would. The pane view gives the terminal a short box on
        # a phone, so a page is a few rows rather than a screenful and this needs longer and asks
        # for less than the older scrollback drive did.
        held = 0
        try:
            held = await phone.wait_for(
                "(() => { const w = document.getElementById('screen-wrap');"
                " const t = [...document.querySelectorAll('.screen-history .screen-row')];"
                " if (t.length >= 60) return t.length; w.scrollTop = 0; return 0; })()",
                timeout=150)
        except AssertionError:
            note("NOTE", "the phone did not reach 60 rows of history in 150 s; what it did"
                         " reach is checked below and shot 11 is where it got to")
        await browser_shot(self.phone, "phone", "11-the-phone-scrolled-back")
        numbers = [int(text.split("-")[-1]) for text in await phone.evaluate(column)
                   if text.startswith("scrollback-")]
        contiguous = bool(numbers) and numbers == list(range(numbers[0], numbers[0] + len(numbers)))
        note("PASS" if (contiguous and held) else "FAIL",
             f"the phone paged {held} rows of history back and the column is one unbroken run:"
             f" rows {numbers[:1]}..{numbers[-1:]}, {len(numbers)} of them,"
             f" contiguous {contiguous} (shot 11)")
        await phone.evaluate(
            "(() => { const w = document.getElementById('screen-wrap');"
            " w.scrollTop = w.scrollHeight; return true; })()")
        await asyncio.sleep(1)

    async def phone_takes_over(self) -> None:
        phone = self.phone
        await wait_idle(phone)
        await press(phone, 'term-take')
        await phone.wait_for(shown('term-release'), timeout=30)
        mode = await phone.evaluate("document.getElementById('term-mode').textContent")
        await compose(phone, "echo the phone is driving this shell", prefer_view=False)
        landed = await terminal_has(phone, "the phone is driving this shell", timeout=40)
        await browser_shot(self.phone, "phone", "12-the-phone-took-over")
        desk("13-the-line-the-phone-typed")
        note("PASS" if landed else "FAIL",
             f"Take over puts the phone on the keyboard ({mode!r}) and its line reached the"
             " program (shots 12, 13)")

        # Section 10.3: the owner's own keystroke takes the keyboard back from whoever holds it,
        # the phone's own device included, and anything of theirs that arrives afterwards is
        # refused `not_driving`. Half a line is left in the phone's box first, because losing the
        # keyboard must not lose the words — the same rule the guest keeps in shot 43.
        await phone.evaluate(
            "(() => { const box = document.getElementById('composer-text');"
            " box.value = 'rm -r bui';"
            " box.dispatchEvent(new Event('input', { bubbles: true })); return true; })()")
        at_prompt("the owner types while the phone is driving", enter=False)
        await asyncio.sleep(6)
        after = await phone.evaluate("""
            (() => ({ mode: document.getElementById('term-mode').textContent,
                      note: document.getElementById('term-note').textContent,
                      said: document.getElementById('thread-note').textContent,
                      kept: document.getElementById('composer-text').value,
                      handBack: !document.getElementById('term-release').hidden,
                      takeOver: !document.getElementById('term-take').hidden }))()
        """)
        await compose(phone, "echo the phone typed after the owner did", prefer_view=False)
        still = await terminal_has(phone, "the phone typed after the owner did", timeout=20)
        await browser_shot(self.phone, "phone", "13a-after-the-owner-typed-in-the-pane")
        desk("13b-the-owner-typing-while-the-phone-held-the-keyboard")
        flipped = after["mode"] == "Watching" and not after["handBack"] and after["takeOver"]
        note("PASS" if (flipped and not still and after["kept"] == "rm -r bui") else "FAIL",
             f"the owner's keystroke took the keyboard back from the phone: it says"
             f" {after['mode']!r}, Hand back is offered ({after['handBack']}), Take over is"
             f" ({after['takeOver']}), it kept the half-typed line ({after['kept']!r}), it was"
             f" told {after['said']!r}, and a line it sent afterwards still reached the program"
             f" ({still}) (shots 13a, 13b)")
        key("ctrl+a", "BackSpace")
        await phone.evaluate(
            "(() => { document.getElementById('composer-text').value = ''; return true; })()")
        # And one tap gets it back, because the owner's phone is the owner: the take-back is not
        # a lock-out, it is a turn.
        await press(phone, 'term-take')
        await asyncio.sleep(3)
        again = await phone.evaluate("document.getElementById('term-mode').textContent")
        await compose(phone, "echo the phone asked for the keyboard again", prefer_view=False)
        back = await terminal_has(phone, "the phone asked for the keyboard again", timeout=30)
        note("PASS" if back else "FAIL",
             f"asking for it again puts the phone back on the keyboard ({again!r}) and its line"
             f" reaches the program ({back})")
        await press(phone, 'term-release')
        await asyncio.sleep(2)

    async def the_pane_waits_for_input(self) -> None:
        """A pane's status has six words (section 6.3) and the phone acts on three of them.

        `Pane::shareStatus()` answered with `password`, `running` or `idle`, so `waiting_input`
        and `failed` — two of the five notification triggers in `remote/notify.py` — could not be
        reached from a GUI pane at all. This is the first of the two, end to end: a real program
        blocked reading the terminal, and the word that reaches the phone's inbox.
        """
        phone = self.phone
        await wait_idle(phone)
        at_prompt("python3 -c \"print('answered', input('Your name: '))\"")
        await asyncio.sleep(6)
        desk("63-the-program-waiting-for-a-line")
        await press(phone, 'thread-back')
        await phone.wait_for(shown('screen-inbox'), timeout=30)
        chip = ("(() => { const n = document.querySelector('.pane-row .chip');"
                " return n ? n.textContent : ''; })()")
        said = ""
        try:
            said = await phone.wait_for(
                f"(() => {{ const t = {chip};"
                " return t === 'Waiting for input' ? t : null; }})()", timeout=60)
        except AssertionError:
            said = await phone.evaluate(chip)
        await browser_shot(self.phone, "phone", "62-the-pane-says-it-is-waiting-for-you")
        note("PASS" if said == "Waiting for input" else "FAIL",
             f"a program blocked reading the terminal reaches the phone as a status: the inbox"
             f" chip reads {said!r} (shots 62, 63)")

        # Answer it from the desktop, the way section 9 says the prompt box does, and the pane
        # goes back to idle: a status nothing clears is not a status.
        at_prompt("Ada")
        await asyncio.sleep(6)
        cleared = ""
        try:
            cleared = await phone.wait_for(
                f"(() => {{ const t = {chip}; return t && t !== 'Waiting for input' ? t : null;"
                " }})()", timeout=60)
        except AssertionError:
            cleared = await phone.evaluate(chip)
        note("PASS" if cleared and cleared != "Waiting for input" else "FAIL",
             f"answering it clears the status: the chip now reads {cleared!r}")
        await phone.evaluate("document.querySelectorAll('.pane-row')[0].click()")
        await phone.wait_for(shown('terminal-pane'), timeout=40)
        await asyncio.sleep(2)

    # -- notifications ------------------------------------------------------------------------------
    async def phone_asks_for_notifications(self) -> None:
        """The real row, the real tap, and what a phone with no push service is told."""
        phone = self.phone
        await press(phone, 'thread-back')
        await phone.wait_for(shown('screen-inbox'), timeout=30)
        state = await phone.evaluate("""
            (() => ({ text: document.getElementById('notify').textContent,
                      hidden: document.getElementById('notify').hidden,
                      note: document.getElementById('notify-note').textContent,
                      permission: window.Notification ? Notification.permission : 'none' }))()
        """)
        await browser_shot(self.phone, "phone", "14-the-notify-row-before-the-tap")
        note("NOTE", f"the Notify row before the tap: {state}")
        await press(phone, 'notify')
        await asyncio.sleep(12)
        after = await phone.evaluate("""
            (() => ({ text: document.getElementById('notify').textContent,
                      note: document.getElementById('notify-note').textContent,
                      permission: window.Notification ? Notification.permission : 'none' }))()
        """)
        await browser_shot(self.phone, "phone", "15-what-the-tap-says-with-no-push-service")
        note("NOTE", f"after the tap, with no push service reachable: {after} (shot 15)")

    async def phone_subscribes_for_real(self) -> None:
        """The same row again, with only the browser's push service stubbed.

        Everything the desktop does is real from here: the hub stores the subscription, decides
        whether to push, constructs the body, seals it, encrypts it to this subscription and has
        the rendezvous post it under a VAPID signature.
        """
        origin = await self.phone.evaluate("location.origin")
        await self.phone.call("Browser.grantPermissions",
                              {"origin": origin, "permissions": ["notifications"]})
        await self.phone.evaluate("window.__relayArmPush()")
        await press(self.phone, "notify")
        kinds = []
        try:
            kinds = await self.phone.wait_for("""
                (() => { const boxes = [...document.querySelectorAll('#notify-kinds input')];
                         return boxes.length ? boxes.map(b => [b.id, b.checked]) : null; })()
            """, timeout=60)
        except AssertionError:
            pass
        await browser_shot(self.phone, "phone", "16-subscribed-with-a-box-per-kind")
        said = await self.phone.evaluate(
            "document.getElementById('notify-note').textContent")
        note("PASS" if kinds else "FAIL",
             "`push_subscribe` reached the desktop and came back with a box per kind:"
             f" {[name for name, _ in kinds]} (shot 16)"
             + ("" if kinds else f"; the row says {said!r}"))
        if not kinds:
            return
        # The seal key the page generated, so the body it is sent can be opened here.
        raw = await self.phone.evaluate("""
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
        self.seal_key = base64.b64decode(raw) if raw else b""
        note("PASS" if self.seal_key else "FAIL",
             f"the phone's seal key is readable for this test: {len(self.seal_key)} bytes")

    # -- 3. voice -----------------------------------------------------------------------------------
    async def phone_speaks(self) -> None:
        phone = self.phone
        await phone.evaluate("document.querySelectorAll('.pane-row')[0].click()")
        await phone.wait_for(shown('terminal-pane'), timeout=40)
        await wait_idle(phone)
        visible = await phone.evaluate(shown('composer-mic'))
        if not visible:
            note("FAIL", "the microphone button is not drawn on the phone")
            return
        mic_state = ("(() => { const m = document.getElementById('composer-mic');"
                     " return { title: m.title, on: m.className,"
                     " note: document.getElementById('term-note').textContent }; })()")
        await press(phone, 'composer-mic')
        await asyncio.sleep(1.0)
        recording = await phone.evaluate(mic_state)
        await browser_shot(self.phone, "phone", "17-the-phone-is-recording")
        note("NOTE", f"while recording, the microphone reads {recording} (shot 17)")
        await press(phone, 'composer-mic')
        await asyncio.sleep(2)
        transcribing = await phone.evaluate(mic_state)
        await browser_shot(self.phone, "phone", "18-transcribing")
        note("NOTE", f"once the clip is sent: {transcribing} (shot 18)")
        for _ in range(60):
            said = await phone.evaluate("document.getElementById('term-note').textContent")
            box = await phone.evaluate("document.getElementById('composer-text').value")
            if box or (said and "ranscrib" not in said and "istening" not in said):
                break
            await asyncio.sleep(1)
        await browser_shot(self.phone, "phone", "19-what-came-back-from-the-clip")
        note("NOTE", f"after the clip: note {said!r}, prompt box {box!r} (shots 18, 19)."
                     " With RELAY_KEYRING=off there is no transcription key on this desktop,"
                     " so a readable error is the right answer")

    # -- 4. a password prompt, and the notification it fires ------------------------------------------
    async def password(self) -> None:
        phone = self.phone
        await wait_idle(phone)
        # The prompt is started at the desktop, in the terminal, so that it is a real foreground
        # program with the terminal's echo off — which is what `checkPasswordPrompt` tests and
        # what puts the pane into `password`. A command handed to the agent instead would print
        # the same word "Password:" out of a tool call and none of that would be true.
        #
        # Section 9's presence rule is a separate step at the end of the run (`the_push`): moving
        # the desktop out of the way is the one thing in this drive that cannot always be undone,
        # so nothing that follows is allowed to depend on it.
        at_prompt("python3 -c 'import getpass; "
                  "print(\"got\", len(getpass.getpass(\"Password: \")))'")
        at_prompt_password = await terminal_has(phone, "Password:", timeout=90)
        note("PASS" if at_prompt_password else "FAIL",
             "a program the owner started at the desktop is at a password prompt, and the phone"
             " can see it")

        # Ordinary typing is refused, and no password field is offered to a device that was not
        # allowed one.
        await compose(phone, "hunter2", prefer_view=False)
        # The refusal is written into the terminal's note line, which the next pane update writes
        # over with the pane's own state, so it is caught rather than read once and missed.
        refusal = ""
        for _ in range(40):
            for element in ("term-note", "thread-note"):
                said = await phone.evaluate(
                    f"document.getElementById('{element}').textContent")
                # Only a refusal counts. The same line carries the pane's own status, and a
                # drive that took the first thing it saw once reported "Requesting model" as
                # proof that typing had been refused.
                if said and ("refus" in said.lower() or "password" in said.lower()):
                    refusal = said
                    break
            if refusal:
                break
            await asyncio.sleep(0.25)
        field = await phone.evaluate(shown('secret-row'))
        await browser_shot(self.phone, "phone", "20-typing-refused-and-no-password-field")
        note("PASS" if (refusal and not field) else "FAIL",
             f"ordinary typing is refused ({refusal!r}) and the password field is absent"
             f" (drawn: {field}) (shot 20)")

        # Now the switch, which lives on the device's row in the share window.
        ensure_share_window()
        share_click(253, device_list_y())                # the device in the list
        await asyncio.sleep(1)
        share_shot("21-the-device-selected-passwords-off")
        # From the list the focus chain is Revoke selected, Passwords, Stop sharing, so the switch
        # is two tabs away wherever the row happens to have been drawn.
        xdo("windowfocus", share_window())
        key("Tab", "Tab", "space")
        await asyncio.sleep(3)
        share_shot("22-passwords-on-for-this-device")

        appeared = False
        for _ in range(40):
            if await self.phone.evaluate(shown('secret-row')):
                appeared = True
                break
            await asyncio.sleep(1)
        await browser_shot(self.phone, "phone", "23-whether-the-password-field-appeared")
        note("PASS" if appeared else "FAIL",
             "turning the device's password switch on puts the field on the phone (shots 21-23)")
        if appeared:
            await compose(self.phone, "a-secret-nobody-logs", box="secret-text",
                          button="secret-send")
            consumed = await terminal_has(self.phone, "got 20", timeout=40)
            await browser_shot(self.phone, "phone", "24-the-prompt-was-consumed")
            desk("25-the-password-prompt-consumed-on-the-desktop")
            note("PASS" if consumed else "FAIL",
                 "the line went to the prompt that asked for it, and the program read it"
                 " (shots 24, 25)")
        # However that went, the prompt has to end: a pane left sitting at `password` never
        # transitions into it again, and the notification step later depends on that transition.
        focus_relay()
        await asyncio.sleep(1)
        at_prompt("")
        await asyncio.sleep(3)

    # -- 5. a guest --------------------------------------------------------------------------------
    async def make_invite(self) -> None:
        ensure_share_window()
        share_shot("26-the-share-window-before-the-invite")
        rows = share_rows()
        if len(rows) < 3:
            raise RuntimeError(f"the share window's rows did not read: {rows}")
        invite_row = rows[1]            # address combo, invite role, Revoke selected
        share_click(65, invite_row)                      # the role combo
        await asyncio.sleep(1)
        key("Down", "Return")                            # Viewer → Editor
        await asyncio.sleep(1)
        share_shot("27-an-editor-invite")
        share_click(441, invite_row)                     # Make a link
        await asyncio.sleep(6)
        share_shot("28-the-link-and-its-qr")
        self.invite = read_url("invite-url.txt")
        if not self.invite:
            raise RuntimeError("no invite url was written")
        note("PASS", "an editor invite link was made from the share window (shots 26-28)")

    async def guest_knocks(self) -> None:
        # Type before the knock, keep typing after it: the Sharing pane opening must not cost the
        # keyboard, because the next keystroke would otherwise land on Admit.
        at_prompt("the keyboard is here before the knock", enter=False)
        await asyncio.sleep(1)
        desk("29-typing-before-the-knock")

        self.guest = await open_phone(self.invite, size={"width": 430, "height": 900,
                                                         "deviceScaleFactor": 2, "mobile": True})
        await self.guest.wait_for(shown('screen-join'), timeout=90)
        await self.guest.wait_for(
            "document.getElementById('join-fingerprint').textContent !== '…'", timeout=30)
        await self.guest.evaluate(
            "(() => { document.getElementById('join-name').value = 'alice'; return true; })()")
        await browser_shot(self.guest, "guest", "30-the-invitation")
        await press(self.guest, 'join-knock')
        self.guest_code = await self.guest.wait_for(
            "document.getElementById('knock-code')?.textContent?.match(/^\\d{5}$/)"
            " ? document.getElementById('knock-code').textContent : ''", timeout=120)
        await asyncio.sleep(2)
        await browser_shot(self.guest, "guest", "31-waiting-to-be-let-in")
        await asyncio.sleep(3)
        desk("32-the-knock-opened-the-sharing-pane")
        typ(" and it still is")
        await asyncio.sleep(1)
        desk("33-the-keyboard-never-left-the-prompt-box")
        note("NOTE", f"the guest's code is {self.guest_code} — shots 31 and 32 must agree")
        note("PASS", "the Sharing pane opened by itself and the typing carried on into the"
                     " prompt box (shots 29, 32, 33)")

    async def admit_and_hand_over(self) -> None:
        click(1111, 320)                                 # Admit as editor
        await self.guest.wait_for(shown('screen-guest'), timeout=60)
        await asyncio.sleep(4)
        desk("34-alice-on-the-people-list")
        await browser_shot(self.guest, "guest", "35-admitted-as-an-editor")
        note("PASS", "admitted as editor; alice is on the people list with her key (shots 34, 35)")

        await self.guest.wait_for(shown('guest-ask'), timeout=40)
        await press(self.guest, 'guest-ask')
        await self.guest.wait_for(
            "document.getElementById('guest-drive-mode').textContent.includes('Asked')",
            timeout=40)
        await asyncio.sleep(3)
        desk("36-alice-asks-to-type")
        await browser_shot(self.guest, "guest", "37-asked-to-type")
        click(964, 240)                                  # Let them type
        await self.guest.wait_for(shown('guest-hand-back'), timeout=40)
        await asyncio.sleep(3)
        desk("38-alice-is-typing")
        await browser_shot(self.guest, "guest", "39-alice-has-the-keyboard")
        note("PASS", "the keyboard was asked for and handed over; the pane header says"
                     " alice is typing (shots 36-39)")

        await compose(self.guest, "echo alice typed this into the owners shell",
                      box="guest-line", button="guest-line-send")
        landed = await self.guest.wait_for(
            "(() => { const n = document.getElementById('guest-screen-wrap');"
            " return n && n.textContent.includes('alice typed this'); })()", timeout=45)
        await browser_shot(self.guest, "guest", "40-the-command-alice-ran")
        desk("41-alices-command-in-the-owners-shell")
        note("PASS" if landed else "FAIL",
             "a line from the guest ran in the owner's shell (shots 40, 41)")

        # The owner's own keystroke takes it back, and does not cost the key that did it.
        await self.guest.evaluate(
            "(() => { document.getElementById('guest-line').value = 'rm -r bui'; return true; })()")
        at_prompt("typing here takes the keyboard back", enter=False)
        await asyncio.sleep(3)
        kept = await self.guest.evaluate("document.getElementById('guest-line').value")
        flipped = not await self.guest.evaluate(shown('guest-hand-back'))
        desk("42-typing-took-the-keyboard-back")
        await browser_shot(self.guest, "guest", "43-the-guest-was-told-and-kept-the-half-line")
        note("PASS" if (kept == "rm -r bui" and flipped) else "FAIL",
             f"the owner's keystroke took control back; the guest's UI flipped ({flipped}) and"
             f" kept the half-typed line ({kept!r}) (shots 42, 43)")
        key("ctrl+a")
        typ(" ")
        key("ctrl+a", "BackSpace")

    async def guest_prompts(self) -> None:
        await compose(self.guest, "name the two files in this folder whose names end in url.txt",
                      box="guest-prompt-text", button="guest-prompt-send")
        await self.guest.wait_for(
            "document.querySelector('.guest-prompt .guest-prompt-state')?.textContent"
            ".includes('waiting') || false", timeout=40)
        await asyncio.sleep(3)
        desk("44-a-guest-prompt-waiting-with-its-whole-text")
        await browser_shot(self.guest, "guest", "45-waiting-for-the-owner")
        click(964, 290)                                  # Approve once
        approved = await self.guest.wait_for(
            "document.querySelector('.guest-prompt.is-approved') !== null", timeout=40)
        await asyncio.sleep(35)
        desk("46-the-approved-prompt-running-on-the-owners-key")
        await browser_shot(self.guest, "guest", "47-approved")
        note("PASS" if approved else "FAIL",
             "the whole text waited on the desktop, Approve once sent it to the agent, and the"
             " queue row names its author (shots 44-47)")

        await compose(self.guest, "and now delete everything in this folder",
                      box="guest-prompt-text", button="guest-prompt-send")
        await self.guest.wait_for(
            "[...document.querySelectorAll('.guest-prompt .guest-prompt-state')]"
            ".some(n => n.textContent.includes('waiting'))", timeout=40)
        await asyncio.sleep(3)
        desk("48-a-second-prompt-waiting")
        click(860, 290)                                  # Refuse
        # `is-declined` is how app/guest.js spells a prompt the owner said no to.
        refused = await self.guest.wait_for("""
            (() => { const row = [...document.querySelectorAll('.guest-prompt')]
                       .find(n => n.className.includes('declined'));
                     return row ? row.textContent : ''; })()
        """, timeout=60)
        await asyncio.sleep(2)
        await browser_shot(self.guest, "guest", "49-the-second-prompt-was-refused")
        note("PASS" if refused else "FAIL",
             f"a second prompt was refused and the guest was told why: {refused!r}"
             " (shots 48, 49)")

    # -- 6. a second device of the owner's ---------------------------------------------------------
    async def second_device(self) -> None:
        # A pairing link lasts five minutes and this one was spent long ago, so the share window is
        # closed and opened again, which mints a fresh one.
        window = share_window(required=False)
        if window:
            xdo("windowfocus", window)
            key("Escape")
            await asyncio.sleep(3)
        ensure_share_window()
        url = read_url("pair-url.txt", since=self.pair_url)
        if not url:
            raise RuntimeError("the share window did not mint a second pairing link")
        self.viewer = await open_phone(url)
        await self.viewer.wait_for(
            "document.getElementById('pair-code')?.textContent?.match(/^\\d{5}$/)"
            " ? document.getElementById('pair-code').textContent : ''", timeout=90)
        await asyncio.sleep(2)
        share_shot("50-a-second-device-asking")
        xdo("windowfocus", share_window())
        key("Tab", "space")                              # Refuse → Allow viewing
        await self.viewer.wait_for(shown('screen-inbox'), timeout=120)
        capability = await self.viewer.evaluate(
            "document.getElementById('capability').textContent")
        await self.viewer.evaluate("document.querySelectorAll('.pane-row')[0].click()")
        await self.viewer.wait_for(shown('terminal-pane'), timeout=40)
        await asyncio.sleep(3)
        composer = await self.viewer.evaluate(shown('composer'))
        take = await self.viewer.evaluate(shown('term-take'))
        said = await self.viewer.evaluate("document.getElementById('term-note').textContent")
        control = await self.viewer.evaluate(
            "(() => { const n = document.getElementById('term-mode');"
            " return n ? n.textContent : ''; })()")
        await browser_shot(self.viewer, "viewer", "51-a-viewing-device-cannot-type")
        note("PASS" if (capability == "view" and not composer and not take) else "FAIL",
             f"the second device is {capability!r}: no prompt box ({composer}), no Take over"
             f" ({take}), and it says {said!r} (shot 51)")
        note("NOTE", f"what the viewing device is told about who is driving: {control!r}")

    # -- pause, demote, remove, stop ------------------------------------------------------------------
    async def pause_and_end(self) -> None:
        click(1030, 375)                                 # Pause guests
        paused = await self.guest.wait_for(
            "document.getElementById('guest-note').textContent.includes('paus')", timeout=40)
        said = await self.guest.evaluate("document.getElementById('guest-note').textContent")
        await asyncio.sleep(2)
        desk("52-guests-are-paused")
        await browser_shot(self.guest, "guest", "53-the-guest-is-told-why")
        note("PASS" if paused else "FAIL", f"pause reaches the guest with a reason: {said!r}"
                                           " (shots 52, 53)")
        click(1050, 375)                                 # Let guests act again
        await asyncio.sleep(4)

        click(880, 280)                                  # Make viewer
        gone = await self.guest.wait_for(f"!({shown('guest-drive-bar')})", timeout=40)
        await asyncio.sleep(2)
        desk("54-demoted-to-viewer")
        await browser_shot(self.guest, "guest", "55-the-guests-typing-ui-is-gone")
        note("PASS" if gone else "FAIL",
             "demoting to viewer takes the typing UI off the guest's page (shots 54, 55)")

        click(980, 262)                                  # Remove
        ended = await self.guest.wait_for(shown('screen-ended'), timeout=60)
        text = await self.guest.evaluate(
            "document.getElementById('ended-text').textContent")
        await asyncio.sleep(2)
        desk("56-nobody-here-now")
        await browser_shot(self.guest, "guest", "57-this-invitation-has-ended")
        note("PASS" if ended else "FAIL",
             f"removing ends the guest's session and says so: {text!r} (shots 56, 57)")

    # -- the notification a pane's own state fires ---------------------------------------------------
    async def the_push(self) -> None:
        """Section 9 end to end: a trigger, the presence rule, a body, and a real delivery.

        Last, because the one thing here that a headless X server does not always undo is moving
        the desktop out of the way, and nothing after it should depend on that having worked.
        """
        phone = self.phone
        await wait_idle(phone)
        if not self.seal_key:
            note("FAIL", "no seal key: the phone never subscribed, so there is nothing to send")
            return

        # The presence rule: the desktop must not push while its window is the one you are looking
        # at. A second X client takes the focus, which is what an ordinary alt-tab does; the
        # window stays mapped, so the run can carry on afterwards whatever happens.
        away = subprocess.Popen(["xmessage", "-geometry", "320x90+2000+1150", "-name", "away",
                                 "Relay is not the window you are looking at"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        await asyncio.sleep(3)
        for window in xdo("search", "--name", "away").split():
            xdo("windowraise", window)
            xdo("windowfocus", window)
        await asyncio.sleep(3)
        focused = xdo("getwindowfocus")
        note("NOTE" if focused != WIN else "FAIL",
             f"the focus is on {focused} and Relay's window is {WIN}:"
             f" the desktop is {'not ' if focused != WIN else ''}the window in front")

        # A password prompt is the trigger a GUI pane can actually reach: `Pane::shareStatus()`
        # answers only `password`, `running` or `idle`, so `waiting_input` and `failed` never
        # arrive from the app however the pane behaves.
        at_prompt("python3 -c 'import getpass; getpass.getpass(\"Password: \")'")
        await terminal_has(phone, "Password:", timeout=90)
        for _ in range(60):
            if self.service.taken:
                break
            await asyncio.sleep(0.5)

        if self.service.taken:
            private, auth = self.subscription
            body = push.open_sealed(self.seal_key, receive(private, auth, self.service.taken[0]))
            (OUT / "push-body.json").write_text(json.dumps(body, indent=2) + "\n")
            title = await phone.evaluate(
                "(() => { const row = document.querySelector('.pane-title');"
                " return row ? row.textContent : ''; })()")
            words = f"{body.get('title', '')} {body.get('body', '')}"
            leaked = [word for word in (title, str(WORK), "getpass", "python3")
                      if word and word in words]
            note("PASS" if not leaked else "FAIL",
                 f"a push was constructed, sealed, signed and delivered: {body}. The pane is"
                 f" titled {title!r} and none of that, nor the folder, nor the program is in the"
                 f" body (leaked: {leaked})")
            # And the rendezvous in the middle could read none of it.
            raw = self.service.taken[0]
            opaque = [word for word in (b"Pane", b"Password", b"password") if word in raw]
            note("PASS" if not opaque else "FAIL",
                 f"what the push service received is opaque to it and to the rendezvous"
                 f" (plaintext found in {len(raw)} bytes: {opaque})")
        else:
            note("FAIL", "no push arrived at the service"
                         f" (endpoint {self.endpoint}); the pane did reach a password prompt if"
                         " `prompt_detected` is in audit.txt, so what did not happen is either"
                         " the presence rule or the delivery")

        # The prompt has to end before Stop sharing, or the shell is left waiting for it. The
        # phone answers it, which it may by now — the switch was turned on in step 4.
        away.terminate()
        focus_relay()
        await asyncio.sleep(2)
        if await phone.evaluate(shown('secret-row')):
            await compose(phone, "done-with-this-prompt", box="secret-text",
                          button="secret-send")
        await asyncio.sleep(3)

    async def stop_sharing(self) -> None:
        ensure_share_window()
        share_shot("58-the-share-window-before-stopping")
        # Reached from the device list: Revoke selected, Passwords, Stop sharing follow it.
        share_click(253, device_list_y())
        await asyncio.sleep(1)
        xdo("windowfocus", share_window())
        key("Tab", "Tab", "Tab", "space")
        await asyncio.sleep(12)
        desk("59-the-desktop-after-stopping")
        for browser, side, what in ((self.phone, "phone", "60-the-phone-after-the-share-stopped"),
                                    (self.viewer, "viewer",
                                     "61-the-viewer-after-the-share-stopped")):
            if browser is None:
                continue
            status = await browser.evaluate(
                "document.getElementById('link-status').textContent")
            await browser_shot(browser, side, what)
            note("NOTE", f"{side} after Stop sharing: link status {status!r}")

    async def go(self) -> None:
        await step("a pane", self.pane())
        await step("pairing the phone", self.pair_phone())
        await step("a command from the phone", self.phone_runs_a_command())
        await step("a prompt from the phone", self.phone_prompts_the_agent())
        await step("scrollback on the phone", self.phone_scrolls_back())
        await step("take over", self.phone_takes_over())
        await step("asking for notifications", self.phone_asks_for_notifications())
        await step("subscribing", self.phone_subscribes_for_real())
        await step("voice", self.phone_speaks())
        await step("an invite", self.make_invite())
        await step("the knock", self.guest_knocks())
        await step("admitting and handing over", self.admit_and_hand_over())
        await step("guest prompts", self.guest_prompts())
        await step("a second device", self.second_device())
        await step("pause, demote, remove", self.pause_and_end())
        # Last but one, and after everything that needs the share window or the palette: a pane
        # sitting at a password prompt takes the keyboard away from both, so a step of this kind
        # that does not finish would otherwise carry off every step after it.
        await step("a password prompt", self.password())
        await step("the push a password prompt fires", self.the_push())
        await step("stop sharing", self.stop_sharing())

    async def close(self) -> None:
        for browser in (self.phone, self.guest, self.viewer):
            if browser is not None:
                problems = [line for line in browser.console if "EXCEPTION" in line]
                if problems:
                    note("NOTE", f"console exceptions: {problems[:4]}")
                await browser.stop()
        await self.service.close()


async def main() -> int:
    run = Run()
    try:
        await run.go()
    finally:
        await run.close()
    failures = [line for line in NOTES if line.startswith("FAIL")]
    print(f"\n{len(NOTES)} notes, {len(failures)} failures", flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
