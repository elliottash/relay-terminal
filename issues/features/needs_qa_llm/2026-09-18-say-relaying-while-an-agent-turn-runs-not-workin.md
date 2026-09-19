---
id: 4E13
type: work
status: needs-qa-llm
labels: [feature, ui]
component: [gui]
milestone: beta
workstream: terminal
assignee: agent
implemented_by: claude
rank: zzzzzu
created: '2026-09-18'
acceptance: 'while work runs the pane shows a bold right-aligned Relaying line above the prompt, violet with the live action for agent turns and blue for a terminal program, and a blinking relay mark in the header; the transcript Done/Need/Problem bolds render green/amber/red and follow the theme'
source: issues/feature_intake.txt
links: {commits: [], evidence: ['docs/qa_evidence/2026-09-19-relaying-status-language/'], github: null, plans: [], related: [CVHT, V8KT, MQ9C, Y4RX]}
---
# Say "relaying…" while an agent turn runs, not "working"

## Issue
add "relaying..." when working rather than "working" or "warping"

this is good but it should be in bold at the bottom right above the prompt, like in codex etc. it should say Relaying and in purple if agent work, blue if terminal program. 

in the pane header, it should be a blue blinking relay icon (terminal program running) or purple blinking relay icon (agent working). gray or black is idle. green is done, amber/orange is needs human input? 

use this to think about the broader visual l anguage / status indicators. in the terminal. questions or items needing human response could be in bold amber. when the agent is saying they are done, that could be in bold dark green. etc.

## Plan
The owner's card (`issues/features/2026-09-18-say-relaying-while-an-agent-turn-runs-not-workin.md`) asks for three things: a bold "Relaying…" line above the prompt (violet for agent work, blue for terminal work), a blinking relay icon in the pane header in the same two colours (gray idle, green done, amber needs-you), and the terminal transcript's bolds brought into the same language (done = bold dark green, needs-you = bold amber).
## Current state
Most of the machinery already exists; this card restyles and relocates it rather than building anew.
* The busy word is already "Relaying…": `stateLabel(State::Working)` at src/PaneStatus.cpp:40, shown DemiBold beside the header glyph by `PaneStateWord` (src/PaneChrome.h:851). Covered by `theBusyStateSaysRelaying` in tests/panestatus_test.cpp:79.
* Header: `paintStateGlyph` (src/PaneChrome.h:203) draws per-state shapes — gray ring idle, blue triangle running, violet star working, green tick done, red crossed disc failed, amber exclamation diamond needs-you — with a gentle scale breath for live states (`pulseScale`, steps 1.0/0.90/0.82/0.90). Colours already match the owner's ask exactly (`stateInk`, src/PaneStatus.cpp:381).
* Near the prompt: the only turn indicator is the plain "thinking · 48 s · Esc stops" label in the strip *under* the prompt box (`tickTurnClock`, src/Pane.h:7779; label created at src/Pane.h:2260). Nothing near the composer shows terminal-program work; that lives in the header glyph and the floating program banner (`updateTakeOver`, src/Pane.h:10832).
* Transcript: card #CVHT taught agents `**Done:**`/`**Need:**`/`**Problem:**` labels, rendered bold magenta / blue / red (`Palette` in src/MarkdownAnsi.h:45-47, keyword lists src/MarkdownAnsi.cpp:64-72). The owner now revises those colours to green/amber so the transcript speaks the same language as the header.
* The Relay mark exists as a one-colour symbolic icon (chevron + dash + ringed dot, `data/icons/org.relayterminal.Relay-symbolic.svg`), redrawable as QPainter primitives like every other glyph.
## Proposed changes
### 1. Bold "Relaying…" line directly above the prompt box
* New thin status row in the pane's column, inserted between the queue strip and the composer frame (src/Pane.h:2201-2244), right-aligned, DemiBold, hidden when nothing runs.
* Agent work says what it is doing right now — "Relaying <action>…" (owner, 2026-09-19: "Relaying [action] for agent would also be good … eg thinking, reading, etc."). The action is the live tool call's gerund, already computed as `Label::running` in src/ToolLabel.cpp ("reading src/Pane.h", "running pytest") and captured on `tool_started` (src/Pane.h:7111); between calls it is "thinking"; blocked on background work it is "waiting for 2 subagents" (`waitingSubject`, src/PaneStatus.cpp:110); blocked on a question card it is "waiting for your answer", in amber — the one needs-you case that can happen mid-turn (#MQ9C's `m_ask`). Full line: "Relaying reading src/Pane.h… · 12 s · Esc stops" in the agent violet.
* Terminal work only: "Relaying <program>…" in the terminal blue (owner-approved wording: one verb, colour says whose work it is).
* Subagents running with no turn of its own: "Relaying waiting for 2 subagents…" in violet, no clock (no turn is running).
* Colours come from `panestatus::stateText` (src/PaneStatus.cpp:394), which lifts the state ink to 4.5:1 on the pane background, so the line stays legible in every shipped theme.
* The line *replaces* the plain "thinking · 48 s …" label in the under-prompt strip (`m_turnClockLabel`, src/Pane.h:2260) — moved and restyled, not duplicated. It is a painted, eliding widget (the PaneStateWord pattern, src/PaneChrome.h:851) placed as the first row of the composer frame so native mode hides it with the composer; `tickTurnClock` (src/Pane.h:7779) retargets to it, tool_started/tool_result update the action, and terminal-program updates ride `refreshStatusStrip` (src/Pane.h:11157), which the shell poll already calls on every tick.
### 2. Pane header: the relay mark blinks for live work
* Add `paintRelayMark` to src/PaneChrome.h: the symbolic icon's chevron + dash + ringed dot as QPainter primitives in one ink, scaled into the same 14-unit box as the other glyphs.
* `paintStateGlyph` (src/PaneChrome.h:203) uses the relay mark for the three *live* states — Running (blue), Working (violet), Subagents (violet; the count badge beside it still disambiguates). The news states keep their distinct shapes (tick, crossed disc, diamond): they are the only colour-free way to tell done from failed from needs-you, and the owner only asked for the relay icon on the two live states. Gray ring idle stays.
* "Blinking" is implemented as a stronger two-step scale blink (full ↔ ~0.65) on the existing 600 ms clock, not an opacity blink: the contrast rule in src/PaneStatus.h:63 forbids fading ink. Reduce-motion (cursor flash time 0) still shows the mark statically at full size. Tab icons inherit all of this through the shared `tabIcon`/`paintStateGlyph` path (src/PaneChrome.h:292).
### 3. Terminal transcript: align the labelled bolds with the state colours
* `Palette` (src/MarkdownAnsi.h:45-47): `done` "1;35" → "1;32" (bold green), `need` "1;34" → "1;33" (bold amber), `problem` stays "1;31" (bold red). Indexed colours only — absolute RGB is burnt into scrollback and breaks theme switching (the comment block at src/MarkdownAnsi.h:19-30).
* Keyword lists (src/MarkdownAnsi.cpp:64-72) and the system-prompt line (backend/relay_core/agent.py:149) already say the right words and name no colours, so they stand. This amends the colour decision of in-progress card #CVHT; the cards get cross-linked (`links.related`), CVHT's text stays untouched per the append-only rule.
### 4. Comments and tests
* Update the visual-language comments in src/PaneStatus.h (the live-states block at lines 13-18 and 50-56) to describe the relay mark, the blink, and the green/amber/red transcript bolds as one language.
* tests/markdownansi_test.cpp: new SGR expectations (1;32 / 1;33 / 1;31) in the labelled-bold cases and the palette-completeness case.
* tests/panestatus_test.cpp: `pulseScale` expectations for the blink steps (currently asserts the 0.90/0.82 breath at lines 110-116).
* tests/pulsepaint_test.cpp (+ tests/pulsepaint_paint.cpp): pixel expectations for the relay-mark glyph in live states.
* tests/test_agent.py: no change expected (the prompt line names no colours); run to confirm.
### 5. Issue bookkeeping
* Write this plan into the card's `## Plan` section (the board's format, as in card #CVHT), keep status `in-progress`, link plans/related. On landing: move to `features/needs_qa_llm/` with implementer evidence under `docs/qa_evidence/2026-09-19-relaying-status-language/`, per issues/README.md.
## Verification
* `./scripts/test.sh` and `ctest --test-dir build` pass (updated markdownansi, panestatus, pulsepaint suites; backend pytest).
* Live under Xvfb with an isolated `XDG_CONFIG_HOME`, screenshots into the evidence dir:
    * Start an agent turn → bold violet "Relaying… · N s" right-aligned above the prompt; header mark blinks violet; block it on a subagent → the waiting wording appears in the same line.
    * Run `sleep 30` in the terminal → bold blue "Relaying… · sleep" above the prompt; header mark blinks blue.
    * A reply containing `**Done:**`, `**Need:**`, `**Problem:**` renders bold green / amber / red; a plain `**bold**` stays uncoloured.
    * Set cursor flash time to 0 → the mark sits still at full size; switch theme → scrollback bolds and all status colours follow (no burnt-in RGB).
## Deferred (not in this card)
* An amber needs-you treatment *above the prompt* (e.g. when a program asks `[Y/n]`): the floating program banner already says it; unifying that is a separate call.
* Whether the relay mark should replace the news-state shapes too — kept as-is deliberately; easy to revisit.

## Tasks

- [x] bold "Relaying…" row directly above the prompt: violet for agent turns with the live action ("Relaying reading src/Pane.h… · 12 s · Esc stops"), "thinking" between calls, "waiting for N subagents…" when blocked, blue "Relaying <program>…" for terminal work; replaces the old under-prompt turn clock <!-- t:y7 -->
- [x] `paintRelayMark` in `src/PaneChrome.h`; the three live header states draw the relay mark with a two-step scale blink on the 600 ms clock; news states keep their shapes <!-- t:jk -->
- [x] labelled bolds recoloured: done `1;32` green, need `1;33` amber, problem stays `1;31` red (`src/MarkdownAnsi.h`), amending #CVHT's colours (cross-linked, its text untouched) <!-- t:nj -->
- [x] visual-language comments in `src/PaneStatus.h`; `docs/ARCHITECTURE.md` records the language <!-- t:dm -->
- [x] tests updated: `tests/markdownansi_test.cpp` (SGR expectations), `tests/panestatus_test.cpp` (blink steps), `tests/pulsepaint_test.cpp` (relay-mark pixels) <!-- t:ks -->
- [x] implementer evidence: `docs/qa_evidence/2026-09-19-relaying-status-language/` <!-- t:4p -->

## Implementation

Assignee: agent (Claude Opus 5). `src/Pane.h`: `PaneBusyLine`, a painted eliding right-aligned DemiBold row between the queue strip and the composer frame — `tickTurnClock` retargets to it, `tool_started`/`tool_result` and the subagent counter set the action, the shell poll's `refreshStatusStrip` drives the terminal-program line, and the old `m_turnClockLabel` is gone. `src/PaneChrome.h`: `paintRelayMark` (chevron + dash + ringed dot) and its use for Running/Working/Subagents in `paintStateGlyph`. `src/PaneStatus.{h,cpp}`: `pulseScale` blink steps {1.0, 0.62, 1.0, 0.62} and the visual-language comments. `src/MarkdownAnsi.h`: `Palette::done` `1;32`, `Palette::need` `1;33`. `src/RelayWindow.h` and `docs/ARCHITECTURE.md` follow the new pieces. Tests: `tests/markdownansi_test.cpp`, `tests/panestatus_test.cpp`, `tests/pulsepaint_test.cpp`.

## QA checklist

- [ ] `src/MarkdownAnsi.h`: `Palette::done/need/problem` default to `1;32` / `1;33` / `1;31` — indexed ANSI, no `38;2;`/`38;5;` — and the comments name the one-language decision (green done, amber need, red problem, matching the header states).
- [ ] `src/PaneStatus.cpp`: `pulseScale` walks {1.0, 0.62, 1.0, 0.62} on the 600 ms clock — a two-step size blink, never an opacity fade (the contrast rule); with cursor flash time 0 the mark sits still at full size.
- [ ] `src/PaneChrome.h`: `paintStateGlyph` draws the relay mark for Running (blue), Working (violet) and Subagents (violet, count badge beside it); done/failed/needs-you keep tick / crossed disc / diamond; idle keeps the gray ring; tab icons inherit through the shared path.
- [ ] `src/Pane.h`: the busy line is hidden when nothing runs; an agent turn shows the violet "Relaying <action>… · N s · Esc stops" row; terminal work shows blue "Relaying <program>…"; a subagent wait shows violet "Relaying waiting for N subagents…"; nothing remains of the old under-prompt clock label.
- [ ] `ctest --test-dir build`: the markdownansi, panestatus and pulsepaint suites pass with the new expectations (52/52 that ran at hand-off; `backend-and-bash` and `./scripts/test.sh` were skipped at the owner's instruction after a git-credential prompt hang — environment, not assertions).
- [ ] Live under Xvfb with an isolated `XDG_CONFIG_HOME`: re-run or read `docs/qa_evidence/2026-09-19-relaying-status-language/` (`drive.sh`, `README.md`, `analysis.txt`) — the OCR lines show the "Relaying …" wordings, the colour-band analysis shows violet vs blue vs nothing-when-idle, and the frame diffs prove the header mark blinks.
- [ ] A reply's `**Done:**` / `**Need:**` / `**Problem:**` words come out green / amber / red in relay-dark *and* a second theme (the evidence uses ibm-beige) — theme-following indexed colours, no burnt-in RGB; a plain `**Bold**` stays uncoloured in both.
- [ ] QA reviewer: a non-Claude model (the implementer was Claude).
