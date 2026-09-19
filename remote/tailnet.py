# SPDX-License-Identifier: AGPL-3.0-or-later
"""``tailscale serve``: a real certificate for the phone, and no warning to accept.

`remote/devtls.py` makes a certificate this machine signed itself. It needs nothing, and it costs
one warning on the phone every time the address changes — and rather more than a warning: browsers
refuse a service worker on a certificate error, so Web Push can never even be tried there, and some
of them drop the pairing fragment when they reload past the interstitial.

`tailscale serve` is the way out that does not need a hosted service. Tailscale terminates TLS
itself, with a certificate Let's Encrypt issued for ``<machine>.<tailnet>.ts.net``, and proxies to a
**plain http** port on loopback — so what goes behind it is the ordinary local listener, not the
self-signed TLS one, and the CSP and the ``/pair`` and ``/join`` routes are the same code answering
at a different origin. The Noise session is end-to-end above TLS either way (§4), so none of this
changes what the phone verifies; it changes only what the browser demands before it will hand out
WebCrypto, service workers and a camera.

Three things have to be true, and each one has its own sentence when it is not:

* tailscale installed, up and logged in;
* MagicDNS on for the tailnet, so this machine has a name;
* ``sudo tailscale set --operator=$USER`` run once, so Relay may drive ``tailscale serve``
  without asking for a password.

A fourth is only visible when you try: **Serve** has to be enabled for the tailnet in the admin
console. ``tailscale serve --bg`` does not fail when it is not — it prints a link and waits for
somebody to click it — so every call here has a timeout and reports what tailscale printed.

Serve is one configuration per node and ``serve reset`` takes all of it down, so detection refuses
when something that is not Relay's own loopback route is already published: publishing would
replace whatever it is, and stopping the share would delete it.
"""
from __future__ import annotations

import json
import shutil
import subprocess
from dataclasses import dataclass

BINARY = "tailscale"

# Long enough for a daemon round trip, short enough that a wedged CLI cannot hold a dialog open.
TIMEOUT = 15.0
# `serve --bg` waits when Serve is not enabled on the tailnet, so this is a deadline, not a budget.
SERVE_TIMEOUT = 20.0

OPERATOR_HINT = ("Relay may not drive tailscale as this user — run "
                 "`sudo tailscale set --operator=$USER` once, then try again.")


@dataclass(frozen=True)
class Tailnet:
    """What detection found. ``ready`` means publishing is worth trying; ``reason`` says why not."""

    name: str = ""
    ready: bool = False
    reason: str = ""

    @property
    def url(self) -> str:
        return f"https://{self.name}" if self.name else ""


def _text(stream: object) -> str:
    """What a subprocess stream said, whether it came back as bytes or as text, or "" for none."""
    if isinstance(stream, bytes):
        return stream.decode("utf-8", "replace")
    return stream if isinstance(stream, str) else ""


def _run(arguments: list[str], timeout: float) -> tuple[int, str]:
    """Run tailscale and return (returncode, everything it printed). A timeout is a failure with
    whatever it had said by then, because that text is the only explanation there is."""
    try:
        done = subprocess.run([BINARY, *arguments], capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired as expired:
        # `text=True` does not reach TimeoutExpired: its stdout and stderr are bytes even then, so
        # an f-string here put `b'\nServe is not enabled...'` in front of the person. This is the
        # path `serve --bg` takes whenever Serve is not enabled for the tailnet, which is exactly
        # when the text matters most.
        printed = "".join(_text(stream) for stream in (expired.stdout, expired.stderr))
        return 124, printed
    except OSError as error:
        return 127, str(error)
    return done.returncode, f"{done.stdout or ''}{done.stderr or ''}"


def _one_sentence(text: str, fallback: str) -> str:
    """Tailscale's own words, folded into one line so a label can hold them."""
    lines = [line.strip() for line in (text or "").splitlines() if line.strip()]
    return " ".join(lines) if lines else fallback


def probe(*, timeout: float = TIMEOUT) -> Tailnet:
    """Can this machine offer a warning-free tailnet address, and if not, why not."""
    if not shutil.which(BINARY):
        return Tailnet(reason="Tailscale is not installed here, so there is no tailnet name to "
                              "serve the app from.")

    code, printed = _run(["status", "--json"], timeout)
    if code != 0:
        return Tailnet(reason=_one_sentence(
            printed, "Tailscale is installed but its daemon did not answer."))
    try:
        status = json.loads(printed or "{}")
    except ValueError:
        return Tailnet(reason="Tailscale answered something that was not JSON; it may be a version "
                              "this does not understand.")
    if not isinstance(status, dict):
        status = {}

    state = str(status.get("BackendState") or "")
    if state in ("NeedsLogin", "NoState", ""):
        return Tailnet(reason="Tailscale is not logged in on this machine — run `tailscale up` "
                              "once, then try again.")
    if state == "NeedsMachineAuth":
        return Tailnet(reason="This machine is waiting to be approved on the tailnet; approve it in "
                              "the Tailscale admin console, then try again.")
    if state == "Stopped":
        return Tailnet(reason="Tailscale is installed but switched off here — run `tailscale up` "
                              "to bring it back.")
    if state == "Starting":
        return Tailnet(reason="Tailscale is still starting up here; try again in a moment.")

    name = str((status.get("Self") or {}).get("DNSName") or "").rstrip(".")
    if not name:
        return Tailnet(reason="This machine has no MagicDNS name — turn MagicDNS on for the tailnet "
                              "in the Tailscale admin console (DNS → MagicDNS).")

    # The only honest test of the operator setting is asking tailscale to do something it guards.
    # `serve status` is that, and it changes nothing.
    code, printed = _run(["serve", "status"], timeout)
    if code != 0:
        lowered = printed.lower()
        if "operator" in lowered or "access denied" in lowered or "must be run as root" in lowered:
            return Tailnet(name=name, reason=OPERATOR_HINT)
        return Tailnet(name=name, reason=_one_sentence(
            printed, "tailscale could not read its serve configuration."))
    # Serve is one configuration for the whole node, and `serve reset` takes all of it down. If
    # something else is already published here, publishing would replace it and stopping the share
    # would delete it, so this stays out of the way and says so. A route to loopback is taken as
    # one Relay left behind — it is the only thing it ever writes.
    settled = printed.strip().lower()
    if settled and "no serve config" not in settled and "127.0.0.1" not in settled:
        return Tailnet(name=name, reason="tailscale is already serving something else on this "
                                         "machine (`tailscale serve status` shows it), and Relay "
                                         "will not replace it.")
    return Tailnet(name=name, ready=True)


def publish(port: int, *, timeout: float = SERVE_TIMEOUT) -> tuple[str | None, str]:
    """Put the local **plain http** port on the tailnet over https. Returns (base URL, message).

    ``port`` is a loopback http listener: tailscale terminates TLS and proxies to it. Handing it
    the self-signed TLS listener instead would make it a proxy in front of a certificate it has no
    reason to trust.
    """
    found = probe(timeout=timeout)
    if not found.ready:
        return None, found.reason

    code, printed = _run(["serve", "--bg", str(port)], timeout)
    if code != 0:
        hint = _one_sentence(printed, "tailscale serve failed.")
        lowered = hint.lower()
        if "operator" in lowered or "access denied" in lowered:
            hint = OPERATOR_HINT
        elif "not enabled" in lowered or "https" in lowered or "cert" in lowered:
            # tailscale prints the admin link that turns the feature on, for this very tailnet and
            # node. Keep it: it is one click, and it beats directions to a console page.
            link = next((word for word in hint.split()
                         if word.startswith("https://login.tailscale.com")), "")
            hint = (f"Serve is not enabled for this tailnet. Enable it at {link} and try again."
                    if link else
                    hint + " Enable Serve and HTTPS certificates for the tailnet in the Tailscale "
                           "admin console, then try again.")
        return None, hint
    return found.url, f"tailscale serve is publishing port {port} at {found.url}"


def unpublish(*, timeout: float = TIMEOUT) -> bool:
    """Take the serve configuration down again. True if tailscale said it did."""
    if not shutil.which(BINARY):
        return False
    code, _ = _run(["serve", "reset"], timeout)
    return code == 0


def dns_name(*, timeout: float = TIMEOUT) -> str | None:
    """The MagicDNS name alone, for callers that only want to print it."""
    return probe(timeout=timeout).name or None
