---
id: SFC1
type: work
status: needs-verification
labels: [bug, terminal, ui]
assignee: codex
implemented_by: openai/gpt-5.6-sol via codex
rank: m
created: '2026-09-21'
source: Codex in a Relay pane, 2026-09-21
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-21-shift-click-external/], related: [GWXM], github: null}
---
# Shift-click file links should open externally

## Issue
shift click of a file link didnt work to open it externally. can you fix that 

it was this bit:
631f6a0f146f4616987d58715d4afa6f

The file is saved at:
/home/elliott/admin/Advisees/joe-lamb/Dissertation-Report-Lamb-Ash.docx

## Done means
- Shift-clicking an existing local file or folder link in terminal output opens that path through the desktop's external application handler.
- Plain clicks still open file links inside Relay, and Ctrl-click behavior remains unchanged.
- A focused Qt test fails if Shift-click is ignored or its modifier is lost between the terminal view and the Relay pane.

## Plan
**Goal:** Make the advertised external-open action work for mouse links as it already does for keyboard-selected links.

**Findings:** `engine/view/TerminalView.cpp` only activates links for plain or Ctrl-click and `engine/TerminalBackend.h` does not carry click modifiers to `src/Pane.h`; `Pane::openOutputLink()` already defines Shift as external open for keyboard navigation.

**Steps:**
1. Carry keyboard modifiers with the terminal link-activation signal and backend callback.
2. Route Shift-clicked local paths through `QDesktopServices` before ordinary in-Relay path routing.
3. Add engine and pane-level regression coverage, then run the focused build/tests.

**Risks:** The link activation signature crosses the engine/GUI boundary; every emitter and receiver must be updated together. Remote-login paths must keep their existing remote routing.

**Verify:** Run the focused engine view test and the Relay test target covering pane output links, then inspect the exact landed diff.

## Execution Summary
Shift-click activation now preserves mouse modifiers across `TerminalView`, `VTermBackend`, and `TerminalBackend`. `Pane` routes Shift-activated local paths to the desktop application handler before card/context/preview routing, while remote-login paths keep their existing remote behavior. Added engine-level modifier coverage and pane-level external-routing coverage. Evidence: `docs/qa_evidence/2026-09-21-shift-click-external/`.

## Tests
- `ctest --test-dir build --output-on-failure -R '^(relay-engine-tests|consolemode)$'`
- `RELAY_SESSION=codex-shift-click scripts/relay-build --target relay`
- `RELAY_SESSION=codex-shift-click scripts/relay-build --target relay-engine-tests relay-consolemode-tests`
- `manual: docs/qa_evidence/2026-09-21-shift-click-external/`
