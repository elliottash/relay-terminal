# A live conversation is not saved, so it cannot be found (card #6GX9)

Evidence for the fix to "the sessions filter isn't working. I can't full text the conversation
content" (owner, 2026-09-19 00:43). Implementer: Claude Opus 5 (Relay), 2026-09-19.

## The fault

`Agent.autosave()` ran only when a turn ended (`_end_turn`), so a session had no file on disk until
its **first** turn finished. The sessions list and the full-text index are built from those files
(`SessionStore.save` → `conv_index.update_session`), so while a turn ran:

* the conversation was absent from the Sessions pane, and no word in it was searchable;
* the work done in it was on disk only as checkpoint blobs, so a Relay restart, a crash or a closed
  pane lost the conversation itself.

Long agent turns are the normal case, so "the conversation I am looking at" was routinely the one
conversation search could not see. Five panes on the owner's machine were in exactly that state
(see `output.txt`), several with 75–106 tool calls behind them.

## The change

`backend/relay_core/agent.py`

* `Agent._autosave_soon()` writes the session while a turn is running, at most every
  `MID_TURN_SAVE_S` (10 s) — the throttle keeps a turn that calls tools in a loop from rewriting a
  large session file on every call. `autosave()` stamps `_last_save`, so the two share one clock.
* It is called after the user's prompt, after each model reply and after each tool result (both the
  subagent batch path and the ordinary one), so the prompt is findable as soon as it is asked.

Docs: `docs/AGENT-SESSIONS-PROTOCOL.md` 14.1 and 14.8, `docs/ARCHITECTURE.md` (the index section).

Tests: `tests/test_sessions.py::SessionTests::test_a_running_turn_is_written_so_it_can_be_found`
and `::test_the_file_is_not_rewritten_on_every_tool_call` (both pass; the whole `SessionTests`
class was run, 8 tests, OK). `./scripts/test.sh` and `ctest` were **not** run for this card — the
owner asked to skip them.

## What is here

| File | What it does |
|---|---|
| `repro-agent-mid-turn.py` | A real `Agent`, a real `SessionStore`, a stub provider whose first reply is `run_command sleep 6`: prints whether the session file exists while the tool is still running. It did not exist before the change; it does now. |
| `repro-search-during-turn.py` | The same, end to end with the real index: searches the index for words that are only in the conversation on screen while the turn runs. The prompt is findable mid-turn; the final answer joins it when the turn ends. |
| `verify-search-on-real-data.sh` | The current build under Xvfb on a jail holding a copy of the owner's real sessions and index, driven with xdotool and OCRed: the rest of the search (saved conversations, matching turns, previews) works. |
| `output.txt` | The captured output of all three, plus the read-only survey of the owner's machine. |

## What was checked about the rest of the report

Full-text search over saved conversations was verified working, on the owner's own data, before the
change — the fault was the missing file, not the query. Three things still make content "not found"
and are deliberate, not faults: the default scope is **this project** (a conversation about a
project saved from a pane whose cwd was elsewhere is filed under that other project), subagent
threads are searched only with the "Subagent threads" box ticked, and an entry's text is capped
(8000 characters for a prompt, 4000 for a reply or tool output).
