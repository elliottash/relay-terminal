#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Private NDJSON stdio worker. No TCP listener, telemetry, or persistent secrets."""
from __future__ import annotations

import json
import os
import sys
import threading

from relay_core import __version__, keystore, skills
from relay_core.agent import Agent
from relay_core.keybindings import KeybindingCatalog, KeybindingError
from relay_core.presets import PRESETS, match_preset
from relay_core.queue import TurnSupervisor
from relay_core.provider import ProviderConfig
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

    turns = TurnSupervisor(emit)

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
                config = ProviderConfig(request.get("base_url", ""), request.get("model", ""),
                                        api_key, request.get("extra", {}),
                                        request.get("max_tokens", 8192))
                config.validate()
                catalog = KeybindingCatalog.from_request(request.get("keybindings"))
                workspace = request.get("workspace", os.getcwd())
                skill_index = skills.from_request(request.get("skills"), workspace)
                agent = Agent(config, workspace, turns.agent_emit, keybindings=catalog, skills=skill_index)
                turns.set_agent(agent)
                event = {"event": "configured", "model": config.model,
                         "skills": len(agent.executor.skills.skills) if agent.executor.skills is not None else 0}
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
                turns.submit(request.get("text", ""), request.get("when", "now"), request.get("id"),
                             request.get("context"))
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
                emit({"event": "reset"})
            elif kind == "shutdown":
                break
            else:
                raise ValueError("Unknown protocol message.")
        except Exception as exc:
            emit({"event": "error", "id": request.get("id") if isinstance(locals().get("request"), dict) else None,
                  "agent_busy": turns.busy,
                  "text": str(exc)[:2000] if isinstance(exc, (ValueError, OSError, keystore.KeystoreError)) else f"Protocol error ({type(exc).__name__})."})
    turns.shutdown(timeout=1)

if __name__ == "__main__":
    main()
