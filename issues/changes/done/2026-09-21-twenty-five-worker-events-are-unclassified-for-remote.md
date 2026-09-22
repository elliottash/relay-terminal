---
id: WEVT
type: work
status: done
labels: [bug, remote]
component: [remote, worker]
priority: 2
rank: m
created: '2026-09-21'
source: 'Claude Code session on #PH0N, 2026-09-21: found by `tests/test_remote_wire.py` while landing Phase 2.6'
links: {plans: [], commits: [e4bfa994], evidence: [docs/qa_evidence/2026-09-22-hg26-verification/report.md], related: [PH0N, W5N2]}
---
# Twenty-five worker events are unclassified for remote forwarding, so `test_remote_wire` fails on main

## Issue

`RELAY_KEYRING=off python3 -m unittest tests.test_remote_wire` fails on main (`954c9f9f` and
`8e9b2e05`, clean exports) in `AllowListTests.test_every_worker_event_is_classified`: 25 worker
event types are in neither `FORWARDED_EVENTS` nor `WITHHELD_EVENTS` in `remote/wire.py`:

`app_catalog_updated`, `app_command`, `board_cards`, `board_chat_cancelled`, `board_chat_queued`,
`board_chat_started`, `board_chat_state`, `board_search`, `board_survey`,
`custom_provider_deleted`, `custom_provider_saved`, `custom_providers`, `loop_check`,
`loop_detected`, `profile`, `recitation`, `signal_thread`, `signals_*`, `tests_*`, `usage_limits`.

They were added to the sessions protocol by the Switchboard, signals, tests-store, custom-provider
and app-command work of 2026-09-19/20 without the remote classification the test exists to force.
Nothing is leaked today (an unlisted event is not forwarded), but each needs a decision — forwarded
to devices, or withheld with the reason — by the session that owns the event, and until then the
suite is red for everyone who runs it. Fix shape: one line per event in `remote/wire.py`; most are
desktop-local administration or board state and belong in `WITHHELD_EVENTS`.

## Execution Summary
Already fixed by e4bfa994 (release protocol policy gates). Verified current main: `RELAY_KEYRING=off python3 -m unittest tests.test_remote_wire` passes all 48 tests. The former WCLS id is now WEVT. No implementation duplicated.

## Tests
- `tests/test_remote_wire.py`

### Check 2026-09-22 12:58
- passed · unittest:tests.test_remote_wire — tests/test_remote_wire.py passed for this revision on spark-dcc9, 2026-09-22T16:58:18Z
- notice · unittest:tests.test_remote_wire — tests/test_remote_wire.py: 9 of 48 are skipped for good (test_the_client_resumes_under_the_hubs_own_stream_names, test_a_plain_link_parses, test_percent_encoded_separators_still_parse…)
- warning · card — none of the listed tests is named after anything this card changed (issues/changes/2026-09-21-one-look-at-the-pairing-dialog-costs-several-pairing-rooms.md, issues/changes/2026-09-21-the-remote-audit-log-misses-invites-knocks-admits-and-guest-prompts.md, issues/changes/2026-09-21-twenty-five-worker-events-are-unclassified-for-remote.md…)
history: thread

## QA checklist
- [x] Independently reran all 48 remote-wire tests, including exhaustive worker event classification; no skips.

## Verdict
PASS, 2026-09-22. Existing fix verified; no implementation duplicated. Evidence: docs/qa_evidence/2026-09-22-hg26-verification/report.md
