---
id: B7SP
type: work
status: needs-verification
labels: [bug, terminal, guest]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: m7s
created: '2026-09-21'
source: Codex in a Relay pane, 2026-09-21
links: {plans: [], commits: [a5e1613724fb08354fe1a71ee5992c8a790fc5f6], evidence: [docs/qa_evidence/2026-09-21-terminal-bottom-gap/], related: [W7DC], github: null}
---
# Blank space at the bottom while Codex thinks

## Issue
i observed something weird though, that there was like reserved black space at the bottom of the pane, when codex was thinking -- even when no text was printing

## Planning notes
The guest adapter emits thinking events only for nonempty text. That alone does not rule out blank space caused by widget sizing or terminal fold layout. Determine whether the reported gap is inside the terminal viewport or between it and the composer before changing geometry.

## Decisions
Owner clarified: "inside the terminal, below the output, between the terminal and the prompt box, like 7 lines".

## Plan
Goal: remove unexplained blank rows below terminal output.

Findings: engine/view/TerminalView.cpp synchronizes one core viewport to the first real row of the visual window. Prose replacements can compress real rows, contradicting its assumption that this covers all visible rows.

Steps:
1. Add a focused GUI reproduction covering compressed prose followed by terminal output and a silent interval.
2. If reproduced, correct frame coverage and verify painting and interaction for the lower rows.
3. Run targeted engine GUI tests under Xvfb and record evidence.

Risks: the observed Codex session is not captured; a deterministic reproduction must establish the fault before treating this hypothesis as the cause. Preserve scrolling, selection, and normal terminal behavior.

## Execution Summary
Reproduced lower rows missing after prose compression. TerminalView now fetches every visible real row, preserves the core frame for remote consumers, and maps selection/hit tests to the corresponding core viewport. Evidence: docs/qa_evidence/2026-09-21-terminal-bottom-gap/. Original live Codex turn is not captured; confirm that scenario visually. Independent double-click finding recorded as W7DC.

## Tests
`RELAY_ENGINE_TEST=ViewTest QT_QPA_PLATFORM=xcb xvfb-run -a build/engine/relay-engine-tests compressedProseKeepsFollowingOutputVisible foldOpensUnderItsAnchorAndShutsAgain foldRowsCountInTheScrollRangeAndScrollOneByOne aFoldStaysUnderItsLineAcrossAResize proseReflowsOnResize selectionCrossesTheFoldBoundaryInVisualOrder markdownLinkLabelsAreClickable`
manual: docs/qa_evidence/2026-09-21-terminal-bottom-gap/README.md — engine/tests/ViewTest.cpp

## QA checklist
- [ ] In a newly built Relay, run a Codex turn after resizing a pane containing prose; confirm no approximately seven-row black gap appears below output during a quiet interval.
- [ ] Confirm the last rows remain clickable and selectable and scrolling away/back preserves output.
- [ ] Compare docs/qa_evidence/2026-09-21-terminal-bottom-gap/libvterm.png with the focused reproduction.
- [ ] Verify with Ghostty when available; implementer build enabled libvterm only.
