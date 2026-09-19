---
id: N8VK
type: work
status: needs-qa-llm
labels: [change, bug]
component: [gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: claude-opus-4-5
rank: zzzzzz
created: '2026-09-19'
acceptance: Ctrl+Enter on an agent prompt starts that turn immediately whenever the agent itself is free, whatever else is in the queue, and interrupts the turn when the agent is busy, as it already does
source: 'issues/feature_intake.txt, 2026-09-19: "ctrl + enter should send immediately"'
links: {plans: [], commits: [8584b96, d26cbb4], evidence: [docs/qa_evidence/2026-09-19-ctrl-enter-sends-now/], related: [C4M8, KJ44], github: null}
---
# Ctrl+Enter should send now, not join the back of the queue

## Issue

ctrl + enter should send immediately

## Reproduced (2026-09-19, live under Xvfb, `main` at `450567e`)

Ctrl+Enter is `agent.interrupt` (`src/Keymap.h:284`, "Send to the agent; while it is busy, interrupt
it and send now") and lands in `interruptAgentWithPrompt()` (`src/Pane.h:709`). Two of its three
cases are already immediate; the third is not.

**Immediate, confirmed:**

- *Agent busy.* Ctrl+Enter prints "Interrupting the current turn…" and sends `{"type":"ask",
  "when":"interrupt"}` straight to the worker, bypassing the queue (`src/Pane.h:9877-9883`).
- *Agent idle, nothing in the queue.* A prompt typed the moment the previous turn ended was on the
  wire inside 1 s — the `route` round trip the submit waits for is not a perceptible delay locally.

**Not immediate — the case that fails:**

1. `sleep 25` in the terminal (running).
2. `echo queued-behind` — queued behind it.
3. Type an agent prompt and press **Ctrl+Enter**.

The prompt is **not sent**. It joins the bottom of the queue, under `echo queued-behind`, and the
toast reads:

```
Queued · the agent prompt runs after the items ahead of it · Enter again to send at the next tool call
```

The agent was idle the whole time. The two items ahead of it are *shell* work, which the agent does
not contend with — so the prompt waits ~25 s on a resource it does not need, from the one key whose
whole promise is "send now".

**Why.** With the agent not busy, `interruptAgentWithPrompt()` falls through to the ordinary submit
path, and `submitAgent()` ends at `if (m_entries.isEmpty() && !m_activeValid && !m_agentBusy)
startAgentEntry(...) else enqueue(entry)` (`src/Pane.h:9884-9889`). `m_entries` is the one queue for
both resources, so a shell command in it sends an agent prompt to the back of the line. The queue
itself already knows better: `pumpQueue()` (`src/Pane.h:10067`) gates an agent head on
`m_agentBusy || !m_configured` and a shell head on `shellIdleForQueue()` — it is only the strict FIFO
order that makes the agent prompt wait.

## Tasks
- [x] `submitAgent()`: an explicit agent submit starts now whenever the agent itself is free, whatever else is in the queue — the same "bypasses the queue, queued items keep their order" rule the interrupt branch already follows (t:c7) <!-- t:wh -->
- [x] One rule for all three keys — Ctrl+Enter, Enter in AGENT mode, the `*` prefix — and for every other door into `submitAgent` ("Continue", `/skill`, delegate, remote prompts, Execute's board task, fix and handoff turns): the same submission by another key (t:d2) <!-- t:1m -->
- [x] The toast: a prompt that starts now never shows a Queued toast, and an entry queued with no running turn no longer offers "Enter again to send at the next tool call" (t:f4) <!-- t:jb -->
- [x] `src/QueueSubmit.{h,cpp}` + `tests/queuesubmit_test.cpp` (ctest `queuesubmit`, 8/8): the reproduction as a state — shell work running and queued cannot hold an agent prompt back, a busy or just-started turn can (t:h6) <!-- t:6g -->
- [x] An explicit agent submit is dispatched locally in `requestRoute()`; the `route` round trip, the "Local router is not ready" refusal and the "Input changed during routing" drop no longer apply to it; the router is asked for `auto` (and shell, which needs the validity check) (t:j1) <!-- t:d1 -->

## Decisions

- 2026-09-19, agent: "immediately" is read as "do not wait on the queue when the agent is free",
  which is what the reproduction shows and what the key's own description already promises. It is
  not read as "interrupt a running turn without being asked" — the busy case already interrupts, and
  that behaviour is unchanged.

## QA checklist
- [x] `ctest -R queuesubmit` — 8/8
- [x] `ctest -j` full: 56/57, the only miss the `backend-and-bash` ctest default timeout; the same suite re-run directly: 3040/3040 OK (queue, steer and interrupt worker tests green throughout)
- [x] Live under Xvfb, `docs/qa_evidence/2026-09-19-ctrl-enter-sends-now/`: the reproduction (`sleep` running, `echo` queued, Ctrl+Enter) — the ask leaves within ~60 ms of the keypress, 4.8 s before the sleep ends, no Queued toast, and the queued echo runs in its original place after; `*` prefix the same; the busy-interrupt branch unchanged
- [x] `relay` builds; `relay-queuesubmit` clean under `-Wall -Wextra -Wpedantic`
- [ ] QA-LLM: re-run the reproduction scene from `drive.sh` and the `queuesubmit` test against the commit
