#!/usr/bin/env bash
set -euo pipefail
# Recreate the independent fixture. Build only via the locked wrapper.
repo=$(cd "$(dirname "$0")/../../.." && pwd)
fixture=${1:-/tmp/verify-7bm4-fresh/orders-own-rerun}
python3 "$repo/docs/qa_evidence/2026-09-20-switchboard-tooling-hub/scenario/stage.py" --dir "$fixture"
printf 'Launch the checked binary with an isolated Xvfb profile: %s --workspace %s --clean-shell --fresh\n' /tmp/verify-7bm4-fresh/src/build/relay "$fixture"
# This evidence directory's drive.py records the interactive xdotool driver.
# Screenshots scenario/01-card.png through 10-attached.png record actual coordinates/results.
