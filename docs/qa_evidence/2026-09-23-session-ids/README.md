# Session IDs in Sessions (#S7D4)

The isolated Xvfb widget capture [sessions-with-ids.png](sessions-with-ids.png) shows an ID line beneath each session title. The first row uses a full UUID; the narrow column elides its middle, while the row tooltip retains the full ID and its context menu offers “Copy session ID.”

Verification: `RELAY_SHOT_DIR=$PWD/docs/qa_evidence/2026-09-23-session-ids xvfb-run -a build/relay-conversations-tests sessionRowsExposeFullIds` passed (3/3 QtTest checks). `scripts/relay-build --target relay-conversations-tests` succeeded.
