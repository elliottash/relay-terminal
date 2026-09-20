#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Deploy the hosted rendezvous and web app at https://join.relay-terminal.ai.
#
# Hosting: elliott-main-1 (138.201.189.28), systemd unit `relay-rendezvous`, code under
# /opt/relay-rendezvous (root-owned: remote/, rendezvous/, app/), database under
# /var/lib/relay-rendezvous, served on 127.0.0.1:8791 straight from a Cloudflare Tunnel.
# There is no nginx vhost for this service, so nothing here can touch the shared nginx that
# serves a dozen other sites on the same box; and nothing here touches /opt/relay, which is
# the Relay Free gateway.
#
# It deploys from a **clean export of a git revision**, never from the working tree: this
# checkout is shared by several sessions and usually holds somebody's half-finished edit to
# app/*.js. What is deployed is therefore always exactly what is on a branch.
#
# Usage:
#   rendezvous/deploy.sh                 export main, deploy, restart, verify
#   rendezvous/deploy.sh <rev>           the same from another revision (tag, sha, branch)
#   rendezvous/deploy.sh --check [rev]   verify only: compare what is served with that revision
#   rendezvous/deploy.sh -n [rev]        show what rsync would change, then stop
#   rendezvous/deploy.sh --rollback      swap /opt/relay-rendezvous.prev back in and restart
#   rendezvous/deploy.sh --install-unit  also copy the unit file and daemon-reload
#
# A restart drops every live socket through the rendezvous (phones reconnect; a share in flight
# does not). Check that nobody is attached first: the health route reports the counts.
set -euo pipefail
SELF=$(readlink -f "$0")
cd "$(dirname "$SELF")/.."

HOST="${RELAY_RENDEZVOUS_SSH:-root@138.201.189.28}"
DEST="/opt/relay-rendezvous"
PREV="/opt/relay-rendezvous.prev"
SERVICE="relay-rendezvous"
ORIGIN="${RELAY_RENDEZVOUS_ORIGIN:-https://join.relay-terminal.ai}"
LOCAL_HEALTH="http://127.0.0.1:8791/v1/health"
DIRS=(remote rendezvous app)

# Cloudflare answers 403 to curl's own User-Agent; every fetch here goes out as a browser.
CURL=(curl -sS -A "Mozilla/5.0 (relay deploy.sh)" -H "Cache-Control: no-cache" --max-time 30)

REV="main"
MODE="deploy"
DRY_RUN=0
INSTALL_UNIT=0
for arg in "$@"; do
  case "$arg" in
    --check)        MODE="check" ;;
    --rollback)     MODE="rollback" ;;
    --install-unit) INSTALL_UNIT=1 ;;
    -n|--dry-run)   DRY_RUN=1 ;;
    -h|--help)      sed -n '3,30p' "$SELF" | sed 's/^# \?//'; exit 0 ;;
    -*)             echo "unknown flag: $arg" >&2; exit 2 ;;
    *)              REV="$arg" ;;
  esac
done

say() { printf '%s\n' "$*"; }
die() { printf 'FAILED: %s\n' "$*" >&2; exit 1; }

on_box() { ssh -o BatchMode=yes "$HOST" "$@"; }

journal() {
  say ""
  say "Last 40 journal lines from $SERVICE:"
  on_box "journalctl -u $SERVICE -n 40 --no-pager" 2>&1 | sed 's/^/  /' || true
}

# ---- rollback --------------------------------------------------------------------------------

if [[ "$MODE" == "rollback" ]]; then
  say "Rolling back $DEST to $PREV on $HOST"
  # The swap is symmetric: what is live now becomes .prev, so a rollback can be rolled back.
  on_box "set -e
    [ -d '$PREV' ] || { echo 'no $PREV on the box — nothing to roll back to'; exit 1; }
    rm -rf '$DEST.swap'
    mv '$DEST' '$DEST.swap'
    mv '$PREV' '$DEST'
    mv '$DEST.swap' '$PREV'
    systemctl restart $SERVICE"
  say "Restarted. Waiting for health on the box…"
  on_box "for i in \$(seq 1 30); do
            curl -sf -m 3 '$LOCAL_HEALTH' && exit 0
            sleep 1
          done
          exit 1" || { journal; die "the rolled-back service is not healthy"; }
  say ""
  code=$("${CURL[@]}" -o /dev/null -w '%{http_code}' "$ORIGIN/v1/health")
  say "  $ORIGIN/v1/health  $code"
  [[ "$code" == "200" ]] || die "$ORIGIN/v1/health returned $code"
  say "Rolled back. The version that was live is now in $PREV."
  exit 0
fi

# ---- export ----------------------------------------------------------------------------------

SHA=$(git rev-parse "$REV") || die "no such revision: $REV"
SUBJECT=$(git log -1 --format='%s' "$SHA")
TMP=$(mktemp -d "${TMPDIR:-/tmp}/relay-rendezvous-deploy.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
git archive "$SHA" "${DIRS[@]}" | tar -x -C "$TMP"
for dir in "${DIRS[@]}"; do
  [[ -d "$TMP/$dir" ]] || die "$REV has no $dir/ — wrong revision?"
done
APP_SHA=$(sha256sum "$TMP/app/app.js" | cut -d' ' -f1)

say "Revision  $(git rev-parse --short "$SHA")  $SUBJECT"
say "app.js    $APP_SHA"
say "Host      $HOST:$DEST"
say ""

# ---- what is served now: every exported app/ file, by sha256 ----------------------------------

compare_served() {
  local rel served expected bad=0 same=0
  say "Comparing $ORIGIN with $REV:"
  while IFS= read -r rel; do
    expected=$(sha256sum "$TMP/app/$rel" | cut -d' ' -f1)
    if ! "${CURL[@]}" -f -o "$TMP/.fetched" "$ORIGIN/$rel"; then
      printf '  %-26s NOT SERVED\n' "$rel"; bad=$((bad + 1)); continue
    fi
    served=$(sha256sum "$TMP/.fetched" | cut -d' ' -f1)
    if [[ "$served" == "$expected" ]]; then
      same=$((same + 1))
    else
      printf '  %-26s DIFFERS  served %s  expected %s\n' "$rel" "${served:0:12}" "${expected:0:12}"
      bad=$((bad + 1))
    fi
  done < <(cd "$TMP/app" && find . -type f -printf '%P\n' | sort)
  rm -f "$TMP/.fetched"
  if (( bad )); then
    say "  $same file(s) match, $bad do not."
    return 1
  fi
  say "  all $same file(s) served match $REV."
}

if [[ "$MODE" == "check" ]]; then
  code=$("${CURL[@]}" -o /dev/null -w '%{http_code}' "$ORIGIN/v1/health")
  say "  $ORIGIN/v1/health  $code"
  [[ "$code" == "200" ]] || die "$ORIGIN/v1/health returned $code"
  compare_served || die "what is served is not $REV — deploy it"
  exit 0
fi

# ---- rsync -----------------------------------------------------------------------------------

# --checksum, not --size-only: a same-size edit must not be skipped.
# --delete inside the three directories only: a file dropped from the repo goes from the box too.
# No -a: -a implies -og, and rsync running as root on the receiver would then stamp this
# checkout's uid onto /opt/relay-rendezvous. --chmod gives what the service expects: root-owned,
# world-readable, directories traversable.
FLAGS=(-rltz --checksum --delete --chmod=D755,F644 --exclude='__pycache__' --exclude='*.pyc'
       -e "ssh -o BatchMode=yes")

say "Would transfer:"
for dir in "${DIRS[@]}"; do
  rsync -n -i "${FLAGS[@]}" "$TMP/$dir/" "$HOST:$DEST/$dir/" | sed "s|^|  $dir/ |"
done
if (( DRY_RUN )); then
  say ""
  say "Dry run only. Run rendezvous/deploy.sh to deploy."
  exit 0
fi

say ""
say "Keeping a rollback copy in $PREV…"
on_box "set -e; rm -rf '$PREV'; cp -a '$DEST' '$PREV'"

for dir in "${DIRS[@]}"; do
  rsync "${FLAGS[@]}" "$TMP/$dir/" "$HOST:$DEST/$dir/"
done
say "Transferred remote/, rendezvous/ and app/."

# The unit that is running is the copy in /etc/systemd/system, not the one just rsynced.
if ! on_box "cmp -s '$DEST/rendezvous/$SERVICE.service' '/etc/systemd/system/$SERVICE.service'"; then
  if (( INSTALL_UNIT )); then
    say "Installing the unit file and reloading systemd…"
    on_box "set -e
      cp '$DEST/rendezvous/$SERVICE.service' '/etc/systemd/system/$SERVICE.service'
      systemctl daemon-reload"
  else
    say ""
    say "NOTE: $REV's $SERVICE.service differs from the one installed on the box:"
    on_box "diff -u '/etc/systemd/system/$SERVICE.service' '$DEST/rendezvous/$SERVICE.service'" \
      2>&1 | sed 's/^/  /' || true
    say "  Re-run with --install-unit to copy it over and daemon-reload."
  fi
fi

say "Restarting $SERVICE…"
on_box "systemctl restart $SERVICE"

say "Waiting for health on the box…"
if ! on_box "for i in \$(seq 1 30); do
               curl -sf -m 3 '$LOCAL_HEALTH' && exit 0
               sleep 1
             done
             exit 1"; then
  journal
  say ""
  say "Rolling back…"
  "$SELF" --rollback || true
  die "$SERVICE did not come back healthy; rolled back"
fi
say ""

# ---- verify from here ------------------------------------------------------------------------

code=$("${CURL[@]}" -o /dev/null -w '%{http_code}' "$ORIGIN/v1/health")
say "  $ORIGIN/v1/health  $code"
if [[ "$code" != "200" ]]; then
  journal
  say ""
  say "Rolling back…"
  "$SELF" --rollback || true
  die "$ORIGIN/v1/health returned $code; rolled back"
fi

if ! compare_served; then
  journal
  die "deployed, and the service is healthy, but what is served is not $REV"
fi

say ""
say "Deployed $(git rev-parse --short "$SHA") to $ORIGIN  (app.js ${APP_SHA:0:12})"
say "Rollback with: rendezvous/deploy.sh --rollback"
