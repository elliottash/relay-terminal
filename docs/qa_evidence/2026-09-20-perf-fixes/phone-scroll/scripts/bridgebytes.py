# SPDX-License-Identifier: AGPL-3.0-or-later
#!/usr/bin/env python3
"""Bytes on the screen stream for one scenario, straight out of relay-screen-bridge.

    bridgebytes.py <bridge> <scenario> [seconds]

The bridge is the same serializer the GUI's share uses (engine/tools/ScreenJson.h), so this is
the screen half of what a phone receives — the half that was 86 % of a streamed reply (#3H5T).
"""
import base64, json, os, subprocess, sys, threading, time

bridge, scenario = sys.argv[1], sys.argv[2]
seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 30.0

# The scenarios are scripts beside this file: 200 lines of 100 characters at ten a second (a
# 20 000-character reply streaming for 20 s), and 50 MB through the terminal at full speed.
HERE = os.path.dirname(os.path.abspath(__file__))
SCENARIOS = {name: f"sh {HERE}/{name}.sh\n" for name in ("prose", "reply", "firehose")}

proc = subprocess.Popen([bridge, "--rows", "24", "--cols", "80", "--shell", "/bin/sh"],
                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, env={"TERM": "xterm-256color",
                        "PATH": "/usr/bin:/bin", "HOME": "/tmp", "SHELL": "/bin/sh"})
counts: dict[str, list[int]] = {}
done = threading.Event()

def read():
    for raw in proc.stdout:
        try:
            message = json.loads(raw)
        except Exception:
            continue
        kind = message.get("t", "?")
        if kind in ("snapshot", "diff") and message.get("scroll"):
            kind = "diff+scroll"
        entry = counts.setdefault(kind, [0, 0])
        entry[0] += 1
        entry[1] += len(raw)
    done.set()

threading.Thread(target=read, daemon=True).start()
time.sleep(1.0)
counts.clear()                                   # the shell's prompt is not the scenario
start = time.time()
proc.stdin.write((json.dumps({"t": "input",
                              "bytes": base64.b64encode(SCENARIOS[scenario].encode()).decode()})
                  + "\n").encode())
proc.stdin.flush()
time.sleep(seconds)
proc.stdin.write(b'{"t":"quit"}\n')
proc.stdin.flush()
done.wait(10)
proc.kill()

total = sum(entry[1] for entry in counts.values())
screen = sum(entry[1] for kind, entry in counts.items() if kind.startswith(("snapshot", "diff")))
print(f"{scenario}: {time.time()-start:.1f}s   screen stream {screen:,} B   everything {total:,} B")
for kind in sorted(counts, key=lambda k: -counts[k][1]):
    n, b = counts[kind]
    print(f"    {kind:<14} x{n:<5} {b:>10,} B   avg {b//max(n,1):>7,} B")
