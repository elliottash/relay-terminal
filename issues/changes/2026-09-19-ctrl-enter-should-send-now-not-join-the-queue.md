---
id: N8VK
type: work
status: ready
labels: [change]
component: [gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
rank: zzzzzz
created: '2026-09-19'
acceptance: Ctrl+Enter on an agent prompt starts that turn immediately whenever the agent itself is free, whatever else is in the queue, and interrupts the turn when the agent is busy, as it already does
source: 'issues/feature_intake.txt, 2026-09-19: "ctrl + enter should send immediately"'
links: {plans: [], commits: [], evidence: [], related: [C4M8, KJ44], github: null}
---
# Ctrl+Enter should send now, not join the back of the queue

## Issue

ctrl + enter should send immediately

## Reproduced (2026-09-19, live under Xvfb, `main` at `7241a40`)

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

- [ ] `submitAgent()`: an explicit agent submit (Ctrl+Enter, the `*` prefix, AGENT mode) starts now <!-- t:c7 -->
      whenever the agent itself is free, whatever else is in the queue — the same "bypasses the
      queue, queued items keep their order" rule the interrupt branch above it already follows
- [ ] Decide and apply the same rule, or deliberately not, to Enter in AGENT mode and to the `*` <!-- t:d2 -->
      prefix: they are the same submission by another key, and a rule that holds for one of the
      three is a rule the user cannot predict
- [ ] The toast is wrong twice over in this state: it should not say "Queued" for a prompt that now <!-- t:f4 -->
      runs, and "Enter again to send at the next tool call" offers steering, which needs a running
      turn there is none of (`src/Pane.h:10024`)
- [ ] A test that an agent prompt submitted while a shell command runs, with shell items queued <!-- t:h6 -->
      behind it, starts at once and leaves the shell queue's order untouched
- [ ] Separately, and not the symptom above: an explicit agent submit still waits for the worker's <!-- t:j1 -->
      `route` reply before dispatching (`requestRoute(true, "agent")`, `src/Pane.h:7646`), although
      the router's verdict cannot change where a forced-agent submit goes. It costs a round trip,
      it refuses outright while the worker is still starting ("Local router is not ready…",
      `src/Pane.h:7731`), and it silently drops the submission if the text changes in that window
      ("Input changed during routing; submit again to use the current text.", `src/Pane.h:7862`).
      Dispatch locally when the mode is explicit and only ask the router for `auto`.

## Decisions

- 2026-09-19, agent: "immediately" is read as "do not wait on the queue when the agent is free",
  which is what the reproduction shows and what the key's own description already promises. It is
  not read as "interrupt a running turn without being asked" — the busy case already interrupts, and
  that behaviour is unchanged.
