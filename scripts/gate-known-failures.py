#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Run the Python suite and fail only on failures that are not already known (card #3MH4).

The queue gate runs every test, and `main` carried failures the legacy targeted-test procedure
never ran. Until they are fixed, `.relay/known-failures.txt` lists them: a known failure is
reported but tolerated, a new one fails the gate, and a known one that now passes is reported
so the list can shrink. The runner must also finish (its "Ran N tests" line), so a crashed or
hung suite is never mistaken for a clean one.

    scripts/gate-known-failures.py [--known FILE] [--update]

`--update` rewrites the list from this run instead of judging it.
"""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RESULT = re.compile(r"^(?:.*\((?P<id>[\w.]+)\)|(?P<doc>.+?)) \.\.\. (?P<outcome>ok|FAIL|ERROR|skipped.*|expected failure|unexpected success)$")
CLASS_ERROR = re.compile(r"^ERROR: (?P<id>\w+ \([\w.]+\))$")


def run_suite() -> tuple[set, bool, str]:
    env = dict(os.environ)
    proc = subprocess.run([str(ROOT / "scripts" / "test.sh")], cwd=ROOT, env=env,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    out = proc.stdout
    failed = set()
    for line in out.splitlines():
        m = RESULT.match(line)
        if m and m.group("outcome") in ("FAIL", "ERROR", "unexpected success"):
            failed.add(m.group("id") or m.group("doc").strip())
            continue
        m = CLASS_ERROR.match(line)
        if m and ("setUpClass" in line or "setUpModule" in line or "tearDown" in line):
            failed.add(m.group("id"))
    finished = re.search(r"^Ran \d+ tests? in ", out, re.M) is not None
    return failed, finished, out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--known", default=str(ROOT / ".relay" / "known-failures.txt"))
    ap.add_argument("--update", action="store_true")
    args = ap.parse_args()
    known_path = Path(args.known)
    known = set()
    if known_path.is_file():
        known = {l.strip() for l in known_path.read_text().splitlines()
                 if l.strip() and not l.startswith("#")}
    failed, finished, out = run_suite()
    if not finished:
        sys.stdout.write(out[-20000:])
        print("gate-known-failures: the suite did not finish (no 'Ran N tests' line)")
        return 1
    if args.update:
        header = ("# Python tests failing on main when the queue gate began (card #3MH4).\n"
                  "# The gate tolerates these and fails on any other. Remove a line once it passes.\n")
        known_path.write_text(header + "".join(sorted(t + "\n" for t in failed)))
        print("gate-known-failures: wrote %d known failure(s) to %s" % (len(failed), known_path))
        return 0
    new = sorted(failed - known)
    fixed = sorted(known - failed)
    print("gate-known-failures: %d failed, %d known, %d new, %d known now passing"
          % (len(failed), len(failed & known), len(new), len(fixed)))
    for t in fixed:
        print("  now passing (remove from %s): %s" % (known_path.name, t))
    if new:
        # The suite's own output for each new failure, so the author sees why.
        sys.stdout.write(out[-60000:])
        for t in new:
            print("  NEW FAILURE: %s" % t)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
