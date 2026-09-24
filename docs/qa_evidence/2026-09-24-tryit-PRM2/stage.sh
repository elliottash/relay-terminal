#!/usr/bin/env bash
# Stages #PRM2's Try it: renders the real pairing dialog in its two states
# (credentials live, and after the rendezvous refused with 429) and prints
# what to open. No network, no rendezvous; rerunnable.
set -euo pipefail
mkdir -p /tmp/claude-1000/tryit
here="$(cd "$(dirname "$0")" && pwd)"
python3 "$here/try_run.py"
echo "Open: docs/qa_evidence/2026-09-24-tryit-PRM2/01-pairing-ok.png"
echo "Then: docs/qa_evidence/2026-09-24-tryit-PRM2/02-after-429.png"
