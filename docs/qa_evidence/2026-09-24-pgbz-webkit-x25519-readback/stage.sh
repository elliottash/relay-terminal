#!/usr/bin/env bash
# #PGBZ phone recheck — staging. Nothing is faked: the judgement happens on the
# affected iPhone over the real network. This script only guards that the desktop
# checkout actually serves the landed fix, then prints the recheck steps.
set -euo pipefail
cd "$(dirname "$0")/../../.."

grep -q "sealNonextractable" app/rrp.js \
  || { echo "app/rrp.js on disk does not hold the #PGBZ sealed-key fix"; exit 1; }
git merge-base --is-ancestor 9c6bea96b759e7012bdc9f8c5a71cad820044ddc HEAD \
  || { echo "commit 9c6bea96 is not on this checkout's HEAD"; exit 1; }

cat <<'EOF'
The web app your desktop serves (app/rrp.js, served from this checkout) holds
the #PGBZ fix. The desktop's remote host must have been (re)started after the
fix landed for the phone to load it; the phone should reload the pairing page
so it does not run a cached copy.

On the desktop: open Relay's Remote pane and bring up the pairing QR/link (the
same screen you used when the bug happened).

On the affected iPhone:
  1. Open the pairing link in Safari and pair. The old error ("this browser
     could not keep the pairing key...") must not appear.
  2. Fully close Safari (swipe it away in the app switcher) and open the link
     again: the app should reconnect to the desktop without asking to pair.
  3. Optional cross-check: if you pair from a Private Window (fresh storage),
     that is the "genuinely lost key" route — pairing should still either work
     or refuse with the clear error, never hang or silently half-pair.

Record what happened as docs/qa_evidence/2026-09-24-pgbz-webkit-x25519-readback/
observed.md (screenshots welcome) and answer the card's Human QA question.
EOF
