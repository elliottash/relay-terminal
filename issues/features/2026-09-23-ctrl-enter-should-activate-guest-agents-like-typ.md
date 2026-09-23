---
id: BG2Y
type: work
status: planned
rank: zzzzzzzzzzzzzzzzzr
created: '2026-09-23'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# ctrl + enter should activate guest agents, like typing a first entry

## Issue
ctrl + enter should activate guest agents, like typing a first entry

## Done means
On a pane whose agent is a guest harness (guest:claude / guest:codex) that has not started yet — the pane says it starts on the first prompt — pressing Ctrl+Enter with a prompt in the box starts the guest and sends that prompt to it, exactly as pressing Enter does today. The answer comes back as an ordinary guest turn. Failure shows as: Ctrl+Enter on such a pane does nothing, shows an error (e.g. "no agent provider is configured"), sends the prompt to a different provider, or sends "Continue" instead of the typed text.

## Plan
**Goal.** Make Ctrl+Enter (`agent.interrupt`) activate a not-yet-started guest harness pane the same way a first Enter submission does: the deferred configure fires, the harness starts, and the typed prompt is its first turn.

**Findings.**

- A pane on a guest harness preset defers its `configure` until the first prompt (owner ruling on #4BPE, 2026-09-21): the pane takes the preset, says the guest "starts on your first prompt", and configures when the first prompt arrives; the prompt waits in the queue, which `configured` then pumps. This logic lives in `src/Pane.h` (the `Pane` class — note the file is too large for the search tools; read it directly).
- Enter with text goes through the pane's ordinary submit path (`Pane::submitAgent`), which is what kicks the deferred configure.
- Ctrl+Enter is `agent.interrupt` (`src/Keymap.h:394`, defaults Ctrl+Return/Ctrl+Enter), dispatched in `src/RelayWindow.h:1427` to `pane->interruptAgentWithPrompt()`. That path is built around "interrupt the running turn and send now" (and "Continue" on an empty box, #SXF1 / `src/ContinueTurn.h`) and does not go through the same first-submission activation — so on a fresh guest pane it never starts the harness.
- A sibling deferred-start bug was already fixed once in this area (#4BPE verification, `d7c27b95`: the ranked model/effort had to be staged in the guest block before startup, with a first-prompt wire assertion added as a regression test). Read that commit before editing; the fix for this card belongs next to it.
- Worker side needs no change: `guest_harness_provider` starts the process inside `configure` (protocol 29.3), and `tests/guest_harness_fake.py` (`FakeHarness`) is the seam for tests — no test may start a real guest.

**Steps.**

1. In `src/Pane.h`, read `Pane::interruptAgentWithPrompt()` and `Pane::submitAgent` side by side and find where the deferred guest configure is kicked (the code behind the "starts on your first prompt" state; commit `d7c27b95` touches it).
2. Change `interruptAgentWithPrompt()` so that when the agent is not busy and the pane's configure is still deferred (guest harness not started), a non-empty box takes the ordinary submit path — same activation, same queueing, same prompt — instead of the interrupt path. Keep the busy behaviour (interrupt and send now) and the empty-box "Continue" behaviour unchanged.
3. Check the two adjacent doors for the same gap and cover whichever shares the bug: Ctrl+Alt+Enter (the old alias on the same action) and `QueueSubmit`'s `StartNow` decision for a deferred guest (`src/QueueSubmit.h` — its `State` has no notion of "not configured yet"; confirm it cannot misclassify the first Ctrl+Enter as an interrupt of nothing).
4. Add a regression test at the seam the `d7c27b95` wire assertion already uses: a guest-preset pane, first submission via the Ctrl+Enter path, assert `configure` is sent and the prompt follows it (and that an empty box still sends "Continue" only when that is the rule).

**Risks.**

- `src/Pane.h` is one enormous header shared by several live sessions; land through `python3 scripts/land.py begin/commit` and keep the diff small.
- Do not change what Ctrl+Enter means while a guest turn is running (interrupt and send now) — only the not-started-yet case.

**Verify.**

- The new regression test plus the existing guest first-prompt wire test, `ctest -R continueturn` (the empty-box Continue path, #SXF1) and any test target covering `QueueSubmit`.
- Live under Xvfb with an isolated `XDG_CONFIG_HOME`: a pane on guest:codex (or the fake harness), type a prompt, Ctrl+Enter as the *first* submission — the guest starts and answers. Repeat with Enter to confirm parity, and once with a running turn to confirm Ctrl+Enter still interrupts.
