"""Live check of the sessions backend against real providers via the worker protocol."""
import json, queue, shutil, subprocess, sys, threading, time
from pathlib import Path

WT = Path("/home/elliott/repos/relay-terminal/.claude/worktrees/agent-a7d90f11600e88880")
SCRATCH = Path(sys.argv[1])
sys.path.insert(0, str(WT / "backend"))
from relay_core.presets import PRESETS  # noqa: E402

if SCRATCH.exists():
    shutil.rmtree(SCRATCH)
WS = SCRATCH / "workspace"; WS.mkdir(parents=True)
SESS = SCRATCH / "sessions"
(WS / "hello.py").write_text('import sys\n\n\ndef main():\n    print("hello")\n\n\nif __name__ == "__main__":\n    main()\n')
(WS / "AGENTS.md").write_text("Project rule: always call the greeting program 'hello.py'.\n")
LOG = open(SCRATCH / "events.jsonl", "w")


class Worker:
    def __init__(self, name):
        self.name = name
        self.proc = subprocess.Popen([sys.executable, str(WT / "backend/worker.py")], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, text=True, cwd=WT)
        self.events = []
        self.cond = threading.Condition()
        threading.Thread(target=self.read, daemon=True).start()

    def read(self):
        for line in self.proc.stdout:
            event = json.loads(line)
            logged = event if event.get("event") not in ("delta", "tool_output") else None
            if logged:
                LOG.write(json.dumps({"worker": self.name, **logged})[:4000] + "\n"); LOG.flush()
            with self.cond:
                self.events.append(event); self.cond.notify_all()

    def send(self, obj):
        LOG.write(json.dumps({"worker": self.name, "send": obj})[:2000] + "\n"); LOG.flush()
        self.proc.stdin.write(json.dumps(obj) + "\n"); self.proc.stdin.flush()

    def wait(self, pred, timeout=400, start=0):
        deadline = time.time() + timeout
        with self.cond:
            while True:
                for e in self.events[start:]:
                    if pred(e):
                        return e
                if time.time() > deadline:
                    raise TimeoutError(json.dumps(self.events[-5:])[:3000])
                self.cond.wait(1)

    def turn(self, text, **extra):
        start = len(self.events)
        self.send({"type": "ask", "text": text, **extra})
        finished = self.wait(lambda e: e["event"] in ("agent_finished",) or (e["event"] == "error" and "agent_busy" in e and not e.get("source")), start=start)
        reply = "".join(e["text"] for e in self.events[start:] if e["event"] == "delta")
        return finished, reply, self.events[start:]


def preset_msg(pid):
    p = PRESETS[pid]
    return {"preset": pid, "base_url": p.base_url, "model": p.model, "extra": p.extra, "use_stored_key": True,
            "max_tokens": 16384}


results = {}
w = Worker("A")
w.wait(lambda e: e["event"] == "ready")
w.send({"type": "configure", **preset_msg("kimi"), "workspace": str(WS), "session_dir": str(SESS)})
configured = w.wait(lambda e: e["event"] in ("configured", "error"))
results["configured"] = configured
print("configured:", json.dumps(configured)[:600])

fin, reply, _ = w.turn("Remember this codeword for later: TANGERINE-42. Reply with just OK.")
print("turn1:", fin["outcome"], reply[:200])
results["turn1"] = (fin["outcome"], reply[:200])

start = len(w.events)
w.send({"type": "set_model", **preset_msg("glm-coding")})
changed = w.wait(lambda e: e["event"] in ("model_changed", "error"), start=start)
print("model_changed:", changed)
results["model_changed"] = changed

fin, reply, evs = w.turn("What codeword did I give you earlier? Reply with just the codeword.")
print("turn2 (glm):", fin["outcome"], reply[:200])
results["turn2"] = (fin["outcome"], reply[:200], "TANGERINE-42" in reply)
ctx = [e for e in evs if e["event"] == "context"]
print("context after glm turn:", ctx[-1] if ctx else None)
results["context_turn2"] = ctx[-1] if ctx else None

start = len(w.events)
w.send({"type": "set_effort", "effort": "low"})
print("effort:", w.wait(lambda e: e["event"] in ("effort_changed", "error"), start=start))

start = len(w.events)
w.send({"type": "set_mode", "mode": "plan"})
w.wait(lambda e: e["event"] == "mode_changed", start=start)
fin, reply, evs = w.turn("Plan how to add a --verbose flag to hello.py that prints the Python version. Read the file first, "
                         "then write the plan with write_plan.")
plans = [e for e in evs if e["event"] == "plan_written"]
tools_used = [e.get("tool") for e in evs if e["event"] == "tool_started"]
print("plan turn:", fin["outcome"], "tools:", tools_used, "plans:", plans, "reply:", reply[:300])
results["plan"] = {"outcome": fin["outcome"], "tools": tools_used, "plans": plans,
                   "hello_unchanged": "verbose" not in (WS / "hello.py").read_text()}
if plans:
    results["plan"]["file_head"] = Path(plans[0]["path"]).read_text()[:600]

start = len(w.events)
w.send({"type": "set_mode", "mode": "build"})
w.send({"type": "checkpoints", "id": "cp"})
print("checkpoints:", json.dumps(w.wait(lambda e: e["event"] == "checkpoints", start=start))[:600])

start = len(w.events)
w.send({"type": "compact", "focus": "the codeword and the plan"})
compacted = w.wait(lambda e: e["event"] in ("compacted", "error"), start=start)
print("compacted:", compacted)
results["compacted"] = compacted
time.sleep(1)
w.send({"type": "context", "id": "ctx"})
print("context after compaction:", w.wait(lambda e: e["event"] == "context" and e.get("id") == "ctx"))

fin, reply, _ = w.turn("After compaction: what was the codeword? Reply with just the codeword.")
print("turn after compaction:", fin["outcome"], reply[:200])
results["after_compaction"] = (fin["outcome"], reply[:200], "TANGERINE-42" in reply)

start = len(w.events)
w.send({"type": "suggest", "kind": "next_prompt", "id": "np"})
print("next_prompt:", w.wait(lambda e: e.get("id") == "np", start=start))
start = len(w.events)
w.send({"type": "suggest", "kind": "next_command", "id": "nc", "command": "python3 hello.py", "exit_status": 0,
        "cwd": str(WS), "output_tail": "hello\n"})
print("next_command:", w.wait(lambda e: e.get("id") == "nc", start=start))
session_id = configured.get("session_id")
w.send({"type": "shutdown"}); w.proc.wait(10)

# Resume in a fresh worker (new pane) with the kimi preset.
r = Worker("B")
r.wait(lambda e: e["event"] == "ready")
r.send({"type": "configure", **preset_msg("kimi"), "workspace": str(WS), "session_dir": str(SESS)})
r.wait(lambda e: e["event"] == "configured")
r.send({"type": "sessions", "id": "s"})
listing = r.wait(lambda e: e["event"] == "sessions")
print("sessions:", listing)
r.send({"type": "resume", "id": session_id})
loaded = r.wait(lambda e: e["event"] in ("state_loaded", "error"))
print("state_loaded:", loaded)
recap = r.wait(lambda e: e["event"] == "recap")
print("recap:", recap)
results.update({"sessions": listing, "state_loaded": loaded, "recap": recap})
fin, reply, _ = r.turn("One more check after resume: what was the codeword? Reply with just the codeword.")
print("after resume:", fin["outcome"], reply[:200])
results["after_resume"] = (fin["outcome"], reply[:200], "TANGERINE-42" in reply)
r.send({"type": "shutdown"}); r.proc.wait(10)
(SCRATCH / "results.json").write_text(json.dumps(results, indent=2, default=str))
print("DONE")
