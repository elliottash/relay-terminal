#!/usr/bin/env python3
"""What the three on-demand groups cost when they are sent, and what the one line costs instead."""
import json, os, re, sys, tempfile, urllib.request
from pathlib import Path
REPO = sys.argv[1]; TOK = sys.argv[2] if len(sys.argv) > 2 else None
SCRATCH = tempfile.mkdtemp(prefix="relay-groups-")
os.environ["RELAY_KEYRING"] = "off"
for var, sub in (("XDG_DATA_HOME","d"),("XDG_CONFIG_HOME","c"),("XDG_STATE_HOME","s"),("XDG_CACHE_HOME","k")):
    os.environ[var] = os.path.join(SCRATCH, sub); os.makedirs(os.environ[var], exist_ok=True)
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(REPO, "backend"))
from relay_core import activity_tools, app_tools, board as B, board_tools as T, skills, tool_groups
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
agent.board = T.BoardTools(B.Board(ws / "issues", ws), emit=lambda e: None, autonomy="auto",
                           state_path=Path(SCRATCH) / "r.json", pane_token="3f2504e0-4f89-11d3-9a0c-0305e82c3301",
                           context=T.ToolContext(actor="agent", model="m", pane="2"))
agent.board.begin_turn("t-1")
def tokens(text):
    if not TOK: return None
    req = urllib.request.Request(TOK.rstrip("/") + "/tokenize", json.dumps({"content": text}).encode(),
                                 {"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=60) as r: return len(json.load(r)["tokens"])
def sizes(label):
    agent.refresh_system_prompt()
    prompt, wire = agent.system_prompt(), json.dumps(agent.tools(), ensure_ascii=False)
    return {"label": label, "tools": len(agent.tools()), "prompt_bytes": len(prompt.encode()),
            "tools_bytes": len(wire.encode()), "prompt_tokens": tokens(prompt), "tools_tokens": tokens(wire)}
report = [sizes("deferred (nothing loaded)")]
for group in ("app", "own_session", "tests"):
    agent._execute(agent._prepare("load_tools", {"group": group}), {})
    report.append(sizes(f"after load_tools({group})"))
agent.__class__._deferred_groups = lambda self: ()
report.append(sizes("everything sent, as before decision 9"))
for row in report:
    row["total_tokens"] = (row["prompt_tokens"] + row["tools_tokens"]) if row["prompt_tokens"] else None
print(json.dumps(report, indent=1))
