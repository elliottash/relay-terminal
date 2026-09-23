# SPDX-License-Identifier: AGPL-3.0-or-later
"""The worker side of Tier A: a guest harness standing in for the chat provider (protocol 29.3).

`guest_harness.py` is the contract and `guest_harness_claude` / `guest_harness_codex` are the two
adapters. This module is what the worker holds instead of a `ChatProvider`, so that everything
above it — `Agent`, the turn record, the request ledger, the queue, the sessions index — is the
ordinary machinery and never learns that the model is a whole other agent:

* `HarnessProvider` satisfies the provider surface `Agent` uses (`complete`, `cancel`, `config`,
  `set_stall_timeout`, `stall_timeout`, `response_open`). `complete()` takes the last user message
  — preceded once by the conversation so far when the harness is fresh mid-conversation
  (`handover_brief`, #1V4F: no context is lost on a model change) — runs **one** harness turn, translates each `HarnessEvent` into the Relay event of the 29.1 table,
  and returns the guest's final text as the assistant message with no tool calls, so the Agent's
  turn loop ends the turn on it exactly as it ends a plain answer. The guest's own tool calls and
  results are written into `agent.messages` as they happen, so the next model inherits them.
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
from .presets import effort_fixed, model_name, provider_rank, tier_start_efforts
from .guest_harness import (HARNESS_GUESTS, HarnessError, HarnessSteerUncertain, HarnessNotAvailable, HarnessEvent,
                            limit_windows,
                            TOOL_NAMES, chunk_tool_output, map_tool_name, validate_effort,
                            validate_permissions)
from .provider import (DEFAULT_STALL_TIMEOUT, Cancelled, ProviderConfig, ProviderError,
                       cache_counts, message_images)

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

# What a running call may stream to the pane before the stream is cut, per call. The same budget
# `tools.MAX_OUTPUT` gives Relay's own live command output (`ToolRunner._await`), so a guest's
# build scrolls the pane exactly as far as Relay's own does and no further; the whole output is
# still in the `tool_result` when the call ends. Not imported from `tools`, which would drag the
# tool runner into every worker that only wants a preset row.
MAX_STREAMED_OUTPUT = 32_768


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


def guest_name(preset_or_config) -> str:
    """How a guest is named to a person — "Claude Code", "Codex" — from a preset id or a config.

    Empty for anything that is not a guest, so a caller can write the name straight into a
    sentence without asking twice which of the two it is holding.
    """
    guest_id = (preset_guest_id(preset_or_config)
                if isinstance(preset_or_config, str) else config_guest_id(preset_or_config))
    return guest.spec(guest_id).name if guest_id else ""


def helper_refusal(name: str) -> str:
    """What the helper agent says when the only model it has is a guest harness (card #GH5T).

    The helper — the Board's agent and the one the Options, Actions and Sessions panes ask
    (protocol 30.7) — works through Relay's own `board_*` and `app_*` tools, which a guest does
    not take (#4NXH). So it cannot run on one, and when the Options › Models priority list holds
    nothing else usable there is nothing to fall back to. This is the sentence the user sees then,
    in place of the endpoint error a guest's `harness://` base URL used to raise deep inside the
    agent builder (owner report, 2026-09-20: "The Switchboard agent could not answer: Base URL
    must be an HTTPS URL without credentials, query, or fragment").
    """
    return (f"The helper agent cannot run on {name or 'a guest session'}. Add a provider under "
            "Options › Models, or pick a model for the helper in its model box.")


class UnavailableProvider:
    """The stand-in a helper worker is built on when it has no model at all (card #GH5T).

    Its Main is a guest harness, which the helper may not run on, and the priority list holds
    nothing else — so there is no endpoint to build. The worker is still configured, because the
    The board is files: the pane opens, its cards are read and its model box says what is wrong.
    Nothing is started and nothing is ever sent; a turn that reaches a provider at all gets
    `helper_refusal` back, the same sentence the agent builder raises before it gets that far.
    """

    serves_side_calls = False       # a side call would be a model call too

    def __init__(self, config: ProviderConfig, text: str):
        self.config = config
        self.text = text
        self.stall_timeout = DEFAULT_STALL_TIMEOUT

    def complete(self, *args, **kwargs):
        raise ProviderError(self.text)

    def cancel(self) -> None:
        pass

    def set_stall_timeout(self, seconds) -> None:
        self.stall_timeout = seconds

    def response_open(self) -> bool:
        return False

    def close(self) -> None:
        pass


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

# What `guest_models("codex")` answers once a scan has *completed empty* — a missing, refusing or
# slow `codex debug models` is then no menu no longer, but the four models codex-cli 0.155.1 lists
# first (read off this machine's own `codex debug models`, 2026-09-20; the scan always wins when it
# works, so this only goes stale while codex itself cannot be asked). Order is codex's own
# `priority` so the menu does not reorder when the scan recovers.
_CODEX_FALLBACK_MODELS = (
    {"id": "gpt-6-astra", "name": "gpt-6-astra", "label": "gpt-6-astra",
     "efforts": ["low", "medium", "high", "xhigh", "max", "ultra"], "default_effort": "medium"},
    {"id": "gpt-6-sol", "name": "gpt-6-sol", "label": "gpt-6-sol",
     "efforts": ["low", "medium", "high", "xhigh", "max", "ultra"], "default_effort": "medium"},
    {"id": "gpt-5.6-terra", "name": "gpt-5.6-terra", "label": "gpt-5.6-terra",
     "efforts": ["low", "medium", "high", "xhigh", "max", "ultra"], "default_effort": "medium"},
    {"id": "gpt-6-luna", "name": "gpt-6-luna", "label": "gpt-6-luna",
     "efforts": ["low", "medium", "high", "xhigh", "max", "ultra"], "default_effort": "medium"},
)

_catalog: dict[str, list[dict]] = {}
_catalog_started: set = set()
_catalog_lock = threading.Lock()
# Set once the background scan has finished, whatever it found. Tests wait on it; nothing else does.
catalog_ready = threading.Event()
# The worker's "the scan landed" hook, called from the scan thread once it has finished — it
# re-emits `presets` so the GUI's cached copy (Options' guest rows) gets the codex list and the
# login answers without a re-ask. None until the worker registers one, and never called when
# there was nothing to scan (no guest installed).
_catalog_listener = None

# Whether each guest's CLI is signed in (`logged_in` on its row, 29.3): `claude auth status` /
# `codex login status`, read **once per worker process on the same background scan** as the
# codex catalogue — each is a subprocess of a second or so, and `presets` is answered on the
# protocol thread, which may never wait for one. None until the scan lands, and refreshed by a
# guest key test (`note_login`), which is the one other moment the worker learns the answer.
CLI_STATUS_TIMEOUT = 15.0
_login: dict[str, bool | None] = {}


def set_catalog_listener(callback) -> None:
    """Register (or clear, with None) the callable told when a catalogue scan finishes."""
    global _catalog_listener
    _catalog_listener = callback


def _read_codex_catalog(binary: str) -> list[dict]:
    """`codex debug models` as the contract's model rows. The seam tests replace."""
    from .guest_harness_codex import catalog_rows
    out = subprocess.run([binary, "debug", "models"], capture_output=True, text=True,
                         stdin=subprocess.DEVNULL, timeout=CODEX_CATALOG_TIMEOUT, check=False)
    if out.returncode != 0:
        raise RuntimeError((out.stderr or "").strip()[:200] or "codex debug models failed")
    return catalog_rows(json.loads(out.stdout).get("models") or [])


def _login_status_args(guest_id: str) -> tuple | None:
    """The status command's arguments (after the binary), from the adapter that knows its CLI."""
    module_name = _ADAPTERS.get(guest_id, ("", ""))[0]
    try:
        module = importlib.import_module(module_name) if module_name else None
    except Exception:                                   # half-written adapter: no answer, not a crash
        return None
    args = getattr(module, "LOGIN_STATUS_ARGS", None)
    return tuple(args) if args else None


def _read_login_status(guest_id: str, binary: str) -> bool | None:
    """Run the guest's status command and read it. The seam tests replace (as with the catalogue);
    None when this guest's CLI has no status command Relay knows, or the run itself failed."""
    args = _login_status_args(guest_id)
    if args is None:
        return None
    module = importlib.import_module(_ADAPTERS[guest_id][0])
    out = subprocess.run([binary, *args], capture_output=True, text=True, stdin=subprocess.DEVNULL,
                         timeout=CLI_STATUS_TIMEOUT, check=False)
    return bool(module.parse_login_status(out.returncode, out.stdout or "", out.stderr or ""))


def login_status(guest_id: str) -> bool | None:
    """`logged_in` for a guest's row: True/False once known, None until the scan has said."""
    with _catalog_lock:
        return _login.get(guest_id)


def note_login(guest_id: str, logged_in: bool | None) -> None:
    """Record what a guest just proved about its login (a key test's turn ran, or it was refused
    for not being signed in), so the rows say so without a second status command."""
    if guest_id not in HARNESS_GUESTS or logged_in is None:
        return
    with _catalog_lock:
        _login[guest_id] = bool(logged_in)


def start_catalog_scan(guest_id: str = "codex") -> None:
    """Start the one background scan of this worker process, if it has not started: each
    installed guest's login status, then codex's catalogue. Never blocks, whichever guest asks.

    `guest_id` is kept for the callers that name codex; the scan is the same one either way.
    """
    with _catalog_lock:
        if "scan" in _catalog_started:
            return
        _catalog_started.add("scan")
    found = installations()
    binaries = {gid: (found.get(gid) or {}).get("binary") or "" for gid in HARNESS_GUESTS}
    if not any(binaries.values()):
        with _catalog_lock:
            _catalog["codex"] = []
        catalog_ready.set()
        return

    def scan():
        for gid, binary in binaries.items():
            if not binary:
                continue
            status = None
            try:
                status = _read_login_status(gid, binary)
            except Exception as exc:    # a CLI that cannot be asked: the row stays "unknown"
                _log.debug("%s login status could not be read: %s", gid, exc)
            with _catalog_lock:
                # A key test that finished while the scan ran has the fresher proof; it wins.
                _login.setdefault(gid, status)
        rows: list[dict] = []
        if binaries["codex"]:
            try:
                rows = _read_codex_catalog(binaries["codex"])
            except Exception as exc:  # a codex that cannot be asked means the fallback menu below
                _log.debug("codex catalogue could not be read: %s", exc)
        with _catalog_lock:
            _catalog["codex"] = rows
        catalog_ready.set()
        listener = _catalog_listener
        if listener is not None:
            try:
                listener()          # the worker pushes a fresh `presets` now that there is a list
            except Exception:                                          # pragma: no cover
                _log.debug("guest scan listener failed", exc_info=True)

    threading.Thread(target=scan, name="relay-guest-scan", daemon=True).start()


def reset_catalog() -> None:
    """Forget the catalogue, the login answers and the fact that they were read (tests, and a
    `presets` refresh)."""
    with _catalog_lock:
        _catalog.clear()
        _catalog_started.clear()
        _login.clear()
    catalog_ready.clear()


def guest_models(guest_id: str) -> list[dict]:
    """What this guest can be set to, for the row's `models` — `[]` when it cannot be said here.

    Every row also carries `tier_effort` — ``{main, high, flash, lite}: the level this model starts
    at when the user adds it to that list by hand`` (Options › Models' `+ add a model…`). One rule
    computes it, `presets.tier_start_efforts`, which is the same one the `defaults` buttons fill the
    lists by, so a hand-added row and a filled list agree; a guest's Main is its own
    `default_effort` (codex's `default_reasoning_level`), never its top level — codex's is `ultra`,
    a delegation mode (card #TKN7).
    """
    if guest_id == "claude":
        global _CLAUDE_MODELS
        if _CLAUDE_MODELS is None:
            adapter = _load_adapter("claude")
            _CLAUDE_MODELS = adapter().models() if adapter is not None else []
        rows = [dict(row) for row in _CLAUDE_MODELS]
    else:
        start_catalog_scan(guest_id)
        with _catalog_lock:
            rows = [dict(row) for row in _catalog.get(guest_id) or ()]
        # Codex only, and only once the scan has completed empty (owner, 2026-09-19: "add model
        # selection in codex options"): before that the first `presets` answer must not wait, and
        # after it an empty scan is a codex that could not be asked — the fallback's four models
        # stand in until one can. A scan that found rows always wins.
        if guest_id == "codex" and not rows and catalog_ready.is_set():
            rows = [dict(row) for row in _CODEX_FALLBACK_MODELS]
    for row in rows:
        efforts = row.get("efforts") if isinstance(row.get("efforts"), list) else []
        default = row.get("default_effort") if isinstance(row.get("default_effort"), str) else None
        # One model, one name (card #MDL1). The adapters fill this in; an older one, or a row that
        # came from somewhere else, gets it here, so no guest row ever reaches the GUI without it.
        name = row.get("name") or model_name(PRESET_PREFIX + guest_id, row.get("id"))
        row["name"] = row["label"] = name
        row["tier_effort"] = tier_start_efforts(efforts, default, guest_id, name)
        # The same key every built-in catalog row carries (presets.effort_fixed): grey the effort
        # box for a model the CLI says has no levels. A guest is never Relay Free, so the list is
        # the whole of the question here.
        row["effort_fixed"] = effort_fixed(efforts)
    return rows


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
    start_catalog_scan()             # login status for every installed guest, then codex's list
    rows = []
    for guest_id in HARNESS_GUESTS:
        state = found.get(guest_id) or {"installed": False, "binary": "", "version": ""}
        models = guest_models(guest_id) if state["installed"] else []
        # How this provider is reached and where it sorts against the others serving the same
        # model (13.2): `model-ranking.md`'s Providers table names both harnesses, and a guest
        # keeps `harness` even if the file has not caught up (`presets.provider_rank`). The
        # picker needs it on *both* sides of a fold — gpt-6-astra through codex and through the
        # OpenAI API are one row — so a guest row carries the pair like every other row.
        kind, order = provider_rank(PRESET_PREFIX + guest_id)
        rows.append({"id": PRESET_PREFIX + guest_id, "label": guest.spec(guest_id).name,
                     "guest": guest_id, "kind": kind, "order": order,
                     "harness": bool(state["installed"] and adapter_available(guest_id)),
                     "installed": state["installed"], "binary": state["binary"],
                     "version": state["version"], "group": "guest",
                     # Whether the CLI is signed in: null until the background scan has asked
                     # it, and always null for a guest that is not installed (29.3).
                     "logged_in": login_status(guest_id) if state["installed"] else None,
                     "has_stored_key": False, "key_source": "guest",
                     "model": "", "base_url": base_url(guest_id),
                     "local": False, "hosted": False,
                     # The guest's own levels and its own models (29.3). `effort_note` is the
                     # line a provider uses to explain the levels it has *not* got; a guest's
                     # list is its own and leaves nothing out, so there is nothing to say.
                     "efforts": guest_efforts(guest_id, models) if state["installed"] else [],
                     "effort_fixed": effort_fixed(guest_efforts(guest_id, models)
                                                  if state["installed"] else []),
                     "effort_note": "", "models": models})
        # The subscription's rolling windows as the guest last reported them to any pane in this
        # worker (`usage_limits`), so a picker opened later still has a figure to show. Absent
        # until a guest has reported one; a limit belongs to the account, not to a pane.
        held = last_limits(guest_id)
        if held:
            rows[-1]["limits"] = held
    return rows


# `usage_limits` as each guest last reported them, keyed by guest id:
# {"windows": [{kind, used_percent, resets_at}], "status"?: str, "updated_at": unix seconds}.
# One worker, one account per guest, so one figure per guest is the whole truth here.
_LAST_LIMITS: dict[str, dict] = {}


def last_limits(guest_id: str) -> dict:
    """A copy of the last `usage_limits` figures for a guest, or {} before it reported any."""
    held = _LAST_LIMITS.get(guest_id)
    if not held:
        return {}
    out = dict(held)
    out["windows"] = [dict(w) for w in held.get("windows", [])]
    return out


def usage_limits_event(guest_id: str, data: dict) -> dict:
    """The worker's `usage_limits` event a harness `limits` event becomes (29.3), or {} when it
    names no window. Also the moment the figures are remembered for `preset_rows()`."""
    windows = limit_windows(data.get("windows") if isinstance(data, dict) else None)
    if not windows:
        return {}
    event = {"event": "usage_limits", "preset": PRESET_PREFIX + guest_id, "guest": guest_id,
             "windows": windows}
    status = data.get("status")
    if isinstance(status, str) and status.strip():
        event["status"] = status.strip()[:40]
    held = {"windows": [dict(w) for w in windows], "updated_at": int(time.time())}
    if "status" in event:
        held["status"] = event["status"]
    _LAST_LIMITS[guest_id] = held
    return event


# ----- starting one ------------------------------------------------------------------------------


def make_harness(guest_id: str, probe: bool = False):
    """The adapter instance for a guest. The single seam tests replace, and the only place either
    adapter module is imported. `probe` asks for the adapter's key-test variant (`for_probe()`:
    claude with no tools), for keytest's one turn."""
    if guest_id not in _ADAPTERS:
        raise HarnessNotAvailable(f"Relay has no harness for {guest_id!r}.")
    factory = _load_adapter(guest_id)
    if factory is None:
        raise HarnessNotAvailable(
            f"{guest.spec(guest_id).name}'s harness is not available in this build of Relay.")
    if probe and callable(getattr(factory, "for_probe", None)):
        return factory.for_probe()
    return factory()


_SKILLS_UNSET = object()


def start_provider(preset_id: str, request: dict, workspace: str,
                   stall_timeout: float = DEFAULT_STALL_TIMEOUT,
                   config: ProviderConfig | None = None, *, skill_index=_SKILLS_UNSET,
                   instruction_suffix: str = "", delegation: bool = True) -> "HarnessProvider":
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
    from .guest_board_bridge import Bridge
    from .guest_instructions import build_instructions
    from .board_tools import find_board_root
    bridge = Bridge(available=delegation and find_board_root(workspace) is not None,
                    delegation=delegation)
    try:
        instructions = (build_instructions(request.get("skills"), workspace)
                        if skill_index is _SKILLS_UNSET else
                        build_instructions(None, workspace, skill_index=skill_index))
        if instruction_suffix:
            instructions += "\n\n" + instruction_suffix
        started = harness.start(cwd=workspace, model=options["model"] or None,
                                resume=options["resume"], fork=options["fork"],
                                permissions=options["permissions"], effort=options["effort"],
                                board_bridge=bridge.descriptor, instructions=instructions)
    except HarnessError as exc:
        bridge.close()
        _close_quietly(harness)
        raise ValueError(str(exc) or f"{guest.spec(guest_id).name} could not be started.") from None
    except Exception as exc:
        bridge.close()
        _close_quietly(harness)
        raise ValueError(f"{guest.spec(guest_id).name} could not be started "
                         f"({type(exc).__name__}).") from None
    # The caller's own config object when it has one, filled in rather than replaced: the worker
    # hands the same object to the role resolver before the guest is started, and the model is only
    # known once it has answered.
    config = config if config is not None else config_for_preset(preset_id, request)
    config.model = started.model or options["model"] or guest_id
    provider = HarnessProvider(config, harness, guest_id, stall_timeout=stall_timeout)
    provider.board_bridge = bridge
    provider.instructions = instructions
    provider.session_id = started.session_id or ""
    provider.permissions = options["permissions"]
    provider.effort = _harness_effort(harness) or options["effort"] or ""
    # A resumed or forked session carries its own history; a fresh one is briefed on its first turn.
    provider.briefed = bool(options["resume"] or options["fork"])
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


# The model-switch handover (#1V4F, owner 2026-09-22: "its critical that no context is loss on
# model changes"). A guest harness started mid-conversation has seen none of it, so its first
# prompt carries the transcript so far. Larger than a plan turn's excerpt: this is the whole
# conversation the guest now continues, and every guest's window is far above it.
HANDOVER_MAX_CHARS = 160_000
HANDOVER_TOOL_RESULT_CHARS = 4_000
HANDOVER_NOTE = ("You are taking over this conversation from another model in Relay. Everything "
                 "below happened earlier in this session, and you are continuing it: treat it as "
                 "your own context, not as a task list.")
# A guest pane's tool results as they enter Relay's transcript (`agent.messages`), so a model
# switched in afterwards sees what the guest read and ran. The same bound as Relay's own tools'
# output (`tools.MAX_OUTPUT`).
RECORDED_TOOL_RESULT_CHARS = 32_768


def handover_brief(messages: list[dict]) -> str:
    """The conversation before this turn's prompt, as a Relay context block for a guest that has
    not seen it, or "" when there is nothing before the prompt (#1V4F)."""
    from .agent import CONTEXT_CLOSE, CONTEXT_OPEN
    from .planning import render_transcript
    # What `complete()` sends is every user message after the last assistant one; the history is
    # what precedes it (the system prompt aside — the guest has its own).
    end = len(messages)
    while end > 0 and messages[end - 1].get("role") == "user":
        end -= 1
    history = [m for m in messages[:end] if m.get("role") != "system"]
    transcript = render_transcript(history, (CONTEXT_OPEN, CONTEXT_CLOSE), max_chars=HANDOVER_MAX_CHARS,
                                   max_tool_chars=HANDOVER_TOOL_RESULT_CHARS, until_prompt=False)
    if not transcript:
        return ""
    return f"{CONTEXT_OPEN}\n{HANDOVER_NOTE}\n\n{transcript}\n{CONTEXT_CLOSE}"


def _recorded(result: dict) -> dict:
    """A guest tool result as it enters `agent.messages`: long text fields cut, still valid JSON."""
    out = dict(result)
    for key in ("output", "diff"):
        value = out.get(key)
        if isinstance(value, str) and len(value) > RECORDED_TOOL_RESULT_CHARS:
            out[key] = value[:RECORDED_TOOL_RESULT_CHARS] + " […]"
    return out


class HarnessProvider:
    """One guest harness, wearing the surface `Agent` calls on `self.provider`.

    Threading follows the contract: `complete()` runs on the turn thread; `cancel()` and
    `answer()` come from the worker's protocol thread while it blocks.
    """
    # A pane's guest writes its tool calls and results into `agent.messages` as Relay's own agent
    # does, so the conversation a later model inherits holds them (#1V4F). Child harnesses did
    # this first, for their thread views.
    record_guest_tools = True

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
        # The guest's own context after its last `usage`, in `Agent.context_event()`'s words
        # ({used_tokens, window, percent}); empty until the guest reports one. `attach()` puts it
        # on every `context` event as `guest_context`, so the pane's chip can say how full the
        # *guest's* window is — which is the only window a guest turn actually runs against.
        self.guest_context: dict = {}
        self.context_generation = 0
        # The subscription's rolling windows after the guest's last `limits` event, in the
        # `usage_limits` event's words (`windows`, `status`?, `updated_at`); {} until it says.
        self.usage_limits: dict = {}
        self._stall_timeout = float(stall_timeout)
        # What this harness is sent in place of its first prompt, once: a fresh session started in
        # the middle of a conversation — a plan turn on a High-list guest (protocol 13.7) — needs
        # the rules and the transcript so far, which the last user message alone does not carry.
        # A string, or a callable given the conversation and returning the prompt; None once used.
        self.opening = None
        # Whether this harness has been given the conversation it is answering in (#1V4F): False
        # for a fresh session, True once briefed, or from the start for a resumed/forked session
        # that holds its own history.
        self.briefed = False
        self._agent = None
        self.board_bridge = None
        self.instructions: str | None = None
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
        if self.board_bridge is not None:
            self.board_bridge.end()
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
        if self.board_bridge is not None:
            self.board_bridge.close()
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
        if self.board_bridge is not None:
            self.board_bridge.bind(agent)

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
        # Inbox notes can follow the owner's prompt at the same step boundary. Send
        # all pending user messages, so a completed child cannot hide that prompt.
        pending = []
        has_notes = False
        for message in reversed(messages):
            if message.get("role") == "assistant":
                break
            if message.get("role") == "user":
                has_notes = has_notes or message.get('relay_kind') in {'note', 'steer'}
                text, images = last_user_message([message])
                pending.append((text, images))
        if has_notes and len(pending) > 1:
            prompt = "\n\n".join(text for text, _ in reversed(pending))
            attachments = [image for _, images in reversed(pending) for image in images]
        opening, self.opening = self.opening, None
        if opening is not None:
            prompt = opening(messages) if callable(opening) else str(opening)
        elif not self.briefed and agent is not None:
            # A fresh harness in the middle of a conversation (#1V4F): the model box switched the
            # pane onto this guest, or back onto it after another model. It gets the transcript
            # once, ahead of the prompt; Relay's own transcript is not touched.
            brief = handover_brief(messages)
            if brief:
                prompt = brief + "\n\n" + prompt
                emit({"event": "status", "text": f"Handed the conversation so far to "
                                                 f"{guest.spec(self.guest_id).name}."})
        if not prompt and not attachments:
            raise ProviderError("There is nothing to send to the guest: the last message has no text.")
        record = getattr(agent, "_turn_record", None) if agent is not None else None
        turn = _Turn(self, agent, record, emit, cancel)
        if self.board_bridge is not None:
            self.board_bridge.begin(cancel)
            prompt = ("Relay offers an MCP server named relay_board. If its tools are visible, "
                      "use its board_list, board_read, board_comment, board_update_card and "
                      "board_move_card tools in preference to file edits. Other board operations "
                      "(including create/claim), or an unavailable connection, use POLICY.md's "
                      "file fallback. Never claim connection success without discovery. "
                      "Delegate only through this server's agent, agent_message and agent_wait; "
                      "use update_todos and link delegated tasks with todo_id. Do not use native "
                      "harness subagents or another agent CLI. If Relay delegation is unavailable, "
                      "work locally and report that limitation.\n\n" + prompt)
            if agent is not None and agent._todos_enabled():
                prompt += "\n\n[Relay tasks: current state, preserve when updating]\n" + json.dumps(
                    agent.todos.snapshot(), ensure_ascii=False)
        # Set before sending: a failed first turn on a live harness has still been given it.
        self.briefed = True
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
        finally:
            turn.release_steer()
            if self.board_bridge is not None:
                self.board_bridge.end()
        turn.close_thinking()
        if (self.board_bridge is not None and self.board_bridge.specs()
                and not self.board_bridge.ready.is_set()):
            emit({"event": "status", "text": "Guest board tools were not discovered; use the board policy's file fallback."})
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
        """The pane's `question_answer` for an ask this provider put up. False when the id is not
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
        self.context_generation = provider.context_generation
        self.agent = agent
        self.record = record
        self.emit = emit
        self.cancel = cancel
        self.turn_id = record.get("turn_id") if isinstance(record, dict) else None
        self.calls: dict[str, dict] = {}
        self.streamed: dict[str, int] = {}
        self.usage_seen = False
        self.error_text = ""
        self._thinking_started = None
        self._thinking_chars = 0
        self._pending_steer = []
        self._steer_failed = False

    def release_steer(self) -> None:
        if self._pending_steer:
            self.agent.steer_settle(False)
            self._pending_steer = []

    def deliver_steer(self) -> None:
        agent = self.agent
        steer = getattr(self.provider.harness, "steer", None)
        reserve = getattr(agent, "steer_reserve", None)
        if (self._steer_failed or self._pending_steer or self.cancel.is_set()
                or not callable(steer) or not callable(reserve)
                or not getattr(agent, "_turn_ctx", None)):
            return
        items = reserve()
        if not items:
            return
        self._pending_steer = items

        def accepted():
            if self._pending_steer is not items:
                return  # a late duplicate must not acknowledge a newer batch
            message = agent._steer_message(items, agent._turn_ctx, agent._turn)
            agent.messages.append(message)
            agent.steer_settle(True)
            self._pending_steer = []

        try:
            message = agent._steer_message(items, agent._turn_ctx, agent._turn, record=False)
            steer(message["content"], accepted=accepted)
        except Exception as exc:
            self._steer_failed = True
            self.release_steer()
            if isinstance(exc, HarnessSteerUncertain):
                self.provider.harness.close()
                raise
            self.emit({"event": "status", "text": f"Guest steering was not acknowledged: {exc}. "
                       "The input is retained for return at the end of the turn."})

    # ----- dispatch -----------------------------------------------------------------------
    def on_event(self, event: HarnessEvent) -> None:
        if event.kind in {"tool_started", "tool_result"}:
            self.deliver_steer()
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
            # `model_name` (protocol 13, card #MDL1 rule 1): Claude Code reports "opus" and
            # Relay calls that model "claude-opus-5.5" everywhere else, so the name travels with it.
            self.emit({"event": "model_changed", "model": model, "applies": "now",
                       "model_name": model_name(PRESET_PREFIX + self.provider.guest_id, model),
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
        self._record_context(usage)
        self.emit({"event": "usage", "usage": usage})
        if self.agent is not None:
            self.emit(self.agent.context_event())

    def _record_context(self, usage: dict) -> None:
        # A late report from the old model must not repopulate the new model's meter.
        if self.context_generation == self.provider.context_generation:
            self.provider.guest_context = guest_context(usage) or self.provider.guest_context

    def _on_limits(self, data: dict) -> None:
        event = usage_limits_event(self.provider.guest_id, data)
        if not event:
            return
        self.provider.usage_limits = last_limits(self.provider.guest_id)
        self.emit(event)

    def finish(self, usage: dict) -> None:
        """The turn ended well. The usage the harness returned goes out only when it never sent a
        `usage` event, so nothing is counted twice; the Agent turns it into the `context` event."""
        if self.usage_seen:
            return
        mapped = relay_usage(usage)
        if mapped:
            self._record_context(mapped)
            self.emit({"event": "usage", "usage": mapped})

    # ----- tool calls -----------------------------------------------------------------------
    def _on_tool_started(self, data: dict) -> None:
        self.close_thinking()
        call_id = str(data.get("call_id") or "") or "guest-" + uuid.uuid4().hex[:12]
        name, guest_tool = self._tool_name(data)
        source = data.get("input")
        if isinstance(source, dict) and source.get("server") == "relay_board":
            source = source.get("arguments")
        args = label_arguments(source)
        preview = tool_preview(name, guest_tool, args, data.get("label"))
        self.calls[call_id] = {"name": name, "guest_tool": guest_tool, "args": args,
                               "preview": preview, "started": time.monotonic()}
        event = {"event": "tool_started", "tool": name, "preview": preview,
                 "label": tool_labels.started_label(_label_name(name, guest_tool), args),
                 "call_id": call_id}
        if self.turn_id is not None:
            event["turn_id"] = self.turn_id
        self.emit(event)

    def _on_tool_output(self, data: dict) -> None:
        """Output a call has printed so far → Relay's live `tool_output` (protocol 11.1).

        Exactly the event `ToolRunner._await` emits for Relay's own long-running commands — the
        live one, `{text}`, not the `stored: true` reply to `tool_output_get` — so the pane's
        running call line counts a guest's build the way it counts Relay's, with no change to the
        pane. `call_id` and `turn_id` ride along for the surfaces that key on them (the internals
        pane, a subagent wrapper); the terminal pane reads `text` alone and ignores the rest.

        Capped per call at MAX_STREAMED_OUTPUT characters, counted the way `ToolRunner._await`
        counts it: past the budget the stream stops and the call's full output still arrives with
        its `tool_result`. Split again at MAX_TOOL_OUTPUT_CHUNK on the way out, so one event is a
        few KiB whatever the harness handed over — the adapters already chunk, and this is the
        last gate before the worker's queue and every subscribed surface's socket.
        """
        text = data.get("text")
        if not isinstance(text, str) or not text:
            return
        call_id = str(data.get("call_id") or "")
        streamed = self.streamed.get(call_id, 0)
        if streamed >= MAX_STREAMED_OUTPUT:
            return
        piece = text[:MAX_STREAMED_OUTPUT - streamed]
        self.streamed[call_id] = streamed + len(piece)
        for chunk in chunk_tool_output(piece):
            event = {"event": "tool_output", "text": chunk}
            if call_id:
                event["call_id"] = call_id
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
        self.streamed.pop(call_id, None)
        ok = data.get("ok") is not False
        diff = data.get("diff") if isinstance(data.get("diff"), str) and data["diff"].strip() else ""
        result = tool_result(data.get("output"), ok, diff)
        for key in ("exit_code", "timed_out", "refused", "error_code"):
            if key in data:
                result[key] = data[key]
        from .guest_board_bridge import ALLOW
        if call['name'] in ALLOW:
            # Preserve native result fields (child id, task state, errors) through
            # the MCP envelope so the existing labels open Relay's child/task view.
            native = bridge_result(data.get('output'))
            if native is not None:
                result.update(native)
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
        if self.agent is not None and getattr(self.agent.provider, 'record_guest_tools', False):
            # Child thread views read messages (not the pane's turn records). Keep
            # guest tool activity there too, so opening a child after completion or
            # subscribing halfway through does not lose the work already observed.
            self.agent.messages.extend([
                {'role': 'assistant', 'content': '', 'tool_calls': [
                    {'id': call_id, 'type': 'function', 'function': {
                        'name': call['name'], 'arguments': json.dumps(call['args'])}}]},
                {'role': 'tool', 'tool_call_id': call_id, 'content': json.dumps(_recorded(result))}])
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
        from .guest_board_bridge import ALLOW
        board_name = (source.get("tool") if source.get("server") == "relay_board" else
                      guest_tool.removeprefix("mcp__relay_board__")
                      if guest_tool.startswith("mcp__relay_board__") else None)
        if isinstance(board_name, str) and board_name in ALLOW:
            return board_name, guest_tool
        name = raw if raw in TOOL_NAMES else map_tool_name(self.provider.guest_id, raw)
        return name, guest_tool

    # ----- questions and approvals -----------------------------------------------------------
    def _on_approval(self, data: dict) -> None:
        """An approval the guest raised under `permissions: "ask"`: a protocol 27 ask with the
        APPROVAL_CHOICES options, and the answer goes back through `harness.answer` (29.3)."""
        request_id = str(data.get("id") or "")
        ask = approval_ask(str(data.get("kind") or "other"), data.get("detail"))
        answers = self.provider._asker.ask(ask, self.turn_id, self.emit, self.cancel)
        picked = str(answers[0][0]).strip() if answers and answers[0] else ""
        decision = approval_decision(picked)
        if decision["behavior"] != "allow":
            decision["message"] = ("The user did not allow this." if answers is not None
                                   else "The turn was stopped.")
        self._answer(request_id, decision)

    def _on_question(self, data: dict) -> None:
        request_id = str(data.get("id") or "")
        ask = clean_questions(data.get("questions"))
        answers = self.provider._asker.ask(ask, self.turn_id, self.emit, self.cancel)
        self._answer(request_id, {"answers": answers if answers is not None else []})

    def _answer(self, request_id: str, decision: dict) -> None:
        try:
            self.provider.harness.answer(request_id, decision)
        except HarnessError as exc:
            _log.debug("guest harness answer failed: %s", exc)


# What the pane offers on an approval ask, and the `harness.answer()` decision each option is
# (GT7X task t:a3). Four rather than two because both guests can say more than allow/deny:
# `scope: "session"` is codex's `acceptForSession` and claude's session-scoped permission rule,
# and `scope: "stop"` is codex's `cancel` and claude's `deny` with `interrupt: true`. The pane
# needs no change to draw them — a protocol 27 ask renders the options it is handed — and an
# answer this table does not know is a plain deny, which is what an unanswered approval is too.
APPROVAL_CHOICES = (
    ("Allow", "Let it go ahead, this once.", {"behavior": "allow", "scope": "once"}),
    ("Allow for session", "And the same again, until this guest session ends.",
     {"behavior": "allow", "scope": "session"}),
    ("Deny", "Refuse this one and let it carry on.", {"behavior": "deny", "scope": "once"}),
    ("Deny and stop", "Refuse it and end the turn here.", {"behavior": "deny", "scope": "stop"}),
)


def approval_decision(label) -> dict:
    """The decision one APPROVAL_CHOICES label means. Anything else — a stopped ask, a pane that
    sent a word this build does not know — is a deny for this one action."""
    picked = " ".join(str(label or "").split()).casefold()
    for name, _description, decision in APPROVAL_CHOICES:
        if picked == name.casefold():
            return dict(decision)          # a copy: the caller adds a `message` to it
    return {"behavior": "deny", "scope": "once"}


def approval_ask(kind: str, detail) -> list[dict]:
    """One approval as a protocol 27 ask: what the guest wants to do, and APPROVAL_CHOICES.

    Through `questions.validate` like any other ask, so the pane is handed exactly the shape it
    already draws (whitespace collapsed, every field capped) and a guest that sends something odd
    gets a plain ask rather than a failed turn.
    """
    header = {"command": "Run command", "patch": "Apply edit", "tool": "Use tool"}.get(kind, "Approve")
    text = " ".join(str(detail or "").replace("\x00", " ").split()) or "Let the guest do this?"
    ask = [{"header": header, "question": text[:questions_mod.MAX_QUESTION],
            "options": [{"label": name, "description": description}
                        for name, description, _decision in APPROVAL_CHOICES],
            "multiple": False}]
    try:
        return questions_mod.validate({"questions": ask})
    except ValueError:                              # pragma: no cover - cleaned above already
        ask[0]["question"] = "Let the guest do this?"
        return questions_mod.validate({"questions": ask})


# ----- the round trip an ask needs -------------------------------------------------------------


class _Asker:
    """The `question` → `question_answer` round trip for a guest's asks.

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
        """Draw the ask and block until the pane answers. Returns the per-question answer lists,
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
        """Wake every waiting ask when Stop is pressed, once per cancel event."""
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
    # The prefix cache, in the one pair of names the `usage` event uses (#GMCF decision 5). Both
    # guests cache: codex reports `cachedInputTokens`, claude `cache_read_input_tokens`.
    cache = cache_counts(data)
    usage.update(cache)
    # Anthropic's `input_tokens` is the *uncached* part of the prompt, where OpenAI's (and codex's)
    # `prompt_tokens` counts the cached part inside itself. Relay reports one number, so the claude
    # harness's is completed here — otherwise "in 18 (38,991 cached)" reads as nonsense and the
    # session totals under-count every cached guest turn by the size of its prefix.
    if "cache_read_input_tokens" in data or "cache_creation_input_tokens" in data:
        usage["prompt_tokens"] = (usage.get("prompt_tokens", 0) + cache.get("cached_tokens", 0)
                                  + cache.get("cache_write_tokens", 0))
    usage["total_tokens"] = usage.get("prompt_tokens", 0) + usage.get("completion_tokens", 0)
    cost = data.get("cost_usd")
    if isinstance(cost, (int, float)) and not isinstance(cost, bool) and cost >= 0:
        usage["cost"] = float(cost)
    # The guest's own context, not Relay's: what the guest put in its own window, out of its own
    # model's window, and the share that is. Carried for the pane; `sessions.add_usage` only reads
    # the four keys above, so none of this reaches the session totals.
    for source, target in (("context_tokens", "guest_context_tokens"),
                           ("context_window", "guest_context_window")):
        value = data.get(source)
        if isinstance(value, int) and not isinstance(value, bool) and value > 0:
            usage[target] = value
    pct = data.get("context_pct")
    if not isinstance(pct, (int, float)) or isinstance(pct, bool):
        # A harness that reports the two numbers and not the share still gets a share: the pane's
        # chip should not have to do arithmetic that is the same for every guest.
        tokens, window = usage.get("guest_context_tokens"), usage.get("guest_context_window")
        pct = min(100.0, 100.0 * tokens / window) if tokens and window else None
    if isinstance(pct, (int, float)) and not isinstance(pct, bool):
        usage["guest_context_pct"] = round(float(pct), 1)
    return usage


def guest_context(usage: dict) -> dict:
    """The guest's own context out of a mapped `usage`, in the words `Agent.context_event()` uses
    for Relay's (`used_tokens`, `window`, `percent`), or {} when the guest said nothing.

    Same names because it is the same measurement of a different window: the pane draws one chip
    from `context` and this is what lets it read "43% · 13k of 258k" instead of "43%".
    """
    if not isinstance(usage, dict):
        return {}
    out = {}
    for source, target in (("guest_context_tokens", "used_tokens"),
                           ("guest_context_window", "window"),
                           ("guest_context_pct", "percent")):
        if source in usage:
            out[target] = usage[source]
    return out


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
    for key in ("pattern", "query", "url", "description", "subagent_type", "intent", "id", "todo_id"):
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


def bridge_result(output) -> dict | None:
    try:
        value = json.loads(output) if isinstance(output, str) else output
        if isinstance(value, dict) and isinstance(value.get('content'), list):
            text = '\n'.join(p.get('text', '') for p in value['content']
                             if isinstance(p, dict) and p.get('type') == 'text')
            value = json.loads(text)
        return value if isinstance(value, dict) else None
    except (ValueError, TypeError):
        return None


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
    * the `context` event measures Relay's transcript against Relay's window, which for a guest
      pane is a record and not what the turn runs against. The guest's own figure rides beside it
      as `guest_context` (same wrapping, same instance, same reason: `Agent.context_event()` is
      not this module's to edit), so the chip can say how full the *guest's* window is.
    """
    provider.bind(agent)
    agent._guest_session_data = provider
    if getattr(agent, "_guest_session_data_wrapped", False):
        return                          # wrapped once per Agent; the line above re-points it
    agent._guest_session_data_wrapped = True
    original = agent.session_data
    original_context = agent.context_event

    def session_data():
        data = original()
        held = getattr(agent, "_guest_session_data", None)
        if held is not None:
            data["guest"] = held.guest_id
            data["guest_session"] = held.session_id
        return data

    def context_event():
        event = original_context()
        held = getattr(agent, "_guest_session_data", None)
        if held is not None:
            event["guest"] = held.guest_id
            event["guest_context"] = dict(held.guest_context)
        return event

    agent.session_data = session_data
    agent.context_event = context_event


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

    29.3 restarts the harness when the *preset* changes, and the fresh one is briefed on the
    conversation (#1V4F). Asking the same guest for another model is not that: the contract has
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
        provider.guest_context = {}
        provider.context_generation += 1
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

    The pane reads `effort`, so it must name the running harness's level as well as
    `guest_effort`. The wrapper Agent's API effort can snap medium to high (or xhigh
    to max); it is not the level sent to the harness. An unknown guest default stays empty.
    """
    provider = agent_provider(agent)
    if provider is None:
        return {}
    return {"guest": provider.guest_id, "guest_session": provider.session_id,
            "guest_effort": provider.effort, "effort": provider.effort}


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
    replacement = None
    try:
        replacement = make_harness(guest_id)
        started = replacement.start(cwd=str(agent.executor.workspace.root),
                                    model=provider.config.model or None, resume=session,
                                    fork=False, permissions=provider.permissions,
                                    effort=provider.effort or None,
                                    board_bridge=(provider.board_bridge.descriptor
                                                  if provider.board_bridge is not None else None),
                                    instructions=provider.instructions)
    except HarnessError as exc:
        if replacement is not None:
            _close_quietly(replacement)
        emit({"event": "status",
              "text": f"{guest.spec(guest_id).name} could not resume that session: {exc}"})
        return
    previous, provider.harness = provider.harness, replacement
    _close_quietly(previous)
    provider.session_id = started.session_id or session
    provider.briefed = True                 # its own session: it holds its own history
    if started.model:
        provider.config.model = started.model
        agent.config.model = started.model
    emit({"event": "status",
          "text": f"{guest.spec(guest_id).name} resumed its own session {provider.session_id}."})
