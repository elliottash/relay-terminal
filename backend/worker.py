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

from relay_core import (__version__, board_protocol, customproviders, hosted, keystore, keytest, localmodels, logs,
                        observe_protocol, roles as model_roles, session_protocol, skills, voice)
from relay_core.agent import Agent, validate_turn_options
from relay_core import activity_tools, agents_defs, app_tools, guest_harness_provider, openrouter_catalog
from relay_core import tool_stream
from relay_core.subagents import SubagentFactory, SubagentManager
from relay_core.keybindings import KeybindingCatalog
from relay_core.presets import PRESETS, tier_list_defaults
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

    def emit(obj: dict):
        if not stream_tool_output[0]:
            obj = tool_stream.counted(obj)
        with output_lock:
            sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n")
            sys.stdout.flush()

    # Subagents observe main-turn endings to wake the main agent for background results.
    subagents = SubagentManager(emit)
    # Which model role this pane's own agent runs (protocol 13): "main", or "flash" for panes that
    # default to the Flash agent.
    state = {"agent_role": "main"}

    # Switchboard (protocol 17). `board` also tags board_ask turn events with their card_id and
    # appends the agent's answer to the card thread, so it is created before the supervisor.
    board = board_protocol.BoardCommands(None, emit)

    # The agent drives the app (protocol 30, card #FEJQ): the GUI's Options and actions catalog,
    # the `app_command` round trip and this worker's change log. Created before the supervisor
    # for the same reason the board is — it outlives every `configure`, so what the agent has
    # already changed is still listed after the pane's model or workspace changes.
    app = app_tools.AppCommands(emit, sessions=lambda: sessions.index(),
                                agent=lambda: turns.agent)

    def turn_emit(obj: dict):
        obj = board.observe(obj)
        emit(obj)
        subagents.observe(obj)
        sessions.observe(obj)   # pane title (protocol 18): a finished turn may be owed a fresh one

    turns = TurnSupervisor(turn_emit)
    subagents.turns = turns
    board.turns = turns

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
        relay_free = {"has_stored_key": False, "key_source": "included", **hosted.status()}
        # OpenRouter's live model list is the `openrouter` row's catalog (owner, 2026-09-20):
        # served from the day-old cache now, fetched on its own thread once per process when
        # that is stale, and re-pushed below when the fetch lands. Never on this thread.
        openrouter_catalog.start_refresh()
        # The two default fillings of Options › Models' five lists (owner, 2026-09-20; 13.7),
        # computed from what can take a turn right now so the GUI's two buttons only apply them.
        guest_rows = guest_harness_provider.preset_rows()
        custom_rows = customproviders.rows()
        list_defaults = tier_list_defaults(
            [p for p in PRESETS if sources.get(p) or (PRESETS[p].hosted and relay_free.get("available"))],
            local=[(e.id, e.model) for e in localmodels.catalog().values()],
            custom=[(row["id"], row.get("model") or "") for row in custom_rows if row.get("has_stored_key")],
            guests=guest_rows)
        emit({"event": "presets", "id": request_id, "warp_default": keystore.warp_default_preset(),
              "tier_defaults": model_roles.tier_catalog(), "role_actions": model_roles.action_catalog(),
              "tier_list_defaults": list_defaults,
              "presets": [{**p.to_dict(), **(relay_free if p.hosted else
                                             {"has_stored_key": bool(sources[p.id]),
                                              "key_source": sources[p.id]})}
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
              + guest_rows})

    # The codex catalogue lands after the first `presets` answer (the scan must not delay it,
    # 29.3), and nothing re-asks — so the worker pushes a fresh `presets` when it does, and
    # Options' Codex row turns from the text field into the dropdown on its own.
    guest_harness_provider.set_catalog_listener(lambda: emit_presets())
    # The same for OpenRouter's listing: the first `presets` answer carries whatever the cache
    # held, and the fetch that lands after it pushes a fresh one, so the id box completes against
    # the live list without a re-ask.
    openrouter_catalog.set_listener(lambda: emit_presets())
    # And for a custom provider's /models listing (28.6): the save answers at once, the probe
    # lands later and pushes the row with the served models added.
    customproviders.set_listener(lambda: emit_presets())

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
            if kind == "route":
                known = request.get("known_commands", [])
                if not isinstance(known, list) or len(known) > 20000 or not all(isinstance(x, str) for x in known):
                    raise ValueError("Invalid command-name list.")
                cwd = request.get("cwd")
                if cwd is not None and (not isinstance(cwd, str) or not os.path.isdir(cwd)):
                    cwd = None
                # remote: the terminal is at a prompt on this ssh host (card #S5SH); the router then
                # ignores the local PATH, aliases and cwd.
                decision = classify(request.get("text", ""), request.get("mode", "auto"), known,
                                    request.get("path", os.environ.get("PATH", os.defpath)), cwd,
                                    remote=request.get("remote"))
                emit({"event": "route", "id": request.get("id"), **decision.to_dict()})
            elif kind == "configure":
                if turns.busy:
                    raise ValueError("Stop the active agent turn before changing provider or workspace.")
                # One resolved absolute workspace for the whole of `configure`. `board_workspace`
                # is None when the GUI named none: the agent may fall back to the process's cwd
                # (that is its sandbox), but the *board* may not — `workspace: ""` used to make
                # `Path("") / "issues"` relative, so a pane with no workspace quietly opened the
                # board of whatever directory Relay was launched from (186 cards of another
                # project), which is why the Switchboard looked global rather than per project.
                asked = request.get("workspace")
                board_workspace = (str(Path(asked).expanduser().resolve())
                                   if isinstance(asked, str) and asked.strip() else None)
                workspace = board_workspace or str(Path(os.getcwd()).resolve())
                # The Switchboard is files, not a model: set it up before the provider is resolved,
                # so a missing key still lets the pane open and browse the cards (only board_ask
                # needs the agent). Before 2026-09-17 a keyless window sat on "Loading…" forever.
                board_summary = board.configure(board_workspace, request)
                # Protocol 30.7: which tab this worker is the helper of. It keys the helper's
                # conversation with the workspace `board.configure` has just settled, so the
                # same tab comes back with its own history after a restart.
                board.set_tab(request.get("tab"))
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
                agent_role = model_roles.validate_role(request.get("agent_role") or "main")
                # Protocol 23.10: stated in full by every `configure`, so a GUI that does not know
                # the option — or one whose pane has just stopped needing the text — gets the
                # default back rather than whatever the last pane asked for.
                stream_tool_output[0] = (tool_stream.validate(request["stream_tool_output"])
                                         if request.get("stream_tool_output") is not None else True)
                options = session_protocol.agent_options(request, workspace)
                options["board"] = board.agent_tools(board_workspace, request)
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
                    # A guest pane's agent *is* the guest: no role may put another model's config
                    # under the harness (the provider would stay the guest's and the pane would
                    # report a model it is not running).
                    agent_role = "main"
                elif not pane_role.is_main:
                    config, options["preset_id"] = pane_role.config, pane_role.preset_id
                else:
                    agent_role = "main"   # the role follows the main agent, or fell back to it
                    if spare is not None:
                        # The helper follows Main and Main was a guest: it follows where the
                        # resolver landed instead (#GH5T).
                        config, options["preset_id"] = spare.config, spare.preset_id
                state["agent_role"] = agent_role
                # Nothing on the list could take it either, so this worker has no model at all. It
                # is still configured — the Switchboard is files, so the pane opens, reads its
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
                                  keybindings=catalog, skills=skill_index, roles=resolver, **options)
                except Exception:
                    if guest_provider is not None:
                        guest_provider.close()   # never leave a guest with no pane to own it
                    raise
                if guest_provider is not None:
                    guest_harness_provider.attach(agent, guest_provider)
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
                # Protocol 19.12: the "initialize a Switchboard here?" round trip watches this
                # agent's cancel_event, so Stop ends a turn that is waiting on the dialog.
                board.bind_agent(agent)
                # Protocol 30.3: Stop ends an `app_command` this agent is waiting on. And 30.5:
                # `session_info` and `activity` are the *pane* agent's, so they are attached
                # here and nowhere else — the helper worker's agents never get them.
                app.bind_agent(agent)
                activity_tools.ActivityTools.attach(agent, live_info=sessions.live_info)
                subagents.configure(agent_catalog, subagent_factory)
                subagents.attach(agent)
                # --- end subagents ---
                event = {"event": "configured", "model": config.model,
                         "skills": len(agent.executor.skills.skills) if agent.executor.skills is not None else 0,
                         "agent_role": agent_role, "roles": resolver.summary(),
                         "tiers": resolver.tier_summary(),
                         "stream_tool_output": stream_tool_output[0],
                         **session_protocol.configured_fields(agent)}
                event["agents"] = len(agent_catalog.definitions)  # subagents
                if board_summary is not None:
                    event["board"] = board_summary   # Switchboard (protocol 17)
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
                # Protocol 19.11: attach this pane to a project's Switchboard, or detach it,
                # **without** ending the conversation. `configure` cannot do it — it builds a new
                # Agent, and with it a new conversation — so attaching a tab that is already
                # talking comes through here: the agent object, its messages and its session id
                # are untouched, and only its board tools and the Switchboard block of its system
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
                emit({"event": "keybindings_updated", "id": request.get("id")})
            elif kind == "presets":
                emit_presets(request.get("id"))
            elif kind in localmodels.TYPES:
                localmodels.handle(request, emit)
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
            elif kind == "ask":
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
                turns.submit(request.get("text", ""), request.get("when", "now"), request.get("id"),
                             request.get("context"), loaded or None,
                             requeue=request.get("requeue", True))
            elif kind == "queue_steer":
                turns.steer(request.get("item"))
            elif kind == "queue_unsteer":
                turns.unsteer(request.get("request"), request.get("as_request"))
            elif kind == "cancel":
                turns.cancel()
            elif kind == "resume_queue":
                turns.resume()
            elif kind == "queue_remove":
                turns.remove(request.get("item"))
            elif kind == "queue_clear":
                turns.clear()
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
                fields = subagents.set_options(request.get("max_auto_turns"))
                fields["stream_tool_output"] = stream_tool_output[0]
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
                resolved = agent.roles.resolve(role)
                new_role = "main" if resolved.is_main else role
                changed = {"event": "model_changed", "id": request.get("id"), "model": resolved.config.model,
                           "preset": resolved.preset_id, "effort": agent.effort, "agent_role": new_role,
                           **({"warning": resolved.warning} if resolved.warning else {})}

                def role_decide(idle, agent=agent, resolved=resolved, new_role=new_role, changed=changed):
                    # Idle: at once, or after the compaction a smaller window needs. Mid-turn (issue
                    # 3ES1): like set_model, it lands before the turn's next request. Its own
                    # follow-up: a role switch sets the pane's role, it does not rebase the roles.
                    def follow(_agent, new_role=new_role):
                        state["agent_role"] = new_role

                    def apply_now():
                        state["agent_role"] = new_role
                        agent.set_model(resolved.config, resolved.preset_id)
                    with agent._model_lock:
                        outcome = agent.request_model(
                            resolved.config, resolved.preset_id, idle=idle, apply_now=apply_now,
                            start_exclusive=lambda task: turns.start_exclusive_locked("set_agent_role", task),
                            on_applied=follow, fields={"agent_role": new_role},
                            refused_fields=lambda: {"agent_role": state["agent_role"]})
                        if outcome["applies"] == "refused":
                            emit({"event": "model_switch_refused", "id": request.get("id"), "at": "request",
                                  "model": resolved.config.model, "current_model": agent.config.model,
                                  "preset": agent.preset.id if agent.preset else None,
                                  "context_window": agent.context.window, "effort": agent.effort,
                                  "agent_role": state["agent_role"], "reason": outcome["reason"]})
                            return
                        if outcome["applies"] == "now":
                            state["agent_role"] = new_role
                        emit({**changed, **outcome})
                turns.now_or_later(lambda: role_decide(True), lambda: role_decide(False))
                emit(agent.context_event())
            # --- the agent typing into the program in the visible pane (protocol 17) ---
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
            # --- end subagents ---
            # --- the agent drives the app (protocol section 30) ---
            elif app.handles(kind):
                app.dispatch(request)
            elif sessions.handles(kind):
                sessions.handle(kind, request)
            elif observe.handles(kind):
                observe.handle(kind, request)
            # --- Switchboard (protocol section 17) ---
            elif board.handles(kind):
                board.dispatch(request)
            elif kind == "shutdown":
                # The pane is closing (`Pane::~Pane` sends `cancel` then this, and waits 1.5 s):
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
            emit({"event": "error", "id": request.get("id") if isinstance(locals().get("request"), dict) else None,
                  "agent_busy": turns.busy,
                  "text": str(exc)[:2000] if isinstance(exc, (ValueError, OSError, keystore.KeystoreError)) else f"Protocol error ({type(exc).__name__})."})
    logs.event(log, "worker_stop", pid=os.getpid())
    # The guest is a process of this worker's (protocol 29.3): it goes when the worker goes.
    if turns.agent is not None:
        guest_harness_provider.detach(turns.agent)
    app.shutdown()       # nothing is left parked on a pane that has gone (protocol 30.3)
    subagents.shutdown()
    observe.shutdown()
    turns.shutdown(timeout=1)

if __name__ == "__main__":
    main()
