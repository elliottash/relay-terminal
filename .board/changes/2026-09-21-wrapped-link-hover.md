---
id: H7WR
type: work
status: needs-verification
labels: [bug, terminal]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: m7h
created: '2026-09-21'
source: Codex in a Relay pane, 2026-09-21
links: {plans: [], commits: [76091e05e8e2bdf9e0f3652b63dabeefb3e43c87], evidence: [docs/qa_evidence/2026-09-21-wrapped-link-hover/], related: [], github: null}
---
# Underline the whole wrapped link on hover

## Issue
small bug to file and fix: if a link straddles multiple lines, if i hover over it, only the bit on the line i am hovering has the underline indicator show up. it would be better if the underline showed up on the whole link

## Plan
Preserve visible row segments for a hovered link in TerminalView, including wrapped plain links, OSC 8 runs, and fold/prose labels. Paint and invalidate every segment together. Verify with rendered mouse-hover regression checks under Xvfb and targeted existing link tests; retain a QA checklist for independent verification.

## Execution Summary
Hover keeps all visible segments of the same wrapped link, paints them together, and clears them together on pointer exit or changed content. Handles plain scanned links and OSC 8 links, with fold/prose label segments preserved. Evidence: docs/qa_evidence/2026-09-21-wrapped-link-hover/README.md.

## Tests
manual: docs/qa_evidence/2026-09-21-wrapped-link-hover/README.md

## QA checklist
- [ ] Hover the first, middle, and last rows of a long wrapped URL or file path; every visible segment underlines.
- [ ] Move onto nearby plain text or outside the terminal; all hover underlines clear.
- [ ] Check an OSC 8 / Markdown label that wraps, and repeat after resizing and scrolling.
- [ ] Separate links and surrounding text remain unhighlighted; clicking still opens the correct target.
