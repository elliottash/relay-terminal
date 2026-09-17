---
id: JRWQ
type: work
status: ready
component: [gui, worker]
milestone: desktop-alpha
workstream: agent
rank: zzu
created: '2026-09-17'
acceptance: each pane's header shows a short agent-written summary of what the session is about, refreshed as the work changes, and a double click lets the user type their own title that sticks
source: 'owner in chat, 2026-09-17: "the pane header should be an agent-produced summary of the session." and "(and you should be able to double click on the pane header to set a new title"'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# The pane header is an agent-written summary of the session

## Today

The header is `TERMINAL  ~/repos/relay-terminal  │  AGENT WORKSPACE  …`. The session "title" in the worker is
just the first 80 characters of the first prompt, used by the resume list and the conversation list.

## Wanted

- A short summary of what this pane is doing ("Fixing pane drag and Ctrl+H sizing"), written by a model, at
  most ~6 words, kept fresh as the work moves on.
- **Double click on the header** to edit it; a title the user typed is theirs and is never overwritten
  (a small "auto" badge or a reset action puts it back under the model's control).
- The directory keeps its place, smaller and to the right; the tooltip keeps both paths.

## Scope

1. Worker: generate the title with a cheap side call (chores role, `sidecall`), after the first turn and then
   only when the work has moved on — e.g. every 5 turns or after a compaction — never on every turn, and never
   when the user has set a title. Emit `session_title {title, source: "model"|"user", session_id}`; store it in
   the session file so the conversation list and resume picker use the same text. Falls back to today's
   first-prompt title when no model is configured or the call fails.
2. GUI: header shows the title, in-place editing on double click (Enter commits, Esc cancels), the directory
   right-aligned and dim, and the tab label follows the title too (elided). Tooltip: full title, terminal
   directory, agent workspace.
3. Protocol: document the event and `set_session_title {title}` in a new subsection.
4. Tests: title refresh cadence, user title never overwritten, restore after resume, and the fallback path.
