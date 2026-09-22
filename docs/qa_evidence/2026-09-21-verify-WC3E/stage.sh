#!/usr/bin/env bash
# Verification of #WC3E ("Expectations before implementation, and a verification record written by
# a separate session"), rerunnable.  It never touches the shared checkout: it exports the revision
# under test into a scratch directory and does everything there, because the working tree holds
# other sessions' uncommitted code.
#
#   docs/qa_evidence/2026-09-21-verify-WC3E/stage.sh [revision] [output directory]
#
# Default revision 486852e0 (the card's own move to needs-verification; the code is 0ef3ee13).
# Writes unittest.txt, ctest.txt, drives.txt and policy-regen.txt beside this script.
set -u
REV="${1:-486852e0}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${2:-$HERE}"
REPO="$(git -C "$HERE" rev-parse --show-toplevel)"
X="$(mktemp -d "${TMPDIR:-/tmp}/verify-WC3E.XXXX")"
trap 'echo "export kept at $X"' EXIT
git -C "$REPO" archive "$REV" | tar -x -C "$X"
export PYTHONPATH="$X/backend:$X/tests" RELAY_KEYRING=off QT_QPA_PLATFORM=offscreen
echo "revision $REV, export $X"

# 1. the four test modules the card names
( cd "$X" && python3 -m unittest tests.test_board_tools tests.test_board_protocol \
      tests.test_board tests.test_system_prompt ) > "$OUT/unittest.txt" 2>&1
echo "unittest: $(tail -1 "$OUT/unittest.txt")"

# 2. the request/response reproduction: the two rules, driven through the tools and the wire
{
  echo "### drive-human-qa.py — the Human QA gate and the Plan turn's two sections"
  python3 "$HERE/drive-human-qa.py" "$X"
  echo
  echo "### drive-sections.py — what \`check\` warns on"
  python3 "$HERE/drive-sections.py" "$X"
  echo
  echo "### drive-execute-notice.py — Execute on a card with and without \`## Done means\`"
  python3 "$HERE/drive-execute-notice.py" "$X"
} > "$OUT/drives.txt" 2>&1

# 3. issues/POLICY.md is what `relay-board.py policy` generates from board_policy.md
{
  cp "$X/issues/POLICY.md" "$X/POLICY.committed.md"
  ( cd "$X" && PYTHONPATH="$X/backend" python3 scripts/relay-board.py policy )
  echo "-- diff committed vs regenerated --"
  diff "$X/POLICY.committed.md" "$X/issues/POLICY.md" && echo "IDENTICAL"
} > "$OUT/policy-regen.txt" 2>&1
echo "policy: $(tail -1 "$OUT/policy-regen.txt")"

# 4. the C++ assertion on the Execute and Verify briefs
cmake -S "$X" -B "$X/build" -DCMAKE_BUILD_TYPE=RelWithDebInfo > "$X/configure.log" 2>&1
cmake --build "$X/build" --target relay-board-tests relay-boardpane-tests -j"${RELAY_JOBS:-16}" \
      > "$X/build.log" 2>&1
( cd "$X/build" && ctest -R '^(board|boardpane)$' --output-on-failure ) > "$OUT/ctest.txt" 2>&1
echo "ctest: $(grep -m1 'tests passed' "$OUT/ctest.txt")"
