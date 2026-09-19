# SPDX-License-Identifier: GPL-3.0-or-later
"""The worker side of Tier A: a guest harness standing in for the chat provider (protocol 29.3).

`guest_harness.py` is the contract and `guest_harness_claude` / `guest_harness_codex` are the two
adapters. This module is what the worker holds instead of a `ChatProvider`, so that everything
above it — `Agent`, the turn record, the request ledger, the queue, the sessions index — is the
ordinary machinery and never learns that the model is a whole other agent:

* `HarnessProvider` satisfies the provider surface `Agent` uses (`complete`, `cancel`, `config`,
  `set_stall_timeout`, `stall_timeout`, `response_open`). `complete()` takes the last user message,
  runs **one** harness turn, translates each `HarnessEvent` into the Relay event of the 29.1 table,
  and returns the guest's final text as the assistant message with no tool calls, so the Agent's
  turn loop ends the turn on it exactly as it ends a plain answer.
* A guest is a **preset**: `preset_rows()` is what the worker's `presets` answer carries, and
  `config_for_preset()` is what `session_protocol.provider_config` returns for `guest:<id>`.
* `start_provider()` builds the adapter (lazily imported, so a missing or half-written one is
  "not available" rather than an import error at worker start) and starts it.

Nothing here spawns anything at import time, and no test may start a real guest: the seam is
`make_harness`, which tests replace with `tests/guest_harness_fake.FakeHarness`.

Protocol: docs/AGENT-SESSIONS-PROTOCOL.md section 29.3.
"""
from __future__ import annotations

import base64
import importlib
import json
import shutil
import subprocess
import threading
import time
import uuid

from . import guest, logs, questions as questions_mod, tool_labels
from .guest_harness import (HARNESS_GUESTS, HarnessError, HarnessNotAvailable, HarnessEvent,
                            TOOL_NAMES, map_tool_name, validate_effort, validate_permissions)
from .provider import (DEFAULT_STALL_TIMEOUT, Cancelled, ProviderConfig, ProviderError,
                       message_images)

_log = logs.get("guest_harness")

# A `presets` row's id, and the scheme its ProviderConfig carries so nothing downstream mistakes a
# guest for an HTTP endpoint (29.3).
PRESET_PREFIX = "guest:"
BASE_SCHEME = "harness://"

# The adapter module for each guest, imported only when one is actually wanted.
_ADAPTERS = {"claude": ("relay_core.guest_harness_claude", "ClaudeHarness"),
             "codex": ("relay_core.guest_harness_codex", "CodexHarness")}

# Where a guest's own tool input keeps the things protocol 23's labels are built from. The guests
# spell them differently (`file_path`, `abs_path`, `cmd`), and the label module only reads Relay's.
_COMMAND_KEYS = ("command", "cmd", "command_line", "argv")
_PATH_KEYS = ("path", "file_path", "filePath", "abs_path", "notebook_path", "notebookPath", "file")

_PREVIEW_TITLES = {"run_command": "RUN COMMAND", "read_file": "READ FILE", "write_file": "WRITE FILE",
                   "edit_file": "EDIT FILE", "list_directory": "LIST DIRECTORY", "search": "SEARCH",
                   "web": "WEB", "agent": "SUBAGENT"}

MAX_OUTPUT_CHARS = 200_000     # one tool result kept in the turn record; the fold shows this much


# ----- the preset ------------------------------------------------------------------------------


def preset_guest_id(preset_id) -> str | None:
    """The guest a `guest:<id>` preset names, or None for anything else."""
    if not isinstance(preset_id, str) or not preset_id.startswith(PRESET_PREFIX):
        return None
    name = preset_id[len(PRESET_PREFIX):]
    return name if name in HARNESS_GUESTS else None


def is_guest_preset(preset_id) -> bool:
    return preset_guest_id(preset_id) is not None


def base_url(guest_id: str) -> str:
    return BASE_SCHEME + guest_id


def config_guest_id(config) -> str | None:
    """The guest a ProviderConfig names, or None. The one test for "this pane is on a guest"."""
    url = getattr(config, "base_url", "")
    if not isinstance(url, str) or not url.startswith(BASE_SCHEME):
        return None
    name = url[len(BASE_SCHEME):]
    return name if name in HARNESS_GUESTS else None


def config_for_preset(preset_id: str, request: dict) -> ProviderConfig:
    """The ProviderConfig for `configure`/`set_model` naming a guest preset.

    Deliberately **not** `config.validate()`d: that method's whole subject is an HTTP endpoint with
    a key, and `harness://claude` is neither (no scheme it allows, no key, and the model is empty
    until the guest says what it is running). The two fields that do matter are checked here.
    """
    guest_id = preset_guest_id(preset_id)
    if guest_id is None:
        raise ValueError(f"Unknown guest preset {preset_id!r}.")
    options = guest_options(request.get("guest"))
    model = options["model"] or ""
    config = ProviderConfig(base_url(guest_id), model, "", {}, _guest_max_tokens())
    return config


def _guest_max_tokens() -> int:
    """An output budget for the context arithmetic only; nothing is ever sent with it. The guest
    decides its own reply length, so this is just what `ContextTracker` holds back."""
    from .provider import MIN_OUTPUT_TOKENS
    return max(MIN_OUTPUT_TOKENS, 32_768)


def guest_options(raw) -> dict:
    """The `guest` block of a configure/set_model request, validated (29.3)."""
    if raw is None:
        raw = {}
    if not isinstance(raw, dict):
        raise ValueError("guest must be an object.")
    unknown = set(raw) - {"model", "resume", "fork", "permissions", "effort"}
    if unknown:
        raise ValueError("guest may only carry model, resume, fork, permissions and effort.")
    model = raw.get("model")
    if model is not None and (not isinstance(model, str) or len(model) > 200):
        raise ValueError("guest.model must be text.")
    resume = raw.get("resume")
    if resume is not None and (not isinstance(resume, str) or not resume.strip() or len(resume) > 200):
        raise ValueError("guest.resume must be the guest's own session id.")
    fork = raw.get("fork", False)
    if type(fork) is not bool:
        raise ValueError("guest.fork must be true or false.")
    return {"model": (model or "").strip(), "resume": (resume or "").strip() or None, "fork": fork,
            "permissions": validate_permissions(raw.get("permissions")),
            # The guest's own levels, not Relay's four: `validate_effort` only checks the shape
            # and the guest decides whether it has that one (29.3, owner 2026-09-19).
            "effort": validate_effort(raw.get("effort"))}


# ----- what this machine has -------------------------------------------------------------------

_detected: dict | None = None
_adapter_cache: dict = {}
_detect_lock = threading.Lock()


def installations(refresh: bool = False) -> dict:
    """`{guest_id: {"installed", "binary", "version"}}`, resolved once per worker process.

    `shutil.which` only. `guest.detect_installations` also runs `<binary> --version`, which is a
    subprocess with a five-second timeout *per guest* — and the `presets` request is answered on the
    worker's protocol thread, so two uninstallable-but-present binaries would freeze the pane for
    ten seconds every time the model box opened. `version` is therefore always "" here; nothing in
    29.3 reads it, and the adapters record the versions they were verified against.
    """
    global _detected
    with _detect_lock:
        if _detected is None or refresh:
            found = {}
            for guest_id in HARNESS_GUESTS:
                binary = None
                for name in guest.spec(guest_id).binaries:
                    binary = shutil.which(name)
                    if binary:
                        break
                found[guest_id] = {"installed": bool(binary), "binary": binary or "", "version": ""}
            _detected = found
        return dict(_detected)


def adapter_available(guest_id: str, refresh: bool = False) -> bool:
    """Whether this guest's adapter module can be imported here. Cached per process; an adapter
    that is missing, or that is half-written by another session and does not import, is simply
    "not available" and the GUI falls back to the Tier B launch (29.4)."""
    if guest_id not in _ADAPTERS:
        return False
    if refresh:
        _adapter_cache.pop(guest_id, None)
    if guest_id not in _adapter_cache:
        _adapter_cache[guest_id] = _load_adapter(guest_id) is not None
    return _adapter_cache[guest_id]


def _load_adapter(guest_id: str):
    module_name, class_name = _ADAPTERS[guest_id]
    try:
        module = importlib.import_module(module_name)
    except Exception as exc:                      # ImportError, SyntaxError while it is being written
        _log.debug("guest harness adapter %s is not importable: %s", module_name, exc)
        return None
    return getattr(module, class_name, None)


# ----- what a guest can be set to (the model box's rows, 29.3) -----------------------------------
#
# Owner, 2026-09-19: "the options menu doesnt have settings for claude and codex yet. you should be
# able to pick the model and reasoning effort for those." A `presets` row therefore carries the same
# two fields every other row does — `efforts` for the pane's `/effort` control and the effort
# shortcuts, `effort_note` for the line under them — plus `models`, which only a guest has, because
# a guest's models are not one of Relay's presets and cannot be matched to one.

# Claude Code's, from `claude --help` (2.1.278); the adapter is the one source for both.
_CLAUDE_MODELS = None            # filled on first use from guest_harness_claude, without importing
                                 # the module at import time (the adapter may be half-written)

# Codex's catalogue is a subprocess (`codex debug models`, ~0.4 MB of JSON), so it is read **once
# per worker process, in a background thread**. The `presets` request is answered on the protocol
# thread and may never wait for it: until the read lands, `models` is [] and `efforts` is every
# level 0.155.1's models name between them.
CODEX_CATALOG_TIMEOUT = 20.0
_CODEX_EFFORTS = ("low", "medium", "high", "xhigh", "max", "ultra")

_catalog: dict[str, list[dict]] = {}
_catalog_started: set = set()
_catalog_lock = threading.Lock()
# Set once the codex scan has finished, whatever it found. Tests wait on it; nothing else does.
catalog_ready = threading.Event()


def _read_codex_catalog(binary: str) -> list[dict]:
    """`codex debug models` as the contract's model rows. The seam tests replace."""
    from .guest_harness_codex import catalog_rows
    out = subprocess.run([binary, "debug", "models"], capture_output=True, text=True,
                         stdin=subprocess.DEVNULL, timeout=CODEX_CATALOG_TIMEOUT, check=False)
    if out.returncode != 0:
        raise RuntimeError((out.stderr or "").strip()[:200] or "codex debug models failed")
    return catalog_rows(json.loads(out.stdout).get("models") or [])


def start_catalog_scan(guest_id: str = "codex") -> None:
    """Read this guest's catalogue behind the request, once per worker process. Never blocks."""
    if guest_id != "codex":
        return
    with _catalog_lock:
        if guest_id in _catalog_started:
            return
        _catalog_started.add(guest_id)
    binary = (installations().get(guest_id) or {}).get("binary") or ""
    if not binary:
        with _catalog_lock:
            _catalog[guest_id] = []
        catalog_ready.set()
        return

    def scan():
        rows: list[dict] = []
        try:
            rows = _read_codex_catalog(binary)
        except Exception as exc:              # a missing, refusing or slow codex is simply no menu
            _log.debug("codex catalogue could not be read: %s", exc)
        with _catalog_lock:
            _catalog[guest_id] = rows
        catalog_ready.set()

    threading.Thread(target=scan, name="relay-codex-models", daemon=True).start()


def reset_catalog() -> None:
    """Forget the catalogue and the fact that it was read (tests, and a `presets` refresh)."""
    with _catalog_lock:
        _catalog.clear()
        _catalog_started.clear()
    catalog_ready.clear()


def guest_models(guest_id: str) -> list[dict]:
    """What this guest can be set to, for the row's `models` — `[]` when it cannot be said here."""
    if guest_id == "claude":
        global _CLAUDE_MODELS
        if _CLAUDE_MODELS is None:
            adapter = _load_adapter("claude")
            _CLAUDE_MODELS = adapter().models() if adapter is not None else []
        return [dict(row) for row in _CLAUDE_MODELS]
    start_catalog_scan(guest_id)
    with _catalog_lock:
        return [dict(row) for row in _catalog.get(guest_id) or ()]


def guest_efforts(guest_id: str, models: list[dict] | None = None) -> list[str]:
    """The levels the pane's effort control offers for this guest.

    Claude's five are fixed. Codex's are the first model the catalogue lists, which is the one
    codex defaults to (`codex debug models` is ordered by its own `priority`, and `model/list`
    puts `isDefault` first); until the catalogue is read, or when there is none, the union of
    everything 0.155.1's models name, so the control is never empty.
    """
    if guest_id == "claude":
        rows = models if models is not None else guest_models("claude")
        return list(rows[0]["efforts"]) if rows else []
    rows = models if models is not None else guest_models(guest_id)
    for row in rows:
        if row.get("efforts"):
            return list(row["efforts"])
    return list(_CODEX_EFFORTS)


def preset_rows() -> list[dict]:
    """One `presets` row per guest the registry knows (29.3). `harness` is what the GUI decides by:
    true means picking the row configures this pane's agent, false means the Tier B launch."""
    found = installations()
    rows = []
    for guest_id in HARNESS_GUESTS:
        state = found.get(guest_id) or {"installed": False, "binary": "", "version": ""}
        models = guest_models(guest_id) if state["installed"] else []
        rows.append({"id": PRESET_PREFIX + guest_id, "label": guest.spec(guest_id).name,
                     "guest": guest_id,
                     "harness": bool(state["installed"] and adapter_available(guest_id)),
                     "installed": state["installed"], "binary": state["binary"],
                     "version": state["version"], "group": "guest",
                     "has_stored_key": False, "key_source": "guest",
                     "model": "", "base_url": base_url(guest_id),
                     "local": False, "hosted": False,
                     # The guest's own levels and its own models (29.3). `effort_note` is the
                     # line a provider uses to explain the levels it has *not* got; a guest's
                     # list is its own and leaves nothing out, so there is nothing to say.
                     "efforts": guest_efforts(guest_id, models) if state["installed"] else [],
                     "effort_note": "", "models": models})
    return rows


# ----- starting one ------------------------------------------------------------------------------


def make_harness(guest_id: str):
    """The adapter instance for a guest. The single seam tests replace, and the only place either
    adapter module is imported."""
    if guest_id not in _ADAPTERS:
        raise HarnessNotAvailable(f"Relay has no harness for {guest_id!r}.")
    factory = _load_adapter(guest_id)
    if factory is None:
        raise HarnessNotAvailable(
            f"{guest.spec(guest_id).name}'s harness is not available in this build of Relay.")
    return factory()


def start_provider(preset_id: str, request: dict, workspace: str,
                   stall_timeout: float = DEFAULT_STALL_TIMEOUT,
                   config: ProviderConfig | None = None) -> "HarnessProvider":
    """Build and start the harness a `configure`/`set_model` asked for, and wrap it as a provider.

    Every failure is a `ValueError`, which is what the worker's protocol loop already turns into an
    `error` event with the text in it — 29.3's "a guest that cannot start is `error` on the
    `configure`", with the pane left on the model it had.
    """
    guest_id = preset_guest_id(preset_id)
    if guest_id is None:
        raise ValueError(f"Unknown guest preset {preset_id!r}.")
    options = guest_options(request.get("guest"))
    harness = make_harness(guest_id)
    try:
        started = harness.start(cwd=workspace, model=options["model"] or None,
                                resume=options["resume"], fork=options["fork"],
                                permissions=options["permissions"], effort=options["effort"])
    except HarnessError as exc:
        _close_quietly(harness)
        raise ValueError(str(exc) or f"{guest.spec(guest_id).name} could not be started.") from None
    except Exception as exc:
        _close_quietly(harness)
        raise ValueError(f"{guest.spec(guest_id).name} could not be started "
                         f"({type(exc).__name__}).") from None
    # The caller's own config object when it has one, filled in rather than replaced: the worker
    # hands the same object to the role resolver before the guest is started, and the model is only
    # known once it has answered.
    config = config if config is not None else config_for_preset(preset_id, request)
    config.model = started.model or options["model"] or guest_id
    provider = HarnessProvider(config, harness, guest_id, stall_timeout=stall_timeout)
    provider.session_id = started.session_id or ""
    provider.permissions = options["permissions"]
    provider.effort = _harness_effort(harness) or options["effort"] or ""
    logs.event(_log, "guest_harness_started", guest=guest_id, model=config.model,
               resumed=bool(options["resume"]), fork=options["fork"],
               permissions=options["permissions"], effort=provider.effort)
    return provider


def _harness_effort(harness) -> str:
    """The level the guest says it is on, when it says (codex answers `thread/start` with it;
    claude never reports one, so the pane's own request stands)."""
    value = getattr(harness, "effort", "")
    return value if isinstance(value, str) else ""


def _close_quietly(harness) -> None:
    try:
        harness.close()
    except Exception:                               # a failed start must not fail twice
        pass


# ----- the provider -------------------------------------------------------------------------------


class HarnessProvider:
    """One guest harness, wearing the surface `Agent` calls on `self.provider`.

    Threading follows the contract: `complete()` runs on the turn thread; `cancel()` and
    `answer()` come from the worker's protocol thread while it blocks.
    """

    # The Agent's side calls (titles, summaries, compaction, route assist) are not served here:
    # each would spend a guest turn. `Agent.side_provider` reads this and uses a role of its own
    # for the job when one is configured, and `Agent._maybe_compact` skips automatic compaction
    # when none is (protocol 29.3).
    serves_side_calls = False

    def __init__(self, config: ProviderConfig, harness, guest_id: str,
                 stall_timeout: float = DEFAULT_STALL_TIMEOUT):
        self.config = config
        self.harness = harness
        self.guest_id = guest_id
        self.session_id = getattr(harness, "session_id", "") or ""
        # The posture this pane started the guest with, so a resume starts the replacement the same
        # way rather than silently dropping back to the default.
        self.permissions = "bypass"
        # The reasoning effort in force, as the guest names it. "" means the guest's own default
        # (29.3): Relay's `agent.effort` is its four-level scale for its own providers and stays
        # out of this, because a guest's levels are the guest's (xhigh, ultra).
        self.effort = ""
        self._stall_timeout = float(stall_timeout)
        self._agent = None
        self._asker = _Asker()
        self._closed = False

    # ----- the Agent's surface --------------------------------------------------------------
    @property
    def stall_timeout(self) -> float:
        """Reported for the log line only. A guest turn is not a streamed HTTP body and has no idle
        deadline of Relay's: the harness owns its own process and its own liveness."""
        return self._stall_timeout

    def set_stall_timeout(self, seconds) -> float:
        self._stall_timeout = float(seconds)
        return self._stall_timeout

    def response_open(self) -> bool:
        """Socket hygiene (`Agent._ensure_no_open_response`): a harness holds a process, not a
        response, and the process is meant to outlive the turn."""
        return False

    def cancel(self) -> None:
        """`Agent.stop()` → `provider.cancel()`. For a guest that is `interrupt()` (29.3)."""
        try:
            self.harness.interrupt()
        except HarnessError as exc:
            _log.debug("guest harness interrupt failed: %s", exc)
        self._asker.fail_pending()

    def close(self) -> None:
        """End the guest process. Idempotent; called when the pane leaves this preset."""
        if self._closed:
            return
        self._closed = True
        self._asker.fail_pending()
        _close_quietly(self.harness)
        logs.event(_log, "guest_harness_closed", guest=self.guest_id)

    def compact(self) -> None:
        """`compact` asks the guest to compact its own context as well (29.3)."""
        try:
            self.harness.compact()
        except HarnessError as exc:
            _log.debug("guest harness compact failed: %s", exc)

    def bind(self, agent) -> None:
        """Remember the pane's Agent, for the turn id the events carry, for the per-call records
        the fold and `tool_output` read, and to tell a pane turn from a side call."""
        self._agent = agent

    # ----- one turn ---------------------------------------------------------------------------
    def complete(self, messages: list[dict], tools: list[dict], emit, cancel: threading.Event) -> dict:
        if cancel.is_set():
            raise Cancelled("Stopped.")
        agent = self._agent
        if agent is not None and messages is not getattr(agent, "messages", None):
            # A side call: a pane title, a session summary, compaction's summary, a recap,
            # route_assist. `Agent.side_provider()` hands an injected provider straight back, so
            # these would each spend a guest turn on a chore. They get nothing instead; every
            # caller already reads "" as "no title / no summary / nothing compacted".
            return {"role": "assistant", "content": ""}
        prompt, attachments = last_user_message(messages)
        if not prompt and not attachments:
            raise ProviderError("There is nothing to send to the guest: the last message has no text.")
        record = getattr(agent, "_turn_record", None) if agent is not None else None
        turn = _Turn(self, agent, record, emit, cancel)
        try:
            result = self.harness.send(prompt, attachments=attachments or None,
                                       emit=turn.on_event, cancel=cancel)
        except Cancelled:
            raise
        except HarnessError as exc:
            turn.close_thinking()
            raise ProviderError(str(exc) or turn.error_text
                                or f"{guest.spec(self.guest_id).name} ended the turn with an error.") from None
        except Exception as exc:
            turn.close_thinking()
            raise ProviderError(f"{guest.spec(self.guest_id).name}'s harness failed "
                                f"({type(exc).__name__}).") from None
        turn.close_thinking()
        stop_reason = getattr(result, "stop_reason", "end")
        if stop_reason == "interrupted" or cancel.is_set():
            # Same end as a stopped HTTP turn: the Agent records it as cancelled and keeps the
            # unfinished request open. What was streamed is already on the user's screen.
            raise Cancelled("Stopped.")
        if stop_reason == "error":
            raise ProviderError(getattr(result, "text", "") or turn.error_text
                                or f"{guest.spec(self.guest_id).name} ended the turn with an error.")
        turn.finish(getattr(result, "usage", None) or {})
        return {"role": "assistant", "content": getattr(result, "text", "") or ""}

    # ----- the question round trip ------------------------------------------------------------
    def resolve_question(self, message: dict) -> bool:
        """The pane's `question_answer` for a card this provider put up. False when the id is not
        one of ours, which is the worker's signal to hand it to the agent's own `ask_user`."""
        return self._asker.resolve(message)


def answer_question(provider, message: dict) -> bool:
    """Route a `question_answer` to a guest harness provider, if that is where it belongs."""
    resolve = getattr(provider, "resolve_question", None)
    return bool(callable(resolve) and resolve(message))


# ----- event translation (the 29.1 table) ----------------------------------------------------------


class _Turn:
    """One `send()`: harness events in, Relay events out, and the turn record kept honest."""

    def __init__(self, provider: HarnessProvider, agent, record, emit, cancel: threading.Event):
        self.provider = provider
        self.agent = agent
        self.record = record
        self.emit = emit
        self.cancel = cancel
        self.turn_id = record.get("turn_id") if isinstance(record, dict) else None
        self.calls: dict[str, dict] = {}
        self.usage_seen = False
        self.error_text = ""
        self._thinking_started = None
        self._thinking_chars = 0

    # ----- dispatch -----------------------------------------------------------------------
    def on_event(self, event: HarnessEvent) -> None:
        handler = getattr(self, "_on_" + event.kind, None)
        if handler is None:
            return
        try:
            handler(event.data if isinstance(event.data, dict) else {})
        except Exception:                            # a malformed event never fails a turn
            _log.debug("guest harness event %s could not be translated", event.kind, exc_info=True)

    # ----- text ---------------------------------------------------------------------------
    def _on_started(self, data: dict) -> None:
        session_id = str(data.get("session_id") or "")
        model = str(data.get("model") or "")
        if session_id:
            self.provider.session_id = session_id
        if model and model != self.provider.config.model:
            self.provider.config.model = model
            if self.agent is not None:
                self.agent.config.model = model
            self.emit({"event": "model_changed", "model": model, "applies": "now",
                       "preset": PRESET_PREFIX + self.provider.guest_id,
                       "guest": self.provider.guest_id,
                       "guest_session": self.provider.session_id})

    def _on_delta(self, data: dict) -> None:
        text = data.get("text")
        if not isinstance(text, str) or not text:
            return
        self.close_thinking()
        self.emit({"event": "delta", "text": text})

    def _on_thinking(self, data: dict) -> None:
        text = data.get("text")
        if not isinstance(text, str) or not text:
            return
        if self._thinking_started is None:
            self._thinking_started = time.monotonic()
        self._thinking_chars += len(text)
        self.emit({"event": "thinking_delta", "text": text})

    def close_thinking(self) -> None:
        if self._thinking_started is None:
            return
        elapsed = int((time.monotonic() - self._thinking_started) * 1000)
        chars = self._thinking_chars
        self._thinking_started, self._thinking_chars = None, 0
        self.emit({"event": "thinking_done", "elapsed_ms": elapsed, "chars": chars})

    def _on_notice(self, data: dict) -> None:
        text = data.get("text")
        if isinstance(text, str) and text.strip():
            self.emit({"event": "status", "text": text.strip()[:500]})

    def _on_error(self, data: dict) -> None:
        # Held, not emitted. The harness raises (or ends "error") right after, and the Agent emits
        # the turn's one `error` event with this text; emitting here as well drew it twice.
        text = data.get("text")
        if isinstance(text, str) and text.strip():
            self.error_text = text.strip()[:2000]

    # ----- usage ---------------------------------------------------------------------------
    def _on_usage(self, data: dict) -> None:
        usage = relay_usage(data)
        if not usage:
            return
        self.usage_seen = True
        self.emit({"event": "usage", "usage": usage})

    def finish(self, usage: dict) -> None:
        """The turn ended well. The usage the harness returned goes out only when it never sent a
        `usage` event, so nothing is counted twice; the Agent turns it into the `context` event."""
        if self.usage_seen:
            return
        mapped = relay_usage(usage)
        if mapped:
            self.emit({"event": "usage", "usage": mapped})

    # ----- tool calls -----------------------------------------------------------------------
    def _on_tool_started(self, data: dict) -> None:
        self.close_thinking()
        call_id = str(data.get("call_id") or "") or "guest-" + uuid.uuid4().hex[:12]
        name, guest_tool = self._tool_name(data)
        args = label_arguments(data.get("input"))
        preview = tool_preview(name, guest_tool, args, data.get("label"))
        self.calls[call_id] = {"name": name, "guest_tool": guest_tool, "args": args,
                               "preview": preview, "started": time.monotonic()}
        event = {"event": "tool_started", "tool": name, "preview": preview,
                 "label": tool_labels.started_label(_label_name(name, guest_tool), args),
                 "call_id": call_id}
        if self.turn_id is not None:
            event["turn_id"] = self.turn_id
        self.emit(event)

    def _on_tool_result(self, data: dict) -> None:
        call_id = str(data.get("call_id") or "")
        call = self.calls.pop(call_id, None)
        if call is None:
            name, guest_tool = self._tool_name(data)
            call = {"name": name, "guest_tool": guest_tool, "args": {}, "preview": "",
                    "started": None}
            call_id = call_id or "guest-" + uuid.uuid4().hex[:12]
        ok = data.get("ok") is not False
        diff = data.get("diff") if isinstance(data.get("diff"), str) and data["diff"].strip() else ""
        result = tool_result(data.get("output"), ok, diff)
        ms = data.get("ms")
        if not isinstance(ms, int) or isinstance(ms, bool) or ms < 0:
            ms = int((time.monotonic() - call["started"]) * 1000) if call["started"] else None
        label = tool_labels.result_label(_label_name(call["name"], call["guest_tool"]),
                                         call["args"], result, ms=ms)
        # One record per call, exactly as `Agent._record_tool` writes for Relay's own: it is what
        # `turn_summary`, `tool_output` and the diff pane read (29.3, "what the transcript holds").
        if self.agent is not None and isinstance(self.record, dict):
            self.agent._record_tool(self.record, call_id, call["name"], call["preview"], result,
                                    ms, label=label, args=call["args"], diff=diff or None)
        event = {"event": "tool_result", "tool": call["name"], "result": result, "label": label,
                 "ms": ms, "call_id": call_id}
        if self.turn_id is not None:
            event["turn_id"] = self.turn_id
        if diff:
            event["diff"] = diff
        self.emit(event)

    def _tool_name(self, data: dict) -> tuple[str, str]:
        """(Relay's name for this tool, the guest's own). The adapters already map, so this only
        maps again when a name arrives unmapped, and never loses the original."""
        raw = str(data.get("tool") or "")
        source = data.get("input") if isinstance(data.get("input"), dict) else {}
        guest_tool = str(source.get("_guest_tool") or "") or (raw if raw not in TOOL_NAMES else "")
        name = raw if raw in TOOL_NAMES else map_tool_name(self.provider.guest_id, raw)
        return name, guest_tool

    # ----- questions and approvals -----------------------------------------------------------
    def _on_approval(self, data: dict) -> None:
        """An approval the guest raised under `permissions: "ask"`: a protocol 27 card with two
        options, and the answer goes back through `harness.answer` (29.3)."""
        request_id = str(data.get("id") or "")
        card = approval_card(str(data.get("kind") or "other"), data.get("detail"))
        answers = self.provider._asker.ask(card, self.turn_id, self.emit, self.cancel)
        allowed = bool(answers and answers[0]
                       and str(answers[0][0]).strip().casefold() == "allow")
        decision = {"behavior": "allow" if allowed else "deny"}
        if not allowed:
            decision["message"] = ("The user did not allow this." if answers is not None
                                   else "The turn was stopped.")
        self._answer(request_id, decision)

    def _on_question(self, data: dict) -> None:
        request_id = str(data.get("id") or "")
        card = clean_questions(data.get("questions"))
        answers = self.provider._asker.ask(card, self.turn_id, self.emit, self.cancel)
        self._answer(request_id, {"answers": answers if answers is not None else []})

    def _answer(self, request_id: str, decision: dict) -> None:
        try:
            self.provider.harness.answer(request_id, decision)
        except HarnessError as exc:
            _log.debug("guest harness answer failed: %s", exc)


def approval_card(kind: str, detail) -> list[dict]:
    """One approval as a protocol 27 card: what the guest wants to do, Allow or Deny.

    Through `questions.validate` like any other card, so the pane is handed exactly the shape it
    already draws (whitespace collapsed, every field capped) and a guest that sends something odd
    gets a plain card rather than a failed turn.
    """
    header = {"command": "Run command", "patch": "Apply edit", "tool": "Use tool"}.get(kind, "Approve")
    text = " ".join(str(detail or "").replace("\x00", " ").split()) or "Let the guest do this?"
    card = [{"header": header, "question": text[:questions_mod.MAX_QUESTION],
             "options": [{"label": "Allow", "description": "Let it go ahead."},
                         {"label": "Deny", "description": "Refuse this one and let it carry on."}],
             "multiple": False}]
    try:
        return questions_mod.validate({"questions": card})
    except ValueError:                              # pragma: no cover - cleaned above already
        card[0]["question"] = "Let the guest do this?"
        return questions_mod.validate({"questions": card})


# ----- the round trip a card needs -------------------------------------------------------------


class _Asker:
    """The `question` → `question_answer` round trip for a guest's cards.

    Its own, rather than the agent's `executor.questions`: that one collapses each answer to the
    text a model reads, and protocol 27.3 wants the raw per-question lists back so they can be
    handed to `harness.answer`. It has no per-turn ask cap either — Relay's cap exists to stop a
    *model* interviewing the user, and a guest's approvals are governed by its permission mode.
    """

    def __init__(self):
        self._lock = threading.Lock()
        self._pending: dict[str, list] = {}
        self._hooked: set[int] = set()

    def ask(self, items: list[dict], turn_id, emit, cancel: threading.Event) -> list | None:
        """Draw the card and block until the pane answers. Returns the per-question answer lists,
        or None when the turn was stopped under it."""
        hooked = self._hook(cancel)
        if cancel.is_set():
            return None
        call_id = "q-" + uuid.uuid4().hex
        done = threading.Event()
        with self._lock:
            self._pending[call_id] = [done, None]
        event = {"event": "question", "id": call_id, "questions": items}
        if turn_id is not None:
            event["turn_id"] = turn_id
        emit(event)
        if cancel.is_set():
            self.fail_pending()
        if hooked:
            done.wait()
        else:                                        # pragma: no cover - see questions.wake_on_set
            while not done.wait(0.25):
                if cancel.is_set():
                    self.fail_pending()
        with self._lock:
            slot = self._pending.pop(call_id, None)
        reply = slot[1] if slot else None
        if not isinstance(reply, dict) or reply.get("code") == "cancelled":
            emit({"event": "question_closed", "id": call_id, "reason": "cancelled"})
            return None
        answers = reply.get("answers")
        return answers if isinstance(answers, list) else []

    def resolve(self, message: dict) -> bool:
        if not isinstance(message, dict):
            return False
        call_id = message.get("id")
        with self._lock:
            slot = self._pending.get(call_id)
            if slot is None:
                return False
            slot[1] = {k: v for k, v in message.items() if k not in ("type", "id")}
            slot[0].set()
        return True

    def fail_pending(self) -> None:
        with self._lock:
            slots = list(self._pending.values())
        for slot in slots:
            if slot[1] is None:
                slot[1] = {"code": "cancelled"}
            slot[0].set()

    def _hook(self, cancel: threading.Event) -> bool:
        """Wake every waiting card when Stop is pressed, once per cancel event."""
        key = id(cancel)
        if key in self._hooked:
            return True
        if questions_mod.wake_on_set(cancel, self.fail_pending):
            self._hooked.add(key)
            return True
        return False


def clean_questions(raw) -> list[dict]:
    """A guest's question list as protocol 27 draws it.

    The guests spell their own question tool differently (`multiSelect`, extra fields, a missing
    header), and `questions.validate` is strict because a *model* is its caller and is shown the
    error. Nothing is shown to a guest, so the fields are coerced here and an item that still will
    not validate becomes one plain open question rather than a failed turn.
    """
    items = raw if isinstance(raw, list) else []
    built = []
    for item in items[:questions_mod.MAX_QUESTIONS]:
        if not isinstance(item, dict):
            continue
        question = str(item.get("question") or item.get("prompt") or "").strip()
        if not question:
            continue
        header = str(item.get("header") or item.get("title") or "Question").strip()
        entry = {"header": header[:questions_mod.MAX_HEADER],
                 "question": question[:questions_mod.MAX_QUESTION]}
        options = []
        raw_options = item.get("options") if isinstance(item.get("options"), list) else []
        for option in raw_options[:questions_mod.MAX_OPTIONS]:
            if isinstance(option, str):
                option = {"label": option}
            if not isinstance(option, dict):
                continue
            label = str(option.get("label") or option.get("name") or "").strip()
            if not label or label.casefold() == questions_mod.CUSTOM_LABEL.casefold():
                continue
            options.append({"label": label[:questions_mod.MAX_LABEL],
                            "description": (str(option.get("description") or label).strip()
                                            or label)[:questions_mod.MAX_DESCRIPTION]})
        if len(options) >= questions_mod.MIN_OPTIONS:
            entry["options"] = options
            if item.get("multiSelect") or item.get("multiple"):
                entry["multiple"] = True
        built.append(entry)
    if not built:
        built = [{"header": "Question", "question": "The guest asked a question with no text."}]
    try:
        return questions_mod.validate({"questions": built})
    except ValueError:                              # pragma: no cover - coerced above already
        return questions_mod.validate({"questions": [{"header": "Question",
                                                      "question": "The guest asked a question."}]})


# ----- shapes ------------------------------------------------------------------------------------


def last_user_message(messages: list[dict]) -> tuple[str, list[dict]]:
    """The prompt the harness is sent: the last user message's text, and its images as the
    contract's `{"kind": "image", "media_type", "data"}` attachments.

    Relay puts images on a user message as OpenAI content parts with an inlined data URL
    (`provider.content_parts`), so the base64 is already there and is handed straight on.
    """
    for message in reversed(messages or []):
        if not isinstance(message, dict) or message.get("role") != "user":
            continue
        content = message.get("content")
        if isinstance(content, str):
            return content, []
        if not isinstance(content, list):
            return "", []
        text = "\n".join(part["text"] for part in content
                         if isinstance(part, dict) and part.get("type") == "text"
                         and isinstance(part.get("text"), str))
        return text, [part for part in (_attachment(p) for p in message_images(message)) if part]
    return "", []


def _attachment(part: dict) -> dict | None:
    url = (part.get("image_url") or {}).get("url")
    if not isinstance(url, str) or not url.startswith("data:"):
        return None
    head, _, data = url.partition(",")
    media_type = head[len("data:"):].split(";", 1)[0]
    if not media_type or not data:
        return None
    try:
        base64.b64decode(data, validate=True)
    except Exception:
        return None
    return {"kind": "image", "media_type": media_type, "data": data}


def relay_usage(data) -> dict:
    """A harness `usage` report in the shape `Agent._provider_emit` counts and `ContextTracker`
    measures: OpenAI's field names, which is what every other provider here reports."""
    if not isinstance(data, dict):
        return {}
    usage = {}
    for source, target in (("input_tokens", "prompt_tokens"), ("output_tokens", "completion_tokens")):
        value = data.get(source)
        if isinstance(value, int) and not isinstance(value, bool) and value >= 0:
            usage[target] = value
    if not usage:
        return {}
    usage["total_tokens"] = usage.get("prompt_tokens", 0) + usage.get("completion_tokens", 0)
    cost = data.get("cost_usd")
    if isinstance(cost, (int, float)) and not isinstance(cost, bool) and cost >= 0:
        usage["cost"] = float(cost)
    pct = data.get("context_pct")
    if isinstance(pct, (int, float)) and not isinstance(pct, bool):
        # The guest's own window, not Relay's. Carried for the pane; `sessions.add_usage` only
        # reads the four keys above, so it never reaches the session totals.
        usage["guest_context_pct"] = round(float(pct), 1)
    return usage


def label_arguments(source) -> dict:
    """The guest's tool input in the words `tool_labels` reads (protocol 23's concise line).

    Only the handful of keys a label is built from are translated; the rest of the guest's input is
    not copied, because it travels in the preview and the diff already and a tool input can be a
    whole file.
    """
    source = source if isinstance(source, dict) else {}
    args: dict = {}
    for key in _COMMAND_KEYS:
        value = source.get(key)
        if isinstance(value, str) and value.strip():
            args["command"] = value
            break
        if isinstance(value, list) and value:
            args["command"] = " ".join(str(item) for item in value)
            break
    for key in _PATH_KEYS:
        value = source.get(key)
        if isinstance(value, str) and value.strip():
            args["path"] = value
            break
    for key in ("pattern", "query", "url", "description", "subagent_type", "intent"):
        value = source.get(key)
        if isinstance(value, str) and value.strip():
            args[key] = value
    return args


def tool_preview(name: str, guest_tool: str, args: dict, label) -> str:
    """The block the pane prints above the call line, in the shape Relay's own tools use: a title
    line, a blank line, and the one thing the call is about."""
    title = _PREVIEW_TITLES.get(name) or (guest_tool or name).upper()
    body = (args.get("command") or args.get("path") or args.get("pattern") or args.get("query")
            or args.get("url") or args.get("description") or "")
    if not body and isinstance(label, str):
        body = label
    return f"{title}\n\n{body}" if body else title


def tool_result(output, ok: bool, diff: str = "") -> dict:
    """A harness tool result in the shape `tool_labels` and the fold read. `added`/`removed` come
    from the diff, because that is what decides inline-versus-diff-pane (protocol 23)."""
    text = output if isinstance(output, str) else ("" if output is None else str(output))
    text = text[:MAX_OUTPUT_CHARS]
    result: dict = {"output": text, "ok": bool(ok)}
    if not ok:
        result["error"] = (text.strip().splitlines() or ["The guest's tool failed."])[0][:500]
    if diff:
        added = sum(1 for line in diff.splitlines() if line.startswith("+") and not line.startswith("+++"))
        removed = sum(1 for line in diff.splitlines() if line.startswith("-") and not line.startswith("---"))
        result["added"], result["removed"] = added, removed
    return result


def _label_name(name: str, guest_tool: str) -> str:
    """Which name protocol 23's label is built from. Relay's, except for `other`, whose line would
    otherwise read "running other"; there the guest's own name is humanised instead (and an MCP
    tool's `server__tool` still gets the `external` kind it deserves)."""
    return guest_tool if name == "other" and guest_tool else name


# ----- the pane's Agent ----------------------------------------------------------------------------


def attach(agent, provider: HarnessProvider) -> None:
    """Wire a freshly built Agent to its harness provider.

    Two things the Agent cannot do for itself here:

    * the provider needs the Agent, for the `turn_id` its events carry and for the per-call records
      `tool_output` and `turn_transcript` read;
    * the session file has to say which guest and which guest session it is (29.3), and
      `Agent.session_data()` is a fixed dict this module may not edit — so it is wrapped, once, on
      this one instance. Unknown keys are ignored by `_apply_session`, so a session written this way
      loads anywhere.
    """
    provider.bind(agent)
    agent._guest_session_data = provider
    if getattr(agent, "_guest_session_data_wrapped", False):
        return                          # wrapped once per Agent; the line above re-points it
    agent._guest_session_data_wrapped = True
    original = agent.session_data

    def session_data():
        data = original()
        held = getattr(agent, "_guest_session_data", None)
        if held is not None:
            data["guest"] = held.guest_id
            data["guest_session"] = held.session_id
        return data

    agent.session_data = session_data


def detach(agent) -> HarnessProvider | None:
    """Take the pane off its guest: close the harness and let the Agent build providers again.

    `Agent._injected_provider` is what stops `set_model`, the vision/plan swap and failover from
    replacing a provider whose owner chose it. Clearing it here is the counterpart of `Agent(...,
    provider=…)` setting it: from this point the pane is an ordinary one again.
    """
    provider = agent_provider(agent)
    agent._guest_session_data = None
    if provider is None:
        return None
    agent._injected_provider = False
    provider.close()
    return provider


def switch_model(agent, guest_id: str | None, request: dict) -> HarnessProvider | None:
    """Reuse the harness this pane already has, when `set_model` only changes the guest's model.

    29.3 restarts the harness when the *preset* changes ("the Relay conversation is kept; the
    guest's context is not"). Asking the same guest for another model is not that: the contract has
    `set_model(model)` for it, the guest keeps everything it has read so far, and restarting would
    throw that away for nothing. Anything that names a `resume` or a `fork` is a different session
    and does restart. Returns None when the pane must start a fresh harness.
    """
    provider = agent_provider(agent)
    if guest_id is None or provider is None or provider.guest_id != guest_id:
        return None
    options = guest_options(request.get("guest"))
    if options["resume"] or options["fork"]:
        return None
    model = options["model"]
    if model and model != provider.config.model:
        try:
            named = provider.harness.set_model(model)
        except HarnessError as exc:
            raise ValueError(str(exc) or f"{guest.spec(guest_id).name} would not switch to {model}.") from None
        provider.config.model = named or model
    # A `guest` block may carry both (the model box's row and its effort are one choice); the
    # effort is applied after the model, because which levels a model has is the model's business.
    effort = options["effort"]
    if effort and effort != provider.effort:
        try:
            provider.effort = provider.harness.set_effort(effort) or effort
        except HarnessError as exc:
            raise ValueError(str(exc) or
                             f"{guest.spec(guest_id).name} would not take {effort}.") from None
    return provider


def agent_provider(agent) -> HarnessProvider | None:
    """The live harness this pane's agent runs on, or None.

    A closed one does not count: `detach()` ends the guest process but cannot take the provider off
    the Agent (it always holds one), and the `set_model` that follows is what replaces it. Until
    then the pane is no longer on a guest and must not be reported as being on one.
    """
    provider = getattr(agent, "provider", None)
    if not isinstance(provider, HarnessProvider) or provider._closed:
        return None
    return provider


def configured_fields(agent) -> dict:
    """What a guest pane adds to `configured` and `model_changed` (29.3); empty for a normal pane.

    `guest_effort` and not `effort`: the event's `effort` is the pane's own level on Relay's
    four-level scale, and a guest's is the guest's own (`xhigh`, `ultra`), so the two travel
    side by side rather than one pretending to be the other.
    """
    provider = agent_provider(agent)
    if provider is None:
        return {}
    return {"guest": provider.guest_id, "guest_session": provider.session_id,
            "guest_effort": provider.effort}


def set_effort(agent, effort) -> dict | None:
    """The pane's `set_effort` when its agent is a guest (29.3), or None when it is not.

    The guest's own knob, not a provider parameter: a guest turn is a process on a pipe and there
    is no request body to put a `reasoning_effort` in, so nothing is written to the
    `ProviderConfig`'s `extra`. Returns what `effort_changed` should carry.
    """
    provider = agent_provider(agent)
    if provider is None:
        return None
    level = validate_effort(effort)
    if level is None:
        raise ValueError(f"{guest.spec(provider.guest_id).name} needs a reasoning effort to set.")
    try:
        applied = provider.harness.set_effort(level)
    except HarnessError as exc:
        raise ValueError(str(exc) or
                         f"{guest.spec(provider.guest_id).name} would not take {level}.") from None
    provider.effort = applied or level
    logs.event(_log, "guest_harness_effort", guest=provider.guest_id, effort=provider.effort)
    # `applied` keeps the event's shape the same as every other `effort_changed`; there are no
    # provider parameters to name, because a guest turn is a process and not a request body.
    return {"guest": provider.guest_id, "guest_effort": provider.effort,
            "effort": provider.effort, "applied": {}}


def session_guest(data) -> tuple[str, str]:
    """(guest, guest session id) recorded in a saved Relay session, or ("", "")."""
    if not isinstance(data, dict):
        return "", ""
    guest_id = data.get("guest")
    session = data.get("guest_session")
    if guest_id not in HARNESS_GUESTS or not isinstance(session, str) or not session.strip():
        return "", ""
    return guest_id, session.strip()[:200]


def resume_session(agent, data, emit) -> None:
    """A Relay `resume`/`load_state` landed on a conversation that ran on a guest (29.3).

    When the pane is already on that guest, the harness is restarted with the guest's own session
    id so the two transcripts line up again. When it is on something else, nothing is started: the
    GUI resumes a guest conversation by configuring the preset with `guest.resume` (29.4), and
    starting a process behind the user's back is not this handler's to do.
    """
    guest_id, session = session_guest(data)
    provider = agent_provider(agent)
    if not guest_id or provider is None or provider.guest_id != guest_id:
        return
    if session == provider.session_id:
        return
    # A *new* harness, not `start()` on the one the pane holds: a harness is one process for one
    # guest session, and both adapters refuse a second start ("already started"). The old one is
    # only closed once the replacement is up, so a guest that will not resume leaves the pane with
    # the agent it had.
    try:
        replacement = make_harness(guest_id)
        started = replacement.start(cwd=str(agent.executor.workspace.root),
                                    model=provider.config.model or None, resume=session,
                                    fork=False, permissions=provider.permissions,
                                    effort=provider.effort or None)
    except HarnessError as exc:
        emit({"event": "status",
              "text": f"{guest.spec(guest_id).name} could not resume that session: {exc}"})
        return
    previous, provider.harness = provider.harness, replacement
    _close_quietly(previous)
    provider.session_id = started.session_id or session
    if started.model:
        provider.config.model = started.model
        agent.config.model = started.model
    emit({"event": "status",
          "text": f"{guest.spec(guest_id).name} resumed its own session {provider.session_id}."})
