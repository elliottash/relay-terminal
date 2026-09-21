<!-- relay:entry 20260921T220400Z-a1 author=claude-code kind=progress -->
### Claude Code · 2026-09-21 22:04
Picked this up and read both queue paths before touching anything: `Pane::submitGuest` /
`Pane::pumpQueue` / `Pane::setGuestBusy` for the TUI route, and `Pane::submitAgent` /
`relay::queuesubmit` / the deferred-harness start (#MDL1) for the harness route.

<!-- relay:entry 20260921T220600Z-a2 author=claude-code kind=evidence -->
### Claude Code · 2026-09-21 22:06
Two defects found by reading the **TUI guest** path (§26.8). Both are real whatever the report
turns out to be about; neither is proven to be what was seen.

1. **A delivered line is not latched, so the queue empties into the TUI at once.** `pumpQueue()`
   hands a guest entry to `typeIntoGuest()` and records nothing to say it is waiting for the guest
   to pick it up; the only gate is `m_guestBusy`, which turns true only when claude's
   `UserPromptSubmit` hook or codex's rollout `state` event reaches the pane's spool — hundreds of
   milliseconds later. `pollShell()` calls `pumpQueue()` about twelve times a second, so the next
   queued item is typed into the TUI inside that gap, and `typeIntoGuest()` opens with Ctrl+U,
   which clears whatever the guest has not submitted yet. The agent path has exactly this latch
   (`relay::queuesubmit::State::agentTurnStarting`, from `m_activeValid` set by
   `startAgentEntry(entry, /*fromQueue*/ true)`); the guest branch has no equivalent.

2. **A running queued shell command wedges the guest queue for the guest's whole life.**
   `pumpQueue()` returns at the top on `m_activeValid`, which stays set until the queued command's
   program exits. The guest's own launch line is such a command — `launchGuest()` ends in
   `submitTerminal(line, false)`, and §26.9 says it is "queued until [the shell] is otherwise",
   which is the documented case for a pane whose shell is not up yet. When the launch line goes
   through the queue rather than straight to the shell, `m_activeValid` is true until claude/codex
   exits, and **nothing queued after it is ever delivered**. A guest entry contends with the guest
   alone, which is the argument #N8VK already made for agent prompts against shell work.

Also noted, not a defect on its own: codex's busy signal needs one `relay_core.guest_codex tail`
per codex pane; there is none running now, and there is no Relay TUI guest running now either —
the three `claude`/`codex` TUIs on the machine are children of `warp-terminal`.

What the **harness** path shows, by contrast: in `~/.local/share/relay/logs/relay.log` every
`type=queued` in a `guest:claude` / `guest:codex` pane this evening is followed by `agent_started`
within about 40 ms, so nothing is stuck in the worker's queue there. A GUI-side queue that never
sent its ask would leave no trace in that log, so this does not clear the harness path — it only
says the worker is not the one holding anything.

<!-- relay:entry 20260921T220700Z-a3 author=claude-code kind=question -->
### Claude Code · 2026-09-21 22:07
1. Which pane was it? (a) a pane running **claude or codex as a TUI** in the terminal, with the
   Relay composer typing into it, or (b) a pane whose **model** is claude / codex (the
   `guest:claude` / `guest:codex` harness the model box picks). *Recommendation:* the fix differs
   completely, so I would rather have one word than guess — (b) is what the logs say you were
   using all evening, but (a) is where I can already show two defects.
2. What did the queue do — did the rows **sit there and never run**, or did they **all fire at
   once** (or land in claude's / codex's own queue instead of Relay's)? *Recommendation:* "sat
   there" points at defect 2 above; "fired at once" points at defect 1.
3. Was it queued **prompts**, queued **terminal commands**, or a **steer** (Ctrl+↑, "send at the
   next tool call")? *Recommendation:* worth naming, because a steer into a harness turn cannot be
   delivered mid-turn — the harness runs the whole CLI turn in one `complete()` call, so a steer
   can only land after it, and that alone would read as "the queue does not work".
