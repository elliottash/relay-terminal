#!/usr/bin/env bash
# Deploy the relay-terminal.ai landing site.
#
# Hosting: nginx on elliott-main-1 (138.201.189.28) behind a Cloudflare Tunnel;
# docroot /opt/relay-terminal/site. Modelled on ~/repos/relit/deploy.sh.
#
# This script only ever writes files into an existing docroot. It touches no
# nginx, tunnel, or DNS configuration — that server hosts a dozen unrelated
# sites through one nginx instance, where a listener mistake takes all of them
# down. See ~/admin/web-sites/RELAY.md before changing anything server-side.
#
# Usage:
#   ./deploy.sh -n    show what would upload, then stop
#   ./deploy.sh       show the dry run, then deploy and verify
set -euo pipefail
cd "$(dirname "$0")"

DEST="deploy@138.201.189.28:/opt/relay-terminal/site/"
# --checksum, not --size-only: same-size edits must not be skipped.
# --delete keeps the docroot an exact mirror of site/.
FLAGS=(-rltz --checksum --delete --chmod=D755,F644 -e ssh)

# -i on the dry run: rsync is silent without it, so an unreviewed "dry run"
# that prints nothing is indistinguishable from one that would delete the site.
echo "Would transfer:"
rsync -n -i "${FLAGS[@]}" site/ "$DEST"
if [[ "${1:-}" == "-n" ]]; then
  echo "Dry run only. Run ./deploy.sh to deploy."
  exit 0
fi
rsync "${FLAGS[@]}" site/ "$DEST"

echo "Deployed. Verifying..."
for url in "https://relay-terminal.ai/" "https://www.relay-terminal.ai/"; do
  code=$(curl -sS -o /dev/null -w '%{http_code}' "$url")
  printf '  %-34s %s\n' "$url" "$code"
  [[ "$code" == "200" ]] || { echo "FAILED: $url returned $code"; exit 1; }
done
# Fetch first, then search: `curl | grep -q` fails under pipefail even on a match, because grep
# exits at the first hit and curl dies writing to the closed pipe (curl: (23)).
page=$(curl -sS https://relay-terminal.ai/)
grep -qi "relay" <<<"$page" \
  || { echo "FAILED: homepage missing expected content"; exit 1; }
echo "Verified at https://relay-terminal.ai"
