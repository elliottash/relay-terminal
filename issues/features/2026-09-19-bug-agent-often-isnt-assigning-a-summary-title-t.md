---
id: 8NCF
type: work
status: needs-verification
assignee: agent
implemented_by: openai/gpt-6-astra via codex
session: b7a78bf3-f266-41da-a607-1a88d0594bc1
priority: 2
rank: zzzzzzzzzzzzzzi
created: '2026-09-19'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-title-retries/], related: [], github: null}
---
# bug? agent often isnt assigning a summary title to the pane header.

## Issue
bug? agent often isnt assigning a summary title to the pane header.

## Done means
After a pane's first finished turn, and at each cadence point after, the pane header shows a model-written summary title instead of the first-prompt fallback. A title side call that fails or returns nothing usable leaves a log line saying why, and the next turn end tries again instead of silently marking the cadence as covered for another five turns.

Failure looks like the reported symptom: pane headers stuck on first-prompt text with nothing in the log explaining why no model title ever landed.

## Plan
## Goal
Pane headers reliably get a model-written title at the cadence `titles.due` already defines. Today a failed or unusable title call is swallowed silently and marks the cadence point as covered, so the pane keeps the first-prompt fallback title for at least five more turns — with no log line to explain it. Make failures visible and retried.

## Findings
- `backend/relay_core/session_protocol.py::SessionCommands.observe` calls `maybe_title()` on every turn end (done/error/cancelled); `maybe_title` claims via `agent.claim_title()` and runs `titles.generate()` on a chores-role side provider, off-thread.
- Every failure path is silent: `except Exception: provider = None` when the side provider cannot be built, and `except Exception: text = ""` around the call itself. Nothing is logged — compare the guest tail, which uses `logs.event` on failure.
- `backend/relay_core/agent.py::release_title` (lines ~3645–3659): on an empty result it still sets `title_turn = claim["turns"]` and clears `_title_stale` — the failure counts as a success for cadence purposes, so the next attempt is `REFRESH_TURNS` (5) turns away (`titles.due`, `backend/relay_core/titles.py`).
- `titles.generate` returns "" for an unusable reply (no JSON, too short after `clean`), also with no log.
- `agent.title_due` also requires `track_requests` (fine for panes) and skips guest-harness specifics: on a guest pane (protocol 29.3) the harness "serves the pane's turns and nothing else" (agent.py:1580), so a chores call may always fail there — worth confirming while logging is added.
- GUI side unverified: no `session_title` handler appears anywhere in `src/` except a comment in `src/PaneTitles.h`. `src/Pane.h` (the terminal pane, >128 KiB) could not be searched in the Plan turn — Execute must check whether the pane consumes the `session_title` event and applies it to the header (`grep -n session_title src/Pane.h`).
- Existing tests: `tests/test_titles.py`, `tests/test_sessions.py` (paired title/summary save), `tests/panetitles_test.cpp` (target `relay-titles-tests`).

## Steps
1. Confirm the GUI consumes `session_title` (grep `src/Pane.h`, the worker event dispatch). If the pane drops the event, that is the bug — fix it first; the worker-side steps below still apply.
2. In `backend/relay_core/agent.py::release_title`, do not advance `title_turn` (and do not clear `_title_stale`) when the call produced no usable text, so the next turn end retries. Keep the existing fallback-title event so the header still updates on success.
3. Add failure logging with `logs.event` (one line, reason only, no transcript text) in `session_protocol.maybe_title` (provider-build failure, call exception) and in `titles.generate` (unusable reply), so "often" becomes diagnosable after this ships.
4. Note the guest-harness case in the log message when `side_provider` fails on a guest pane, if that is what step 1/3 reveals; no behaviour change for guests beyond visibility unless the cause is trivial.
5. Extend `tests/test_titles.py` / `tests/test_sessions.py`: a failed first title is retried at the next turn end; a successful one is not; an unusable model reply keeps the fallback title and still schedules the retry.

## Risks
- Retrying on every turn end after a persistent failure adds one cheap side call per turn; bounded by the chores role, but if the owner would rather back off (e.g. retry once, then keep the 5-turn cadence), that is a one-line choice — flagged here, default is retry each turn end until a title lands.
- Do not change `REFRESH_TURNS` or the summary cadence; the summary rides `maybe_summary()` on the same hook and must not be affected.

## Verify
- `python3 -m pytest tests/test_titles.py tests/test_sessions.py -q` (plus the new cases).
- `scripts/relay-build` then `ctest --test-dir build -R titles`.
- Live under Xvfb with an isolated `XDG_CONFIG_HOME`: run a pane through one turn and see a model title replace the fallback in the header; break the chores role (bad key) and confirm a log line appears and the next turn end retries.

## Tests
- `tests/test_titles.py`
- `tests/test_sessions.py`
- `ctest -R titles`
- manual: docs/qa_evidence/2026-09-22-title-retries/README.md

### Check 2026-09-22 00:49
- passed · unittest:tests.test_titles — tests/test_titles.py passed for this revision on spark-dcc9, 2026-09-22T04:49:36Z
- passed · unittest:tests.test_sessions — tests/test_sessions.py passed for this revision on spark-dcc9, 2026-09-22T04:49:36Z
- passed · ctest:titles — ctest -R titles passed for this revision on spark-dcc9, 2026-09-22T04:49:35Z
- not-applicable · manual:docs/qa_evidence/2026-09-22-title-retries/README.md — manual evidence, recorded by hand: docs/qa_evidence/2026-09-22-title-retries/README.md
- notice · unittest:tests.test_sessions — tests/test_sessions.py: 1 of 58 are slow (test_a_running_turn_is_written_so_it_can_be_found)
- notice · unittest:tests.test_sessions — tests/test_sessions.py: 4 of 58 are not in the project any more (test_exit_plan_mode_approval_enables_edits_in_the_same_turn, test_exit_plan_mode_cancellation_never_enables_edits, test_exit_plan_mode_question_limit_does_not_authorize_execution…)
history: thread
## Execution Summary
Failed title calls retain the owed cadence and fallback, retrying on the next turn end. Added reason-only title_failed logging for provider setup/call failures, unavailable guest side providers, empty transcripts and unusable replies. Invalid title JSON no longer becomes header text. User titles and successful title/summary cadences remain unchanged.

Pane.h already consumes session_title; no GUI code change was needed. Evidence: docs/qa_evidence/2026-09-22-title-retries/. The isolated Xvfb protocol fixture visibly shows the model title; actual failure/recovery is exercised with scripted providers in the worker tests, not live paid-provider calls.
