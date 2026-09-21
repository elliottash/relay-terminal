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

<!-- relay:entry 20260921T230504Z-rx author=agent kind=decision model=gpt-6-astra pane=a76d5a83 turn=5a38cef7388c459a84bd6c3f61b7bb2d/c906ca1d59a94d29bf77803e835dc720 -->
Owner clarified: "i meant harness, fix #1 first" and "then let me test it". Scope is the guest harness queue; leave live acceptance testing to the owner. The numbered defect #1 in the prior investigation concerns the TUI path, so confirming the harness symptom before applying that diagnosis.

<!-- relay:entry 20260921T230609Z-6n author=agent kind=decision model=gpt-6-astra pane=a76d5a83 turn=5a38cef7388c459a84bd6c3f61b7bb2d/86ee019f3e234b679d8453140f59362f -->
Owner clarified: "the issue is \"queue at next tool call\", it doesnt fire at next tool call." Owner then requested: "fix the TUI and the harness issues in separate relay subagents". Harness scope is mid-turn steering; TUI scope is the two independently diagnosed queue defects. Owner will perform live acceptance testing ("then let me test it").

<!-- relay:entry 20260921T230748Z-h1 author=codex kind=progress -->
### Codex · harness subagent
Claimed authorized continuation; implementing backend steering and fake-process regression tests. This guest exposes no relay_board tools; using documented file fallback. TUI files belong to sibling. Plan added to card.

<!-- relay:entry 20260921T231445Z-h2 author=codex kind=evidence -->
### Codex · harness subagent
Implemented native steering with transactional queue acknowledgement, UUID replay handling across Claude result boundaries, rejection/timeout preservation and duplicate protection. Targeted fake-transport tests; no paid turn. Evidence and owner live checklist: docs/qa_evidence/2026-09-21-harness-steering/README.md.

<!-- relay:entry 20260921T231710Z-h3 author=codex kind=evidence -->
### Codex · harness subagent
Landed backend implementation, tests, protocol docs and card in 16fd7eddbec2dd261be180fb22714cdb61bdf789. 260 targeted Python tests pass. Card left needs-verification for owner live test. This thread remains uncommitted because its snapshot includes the parent's uncommitted decision comments and land.py reports an append/deletion conflict against main; parent should land the complete thread with its own snapshot. Added implementation commit link to card.

<!-- relay:entry 20260921T231917Z-h4 author=codex kind=evidence -->
### Codex · harness subagent
Parent review addressed in c90b6135: preserve earlier Claude continuation text and cover pending-ack visibility, clear/cancel refusal semantics, idle reset and turn-end lease recovery. 128 focused tests passed. Added follow-up commit link. Source/fake-stream evidence supports native next-priority ingestion; owner live guest timing verification remains pending.
