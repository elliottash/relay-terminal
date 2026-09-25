#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../.." && pwd)
out=$(cd "$(dirname "$0")" && pwd)
cd "$repo"
{
    date -u '+UTC %Y-%m-%d %H:%M:%S'
    echo '$ ctest --test-dir build -R ... --output-on-failure'
    ctest --test-dir build -R '^(backends|keymap|remotesession|sshconfig|relay-engine-tests)$' --output-on-failure
    echo '$ python3 tests/test_ssh_shell.py'
    python3 tests/test_ssh_shell.py
    echo '$ python3 tests/test_remote_marks.py'
    python3 tests/test_remote_marks.py
    echo 'PASS: all selected checks'
} > "$out/tests.txt" 2>&1
