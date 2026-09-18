# SPDX-License-Identifier: GPL-3.0-or-later
"""``python3 -m remote.cli`` — run remote access from the terminal, before the GUI has a button.

This is the harness the phone actually talks to today: it starts a rendezvous, serves the web app,
registers the desktop, opens a pairing room and prints the QR code. When the GUI grows Settings →
Remote it will call the same ``remote.host.Host`` with a real pane source instead of the demo one.

    python3 -m remote.cli dev --tailscale     # HTTPS on your tailnet, so a phone can open it
    python3 -m remote.cli dev                 # http://127.0.0.1:8787, desktop browser only
    python3 -m remote.cli devices             # list paired devices
    python3 -m remote.cli revoke <device-id>

A phone needs a **secure context** for WebCrypto, so plain http over a LAN address will not work:
use ``--tailscale``, or put it behind any https front end and pass ``--public``.
"""
from __future__ import annotations

import argparse
import asyncio
import contextlib
import json
import logging
import shutil
import subprocess
import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from remote import devtls
from remote import host as host_mod
from remote import identity as identity_mod
from remote import panes as panes_mod
from remote import wire
from rendezvous.server import Store, build

APP_DIR = Path(__file__).resolve().parent.parent / "app"
log = logging.getLogger("relay.remote")


# ---- the QR code ------------------------------------------------------------------------------

def print_qr(url: str) -> bool:
    """Draw the pairing URL as a QR code in the terminal. False if we could not."""
    try:
        import qrcode
    except ImportError:
        return False
    code = qrcode.QRCode(border=2, error_correction=qrcode.constants.ERROR_CORRECT_M)
    code.add_data(url)
    code.make(fit=True)
    matrix = code.get_matrix()
    # Two rows per line of half blocks: the result is square-ish and scans from a normal terminal.
    for top in range(0, len(matrix), 2):
        line = []
        for column in range(len(matrix[top])):
            upper = matrix[top][column]
            lower = matrix[top + 1][column] if top + 1 < len(matrix) else False
            line.append("█" if upper and lower else "▀" if upper else "▄" if lower else " ")
        print("".join(line))
    return True


# ---- tailscale --------------------------------------------------------------------------------

def tailscale_dns_name() -> str | None:
    if not shutil.which("tailscale"):
        return None
    try:
        status = json.loads(subprocess.run(["tailscale", "status", "--json"], capture_output=True,
                                           timeout=15, check=True).stdout)
    except (OSError, subprocess.SubprocessError, ValueError):
        return None
    name = (status.get("Self") or {}).get("DNSName") or ""
    return name.rstrip(".") or None


def tailscale_serve(port: int) -> tuple[str | None, str]:
    """Publish the local port on the tailnet over HTTPS. Returns (base URL, message)."""
    name = tailscale_dns_name()
    if not name:
        return None, "tailscale is not running here."
    done = subprocess.run(["tailscale", "serve", "--bg", str(port)], capture_output=True, text=True)
    if done.returncode != 0:
        detail = (done.stderr or done.stdout).strip().splitlines()
        hint = detail[0] if detail else "tailscale serve failed."
        if "HTTPS" in hint or "cert" in hint.lower():
            hint += ("\n  Enable HTTPS certificates for the tailnet in the Tailscale admin console "
                     "(DNS → HTTPS Certificates), then run this again.")
        return None, hint
    return f"https://{name}", f"tailscale serve is publishing port {port} at https://{name}"


def tailscale_reset() -> None:
    with contextlib.suppress(OSError, subprocess.SubprocessError):
        subprocess.run(["tailscale", "serve", "reset"], capture_output=True, timeout=15)


# ---- approving a device -----------------------------------------------------------------------

def make_approver(mode: str, capability: str):
    async def approver(request: host_mod.PairRequest) -> tuple[bool, str]:
        print("\n" + "─" * 60)
        print(f"  {request.name} ({request.platform}) from {request.peer} wants access")
        print(f"  Device key   {request.fingerprint}")
        print(f"  Code on the phone must read   {request.code}")
        print(f"  Capability   {capability}")
        print("─" * 60)
        if mode == "auto":
            print("  --approve auto: allowing.\n")
            return True, capability
        answer = await asyncio.to_thread(
            input, "  Does the code match, and do you want to allow it? [y/N] ")
        allowed = answer.strip().lower() in ("y", "yes")
        print("  Allowed.\n" if allowed else "  Refused.\n")
        return allowed, capability
    return approver


# ---- commands ---------------------------------------------------------------------------------

async def dev(args) -> int:
    directory = Path(args.state).expanduser() if args.state else None
    identity = identity_mod.Identity.load_or_create(directory)
    devices = identity_mod.DeviceStore(directory)

    store = Store(args.db)
    server = build(store, static_root=APP_DIR)
    # The loopback listener is what this process registers over; a phone never uses it.
    await server.start("127.0.0.1", args.port)
    port = server.port
    local = f"http://127.0.0.1:{port}"

    public = args.public
    served_by_tailscale = False
    tls_note = ""
    if args.tailscale and not public:
        public, message = tailscale_serve(port)
        print(message)
        if public is None:
            await server.close()
            return 2
        served_by_tailscale = True
    elif args.tls and not public:
        directory_for_cert = identity_mod.state_dir() if directory is None else directory
        address = args.address or devtls.preferred_address()
        listener = await server.start("0.0.0.0", args.tls_port,
                                      ssl_context=devtls.context(directory_for_cert))
        public = f"https://{address}:{server.port_of(listener)}"
        tls_note = ("  The certificate is self-signed, so the phone will warn once. Its "
                    f"SHA-256 begins {devtls.fingerprint(directory_for_cert)}.")
    base = public or local

    source = panes_mod.DemoPaneSource()
    host = host_mod.Host(identity, devices, source, app_base=base,
                         approver=make_approver(args.approve, args.capability),
                         name=args.name)
    try:
        await host.register(local)
    except wire.WireError as error:
        print(f"could not register with the rendezvous: {error}")
        await server.close()
        return 2

    serving = asyncio.create_task(host.serve())
    for _ in range(100):
        await asyncio.sleep(0.02)
        if host.socket is not None:
            break

    url, room = await host.open_pairing()
    print()
    print(f"  Relay remote — {identity.fingerprint}   (desktop {identity.desktop_id[:8]})")
    print(f"  App          {base}")
    print(f"  Rendezvous   {local}   ·   paired devices: {len(devices.live())}")
    print()
    if not print_qr(url):
        print("  (install the python 'qrcode' package to see a scannable code here)")
    print()
    print(f"  Scan with your phone's camera, or open:\n  {url}")
    print(f"  The code expires in {room.seconds_left()}s; press Ctrl+C to stop.")
    if not public:
        print("\n  Note: this is a plain http address, so only a browser on this machine can use\n"
              "  it — WebCrypto needs a secure context. Use --tls (self-signed, one warning on\n"
              "  the phone) or --tailscale (a real certificate; needs\n"
              "  'sudo tailscale set --operator=$USER' once).")
    elif tls_note:
        print(tls_note)
    print()

    try:
        await serving
    except asyncio.CancelledError:
        pass
    finally:
        await host.stop()
        await server.close()
        store.close()
        if served_by_tailscale:
            tailscale_reset()
    return 0


def devices_command(args) -> int:
    directory = Path(args.state).expanduser() if args.state else None
    store = identity_mod.DeviceStore(directory)
    live = store.live()
    if not live:
        print("no paired devices.")
        return 0
    for device in live:
        print(f"{device.device_id}  {device.capability:5}  {device.name} ({device.platform})"
              f"  key {device.fingerprint}")
    return 0


def revoke_command(args) -> int:
    directory = Path(args.state).expanduser() if args.state else None
    store = identity_mod.DeviceStore(directory)
    if not store.revoke(args.device_id):
        print(f"no device {args.device_id}.")
        return 1
    print(f"revoked {args.device_id}. It cannot connect again, even if the rendezvous is down.")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="remote.cli", description=__doc__.splitlines()[0])
    parser.add_argument("--state", help="directory for the identity and device list")
    sub = parser.add_subparsers(dest="command", required=True)

    run = sub.add_parser("dev", help="run the rendezvous, the app and a demo desktop")
    run.add_argument("--host", default="127.0.0.1")
    run.add_argument("--port", type=int, default=8787)
    run.add_argument("--db", default=":memory:")
    run.add_argument("--public", help="the https base a phone can reach (skips --tls/--tailscale)")
    run.add_argument("--tailscale", action="store_true", help="publish over your tailnet on https")
    run.add_argument("--tls", action="store_true",
                     help="serve https with a self-signed certificate, for a phone on this network")
    run.add_argument("--tls-port", type=int, default=8443)
    run.add_argument("--address", help="the address to put in the pairing link (default: tailnet)")
    run.add_argument("--approve", choices=("ask", "auto"), default="ask")
    run.add_argument("--capability", choices=wire.CAPABILITIES, default=wire.AGENT)
    run.add_argument("--name", default="this desktop")
    run.add_argument("--verbose", action="store_true")

    sub.add_parser("devices", help="list paired devices")
    revoke = sub.add_parser("revoke", help="revoke a device")
    revoke.add_argument("device_id")

    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.DEBUG if getattr(args, "verbose", False) else logging.INFO,
                        format="%(asctime)s %(levelname)s %(name)s %(message)s")
    if args.command == "devices":
        return devices_command(args)
    if args.command == "revoke":
        return revoke_command(args)
    try:
        return asyncio.run(dev(args))
    except KeyboardInterrupt:
        print("\nstopped.")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
