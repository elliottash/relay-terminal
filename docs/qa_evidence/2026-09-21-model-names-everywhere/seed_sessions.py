#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Two saved conversations in the sandbox, recorded under two *spellings of one model* and one
# vendor-prefixed id — which is what card #MDL1's rule 1 is about in the Sessions pane: history
# keeps the id the API took, the pane prints the name, and the filter menu has one entry per model.
#
#   k3                   the Kimi Coding Plan's id for Kimi K3 → "kimi-k3" (only its catalog row
#                        knows this, so the worker is the one that can name it)
#   kimi-k3              the Kimi platform's id for the same model → "kimi-k3"
#   openai/gpt-5.6-sol   OpenRouter's slug → "gpt-5.6-sol"
#
# Run with HOME and the XDG_* variables already pointing at the sandbox.
import json
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, "/home/elliott/repos/relay-terminal/backend")
from relay_core import conv_index  # noqa: E402

workspace = sys.argv[1]
root = Path(os.environ["XDG_DATA_HOME"]) / "relay" / "sessions" / conv_index.workspace_digest(workspace)
root.mkdir(parents=True, exist_ok=True)
now = time.time()

ROWS = [("a" * 32, "Kimi on the coding plan", "kimi-code", "k3", 300),
        ("b" * 32, "Kimi on the platform", "kimi", "kimi-k3", 200),
        ("c" * 32, "Sol through OpenRouter", "openrouter", "openai/gpt-5.6-sol", 100)]

for session_id, title, preset, model, ago in ROWS:
    data = {"version": 1, "kind": "relay_session", "id": session_id, "title": title,
            "created": now - ago - 60, "updated": now - ago, "workspace": workspace,
            "model": model, "preset": preset, "effort": "high", "mode": "build", "turns": 1,
            "epoch": 0, "models": [model],
            "messages": [{"role": "user", "content": "what does this repository do"},
                         {"role": "assistant", "content": "It is a terminal whose panes have agents."}],
            "snapshots": {}, "requests": {"items": []}, "todos": {"items": []}, "plan_path": None,
            "open_requests": 0,
            "checkpoints": {"items": [{"turn": 1, "prompt": "what does this repository do",
                                       "prompt_preview": "what does this", "time": now - ago - 30,
                                       "ended": now - ago, "locations": {"0": 1}, "files": {}}]}}
    (root / f"{session_id}.json").write_text(json.dumps(data), encoding="utf-8")

print(f"seeded {len(ROWS)} conversations in {root}")
