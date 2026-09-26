---
id: QED5
type: work
status: discussing
labels: [feature, design, landing, gui]
parent: 3MH4
discovered_from: VK6J
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-26'
source: Claude pane 21c53ef4, 2026-09-26
links: {plans: [], commits: [], evidence: [], related: [9N0F, VK6J, 55HG, S84D, TE6D], github: null}
---
# A build manager in the Projects pane: the landing queue, gate progress, history and workspaces in one view

## Issue
Today the landing queue is visible only through relay-land status/snapshot, sqlite and journalctl; on 2026-09-26 following one gate, finding a stopped job's card, reordering jobs and switching the gate policy all needed hand-run commands. The owner asks whether Relay needs a GUI build manager in the project pane to track the queue and its history.

> also file a card, whether we need like a build manager in the project pane, where you can track the quees and history in a gui
> — elliott · [session:97d268b4846648f49e6aba30a5ebe433](relay://session/97d268b4846648f49e6aba30a5ebe433) · 2026-09-26

## Recommendation
Yes, but as a read-mostly view built on data that already exists, not a second control plane.

**Why.** Every question the owner asked on 2026-09-26 (what is running, how long, what's next, whose card, why it failed, what landed) had an answer in `queue.sqlite3`, `logs/<job>/`, the `events` table and `main-status`, but reaching it took a terminal and an agent. A view makes that a glance.

**What it shows (one page per project, in the Projects pane).**
1. *Now*: the gating job with its card, author pane, phase and `progress` line (the live log from 8df818b3), elapsed time, and the gate policy in force (full / build-only), with a warning when it is not the full gate.
2. *Queue*: waiting jobs in pick order (Board snapshots first), each with card, workspace, age, commits, and actions: cancel, move to front (#9N0F), open card, open the author's pane.
3. *History*: landed / failed / cancelled / conflict jobs with duration, reason, the known-failure summary line, and links to the verify log and receipt. Filters by card and author.
4. *Workspaces*: count against quota, in use / released / retained, disk, and trees with unlanded commits (the cleanup that took SQL by hand today).
5. *Installed main*: sha, lag, last build, and whether the running Relay is on it (so a restart prompt can say what it gets).

**How.** A `queue_snapshot` worker call that returns what `relay-land snapshot` already builds, polled while the page is visible, with `progress` from `landq.status`. Actions go through the same `relay-land` verbs (cancel, reorder, capture-policy for the owner only), so GUI and CLI never diverge.

**Not in scope.** Editing gate policy in the GUI beyond switching between committed, accepted policies; running gates from the GUI.

**Order.** After #9N0F (reorder) so "move to front" has a real verb; the live log from #VK6J is already in place.
