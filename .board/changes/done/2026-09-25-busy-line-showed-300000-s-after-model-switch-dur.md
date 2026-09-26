---
id: 1BGS
type: work
status: done
labels: [bug, agent-console, busy-line]
implemented_by: glm/glm-5.3-flash
verified_by: glm/glm-5.3-flash
resolution: done
rank: zzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
source: pane p25, 2026-09-25
links: {plans: [], commits: [f4bc2f991bcf], evidence: [], related: [], github: null}
---
# Busy line showed ~300000 s after model switch during compaction

## Issue
The pane busy line ("Relaying · N s · Esc stops") showed an absurd value (~300000 s ≈ 3.5 days) right after the user switched models while the context was compacting. Backend logs show no huge turn (largest turn_summary ms that hour was ~53 s and outcome=done), so the number came from the frontend turn clock (Pane::m_turnElapsed) counting a stuck busy state, not from a real long turn. Suspects: the pane goes/stays m_agentBusy through the model-switch + compaction lane without stopTurnClock, and/or the event path that sets m_agentBusy from an error/pane_state event (PaneEvents.cpp ~869) can make the line tick with a stale or never-started clock (refreshBusyLine → tickTurnClock reads m_turnElapsed unconditionally when busy).

> "buggy time indicator for relaying … happened when i changed over to a new model and it was compacting (i think)"
> — elliott · [session:d4a64c3b906c4301b7649c1d8c36f82f](relay://session/d4a64c3b906c4301b7649c1d8c36f82f) · 2026-09-25

## Done means
- The busy line can no longer count for days: after 2 hours of total worker silence with no ask open and nothing in the waiting line, the pane ends the turn, logs `stuck_turn` to relay.log and prints an inline notice (src/Pane.h, `kStuckTurnSilenceMs`).
- Busy arriving without an `agent_started` (error/pane_state event) starts the turn clock instead of leaving it invalid (src/PaneEvents.cpp).
- `tickTurnClock` never reads a never-started `m_turnElapsed`.
- Proven by the land.py build gate, which compiled the exact landed tree before the commit was allowed (f4bc2f991bcf).
