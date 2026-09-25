"""#RCPF drive: every Relay Free entry point, against a loopback stub that logs each request.

Run under scripts/relay-qa-run (which sets RELAY_HOSTED=off) and again with RELAY_HOSTED= to
prove the stub is reachable when the switch is unset. Prints the requests the stub saw.
"""
import http.server, json, os, sys, threading
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "../../../backend"))

seen = []
class Stub(http.server.BaseHTTPRequestHandler):
    def _log(self):
        seen.append(f"{self.command} {self.path}")
        self.send_response(503); self.send_header("Content-Type", "application/json"); self.end_headers()
        self.wfile.write(b'{"error":{"code":"free_unavailable"}}')
    do_GET = do_POST = _log
    def log_message(self, *a): pass

server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Stub)
threading.Thread(target=server.serve_forever, daemon=True).start()
os.environ["RELAY_HOSTED_URL"] = f"http://127.0.0.1:{server.server_port}/v1"

from relay_core import hosted, relay_pro, keytest
from relay_core.provider import ProviderConfig, make_provider
from relay_core.roles import RoleResolver

print("RELAY_HOSTED=%r  available=%s  status=%s" % (os.environ.get("RELAY_HOSTED"), hosted.available(), hosted.status()))
harness = ProviderConfig("harness://claude", "fable", "", {}, 32768)
r = RoleResolver(harness, "guest:claude", key_lookup=lambda p: "",
                 tiers={"flash": [{"preset": "guest:claude", "model": "fable"}]}, guest_check=lambda g: True)
for role in ("summaries", "chores", "terminal_use"):
    got = r.resolve(role)
    print(f"harness pane {role:13s} -> preset={got.preset_id} model={got.model} is_main={got.is_main}")

def attempt(label, fn):
    try:
        fn(); print(f"{label}: returned")
    except Exception as exc:
        print(f"{label}: {type(exc).__name__}: {str(exc)[:110]}")

free = ProviderConfig("https://api.relay-terminal.ai/v1", "relay-flash", "", {}, 1024, hosted=True)
cancel = threading.Event()
attempt("chat turn (recap/title/summary transport)",
        lambda: make_provider(free).complete([{"role": "user", "content": "recap"}], [], lambda e: None, cancel))
attempt("token()", lambda: hosted.session().token())
attempt("fetch_pro", lambda: relay_pro.validate("abc123"))
attempt("image", lambda: hosted.session().image({"prompt": "x"}))
print("image_roles:", hosted.image_roles())
print("REQUESTS SEEN BY STUB:", seen if seen else "none")
