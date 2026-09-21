# SPDX-License-Identifier: AGPL-3.0-or-later
"""#PH0N wave 3, driven once against the real hosted rendezvous (drive.sh runs this).

Three actors take turns: the desktop (xdotool against the Xvfb window this run starts Relay in),
the owner's phone (a headless Chrome at 390×844 on https://join.relay-terminal.ai) and a guest
called alice (a second headless Chrome). The rendezvous in the middle is the production one at
join.relay-terminal.ai; nothing is stubbed there. The one stub in the browser is
`pushManager.subscribe` (step 10), because no push service is reachable from this machine, and
the one stand-in on the desktop is the fake model (fake-model.py), which is a local endpoint like
any other.

Every step writes what it saw to notes.txt with PASS/FAIL/NOTE in front of it, and a step that
throws is recorded and the run carries on: one run should surface every problem, not the first.
"""
from __future__ import annotations

import asyncio
import base64
import hashlib
import json
import os
import re
import signal
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))

from remote import ws                                                    # noqa: E402
from tests.browser import SCREENS_SHOWN, Browser, shown                  # noqa: E402

OUT = Path(os.environ["RELAY_QA_OUT"])
WORK = Path(os.environ["RELAY_QA_WORK"])
JAIL = Path(os.environ["RELAY_QA_JAIL"])
BUILD = Path(os.environ["RELAY_QA_BUILD"])
HOSTED = "https://join.relay-terminal.ai"

NOTES: list[str] = []
TIMES: list[tuple[str, float]] = []


def note(verdict: str, text: str) -> None:
    line = f"{verdict:4} {text}"
    print(line, flush=True)
    NOTES.append(line)
    (OUT / "notes.txt").write_text("\n".join(NOTES) + "\n")


def shot_name(side: str, what: str) -> str:
    number, _, rest = what.partition("-")
    return f"{number}-{side}-{rest}.png"


# ---- the desktop ---------------------------------------------------------------------------------

WIN = ""
RELAY: subprocess.Popen | None = None


def xdo(*args: str) -> str:
    return subprocess.run(["xdotool", *args], capture_output=True, text=True).stdout.strip()


def relay_children() -> list[str]:
    """The command lines of Relay's child processes — the sidecar is `python3 … gui_host`."""
    if RELAY is None:
        return []
    out = subprocess.run(["pgrep", "-a", "-P", str(RELAY_PID)], capture_output=True, text=True).stdout
    return [line for line in out.splitlines() if line.strip()]


def sidecar_running() -> bool:
    return any("gui_host" in line for line in relay_children())


RELAY_PID = 0          # the relay process itself, which under gdb is not RELAY.pid
GDB = os.environ.get("RELAY_QA_GDB") == "1"


def start_relay(tag: str) -> None:
    """Start Relay on the run's profile and find its window. `tag` names the stderr log.

    With RELAY_QA_GDB=1 it runs under gdb in batch mode, which prints a full backtrace into the
    stderr log if it crashes (the in-process handler's frames name the function but not the
    line). SIGTERM and SIGPIPE are passed straight through so a quit is still an ordinary quit.
    """
    global WIN, RELAY, RELAY_PID
    log = open(OUT / f"relay-stderr{tag}.log", "ab")
    command = [str(BUILD / "relay"), "--workspace", str(WORK)]
    if GDB:
        command = ["gdb", "-q", "-batch", "-ex", "set debuginfod enabled off",
                   "-ex", "handle SIGTERM nostop noprint pass", "-ex", "handle SIGPIPE nostop noprint pass",
                   "-ex", "run", "-ex", "bt 40", "-ex", "info locals", "--args", *command]
    RELAY = subprocess.Popen(command, stdout=log, stderr=log)
    RELAY_PID = RELAY.pid
    for _ in range(60):
        time.sleep(1)
        if GDB:
            found = subprocess.run(["pgrep", "-P", str(RELAY.pid), "-f", "build/relay"],
                                   capture_output=True, text=True).stdout.split()
            if not found:
                continue
            RELAY_PID = int(found[0])
        best = ""
        area = 0
        for w in xdo("search", "--onlyvisible", "--pid", str(RELAY_PID), "--name", "^Relay").split():
            geometry = xdo("getwindowgeometry", "--shell", w)
            width = int(re.search(r"WIDTH=(\d+)", geometry).group(1)) if "WIDTH=" in geometry else 0
            height = int(re.search(r"HEIGHT=(\d+)", geometry).group(1)) if "HEIGHT=" in geometry else 0
            if width * height > area:
                best, area = w, width * height
        if best and area > 200000:
            WIN = best
            break
    if not WIN:
        raise RuntimeError("no Relay window")
    xdo("windowmove", WIN, "0", "0")
    xdo("windowsize", WIN, "1600", "1000")
    xdo("windowfocus", WIN)
    time.sleep(6)          # the first pane's shell and the worker


def stop_relay() -> str:
    """SIGTERM, which main.cpp turns into an ordinary quit; returns how it ended."""
    global RELAY
    if RELAY is None:
        return "not running"
    os.kill(RELAY_PID, signal.SIGTERM)
    try:
        code = RELAY.wait(60)
        ended = f"exit {code}"
        if GDB:
            # gdb's own exit code says nothing; the inferior's line in the log does.
            text = "".join(open(path, errors="replace").read() for path in OUT.glob("relay-stderr*.log"))
            match = re.findall(r"\[Inferior 1 \(process \d+\) (exited[^\]]*)\]", text)
            ended = match[-1] if match else f"gdb exit {code}"
    except subprocess.TimeoutExpired:
        RELAY.kill()
        ended = "killed after 40 s"
    RELAY = None
    return ended


def desk(what: str) -> str:
    name = shot_name("desktop", what)
    subprocess.run(["import", "-window", WIN, str(OUT / name)], check=False)
    print("shot", name, flush=True)
    return name


def root_shot(what: str) -> str:
    """The whole screen: the Relay window and whatever popup is over it."""
    name = shot_name("desktop", what)
    # Wider than the Relay window: the plug's menu opens off its right edge.
    subprocess.run(["import", "-window", "root", "-crop", "1900x1000+0+0", str(OUT / name)], check=False)
    print("shot", name, flush=True)
    return name


def share_window(required: bool = True) -> str:
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


WIDGET = (37, 29, 25)          # a button, a combo box, a spin box, a line edit (the 2026-09-20 theme)
LIST_FILL = (21, 22, 26)       # the paired-device list


def _bands(column: int, colour: tuple[int, int, int]) -> list[tuple[int, int]]:
    """Every run of `colour` down one column of the share window, as (top, bottom) pairs.
    The window's rows move with the wrapped text above them, so they are read off a photograph."""
    from PIL import Image

    path = OUT / ".share-probe.png"
    subprocess.run(["import", "-window", share_window(), str(path)], check=False)
    image = Image.open(path).convert("RGB")
    width, height = image.size
    column = min(max(column, 0), width - 1)
    hits = [y for y in range(height)
            if all(abs(a - b) <= 10 for a, b in zip(image.getpixel((column, y)), colour))]
    bands: list[list[int]] = []
    for y in hits:
        if bands and y - bands[-1][1] <= 10:
            bands[-1][1] = y
        else:
            bands.append([y, y])
    return [(top, bottom) for top, bottom in bands if bottom - top >= 16]


def _list_has_text() -> bool:
    """Whether the paired-device list (the LIST_FILL box) has text drawn in it."""
    from PIL import Image

    path = OUT / ".share-probe.png"
    subprocess.run(["import", "-window", share_window(), str(path)], check=False)
    image = Image.open(path).convert("RGB")
    width, height = image.size
    # The list is the 90 px box just above the bottom row of buttons, whatever wrapped above it:
    # its rows are drawn in the text colour, and nothing else in that band is that bright.
    return any(sum(image.getpixel((x, y))) > 500
               for y in range(height - 145, height - 55, 2) for x in range(14, min(300, width), 2))


def share_rows() -> list[int]:
    return [(top + bottom) // 2 for top, bottom in _bands(65, WIDGET)]


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
    focus_relay()
    click(*PROMPT_BOX)
    time.sleep(0.3)
    key("ctrl+shift+a")
    time.sleep(1.2)
    typ(action)
    time.sleep(1.2)
    key("Return")


def options_toggle_remote() -> None:
    """Options › Remote's switch, the way the stub run did it: open Options, search for the row,
    Return toggles it. Then Options is closed again (the same key toggles the pane away) so the
    pane's prompt box is back where the rest of the run expects it."""
    focus_relay()
    click(*PROMPT_BOX)
    time.sleep(0.3)
    key("ctrl+shift+o")
    time.sleep(2.5)
    typ("remote control")
    time.sleep(2)
    key("Return")
    time.sleep(3)


def close_options() -> None:
    """Esc closes the Options pane from its search box, where `options_toggle_remote` leaves the
    focus. Ctrl+Shift+O would only toggle it away while the Options leaf is the active one, and
    after a menu or a dialog it is not."""
    key("Escape")
    time.sleep(2)


def park_share_window() -> None:
    window = share_window(required=False)
    if window:
        xdo("windowmove", window, "1700", "10")


def ensure_share_window() -> str:
    if not share_window(required=False):
        palette("Share this pane")
        for _ in range(40):
            time.sleep(1)
            if share_window(required=False):
                break
    park_share_window()
    time.sleep(1)
    return share_window()


def close_share_window() -> None:
    window = share_window(required=False)
    if window:
        xdo("windowfocus", window)
        key("Escape")
        time.sleep(2)


def read_url(name: str, since: str = "") -> str:
    path = JAIL / name
    for _ in range(120):
        if path.is_file():
            text = path.read_text().strip()
            if text and text != since:
                return text
        time.sleep(0.5)
    return ""


def health() -> dict:
    # The rendezvous answers a browser's or the sidecar's User-Agent; urllib's own gets a 403.
    request = urllib.request.Request(f"{HOSTED}/v1/health", headers={"User-Agent": ws.USER_AGENT})
    with urllib.request.urlopen(request, timeout=10) as reply:
        return json.loads(reply.read())


def desktops_online() -> int:
    return int(health().get("desktops", -1))


def wait_desktops(expected: int, timeout: float = 90) -> int:
    deadline = time.monotonic() + timeout
    seen = -1
    while time.monotonic() < deadline:
        seen = desktops_online()
        if seen == expected:
            return seen
        time.sleep(2)
    return seen


def requests_for(scene: str) -> int:
    """How many model requests with tools the fake model answered for one scene."""
    count = 0
    for line in (OUT / "requests.jsonl").read_text().splitlines():
        row = json.loads(line)
        if row.get("scene") == scene and row.get("tools"):
            count += 1
    return count


# ---- the phone and the browsers -------------------------------------------------------------------

PHONE = {"width": 390, "height": 844, "deviceScaleFactor": 2, "mobile": True}

# Keeps a handle on every WebSocket the page opens, so step 9 can cut the live one from inside the
# page — the way a phone in a tunnel loses it — without reaching into the app's own module scope.
SOCKET_TRACKER = """
  (() => {
    const Real = window.WebSocket;
    const opened = [];
    const Tracked = function (...args) {
      const socket = new Real(...args);
      opened.push(socket);
      return socket;
    };
    Tracked.prototype = Real.prototype;
    for (const name of ['CONNECTING', 'OPEN', 'CLOSING', 'CLOSED']) Tracked[name] = Real[name];
    window.WebSocket = Tracked;
    Object.defineProperty(window, '__relaySockets', { value: opened });
  })();
"""


async def browser_shot(browser: Browser, side: str, what: str) -> str:
    name = shot_name(side, what)
    result = await browser.call("Page.captureScreenshot", {"format": "png", "captureBeyondViewport": False})
    (OUT / name).write_bytes(base64.b64decode(result["data"]))
    print("shot", name, flush=True)
    return name


async def open_phone(url: str, *, size: dict | None = None, track_sockets: bool = False,
                     stub_push: bool = False) -> Browser:
    browser = Browser()
    await browser.start()
    await browser.call("Emulation.setDeviceMetricsOverride", size or PHONE)
    if track_sockets:
        await browser.call("Page.addScriptToEvaluateOnNewDocument", {"source": SOCKET_TRACKER})
    if stub_push:
        await install_push_stub(browser)
    await browser.navigate(url)
    return browser


async def install_push_stub(browser: Browser) -> None:
    """`pushManager.subscribe` answered without a push service (the one browser stub, step 10).

    A headless Chrome here has no Google push service to talk to. The subscription it would have
    returned is faked with a P-256 key the page never needs to open; everything after it — the
    page's own seal key, `push_subscribe` inside the Noise session, the desktop storing the
    choice and answering with the switches — is the real thing. Delivery is Phase 3, on the phone.
    """
    from tests.test_remote_push import subscription_keypair
    _, public, auth = subscription_keypair()
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
    """ % (json.dumps("https://push.invalid/ph0n-drive/subscription"), json.dumps(b64(public)),
           json.dumps(b64(auth)))
    await browser.call("Page.addScriptToEvaluateOnNewDocument", {"source": script})


def screen_text(selector: str = ".screen-grid") -> str:
    return (f"(() => {{ const n = document.querySelector({json.dumps(selector)});"
            f" return n ? n.textContent : ''; }})()")


async def compose(browser: Browser, text: str, box: str = "composer-text",
                  button: str = "composer-send", prefer_view: bool = True) -> str:
    """Type into the prompt box that is on screen and press its send, as a thumb would."""
    used = await browser.evaluate(f"""
        (() => {{
          const text = {json.dumps(text)};
          const wanted = {json.dumps(box)};
          const view = wanted === 'composer-text' && {json.dumps(bool(prefer_view))}
            ? document.querySelector('.rp-composer:not([hidden]) .rp-input') : null;
          const send = view ? view.closest('.rp-composer').querySelector('.rp-send') : null;
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


async def click_first(browser: Browser, selector: str) -> str:
    result = await browser.evaluate(f"""
        (() => {{
          const node = document.querySelector({json.dumps(selector)});
          if (!node) return 'MISSING';
          try {{ node.click(); return ''; }}
          catch (error) {{ return 'THREW ' + String(error && (error.stack || error)); }}
        }})()
    """)
    if result:
        note("NOTE", f"clicking {selector}: {result[:300]}")
    return result


async def terminal_has(browser: Browser, needle: str, timeout: float = 40) -> bool:
    try:
        await browser.wait_for(f"{screen_text()}.includes({json.dumps(needle)})", timeout=timeout)
        return True
    except AssertionError:
        return False
    except RuntimeError as error:
        note("NOTE", f"looking for {needle!r} on the screen: {error}")
        return False


CLOCK = "(() => { const n = document.querySelector('.rp-clock'); return n ? n.textContent : ''; })()"
LINK = "document.getElementById('link-status').textContent"
CHIPS = ("(() => [...document.querySelectorAll('[data-pane-chip]')]"
         ".map(n => n.textContent))()")


async def wait_idle(browser: Browser, timeout: float = 120) -> str:
    deadline = asyncio.get_running_loop().time() + timeout
    last = ""
    while asyncio.get_running_loop().time() < deadline:
        last = await browser.evaluate(CLOCK) or ""
        if not last.strip():
            return last
        await asyncio.sleep(1)
    note("NOTE", f"the pane was still working after {timeout:.0f} s: {last!r}")
    return last


async def open_pane_row(browser: Browser, index: int = 0) -> None:
    await browser.evaluate(f"document.querySelectorAll('.pane-row')[{index}].click()")
    await browser.wait_for(shown('terminal-pane'), timeout=40)
    await browser.wait_for("document.querySelectorAll('.screen-row').length > 1", timeout=40)


async def back_to_inbox(browser: Browser) -> None:
    await press(browser, "thread-back")
    await browser.wait_for(shown('screen-inbox'), timeout=30)


async def step(name: str, coroutine) -> None:
    started = time.monotonic()
    try:
        await coroutine
    except Exception as error:                                           # noqa: BLE001
        note("FAIL", f"{name}: {type(error).__name__}: {error}")
    TIMES.append((name, time.monotonic() - started))


# ---- the run ----------------------------------------------------------------------------------------

class Run:
    def __init__(self) -> None:
        self.phone: Browser | None = None
        self.guest: Browser | None = None
        self.pair_url = ""
        self.invite = ""
        self.desktops_before = -1
        self.desktop_id = ""
        self.device_id = ""

    # -- 1. off by default -----------------------------------------------------------------------------
    async def starts_off(self) -> None:
        self.desktops_before = desktops_online()
        start_relay("")
        await asyncio.sleep(8)
        children = relay_children()
        desk("01-remote-control-off-at-start")
        conf = (JAIL / "config/RelayTerminal/relay.conf").read_text()
        note("PASS" if not sidecar_running() else "FAIL",
             f"a fresh profile starts with remote control off: Relay's children after 14 s are"
             f" {[c.split(' ', 1)[1][:60] for c in children]!r} (no gui_host sidecar); relay.conf has"
             f" no [remote] section: {'[remote]' not in conf}; join.relay-terminal.ai lists"
             f" {self.desktops_before} desktop(s) online before this run (shot 01)")

    # -- 2. the switch -------------------------------------------------------------------------------
    async def switch_on(self) -> None:
        options_toggle_remote()
        desk("02-options-remote-just-switched-on")
        up = False
        for _ in range(30):
            if sidecar_running():
                up = True
                break
            await asyncio.sleep(1)
        registered = wait_desktops(self.desktops_before + 1, timeout=90)
        await asyncio.sleep(3)
        close_options()
        # The Remote tab itself, with the status line and the base URL under it: Options opened
        # fresh beside the one pane sits on the right half, so its tab strip is where shot 03 of
        # the earlier run had it.
        focus_relay()
        click(*PROMPT_BOX)
        key("ctrl+shift+o")
        await asyncio.sleep(2.5)
        click(1490, 150)                                # the Remote tab
        await asyncio.sleep(1.5)
        desk("03-options-remote-online")
        close_options()
        # And the plug's menu: its first line is the same sentence.
        click(1481, 26)
        await asyncio.sleep(1.5)
        root_shot("03a-the-plug-menu-status-line")
        key("Escape")
        await asyncio.sleep(1)
        conf = (JAIL / "config/RelayTerminal/relay.conf").read_text()
        remembered = re.search(r"\[remote\][^\[]*alwaysOn=true", conf, re.S) is not None
        note("PASS" if (up and registered == self.desktops_before + 1 and remembered) else "FAIL",
             f"Options › Remote's switch starts the sidecar ({up}), the desktop registers at"
             f" join.relay-terminal.ai (health desktops {self.desktops_before} → {registered}), and"
             f" relay.conf remembers remote/alwaysOn=true ({remembered}); the address row is the"
             f" default, relay-terminal.ai (shots 02, 03, 03a)")
        await asyncio.sleep(1)
        desk("04-back-to-the-pane-with-the-service-on")

    # -- 3. pair the phone -----------------------------------------------------------------------------
    async def pair_phone(self) -> None:
        focus_relay()
        click(*PROMPT_BOX)
        key("ctrl+e")                                   # a second pane
        await asyncio.sleep(5)
        ensure_share_window()
        share_shot("05-the-share-window-pairing-through-the-hosted-address")
        url = read_url("pair-url.txt")
        if not url:
            raise RuntimeError("no pairing url was written")
        self.pair_url = url
        hosted = url.startswith(f"{HOSTED}/pair#")
        note("PASS" if hosted else "FAIL",
             f"the pairing link points at the hosted app: {url.split('#')[0]}#… ({len(url)} chars)")
        # The desktop id, from the public key in the link — the rendezvous derives it the same way.
        fragment = dict(part.split("=", 1) for part in url.split("#", 1)[1].split("&"))
        public = base64.urlsafe_b64decode(fragment["d"] + "=" * (-len(fragment["d"]) % 4))
        self.desktop_id = hashlib.sha256(public).hexdigest()[:32]

        self.phone = await open_phone(url, track_sockets=True, stub_push=True)
        code = await self.phone.wait_for(
            "document.getElementById('pair-code')?.textContent?.match(/^\\d{5}$/)"
            " ? document.getElementById('pair-code').textContent : ''", timeout=90)
        await browser_shot(self.phone, "phone", "06-the-code-on-the-phone")
        await asyncio.sleep(2)
        share_shot("07-the-same-code-on-the-desktop")
        note("NOTE", f"the phone shows {code}; shot 07 is the desktop's — compare them")
        xdo("windowfocus", share_window())
        await asyncio.sleep(1)
        key("Tab", "Tab", "space")                      # Refuse holds focus; third is Allow typing
        await self.phone.wait_for(shown('screen-inbox'), timeout=120)
        capability = await self.phone.evaluate("document.getElementById('capability').textContent")
        link = await self.phone.evaluate(LINK)
        await browser_shot(self.phone, "phone", "08-paired-full-inbox")
        note("PASS" if capability == "full" else "FAIL",
             f"the phone paired with Allow typing and reads {capability!r}, link {link!r} (shot 08)")

        # The connect token: on the device record the page keeps, presented as `ct` on connect.
        record = await self.phone.evaluate("""
            (async () => {
              const db = await new Promise((res, rej) => {
                const r = indexedDB.open('relay-remote', 1);
                r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error);
              });
              const row = await new Promise((res) => {
                const r = db.transaction('device').objectStore('device').get('paired');
                r.onsuccess = () => res(r.result); r.onerror = () => res(null);
              });
              if (!row) return null;
              return { desktopId: row.desktopId, deviceId: row.deviceId,
                       tokenLength: (row.connectToken || '').length };
            })()
        """)
        self.device_id = (record or {}).get("deviceId", "")
        has_token = bool(record and record.get("tokenLength"))
        same_id = bool(record and record.get("desktopId") == self.desktop_id)
        note("PASS" if (has_token and same_id) else "FAIL",
             f"the phone holds a connect token ({(record or {}).get('tokenLength', 0)} chars) for"
             f" desktop {self.desktop_id[:8]}… (the record's id matches the link's key: {same_id})")

        # A raw socket to the same desktop with no `ct`: refused like an unknown desktop (4404).
        for label, query in (("no token at all", f"desktop={self.desktop_id}&device={self.device_id}"),
                             ("a made-up token", f"desktop={self.desktop_id}&device={self.device_id}&ct=nonsense")):
            outcome = ""
            try:
                socket = await ws.connect(f"wss://join.relay-terminal.ai/v1/connect?{query}")
                try:
                    await asyncio.wait_for(socket.recv(), 15)
                    outcome = "the rendezvous opened a channel"
                except ws.ConnectionClosed as closed:
                    outcome = f"closed {closed.code} {closed.reason!r}"
                except asyncio.TimeoutError:
                    outcome = "no answer in 15 s"
                await socket.close()
            except ws.ConnectionClosed as closed:
                outcome = f"closed {closed.code} {closed.reason!r}"
            except Exception as error:                                   # noqa: BLE001
                outcome = f"{type(error).__name__}: {error}"
            note("PASS" if "4404" in outcome else "FAIL",
                 f"a connect with {label} to the paired desktop is refused: {outcome}")

    # -- 4. the inbox lists every pane -------------------------------------------------------------------
    async def inbox(self) -> None:
        phone = self.phone
        close_share_window()
        rows = await phone.wait_for(
            "document.querySelectorAll('.pane-row').length >= 2 ? document.querySelectorAll('.pane-row').length : 0",
            timeout=30)
        chips = await phone.evaluate(CHIPS)
        await browser_shot(phone, "phone", "09-inbox-two-panes-with-chips")
        note("PASS" if rows == 2 else "FAIL",
             f"the inbox lists both panes with no share button pressed: {rows} rows, chips {chips}"
             " (shot 09)")
        focus_relay()
        click(*PROMPT_BOX)
        key("ctrl+e")                                   # a third pane, on the desktop
        try:
            rows = await phone.wait_for(
                "document.querySelectorAll('.pane-row').length >= 3 ? document.querySelectorAll('.pane-row').length : 0",
                timeout=40)
        except AssertionError:
            rows = await phone.evaluate("document.querySelectorAll('.pane-row').length")
        await asyncio.sleep(1)
        await browser_shot(phone, "phone", "10-inbox-the-third-pane-appeared")
        desk("11-three-panes-on-the-desktop")
        note("PASS" if rows == 3 else "FAIL",
             f"a pane opened on the desktop appears in the inbox by itself: {rows} rows (shots 10, 11)")

    # -- 5. a prompt from the phone ---------------------------------------------------------------------
    async def prompt(self) -> None:
        phone = self.phone
        await open_pane_row(phone, 0)
        drawn = await phone.evaluate(SCREENS_SHOWN)
        await browser_shot(phone, "phone", "12-the-pane-view")
        used = await compose(phone, "QUICK in one sentence, what does pwd print?")
        await back_to_inbox(phone)
        running = ""
        try:
            running = await phone.wait_for(
                f"(() => {{ const c = {CHIPS}; return c.find(t => /^running · \\d+s$/.test(t)) || ''; }})()",
                timeout=30)
        except AssertionError:
            running = json.dumps(await phone.evaluate(CHIPS))
        await browser_shot(phone, "phone", "13-inbox-running-with-the-clock")
        finished = ""
        try:
            finished = await phone.wait_for(
                f"(() => {{ const c = {CHIPS}; return c.find(t => t === 'finished') || ''; }})()", timeout=90)
        except AssertionError:
            finished = json.dumps(await phone.evaluate(CHIPS))
        await browser_shot(phone, "phone", "14-inbox-finished")
        await open_pane_row(phone, 0)
        answered = await terminal_has(phone, "came from the fake model", timeout=30)
        await browser_shot(phone, "phone", "15-the-answer-on-the-phone")
        desk("16-the-answer-in-the-pane")
        note("PASS" if (answered and running.startswith("running") and finished == "finished") else "FAIL",
             f"a prompt sent through {used} (one screen drawn: {drawn == 1}) ran on the desktop:"
             f" the chip read {running!r} then {finished!r}, and the fake model's answer reached the"
             f" terminal ({answered}) (shots 12-16)")

    # -- 6. stop -----------------------------------------------------------------------------------------
    async def stop(self) -> None:
        phone = self.phone
        await wait_idle(phone)
        await compose(phone, "SLOWTURN please take your time")
        stop_shown = False
        try:
            await phone.wait_for(f"(() => {{ const b = document.querySelector('.rp-stop');"
                                 f" return b && !b.hidden && b.getClientRects().length > 0; }})()", timeout=30)
            stop_shown = True
        except AssertionError:
            pass
        await asyncio.sleep(4)
        clock_before = await phone.evaluate(CLOCK)
        await browser_shot(phone, "phone", "17-stop-while-the-turn-runs")
        started = time.monotonic()
        await click_first(phone, ".rp-stop")
        ended = False
        try:
            await phone.wait_for(f"!({CLOCK}.trim())", timeout=30)
            ended = True
        except AssertionError:
            pass
        took = time.monotonic() - started
        await asyncio.sleep(2)
        await browser_shot(phone, "phone", "18-after-stop")
        desk("19-the-stopped-turn-on-the-desktop")
        note("PASS" if (stop_shown and ended) else "FAIL",
             f"Stop is in the strip while the turn runs ({stop_shown}, clock {clock_before!r}) and"
             f" one tap ends a turn the model would have kept up for two minutes ({ended}, in"
             f" {took:.1f} s) (shots 17-19)")

    # -- 7. the agent's question ---------------------------------------------------------------------
    async def question(self) -> None:
        phone = self.phone
        await wait_idle(phone)
        await compose(phone, "ASKME which branch?")
        asked = False
        try:
            await phone.wait_for("(() => { const a = document.querySelector('.rp-ask');"
                                 " return a && !a.hidden && a.getClientRects().length > 0; })()", timeout=40)
            asked = True
        except AssertionError:
            pass
        drawn = await phone.evaluate("""
            (() => ({ header: document.querySelector('.rp-ask-header')?.textContent,
                      text: document.querySelector('.rp-ask-text')?.textContent,
                      choices: [...document.querySelectorAll('.rp-ask-choice')].map(e => e.textContent) }))()
        """)
        await browser_shot(phone, "phone", "20-the-question-on-the-phone")
        desk("21-the-question-on-the-desktop")
        await click_first(phone, ".rp-ask-choice")
        gone = False
        try:
            await phone.wait_for("(() => { const a = document.querySelector('.rp-ask');"
                                 " return !a || a.hidden; })()", timeout=30)
            gone = True
        except AssertionError:
            pass
        answered = await terminal_has(phone, "You chose: main", timeout=60)
        await browser_shot(phone, "phone", "22-the-choice-answered-it")
        desk("23-the-answer-reached-the-desktop-ask")
        note("PASS" if (asked and gone and answered) else "FAIL",
             f"the worker's ask_user is drawn on the phone ({asked}: {drawn}), a tap on a choice"
             f" closes it ({gone}) and the model got the answer ({answered}) (shots 20-23)")

    # -- 8. recap ------------------------------------------------------------------------------------------
    async def recap(self) -> None:
        phone = self.phone
        await wait_idle(phone)
        await click_first(phone, ".rp-more")
        await phone.wait_for("!!document.querySelector('.rp-sheet-item[data-action=\"recap\"]')", timeout=10)
        await browser_shot(phone, "phone", "24-the-pane-menu")
        await click_first(phone, ".rp-sheet-item[data-action=\"recap\"]")
        # The pane's own recap header ("Recap · 19:50 → 19:52 · 2m"), whichever model wrote the
        # body: the summaries role resolves through the Flash tier, and which model that is is the
        # profile's business (notes.txt says whether the fake model saw the side call).
        printed = await terminal_has(phone, "Recap ·", timeout=90)
        notes = await phone.evaluate("""
            (() => ({ term: document.getElementById('term-note')?.textContent,
                      thread: document.getElementById('thread-note')?.textContent,
                      toast: document.querySelector('.rp-toast')?.textContent }))()
        """)
        await browser_shot(phone, "phone", "25-the-recap-in-the-pane")
        desk("26-the-recap-on-the-desktop")
        errors = [v for v in notes.values() if v and ("rror" in v or "efus" in v)]
        note("PASS" if (printed and not errors) else "FAIL",
             f"Recap from the pane menu prints a recap in the pane ({printed}) with no error"
             f" ({notes}) (shots 24-26)")
        fake_recaps = sum(1 for line in (OUT / "requests.jsonl").read_text().splitlines()
                          if json.loads(line).get("scene") == "recap")
        note("NOTE", f"the recap's side call reached the fake model {fake_recaps} time(s); 0 means the"
                     " summaries role resolved to another provider (see README, \"what is real\")")

    # -- 9. the offline queue ---------------------------------------------------------------------------
    async def offline_queue(self) -> None:
        phone = self.phone
        await wait_idle(phone)
        await phone.call("Network.enable")
        await phone.call("Network.emulateNetworkConditions",
                         {"offline": True, "latency": 0, "downloadThroughput": -1, "uploadThroughput": -1})
        cut = await phone.evaluate("""
            (() => { const live = window.__relaySockets.filter(s => s.readyState === WebSocket.OPEN);
                     live.forEach(s => s.close(4000, 'tunnel'));
                     return live.length; })()
        """)
        try:
            link = await phone.wait_for(f"['offline', 'dropped'].includes({LINK}) ? {LINK} : ''", timeout=20)
        except AssertionError:
            link = await phone.evaluate(LINK)
        await compose(phone, "OFFLINEQ typed in the tunnel")
        pending = ""
        try:
            pending = await phone.wait_for("document.getElementById('outbox-note')?.textContent || ''", timeout=10)
        except AssertionError:
            pass
        await browser_shot(phone, "phone", "27-typed-while-offline")
        await phone.call("Network.emulateNetworkConditions",
                         {"offline": False, "latency": 0, "downloadThroughput": -1, "uploadThroughput": -1})
        try:
            back = await phone.wait_for(f"{LINK} === 'connected' ? 'connected' : ''", timeout=60)
        except AssertionError:
            back = await phone.evaluate(LINK)
        arrived = await terminal_has(phone, "arrived, once", timeout=60)
        await asyncio.sleep(8)                          # room for a second delivery to show up
        cleared = await phone.evaluate("document.getElementById('outbox-note')?.textContent || ''")
        await browser_shot(phone, "phone", "28-back-online-and-delivered")
        desk("29-the-offline-prompt-on-the-desktop")
        turns = requests_for("OFFLINEQ")
        baseline = requests_for("QUICK")
        note("PASS" if (cut and pending.startswith("Sends when back online") and back == "connected"
                        and arrived and turns == baseline and not cleared) else "FAIL",
             f"{cut} socket cut and the page taken offline (link {link!r}); a prompt typed then reads"
             f" {pending!r}; back online ({back!r}) it arrived ({arrived}) exactly once (the model saw"
             f" {turns} request(s) for it, the same as for step 5's prompt: {baseline}); the note is"
             f" cleared afterwards ({cleared!r}) (shots 27-29)")

    # -- 10. the notification switches ------------------------------------------------------------------
    async def notifications(self) -> None:
        phone = self.phone
        await back_to_inbox(phone)
        origin = await phone.evaluate("location.origin")
        await phone.call("Browser.grantPermissions", {"origin": origin, "permissions": ["notifications"]})
        before = await phone.evaluate("""
            (() => ({ text: document.getElementById('notify').textContent,
                      note: document.getElementById('notify-note').textContent,
                      permission: window.Notification ? Notification.permission : 'none' }))()
        """)
        await browser_shot(phone, "phone", "30-the-notify-row")
        await press(phone, "notify")
        switches = []
        try:
            switches = await phone.wait_for("""
                (() => { const boxes = [...document.querySelectorAll('#notify-kinds input')];
                         return boxes.length ? boxes.map(b => [b.id, b.checked, b.dataset.kinds]) : null; })()
            """, timeout=60)
        except AssertionError:
            pass
        after = await phone.evaluate("document.getElementById('notify-note').textContent")
        await browser_shot(phone, "phone", "31-the-two-switches")
        note("PASS" if len(switches) == 2 else "FAIL",
             f"the Notify row ({before}) subscribes (push service stubbed in the browser) and the"
             f" desktop answers with the two switches: {switches}; the row says {after!r}"
             " (shots 30, 31). Delivery to a lock screen is Phase 3, on the real phone")
        # And one switch off: the desktop keeps the choice (a second push_subscribe, no re-prompt).
        if len(switches) == 2:
            await phone.evaluate("document.getElementById('notify-switch-needs').click()")
            await asyncio.sleep(3)
            state = await phone.evaluate(
                "[...document.querySelectorAll('#notify-kinds input')].map(b => [b.id, b.checked])")
            await browser_shot(phone, "phone", "32-one-switch-off")
            note("NOTE", f"after turning the second switch off: {state} (shot 32)")

    # -- 11. admit a guest from the phone --------------------------------------------------------------
    async def admit_from_phone(self) -> None:
        phone = self.phone
        await open_pane_row(phone, 0)
        ensure_share_window()
        await asyncio.sleep(2)
        share_shot("33-the-share-window-before-the-invite")
        # A row of text in the device list: any pixel of the text colour inside the list's box.
        listed = _list_has_text()
        note("PASS" if listed else "FAIL",
             f"the share window opened later in the run lists the paired phone: {listed} (shot 33;"
             " with remote control on the list was reported at launch, before any window existed"
             " — RemoteShare keeps it now)")
        rows = share_rows()
        if len(rows) < 3:
            raise RuntimeError(f"the share window's rows did not read: {rows}")
        invite_row = rows[1]                            # address, invite role, Revoke selected
        share_click(65, invite_row)                     # the role combo
        await asyncio.sleep(1)
        key("Down", "Return")                           # Viewer → Editor
        await asyncio.sleep(1)
        share_click(441, invite_row)                    # Make a link
        await asyncio.sleep(6)
        share_shot("34-an-editor-invite")
        self.invite = read_url("invite-url.txt")
        if not self.invite:
            raise RuntimeError("no invite url was written")
        close_share_window()
        hosted = self.invite.startswith(f"{HOSTED}/join#")
        note("PASS" if hosted else "FAIL", f"an editor invite was made on the desktop, through the hosted"
                                           f" address: {self.invite.split('#')[0]}#…")

        self.guest = await open_phone(self.invite, size={"width": 430, "height": 900,
                                                         "deviceScaleFactor": 2, "mobile": True})
        await self.guest.wait_for(shown('screen-join'), timeout=90)
        await self.guest.wait_for("document.getElementById('join-fingerprint').textContent !== '…'", timeout=30)
        await self.guest.evaluate("(() => { document.getElementById('join-name').value = 'alice'; return true; })()")
        await press(self.guest, 'join-knock')
        code = await self.guest.wait_for(
            "document.getElementById('knock-code')?.textContent?.match(/^\\d{5}$/)"
            " ? document.getElementById('knock-code').textContent : ''", timeout=120)
        await browser_shot(self.guest, "guest", "35-alice-knocks")
        row = ""
        try:
            row = await phone.wait_for(
                "(() => { const r = document.querySelector('.rp-ask-row[data-kind=\"knock\"]');"
                " return r ? r.textContent : ''; })()", timeout=60)
        except AssertionError:
            pass
        await browser_shot(phone, "phone", "36-the-knock-on-the-phone")
        desk("37-the-knock-on-the-desktop")
        note("PASS" if (row and code in row) else "FAIL",
             f"the knock reaches the owner's phone with the same code the guest sees ({code}):"
             f" {row!r} (shots 35-37)")
        await click_first(phone, '.rp-ask-row[data-kind="knock"] .rp-ask-yes')
        admitted = False
        try:
            await self.guest.wait_for(shown('screen-guest'), timeout=60)
            admitted = True
        except AssertionError:
            pass
        await asyncio.sleep(3)
        await browser_shot(self.guest, "guest", "38-alice-admitted-from-the-phone")
        desk("39-alice-on-the-people-list")
        note("PASS" if admitted else "FAIL", "Admit on the phone lets the guest in (shots 38, 39)")

        # A guest prompt, decided from the phone. The row belongs to the pane the invite was for,
        # so the phone opens each pane until it finds it.
        await compose(self.guest, "GUESTQ what is in this folder?", box="guest-prompt-text",
                      button="guest-prompt-send")
        await self.guest.wait_for(
            "document.querySelector('.guest-prompt .guest-prompt-state')?.textContent.includes('waiting') || false",
            timeout=40)
        await browser_shot(self.guest, "guest", "40-the-guest-prompt-waits")
        found = ""
        for index in range(3):
            await back_to_inbox(phone)
            await open_pane_row(phone, index)
            try:
                found = await phone.wait_for(
                    "(() => { const r = document.querySelector('.rp-ask-row[data-kind=\"prompt\"]');"
                    " return r ? r.textContent : ''; })()", timeout=8)
                break
            except AssertionError:
                continue
        await browser_shot(phone, "phone", "41-the-guest-prompt-on-the-phone")
        await click_first(phone, '.rp-ask-row[data-kind="prompt"] .rp-ask-yes')
        approved = False
        try:
            await self.guest.wait_for("document.querySelector('.guest-prompt.is-approved') !== null", timeout=40)
            approved = True
        except AssertionError:
            pass
        ran = await terminal_has(phone, "guest's prompt ran", timeout=60)
        await browser_shot(self.guest, "guest", "42-the-guest-prompt-approved")
        await browser_shot(phone, "phone", "43-the-guest-prompt-ran")
        desk("44-the-guest-prompt-on-the-desktop")
        note("PASS" if (found and approved and ran) else "FAIL",
             f"a guest prompt waits on the desktop, shows on the phone ({found!r}), Run sends it to"
             f" the agent ({approved}) and the model answered it ({ran}) (shots 40-44)")

    # -- 12. restart -------------------------------------------------------------------------------------
    async def restart(self) -> None:
        phone = self.phone
        ended = stop_relay()
        gone = wait_desktops(self.desktops_before, timeout=60)
        crashes = sum((OUT / "relay-stderr.log").read_text(errors="replace").count("gui_crash")
                      for _ in [0])
        try:
            dropped = await phone.wait_for(f"['offline', 'dropped'].includes({LINK}) ? {LINK} : ''", timeout=30)
        except AssertionError:
            dropped = await phone.evaluate(LINK)
        await browser_shot(phone, "phone", "45-the-desktop-quit")
        clean = ended in ("exit 0", "exited normally")
        note("PASS" if (clean and gone == self.desktops_before and crashes == 0) else "FAIL",
             f"Relay quit cleanly on SIGTERM ({ended}, gui_crash lines: {crashes}); the rendezvous"
             f" shows it gone (desktops {gone}); the phone says {dropped!r} (shot 45)")

        start_relay("-restart")
        came_back = False
        for _ in range(30):
            if sidecar_running():
                came_back = True
                break
            await asyncio.sleep(1)
        registered = wait_desktops(self.desktops_before + 1, timeout=90)
        desk("46-relay-started-again-nothing-pressed")
        # Opened again the way an installed app opens: at the manifest's start_url, `/`. (The tab
        # this run paired in was still at `/pair`; what a refresh of *that* does is step 12a.)
        await phone.navigate(f"{HOSTED}/")
        await asyncio.sleep(2)
        reconnected = False
        try:
            await phone.wait_for(f"{shown('screen-inbox')} && {LINK} === 'connected'", timeout=90)
            reconnected = True
        except AssertionError:
            pass
        rows = await phone.evaluate("document.querySelectorAll('.pane-row').length")
        said = await phone.evaluate("(document.getElementById('welcome-note') || {}).textContent || ''")
        link = await phone.evaluate(LINK)
        screens = await phone.evaluate(
            "[...document.querySelectorAll('.screen')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.id)")
        await browser_shot(phone, "phone", "47-the-phone-reloaded-and-reconnected")
        note("PASS" if (came_back and registered == self.desktops_before + 1 and reconnected and rows >= 1) else "FAIL",
             f"after a restart on the same profile the sidecar comes back by itself ({came_back}),"
             f" the desktop is registered again (desktops {registered}), and the reloaded phone is"
             f" straight in the inbox with {rows} pane(s), no pairing ({screens}; link {link!r};"
             f" welcome note {said!r}) (shots 46, 47)")
        # 12a. The tab a phone paired in sits at `/pair` (the fragment is gone), and Safari reopens
        # a tab where it was. The served app is whatever is deployed; the repo's app.js sends a
        # paired device from there to the inbox.
        await phone.navigate(f"{HOSTED}/pair")
        await asyncio.sleep(2)
        try:
            await phone.wait_for(f"{shown('screen-inbox')} && {LINK} === 'connected'", timeout=60)
            again = "the inbox"
        except AssertionError:
            again = await phone.evaluate("(document.getElementById('welcome-note') || {}).textContent || ''")
        await browser_shot(phone, "phone", "47a-the-pairing-tab-refreshed")
        note("PASS" if again == "the inbox" else "FAIL",
             f"a paired phone refreshing the tab it paired in (/pair, no code) lands in {again!r}"
             " (shot 47a)")
        if again != "the inbox":
            await phone.navigate(f"{HOSTED}/")
            await phone.wait_for(f"{shown('screen-inbox')} && {LINK} === 'connected'", timeout=60)

    # -- 13. switch off ------------------------------------------------------------------------------------
    async def switch_off(self) -> None:
        phone = self.phone
        options_toggle_remote()
        desk("48-options-remote-switched-off")
        gone = wait_desktops(self.desktops_before, timeout=60)
        try:
            link = await phone.wait_for(f"['offline', 'dropped'].includes({LINK}) ? {LINK} : ''", timeout=30)
        except AssertionError:
            link = await phone.evaluate(LINK)
        await asyncio.sleep(2)
        await browser_shot(phone, "phone", "49-the-phone-after-the-switch")
        conf = (JAIL / "config/RelayTerminal/relay.conf").read_text()
        remembered_off = re.search(r"\[remote\][^\[]*alwaysOn=false", conf, re.S) is not None
        note("PASS" if (gone == self.desktops_before and link in ("offline", "dropped") and remembered_off) else "FAIL",
             f"the switch off withdraws the desktop from the rendezvous (desktops {gone}), the phone"
             f" says {link!r}, and alwaysOn=false is remembered ({remembered_off}); the sidecar is"
             f" {'still' if sidecar_running() else 'no longer'} a child of Relay (shots 48, 49)")
        close_options()

    async def go(self) -> None:
        await step("1 off at start", self.starts_off())
        await step("2 the switch", self.switch_on())
        await step("3 pairing", self.pair_phone())
        await step("4 the inbox", self.inbox())
        await step("5 a prompt", self.prompt())
        await step("6 stop", self.stop())
        await step("7 the question", self.question())
        await step("8 recap", self.recap())
        await step("9 the offline queue", self.offline_queue())
        await step("10 notifications", self.notifications())
        await step("11 admit from the phone", self.admit_from_phone())
        await step("12 restart", self.restart())
        await step("13 switch off", self.switch_off())

    async def close(self) -> None:
        for side, browser in (("phone", self.phone), ("guest", self.guest)):
            if browser is not None:
                problems = [line for line in browser.console if "EXCEPTION" in line or "error" in line.lower()]
                if problems:
                    note("NOTE", f"{side} console: {problems[:6]}")
                await browser.stop()
        ended = stop_relay()
        note("NOTE", f"Relay ended: {ended}")
        (OUT / "timings.txt").write_text("".join(f"{took:7.1f} s  {name}\n" for name, took in TIMES))


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
