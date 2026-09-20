#!/usr/bin/env python3
"""Streaming cost: N SSE content deltas in one turn -> worker CPU, stdout events and bytes.

  streambench.py <src-root> <n_deltas> [chars_per_delta] [rate_per_s]
rate 0 = as fast as possible.
"""
import json, os, subprocess, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = sys.argv[1]
N = int(sys.argv[2])
CHARS = int(sys.argv[3]) if len(sys.argv) > 3 else 12
RATE = float(sys.argv[4]) if len(sys.argv) > 4 else 0.0
scratch = os.path.dirname(os.path.abspath(__file__))
state = {}


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        self.rfile.read(length)
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()

        def chunk(delta, fr=None):
            d = {"id": "s", "object": "chat.completion.chunk", "created": 0, "model": "stub",
                 "choices": [{"index": 0, "delta": delta, "finish_reason": fr}]}
            raw = b"data: " + json.dumps(d).encode() + b"\n\n"
            self.wfile.write(hex(len(raw))[2:].encode() + b"\r\n" + raw + b"\r\n")
            self.wfile.flush()

        piece = "x" * CHARS
        state["t_stream0"] = time.perf_counter()
        start = time.perf_counter()
        for i in range(N):
            if RATE:
                due = start + i / RATE
                now = time.perf_counter()
                if due > now:
                    time.sleep(due - now)
            chunk({"role": "assistant", "content": piece})
        chunk({}, "stop")
        tail = b"data: [DONE]\n\n"
        self.wfile.write(hex(len(tail))[2:].encode() + b"\r\n" + tail + b"\r\n")
        self.wfile.write(b"0\r\n\r\n")
        self.wfile.flush()
        state["t_stream1"] = time.perf_counter()

    def do_GET(self):
        raw = b'{"data":[{"id":"stub"}]}'
        self.send_response(200); self.send_header("Content-Length", str(len(raw))); self.end_headers()
        self.wfile.write(raw)


def cpu_of(pid):
    st = open(f"/proc/{pid}/stat").read().rsplit(") ", 1)[1].split()
    return (int(st[11]) + int(st[12])) / os.sysconf("SC_CLK_TCK")


srv = ThreadingHTTPServer(("127.0.0.1", 0), H)
port = srv.server_address[1]
threading.Thread(target=srv.serve_forever, daemon=True).start()
env = dict(os.environ); env["RELAY_KEYRING"] = "off"; env["PYTHONPATH"] = os.path.join(ROOT, "backend")
for k, sub in (("XDG_DATA_HOME", "data"), ("XDG_CONFIG_HOME", "config"),
               ("XDG_STATE_HOME", "state"), ("XDG_CACHE_HOME", "cache")):
    env[k] = os.path.join(scratch, "xdg-s", sub); os.makedirs(env[k], exist_ok=True)
ws = os.path.join(scratch, "ws"); os.makedirs(ws, exist_ok=True)
p = subprocess.Popen([sys.executable, "-S", "-u", os.path.join(ROOT, "backend", "worker.py")],
                     stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=env, cwd=ws)


def send(o):
    p.stdin.write((json.dumps(o) + "\n").encode()); p.stdin.flush()


counts, bytes_out = {}, 0
def wait(names):
    global bytes_out
    while True:
        line = p.stdout.readline()
        if not line:
            raise SystemExit("died")
        bytes_out += len(line)
        o = json.loads(line)
        counts[o.get("event")] = counts.get(o.get("event"), 0) + 1
        if o.get("event") in names:
            return o


wait({"ready"})
send({"type": "configure", "base_url": f"http://127.0.0.1:{port}/v1", "model": "stub",
      "api_key": "k", "workspace": ws})
wait({"configured", "error"})
counts.clear(); bytes_out = 0
c0 = cpu_of(p.pid); t0 = time.perf_counter()
send({"type": "ask", "id": "1", "text": "stream please"})
wait({"agent_finished"})
wall = time.perf_counter() - t0
cpu = cpu_of(p.pid) - c0
send({"type": "shutdown"}); p.wait(timeout=30)
srv.shutdown()
sd = state.get("t_stream1", 0) - state.get("t_stream0", 0)
print(f"deltas={N} chars={CHARS} rate={RATE or 'max'}/s  loadavg={os.getloadavg()[0]:.2f}")
print(f"  turn wall {wall*1000:.0f} ms, provider streamed for {sd*1000:.0f} ms")
print(f"  worker CPU {cpu*1000:.0f} ms  =>  {cpu/N*1e6:.0f} us per delta")
print(f"  worker stdout: {sum(counts.values())} events, {bytes_out} bytes  "
      f"({sum(counts.values())/N:.2f} events and {bytes_out/N:.1f} bytes per delta)")
print(f"  events: {sorted(counts.items(), key=lambda x: -x[1])[:8]}")
