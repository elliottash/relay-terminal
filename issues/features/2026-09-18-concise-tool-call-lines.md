---
id: TK9C
type: work
status: in-progress
labels: [feature]
component: [agent, worker, gui]
milestone: 0.1-preview
workstream: agent
assignee: agent
rank: zz06
created: '2026-09-18'
acceptance: every agent tool call prints one concise line ("ran python script · 14 lines · exit 0 · 1.2 s", "wrote x.py · new · 48 lines", "edited x.py · +3 −1"); clicking it folds the full detail open in place; a diff of at most 12 changed lines prints inline and a larger one opens a diff pane; consecutive reads and listings merge into one line
source: 'owner, 2026-09-18: "agent tool calls are too detailed. rather than seeing a mini python script, i would rather see something like ''executed python'' … you can then click on them to uncollapse the full details. similarly with ''wrote x.py'' or ''edited x.py'' … concise and informative and allow easy access of relevant information."'
links: {plans: [], commits: [07ad727, 40b785a], evidence: ['docs/qa_evidence/2026-09-18-concise-tool-call-lines/pane-README.md'], related: [E4TX], github: null}
---
# Concise tool-call lines, with the detail one click away

## Issue
agent tool calls are too detailed. rather than seeing a mini python script, i would rather see
something like "executed python" … you can then click on them to uncollapse the full details.
similarly with "wrote x.py" or "edited x.py" … concise and informative and allow easy access of
relevant information.

## Decisions
- 2026-09-18, owner: the verb is **"ran"**, not "executed" — "keep it concise".
- 2026-09-18, owner: a click **folds the detail open in place** in the terminal, it does not open a
  separate pane. A diff of **at most 12 changed lines** (added + removed) prints inline under the
  line with no click at all; a larger one opens the diff pane (`src/DiffView`, #E4TX).
- 2026-09-18, owner: **consecutive similar calls merge** into one line ("read 6 files · 4,100 lines").
  Only reads and directory listings merge; commands, edits and agents never do, and a failed call
  never merges.
- 2026-09-18, owner: a **short command** (one line, at most ~40 characters) is shown whole
  ("ran ls -la src"); a longer one shrinks to the program name, keeping the subcommand for
  multi-command CLIs ("ran git commit"), and a pipeline or `&&` chain names the first real command
  and counts the rest ("ran grep +2").

## The look
```
▸ ran python script · 14 lines · exit 0 · 1.2 s
▸ ran git status · 6 lines
▸ ran grep +2 · 37 lines
▸ ran pytest ✗ exit 1 · 212 lines · 8 s
▸ read 6 files · 4,100 lines            (merged run of reads)
▸ listed src/ · 40 entries
▸ wrote x.py · new · 48 lines
▸ edited x.py · +3 −1                   (+ the short diff printed beneath)
▸ edited Pane.h · +212 −87              (click → diff pane)
▸ started job: npm run dev              (background run_command)
▸ started subagent "fix tests"
▸ updated todos · 3 open
▸ moved card #K7Q2 → done
✗ edit x.py · old_string was not found in the file
```

## Tasks
- [x] Package C — backend labels: `tool_labels.py`, the `label` on the tool events and the turn summary, the structured `detail` on the `tool_output_get` reply, and the protocol section the other packages read <!-- t:c1 -->
- [ ] Package E — the engine fold layer: a terminal line that folds its detail open in place <!-- t:e1 -->
- [x] Package G — the GUI: render the label fields, the inline diff, the merged runs, and what a click opens (fold, file, diff pane, subagent, card, plan, todos) <!-- t:g1 -->
      The terminal pane: `src/CallLines.{h,cpp}` (the URI, the row and its cut, the
      rewrite-or-new-row state machine and a fold's rows, all headless in
      `tests/calllines_test.cpp`) and the glue in `src/Pane.h` that writes the bytes, plus the diff
      pane (`ToolPane::Kind::Diff` over `relay::DiffView`) the turn pane and the subagent
      transcript now open too. Commits 07ad727, 40b785a. Evidence:
      `docs/qa_evidence/2026-09-18-concise-tool-call-lines/pane-README.md`.
- [x] The other surfaces (web app, remote panes) render the same labels <!-- t:s1 -->
      Package F: `src/ToolLabel.{h,cpp}` (the shared parser, reused by the terminal pane's
      `src/CallLines.h`), the subagent transcript, the turn pane, the Switchboard cleanup's
      progress line, `app/app.js` and the `remote/panes.py` demo. Evidence:
      `docs/qa_evidence/2026-09-18-concise-tool-call-lines/surfaces-README.md`.
