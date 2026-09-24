# SPDX-License-Identifier: AGPL-3.0-or-later
"""Paid media requests are mocked; these tests assert the wire and file boundaries."""
import base64
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from relay_core import media
from relay_core.tools import Workspace


PNG = b"\x89PNG\r\n\x1a\n" + b"image"
MP3 = b"ID3" + b"audio"
MP4 = b"\x00\x00\x00\x18ftypmp42" + b"video"
VEO = {"id": "google/veo-3.1-lite", "supported_durations": [4, 6, 8],
       "supported_resolutions": ["720p", "1080p"], "supported_aspect_ratios": ["16:9", "9:16"],
       "pricing_skus": {"duration_seconds_without_audio_720p": "0.03",
                        "duration_seconds_with_audio_720p": "0.05"}}


class MediaTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        profile = mock.patch.dict("os.environ", {"XDG_DATA_HOME": self.temp.name})
        profile.start()
        self.addCleanup(profile.stop)
        keys = mock.patch.object(media.keystore, "lookup", side_effect=lambda key: "test-key")
        keys.start()
        self.addCleanup(keys.stop)
        self.service = media.MediaTools(Workspace(self.temp.name))
        self.service._catalog = {VEO["id"]: VEO}
        self.service._catalog_at = media.time.time()
        self.service._images = {model: {"id": model, "supported_parameters": {}}
                                for model in media.IMAGE_MODELS}
        self.service._images_at = media.time.time()
        self.service._image_endpoints[media.IMAGE_DEFAULT] = (media.time.time(), [])

    def test_veo_quote_and_restart_download(self):
        quoted = self.service.quote({"kind": "video", "prompt": "clouds", "duration": 8})
        self.assertEqual(quoted["estimated_usd"], 0.24)
        with mock.patch.object(media, "_json_request", return_value={"id": "video_1"}) as post:
            submitted = self.service.generate({"quote_id": quoted["quote_id"]})
        self.assertEqual(post.call_args.kwargs["payload"]["generate_audio"], False)
        self.assertEqual(post.call_args.kwargs["payload"]["resolution"], "720p")
        restarted = media.MediaTools(Workspace(self.temp.name))
        with mock.patch.object(media, "_json_request", return_value={"status": "completed", "usage": {"cost": 0.25}}), \
             mock.patch.object(media, "_bytes_request", return_value=(MP4, "video/mp4")):
            result = restarted.job({"job_id": submitted["job_id"]})
        self.assertEqual(Path(result["path"]).read_bytes(), MP4)
        self.assertEqual(result["actual_usd"], 0.25)
        self.assertEqual(restarted.job({"job_id": submitted["job_id"]})["path"], result["path"])

    def test_direct_music_uses_music_endpoint(self):
        quoted = self.service.quote({"kind": "music", "provider": "elevenlabs", "prompt": "piano", "duration": 8})
        with mock.patch.object(media, "_bytes_request", return_value=(MP3, "audio/mpeg")) as request:
            result = self.service.generate({"quote_id": quoted["quote_id"]})
        self.assertTrue(request.call_args.args[0].endswith("/music"))
        self.assertEqual(request.call_args.kwargs["payload"]["music_length_ms"], 8000)
        self.assertEqual(Path(result["path"]).read_bytes(), MP3)

    def test_seedance_quote_uses_video_tokens(self):
        self.service._catalog[media.VIDEO_MODELS[0]] = {
            "id": media.VIDEO_MODELS[0], "supported_durations": list(range(4, 16)),
            "supported_resolutions": ["720p"], "supported_aspect_ratios": ["16:9"],
            "supported_sizes": ["1280x720"], "pricing_skus": {"video_tokens_without_audio": "0.000007"}}
        quoted = self.service.quote({"kind": "video", "model": media.VIDEO_MODELS[0],
                                     "prompt": "clouds", "duration": 8})
        self.assertEqual(quoted["estimated_usd"], 1.2096)
        self.assertIn("approximate", quoted["price_source"])

    def test_lyria_and_image_decode(self):
        for kind, encoded, payload in (
            ("music", MP3, {"choices": [{"message": {"audio": {"data": base64.b64encode(MP3).decode()}}}]}),
            ("image", PNG, {"data": [{"b64_json": base64.b64encode(PNG).decode()}]}),
        ):
            quoted = self.service.quote({"kind": kind, "prompt": "test", "provider": "openrouter"})
            with mock.patch.object(media, "_json_request", return_value=payload):
                result = self.service.generate({"quote_id": quoted["quote_id"]})
            self.assertEqual(Path(result["path"]).read_bytes(), encoded)

    def test_fal_sfx_job_and_key_fallback(self):
        quoted = self.service.quote({"kind": "sfx", "prompt": "rain", "loop": True, "duration": 2})
        with mock.patch.object(media, "_json_request", return_value={"request_id": "fal_1"}) as post:
            submitted = self.service.generate({"quote_id": quoted["quote_id"]})
        self.assertTrue(post.call_args.kwargs["payload"]["loop"])
        with mock.patch.object(media, "_json_request", side_effect=[
            {"status": "COMPLETED"}, {"audio": {"url": "https://storage.googleapis.com/a/sound.mp3"}}]), \
             mock.patch.object(media, "_bytes_request", return_value=(MP3, "audio/mpeg")):
            result = self.service.job({"job_id": submitted["job_id"]})
        self.assertEqual(Path(result["path"]).read_bytes(), MP3)
        with mock.patch.object(media.keystore, "lookup", side_effect=lambda name: "test-key" if name == "openrouter" else None):
            self.assertEqual(self.service.quote({"kind": "music", "prompt": "piano"})["provider"], "openrouter")
            with self.assertRaisesRegex(media.MediaError, "fal.ai or ElevenLabs key"):
                self.service.quote({"kind": "sfx", "prompt": "rain"})

    def test_validation_and_no_artifact_on_bad_provider_data(self):
        with self.assertRaisesRegex(media.MediaError, "unsafe media URL|unexpected media host"):
            media._file_url("https://example.com/file.mp3")
        with self.assertRaisesRegex(media.MediaError, "0.5–22"):
            self.service.quote({"kind": "sfx", "prompt": "rain", "duration": 30})
        with self.assertRaisesRegex(media.MediaError, "supports 4, 6 or 8"):
            self.service.quote({"kind": "video", "prompt": "clouds", "duration": 9})
        quoted = self.service.quote({"kind": "image", "prompt": "test", "output_path": "media/test"})
        with mock.patch.object(media, "_json_request", return_value={"data": [{"b64_json": base64.b64encode(MP3).decode()}]}):
            with self.assertRaisesRegex(media.MediaError, "unrecognized media format"):
                self.service.generate({"quote_id": quoted["quote_id"]})
        self.assertFalse((Path(self.temp.name) / "media/test.png").exists())
        with self.assertRaisesRegex(media.MediaError, "unknown or expired"):
            self.service.generate({"quote_id": quoted["quote_id"]})


RELAY_FREE_ROLE = {"role": "relay-image", "model": "black-forest-labs/flux.2-klein-4b",
                   "price_usd": 0.003, "resolutions": ["1024x1024", "1536x1024"],
                   "aspect_ratios": ["1:1", "3:2"]}


class FakeHostedSession:
    """The slice of the hosted session media.py reads, with no network: the cached image
    quota, and either a picture or a refusal from ``POST /images``."""

    def __init__(self, *, image=None, reply=None, error=None):
        self.image_quota_value = image
        self.reply, self.error = reply, error
        self.payloads = []

    def quota(self):
        return {"limit": 250000, "used": 1200, "resets_at": 452257800}

    def image_quota(self):
        return self.image_quota_value

    def image(self, payload):
        self.payloads.append(payload)
        if self.error is not None:
            raise media.hosted.HostedUnavailable(self.error[0], self.error[1])
        return self.reply


class RelayFreeImageTests(unittest.TestCase):
    """A keyless install makes a picture on Relay Free's gateway; an install with its own
    OpenRouter key never passes through it."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        profile = mock.patch.dict("os.environ", {"XDG_DATA_HOME": self.temp.name})
        profile.start()
        self.addCleanup(profile.stop)
        keyless = mock.patch.object(media.keystore, "lookup",
                                    side_effect=lambda key: "test-key" if key == "fal" else None)
        keyless.start()
        self.addCleanup(keyless.stop)
        self.session = FakeHostedSession(
            image={"limit": 10, "used": 3, "resets_at": 452257800},
            reply={"created": 1, "data": [{"b64_json": base64.b64encode(PNG).decode()}],
                   "usage": {"cost": 0.003}})
        for patch in (mock.patch.object(media.hosted, "available", return_value=True),
                      mock.patch.object(media.hosted, "image_roles",
                                        return_value=[RELAY_FREE_ROLE]),
                      mock.patch.object(media.hosted, "session", return_value=self.session)):
            patch.start()
            self.addCleanup(patch.stop)
        self.service = media.MediaTools(Workspace(self.temp.name))

    def test_keyless_image_quotes_and_generates_on_relay_free(self):
        events = []
        self.service.emit = events.append
        quoted = self.service.quote({"kind": "image", "prompt": "a lighthouse",
                                     "output_path": "art"})
        self.assertEqual((quoted["provider"], quoted["model"]), ("relay-free", "relay-image"))
        self.assertEqual(quoted["estimated_usd"], 0.003)
        self.assertIn("7 remaining", quoted["price_source"])
        result = self.service.generate({"quote_id": quoted["quote_id"]})
        self.assertEqual(Path(result["path"]).read_bytes(), PNG)
        self.assertEqual(result["provider"], "relay-free")
        self.assertEqual(self.session.payloads,
                         [{"model": "relay-image", "prompt": "a lighthouse"}])
        # The image just spent is announced for the chip, with both counts.
        self.assertEqual(events, [{"event": "hosted_quota", "limit": 250000, "used": 1200,
                                   "resets_at": 452257800, "image_limit": 10, "image_used": 3,
                                   "image_resets_at": 452257800}])

    def test_an_own_openrouter_key_never_uses_the_gateway(self):
        with mock.patch.object(media.keystore, "lookup", return_value="test-key"):
            quoted = self.service.quote({"kind": "image", "prompt": "a lighthouse"})
        self.assertEqual(quoted["provider"], "openrouter")
        self.assertEqual(self.session.payloads, [])

    def test_relay_free_is_named_and_its_settings_checked(self):
        quoted = self.service.quote({"kind": "image", "provider": "relay-free",
                                     "prompt": "a lighthouse", "resolution": "1536x1024"})
        self.assertEqual(quoted["provider"], "relay-free")
        result = self.service.generate({"quote_id": quoted["quote_id"]})
        self.assertEqual(self.session.payloads[-1]["resolution"], "1536x1024")
        with self.assertRaisesRegex(media.MediaError, "not supported by Relay Free"):
            self.service.quote({"kind": "image", "provider": "relay-free", "prompt": "x",
                                "resolution": "999x999"})
        with mock.patch.object(media.hosted, "image_roles", return_value=[]):
            with self.assertRaisesRegex(media.MediaError, "needs a openrouter key"):
                self.service.quote({"kind": "image", "prompt": "x"})
            with self.assertRaisesRegex(media.MediaError, "not available here"):
                self.service.quote({"kind": "image", "provider": "relay-free", "prompt": "x"})

    def test_exhaustion_words_the_persons_own_providers(self):
        self.session.error = ("Relay Free images used for today.", "quota_exhausted")
        quoted = self.service.quote({"kind": "image", "prompt": "a lighthouse"})
        with self.assertRaisesRegex(media.MediaError,
                                    "daily images are used up.*OpenRouter key"):
            self.service.generate({"quote_id": quoted["quote_id"]})

    def test_catalog_offers_relay_free_with_its_allowance(self):
        info = self.service.catalog()["image"]["relay_free"]
        self.assertEqual(info["available"], True)
        self.assertEqual(info["role"], "relay-image")
        self.assertEqual(info["model"], "black-forest-labs/flux.2-klein-4b")
        self.assertEqual(info["price_usd"], 0.003)
        self.assertEqual((info["images_limit"], info["images_left_today"]), (10, 7))


if __name__ == "__main__":
    unittest.main()
