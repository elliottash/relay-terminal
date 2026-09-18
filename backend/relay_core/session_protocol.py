# SPDX-License-Identifier: GPL-3.0-or-later
"""Worker protocol handlers for sessions, model/effort, context, checkpoints, plan mode,
instructions, recaps, suggestions, pane titles and aliases (docs/AGENT-SESSIONS-PROTOCOL.md
sections 1-7, 9, 10, 17, 18, 20).

Conversation rewrites (compact) run through TurnSupervisor.run_exclusive so they never overlap a
turn. Calls that only read a copy of the conversation (recaps, suggestions, synthesis) run on
their own threads with a separate provider, so the protocol loop never blocks on the network.
"""
from __future__ import annotations

import os
import threading
import uuid

from . import (alias_import, aliases, attachments, conv_index, instructions, keystore, logs,
               planning, suggestions, titles)
from .agent import validate_turn_options
from .requests import check_ledger_id
from .context import validate_threshold, validate_window
from .presets import PRESETS, match_preset, resolve_preset, validate_effort
from .provider import ProviderConfig, ProviderError
from .sessions import SessionStore, check_id, default_session_dir

_log = logs.get("aliases")

TYPES = {"set_model", "set_effort", "context", "compact", "checkpoints", "rewind", "fork", "load_state",
         "sessions", "resume", "recap_request", "set_mode", "plan_execute", "scan_instructions",
         "synthesize_instructions", "suggest",
         # pane title and tab label (protocol section 18)
         "set_session_title", "tab_label",
         # aliases: saved commands and prompts (protocol section 20)
         "aliases", "alias_run", "alias_save", "alias_delete",
         "alias_import_preview", "alias_import_apply",
         # request ledger and todos (protocol section 12)
         "requests", "request_get", "request_set", "request_reask", "todos",
         # conversation list and full-text search (protocol section 14)
         "conversations", "conversation_get", "conversation_delete", "conversation_rename",
         "conversation_pin", "terminal_history", "index_rebuild"}


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
                            request.get("extra", {}), request.get("max_tokens", 32768))
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
        # Conversation index (protocol 14), opened on the first conversation command.
        self._index = None
        # Alias import previews (protocol 20), held until the matching apply names one.
        self._alias_previews: dict[str, list] = {}
        self._alias_lock = threading.Lock()

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
        provider = agent.side_provider(cheap=True, role="summaries")
        open_items = open_request_items(agent)
        # The span the recap states comes off the recorded turn stamps, never the model's text
        # (owner request, 2026-09-17), so the stamps are snapshotted with the messages: the
        # background thread must not read a checkpoint list a later turn is appending to.
        turn_items = [{"time": item.get("time"), "ended": item.get("ended")}
                      for item in agent.checkpoints.items]

        def work():
            event = suggestions.recap(provider, messages, turns, reason, turn_items=turn_items)
            event["open_items"] = open_items
            return event
        self._background("recap", request_id, work,
                         lambda text: {"event": "recap", "id": request_id, "skipped": "failed", "error": text,
                                       "reason": reason, "turns_covered": turns, "open_items": open_items})

    # ----- pane title and tab label (protocol section 18) -----------------------------------
    def _set_session_title(self, request):
        """Name this pane by hand. An empty title hands the name back to the model, which writes a
        fresh one straight away rather than at the next cadence point."""
        title = request.get("title")
        if title is not None and not isinstance(title, str):
            raise ValueError("title must be text.")
        agent = self._agent()
        event = agent.set_title(title or "", "user")
        self.emit({**event, "id": request.get("id")})
        if event["source"] != "user":
            self.maybe_title()

    def observe(self, event: dict) -> None:
        """Worker emit hook for main-turn events: a finished turn may be owed a fresh pane title."""
        if event.get("event") in ("done", "error", "cancelled"):
            self.maybe_title()

    def maybe_title(self) -> None:
        """Write the pane title on a cheap chores-role side call, when the cadence says one is owed.

        Off the protocol thread with its own provider, like recaps: the GUI never waits for it, and
        stopping the turn never cancels it. A failure is not an error - the first-prompt title in
        `session_title` still names the pane.
        """
        agent = self.turns.agent
        if agent is None:
            return
        claim = agent.claim_title()
        if claim is None:
            return
        try:
            provider = agent.side_provider(cheap=True, role="chores", max_tokens=titles.MAX_TOKENS)
        except Exception:
            provider = None

        def work():
            text = ""
            try:
                if provider is not None:
                    text = titles.generate(provider, claim["messages"], threading.Event())
            except Exception:
                text = ""
            return agent.release_title(text, claim)
        self._background("session_title", None, work, lambda _text: agent.release_title("", claim) or agent.title_event())

    def _tab_label(self, request):
        """One label for a tab from the titles its panes already have: no extra title call, just a
        cheap "same work or not" judgement on the chores role. Never an error: a failed call falls
        back to the plain-text comparison, which is what the GUI shows in the meantime."""
        items = request.get("titles")
        if not isinstance(items, list) or len(items) > 32 or not all(isinstance(t, str) for t in items):
            raise ValueError("titles must be a list of at most 32 strings.")
        items = [t[:400] for t in items]
        request_id = request.get("id")
        agent = self.turns.agent
        provider = agent.side_provider(cheap=True, role="chores", max_tokens=titles.MAX_TOKENS) if agent else None
        offline = {"event": "tab_label", "id": request_id, **titles.label(None, items)}

        def work():
            try:
                return {"event": "tab_label", "id": request_id, **titles.label(provider, items)}
            except Exception:
                return offline
        self._background("tab_label", request_id, work, lambda text: offline)

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

    # ----- conversation list and full-text search (protocol section 14) ----------------------
    def index(self):
        """The shared conversation index, opened on first use. Raises when it is off."""
        if self._index is None:
            if not conv_index.enabled():
                raise ValueError("The conversation index is disabled (RELAY_INDEX=off).")
            self._index = conv_index.ConversationIndex(rebuild_on_reset=True)
        return self._index

    def _workspace(self, request) -> str:
        """The workspace a conversation query is scoped to: the request's, else this pane's."""
        workspace = request.get("workspace")
        if isinstance(workspace, str) and workspace:
            return os.path.expanduser(workspace)
        agent = self.turns.agent
        return str(agent.executor.workspace.root) if agent is not None else ""

    @staticmethod
    def _conversation_id(value) -> str:
        if isinstance(value, str) and conv_index.TERMINAL_ID.match(value):
            return value
        return check_id(value)

    def _conversations(self, request):
        for name in ("model",):
            if request.get(name) is not None and not isinstance(request.get(name), str):
                raise ValueError(f"{name} must be text.")
        sources = request.get("sources")
        if sources is not None and (not isinstance(sources, list) or not all(isinstance(s, str) for s in sources)):
            raise ValueError("sources must be a list of \"agent\" and/or \"terminal\".")
        result = self.index().search(
            request.get("query", "") or "", scope=request.get("scope", "project") or "project",
            workspace=self._workspace(request), model=request.get("model") or None,
            has_open=bool(request.get("has_open_tasks")), since=request.get("since"),
            until=request.get("until"), sources=sources, limit=request.get("limit", 50))
        self.emit({"event": "conversations", "id": request.get("id"),
                   "scope": request.get("scope", "project") or "project",
                   "workspace": self._workspace(request), **result})

    def _conversation_get(self, request):
        session_id = self._conversation_id(request.get("session_id", request.get("id")))
        turn = request.get("turn")
        if turn is not None and type(turn) is not int:
            raise ValueError("turn must be an integer.")
        data = self.index().conversation(session_id, turn, request.get("query", "") or "",
                                         request.get("limit", 400))
        self.emit({"event": "conversation", "id": request.get("id"), **data})

    def _conversation_delete(self, request):
        session_id = self._conversation_id(request.get("session_id"))
        index = self.index()
        removed = {"session_id": session_id, "files": 0}
        if session_id.startswith("term-"):
            index.delete_session(session_id, remove_files=False)
        else:
            directory = ""
            try:
                directory = index.conversation(session_id).get("session_dir") or ""
            except ValueError:
                directory = ""
            agent = self.turns.agent
            if not directory and agent is not None and agent.store is not None:
                directory = str(agent.store.directory)
            if not directory:
                raise ValueError("That conversation is not in the index; nothing to delete.")
            store = SessionStore(directory, index=index)
            try:
                removed = store.delete(session_id)
            except ValueError:
                index.delete_session(session_id, remove_files=False)
            # Deleting the conversation this pane is showing starts a fresh one.
            if agent is not None and agent.session_id == session_id and not self.turns.busy:
                self.turns.reset()
                self.on_conversation_replaced()
                self.emit({"event": "reset"})
        self.emit({"event": "conversation_deleted", "id": request.get("id"),
                   "session_id": session_id, **{k: v for k, v in removed.items() if k != "session_id"}})

    def _conversation_rename(self, request):
        session_id = self._conversation_id(request.get("session_id"))
        title = request.get("title")
        if title is not None and not isinstance(title, str):
            raise ValueError("title must be text.")
        self.index().rename(session_id, title or "")
        self.emit({"event": "conversation_renamed", "id": request.get("id"), "session_id": session_id,
                   "title": " ".join((title or "").split())[:200]})

    def _conversation_pin(self, request):
        session_id = self._conversation_id(request.get("session_id"))
        pinned = request.get("pinned", True)
        if type(pinned) is not bool:
            raise ValueError("pinned must be a boolean.")
        self.index().set_pinned(session_id, pinned)
        self.emit({"event": "conversation_pinned", "id": request.get("id"), "session_id": session_id,
                   "pinned": pinned})

    def _terminal_history(self, request):
        # Sent for every command the pane ran; with the index off it is simply dropped, so a
        # disabled index never turns each command into an error.
        if not conv_index.enabled():
            return
        workspace = self._workspace(request)
        count = self.index().record_commands(workspace, request.get("items") or [])
        if request.get("id") is not None:
            self.emit({"event": "terminal_history_indexed", "id": request.get("id"),
                       "workspace": workspace, "rows": count})

    def _index_rebuild(self, request):
        index = self.index()
        request_id = request.get("id")
        self._background("index_rebuild", request_id,
                         lambda: {"event": "index_rebuilt", **index.rebuild(), **index.stats()})

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

    # ----- aliases: saved commands and prompts (protocol section 20) ------------------
    # Reading and running an alias needs no provider and no configured agent: the palette wants
    # the list before a key is entered. Only the suggestion is a model call.

    def _alias_scope(self, request, default=None):
        scope = request.get("scope")
        if scope is None:
            return default
        if scope not in aliases.SCOPES:
            raise ValueError('scope must be "local" or "global".')
        return scope

    def _emit_aliases(self, request_id, workspace):
        catalog, problems = aliases.catalog(workspace)
        self.emit({"event": "aliases", "id": request_id, "workspace": workspace,
                   "items": [a.to_dict() for a in catalog], "problems": problems})

    def _aliases(self, request):
        self._emit_aliases(request.get("id"), self._workspace(request))

    def _alias_run(self, request):
        """Expand one alias. The worker does the substitution, so the quoting rules that make a
        parameter value data rather than syntax live in one place (relay_core/aliases.py)."""
        name = request.get("name")
        if not isinstance(name, str) or not name.strip():
            raise ValueError("alias_run needs the alias name.")
        values = request.get("values") or {}
        if not isinstance(values, dict):
            raise ValueError("values must be an object of parameter names to text.")
        workspace = self._workspace(request)
        try:
            alias = aliases.resolve(name, workspace, self._alias_scope(request))
            text = aliases.fill(alias, values)
        except aliases.AliasError as exc:
            raise ValueError(str(exc)) from None
        self.emit({"event": "alias_expanded", "id": request.get("id"), "name": alias.name,
                   "kind": alias.kind, "scope": alias.scope, "text": text,
                   "title": alias.title, "path": alias.path})

    def _alias_save(self, request):
        scope = self._alias_scope(request, "local")
        params = request.get("params") or []
        if not isinstance(params, list):
            raise ValueError("params must be a list.")
        alias = aliases.Alias(
            name=str(request.get("name") or ""), kind=str(request.get("kind") or "command"),
            title=str(request.get("title") or ""), description=str(request.get("description") or ""),
            text=str(request.get("text") or ""),
            params=[aliases.Param(str(p.get("name") or ""), p.get("default"),
                                  str(p.get("description") or ""))
                    for p in params if isinstance(p, dict)],
            labels=[str(x) for x in (request.get("labels") or [])],
            source=str(request["source"]) if request.get("source") else None,
            status=str(request.get("status") or "active"))
        workspace = self._workspace(request)
        try:
            saved = aliases.save(alias, workspace, scope)
        except aliases.AliasError as exc:
            raise ValueError(str(exc)) from None
        logs.event(_log, "alias saved", name=saved.name, kind=saved.kind, scope=scope)
        self.emit({"event": "alias_saved", "id": request.get("id"), "name": saved.name,
                   "kind": saved.kind, "scope": scope, "path": saved.path, "alias_id": saved.card_id})
        self._emit_aliases(None, workspace)

    def _alias_delete(self, request):
        name = request.get("name")
        if not isinstance(name, str) or not name.strip():
            raise ValueError("alias_delete needs the alias name.")
        scope = self._alias_scope(request, "local")
        workspace = self._workspace(request)
        try:
            path = aliases.delete(name, workspace, scope)
        except (aliases.AliasError, OSError) as exc:
            raise ValueError(str(exc)) from None
        self.emit({"event": "alias_deleted", "id": request.get("id"), "name": name,
                   "scope": scope, "path": path})
        self._emit_aliases(None, workspace)

    def _alias_import_preview(self, request):
        """Read Warp's workflows and the shell startup files and say what *would* be written.

        Nothing is executed and nothing is written. The result is held here, so the matching
        apply writes bytes this worker read itself rather than text handed back over the pipe.
        """
        sources = request.get("sources")
        if sources is not None and not isinstance(sources, list):
            raise ValueError("sources must be a list of \"warp\" and/or \"shell\".")
        workspace = self._workspace(request)
        request_id = request.get("id")

        def work():
            result = alias_import.preview(tuple(str(s) for s in sources) if sources else alias_import.SOURCES,
                                          workspace=workspace)
            token = uuid.uuid4().hex
            with self._alias_lock:
                self._alias_previews = {token: result["items"]}
            logs.event(_log, "alias import previewed",
                       items=len(result["items"]), skipped=len(result["skipped"]))
            return {**result, "preview_id": token, "workspace": workspace}
        self._background("alias_import_preview", request_id, work)

    def _alias_import_apply(self, request):
        token = request.get("preview_id")
        names = request.get("names")
        if not isinstance(token, str) or not token:
            raise ValueError("alias_import_apply needs the preview_id from the preview.")
        if not isinstance(names, list) or not names:
            raise ValueError("Choose at least one alias to import.")
        renames = request.get("renames") or {}
        if not isinstance(renames, dict):
            raise ValueError("renames must be an object of preview names to new names.")
        scope = self._alias_scope(request, "global")
        workspace = self._workspace(request)
        with self._alias_lock:
            items = self._alias_previews.get(token)
        if items is None:
            raise ValueError("That preview has expired. Preview the import again.")
        try:
            result = alias_import.apply(items, names, scope, workspace, renames)
        except aliases.AliasError as exc:
            raise ValueError(str(exc)) from None
        logs.event(_log, "aliases imported", written=len(result["written"]),
                   failed=len(result["failed"]), scope=scope)
        self.emit({**result, "id": request.get("id")})
        self._emit_aliases(None, workspace)

    def _suggest(self, request):
        agent = self._agent()
        kind = request.get("kind")
        suggestion_id = request.get("id") if isinstance(request.get("id"), str) else uuid.uuid4().hex
        provider = agent.side_provider(cheap=True, role="suggestions")
        if kind == "alias":
            # The agent may propose an alias for a command the user keeps re-typing. A suggestion
            # only: it is logged here and shown, and nothing is written unless the user saves it.
            history = request.get("commands")
            if not isinstance(history, list):
                raise ValueError("suggest kind alias needs the recent commands.")
            repeated = aliases.repeats(history, suggestions.ALIAS_MIN_COUNT)
            if not repeated:
                self.emit({"event": "suggestion", "kind": kind, "id": suggestion_id,
                           "text": "", "reason": "no_repeats"})
                return
            logs.event(_log, "alias suggestion requested",
                       repeats=len(repeated), top=repeated[0]["count"])

            def work():
                event = suggestions.propose_alias(provider, repeated)
                if event.get("alias"):
                    logs.event(_log, "alias suggested",
                               name=event["alias"]["name"], reason=event.get("reason"))
                return {**event, "id": suggestion_id}
            self._background("suggest", suggestion_id, work)
            return
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
        # A failed suggestion is reported as a suggestion, not as a bare protocol error: the GUI can
        # then say which side call failed and on which model, and a background failure never looks
        # like the agent turn erroring out (#308N).
        model = agent.role_model("suggestions")
        self._background("suggest", suggestion_id, work,
                         lambda text: {"event": "suggestion", "kind": kind, "id": suggestion_id,
                                       "text": "", "error": text, "model": model})
