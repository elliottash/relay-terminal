# SPDX-License-Identifier: GPL-3.0-or-later
"""Worker protocol handlers for sessions, model/effort, context, checkpoints, plan mode,
instructions, recaps and suggestions (docs/AGENT-SESSIONS-PROTOCOL.md sections 1-7, 9, 10).

Conversation rewrites (compact) run through TurnSupervisor.run_exclusive so they never overlap a
turn. Calls that only read a copy of the conversation (recaps, suggestions, synthesis) run on
their own threads with a separate provider, so the protocol loop never blocks on the network.
"""
from __future__ import annotations

import os
import threading
import uuid

from . import attachments, instructions, keystore, planning, suggestions
from .agent import validate_turn_options
from .requests import check_ledger_id
from .context import validate_threshold, validate_window
from .presets import PRESETS, match_preset, resolve_preset, validate_effort
from .provider import ProviderConfig, ProviderError
from .sessions import default_session_dir

TYPES = {"set_model", "set_effort", "context", "compact", "checkpoints", "rewind", "fork", "load_state",
         "sessions", "resume", "recap_request", "set_mode", "plan_execute", "scan_instructions",
         "synthesize_instructions", "suggest",
         # request ledger and todos (protocol section 12)
         "requests", "request_get", "request_set", "request_reask", "todos"}


def provider_config(request: dict) -> ProviderConfig:
    """ProviderConfig from a configure/set_model request, using the stored key when asked."""
    api_key = request.get("api_key", "")
    if not isinstance(api_key, str):
        raise ValueError("API key must be text.")
    if not api_key and request.get("use_stored_key"):
        # The key never crosses the frontend pipe in this path.
        preset_id = request.get("preset", "")
        if preset_id not in PRESETS:
            # "Custom" settings that point at a known endpoint still use its stored key.
            match = match_preset(str(request.get("base_url", "")), str(request.get("model", "")))
            preset_id = match.id if match else ""
        api_key = keystore.lookup(preset_id) if preset_id else ""
        if not api_key:
            raise ValueError("No stored key for this provider. Import from Warp or enter a key.")
    config = ProviderConfig(request.get("base_url", ""), request.get("model", ""), api_key,
                            request.get("extra", {}), request.get("max_tokens", 8192))
    config.validate()
    return config


def _abs_dir(value, name: str) -> str | None:
    if value is None:
        return None
    if not isinstance(value, str) or not os.path.isabs(os.path.expanduser(value)):
        raise ValueError(f"{name} must be an absolute path.")
    return os.path.expanduser(value)


def agent_options(request: dict, workspace: str) -> dict:
    """Extra Agent keyword arguments from the configure fields added by protocol section 1."""
    window = request.get("context_window")
    threshold = request.get("compact_threshold")
    effort = request.get("effort")
    session_dir = _abs_dir(request.get("session_dir"), "session_dir") or str(default_session_dir(workspace))
    return {"preset_id": request.get("preset") if isinstance(request.get("preset"), str) else None,
            "context_window": validate_window(window) if window is not None else None,
            "compact_threshold": validate_threshold(threshold) if threshold is not None else None,
            "effort": validate_effort(effort) if effort is not None else None,
            "session_dir": session_dir,
            "plans_dir": _abs_dir(request.get("plans_dir"), "plans_dir"),
            "instructions": instructions.load(request.get("instructions"), workspace),
            **validate_turn_options(request)}


def configured_fields(agent) -> dict:
    fields = {"context_window": agent.context.window, "compact_threshold": agent.context.threshold,
              "limit_tokens": agent.context.limit, "effort": agent.effort, "mode": agent.mode,
              "instructions": list(agent.instructions.loaded) if agent.instructions else [],
              "session_id": agent.session_id, "plans_dir": str(agent.plans_dir),
              "session_dir": str(agent.store.directory) if agent.store else None,
              **agent.options()}
    if agent.instructions:
        fields["instructions_max_bytes"] = agent.instructions.cap
        fields["instructions_bytes"] = len(agent.instructions.section.encode("utf-8"))
        if agent.instructions.truncated:
            fields["instructions_truncated"] = agent.instructions.truncated
        if agent.instructions.skipped:
            fields["instructions_skipped"] = agent.instructions.skipped[:50]
    return fields


def load_attachments(request: dict, turns) -> list[dict] | None:
    raw = request.get("attachments")
    if raw is None:
        return None
    agent = turns.agent
    if agent is None:
        raise ValueError("Configure a provider and workspace first.")
    return attachments.load(raw, agent.executor.workspace.root) or None


def open_request_items(agent) -> list[dict]:
    """Unfinished user requests for recaps (at most 20)."""
    if not getattr(agent, "track_requests", False):
        return []
    out = []
    for item in agent.requests.to_json()["items"]:
        if item["requires_completion"] and item["status"] not in ("done", "cancelled", "cancelled_by_user"):
            out.append({"id": item["id"], "status": item["status"], "reason": item["reason"],
                        "preview": " ".join(item["text"].split())[:120]})
    return out[-20:]


class SessionCommands:
    def __init__(self, turns, emit, *, on_model_changed=None, on_conversation_replaced=None):
        self.turns = turns
        self.emit = emit
        # Worker hooks (subagents): follow a model switch; drop background work of a replaced conversation.
        self.on_model_changed = on_model_changed or (lambda agent: None)
        self.on_conversation_replaced = on_conversation_replaced or (lambda: None)

    @staticmethod
    def handles(kind) -> bool:
        return kind in TYPES

    # ----- helpers --------------------------------------------------------------
    def _agent(self):
        agent = self.turns.agent
        if agent is None:
            raise ValueError("Configure a provider and workspace first.")
        return agent

    def _idle_agent(self, action: str):
        agent = self._agent()
        if self.turns.busy:
            raise ValueError(f"Stop the active agent turn before {action}.")
        return agent

    def _background(self, name: str, request_id, work, on_error=None) -> None:
        def run():
            try:
                event = work()
                if event is not None:
                    if request_id is not None:
                        event.setdefault("id", request_id)
                    self.emit(event)
            except Exception as exc:
                text = str(exc)[:2000] if isinstance(exc, (ValueError, OSError, ProviderError)) else f"{name} failed ({type(exc).__name__})."
                self.emit(on_error(text) if on_error else {"event": "error", "id": request_id, "source": name, "text": text})
        threading.Thread(target=run, name=f"relay-{name}", daemon=True).start()

    # ----- dispatch -----------------------------------------------------------------
    def handle(self, kind: str, request: dict) -> None:
        getattr(self, "_" + kind)(request)

    def _set_model(self, request):
        agent = self._idle_agent("switching model")
        config = provider_config(request)
        window = request.get("context_window")
        agent.set_model(config, request.get("preset") if isinstance(request.get("preset"), str) else None,
                        validate_window(window) if window is not None else None)
        preset = resolve_preset(request.get("preset"), config.base_url, config.model)
        self.on_model_changed(agent)
        self.emit({"event": "model_changed", "id": request.get("id"), "model": config.model,
                   "preset": preset.id if preset else None, "context_window": agent.context.window,
                   "effort": agent.effort})
        self.emit(agent.context_event())

    def _set_effort(self, request):
        agent = self._agent()
        applied = agent.set_effort(validate_effort(request.get("effort")))
        self.emit({"event": "effort_changed", "id": request.get("id"), "effort": agent.effort, "applied": applied})

    def _context(self, request):
        event = self._agent().context_event()
        event["id"] = request.get("id")
        self.emit(event)

    def _compact(self, request):
        focus = request.get("focus")
        if focus is not None and (not isinstance(focus, str) or len(focus) > 2000):
            raise ValueError("focus must be text of at most 2000 characters.")

        def task(agent):
            agent.compact("manual", focus or None)
            agent.autosave()
        self.turns.run_exclusive("compact", task)

    def _checkpoints(self, request):
        self.emit({"event": "checkpoints", "id": request.get("id"), "items": self._agent().checkpoint_listing()})

    def _rewind(self, request):
        agent = self._idle_agent("rewinding")
        turn = request.get("turn")
        if type(turn) is not int:
            raise ValueError("turn must be an integer from the checkpoints list.")
        event = agent.rewind(turn, request.get("restore", "both"))
        event["id"] = request.get("id")
        self.emit(event)
        self.emit(agent.context_event())

    def _fork(self, request):
        agent = self._idle_agent("forking")
        turn = request.get("turn")
        if turn is not None and type(turn) is not int:
            raise ValueError("turn must be an integer.")
        self.emit({"event": "fork_state", "id": request.get("id"), "state": agent.fork(turn)})

    def _load_state(self, request):
        agent = self._idle_agent("loading a conversation")
        self.turns.clear()
        self.on_conversation_replaced()
        event = agent.load_state(request.get("state"))
        event["id"] = request.get("id")
        self.emit(event)
        agent.announce_requests()
        self.emit({"event": "mode_changed", "mode": agent.mode})
        self.emit(agent.context_event())

    def _sessions(self, request):
        agent = self._agent()
        items = agent.store.listing() if agent.store else []
        self.emit({"event": "sessions", "id": request.get("id"), "items": items})

    def _resume(self, request):
        agent = self._idle_agent("resuming a session")
        self.turns.clear()
        self.on_conversation_replaced()
        # Protocol v1 names the session "id"; "session_id" is also accepted.
        event = agent.resume(request.get("session_id", request.get("id")))
        self.emit(event)
        agent.announce_requests()
        self.emit({"event": "mode_changed", "mode": agent.mode})
        self.emit(agent.context_event())
        self._start_recap(agent, "resume", None)

    def _recap_request(self, request):
        reason = request.get("reason", "manual")
        if reason not in suggestions.RECAP_REASONS:
            raise ValueError('reason must be "away", "resume" or "manual".')
        self._start_recap(self._agent(), reason, request.get("id"))

    def _start_recap(self, agent, reason: str, request_id) -> None:
        messages, turns = list(agent.messages), agent.turns
        provider = agent.side_provider(cheap=True)
        open_items = open_request_items(agent)

        def work():
            event = suggestions.recap(provider, messages, turns, reason)
            event["open_items"] = open_items
            return event
        self._background("recap", request_id, work,
                         lambda text: {"event": "recap", "id": request_id, "skipped": "failed", "error": text,
                                       "reason": reason, "turns_covered": turns, "open_items": open_items})

    # ----- request ledger and todos (protocol section 12) ------------------------------------
    def _tracking_agent(self):
        agent = self._agent()
        if not agent.track_requests:
            raise ValueError("Request tracking is off for this agent.")
        return agent

    def _requests(self, request):
        agent = self._tracking_agent()
        self.emit({**agent.requests.event(agent.todos.items), "id": request.get("id")})

    def _request_get(self, request):
        agent = self._tracking_agent()
        item = agent.requests.get(check_ledger_id(request.get("ledger_id")))
        self.emit({"event": "request", "id": request.get("id"),
                   "item": agent.requests.entry(item, agent.todos.items, full=True)})

    def _request_set(self, request):
        agent = self._tracking_agent()
        ledger_id = check_ledger_id(request.get("ledger_id"))
        status = request.get("status")
        agent.requests.set_status(ledger_id, status, request.get("reason"))
        agent.autosave()

    def _request_reask(self, request):
        agent = self._tracking_agent()
        item = agent.requests.get(check_ledger_id(request.get("ledger_id")))
        if item["status"] == "in_progress" or self.turns.waiting(item["queue_item"]):
            raise ValueError("That request is already running or queued.")
        paths = [{"path": p} for p in item["attachments"]]
        loaded = attachments.load(paths, agent.executor.workspace.root) if paths else None
        self.turns.submit(item["text"], request.get("when", "queue"), request.get("id"), None, loaded or None,
                          ledger_id=item["id"])
        item["reasked"] += 1

    def _todos(self, request):
        agent = self._tracking_agent()
        self.emit({**agent.todos.event(None), "id": request.get("id")})

    def _set_mode(self, request):
        agent = self._agent()
        agent.set_mode(planning.validate_mode(request.get("mode")))
        self.emit({"event": "mode_changed", "id": request.get("id"), "mode": agent.mode})

    def _plan_execute(self, request):
        agent = self._agent()
        path = request.get("path")
        content = planning.read_plan(path)
        prompt = planning.execution_prompt(path, content)
        if len(prompt.encode("utf-8")) > 131072:
            prompt = (f"Execute the plan in {path}. It is too long to include here; read it with run_command "
                      f"(for example `cat {path}`) before starting.")
        fresh = request.get("fresh", False)
        if type(fresh) is not bool:
            raise ValueError("fresh must be a boolean.")
        if fresh:
            self.turns.reset()
            self.on_conversation_replaced()
            self.emit({"event": "reset"})
        agent.set_mode("build")
        self.emit({"event": "mode_changed", "mode": "build"})
        self.turns.submit(prompt, request.get("when", "now"), request.get("id"))

    def _scan_instructions(self, request):
        workspace = request.get("workspace")
        if workspace is None and self.turns.agent is not None:
            workspace = str(self.turns.agent.executor.workspace.root)
        if not isinstance(workspace, str) or not os.path.isdir(workspace):
            raise ValueError("scan_instructions needs an existing workspace directory.")
        self.emit({"event": "instructions_found", "id": request.get("id"), "items": instructions.scan(workspace)})

    def _synthesize_instructions(self, request):
        agent = self._agent()
        files = request.get("files")
        target = request.get("target")
        if target is not None and (not isinstance(target, str) or not os.path.isabs(os.path.expanduser(target))):
            raise ValueError("target must be an absolute path.")
        if not isinstance(files, list) or not files:
            raise ValueError("files must be a non-empty list of absolute paths.")
        provider = agent.side_provider()

        def work():
            text = instructions.synthesize(provider, files)
            path = instructions.write_synthesized(target, text)
            return {"event": "instructions_synthesized", "id": request.get("id"), "path": str(path),
                    "bytes": len(text.encode("utf-8"))}
        self._background("synthesize_instructions", request.get("id"), work)

    def _suggest(self, request):
        agent = self._agent()
        kind = request.get("kind")
        suggestion_id = request.get("id") if isinstance(request.get("id"), str) else uuid.uuid4().hex
        provider = agent.side_provider(cheap=True)
        if kind == "next_command":
            suggestions.validate_next_command(request)
            work = lambda: {**suggestions.next_command(provider, request), "id": suggestion_id}  # noqa: E731
        elif kind == "next_prompt":
            if agent.mode == "plan":
                self.emit({"event": "suggestion", "kind": kind, "id": suggestion_id, "text": "", "reason": "plan_mode"})
                return
            messages, turns = list(agent.messages), agent.turns
            work = lambda: {**suggestions.next_prompt(provider, messages, turns), "id": suggestion_id}  # noqa: E731
        else:
            raise ValueError('suggest kind must be "next_command" or "next_prompt".')
        self._background("suggest", suggestion_id, work)
