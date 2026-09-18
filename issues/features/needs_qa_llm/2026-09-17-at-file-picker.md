---
id: AM2Z
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (GUI D subagent), 2026-09-17
rank: ei
created: '2026-09-17'
acceptance: a non-Claude model QA session runs the checklist and records it under `docs/qa_evidence/`
source: '`issues/feature_intake.txt` (2026-09-17): "how to open files by typing their names, maybe @? any file that can be previewed in a pane will then start showing up with a type text filter."; owner decision 6'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# @ file picker in the prompt box

## Behavior as implemented

- Typing `@` (at the start or after a space) opens a picker above the prompt box, filtered as you type (fuzzy; file-name matches weighted over path matches; exact names boosted; shorter paths preferred). Previewable files (text/code/Markdown/JSON/images/PDF by extension) rank first and are marked ◆.
- File list: inside a git repository, `git ls-files --cached --others --exclude-standard` from the repository root (respects `.gitignore`, capped at 20,000); otherwise a walk of the pane's directory skipping hidden paths and `node_modules`, capped at 5,000. Cached for 15 s per directory. With an empty query, recently opened files and `git status` changes rank first.
- Up/Down move, Enter or Tab insert `@relative/path ` (quoted if it contains spaces), Esc closes, click selects.
- `@path` alone + Enter opens the file (or folder) in the pane's preview/explorer (existing `openPath`).
- In an agent prompt, existing `@path` files are sent as `attachments: [{"path": abs}]` on `ask` (protocol v1 section 10) and stay in the prompt text. The current worker ignores the field; backend support is tracked in `issues/features/2026-09-17-agent-sessions-planning-subagents.md`.

## Implementer check (not a QA verdict)

Under Xvfb (`docs/qa_evidence/2026-09-17-at-file-picker/`): `@READ` listed `README.md`; Tab inserted `@README.md`; Enter opened a rendered preview pane. An agent prompt with `@notes.txt` was sent without errors (the agent read the file with its own tool).

## QA checklist

1. In a large repository, `@` responds quickly and ignored files do not appear.
2. Outside a repository, hidden folders and `node_modules` are skipped.
3. `@name` + Enter opens previews for code, Markdown and an image.
4. After backend support lands: an agent prompt with `@file` includes the file content without a tool call.
