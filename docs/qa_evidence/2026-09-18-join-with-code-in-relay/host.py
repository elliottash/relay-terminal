# SPDX-License-Identifier: GPL-3.0-or-later
"""The sharing side of the live /join drive: a real rendezvous and hub on loopback, two demo panes
shared as one tab, and a meeting code for the tab. Knocks are admitted automatically (the owner's
click is not what is under test here). Once somebody is in, a third pane is added to the tab after
a pause, so the joined Relay should open it without being asked.

    host.py <port> <state dir>     writes <state>/code.txt ("CODE PIN") and <state>/events.log
"""
import asyncio
import os
import sys
import tempfile
from pathlib import Path

os.environ["RELAY_KEYRING"] = "off"
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from remote import guests as guests_mod, host as host_mod, identity as identity_mod, wire  # noqa: E402
from rendezvous.server import Store, build  # noqa: E402
from tests.test_remote_control import SharedSource  # noqa: E402

TAB = "tab-live"


async def main(port: int, state: Path) -> None:
    log = (state / "events.log").open("a", buffering=1)
    directory = Path(tempfile.mkdtemp(dir=state))
    store = Store(":memory:")
    server = build(store, static_root=Path(__file__).resolve().parents[3] / "app")
    await server.start("127.0.0.1", port)
    base = f"http://127.0.0.1:{port}"
    identity = identity_mod.Identity.create(directory)
    devices = identity_mod.DeviceStore(directory)
    guests = guests_mod.GuestStore(directory, devices=devices)
    source = SharedSource()

    async def approver(request):
        return False, wire.VIEW

    async def knock_approver(request):
        log.write(f"knock from {request.name!r}, admitted as {request.role}\n")
        return True, request.role

    host = host_mod.Host(identity, devices, source, app_base=base, approver=approver,
                         knock_approver=knock_approver, guests=guests, name="Ana's desktop")
    await host.register(base)
    serving = asyncio.create_task(host.serve())
    for _ in range(100):
        await asyncio.sleep(0.05)
        if host.socket is not None:
            break
    for pane in ("pane-1", "pane-2"):
        host.pane_tab(pane, TAB)
    source._changed()
    record = await host.code_create(["pane-1"], wire.EDITOR, tab=TAB)
    (state / "code.txt").write_text(f"{record.code} {record.pin}\n")
    log.write(f"code ready for {TAB}\n")

    while not guests.live():
        await asyncio.sleep(0.5)
    log.write(f"admitted: {[(p.name, sorted(p.panes)) for p in guests.live()]}\n")
    await asyncio.sleep(12)
    source._panes.append({"id": "pane-3", "window": 1, "tab": "pane-3", "title": "added later",
                          "cwd": "~/later", "program": "", "control": "human", "status": "idle",
                          "unread": 0, "queue": 0, "updated": 0})
    host.pane_tab("pane-3", TAB)
    source._changed()
    log.write(f"pane-3 added to the tab: {[(p.name, sorted(p.panes)) for p in guests.live()]}\n")
    await serving


if __name__ == "__main__":
    asyncio.run(main(int(sys.argv[1]), Path(sys.argv[2])))
