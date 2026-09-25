"""`scripts/relay-qa-run` never hands QA a binary that predates the change (card #J6MF).

The incident: a session landed #234Z, ran `./build/relay` for the hand-off, and that binary had
been linked before the edit was compiled -- the fix looked missing and the session went on to
debug code that was never in the binary. relay-qa-run now builds build/relay through
scripts/relay-build and, with --check, proves the binary holds a literal the change adds.

Each test copies relay-qa-run into a throwaway root next to a stub relay-build that logs its
arguments, so nothing here builds or touches the real build/.
"""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SCRIPT = REPO / "scripts" / "relay-qa-run"

# The stub stands in for scripts/relay-build: a plain build succeeds; --check-only answers from
# the fake binary's contents, with the same named failure and exit code the real one gives.
STUB_BUILD = r"""#!/usr/bin/env bash
echo "$*" >> "$(dirname "$0")/../relay-build.log"
if [[ $1 != --check-only ]]; then exit 0; fi
shift
binary=; markers=()
while [[ $# -gt 0 ]]; do
    case $1 in
        --check) markers+=("$2"); shift 2 ;;
        --check-binary) binary=$2; shift 2 ;;
        *) shift ;;
    esac
done
for m in "${markers[@]}"; do
    if ! grep -qaF -- "$m" "$binary"; then
        echo "relay-build: binary predates the change: $binary does not contain '$m' (build id $(cat "$(dirname "$binary")/relay.build-id"); HEAD abc1234)"
        exit 5
    fi
done
exit 0
"""


class RelayQaRunTest(unittest.TestCase):
    def make_root(self):
        root = Path(tempfile.mkdtemp(prefix="relay-qa-run-test-"))
        self.addCleanup(shutil.rmtree, root, ignore_errors=True)
        (root / "scripts").mkdir()
        shutil.copy2(SCRIPT, root / "scripts" / "relay-qa-run")
        stub = root / "scripts" / "relay-build"
        stub.write_text(STUB_BUILD)
        stub.chmod(0o755)
        (root / "build").mkdir()
        binary = root / "build" / "relay"
        binary.write_text("#!/bin/sh\necho OLD-CODE-ONLY\ntouch \"$RAN\"\n")
        binary.chmod(0o755)
        (root / "build" / "relay.build-id").write_text("0924.07.3\n")
        return root

    def run_qa(self, root, *args):
        env = dict(os.environ, RAN=str(root / "ran"))
        return subprocess.run([str(root / "scripts" / "relay-qa-run"), *args], cwd=str(root),
                              env=env, capture_output=True, text=True, timeout=60)

    def build_log(self, root):
        log = root / "relay-build.log"
        return log.read_text().splitlines() if log.exists() else []

    def test_a_stale_binary_is_named_and_never_run(self):
        root = self.make_root()
        done = self.run_qa(root, "--check", "NEW-CODE-MARKER", str(root / "build" / "relay"))
        self.assertEqual(done.returncode, 5, done.stderr)
        self.assertIn("binary predates the change", done.stderr)
        self.assertIn("build id 0924.07.3", done.stderr)
        self.assertIn("HEAD abc1234", done.stderr)
        self.assertFalse((root / "ran").exists(), "the stale binary was run anyway")
        log = self.build_log(root)
        self.assertEqual(log[0], "", "build/relay was not built through relay-build first")
        self.assertTrue(log[1].startswith("--check-only --check NEW-CODE-MARKER"), log)

    def test_a_binary_holding_the_change_runs(self):
        root = self.make_root()
        done = self.run_qa(root, "--check", "OLD-CODE-ONLY", "./build/relay")
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertTrue((root / "ran").exists())
        self.assertEqual(done.stdout, "OLD-CODE-ONLY\n", "build output leaked into stdout")

    def test_a_drive_script_with_check_checks_build_relay(self):
        root = self.make_root()
        done = self.run_qa(root, "--check", "NEW-CODE-MARKER",
                           sys.executable, "-c", "open(__import__('os').environ['RAN'], 'w')")
        self.assertEqual(done.returncode, 5, done.stderr)
        self.assertFalse((root / "ran").exists())
        self.assertIn(str(root / "build" / "relay"), self.build_log(root)[1])

    def test_build_relay_without_check_is_still_built_first(self):
        root = self.make_root()
        done = self.run_qa(root, "build/relay")
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.build_log(root), [""])
        self.assertTrue((root / "ran").exists())

    def test_a_binary_outside_the_checkout_is_checked_not_built(self):
        root = self.make_root()
        with tempfile.TemporaryDirectory() as tmp:
            packaged = Path(tmp) / "relay"
            packaged.write_text("NEW-CODE-MARKER\n")
            (Path(tmp) / "relay.build-id").write_text("x\n")
            done = self.run_qa(root, "--check", "NEW-CODE-MARKER", "--check-binary",
                               str(packaged), "touch", str(root / "ran"))
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertTrue((root / "ran").exists())
        self.assertEqual(len(self.build_log(root)), 1)
        self.assertTrue(self.build_log(root)[0].startswith("--check-only"))

    def test_a_plain_probe_never_builds(self):
        root = self.make_root()
        done = self.run_qa(root, sys.executable, "-c", "print('probe')")
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(done.stdout, "probe\n")
        self.assertEqual(self.build_log(root), [])


if __name__ == "__main__":
    unittest.main()
