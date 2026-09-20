---
id: WCLS
type: work
status: inbox
labels: [bug, remote]
component: [remote, worker]
rank: m
created: '2026-09-21'
source: 'Claude Code session on #PH0N, 2026-09-21: found by `tests/test_remote_wire.py` while landing Phase 2.6'
links: {plans: [], commits: [], evidence: [], related: [PH0N, W5N2]}
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
