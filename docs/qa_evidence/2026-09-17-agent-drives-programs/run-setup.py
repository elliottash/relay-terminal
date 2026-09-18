#!/usr/bin/env python3
"""Private X server plus an isolated Relay profile and a scratch workspace for the live QA."""
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

QA = Path(os.environ.get("QA", "/tmp/claude-1000/-home-elliott-repos-relay-terminal/"
                               "ea11ece1-7ec2-4597-8639-32fb1f43f073/scratchpad/qa"))
DISPLAY = os.environ.get("DISPLAY_NUM", ":95")
PRESET = os.environ.get("RELAY_QA_PRESET", "kimi")

for name in ("home", "work"):
    shutil.rmtree(QA / name, ignore_errors=True)
(QA / "home/config/RelayTerminal").mkdir(parents=True, exist_ok=True)
(QA / "home/data").mkdir(parents=True, exist_ok=True)
(QA / "work").mkdir(parents=True, exist_ok=True)
(QA / "shots").mkdir(parents=True, exist_ok=True)

(QA / "home/config/RelayTerminal/relay.conf").write_text(f"""[provider]
preset={PRESET}
max_tokens=4096

[control]
default=agent

[hints]
enabled=false
""")

# Every script below is a real foreground program, so the pane sees a process group other than
# its shell. Bash's own `read` builtin would not be one, which is why these are scripts.
scripts = {
"ask.sh": """#!/usr/bin/env bash
read -p "Continue? " answer
echo "got: $answer"
""",
"fake-apt.sh": """#!/usr/bin/env bash
# The tail of `apt-get --simulate upgrade`, word for word, then apt's question.
echo "Reading package lists... Done"
echo "Building dependency tree... Done"
echo "Reading state information... Done"
echo "The following packages will be upgraded:"
echo "  curl libcurl4"
echo "2 upgraded, 0 newly installed, 0 to remove and 12 not upgraded."
echo "Need to get 568 kB of archives."
echo "After this operation, 4096 B of additional disk space will be used."
printf 'Do you want to continue? [Y/n] '
read answer
echo "Inst curl [8.4.0] (8.5.0 Ubuntu:24.04/noble-updates [amd64])"
echo "answer was: ${answer:-Y}"
""",
"ask-then-password.sh": """#!/usr/bin/env bash
printf 'Do you want to continue? [Y/n] '
read answer
echo "continuing with ${answer:-Y}"
read -s -p "[sudo] password for elliott: " secret
echo
echo "password length: ${#secret}"
""",
"three-questions.sh": """#!/usr/bin/env bash
for n in 1 2 3 4 5; do
  printf 'Question %s: keep going? [Y/n] ' "$n"
  read answer
  echo "answer $n: ${answer:-Y}"
done
echo "done"
""",
}
for name, body in scripts.items():
    path = QA / "work" / name
    path.write_text(body)
    path.chmod(0o755)

subprocess.run(["pkill", "-f", f"Xvfb {DISPLAY}"], capture_output=True)
time.sleep(1)
server = subprocess.Popen(["Xvfb", DISPLAY, "-screen", "0", "1700x1000x24"],
                          stdout=open(QA / "xvfb.log", "w"), stderr=subprocess.STDOUT)
(QA / "xvfb.pid").write_text(str(server.pid))
time.sleep(2)
print(f"display {DISPLAY} up, pid {server.pid}, preset {PRESET}")
