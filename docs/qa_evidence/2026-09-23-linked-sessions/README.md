# One combined row for a linked guest session (#D2PX)

The Sessions index now stores the `guest` and `guest_session` fields already present in Relay session JSON. In a combined source view it suppresses a Claude or Codex guest row when a Relay session points to that exact source and id. A guest-only source view still returns the native transcript; deleting the Relay session reveals it again.

Read-only inspection of the user's 2026-09-23 index and session files found 35 Claude links and 95 Codex links whose guest rows were also indexed. The screenshot in the user report shows one Codex pair.

`PYTHONPATH=backend:tests python3 -m unittest test_conv_index` passed: 124 tests, including a case with linked Claude and Codex sessions and an in-place v6→v7 migration/backfill case. These tests use temporary databases and do not alter the user's index.
