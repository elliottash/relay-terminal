---
id: K9KC
type: work
status: needs-verification
labels: [bug, switchboard]
assignee: codex
rank: m
created: '2026-09-21'
source: 'Codex in a Relay pane, 2026-09-21'
links: {plans: [], commits: [1f71d6050a27b8b2045448e29b3dd18997f8b5bd, dae7a639bf4a928d1dfe1f0fa63645d0cd122670], evidence: [docs/qa_evidence/2026-09-21-card-file-links/notes.md, docs/qa_evidence/2026-09-21-card-file-links/commit-audit.md], related: [GWXM, MDKN], github: null}
---
# Card file links should open the Switchboard card

## Issue
bug in session df8af0c4932e4c1a8120358358571b40.

i clikcked on the link for #GWXM and it opend the MD file rather than the switchboard card

seems like # linking regressed totally

when done, trace the commit that led to that and check for other introduced regressions

## Planning notes
Rewrapped prose uses FoldLayer rows. TerminalView::linkAt checked explicit span links, then frameRowOf returned -1 for a fold, so plain card references never reached links::scan.

## Plan
**Goal:** Restore plain #ID links after prose rewrapping and open card-file links as cards.
**Findings:** The saved reply links #GWXM to its absolute issues/features/needs_qa_llm Markdown path. Pane::openOutputTarget treats it as an ordinary file.
**Steps:** Identify card files within the pane's own board; route them through the existing card target before context dispatch; test the reported path and exclusions; build and record evidence.
**Risks:** Ordinary Markdown, thread files, other projects and explicit line links must retain file navigation. Cold card indexes must work.
**Verify:** Targeted boardworkspace tests and the Relay build, followed by isolated GUI verification.

## Execution Summary
Plain card references in rewrapped prose now use the same link scanner as original terminal rows, with positions mapped across graphemes and wrapped rows. Known IDs open cards; unknown IDs stay plain text. Card-file paths resolve against the pane's attached board before context dispatch, without waiting for its card index. Explicit line links remain file links.

Audit traced the rewrap regression to 8147cc55 and explicit label-target precedence to 195f2442 (completing a7ba08d8 and 70a288c9). The stale-file label edge case is also fixed in dae7a639. Three other measured findings are tracked on #C7WP, #J4WK and existing #8SBD; see the commit audit evidence.

## Tests
- `ctest --test-dir build -R '^(boardworkspace|outputlinks)$' --output-on-failure`
- `RELAY_ENGINE_TEST=ViewTest build/engine/relay-engine-tests markdownLinkLabelsAreClickable`
- `manual: docs/qa_evidence/2026-09-21-card-file-links/notes.md`

## QA checklist
- [ ] Click #GWXM in a reply before and after narrowing the pane; both open its card.
- [ ] Click the saved session's Markdown label and its card-file target; both open Switchboard.
- [ ] Ordinary Markdown and explicit file:line links still open the file.
