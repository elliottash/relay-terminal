#!/usr/bin/env bash
# Restage the #7BM4 verification situation (Kimi K3 verify run, 2026-09-22).
# 1. Stage the fixture project (an 'orders' project whose "done" card has a failing, a deleted
#    and a never-run test, a flaky test across two hosts, and a build dominated by one file).
# 2. Play the mechanical GUI steps in a real Relay under Xvfb, xdotool only.
#
#   ./stage.sh <fixture dir> <out dir>            # stage fresh, then play every step
#   ./stage.sh <fixture dir> <out dir> --no-stage # reuse an already-staged fixture
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
work=${1:?fixture dir}; out=${2:?out dir}; mode=${3:-}
export RELAY_BIN=${RELAY_BIN:-$repo/build/relay}
if [[ $mode != --no-stage ]]; then
  python3 "$repo/docs/qa_evidence/2026-09-20-switchboard-tooling-hub/scenario/stage.py" --dir "$work" --fresh
fi
mkdir -p "$out"
# Scenario 1: the agent says it is done -> Tests strip, Check, the needs-verification gate
bash "$here/drive.sh" "$work" "$out" discover
bash "$here/drive.sh" "$work" "$out" check
CHECK_X=1548 CHECK_Y=306 STATUS_X=1532 STATUS_Y=226 bash "$here/drive.sh" "$work" "$out" gate
# Scenario 2: CI goes red once a week -> Test suites pane, flaky first, row detail
bash "$here/drive.sh" "$work" "$out" tests
ROW1_X=870 ROW1_Y=839 bash "$here/drive.sh" "$work" "$out" detail
# Scenario 3: the build got slow -> Profile menu, Build (this machine), result table
bash "$here/drive.sh" "$work" "$out" profile
BUILD_X=1180 BUILD_Y=724 BUILD_WAIT=75 bash "$here/drive.sh" "$work" "$out" profilerun
echo "staged and played; captures in $out"
