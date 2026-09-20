#!/usr/bin/env python3
"""tracemalloc growth of the worker across a stub conversation.  memgrow.py <src-root> <turns>

Uses the instrumented copy (a `memstat` protocol message). Prints Pss and traced bytes at
checkpoints and the top allocation sites at the end.
"""
import json, os, subprocess, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT, TURNS = sys.argv[1], int(sys.argv[2])
scratch = os.path.dirname(os.path.abspath(__file__))
PROSE = ("This is a stub answer. " * 20).strip()


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def do_POST(self):
        self.rfile.read(int(self.headers.get("Content-Length") or 0))
        self.send_response(200); self.send_header("Content-Type", "text/event-stream")
        self.send_header("Transfer-Encoding", "chunked"); self.end_headers()

        def chunk(delta, fr=None):
            d = {"id": "s", "object": "chat.completion.chunk", "created": 0, "model": "stub",
                 "choices": [{"index": 0, "delta": delta, "finish_reason": fr}]}
            raw = b"data: " + json.dumps(d).encode() + b"\n\n"
            self.wfile.write(hex(len(raw))[2:].encode() + b"\r\n" + raw + b"\r\n"); self.wfile.flush()
        for i in range(3):
            chunk({"role": "assistant", "content": PROSE[i * 150:(i + 1) * 150]})
        chunk({}, "stop")
        tail = b"data: [DONE]\n\n"
        self.wfile.write(hex(len(tail))[2:].encode() + b"\r\n" + tail + b"\r\n")
        self.wfile.write(b"0\r\n\r\n"); self.wfile.flush()

    def do_GET(self):
        raw = b'{"data":[{"id":"stub"}]}'
        self.send_response(200); self.send_header("Content-Length", str(len(raw))); self.end_headers()
        self.wfile.write(raw)


srv = ThreadingHTTPServer(("127.0.0.1", 0), H); port = srv.server_address[1]
threading.Thread(target=srv.serve_forever, daemon=True).start()
env = dict(os.environ); env["RELAY_KEYRING"] = "off"; env["PYTHONPATH"] = os.path.join(ROOT, "backend")
env["RELAY_TRACEMALLOC"] = "1"
for k, sub in (("XDG_DATA_HOME", "data"), ("XDG_CONFIG_HOME", "config"),
               ("XDG_STATE_HOME", "state"), ("XDG_CACHE_HOME", "cache")):
    env[k] = os.path.join(scratch, "xdg-m", sub); os.makedirs(env[k], exist_ok=True)
ws = os.path.join(scratch, "ws"); os.makedirs(ws, exist_ok=True)
err = open(os.path.join(scratch, "memgrow.stderr"), "w+b")
p = subprocess.Popen([sys.executable, "-S", "-u", os.path.join(ROOT, "backend", "worker.py")],
                     stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=err, env=env, cwd=ws)


def send(o):
    p.stdin.write((json.dumps(o) + "\n").encode()); p.stdin.flush()


def wait(names):
    while True:
        line = p.stdout.readline()
        if not line:
            raise SystemExit("died: " + open(os.path.join(scratch, "memgrow.stderr")).read()[-1500:])
        o = json.loads(line)
        if o.get("event") in names:
            return o


def pss():
    for line in open(f"/proc/{p.pid}/smaps_rollup"):
        if line.startswith("Pss:"):
            return int(line.split()[1]) / 1024
    return 0


wait({"ready"})
send({"type": "configure", "base_url": f"http://127.0.0.1:{port}/v1", "model": "stub",
      "api_key": "k", "workspace": ws})
wait({"configured", "error"})
marks = [0, 1, 10, 50, 100, 200, 300, 500]
out = []
for i in range(TURNS):
    send({"type": "ask", "id": str(i), "text": f"stub turn {i} please answer"})
    wait({"agent_finished"})
    if i + 1 in marks or i + 1 == TURNS:
        send({"type": "memstat", "id": "m", "tag": i + 1})
        wait({"memstat"})
        out.append((i + 1, pss()))
send({"type": "shutdown"}); p.wait(timeout=60)
err.flush(); err.seek(0)
stats = [json.loads(l.decode()[8:]) for l in err.read().split(b"\n") if l.startswith(b"MEMSTAT ")]
print(f"python {sys.version.split()[0]}  loadavg {os.getloadavg()[0]:.2f}")
print(f"{'turns':>6} {'Pss MiB':>9} {'traced MiB':>11} {'peak MiB':>9} {'gc objects':>11}")
for (t, ps), s in zip(out, stats):
    print(f"{t:6d} {ps:9.1f} {s['traced_mib']:11.2f} {s['peak_mib']:9.2f} {s['objects']:11d}")
if stats:
    print("\ntop allocation sites at the end:")
    for line in stats[-1]["top"]:
        print("  " + line)
