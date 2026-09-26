---
id: 9N0F
type: work
status: planned
labels: [feature, landing, cli]
parent: 3MH4
discovered_from: VK6J
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-26'
source: Claude pane 21c53ef4, 2026-09-26
links: {plans: [], commits: [], evidence: [], related: [VK6J], github: null}
---
# relay-land reorder: move a queued job ahead of or behind others

## Issue
The landing queue runs code jobs strictly in submission order, after Board snapshots, and relay-land has no way to change that. On 2026-09-26 the owner wanted an urgent fix gated second; the only way was editing queue.sqlite3 by hand (swapping two jobs' rowids). Add a supported, audited reorder command.

> add a card for a re-order command
> — elliott · [session:97d268b4846648f49e6aba30a5ebe433](relay://session/97d268b4846648f49e6aba30a5ebe433) · 2026-09-26

## Plan
**Model.** Add a `priority` integer column to `jobs` (default 0; migration fills 0). The publisher picks `ORDER BY (kind='metadata') DESC, priority DESC, rowid` (landq.py `process_one`), so Board snapshots stay first and ties keep submission order. Never rewrite rowids.

**CLI (`relay-land`).**
- `reorder JOB --first` puts it ahead of every queued code job (priority = current max + 1).
- `reorder JOB --before OTHER` / `--after OTHER` sets priority relative to another queued job, renumbering only as needed.
- `reorder JOB --reset` returns it to submission order (priority 0).
- `queue` (or `status --queue`) prints the pick order: position, id, kind, workspace, card, age, priority.
- It works only on `queued` jobs. A job that is already verifying cannot jump; the command says so and suggests `cancel` plus resubmit if the owner really wants to preempt.

**Who may use it.** The owner, plus a pane for its own jobs relative to each other. Moving another pane's job requires `--force`, and the event records who did it.

**Record.** Each change writes an `events` row (`job_reordered`: job, old and new position, actor, reason via `--reason`) and a note on the job's card thread when the job names one, so a job overtaken by another is never a silent surprise.

**GUI.** Later: the queue view in the Board/Sessions pane gets "Move to front" on a queued job, with a shortcut hint per RELAY.md.

**Tests.** landq: priority order beats rowid; metadata still first; verifying job refused; `--reset` restores order; the event is written. CLI: the printed order matches what `process_one` picks next.

**Docs.** docs/TREES-AND-LANDING.md gains the command and the rule that Board snapshots always go first.
