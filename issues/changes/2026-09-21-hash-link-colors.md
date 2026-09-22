---
id: HCR2
type: work
status: needs-verification
labels: [bug, terminal]
assignee: codex
rank: m
created: '2026-09-21'
source: 'Codex in a Relay pane, 2026-09-21'
links: {plans: [], commits: [ca46089c302ba6d2f2b22563fdd695c999ed1f37], evidence: [docs/qa_evidence/2026-09-21-hash-link-colors/README.md], related: [K9KC], github: null}
---
# Color hash references in rewrapped replies

## Issue
@/home/elliott/.cache/RelayTerminal/relay/images/relay-paste-20260921-215111.png the hash code links arent getting colored appropriately

## Done means
Known #ID references wear the theme link color before and after reply resizing, including bold references. Unknown IDs stay plain. Disabling link coloring removes automatic coloring; clicks still resolve.

## Plan
**Goal:** Keep link ink consistent across grid and rewrapped prose.
**Findings:** TerminalView::paintProseRow and paintFoldRow color explicit links only, although linkAt resolves bare references.
**Steps:** Add cached resolution for fold cells; apply existing plain-ink rules; test pixels and targets across widths.
**Risks:** Preserve intentional program colors, role ink and search highlighting.
**Verify:** Targeted ViewTest cases under Xvfb with isolated configuration; exact-tree build at landing.

## Execution Summary
Added cached link resolution to rewrapped prose and expanded fold painting. Resolved references in plain ink use the theme link color, preserving bold weight and existing color/search/role protections. Regression coverage checks known and unknown references, link targets, widths and disabling automatic coloring.

## Tests
`manual: docs/qa_evidence/2026-09-21-hash-link-colors/README.md`
