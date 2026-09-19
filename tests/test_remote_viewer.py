# SPDX-License-Identifier: GPL-3.0-or-later
"""Relay-to-Relay: the laptop's viewer process (remote/viewer.py) against a real desktop hub.

A real rendezvous, a real hub over the GUI's pane source and a real Noise session, as in
test_remote_host.py and test_remote_gui_host.py. The viewer is driven through ``handle`` with its
stdout replaced by a list, which is exactly the JSON the Qt side reads; one test runs it as a
process to pin the stdio framing itself.
"""
import asyncio
import contextlib
import json
import logging
import os
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

from remote import gui_host, host as host_mod, identity as identity_mod, viewer, wire
from rendezvous.server import Store, build

ROOT = Path(__file__).resolve().parent.parent


def run(coroutine, timeout=60):
    return asyncio.run(asyncio.wait_for(coroutine, timeout))


class Capture(logging.Handler):
    """Every log record from every logger, at every level, formatted as it would be written."""

    def __init__(self):
        super().__init__(level=logging.DEBUG)
        self.lines: list[str] = []

    def emit(self, record):
        text = record.getMessage()
        if record.exc_info:
            text += logging.Formatter().formatException(record.exc_info)
        self.lines.append(text)


class Harness:
    """A rendezvous, a desktop hub with one GUI pane, and a viewer whose stdout is a list."""

    def __init__(self, capability=wire.FULL):
        self.capability = capability
        self.requests = []
        self.out: list[dict] = []
        self.received: list[tuple[str, dict]] = []      # what the hub was sent, in order

    async def __aenter__(self):
        self.temporary = tempfile.TemporaryDirectory()
        directory = Path(self.temporary.name)
        self.store = Store(":memory:")
        self.server = build(self.store)
        await self.server.start("127.0.0.1", 0)
        self.base = f"http://127.0.0.1:{self.server.port}"

        # Never the real keyring: Identity.create would otherwise store this test's desktop key
        # over the owner's own remote identity, and every phone he paired would stop connecting.
        self.keyring = mock.patch.dict(os.environ, {"RELAY_KEYRING": "off"})
        self.keyring.start()
        desktop = directory / "desktop"
        desktop.mkdir(mode=0o700)
        self.identity = identity_mod.Identity.create(desktop)
        self.devices = identity_mod.DeviceStore(desktop)
        self.to_gui: list[dict] = []
        self.source = gui_host.GuiPaneSource(self.to_gui.append)
        self.source.set_pane({"id": "p1", "title": "relay-terminal", "cwd": "/home/elliott",
                              "rows": 4, "cols": 20, "status": "idle"})
        self.source.set_frame({"pane": "p1", "full": True, "rows": 4, "cols": 20, "alt": False,
                               "cursor": {"row": 0, "col": 2, "visible": True, "shape": 0},
                               "lines": [{"row": 0, "segs": [["$ ", 0, 0, 0]]}]})

        async def approver(request):
            self.requests.append(request)
            return True, self.capability

        self.host = host_mod.Host(self.identity, self.devices, self.source, app_base=self.base,
                                  approver=approver, name="test desktop")
        original = self.host.handle

        async def spy(channel, kind, message):
            self.received.append((kind, message))
            await original(channel, kind, message)
        self.host.handle = spy

        await self.host.register(self.base)
        self.serving = asyncio.create_task(self.host.serve())
        for _ in range(100):
            await asyncio.sleep(0.02)
            if self.host.socket is not None:
                break
        self.laptop = directory / "laptop"
        self.laptop.mkdir(mode=0o700)
        self.viewer = viewer.Viewer(self.out.append, self.laptop)
        return self

    async def __aexit__(self, *exc):
        await self.viewer.stop()
        await self.host.stop()
        self.serving.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await self.serving
        await self.server.close()
        self.store.close()
        self.keyring.stop()
        self.temporary.cleanup()

    # ---- helpers -------------------------------------------------------------------------------

    def emitted(self, kind: str) -> list[dict]:
        return [line for line in self.out if line.get("t") == kind]

    def messages(self, kind: str | None = None) -> list[dict]:
        return [line["message"] for line in self.out if line.get("t") == "message"
                and (kind is None or line["message"].get("t") == kind)]

    async def until(self, predicate, what: str, timeout: float = 10.0):
        deadline = asyncio.get_running_loop().time() + timeout
        while True:
            value = predicate()
            if value:
                return value
            if asyncio.get_running_loop().time() > deadline:
                raise AssertionError(f"timed out waiting for {what}; the viewer said "
                                     f"{[line.get('t') for line in self.out][-30:]}")
            await asyncio.sleep(0.02)

    async def pair(self) -> str:
        url, _ = await self.host.open_pairing()
        await self.viewer.handle({"t": "pair", "url": url, "name": "laptop",
                                  "platform": "Relay"})
        await self.until(lambda: self.emitted("welcome"), "the welcome after pairing")
        return url

    def frame(self, row: int, text: str, base: int = 0) -> None:
        self.source.set_frame({"pane": "p1", "full": False, "base": base, "history": base,
                               "cursor": {"row": row, "col": len(text), "visible": True},
                               "lines": [{"row": row, "segs": [[text, 0, 0, 0]]}]})

    def say(self, text: str) -> None:
        self.source.agent_event("p1", {"event": "delta", "text": text})

    def seqs(self, stream: str) -> list[int]:
        return [message["seq"] for message in self.messages()
                if viewer.stream_of(message) == stream]


class PairingTests(unittest.TestCase):
    def test_pairing_shows_the_code_and_keeps_the_record_private(self):
        async def main():
            capture = Capture()
            root = logging.getLogger()
            previous = root.level
            root.addHandler(capture)
            root.setLevel(logging.DEBUG)
            try:
                async with Harness() as harness:
                    url = await harness.pair()
                    # The code the laptop shows is the one the desktop's dialog showed.
                    codes = harness.emitted("code")
                    self.assertEqual([line["code"] for line in codes],
                                     [harness.requests[0].code])
                    self.assertEqual(harness.requests[0].name, "laptop")
                    # Shown before the desktop answered, not after.
                    kinds = [line["t"] for line in harness.out]
                    self.assertLess(kinds.index("code"), kinds.index("paired"))

                    paired = harness.emitted("paired")[0]
                    self.assertEqual(paired["desktop"], "test desktop")
                    self.assertEqual(paired["fingerprint"], harness.identity.fingerprint)
                    welcome = harness.emitted("welcome")[0]
                    self.assertEqual(welcome["capability"], wire.FULL)
                    self.assertIn("screen", welcome["features"])
                    self.assertEqual(harness.emitted("status")[-1]["state"], "connected")
                    # A full device of the owner's, not a guest.
                    self.assertEqual(len(harness.devices.live()), 1)
                    self.assertEqual(welcome["device"], harness.devices.live()[0].device_id)

                    record = viewer.Record.path(harness.laptop)
                    self.assertEqual(stat.S_IMODE(record.stat().st_mode), 0o600)
                    self.assertEqual(stat.S_IMODE(harness.laptop.stat().st_mode), 0o700)
                    stored = json.loads(record.read_text())
                    self.assertEqual(stored["rendezvous"], harness.base)
                    self.assertEqual(stored["desktop_name"], "test desktop")
                    self.assertEqual(stored["paired"]["capability"], wire.FULL)

                    # Neither the link's secret nor the device key reaches stdout or a log.
                    secret = parse_qs(urlsplit(url).fragment)["s"][0]
                    private = stored["paired"]["static_private"]
                    said = json.dumps(harness.out) + "\n".join(capture.lines)
                    self.assertNotIn(secret, said)
                    self.assertNotIn(private, said)
                    self.assertNotIn(url, said)
            finally:
                root.removeHandler(capture)
                root.setLevel(previous)
        run(main())

    def test_a_damaged_link_is_refused_without_echoing_it(self):
        async def main():
            async with Harness() as harness:
                url, _ = await harness.host.open_pairing()
                broken = url.split("&r=")[0]            # the room is gone
                await harness.viewer.handle({"t": "pair", "url": broken, "name": "laptop"})
                error = harness.emitted("error")[-1]["message"]
                self.assertIn("incomplete or damaged", error)
                secret = parse_qs(urlsplit(url).fragment)["s"][0]
                self.assertNotIn(secret, json.dumps(harness.out))
                await harness.viewer.handle({"t": "pair", "url": "not a link", "name": "x"})
                self.assertIn("not a Relay pairing link", harness.emitted("error")[-1]["message"])
                self.assertEqual(harness.devices.live(), [])
        run(main())

    def test_a_refused_pairing_says_so_and_stores_nothing(self):
        async def main():
            async with Harness() as harness:
                async def refuse(request):
                    harness.requests.append(request)
                    return False, wire.FULL
                harness.host.approver = refuse
                url, _ = await harness.host.open_pairing()
                await harness.viewer.handle({"t": "pair", "url": url, "name": "laptop"})
                await harness.until(lambda: harness.emitted("error"), "the refusal")
                self.assertEqual(harness.emitted("code")[0]["code"], harness.requests[0].code)
                self.assertEqual(harness.emitted("status")[-1]["state"], "unpaired")
                self.assertFalse(viewer.Record.path(harness.laptop).exists())
        run(main())

    def test_forget_deletes_the_record_and_ends_the_session(self):
        async def main():
            async with Harness() as harness:
                await harness.pair()
                self.assertTrue(viewer.Record.path(harness.laptop).exists())
                await harness.viewer.handle({"t": "forget"})
                self.assertFalse(viewer.Record.path(harness.laptop).exists())
                self.assertEqual(harness.emitted("status")[-1]["state"], "unpaired")
                self.assertFalse(harness.viewer.connected)
                await harness.viewer.handle({"t": "connect"})
                self.assertEqual(harness.emitted("status")[-1]["state"], "unpaired")
                # A fresh viewer on the same directory finds nothing either.
                self.assertIsNone(viewer.Viewer(lambda _: None, harness.laptop).record)
        run(main())

    def test_connect_uses_the_stored_record(self):
        async def main():
            async with Harness() as harness:
                await harness.pair()
                await harness.viewer.stop()
                again = viewer.Viewer(harness.out.append, harness.laptop)
                harness.viewer = again
                before = len(harness.emitted("welcome"))
                await again.handle({"t": "connect"})
                await harness.until(lambda: len(harness.emitted("welcome")) > before,
                                    "a second welcome")
                self.assertEqual(harness.emitted("status")[-1]["state"], "connected")
                self.assertEqual(harness.emitted("welcome")[-1]["desktop"], "test desktop")
        run(main())


class SessionTests(unittest.TestCase):
    def test_open_focuses_then_asks_for_the_pane_state(self):
        async def main():
            async with Harness() as harness:
                await harness.pair()
                await harness.viewer.handle({"t": "open", "pane": "p1"})
                await harness.until(lambda: harness.messages("screen_snapshot"), "a snapshot")
                kinds = [kind for kind, _ in harness.received if kind.startswith("pane_")]
                self.assertEqual(kinds[:2], ["pane_focus", "pane_state_get"])
                self.assertEqual(harness.received[-1][1].get("pane"), "p1")
                snapshot = harness.messages("screen_snapshot")[-1]
                self.assertEqual(snapshot["lines"][0]["segs"][0][0], "$ ")
                self.assertIn("base", snapshot)
                await harness.viewer.handle({"t": "close", "pane": "p1"})
                await harness.until(lambda: any(kind == "pane_blur"
                                                for kind, _ in harness.received), "pane_blur")
        run(main())

    def test_frames_and_events_are_forwarded_in_order(self):
        async def main():
            async with Harness() as harness:
                await harness.pair()
                await harness.viewer.handle({"t": "open", "pane": "p1"})
                await harness.until(lambda: harness.messages("control"), "the control state")
                for index in range(6):
                    harness.frame(1, f"line {index}", base=index)
                    harness.say(f"word {index} ")
                await harness.until(lambda: len(harness.messages("screen_diff")) == 6, "six diffs")
                await harness.until(lambda: len(harness.messages("agent")) == 6, "six deltas")
                diffs = harness.messages("screen_diff")
                self.assertEqual([d["lines"][0]["segs"][0][0] for d in diffs],
                                 [f"line {i}" for i in range(6)])
                self.assertEqual([d["base"] for d in diffs], list(range(6)))
                seqs = harness.seqs("screen:p1")
                self.assertEqual(seqs, sorted(seqs))
                texts = [m["event"]["text"] for m in harness.messages("agent")]
                self.assertEqual(texts, [f"word {i} " for i in range(6)])
                # Every line the Qt side got is one of the contract's.
                self.assertTrue({line["t"] for line in harness.out}
                                <= {"status", "code", "paired", "welcome", "message", "error"})
        run(main())

    def test_types_the_desktop_would_refuse_are_refused_here_first(self):
        async def main():
            async with Harness() as harness:
                await harness.pair()
                before = len(harness.received)
                for kind in ("store_key", "invite_create", "control_take", "set_model",
                             "made_up", "hello", "resume", "transport_switch", "pair_prove"):
                    errors = len(harness.emitted("error"))
                    await harness.viewer.handle({"t": "send", "message": {"t": kind}})
                    self.assertEqual(len(harness.emitted("error")), errors + 1, kind)
                await harness.viewer.handle({"t": "send", "message": "compose"})
                await harness.viewer.handle({"t": "send", "message": {"t": "ping", "at": 1}})
                await harness.until(lambda: harness.messages("pong"), "a pong")
                self.assertEqual([kind for kind, _ in harness.received[before:]], ["ping"])
        run(main())

    def test_control_follows_the_desktop_as_the_phone_does(self):
        async def main():
            async with Harness() as harness:
                await harness.pair()
                await harness.viewer.handle({"t": "open", "pane": "p1"})
                await harness.until(lambda: harness.messages("control"), "the control state")
                first = [line for line in harness.out if line.get("t") == "message"
                         and line["message"]["t"] == "control"][-1]
                self.assertFalse(first["driving"])
                await harness.viewer.handle({"t": "send", "message": {"t": "control_request",
                                                                      "pane": "p1"}})

                def controls():
                    return [line for line in harness.out if line.get("t") == "message"
                            and line["message"]["t"] == "control"]
                await harness.until(lambda: len(controls()) >= 2, "the take-over")
                self.assertTrue(controls()[-1]["driving"])
                await harness.viewer.handle({"t": "send", "message": {"t": "line", "pane": "p1",
                                                                      "text": "ls"}})
                await harness.until(lambda: harness.to_gui and any(
                    m.get("t") == "input" for m in harness.to_gui), "the line reaching the GUI")
                # The owner types at the desktop: the keyboard comes back, and the laptop is told
                # it lost it — and the hub then refuses its typing.
                harness.host.take_control("p1")
                await harness.until(lambda: len(controls()) >= 3, "the owner taking it back")
                self.assertFalse(controls()[-1]["driving"])
                self.assertTrue(controls()[-1]["lost"])
                await harness.viewer.handle({"t": "send", "message": {"t": "line", "pane": "p1",
                                                                      "text": "rm"}})
                await harness.until(lambda: [m for m in harness.messages("error")
                                             if m.get("code") == "not_driving"], "not_driving")
        run(main())


class ResumeTests(unittest.TestCase):
    def test_a_dropped_link_resumes_without_replaying_or_losing_a_frame(self):
        async def main():
            original = viewer.RETRY
            viewer.RETRY = (0.3,)
            try:
                async with Harness() as harness:
                    await harness.pair()
                    await harness.viewer.handle({"t": "open", "pane": "p1"})
                    await harness.until(lambda: harness.messages("control"), "the control state")
                    for index in range(3):
                        harness.frame(1, f"before {index}")
                        harness.say(f"before {index} ")
                    await harness.until(lambda: len(harness.messages("agent")) >= 3, "deltas")
                    await harness.until(lambda: len(harness.messages("screen_diff")) >= 3,
                                        "diffs")
                    welcomes = len(harness.emitted("welcome"))

                    # Kill the connection under the viewer, mid-stream, with no goodbye.
                    harness.viewer.client.socket._writer.transport.abort()
                    await harness.until(lambda: not harness.viewer.connected, "the drop")
                    # What happens while the laptop is away: the hub's rings hold it.
                    for index in range(4):
                        harness.frame(2, f"during {index}")
                        harness.say(f"during {index} ")

                    await harness.until(lambda: len(harness.emitted("welcome")) > welcomes,
                                        "the reconnect", timeout=15)
                    resumed = await harness.until(lambda: harness.messages("resumed"), "resumed")
                    # The four of each made while the laptop was away came from the rings.
                    self.assertEqual(resumed[-1]["streams"]["agent:p1"], 4)
                    self.assertEqual(resumed[-1]["streams"]["screen:p1"], 4)
                    resume = [m for kind, m in harness.received if kind == "resume"][-1]
                    self.assertEqual(set(resume["streams"]), {"panes", "agent:p1", "screen:p1"})
                    self.assertEqual(resume["hub_epoch"], harness.host.epoch)
                    for index in range(3):
                        harness.frame(3, f"after {index}")
                        harness.say(f"after {index} ")

                    agent_high = harness.host.streams["agent:p1"].seq
                    screen_high = harness.host.streams["screen:p1"].seq
                    await harness.until(lambda: harness.seqs("agent:p1")
                                        and harness.seqs("agent:p1")[-1] == agent_high,
                                        "the last delta")
                    await harness.until(lambda: harness.seqs("screen:p1")
                                        and harness.seqs("screen:p1")[-1] == screen_high,
                                        "the last diff")
                    await asyncio.sleep(0.3)        # anything replayed twice would be here now

                    for stream in ("agent:p1", "screen:p1"):
                        seqs = harness.seqs(stream)
                        self.assertEqual(seqs, list(range(seqs[0], seqs[-1] + 1)), stream)
                    texts = [m["event"]["text"] for m in harness.messages("agent")]
                    self.assertEqual(texts, [f"{phase} {i} " for phase, count in
                                             (("before", 3), ("during", 4), ("after", 3))
                                             for i in range(count)])
                    # The replay arrived ahead of the fresh snapshot pane_focus sends, so an old
                    # diff is never painted over a newer screen.
                    screen = [m for m in harness.messages()
                              if m["t"] in ("screen_snapshot", "screen_diff")]
                    during = [i for i, m in enumerate(screen) if m["t"] == "screen_diff"
                              and m["lines"][0]["segs"][0][0].startswith("during")]
                    fresh = [i for i, m in enumerate(screen) if m["t"] == "screen_snapshot"
                             and "seq" not in m]
                    self.assertLess(during[-1], fresh[-1])
                    # And the open pane was focused again on the new session.
                    self.assertEqual([kind for kind, _ in harness.received].count("pane_focus"), 2)
            finally:
                viewer.RETRY = original
        run(main())

    def test_a_new_hub_epoch_discards_what_was_held(self):
        with tempfile.TemporaryDirectory() as directory:
            v = viewer.Viewer(lambda _: None, Path(directory))
            v._adopt_epoch("first")
            v.forward({"t": "panes", "seq": 9, "items": []})
            self.assertFalse(v.streams["panes"].fresh(9))      # a replay of 9 is dropped
            v._adopt_epoch("first")
            self.assertIn("panes", v.streams)                   # same desktop: kept
            v._adopt_epoch("second")
            self.assertEqual(v.streams, {})                     # restarted: seq 1 is new again
            v.forward({"t": "panes", "seq": 1, "items": []})
            self.assertTrue(v.streams["panes"].high == 1)


class StdioTests(unittest.TestCase):
    def test_the_process_speaks_json_lines(self):
        with tempfile.TemporaryDirectory() as home:
            env = dict(os.environ, XDG_DATA_HOME=home, RELAY_KEYRING="off")
            lines = "\n".join(json.dumps(m) for m in (
                {"t": "connect"},
                {"t": "send", "message": {"t": "store_key"}},
                {"t": "stop"})) + "\n"
            # How src/RemotePane.cpp starts it.
            done = subprocess.run([sys.executable, "-m", "remote.viewer"],
                                  input=lines, capture_output=True, text=True, timeout=30,
                                  env=env, cwd=ROOT)
            self.assertEqual(done.returncode, 0, done.stderr)
            out = [json.loads(line) for line in done.stdout.splitlines()]
            self.assertEqual(out[0], {"t": "status", "state": "unpaired",
                                      "message": "This computer is not paired with a desktop "
                                                 "yet."})
            self.assertEqual(out[1]["state"], "unpaired")
            self.assertEqual(out[2]["t"], "error")
            directory = Path(home) / "relay" / "viewer"
            self.assertEqual(stat.S_IMODE(directory.stat().st_mode), 0o700)


if __name__ == "__main__":
    unittest.main()
