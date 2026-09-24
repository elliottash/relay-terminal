"""Exercise the SSH delegation default in an isolated Relay/Xvfb profile."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
APP = Path(os.environ.get("RELAY_SSH_TEST_BINARY", ROOT / "build-fast/relay"))
FAKE = '''import sys,json,os
print(json.dumps({'event':'ready'}),flush=True)
for line in sys.stdin:
 r=json.loads(line); t=r.get('type')
 if t in ('program_state','remote_session_update'):
  with open(os.environ['SSH_DRIVE_EVENTS'],'a') as f: f.write(json.dumps(r)+'\\n')
 if t=='shutdown': break
 if t=='presets': o={'event':'presets','presets':[{'id':'guest:codex','label':'Fixture','group':'guest','harness':True,'model':'gpt-6-astra','models':[{'id':'gpt-6-astra','label':'Fixture','name':'Fixture'}]}]}
 elif t=='configure': o={'event':'configured','model':'gpt-6-astra','mode':'build','session_id':'ssh-default','roles':{'main':{'model':'gpt-6-astra'}}}
 elif t=='route': o={'event':'route','id':r.get('id'),'route':'shell','text':r.get('text','').removeprefix('!')}
 else: continue
 print(json.dumps(o),flush=True)
 if t=='presets': print(json.dumps({'event':'configured','model':'gpt-6-astra','mode':'build','session_id':'ssh-default','roles':{'main':{'model':'gpt-6-astra'}}}),flush=True)
'''

with tempfile.TemporaryDirectory(prefix="relay-ssh-default-") as tmp:
    base = Path(tmp)
    for name in ("config/RelayTerminal", "data", "cache", "run", "fixture/backend", "work", "tmp"):
        (base / name).mkdir(parents=True)
    (base / "run").chmod(0o700)
    (base / "fixture/backend/worker.py").write_text(FAKE)
    for name in ("shell", "scripts", "assets"):
        (base / "fixture" / name).symlink_to(ROOT / name)
    (base / "config/RelayTerminal/relay.conf").write_text(
        "[instructions]\nonboarded=true\n[isolation]\nenabled=false\n"
        "[models]\ntier\\main=guest:codex|gpt-6-astra|high\n"
        "[suggestions]\nnext_command=false\nnext_prompt=false\n"
        "[security]\napprovals_chosen=true\n[url_handler]\nannounced=true\n"
    )
    events = OUT / "events.jsonl"
    events.write_text("")
    display = next(":" + str(n) for n in range(450, 490)
                   if not Path("/tmp/.X11-unix/X" + str(n)).exists())
    env = {**os.environ, "SSH_DRIVE_EVENTS": str(events), "DISPLAY": display,
           "XDG_CONFIG_HOME": str(base / "config"), "XDG_DATA_HOME": str(base / "data"),
           "XDG_CACHE_HOME": str(base / "cache"), "XDG_RUNTIME_DIR": str(base / "run"),
           "TMPDIR": str(base / "tmp"), "RELAY_DATA_DIR": str(base / "fixture"),
           "RELAY_KEYRING": "off", "QT_QPA_PLATFORM": "xcb", "OMP_THREAD_LIMIT": "1"}
    xvfb = subprocess.Popen(["Xvfb", display, "-screen", "0", "1300x950x24"],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    app = None
    try:
        def x(*args):
            return subprocess.check_output(["xdotool", *args], env=env, text=True).strip()

        def shot(name):
            subprocess.run(["import", "-window", window, str(OUT / (name + ".png"))],
                           env=env, check=True)
            with (OUT / (name + ".txt")).open("w") as capture:
                subprocess.run(["tesseract", str(OUT / (name + ".png")), "stdout"],
                               stdout=capture, stderr=subprocess.DEVNULL, env=env, check=True)

        time.sleep(.5)
        with (OUT / "gui.log").open("w") as log:
            app = subprocess.Popen([str(APP), "--clean-shell", "--workspace", str(base / "work")],
                                   env=env, stdout=log, stderr=log)
            time.sleep(5)
            windows = x("search", "--pid", str(app.pid)).splitlines()
            window = max(windows, key=lambda w: int(x("getwindowgeometry", "--shell", w)
                                                  .split("WIDTH=")[1].splitlines()[0]))
            x("windowsize", window, "1200", "900")
            x("windowfocus", "--sync", window)
            x("type", "--clearmodifiers", "!ssh -t localhost 'HISTFILE=/dev/null bash --noprofile --norc -i'")
            x("key", "Return")
            time.sleep(7)
            shot("01-agent-driving-ssh")
            x("key", "ctrl+h")
            time.sleep(1)
            shot("02-takeover")
            x("key", "ctrl+shift+h")
            time.sleep(1)
            shot("03-prompt-box-restored")
        states = [json.loads(line) for line in events.read_text().splitlines()
                  if line and json.loads(line).get("type") == "program_state"]
        granted = [i for i, state in enumerate(states) if state.get("granted") is True]
        assert granted, "SSH never granted program control"
        takeover = next((i for i, state in enumerate(states[granted[0] + 1:], granted[0] + 1)
                         if state.get("reason") == "take_over" and not state.get("granted")), None)
        assert takeover is not None, "Takeover did not revoke the grant"
        assert not any(state.get("granted") for state in states[takeover + 1:]), "Control was regranted"
        assert "Agent driving ssh" in (OUT / "01-agent-driving-ssh.txt").read_text(), "Banner absent"
        print("PASS: SSH starts delegated, takeover revokes, prompt restoration does not regrant")
    finally:
        if app:
            app.terminate()
            try:
                app.wait(timeout=5)
            except subprocess.TimeoutExpired:
                app.kill()
                app.wait()
        for socket in (base / "run/relay-ssh").glob("*"):
            subprocess.run(["ssh", "-S", str(socket), "-O", "exit", "localhost"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        xvfb.terminate()
        xvfb.wait()
