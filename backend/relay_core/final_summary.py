# SPDX-License-Identifier: AGPL-3.0-or-later
"""Finish a closed pane's session summary outside that pane's short-lived worker.

The worker sends the chosen chores provider over a private stdin pipe. No API key, prompt, or
conversation text is placed in process arguments or a temporary file. The child reads the saved
session and writes only its metadata and index, which remain safe if another pane resumes it.
"""
from __future__ import annotations

import dataclasses
import hashlib
import json
import os
import subprocess
import sys
import threading
from pathlib import Path

from . import logs, titles
from .agent import _provider_for
from .provider import ProviderConfig
from .sessions import SessionStore, read_meta

_log = logs.get("final_summary")


def _fingerprint(data: dict) -> str:
    relevant = (data.get("turns"), data.get("epoch"), data.get("messages"))
    return hashlib.sha256(json.dumps(relevant, sort_keys=True, ensure_ascii=False).encode()).hexdigest()


def _success_turn(data: dict, meta: dict) -> int:
    if not (meta.get("summary") or data.get("summary")):
        return 0
    values = (meta.get("summary_success_turn"), data.get("summary_success_turn"))
    known = [v for v in values if type(v) is int and v >= 0]
    if known:
        return max(known)
    # Old saved sessions had only summary_turn. A failed refresh could advance it, so this
    # fallback applies only to sessions written before summary_success_turn existed.
    old = (meta.get("summary_turn"), data.get("summary_turn"))
    return max((v for v in old if type(v) is int and v >= 0), default=0)


def needs_final_summary(data: dict, meta: dict) -> bool:
    turns = data.get("turns")
    if type(turns) is not int or turns < 1:
        return False
    inputs = titles.saved_inputs(data)
    return titles.has_reply(inputs["messages"]) and _success_turn(data, meta) < turns


def start(agent) -> bool:
    """Schedule a detached recap before worker shutdown; False means nothing was scheduled."""
    store = agent.store if agent is not None else None
    if store is None or agent.turns < 1:
        return False
    agent.autosave()
    try:
        data = store.load(agent.session_id)
        if not needs_final_summary(data, read_meta(store.directory, agent.session_id)):
            return False
        provider = agent.side_provider(cheap=True, role="chores", max_tokens=titles.SUMMARY_MAX_TOKENS)
        if provider is None or not getattr(provider, "serves_side_calls", True):
            logs.event(_log, "final_summary_skipped", reason="no_chores_provider", session=agent.session_id)
            return False
        config = dataclasses.asdict(provider.config)
        payload = {"session_dir": str(store.directory), "session_id": agent.session_id,
                   "turns": data["turns"], "fingerprint": _fingerprint(data),
                   "config": config, "stall_timeout": agent.stall_timeout_s}
        backend = str(Path(__file__).resolve().parent.parent)
        env = os.environ.copy()
        env["PYTHONPATH"] = os.pathsep.join(filter(None, (backend, env.get("PYTHONPATH"))))
        flags = (subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.CREATE_NO_WINDOW) if os.name == "nt" else 0
        child = subprocess.Popen([sys.executable, "-X", "utf8", "-S", "-m", "relay_core.final_summary"],
                                 stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL, env=env, close_fds=True,
                                 start_new_session=os.name != "nt", creationflags=flags)
        assert child.stdin is not None
        child.stdin.write((json.dumps(payload) + "\n").encode("utf-8"))
        child.stdin.close()
        # Reap it when this worker stays alive (tests or a future close path); the child itself
        # remains independent when the worker exits immediately after shutdown.
        threading.Thread(target=child.wait, name="relay-final-summary-reap", daemon=True).start()
        logs.event(_log, "final_summary_started", session=agent.session_id, pid=child.pid)
        return True
    except Exception as exc:
        logs.event(_log, "final_summary_failed", session=agent.session_id, error=type(exc).__name__)
        return False


def run_job(job: dict, provider=None) -> bool:
    """Generate and conditionally store a recap. True only when a new one was saved."""
    store = SessionStore(job["session_dir"])
    session_id = job["session_id"]
    data = store.load(session_id)
    meta = read_meta(store.directory, session_id)
    if (_fingerprint(data) != job["fingerprint"] or not needs_final_summary(data, meta)):
        return False
    if provider is None:
        provider = _provider_for(ProviderConfig(**job["config"]), job["stall_timeout"])
    inputs = titles.saved_inputs(data)
    summary = titles.generate_summary(provider, inputs["messages"], threading.Event(),
                                      files=inputs["files"], todos=inputs["todos"])
    if not summary:
        return False
    # Another pane may have resumed or a regular summary may have won while the model ran.
    current = store.load(session_id)
    meta = read_meta(store.directory, session_id)
    if _fingerprint(current) != job["fingerprint"] or not needs_final_summary(current, meta):
        return False
    return store.note_summary(session_id, summary, turn=job["turns"])


def main() -> None:
    logs.configure("final_summary")
    try:
        job = json.loads(sys.stdin.readline(65537))
        if run_job(job):
            logs.event(_log, "final_summary_saved", session=job["session_id"])
    except Exception as exc:
        logs.event(_log, "final_summary_failed", error=type(exc).__name__)


if __name__ == "__main__":
    main()
