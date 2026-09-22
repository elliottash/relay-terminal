#!/usr/bin/env bash
# x.sh <shot-name> <sleep> [xdotool args...]
set -uo pipefail
S=/tmp/claude-1000/-home-elliott-repos-relay-terminal/846bff2a-de91-4203-bbc2-bc35f5894e19/scratchpad/pr4q-verify
source $S/gui.env
name=$1; nap=$2; shift 2
if (( $# )); then xdotool "$@"; fi
sleep "$nap"
import -window root "$OUT/$name.png"
echo "shot $OUT/$name.png"
