---
id: YMSR
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: GLM-5.3 (Relay session), 2026-09-19
rank: i1
created: '2026-09-19'
acceptance: a pane header shows a violet chip with the number of live subagents beside the state word, and no badge at all when none are running
source: 'owner, 2026-09-19, in a Relay session on this checkout: "in the pane header, add a badge with a number for number of subagents, if applicable"'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-subagent-badge-in-pane-header/], related: [XM0T, V8KT], github: null}
---
# A subagent badge in the pane header

## Issue
in the pane header, add a badge with a number for number of subagents, if applicable

## Findings

- The pane header is one row (`Pane::headerLayout()`), filled left to right by
  `PaneChrome::buildStatus()`: the state glyph, the state's word (#V8KT), the ssh chip, the phone
  chip, the usage meter (#CPUM). Each of them takes its room from the title — `Pane::updateHeader()`
  walks the row's visible widgets and elides the title around them — so a new one costs nothing
  else.
- The count already exists in the pane: `Pane::statusFacts()` sets
  `facts.liveSubagents = m_subagents.liveCount()`, and `resolve()` turns that into
  `State::Subagents`. A badge fed from the same number cannot disagree with the glyph beside it.
- `SubagentModel::liveCount()` counts rows whose status is `waiting` or `running`. Finished rows
  stay in the model, where the strip under the composer lists them — they are not work happening
  now.
- The header's chips are painted, not styled: `src/Theme.cpp` has no rules for them, because their
  ink follows the state and the theme at paint time.
- The tooltip's key text has a precedent: `SubagentsPanel::setFolded(folded, keys)` takes the live
  Keymap text, so a rebound key is right in the strip's own folded line.
- `atLeast()` — the contrast helper every band, chip and state colour goes through — chose its pole
  with `isLight()`'s 0.35 luminance split. Between roughly 0.18 and 0.35 luminance only black can
  reach 4.5:1, while `isLight` still calls that a dark ground and sends the walk towards white,
  which cannot get there; the loop then returned white with the promise unmet. The ssh band's fill
  on `ibm-beige` — the ground the badge lands on when a session is up — is exactly such a mid-tone,
  and the new 4.5:1 assertion caught it on the first run.

## Decisions

- **Live subagents, not a session total** (the owner's "if applicable"). The count is
  `SubagentModel::liveCount()`, the same number the pane's state resolves from. A badge that kept
  counting agents that had already finished would read "3" over a pane with nothing running.
- **A read-out, not a button.** A press anywhere on the header moves the pane, and the badge is in
  the header; the key that opens the subagents pane is taught in the tooltip instead, as the folded
  strip under the composer does.
- **Zero is absent, not "0".** `subagentBadgeText(0)` is empty and the widget hides itself, so a
  pane that has never started a subagent looks exactly as it did before.

## Implemented

- `src/PaneStatus.{h,cpp}`: `subagentBadgeText(live)` (the number; empty at zero),
  `subagentBadgeTooltip(live, keys)` ("3 subagents running in this pane · Alt+A opens the subagents
  pane", and no key clause when the action is unbound), `BadgeStyle subagentBadgeStyle(ground,
  tokens)` (the agent's violet tinted like a chip — the phone chip's strength — with its ink lifted
  to 4.5:1 on that fill), and the `atLeast()` pole fix described in Findings.
- `src/PaneChrome.h`: `paintSubagentBadge()` as a free function beside `paintTypeGlyph` /
  `paintStateGlyph` (so a rendering test can paint one and measure it), the `PaneSubagentBadge`
  widget (hidden at zero; sizeHint from the number's own width; tooltip rebuilt when the count *or*
  the live key text changes; `Pane::updateHeader()` when it appears, leaves or widens) and
  `PaneChrome::setSubagents()`. It is inserted right of the state's word, before the ssh and phone
  chips.
- `src/RelayWindow.h`: `refreshPaneStatus()`, the 400 ms poll, passes `facts.liveSubagents` — the
  same value it resolved the state from.
- Docs: `docs/ARCHITECTURE.md`, the "subagent badge" paragraph in the pane-states section.
- Tests: `tests/panestatus_test.cpp` — `theSubagentBadgeCountsOnlyLiveAgents()` (empty at zero, the
  wording, the unbound key) and `theSubagentBadgeIsLegibleOnEveryGround()` (4.5:1 for the number on
  the chip's fill, a visible hairline, not a block of colour, on the header's ground *and* the ssh
  band's fill, in every shipped theme); `tests/pulsepaint_test.cpp` with `tests/pulsepaint_paint.cpp`
  — `theSubagentBadgeIsPaintedOnlyWhenItApplies()` (nothing painted at zero; the star inked in its
  cell; 3 and 8 differ only to the right of the star; at dpr 1 and 2, on both grounds).

## QA checklist

- [ ] A pane with no subagents shows no badge in its header (not a 0), and that header looks as it
      did before this card.
- [ ] One background subagent: a violet chip with a star and "1" sits right of the state's word;
      once the main turn ends the word is "Subagents working".
- [ ] Two live subagents: the badge reads 2; it drops to 1 and then disappears as they finish (live
      count, not a session total).
- [ ] A main turn still running with a subagent live: the badge reads 1 beside "Relaying…".
- [ ] Hovering the badge: "N subagents running in this pane · <key> opens the subagents pane", with
      the live Keymap text (rebinding `agent.subagentPane` changes it).
- [ ] In a narrow pane the title elides first and the badge stays whole; with an ssh session up the
      badge is still legible on the hatched band, and the band's own colours are unchanged.
- [ ] Pressing or dragging the badge moves the pane like any other press on the header; the badge
      has no action of its own.
- [ ] Every shipped theme: the number reads at 4.5:1 and the star at 3:1 (`ctest -R panestatus`,
      `ctest -R pulsepaint`).
