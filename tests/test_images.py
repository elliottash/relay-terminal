# SPDX-License-Identifier: AGPL-3.0-or-later
"""Image context in agent prompts (issue EM1E, docs/AGENT-SESSIONS-PROTOCOL.md section 17).

Everything here runs offline against a recording provider: no API key, no network. The provider
transport is replaced with a class that keeps the ProviderConfig it was built with, which is how
each test proves *which model actually served the turn*.
"""
import base64
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from relay_core import agent as agent_module
from relay_core import attachments, context, presets
from relay_core import provider as transport
from relay_core.agent import Agent
from relay_core.provider import ProviderConfig
from relay_core.roles import RoleResolver

# A 1x1 PNG, written out so the tests need no image library and no fixture file.
PNG = base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==")
GIF = b"GIF89a" + b"\x00" * 20
JPEG = b"\xff\xd8\xff\xe0" + b"\x00" * 20

GLM = ProviderConfig("https://api.z.ai/api/paas/v4", "glm-5.3", "key",
                     {"thinking": {"type": "enabled"}, "reasoning_effort": "high"}, 8192)
KIMI = ProviderConfig("https://api.moonshot.ai/v1", "kimi-k3", "key", {"reasoning_effort": "high"}, 8192)
OPENAI = ProviderConfig("https://api.openai.com/v1", "gpt-6-astra", "key", {"reasoning_effort": "high"}, 8192)


class RecordingProvider:
    """Stands in for ChatProvider. Records every turn it served, with the model it was built for."""

    served: list = []
    # The conversation as the agent handed it over, before `wire_messages` strips Relay's own keys:
    # a swap has to convert it to the serving model's reasoning dialect, which is visible only here.
    raw: list = []
    # The agent's live context window at each call, and the agent to read it off. A turn routed to
    # another model must be measured against *that* model's window, not the pane's.
    windows: list = []
    agent = None

    def __init__(self, config, stall_timeout=60.0):
        self.config = config
        self.calls = 0

    def complete(self, messages, tools, emit, cancel):
        self.calls += 1
        RecordingProvider.served.append((self.config.model, transport.wire_messages(messages)))
        RecordingProvider.raw.append([dict(message) for message in messages])
        RecordingProvider.windows.append(
            RecordingProvider.agent.context.window if RecordingProvider.agent is not None else None)
        emit({"event": "delta", "text": "A red square."})
        return {"role": "assistant", "content": "A red square."}

    def cancel(self):
        pass

    def response_open(self):
        return False

    def set_stall_timeout(self, seconds):
        return seconds


class RefusingProvider(RecordingProvider):
    """The recording provider, refusing outright for the models named in ``refuse``.

    Nothing is streamed before the refusal, which is the case that may move a turn: a call that has
    put part of an answer on the screen is never retried or re-routed (protocol 15.2).
    """

    refuse: frozenset = frozenset()

    def complete(self, messages, tools, emit, cancel):
        if self.config.model in RefusingProvider.refuse:
            RecordingProvider.served.append((self.config.model, []))
            raise transport.ProviderError(f"Provider HTTP 503 for {self.config.model}.")
        return super().complete(messages, tools, emit, cancel)


def resolver(main: ProviderConfig, preset_id: str, roles=None):
    """A RoleResolver with every provider's key present, so nothing falls back for lack of a key."""
    return RoleResolver(main, preset_id, roles or {}, key_lookup=lambda preset: "key")


class ImageFileTests(unittest.TestCase):
    """Reading an image off disk: the `@path`, paste, drop and screenshot inputs all land here."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def write(self, name: str, data: bytes) -> str:
        path = self.root / name
        path.write_bytes(data)
        return str(path)

    def test_an_image_attachment_is_loaded_as_bytes_with_its_media_type(self):
        path = self.write("shot.png", PNG)
        loaded = attachments.load([{"path": path}], self.root)
        self.assertEqual(len(loaded), 1)
        self.assertEqual(loaded[0]["kind"], "image")
        self.assertEqual(loaded[0]["media_type"], "image/png")
        self.assertEqual(loaded[0]["raw"], PNG)
        self.assertEqual(attachments.images(loaded), loaded)

    def test_the_type_comes_from_the_bytes_not_the_file_name(self):
        # A PNG called .jpg is still a PNG; the provider is told what is actually sent.
        path = self.write("mislabelled.jpg", PNG)
        self.assertEqual(attachments.load([{"path": path}], self.root)[0]["media_type"], "image/png")
        self.assertEqual(attachments.load([{"path": self.write("a.gif", GIF)}], self.root)[0]["media_type"],
                         "image/gif")
        self.assertEqual(attachments.load([{"path": self.write("a.jpg", JPEG)}], self.root)[0]["media_type"],
                         "image/jpeg")
        webp = b"RIFF" + b"\x00\x00\x00\x00" + b"WEBP" + b"\x00" * 16
        self.assertEqual(attachments.load([{"path": self.write("a.webp", webp)}], self.root)[0]["media_type"],
                         "image/webp")

    def test_a_text_attachment_still_loads_as_text(self):
        loaded = attachments.load([{"path": self.write("notes.txt", b"hello\n")}], self.root)
        self.assertEqual(loaded[0]["kind"], "file")
        self.assertIn("hello", attachments.format_block(loaded))

    def test_an_image_never_appears_as_text_in_the_prompt(self):
        loaded = attachments.load([{"path": self.write("shot.png", PNG)}], self.root)
        self.assertEqual(attachments.format_block(loaded), "")
        block = attachments.image_block(loaded)
        self.assertIn("shot.png", block)
        self.assertNotIn(base64.b64encode(PNG).decode("ascii")[:20], block)

    def test_an_image_over_the_cap_is_refused_with_a_message(self):
        big = PNG + b"\x00" * transport.MAX_IMAGE_BYTES
        with self.assertRaises(ValueError) as caught:
            attachments.load([{"path": self.write("huge.png", big)}], self.root)
        self.assertIn("larger than", str(caught.exception))

    def test_a_non_image_binary_is_still_refused(self):
        with self.assertRaises(ValueError):
            attachments.load([{"path": self.write("blob.bin", b"\x00\x01\x02" * 100)}], self.root)


class ContentPartTests(unittest.TestCase):
    """The provider layer: OpenAI-compatible multimodal content parts with base64 data URLs."""

    def test_the_text_comes_first_and_the_image_is_a_base64_data_url(self):
        parts = transport.content_parts("what is this?", [{"media_type": "image/png", "raw": PNG}])
        self.assertEqual(parts[0], {"type": "text", "text": "what is this?"})
        self.assertEqual(parts[1]["type"], "image_url")
        url = parts[1]["image_url"]["url"]
        self.assertTrue(url.startswith("data:image/png;base64,"))
        self.assertEqual(base64.b64decode(url.split(",", 1)[1]), PNG)

    def test_an_unsupported_type_and_an_oversized_image_are_refused(self):
        with self.assertRaises(ValueError):
            transport.data_url("image/tiff", PNG)
        with self.assertRaises(ValueError):
            transport.data_url("image/png", b"\x00" * (transport.MAX_IMAGE_BYTES + 1))

    def test_too_many_or_too_large_a_batch_is_refused(self):
        one = {"media_type": "image/png", "raw": PNG}
        with self.assertRaises(ValueError):
            transport.content_parts("hi", [one] * (transport.MAX_IMAGES_PER_TURN + 1))
        heavy = {"media_type": "image/png", "raw": b"\x89PNG\r\n\x1a\n" + b"\x00" * (transport.MAX_IMAGE_BYTES - 8)}
        with self.assertRaises(ValueError):
            transport.content_parts("hi", [heavy] * 3)

    def test_relay_bookkeeping_keys_never_reach_the_wire(self):
        message = {"role": "user", "content": transport.content_parts("hi", [{"media_type": "image/png", "raw": PNG}]),
                   "relay_kind": "prompt", "relay_images": [{"path": "/tmp/a.png"}]}
        wired = transport.wire_messages([message])[0]
        self.assertNotIn("relay_images", wired)
        self.assertTrue(transport.has_images([wired]))

    def test_an_image_is_estimated_as_a_constant_not_as_its_base64_length(self):
        messages = [{"role": "user", "content": transport.content_parts(
            "hi", [{"media_type": "image/png", "raw": b"\x89PNG\r\n\x1a\n" + b"\x00" * 900_000}])}]
        # Without the image-aware estimate this is ~300k tokens and compacts in mid-turn.
        self.assertLess(context.estimate_tokens(messages), context.IMAGE_TOKENS * 2)


class VisionCapabilityTests(unittest.TestCase):
    def test_glm_53_is_text_only_and_glm_53_flash_reads_images(self):
        self.assertFalse(presets.model_supports_vision("glm-5.3"))
        self.assertTrue(presets.model_supports_vision("glm-5.3-flash"))
        self.assertFalse(presets.PRESETS["glm"].vision)
        self.assertFalse(presets.PRESETS["glm-coding"].vision)

    def test_openrouter_style_slugs_match_on_their_last_segment(self):
        self.assertTrue(presets.model_supports_vision("google/gemini-3.8-flash"))
        self.assertFalse(presets.model_supports_vision("deepseek/deepseek-v4.1-flash"))

    def test_presets_report_their_image_support(self):
        for preset_id in ("openai", "anthropic", "gemini"):
            self.assertTrue(presets.PRESETS[preset_id].to_dict()["vision"], preset_id)
        for preset_id in ("kimi", "kimi-code", "minimax", "openrouter"):
            self.assertFalse(presets.PRESETS[preset_id].to_dict()["vision"], preset_id)

    def test_the_vision_role_defaults_to_glm_flash_on_glm_and_to_nothing_elsewhere(self):
        self.assertEqual(resolver(GLM, "glm").vision_target().config.model, "glm-5.3-flash")
        self.assertIsNone(resolver(KIMI, "kimi").vision_target())

    def test_a_pinned_vision_model_wins_over_the_provider_default(self):
        roles = {"vision": {"preset": "openai", "model": "gpt-6-astra"}}
        self.assertEqual(resolver(GLM, "glm", roles).vision_target().config.model, "gpt-6-astra")


class ImageTurnTests(unittest.TestCase):
    """The whole path: an image on `ask`, the model it goes to, and what the history keeps."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.image = self.root / "shot.png"
        self.image.write_bytes(PNG)
        RecordingProvider.served = []
        RecordingProvider.raw = []
        RecordingProvider.windows = []
        RecordingProvider.agent = None
        self.addCleanup(setattr, RecordingProvider, "agent", None)
        patch = mock.patch.object(agent_module, "ChatProvider", RecordingProvider)
        patch.start()
        self.addCleanup(patch.stop)
        self.addCleanup(self.temp.cleanup)

    def build(self, config, preset_id, roles=None):
        self.events: list = []
        return Agent(config, self.temp.name, self.events.append, preset_id=preset_id,
                     roles=resolver(config, preset_id, roles), track_requests=False,
                     todo_tool=False, completion_check=False)

    def attachment(self):
        return [{"path": str(self.image), "kind": "image", "media_type": "image/png",
                 "raw": PNG, "bytes": len(PNG), "content": "", "truncated": False}]

    def kinds(self):
        return [event["event"] for event in self.events]

    def event(self, name):
        return next((e for e in self.events if e["event"] == name), None)

    # ----- a model that reads images -------------------------------------------------
    def test_an_image_reaches_a_vision_capable_model_as_a_content_part(self):
        agent = self.build(OPENAI, "openai")
        agent.ask("what is this?", attachments=self.attachment())
        model, messages = RecordingProvider.served[-1]
        self.assertEqual(model, "gpt-6-astra")
        content = messages[-1]["content"]
        self.assertIsInstance(content, list)
        self.assertEqual(content[1]["type"], "image_url")
        self.assertIn("what is this?", content[0]["text"])
        self.assertIn(str(self.image), content[0]["text"])   # the path is named for the model too
        self.assertNotIn("vision_route", self.kinds())        # nothing to say: no swap happened
        self.assertEqual(self.events[-1]["event"], "done")

    # ----- GLM: swap to Flash for the turn, then back --------------------------------
    def test_glm_swaps_to_glm_53_flash_for_an_image_turn_and_says_so(self):
        agent = self.build(GLM, "glm")
        agent.ask("what is this?", attachments=self.attachment())
        route = self.event("vision_route")
        self.assertIsNotNone(route)
        self.assertEqual(route["model"], "glm-5.3-flash")
        self.assertEqual(route["from_model"], "glm-5.3")
        self.assertEqual(route["scope"], "turn")
        self.assertIn("glm-5.3-flash", route["text"])
        self.assertEqual(RecordingProvider.served[-1][0], "glm-5.3-flash")

    def test_the_turn_after_an_image_is_back_on_the_main_model(self):
        agent = self.build(GLM, "glm")
        agent.ask("what is this?", attachments=self.attachment())
        ended = self.event("vision_route_ended")
        self.assertIsNotNone(ended)
        self.assertEqual(ended["model"], "glm-5.3")
        self.assertEqual(agent.provider.config.model, "glm-5.3")
        agent.ask("and now in words")
        self.assertEqual(RecordingProvider.served[-1][0], "glm-5.3")
        self.assertEqual([model for model, _ in RecordingProvider.served], ["glm-5.3-flash", "glm-5.3"])

    def test_the_main_model_comes_back_even_when_the_image_turn_fails(self):
        agent = self.build(GLM, "glm")
        with mock.patch.object(RecordingProvider, "complete", side_effect=RuntimeError("boom")):
            agent.ask("what is this?", attachments=self.attachment())
        self.assertEqual(self.events[-1]["event"], "error")
        self.assertIsNotNone(self.event("vision_route_ended"))
        self.assertEqual(agent.provider.config.model, "glm-5.3")

    # ----- no vision anywhere: refuse with a message ---------------------------------
    def test_a_preset_without_vision_and_no_vision_model_refuses_with_a_message(self):
        agent = self.build(KIMI, "kimi")
        agent.ask("what is this?", attachments=self.attachment())
        refusal = self.event("vision_unavailable")
        self.assertIsNotNone(refusal)
        self.assertIn("kimi-k3", refusal["text"])
        self.assertIn("Vision model", refusal["text"])
        error = self.events[-1]
        self.assertEqual(error["event"], "error")
        self.assertEqual(error["text"], refusal["text"])
        # Refused, not failed: nothing was sent to the provider at all.
        self.assertEqual(RecordingProvider.served, [])

    def test_a_refused_image_turn_leaves_no_image_in_the_conversation(self):
        agent = self.build(KIMI, "kimi")
        agent.ask("what is this?", attachments=self.attachment())
        self.assertFalse(transport.has_images(agent.messages))
        prompt = [m for m in agent.messages if m.get("relay_kind") == "prompt"][-1]
        self.assertIn("what is this?", prompt["content"])
        self.assertIsInstance(prompt["content"], str)

    # ----- the configured vision model -----------------------------------------------
    def test_a_configured_vision_model_takes_the_image_turn(self):
        agent = self.build(KIMI, "kimi", {"vision": {"preset": "openai", "model": "gpt-6-astra"}})
        agent.ask("what is this?", attachments=self.attachment())
        self.assertEqual(RecordingProvider.served[-1][0], "gpt-6-astra")
        self.assertEqual(self.event("vision_route")["model"], "gpt-6-astra")
        self.assertEqual(self.events[-1]["event"], "done")
        self.assertEqual(agent.provider.config.model, "kimi-k3")

    def test_a_vision_model_the_user_picked_wins_over_a_vision_capable_main_model(self):
        agent = self.build(OPENAI, "openai", {"vision": {"preset": "gemini", "model": "gemini-3.1-pro-preview"}})
        agent.ask("what is this?", attachments=self.attachment())
        self.assertEqual(RecordingProvider.served[-1][0], "gemini-3.1-pro-preview")

    # ----- a vision model whose provider is down (owner, 2026-09-19) -------------------
    def refusing(self, models):
        RefusingProvider.refuse = frozenset(models)
        self.addCleanup(setattr, RefusingProvider, "refuse", frozenset())
        patch = mock.patch.object(agent_module, "ChatProvider", RefusingProvider)
        patch.start()
        self.addCleanup(patch.stop)

    def test_a_vision_model_that_will_not_answer_hands_the_turn_back_to_the_pane(self):
        # Protocol 15.2.3. The pinned vision model refuses before streaming anything; the pane's own
        # model reads images too, so the turn finishes there instead of failing.
        self.refusing({"gemini-3.1-pro-preview"})
        agent = self.build(OPENAI, "openai",
                           {"vision": {"preset": "gemini", "model": "gemini-3.1-pro-preview"}})
        agent.ask("what is this?", attachments=self.attachment())
        self.assertEqual([model for model, _ in RecordingProvider.served],
                         ["gemini-3.1-pro-preview", "gpt-6-astra"])
        self.assertEqual(self.events[-1]["event"], "done")
        dropped = self.event("provider_retry")
        self.assertEqual(dropped["reason"], "route_dropped")
        self.assertIn("Vision model", dropped["text"])
        self.assertIn("is not answering", dropped["text"])
        self.assertIn("gpt-6-astra", dropped["text"])
        # The routing is over before the note claims the pane has its own model back.
        self.assertLess(self.kinds().index("vision_route_ended"),
                        self.kinds().index("provider_retry"))
        self.assertEqual(self.kinds().count("vision_route_ended"), 1)
        self.assertEqual(agent.provider.config.model, "gpt-6-astra")
        self.assertIsNone(agent._vision)

    def test_a_pane_that_cannot_read_images_keeps_the_vision_failure(self):
        # Dropping back would only hand the pictures to a model that refuses them, which is why the
        # turn was routed at all: the vision provider's failure is reported as before.
        self.refusing({"gpt-6-astra"})
        agent = self.build(KIMI, "kimi", {"vision": {"preset": "openai", "model": "gpt-6-astra"}})
        agent.ask("what is this?", attachments=self.attachment())
        self.assertEqual([model for model, _ in RecordingProvider.served], ["gpt-6-astra"])
        self.assertEqual(self.events[-1]["event"], "error")
        self.assertIn("503", self.events[-1]["text"])
        self.assertIsNone(self.event("provider_retry"))
        self.assertIsNotNone(self.event("vision_route_ended"))

    # ----- a vision model on another vendor gets the history and the window it needs ---
    def test_a_vision_model_on_another_vendor_gets_the_history_in_its_own_dialect(self):
        # The swap used to replace the provider and nothing else, so the conversation went to the
        # vision model in the pane's reasoning dialect and the turn was measured against the pane's
        # context window. Kimi refuses an assistant tool-call message with no `reasoning_content`;
        # an OpenAI pane never writes one, so its appearance is the swap doing the conversion.
        agent = self.build(OPENAI, "openai", {"vision": {"preset": "kimi", "model": "kimi-k3"}})
        RecordingProvider.agent = agent
        agent.messages.append({"role": "assistant", "content": "",
                               "tool_calls": [{"id": "c0", "type": "function",
                                               "function": {"name": "read_file", "arguments": "{}"}}]})
        agent.messages.append({"role": "tool", "tool_call_id": "c0", "content": "ok"})
        agent.ask("what is this?", attachments=self.attachment())
        self.assertEqual(RecordingProvider.served[-1][0], "kimi-k3")
        adapted = [m for m in RecordingProvider.raw[-1] if m.get("tool_calls")]
        self.assertTrue(adapted and adapted[0].get("reasoning_content"))
        self.assertEqual(RecordingProvider.windows[-1], presets.PRESETS["kimi"].context_window)
        # ... and the pane is measured against its own again the moment the turn is over.
        self.assertEqual(agent.context.window, presets.PRESETS["openai"].context_window)
        self.assertEqual(agent.config.model, "gpt-6-astra")
        self.assertEqual(agent.provider.config.model, "gpt-6-astra")

    def test_a_save_while_an_image_turn_is_routed_records_the_panes_own_model(self):
        # The session file, the resume picker and the index read this: a mid-turn autosave must
        # never say the conversation is on the vision model.
        agent = self.build(KIMI, "kimi", {"vision": {"preset": "openai", "model": "gpt-6-astra"}})
        seen = []
        original = RecordingProvider.complete

        def complete(provider, messages, tools, emit, cancel):
            seen.append((agent.session_data()["model"], agent.session_data()["preset"]))
            return original(provider, messages, tools, emit, cancel)

        with mock.patch.object(RecordingProvider, "complete", complete):
            agent.ask("what is this?", attachments=self.attachment())
        self.assertEqual(RecordingProvider.served[-1][0], "gpt-6-astra")
        self.assertEqual(seen, [("kimi-k3", "kimi")])
        self.assertEqual(agent.session_data()["model"], "kimi-k3")

    def test_the_route_and_the_return_name_the_preset_on_the_note_and_on_the_status(self):
        # One shape for vision, plan and failover (protocol 15.2.2): both ends of both notes name
        # the preset as well as the model, and the return says so on the status line too, which an
        # image turn did not emit at all.
        agent = self.build(OPENAI, "openai", {"vision": {"preset": "kimi", "model": "kimi-k3"}})
        agent.ask("what is this?", attachments=self.attachment())
        kimi = presets.PRESETS["kimi"].label
        openai = presets.PRESETS["openai"].label
        route = self.event("vision_route")
        self.assertEqual(route["text"],
                         f"Image in this prompt · this turn runs on kimi-k3 ({kimi}), "
                         f"then back to gpt-6-astra ({openai}).")
        self.assertEqual((route["preset"], route["from_preset"]), ("kimi", "openai"))
        ended = self.event("vision_route_ended")
        self.assertEqual(ended["text"], f"Back to gpt-6-astra ({openai}).")
        self.assertEqual((ended["preset"], ended["was_preset"]), ("openai", "kimi"))
        statuses = [e["text"] for e in self.events if e["event"] == "status"]
        self.assertIn(f"Image turn · kimi-k3 ({kimi})", statuses)
        self.assertIn(f"Back to gpt-6-astra ({openai})", statuses)

    # ----- images live for one turn ---------------------------------------------------
    def test_the_image_is_replaced_by_a_description_and_its_path_after_the_turn(self):
        agent = self.build(GLM, "glm")
        agent.ask("what is this?", attachments=self.attachment())
        self.assertFalse(transport.has_images(agent.messages))
        prompt = [m for m in agent.messages if m.get("relay_kind") == "prompt"][-1]
        self.assertIsInstance(prompt["content"], str)
        self.assertIn(str(self.image), prompt["content"])
        self.assertIn("since removed", prompt["content"])
        self.assertIn("what is this?", prompt["content"])
        self.assertNotIn("relay_images", prompt)
        self.assertNotIn(base64.b64encode(PNG).decode("ascii")[:16], prompt["content"])

    def test_a_second_turn_no_longer_carries_the_first_turns_image(self):
        agent = self.build(GLM, "glm")
        agent.ask("what is this?", attachments=self.attachment())
        agent.ask("say more")
        _, messages = RecordingProvider.served[-1]
        self.assertFalse(transport.has_images(messages))
        self.assertTrue(any(str(self.image) in str(m.get("content")) for m in messages))

    def test_the_image_is_still_there_while_its_own_turn_runs(self):
        agent = self.build(GLM, "glm")
        agent.ask("what is this?", attachments=self.attachment())
        _, messages = RecordingProvider.served[0]
        self.assertTrue(transport.has_images(messages))


if __name__ == "__main__":
    unittest.main()
