#!/usr/bin/env bash
# /update (card #HDA9) evidence: everything short of installing over this machine's Relay.
#
#   ./drive.sh [repo-root]
#
# 1. The decision functions are unit-tested offline (tests/test_update.py).
# 2. The app that runs the updater still compiles (CMake target relay).
# 3. `check` on this source-checkout machine refuses the .deb path — the guard that keeps a
#    source install from being shadowed by /usr.
# 4. The full download path against the live GitHub release, with dpkg made to report an
#    installed beta.1: discovery → the right asset for ubuntu24.04/arm64 → SHA256SUMS
#    verification → the dry-run stop before pkexec.
set -euo pipefail
cd "${1:-$(dirname "$BASH_SOURCE")/../../..}"
echo "== unit tests =="
python3 -m unittest tests.test_update -v 2>&1 | tail -3
echo "== build the app =="
cmake --build build --target relay -j"$(nproc)" >/dev/null
echo "relay built"
echo "== check on a source checkout =="
python3 scripts/relay-update.py check || echo "exit=$? (expected 1: no dpkg package here)"
echo "== live dry-run, installed version faked to beta.1 =="
python3 - <<'EOF'
import importlib.util
spec = importlib.util.spec_from_file_location("relay_update", "scripts/relay-update.py")
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
m.installed_deb_version = lambda package="relay": "0.1.0~beta.1-1~ubuntu24.04"
raise SystemExit(m.install_command("ubuntu24.04", "aarch64", dry_run=True, restart=False))
EOF
echo "exit=$?"
