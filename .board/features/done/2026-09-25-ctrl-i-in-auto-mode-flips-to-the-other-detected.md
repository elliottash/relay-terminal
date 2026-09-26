---
id: 6T6R
type: work
status: done
labels: [feature, input, routing]
assignee: agent
implemented_by: glm/glm-5.3-flash
verified_by: glm/glm-5.3-flash
resolution: done
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
source: pane 0 (this session), 2026-10-01
links: {plans: [], commits: [5f2fe490a164], evidence: [], related: [], github: null}
---
# Ctrl+I in auto mode flips to the other detected mode first

## Issue
In the prompt box's routing mode cycle (Ctrl+I / `input.toggle`), when the pane is in `auto` mode and the router has already decided the draft goes to agent or shell, the first Ctrl+I should land on the *other* mode: flipping to the detected mode would change nothing. Otherwise keep the existing cycle (auto → shell → agent → [program] → auto).

> if you are in auto mode and a mode (agent/shell) has been detected, ctrl+I should flip to the other mode first.
> — elliott · [session:fe8c0445d6ed40638e0ddb1ad4e7f790](relay://session/fe8c0445d6ed40638e0ddb1ad4e7f790) · 2026-09-25

## Done means
With the pane in `auto` mode and the router chip showing a verdict for the draft in the editor, pressing Ctrl+I lands on the *other* mode (detected TERMINAL → Agent; detected AGENT → Terminal). With no verdict in hand the cycle is unchanged (auto → shell → agent → [program] → auto), and `detected` is ignored outside auto.

## Tests
- `InputPolicyTests::cycledModeFlipsAwayFromTheDetectedRoute()` — auto+shell→agent, auto+agent→shell, auto+no-verdict→shell (unchanged), and the rest of the cycle with `detected` ignored outside auto. `./build/relay-input-tests`: 34 passed, 0 failed (2026-09-25).
- `scripts/relay-build --target relay` green; landed via `scripts/land.py`, whose verify slot built the exact tree committed (5f2fe490a164).
- Wire-up: `Pane::detectedRoute()` returns the router's verdict for the draft still in the editor (assist answer > local guess > plain route event, matched against the text so stale verdicts never count); route events record it in `m_detectedRoute`/`m_detectedRouteText`; `toggleInputMode` passes it to `relay::input::cycledMode`.
