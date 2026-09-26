# SPDX-License-Identifier: AGPL-3.0-or-later
"""A fake stdio MCP server for tests/test_mcp.py (card #SSRQ).

Newline-delimited JSON-RPC on stdin/stdout. Tools:

  echo    returns its `text` (marked read-only)
  add     returns a + b, and the value of $FAKE_MCP_SECRET's *length*, never the value
  slow    sleeps `seconds`, then answers
  fail    answers with isError
  crash   exits the process without answering

`--exit-at-start` exits before initialize; `--garbage` answers initialize with non-JSON.
"""
import json
import os
import sys
import time

TOOLS = [
    {"name": "echo", "description": "Echo text back.", "annotations": {"readOnlyHint": True},
     "inputSchema": {"type": "object", "properties": {"text": {"type": "string"}}, "required": ["text"]}},
    {"name": "add", "description": "Add two numbers.",
     "inputSchema": {"type": "object", "properties": {"a": {"type": "number"}, "b": {"type": "number"}}}},
    {"name": "slow", "description": "Sleep.",
     "inputSchema": {"type": "object", "properties": {"seconds": {"type": "number"}}}},
    {"name": "fail", "description": "Always fails.", "inputSchema": {"type": "object"}},
    {"name": "crash", "description": "Exit.", "inputSchema": {"type": "object"}},
]


def send(message):
    sys.stdout.write(json.dumps(message) + "\n")
    sys.stdout.flush()


def main():
    if "--exit-at-start" in sys.argv:
        print("fake server refuses to start", file=sys.stderr)
        sys.exit(3)
    for line in sys.stdin:
        message = json.loads(line)
        method, mid = message.get("method"), message.get("id")
        if mid is None:
            continue
        if method == "initialize":
            if "--garbage" in sys.argv:
                sys.stdout.write("this is not json\n")
                sys.stdout.flush()
                continue
            send({"jsonrpc": "2.0", "id": mid, "result": {
                "protocolVersion": message["params"]["protocolVersion"], "capabilities": {"tools": {}},
                "serverInfo": {"name": "fake", "version": "1"}}})
        elif method == "tools/list":
            cursor = (message.get("params") or {}).get("cursor")
            page = TOOLS[:2] if not cursor else TOOLS[2:]
            result = {"tools": page}
            if not cursor:
                result["nextCursor"] = "p2"
            send({"jsonrpc": "2.0", "id": mid, "result": result})
        elif method == "tools/call":
            name, args = message["params"]["name"], message["params"].get("arguments") or {}
            if name == "echo":
                content = [{"type": "text", "text": args["text"]}]
                send({"jsonrpc": "2.0", "id": mid, "result": {"content": content}})
            elif name == "add":
                text = f"{args['a'] + args['b']} secret-len={len(os.environ.get('FAKE_MCP_SECRET', ''))}"
                send({"jsonrpc": "2.0", "id": mid, "result": {"content": [{"type": "text", "text": text}]}})
            elif name == "slow":
                time.sleep(float(args.get("seconds", 5)))
                send({"jsonrpc": "2.0", "id": mid, "result": {"content": [{"type": "text", "text": "late"}]}})
            elif name == "fail":
                send({"jsonrpc": "2.0", "id": mid, "result": {
                    "isError": True, "content": [{"type": "text", "text": "the thing broke"}]}})
            elif name == "crash":
                print("fake server crashing", file=sys.stderr)
                sys.stderr.flush()
                os._exit(7)
        else:
            send({"jsonrpc": "2.0", "id": mid, "error": {"code": -32601, "message": "no such method"}})


if __name__ == "__main__":
    main()
