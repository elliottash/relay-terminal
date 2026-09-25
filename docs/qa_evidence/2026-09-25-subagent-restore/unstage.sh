#!/usr/bin/env bash
# Card #12JX — remove the Try it sandbox. Close the staged Relay window first.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
rm -rf "$HERE/sandbox"
echo "sandbox removed"
