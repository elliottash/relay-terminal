---
id: C52H
type: work
status: needs-verification
labels: [feature, board, gui]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: a2b66c29-5d0f-4b5a-bfe4-614e18a61c9c
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzy
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
source: Relay pane, 2026-09-25, answering what the top-of-board strip is
links: {plans: [], commits: [4d1ba7c023ad, 5dc545acbd84, 8659c5763b92, d30cb6379a8a], evidence: [docs/qa_evidence/2026-09-25-C52H/], related: [TBRH, EA37], github: null, merged_from: [C1A8]}
---
# The Board's object tabs: Live rows per pane, and Background work on Ctrl+Shift+B

## Issue
The Live strip at the top of the Cards tab reads as noise: a wall of token/model chips mixed with card chips. The user wants it off the Cards tab entirely and rendered as rows — one row per open pane (pane, model, busy state, the cards it holds) — under its own tab in the board's tab row instead of the chip flow.

> "i think it should be on a different tab than the cards, and they should be rows"
> — elliott · [session:da8c2a806ad7450da6674120a858f773](relay://session/da8c2a806ad7450da6674120a858f773) · 2026-09-25

## Merged in
### #C1A8 — Background work as a Board tab, opened by Ctrl+Shift+B (2026-09-25)

Merged from `.board/features/2026-09-25-background-work-as-a-board-tab-opened-by-ctrl-sh.md` (executing): Same surface and one direction: the Board pane's object tabs. The owner asked to fold the new Background-tab request (with its Ctrl+Shift+B key) into the Live-tab card while that work is landing.

#### Issue
Background work (sessions kept running after their pane closed) is only reachable as a drill-in page inside the Sessions tab of the Sessions & Projects pane. The user wants it as a tab in the Board page with a direct hotkey, Ctrl+Shift+B.

> "whats the hotkey to view background tasks. we need that in a tab in the board i think" / "ctrl shift b is a good candidate"
> — elliott · [session:c0e1e4195a18460c8bb0bdd212aad8de](relay://session/c0e1e4195a18460c8bb0bdd212aad8de) · 2026-09-25

#### Done means
- The Board pane (Ctrl+Shift+A) has a fifth object tab, **Background**, beside Live: one row per background pane with its state (working / needs you / done / failed / interrupted) and an action that focuses or reopens that pane; an empty-state line when there is none.
- **Ctrl+Shift+B** (`background.open`) opens the Board straight onto the Background tab, and closes it again when pressed while the Board is the focused pane — the same toggle rule as Ctrl+Shift+A.
- The Ctrl+Shift+B clash is resolved in every preset: the warp preset no longer binds files.explorer to it. Warp's files.explorer lands unbound rather than on Ctrl+Shift+D, which is pane.splitRight in that preset (Warp's real split binding).
- The Sessions pane's nested Background page and its hidden button are gone; "Recently closed" keeps its page. The palette entry for background work moves to the Board section and opens the Background tab.
- `tests/boardpane_test.cpp` covers the new tab (rows, empty state, focus callback); the Sessions-page test for the old nested page is removed with the page.

#### Tasks

- [ ] BoardView: Page::Background, kPageDefs entry, build/syncBackgroundPage, backgroundPanes + onFocusBackground callbacks, showBackgroundPage() <!-- t:x5 -->
- [ ] Keymap: background.open on Ctrl+Shift+B; unbind files.explorer in the warp preset; retune sessions.open label/comments <!-- t:1w -->
- [ ] Window: dispatch background.open, wire backgroundPanes/onFocusBackground, retarget the palette entry, drop openSessions("background") <!-- t:s5 -->
- [ ] Sessions pane: remove the nested background page (Conversations.{h,cpp}) and the window-side registration <!-- t:8n -->
- [ ] Tests: boardpane_test background tab; keymap_test default binding; remove the conversations background-page case <!-- t:h4 -->
- [ ] Docs: ARCHITECTURE.md and KEYBINDING-PRESETS.md updates <!-- t:v0 -->

## Tasks

- [x] Live strip off Cards to its own Live tab, one row per pane <!-- t:9t -->
- [x] BoardView: Background tab, rows, callbacks and direct open entry point <!-- t:0x -->
- [x] Keymap: background.open on Ctrl+Shift+B; free the Warp preset collision <!-- t:hq -->
- [x] Window: dispatch background.open, wire background panes and focus, move palette entry <!-- t:3c -->
- [x] Sessions pane: remove the nested Background page and its registration <!-- t:z4 -->
- [x] Tests: Board background tab, keymap, and Sessions navigation <!-- t:vn -->
- [x] Docs: shortcut, architecture, and Board tab descriptions <!-- t:ma -->

## Done means
Top-level summary of the merged scope (the #C1A8 copy under Merged in stays verbatim):

- **Live tab**: the Live strip is gone from the Cards tab; a Live object tab renders one row per open pane (pane, model, busy state, the cards it holds).
- **Background tab**: a Background object tab beside Live renders one row per background pane with its state and an action that focuses/reopens it; empty state when none.
- **Ctrl+Shift+B** (`background.open`) opens the Board straight onto the Background tab and closes it again on repeat — the Board's toggle rule. The warp preset gives up files.explorer's Ctrl+Shift+B (files.explorer lands unbound there: Ctrl+Shift+D is pane.splitRight in warp).
- The Sessions pane's nested Background page and its hidden button are gone; the palette's background-work entry opens the Board's Background tab.
- `tests/boardpane_test.cpp` covers both tabs; the Sessions-page test for the old nested page goes with the page.

## Plan
Goal: put background work beside Live in the Board, with Ctrl+Shift+B opening that tab.

Findings: BoardView's object tabs and Live rows are in src/BoardPane.{h,cpp}; RelayWindow creates and focuses Board panes in src/RelayWindow.h; src/Keymap.h owns presets. The old nested Background page lives in Conversations and RelayWindow.

Steps:
1. Let the active Live-tab landing finish, then extend the resulting BoardView tab row with Background rows and an openBackgroundPage entry point.
2. Supply project background panes and focus callbacks from RelayWindow; route background.open to the Board toggle/open path, and update palette and background notices.
3. Move Ctrl+Shift+B from warp's files.explorer to background.open; retire the Sessions nested Background page.
4. Add targeted Board, keymap and Sessions tests; update shortcut and Board docs.
5. Build and run targeted tests with land.py try; land code through land.py and record evidence on this merged card.

Risk: other sessions edit this shared checkout. Snapshot only the paths touched, and review the land diff before committing. Verify: Board rows, empty state, focus action, shortcut and toggle behavior in targeted tests; capture the visible tab in an isolated profile if feasible.

## Tests
### Check · 2026-09-25

- `ctest -R boardpane` — tests/boardpane_test.cpp
- `ctest -R keymap` — tests/keymap_test.cpp
- `tests/test_keybindings.py::GuiDefaultsTests::test_qwas_defaults`

All three checks passed in Relay's recorded run for this revision. The isolated land.py gates passed for 5dc545ac (keymap), 8659c576 (boardpane), and d30cb637 (app build and Sessions navigation). The full Conversations suite has an unrelated stale Helper Agent label expectation; its affected navigation case passes.

## Execution Summary
The Live strip is now a Live object tab with one row per pane. The Board's Background tab lists this project's retained panes, state, and Reopen action. Ctrl+Shift+B opens it directly in every preset; Warp's former explorer binding is unbound there. The nested Sessions Background page and its palette entry were retired. Commits: 5dc545ac, 8659c576, d30cb637.

![Background tab showing a Needs you row and Reopen](docs/qa_evidence/2026-09-25-C52H/background-tab.png)
