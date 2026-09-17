---
id: H3QW
type: work
status: needs-qa-llm
component: [gui]
milestone: desktop-alpha
workstream: agent
assignee: implemented by Claude Opus 5 (Claude Code), 2026-09-17
rank: t1
created: '2026-09-17'
acceptance: '`docs/qa_evidence/2026-09-17-tasks-are-todos-only/` (implementer run: 14 QtTest cases over the model and the real panel widget); a non-Claude model QA session runs the checklist below with a preset that writes todo lists and records it there'
source: 'owner report, Claude Code session, 2026-09-17: "it seems like the tasks menu isnt very well designed in relay. it was just taking my agnet command and adding that as a task." / decision the same session: "make that change, and the previous requests ledger, keep that invisible to the user"'
links: {plans: [], commits: [], evidence: [], related: [WGAR], github: null}
---
# Tasks are the model's todos only; the request ledger becomes invisible

Reworks the Tasks surface shipped in [`WGAR`](../../features/needs_qa_llm/2026-09-17-requests-ui.md),
which is still in the QA lane.

## Before

`RequestLedgerModel::deriveTasks()` counted **a user request with no linked todos as a task of its
own**, with the request's own preview as the task text. Because
`backend/relay_core/todos.py` tells the model to "Skip the list for a single simple ask", that was
the common case: typing one ask produced `Tasks 0/1` → `Tasks 1/1` with the owner's own command as
the task.

Two problems behind the symptom:

1. **The surface conflated two jobs.** The ledger is an anti-drop safety net that must be complete
   and deterministic ("kept by the worker deterministically, never by a summarizer",
   `docs/MEMORY-AND-MULTI-REQUEST-RESEARCH.md` section 6). A task list is editorial and may be
   absent. Merging them forced the batch rules and the counts-itself-unless-it-has-todos rule that
   made `c/t` mean different things turn to turn.
2. **The claim was weaker than the vocabulary.** A todo-less request is marked `done` by
   `RequestLedger.finish_turn()` (`backend/relay_core/requests.py:178`) purely because its turn ended
   normally — nothing verifies the work happened, and `_open_items()` (`agent.py:700`) structurally
   skips such requests when deciding whether to re-prompt. So `Tasks 1/1` reported "the model stopped
   talking without crashing", which the spinner already showed.

Relay already had the whole Warp/Claude-Code todo mechanism (`update_todos`, one `in_progress`,
`deferred`/`blocked` with reasons, survives compaction, 8-step stale reminder). What it lacked was a
display: the todos only ever rendered nested under request rows inside a request-framed panel.

## Change

**A task is one of the model's todos and nothing else. A request is never shown.**

- `src/RequestLedger.cpp`: the old derivation is now `deriveAll()` and is used **only** by the batch
  walk, so a turn that wrote no todos still closes a task list. `deriveTasks()` returns todos, so
  `tasks()`, `summary()`, `hasTasks()`, `chipText()` and `turnEndLine()` are todo-driven. The chip
  is hidden and the end-of-turn line silent when the model wrote no list.
- `chipToolTip()` drops the "N requests this session" lines. `openItemsLine()` counts todos only.
  `auditLine()` keeps the user's quoted words and drops the `R<n>` id.
- `src/RequestsPanel.*`: rewritten as a flat task list — the current batch's todos, then a folded
  `Earlier · c/t` row; the selected task's full text, status and note below. Empty state: "No task
  list for this session yet…". Keys are `↑↓`, Enter/Space (fold Earlier) and Esc.
- `src/main.cpp`: the panel's `onSetStatus`/`onReask`/`onFetch` wiring is gone, as are the
  `state_loaded` "N tasks still open" line and the resume picker's `· N open` column, both derived
  from `open_requests`. Chip tooltip, `/tasks` help, keymap and palette wording follow.

**Removed with the ledger surface:** `d` done, `x` cancel, `o` reopen and `r` re-ask. `request_set`,
`request_get` and `request_reask` are untouched in the protocol and the worker, and
`Pane::reaskRequest()` is still in `main.cpp`, so re-ask can come back as a palette action if it is
missed. No backend or protocol change in this card.

## Effect

- A single ask shows **no Tasks chip at all** and prints no `✦ Tasks …` line.
- A multi-step turn shows `Tasks 2/5` counting the agent's own steps, and the panel lists those
  steps rather than the prompts that caused them.
- `Tasks c/t` now means one thing in every turn.

## Risk to check in QA

The task surface is now model-quality-dependent: a preset that never calls `update_todos` shows an
empty panel where it used to show a manufactured task. `docs/MEMORY-AND-MULTI-REQUEST-RESEARCH.md`
section 2 records todo-tool uptake for open-weight presets as UNVERIFIED and says the eval decides
it. **`scripts/eval-requests.py` (with and without `--no-todos`) has not been run for this card** —
the old backfill was hiding the real uptake rate, so measuring it is the next step.

## Implementer check (not a QA verdict)

Automated: `tests/requests_test.cpp`, 14 cases (was 12), including two new ones —
`aPromptIsNeverATask` (a prompt produces no task, no chip and no turn-end line, while the ledger
entry is still parsed) and `panelWithoutATaskList` (empty panel, empty-state text, the prompt text
absent). `panelKeys` became `panelShowsTasks`: the real `RequestsPanel` widget under offscreen Qt,
asserting exact row text `✓  T1  fix parser` and that the detail pane never names `R2`.
`earlierInPanel` now asserts todo rows and an `Earlier · 1/1` group. `ctest --test-dir build` 13/13;
`./scripts/test.sh` 422 OK; build without new warnings.

Live: launched under `xvfb-run` with isolated `HOME`, `XDG_CONFIG_HOME` and `XDG_DATA_HOME`
(`implementer-01-no-chip-on-startup.png`) — starts clean with no Tasks chip.
**Not verified live: a real agent turn.** The isolated profile has no provider key ("No stored key"),
and a live run was not made against the owner's own credentials. So the two behaviours that matter
most — the chip staying hidden through a simple turn, and the panel listing todos during a
multi-step turn — rest on the widget-level tests above, not on a live model. QA should close that
gap first.

## QA checklist

1. With a preset that writes todo lists, send a **single simple ask** ("what is in README?"): no
   Tasks chip appears at any point, and the turn ends with no `✦ Tasks …` line.
2. Send a **multi-part ask** (three small edits, one the agent must mark blocked, one deferred): the
   chip counts the agent's todos, ends amber as `Tasks 3/5 (1 failed, 1 deferred)`, and the
   `✦ Tasks …` line names the failed and deferred steps.
3. Ctrl+Shift+K (or `/tasks`): the panel lists **todos only** — no `R<n>` rows, no prompt text, no
   Mark done / Cancel / Reopen / Re-ask buttons. The selected task's text, status and note show
   below. `d`, `x`, `o` and `r` do nothing.
4. After step 1 only (no list was ever written), open `/tasks`: the panel is empty with "No task
   list for this session yet…" and does not show what you typed.
5. Let a turn stop at the step limit with an open todo, then Continue: the unfinished todo is carried
   into the new list. Restart and `/resume`: the last list shows, with earlier ones folded under
   `Earlier`, and neither the session-loaded line nor the resume picker mentions open requests.
6. Turn on Agent options › Audit requests and provoke a flag: the line reads
   `⚠ may be unaddressed: “…”` with no `R<n>` id.
7. Run `scripts/eval-requests.py --preset <yours>` across the presets and record how often a
   multi-ask prompt actually yields a linked todo list (see "Risk to check in QA").
