#!/usr/bin/env bash
# Stage the Try it fixture for card #7BM4: the `orders` project, in which the three problems the
# card was written to solve are actually happening (an "all tests pass" card with a failing, a
# deleted and a never-run test; a test that flakes at the same commit on two machines; one file
# that is most of the build). No arguments, no model, no network; safe to run twice — it wipes
# and re-stages. The fixture is disposable: rm -rf /tmp/claude-1000/tryit when done.
set -euo pipefail
REPO=/home/elliott/repos/relay-terminal
DIR=/tmp/claude-1000/tryit/orders
python3 "$REPO/docs/qa_evidence/2026-09-20-switchboard-tooling-hub/scenario/stage.py" --dir "$DIR" --fresh
cat <<EOF

Open it in Relay:
  $REPO/build/relay --workspace $DIR
then Ctrl+Shift+S for the Switchboard. If the Test suites / Profile panes open short below the
board, widen the window or maximise the pane (the button in its header).
EOF
