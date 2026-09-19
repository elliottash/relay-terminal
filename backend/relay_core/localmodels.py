# SPDX-License-Identifier: GPL-3.0-or-later
"""Model servers on this machine: the endpoint registry and the probe (protocol section 28).

A local server is not a preset. Its model id and the window it was started with are not knowable
ahead of time, it has no key and no key page, and every invariant the preset table is tested for
(https, a 128K window, a tier triple) is wrong for it. So local endpoints live beside the presets,
in ``$XDG_CONFIG_HOME/relay/local-models.json``, and the rest of the backend sees one through
``as_preset()``. The file is written here rather than in QSettings because ``scripts/relay-agent.py``
and a worker with no GUI read the same registry.

Ids are ``local:<slug>``. The colon is what keeps them apart: no preset id contains one, and
``keystore._check_id`` refuses one, so a local id can never reach the keyring.

Nothing here starts a server. A probe that finds nothing answers with the command that would.
"""
from __future__ import annotations

import json
import os
import re
import threading
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path

from .presets import Preset
from .provider import LOCAL_HOSTS, NoRedirect, loopback_http

PREFIX = "local:"
SERVERS = ("llamacpp", "ollama", "lmstudio", "vllm", "openai-compatible")
SERVER_LABELS = {"llamacpp": "llama.cpp", "ollama": "Ollama", "lmstudio": "LM Studio", "vllm": "vLLM",
                 "openai-compatible": "OpenAI-compatible server"}
DEFAULT_PORTS = {"ollama": 11434, "lmstudio": 1234, "llamacpp": 8080, "vllm": 8000}

# A loopback server answers in milliseconds or not at all. Codex gives its probe 5 s against a
# 300 s stream budget; the same split, tighter because nothing here crosses a network.
CONNECT_PROBE_TIMEOUT = 2.0
MAX_PROBE_BODY = 1024 * 1024
MAX_MODELS = 200

# What a local endpoint gets when nothing better is known. 32K is the floor the other harnesses
# name for tool calling (opencode: "start around 16k - 32k"), and guessing low only compacts early,
# while guessing high overflows the server.
FALLBACK_CONTEXT_WINDOW = 32_768
MIN_CONTEXT_WINDOW, MAX_CONTEXT_WINDOW = 2_048, 4_000_000
# A cold model load plus a long prefill is silent for minutes. opencode's headerTimeout and
# chunkTimeout, Codex's stream idle timeout and Qwen Code's streamIdleTimeoutMs all sit at 300 s.
DEFAULT_FIRST_TOKEN_TIMEOUT = 300.0

ENV_PATH = "RELAY_LOCAL_MODELS"
_SLUG = re.compile(r"^[a-z0-9][a-z0-9-]{0,47}$")
TYPES = {"local_probe", "local_endpoints", "local_endpoint_save", "local_endpoint_delete"}


def is_local_id(value) -> bool:
    return isinstance(value, str) and value.startswith(PREFIX)


def _bool(value, name: str, default: bool) -> bool:
    if value is None:
        return default
    if not isinstance(value, bool):
        raise ValueError(f"{name} must be true or false.")
    return value


@dataclass(frozen=True)
class LocalEndpoint:
    id: str                         # "local:<slug>"
    label: str
    base_url: str                   # the OpenAI-compatible base, e.g. http://127.0.0.1:8080/v1
    model: str
    server: str = "openai-compatible"
    context_window: int = FALLBACK_CONTEXT_WINDOW
    tools: bool = True              # the served chat template takes a tools array
    thinking: bool = False
    extra: dict = field(default_factory=dict)
    note: str = ""
    first_token_timeout: float = DEFAULT_FIRST_TOKEN_TIMEOUT
    parallel_tool_calls: bool = False
    # Off unless the owner of the endpoint turns it on: a recovery parser that misfires turns a JSON
    # block in an ordinary answer into an executed command (card #24XJ, Decisions).
    tool_text_recovery: bool = False
    # Some chat templates cannot read a tool call's arguments as the JSON string OpenAI's shape
    # carries: Meta's ATEM template on llama.cpp --jinja needs a mapping, and without this the
    # second turn of every tool conversation fails. Ollama serving the same model does not.
    tool_arguments_as_object: bool = False

    @property
    def slug(self) -> str:
        return self.id[len(PREFIX):]

    def to_dict(self) -> dict:
        """The keys of ``Preset.to_dict()`` plus what only a local endpoint has."""
        return {**self.as_preset().to_dict(), "tools": self.tools, "thinking": self.thinking,
                "first_token_timeout": self.first_token_timeout,
                "parallel_tool_calls": self.parallel_tool_calls,
                "tool_text_recovery": self.tool_text_recovery,
                "tool_arguments_as_object": self.tool_arguments_as_object}

    def stored(self) -> dict:
        return {"id": self.id, "label": self.label, "base_url": self.base_url, "model": self.model,
                "server": self.server, "context_window": self.context_window, "tools": self.tools,
                "thinking": self.thinking, "extra": dict(self.extra), "note": self.note,
                "first_token_timeout": self.first_token_timeout,
                "parallel_tool_calls": self.parallel_tool_calls,
                "tool_text_recovery": self.tool_text_recovery,
                "tool_arguments_as_object": self.tool_arguments_as_object}

    def as_preset(self) -> Preset:
        """This endpoint in the shape the rest of the backend already reads.

        ``effort_style`` is "none": an OpenAI-compatible local server has no effort knob Relay can
        rely on, so no reasoning_effort is sent and the GUI offers no effort choice.
        """
        return Preset(self.id, self.label, self.base_url, self.model, dict(self.extra),
                      context_window=self.context_window, effort_style="none", group="local",
                      note=self.note, provider=self.label, plan=SERVER_LABELS.get(self.server, ""),
                      local=True, server=self.server)


def make_id(value: str) -> str:
    """``local:<slug>`` from an id, a bare slug, or a label."""
    if not isinstance(value, str) or not value.strip():
        raise ValueError("A local endpoint needs an id.")
    raw = value.strip().lower()
    raw = raw[len(PREFIX):] if raw.startswith(PREFIX) else raw
    slug = re.sub(r"[^a-z0-9]+", "-", raw).strip("-")[:48].strip("-")
    if not _SLUG.match(slug):
        raise ValueError("A local endpoint id is lowercase letters, digits and hyphens.")
    return PREFIX + slug


def from_dict(spec: dict) -> LocalEndpoint:
    """Validate one endpoint. Raises ValueError with a sentence a person can act on."""
    if not isinstance(spec, dict):
        raise ValueError("A local endpoint must be an object.")
    base_url = str(spec.get("base_url") or "").strip().rstrip("/")
    if not loopback_http(base_url):
        raise ValueError("A local endpoint is plain http:// on localhost, 127.0.0.1 or ::1. "
                         "A server elsewhere needs https and a key: add it as a custom provider.")
    url = urllib.parse.urlsplit(base_url)
    if url.username or url.password or url.query or url.fragment:
        raise ValueError("The base URL must not carry credentials, a query or a fragment.")
    model = str(spec.get("model") or "").strip()
    if not model:
        raise ValueError("A local endpoint needs a model id. Probe the server to list what it serves.")
    server = spec.get("server") or "openai-compatible"
    if server not in SERVERS:
        raise ValueError("server must be one of " + ", ".join(SERVERS) + ".")
    window = spec.get("context_window", FALLBACK_CONTEXT_WINDOW)
    if isinstance(window, bool) or not isinstance(window, int) \
            or not MIN_CONTEXT_WINDOW <= window <= MAX_CONTEXT_WINDOW:
        raise ValueError(f"context_window must be a whole number from {MIN_CONTEXT_WINDOW} to {MAX_CONTEXT_WINDOW}.")
    timeout = spec.get("first_token_timeout", DEFAULT_FIRST_TOKEN_TIMEOUT)
    if isinstance(timeout, bool) or not isinstance(timeout, (int, float)) or not 1.0 <= float(timeout) <= 1800.0:
        raise ValueError("first_token_timeout must be a number from 1 to 1800 seconds.")
    extra = spec.get("extra") if spec.get("extra") is not None else {}
    if not isinstance(extra, dict):
        raise ValueError("extra must be a JSON object.")
    endpoint_id = make_id(spec.get("id") or spec.get("label") or model)
    label = str(spec.get("label") or "").strip() or f"{model} (local)"
    return LocalEndpoint(endpoint_id, label[:80], base_url, model, server, window,
                         _bool(spec.get("tools"), "tools", True),
                         _bool(spec.get("thinking"), "thinking", False),
                         dict(extra), str(spec.get("note") or "")[:200], float(timeout),
                         _bool(spec.get("parallel_tool_calls"), "parallel_tool_calls", False),
                         _bool(spec.get("tool_text_recovery"), "tool_text_recovery", False),
                         _bool(spec.get("tool_arguments_as_object"), "tool_arguments_as_object", False))


# ----- the registry file ---------------------------------------------------------------------
def config_path() -> Path:
    override = os.environ.get(ENV_PATH, "").strip()
    if override:
        return Path(override).expanduser()
    base = os.environ.get("XDG_CONFIG_HOME") or str(Path.home() / ".config")
    return Path(base) / "relay" / "local-models.json"


_lock = threading.Lock()
_cache: tuple[str, float, int, dict[str, LocalEndpoint]] | None = None


def catalog() -> dict[str, LocalEndpoint]:
    """Every saved endpoint by id. Re-read when the file changes: each pane has its own worker, and
    an endpoint added in one must be there for the next configure in another."""
    global _cache
    path = config_path()
    try:
        stat = path.stat()
    except OSError:
        return {}
    with _lock:
        if _cache is not None and _cache[:3] == (str(path), stat.st_mtime, stat.st_size):
            return dict(_cache[3])
        items: dict[str, LocalEndpoint] = {}
        try:
            raw = json.loads(path.read_text(encoding="utf-8"))
            for spec in (raw.get("endpoints") if isinstance(raw, dict) else None) or []:
                try:
                    endpoint = from_dict(spec)
                except ValueError:
                    continue            # one bad entry must not hide the others
                items[endpoint.id] = endpoint
        except (OSError, ValueError):
            items = {}
        _cache = (str(path), stat.st_mtime, stat.st_size, items)
        return dict(items)


def find(endpoint_id) -> LocalEndpoint | None:
    return catalog().get(endpoint_id) if is_local_id(endpoint_id) else None


def _same_url(a: str, b: str) -> bool:
    return a.strip().rstrip("/").lower() == b.strip().rstrip("/").lower()


def match(base_url: str, model: str = "") -> LocalEndpoint | None:
    """The saved endpoint at a base URL; ``model`` only breaks ties."""
    if not loopback_http(base_url):
        return None
    candidates = [e for e in catalog().values() if _same_url(e.base_url, base_url)]
    for endpoint in candidates:
        if endpoint.model == (model or "").strip():
            return endpoint
    return candidates[0] if candidates else None


def resolve(preset_id=None, base_url: str = "", model: str = "") -> LocalEndpoint | None:
    return find(preset_id) or match(base_url or "", model or "")


def keyless(preset_id=None, base_url: str = "") -> bool:
    """Whether this provider legitimately has no key: a saved local endpoint, or plain HTTP to a
    loopback host. Never "no key was found"."""
    return find(preset_id) is not None or loopback_http(base_url)


def provider_fields(preset_id=None, base_url: str = "", model: str = "") -> dict:
    """ProviderConfig keyword arguments for a model server on this machine; ``{}`` for anything else.

    A loopback URL that was never saved still counts: it gets the local transport with the cautious
    defaults, because what makes a server local is where it is, not whether it is in the registry.
    """
    if not loopback_http(base_url):
        return {}
    endpoint = resolve(preset_id, base_url, model)
    if endpoint is None:
        return {"local": True, "first_token_timeout": DEFAULT_FIRST_TOKEN_TIMEOUT}
    return {"local": True, "first_token_timeout": endpoint.first_token_timeout,
            "parallel_tool_calls": endpoint.parallel_tool_calls,
            "tool_text_recovery": endpoint.tool_text_recovery,
            "tool_arguments_as_object": endpoint.tool_arguments_as_object,
            "context_window": endpoint.context_window}


def clamp_max_tokens(max_tokens: int, fields: dict) -> int:
    """The output limit a local server can honour: a quarter of the served window at most.

    On a 32K server, an unclamped limit promises the whole window to the reply, and compaction
    (80% of the window) would leave 6.5K for an answer that was promised 32K.

    0 is "automatic" and passes straight through: ``ProviderConfig`` settles it, and applies this
    same quarter afterwards, so a local endpoint gets the quarter either way.
    """
    window = fields.get("context_window") if fields else None
    if not window or isinstance(max_tokens, bool) or not isinstance(max_tokens, int) or max_tokens <= 0:
        return max_tokens
    return max(256, min(max_tokens, window // 4))


def _write(items: dict[str, LocalEndpoint]) -> None:
    global _cache
    path = config_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    body = json.dumps({"version": 1, "endpoints": [e.stored() for e in items.values()]},
                      indent=2, ensure_ascii=False) + "\n"
    temp = path.with_name(path.name + f".{os.getpid()}.tmp")
    temp.write_text(body, encoding="utf-8")
    os.replace(temp, path)
    with _lock:
        _cache = None


def save(spec: dict) -> LocalEndpoint:
    endpoint = from_dict(spec)
    items = catalog()
    items[endpoint.id] = endpoint
    _write(items)
    return endpoint


def delete(endpoint_id) -> bool:
    items = catalog()
    if not is_local_id(endpoint_id) or endpoint_id not in items:
        return False
    del items[endpoint_id]
    _write(items)
    return True


# ----- the probe -----------------------------------------------------------------------------
@dataclass
class Probe:
    base_url: str
    ok: bool = False
    server: str = ""
    state: str = "down"             # ready | loading | sleeping | down
    context_window: int | None = None
    models: list = field(default_factory=list)
    error: str = ""

    def to_dict(self) -> dict:
        out = {"base_url": self.base_url, "ok": self.ok, "server": self.server, "state": self.state,
               "context_window": self.context_window, "models": self.models}
        if self.error:
            out["error"] = self.error
        return out


class _Down(Exception):
    pass


def _get(url: str, timeout: float, body: dict | None = None):
    """(status, parsed JSON or None). No Authorization header, no redirects, a bounded body."""
    data = json.dumps(body).encode("utf-8") if body is not None else None
    headers = {"Accept": "application/json", "User-Agent": "Relay/0.1"}
    if data is not None:
        headers["Content-Type"] = "application/json"
    request = urllib.request.Request(url, data=data, headers=headers, method="POST" if data else "GET")
    opener = urllib.request.build_opener(NoRedirect(), urllib.request.ProxyHandler({}))
    try:
        with opener.open(request, timeout=timeout) as response:
            status, raw = response.status, response.read(MAX_PROBE_BODY + 1)
    except urllib.error.HTTPError as exc:
        status, raw = exc.code, exc.read(MAX_PROBE_BODY + 1)
        exc.close()
    except (urllib.error.URLError, TimeoutError, OSError) as exc:
        raise _Down(type(getattr(exc, "reason", exc)).__name__) from None
    except RuntimeError:                # NoRedirect raises ProviderError
        return 0, None
    if len(raw) > MAX_PROBE_BODY:
        return status, None
    try:
        return status, json.loads(raw)
    except (ValueError, UnicodeError):
        return status, None


def roots(base_url: str) -> tuple[str, str]:
    """(server root, OpenAI base). ``http://h:8080`` and ``http://h:8080/v1`` name the same server:
    the native endpoints (/health, /props, /api/tags) hang off the root, chat off ``/v1``."""
    url = urllib.parse.urlsplit(base_url.strip().rstrip("/"))
    path = url.path.rstrip("/")
    root_path = path[:-3] if path.endswith("/v1") else path
    root = urllib.parse.urlunsplit((url.scheme, url.netloc, root_path, "", ""))
    base = root + "/v1" if not path or path.endswith("/v1") else root
    return root.rstrip("/"), base.rstrip("/")


def start_hint(base_url: str) -> str:
    """What to run when nothing answers. Keyed by port, because that is all there is to go on."""
    url = urllib.parse.urlsplit(base_url)
    port = url.port or 80
    where = f"{url.hostname}:{port}"
    if port == DEFAULT_PORTS["ollama"]:
        return f"No Ollama server is answering on {where}. Start it with `ollama serve`."
    if port == DEFAULT_PORTS["lmstudio"]:
        return (f"No LM Studio server is answering on {where}. Start it with `lms server start`, "
                "or Developer › Start Server in the app.")
    if port == DEFAULT_PORTS["vllm"]:
        return f"No vLLM server is answering on {where}. Start it with `vllm serve <model> --port {port}`."
    return (f"No model server is answering on {where}. Start it, for example "
            f"`llama-server -m <model.gguf> --jinja --port {port}`, or the systemd user unit that "
            "serves it (docs/LOCAL-MODELS.md).")


def _window(value) -> int | None:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        return None
    return min(max(value, MIN_CONTEXT_WINDOW), MAX_CONTEXT_WINDOW)


def _model_rows(listing, window: int | None = None, tools=None, thinking=None) -> list[dict]:
    rows = []
    data = listing.get("data") if isinstance(listing, dict) else None
    for item in (data if isinstance(data, list) else [])[:MAX_MODELS]:
        if not isinstance(item, dict) or not isinstance(item.get("id"), str) or not item["id"]:
            continue
        # LM Studio reports the loaded window, vLLM the configured maximum.
        own = _window(item.get("loaded_context_length")) or _window(item.get("max_context_length")) \
            or _window(item.get("max_model_len"))
        rows.append({"id": item["id"], "context_window": own or window, "tools": tools, "thinking": thinking})
    return rows


def _probe_llamacpp(root: str, base: str, timeout: float) -> Probe | None:
    status, props = _get(root + "/props", timeout)
    if status == 503:
        # llama-server answers 503 "Loading model" on every route until the weights are in.
        return Probe(base, ok=True, server="llamacpp", state="loading")
    if status != 200 or not isinstance(props, dict) or "default_generation_settings" not in props:
        return None
    settings = props.get("default_generation_settings")
    window = _window(settings.get("n_ctx")) if isinstance(settings, dict) else None
    caps = props.get("chat_template_caps")
    tools = thinking = None
    if isinstance(caps, dict):
        known = [caps[k] for k in ("supports_tools", "supports_tool_calls") if isinstance(caps.get(k), bool)]
        tools = any(known) if known else None
        # The template says whether it has a reasoning mode (measured on llama-server b10706:
        # supports_preserve_reasoning, supports_reasoning_effort). Absent keys mean "not known".
        reasons = [v for k, v in caps.items() if "reasoning" in k and isinstance(v, bool)]
        thinking = any(reasons) if reasons else None
    probe = Probe(base, ok=True, server="llamacpp", context_window=window,
                  state="sleeping" if props.get("is_sleeping") is True else "ready")
    _, listing = _get(base + "/models", timeout)
    probe.models = _model_rows(listing, window, tools, thinking)
    return probe


def _probe_ollama(root: str, base: str, timeout: float) -> Probe | None:
    status, tags = _get(root + "/api/tags", timeout)
    if status != 200 or not isinstance(tags, dict) or not isinstance(tags.get("models"), list):
        return None
    probe = Probe(root + "/v1", ok=True, server="ollama", state="ready")
    for item in tags["models"][:MAX_MODELS]:
        name = item.get("name") or item.get("model") if isinstance(item, dict) else None
        if not isinstance(name, str) or not name:
            continue
        details = item.get("details") if isinstance(item.get("details"), dict) else {}
        caps = item.get("capabilities")
        window = _window(details.get("context_length"))
        if window is None or not isinstance(caps, list):
            # Older Ollama: neither field is in /api/tags, both are in /api/show.
            _, shown = _get(root + "/api/show", timeout, {"model": name})
            if isinstance(shown, dict):
                caps = caps if isinstance(caps, list) else shown.get("capabilities")
                info = shown.get("model_info") if isinstance(shown.get("model_info"), dict) else {}
                window = window or next((_window(v) for k, v in info.items()
                                         if k.endswith(".context_length") and _window(v)), None)
        caps = caps if isinstance(caps, list) else None
        probe.models.append({"id": name, "context_window": window,
                             "tools": ("tools" in caps) if caps is not None else None,
                             "thinking": ("thinking" in caps) if caps is not None else None})
    return probe


def _probe_openai(root: str, base: str, timeout: float) -> Probe | None:
    status, listing = _get(base + "/models", timeout)
    if status != 200 or not isinstance(listing, dict):
        return None
    rows = _model_rows(listing)
    data = listing.get("data") if isinstance(listing.get("data"), list) else []
    server = "openai-compatible"
    if any(isinstance(i, dict) and "max_model_len" in i for i in data):
        server = "vllm"
    elif any(isinstance(i, dict) and ("loaded_context_length" in i or "max_context_length" in i) for i in data):
        server = "lmstudio"
    return Probe(base, ok=True, server=server, state="ready", models=rows)


def probe(base_url: str, *, timeout: float = CONNECT_PROBE_TIMEOUT) -> Probe:
    """What is serving at ``base_url``, what it serves, and the window it was started with.

    Never raises. The window reported is the *served* one (llama.cpp ``n_ctx``, LM Studio's loaded
    length), not the model's maximum: that is the number a request overflows.
    """
    base_url = str(base_url or "").strip()
    if not loopback_http(base_url):
        return Probe(base_url, error="Relay only probes a model server on this machine: "
                                     "http://localhost, http://127.0.0.1 or http://[::1].")
    root, base = roots(base_url)
    port = urllib.parse.urlsplit(root).port
    order = [_probe_llamacpp, _probe_ollama, _probe_openai]
    if port == DEFAULT_PORTS["ollama"]:
        order = [_probe_ollama, _probe_openai, _probe_llamacpp]
    try:
        for step in order:
            found = step(root, base, timeout)
            if found is not None:
                if found.context_window is None:
                    windows = {m["context_window"] for m in found.models if m.get("context_window")}
                    found.context_window = windows.pop() if len(windows) == 1 else None
                if found.state == "ready" and not found.models:
                    found.error = "The server is up but lists no models. Load or pull one first."
                return found
    except _Down:
        return Probe(base, error=start_hint(base))
    return Probe(base, error=f"Something answers at {root}, but not as a model server Relay knows "
                             "(llama.cpp, Ollama, LM Studio, vLLM or an OpenAI-compatible /v1/models).")


LOADING_POLL_S = 1.0
DETECT_WAIT_S = 120.0     # a 27B model loads from a cold disk in well under this


def detect(spec: dict, *, timeout: float = CONNECT_PROBE_TIMEOUT, wait: float = 0.0) -> tuple[dict, Probe]:
    """Fill what a probe can know (server kind, base URL, model, window, capabilities) into ``spec``.

    What the caller wrote wins, except the window: the served window is a fact, not a preference.
    A server that is still loading its weights knows none of this yet, so it is polled for up to
    ``wait`` seconds; one that is still loading after that is reported as such, not as "no model".
    """
    import time
    deadline = time.monotonic() + max(0.0, wait)
    found = probe(spec.get("base_url", ""), timeout=timeout)
    while found.ok and found.state == "loading" and time.monotonic() < deadline:
        time.sleep(LOADING_POLL_S)
        found = probe(spec.get("base_url", ""), timeout=timeout)
    out = dict(spec)
    if found.ok and found.state == "loading":
        found.error = "The server is still loading its model. Try again when it is ready."
        found.ok = False
    if not found.ok:
        return out, found
    out["base_url"] = found.base_url
    out.setdefault("server", found.server)
    wanted = str(out.get("model") or "").strip()
    row = next((m for m in found.models if m["id"] == wanted), None)
    if row is None and not wanted and len(found.models) == 1:
        row = found.models[0]
        out["model"] = row["id"]
    window = (row or {}).get("context_window") or found.context_window
    if window:
        out["context_window"] = window
    for name in ("tools", "thinking"):
        if out.get(name) is None and row is not None and isinstance(row.get(name), bool):
            out[name] = row[name]
    return out, found


# ----- worker protocol (section 28) ----------------------------------------------------------
def handle(request: dict, emit) -> threading.Thread | None:
    """Answer one of ``TYPES``. ``local_probe`` runs on its own thread so the protocol loop never
    waits on a socket; exactly one event follows every request."""
    kind, request_id = request.get("type"), request.get("id")
    if kind == "local_endpoints":
        emit({"event": "local_endpoints", "id": request_id,
              "items": [e.to_dict() for e in catalog().values()]})
    elif kind == "local_endpoint_delete":
        endpoint_id = request.get("endpoint_id")
        emit({"event": "local_endpoint_deleted", "id": request_id, "endpoint_id": endpoint_id,
              "removed": delete(endpoint_id)})
    elif kind == "local_probe":
        base_url = request.get("base_url")

        def work():
            emit({"event": "local_probed", "id": request_id, **probe(base_url).to_dict()})

        thread = threading.Thread(target=work, name="relay-local-probe", daemon=True)
        thread.start()
        return thread
    elif kind == "local_endpoint_save":
        spec = request.get("endpoint")
        if not isinstance(spec, dict):
            raise ValueError("endpoint must be an object.")
        if not request.get("detect"):
            endpoint = save(spec)
            emit({"event": "local_endpoint_saved", "id": request_id, "endpoint": endpoint.to_dict()})
            return None

        def work():
            try:
                filled, found = detect(spec, wait=DETECT_WAIT_S)
                if not found.ok:
                    raise ValueError(found.error or "Nothing to detect at that address.")
                endpoint = save(filled)
                emit({"event": "local_endpoint_saved", "id": request_id, "endpoint": endpoint.to_dict(),
                      "probe": found.to_dict()})
            except ValueError as exc:
                emit({"event": "error", "id": request_id, "text": str(exc)[:2000]})

        thread = threading.Thread(target=work, name="relay-local-save", daemon=True)
        thread.start()
        return thread
    else:
        raise ValueError("Unknown local-model request.")
    return None
