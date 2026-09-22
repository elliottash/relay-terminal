---
id: 62M4
type: work
status: needs-verification
labels: [feature, remote]
assignee: codex
implemented_by: openai/gpt-5.6-sol via codex
rank: zzzzzzzzzzzzzzzzw
created: '2026-09-22'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-phone-indicator-icon-only/], related: [], github: null}
---
# the phone indicator in the panes, remove the word phone -- the icon is enough

## Issue
the phone indicator in the panes, remove the word phone -- the icon is enough

## Done means
- A pane shared only with the owner’s phone shows the phone glyph without the word “phone”.
- Guest counts and active driver names remain visible because they carry information beyond the glyph.
- The indicator retains its existing tooltip and warning treatment; failure is any lost icon, tooltip, or sharing/driver label.

## Plan
**Goal**

Make the default phone-sharing pane indicator icon-only without removing contextual sharing text.

**Findings**

- `src/SharingPane.cpp` emits `phone` for a shared pane with no guests.
- `src/PaneChrome.h` independently substitutes and initializes the same label.
- `tests/sharingpane_test.cpp` covers the model’s default and contextual chip states.

**Steps**

1. Make the no-guest model state carry an empty display label while retaining its tooltip.
2. Let `PaneChrome` render an empty phone-chip label rather than restoring `phone`.
3. Update the focused model test and run its CTest target plus a production build.

**Risks**

An empty label also represents an unshared model result, so visibility must continue to come only from the separate shared-phone boolean. Guest counts and driver labels must remain unchanged.

**Verify**

Run the targeted sharing-pane test, build the exact landing tree, and capture an isolated GUI screenshot showing the icon-only state.

## Tasks

- [x] Change the default shared-phone label to icon-only <!-- t:vc -->
- [x] Preserve contextual guest and driver labels <!-- t:k9 -->
- [x] Run targeted test, build, and GUI evidence <!-- t:j1 -->
- [x] Commit and move card to needs-verification <!-- t:ss -->

## Execution Summary
- Removed the default `phone` display text from the sharing model and from the pane chrome fallback/initialization.
- Kept the phone glyph, tooltip, alarm treatment, guest counts, and active driver names intact.
- Updated the sharing-model regression coverage and added reproducible live GUI evidence at `docs/qa_evidence/2026-09-22-phone-indicator-icon-only/`.

## Tests
- `scripts/relay-build --target relay-sharing-tests`
- `ctest --test-dir build -R '^sharing$' --output-on-failure`
- `scripts/relay-build --target relay`
- `docs/qa_evidence/2026-09-22-phone-indicator-icon-only/drive.sh`
- `manual: docs/qa_evidence/2026-09-22-phone-indicator-icon-only/icon-only.png`
