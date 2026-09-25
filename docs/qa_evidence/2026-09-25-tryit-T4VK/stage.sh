#!/bin/sh
# Stage #T4VK: print the timeline of a steered message reaching an agent parked in agent_wait.
# No model, no network, no UI: the scripted-provider harness from tests/test_subagents.py.
# Run twice if you like; it cleans up after itself.
set -e
cd "$(dirname "$0")/../../.."
PYTHONPATH=backend python3 docs/qa_evidence/2026-09-25-tryit-T4VK/demo.py
