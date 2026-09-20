#!/usr/bin/env python3
"""Per-turn overhead of backend/worker.py against an in-process stub provider.

  turnbench.py <src-root> <turns> [--tools] [--profile] [--tracemalloc] [--deltas N]

Measures, per turn:
  ask_to_request   driver writes {"type":"ask"}  ->  stub sees the first request byte
  last_to_done     stub finishes writing the response -> driver sees {"event":"done"}
  wall             ask -> done
and the worker's own CPU (utime+stime from /proc/<pid>/stat) across the run.
"""
import json, os, subprocess, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = sys.argv[1]
TURNS = int(sys.argv[2])
ARGS = sys.argv[3:]
WITH_TOOLS = "--tools" in ARGS
DELTAS = 3
for i, a in enumerate(ARGS):
    if a == "--deltas":
        DELTAS = int(ARGS[i + 1])

marks = {}          # turn index -> dict of timestamps, filled by the stub thread
counter = {"n": 0}
lock = threading.Lock()

PROSE = ("This is a stub answer. " * 20).strip()


def pieces(text, n):
    if n <= 1:
        return [text]
    step = max(1, len(text) // n)
    out = [text[i:i + step] for i in range(0, len(text), step)]
    return out[:n] if len(out) >= n else out + [""] * (n - len(out))


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def do_POST(self):
        t_req = time.perf_counter()
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) or b"{}"
        request = json.loads(body)
        messages = request.get("messages") or []
        with lock:
            n = counter["n"]
            m = marks.setdefault(n, {})
            m.setdefault("t_first_request", t_req)
            m["t_request"] = t_req
            m["req_bytes"] = m.get("req_bytes", 0) + len(body)
            m["msg_count"] = len(messages)
            m["calls"] = m.get("calls", 0) + 1
            step = m["calls"] - 1
        want_tools = WITH_TOOLS and step == 0
        model = request.get("model") or "stub"
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()

        def chunk(delta, finish_reason=None):
            data = {"id": "stub", "object": "chat.completion.chunk", "created": 0, "model": model,
                    "choices": [{"index": 0, "delta": delta, "finish_reason": finish_reason}]}
            raw = b"data: " + json.dumps(data).encode() + b"\n\n"
            self.wfile.write(hex(len(raw))[2:].encode() + b"\r\n" + raw + b"\r\n")
            self.wfile.flush()

        for piece in pieces(PROSE, DELTAS):
            chunk({"role": "assistant", "content": piece})
        if want_tools:
            chunk({"role": "assistant", "content": None, "tool_calls": [
                {"index": 0, "id": "call_1", "type": "function",
                 "function": {"name": "run_command", "arguments": json.dumps({"command": "echo hi"})}}]})
        chunk({}, "tool_calls" if want_tools else "stop")
        tail = b"data: [DONE]\n\n"
        self.wfile.write(hex(len(tail))[2:].encode() + b"\r\n" + tail + b"\r\n")
        self.wfile.write(b"0\r\n\r\n")
        self.wfile.flush()
        with lock:
            marks.setdefault(counter["n"], {})["t_last_byte"] = time.perf_counter()

    def do_GET(self):
        raw = json.dumps({"data": [{"id": "stub"}]}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


def cpu_of(pid):
    try:
        with open(f"/proc/{pid}/stat") as f:
            parts = f.read().rsplit(") ", 1)[1].split()
        ticks = os.sysconf("SC_CLK_TCK")
        return (int(parts[11]) + int(parts[12])) / ticks
    except Exception:
        return 0.0


def rss_of(pid):
    try:
        with open(f"/proc/{pid}/smaps_rollup") as f:
            for line in f:
                if line.startswith("Pss:"):
                    return int(line.split()[1])
    except Exception:
        pass
    return 0


def main():
    srv = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    port = srv.server_address[1]
    threading.Thread(target=srv.serve_forever, daemon=True).start()

    scratch = os.path.dirname(os.path.abspath(__file__))
    ws = os.path.join(scratch, "ws")
    os.makedirs(ws, exist_ok=True)
    env = dict(os.environ)
    env["RELAY_KEYRING"] = "off"
    env["PYTHONPATH"] = os.path.join(ROOT, "backend")
    for k, sub in (("XDG_DATA_HOME", "data"), ("XDG_CONFIG_HOME", "config"),
                   ("XDG_STATE_HOME", "state"), ("XDG_CACHE_HOME", "cache")):
        env[k] = os.path.join(scratch, "xdg", sub)
        os.makedirs(env[k], exist_ok=True)
    cmd = [sys.executable, "-S", "-u", os.path.join(ROOT, "backend", "worker.py")]
    if "--profile" in ARGS:
        cmd = [sys.executable, "-u", "-m", "cProfile", "-o", os.path.join(scratch, "worker.prof"),
               os.path.join(ROOT, "backend", "worker.py")]
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=open(os.path.join(scratch, "worker.stderr"), "wb"), env=env, cwd=ws)

    def send(obj):
        p.stdin.write((json.dumps(obj) + "\n").encode())
        p.stdin.flush()

    seen = {}

    def wait_for(events, timeout=300):
        deadline = time.perf_counter() + timeout
        while time.perf_counter() < deadline:
            line = p.stdout.readline()
            if not line:
                raise SystemExit("worker died: " + open(os.path.join(scratch, "worker.stderr")).read()[-2000:])
            obj = json.loads(line)
            name = obj.get("event")
            seen[name] = seen.get(name, 0) + 1
            if name in ("done", "error", "cancelled") and "t_done" not in seen:
                seen["t_done"] = time.perf_counter()
                seen["last_outcome"] = obj
            if name in events:
                return obj, time.perf_counter()
        raise SystemExit("timeout waiting for " + str(events))

    wait_for({"ready"})
    send({"type": "configure", "base_url": f"http://127.0.0.1:{port}/v1", "model": "stub",
          "api_key": "stub", "workspace": ws, "approval": "never",
          "options": {"turn_limits": {"steps": 500, "tool_calls": 2000}}})
    wait_for({"configured", "error"})

    rows = []
    cpu0 = cpu_of(p.pid)
    t_run0 = time.perf_counter()
    for i in range(TURNS):
        with lock:
            counter["n"] = i
            marks[i] = {}
        seen.pop("t_done", None)
        t_ask = time.perf_counter()
        send({"type": "ask", "id": str(i), "text": f"stub turn {i} please answer"})
        obj, _ = wait_for({"agent_finished"})
        t_done = seen.get("t_done", time.perf_counter())
        if seen.get("last_outcome", {}).get("event") == "error" and i == 0:
            raise SystemExit("turn 0 errored: " + str(seen["last_outcome"])[:400])
        with lock:
            m = dict(marks.get(i, {}))
        rows.append({
            "turn": i,
            "wall": t_done - t_ask,
            "ask_to_request": (m.get("t_first_request", t_ask) - t_ask),
            "last_to_done": (t_done - m.get("t_last_byte", t_done)),
            "req_bytes": m.get("req_bytes", 0),
            "msgs": m.get("msg_count", 0),
            "calls": m.get("calls", 0),
            "cpu": cpu_of(p.pid),
            "pss_kb": rss_of(p.pid) if i % 10 == 0 or i == TURNS - 1 else 0,
        })
    t_run = time.perf_counter() - t_run0
    cpu1 = cpu_of(p.pid)
    pss = rss_of(p.pid)
    out = os.path.join(scratch, f"turns-{TURNS}{'-tools' if WITH_TOOLS else ''}.json")
    with open(out, "w") as f:
        json.dump({"rows": rows, "run_wall": t_run, "worker_cpu": cpu1 - cpu0, "pss_kb": pss,
                   "python": sys.version.split()[0], "loadavg": os.getloadavg(),
                   "deltas": DELTAS, "tools": WITH_TOOLS}, f)
    send({"type": "shutdown"})
    try:
        p.wait(timeout=30)
    except subprocess.TimeoutExpired:
        p.kill()
    srv.shutdown()

    def band(lo, hi):
        sel = [r for r in rows if lo <= r["turn"] < hi]
        if not sel:
            return None
        f = lambda k: sorted(r[k] for r in sel)[len(sel) // 2]
        return (f"turns {lo}-{hi-1}: wall={f('wall')*1000:7.1f} ms  ask->req={f('ask_to_request')*1000:7.1f} ms  "
                f"last->done={f('last_to_done')*1000:7.1f} ms  req_bytes={f('req_bytes'):8d}  msgs={f('msgs')}")
    print(f"python {sys.version.split()[0]}  loadavg {os.getloadavg()}  turns={TURNS} tools={WITH_TOOLS} deltas={DELTAS}")
    for lo, hi in ((0, 1), (1, 10), (10, 50), (50, 100), (100, 200), (200, 300), (300, 400)):
        s = band(lo, hi)
        if s:
            print(s)
    print(f"total wall {t_run:.1f} s, worker CPU {cpu1-cpu0:.2f} s, Pss {pss/1024:.1f} MiB -> {out}")


main()
