"""Two turns on the Local tier, through the real provider path, printing what the usage event says.

  RELAY_KEYRING=off PYTHONPATH=backend python3 live.py
"""
import json, threading, sys
from relay_core.provider import ChatProvider, ProviderConfig
from relay_core import sessions

BASE = "http://127.0.0.1:8080/v1"
PREFIX = ("You are a terse assistant. " + "Relay is a terminal with an agent in every pane. " * 60)

config = ProviderConfig(BASE, "bonsai-2-27b", "", local=True, max_tokens=256)
provider = ChatProvider(config)
totals = sessions.empty_usage()
for turn, ask in enumerate(("Reply with the single word ok.", "Reply with the single word yes."), 1):
    events = []
    provider.complete([{"role": "system", "content": PREFIX}, {"role": "user", "content": ask}],
                      [], events.append, threading.Event())
    usage = [e["usage"] for e in events if e["event"] == "usage"][-1]
    sessions.add_usage(totals, usage)
    print(f"turn {turn}: prompt_tokens={usage['prompt_tokens']} "
          f"cached_tokens={usage.get('cached_tokens', 'not reported')} "
          f"(raw prompt_tokens_details={usage.get('prompt_tokens_details')})")
print("session totals:", json.dumps(totals))
