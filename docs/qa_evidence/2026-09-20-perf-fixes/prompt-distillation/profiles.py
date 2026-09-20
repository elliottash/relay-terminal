#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Assemble the three profiles this proposal compares, size them, and optionally time their prefill.

    python3 profiles.py --repo <checkout or clean export> [--tokenize URL] [--bench URL] [--out DIR]

  today      what `Agent.system_prompt()` and `Agent.tools()` produce in <repo>, pane with a board
  distilled  SYSTEM.distilled.txt + TODO-RULES.distilled.txt + BOARD-POLICY.core.txt, the same
             catalogue/app/own-session text as <repo>, and the same tools except that
             `set_keybinding` loses its 91-line action listing and enum (decision 1)
  short      SYSTEM.short.txt + a names-only skills line, and tools.short.json (written here)

Read-only towards the repository. `--bench` posts each profile to a llama.cpp server's
/v1/chat/completions with `cache_prompt: false`, `max_tokens: 1`, and reads the server's own
`timings` (prompt_n, prompt_ms); then once more with the cache on, to show what a byte-stable
prefix costs on the second turn. It is the owner's local model: no key, no cost. Never point it
at a hosted provider.
"""
from __future__ import annotations

import argparse
import copy
import json
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument("--repo", required=True)
parser.add_argument("--tokenize", default=None)
parser.add_argument("--bench", default=None)
parser.add_argument("--runs", type=int, default=3)
parser.add_argument("--out", default=None)
ARGS = parser.parse_args()

# The inventory script already knows how to build a wired pane agent; reuse it rather than fork it.
dump = Path(ARGS.out or "/tmp/claude-1000/pf4k/fix/distill/profiles")
dump.mkdir(parents=True, exist_ok=True)
subprocess.run([sys.executable, str(HERE / "measure.py"), "--repo", ARGS.repo, "--dump", str(dump / "today")],
               check=True, stdout=subprocess.DEVNULL)


def load(stem: str) -> tuple[str, list[dict]]:
    return ((dump / "today" / f"{stem}.prompt.txt").read_text(encoding="utf-8"),
            json.loads((dump / "today" / f"{stem}.tools.json").read_text(encoding="utf-8")))


SHORT_DESCRIPTIONS = {
    "run_command": ("Run a non-interactive Bash command in the workspace. No tty and no stdin: a command that "
                    "prompts or needs sudo fails. Waits up to timeout_seconds (default 30); a command still running "
                    "then comes back with a job_id to read with command_output. Set background: true for a server."),
    "read_file": "Read a UTF-8 text file inside the workspace.",
    "list_directory": "List a directory inside the workspace.",
    "write_file": "Create a new UTF-8 file, or replace one in full. To change part of an existing file use edit_file.",
    "edit_file": ("Change an existing file by replacing an exact string. old_string must match the file byte for "
                  "byte, including indentation, and appear exactly once unless replace_all is true: include enough "
                  "surrounding lines to make it unique."),
    "command_output": "Read what a run_command job has printed since you last read it; wait_seconds waits for it to finish.",
    "stop_command": "Stop a run_command job.",
    "load_skill": "Load the full SKILL.md of one of the user's skills by name.",
}
SHORT_PARAM_DESCRIPTIONS_DROPPED = {"cwd", "host"}   # kept as parameters, said once in the description


def short_tools(specs: list[dict]) -> list[dict]:
    out = []
    for spec in specs:
        name = spec["function"]["name"]
        if name not in SHORT_DESCRIPTIONS:
            continue
        spec = copy.deepcopy(spec)
        spec["function"]["description"] = SHORT_DESCRIPTIONS[name]
        for key, value in spec["function"]["parameters"].get("properties", {}).items():
            if isinstance(value, dict) and (key in SHORT_PARAM_DESCRIPTIONS_DROPPED or name != "run_command"):
                value.pop("description", None)
        out.append(spec)
    return out


def slim_keybinding(specs: list[dict]) -> list[dict]:
    out = copy.deepcopy(specs)
    for spec in out:
        if spec["function"]["name"] == "set_keybinding":
            spec["function"]["description"] = (
                "Change the keyboard shortcut for one Relay action. Keys use Qt portable format, for example "
                "'Ctrl+Shift+P', 'Alt+Left', 'F5'. An empty keys list unbinds the action. Find the action id with "
                "app_action_list; an unknown id is refused with the closest ids. Writes only Relay's "
                "keybindings.json, which Relay reloads automatically.")
            spec["function"]["parameters"]["properties"]["action"] = {"type": "string"}
    return out


def profile_today() -> tuple[str, list[dict]]:
    return load("pane--board--build")


def profile_distilled() -> tuple[str, list[dict]]:
    sys.path.insert(0, str(Path(ARGS.repo) / "backend"))
    sys.dont_write_bytecode = True
    from relay_core import agent as agent_module, board_tools, todos
    prompt, specs = load("pane--board--build")
    system = (HERE / "SYSTEM.distilled.txt").read_text(encoding="utf-8").rstrip("\n")
    rules = "\n\n" + (HERE / "TODO-RULES.distilled.txt").read_text(encoding="utf-8").rstrip("\n")
    core = (HERE / "BOARD-POLICY.core.txt").read_text(encoding="utf-8").strip()
    assert prompt.startswith(agent_module.SYSTEM) and todos.RULES in prompt and board_tools.policy_text() in prompt
    prompt = system + prompt[len(agent_module.SYSTEM):]
    prompt = prompt.replace(todos.RULES, rules).replace(board_tools.policy_text(), core)
    return prompt, slim_keybinding(specs)


def profile_short() -> tuple[str, list[dict]]:
    prompt, specs = load("pane--no-board--build")
    workspace = next(line for line in prompt.split("\n") if line.startswith("Chosen workspace: "))
    names = [line[2:].split(":", 1)[0] for line in prompt.split("\n")
             if line.startswith("- ") and ":" in line and not line.startswith("- also loadable")]
    skills_line = ("\nSkills the user has, by name; call load_skill before following one: " + ", ".join(names) + "."
                   if names else "")
    system = (HERE / "SYSTEM.short.txt").read_text(encoding="utf-8").rstrip("\n")
    tools = short_tools(specs)
    (HERE / "tools.short.json").write_text(json.dumps(tools, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    return system + "\n" + workspace + skills_line, tools


def count(text: str) -> int | None:
    if not ARGS.tokenize:
        return None
    body = json.dumps({"content": text}).encode()
    request = urllib.request.Request(ARGS.tokenize.rstrip("/") + "/tokenize", body, {"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=60) as response:
        return len(json.load(response)["tokens"])


def bench(prompt: str, tools: list[dict], cache: bool, nonce: str) -> dict:
    # The nonce is in the *user* message, after the prefix, so a cached run can reuse system + tools
    # and an uncached run reuses nothing (cache_prompt false).
    body = {"model": "local", "stream": False, "max_tokens": 1, "cache_prompt": cache,
            "messages": [{"role": "system", "content": prompt},
                         {"role": "user", "content": f"[{nonce}] Reply with the single word OK."}],
            "tools": tools}
    request = urllib.request.Request(ARGS.bench.rstrip("/") + "/v1/chat/completions", json.dumps(body).encode(),
                                     {"Content-Type": "application/json"})
    started = time.perf_counter()
    with urllib.request.urlopen(request, timeout=600) as response:
        answer = json.load(response)
    wall = time.perf_counter() - started
    timings = answer.get("timings") or {}
    return {"wall_s": round(wall, 3), "prompt_n": timings.get("prompt_n"), "cache_n": timings.get("cache_n"),
            "prompt_ms": timings.get("prompt_ms"), "prompt_tokens": (answer.get("usage") or {}).get("prompt_tokens")}


def main() -> int:
    profiles = {"today": profile_today(), "distilled": profile_distilled(), "short": profile_short()}
    report = {}
    for name, (prompt, tools) in profiles.items():
        (dump / f"{name}.prompt.txt").write_text(prompt, encoding="utf-8")
        (dump / f"{name}.tools.json").write_text(json.dumps(tools, indent=1, ensure_ascii=False), encoding="utf-8")
        wire = json.dumps(tools, ensure_ascii=False)
        report[name] = {"prompt_bytes": len(prompt.encode()), "tools_bytes": len(wire.encode()), "tools": len(tools),
                        "prompt_tokens": count(prompt), "tools_tokens": count(wire)}
        print(name, report[name])
    if ARGS.bench:
        bench("You are a test.", [], False, "warm")          # wake a sleeping model before timing anything
        for name, (prompt, tools) in profiles.items():
            cold = [bench(prompt, tools, False, f"cold-{name}-{i}") for i in range(ARGS.runs)]
            bench(prompt, tools, True, f"prime-{name}")       # fill the slot's cache with this prefix
            warm = [bench(prompt, tools, True, f"warm-{name}-{i}") for i in range(ARGS.runs)]
            report[name]["cold"], report[name]["warm"] = cold, warm
            print(name, "cold", cold)
            print(name, "warm", warm)
    (dump / "report.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
