# #SHE3 — "todos" reads "tasks" everywhere a person reads. Implementer evidence

Implemented by Claude Opus 5 subagent (Claude Code session), 2026-09-19. QA is another model's job:
nothing in the card's checklist is ticked here.

## What changed

Owner, 2026-09-19: *"i would rather call todos tasks"*, and *"you can still call it todos
internally."* So the rename is of what a person reads, and of nothing else.

| Where | Was | Is |
|---|---|---|
| `backend/relay_core/tool_labels.py` (`_base`) | `updating todos` / `updated todos` / `update todos` | `updating tasks` / `updated tasks` / `update tasks` |
| `backend/relay_core/tool_labels.py` (`detail`) | the fold section `todos` | the fold section `tasks` |
| `backend/relay_core/todos.py` | the two `[Relay reminder: …]` nudges — which are `relay_kind: "note"` messages and so are read back in the transcript and the export — said "one todo per part", "while todos are open" | "one task per part", "while tasks are open" |
| `backend/relay_core/todos.py` | every `ValueError` a failed `update_todos` puts on the row and in the fold's `error` section: "Todo 3: …", "At most 50 todos.", "No todo T7 in the list.", "Todo T2 is completed; a … todo cannot go to a subagent.", "Invalid todo list/entry.", "a deferred todo needs a note", "…had not finished this todo." | the same with "Task" / "task" |
| `src/ToolLabel.cpp` (`humanise`) | a worker that sends no label at all fell back to the humanised tool name, "update todos" | one exception in the humaniser: `update_todos` → "update tasks" |
| `src/Pane.h` | the `/` palette listed `/todos` — "Task list (same as /tasks)" | `SlashCommand` gains `hidden`; `/todos` is the first hidden name |
| `src/RequestLedger.h` | a stale comment quoting "· 1 todo open" | the line the code actually builds |
| `docs/AGENT-SESSIONS-PROTOCOL.md` | the § 23 example row `▸ updated todos · 3 open`; § 23.5's `update_todos` → `todos` (args) | `▸ updated tasks · 3 open`; `update_todos` → `tasks` (args) |
| `docs/ARCHITECTURE.md` | — | the rule, the wire names it does not touch, and what `hidden` means |

**`hidden` on a `SlashCommand`** is the mechanism for "a silent alias": the name is left out of the
`/` popup and is never completed from a prefix (so typing `/to` no longer offers `/todos`), while
`slashCommands()` still holds it — so `/todos` typed in full still opens the task list, it is still
a known command (`reportUnknownSlashCommand` does not claim it does not exist), and an alias or a
skill still cannot take the name.

## What deliberately did **not** change

The wire, exactly as the card says: the `update_todos` tool and its argument names, the `todos` and
`todo_subagent` protocol messages, `open: {"type": "todos"}`, `todos.py`, every C++ and Python
identifier, the `/todos` alias itself, and the message names in `docs/AGENT-SESSIONS-PROTOCOL.md`.
No protocol version moved. The model-facing prompt (`todos.py`'s `SPEC` and `RULES`,
`context.py`'s `## Todos` block, `titles.py`'s digest) still says "todo" where it names the tool's
own arguments — the card's decision.

The palette's *search keywords* still carry "todos todo" (`src/RelayWindow.h`), which is how someone
who still types the old word finds the Tasks rows. Keywords are matched, never drawn.

## Evidence in this folder

- `implementer-backend-tests.log` — the backend modules this touches.
- `implementer-grep-todo.txt` — the card's first QA check, run: every string literal left in `src/`
  and `backend/` that contains "todo", with what each one is. All of them are wire names, model-facing
  prompt text, palette search keywords, comments or docstrings; none is drawn for a person.

## Test results

- `python3 -m unittest tests.test_tool_labels tests.test_todo_subagents tests.test_requests
  tests.test_agent tests.test_keybindings` — 159 tests, OK (`implementer-backend-tests.log`).
- `ctest --test-dir build` — 56 of 57 pass. The one failure is `settings`
  (`SettingsPaneTests::everyOpenPaneRedrawsWhenAValueIsWrittenAnywhereElse`), which belongs to the
  Security-section work another session landed in `4181bd1` and is untouched by this card.
