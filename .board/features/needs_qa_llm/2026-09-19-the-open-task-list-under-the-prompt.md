---
id: TKS9
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Fable 5.1 orchestrating Claude Opus subagents, 2026-09-19
rank: i2
created: '2026-09-19'
acceptance: the strip under the prompt lists the current task list beside the running agents — up to five rows, the window centred on the marginal task, subagents left and tasks right when there are both — and is absent when there is neither
source: 'issues/feature_intake.txt, 2026-09-19: "i think the open task list could be nice to have underneat the prompt…"'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-open-task-list-under-the-prompt/], related: [QHR1, M9T4], github: null}
---
# The open task list under the prompt

## Issue
i think the open task list could be nice to have underneat the prompt, sort of like how we have the subagents list. it shows up to (say) 5 tasks. if there are more than 5, it centers on the marginal task.

if you have both subagents and tasks, try to split it horizontally with subagents on the left and tasks on the right. ideally you could visualize the link of subagents to tasks there, eg a subagent with two tasks gets two rows.

## Decisions

- **One widget, not two.** The task column was added inside `SubagentsPanel` rather than as a
  sibling `TasksStrip`. Row alignment *is* the feature the owner asked for — "a subagent with two
  tasks gets two rows" means the left and right cells of one row are decided together — and two
  widgets would each own their own row grid, `rowHeight()` rounding and first-row offset, so any
  font or padding drift would break the link. It also keeps the focus chain as it is: today `Down`
  from the prompt enters the strip, `Down` past the last row falls to the jobs list and
  `JobsPanel::onExitUp` comes back; with one widget `Left`/`Right` move between the columns and that
  vertical chain needed no edit at all. And folded mode has one line — two strips would fold to two
  lines under the prompt, which is the floating-strip shape the owner has already rejected. The
  class, file, CMake target and every existing public member keep their names.
- **The window rule.** The list is the *current batch* (`allTodos()` filtered to `currentBatch()`,
  the same filter `RequestsPanel::refresh()` uses), in list order; the window of at most five rows
  is centred on the **marginal task** — the first `in_progress` todo, else the first that is not
  completed/done/cancelled, else, when everything is settled, the tail of the list. The marginal row
  sits third of five, two rows of context above and below, clamped at both edges. The window depends
  only on the statuses, never on the selection, so it is stable across repaints, and an
  `in_progress` task wins over an earlier still-`pending` one because that is where the work is.
- **Completed tasks of the current list show as `✓` when the window has room.** "Centres on the
  marginal task" is only meaningful if the settled tasks are in the list — with open-only rows the
  marginal task is always the first and the centring is a no-op. It also reads as a checklist (two
  done above, the one being worked, two to come) and matches the floating task list, the Tasks chip
  menu and `summary().progress()`'s `"3/5"`, all of which count settled tasks. The strip itself
  still appears only while the batch has at least one *open* task, so it really is the open task
  list: it comes up when work starts and goes away when the list is done.
- **No protocol change; "a subagent with two tasks" is built but cannot happen yet.** The pairing is
  written generically — N rows per subagent, N = max(1, tasks linked to it), with the continuation
  mark on the second and later rows — so the strip is already right the day the backend allows it.
  Today N is always 1: the link is scalar at every layer, and `#QHR1`'s decision ("one subagent per
  task at a time; a task keeps the id of the last subagent that had it") stands. Making it real is a
  protocol change, deliberately not started here: `todo_id` would become `todo_ids` in the four
  places that carry it — the `agent` tool's arguments, the `subagent_started` event, the
  `agents_status` items and the `todo_subagent` message (protocol §8 and §12.4) — plus
  `Subagent.todo_id` → a list in `backend/relay_core/subagents.py` and
  `TodoList.subagent_finished()`'s `next(...)` reverse lookup in `backend/relay_core/todos.py`,
  which today settles only the first match and would strand the rest. The multi-row case that *is*
  real today is the inverse: several finished subagent rows pointing at one re-run todo, each with
  its own row and the task cell drawn on the first of them.

## Implemented

- `src/SubagentsPanel.{h,cpp}` (library `relay-subagents`, which now links `relay-requests`): the
  pure `currentTaskList()`, `marginalWindowStart()` and `layoutStrip()` returning a `StripLayout` of
  `StripRow`s; the widget's second column, the half-width split with the violet `theme::Agent`
  connector on a linked pair, `Left`/`Right`, `Enter`, `S`, the task-cell click, the overflow line's
  `+N agents · +N tasks` wording, `foldedText()`'s `· N tasks open` tail, and a `refresh()` that
  shows the strip for tasks alone.
- `src/Pane.h`: the panel is constructed with the ledger; `onOpenTask` → `openTask()` (the task
  list, opened on that todo), `onRunTaskAsSubagent` → `todo_subagent`, `onMouseOpenTask` → the
  `tasks.strip.open.mouse` hint; `m_ledger.onChanged` now refreshes the strip as well as the chip
  and the floating panel; `fillWorkMenu()`'s task rows teach the new fast path.
- Docs: `docs/ARCHITECTURE.md`, the "open task list under the prompt" paragraph in the Tasks UI
  section and `tasks.strip.open.mouse` in the shortcut-hint trigger list.
- Tests: `tests/striplayout_test.cpp` (new target `relay-striplayout-tests`, ctest name
  `striplayout`) for the window and the pairing; `tests/subagents_test.cpp` extended with the
  widget behaviour.
- Evidence: `docs/qa_evidence/2026-09-19-open-task-list-under-the-prompt/`.

## QA checklist

- [ ] A pane with no subagents and no open task shows no strip at all — not an empty card — and the
      terminal keeps the rows it had before this card.
- [ ] Three open tasks and no subagents: the strip shows full width under the prompt, with the
      `main` row above the task rows and no overflow line.
- [ ] Seven tasks with T5 in progress: five rows, T3..T7, T5 third of the five, and the overflow
      line counts the two that did not fit.
- [ ] Everything in the list completed: the strip is gone (the list must have an open task to show).
- [ ] Seven tasks and two background subagents on T5 and T6: the rows split at half width, agents
      left and tasks right, each pair on one row with the violet connector between the cells.
- [ ] `↓` from the prompt enters the strip; `←`/`→` move between the columns; `↑` at the top and
      `Esc` return to the prompt; `↓` past the last row reaches the jobs list when one is showing.
- [ ] `Enter` on a task that has a subagent opens that subagent's tab; `Enter` on a plain task opens
      the task list with that task selected.
- [ ] `S` on a plain, delegable task prints "Handing T4 to a subagent…" and the task then shows its
      new agent; `S` on a completed or already-delegated task does nothing destructive.
- [ ] A click on a task row does what Enter does and shows the "next time" hint once; the Tasks chip
      menu's task rows show it too.
- [ ] With the subagent pane open the strip folds to its one line and the line names the open task
      count; with no subagents at all it does not fold.
- [ ] Every shipped theme: the task glyphs keep their status colours and the connector stays the
      agent violet.
