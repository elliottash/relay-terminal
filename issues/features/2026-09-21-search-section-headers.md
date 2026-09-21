---
id: S3JH
type: work
status: needs-verification
labels: [feature, actions, options, search]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: m
created: '2026-09-21'
source: Codex in Relay, 2026-09-21
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-21-search-section-headers/evidence.md], related: [], github: null}
---
# Search and jump to Actions and Options sections

## Issue
also in actions and options, allow the text search to match the section headers, you click on them to clear the filter and zoom to the section

## Plan
Index Options tabs and section headings plus Actions categories and submenu headings as navigable search results. Activation clears search, selects the appropriate page and scrolls to the heading, expanding a collapsed Options section if needed. Preserve ordinary search results and keyboard activation. Verify both modes and scrolling under Xvfb.

## Execution Summary
Section search results cover Options tabs, headings and subheadings, and Actions categories, submenu headings and Recent. Clicking or pressing Enter clears the filter and navigates within the pane, switching Actions/Options when appropriate. Heading targets scroll into view, and folded Options parents expand. Navigation bypasses action execution so the pane stays open.

## Tests
ctest:settings
manual: docs/qa_evidence/2026-09-21-search-section-headers/evidence.md

## QA checklist
- [ ] Search an Actions category or submenu heading; click it and check the filter clears and the section appears.
- [ ] Search an Options tab or subsection heading; activate it with Enter and check navigation stays in the pane.
- [ ] Search a subheading inside a collapsed Options section; check its parent opens and the heading is visible.
- [ ] Ordinary option/action search results still work and section navigation executes no action.
