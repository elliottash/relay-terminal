#!/usr/bin/env bash
# Run the six backend drives, each on its own fresh staging of the #7BM4 orders project.
#   X=<clean export> ./run.sh            (X defaults to a fresh export of the checked revision)
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
X=${X:?set X to a clean export of the revision under test}
for n in 1 2 3 4 5 6; do
  W=/tmp/claude-1000/v-pr4q-drive$n
  PYTHONPATH=$X/backend:$X/tests RELAY_KEYRING=off \
    python3 "$X/docs/qa_evidence/2026-09-20-switchboard-tooling-hub/scenario/stage.py" --dir "$W" --fresh >/dev/null
  echo "############################################ drive$n  (staged in $W)"
  X=$X WORK=$W PYTHONPATH=$X/backend RELAY_KEYRING=off python3 "$HERE/drive$n.py"
  echo
done
