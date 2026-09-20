#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The short profile with and without a Switchboard: bytes, the Local tier's tokens, prefill.

`assembly/profilesize.py` with one thing added — it measured a pane that always had a board, and
under the old short profile the board was dropped, so its "short" column was the no-board number
under another name. Since the owner's decision of 2026-09-20 the short profile carries five board
tools and the tiered policy, so the two cases are genuinely different and both are reported.

    python3 docs/qa_evidence/2026-09-20-perf-fixes/tiers/profilesize-tiers.py . http://127.0.0.1:8080
"""
import json, os, re, sys, tempfile, time, urllib.request
from pathlib import Path
REPO = sys.argv[1]; BENCH = sys.argv[2] if len(sys.argv) > 2 else None
SCRATCH = tempfile.mkdtemp(prefix="relay-profilesize-tiers-")
os.environ["RELAY_KEYRING"] = "off"
for var, sub in (("XDG_DATA_HOME", "d"), ("XDG_CONFIG_HOME", "c"), ("XDG_STATE_HOME", "s"), ("XDG_CACHE_HOME", "k")):
    os.environ[var] = os.path.join(SCRATCH, sub); os.makedirs(os.environ[var], exist_ok=True)
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(REPO, "backend"))
from relay_core import activity_tools, app_tools, board as B, board_tools as T, skills
from relay_core.agent import Agent
from relay_core.keybindings import KeybindingCatalog
from relay_core.provider import ProviderConfig
CONFIG = ProviderConfig("http://127.0.0.1:9/v1", "stub", "stub", {}, 131072)
APP = {"tab": "t1", "writes_enabled": True,
       "options": [{"id": "appearance.theme", "section": "appearance", "section_label": "Appearance",
                    "label": "Theme", "kind": "choice", "value": "dark", "settable": True,
                    "choices": [{"value": "dark", "label": "Dark"}, {"value": "light", "label": "Light"}]}],
       "actions": [{"key": "settings.open", "section": "Relay", "label": "Open settings", "agent_safe": True}]}


def keybindings(repo):
    source = (Path(repo) / "src" / "Keymap.h").read_text(encoding="utf-8")
    actions = [{"id": m.group(1), "description": m.group(2),
                "keys": re.findall(r'QStringLiteral\("([^"]+)"\)', m.group(3))}
               for m in re.finditer(r'^\s*add\("([^"]+)",\s*"[^"]*",\s*"((?:[^"\\]|\\.)*)",\s*\{(.*?)\}\);', source, re.M)]
    return KeybindingCatalog(str(Path(SCRATCH) / "keybindings.json"), actions)


ws = Path(SCRATCH) / "ws"; (ws / "issues").mkdir(parents=True)
(ws / "issues" / B.BOARD_CONFIG).write_text((Path(REPO) / "issues" / B.BOARD_CONFIG).read_text())
agent = Agent(CONFIG, str(ws), lambda e: None, provider=object(), keybindings=keybindings(REPO))
agent.executor.skills = skills.from_request(None, str(ws))
agent.app = app_tools.AppTools(app_tools.AppCatalog.from_request(APP), app_tools.AppBridge(lambda e: None))
agent.activity = activity_tools.ActivityTools(agent)
BOARD = T.BoardTools(B.Board(ws / "issues", ws), emit=lambda e: None, autonomy="auto",
                     state_path=Path(SCRATCH) / "r.json", pane_token="3f2504e0-4f89-11d3-9a0c-0305e82c3301",
                     context=T.ToolContext(actor="agent", model="m", pane="2"))
BOARD.begin_turn("t-1")


def post(path, body, timeout=600):
    req = urllib.request.Request(BENCH.rstrip("/") + path, json.dumps(body).encode(), {"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.load(r)


def tokens(text):
    return len(post("/tokenize", {"content": text})["tokens"]) if BENCH else None


def bench(prompt, tools, cache, nonce):
    body = {"model": "local", "stream": False, "max_tokens": 1, "cache_prompt": cache,
            "messages": [{"role": "system", "content": prompt},
                         {"role": "user", "content": f"[{nonce}] Reply with the single word OK."}], "tools": tools}
    started = time.perf_counter(); answer = post("/v1/chat/completions", body)
    t = answer.get("timings") or {}
    return {"wall_s": round(time.perf_counter() - started, 2), "prompt_n": t.get("prompt_n"),
            "prompt_ms": round(t.get("prompt_ms", 0), 1)}


report = {}
for label, profile, board in (("full-board", "full", True), ("short-board", "short", True),
                              ("short-noboard", "short", False)):
    agent.board = BOARD if board else None
    agent.prompt_profile = profile
    agent.refresh_system_prompt()
    prompt, tools = agent.system_prompt(), agent.tools()
    wire = json.dumps(tools, ensure_ascii=False)
    row = {"prompt_bytes": len(prompt.encode()), "tools_bytes": len(wire.encode()), "tools": len(tools),
           "tool_names": [t["function"]["name"] for t in tools],
           "prompt_tokens": tokens(prompt), "tools_tokens": tokens(wire)}
    if row["prompt_tokens"]:
        row["total_tokens"] = row["prompt_tokens"] + row["tools_tokens"]
    if BENCH:
        bench("You are a test.", [], False, "wake")
        row["cold"] = [bench(prompt, tools, False, f"cold-{label}-{i}") for i in range(3)]
        bench(prompt, tools, True, f"prime-{label}")
        row["warm"] = [bench(prompt, tools, True, f"warm-{label}-{i}") for i in range(3)]
    report[label] = row
print(json.dumps(report, indent=1))
