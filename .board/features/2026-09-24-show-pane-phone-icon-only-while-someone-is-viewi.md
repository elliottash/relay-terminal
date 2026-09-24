---
id: T279
type: work
status: needs-verification
labels: [feature, remote, ui]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 57059618-4892-4fb3-aadb-fea2903b7860
rank: zzzzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
verify: {artifact: visual, primary: script, also: [person], human: optional, criteria: The phone icon appears only on a pane actively viewed from another connected device or guest., sign_off: none, effort: medium, stakes: rework, blast: capability}
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-24-phone-icon-T279/], related: [], github: null}
---
# Show pane phone icon only while someone is viewing

## Issue
remove the phone icon for panes unless somebody is connected and viewing it

## Done means
- A shared pane with no connected viewer has no phone icon.
- The icon appears while an online guest or paired device views that pane, and clears after blur or disconnect.
- Viewer counts and driving labels reflect only current connections.

## Plan
**Goal:** Show the header phone icon only for live viewers.

**Findings:** `src/RelayWindowCore.cpp` passes publication state to `PaneChrome`; `remote/host.py` tracks device subscriptions; `src/SharingPane.cpp` holds guest and device presence.

**Steps:** 1. Send device pane subscriptions from the sidecar on focus, blur and disconnect. 2. Derive per-pane live viewer presence in the sharing model and apply it in the header. 3. Cover presence transitions in focused tests.

**Risks:** A published pane may remain accessible while the icon is hidden, as requested. **Verify:** sharing model test, remote presence test and Relay build, plus visual capture if a live pairing is available.

## Tests
- `ctest --test-dir build -R '^sharing$' --output-on-failure` — passed, including published, focused, blurred and offline states.
- `python3 -m unittest tests.test_remote_gui_host.AlwaysOnTests.test_start_with_always_brings_the_service_up_at_the_hosted_rendezvous` — passed, including device pane focus and blur reports.
- `scripts/relay-build --target relay` — passed.

### Check
Pass: the sharing model hides the chip without a live pane viewer and the sidecar reports focus and blur transitions.

## Execution Summary
The hub now reports each connected guest's and paired device's open panes. The sharing model uses that live per-pane presence to show the header phone icon and clear it on blur or disconnect. Viewer labels exclude offline guests. The desktop window build passed. Focused test logs: `docs/qa_evidence/2026-09-24-phone-icon-T279/`. A live visual capture still belongs to the independent verification stage.
