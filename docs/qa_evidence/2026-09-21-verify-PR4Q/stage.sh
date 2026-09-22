#!/usr/bin/env bash
# Put back the situation #PR4Q was verified in (verifier session, 2026-09-21).
#
#   ./stage.sh [<export dir>] [<staged project dir>]
#
# It makes a clean export of the revision that was checked, stages the #7BM4 orders project in
# it (a passing check, a failing one, a retired one and one that has never run, with ten days of
# history from two machines), and prints the three commands the verification used: the two
# unittest modules, `ctest -R cardtests`, and the backend drives under `drives/`.
set -euo pipefail
REV=${REV:-d6682f97}
REPO=${REPO:-/home/elliott/repos/relay-terminal}
X=${1:-$(mktemp -d /tmp/claude-1000/v-pr4q.XXXX)}
WORK=${2:-/tmp/claude-1000/v-pr4q-orders}

mkdir -p "$X"
git -C "$REPO" archive "$REV" | tar -x -C "$X"
export PYTHONPATH="$X/backend:$X/tests" RELAY_KEYRING=off
python3 "$X/docs/qa_evidence/2026-09-20-switchboard-tooling-hub/scenario/stage.py" --dir "$WORK" --fresh

cat <<TXT

Export:  $X
Staged:  $WORK   (cards in $WORK/STAGED.json: done / flaky / slow)

The tests:
  cd $X && PYTHONPATH=$X/backend:$X/tests RELAY_KEYRING=off \\
      python3 -m unittest tests.test_test_history tests.test_tests_protocol
  cmake -S $X -B $X/build -DCMAKE_BUILD_TYPE=RelWithDebInfo
  cmake --build $X/build --target relay relay-cardtests-tests -j16
  ctest --test-dir $X/build -R '^cardtests\$' -V

The backend drives (this folder, drives/): each prints one transcript, in order.
  for d in $(dirname "$0")/drives/drive[1-6].py; do X=$X PYTHONPATH=$X/backend python3 "\$d"; done

The GUI drive: gui-drive.sh in this folder, RELAY_BIN=$X/build/relay.
TXT
