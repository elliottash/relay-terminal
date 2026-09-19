# SPDX-License-Identifier: AGPL-3.0-or-later
"""``cloudflared`` quick tunnel: a public link, for someone who is not on your network.

`remote/devtls.py` serves the app with a certificate this machine signed, which costs a warning on
the phone and blocks service workers. `remote/tailnet.py` gets a real certificate from
``tailscale serve``, with no warning — but only for devices signed in to the same tailnet. A
colleague has neither: not on the Wi-Fi, not on the tailnet, and not about to install a VPN to look
at one pane for ten minutes (owner, 2026-09-18: "i want to share with colleagues").

A quick tunnel is the third address. ``cloudflared tunnel --url http://127.0.0.1:<port>`` needs no
account and no DNS record: Cloudflare answers on a ``*.trycloudflare.com`` name with its own
certificate and proxies to a **plain http** port on loopback — the same listener `tailnet.py`
fronts, for the same reason. What the tunnel carries is RRP inside Noise (§4), so Cloudflare sees
ciphertext and a WebSocket, exactly as the rendezvous does in the hosted design.

What this widens, and what it does not:

* it does **not** add a door. Reaching the app is still pairing with the one-time secret from the
  QR fragment, or an invite for a guest, and the hub's capability rules and rate limits are the
  same ones a phone on the LAN meets;
* it does widen the hallway: while a tunnel is up, that http listener is reachable from the
  internet rather than from the house. So it is off unless the owner picks that address, it is
  taken down when the share ends, and the URL — which is the thing worth guessing — stays out of
  the logs.

Two traps this module hit, both found by tests rather than by use, and both easy to re-introduce:

* **reading the child's output.** `readline()` plus `select` looks right and is not: a buffered
  stream pulls several lines into Python on one read, after which `select` truthfully reports the
  pipe empty while the line being waited for is already in hand — the tunnel "never came up"
  although its URL had been printed. It only shows when a second tunnel follows a first in one
  process. So the loop below reads raw bytes and splits lines itself, and the deadline is enforced
  on the wait, never between lines;
* **making the child die with its parent.** `PR_SET_PDEATHSIG` from a `preexec_fn` is the obvious
  way and deadlocks the child before it execs when the process has threads — this one keeps a
  reader thread per tunnel. Teardown is explicit instead: on address switch, on share end, on
  sidecar stop, and `atexit`.

A quick tunnel's name is new every run and its lifetime is the process's. That suits a demo and a
"look at this for ten minutes"; a colleague's link dies when the share does. The stable version is
a **named** tunnel under a domain the owner controls, which needs a DNS record and a credentials
file, and is the same server-side work as the hosted `app.relay-terminal.ai` plan
(`REMOTE-AND-MULTIPLAYER-DESIGN.md` §3.1).
"""
from __future__ import annotations

import atexit
import re
import select
import shutil
import socket
import subprocess
import threading
import time
from dataclasses import dataclass, field

BINARY = "cloudflared"

# A quick tunnel is registered with Cloudflare before it answers; this is how long we wait for the
# line that carries the name. Long enough for a slow link, short enough not to hold a dialog.
READY_TIMEOUT = 40.0
STOP_TIMEOUT = 10.0

# The line cloudflared prints when the tunnel is up. It draws a box around it, so the URL is found
# anywhere in the output — but only this exact shape is taken, never whatever else it printed.
# The negative lookahead is the point: `https://x.trycloudflare.com.attacker.test/` contains a
# perfectly good match otherwise, and taking it would send the owner's link to somebody else's host.
URL = re.compile(r"https://[a-z0-9-]+\.trycloudflare\.com(?![a-z0-9.:-])")

# The name is registered a moment after cloudflared announces it, so a link handed over instantly
# can fail to resolve for a few seconds. Long enough to cover that, short enough to stop waiting.
LIVE_TIMEOUT = 45.0


@dataclass
class Tunnel:
    """One quick tunnel, or the reason there is none."""

    url: str = ""
    reason: str = ""
    port: int = 0
    process: subprocess.Popen | None = field(default=None, repr=False)

    @property
    def ready(self) -> bool:
        return bool(self.url)


_current: Tunnel | None = None
_lock = threading.Lock()


def probe() -> Tunnel:
    """Can this machine offer a public link, and if not, why not. Starts nothing."""
    if not shutil.which(BINARY):
        return Tunnel(reason="cloudflared is not installed here, so there is no public link to "
                             "offer. Install it, or share over your network or tailnet instead.")
    return Tunnel(url="", reason="")


def current() -> Tunnel | None:
    """The tunnel this process started, if it is still running."""
    with _lock:
        if _current is not None and _current.process is not None and _current.process.poll() is None:
            return _current
        return None


def publish(port: int, *, timeout: float = READY_TIMEOUT) -> tuple[str | None, str]:
    """Put the local **plain http** port behind a quick tunnel. Returns (base URL, message).

    One tunnel per process: a second call for the same port returns the one already running, and
    for a different port replaces it, because the link a person was given must keep meaning the
    pane they were given it for.
    """
    global _current
    if not shutil.which(BINARY):
        return None, probe().reason

    running = current()
    if running is not None:
        if running.port == port:
            return running.url, f"a public link is already open at {running.url}"
        unpublish()

    try:
        process = subprocess.Popen(
            [BINARY, "tunnel", "--no-autoupdate", "--url", f"http://127.0.0.1:{port}"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
    except OSError as error:
        return None, f"cloudflared would not start: {error}"

    url, lines, pending = "", [], b""
    deadline = time.monotonic() + timeout
    # Raw bytes, read in chunks, and this function does the line splitting. Two traps are avoided
    # by doing it the long way. `readline()` alone waits for a line that may never come, so a
    # cloudflared that says one thing and goes quiet would hold this call — and the dialog behind
    # it — for ever; the deadline has to be enforced on the *wait*. And a buffered text stream
    # will pull several lines into Python on one read, after which `select` truthfully says the
    # pipe is empty while the line we are waiting for is already in hand: the tunnel then "never
    # came up" although its URL had been printed. That one only appeared when a second tunnel
    # followed a first, which is exactly the case a test caught and a person would not have.
    while process.stdout is not None:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        if not select.select([process.stdout], [], [], min(remaining, 0.5))[0]:
            if process.poll() is not None and not pending:
                break
            continue
        chunk = process.stdout.read(4096)
        if not chunk:
            if process.poll() is not None:
                break
            continue
        pending += chunk
        while b"\n" in pending and not url:
            raw, pending = pending.split(b"\n", 1)
            line = raw.decode("utf-8", "replace")
            lines.append(line.strip())
            found = URL.search(line)
            if found:
                url = found.group(0)
        if url:
            break
    if not url:
        process.terminate()
        # cloudflared's own last words, which name a proxy, a blocked port or a DNS failure far
        # better than a guess would. The URL is never in them at this point.
        tail = " ".join(line for line in lines[-4:] if line) or "it printed nothing"
        return None, f"cloudflared did not open a tunnel within {int(timeout)} s: {tail}"

    # Keep draining, or cloudflared blocks on a full pipe once the link sees traffic. The lines are
    # dropped rather than logged: they carry the URL, and the URL is the secret part of a quick
    # tunnel.
    threading.Thread(target=_drain, args=(process,), daemon=True).start()
    with _lock:
        _current = Tunnel(url=url, port=port, process=process)
    return url, ("a public link is open — anyone with it can reach the pairing page, and it stops "
                 "working when you stop sharing")


# A tunnel nobody knows about is the worst state this module can leave behind: a public address
# into this machine, held open by an orphan. The obvious guard is PR_SET_PDEATHSIG from a
# `preexec_fn`, and it is a trap: `preexec_fn` runs between fork and exec, and in a process that
# has threads — this one keeps a reader thread per tunnel — it can deadlock the child before it
# ever execs. It did, reproducibly, as soon as a second tunnel followed a first in the same
# process. So the child is ordinary, and the teardown is explicit and belt-and-braces:
# `unpublish()` on address switch, on share end and on sidecar stop, plus this.
atexit.register(lambda: unpublish(timeout=2.0))


def live(url: str, *, timeout: float = LIVE_TIMEOUT) -> bool:
    """Wait until the tunnel's name resolves here, so a link is not handed over before it works.

    The name is what Cloudflare registers last. A resolver that has already cached the miss can
    take longer than the tunnel itself, so a False here means "not yet from this machine", not
    "broken": the link may already work for the person it was sent to.
    """
    host = url.split("//", 1)[-1].split("/", 1)[0]
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            socket.getaddrinfo(host, 443, proto=socket.IPPROTO_TCP)
            return True
        except socket.gaierror:
            time.sleep(2)
    return False


def _drain(process: subprocess.Popen) -> None:
    """Keep reading, or cloudflared blocks on a full pipe once the link sees traffic."""
    try:
        while process.stdout is not None and process.stdout.read(4096):
            pass
    except (OSError, ValueError):
        pass


def unpublish(*, timeout: float = STOP_TIMEOUT) -> bool:
    """Close the tunnel this process opened. True if there was one and it is gone.

    Only ever the one this process started: `cloudflared` may be running for something else on this
    machine — another Relay, or the owner's own tunnel — and a public link is not ours to close
    just because we can see it.
    """
    global _current
    with _lock:
        tunnel = _current
        _current = None
    if tunnel is None or tunnel.process is None:
        return False
    process = tunnel.process
    if process.poll() is not None:
        return False
    process.terminate()
    try:
        process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        process.kill()
        try:
            process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            return False
    return True
