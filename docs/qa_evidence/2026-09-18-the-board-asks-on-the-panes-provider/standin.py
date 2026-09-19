#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""A stand-in for two providers on 127.0.0.1, so the whole path runs with no key and no network.

Each path is one provider's OpenAI-compatible endpoint and accepts exactly one bearer token — its
own. A request that arrives with the *other* provider's token is answered 401, which is what a real
provider does when a Z.AI key is presented to Moonshot.

    /zai/chat/completions        accepts "zai-key"
    /moonshot/chat/completions   accepts "moonshot-key"

Started by run.sh; prints the port on stdout and serves until killed.
"""
from __future__ import annotations

import json
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

KEYS = {"/zai": "zai-key", "/moonshot": "moonshot-key"}
LOG: list[dict] = []


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        self.rfile.read(int(self.headers.get("Content-Length") or 0))
        prefix = "/" + self.path.lstrip("/").split("/", 1)[0]
        want = KEYS.get(prefix)
        got = (self.headers.get("Authorization") or "")[len("Bearer "):]
        # The key itself is a dummy; only which provider it belongs to is recorded.
        LOG.append({"endpoint": prefix,
                    "presented": next((p for p, k in KEYS.items() if k == got), "unknown"),
                    "accepted": bool(want) and got == want})
        if not want or got != want:
            self.send_response(401)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(b'{"error":{"message":"invalid api key"}}')
            return
        body = json.dumps({"choices": [{"finish_reason": "stop",
                                        "message": {"role": "assistant", "content": "ok"}}]}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main() -> None:
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    print(server.server_port, flush=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        for line in sys.stdin:                      # "dump\n" prints what each endpoint saw
            if line.strip() == "dump":
                print(json.dumps(LOG), flush=True)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
