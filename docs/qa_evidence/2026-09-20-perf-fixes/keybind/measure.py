#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""What `set_keybinding` costs per request, before and after #GMCF decision 1.

    python3 docs/qa_evidence/2026-09-20-perf-fixes/keybind/measure.py [--configure captured.json]

Read-only, and it calls no model. With `--configure` it measures the **real** blocks a window
sends — capture one by putting a tee in front of `backend/worker.py` (README.md has the recipe);
without it, the keybinding registry is read from `src/Keymap.h`, which is what the window sends.

"Before" is not guessed: the pre-#GMCF `keybindings.py` is read out of git (`--before <rev>`),
imported under its own name, and asked for the same catalog's schema.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "backend"))

from relay_core import activity_tools, app_tools, skills as skills_mod  # noqa: E402
from relay_core.agent import Agent  # noqa: E402
from relay_core.context import estimate_tokens  # noqa: E402
from relay_core.keybindings import KeybindingCatalog  # noqa: E402
from relay_core.provider import ProviderConfig  # noqa: E402

CONFIG = ProviderConfig("http://127.0.0.1:12345/v1", "mock", "")
APP = {"tab": "t1", "writes_enabled": True, "options": [], "actions": []}


class NeverCalled:
    def complete(self, *args, **kwargs):     # pragma: no cover - nothing here runs a turn
        raise AssertionError("measure.py never calls a model")

    def cancel(self) -> None:
        pass


def registry_from_keymap() -> list[dict]:
    """`src/Keymap.h`'s `add(id, category, description, {keys})` table — what `configure` carries."""
    source = (ROOT / "src" / "Keymap.h").read_text(encoding="utf-8")
    return [{"id": m.group(1), "description": m.group(2),
             "keys": re.findall(r'QStringLiteral\("([^"]+)"\)', m.group(3))}
            for m in re.finditer(
                r'^\s*add\("([^"]+)",\s*"[^"]*",\s*"((?:[^"\\]|\\.)*)",\s*\{(.*?)\}\);', source, re.M)]


def old_module(rev: str, temp: Path):
    """`backend/relay_core/keybindings.py` as it was before the change, importable."""
    text = subprocess.run(["git", "-C", str(ROOT), "show", f"{rev}:backend/relay_core/keybindings.py"],
                          check=True, capture_output=True, text=True).stdout
    path = temp / "keybindings_before.py"
    path.write_text(text, encoding="utf-8")
    spec = importlib.util.spec_from_file_location("keybindings_before", path)
    module = importlib.util.module_from_spec(spec)
    sys.modules["keybindings_before"] = module       # @dataclass looks its own module up by name
    spec.loader.exec_module(module)
    return module


def size(text: str) -> tuple[int, int]:
    return len(text.encode("utf-8")), estimate_tokens(text)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configure", help="a captured configure message (README.md)")
    parser.add_argument("--before", default="79fd67d6", help="the revision to measure 'before' from")
    args = parser.parse_args()

    request = json.loads(Path(args.configure).read_text(encoding="utf-8")) if args.configure else {}
    actions = (request.get("keybindings") or {}).get("actions") or registry_from_keymap()
    app_block = request.get("app") or APP

    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        (root / "ws").mkdir()
        path = root / "conf" / "keybindings.json"
        catalog = KeybindingCatalog(str(path), json.loads(json.dumps(actions)))
        before = old_module(args.before, root).KeybindingCatalog(
            str(root / "before" / "keybindings.json"), json.loads(json.dumps(actions)))

        old_spec = json.dumps(before.tool_spec(), ensure_ascii=False)
        new_spec = json.dumps(catalog.tool_spec(), ensure_ascii=False)
        print(f"actions in the catalog: {len(catalog.actions)}  "
              f"(bound: {sum(1 for a in catalog.actions.values() if a.keys)})")
        print(f"set_keybinding schema  before: {size(old_spec)[0]:,} B / ~{size(old_spec)[1]:,} tok"
              f"   after: {size(new_spec)[0]:,} B / ~{size(new_spec)[1]:,} tok")

        agent = Agent(CONFIG, str(root / "ws"), lambda event: None, provider=NeverCalled(),
                      keybindings=catalog)
        agent.executor.skills = skills_mod.SkillIndex.load([root / "skills"], defaults=False)
        agent.app = app_tools.AppTools(app_tools.AppCatalog.from_request(app_block),
                                       app_tools.AppBridge(lambda event: None),
                                       keybindings=lambda: agent.executor.keybindings)
        activity_tools.ActivityTools.attach(agent, live_info=lambda: {})
        agent.refresh_system_prompt()
        tools = agent.tools()
        now = json.dumps(tools, ensure_ascii=False)
        was = now.replace(new_spec, old_spec)
        print(f"whole tool list ({len(tools)} tools)  before: {size(was)[0]:,} B / ~{size(was)[1]:,} tok"
              f"   after: {size(now)[0]:,} B / ~{size(now)[1]:,} tok")

        # The replacement for the listing: the model finds an id here, with its current keys.
        listing = agent.app.run("app_action_list", {"search": "focus pane"})
        print(f"app_action_list 'focus pane' → "
              f"{[(a['key'], a.get('keys')) for a in listing['actions']]}")
        whole = agent.app.run("app_action_list", {})
        print(f"app_action_list {{}} → {whole['count']} of {whole['total']} rows, "
              f"{sum(1 for a in whole['actions'] if 'keys' in a)} with keys")
        for guess in ("pane.split_right", "focusleft", "close the pane"):
            try:
                catalog.prepare({"action": guess, "keys": ["Ctrl+Q"]})
            except Exception as refusal:                      # noqa: BLE001 - the message is the point
                print(f"refusal for {guess!r}: {refusal}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
