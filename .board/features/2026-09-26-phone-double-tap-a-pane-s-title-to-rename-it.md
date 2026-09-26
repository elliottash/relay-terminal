---
id: 4YMX
type: work
status: executing
labels: [feature, remote, phone]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude:ashe-ethz-ch
session: 47171b5e-f82e-4bcd-a055-bf22f59d39e7
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-26'
verify: {artifact: code, primary: script, also: [person], human: optional, criteria: 'Double-tap on the phone''s pane title opens an in-place field; Enter renames the desktop pane (Pane::renameTo), Esc keeps the old name, empty goes back to automatic; the new name comes back through the pane list; a view device and a guest cannot.', sign_off: none, effort: low, stakes: nuisance, blast: capability}
source: terminal pane 47171b5e, 2026-09-26
links: {plans: [], commits: [], evidence: [], related: [8R3V], github: null}
---
# Phone: double-tap a pane's title to rename it

## Issue
On the phone, a double tap on the pane's title in the thread bar edits it in place, and the new name renames the pane on the desktop, the same as the desktop's own rename (`Pane::renameTo`, the worker's `set_session_title`). An empty name hands the pane back to its automatic name. There is no rename message on the wire today.

> in the phone, double click on a pane title should allow you to rename it.
> — elliott · [session:126f58f09e8e410e9141b406f427a4e8](relay://session/126f58f09e8e410e9141b406f427a4e8) · 2026-09-26

## Done means
On a phone, a double tap on the pane's title in the thread bar turns it into a text field holding the current name. Enter or tapping away renames the pane on the desktop, the same act as the desktop's own rename, and the phone's bar and inbox then show the new name. Esc, or submitting the unchanged name, keeps it as it was, and an empty name hands the pane back to its automatic name. A view-only device and a guest get no field, and the host refuses the message from them.

Failure: a double tap does nothing, or zooms the page, or the phone shows a new name the desktop never took.
