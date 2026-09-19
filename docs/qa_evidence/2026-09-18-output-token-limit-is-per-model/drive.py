#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Card #Z79Y evidence: what each preset actually asks for, and what a mock endpoint receives.

Part 1 is the table: ProviderConfig resolved per preset, automatic and pinned.
Part 2 sends one real request per preset through the real ChatProvider to a loopback mock and
prints the `max_tokens` in the body that arrived. No key, no network.

Usage: PYTHONPATH=backend python3 drive.py
"""
import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from relay_core.presets import DEFAULT_MAX_OUTPUT, PRESETS
from relay_core.provider import MAX_OUTPUT_TOKENS, ChatProvider, ProviderConfig

SEEN = []


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        SEEN.append(body["max_tokens"])
        data = (b'data: ' + json.dumps({"choices": [{"delta": {"content": "ok"},
                                                     "finish_reason": "stop"}]}).encode()
                + b"\n\ndata: [DONE]\n\n")
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


print("== what each preset asks for ==")
print(f"{'preset':<12} {'window':>10} {'automatic':>10} {'pinned 131072':>14} {'pinned 8192':>12}")
for preset in PRESETS.values():
    auto = ProviderConfig(preset.base_url, preset.model, "k").max_tokens
    high = ProviderConfig(preset.base_url, preset.model, "k", max_tokens=MAX_OUTPUT_TOKENS).max_tokens
    low = ProviderConfig(preset.base_url, preset.model, "k", max_tokens=8192).max_tokens
    print(f"{preset.id:<12} {preset.context_window:>10,} {auto:>10,} {high:>14,} {low:>12,}")

print(f"\n{'custom endpoint':<12} {'':>10} "
      f"{ProviderConfig('https://my.host/v1', 'm', 'k').max_tokens:>10,} "
      f"{ProviderConfig('https://my.host/v1', 'm', 'k', max_tokens=MAX_OUTPUT_TOKENS).max_tokens:>14,}"
      "   (a number the user typed for their own server is theirs)")
local = ProviderConfig("http://127.0.0.1:8080/v1", "bonsai-2-27b", "", local=True, context_window=131_072)
print(f"{'local 131072':<12} {131_072:>10,} {local.max_tokens:>10,}"
      "                     (a quarter of the served window)")
print(f"\nfallback for anything Relay cannot name: {DEFAULT_MAX_OUTPUT:,}")
print("10% of the window would be: " + ", ".join(
    f"{p.id} {p.context_window // 10:,}" for p in list(PRESETS.values())[:3]) + " …")

print("\n== what the endpoint receives ==")
server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
port = server.server_address[1]
for preset_id in ("glm-coding", "gemini", "openai", "openrouter"):
    preset = PRESETS[preset_id]
    resolved = ProviderConfig(preset.base_url, preset.model, "k").max_tokens
    # Same resolved number, sent to a loopback endpoint so the body can be read back.
    mock = ProviderConfig(f"http://127.0.0.1:{port}/v1", "mock", "", max_tokens=resolved)
    ChatProvider(mock).complete([{"role": "user", "content": "hi"}], [], lambda e: None,
                                threading.Event())
    print(f"  {preset_id:<12} request body max_tokens = {SEEN[-1]:,}")
server.shutdown()
