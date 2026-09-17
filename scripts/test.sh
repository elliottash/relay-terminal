#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
PYTHONPATH="$PWD/backend${PYTHONPATH:+:$PYTHONPATH}" python3 -m unittest discover -s tests -v
