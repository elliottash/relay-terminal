---
id: XCXD
type: work
status: needs-verification
labels: [bug, queue, keyboard]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: a09e2416-39d1-413c-acce-afbd49595129
rank: zzzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [ai-visual], human: optional, criteria: Queue columns and stop labels match running resources; shortcuts behave as specified., sign_off: none, effort: high}
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-24-XCXD/], related: [QFF1, 7JD1, QAN1, QSN1], github: null}
---
# Review command queue keyboard behavior and repeated Enter timing

## Issue
there is something fishy with the command queue, sometimes the double enter to send doesnt work. and it seems like it is more finicky when there are mmultiple commands queued. can you do a review of that system and check the logic and check it works appropriately on a conceptual and implementation level. 

and lets actually review the exact logic together, what the different keys do in different circumstaces, so we are clear. 

no queue
1 in queue
2 in quee 
3+ in queue

enter
enter x2
enter x3
ctrl enter
etc

## Plan
Goal: implement the accepted independent queues, stable Enter sequence, fresh turn-start shell context and Esc/Alt+Esc controls.
Findings: Pane.h owns scheduling/state, PaneRuntime.cpp owns routing/rendering, PaneSession.cpp consumes steering events, terminal_context.py gates pinned/live evidence. Existing source has shared active/paused state and UI list; preserve standalone guest channel ownership.
Steps: 1. GPT-6-Sol core child implements scheduling and Enter state machine. 2. GPT-6-Sol UI child implements two-column rendering, stop controls and question/queue Esc changes. 3. GPT-6-Sol context child implements dispatch-time context and tests. 4. Root integrates shared regions, updates documentation, builds current source, runs targeted regressions and captures real isolated GUI evidence. 5. Land scoped hunks through land.py and submit evidence for verification.
Risks: concurrent edits in Pane.h; root snapshots/commits shared paths, children own named regions and coordinate before overlaps. Routing and native/TUI states must not duplicate prompts or cross terminal ownership. Automatic context changes cannot revive revoked sharing.
Verify: current-source consolemode/queue tests, terminal-context Python tests and isolated Xvfb screenshots; no full suite.

## Done means
- Agent and shell queues drain independently in FIFO order with side-by-side (narrow: stacked) presentation and independent pause/resume; guest TUI ownership stays serialized.
- Repeated Enter targets the oldest agent prompt stably across routing delay, elapsed time and delivery events, without duplication or changing target.
- Automatic shell context refreshes at actual turn start, including running/incomplete or completed results; pinned snapshots and sharing restrictions remain respected.
- Esc stops the sole running resource, agent first if both run; Alt+Esc targets shell, buttons display those keys, Ctrl+C remains copy in composer, and queue editing/questions do not consume Esc (blank Enter skips questions).
- Targeted current-source builds/tests and isolated GUI evidence demonstrate the behavior; unrelated workspace edits remain intact.

## Execution Summary
Read-only conceptual and implementation review; no product files changed.
Findings:
1. src/PaneRuntime.cpp requestRoute keeps the draft while auto-routing and ignores subsequent non-agent submissions while m_pendingSubmit/m_heldDecision/m_loading is set. A second Enter before routing completes is not remembered as a steer intent.
2. src/Pane.h:8810 gates head promotion on m_lastQueuedAt <=15 seconds. That timer is set only for agent entries enqueued while m_agentBusy, and invalidated when its particular latest entry is steered. Existing visible queues therefore may not respond to empty Enter.
3. src/Pane.h:12822 tries escalation then promotion. src/PaneSession.cpp:43 removes delivered steers. Delivery between keys makes escalation fail and permits promotion of the next queued item. tests/consolemode_test.cpp:1112 covers only two entries with no delivery event between keypresses.
4. A recent steer retains priority even after a different draft is queued: enqueue does not clear m_lastSteeredAt. Repeated Enter can target that older steer.
5. Empty-Enter promotion reads only local m_entries, unlike queue display/navigation which also includes workerQueued(). Shell/TUI entries are not agent steers. Queue size alone cannot describe the behavior.
Proposed contract for discussion: Enter with text submits/queues; empty Enter steers the FIFO head at any age; the next Enter stays associated with that same prompt (if already delivered, report that rather than promote another); preserve intent across pending auto-routing. Keep Ctrl+Enter as explicit immediate agent submit. User decision pending.
Implementation attempt 2026-09-24: requested three gpt-6-sol children through Relay agent tool. Saved records revealed all actually ran glm-5.3, including after agent_set_model reported success. All three have stopped and report read-only investigation only; no product changes were made. Root removed its provisional unimplemented documentation draft. Existing defect #VTJR now carries this reproduction. Queue implementation remains pending; owner asked whether to fix GPT-6-Sol delegation first or allow direct implementation in the current session.
Implementation landed 2026-09-24 (pane a09e2416, taking over from closed pane a06c9360, whose GLM children had written the bulk of it but never landed): c299337b, with dd70fb61 re-landing the agent.py turn-start resolution alone.
- Independent agent/terminal FIFO lanes, side by side (stacked < 640 px), each with its own count, pause and Resume; stable empty-Enter sequence pinned to the oldest agent prompt with no fallthrough; routing-pending presses buffered and replayed; blank Enter skips a question; Esc stops the sole running resource (agent first), Alt+Esc the shell; automatic terminal context resolved at actual turn start (Pane.h, PaneRuntime.cpp, PaneSession.cpp, QueueSubmit.*, terminal_context.py, agent.py, queue.py `unsteer(item_id=)`).
- Owner's UI feedback on the restarted build: a busy agent alone no longer raises the strip, the Stop agent button is gone (the Relaying line's "Esc stops" covers it), and an empty lane draws no "Terminal · 0" (PaneRuntime.cpp rebuildQueueStrip).
- Fixed on the way: blank Enter did not resume a card queue the worker paused (`agentLanePaused()` now counts the worker half); buffered route presses were wiped when the box was rewritten with the same text (`noteComposerDraft`); `showBubble` tested `isVisible()` and never re-showed a strip hidden while the pane was small (now `isHidden()`); debug `qWarning`s removed; enqueue pinning restructured so it lands without touching #R5TC's uncommitted `noHandoff` line.
- Landing incident: c299337b also swept in #Z82M's uncommitted `MediaTools(emit=...)` agent.py hunk (claimed during the land, so not flagged). Taken back at once with `land.py repair` (232601bf); main verified on a clean export; #Z82M notified. imgpaste's `@image` hunks in PaneRuntime.cpp/consolemode_test.cpp were excluded and remain uncommitted for that session.

## Tests
Current source, 2026-09-24 (after the owner's UI feedback):
- `scripts/relay-build --target relay-consolemode-tests relay-queuenav-tests relay-queuesubmit-tests`, then `ctest --test-dir build -R '^(consolemode|queuecontract|queuenav|queuesubmit)$'`: 4/4 passed. `queuecontract` runs `--xcxd-only`: queue, review, UI and (now wired) context cases.
- land.py build gate on the exact landed tree (c299337b): fresh configure + build + the same four ctests, passed.
- `PYTHONPATH=backend python3 -m unittest tests.test_terminal_context tests.test_terminal_context_integration tests.test_queue`: 88 run, 87 passed, on the working tree and on a clean `git archive dd70fb61` export. The one failure is `ConsoleFieldTests.test_a_card_turn_is_refused_the_writers_at_call_time_and_told_why` (Execute vs Run), already on #E728.
- Test changes: `answersBypassQueuedPrompts` updated to the blank-Enter-skips contract (three questions: numeric pick, blank skip, text); `SteerTests.test_unsteer_by_item_interrupts_the_turn_card_xcxd` was racing on an already-recorded `agent_finished` (5/5 passes after); new UI regressions: busy agent with nothing queued shows no strip, no lane label, no Stop agent; running shell with nothing queued shows no lane label.
- Evidence: docs/qa_evidence/2026-09-24-XCXD/ (queue-wide.png, queue-narrow.png, queue-shell-only.png, README.md), captured by the suite with `RELAY_XCXD_EVIDENCE_DIR`.
- Not synthesized: per-lane drag reordering (needs a real drag source).

## Decisions
Owner: "i agree with your design and recommendations. put those on a card."

Agreed agent-prompt contract (recorded for implementation; not implemented in this review):
- Enter with text submits it normally; while the agent is busy it joins the queue.
- With A and B queued and new draft D, repeated Enter targets A (FIFO), not D. Ctrl+Enter remains the explicit immediate-send path for D.
- Empty Enter can steer the FIFO agent head regardless of how long it has been queued; remove the hidden 15-second queue-age restriction.
- Subsequent Enter in that sequence remains attached to the same prompt. If already delivered, report that; never silently advance to another prompt because a worker event arrived between presses.
- Preserve repeated-Enter intent while Auto routing is pending.
- Do not let a previous steer capture a new submission sequence.
- Keep Ctrl+Enter behavior described in the review: text sends immediately (interrupting a busy agent); empty idle sends Continue; empty busy does not send the queue head.
- Cover 0/1/2/3+ queued agent prompts, routing delays, delayed queue action, delivery between presses, and stale previous-steer state in regression tests.

Owner follow-up: "i want to deal with how shell commands are treated as well in queues, so describe that after"
Shell and mixed-queue semantics are being described separately; acceptance above does not decide whether shell work should use a separate queue or whether repeated Enter should interrupt a shell command.
Owner: "ok i like that, so if there are both agent and shell commands queued, show two queues side by side?"

Accepted direction: independent FIFO agent and shell queues, shown side by side when both contain waiting work. This supersedes the earlier open question about shared FIFO versus independent draining. Repeated Enter must not implicitly interrupt shell commands.
Layout recommendation: Agent on the left, Terminal on the right, each with its own count and waiting/paused status; one nonempty queue uses full width; hide both when empty; stack the two on narrow panes. Keep one composer and visibly indicate the destination. Reordering stays within each queue. These responsive details are proposed defaults, not separately requested by the owner.
Implementation must preserve terminal ownership: guest TUI !commands share that guest’s input channel and must not be treated as an independently available native shell. Cross-queue dependency controls and exact keyboard navigation between columns still need specification.
Owner: "yes, thats great, put that on the card"

Agreed terminal context behavior for independent queues (planned, not implemented):
- Resolve automatic terminal context when an agent turn actually starts, not only when its prompt is submitted or queued.
- Include new shell results since the previous snapshot. If a command is still running at turn start, identify it and include available output explicitly marked running/incomplete. If it has completed, include its result and exit code instead.
- Include command, working directory, status and bounded output excerpts; retain access to larger output via explicit read tools.
- Preserve explicitly attached/pinned output revisions and respect automatic/manual/off sharing settings and revocation.
- Shell output arriving during a running agent turn does not automatically interrupt it or get inserted into its conversation. Immediate delivery requires an explicit update or read within the available sharing grants.
- Starting an agent turn does not automatically wait for a running shell command. Waiting for a particular result is an explicit dependency, whose UI remains to be specified.
- Regression coverage must include a prompt queued before shell completion but started after completion; a still-running command at turn start; multiple intervening command results; pinned attachments; and sharing disabled before dispatch.
Owner: "yeah, i dont want ctrl+esc. i would rather modify how it works with the other situations:\nyou dont need esc while editing a queue item -- just use the down arrow instead. \nyou dont need esc while answering a question, just press enter with blank. \n\nthe other ones are rare enough i dont mind".

Agreed simplification, superseding the proposed Ctrl+Esc override:
- Do not introduce Ctrl+Esc as an agent-stop override.
- In the Relay composer, queue editing/selection must not consume Esc. Esc stops the running agent; Down is the way to navigate out of queue editing. Preserve draft text rather than treating stop as discard.
- An agent question must not consume Esc as Skip. Esc stops the agent; plain Enter on a blank answer skips the current question instead. Handle blank-answer Enter before queue steering/resume, so skipping cannot also dispatch queued work. Skipping is not approval or acceptance of a preselected answer.
- Keep popup dismissal and native terminal Esc behavior as exceptions; the owner accepts those rarer cases.
- Tests must cover Esc with a selected queue item and with an open question, blank Enter skipping without queue actions, and preserved popup/native behavior.
This is a design decision recorded for implementation, not a claim that these behaviors have changed.
Owner: "the next hing -- i dont like ctrl c for interruption. 

for shell vs agent, if only one is running, i want esc to interrupt whichever is working. 

if both are running, i propose the following. esc interrupts the agent; alt+esc interrupts the shell. and you see both buttons stop agent (esc) stop shell (alt+esc)"

Current approved interrupt-key contract, superseding the earlier fixed-target Esc/Ctrl+C proposals:
- In the Relay composer, only agent running: Esc stops agent. Only shell program running: Esc interrupts shell. Both running: Esc stops agent and Alt+Esc interrupts shell. Neither running: neither shortcut issues a stop; existing non-stop Esc behavior can remain.
- Alt+Esc is shell-targeted and never falls back to stopping the agent; it can remain a shell interrupt alias when shell is the only running resource.
- Both running: show Stop agent (Esc) and Stop shell (Alt+Esc). Agent only: show Stop agent (Esc). Shell only: show Stop shell (Esc). Keep the applicable stop controls visible even when their waiting queues are empty; render live keymap labels.
- Ctrl+C remains copying in the composer; do not add interruption there. Previously accepted native-input behavior remains unchanged, including program-owned Esc and ordinary native Ctrl+C. No Ctrl+Esc override.
- The earlier question/queue-edit simplification remains: neither consumes Esc; Down navigates out of queue editing, blank Enter skips a question, and prompt/edited text survives a stop. Stop target does not depend on whether the composer contains text.
- Previously accepted popup dismissal and native-input exceptions remain.
- Record this as planned design, not implemented behavior. Verify the running-state matrix, dynamic button labels, preserved drafts, and transitions where one resource finishes while a stop is requested. One keypress must issue at most one stop; auto-repeat must not spill from agent to shell as the agent finishes.
Owner: "implement with gpt-6-sol-subagents." Implementation authorized for the agreed contract.
Owner: "glm is fine, they can do it". Proceed with the existing GLM-5.3 children; model fallback no longer blocks this implementation.

## Tasks
- [x] Review current key transitions, queue-size matrix and existing regression coverage <!-- t:f2 -->
- [x] Record owner-approved agent-prompt contract <!-- t:e6 -->
- [x] Implement stable FIFO Enter targeting and preserve intent during routing <!-- t:cb -->
- [x] Add timing/event-interleaving regressions and verify current build <!-- t:w9 -->
- [x] Describe shell and mixed-queue behavior for follow-up design discussion <!-- t:6t -->


## Discussion points
Shell/mixed queue source review (2026-09-24; no behavioral changes):
- submitTerminal in src/Pane.h starts immediately only when local queue is empty, no active queued entry owns the slot, and shellIdleForQueue is true. Otherwise it appends a shell entry.
- pumpQueue examines only the head, gated by pause/question/selection and m_activeValid. Shell head waits for shell readiness; agent head waits for agent readiness. It does not scan past blocked heads.
- New agent prompts bypass existing queued work when the agent is idle (QueueSubmit.cpp), but new shell commands do not have the matching bypass. Thus this is neither strict global FIFO nor two independent resource queues.
- Repeated empty Enter does not promote a shell head, skip it to steer an agent behind it, or interrupt a running shell command. A prior agent steer may still win the escalation branch.
- Ctrl+Shift+Enter selects terminal delivery, not immediate execution; Ctrl+Enter selects agent delivery even for shell-looking text.
- If a foreground program requests a line, non-agent submission may go directly to that program's stdin instead of joining the command queue. Empty Enter can submit a blank response. Passwords use their own input path.
- Up recall/edit and Ctrl+Up/Down reorder apply to queued shell entries; selected head holds draining. Shell entries have no agent send-now arrow.
- Standalone Claude/Codex TUI guest shell submissions use !command in that guest's input queue, not the native shell queue.
Open design topic: strict shared FIFO versus independently draining shell/agent queues, including intentional cross-resource dependencies. Not decided by acceptance of the agent-only contract.
Interrupt design analysis and options (proposal, not yet approved):
Problem: Esc currently targets shell only with an empty composer and idle agent, but targets agent when busy; Ctrl+C in composer is copy-only. Stop can pause the shared list. Local queued-command failure pauses on nonzero status, while SSH finishLoginCommand excludes status 130, so interrupt/pause semantics are inconsistent.
Earlier familiar-key option (Ctrl+C interruption rejected; current shortcut contract is in Decisions):
- Do not add Ctrl+C interruption to the Relay composer. Keep its copying behavior. Use the owner-approved Esc/Alt+Esc contract below in Decisions.
- Ctrl+Shift+C is copy-only. Explicit Terminal Interrupt action/button bypasses selection.
- Superseded Esc proposal: see the later owner decision in Decisions. Queue editing and questions no longer consume Esc; popup dismissal and native terminal input remain exceptions.
- Ctrl+Esc override rejected by owner; do not introduce it.
- Clearly named Stop agent and Interrupt terminal controls remain visible while their resource is active even with zero waiting items. This refines the earlier hide-empty-queue proposal: hide empty waiting lists, not controls for active work.
- Explicit stop immediately pauses only that resource's queue; retain queued items and drafts. Implement pause intent before awaiting completion so races, handled interrupts, exit code 0/130 and local/SSH differences cannot start the next item. The other queue continues unless it has an explicit dependency.
- Terminal interrupt means normal terminal interrupt, not forced termination; show Interrupt sent/still running until confirmed. Agent cancellation similarly waits for completion.
- Resume is per queue; never silently resume both. Exact empty-Enter resume targeting remains a design detail to settle alongside two-column keyboard navigation. Agent send-now replacement remains distinct from plain Stop and follows the accepted send-now contract.
Rejected alternative: Ctrl+C always interrupts the terminal. Owner instead chose Esc based on running resources, with agent priority when both run, and Alt+Esc explicitly targeting shell. Draft text and route mode must not change that target.
Suggested verification: both resources running, zero waiting items, selected output, nonempty drafts, active questions/popups, native input, local/SSH commands, interrupted process returning 0 or 130, and finish/interrupt races. No implementation performed.
