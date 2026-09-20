# SPDX-License-Identifier: AGPL-3.0-or-later
"""`profile_run` / `profile_stop` on the board worker (protocol 31.9, card #7BM4 phase 5).

Nothing here runs a real profiler.  Each test writes a throwaway project with its own
`scripts/relay-profile` — a few lines of bash that print the progress lines the real one prints
and leave the same `rows.json` and `meta.json` behind — which is exactly how `ProfileCommands`
finds the script, so the resolution, the argument vector, the streaming and the summary are all
under test without a compiler, a `perf` or a network.
"""
import json
import os
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "backend"))

from relay_core import board_protocol as BP          # noqa: E402
from relay_core import profile_convert as C          # noqa: E402
from relay_core import profile_protocol as PP        # noqa: E402

ROWS = {"kind": "build", "wall": 12.5, "steps": 3, "sum": 20.0, "total_rows": 3,
        "rows": [{"name": "src/main.cpp.o", "self": 18.0, "total": 18.0,
                  "self_pct": 90.0, "total_pct": 90.0},
                 {"name": "src/other.cpp.o", "self": 1.5, "total": 1.5,
                  "self_pct": 7.5, "total_pct": 7.5},
                 {"name": "relay", "self": 0.5, "total": 0.5, "self_pct": 2.5,
                  "total_pct": 2.5}]}
META = {"target": "build", "host": "spark", "commit": "abc1234",
        "started": "2026-09-20T10:00:00Z", "finished": "2026-09-20T10:00:12Z",
        "command": "scripts/relay-profile build", "raw": ["ninja_log", "build.trace.json"],
        "tool_versions": {"ninja": "1.11.1"}}

FAKE = r"""#!/usr/bin/env bash
out=
while [[ $# -gt 0 ]]; do
    case $1 in
        --out) out=$2; shift 2 ;;
        *) shift ;;
    esac
done
echo "relay-profile: configuring"
echo "relay-profile: project is $RELAY_PROFILE_PROJECT"
echo "relay-profile: building target relay"
mkdir -p "$out"
cp "$FAKE_ROWS" "$out/rows.json"
cp "$FAKE_META" "$out/meta.json"
: > "$out/build.trace.json"
echo "relay-profile: $out"
"""

SLOW = r"""#!/usr/bin/env bash
echo "relay-profile: starting"
sleep 30
echo "relay-profile: never"
"""

FAILING = r"""#!/usr/bin/env bash
echo "relay-profile: ninja is not installed (scripts/relay-tooling-setup --install)" >&2
exit 127
"""


class Harness:
    """A project with a fake `scripts/relay-profile`, and the events its worker emitted."""

    def __init__(self, body=FAKE, rows=None, meta=None):
        self.dir = tempfile.TemporaryDirectory(prefix="relay-profile-")
        self.root = Path(self.dir.name)
        (self.root / "scripts").mkdir()
        script = self.root / "scripts" / "relay-profile"
        script.write_text(body)
        script.chmod(0o755)
        (self.root / "rows.json").write_text(json.dumps(rows if rows is not None else ROWS))
        (self.root / "meta.json").write_text(json.dumps(meta if meta is not None else META))
        os.environ["FAKE_ROWS"] = str(self.root / "rows.json")
        os.environ["FAKE_META"] = str(self.root / "meta.json")
        self.events = []
        self.lock = threading.Lock()
        self.commands = PP.ProfileCommands(self.root, self.record)

    def record(self, event):
        with self.lock:
            self.events.append(event)

    def close(self):
        self.commands.shutdown()
        self.dir.cleanup()

    def wait(self, state, seconds=30.0):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            with self.lock:
                for event in self.events:
                    if event.get("state") == state:
                        return event
            time.sleep(0.02)
        raise AssertionError(f"no {state!r} event in {self.states()}")

    def states(self):
        with self.lock:
            return [event.get("state") for event in self.events]

    def lines(self):
        with self.lock:
            return [event.get("line") for event in self.events if event.get("state") == "progress"]


class ProfileRunTests(unittest.TestCase):
    def setUp(self):
        self.h = Harness()
        self.addCleanup(self.h.close)

    def test_started_progress_finished(self):
        self.h.commands.dispatch({"type": "profile_run", "target": "build", "id": "r1"})
        started = self.h.wait("started")
        self.assertEqual(started["event"], "profile")
        self.assertEqual(started["target"], "build")
        self.assertEqual(started["id"], "r1")
        self.assertTrue(started["out"].endswith("-profile-build"))
        finished = self.h.wait("finished")
        self.assertIn("relay-profile: building target relay", self.h.lines())
        # The script lives in Relay's checkout; the project it profiles is the board's.
        self.assertIn(f"relay-profile: project is {self.h.root}", self.h.lines())
        summary = finished["summary"]
        self.assertEqual(summary["kind"], "build")
        self.assertEqual(len(summary["rows"]), 3)
        self.assertEqual(summary["rows"][0]["name"], "src/main.cpp.o")
        self.assertEqual(summary["host"], "spark")
        self.assertIn("3 steps", summary["line"])
        self.assertIn("| `src/main.cpp.o` | 18.0 | 90.0% |", summary["markdown"])
        self.assertTrue(summary["flame"].endswith("build.trace.json"))
        # The evidence directory is the convention, under the project it profiled.
        self.assertTrue(Path(summary["out"]).is_dir())
        self.assertEqual(Path(summary["out"]).parent.name, "qa_evidence")

    def test_second_run_is_refused_while_one_is_in_flight(self):
        slow = Harness(SLOW)
        self.addCleanup(slow.close)
        slow.commands.dispatch({"type": "profile_run", "target": "build"})
        slow.wait("started")
        slow.wait("progress")
        slow.commands.dispatch({"type": "profile_run", "target": "tests"})
        error = slow.wait("error")
        self.assertIn("already running", error["message"])
        self.assertEqual(slow.states().count("started"), 1)

    def test_stop_ends_the_run(self):
        slow = Harness(SLOW)
        self.addCleanup(slow.close)
        slow.commands.dispatch({"type": "profile_run", "target": "app"})
        slow.wait("progress")
        slow.commands.dispatch({"type": "profile_stop"})
        stopped = slow.wait("stopped", seconds=20)
        self.assertEqual(stopped["message"], "the profile was stopped")
        self.assertFalse(slow.commands.running())

    def test_stop_with_nothing_running_says_so(self):
        self.h.commands.dispatch({"type": "profile_stop"})
        self.assertIn("no profile running", self.h.wait("error")["message"])

    def test_a_script_that_fails_reports_its_own_last_words(self):
        bad = Harness(FAILING)
        self.addCleanup(bad.close)
        bad.commands.dispatch({"type": "profile_run", "target": "build"})
        error = bad.wait("error")
        self.assertIn("ninja is not installed", error["message"])
        self.assertNotIn("summary", error)

    def test_unknown_target_is_refused_by_name(self):
        self.h.commands.dispatch({"type": "profile_run", "target": "everything"})
        message = self.h.wait("error")["message"]
        self.assertIn("build-remote", message)
        self.assertIn("app", message)

    def test_build_remote_needs_a_host_named_by_the_user(self):
        self.h.commands.dispatch({"type": "profile_run", "target": "build-remote"})
        self.assertIn("host", self.h.wait("error")["message"])
        self.h.commands.dispatch({"type": "profile_run", "target": "build-remote",
                                  "host": "not a host!"})
        errors = [e for e in self.h.events if e.get("state") == "error"]   # wait() finds the first
        self.assertEqual(len(errors), 2)
        self.assertIn("not a hostname", errors[-1]["message"])
        self.h.commands.dispatch({"type": "profile_run", "target": "build-remote",
                                  "host": "laptop.local"})
        started = self.h.wait("started")
        self.assertIn("--host laptop.local", started["command"])
        # Nothing in the module names a machine: the host is the request's or the environment's.
        self.assertNotIn("sphinx", open(PP.__file__).read())

    def test_arguments_are_checked_by_name(self):
        self.assertEqual(PP.ProfileCommands._extra(["--stop-after", "30"]), ["--stop-after", "30"])
        self.assertEqual(PP.ProfileCommands._extra(["--time-trace"]), ["--time-trace"])
        with self.assertRaises(PP.ProfileError):
            PP.ProfileCommands._extra(["--binary", "/bin/sh"])
        with self.assertRaises(PP.ProfileError):
            PP.ProfileCommands._extra(["; rm -rf /"])
        with self.assertRaises(PP.ProfileError):
            PP.ProfileCommands._extra(["--out", "/etc"])

    def test_extra_arguments_reach_the_script(self):
        # The fake echoes nothing, so this is checked on the command the run records.
        run = self.h.commands.start("app", ["--stop-after", "5"])
        self.assertIn("--stop-after 5", run.command)
        self.assertIn(" app ", f" {run.command} ")
        run.finished.wait(30)     # let it end before the harness takes its directory away

    def test_no_rows_is_not_a_finish(self):
        empty = Harness(FAKE, rows={"kind": "build", "rows": []})
        self.addCleanup(empty.close)
        empty.commands.dispatch({"type": "profile_run", "target": "build"})
        self.assertIn("wrote no summary", empty.wait("error")["message"])


class WireTests(unittest.TestCase):
    def test_board_protocol_knows_the_same_two_requests(self):
        self.assertEqual(set(BP.PROFILE_TYPES), set(PP.TYPES))
        self.assertTrue(set(PP.TYPES) <= BP.TYPES)

    def test_every_target_has_a_label_and_a_line_of_its_own(self):
        for name, target in PP.TARGETS.items():
            self.assertTrue(target["label"], name)
            self.assertGreater(len(target["detail"]), 40, name)
            self.assertEqual(target["args"][0], name.split("-")[0])

    def test_summary_line_of_a_sampled_profile(self):
        folded = {"kind": "profile", "wall": 61.0, "samples": 400, "total_rows": 12, "rows": []}
        line = PP.ProfileCommands.summary_line(folded, {"host": "spark"})
        self.assertEqual(line, "12 functions · 400 samples · 1m 1s · spark")

    def test_markdown_is_the_converter_s_own_table(self):
        text = PP.ProfileCommands.markdown(ROWS, META)
        self.assertIn("### build · 2026-09-20 10:00 · spark · `abc1234`", text)
        self.assertIn(C.markdown_table(ROWS), text)


if __name__ == "__main__":
    unittest.main()
