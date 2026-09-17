#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Private NDJSON stdio worker. No TCP listener, telemetry, or persistent secrets."""
from __future__ import annotations

import json
import os
import sys
import threading
import urllib.parse

from relay_core import (__version__, keystore, keytest, logs, observe_protocol, roles as model_roles,
                        session_protocol, skills, voice)
from relay_core.agent import Agent, validate_turn_options
from relay_core import agents_defs
from relay_core.subagents import SubagentFactory, SubagentManager
from relay_core.keybindings import KeybindingCatalog, KeybindingError
from relay_core.presets import PRESETS
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

    def emit(obj: dict):
        with output_lock:
            sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n")
            sys.stdout.flush()

    # Subagents observe main-turn endings to wake the main agent for background results.
    subagents = SubagentManager(emit)
    # Which model role this pane's own agent runs (protocol 13): "main", or "fast" for panes that
    # default to the fast agent.
    state = {"agent_role": "main"}

    def turn_emit(obj: dict):
        emit(obj)
        subagents.observe(obj)

    turns = TurnSupervisor(turn_emit)
    subagents.turns = turns

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
        on_conversation_replaced=lambda: subagents.stop_all(reset=True))

    observe = observe_protocol.ObserveCommands(turns, emit)  # protocol 11

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
                decision = classify(request.get("text", ""), request.get("mode", "auto"), known,
                                    request.get("path", os.environ.get("PATH", os.defpath)), cwd)
                emit({"event": "route", "id": request.get("id"), **decision.to_dict()})
            elif kind == "configure":
                if turns.busy:
                    raise ValueError("Stop the active agent turn before changing provider or workspace.")
                config = session_protocol.provider_config(request)
                catalog = KeybindingCatalog.from_request(request.get("keybindings"))
                workspace = request.get("workspace", os.getcwd())
                skill_index = skills.from_request(request.get("skills"), workspace)
                # --- model roles (protocol 13) ---
                role_table = model_roles.validate_roles(request.get("roles"))
                tier_table = model_roles.validate_tiers(request.get("tiers"))
                agent_role = model_roles.validate_role(request.get("agent_role") or "main")
                options = session_protocol.agent_options(request, workspace)
                resolver = model_roles.RoleResolver(config, options.get("preset_id"), role_table,
                                                    key_lookup=keystore.lookup, main_effort=options.get("effort"),
                                                    tiers=tier_table)
                pane_role = resolver.resolve(agent_role)
                if not pane_role.is_main:
                    config, options["preset_id"] = pane_role.config, pane_role.preset_id
                else:
                    agent_role = "main"   # the role follows the main agent, or fell back to it
                state["agent_role"] = agent_role
                # --- end model roles ---
                agent = Agent(config, workspace, turns.agent_emit, keybindings=catalog, skills=skill_index,
                              roles=resolver, **options)
                # --- subagents ---
                agents_request = request.get("agents") or {}
                if not isinstance(agents_request, dict):
                    raise ValueError("agents must be an object.")
                agent_catalog = agents_defs.load_catalog(workspace, agents_request.get("dirs"))
                subagent_factory = SubagentFactory(resolver.main_config, workspace, skills=skill_index,
                                                   preset_id=resolver.main_preset_id, key_lookup=keystore.lookup,
                                                   aliases=agents_request.get("aliases"), roles=resolver)
                if "max_auto_turns" in agents_request:
                    subagents.set_options(agents_request["max_auto_turns"])
                turns.set_agent(agent)
                subagents.configure(agent_catalog, subagent_factory)
                subagents.attach(agent)
                # --- end subagents ---
                event = {"event": "configured", "model": config.model,
                         "skills": len(agent.executor.skills.skills) if agent.executor.skills is not None else 0,
                         "agent_role": agent_role, "roles": resolver.summary(),
                         "tiers": resolver.tier_summary(),
                         **session_protocol.configured_fields(agent)}
                event["agents"] = len(agent_catalog.definitions)  # subagents
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
            elif kind == "keybindings":
                # Refresh the catalog after the GUI reloads keybindings.json; keeps the conversation.
                catalog = KeybindingCatalog(request.get("path"), request.get("actions"))
                agent = turns.agent
                if agent is None:
                    raise ValueError("Configure a provider before updating keybindings.")
                agent.executor.keybindings = catalog
                emit({"event": "keybindings_updated", "id": request.get("id")})
            elif kind == "presets":
                # key_source says where each key comes from so the keys modal can show "from
                # RELAY_*_API_KEY" instead of offering to remove something it cannot remove.
                sources = keystore.sources()
                emit({"event": "presets", "id": request.get("id"), "warp_default": keystore.warp_default_preset(),
                      "tier_defaults": model_roles.tier_catalog(), "role_actions": model_roles.action_catalog(),
                      "presets": [{**p.to_dict(), "has_stored_key": bool(sources[p.id]),
                                   "key_source": sources[p.id]} for p in PRESETS.values()]})
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
                turns.submit(request.get("text", ""), request.get("when", "now"), request.get("id"),
                             request.get("context"), session_protocol.load_attachments(request, turns),
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
                emit({"event": "reset"})
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
            elif kind == "agent_stop":
                target = request.get("id")
                emit({"event": "agent_stopped", "ids": subagents.stop("all" if target in (None, "all") else target)})
            elif kind == "set_agent_options":
                validate_turn_options(request)  # refuse bad values before changing anything
                role_table = model_roles.validate_roles(request.get("roles")) if "roles" in request else None
                tier_table = model_roles.validate_tiers(request.get("tiers")) if "tiers" in request else None
                fields = subagents.set_options(request.get("max_auto_turns"))
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
                # Protocol 13: switch this pane between the main agent and another role (the fast agent).
                role = model_roles.validate_role(request.get("role"))
                agent = turns.agent
                if agent is None or agent.roles is None:
                    raise ValueError("Configure a provider and workspace first.")
                if turns.busy:
                    raise ValueError("Stop the active agent turn before switching the pane's agent.")
                resolved = agent.roles.resolve(role)
                state["agent_role"] = "main" if resolved.is_main else role
                agent.set_model(resolved.config, resolved.preset_id)
                emit({"event": "model_changed", "id": request.get("id"), "model": resolved.config.model,
                      "preset": resolved.preset_id, "context_window": agent.context.window,
                      "effort": agent.effort, "agent_role": state["agent_role"],
                      **({"warning": resolved.warning} if resolved.warning else {})})
                emit(agent.context_event())
            elif kind == "agents_status":
                emit({"event": "agents_status", "items": subagents.list()})
            # --- end subagents ---
            elif sessions.handles(kind):
                sessions.handle(kind, request)
            elif observe.handles(kind):
                observe.handle(kind, request)
            elif kind == "shutdown":
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
    subagents.shutdown()
    observe.shutdown()
    turns.shutdown(timeout=1)

if __name__ == "__main__":
    main()
