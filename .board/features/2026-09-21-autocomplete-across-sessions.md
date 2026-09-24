---
id: CMEM
type: work
status: needs-verification
labels: [feature, terminal]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: mcmem
created: '2026-09-21'
source: Codex in a Relay pane, 2026-09-21
links: {plans: [], commits: [5ddc0f142c5a3a64ffae3acde29f748e29cb7cf1], evidence: [docs/qa_evidence/2026-09-21-autocomplete-across-sessions/], related: [FR72], github: null}
---
# Command autocomplete remembers across new sessions

## Issue
feature reqest -- terminal command memory should persist across sessions

i meant for autocompelte, eg "sudo apt" doesnt add update when i open a new session

## Plan
**Goal:** Typing a prior shell command prefix in a new pane offers its saved suffix.

**Findings:** `Pane::historySuggestion` reads only pane-local history and the shell history file. The loaded-command acknowledgement is the existing boundary that records actual terminal commands. `PromptHistory` already provides private, bounded append storage.

**Steps:**
1. Add a separate persistent command suggestion store and cached prefix lookup to `src/PromptHistory.*`; clear it with Clear prompt history.
2. Record acknowledged shell commands and consult the shared store after pane-local suggestions in `src/Pane.h`; teach the existing Right-arrow accept path with a hint.
3. Test restart/new-reader lookup, refresh across writers, clearing, and pane-history separation; build and exercise the UI in isolation.

**Risks:** Do not aggregate agent prompts or program/password input. Keep existing pane-local ranking and Up/Down behavior. Shared writes must serialize trimming.

**Verify:** Targeted prompthistory/editor tests and isolated GUI prefix/accept check; evidence under `docs/qa_evidence/2026-09-21-autocomplete-across-sessions/`.

## Tests
`ctest -R prompthistory`
`ctest -R editor`
manual: docs/qa_evidence/2026-09-21-autocomplete-across-sessions/

Built both test targets with scripts/relay-build; both passed via ctest --test-dir build. New cases cover new-session lookup, cache refresh, clearing and pane-history separation.

## Execution Summary
Added a persistent shared store for acknowledged terminal commands, separate from pane Up/Down history. New panes and restarted Relay processes consult it for autocomplete; Clear prompt history removes it. Added a Right-arrow acceptance hint. Built and tested; live screenshots and reproducible driver: `docs/qa_evidence/2026-09-21-autocomplete-across-sessions/`.

## QA checklist
- [ ] Submit a harmless command, open a new pane, and confirm its suffix appears from a prefix.
- [ ] Restart Relay and repeat in a fresh pane; accept with Right.
- [ ] Confirm Up/Down does not inherit another pane's history.
- [ ] Confirm Agent mode, multiline text, and disabled history suggestions suppress ghost text.
- [ ] Clear prompt history and check saved shared commands are gone (the independent shell-history fallback may still suggest shell-owned entries).
