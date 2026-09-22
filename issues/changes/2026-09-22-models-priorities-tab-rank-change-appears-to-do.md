---
id: YX8Q
type: work
status: needs-verification
labels: [bug, models]
implemented_by: kimi/kimi-k3
waiting_on: owner
rank: zzzzzzzzzzzzzzzzz
created: '2026-09-22'
source: pane report, 2026-09-22
links: {commits: [02c32b7813fcc5f0dceea730bab65d23834fe6ee], evidence: [docs/qa_evidence/2026-09-22-rank-reorder-repro/], related: [RKP3], plans: [], github: null}
---
# Models priorities tab: rank change appears to do nothing

## Issue
in the models priorities tab, i couldnt change the rank

## Discussion points
Reproduced on the current build (same ModelPicker code as the reporter's running instance — no picker commits between their 07:32 start and the 09:18 relink) under Xvfb with an isolated config, seeded `models/tier/*` lists; drive scripts and screenshots in `docs/qa_evidence/2026-09-22-rank-reorder-repro/`. Measured:

- **Alt+↓/Alt+↑ work**: three presses moved `main` m1,m2,m3 → m2,m3,m1 → m2,m1,m3, persisted to `relay.conf` (`drive.py`).
- **Mouse drag within one section works**: dragging main rank 1 below rank 2 stored m2,m1,m3 (`drive2.py`).
- **Drag across a section header stores nothing, silently** — by design (`commitDragOrder`: "a drag that crosses a header changes no list"; `moveSelected` is "clamped inside its own section"). The row follows the drag, the drop indicator shows, then the row snaps back with no message. Moving a model between classes is only possible via ctrl+enter + del.
- **Drag in a filtered view where the filter hides listed rows also stores nothing, silently** (count mismatch → redraw only).

So the within-list reorder paths work; the plausible ways the reporter hit "couldn't change the rank" are the two silent no-ops above, a drop landing on a section header, or expecting the rank number to be editable.
