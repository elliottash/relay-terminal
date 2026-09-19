---
id: 5G43
type: work
status: needs-qa-llm
implemented_by: glm/glm-5.3
rank: zzzzzzzy
created: '2026-09-19'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-switchboard-new-card-in-inbox/], related: [], github: null}
---
# new cards getting put into in progress rather than inbox

## Issue
new cards were someties getting put into in progress rather than inbox. not sure why, maybe that section was highlighted or something. but that shouldnt happen by default.

## QA checklist
Fix: `n` and the toolbar button target the board's intake section (the one whose dropStatus is `inbox`, else the first section that takes new cards), never the selection's section; `focusedSection()` removed; a section's own + still adds into that section; tooltip "New card in Inbox (n)".

- [ ] `ctest -R '^board$'`: quickAddNamesTheSectionItAddsTo — `n` with a Ready card selected sends `board_create` with `status: "inbox"` and the field names Inbox; `quickAddIn("ready")` still names Ready to start; Done still falls back (verify.log).
- [ ] `ctest -R boardsections|boardworkspace` passes; `scripts/relay-build` clean.
- [ ] Live: GUI starts under Xvfb with isolated `HOME`/`XDG_CONFIG_HOME`; in the Switchboard, select a card in In progress, press `n`, type a title, Enter — the new card is in Inbox.
- [ ] The section + button still creates in that section.

Evidence: docs/qa_evidence/2026-09-19-switchboard-new-card-in-inbox/
