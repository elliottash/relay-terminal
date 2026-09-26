---
id: C1A8
type: work
status: dropped
labels: [feature, board]
assignee: agent
implemented_by: glm/glm-5.3
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
source: pane 1, 2026-09-24 20:10
links: {plans: [], commits: [], evidence: [], related: [], github: null, merged_into: C52H}
---
# Background work as a Board tab, opened by Ctrl+Shift+B

## Issue
Background work (sessions kept running after their pane closed) is only reachable as a drill-in page inside the Sessions tab of the Sessions & Projects pane. The user wants it as a tab in the Board page with a direct hotkey, Ctrl+Shift+B.

> "whats the hotkey to view background tasks. we need that in a tab in the board i think" / "ctrl shift b is a good candidate"
> — elliott · [session:c0e1e4195a18460c8bb0bdd212aad8de](relay://session/c0e1e4195a18460c8bb0bdd212aad8de) · 2026-09-25

## Done means
- The Board pane (Ctrl+Shift+A) has a fifth object tab, **Background**, beside Live: one row per background pane with its state (working / needs you / done / failed / interrupted) and an action that focuses or reopens that pane; an empty-state line when there is none.
- **Ctrl+Shift+B** (`background.open`) opens the Board straight onto the Background tab, and closes it again when pressed while the Board is the focused pane — the same toggle rule as Ctrl+Shift+A.
- The Ctrl+Shift+B clash is resolved in every preset: the warp preset no longer binds files.explorer to it. Warp's files.explorer lands unbound rather than on Ctrl+Shift+D, which is pane.splitRight in that preset (Warp's real split binding).
- The Sessions pane's nested Background page and its hidden button are gone; "Recently closed" keeps its page. The palette entry for background work moves to the Board section and opens the Background tab.
- `tests/boardpane_test.cpp` covers the new tab (rows, empty state, focus callback); the Sessions-page test for the old nested page is removed with the page.

## Tasks

- [ ] BoardView: Page::Background, kPageDefs entry, build/syncBackgroundPage, backgroundPanes + onFocusBackground callbacks, showBackgroundPage() <!-- t:x5 -->
- [ ] Keymap: background.open on Ctrl+Shift+B; unbind files.explorer in the warp preset; retune sessions.open label/comments <!-- t:1w -->
- [ ] Window: dispatch background.open, wire backgroundPanes/onFocusBackground, retarget the palette entry, drop openSessions("background") <!-- t:s5 -->
- [ ] Sessions pane: remove the nested background page (Conversations.{h,cpp}) and the window-side registration <!-- t:8n -->
- [ ] Tests: boardpane_test background tab; keymap_test default binding; remove the conversations background-page case <!-- t:h4 -->
- [ ] Docs: ARCHITECTURE.md and KEYBINDING-PRESETS.md updates <!-- t:v0 -->

## Resolution
Merged into [#C52H](../2026-09-25-move-the-live-strip-off-cards-to-its-own-live-ta.md) on 2026-09-25: Same surface and one direction: the Board pane's object tabs. The owner asked to fold the new Background-tab request (with its Ctrl+Shift+B key) into the Live-tab card while that work is landing.

Nothing was thrown away: the text above is also kept on #C52H under `## Merged in`, and this card stays here so `#C1A8` keeps resolving.
