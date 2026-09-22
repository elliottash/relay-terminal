#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# #JNYN verifier's staging: the throwaway `orders` project this session drove Try it in.
# Rerunnable, takes no arguments, needs no model and no network. It prints the one line that
# opens what it staged.
#
#   RELAY_BIN=<relay> bash docs/qa_evidence/2026-09-21-verify-JNYN/stage.sh [--dir DIR]
#
# What it builds: the worked example's `orders` fixture (a git repo, a Switchboard, three
# seeded cards), with card `done` in Needs verification — the lane Try it's button lives in —
# and a hand-written `## Try it` section on the `flaky` card, with its expected result sealed
# in `expected.md` beside it, so the strip, the Open it button and the answer box can be driven
# without a model.
set -uo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# REPO lets this run against a clean export; the shared checkout is mid-rename.
repo=${REPO:-$(cd "$here/../../.." && pwd)}
dir=/tmp/claude-1000/v-jnyn-orders
[[ ${1:-} == --dir ]] && dir=${2:?dir}

python3 "$repo/docs/qa_evidence/2026-09-20-switchboard-tooling-hub/scenario/stage.py" \
        --dir "$dir" --fresh >/dev/null || exit 1

PYTHONPATH="$repo/backend" RELAY_KEYRING=off REPO="$repo" DIR="$dir" python3 - <<'PY'
import json, os
from pathlib import Path
from relay_core import board as B
target = Path(os.environ["DIR"])
staged = json.loads((target / "STAGED.json").read_text())
card_id = staged["cards"]["flaky"]
root = next(p.parent for p in target.glob("*/board.yaml"))
board = B.Board(root, target)
card = board.card_by_id(card_id)
out = target / "docs" / "qa_evidence" / ("2026-09-21-tryit-" + card_id)
out.mkdir(parents=True, exist_ok=True)
(out / "expected.md").write_text(
    "It should have stopped you: `inventory_sync` passes and fails at the same commit,\n"
    "and the row says so before you open anything.\n", encoding="utf-8")
(out / "staging-notes.md").write_text(
    "Seeded fixture: the ten days of run history are invented and the two machines do not "
    "exist.\n", encoding="utf-8")
open_line = "printf 'staged: %s\\n' && ls %s" % (target, out)
section = (
    "1. Open it: `%s`\n\n"
    "2. The task: look at what the fixture put in front of you and decide, without opening "
    "anything else, whether you can tell which test is untrustworthy.\n\n"
    "3. **Did it stop you, and did you know why quickly enough that you would use it?**\n\n"
    "Expected: docs/qa_evidence/2026-09-21-tryit-%s/expected.md (sealed until you answer)\n"
    % (open_line, card_id))
body = card.body
if "## Try it" in body:
    import re
    body = re.sub(r"## Try it\n.*?(?=\n## |\Z)", "", body, flags=re.S)
card.set("status", "needs-verification")   # the lane Try it's button lives in
card.body = body.rstrip() + "\n\n## Try it\n" + section
board.save(card)
print("seeded ## Try it (needs-verification) on #%s (sealed expected.md under %s)" % (card_id, out.relative_to(target)))
PY

echo
echo "Open it:  ${RELAY_BIN:-build/relay} --workspace $dir --clean-shell   then Ctrl+Shift+S"
