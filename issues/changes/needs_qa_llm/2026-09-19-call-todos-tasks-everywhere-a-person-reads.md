---
id: SHE3
type: work
status: needs-qa-llm
labels: [change]
component: [gui, worker]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 subagent (Claude Code session), 2026-09-19; follow-up 2c43e19 by Claude Fable 5.1 (the coordinating session)
rank: zzzzzzy
created: '2026-09-19'
acceptance: no string a person reads in Relay says "todo" — tool-call rows, palette entries, hints, toasts, panel titles, slash-command help and the user docs all say "task"; the wire names (the update_todos tool, the todos / todo_subagent messages, the /todos alias) are unchanged and no protocol version moves
source: 'issues/bug_intake.txt, 2026-09-19: "i would rather call todos tasks"'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-call-todos-tasks-everywhere-a-person-reads/], related: [BDXG], github: null}
---
# Call todos "tasks" everywhere a person reads

## Issue
i would rather call todos tasks

## Decisions

- **Owner, 2026-09-19: "you can still call it todos internally."** The rename is of what a person
  reads. The `update_todos` tool, the `todos` and `todo_subagent` protocol messages, `todos.py`, the
  C++ identifiers and the `/todos` alias keep their names; nothing in
  `docs/AGENT-SESSIONS-PROTOCOL.md` changes and no saved session needs an alias.
- The tool-call row reads "updating tasks… / updated tasks / update tasks".
- `/tasks` is already the primary command; `/todos` stays as a silent alias and its palette row
  ("Task list (same as /tasks)") goes, so the palette does not teach the old word.
- The model-facing prompt in `todos.py` may keep saying "todo" where it names the tool's own
  arguments; where it produces text the user will read (nudges echoed into the transcript, the
  tool's result text if it is ever shown in a fold), it says "task".

## Implementation (2026-09-19)

The row reads "updating tasks / updated tasks / update tasks" and the fold's section is `tasks`
(`backend/relay_core/tool_labels.py`). `update_todos`' own messages — the two `[Relay reminder: …]`
nudges, which reach the transcript as `relay_kind: "note"`, and every validation error a failed call
puts on the row and in the fold — say "task" (`backend/relay_core/todos.py`); the model-facing `SPEC`
and `RULES` keep "todo" where they name the tool's arguments, as decided above. A worker that sends
no label at all falls back to the humanised tool name, so `humanise()` has one exception:
`update_todos` → "update tasks" (`src/ToolLabel.cpp`).

`/todos` is now a **hidden** `SlashCommand` (`src/Pane.h`): left out of the `/` popup and never
completed from a prefix, so nothing teaches the retired word, while the name stays known — typed in
full it still opens the task list, it is never reported as an unknown command, and no alias or skill
can take it.

Nothing on the wire moved. Evidence, including the card's own grep run in full:
`docs/qa_evidence/2026-09-19-call-todos-tasks-everywhere-a-person-reads/`.

## QA checklist

- [ ] `grep -rni "todo" src/ backend/` hits only identifiers, wire names and comments — no
      user-visible string literal.
- [ ] A live turn that calls `update_todos` prints "updated tasks".
- [ ] `/todos` still opens the tasks panel.
