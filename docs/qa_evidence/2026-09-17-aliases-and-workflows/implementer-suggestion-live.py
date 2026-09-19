#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""One live check of the agent's alias suggestion (issue G8DK, protocol 20.6).

`./scripts/test.sh` stays offline, so this is the one place the suggestion meets a real model. It
reads the stored `glm-coding` key through relay_core.keystore (RELAY_KEYRING left alone) and never
prints it, writes it anywhere, or lets it into the output. Nothing is written to any Switchboard:
the point of the run is that a suggestion is a suggestion.

    docs/qa_evidence/2026-09-17-aliases-and-workflows/implementer-suggestion-live.py
"""
import json
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "backend"))

from relay_core import aliases, keystore, presets, suggestions  # noqa: E402
from relay_core.provider import ChatProvider, ProviderConfig  # noqa: E402

PRESET = os.environ.get("RELAY_PRESET", "glm-coding")

history = (["pytest tests/test_agent.py -k slow -x"] * 4
           + ["docker compose -f deploy/compose.yml up -d --build web"] * 3
           + ["ls"] * 11 + ["git status"] * 2)

repeated = aliases.repeats(history, suggestions.ALIAS_MIN_COUNT)
print("repeats found offline (no model call yet):")
for entry in repeated:
    print(f"  {entry['count']}x  {entry['command']}")
if not repeated:
    sys.exit("no repeats; nothing to ask the model")

key = keystore.lookup(PRESET)
if not key:
    sys.exit(f"no stored key for {PRESET}; skipping the live call")
preset = presets.PRESETS[PRESET]
# A generous output budget: a reasoning model given 64 tokens spends them before answering.
config = ProviderConfig(preset.base_url, preset.model, key, {}, 1024)
provider = ChatProvider(config, 120)

event = suggestions.propose_alias(provider, repeated)
alias = event.get("alias")
print("\nsuggestion event (no key, no prompt text in it):")
print(json.dumps({k: v for k, v in event.items() if k != "alias"}, indent=2))
if not alias:
    sys.exit("the model proposed nothing usable; reason above")
print("\nproposed alias:")
print(f"  name        {alias['name']}")
print(f"  title       {alias['title']}")
print(f"  kind        {alias['kind']}")
print(f"  text        {alias['text']}")
print(f"  params      {[(p['name'], p['default']) for p in alias['params']]}")
print(f"  required    {alias['required']}")
print(f"  source      {alias['source']}")

# It is a real alias: it renders to a card and fills safely. Still nothing is written.
draft = aliases.Alias(name=alias["name"], kind=alias["kind"], title=alias["title"],
                      text=alias["text"],
                      params=[aliases.Param(p["name"], p["default"], p["description"])
                              for p in alias["params"]])
values = {name: "v; rm -rf / #" for name in draft.placeholders()}
print("\nwhat it would run with a hostile value in every parameter:")
print("  " + aliases.fill(draft, values))
print("\nnothing was written: " + str(aliases.load(None, scopes=("local",))[0]))
