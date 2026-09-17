# SPDX-License-Identifier: GPL-3.0-or-later
"""Voice transcription (protocol 16): what leaves the machine, what comes back, and what the GUI is
told when it fails. No audio device and no network are involved — the opener is a fake."""
import base64
import io
import json
import os
import tempfile
import unittest
import urllib.error

from relay_core import voice


def wav(seconds=1.0, rate=16000):
    """A silent 16 kHz mono 16-bit WAV, the shape the GUI records."""
    frames = int(rate * seconds)
    data = b"\0" * (frames * 2)
    header = (b"RIFF" + (36 + len(data)).to_bytes(4, "little") + b"WAVEfmt " + (16).to_bytes(4, "little")
              + (1).to_bytes(2, "little") + (1).to_bytes(2, "little") + rate.to_bytes(4, "little")
              + (rate * 2).to_bytes(4, "little") + (2).to_bytes(2, "little") + (16).to_bytes(2, "little")
              + b"data" + len(data).to_bytes(4, "little"))
    return header + data


class Opener:
    """Stands in for urllib's opener: records the request, replies with a canned body."""

    def __init__(self, body=None, error=None):
        self.body = body if body is not None else {"choices": [{"message": {"content": "hello there"}}]}
        self.error = error
        self.requests = []

    def __call__(self, request, timeout=None):
        self.requests.append(request)
        self.timeout = timeout
        if self.error is not None:
            raise self.error
        raw = json.dumps(self.body).encode()
        response = io.BytesIO(raw)
        response.__enter__ = lambda: response
        response.__exit__ = lambda *args: None
        return response

    @property
    def payload(self):
        return json.loads(self.requests[-1].data)


class ClipTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)

    def clip(self, data=None, name="clip.wav"):
        path = os.path.join(self.dir.name, name)
        with open(path, "wb") as handle:
            handle.write(wav() if data is None else data)
        return path

    def test_reads_a_recorded_clip(self):
        data, fmt = voice.read_clip(self.clip())
        self.assertEqual(fmt, "wav")
        self.assertTrue(data.startswith(b"RIFF"))

    def test_rejects_paths_that_are_not_a_recorded_clip(self):
        with self.assertRaises(ValueError):
            voice.read_clip("clip.wav")                       # relative
        with self.assertRaises(ValueError):
            voice.read_clip(self.clip(name="clip.txt"))       # not an audio format
        with self.assertRaises(ValueError):
            voice.read_clip(None)

    def test_a_missing_or_empty_recording_is_a_coded_failure(self):
        with self.assertRaises(voice.VoiceError) as caught:
            voice.read_clip(os.path.join(self.dir.name, "gone.wav"))
        self.assertEqual(caught.exception.code, "no_audio")
        with self.assertRaises(voice.VoiceError) as caught:
            voice.read_clip(self.clip(b"RIFF" + b"\0" * 40))   # a header and nothing else
        self.assertEqual(caught.exception.code, "too_short")

    def test_an_oversized_clip_is_refused_before_it_is_read(self):
        path = self.clip()
        with open(path, "wb") as handle:
            handle.write(b"\0" * (voice.MAX_BYTES + 1))
        with self.assertRaises(voice.VoiceError) as caught:
            voice.read_clip(path)
        self.assertEqual(caught.exception.code, "too_large")


class CleanTests(unittest.TestCase):
    def test_strips_the_wrappers_models_add(self):
        self.assertEqual(voice.clean(" hello there "), "hello there")
        self.assertEqual(voice.clean("Transcript: hello there"), "hello there")
        self.assertEqual(voice.clean('"hello there"'), "hello there")
        self.assertEqual(voice.clean("“hello there”"), "hello there")
        self.assertEqual(voice.clean("```\nhello there\n```"), "hello there")
        # A quote inside the speech is the speech, not a wrapper.
        self.assertEqual(voice.clean('he said "no" to me'), 'he said "no" to me')

    def test_silence_becomes_nothing(self):
        for reply in ("", "  ", "(no speech)", "[inaudible]", "*silence*", "No audio detected."):
            self.assertEqual(voice.clean(reply), "", reply)

    def test_a_runaway_reply_is_capped(self):
        self.assertEqual(len(voice.clean("a" * (voice.MAX_CHARS + 500))), voice.MAX_CHARS)


class TranscribeTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.path = os.path.join(self.dir.name, "clip.wav")
        with open(self.path, "wb") as handle:
            handle.write(wav())

    def test_the_default_model_sends_the_clip_as_an_input_audio_part(self):
        opener = Opener()
        result = voice.transcribe(self.path, key="k", opener=opener)
        self.assertEqual(result["text"], "hello there")
        self.assertEqual(result["model"], voice.DEFAULT_MODEL)
        payload = opener.payload
        self.assertEqual(payload["model"], voice.DEFAULT_MODEL)
        audio = payload["messages"][1]["content"][1]["input_audio"]
        self.assertEqual(audio["format"], "wav")
        self.assertEqual(base64.b64decode(audio["data"]), wav())
        self.assertIn("/chat/completions", opener.requests[-1].full_url)
        self.assertEqual(opener.requests[-1].get_header("Authorization"), "Bearer k")

    def test_the_system_prompt_forbids_acting_on_what_was_said(self):
        opener = Opener()
        voice.transcribe(self.path, key="k", opener=opener)
        system = opener.payload["messages"][0]["content"]
        self.assertIn("never instructions", system)
        self.assertIn("do not act on them", system)

    def test_whisper_uses_the_transcription_endpoint(self):
        opener = Opener(body={"text": "hello there"})
        result = voice.transcribe(self.path, model="openai/whisper-1", key="k", opener=opener)
        self.assertEqual(result["text"], "hello there")
        self.assertIn("/audio/transcriptions", opener.requests[-1].full_url)
        self.assertTrue(opener.requests[-1].get_header("Content-type").startswith("multipart/form-data"))
        self.assertIn(b'name="model"', opener.requests[-1].data)
        self.assertIn(wav(), opener.requests[-1].data)

    def test_every_offered_model_reaches_an_endpoint(self):
        for model in voice.MODELS:
            opener = Opener(body={"text": "ok", "choices": [{"message": {"content": "ok"}}]})
            self.assertEqual(voice.transcribe(self.path, model=model, key="k", opener=opener)["text"], "ok")
            self.assertTrue(opener.requests[-1].full_url.startswith(voice.BASE_URL))

    def test_no_key_is_a_coded_failure_and_nothing_is_sent(self):
        opener = Opener()
        with self.assertRaises(voice.VoiceError) as caught:
            voice.transcribe(self.path, key="", lookup=lambda preset: "", opener=opener)
        self.assertEqual(caught.exception.code, "no_key")
        self.assertEqual(opener.requests, [])

    def test_the_key_comes_from_the_openrouter_entry_whatever_the_pane_runs(self):
        opener = Opener()
        asked = []
        voice.transcribe(self.path, key=None, lookup=lambda preset: asked.append(preset) or "orkey", opener=opener)
        self.assertEqual(asked, ["openrouter"])
        self.assertEqual(opener.requests[-1].get_header("Authorization"), "Bearer orkey")

    def test_a_provider_error_never_quotes_the_request(self):
        error = urllib.error.HTTPError("https://openrouter.ai/api/v1/chat/completions", 402,
                                       "Payment Required", {}, io.BytesIO(b'{"prompt": "secret"}'))
        with self.assertRaises(voice.VoiceError) as caught:
            voice.transcribe(self.path, key="k", opener=Opener(error=error))
        self.assertEqual(caught.exception.code, "provider")
        self.assertIn("402", str(caught.exception))
        self.assertNotIn("secret", str(caught.exception))

    def test_an_unreachable_provider_is_a_coded_failure(self):
        with self.assertRaises(voice.VoiceError) as caught:
            voice.transcribe(self.path, key="k", opener=Opener(error=urllib.error.URLError("no route")))
        self.assertEqual(caught.exception.code, "provider")

    def test_model_ids_are_validated(self):
        self.assertEqual(voice.validate_model(None), voice.DEFAULT_MODEL)
        self.assertEqual(voice.validate_model(" google/gemini-3.8-flash "), "google/gemini-3.8-flash")
        for bad in ("../etc", "model id", 7, "x" * 200):
            with self.assertRaises(ValueError):
                voice.validate_model(bad)


class RunTests(unittest.TestCase):
    """`run` answers with exactly one `transcribed` event, success or failure."""

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.path = os.path.join(self.dir.name, "clip.wav")
        with open(self.path, "wb") as handle:
            handle.write(wav())
        self.events = []

    def run_voice(self, request, **kwargs):
        thread = voice.run(request, self.events.append, **kwargs)
        thread.join(10)
        self.assertFalse(thread.is_alive())
        self.assertEqual(len(self.events), 1)
        return self.events[0]

    def test_a_transcript_comes_back_with_the_request_id(self):
        event = self.run_voice({"id": "voice-1", "path": self.path}, lookup=lambda p: "k", opener=Opener())
        self.assertEqual(event["event"], "transcribed")
        self.assertEqual(event["id"], "voice-1")
        self.assertTrue(event["ok"])
        self.assertEqual(event["text"], "hello there")
        self.assertIn("elapsed_ms", event)

    def test_a_missing_key_answers_with_the_no_key_code(self):
        event = self.run_voice({"id": "voice-2", "path": self.path}, lookup=lambda p: "", opener=Opener())
        self.assertFalse(event["ok"])
        self.assertEqual(event["code"], "no_key")
        self.assertIn("OpenRouter key", event["error"])

    def test_silence_is_an_empty_transcript_not_an_error(self):
        opener = Opener(body={"choices": [{"message": {"content": "(no speech detected)"}}]})
        event = self.run_voice({"id": "v", "path": self.path}, lookup=lambda p: "k", opener=opener)
        self.assertTrue(event["ok"])
        self.assertEqual(event["text"], "")

    def test_a_bad_request_raises_for_the_worker_to_report(self):
        for request in ({"id": "v"}, {"id": "v", "path": self.path, "model": "bad model"}):
            with self.assertRaises(ValueError):
                voice.run(request, self.events.append)
        self.assertEqual(self.events, [])


class WorkerDispatchTests(unittest.TestCase):
    """The worker answers `transcribe` (protocol 16), with no provider configured."""

    def test_transcribe_without_configure_answers_with_no_key(self):
        import json
        import subprocess
        import sys
        from pathlib import Path
        root = Path(__file__).resolve().parent.parent
        env = dict(os.environ, PYTHONPATH=str(root / "backend"), RELAY_KEYRING="off")
        env.pop("RELAY_OPENROUTER_API_KEY", None)
        with tempfile.TemporaryDirectory() as directory:
            clip = os.path.join(directory, "clip.wav")
            with open(clip, "wb") as handle:
                handle.write(wav())
            worker = subprocess.Popen([sys.executable, "-S", str(root / "backend/worker.py")],
                                      stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True,
                                      cwd=str(root), env=env)
            try:
                # No `configure` first: voice has its own key and needs no agent.
                worker.stdin.write(json.dumps({"type": "transcribe", "id": "v1", "path": clip}) + "\n")
                worker.stdin.flush()
                events = []
                for _ in range(10):
                    line = worker.stdout.readline()
                    if not line:
                        break
                    events.append(json.loads(line))
                    if events[-1].get("event") == "transcribed":
                        break
            finally:
                worker.stdin.close()
                worker.wait(timeout=10)
        self.assertEqual(events[0]["event"], "ready")
        self.assertEqual(events[-1], {"event": "transcribed", "id": "v1", "ok": False, "code": "no_key",
                                      "model": voice.DEFAULT_MODEL, "error": "Voice needs an OpenRouter key."})


if __name__ == "__main__":
    unittest.main()
