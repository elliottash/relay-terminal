#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Card #EFT9, stream E: the whole seam in one run, measured rather than read.

`effort_probe` is the desktop's own `relay::panestate::build()` (linked against
`librelay-panestate.a`); every `model` block it prints is put into a real `pane_state` and run
through `remote/pane_state.py` — the hub's cleaner and its per-capability strip — twice: once with
the module as it is now, and once with the copy from before this stream's first commit, so what
changed is a diff and not a claim.

Run from the repo root, with the probe built (see README).
"""
import copy
import importlib.util
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from remote import pane_state as new, wire   # noqa: E402

BEFORE = "2ee01f0c2467^"        # the tip this stream started from


def old_module():
    """The cleaner as it was before #EFT9, imported beside the new one."""
    source = subprocess.run(["git", "show", f"{BEFORE}:remote/pane_state.py"],
                            cwd=ROOT, capture_output=True, text=True, check=True).stdout
    path = ROOT / "remote" / "pane_state_before_eft9.py"
    path.write_text(source)
    try:
        spec = importlib.util.spec_from_file_location("remote.pane_state_before_eft9", path)
        module = importlib.util.module_from_spec(spec)
        sys.modules["remote.pane_state_before_eft9"] = module
        spec.loader.exec_module(module)     # `from . import wire` resolves inside the package
    finally:
        path.unlink()
    return module


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else "/tmp/claude-1000/streamE/ev/effort_probe"
    probe = subprocess.run([binary], capture_output=True, text=True, check=True).stdout
    before = old_module()
    for line in probe.strip().splitlines():
        what, block = line.split("\t", 1)
        message = copy.deepcopy(new.EXAMPLE)
        message["model"] = json.loads(block)
        cleaned_new = new.clean(message)["model"]
        cleaned_old = before.clean(copy.deepcopy(message))["model"]
        print(f"=== {what}")
        print(f"  desktop      {block}")
        print(f"  hub (before) {json.dumps({k: v for k, v in cleaned_old.items() if 'effort' in k})}")
        print(f"  hub (now)    {json.dumps({k: v for k, v in cleaned_new.items() if 'effort' in k})}")
        for capability in (wire.VIEW, wire.AGENT, wire.FULL):
            model = new.for_capability(new.clean(message), capability)["model"]
            print(f"  {capability:<5}        "
                  f"{json.dumps({k: v for k, v in model.items() if 'effort' in k}) or '{}'}")


if __name__ == "__main__":
    main()
