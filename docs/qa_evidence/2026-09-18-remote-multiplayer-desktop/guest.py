#!/usr/bin/env python3
"""The guest, for the live multiplayer run (#W5N2).

Not the web client: ``remote.client.Client`` is the Python client the protocol tests already use,
so this run does not depend on the web-client sibling's work. It follows an invite link, knocks,
and prints the five-digit code the desktop is showing so the driver can check the two agree. Once
admitted it says which role and panes it was given, then — if the hub has grown the lines for them
— asks for the keyboard and sends a prompt, so the Sharing pane has something to answer.

  python3 guest.py <invite url> [--name alice] [--platform Chrome] [--ask-control]
                                [--prompt TEXT] [--hold SECONDS]

Everything it prints starts with "guest:" so the driver can grep for a step.
"""
from __future__ import annotations

import argparse
import asyncio
import ssl
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

# The desktop serves the web app over its own development certificate, so this client trusts it the
# way tests/browser.py does. Authentication does not come from TLS here in any case: it comes from
# the Noise handshake against the desktop key in the link (docs/REMOTE-PROTOCOL.md section 4).
_real_context = ssl.create_default_context


def _development_context(*args, **kwargs):
    context = _real_context(*args, **kwargs)
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    return context


ssl.create_default_context = _development_context

from remote import client as client_mod, pairing, wire  # noqa: E402  (after the ssl shim)


async def drain(guest, rounds: int, timeout: float = 3.0) -> None:
    for _ in range(rounds):
        try:
            message = await asyncio.wait_for(guest.inbox.get(), timeout)
        except asyncio.TimeoutError:
            return
        extra = message.get("code") or message.get("holder") or message.get("id") or ""
        print(f"guest: <- {message['t']} {extra}".rstrip(), flush=True)


async def run(args) -> int:
    origin = args.url.split("/join", 1)[0]
    link = pairing.parse_invite_url(args.url)
    guest = client_mod.Client(origin)
    print(f"guest: knocking at {origin} (room {link['room'][:8]}…)", flush=True)
    knocking = asyncio.create_task(
        guest.knock(args.url, name=args.name, platform=args.platform))
    # The code is derived from the handshake and is on the client the moment the handshake is done,
    # which is before the owner has answered. Printing it here is what lets the driver check that
    # the number on the desktop's knock row is this guest's and not a made-up one.
    for _ in range(100):
        if guest.auth_code:
            print(f"guest: my code is {guest.auth_code}", flush=True)
            break
        await asyncio.sleep(0.1)
    try:
        joined = await knocking
    except asyncio.TimeoutError:
        print("guest: nobody answered the knock", flush=True)
        return 2
    except wire.WireError as error:
        print(f"guest: refused ({error})", flush=True)
        return 3
    print(f"guest: admitted as {joined.role} on {list(joined.panes)}", flush=True)
    await drain(guest, 4)

    pane = joined.panes[0] if joined.panes else ""
    # A pause between each step, so the driver can screenshot one waiting row at a time and answer
    # it before the next arrives.
    await asyncio.sleep(args.pause)
    if args.ask_control and pane:
        await guest.send({"t": "control_request", "pane": pane})
        print("guest: asked for the keyboard", flush=True)
        await drain(guest, 3, timeout=args.pause or 20)

    await asyncio.sleep(args.pause)
    if args.prompt and pane:
        await guest.send({"t": "compose", "pane": pane, "text": args.prompt, "agent": True})
        print(f"guest: sent a prompt ({args.prompt!r})", flush=True)
        await drain(guest, 4, timeout=args.pause or 20)

    # Stay connected while the desktop is being screenshotted: a guest who hangs up drops off the
    # participants list, and the point of the shots is to show them on it.
    await asyncio.sleep(args.hold)
    await guest.close()
    print("guest: done", flush=True)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("url")
    parser.add_argument("--name", default="alice")
    parser.add_argument("--platform", default="Chrome")
    parser.add_argument("--ask-control", action="store_true")
    parser.add_argument("--prompt", default="")
    parser.add_argument("--hold", type=float, default=60.0)
    parser.add_argument("--pause", type=float, default=0.0)
    return asyncio.run(run(parser.parse_args()))


if __name__ == "__main__":
    raise SystemExit(main())
