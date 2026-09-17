#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Private NDJSON stdio worker. No TCP listener, telemetry, or persistent secrets."""
from __future__ import annotations

import json
import os
import sys
import threading

from relay_core import __version__, keystore, observe_protocol, session_protocol, skills
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
    output_lock = threading.Lock()

    def emit(obj: dict):
        with output_lock:
            sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n")
            sys.stdout.flush()

    # Subagents observe main-turn endings to wake the main agent for background results.
    subagents = SubagentManager(emit)

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
                agent = Agent(config, workspace, turns.agent_emit, keybindings=catalog, skills=skill_index,
                              **session_protocol.agent_options(request, workspace))
                # --- subagents ---
                agents_request = request.get("agents") or {}
                if not isinstance(agents_request, dict):
                    raise ValueError("agents must be an object.")
                agent_catalog = agents_defs.load_catalog(workspace, agents_request.get("dirs"))
                subagent_factory = SubagentFactory(config, workspace, skills=skill_index,
                                                   preset_id=request.get("preset"), key_lookup=keystore.lookup,
                                                   aliases=agents_request.get("aliases"))
                if "max_auto_turns" in agents_request:
                    subagents.set_options(agents_request["max_auto_turns"])
                turns.set_agent(agent)
                subagents.configure(agent_catalog, subagent_factory)
                subagents.attach(agent)
                # --- end subagents ---
                event = {"event": "configured", "model": config.model,
                         "skills": len(agent.executor.skills.skills) if agent.executor.skills is not None else 0,
                         **session_protocol.configured_fields(agent)}
                event["agents"] = len(agent_catalog.definitions)  # subagents
                if skill_index is not None and skill_index.skipped:
                    event["skills_skipped"] = skill_index.skipped[:50]
                emit(event)
            elif kind == "keybindings":
                # Refresh the catalog after the GUI reloads keybindings.json; keeps the conversation.
                catalog = KeybindingCatalog(request.get("path"), request.get("actions"))
                agent = turns.agent
                if agent is None:
                    raise ValueError("Configure a provider before updating keybindings.")
                agent.executor.keybindings = catalog
                emit({"event": "keybindings_updated", "id": request.get("id")})
            elif kind == "presets":
                stored = keystore.available()
                emit({"event": "presets", "id": request.get("id"), "warp_default": keystore.warp_default_preset(),
                      "presets": [{**p.to_dict(), "has_stored_key": stored[p.id]} for p in PRESETS.values()]})
            elif kind == "store_key":
                keystore.store(request.get("preset", ""), request.get("api_key", ""))
                emit({"event": "key_stored", "id": request.get("id"), "preset": request.get("preset")})
            elif kind == "import_warp":
                imported, skipped = keystore.import_from_warp()
                emit({"event": "warp_imported", "id": request.get("id"),
                      "imported": [item.to_dict() for item in imported], "skipped": skipped})
            elif kind == "ask":
                subagents.user_activity()
                turns.submit(request.get("text", ""), request.get("when", "now"), request.get("id"),
                             request.get("context"), session_protocol.load_attachments(request, turns),
                             requeue=request.get("requeue", True))
            elif kind == "queue_steer":
                turns.steer(request.get("item"))
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
                fields = subagents.set_options(request.get("max_auto_turns"))
                if turns.agent is not None:
                    fields.update(turns.agent.set_options(request))  # protocol section 12
                emit({"event": "agent_options", "id": request.get("id"), **fields})
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
            emit({"event": "error", "id": request.get("id") if isinstance(locals().get("request"), dict) else None,
                  "agent_busy": turns.busy,
                  "text": str(exc)[:2000] if isinstance(exc, (ValueError, OSError, keystore.KeystoreError)) else f"Protocol error ({type(exc).__name__})."})
    subagents.shutdown()
    observe.shutdown()
    turns.shutdown(timeout=1)

if __name__ == "__main__":
    main()
