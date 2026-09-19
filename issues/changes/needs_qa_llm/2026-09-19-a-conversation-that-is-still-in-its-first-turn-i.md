---
id: 6GX9
type: work
status: needs-qa-llm
labels: [bug, sessions, worker, gui]
implemented_by: Claude Opus 5 (Relay agent), 2026-09-19
rank: zzzzzz
created: '2026-09-19'
source: pane, 2026-09-19 00:43
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-live-conversation-is-not-saved/], related: [CCKY, R6J0], github: null}
---
# A conversation that is still in its first turn is never saved, so the sessions list and full-text search cannot see it

## Issue
it seems like the sessions filter isnt working. i cant full text the conversation content

full text search

## The fault
`Agent.autosave()` only ran when a turn ended, so a session had **no file on disk until its first turn finished**. The sessions list and the full-text index are built from those files, so while a turn ran the conversation was absent from the Sessions pane and no word in it was searchable — and its work survived only as checkpoint blobs, so a restart or a closed pane lost the conversation.

Measured on the owner's machine at 00:53, five open panes held a session id with no file and no index row, with 1–106 tool calls behind them (d9316a7f 106, 6ffe5e5d 87, 3f3095ef 75, 051a7639 48, ab127aae 1); three had checkpoint blobs on disk, so the writes were saved and the conversation was not. Full-text search over *saved* conversations was verified working on the same data before the change: the fault is the missing file, not the query.

## The change
- `backend/relay_core/agent.py`: `Agent._autosave_soon()` writes the session while its turn is still running, at most every `MID_TURN_SAVE_S` (10 s), and is called after the user's prompt, after each model reply and after each tool result (both the subagent batch path and the ordinary one). `autosave()` stamps `_last_save`, so the throttled and the turn-end saves share one clock.
- Docs: `docs/AGENT-SESSIONS-PROTOCOL.md` 14.1 and 14.8; `docs/ARCHITECTURE.md` (the index section).
- Tests: `tests/test_sessions.py` — `test_a_running_turn_is_written_so_it_can_be_found` and `test_the_file_is_not_rewritten_on_every_tool_call` (the `SessionTests` class was run: 8 tests, OK). `./scripts/test.sh` and `ctest` were **not** run — the owner asked to skip them.
- Evidence: `docs/qa_evidence/2026-09-19-live-conversation-is-not-saved/`.

## QA checklist
1. **The conversation you are having is searchable.** Ask a pane something whose turn runs for a minute (a build, a long `run_command`), then Ctrl+Shift+Y and search a word from the prompt you just typed: the row is there with its matching turn, within ~10 s of the ask, while the turn is still running.
2. **It is in the list too.** The row shows the live conversation with its turn count and `open`; the preview shows the prompt, the reply so far and the tool calls it has made.
3. **A busy turn does not rewrite the file on every call.** `ls -l ~/.local/share/relay/sessions/<digest>/<id>.json` while a turn with many tool calls runs: the mtime moves about every 10 s, not with each call (`tests/test_sessions.py` asserts the same).
4. **The end of the turn only adds.** After the turn finishes, the file and its index row are the same conversation with the final reply; searching the answer finds it.
5. **Nothing is lost mid-turn.** Start a turn that takes minutes, stop the pane's agent (Ctrl+Shift+R), restart Relay: the conversation is in the Sessions pane and resumable.
6. **Nothing else regressed.** `/resume`, Enter to resume here, Shift+Enter for a new pane, rename, pin, delete, the ⓘ view and the "already open: Enter goes to that pane" case all still work on a live session.
7. **A pane with no turn still writes nothing.** Open a fresh pane, ask nothing, wait: no session file appears.
