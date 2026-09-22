---
id: R3YN
type: work
status: needs-verification
labels: [feature, terminal, ui]
assignee: codex
implemented_by: openai/gpt-5.6-sol via codex
rank: zzzzzzzzzzzzzzzzz
created: '2026-09-21'
source: Codex in a Relay pane, 2026-09-21
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-21-r3yn-status-above-prompt/], related: [4E13, HQ2B, V7QD, KP4M, 4X53], github: null}
---
# Put the Relaying status above the prompt

## Issue
i think that "relaying..." should show right above the prompt box, rather than in it. what do you think?

plan that, as it affects a lot of things, as it should be consistnet across the terminal and helper agents, should work for agents and shell commands, etc.

yes, that too. but make that white/gray rather than violet /cyan

but they should all pulse a little bit, like codex. and the relay icon should spin

only the status line

or it can pulse like it does in the pane header

execute the plan

## Decisions
- “yes, that too. but make that white/gray rather than violet /cyan”: background subagent/job waiting moves out of the editor and uses neutral text; active agent work remains violet and shell work remains cyan.
- “only the status line” / “or it can pulse like it does in the pane header”: only the status-line Relay mark gains this motion, reusing the existing pane-header pulse; header and tab behavior do not change.

## Done means
- The Relaying line is visually outside and immediately above the prompt frame in terminal panes and every helper-agent console.
- Agent work is violet, shell work cyan, background subagent/job waiting neutral, and a question waiting on the person remains amber.
- Background-wait text no longer replaces the prompt editor's normal placeholder.
- Every visible status-line variant uses the same subtle, reduce-motion-aware pulse; header and tab indicators are unchanged.
- Native mode, narrow panes, Activity opening, accessibility text, and prompt routing continue to work.

## Plan
**Goal.** Make one shared status line outside the composer frame for terminal panes and helper consoles, with state-specific color and consistent motion.

**Findings.** `PaneBusyLine` is currently the first child of `QFrame#composer` in `src/Pane.h`; `tickTurnClock()` handles agent/ask states, `refreshBusyLine()` handles background agents and shell programs, and `refreshBackgroundWait()` duplicates background waiting into the editor placeholder. Helper consoles already reuse `Pane` with `shell: false`. Waiting wording and the synchronized pulse live in `src/PaneStatus.{h,cpp}`.

**Steps.**
1. Reparent and lay out `PaneBusyLine` immediately before the composer while preserving alignment, elision, Activity interaction, and explicit native-mode visibility.
2. Keep active agent, shell, and ask colors; render subagent/job waits in neutral theme text and remove their ownership of the editor placeholder.
3. Drive the status-line Relay mark from the existing synchronized, reduce-motion-aware header pulse for every visible status state without changing header/tab animation.
4. Add shared terminal/helper-console layout and state tests plus focused waiting/pulse coverage.
5. Update `docs/ARCHITECTURE.md` and `docs/AGENT-SESSIONS-PROTOCOL.md`, build, run targeted tests, and capture live evidence.

**Risks.** Once outside the composer, the line no longer inherits native-mode hiding; its visibility must be explicit. It must retain ignored horizontal sizing so narrow panes do not redistribute splitters. Neutral ink must retain theme contrast.

**Verify.** `scripts/relay-build --target relay-consolemode-tests`; `ctest --test-dir build -R 'consolemode|panestatus|pulsepaint'`; `scripts/relay-build`; live isolated-Xvfb checks for terminal, helper, shell, waiting, ask, narrow, native, theme, Activity, and reduce-motion states.

## Tasks
- [x] Move the shared status line outside the composer and preserve lifecycle behavior <!-- t:te -->
- [x] Make background waits neutral and remove their editor placeholder <!-- t:q6 -->
- [x] Pulse the status-line Relay mark using the existing live-state pulse <!-- t:xj -->
- [x] Add focused regression tests <!-- t:mk -->
- [x] Update docs and capture implementer evidence <!-- t:p2 -->


## Execution Summary
- Moved the shared `PaneBusyLine` out of `QFrame#composer` and placed it immediately before the composer for both terminal and shell-less helper contexts.
- Kept agent, shell, and ask state colors while rendering background subagent/job waits with neutral theme ink and leaving the normal editor placeholder intact.
- Applied the existing synchronized, reduce-motion-aware pulse to the status-line Relay mark only; added explicit native-mode suppression and preserved Activity interaction, elision, accessibility, and prompt routing.
- Removed the obsolete waiting-placeholder ladder, added hierarchy/state regression coverage and a deterministic helper-console visual fixture, and updated the architecture and agent-session protocol docs.

## Tests
- `scripts/relay-build --target relay-consolemode-tests relay-panestatus-tests relay-pulsepaint-tests relay-console-harness` — passed.
- `ctest --test-dir build -R '^(consolemode|panestatus|pulsepaint)$' --output-on-failure` — 3/3 passed.
- `scripts/relay-build --target relay` — passed (one pre-existing unrelated `ForkText::guest` initializer warning).
- Live `relay-console-harness` waiting-state capture under Xvfb shows neutral status outside the frame and the normal helper placeholder inside it; evidence: `docs/qa_evidence/2026-09-21-r3yn-status-above-prompt/`.
