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
import sqlite3
import threading
import time
import uuid
import weakref
from urllib.parse import urlsplit

from . import (alias_import, aliases, attachments, conv_index, guest_harness_provider,
               guest_sessions, instructions, keystore, localmodels, logs, planning, suggestions,
               titles)
from .agent import validate_turn_options
from .requests import check_ledger_id
from .context import validate_threshold, validate_window
from .presets import PRESETS, match_preset, resolve_preset, validate_effort
from .provider import AUTOMATIC_OUTPUT_TOKENS, ProviderConfig, ProviderError
from . import sessions as session_files
from .sessions import SessionStore, check_id, default_session_dir

_log = logs.get("aliases")
_guest_log = logs.get("guest_sessions")

# The `sources` a `conversations` request may name, for the one error message that lists them.
LISTABLE_SOURCES = ", ".join(f'"{name}"' for name in conv_index.SOURCES)
# A guest session id is the guest's own (protocol 26.7); this only stops an unbounded string
# reaching the index as a lookup key.
MAX_GUEST_ID = 200
# How often a `conversations` request may set a guest reconcile going (seconds). The pane queries
# on every keystroke; the guests' files do not change that fast.
GUEST_RECONCILE_EVERY = 5.0
# How often the *pane's own* guest is tailed (protocol 26.7), and how often a tail whose guest has
# not written its first line yet tries again. A poll is a stat and, only when the file grew, the
# bytes the guest appended, which is why it may run far oftener than the reconcile — the whole
# point is that the row moves while the user is watching it rather than at the next scan.
GUEST_TAIL_POLL_EVERY = 0.5
GUEST_TAIL_RETRY_EVERY = 1.0

TYPES = {"set_model", "set_effort", "context", "compact", "checkpoints", "rewind", "fork", "load_state",
         "sessions", "resume", "recap_request", "set_mode", "plan_execute", "scan_instructions",
         "synthesize_instructions", "suggest",
         # pane title and tab label (protocol section 18)
         "set_session_title", "tab_label",
         # session summaries, on demand and in a batch (protocol section 18.4)
         "conversation_summarize", "conversations_summarize_estimate",
         "conversations_summarize_all", "conversations_summarize_cancel",
         # aliases: saved commands and prompts (protocol section 20)
         "aliases", "alias_run", "alias_save", "alias_delete",
         "alias_import_preview", "alias_import_apply",
         # request ledger and todos (protocol section 12)
         "requests", "request_get", "request_set", "request_reask", "todos",
         # conversation list and full-text search (protocol section 14)
         "conversations", "conversation_get", "conversation_delete", "conversation_rename",
         "conversation_pin", "terminal_history", "index_rebuild",
         # session info and the thread history (protocol section 25)
         "session_info"}


def provider_name(preset_id: str, base_url: str = "") -> str:
    """How to name a provider in an error a person reads: its preset label, else its host."""
    if preset_id in PRESETS:
        return f"{PRESETS[preset_id].label} ({preset_id})"
    if localmodels.find(preset_id) is not None:
        return f"{localmodels.find(preset_id).label} ({preset_id})"
    host = urlsplit(base_url).hostname if base_url else ""
    return host or "this provider"


def provider_config(request: dict) -> ProviderConfig:
    """ProviderConfig from a configure/set_model request, using the stored key when asked.

    A request that names a built-in preset may leave out ``base_url``, ``model`` and ``extra``: the
    preset supplies them, exactly as a ``roles`` entry does (protocol 13.4). That is what keeps a key
    and an endpoint together. A caller that assembles the two out of separate settings can otherwise
    name one preset and pass another provider's URL, and the stored key for the named preset is then
    posted to a foreign endpoint - which is an HTTP 401 and nothing more legible. The Switchboard did
    exactly that until 2026-09-18.
    """
    named = request.get("preset")
    if guest_harness_provider.is_guest_preset(named):
        # A guest agent is a preset too (protocol 29.3): its "endpoint" is a process this worker
        # starts, so there is no key, no URL to validate and no model until the guest says which
        # one it is running.
        return guest_harness_provider.config_for_preset(named, request)
    api_key = request.get("api_key", "")
    if not isinstance(api_key, str):
        raise ValueError("API key must be text.")
    preset = PRESETS.get(named) if isinstance(named, str) else None
    if preset is None and localmodels.is_local_id(named):
        # A model server on this machine (protocol 28): the registry supplies URL and model.
        endpoint = localmodels.find(named)
        if endpoint is None:
            raise ValueError(f"No local endpoint {named!r} is saved. Add it with scripts/relay-local.py add, "
                             "or pick another model.")
        preset = endpoint.as_preset()
    base_url = str(request.get("base_url") or "") or (preset.base_url if preset else "")
    model = str(request.get("model") or "") or (preset.model if preset else "")
    extra = request.get("extra")
    if extra is None:
        extra = dict(preset.extra) if preset else {}
    # Plain HTTP to a loopback host is a local model server: no key exists, none is looked up and
    # none is sent, whatever the request carried. Anything else keeps the rule below.
    local = localmodels.provider_fields(preset.id if preset else None, base_url, model)
    # Relay Free (protocol 13.9) has no key either: the transport takes a token before each call.
    # Only the preset's own endpoint is hosted; a request that names it and points elsewhere is a
    # custom endpoint that needs a key like any other.
    is_hosted = bool(preset is not None and preset.hosted and base_url == preset.base_url)
    if local or is_hosted:
        api_key = ""
    elif not api_key and request.get("use_stored_key"):
        # The key never crosses the frontend pipe in this path.
        preset_id = preset.id if preset else ""
        if not preset_id:
            # "Custom" settings that point at a known endpoint still use its stored key.
            match = match_preset(base_url, model)
            preset_id = match.id if match else ""
        api_key = keystore.lookup(preset_id) if preset_id else ""
        if not api_key:
            raise ValueError(f"No stored key for {provider_name(preset_id, base_url)}. "
                             "Import from Warp or enter a key.")
    config = ProviderConfig(base_url, model, api_key, extra,
                            localmodels.clamp_max_tokens(
                                request.get("max_tokens", AUTOMATIC_OUTPUT_TOKENS), local),
                            hosted=is_hosted, **local)
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
    # Protocol 29.3: a pane whose agent is a guest harness says which guest, and which session of
    # the guest's own, so the GUI can label the pane and the sessions row can be resumed.
    fields.update(guest_harness_provider.configured_fields(agent))
    _note_configured(agent)
    if agent.instructions:
        fields["instructions_max_bytes"] = agent.instructions.cap
        fields["instructions_bytes"] = len(agent.instructions.section.encode("utf-8"))
        if agent.instructions.truncated:
            fields["instructions_truncated"] = agent.instructions.truncated
        if agent.instructions.skipped:
            fields["instructions_skipped"] = agent.instructions.skipped[:50]
    return fields


# This worker's `SessionCommands`, weakly. `configure` is handled in `backend/worker.py`, which
# calls `configured_fields()` above with the new Agent and holds no reference to the commands
# object; a worker is one pane and one `SessionCommands`, so the instance registers itself here
# and that call is the hand-off. Nothing else uses it.
_COMMANDS = None


def _note_configured(agent) -> None:
    """A `configure` built a new Agent for this pane: hand it to the session commands, which
    follow the pane's guest session while it runs (protocol 26.7, `SessionCommands.note_agent`)."""
    commands = _COMMANDS() if _COMMANDS is not None else None
    if commands is not None:
        commands.note_agent(agent)


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
    # Whether Relay indexes the guests' own sessions (Options › Privacy, `sessions/index_guests`,
    # protocol 26.7, review B1). The pane sends it with every `conversations` request; None means
    # "as the environment says" (`guest_sessions.guests_enabled()`, `RELAY_INDEX_GUESTS`): on.
    index_guests: bool | None = None

    def __init__(self, turns, emit, *, on_model_changed=None, on_conversation_replaced=None, subagents=None):
        self.turns = turns
        self.emit = emit
        # The worker's SubagentManager, for the live state of a thread that is still running.
        self.subagents = subagents
        # Worker hooks (subagents): follow a model switch; drop background work of a replaced conversation.
        self.on_model_changed = on_model_changed or (lambda agent: None)
        self.on_conversation_replaced = on_conversation_replaced or (lambda: None)
        # Conversation index (protocol 14), opened on the first conversation command.
        self._index = None
        # Alias import previews (protocol 20), held until the matching apply names one.
        self._alias_previews: dict[str, list] = {}
        self._alias_lock = threading.Lock()
        # The pane's live guest session (protocol 26.7), followed while it runs. One tail per
        # worker, built on first use; see `_tail_poll`. `_tail_at` is the single throttle — the
        # GuestTail's own is turned off (`min_poll=0`) so there are not two of them disagreeing.
        self._tail: guest_sessions.GuestTail | None = None
        self._tail_lock = threading.Lock()
        self._tail_want: tuple[str, str, str] | None = None   # (source, workspace, session id)
        self._tail_on = False        # `start()` has found the transcript and the rows are being kept
        self._tail_owner = ""        # "agent" (Tier A) or "program" (Tier B); see `_follow`
        self._tail_at = 0.0          # monotonic of the last poll or start attempt
        global _COMMANDS
        _COMMANDS = weakref.ref(self)

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
        """Switch the pane's model, keeping the conversation. Accepted while a turn runs (issue 3ES1):
        the request in flight finishes on the old model and the switch lands before the next one,
        which `model_applied` announces; idle, it applies at once as it always did. A conversation
        over the new window's limit is compacted first (by the model in force); one that cannot fit
        the new window at all is refused with `model_switch_refused`, and the model stays."""
        agent = self._agent()
        # Resolved now, turn or no turn: a missing key is refused here, not at the next step.
        config = provider_config(request)
        window = request.get("context_window")
        window = validate_window(window) if window is not None else None
        preset_id = request.get("preset") if isinstance(request.get("preset"), str) else None
        # Protocol 29.3: switching to a `guest:` preset starts the guest's harness *here*, for the
        # same reason the key is looked up here — a guest that cannot start refuses the switch at
        # the request and the pane keeps the model it has, rather than failing at the next step.
        # Staying on the same guest and only naming another of its models keeps the harness (and
        # with it the guest's own context); every other move to a guest starts a fresh one.
        guest_id = guest_harness_provider.preset_guest_id(preset_id)
        guest_provider = guest_harness_provider.switch_model(agent, guest_id, request)
        restart_guest = guest_id is not None and guest_provider is None
        if restart_guest:
            guest_provider = guest_harness_provider.start_provider(
                preset_id, request, str(agent.executor.workspace.root), agent.stall_timeout_s)
        if guest_provider is not None:
            config = guest_provider.config
        preset = resolve_preset(preset_id, config.base_url, config.model)
        agent.on_model_applied = self.on_model_changed

        def apply_now():
            # Leaving a guest ends its process and hands provider-building back to the Agent; a
            # guest replacing a guest is a restart, which is what 29.3 says a guest-to-guest
            # switch is (the Relay conversation is kept, the guest's context is not).
            if restart_guest or guest_id is None:
                guest_harness_provider.detach(agent)
            if guest_provider is not None:
                agent.set_model(config, preset_id, window, provider=guest_provider)
                guest_harness_provider.attach(agent, guest_provider)
            else:
                agent.set_model(config, preset_id, window)
            # Protocol 26.7: the pane's live guest session follows the switch — onto the new
            # harness's own session, or, for a pane that has just left its guest, onto nothing.
            self.follow_agent_guest(agent)
            self.on_model_changed(agent)

        def decide(idle: bool) -> dict:
            # Under the agent's model lock, so `model_changed` always precedes the `model_applied`
            # (or `model_switch_refused`) of the same switch.
            with agent._model_lock:
                outcome = agent.request_model(config, preset_id, window, idle=idle, apply_now=apply_now,
                                              start_exclusive=lambda task: self.turns.start_exclusive_locked(
                                                  "set_model", task))
                if outcome["applies"] == "refused":
                    if restart_guest and guest_provider is not None:
                        guest_provider.close()   # started for a switch that is not happening
                    self.emit({"event": "model_switch_refused", "id": request.get("id"), "at": "request",
                               "model": config.model, "current_model": agent.config.model,
                               "preset": agent.preset.id if agent.preset else None,
                               "context_window": agent.context.window, "effort": agent.effort,
                               "reason": outcome["reason"]})
                else:
                    changed = {"event": "model_changed", "id": request.get("id"), "model": config.model,
                               "preset": preset.id if preset else preset_id if guest_provider else None,
                               "effort": agent.effort, **outcome}
                    if guest_provider is not None:
                        changed["guest"] = guest_provider.guest_id
                        changed["guest_session"] = guest_provider.session_id
                        changed["guest_effort"] = guest_provider.effort
                    self.emit(changed)
                return outcome
        self.turns.now_or_later(lambda: decide(True), lambda: decide(False))
        # Every outcome moves the context bar: the window now in force, or the one about to be.
        self.emit(agent.context_event())

    def _set_effort(self, request):
        agent = self._agent()
        # Protocol 29.3: on a guest pane the effort is the guest's own knob (its command line or
        # its `turn/start`), not a parameter of a request Relay makes, so the harness is told and
        # nothing is written to the ProviderConfig. The guest's levels are its own (xhigh, ultra),
        # which is why `presets.validate_effort`'s four are not consulted on that path.
        guest_applied = guest_harness_provider.set_effort(agent, request.get("effort"))
        if guest_applied is not None:
            self.emit({"event": "effort_changed", "id": request.get("id"), **guest_applied})
            return
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
            # Protocol 29.3: on a guest pane, Compact means both context windows — the guest
            # compacts its own, and Relay compacts the transcript it keeps.
            provider = guest_harness_provider.agent_provider(agent)
            if provider is not None:
                provider.compact()
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
        # Protocol 29.3: the session file says which guest ran it and under which of the guest's own
        # sessions; a pane already on that guest points its harness back at the same one.
        if agent.store is not None:
            try:
                guest_harness_provider.resume_session(agent, agent.store.load(agent.session_id), self.emit)
            except (OSError, ValueError):
                pass
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
        """Worker emit hook for main-turn events: a finished turn may be owed a fresh pane title,
        and a guest pane's turn is the guest writing its transcript (`_guest_tick`)."""
        if event.get("event") in ("done", "error", "cancelled"):
            self.maybe_title()
        self._guest_tick()

    def maybe_title(self) -> None:
        """Write the pane title on a cheap chores-role side call, when the cadence says one is owed.

        Off the protocol thread with its own provider, like recaps: the GUI never waits for it, and
        stopping the turn never cancels it. A failure is not an error - the first-prompt title in
        `session_title` still names the pane.

        The session summary rides the same moments (section 18.4), so the two cheap calls happen
        together and neither one ever runs per turn.
        """
        self.maybe_summary()
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

    # ----- session summaries (protocol section 18.4) -----------------------------------------
    def _summary_state(self) -> dict:
        """State of the one batch this worker may run. Built on first use so the constructor,
        which other sessions share, stays as it is."""
        state = getattr(self, "_summaries", None)
        if state is None:
            state = {"lock": threading.Lock(), "cancel": threading.Event(), "running": False}
            self._summaries = state
        return state

    @staticmethod
    def _chores_provider(agent):
        """The cheap chores-role provider summaries run on, or None when none can be built."""
        if agent is None:
            return None
        try:
            return agent.side_provider(cheap=True, role="chores", max_tokens=titles.SUMMARY_MAX_TOKENS)
        except Exception:
            return None

    @staticmethod
    def _chores_model(agent) -> str:
        """Which model a summary would run on, for the estimate."""
        if agent is None:
            return ""
        try:
            resolved = agent.roles.resolve("chores") if agent.roles is not None else None
            if resolved is not None and not resolved.is_main:
                return resolved.config.model
        except Exception:
            pass
        return agent.config.model

    def maybe_summary(self, force: bool = False) -> None:
        """Write the session summary on a cheap chores-role call, when the cadence says one is owed.

        Started from maybe_title(), so the summary and the title share their moments; off the
        protocol thread, so no turn ever waits for it, and never twice at once per pane. A failure
        is not an error: the summary already stored stands.
        """
        agent = self.turns.agent
        if agent is None:
            return
        claim = agent.claim_summary(force=force)
        if claim is None:
            return
        provider = self._chores_provider(agent)

        def work():
            text = ""
            try:
                if provider is not None:
                    text = titles.generate_summary(provider, claim["messages"], threading.Event(),
                                                   files=claim["files"], todos=claim["todos"])
            except Exception:
                text = ""
            return agent.release_summary(text, claim)
        self._background("session_summary", None, work, lambda _text: agent.release_summary("", claim))

    def _summary_directory(self, request) -> str:
        directory = _abs_dir(request.get("session_dir"), "session_dir")
        if directory:
            return directory
        agent = self.turns.agent
        if agent is None or agent.store is None:
            raise ValueError("This pane has no saved sessions.")
        return str(agent.store.directory)

    def _summary_store(self, directory: str) -> SessionStore:
        """A store for a session this pane does not hold, sharing the worker's index."""
        return SessionStore(directory, index=self._index_or_none() or False)

    def _index_or_none(self):
        try:
            return self.index()
        except (ValueError, sqlite3.Error, OSError):
            return None

    def _summarize_saved(self, store: SessionStore, session_id: str, provider) -> tuple[str, str]:
        """Generate and store one saved session's summary. Returns (summary, error); never raises.

        The result goes to that session's `<id>.meta.json` and to the index, never to the session
        file: another worker may have it open and owns those bytes.
        """
        try:
            data = store.load(session_id)
        except (ValueError, OSError) as exc:
            return "", str(exc)[:300]
        inputs = titles.saved_inputs(data)
        if not titles.has_reply(inputs["messages"]):
            return "", "That session has no assistant reply to summarise."
        try:
            text = titles.generate_summary(provider, inputs["messages"], threading.Event(),
                                           files=inputs["files"], todos=inputs["todos"])
        except (ValueError, OSError, ProviderError) as exc:
            return "", str(exc)[:300]
        except Exception as exc:
            return "", f"Summary failed ({type(exc).__name__})."
        if not text:
            return "", "The model did not return a usable summary."
        try:
            if not store.note_summary(session_id, text):
                return "", "That session has no metadata file to hold a summary."
        except (ValueError, OSError, sqlite3.Error) as exc:
            return "", str(exc)[:300]
        return text, ""

    def _conversation_summarize(self, request):
        """`conversation_summarize {session_id, session_dir?}`: summarise one saved session now.

        The session this pane holds is summarised in place (its own `session_summary` follows);
        any other is read from disk and its summary written to its meta file alone.
        """
        session_id = check_id(request.get("session_id"))
        directory = self._summary_directory(request)
        request_id = request.get("id")
        agent = self._agent()
        provider = self._chores_provider(agent)
        if provider is None:
            raise ValueError("No model is configured for the chores role; a summary needs one.")
        failed = lambda text: {"event": "conversation_summary", "id": request_id,   # noqa: E731
                               "session_id": session_id, "error": text}
        live = (agent.store is not None and agent.session_id == session_id
                and os.path.normpath(directory) == os.path.normpath(str(agent.store.directory)))
        if live:
            claim = agent.claim_summary(force=True)
            if claim is None:
                raise ValueError("A summary of this session is already being written.")

            def work():
                text = ""
                try:
                    text = titles.generate_summary(provider, claim["messages"], threading.Event(),
                                                   files=claim["files"], todos=claim["todos"])
                except Exception:
                    text = ""
                event = agent.release_summary(text, claim)
                if event is None:
                    return failed("The model did not return a usable summary.")
                self.emit(event)
                return {"event": "conversation_summary", "id": request_id, "session_id": session_id,
                        "summary": event["summary"], "turn": event["turn"], "live": True}

            def give_up(text):
                agent.release_summary("", claim)
                return failed(text)
            self._background("conversation_summary", request_id, work, give_up)
            return
        store = self._summary_store(directory)

        def work():
            summary, error = self._summarize_saved(store, session_id, provider)
            if error:
                return failed(error)
            return {"event": "conversation_summary", "id": request_id, "session_id": session_id,
                    "summary": summary, "session_dir": directory}
        self._background("conversation_summary", request_id, work, failed)

    def _summary_scope(self, request) -> tuple[str, str]:
        scope = request.get("scope", "project") or "project"
        if scope not in ("project", "all"):
            raise ValueError('scope must be "project" or "all".')
        return scope, self._workspace(request)

    def _summary_rows(self, scope: str, workspace: str) -> list[dict]:
        """Agent sessions in scope, from the index; with the index off, this pane's own directory."""
        rows = []
        try:
            result = self.index().search("", scope=scope, workspace=workspace, sources=["agent"],
                                         limit=conv_index.MAX_LIMIT)
            rows = [{"session_id": item["session_id"], "session_dir": item.get("session_dir") or "",
                     "turns": item.get("turns") or 0} for item in result.get("items") or []]
        except (ValueError, sqlite3.Error, OSError):
            rows = []
        if rows:
            return [row for row in rows if row["session_dir"]]
        agent = self.turns.agent
        if agent is None or agent.store is None:
            return []
        return [{"session_id": item["id"], "session_dir": str(agent.store.directory),
                 "turns": item.get("turns") or 0} for item in agent.store.listing()]

    def _summary_candidates(self, scope: str, workspace: str) -> tuple[list[dict], int]:
        """(sessions in scope with no summary yet, sessions in scope). A session with no turn has
        no assistant reply either, so it is not counted."""
        rows = self._summary_rows(scope, workspace)
        pending = []
        for row in rows:
            try:
                meta = session_files.read_meta(row["session_dir"], row["session_id"])
                if (row["turns"] or 0) < 1 or meta.get("summary"):
                    continue
                size = SessionStore(row["session_dir"], index=False).path(row["session_id"]).stat().st_size
            except (OSError, ValueError):
                continue
            pending.append({**row, "chars": min(titles.SUMMARY_DIGEST_CHARS, size)})
        return pending, len(rows)

    def _conversations_summarize_estimate(self, request):
        """What summarising everything in scope would cost. Nothing is sent to any model here."""
        scope, workspace = self._summary_scope(request)
        pending, total = self._summary_candidates(scope, workspace)
        chars = sum(item["chars"] for item in pending) + len(titles.SUMMARY_SYSTEM) * len(pending)
        self.emit({"event": "conversations_summarize_estimate", "id": request.get("id"),
                   "scope": scope, "workspace": workspace, "count": len(pending), "sessions": total,
                   "approx_input_tokens": titles.approx_tokens(chars),
                   "approx_output_tokens": len(pending) * titles.SUMMARY_OUTPUT_TOKENS,
                   "model": self._chores_model(self.turns.agent)})

    def _conversations_summarize_all(self, request):
        """Summarise every session in scope that has none, one after another in the background.

        Only ever on an explicit request: Relay never backfills summaries by itself.
        """
        scope, workspace = self._summary_scope(request)
        limit = request.get("limit")
        if limit is not None and type(limit) is not int:
            raise ValueError("limit must be an integer.")
        provider = self._chores_provider(self._agent())
        if provider is None:
            raise ValueError("No model is configured for the chores role; a summary needs one.")
        state = self._summary_state()
        with state["lock"]:
            if state["running"]:
                raise ValueError("A batch of summaries is already running.")
            state["running"] = True
            state["cancel"] = threading.Event()
            cancel = state["cancel"]
        try:
            pending, _total = self._summary_candidates(scope, workspace)
        except Exception:
            with state["lock"]:
                state["running"] = False
            raise
        if limit is not None and limit > 0:
            pending = pending[:limit]
        request_id, total = request.get("id"), len(pending)

        def work():
            done = failed = 0
            try:
                for item in pending:
                    if cancel.is_set():
                        break
                    summary, error = self._summarize_saved(self._summary_store(item["session_dir"]),
                                                           item["session_id"], provider)
                    done += 1
                    failed += 1 if error else 0
                    progress = {"event": "conversations_summarize_progress", "id": request_id,
                                "done": done, "total": total, "session_id": item["session_id"]}
                    progress["error" if error else "summary"] = error or summary
                    self.emit(progress)
            finally:
                with state["lock"]:
                    state["running"] = False
            return {"event": "conversations_summarize_progress", "id": request_id, "done": done,
                    "total": total, "finished": True, "failed": failed, "cancelled": cancel.is_set()}
        self._background("conversations_summarize", request_id, work)

    def _conversations_summarize_cancel(self, request):
        """Stop a running batch after the session it is on."""
        state = self._summary_state()
        with state["lock"]:
            running = state["running"]
            if running:
                state["cancel"].set()
        self.emit({"event": "conversations_summarize_cancelled", "id": request.get("id"), "running": running})

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
            # Sessions saved before the index existed, with it off, or by another build: pick them
            # up (and drop rows whose files are gone) once per worker, before the first answer.
            try:
                self._index.reconcile()
            except (OSError, ValueError, sqlite3.Error):
                pass
        return self._index

    def _workspace(self, request) -> str:
        """The workspace a conversation query is scoped to: the request's, else this pane's."""
        workspace = request.get("workspace")
        if isinstance(workspace, str) and workspace:
            return os.path.expanduser(workspace)
        agent = self.turns.agent
        return str(agent.executor.workspace.root) if agent is not None else ""

    def _conversation_id(self, value) -> str:
        """The session id of a conversation request, in the three spellings the index holds.

        Relay's own sessions are 32 hex digits (`sessions.check_id`) and terminal history is
        `term-<digest>`. A guest row (protocol 26.7) is keyed by the guest's own id — claude and
        codex name their transcripts after a dashed UUID — which no shape of Relay's would ever
        accept, so it is checked against the index instead of against a pattern: an id the index
        holds under a guest source is that guest's session and nothing else's.
        """
        if isinstance(value, str) and conv_index.TERMINAL_ID.match(value):
            return value
        if (isinstance(value, str) and 0 < len(value) <= MAX_GUEST_ID and conv_index.enabled()
                and self._indexed(value).get("source") in conv_index.GUEST_SOURCES):
            return value
        return check_id(value)

    def _guest_state(self) -> dict:
        """The one guest reconcile this worker may have running, built on first use."""
        state = getattr(self, "_guests", None)
        if state is None:
            state = {"lock": threading.Lock(), "running": False, "at": 0.0, "request": None}
            self._guests = state
        return state

    def _guest_refresh(self, request, sources) -> None:
        """Bring the guest rows in line with `~/.claude` and `~/.codex` — off the request path.

        The listing is answered from the index straight away; the work runs on its own thread, and
        only if the answer actually changed does a second `conversations` event replace the list
        the pane drew. Two things happen on that thread, and they run at different rates:

        * **the pane's own guest** is tailed first (`_tail_poll`, protocol 26.7): a stat, and the
          bytes the guest appended since the last tick. That is the row the user is watching, so it
          is followed as often as the pane lists, down to `GUEST_TAIL_POLL_EVERY`;
        * **every other guest session** is reconciled, at most once every `GUEST_RECONCILE_EVERY`
          seconds. A reconcile is incremental (a transcript whose mtime matches the indexed one is
          not read at all: warm, it is no work), but the *first* one over a long claude history is
          seconds of parsing, which is exactly why it may not sit in front of the answer, and why
          typing in the search box may not queue a rescan per keystroke.

        The second answer is built for the **latest** request, not for the one that happened to
        set the work going: the user has gone on typing in the meantime, and re-sending an older
        query's results under the same id would put the wrong list in front of them.
        """
        if not any(source in conv_index.GUEST_SOURCES for source in sources):
            return
        state = self._guest_state()
        now = time.monotonic()
        with state["lock"]:
            state["request"] = request         # even when this one is throttled away
            if state["running"]:
                return
            # The reconcile keeps its own five-second throttle; the tail is the reason a listing
            # in between still starts the thread.
            rescan = not state["at"] or now - state["at"] >= GUEST_RECONCILE_EVERY
            if not rescan and not self._tail_due():
                return
            state["running"] = True
        self._guest_work(request, rescan)

    def _guest_tick(self) -> None:
        """Something a running guest may have written about just happened — the pane sent a
        `program_state`, or a turn event went by. Poll the tail, and if it moved, send the pane
        the listing it last asked for, again.

        This is what keeps the row moving while nobody is typing. The Sessions pane re-lists when
        the user touches it and *not* on a timer, so leaving the poll to `conversations` alone
        would leave a guest answering in front of an open, idle pane moving only at the next
        reconcile — the thing this is here to fix. It adds no timer and no thread of its own: it
        borrows the background slot the reconcile uses, and costs a lock and a clock read when
        there is no guest to follow or no listing to answer.
        """
        if self._tail_want is None:
            return                              # the common case: no guest, no work, no locks
        state = getattr(self, "_guests", None)
        if state is None:
            return
        with state["lock"]:
            request = state["request"]
            if request is None or state["running"] or not self._tail_due():
                return
            state["running"] = True
        self._guest_work(request, rescan=False)

    def _guest_work(self, request, rescan: bool) -> None:
        """The background half, with `state["running"]` already claimed by the caller."""
        state = self._guest_state()

        def work():
            changed = False
            try:
                # The pane's own guest first: it is cheap, and it is the row that moves while the
                # user is looking at it.
                changed = self._tail_poll()
                if rescan:
                    changed = self._reconcile_guests() or changed
            finally:
                with state["lock"]:
                    state["running"] = False
                    if rescan:
                        state["at"] = time.monotonic()
            if not changed:
                return None
            with state["lock"]:
                latest = state["request"]
            try:
                return self._conversations_event(latest)
            except (OSError, ValueError, sqlite3.Error):
                return None

        self._background("guest_sessions", request.get("id"), work)

    def _reconcile_guests(self) -> bool:
        """One scan of the guests' homes. True when the rows changed."""
        try:
            # Off: nothing under ~/.claude or ~/.codex is read and the guest rows leave the
            # index; the pins and the names are kept for when it goes back on (26.7).
            outcome = guest_sessions.reconcile(self.index(), enabled=self.index_guests)
        except (OSError, ValueError, sqlite3.Error) as exc:
            # A guest's files are not Relay's to depend on: an unreadable home is a log line,
            # never an error on a listing the user already has in front of them.
            logs.event(_guest_log, "guest reconcile failed", error=str(exc)[:200])
            return False
        except Exception:
            logs.event(_guest_log, "guest reconcile failed", error="unexpected")
            return False
        return bool(outcome["added"] or outcome["refreshed"] or outcome["removed"])

    # ----- the pane's own guest session, while it runs (protocol 26.7) ------------------------
    #
    # A reconcile is a scan on a timer: while a guest is answering in front of the user its row in
    # the Sessions pane would sit still for as much as five seconds. `guest_sessions.GuestTail`
    # follows the one transcript that matters instead, and this is what tells it which one. There
    # are two ways a guest gets into a pane and each answers that question differently:
    #
    #   Tier A (29.3) the pane's *agent* is the guest's headless harness, which knows its own
    #           guest id and session id — `note_agent` at configure, `_set_model` on a switch.
    #   Tier B  the guest runs as a TUI in the pane's own shell, and the only thing the worker
    #           hears is `program_state`, which carries `guest` and `guest_session` (the id the
    #           pane passed on the launch line). `_program_state` is called from there.
    #
    # The session id is not optional. Two claudes started in one directory write two transcripts
    # in the same project folder, and "the newest file" is whichever of the two typed last, so
    # without it each pane would tail the other's session the moment the other answered (GT7X
    # review, B7). `LiveTail.for_session` takes the id and opens exactly that file.

    @property
    def guest_tail(self) -> guest_sessions.GuestTail | None:
        """The tail that is following this pane's guest, or None when none is."""
        return self._tail if self._tail_on else None

    def note_agent(self, agent) -> None:
        """A `configure` built this pane a new Agent (via `configured_fields`).

        Two wires: the new agent's `ProgramControl` is the one the pane's `program_state` messages
        will land in (Tier B), and the agent itself may be a guest harness (Tier A).
        """
        program = getattr(getattr(agent, "executor", None), "program", None)
        watch = getattr(program, "watch", None)
        if callable(watch):
            watch(self._program_state)
        self.follow_agent_guest(agent)

    def follow_agent_guest(self, agent) -> None:
        """Tier A: follow the session of the harness this pane's agent runs on, if it runs on one.

        A pane that is not on a guest follows nothing — which is also what stops the tail when the
        pane reconfigures onto another model.
        """
        provider = guest_harness_provider.agent_provider(agent)
        if provider is None:
            self._unfollow("agent")
            return
        workspace = ""
        root = getattr(getattr(agent, "executor", None), "workspace", None)
        if root is not None:
            workspace = str(getattr(root, "root", "") or "")
        self._follow("agent", provider.guest_id, workspace, provider.session_id)

    def _program_state(self, program) -> None:
        """Tier B: the pane says what is running in its terminal (`ProgramControl.watch`).

        A pane whose *agent* is a guest harness keeps following that: its terminal is the user's
        own shell and whatever is in it is not this pane's agent. `guest` going empty — the guest
        exited, or something else came to the foreground — stops the tail. A `program_state` that
        names the guest but not a session leaves the tail alone rather than dropping it: an older
        pane never sends the field at all, and the state it does send says nothing about which
        session is being written.
        """
        if self._tail_owner == "agent":
            return
        source = (getattr(program, "guest", "") or "").strip()
        if not source:
            self._unfollow("program")
            return
        session = (getattr(program, "guest_session", "") or "").strip()
        if session:
            self._follow("program", source, self._workspace({}), session)
        # The pane sends one of these whenever the guest's statusline, its busy flag or the screen
        # in front of it changes, which is as often as a guest working says anything at all. That
        # is the tick a Tier B guest has: no turn of Relay's is running, so nothing else moves.
        self._guest_tick()

    def _guests_indexed(self) -> bool:
        """Whether Relay may read the guests' files at all (Options > Privacy, review B1)."""
        return guest_sessions.guests_enabled() if self.index_guests is None else bool(self.index_guests)

    def _follow(self, owner: str, source: str, workspace: str, session_id: str) -> None:
        """Follow this guest session from the next tick. Nothing is read here: `start()` parses a
        resumed transcript from its first byte, and the protocol thread is not the place for it."""
        session_id = (session_id or "").strip()[:MAX_GUEST_ID]
        if source not in conv_index.GUEST_SOURCES or not session_id:
            return
        want = (source, workspace or "", session_id)
        with self._tail_lock:
            if self._tail_owner == owner and self._tail_want == want:
                return
            if self._tail is not None:
                self._tail.stop()          # a different session: the old one is the reconcile's again
            self._tail_want, self._tail_owner = want, owner
            self._tail_on, self._tail_at = False, 0.0

    def _unfollow(self, owner: str) -> None:
        """Stop following, if what is being followed is this owner's. A Tier B guest in the
        terminal is not ended by a `configure`, and a Tier A harness is not ended by a program
        leaving the foreground, so neither may stop the other's tail."""
        with self._tail_lock:
            if self._tail_want is None or self._tail_owner != owner:
                return
            tail = self._tail
            self._tail_want, self._tail_owner = None, ""
            self._tail_on, self._tail_at = False, 0.0
        if tail is not None:
            tail.stop()

    def close(self) -> None:
        """The worker is going away: nothing is being followed any more."""
        with self._tail_lock:
            tail = self._tail
            self._tail_want, self._tail_owner = None, ""
            self._tail_on, self._tail_at = False, 0.0
        if tail is not None:
            tail.stop()

    def _tail_due(self) -> bool:
        """Whether a poll now would do anything: nothing followed is no, and two listings inside
        one interval are one poll (the pane lists on every keystroke)."""
        with self._tail_lock:
            if self._tail_want is None:
                return False
            every = GUEST_TAIL_POLL_EVERY if self._tail_on else GUEST_TAIL_RETRY_EVERY
            return not self._tail_at or time.monotonic() - self._tail_at >= every

    def _tail_poll(self) -> bool:
        """Put what the pane's guest has written since the last tick into the index. True when the
        rows changed, which is what asks the listing to be sent again.

        Indexing off (review B1) reads nothing and stops a tail that was running: the setting can
        be turned off while a guest is answering, and the whole promise of it is that Relay then
        does not open the guests' files. A guest that has not written its first line yet has no
        transcript to open and `start()` says so; the want is kept and tried again on the next
        tick rather than thrown away, no oftener than `GUEST_TAIL_RETRY_EVERY`.
        """
        if not self._guests_indexed():
            with self._tail_lock:
                tail, running = self._tail, self._tail_on
                self._tail_on, self._tail_at = False, 0.0
            if running and tail is not None:
                tail.stop()
            return False
        with self._tail_lock:
            want, started = self._tail_want, self._tail_on
            if want is None:
                return False
            every = GUEST_TAIL_POLL_EVERY if started else GUEST_TAIL_RETRY_EVERY
            now = time.monotonic()
            if self._tail_at and now - self._tail_at < every:
                return False
            self._tail_at = now
            if self._tail is None:
                # `min_poll=0`: the cadence is this method's, so there is one throttle and not two.
                self._tail = guest_sessions.GuestTail(min_poll=0.0)
            tail = self._tail
        try:
            if started:
                return tail.poll()
            source, workspace, session_id = want
            if not tail.start(self.index(), source, workspace or None, session_id=session_id):
                return False
            with self._tail_lock:
                if self._tail_want == want:
                    self._tail_on = True
            return True
        except (OSError, ValueError, sqlite3.Error) as exc:
            logs.event(_guest_log, "guest tail failed", error=str(exc)[:200])
            return False
        except Exception:
            logs.event(_guest_log, "guest tail failed", error="unexpected")
            return False

    def _conversations(self, request):
        for name in ("model", "file", "branch"):
            if request.get(name) is not None and not isinstance(request.get(name), str):
                raise ValueError(f"{name} must be text.")
        # The guests are sources like any other here (protocol 26.7): naming one is what asks for
        # its rows, and naming something that is not a source is an error rather than a silence.
        sources = request.get("sources")
        if sources is not None and (not isinstance(sources, list)
                                    or not all(isinstance(s, str) and s in conv_index.SOURCES for s in sources)):
            raise ValueError("sources must be a list of " + LISTABLE_SOURCES + ".")
        for name in ("offset", "matches_per_item", "limit"):
            value = request.get(name)
            if value is not None and type(value) is not int:
                raise ValueError(f"{name} must be an integer.")
        if type(request.get("include_threads", False)) is not bool:
            raise ValueError("include_threads must be true or false.")
        # Options › Privacy: whether the guests' sessions are indexed at all (26.7, review B1). It
        # rides every listing, so a change takes effect at the next one with no second message.
        if request.get("index_guests") is not None:
            if type(request["index_guests"]) is not bool:
                raise ValueError("index_guests must be true or false.")
            self.index_guests = request["index_guests"]
        # The three-state filters (protocol 14.3): absent means "do not filter", not "false".
        for name in ("has_edits", "unfinished", "pinned", "has_summary"):
            value = request.get(name)
            if value is not None and type(value) is not bool:
                raise ValueError(f"{name} must be true or false.")
        self.emit(self._conversations_event(request))
        # The guests' own transcripts are a cache like everything else here, refreshed behind the
        # answer the pane already has (protocol 26.7).
        self._guest_refresh(request, sources or [])

    def _conversations_event(self, request) -> dict:
        """The `conversations` answer for one request — built twice for the same request when a
        guest reconcile changed the rows behind it, so it may not depend on anything but `request`."""
        include_threads = bool(request.get("include_threads", False))
        flags = {name: request.get(name) for name in ("has_edits", "unfinished", "pinned", "has_summary")}
        result = self.index().search(
            request.get("query", "") or "", scope=request.get("scope", "project") or "project",
            workspace=self._workspace(request), model=request.get("model") or None,
            has_open=bool(request.get("has_open_tasks")), since=request.get("since"),
            until=request.get("until"), sources=request.get("sources"), limit=request.get("limit", 50),
            include_threads=include_threads, sort=request.get("sort") or "recent",
            offset=request.get("offset") or 0,
            matches_per_item=request.get("matches_per_item") or conv_index.MAX_MATCHES_PER_ITEM,
            file=request.get("file") or None, branch=request.get("branch") or None, **flags)
        # A guest row carries what it takes to resume it: the tool's own argv and the directory it
        # must be run in (protocol 26.7). `fork_command` is the same argv with the guest's fork
        # flag, so Ctrl+Enter on a guest row is one message rather than a rule spelled twice.
        result["items"] = guest_sessions.annotate_items(result.get("items") or [])
        for item in result["items"]:
            if isinstance(item, dict) and item.get("source") in conv_index.GUEST_SOURCES:
                item["fork_command"] = guest_sessions.resume_command(item["source"], item["id"], fork=True)
        return {"event": "conversations", "id": request.get("id"),
                "scope": request.get("scope", "project") or "project",
                "workspace": self._workspace(request), **result}

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
        elif self._indexed(session_id).get("source") in conv_index.GUEST_SOURCES:
            # A guest session is the guest's file (protocol 26.7). Deleting the row drops Relay's
            # cached copy of its text and nothing else: `~/.claude` and `~/.codex` are never
            # written or unlinked, and the next reconcile lists the session again if it is still
            # on disk — which is what the pane's confirmation says it will do.
            index.delete_session(session_id, remove_files=False)
        elif self._indexed(session_id).get("source") == "subagent":
            row = self._indexed(session_id)
            try:
                store = SessionStore(row.get("session_dir") or "/nonexistent", index=index)
                path = store.thread_path(row.get("owner_session"), session_id)
                path.unlink()
                removed["files"] = 1
            except (OSError, ValueError):
                pass
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
                self.emit({"event": "reset", "session_id": agent.session_id})
        self.emit({"event": "conversation_deleted", "id": request.get("id"),
                   "session_id": session_id, **{k: v for k, v in removed.items() if k != "session_id"}})

    def _set_user_fields(self, session_id: str, **fields) -> None:
        """A rename or pin goes to the session's own files (meta, or the thread file), which the
        index mirrors; terminal history and the guest sessions have no file of Relay's, so only
        their index rows hold them."""
        index = self.index()
        if session_id.startswith("term-"):
            if "custom_title" in fields:
                index.rename(session_id, fields["custom_title"])
            if "pinned" in fields:
                index.set_pinned(session_id, fields["pinned"])
            return
        row = self._indexed(session_id)
        if row.get("source") in conv_index.GUEST_SOURCES:
            # Index-only, for the same reason (protocol 26.7): there is no `.meta.json` beside a
            # guest transcript to put a name or a pin in, and Relay may not make one. `update_guest`
            # merges these two keys per key, so a re-index keeps whichever the user set.
            if "custom_title" in fields:
                index.rename(session_id, fields["custom_title"])
            if "pinned" in fields:
                index.set_pinned(session_id, fields["pinned"])
            return
        directory = row.get("session_dir") or ""
        if not directory and self.turns.agent is not None and self.turns.agent.store is not None:
            directory = str(self.turns.agent.store.directory)
        if not directory:
            raise ValueError("That conversation is not in the index.")
        store = SessionStore(directory, index=index)
        if row.get("source") == "subagent":
            store.set_thread_fields(session_id, **fields)
        else:
            store.set_user_fields(session_id, **fields)

    def _indexed(self, session_id: str) -> dict:
        try:
            return self.index().conversation(session_id, limit=1)
        except ValueError:
            return {}

    def _conversation_rename(self, request):
        session_id = self._conversation_id(request.get("session_id"))
        title = request.get("title")
        if title is not None and not isinstance(title, str):
            raise ValueError("title must be text.")
        self._set_user_fields(session_id, custom_title=title or "")
        self.emit({"event": "conversation_renamed", "id": request.get("id"), "session_id": session_id,
                   "title": " ".join((title or "").split())[:200]})

    def _conversation_pin(self, request):
        session_id = self._conversation_id(request.get("session_id"))
        pinned = request.get("pinned", True)
        if type(pinned) is not bool:
            raise ValueError("pinned must be a boolean.")
        self._set_user_fields(session_id, pinned=pinned)
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

    # ----- session info and thread history (protocol section 25) -------------------------------
    def _session_info(self, request):
        """What the ⓘ view shows. No ids: this pane's session, live (model, context, usage).
        `session_id` (+ `session_dir`): a saved session. `thread_id` (+ `session_dir`, and
        `owner_session` when known): one subagent thread and its own history."""
        thread_id = request.get("thread_id")
        session_id = request.get("session_id")
        directory = _abs_dir(request.get("session_dir"), "session_dir")
        agent = self.turns.agent
        if directory is None:
            if agent is None or agent.store is None:
                raise ValueError("This pane has no saved sessions.")
            directory = str(agent.store.directory)
        store = SessionStore(directory, index=False)
        if thread_id is not None:
            event = self._thread_info(store, check_id(thread_id), request.get("owner_session"))
        elif session_id is None or (agent is not None and session_id == agent.session_id
                                    and agent.store is not None and store.directory == agent.store.directory):
            if agent is None:
                raise ValueError("Configure a provider and workspace first.")
            event = self._live_session_info(agent)
        else:
            data = store.load(check_id(session_id))
            event = self._saved_session_info(store, data)
        event["id"] = request.get("id")
        self.emit(event)

    def _session_fields(self, store: SessionStore, data: dict) -> dict:
        session_id = data["id"]
        threads = store.threads(session_id)
        turns, unplaced = session_files.session_history(data, threads)
        user = conv_index.read_user_fields(store.directory, session_id)
        usage = session_files.load_usage(data.get("usage"))
        return {"event": "session_info", "kind": "session", "session_id": session_id, "session_dir": str(store.directory),
                "file": str(store.path(session_id)), "file_exists": store.path(session_id).is_file(),
                "title": user.get("custom_title") or data.get("title") or "",
                "workspace": data.get("workspace") or "", "created": data.get("created"),
                "updated": data.get("updated"), "turns": data.get("turns") or len(turns),
                "model": data.get("model") or "", "models": data.get("models") or ([data["model"]] if data.get("model") else []),
                "preset": data.get("preset") or "", "effort": data.get("effort"), "mode": data.get("mode"),
                "usage": usage, "instructions": data.get("instructions") or [],
                "forked_from": data.get("forked_from"), "history": turns, "unplaced_threads": unplaced,
                "thread_count": len(threads), "open_requests": data.get("open_requests") or 0}

    def _live_session_info(self, agent) -> dict:
        data = agent.session_data()
        # Threads this worker is still running are saved at start and at each run's end; the live
        # rows here keep their status current without writing the files again.
        event = self._session_fields(agent.store, data)
        # The turn's `done` goes out before the agent's autosave writes the file (Agent.ask's
        # `finally`), and the ⓘ pane refreshes on `done`: a session with turns is saved or about to
        # be, so it is not "not saved yet" in that gap.
        event["file_exists"] = event["file_exists"] or agent.turns > 0
        live = {row.get("thread_id"): row for row in (self.subagents.list() if self.subagents else [])}
        for turn in event["history"]:
            for thread in turn["threads"]:
                if thread.get("id") in live:
                    thread["status"] = live[thread["id"]].get("status")
                    thread["live"] = True
        preset = agent.preset
        context = agent.context_event()
        event.update({"live": True, "provider": provider_name(preset.id if preset else "", agent.config.base_url),
                      "base_url_host": urlsplit(agent.config.base_url).hostname or "",
                      "context": {k: context.get(k) for k in ("used_tokens", "window", "limit_tokens", "percent",
                                                               "estimated") if k in context},
                      "instructions_bytes": len(agent.instructions.section.encode("utf-8")) if agent.instructions else 0,
                      "git_branch": session_files.git_branch(event["workspace"]) if event["workspace"] else ""})
        return event

    def _is_live_session(self, store: SessionStore, session_id: str) -> bool:
        """The session this pane's agent holds, in this directory: it exists even in the moment
        between a turn's `done` and the autosave that writes its file."""
        agent = self.turns.agent
        return (agent is not None and agent.session_id == session_id and agent.store is not None
                and agent.store.directory == store.directory)

    def _saved_session_info(self, store: SessionStore, data: dict) -> dict:
        event = self._session_fields(store, data)
        preset = data.get("preset") or ""
        event.update({"live": False, "provider": provider_name(preset) if preset else "",
                      "context": None,
                      "git_branch": session_files.git_branch(event["workspace"]) if event["workspace"] else ""})
        return event

    def _thread_info(self, store: SessionStore, thread_id: str, owner) -> dict:
        live = self.subagents.live_thread(thread_id) if self.subagents is not None else None
        if live is not None:
            data, path = live, None
            try:
                path = str(store.thread_path(live["owner_session"], thread_id))
            except ValueError:
                path = None
        else:
            data = store.load_thread(thread_id, owner if isinstance(owner, str) and owner else None)
            path = data.pop("_path", None)
        owner_id = data.get("owner_session")
        siblings = store.threads(owner_id) if owner_id else []
        children = [t for t in siblings if t.get("parent_thread") == thread_id]
        owner_title, parent_title = "", ""
        if owner_id:
            try:
                owner_data = store.load(owner_id)
                owner_title = (conv_index.read_user_fields(store.directory, owner_id).get("custom_title")
                               or owner_data.get("title") or "")
            except ValueError:
                owner_title = ""
        if data.get("parent_thread"):
            parent = next((t for t in siblings if t.get("id") == data["parent_thread"]), None)
            parent_title = parent.get("title") if parent else ""
        summary = session_files.thread_summary(data)
        return {"event": "session_info", "kind": "thread", "thread_id": thread_id, **{k: v for k, v in summary.items() if k != "id"},
                "session_dir": str(store.directory), "file": path, "owner_title": owner_title,
                "owner_exists": bool(owner_id) and (store.path(owner_id).is_file() or self._is_live_session(store, owner_id)),
                "parent_title": parent_title, "task": str(data.get("task") or "")[:4000],
                "effort": data.get("effort"), "tools": data.get("tools"),
                # Still known to this worker: the pane's subagents pane can show it.
                "live": live is not None,
                "history": session_files.thread_history(data, children)}

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
            self.emit({"event": "reset", "session_id": agent.session_id})
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
