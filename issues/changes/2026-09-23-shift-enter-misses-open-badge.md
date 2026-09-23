---
id: B4G7
type: work
status: needs-verification
labels: [bug, sessions, keyboard]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: m
created: '2026-09-23'
source: User in Relay, 2026-09-23
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-23-shift-open-B4G7/], related: [E7FP, J8QP], github: null}
---
# Shift+Enter misses the Sessions open badge

## Issue
in the session manager, it doesnt add "open" when i do shift+enter

## Done means
After Shift+Enter attaches a saved conversation, its Sessions row gains the `open` badge while the list stays focused.
An attach that fails does not leave a false `open` badge.
Plain Enter still opens the selected conversation and moves focus to it.

## Plan
**Goal.** Make Shift+Enter actually attach the saved conversation and update its open state.

**Findings.** `src/Pane.h` defers starting a ranked guest preset until a prompt even when `m_initialState` is waiting; `src/RelayWindow.h` opens the pane and the Sessions badge reads only attached pane IDs. The earlier isolated GUI run showed panes without configured agents and no `open` badge.

**Steps.** Configure a new pane promptly when it has a saved state to load, then let the existing state-loaded ID and status poll feed Sessions. Add a focused regression check and repeat the visible Shift+Enter exercise with a configured provider.

**Risks.** A saved state might have a different preset from the newly ranked default; ensure attachment can use its saved preset. Do not mark failed loads as open.

**Verify.** Build with `scripts/relay-build`, run targeted tests, and capture the Sessions row after Shift+Enter in an isolated profile.

## Execution Summary
`src/Pane.h` now carries the saved preset and model into a newly opened pane and configures it immediately when a saved state is pending. The existing `state_loaded` session ID then drives the Sessions `open` badge; failed loads cannot mark a row open. The isolated GUI run opened alpha and bravo with Shift+Enter, retained Sessions focus, and showed bravo tagged `open`.

![Bravo session tagged open after Shift+Enter](docs/qa_evidence/2026-09-23-shift-open-B4G7/02-after-bravo.png)

## Tests
- `scripts/relay-build --target relay relay-conversations-tests` — passed.
- `QT_QPA_PLATFORM=offscreen build/relay-conversations-tests keysReachEveryAction rowsCarryTheirSummaryAndTags openBadgeDistinguishesGuestSources` — 5 passed.
- `bash docs/qa_evidence/2026-09-23-shift-open-B4G7/drive.sh` — two sessions loaded; final screenshot shows `open` badge and Sessions retained focus.
- `git diff --check -- src/Pane.h` — passed.
