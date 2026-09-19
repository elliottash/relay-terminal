#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Drive the Claude IDE bridge end to end on a live Relay pane and photograph it (GT7X, 26.5).

This harness is claude's side of the IDE integration. Relay is started under Xvfb with the bridge
turned on (`guests/claude_bridge`) and a stand-in `claude` in the pane's foreground (the classifier
reads argv[0], exactly as with a real one); the pane's startTerminal then starts the sidecar and
registers the pane. The harness finds the bridge the way a real claude does — the lock file the
sidecar writes in `~/.claude/ide/<port>.lock` — and speaks the WebSocket variant of MCP from there:
`initialize`, `tools/list`, and `tools/call openDiff`, the blocking one.

The whole round trip is what the pictures are evidence of: openDiff arrives as a `bridge` event
through the guest channel (26.3), the pane opens Relay's diff view beside itself, and **Accept or
Reject in that view's own header** is the decision — only that click returns the tool call:
FILE_SAVED after the *sidecar* has written the file, or DIFF_REJECTED with the file untouched. The
pane's banner beside it is a pointer at the diff pane and nothing more (26.5): it has no action of
its own, because a pane has one banner and any other notice may replace it.

Each picture is checked, not just taken: the harness OCRs the screenshot and logs what the window
actually says, so the run's output is the evidence's own verification. The hard assertions are the
ones a guest depends on — what the tool call returned, and what is on disk afterwards.

    xvfb-run -a -s "-screen 0 1600x1000x24" python3 claude-bridge-drive.py <output-dir>

Nothing of the user's real session is read: the whole run happens under a fresh temp root.
"""
import base64
import json
import os
import re
import secrets
import select
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
RELAY = REPO / "build" / "relay"


def log(message: str) -> None:
    print(f"[drive] {message}", flush=True)


# ----- the isolated session ------------------------------------------------------------------


def make_isolation() -> dict:
    root = Path(tempfile.mkdtemp(prefix="relay-qa-bridge-"))
    home = root / "home"
    for name in ("home", "config", "data", "run", "tmp", "workspace"):
        (root / name).mkdir(parents=True, exist_ok=True)
    (home / ".claude").mkdir(parents=True, exist_ok=True)
    os.chmod(root / "run", 0o700)
    # First-launch dialogs stay out of the pictures; the bridge is the one thing under test that is
    # off by default (26.5), so the run turns exactly that on; hints are muted so their toasts do
    # not crowd out the ones being read.
    (root / "config" / "RelayTerminal").mkdir(parents=True, exist_ok=True)
    (root / "config" / "RelayTerminal" / "relay.conf").write_text(
        "[instructions]\nonboarded=true\n\n[hints]\nenabled=false\n\n"
        "[guests]\nclaude_bridge=true\n", encoding="utf-8")
    environment = dict(os.environ)
    environment.update({
        "HOME": str(home),
        "XDG_CONFIG_HOME": str(root / "config"),
        "XDG_DATA_HOME": str(root / "data"),
        "XDG_RUNTIME_DIR": str(root / "run"),
        "TMPDIR": str(root / "tmp"),
        "RELAY_KEYRING": "off",
        "RELAY_LOCAL_MODELS": str(root / "data" / "local-models.json"),
    })
    for name in ("RELAY_DATA_DIR", "RELAY_ENGINE_CORE", "PYTHONPATH"):
        environment.pop(name, None)
    # The sidecar, the helper and the backend under test must be this worktree's.
    environment["RELAY_DATA_DIR"] = str(REPO)
    return {"root": root, "env": environment, "home": home, "workspace": root / "workspace"}


# ----- the pane, and the claude stand-in in it -------------------------------------------------


def pane_runtime_dirs(environment: dict) -> list[Path]:
    tmp = Path(environment["TMPDIR"])
    return [entry for entry in sorted(tmp.glob("relay-??????")) if (entry / "owner").exists()]


def reporting_pane(environment: dict, timeout: float = 90.0) -> tuple[Path, str]:
    """The pane whose shell reported — the terminal pane, in other words: only a shell writes
    state.json, and Relay makes a runtime dir for the file pane too, whose sorted-first name is
    sometimes the one without. A busy machine can also start the shell's bridge slowly."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for candidate in pane_runtime_dirs(environment):
            state = candidate / "state.json"
            if not state.exists():
                continue
            try:
                return candidate, json.loads(state.read_text(encoding="utf-8"))["token"]
            except (ValueError, KeyError, OSError):
                continue
        time.sleep(0.2)
    raise RuntimeError("no state.json with a token: no pane's shell ever reported")


def claude_under(relay_pid: int) -> int:
    """The pid of a process called `claude` in this Relay's process tree, or 0."""
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            argv = (entry / "cmdline").read_bytes().split(b"\0")
            if argv and argv[0].endswith(b"claude") and not argv[0].endswith(b"relay_core"):
                pid = int(entry.name)
                if relay_pid in ancestors(pid):
                    return pid
        except (OSError, ValueError):
            continue
    return 0


def ancestors(pid: int) -> set[int]:
    seen = set()
    while pid > 1 and pid not in seen:
        seen.add(pid)
        try:
            stat = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8")
        except OSError:
            break
        pid = int(stat[stat.rindex(")") + 2:].split()[1])
    return seen


# ----- the bridge, as claude finds it -----------------------------------------------------------


def find_lock(home: Path, timeout: float = 40.0) -> tuple[int, str]:
    """The sidecar's `<port>.lock` under the isolated `~/.claude/ide`: upstream's own discovery."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for lock in sorted((home / ".claude" / "ide").glob("*.lock")):
            try:
                payload = json.loads(lock.read_text(encoding="utf-8"))
            except (ValueError, OSError):
                continue
            if payload.get("ideName") == "relay" and payload.get("transport") == "ws":
                log(f"bridge lock {lock.name}: pid {payload.get('pid')}, "
                    f"folders {payload.get('workspaceFolders')}")
                return int(lock.stem), payload["authToken"]
        time.sleep(0.3)
    raise RuntimeError("no relay lock appeared in ~/.claude/ide: the sidecar never started")


class ClaudeClient:
    """The WebSocket MCP client claude is: an upgrade with the auth header, then JSON-RPC frames."""

    def __init__(self, port: int, token: str):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=10)
        self.socket.setblocking(False)
        self.buffer = b""
        key = base64.b64encode(secrets.token_bytes(16)).decode()
        self.socket.sendall((
            f"GET / HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nUpgrade: websocket\r\n"
            f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n"
            f"x-claude-code-ide-authorization: {token}\r\n\r\n").encode())
        head = self._read_until(b"\r\n\r\n", timeout=10).decode("latin-1")
        if "101" not in head.splitlines()[0]:
            raise RuntimeError(f"the bridge refused the upgrade: {head.splitlines()[0]}")

    def _read_until(self, marker: bytes, timeout: float) -> bytes:
        deadline = time.monotonic() + timeout
        while marker not in self.buffer:
            ready, _, _ = select.select([self.socket], [], [], max(0.0, deadline - time.monotonic()))
            if not ready:
                raise TimeoutError("the bridge went quiet mid-handshake")
            chunk = self.socket.recv(4096)
            if not chunk:
                raise RuntimeError("the bridge closed the connection")
            self.buffer += chunk
        head, self.buffer = self.buffer.split(marker, 1)
        return head + marker

    def send(self, message: dict) -> None:
        """One masked text frame (RFC 6455: client-to-server frames are masked)."""
        payload = json.dumps(message).encode()
        mask = secrets.token_bytes(4)
        header = bytes([0x81])
        length = len(payload)
        if length < 126:
            header += bytes([0x80 | length])
        elif length < 65536:
            header += bytes([0x80 | 126]) + struct.pack(">H", length)
        else:
            header += bytes([0x80 | 127]) + struct.pack(">Q", length)
        self.socket.sendall(header + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload)))

    def call(self, method: str, params: dict, request_id) -> dict:
        """Send a request and block for its own reply, skipping the keepalive pings."""
        self.send({"jsonrpc": "2.0", "id": request_id, "method": method, "params": params})
        return self.wait_reply(request_id, timeout=30)

    def wait_reply(self, request_id, actions=(), timeout: float = 60.0) -> dict:
        """Poll for one request's reply while `actions` — (when, what) pairs, seconds from now —
        run on the GUI in the meantime: openDiff blocks until the user decides, and this harness
        is the user."""
        pending = sorted(actions, key=lambda action: action[0])
        start = time.monotonic()
        deadline = start + timeout
        while True:
            ready, _, _ = select.select([self.socket], [], [], 0.25)
            if ready:
                self.socket.setblocking(True)
                self.socket.settimeout(5)
                opcode, payload = self._read_frame()
                self.socket.setblocking(False)
                if opcode == 0x1:
                    message = json.loads(payload.decode())
                    if message.get("id") == request_id:
                        return message
                continue   # a ping, a pong, or a reply to something else
            now = time.monotonic() - start
            while pending and pending[0][0] <= now:
                pending.pop(0)[1]()
            if time.monotonic() > deadline:
                raise TimeoutError(f"no reply to request {request_id}")

    def _read_frame(self) -> tuple[int, bytes]:
        def exact(count: int) -> bytes:
            data = b""
            while len(data) < count:
                chunk = self.socket.recv(count - len(data))
                if not chunk:
                    raise RuntimeError("the bridge closed the connection")
                data += chunk
            return data

        head = exact(2)
        fin, opcode = head[0] & 0x80, head[0] & 0x0F
        length = head[1] & 0x7F
        if length == 126:
            length = struct.unpack(">H", exact(2))[0]
        elif length == 127:
            length = struct.unpack(">Q", exact(8))[0]
        return opcode, exact(length)


# ----- driving the window -------------------------------------------------------------------


def xdo(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["xdotool", *args], text=True, capture_output=True, check=False)


def window_id() -> str:
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        ids = [line for line in xdo("search", "--name", "Relay").stdout.split() if line.strip()]
        if ids:
            return ids[-1]
        time.sleep(0.5)
    raise RuntimeError("no Relay window appeared")


def type_text(window: str, text: str) -> None:
    xdo("windowactivate", "--sync", window)
    xdo("type", "--window", window, "--delay", "40", text)


def press(window: str, *keys: str) -> None:
    xdo("windowactivate", "--sync", window)
    for name in keys:
        xdo("key", "--window", window, name)
        time.sleep(0.3)


def screenshot(path: Path) -> str:
    path.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(["import", "-window", "root", str(path)], text=True, capture_output=True)
    if result.returncode != 0:
        raise RuntimeError(f"screenshot failed: {result.stderr}")
    log(f"wrote {path.name}")
    return ocr(path)


def ocr(path: Path) -> str:
    return subprocess.run(["tesseract", str(path), "-"], text=True, capture_output=True).stdout


def report(name: str, text: str, expect: list[str], forbid: list[str] = ()) -> None:
    for needle in expect:
        log(f"{name}: {'found' if needle in text else 'MISSING'} {needle!r}")
    for needle in forbid:
        if needle in text:
            log(f"{name}: UNEXPECTED {needle!r} is still on screen")


def words_of(path: Path) -> list[tuple[int, int, int, int, str]]:
    """Every box OCR'd, with its place in screen pixels — text or not, for the pane's edge OCRs
    as a tall column with no text at all — as (left, top, width, height, text)."""
    result = subprocess.run(["tesseract", str(path), "-", "tsv"], text=True, capture_output=True)
    words = []
    for line in result.stdout.splitlines()[1:]:
        fields = line.split("\t")
        if len(fields) < 12:
            continue
        try:
            left, top, width, height = int(fields[6]), int(fields[7]), int(fields[8]), int(fields[9])
        except ValueError:
            continue
        if width > 0 and height > 0:
            words.append((left, top, width, height, fields[11].strip()))
    return words


def shoot_words(window: str, picture: Path, ready, attempts: int = 5) -> list[tuple[int, int, int, int, str]]:
    """Screenshot into `picture` until `ready(words)` holds. Tesseract misreads a word now and
    then (Save became See one run), and openDiff blocks — the GUI stays put while we re-shoot."""
    for attempt in range(attempts):
        screenshot(picture)
        words = words_of(picture)
        if ready(words):
            return words
        log(f"{picture.name}: OCR did not see what it needs ({attempt + 1}/{attempts}), re-shooting")
        time.sleep(1.0)
    raise RuntimeError(f"{picture.name}: the window never OCR'd into something clickable")


def click(x: int, y: int) -> None:
    xdo("mousemove", str(x), str(y))
    time.sleep(0.1)
    xdo("click", "1")


def decision_geometry(words, label: str):
    """Where the diff pane's Accept or Reject button is.

    This got *simpler* when the decision moved out of the banner (26.5). The old code could not
    trust what the words said — the banner's one button carried its shortcut in the same label and
    the same pixels OCR'd as `Save` one run and `See` the next — so it found the button by geometry:
    the rightmost full-sized word of the row holding `proposes changes`, left of the diff pane's
    edge. The diff pane's header has two buttons whose whole label is one unambiguous word each,
    and nothing else on the screen says either, so the word *is* the button. The rightmost match
    wins, because the header reads `<file>  +n −m    Reject  Accept`."""
    hits = [w for w in words if w[4].strip().strip(".,:;|") == label]
    if not hits:
        return None
    box = max(hits, key=lambda w: w[0] + w[2])
    return (box[0] + box[2] // 2, box[1] + box[3] // 2)


def click_decision(window: str, picture: Path, label: str) -> None:
    def at(words):
        return decision_geometry(words, label)

    point = at(shoot_words(window, picture, at))
    xdo("windowactivate", "--sync", window)
    click(*point)
    log(f"clicked the diff pane's {label} button at {point}")


# ----- the run ------------------------------------------------------------------------------


def main() -> int:
    output = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
    output.mkdir(parents=True, exist_ok=True)
    if not RELAY.exists():
        log(f"missing {RELAY}: build the worktree first")
        return 1
    isolation = make_isolation()
    environment = isolation["env"]
    workspace = isolation["workspace"]
    log(f"isolated session at {isolation['root']}")

    # The file both diffs are about: one proposal saved, one refused.
    target = workspace / "haiku.txt"
    target.write_text("the ink dries slowly\n", encoding="utf-8")

    process = subprocess.Popen([str(RELAY), "--fresh", "-w", str(workspace)],
                               env=environment, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                               text=True)
    log(f"relay pid {process.pid}")
    try:
        window = window_id()
        log(f"window {window}")
        time.sleep(6)   # the Bash bridge's first prompt

        try:
            pane, token = reporting_pane(environment)
        except RuntimeError as error:
            log(str(error))
            return 1
        log(f"pane runtime {pane}, token {token[:8]}…")

        # A `claude` in the foreground, as the classifier reads it: `exec -a` sets argv[0] and the
        # `!` prefix runs the line in the terminal, leaving the shell and its bridge alive.
        type_text(window, "!bash -c 'exec -a claude sleep 900'")
        press(window, "Return")
        guest_pid = 0
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline and not guest_pid:
            guest_pid = claude_under(process.pid)
            time.sleep(0.5)
        if not guest_pid:
            log("the stand-in claude never reached the foreground")
            return 1
        time.sleep(3)   # the program poll that classifies it
        log(f"a process called `claude` is in the foreground (pid {guest_pid})")

        # Discovery, upstream's own way: the lock the sidecar wrote.
        port, auth = find_lock(isolation["home"])
        client = ClaudeClient(port, auth)
        log(f"connected to the bridge on 127.0.0.1:{port}")

        reply = client.call("initialize", {"protocolVersion": "2025-03-26"}, 1)
        log(f"initialize: {reply['result']['serverInfo']}")
        reply = client.call("tools/list", {}, 2)
        names = [tool["name"] for tool in reply["result"]["tools"]]
        log(f"tools/list: {len(names)} tools")
        if "openDiff" not in names:
            log("openDiff is not among the published tools")
            return 1

        # 1. openDiff, accepted: the call blocks, the pane asks, the click is the answer.
        saved = "the ink dries fast\n"
        client.send({"jsonrpc": "2.0", "id": 10, "method": "tools/call",
                     "params": {"name": "openDiff", "arguments": {
                         "old_file_path": str(target), "new_file_path": str(target),
                         "new_file_contents": saved, "tab_name": "claude's haiku"}}})

        def shoot_first() -> None:
            time.sleep(1.0)
            picture = output / "claude-bridge-01-opendiff-banner.png"
            report("01-opendiff-banner", screenshot(picture),
                   ["claude proposes changes", "haiku.txt", "Accept", "Reject"], forbid=[])
            click_decision(window, picture, "Accept")

        reply = client.wait_reply(10, actions=[(3.5, shoot_first)], timeout=60)
        outcome = reply["result"]["content"][0]["text"]
        log(f"openDiff #1 returned {outcome!r}")
        if outcome != "FILE_SAVED":
            log("the accepted diff did not answer FILE_SAVED")
            return 1
        time.sleep(1.2)
        after = target.read_text(encoding="utf-8")
        log(f"the file on disk reads {after!r}")
        if after != saved:
            log("the sidecar did not write the saved contents")
            return 1
        report("02-saved", screenshot(output / "claude-bridge-02-saved.png"),
               ["Saved claude"], forbid=["proposes changes"])

        # 2. openDiff, rejected: the same flow, the diff pane's Reject instead of Accept.
        refused = "the page stays blank\n"
        client.send({"jsonrpc": "2.0", "id": 20, "method": "tools/call",
                     "params": {"name": "openDiff", "arguments": {
                         "old_file_path": str(target), "new_file_path": str(target),
                         "new_file_contents": refused, "tab_name": "claude's haiku"}}})

        def shoot_second() -> None:
            time.sleep(1.0)
            picture = output / "claude-bridge-03-second-diff.png"
            report("03-second-diff", screenshot(picture),
                   ["claude proposes changes", "Accept", "Reject"])
            click_decision(window, picture, "Reject")

        reply = client.wait_reply(20, actions=[(3.5, shoot_second)], timeout=60)
        outcome = reply["result"]["content"][0]["text"]
        log(f"openDiff #2 returned {outcome!r}")
        if outcome != "DIFF_REJECTED":
            log("the dismissed diff did not answer DIFF_REJECTED")
            return 1
        time.sleep(1.2)
        after = target.read_text(encoding="utf-8")
        log(f"the file on disk still reads {after!r}")
        if after != saved:
            log("a rejected diff changed the file anyway")
            return 1
        report("04-rejected", screenshot(output / "claude-bridge-04-rejected.png"),
               ["Kept the file"], forbid=["proposes changes"])

        log("PASS: openDiff accepted and rejected, end to end")
    finally:
        process.terminate()
        try:
            process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            process.kill()
        log("relay stopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
