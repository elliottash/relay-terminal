# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generated media tools. Provider bytes stay off the worker's JSON protocol."""
from __future__ import annotations

import base64
import binascii
import hashlib
import json
import math
import os
import re
import secrets
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

from . import keystore
from .tools import Workspace

OPENROUTER = "https://openrouter.ai/api/v1"
FAL = "https://queue.fal.run"
ELEVENLABS = "https://api.elevenlabs.io/v1"
VIDEO_MODELS = ("bytedance/seedance-2.0", "google/veo-3.1-lite")
LYRIA_MODELS = ("google/lyria-3-clip-preview", "google/lyria-3-pro-preview")
IMAGE_DEFAULT = "black-forest-labs/flux.2-klein-4b"
IMAGE_MODELS = (IMAGE_DEFAULT, "google/gemini-3.1-flash-lite-image")
FAL_SFX = "fal-ai/elevenlabs/sound-effects/v2"
FAL_MUSIC = "elevenlabs/music/v2.5"
MAX_JSON = 32 * 1024 * 1024
MAX_FILE = 150 * 1024 * 1024
QUOTE_SECONDS = 600
_ID = re.compile(r"^[A-Za-z0-9_-]{1,128}$")


def _spec(name, description, properties, required=()):
    return {"type": "function", "function": {"name": name, "description": description,
        "parameters": {"type": "object", "properties": properties,
                       "required": list(required), "additionalProperties": False}}}


TOOL_SPECS = [
    _spec("media_catalog", "List media generation choices, key availability and current OpenRouter video capabilities. Does not spend money.",
          {}),
    _spec("media_quote", "Validate and quote one media generation. Call before media_generate; show its estimate to the user. The quote expires after ten minutes.",
          {"kind": {"type": "string", "enum": ["image", "music", "sfx", "video"]},
           "prompt": {"type": "string"}, "model": {"type": "string"},
           "provider": {"type": "string", "enum": ["openrouter", "fal", "elevenlabs"]},
           "duration": {"type": "number"}, "resolution": {"type": "string"},
           "aspect_ratio": {"type": "string"}, "generate_audio": {"type": "boolean"},
           "loop": {"type": "boolean"}, "prompt_influence": {"type": "number"},
           "output_path": {"type": "string"}}, ("kind", "prompt")),
    _spec("media_generate", "Spend the quoted amount to generate media. Returns a local file, or a durable job ID to pass to media_job. Never retry an uncertain paid submission automatically.",
          {"quote_id": {"type": "string"}}, ("quote_id",)),
    _spec("media_job", "Check a submitted video or fal audio job; download the result when ready. Jobs survive worker restarts.",
          {"job_id": {"type": "string"}}, ("job_id",)),
]
TOOL_NAMES = frozenset(s["function"]["name"] for s in TOOL_SPECS)


class MediaError(ValueError):
    pass


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, msg, headers, newurl):
        return None


def _json_request(url, *, key="", auth="Bearer", payload=None, limit=MAX_JSON):
    headers = {"Accept": "application/json"}
    if key:
        headers["Authorization"] = f"{auth} {key}"
    data = None if payload is None else json.dumps(payload).encode("utf-8")
    if data is not None:
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=headers,
                                 method="POST" if data is not None else "GET")
    try:
        with urllib.request.build_opener(_NoRedirect).open(req, timeout=45) as response:
            raw = response.read(limit + 1)
    except urllib.error.HTTPError as exc:
        raise MediaError(f"Media provider returned HTTP {exc.code}.") from None
    except (urllib.error.URLError, TimeoutError) as exc:
        raise MediaError(f"Media provider request failed ({type(exc).__name__}).") from None
    if len(raw) > limit:
        raise MediaError("Media provider response is too large.")
    try:
        value = json.loads(raw)
    except (UnicodeError, json.JSONDecodeError):
        raise MediaError("Media provider returned invalid JSON.") from None
    if not isinstance(value, dict):
        raise MediaError("Media provider returned an unexpected response.")
    return value


def _bytes_request(url, *, key="", auth="Bearer", payload=None, headers=None):
    hdrs = {"Authorization": f"{auth} {key}"} if key else {}
    hdrs.update(headers or {})
    data = None if payload is None else json.dumps(payload).encode("utf-8")
    if data is not None:
        hdrs["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=hdrs,
                                 method="POST" if data is not None else "GET")
    try:
        with urllib.request.build_opener(_NoRedirect).open(req, timeout=120) as response:
            raw = response.read(MAX_FILE + 1)
            media_type = response.headers.get("Content-Type", "").split(";", 1)[0]
    except urllib.error.HTTPError as exc:
        if exc.code in (301, 302, 303, 307, 308) and payload is None:
            location = exc.headers.get("Location", "")
            if location:
                destination = _file_url(urllib.parse.urljoin(url, location))
                if destination == url:
                    raise MediaError("Provider media redirect loop.")
                return _bytes_request(destination)
        raise MediaError(f"Media provider returned HTTP {exc.code}.") from None
    except (urllib.error.URLError, TimeoutError) as exc:
        raise MediaError(f"Media provider request failed ({type(exc).__name__}).") from None
    if len(raw) > MAX_FILE:
        raise MediaError("Generated media exceeds the 150 MB limit.")
    return raw, media_type


def _file_url(url):
    parsed = urllib.parse.urlsplit(url)
    host = (parsed.hostname or "").lower()
    if parsed.scheme != "https" or parsed.username or parsed.password or parsed.port not in (None, 443):
        raise MediaError("Provider returned an unsafe media URL.")
    if not (host == "storage.googleapis.com" or host == "fal.media" or host.endswith(".fal.media")):
        raise MediaError("Provider returned an unexpected media host.")
    return url


def _b64(value):
    if not isinstance(value, str) or len(value) > MAX_FILE * 2:
        raise MediaError("Provider returned invalid media bytes.")
    try:
        raw = base64.b64decode(value, validate=True)
    except (ValueError, binascii.Error):
        raise MediaError("Provider returned invalid base64 media.") from None
    if not raw or len(raw) > MAX_FILE:
        raise MediaError("Provider returned empty or oversized media.")
    return raw


def _extension(kind, raw):
    if kind == "image" and raw.startswith(b"\x89PNG\r\n\x1a\n"):
        return ".png"
    if kind == "image" and raw.startswith(b"\xff\xd8\xff"):
        return ".jpg"
    if kind == "image" and raw.startswith(b"RIFF") and raw[8:12] == b"WEBP":
        return ".webp"
    if kind in ("music", "sfx") and raw.startswith(b"RIFF") and raw[8:12] == b"WAVE":
        return ".wav"
    if kind in ("music", "sfx") and (raw.startswith(b"ID3") or raw[:2] in (b"\xff\xfb", b"\xff\xf3", b"\xff\xf2")):
        return ".mp3"
    if kind in ("music", "sfx") and raw.startswith(b"OggS"):
        return ".ogg"
    if kind == "video" and len(raw) > 12 and raw[4:8] == b"ftyp":
        return ".mp4"
    raise MediaError("Provider returned an unrecognized media format.")


def _audio_from_completion(reply):
    choices = reply.get("choices") or []
    message = choices[0].get("message", {}) if choices and isinstance(choices[0], dict) else {}
    values = [message.get("audio")]
    content = message.get("content")
    if isinstance(content, list):
        values += content
    for item in values:
        if not isinstance(item, dict):
            continue
        candidate = item.get("data") or item.get("b64_json")
        if not candidate and isinstance(item.get("audio"), dict):
            candidate = item["audio"].get("data")
        if candidate:
            return _b64(candidate)
    raise MediaError("Lyria returned no audio data; no file was saved.")


def _settings(choice):
    return {key: choice[key] for key in ("duration", "resolution", "aspect_ratio", "generate_audio",
                                         "loop", "prompt_influence") if key in choice}


class MediaTools:
    def __init__(self, workspace: Workspace, cancel_event=None):
        self.workspace = workspace
        self.cancel_event = cancel_event
        digest = hashlib.sha256(str(workspace.root).encode()).hexdigest()[:16]
        base = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")) / "relay/media-jobs" / digest
        self.jobs_dir = base
        self.quotes = {}
        self._catalog = {}
        self._catalog_at = 0.0
        self._images = {}
        self._images_at = 0.0
        self._image_endpoints = {}

    def _check_cancel(self):
        if self.cancel_event is not None and self.cancel_event.is_set():
            raise MediaError("Media operation cancelled. A submitted job can be checked later with media_job.")

    def _image_catalog(self):
        if time.time() - self._images_at < 3600:
            return self._images
        try:
            rows = _json_request(OPENROUTER + "/images/models").get("data", [])
            self._images = {m["id"]: m for m in rows if isinstance(m, dict) and m.get("id") in IMAGE_MODELS}
            self._images_at = time.time()
        except MediaError:
            return self._images
        return self._images

    def _image_pricing(self, model):
        cached = self._image_endpoints.get(model)
        if cached and time.time() - cached[0] < 3600:
            return cached[1]
        try:
            endpoints = _json_request(OPENROUTER + "/images/models/" + model + "/endpoints").get("endpoints", [])
        except MediaError:
            return cached[1] if cached else []
        self._image_endpoints[model] = (time.time(), endpoints)
        return endpoints

    def _video_catalog(self):
        if time.time() - self._catalog_at < 3600:
            return self._catalog
        try:
            rows = _json_request(OPENROUTER + "/videos/models").get("data", [])
            self._catalog = {m["id"]: m for m in rows if isinstance(m, dict) and m.get("id") in VIDEO_MODELS}
            self._catalog_at = time.time()
        except MediaError:
            return self._catalog
        return self._catalog

    def catalog(self):
        return {"image": {"provider": "openrouter", "default_model": IMAGE_DEFAULT,
                          "models": [{"id": model, "capabilities": self._image_catalog().get(model),
                                      "endpoints": self._image_pricing(model)}
                                     for model in IMAGE_MODELS]},
                "music": {"models": list(LYRIA_MODELS), "elevenlabs": bool(keystore.lookup("fal") or keystore.lookup("elevenlabs"))},
                "sfx": {"model": "eleven_text_to_sound_v2", "available": bool(keystore.lookup("fal") or keystore.lookup("elevenlabs"))},
                "video": [{"id": model, "capabilities": self._video_catalog().get(model)} for model in VIDEO_MODELS],
                "keys": {name: bool(keystore.lookup(name)) for name in ("openrouter", "fal", "elevenlabs")}}

    def _select(self, args):
        kind = args.get("kind")
        prompt = args.get("prompt")
        if kind not in ("image", "music", "sfx", "video") or not isinstance(prompt, str) or not prompt.strip() or len(prompt) > 4000:
            raise MediaError("Choose image, music, sfx or video and a prompt of 1–4000 characters.")
        choice = dict(args)
        choice["prompt"] = prompt.strip()
        allowed = {
            "image": {"model", "provider", "resolution", "aspect_ratio"},
            "video": {"model", "provider", "duration", "resolution", "aspect_ratio", "generate_audio"},
            "music": {"model", "provider", "duration"},
            "sfx": {"provider", "duration", "loop", "prompt_influence"},
        }
        extra = set(choice) - allowed[kind] - {"kind", "prompt", "output_path"}
        if extra:
            raise MediaError(f"Unsupported {kind} setting: {', '.join(sorted(extra))}.")
        provider = choice.get("provider")
        if kind in ("image", "video"):
            if provider not in (None, "openrouter"):
                raise MediaError("Images and videos use OpenRouter.")
            provider = "openrouter"
            model = choice.get("model") or (IMAGE_DEFAULT if kind == "image" else "google/veo-3.1-lite")
            if kind == "video" and model not in VIDEO_MODELS:
                raise MediaError("Video model must be standard Seedance 2.0 or Veo 3.1 Lite.")
            if kind == "image" and model not in IMAGE_MODELS:
                raise MediaError("Image model must be FLUX.2 Klein 4B or Gemini 3.1 Flash Lite Image.")
            choice["model"] = model
        elif kind == "music":
            provider = provider or ("fal" if keystore.lookup("fal") else "elevenlabs" if keystore.lookup("elevenlabs") else "openrouter")
            if provider not in ("openrouter", "fal", "elevenlabs"):
                raise MediaError("Music uses OpenRouter, fal.ai or ElevenLabs.")
            if provider == "openrouter":
                choice["model"] = choice.get("model") or LYRIA_MODELS[0]
                if choice["model"] not in LYRIA_MODELS:
                    raise MediaError("Choose Lyria 3 Clip or Pro for OpenRouter music.")
            else:
                if choice.get("model") not in (None, "music_v2_5"):
                    raise MediaError("ElevenLabs music uses music_v2_5.")
                choice["model"] = "music_v2_5"
        else:
            if not provider and not (keystore.lookup("fal") or keystore.lookup("elevenlabs")):
                raise MediaError("Sound effects need a fal.ai or ElevenLabs key. Add one in Options › Models › API keys.")
            provider = provider or ("fal" if keystore.lookup("fal") else "elevenlabs")
            if provider not in ("fal", "elevenlabs"):
                raise MediaError("Sound effects need a fal.ai or ElevenLabs key.")
            choice["model"] = "eleven_text_to_sound_v2"
        if not keystore.lookup(provider):
            raise MediaError(f"{kind} needs a {provider} key. Add it in Options › Models › API keys.")
        choice["provider"] = provider
        duration = choice.get("duration")
        if duration is not None:
            if isinstance(duration, bool) or not isinstance(duration, (int, float)) or not math.isfinite(duration):
                raise MediaError("Duration must be a number of seconds.")
            if kind == "sfx" and not (0.5 <= duration <= (22 if provider == "fal" else 30)):
                raise MediaError("SFX duration is 0.5–22 s via fal, or 0.5–30 s direct.")
            if kind == "video" and (duration != int(duration) or
                                    (duration not in (4, 6, 8) if choice["model"] == "google/veo-3.1-lite" else not 4 <= duration <= 15)):
                raise MediaError("Veo Lite supports 4, 6 or 8 s; Seedance 2.0 supports 4–15 s.")
            if kind == "music" and provider != "openrouter" and not 3 <= duration <= 600:
                raise MediaError("ElevenLabs music duration is 3–600 s.")
        if kind == "sfx":
            influence = choice.get("prompt_influence", 0.3)
            if isinstance(influence, bool) or not isinstance(influence, (int, float)) or not 0 <= influence <= 1:
                raise MediaError("Prompt influence must be between 0 and 1.")
            if not isinstance(choice.get("loop", False), bool):
                raise MediaError("loop must be true or false.")
        if kind == "video":
            choice.setdefault("duration", 8)
            choice.setdefault("resolution", "720p")
            choice.setdefault("aspect_ratio", "16:9")
            choice.setdefault("generate_audio", False)
            if not isinstance(choice["generate_audio"], bool):
                raise MediaError("generate_audio must be true or false.")
            if choice["resolution"] not in ("480p", "720p", "1080p", "4K"):
                raise MediaError("Unsupported video resolution.")
            if choice["aspect_ratio"] not in ("16:9", "9:16", "1:1", "4:3", "3:4", "21:9", "9:21"):
                raise MediaError("Unsupported video aspect ratio.")
            row = self._video_catalog().get(choice["model"], {})
            if not row and (choice["resolution"], choice["aspect_ratio"]) != ("720p", "16:9"):
                raise MediaError("Video catalog unavailable; quote the default 720p 16:9 settings or retry later.")
            for setting, field in (("resolution", "supported_resolutions"), ("aspect_ratio", "supported_aspect_ratios")):
                value = choice.get(setting)
                if value is not None and row.get(field) and value not in row[field]:
                    raise MediaError(f"{setting} is not supported by {choice['model']}.")
            if row.get("supported_durations") and choice["duration"] not in row["supported_durations"]:
                raise MediaError("Duration is not supported by the selected video model.")
        if kind == "image":
            row = self._image_catalog().get(choice["model"], {})
            if not row and (choice.get("resolution") or choice.get("aspect_ratio")):
                raise MediaError("Image catalog unavailable; retry model-specific settings later.")
            parameters = row.get("supported_parameters") or {}
            for setting in ("resolution", "aspect_ratio"):
                value = choice.get(setting)
                if value is None:
                    continue
                descriptor = parameters.get(setting)
                if row and (not descriptor or value not in descriptor.get("values", [])):
                    raise MediaError(f"{setting} is not supported by {choice['model']}.")
        path = choice.get("output_path")
        if path is not None:
            if not isinstance(path, str) or not path or Path(path).suffix:
                raise MediaError("output_path must be a path stem inside the workspace, without an extension.")
            self.workspace.resolve(path, allow_missing=True)
        return choice

    def quote(self, args):
        choice = self._select(args)
        estimate = None
        source = "estimate unavailable"
        if choice["kind"] == "video":
            row = self._video_catalog().get(choice["model"], {})
            skus = row.get("pricing_skus") or {}
            resolution = choice["resolution"]
            audio = "with_audio" if choice["generate_audio"] else "without_audio"
            rate = (skus.get(f"duration_seconds_{audio}_{resolution}") or
                    skus.get(f"duration_seconds_{audio}") or
                    skus.get("duration_seconds_" + resolution) or skus.get("duration_seconds"))
            if rate is not None:
                try:
                    estimate = round(float(rate) * choice.get("duration", 8), 4)
                    source = "OpenRouter video model catalog"
                except (TypeError, ValueError):
                    pass
            elif choice["model"] == "bytedance/seedance-2.0":
                shortest = 2160 if resolution == "4K" else int(resolution.removesuffix("p"))
                ratio = choice["aspect_ratio"].split(":")
                target_ratio = int(ratio[0]) / int(ratio[1])
                sizes = row.get("supported_sizes") or []
                dimensions = []
                for size in sizes:
                    try:
                        width, height = map(int, size.split("x"))
                    except (ValueError, AttributeError):
                        continue
                    if min(width, height) == shortest and abs(width / height - target_ratio) < 0.025:
                        dimensions.append((width, height))
                rate = skus.get("video_tokens_1080p" if resolution == "1080p" else
                                "video_tokens_4k" if resolution == "4K" else
                                "video_tokens_without_audio" if not choice["generate_audio"] else "video_tokens")
                if dimensions and rate is not None:
                    width, height = dimensions[0]
                    try:
                        estimate = round(width * height * choice["duration"] * 24 / 1024 * float(rate), 4)
                        source = "OpenRouter video token rate × catalog dimensions × 24 fps; approximate"
                    except (TypeError, ValueError):
                        pass
        elif choice["kind"] == "music" and choice["provider"] == "openrouter":
            estimate = 0.04 if choice["model"] == LYRIA_MODELS[0] else 0.08
            source = "OpenRouter Lyria published per-song rate; check current price"
        elif choice["kind"] == "image" and choice["model"] == IMAGE_DEFAULT:
            for endpoint in self._image_pricing(IMAGE_DEFAULT):
                for price in endpoint.get("pricing", []):
                    if price.get("billable") == "output_image" and price.get("unit") == "megapixel":
                        try:
                            estimate = float(price["cost_usd"])
                            source = "OpenRouter image endpoint, approximate 1 megapixel output"
                        except (TypeError, ValueError, KeyError):
                            pass
                        break
                if estimate is not None:
                    break
        token = secrets.token_urlsafe(24)
        self.quotes[token] = (time.time(), choice, estimate, source)
        return {"quote_id": token, "kind": choice["kind"], "provider": choice["provider"],
                "model": choice["model"], "settings": _settings(choice),
                "estimated_usd": estimate, "price_source": source, "quoted_at": int(time.time()),
                "expires_in_seconds": QUOTE_SECONDS}

    def _save(self, choice, raw):
        ext = _extension(choice["kind"], raw)
        stem = choice.get("output_path") or "media/" + secrets.token_hex(8)
        target = self.workspace.resolve(stem + ext, allow_missing=True)
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists():
            raise MediaError("Output path already exists; choose a new path stem.")
        fd, temporary = tempfile.mkstemp(prefix=".relay-media-", dir=target.parent)
        try:
            with os.fdopen(fd, "wb") as stream:
                stream.write(raw)
                stream.flush()
                os.fsync(stream.fileno())
            os.chmod(temporary, 0o600)
            os.link(temporary, target)  # no overwrite if another session produced the same path
        finally:
            os.unlink(temporary)
        return str(target)

    def _save_job(self, job):
        self.jobs_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
        path = self.jobs_dir / (job["job_id"] + ".json")
        fd, temporary = tempfile.mkstemp(dir=self.jobs_dir, prefix=".media-job-")
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as stream:
                json.dump(job, stream)
            os.chmod(temporary, 0o600)
            os.replace(temporary, path)
        finally:
            if os.path.exists(temporary):
                os.unlink(temporary)

    def _new_job(self, choice, provider_id, estimate):
        job = {"job_id": secrets.token_urlsafe(18), "provider": choice["provider"],
               "provider_id": provider_id, "kind": choice["kind"], "model": choice["model"],
               "settings": _settings(choice),
               "output_path": choice.get("output_path"), "estimated_usd": estimate,
               "created": int(time.time()), "status": "pending"}
        self._save_job(job)
        return {"job_id": job["job_id"], "status": "pending", "provider": job["provider"],
                "model": job["model"], "kind": job["kind"], "settings": job["settings"],
                "estimated_usd": estimate}

    def generate(self, args):
        token = args.get("quote_id")
        if not isinstance(token, str) or token not in self.quotes:
            raise MediaError("Call media_quote first; this quote is unknown or expired.")
        quoted, choice, estimate, _ = self.quotes.pop(token)
        if time.time() - quoted > QUOTE_SECONDS:
            raise MediaError("Media quote expired. Quote the request again.")
        provider = choice["provider"]
        key = keystore.lookup(provider)
        if not key:
            raise MediaError(f"The {provider} key is no longer available.")
        self._check_cancel()
        kind = choice["kind"]
        if kind == "video":
            payload = {"model": choice["model"], "prompt": choice["prompt"],
                       "duration": choice.get("duration", 8)}
            for field in ("resolution", "aspect_ratio", "generate_audio"):
                if field in choice:
                    payload[field] = choice[field]
            result = _json_request(OPENROUTER + "/videos", key=key, payload=payload)
            provider_id = result.get("id")
            if not isinstance(provider_id, str) or not _ID.fullmatch(provider_id):
                raise MediaError("OpenRouter did not return a valid video job ID. Check the provider account before retrying.")
            return self._new_job(choice, provider_id, estimate)
        if kind in ("music", "sfx") and provider == "fal":
            endpoint = FAL_SFX if kind == "sfx" else FAL_MUSIC
            payload = ({"text": choice["prompt"], "prompt_influence": choice.get("prompt_influence", 0.3),
                        "loop": choice.get("loop", False)} if kind == "sfx" else
                       {"prompt": choice["prompt"]})
            if choice.get("duration") is not None:
                payload["duration_seconds" if kind == "sfx" else "music_length_ms"] = (
                    choice["duration"] if kind == "sfx" else int(choice["duration"] * 1000))
            result = _json_request(FAL + "/" + endpoint, key=key, auth="Key", payload=payload)
            provider_id = result.get("request_id")
            if not isinstance(provider_id, str) or not _ID.fullmatch(provider_id):
                raise MediaError("fal did not return a valid job ID. Check the provider account before retrying.")
            return self._new_job(choice, provider_id, estimate)
        if kind == "image":
            payload = {"model": choice["model"], "prompt": choice["prompt"]}
            for field in ("resolution", "aspect_ratio"):
                if field in choice:
                    payload[field] = choice[field]
            reply = _json_request(OPENROUTER + "/images", key=key, payload=payload)
            images = reply.get("data") or []
            if not images or not isinstance(images[0], dict):
                raise MediaError("Image provider returned no image.")
            raw = _b64(images[0].get("b64_json"))
            return {"path": self._save(choice, raw), "kind": kind, "model": choice["model"],
                    "settings": _settings(choice),
                    "provider": provider, "estimated_usd": estimate,
                    "actual_usd": (reply.get("usage") or {}).get("cost")}
        if kind == "music" and provider == "openrouter":
            reply = _json_request(OPENROUTER + "/chat/completions", key=key,
                                  payload={"model": choice["model"],
                                           "modalities": ["audio"],
                                           "messages": [{"role": "user", "content": choice["prompt"]}]})
            raw = _audio_from_completion(reply)
            return {"path": self._save(choice, raw), "kind": kind, "model": choice["model"],
                    "settings": _settings(choice),
                    "provider": provider, "estimated_usd": estimate,
                    "actual_usd": (reply.get("usage") or {}).get("cost")}
        payload = {"text": choice["prompt"], "model_id": choice["model"],
                   "loop": choice.get("loop", False),
                   "prompt_influence": choice.get("prompt_influence", 0.3)}
        if choice.get("duration") is not None:
            payload["duration_seconds"] = choice["duration"]
        if kind == "music":
            payload = {"prompt": choice["prompt"], "model_id": "music_v2_5"}
            if choice.get("duration") is not None:
                payload["music_length_ms"] = int(choice["duration"] * 1000)
            route = "/music"
        else:
            route = "/sound-generation"
        raw, _ = _bytes_request(ELEVENLABS + route, payload=payload,
                                headers={"xi-api-key": key})
        return {"path": self._save(choice, raw), "kind": kind, "model": choice["model"],
                "settings": _settings(choice),
                "provider": provider, "estimated_usd": estimate, "actual_usd": None}

    def job(self, args):
        identifier = args.get("job_id")
        if not isinstance(identifier, str) or not _ID.fullmatch(identifier):
            raise MediaError("Invalid media job ID.")
        try:
            job = json.loads((self.jobs_dir / (identifier + ".json")).read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            raise MediaError("Media job not found in this workspace.") from None
        if job.get("status") == "completed":
            return {k: job.get(k) for k in ("job_id", "status", "path", "kind", "model", "provider", "settings", "estimated_usd", "actual_usd")}
        self._check_cancel()
        provider = job["provider"]
        key = keystore.lookup(provider)
        if not key:
            raise MediaError(f"Checking this job needs the {provider} key.")
        provider_id = job["provider_id"]
        if not isinstance(provider_id, str) or not _ID.fullmatch(provider_id):
            raise MediaError("Saved media job has an invalid provider ID.")
        if provider == "openrouter":
            url = OPENROUTER + "/videos/" + provider_id
            reply = _json_request(url, key=key)
            status = reply.get("status")
            if status in ("failed", "cancelled", "canceled"):
                job["status"] = status
                self._save_job(job)
                return {"job_id": identifier, "status": status, "model": job["model"], "provider": provider}
            if status != "completed":
                return {"job_id": identifier, "status": status or "pending", "model": job["model"], "provider": provider}
            self._check_cancel()
            raw, _ = _bytes_request(url + "/content", key=key)
            actual = (reply.get("usage") or {}).get("cost")
        else:
            endpoint = FAL_SFX if job["kind"] == "sfx" else FAL_MUSIC
            url = FAL + "/" + endpoint + "/requests/" + provider_id
            status_reply = _json_request(url + "/status", key=key, auth="Key")
            status = status_reply.get("status")
            if status in ("FAILED", "CANCELLED", "CANCELED"):
                job["status"] = status.lower()
                self._save_job(job)
                return {"job_id": identifier, "status": job["status"], "model": job["model"], "provider": provider}
            if status != "COMPLETED":
                return {"job_id": identifier, "status": status or "pending", "model": job["model"], "provider": provider}
            self._check_cancel()
            reply = _json_request(url, key=key, auth="Key")
            audio = reply.get("audio") or (reply.get("data") or {}).get("audio") or {}
            if not isinstance(audio, dict) or not isinstance(audio.get("url"), str):
                raise MediaError("fal returned no audio file.")
            raw, _ = _bytes_request(_file_url(audio["url"]))
            actual = None
        choice = {"kind": job["kind"], "output_path": job.get("output_path")}
        path = self._save(choice, raw)
        job.update({"status": "completed", "path": path, "actual_usd": actual})
        self._save_job(job)
        return {k: job.get(k) for k in ("job_id", "status", "path", "kind", "model", "provider", "settings", "estimated_usd", "actual_usd")}

    def run(self, name, args):
        if name == "media_catalog":
            return self.catalog()
        if name == "media_quote":
            return self.quote(args)
        if name == "media_generate":
            return self.generate(args)
        if name == "media_job":
            return self.job(args)
        raise MediaError("Unknown media tool.")
