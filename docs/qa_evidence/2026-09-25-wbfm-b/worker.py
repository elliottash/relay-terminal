#!/usr/bin/env python3
# Fixture for card #WBFM-B / #WK7C evidence: enough of the worker protocol for the models
# pane's Job rules tab — presets for the Sources tab, and a protocol-13 `model_roles` answer
# for every roles request, so the tab's live column carries the served pane's worker's own
# answer (what the before shot showed as "runs on", what the after shot dims as
# "live (this pane)"). A `set_agent_options` is answered the same way: the re-resolve the
# tab waits for after any roles change.
import json
import sys

PRESETS = [
    {"id": "guest:claude", "provider": "claude", "label": "Claude Code", "logged_in": True},
    {"id": "guest:codex", "provider": "codex", "label": "Codex", "logged_in": False},
    {"id": "kimi", "provider": "Moonshot", "label": "Kimi", "key_url": "https://platform.moonshot.cn",
     "has_stored_key": False},
]

SUMMARIES = {
    "main": "kimi-k3 · high",
    "subagent": "kimi-k3 · high",
    "switchboard": "glm-5.3 · high",
    "planning": "gpt-6-astra · xhigh",
    "suggestions": "glm-5.3-flash · low",
    "summaries": "glm-5.3-flash · low",
    "chore": "glm-5.3-flash · low",
    "images": "gemini-3-pro-image",
    "files": "kimi-k3 · high",
}
TIERS = {
    "main": "kimi-k3 · high",
    "high": "gpt-6-astra · xhigh",
    "flash": "glm-5.3-flash · low",
    "lite": "glm-5.3-flash · low",
    "local": "qwen3-coder-30b · local",
}


def emit(event, **kw):
    print(json.dumps(dict(event=event, **kw)), flush=True)


emit("ready")
for line in sys.stdin:
    try:
        r = json.loads(line)
    except ValueError:
        continue
    t = r.get("type")
    if t == "presets":
        emit("presets", presets=PRESETS)
    elif t == "configure":
        emit("configured", model="kimi-k3", context=r.get("context", {}), agent_role="switchboard")
        emit("model_roles", roles=SUMMARIES, tiers=TIERS)
    elif t in ("roles", "set_agent_options"):
        emit("model_roles", roles=SUMMARIES, tiers=TIERS)
    elif t == "shutdown":
        break
