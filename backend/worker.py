#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Private NDJSON stdio worker. No TCP listener, telemetry, or persistent secrets."""
from __future__ import annotations

import json
import os
import sys
import threading
import urllib.parse
from pathlib import Path

from relay_core import globals_protocol
from relay_core import (__version__, board_protocol, customproviders, hosted, relay_pro, keystore, keytest, localmodels, logs,
                        observe_protocol, roles as model_roles, session_protocol, skills, voice)
from relay_core.agent import Agent, validate_turn_options
from relay_core import activity_tools, agent_context, agents_defs, app_tools, guest_harness_provider
from relay_core.open_buffers import OpenBuffers
from relay_core import guest_accounts
from relay_core import board_chat
from relay_core import memory_import, openrouter_catalog, provider_limits, guest_usage_poll
from relay_core import final_summary
from relay_core import workspace_plugins
from relay_core import request_stream, tool_stream
from relay_core.subagents import SubagentFactory, SubagentManager
from relay_core.keybindings import KeybindingCatalog
from relay_core.presets import PRESETS, model_name, tier_list_defaults
from relay_core.queue import TurnSupervisor
from relay_core.router import classify

MAX_MESSAGE = 2 * 1024 * 1024


OOM_SCORE_ADJ = 500


def prefer_as_oom_victim(value: int = OOM_SCORE_ADJ) -> bool:
    """Ask the kernel to kill this worker before the Relay window when memory runs out.

    Raising oom_score_adj is unprivileged; lowering is not, so never lower an existing value.
    """
    path = "/proc/self/oom_score_adj"
    try:
        with open(path, "r", encoding="ascii") as handle:
            current = int(handle.read().strip() or 0)
        if current >= value:
            return True
        with open(path, "w", encoding="ascii") as handle:
            handle.write(str(value))
        return True
    except (OSError, ValueError):
        return False


def main():
    # This is a UTF-8 byte protocol even when Windows launches the private Python
    # runtime with redirected pipes. Its isolated ._pth ignores PYTHONIOENCODING.
    for stream, errors in ((sys.stdout, "strict"), (sys.stderr, "backslashreplace")):
        if hasattr(stream, "reconfigure"):  # In-process consumers may use StringIO.
            stream.reconfigure(encoding="utf-8", errors=errors)
    prefer_as_oom_victim()
    # Rotating diagnostics under $XDG_DATA_HOME/relay/logs (docs/ARCHITECTURE.md, "Logs"). Pane id
    # and level come from the GUI through the environment. Never logs prompts or tool output.
    logs.configure("worker")
    log = logs.get("worker")
    logs.event(log, "worker_start", version=__version__, pid=os.getpid(), level=logs.level())
    output_lock = threading.Lock()
    # Protocol 23.10 (card #PPR4): whether a tool's output travels as text or as its counts. The
    # GUI asks for the counts only while nothing on its side is reading the text, and it is the
    # *wire* that is trimmed and nothing else — every observer below (the board, the subagent
    # manager, the session titler) and every stored result the fold fetches sees the full event,
    # because this is the last thing that happens before the bytes leave the process.
    stream_tool_output = [True]
    # Protocol 12.11 (card #PPR4): whether the `requests` event carries the whole ledger or only
    # the entries that changed. Same shape and same place as the switch above — it trims the wire
    # and nothing else, so every observer in this process still sees the full list. The state it
    # keeps is per connection, which is what this closure is.
    requests_delta = [False]
    requests_stream = request_stream.RequestStream()

    def emit(obj: dict):
        if obj.get("event") == "usage_limits":
            logs.usage_state(obj)
        if not stream_tool_output[0]:
            obj = tool_stream.counted(obj)
        if requests_delta[0]:
            obj = requests_stream.trim(obj)
        with output_lock:
            sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n")
            sys.stdout.flush()

    # Subagents observe main-turn endings to wake the main agent for background results.
    subagents = SubagentManager(emit)
    # Which model role this pane's own agent runs (protocol 13): "main", or "flash" for panes that
    # default to the Flash agent.
    # `context` is what this worker's agent is *about* (protocol 33): None for a terminal pane,
    # a `ContextSpec` for an agent console. Kept here rather than as a local of `configure`,
    # because `ask` reads it and a message may arrive before any `configure` has.
    state = {"agent_role": "main", "context": None}

    # Board (protocol 17). `board` also tags board_ask turn events with their card_id and
    # appends the agent's answer to the card thread, so it is created before the supervisor.
    board = board_protocol.BoardCommands(None, emit)
    globals_commands = globals_protocol.GlobalsCommands(emit, workspace=lambda: board.workspace)

    # The agent drives the app (protocol 30, card #FEJQ): the GUI's Options and actions catalog,
    # the `app_command` round trip and this worker's change log. Created before the supervisor
    # for the same reason the board is — it outlives every `configure`, so what the agent has
    # already changed is still listed after the pane's model or workspace changes.
    app = app_tools.AppCommands(emit, sessions=lambda: sessions.index(),
                                agent=lambda: turns.agent)
    # The files open in the editor of this pane's window, and the round trip that edits them there
    # (protocol 35, card #F8R7). Outlives every `configure`, like the app catalog.
    buffers = OpenBuffers(emit)

    def turn_emit(obj: dict):
        obj = board.observe(obj)
        emit(obj)
        subagents.observe(obj)
        sessions.observe(obj)   # pane title (protocol 18): a finished turn may be owed a fresh one

    turns = TurnSupervisor(turn_emit)
    subagents.turns = turns

    # Protocol 36 (#C0Q8): this pane's task-plugin workspace — its router, its kernel or TeX
    # builder, and the tool group its agent may load. Outlives every `configure`: a kernel's state
    # is the workspace's, not the conversation's.
    def workspace_changed(workspace_id):
        if workspace_id == workspace_plugins.DEFAULT_WORKSPACE and turns.agent is not None:
            turns.agent.plugin_workspace_changed()

    workspaces = workspace_plugins.WorkspaceManager(emit, on_change=workspace_changed)
    board.turns = turns
    # Signal threads (#AQ6X step 7b): a failing check nobody is on is picked up as a subagent of
    # this worker, so the board's half needs the manager. One line rather than a constructor
    # argument because `board` is built before `subagents.configure` has anything to configure.
    board.subagents = subagents

    def queue_for(request: dict):
        """Which queue a message is addressed to: a card's own, or this worker's (33, #CTRN).

        `ask {surface}` has named the asking console since #AGNT. Since a card turn became an
        ordinary supervised turn it has a queue of its own, so `queue_remove`, `queue_move`,
        `queue_steer`, `queue_unsteer`, `queue_clear`, `resume_queue` and `cancel` carry the
        same `surface` and operate that card's queue — including the resume a card's own queue
        needs after a turn on it failed. Everything else — a terminal pane, a tab console, a GUI
        that sends no surface at all — is this worker's own supervisor, exactly as before.
        """
        own = board.card_queue(request.get("surface"))
        return own if own is not None else turns

    def model_changed(agent):
        # Subagents that inherit the main model follow a set_model switch.
        factory = subagents.factory
        if factory is not None:
            factory.config = agent.config
            factory.preset_id = agent.preset.id if agent.preset else None
        # Model roles (protocol 13): per-provider defaults follow the pane's new main model.
        if agent.roles is not None:
            agent.roles.rebase(agent.config, agent.preset.id if agent.preset else None, agent.effort)
            state["agent_role"] = "main"
            emit(agent.roles.event("main"))

    sessions = session_protocol.SessionCommands(
        turns, emit, on_model_changed=model_changed,
        on_conversation_replaced=lambda: subagents.stop_all(reset=True), subagents=subagents)

    observe = observe_protocol.ObserveCommands(turns, emit)  # protocol 11

    def emit_presets(request_id=None):
        # key_source says where each key comes from so the keys modal can show "from
        # RELAY_*_API_KEY" instead of offering to remove something it cannot remove.
        sources = keystore.sources()
        # Relay Free (protocol 13.9): nothing is stored and nothing can be, so key_source
        # says "included"; `available` (cryptography imports) is what makes the row usable,
        # and `quota` is the last allowance seen, null before the first exchange.
        relay_pro.start_refresh()
        pro_status = relay_pro.status()
        relay_free = {"has_stored_key": False, "key_source": "included", **hosted.status()}
        # OpenRouter's live model list is the `openrouter` row's catalog (owner, 2026-09-20):
        # served from the day-old cache now, fetched on its own thread once per process when
        # that is stale, and re-pushed below when the fetch lands. Never on this thread.
        openrouter_catalog.start_refresh()
        provider_limits.start(key_lookup=keystore.lookup, emit=emit)
        guest_usage_poll.start(emit=emit, changed=lambda: emit_presets())
        # The two default fillings of Options › Models' five lists (owner, 2026-09-20; 13.7),
        # computed from what can take a turn right now so the GUI's two buttons only apply them.
        guest_rows = guest_harness_provider.preset_rows()
        custom_rows = customproviders.rows()
        list_defaults = tier_list_defaults(
            [p for p in PRESETS if (pro_status["available"] if p == "relay-pro" else
             relay_free["available"] if p == "relay-free" else bool(sources.get(p)))],
            local=[(e.id, e.model) for e in localmodels.catalog().values()],
            custom=[(row["id"], row.get("model") or "") for row in custom_rows if row.get("has_stored_key")],
            guests=guest_rows)
        emit({"event": "presets", "id": request_id, "warp_default": keystore.warp_default_preset(),
              "tier_defaults": model_roles.tier_catalog(), "role_actions": model_roles.action_catalog(),
              "tier_list_defaults": list_defaults,
              "presets": [{**p.to_dict(), **(pro_status if p.id == "relay-pro" else relay_free if p.id == "relay-free" else
                                             {"has_stored_key": bool(sources[p.id]),
                                              "key_source": sources[p.id],
                                              "limits": provider_limits.last(p.id)})}
                          for p in PRESETS.values()]
              # Model servers on this machine (protocol 28): no key to store, so
              # has_stored_key stays false and `local` is what makes the row usable.
              + [{**e.to_dict(), "has_stored_key": False, "key_source": "local"}
                 for e in localmodels.catalog().values()]
              # Custom providers (protocol 28.6): a key under the entry's own id, so
              # has_stored_key and key_source are read like a built-in row's.
              + custom_rows
              # Guest agents on this machine (protocol 29.3): no key either, and `harness`
              # is what makes the row this pane's agent rather than a Tier B launch.
              + guest_rows,
              "media_keys": [{"id": service, "label": label, "group": "media",
                              "key_url": url, "key_source": keystore.key_source(service),
                              "note": "For music and sound effects generation."}
                             for service, label, url in (
                                 ("fal", "fal.ai", "https://fal.ai/dashboard/keys"),
                                 ("elevenlabs", "ElevenLabs", "https://elevenlabs.io/app/settings/api-keys"))]})

    # The codex catalogue lands after the first `presets` answer (the scan must not delay it,
    # 29.3), and nothing re-asks — so the worker pushes a fresh `presets` when it does, and
    # Options' Codex row turns from the text field into the dropdown on its own.
    guest_harness_provider.set_catalog_listener(lambda: emit_presets())
    # The same for OpenRouter's listing: the first `presets` answer carries whatever the cache
    # held, and the fetch that lands after it pushes a fresh one, so the id box completes against
    # the live list without a re-ask.
    openrouter_catalog.set_listener(lambda: emit_presets())
    provider_limits.set_listener(lambda: emit_presets())
    # And for a custom provider's /models listing (28.6): the save answers at once, the probe
    # lands later and pushes the row with the served models added.
    customproviders.set_listener(lambda: emit_presets())
    relay_pro.set_listener(lambda: emit_presets())

    # Protocol 34 (#MEMS): Claude Code / Codex memories offered as suggestions, once per process,
    # on a thread begun by the first `configure`; RELAY_MEMORY_IMPORT=off skips it.
    memory_startup = memory_import.StartupImport(emit)

    emit({"event": "ready", "version": __version__})
    while True:
        line = sys.stdin.buffer.readline(MAX_MESSAGE + 1)
        if not line:
            break
        if len(line) > MAX_MESSAGE:
            emit({"event": "error", "text": "Protocol message too large."})
            break
        try:
            request = json.loads(line)
            if not isinstance(request, dict):
                raise ValueError("Protocol message must be an object.")
            kind = request.get("type")
            if kind == "set_model" and board.refuse_model_selection(request):
                continue
            if kind == "route":
                known = request.get("known_commands", [])
                if not isinstance(known, list) or len(known) > 20000 or not all(isinstance(x, str) for x in known):
                    raise ValueError("Invalid command-name list.")
                cwd = request.get("cwd")
                if cwd is not None and (not isinstance(cwd, str) or not os.path.isdir(cwd)):
                    cwd = None
                # Protocol 36: a Python/Stata workspace, or a language REPL in the foreground,
                # replaces step 5 (`bash -n`) with the language's check; None leaves it to Bash.
                routed = workspaces.route(request)
                if routed is not None:
                    emit({"event": "route", "id": request.get("id"), **routed})
                    continue
                # remote: the terminal is at a prompt on this ssh host (card #S5SH); the router then
                # ignores the local PATH, aliases and cwd.
                decision = classify(request.get("text", ""), request.get("mode", "auto"), known,
                                    request.get("path", os.environ.get("PATH", os.defpath)), cwd,
                                    remote=request.get("remote"))
                emit({"event": "route", "id": request.get("id"),
                      **workspaces.annotate(decision.to_dict(), request)})
            elif kind == "configure":
                if turns.busy:
                    raise ValueError("Stop the active agent turn before changing provider or workspace.")
                # One resolved absolute workspace for the whole of `configure`. `board_workspace`
                # is None when the GUI named none: the agent may fall back to the process's cwd
                # (that is its sandbox), but the *board* may not — `workspace: ""` used to make
                # `Path("") / "issues"` relative, so a pane with no workspace quietly opened the
                # board of whatever directory Relay was launched from (186 cards of another
                # project), which is why the board looked global rather than per project.
                asked = request.get("workspace")
                board_workspace = (str(Path(asked).expanduser().resolve())
                                   if isinstance(asked, str) and asked.strip() else None)
                workspace = board_workspace or str(Path(os.getcwd()).resolve())
                # The board is files, not a model: set it up before the provider is resolved,
                # so a missing key still lets the pane open and browse the cards (only board_ask
                # needs the agent). Before 2026-09-17 a keyless window sat on "Loading…" forever.
                board_summary = board.configure(board_workspace, request)
                memory_startup.configure(request.get("memory_import"))
                # Protocol 33 (card #AGNT): what this agent is *about*. One `configure` builds a
                # terminal pane's agent or an agent console's, and the difference is this block —
                # the surface's name, the role, the brief, where the conversation is kept, the
                # named tool scope. A GUI that sends none gets a terminal pane, which is every
                # worker before this card.
                context = agent_context.from_request(request)
                state["context"] = context
                # Protocol 30.7: which tab this worker is the helper of. It keys the helper's
                # conversation with the workspace `board.configure` has just settled, so the
                # same tab comes back with its own history after a restart. The context's
                # `persist.key` is the tab id by another name and wins when both are sent —
                # except a card console's, which names one card's conversation and not the tab
                # (`board_chat.tab_of`, card #KSKH): it must not become the board's tab, or
                # every card session built afterwards is keyed under it.
                board.set_tab(board_chat.tab_of(context.persist_key if context is not None else "",
                                                request.get("tab")))
                # A console's board tools offer the console's set — merge, split, the import and
                # `search_files` — for as long as the console exists, rather than for one turn
                # the way a card's stage scope does (`ConsoleScope`, #AGNT).
                board.console = context is not None and context.is_console()
                config = session_protocol.provider_config(request)
                # Tier A (protocol 29.3): a `guest:` preset makes the guest's own headless harness
                # this pane's agent. `is_guest` here, the process started further down — after
                # everything that can refuse this request, so a bad `roles` table or an unreadable
                # skills directory never leaves a guest process behind with no pane to own it.
                is_guest = guest_harness_provider.is_guest_preset(request.get("preset"))
                catalog = KeybindingCatalog.from_request(request.get("keybindings"))
                skill_index = skills.from_request(request.get("skills"), workspace)
                # --- model roles (protocol 13) ---
                role_table = model_roles.validate_roles(request.get("roles"))
                tier_table = model_roles.validate_tiers(request.get("tiers"))
                # The role may be named at the top level (every GUI before #AGNT) or inside the
                # context block; they say the same thing and the top level wins, so a GUI that
                # sends both cannot contradict itself.
                agent_role = model_roles.validate_role(
                    request.get("agent_role")
                    or (context.agent_role if context is not None else "") or "main")
                # Protocol 23.10: stated in full by every `configure`, so a GUI that does not know
                # the option — or one whose pane has just stopped needing the text — gets the
                # default back rather than whatever the last pane asked for.
                stream_tool_output[0] = (tool_stream.validate(request["stream_tool_output"])
                                         if request.get("stream_tool_output") is not None else True)
                # Protocol 12.11, the same rule: stated in full by every `configure`, and the
                # ledger this connection has sent so far is forgotten, because a `configure`
                # replaces the conversation the entries belonged to.
                requests_delta[0] = (request_stream.validate(request["requests_delta"])
                                     if request.get("requests_delta") is not None else False)
                requests_stream.reset()
                options = session_protocol.agent_options(request, workspace)
                options["board"] = board.agent_tools(board_workspace, request)
                if board.console and options["board"] is not None:
                    options["board"].begin_console()
                # Protocol 33: a console's conversation is kept where its context says, not in
                # `relay/sessions/` — a console's chatter is not one of the person's own
                # conversations and is not listed as one (14). The file layout is #FEJQ's,
                # unchanged, so a tab's history from before this card is found by the same name.
                console_dir, console_session = (context.store(board_workspace or "")
                                                if context is not None else (None, None))
                if console_dir:
                    options["session_dir"] = console_dir
                # Protocol 30.2: the `app` block, or None for a GUI that sent none — then this
                # worker has no app tools at all, which is what every worker had before 30.
                options["app"] = app.configure(request, workspace)
                resolver = model_roles.RoleResolver(config, options.get("preset_id"), role_table,
                                                    key_lookup=keystore.lookup, main_effort=options.get("effort"),
                                                    tiers=tier_table)
                # Card #GH5T (owner report, 2026-09-20: "The Switchboard agent could not answer:
                # Base URL must be an HTTPS URL without credentials, query, or fragment"). The
                # helper worker is configured with the *window's* preset, so a Main on Claude Code
                # made it a guest worker — and the helper's whole job is Relay's own `board_*` and
                # `app_*` tools, which a guest does not take (#4NXH). So it never starts one: the
                # resolver is moved off the harness onto the Options › Models priority list before
                # any role is resolved, which is what makes "Follow Main", the tiers and a role
                # pick in the helper's model box all name a model that can actually answer. A
                # *pane* on a guest preset is untouched; this is the helper role and nothing else.
                helper_on_guest = is_guest and agent_role == model_roles.HELPER_ROLE
                spare = (resolver.leave_guest(options.get("fallbacks"),
                                              guest_harness_provider.guest_name(request.get("preset")))
                         if helper_on_guest else None)
                pane_role = resolver.resolve(agent_role)
                # The guest starts here: every field of the request has been accepted, and a guest
                # that cannot start is one `error` with the pane left on the model it had (29.3).
                # `config` is filled in rather than replaced, so the resolver holds the same object
                # and its summary names the model the guest reports.
                guest_provider = (guest_harness_provider.start_provider(
                    request.get("preset"), request, workspace, config=config)
                    if is_guest and not helper_on_guest else None)
                if guest_provider is not None:
                    agent_role = "main"
                elif not pane_role.is_main:
                    config, options["preset_id"] = pane_role.config, pane_role.preset_id
                    if guest_harness_provider.is_guest_preset(pane_role.preset_id):
                        # A mode chosen before the pane's first prompt needs the same harness
                        # startup as a later role pick (#MSW7), while its Main stays in resolver.
                        role_request = {**request, "guest": {"model": config.model,
                                                            "effort": pane_role.effort}}
                        guest_provider = guest_harness_provider.start_provider(
                            pane_role.preset_id, role_request, workspace, config=config)
                else:
                    agent_role = "main"   # the role follows the main agent, or fell back to it
                    if spare is not None:
                        # The helper follows Main and Main was a guest: it follows where the
                        # resolver landed instead (#GH5T).
                        config, options["preset_id"] = spare.config, spare.preset_id
                state["agent_role"] = agent_role
                # Nothing on the list could take it either, so this worker has no model at all. It
                # is still configured — the board is files, so the pane opens, reads its
                # cards and shows in its model box why it cannot answer — on a provider that is
                # never called: a turn gets the sentence, not an endpoint error (#GH5T).
                stand_in = (guest_harness_provider.UnavailableProvider(
                    config, guest_harness_provider.helper_refusal(
                        guest_harness_provider.guest_name(config)))
                    if helper_on_guest and spare is None else None)
                # --- end model roles ---
                # A configure replaces the pane's agent, so a guest harness the old one held has
                # nobody left to close it (protocol 29.3). Idle by now: configure refuses mid-turn.
                if turns.agent is not None:
                    guest_harness_provider.detach(turns.agent)
                try:
                    agent = Agent(config, workspace, turns.agent_emit,
                                  provider=guest_provider or stand_in,
                                  keybindings=catalog, skills=skill_index, roles=resolver,
                                  # Protocol 33: the named scope and the brief. `tool_scope`
                                  # decides the tool set in one place (`Agent.tools`) instead of
                                  # being read off whether a board happens to be attached, and
                                  # the brief goes into the system prompt rather than in front
                                  # of every prompt.
                                  tool_scope=(context.scope if context is not None else None),
                                  context_spec=context, **options)
                except Exception:
                    if guest_provider is not None:
                        guest_provider.close()   # never leave a guest with no pane to own it
                    raise
                if guest_provider is not None:
                    guest_harness_provider.attach(agent, guest_provider)
                if console_session:
                    # The conversation this console had last time, loaded now that the agent
                    # exists; a key nothing has been said under yet starts empty under that id,
                    # and the ordinary end-of-turn autosave keeps it from then on (30.7). Never
                    # fatal: a console whose history cannot be read is a console with no history,
                    # not a surface that cannot be talked to.
                    try:
                        agent.adopt_session(console_session)
                    except (ValueError, OSError):
                        logs.event(log, "console_session_unreadable", level_name="warning")
                # --- subagents ---
                agents_request = request.get("agents") or {}
                if not isinstance(agents_request, dict):
                    raise ValueError("agents must be an object.")
                agent_catalog = agents_defs.load_catalog(workspace, agents_request.get("dirs"))
                subagent_factory = SubagentFactory(resolver.main_config, workspace, skills=skill_index,
                                                   preset_id=resolver.main_preset_id, key_lookup=keystore.lookup,
                                                   aliases=agents_request.get("aliases"), roles=resolver,
                                                   main_agent=agent)
                if "max_auto_turns" in agents_request:
                    subagents.set_options(agents_request["max_auto_turns"])
                turns.set_agent(agent)
                # Protocol 19.12: the "initialize a board here?" round trip watches this
                # agent's cancel_event, so Stop ends a turn that is waiting on the dialog.
                board.bind_agent(agent)
                # Protocol 30.3: Stop ends an `app_command` this agent is waiting on. And 30.5:
                # `session_info` and `activity` read this agent's *own* session — which is every
                # agent's to read since #AGNT. They used to be attached to a pane's agent alone,
                # so a console could not answer "why was that turn slow" about a turn of its own;
                # now there is one agent per worker and it gets them whatever surface it serves.
                app.bind_agent(agent)
                buffers.fail_pending()
                buffers.cancel = agent.cancel_event
                agent.executor.buffers = buffers
                activity_tools.ActivityTools.attach(agent, live_info=sessions.live_info)
                state["workspace"] = workspace
                agent.plugin_tools = workspace_plugins.PluginTools(workspaces)
                if agent.plugin_tools.groups():
                    agent.plugin_workspace_changed()   # a workspace active before this configure
                subagents.configure(agent_catalog, subagent_factory)
                subagents.attach(agent)
                # --- end subagents ---
                # `model_name` (protocol 13, card #MDL1 rule 1): the one name this model has,
                # beside the id the API takes, so every surface that prints it — the pane, the
                # phone — prints the same word without a catalog of its own.
                event = {"event": "configured", "model": config.model,
                         "model_name": model_name(resolver.main_preset_id, config.model),
                         "skills": len(agent.executor.skills.skills) if agent.executor.skills is not None else 0,
                         "agent_role": agent_role, "roles": resolver.summary(),
                         "tiers": resolver.tier_summary(),
                         "stream_tool_output": stream_tool_output[0],
                         **session_protocol.configured_fields(agent)}
                event["agents"] = len(agent_catalog.definitions)  # subagents
                if context is not None:
                    # Echoed so the GUI can see that the worker read the surface it is drawn on
                    # — the scope it settled on above all, since that is what decides the tools.
                    event["context"] = {**context.to_json(), "scope": agent.tool_scope}
                if board_summary is not None:
                    event["board"] = board_summary   # Board (protocol 17)
                if agent.executor.skills is not None:
                    event["skill_commands"] = agent.executor.skills.commands()
                if skill_index is not None and skill_index.skipped:
                    event["skills_skipped"] = skill_index.skipped[:50]
                emit(event)
                logs.event(log, "configured", model=config.model,
                           host=urllib.parse.urlsplit(config.base_url).hostname, role=agent_role,
                           skills=event.get("skills"), agents=event.get("agents"),
                           stall_s=getattr(agent.provider, "stall_timeout", agent.stall_timeout_s),
                           session=agent.session_id)
                # Protocol 13: `configured` already carries the table; a separate model_roles event
                # follows only when a role fell back, so its warnings reach the pane.
                if resolver.warnings:
                    emit(resolver.event(agent_role))
            elif kind == "set_board":
                # Protocol 19.11: attach this pane to a project's board, or detach it,
                # **without** ending the conversation. `configure` cannot do it — it builds a new
                # Agent, and with it a new conversation — so attaching a tab that is already
                # talking comes through here: the agent object, its messages and its session id
                # are untouched, and only its board tools and the board block of its system
                # prompt change. Mid-turn it lands when the turn ends, so a running turn keeps the
                # tool set it started with.
                board.set_board(request)
            elif kind == "keybindings":
                # Refresh the catalog after the GUI reloads keybindings.json; keeps the conversation.
                catalog = KeybindingCatalog(request.get("path"), request.get("actions"))
                agent = turns.agent
                if agent is None:
                    raise ValueError("Configure a provider before updating keybindings.")
                agent.executor.keybindings = catalog
                # The tab's helper runs a second Agent on this worker (30.7) and may rebind keys
                # itself (#GMCF), so its executor is pointed at the same fresh catalogue.
                board.set_keybindings(catalog)
                emit({"event": "keybindings_updated", "id": request.get("id")})
            elif kind == "presets":
                emit_presets(request.get("id"))
            elif kind in localmodels.TYPES:
                localmodels.handle(request, emit)
            elif kind == "guest_logins_refresh":
                # A guest sign-in the pane typed has finished (#M8S2): every login is asked again,
                # and the rows are pushed once the answers are in.
                guest_harness_provider.refresh_logins()
            elif kind in guest_accounts.TYPES:
                # A registered Claude Code / Codex login (#M8S2). A save or a delete changes the
                # preset list, and a saved account's login is asked again on a thread; the rows
                # are pushed once more when that answer lands.
                guest_accounts.handle(request, emit)
                if kind != "guest_accounts":
                    emit_presets()
                    guest_harness_provider.refresh_account_logins()
            elif kind in customproviders.TYPES:
                # A save or delete changes the preset list, so a fresh `presets` follows the answer.
                customproviders.handle(request, emit)
                if kind != "custom_providers":
                    emit_presets()
            elif kind == "hosted_quota":
                # Protocol 13.9: GET /v1/quota on a thread, so the loop never waits on the network;
                # exactly one event follows, hosted_quota or error.
                request_id = request.get("id")

                def quota_work(request_id=request_id):
                    try:
                        emit({"event": "hosted_quota", "id": request_id, **hosted.session().fetch_quota()})
                    except hosted.HostedUnavailable as exc:
                        emit({"event": "error", "id": request_id, "text": str(exc), "code": exc.code,
                              "resets_at": exc.resets_at})
                    except Exception as exc:                # never lets a thread die silently
                        emit({"event": "error", "id": request_id,
                              "text": f"Relay Free quota check failed ({type(exc).__name__})."})

                threading.Thread(target=quota_work, name="relay-hosted-quota", daemon=True).start()
            elif kind in ("store_key", "remove_key", "test_key") and request.get("preset") == "relay-pro":
                relay_pro.run(kind.removesuffix("_key"), emit, request.get("id"), request.get("api_key", ""))
            elif kind == "store_key":
                keystore.store(request.get("preset", ""), request.get("api_key", ""))
                emit({"event": "key_stored", "id": request.get("id"), "preset": request.get("preset")})
            elif kind == "remove_key":
                preset_id = request.get("preset", "")
                removed = keystore.remove(preset_id)
                emit({"event": "key_removed", "id": request.get("id"), "preset": preset_id, "removed": removed})
            elif kind == "test_key":
                # Protocol 13.8: one minimal call. The key is read here and never crosses the pipe.
                keytest.run(request.get("preset", ""), emit, request.get("id"))
            elif kind == "transcribe":
                # Protocol 16: the GUI recorded a clip and passes its path; the audio itself never
                # crosses this pipe. One `transcribed` event follows, success or failure.
                voice.run(request, emit)
            elif kind == "import_warp":
                imported, skipped = keystore.import_from_warp()
                emit({"event": "warp_imported", "id": request.get("id"),
                      "imported": [item.to_dict() for item in imported], "skipped": skipped})
            elif kind == "import_agent_tools":
                imported, skipped = keystore.import_from_agent_tools()
                emit({"event": "agent_tools_imported", "id": request.get("id"),
                      "imported": [item.to_dict() for item in imported], "skipped": skipped})
            elif kind == "import_opencode":
                imported, skipped = keystore.import_from_opencode()
                emit({"event": "opencode_imported", "id": request.get("id"),
                      "imported": [item.to_dict() for item in imported], "skipped": skipped})
            elif kind == "ask":
                # A pane-message wake (#R5TC) is an ask nobody typed; it is not the person
                # returning, so it must not reset the idle clocks.
                if not isinstance(request.get("pane_note"), dict):
                    subagents.user_activity()
                loaded = session_protocol.load_attachments(request, turns)
                if request.get("cards"):
                    # `#K7Q2` in the composer: the card, its open tasks and its thread tail travel
                    # with the prompt (protocol 17.8).
                    agent = turns.agent
                    if agent is None:
                        raise ValueError("Configure a provider and workspace first.")
                    loaded = (loaded or []) + board_protocol.card_attachments(
                        str(agent.executor.workspace.root), request["cards"],
                        board.tools.board if board.tools is not None else None)
                if request.get("skills"):
                    # `/clean-commit` in the composer: that skill's SKILL.md goes with the prompt as
                    # its instructions (protocol 11, skill commands).
                    agent = turns.agent
                    if agent is None:
                        raise ValueError("Configure a provider and workspace first.")
                    if agent.executor.skills is None:
                        raise ValueError("No skills are available in this pane.")
                    try:
                        loaded = (loaded or []) + agent.executor.skills.invoked(request["skills"])
                    except skills.SkillError as exc:
                        raise ValueError(f"Skill: {exc}") from None
                # Protocol 30.7: the one surface whose whole subject is the cards, asked in a
                # tab with no project attached, is refused in a sentence rather than with a
                # protocol error nobody can act on. Options, Actions and Sessions are about the
                # app and run exactly as usual without a board.
                asked_by = state.get("context")
                if (asked_by is not None and asked_by.name == "switchboard"
                        and board.tools is None):
                    raise ValueError(board_protocol.NO_BOARD_CHAT_ERROR)
                text = request.get("text", "")
                # The board a Board console's first question is seeded with (19.18): one
                # line per card, in front of the first prompt of the conversation and never
                # again. Every later prompt is the owner's words alone, the conversation being
                # the context — the card sessions' seeding rule.
                if turns.agent is not None and isinstance(text, str):
                    seed = board.console_seed(turns.agent)
                    if seed:
                        text = seed + "\n" + text
                # Cross-pane messaging (#R5TC, protocol 37): an idle pane is woken by the note
                # its GUI submits. The frame is built here, by the receiving worker, from the
                # delivery hop — the sender writes none of the text. origin "pane:p2" rides the
                # queue item and the ledger; author names the sender on the queue row.
                origin, author = "user", None
                pane_note = request.get("pane_note")
                if isinstance(pane_note, dict) and turns.agent is not None:
                    wake = turns.agent.executor.panes.wake_ask(pane_note)
                    text, origin, author = wake["text"], wake["origin"], wake["author"]
                turns.submit(text, request.get("when", "now"), request.get("id"),
                             request.get("context"), loaded or None,
                             requeue=request.get("requeue", True),
                             # Protocol 33 (#AGNT): which console asked, what it is showing, and
                             # whether this turn writes anything. A terminal pane sends none of
                             # the three and its turn is exactly what it was.
                             surface=request.get("surface"), screen=request.get("screen"),
                             readonly=bool(request.get("readonly", False)),
                             origin=origin, author=author)
            # --- cross-pane messaging (#R5TC, protocol 37) ---
            elif kind == "pane_roster":
                # The pane's directory push: who else exists (handle, title, workspace, busy),
                # this pane's own handle, and the agent/cross_pane kill switch. pane_list and
                # pane_send appear on the next model call after the first roster arrives.
                agent = turns.agent
                if agent is not None and agent.executor is not None:
                    agent.executor.panes.roster(request.get("panes"), request.get("self"),
                                                request.get("enabled", True))
            elif kind == "pane_note":
                # The GUI placed a peer's message in this pane: its turn is busy (the note is
                # read at the next step boundary), or a wake was withheld (the depth rule or
                # the pane's wake budget) and the note waits for the next turn. Either way the
                # existing notices path carries it — notify_main never starts a turn, and an
                # unread notice survives to the next one (_MainInbox.drain reads _notices lazily).
                agent = turns.agent
                if agent is None or agent.executor is None:
                    raise ValueError("No agent is configured in this pane.")
                note, peer_line = agent.executor.panes.deliver_note(request)
                subagents.notify_main(note)
                emit(peer_line)
            elif kind == "pane_message_result":
                # The directory's acceptance verdict for one pane_send: {id, ok, outcome...}.
                agent = turns.agent
                if agent is not None and agent.executor is not None:
                    agent.executor.panes.resolve(request)
            elif kind == "queue_steer":
                queue_for(request).steer(request.get("item"))
            elif kind == "queue_unsteer":
                queue_for(request).unsteer(request.get("request"), request.get("as_request"),
                                           request.get("item"))
            elif kind == "cancel":
                queue_for(request).cancel()
            elif kind == "resume_queue":
                # Enter on an empty prompt box, and the queue strip's Resume button (#7JD1). A
                # prompt submitted instead of it resumes the same queue on its way past
                # (`TurnSupervisor.submit`), which is why an `ask` carries nothing new.
                queue_for(request).resume()
            elif kind == "queue_remove":
                queue_for(request).remove(request.get("item"), request.get("id"))
            elif kind == "queue_move":
                # Protocol 33: drag a queued prompt up or down the line. The helper's own FIFO
                # had reorder and the pane's queue did not (#AGNT's Issue, itemised); now there
                # is one queue and it has it.
                queue_for(request).move(request.get("item"), request.get("to"), request.get("id"))
            elif kind == "queue_clear":
                queue_for(request).clear(request.get("id"))
            elif kind == "reset":
                turns.reset()
                subagents.stop_all(reset=True)
                # The new conversation's id: the pane's ⓘ and "already open" checks go by it.
                emit({"event": "reset", "session_id": turns.agent.session_id if turns.agent else None})
            # --- subagents (protocol sections 7 and 8) ---
            elif kind == "agents_list":
                catalog = subagents.catalog
                if catalog is None:
                    catalog = agents_defs.load_catalog(request.get("workspace") or os.getcwd(), None)
                emit({"event": "agents", "id": request.get("id"), "items": catalog.items(),
                      "duplicates": catalog.duplicates, "skipped": catalog.skipped})
            elif kind == "agent_subscribe":
                subagents.subscribe(request.get("id"), request.get("on", True) is not False)
            elif kind == "agent_message":
                result = subagents.send_message(request.get("id"), request.get("text"), origin="user")
                emit({"event": "agent_message_delivered", **result})
            elif kind == "todo_subagent":
                # The user hands one of the agent's todos to a background subagent (card #QHR1).
                agent = turns.agent
                if agent is None or subagents.catalog is None:
                    raise ValueError("Configure a provider and workspace first.")
                todo_id = request.get("todo_id")
                args = {**agent.todo_subagent_task(todo_id), "todo_id": todo_id}
                if isinstance(request.get("subagent_type"), str) and request["subagent_type"].strip():
                    args["subagent_type"] = request["subagent_type"].strip()
                sub = subagents.spawn(args)
                subagents.notify_main(f"The user handed todo {todo_id} to background subagent {sub.id} from the task "
                                      "list. Do not work on it yourself; its result will be delivered to you.")
                emit({"event": "todo_subagent", "id": request.get("id"), "todo_id": todo_id, "agent_id": sub.id})
            elif kind == "agent_stop":
                target = request.get("id")
                emit({"event": "agent_stopped", "ids": subagents.stop("all" if target in (None, "all") else target)})
            elif kind == "agent_pause":
                target = request.get("id")
                emit({"event": "agent_paused", "ids": subagents.pause("all" if target in (None, "all") else target)})
            elif kind == "agent_resume":
                result = subagents.resume(request.get("id"))
                emit({"event": "agent_message_delivered", **result})
            elif kind == "agent_set_model":
                target = request.get("id")
                subagents.set_model("all" if target in (None, "all") else target, request.get("model"))
            elif kind == "set_agent_options":
                validate_turn_options(request)  # refuse bad values before changing anything
                role_table = model_roles.validate_roles(request.get("roles")) if "roles" in request else None
                tier_table = model_roles.validate_tiers(request.get("tiers")) if "tiers" in request else None
                # Protocol 23.10: the pane's own switch, not the agent's — it changes what leaves
                # this process, so it applies with no agent configured and to the very next event.
                if "stream_tool_output" in request:
                    stream_tool_output[0] = tool_stream.validate(request["stream_tool_output"])
                # Protocol 12.11: likewise the pane's own switch. Turning it on starts from a whole
                # list, so the GUI is never left merging into a ledger it was not sent.
                if "requests_delta" in request:
                    requests_delta[0] = request_stream.validate(request["requests_delta"])
                    requests_stream.reset()
                fields = subagents.set_options(request.get("max_auto_turns"))
                fields["stream_tool_output"] = stream_tool_output[0]
                fields["requests_delta"] = requests_delta[0]
                agent = turns.agent
                changed_models = False
                if agent is not None:
                    fields.update(agent.set_options(request))  # protocol section 12
                    if agent.roles is not None and (role_table is not None or tier_table is not None):
                        # Protocol 13: both tables apply from the next call.
                        if tier_table is not None:
                            agent.roles.set_tiers(tier_table)
                        if role_table is not None:
                            agent.roles.set_roles(role_table)
                        fields["roles"] = agent.roles.summary()
                        fields["tiers"] = agent.roles.tier_summary()
                        changed_models = True
                emit({"event": "agent_options", "id": request.get("id"), **fields})
                if changed_models:
                    emit(agent.roles.event(state["agent_role"], request.get("id")))
            elif kind == "set_agent_role":
                # Protocol 13: switch this pane between the Main agent and another role (the Flash agent).
                role = model_roles.validate_role(request.get("role"))
                agent = turns.agent
                if agent is None or agent.roles is None:
                    raise ValueError("Configure a provider and workspace first.")
                # `preset`/`model`/`effort`: this pane's own pick for that role (13.5, card #MDL1).
                # The model box shows the mode's list and Enter on one of its rows means "this pane,
                # this mode, that model" — which the tier list alone cannot say, because it belongs
                # to every pane. Absent, the role resolves off the list exactly as it always has.
                pick = request.get("preset")
                resolved = (agent.roles.resolve_entry(role, {"preset": pick,
                                                             "model": request.get("model"),
                                                             "effort": request.get("effort")})
                            if isinstance(pick, str) and pick.strip() else agent.roles.choose_role(role))
                if board.refuse_model_selection(request, resolved.config):
                    continue
                new_role = "main" if resolved.is_main else role
                def follow_role(_agent, new_role=new_role):
                    state["agent_role"] = new_role

                role_request = dict(request)
                if guest_harness_provider.is_guest_preset(resolved.preset_id):
                    role_request["guest"] = {"model": resolved.config.model,
                                             "effort": resolved.effort or agent.effort}
                elif resolved.effort:
                    role_request["effort"] = resolved.effort
                sessions.switch_model(
                    role_request, resolved.config, resolved.preset_id, follow=follow_role,
                    fields={"agent_role": new_role,
                            **({"warning": resolved.warning} if resolved.warning else {})},
                    refused_fields=lambda: {"agent_role": state["agent_role"]})
            # --- the agent typing into the program in the visible pane (protocol 17) ---
            elif kind == "remote_session_update":
                # Live pane identity can change while a guest turn is still running.
                # Replace the snapshot atomically; do not wait on its active tool lock.
                from relay_core import remote_session
                if turns.agent is not None:
                    turns.agent.executor.remote_session = remote_session.validate(request.get("remote_session") or None)
            elif kind == "program_state":
                # The pane's live view: who owns the terminal, what it is asking, and whether the
                # user has handed it over. A take-over arrives here as granted: false.
                agent = turns.agent
                if agent is None:
                    raise ValueError("Configure a provider and workspace first.")
                summary = agent.executor.program.update(request)
                if request.get("id") is not None:
                    emit({"event": "program_control", "id": request.get("id"), **summary})
            elif kind == "program_input_result":
                # The pane's answer to a `program_input`; the waiting turn thread picks it up.
                agent = turns.agent
                if agent is None:
                    raise ValueError("Configure a provider and workspace first.")
                agent.executor.program.resolve(request)
            elif kind == "question_answer":
                # The pane's answer to an `ask_user` card (protocol 27); the waiting turn thread
                # picks it up.
                agent = turns.agent
                if agent is None:
                    raise ValueError("Configure a provider and workspace first.")
                # A guest's approval or question card is the same round trip (protocol 29.3), so
                # the harness provider gets first refusal on the id before the agent's own tool.
                if not guest_harness_provider.answer_question(agent.provider, request):
                    # The card may be this agent's or one of its subagents' (card #K2FV); the id
                    # pending says whose, and a late answer nobody holds is ignored either way.
                    if not (agent.subagents is not None and agent.subagents.resolve_question(request)):
                        agent.executor.questions.resolve(request)
            elif kind == "terminal_command_result":
                # The pane's answer to a `terminal_command` (protocol 22).
                agent = turns.agent
                if agent is None:
                    raise ValueError("Configure a provider and workspace first.")
                agent.executor.terminal.resolve(request)
            # --- end program control ---
            elif kind == "agents_status":
                emit({"event": "agents_status", "items": subagents.list()})
            elif kind == "memory_import":
                # Protocol 34 (#MEMS): the same switch as `configure.memory_import`, for a pane whose
                # configure waits for its first prompt (a guest ranked first, or no provider yet),
                # so the import still runs when Relay starts.
                memory_startup.configure(request.get("enabled", True))
            # --- end subagents ---
            # --- the agent drives the app (protocol section 30) ---
            elif app.handles(kind):
                app.dispatch(request)
            elif buffers.handles(kind):
                buffers.dispatch(request)
            elif sessions.handles(kind):
                sessions.handle(kind, request)
            elif observe.handles(kind):
                observe.handle(kind, request)
            # --- Board (protocol section 17) ---
            elif globals_commands.handles(kind):
                globals_commands.dispatch(request)
            elif workspaces.handles(kind):
                workspaces.dispatch(request, state.get("workspace"))
            elif board.handles(kind):
                board.dispatch(request)
            elif kind == "shutdown":
                # The pane is closing (`Pane::~Pane` sends `cancel` then this, and waits 1.5 s):
                # a final recap has to outlive this worker. The helper reads the saved session
                # and writes its metadata after this process has gone (#RCP9).
                final_summary.start(turns.agent)
                # the cards it claimed are nobody's, so its `session` comes off them before the
                # loop ends (protocol 19.19, #R9G7). Bounded, never raises, and the cards stay in
                # Executing — the work is in flight, only the pane that held it has gone.
                board.release_claims("the pane closed")
                break
            else:
                raise ValueError("Unknown protocol message.")
        except Exception as exc:
            # The message, not the request: a request carries prompt text and must not be logged.
            logs.event(log, "protocol_error", level_name="error", kind=locals().get("kind"),
                       error=type(exc).__name__, msg=str(exc)[:300])
            configure_fault = (locals().get("kind") == "configure"
                               and isinstance(exc, (NameError, AttributeError, TypeError, ImportError)))
            error = {"event": "error", "id": request.get("id") if isinstance(locals().get("request"), dict) else None,
                     "agent_busy": turns.busy,
                     "text": str(exc)[:2000] if configure_fault or isinstance(exc, (ValueError, OSError, keystore.KeystoreError))
                     else f"Protocol error ({type(exc).__name__})."}
            if locals().get("kind") in ("set_model", "set_agent_role") and turns.agent is not None:
                active = turns.agent
                error.update(event="model_switch_refused", code="model_switch_failed", at="request",
                             model=request.get("model", ""), current_model=active.config.model,
                             preset=active.preset.id if active.preset else None,
                             agent_role=state["agent_role"], context_window=active.context.window,
                             effort=active.effort, reason=error["text"])
            if configure_fault:
                error.update(code="configure_failed", exception=type(exc).__name__, restart_worker=not turns.busy)
            emit(error)
    logs.event(log, "worker_stop", pid=os.getpid())
    # The guest is a process of this worker's (protocol 29.3): it goes when the worker goes.
    if turns.agent is not None:
        guest_harness_provider.detach(turns.agent)
    app.shutdown()       # nothing is left parked on a pane that has gone (protocol 30.3)
    workspaces.shutdown()   # every kernel and TeX builder this pane started (protocol 36)
    buffers.fail_pending()
    subagents.shutdown()
    observe.shutdown()
    turns.shutdown(timeout=1)

if __name__ == "__main__":
    main()
