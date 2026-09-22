# SPDX-License-Identifier: AGPL-3.0-or-later
"""One pane model, two views: ``pane_state`` and the actions a phone sends back (section 16).

The cleaner and the capability filter on their own first, then the hub over real sockets — a
rendezvous, a Noise session per client and a GUI whose side of the pipe is a list — so what is
tested is the path a phone's message actually takes, including the guest who must never see any
of it.
"""
import asyncio
import contextlib
import copy
import json
import tempfile
import time
import unittest
from pathlib import Path

from remote import client as client_mod
from remote import gui_host, guests as guests_mod, host as host_mod, identity as identity_mod
from remote import pane_state, wire
from rendezvous.server import Store, build

APP_DIR = Path(__file__).resolve().parent.parent / "app"


def state(**changes) -> dict:
    """The contract's example with the pane renamed to the harness's."""
    out = copy.deepcopy(pane_state.EXAMPLE)
    out["pane"] = "p1"
    out.update(changes)
    return out


# ---- the cleaner ----------------------------------------------------------------------------------

class CleanTests(unittest.TestCase):
    def test_the_contract_example_survives_whole(self):
        cleaned = pane_state.clean(pane_state.EXAMPLE)
        expected = {key: value for key, value in pane_state.EXAMPLE.items() if key != "seq"}
        self.assertEqual(cleaned, expected)

    def test_not_a_pane_state_is_nothing(self):
        for bad in (None, [], {"t": "panes"}, {**state(), "v": 2}, {**state(), "pane": ""},
                    {**state(), "pane": "../etc/passwd"}, {**state(), "pane": 7}):
            self.assertIsNone(pane_state.clean(bad), bad if not isinstance(bad, dict) else bad.get("pane"))

    def test_the_allowance_is_the_desktops_words_or_absent(self):
        """Relay Free's chip: cleaned like the context, warn read off the percentage, and dropped
        whole when the desktop sent no label (the pane is not on the hosted preset)."""
        message = state()
        message["allowance"] = {"label": "Free · 73% left", "percent_left": 73, "warn": True,
                                "detail": "182,400 of 250,000 tokens today · resets at 02:00",
                                "preset": "relay-free", "base_url": "https://api.relay-terminal.ai"}
        cleaned = pane_state.clean(message)
        self.assertEqual(cleaned["allowance"], {"label": "Free · 73% left", "percent_left": 73,
                                                "warn": False,
                                                "detail": "182,400 of 250,000 tokens today · resets at 02:00"})
        for bad in ({}, {"label": ""}, {"label": "   ", "percent_left": 5}, "Free", 7):
            message = state()
            message["allowance"] = bad
            self.assertNotIn("allowance", pane_state.clean(message), bad)

    def test_the_theme_is_an_id_or_it_is_absent(self):
        """The desktop's theme reaches the phone so the two panes are the same colour (owner,
        2026-09-19). It goes into the view's `data-theme`, so it is shape-checked like every other
        id here: anything that is not one is simply not copied, and the view keeps its theme."""
        self.assertEqual(pane_state.clean(state())["theme"], "relay-dark")
        for good in ("dark-copper", "relay-light", "gruvbox-dark", "ibm-beige", "a", "0", "x" * 40):
            self.assertEqual(pane_state.clean(state(theme=good))["theme"], good, good)
        for bad in ("", "Relay-Dark", "relay dark", "relay_dark", "../../etc/passwd",
                    "/home/elliott/.local/share/relay/themes/mine.toml", "mine.toml",
                    "https://example/theme.css", "x" * 41, "relay-dark;", 7, True, None, ["relay-dark"]):
            message = state()
            message["theme"] = bad
            self.assertNotIn("theme", pane_state.clean(message), bad)
        # A desktop that names none says nothing, rather than an empty string the view would have
        # to read as "go back to the default".
        message = state()
        del message["theme"]
        self.assertNotIn("theme", pane_state.clean(message))

    def test_unknown_fields_are_dropped_at_every_level(self):
        message = state()
        message["api_key"] = "sk-live-0123456789abcdefghij"
        message["turn"]["base_url"] = "https://api.example/v1"
        message["model"]["preset"] = "openrouter-kimi"
        message["model"]["choices"][0]["preset"] = "openrouter-kimi"
        message["model"]["choices"][0]["base_url"] = "https://openrouter.ai/api/v1"
        message["queue"]["rows"][0]["path"] = "/home/elliott/.ssh/id_ed25519"
        message["sessions"]["rows"][0]["session_dir"] = "/home/elliott/.local/share/relay/s/0f3a"
        message["sessions"]["rows"][0]["session_id"] = "0f3a9e"
        message["composer"]["keyring"] = "org.relayterminal.Relay"
        cleaned = pane_state.clean(message)
        text = json.dumps(cleaned)
        for leak in ("sk-live", "api.example", "openrouter", "/home/", ".ssh", "0f3a", "keyring",
                     "org.relayterminal", "base_url", "preset", "session_dir", "session_id"):
            self.assertNotIn(leak, text, leak)

    def test_ids_must_be_ones_the_desktop_mints(self):
        """A preset id, a path or a session file name cannot be an id: the shapes exclude them."""
        message = state()
        message["model"]["choices"] = [
            {"id": "openrouter-kimi", "label": "a"}, {"id": "/home/x", "label": "b"},
            {"id": "m0", "label": "c"}, {"id": "m7", "label": "kept"}, {"id": "role:main", "label": "d"}]
        message["sessions"]["rows"] = [
            {"id": "0f3a.json", "title": "a"}, {"id": "s", "title": "b"}, {"id": "s12", "title": "kept"}]
        message["queue"]["rows"] = [
            {"id": "running", "kind": "agent"}, {"id": "entry:../x", "kind": "agent"},
            {"id": "steer:a b", "kind": "steer"}, {"id": "entry:4", "kind": "shell"},
            {"id": "entry:4", "kind": "agent", "label": "kept"}]
        cleaned = pane_state.clean(message)
        self.assertEqual([c["id"] for c in cleaned["model"]["choices"]], ["m7"])
        self.assertEqual([s["id"] for s in cleaned["sessions"]["rows"]], ["s12"])
        self.assertEqual([r["label"] for r in cleaned["queue"]["rows"]], ["kept"])

    def test_a_key_or_a_base_url_in_a_label_is_cut_out(self):
        message = state()
        message["model"]["label"] = "kimi · https://api.moonshot.ai/v1 · /home/me/models/q4.gguf"
        message["model"]["choices"][0]["label"] = "sk-ant-api03-0123456789abcdefABCDEF please"
        message["thinking"]["tail"] = "the key is sk-proj-AAAAAAAAAAAAAAAAAAAAAAAA ok"
        message["queue"]["rows"][1]["label"] = "✦ use api_key=abcdefghijklmnop0123456789"
        cleaned = pane_state.clean(message)
        text = json.dumps(cleaned)
        for leak in ("moonshot", "/home/me", "q4.gguf", "sk-ant", "sk-proj", "abcdefghijklmnop0123"):
            self.assertNotIn(leak, text, leak)
        self.assertTrue(cleaned["model"]["label"].startswith("kimi"))
        self.assertIn("[redacted]", cleaned["thinking"]["tail"])

    def test_caps(self):
        message = state()
        message["model"]["label"] = "x" * 5000
        message["thinking"]["tail"] = "y" * 9000 + "END"
        message["queue"]["rows"] = [{"id": f"entry:{i}", "kind": "agent", "label": "r",
                                     "state": "queued", "actions": ["remove"]} for i in range(1, 200)]
        message["sessions"]["rows"] = [{"id": f"s{i}", "title": "t"} for i in range(1, 120)]
        message["model"]["choices"] = [{"id": f"m{i}", "label": "c"} for i in range(1, 100)]
        cleaned = pane_state.clean(message)
        self.assertEqual(len(cleaned["model"]["label"]), pane_state.LABEL_MAX)
        self.assertEqual(len(cleaned["thinking"]["tail"]), pane_state.TAIL_MAX)
        self.assertTrue(cleaned["thinking"]["tail"].endswith("END"), "the tail is the newest text")
        self.assertEqual(len(cleaned["queue"]["rows"]), pane_state.ROWS_MAX)
        self.assertEqual(len(cleaned["sessions"]["rows"]), pane_state.SESSIONS_MAX)
        self.assertEqual(len(cleaned["model"]["choices"]), pane_state.CHOICES_MAX)

    def test_enumerations_are_checked(self):
        message = state()
        message["turn"]["phase"] = "rm -rf"
        message["composer"]["mode"] = "root"
        message["composer"]["modes"] = ["agent", "sudo", "shell"]
        message["queue"]["rows"][0]["state"] = "<script>"
        message["queue"]["rows"][0]["actions"] = ["remove", "remove", "delete_everything", "edit"]
        message["context"]["percent_left"] = 250
        cleaned = pane_state.clean(message)
        self.assertEqual(cleaned["turn"]["phase"], "idle")
        self.assertEqual(cleaned["composer"]["mode"], "auto")
        self.assertEqual(cleaned["composer"]["modes"], ["shell", "agent"])
        self.assertEqual(cleaned["queue"]["rows"][0]["state"], "queued")
        self.assertEqual(cleaned["queue"]["rows"][0]["actions"], ["remove", "edit"])
        self.assertEqual(cleaned["context"]["percent_left"], 100)
        message["context"]["percent_left"] = True
        self.assertIsNone(pane_state.clean(message)["context"]["percent_left"])

    def test_running_is_null_when_nothing_runs(self):
        message = state()
        message["queue"]["running"] = None
        self.assertIsNone(pane_state.clean(message)["queue"]["running"])


class CapabilityTests(unittest.TestCase):
    def setUp(self):
        self.cleaned = pane_state.clean(state())

    def test_a_view_device_is_offered_nothing_to_press(self):
        view = pane_state.for_capability(self.cleaned, wire.VIEW)
        self.assertTrue(view["queue"]["rows"])
        for row in view["queue"]["rows"]:
            self.assertNotIn("actions", row)
        self.assertNotIn("choices", view["model"])
        self.assertEqual(view["model"]["label"], "fake · local")
        # A viewer observes *this* conversation. The ones before it are the owner's level, so the
        # whole block goes rather than its buttons (owner, 2026-09-18).
        self.assertNotIn("sessions", view)
        self.assertEqual(view["composer"]["modes"], [])

    def test_a_partner_types_here_and_sees_no_other_conversation(self):
        agent = pane_state.for_capability(self.cleaned, wire.AGENT)
        self.assertEqual(agent["composer"]["modes"], ["agent"])
        self.assertTrue(agent["queue"]["rows"][0]["actions"])
        self.assertTrue(agent["model"]["choices"])
        # "can type in this convo" and nothing about the others: not their titles, not their count.
        self.assertNotIn("sessions", agent)

    def test_only_an_owner_device_reaches_the_other_conversations(self):
        full = pane_state.for_capability(self.cleaned, wire.FULL)
        self.assertEqual(full["composer"]["modes"], ["auto", "shell", "agent"])
        self.assertTrue(full["sessions"]["rows"])
        self.assertTrue(full["sessions"]["can_new"])
        self.assertTrue(full["sessions"]["can_open"])

    def test_no_capability_is_nothing(self):
        for capability in (None, "", "owner", "editor", "viewer"):
            self.assertIsNone(pane_state.for_capability(self.cleaned, capability), capability)

    def test_every_level_sees_the_theme(self):
        """How the pane looks is not a permission: there is nothing to press in a theme id and
        nothing in it to leak, so a viewer's pane is the same colour as the owner's."""
        for capability in (wire.VIEW, wire.AGENT, wire.FULL):
            self.assertEqual(pane_state.for_capability(self.cleaned, capability)["theme"],
                             "relay-dark", capability)

    def test_filtering_never_touches_the_stored_state(self):
        pane_state.for_capability(self.cleaned, wire.VIEW)
        self.assertIn("actions", self.cleaned["queue"]["rows"][0])
        self.assertIn("choices", self.cleaned["model"])


class WireTests(unittest.TestCase):
    NEW_CLIENT = ("queue_move", "queue_edit", "queue_send_now", "queue_resume", "model_pick",
                  "conversation_new", "conversation_open", "conversation_id", "pane_state_get")

    def test_the_new_client_types_are_classified(self):
        for kind in self.NEW_CLIENT:
            self.assertIn(kind, wire.CLIENT_TYPES, kind)
            self.assertIn(kind, wire.GUEST_NEVER, kind)
            self.assertNotIn(kind, wire.GUEST_TYPES, kind)
            self.assertNotIn(kind, wire.NEVER_FROM_CLIENT, kind)
        # A partner types here: the queue and the model are its level.
        for kind in ("queue_move", "queue_edit", "queue_send_now", "queue_resume", "model_pick"):
            self.assertEqual(wire.CLIENT_TYPES[kind], wire.AGENT, kind)
        # The conversations before this one are the owner's level (owner, 2026-09-18) — and so is
        # naming one of them outside Relay (2026-09-22, `Copy id`).
        for kind in ("conversation_new", "conversation_open", "conversation_id"):
            self.assertEqual(wire.CLIENT_TYPES[kind], wire.FULL, kind)
        # Reading, like screen_get: a view device is sent pane_state, so it may ask for one.
        self.assertEqual(wire.CLIENT_TYPES["pane_state_get"], wire.VIEW)

    def test_the_new_server_types_are_classified(self):
        for kind in ("pane_state", "queue_edit_text", "conversation_id_text"):
            self.assertIn(kind, wire.SERVER_TYPES, kind)
            if hasattr(wire, "GUEST_SERVER_TYPES"):
                self.assertNotIn(kind, wire.GUEST_SERVER_TYPES, kind)

    def test_every_handler_exists(self):
        for kind in self.NEW_CLIENT:
            self.assertTrue(callable(getattr(host_mod.Host, f"_on_{kind}", None)), kind)

    def test_set_model_stays_forbidden(self):
        """model_pick is a token from pane_state, never the worker's set_model with a preset."""
        self.assertIn("set_model", wire.NEVER_FROM_CLIENT)
        self.assertIn("conversation_delete", wire.NEVER_FROM_CLIENT)
        # conversation_open is offered now, but only at the owner's level and only by a token
        # from a pane_state: never a path, a session file name or the worker's own types.
        self.assertEqual(wire.CLIENT_TYPES["conversation_open"], wire.FULL)
        self.assertNotIn("conversation_resume", wire.CLIENT_TYPES)
        with self.assertRaises(wire.WireError):
            pane_state.session_of({"session": "../../etc/passwd"})
        with self.assertRaises(wire.WireError):
            pane_state.session_of({"session": "2026-09-18-thinking.json"})
        self.assertEqual(pane_state.session_of({"session": "s3"}), "s3")


# ---- the hub ------------------------------------------------------------------------------------

def run(coroutine, timeout=60):
    return asyncio.run(asyncio.wait_for(coroutine, timeout))


class Harness:
    """A hub over a GuiPaneSource with one pane, a guest store, and the GUI's pipe as a list."""

    def __init__(self, capability=wire.AGENT):
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
        self.guests = guests_mod.GuestStore(directory, devices=self.devices)
        self.source = gui_host.GuiPaneSource(self.to_gui.append)
        self.source.set_pane({"id": "p1", "title": "relay", "cwd": "/home/elliott", "rows": 24,
                              "cols": 80, "status": "idle"})
        self.source.set_frame({"pane": "p1", "full": True, "rows": 24, "cols": 80, "alt": False,
                               "cursor": {"row": 0, "col": 0, "visible": True, "shape": 0},
                               "lines": []})

        async def approver(request):
            return True, self.capability

        async def knock_approver(request):
            return True, request.role

        self.host = host_mod.Host(self.identity, self.devices, self.source, app_base=self.base,
                                  approver=approver, knock_approver=knock_approver,
                                  guests=self.guests, name="test desktop")
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

    async def device(self, capability=None, name="iPad"):
        if capability is not None:
            self.capability = capability
        url, _ = await self.host.open_pairing()
        pairing_client = client_mod.Client(self.base)
        record = await pairing_client.pair(url, name=name, platform="Safari")
        await pairing_client.close()
        client = client_mod.Client(self.base)
        await client.connect(record)
        await client.expect("panes")
        return client, record

    async def guest(self, role=wire.EDITOR):
        _, url = await self.host.invite_create(["p1"], role)
        client = client_mod.Client(self.base)
        await client.knock(url, name="alice", platform="Chrome")
        await client.expect("panes")
        return client

    def sent(self, kind: str) -> list[dict]:
        return [message for message in self.to_gui if message.get("t") == kind]

    async def settle(self, kind: str, timeout: float = 5.0) -> dict:
        deadline = time.monotonic() + timeout
        while not self.sent(kind):
            if time.monotonic() > deadline:
                raise AssertionError(f"the GUI was never sent {kind!r}: "
                                     f"{[m.get('t') for m in self.to_gui]}")
            await asyncio.sleep(0.02)
        return self.sent(kind)[-1]

    def publish(self, message=None) -> None:
        """What the sidecar does with a `pane_state` line from the GUI."""
        self.host.pane_state_from_gui(message or state())


async def drain(client, seconds: float = 0.5) -> list[dict]:
    """Everything that reaches a client's socket in the next moment."""
    got = []
    deadline = time.monotonic() + seconds
    while True:
        left = deadline - time.monotonic()
        if left <= 0:
            return got
        try:
            got.append(await asyncio.wait_for(client.inbox.get(), left))
        except asyncio.TimeoutError:
            return got


class FanOutTests(unittest.TestCase):
    def test_a_watching_device_gets_the_state_and_an_idle_one_does_not(self):
        async def main():
            async with Harness() as harness:
                watching, _ = await harness.device(name="phone")
                idle, _ = await harness.device(name="tablet")
                await watching.send({"t": "pane_focus", "pane": "p1"})
                await watching.expect("screen_snapshot")
                harness.publish()
                got = await watching.expect("pane_state")
                self.assertEqual(got["pane"], "p1")
                self.assertEqual(got["seq"], 1)
                self.assertEqual(got["queue"]["rows"][0]["id"], "steer:steer-3")
                self.assertEqual(got["composer"]["modes"], ["agent"])     # an agent device
                harness.publish()
                self.assertEqual((await watching.expect("pane_state"))["seq"], 2)
                self.assertFalse([m for m in await drain(idle) if m["t"] == "pane_state"])
                await watching.close()
                await idle.close()
        run(main())

    def test_capability_is_read_as_each_state_is_sent(self):
        async def main():
            async with Harness(capability=wire.FULL) as harness:
                client, record = await harness.device()
                await client.send({"t": "pane_focus", "pane": "p1"})
                await client.expect("screen_snapshot")
                harness.publish()
                first = await client.expect("pane_state")
                self.assertIn("actions", first["queue"]["rows"][0])
                self.assertIn("choices", first["model"])
                harness.devices.set_capability(record.device_id, wire.VIEW)
                harness.publish()
                second = await client.expect("pane_state")
                self.assertNotIn("actions", second["queue"]["rows"][0])
                self.assertNotIn("choices", second["model"])
                # Downgraded to a viewer mid-stream: the other conversations go with the buttons.
                self.assertNotIn("sessions", second)
                await client.close()
        run(main())

    def test_a_state_for_a_pane_that_is_gone_is_dropped(self):
        async def main():
            async with Harness() as harness:
                harness.publish(state(pane="p9"))
                self.assertNotIn("p9", harness.host._pane_state_book().latest)
        run(main())

    def test_resume_never_replays_a_state(self):
        """Kept out of the stream rings: a replay would skip the capability filter."""
        async def main():
            async with Harness() as harness:
                harness.publish()
                self.assertFalse([name for name in harness.host.streams if "pane_state" in name])
        run(main())


class GuestTests(unittest.TestCase):
    """Section 16's guest rule, at the socket: nothing about the owner's pane model reaches one."""

    def test_a_guest_on_the_pane_never_receives_a_state(self):
        async def main():
            async with Harness() as harness:
                guest = await harness.guest(role=wire.EDITOR)
                await guest.send({"t": "pane_focus", "pane": "p1"})
                owner, _ = await harness.device()
                await owner.send({"t": "pane_focus", "pane": "p1"})
                await owner.expect("screen_snapshot")
                harness.publish()
                await owner.expect("pane_state")
                harness.publish()
                seen = await drain(guest, 0.8)
                self.assertFalse([m for m in seen if m.get("t") in ("pane_state", "queue_edit_text")],
                                 [m.get("t") for m in seen])
                await guest.close()
                await owner.close()
        run(main())

    def test_a_guest_can_neither_ask_for_nor_act_on_a_state(self):
        async def main():
            async with Harness() as harness:
                harness.publish()
                guest = await harness.guest(role=wire.EDITOR)
                for message in ({"t": "pane_state_get", "pane": "p1"},
                                {"t": "queue_move", "pane": "p1", "row": "entry:4", "to": "up"},
                                {"t": "queue_edit", "pane": "p1", "row": "entry:4"},
                                {"t": "queue_send_now", "pane": "p1", "row": "steer:steer-3"},
                                {"t": "queue_resume", "pane": "p1"},
                                {"t": "model_pick", "pane": "p1", "choice": "m1"},
                                {"t": "conversation_new", "pane": "p1"}):
                    await guest.send(message)
                    with self.assertRaises(wire.WireError) as refused:
                        await guest.expect("pane_state")
                    self.assertEqual(refused.exception.code, "not_permitted", message["t"])
                for kind in ("queue_move", "queue_edit", "queue_send_now", "queue_resume",
                             "model_pick", "conversation_new", "pane_state_get"):
                    self.assertFalse(harness.sent(kind), kind)
                await guest.close()
        run(main())

    def test_a_forged_edit_answer_reaches_nobody(self):
        async def main():
            async with Harness() as harness:
                guest = await harness.guest(role=wire.EDITOR)
                owner, _ = await harness.device()
                harness.host.pane_state_from_gui({"t": "queue_edit_text", "id": "qe1", "pane": "p1",
                                                  "row": "entry:4", "text": "secret prompt"})
                for client in (guest, owner):
                    self.assertFalse([m for m in await drain(client, 0.4)
                                      if m.get("t") == "queue_edit_text"])
                await guest.close()
                await owner.close()
        run(main())


class ActionTests(unittest.TestCase):
    def test_pane_state_get_answers_from_the_latest_state(self):
        async def main():
            async with Harness() as harness:
                client, _ = await harness.device()
                harness.publish()
                await client.send({"t": "pane_state_get", "pane": "p1", "id": 7})
                got = await client.expect("pane_state")
                self.assertEqual(got["id"], 7)
                self.assertEqual(got["seq"], 1)
                self.assertFalse(harness.sent("pane_state_get"), "the hub had it; the GUI is not asked")
                await client.close()
        run(main())

    def test_pane_state_get_before_any_state_asks_the_gui(self):
        async def main():
            async with Harness(capability=wire.VIEW) as harness:
                client, _ = await harness.device()
                await client.send({"t": "pane_state_get", "pane": "p1", "id": "a"})
                line = await harness.settle("pane_state_get")
                self.assertEqual(line, {"t": "pane_state_get", "pane": "p1"})
                harness.publish()
                got = await client.expect("pane_state")
                self.assertEqual(got["id"], "a")       # answered although never focused
                self.assertNotIn("actions", got["queue"]["rows"][0])   # a view device
                await client.close()
        run(main())

    def test_pane_state_get_for_an_unknown_pane_is_refused(self):
        async def main():
            async with Harness() as harness:
                client, _ = await harness.device()
                await client.send({"t": "pane_state_get", "pane": "nope"})
                with self.assertRaises(wire.WireError) as refused:
                    await client.expect("pane_state")
                self.assertEqual(refused.exception.code, "no_such_pane")
                await client.close()
        run(main())

    def test_queue_actions_reach_the_gui_with_ids_only(self):
        async def main():
            async with Harness() as harness:
                client, record = await harness.device(name="Pixel 9")
                await client.send({"t": "queue_move", "pane": "p1", "row": "entry:4", "to": "steer"})
                move = await harness.settle("queue_move")
                self.assertEqual(move, {"t": "queue_move", "pane": "p1", "row": "entry:4",
                                        "to": "steer", "origin": f"remote:{record.device_id}",
                                        "device_name": "Pixel 9"})
                await client.send({"t": "queue_send_now", "pane": "p1", "row": "steer:steer-3",
                                   "text": "ignored", "item": "ignored"})
                now = await harness.settle("queue_send_now")
                self.assertEqual(set(now), {"t", "pane", "row", "origin", "device_name"})
                # The empty send: no row, nothing to aim, and the pane decides (#7JD1).
                await client.send({"t": "queue_resume", "pane": "p1", "row": "ignored"})
                resumed = await harness.settle("queue_resume")
                self.assertEqual(resumed, {"t": "queue_resume", "pane": "p1",
                                           "origin": f"remote:{record.device_id}",
                                           "device_name": "Pixel 9"})
                # conversation_new and conversation_open are the owner's level, so this device
                # is refused and a full one is not.
                await client.send({"t": "conversation_new", "pane": "p1"})
                refused = await client.expect("error")
                self.assertEqual(refused["code"], "not_permitted")
                owner, _ = await harness.device(wire.FULL, name="laptop")
                await owner.send({"t": "conversation_new", "pane": "p1"})
                self.assertEqual((await harness.settle("conversation_new"))["pane"], "p1")
                await owner.send({"t": "conversation_open", "pane": "p1", "session": "s2"})
                opened = await harness.settle("conversation_open")
                self.assertEqual((opened["pane"], opened["session"]), ("p1", "s2"))
                await owner.close()
                await client.close()
        run(main())

    def test_bad_rows_moves_and_choices_are_refused_before_the_gui(self):
        async def main():
            async with Harness() as harness:
                client, _ = await harness.device()
                for message in ({"t": "queue_move", "pane": "p1", "row": "running", "to": "up"},
                                {"t": "queue_move", "pane": "p1", "row": "entry:4", "to": "delete"},
                                {"t": "queue_edit", "pane": "p1", "row": "../../x"},
                                {"t": "queue_send_now", "pane": "p1"},
                                {"t": "model_pick", "pane": "p1", "choice": "openrouter-kimi"},
                                {"t": "model_pick", "pane": "p1", "choice": "/etc/passwd"},
                                {"t": "model_pick", "pane": "p1", "choice": "role:main"}):
                    await client.send(message)
                    with self.assertRaises(wire.WireError) as refused:
                        await client.expect("pane_state")
                    self.assertEqual(refused.exception.code, "unknown_type", message)
                for kind in ("queue_move", "queue_edit", "queue_send_now", "model_pick"):
                    self.assertFalse(harness.sent(kind), kind)
                await client.close()
        run(main())

    def test_a_view_device_cannot_act(self):
        async def main():
            async with Harness(capability=wire.VIEW) as harness:
                client, _ = await harness.device()
                for message in ({"t": "queue_move", "pane": "p1", "row": "entry:4", "to": "up"},
                                {"t": "model_pick", "pane": "p1", "choice": "m1"},
                                {"t": "conversation_new", "pane": "p1"}):
                    await client.send(message)
                    with self.assertRaises(wire.WireError) as refused:
                        await client.expect("pane_state")
                    self.assertEqual(refused.exception.code, "not_permitted")
                self.assertFalse(harness.sent("model_pick"))
                await client.close()
        run(main())

    def test_model_pick_names_the_device(self):
        async def main():
            async with Harness() as harness:
                client, _ = await harness.device(name="Elliott's iPhone")
                await client.send({"t": "model_pick", "pane": "p1", "choice": "m2"})
                pick = await harness.settle("model_pick")
                self.assertEqual(pick["choice"], "m2")
                self.assertEqual(pick["device_name"], "Elliott's iPhone")
                await client.close()
        run(main())

    def test_queue_edit_text_goes_back_to_the_device_that_asked(self):
        async def main():
            async with Harness() as harness:
                asker, _ = await harness.device(name="phone")
                other, _ = await harness.device(name="tablet")
                await other.send({"t": "pane_focus", "pane": "p1"})
                await asker.send({"t": "queue_edit", "pane": "p1", "row": "entry:4", "id": 3})
                line = await harness.settle("queue_edit")
                self.assertTrue(line["id"].startswith("qe"), "the hub mints the GUI's id")
                harness.host.pane_state_from_gui({"t": "queue_edit_text", "id": line["id"],
                                                  "pane": "p1", "row": "entry:4",
                                                  "text": "then run the tests\nand the linter"})
                got = await asker.expect("queue_edit_text")
                self.assertEqual(got, {"t": "queue_edit_text", "pane": "p1", "row": "entry:4",
                                       "text": "then run the tests\nand the linter", "id": 3})
                self.assertFalse([m for m in await drain(other, 0.4)
                                  if m.get("t") == "queue_edit_text"])
                # Answered once: a second answer to the same id reaches nobody.
                harness.host.pane_state_from_gui({"t": "queue_edit_text", "id": line["id"],
                                                  "pane": "p1", "row": "entry:4", "text": "again"})
                self.assertFalse([m for m in await drain(asker, 0.3)
                                  if m.get("t") == "queue_edit_text"])
                await asker.close()
                await other.close()
        run(main())

    def test_a_refused_edit_is_an_error_on_the_phone(self):
        async def main():
            async with Harness() as harness:
                client, _ = await harness.device()
                await client.send({"t": "queue_edit", "pane": "p1", "row": "entry:4", "id": 1})
                line = await harness.settle("queue_edit")
                harness.host.pane_state_from_gui({"t": "queue_edit_text", "id": line["id"],
                                                  "pane": "p1", "row": "entry:4", "ok": False,
                                                  "error": "that row has already run."})
                with self.assertRaises(wire.WireError) as refused:
                    await client.expect("queue_edit_text")
                self.assertIn("already run", refused.exception.message)
                await client.close()
        run(main())

    def test_conversation_id_goes_to_the_device_that_asked_and_to_nobody_else(self):
        async def main():
            async with Harness() as harness:
                asker, _ = await harness.device(capability=wire.FULL, name="phone")
                other, _ = await harness.device(capability=wire.FULL, name="tablet")
                await other.send({"t": "pane_focus", "pane": "p1"})
                await asker.send({"t": "conversation_id", "pane": "p1", "session": "s2", "id": 7})
                line = await harness.settle("conversation_id")
                self.assertEqual(line["session"], "s2")
                self.assertTrue(line["id"].startswith("ci"), "the hub mints the GUI's id")
                harness.host.pane_state_from_gui(
                    {"t": "conversation_id_text", "id": line["id"], "pane": "p1", "session": "s2",
                     "conversation": "9f2c7a1e-4b3d-4e5f-8a90-1b2c3d4e5f60"})
                got = await asker.expect("conversation_id_text")
                self.assertEqual(got, {"t": "conversation_id_text", "pane": "p1", "session": "s2",
                                       "conversation": "9f2c7a1e-4b3d-4e5f-8a90-1b2c3d4e5f60",
                                       "id": 7})
                self.assertFalse([m for m in await drain(other, 0.4)
                                  if m.get("t") == "conversation_id_text"])
                # Answered once: a replayed answer to the same id reaches nobody.
                harness.host.pane_state_from_gui(
                    {"t": "conversation_id_text", "id": line["id"], "pane": "p1", "session": "s2",
                     "conversation": "again"})
                self.assertFalse([m for m in await drain(asker, 0.3)
                                  if m.get("t") == "conversation_id_text"])
                await asker.close()
                await other.close()
        run(main())

    def test_a_refused_or_wrong_conversation_answer_is_an_error_on_the_phone(self):
        async def main():
            async with Harness() as harness:
                client, _ = await harness.device(capability=wire.FULL)
                await client.send({"t": "conversation_id", "pane": "p1", "session": "s1", "id": 1})
                line = await harness.settle("conversation_id")
                harness.host.pane_state_from_gui(
                    {"t": "conversation_id_text", "id": line["id"], "pane": "p1", "session": "s1",
                     "ok": False, "error": "that conversation is not in the pane's list anymore."})
                with self.assertRaises(wire.WireError) as refused:
                    await client.expect("conversation_id_text")
                self.assertIn("not in the pane's list", refused.exception.message)
                # An answer naming another session than the ask is stopped here, not delivered.
                await client.send({"t": "conversation_id", "pane": "p1", "session": "s1", "id": 2})
                line = await harness.settle("conversation_id")
                harness.host.pane_state_from_gui(
                    {"t": "conversation_id_text", "id": line["id"], "pane": "p1", "session": "s9",
                     "conversation": "someone-elses-session"})
                self.assertFalse([m for m in await drain(client, 0.3)
                                  if m.get("t") == "conversation_id_text"])
                await client.close()
        run(main())

    def test_a_conversation_answer_is_an_id_not_a_path(self):
        ok, conversation = pane_state.conversation_answer(
            {"conversation": "9f2c7a1e\n../../../etc/passwd"})
        self.assertTrue(ok)
        self.assertNotIn("/", conversation)
        self.assertNotIn("\n", conversation)
        ok, conversation = pane_state.conversation_answer({"conversation": "x" * 500})
        self.assertTrue(ok)
        self.assertLessEqual(len(conversation), 64)
        ok, reason = pane_state.conversation_answer({"ok": False, "error": "gone"})
        self.assertFalse(ok)
        self.assertEqual(reason, "gone")
        ok, reason = pane_state.conversation_answer({})
        self.assertFalse(ok)

    def test_a_wedged_gui_is_an_error_not_a_spinner(self):
        async def main():
            saved = host_mod.QUEUE_EDIT_TIMEOUT
            host_mod.QUEUE_EDIT_TIMEOUT = 0.3
            try:
                async with Harness() as harness:
                    client, _ = await harness.device()
                    await client.send({"t": "queue_edit", "pane": "p1", "row": "entry:4", "id": 1})
                    with self.assertRaises(wire.WireError) as refused:
                        await client.expect("queue_edit_text", timeout=5)
                    self.assertIn("did not answer", refused.exception.message)
                    self.assertFalse(harness.host._pane_state_book().edits)
                    await client.close()
            finally:
                host_mod.QUEUE_EDIT_TIMEOUT = saved
        run(main())


class SidecarTests(unittest.TestCase):
    def test_the_sidecar_passes_both_lines_to_the_hub(self):
        async def main():
            sidecar = gui_host.Sidecar()
            got = []

            class Hub:
                def pane_state_from_gui(self, message):
                    got.append(message["t"])

            sidecar.host = Hub()
            await sidecar.handle({"t": "pane_state"})
            await sidecar.handle({"t": "queue_edit_text"})
            self.assertEqual(got, ["pane_state", "queue_edit_text"])
            sidecar.host = None
            await sidecar.handle({"t": "pane_state"})       # not started: nothing, no error
        run(main())


if __name__ == "__main__":
    unittest.main()
