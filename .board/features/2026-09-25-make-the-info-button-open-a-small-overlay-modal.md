---
id: 7EWF
type: work
status: planned
labels: [feature, gui]
component: [gui]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# make the info button open a small overlay modal rather than a new pane

## Issue
make the info button open a small overlay modal rather than a new pane. we can also simplify the information shown. the cost is not needed. the history should be a clickable link instead of showing it.

as part of this, i also think i dont want to show the pane id any more as copyable, id rather show session ids. 

is there a clear icon we can put after the pane header, where you click it and it copies the session id. 

remove the auto tag from the pane header

i would also want to put the dim pane icon back at the top right i think. or maybe, we just have info,  hamurger icon, new pane, and close pane.  the hamburger icon, drops down and has move to new tab, move to background, and dim. 

finally, i think the new pane icon should be a rectangle with a + at the top right.

## Decisions
Owner, 2026-09-25, from feedback in a Claude guest pane:

- The overlay replaces the #P1CP hover popover outright. Hovering ⓘ does nothing but show its tooltip.
- No history in the overlay, not even as a link — rewind shows it.
- The header icon copies the full Relay session ID (`Pane::sessionId()`, the one in `relay://session/…` and Board links). The overlay keeps the short pane ID (`sessionToken().left(8)`) as small secondary text, so `pane <id>` in logs, `land.py` claims and FOREIGN-hunk reports can still be matched to a pane. The icon is hidden while the pane has no session.
- The `auto` badge after a model-written title is dropped, with no replacement.
- The top-right row is ⓘ, ☰, new pane, × — in that order. ☰ opens Move to new tab, Move to background (terminal panes only), Dim. This supersedes the 2026-09-17 rule that move-to-tab is always on screen (owner: "that rule is stale").
- New-pane icon: a painted rectangle with a small + at its top-right corner.

## Done means
- In every terminal pane the top-right row reads ⓘ ☰ new-pane ×, and ⓘ is there even with the chain chip or share button above/below the row (it is currently missing: see Plan, step 1).
- Clicking ⓘ, Alt+I or `/status` opens a small overlay anchored under ⓘ, over the pane, showing model, context, tokens, session (full ID + short pane ID), started, turns and instructions — no cost, no history. The same action, Esc, or a click outside closes it; no info pane is created.
- ☰ drops a menu with Move to new tab, Move to background (terminal panes only) and Dim, each showing its live shortcut; choosing one from the menu shows that action's shortcut hint.
- The pane header has no `auto` badge, and a copy icon after the title copies the full session ID and confirms it; the icon is absent on a pane with no session.
- The new-pane button is a painted rectangle with a + at its top-right corner, legible in light and dark themes.

## Plan
**Goal.** A quiet top-right row (ⓘ ☰ new-pane ×), conversation info as an overlay instead of a pane, and a header that offers the session ID instead of the `auto` badge.

**Findings.**
- ⓘ is missing today because `RelayWindow::syncChrome()` (`src/RelayWindow.h` ~7598) finds the button row by casting `column->itemAt(0)` to a layout. An in-progress edit to `PaneChrome` (not landed yet, from another session) inserts `PaneChainChip` at index 0, so the cast fails and ⓘ is silently skipped. It has broken this way once before (2026-09-20 comment).
- The row is built in the `PaneChrome` constructor (`src/PaneChrome.h` ~706–727): ⊞ new pane, ⇱ new tab, ↗ background (Pane only), ×. `refreshTooltips` appends live keys from the `keysFrom`/`action` properties.
- `PaneInfoPopover` and `InfoButton` live in `src/SessionInfo.{h,cpp}`; the hover popover also carries Dim state through `PaneChrome::paintDimming`. Tests in `tests/conversations_test.cpp`.
- `agent.info` (`src/RelayWindowCore.cpp` ~421) toggles an Info `ToolPane` via `Pane::openInfo()` / `infoPaneOf()`. `InfoView` is also how the Sessions pane and subagent-thread links show a saved session or thread — those paths keep the pane; only the ⓘ/Alt+I/`/status` path moves to the overlay.
- `renderInfo()` (`src/SessionInfo.cpp` ~449, rows at ~258–318) draws Model, Context, Tokens, Cost, Session, Started, Turns, Instructions, then the history.
- The badge is `m_titleAuto` in `src/Pane.h` (~17460, and its width reservation ~17424). `Pane::sessionId()` is the Relay session ID.

**Steps.**
1. **ⓘ slot owned by PaneChrome.** Give `PaneChrome` an `addLeading(QWidget*)` (or construct ⓘ itself when the leaf is a `Pane`) and have `syncChrome()` call it instead of digging through layouts. Fixes the disappearance independently of the chain chip; land first.
2. **Button row.** Replace ⇱ and ↗ with a ☰ `QToolButton` whose `QMenu` holds Move to new tab, Move to background (Pane only), Dim (checkable, reflecting `dimming.manual`). Menu entries show `Keymap::shortcutText`, run through `onAction`, and trigger the per-action mouse hints. Paint the new-pane icon (rect + corner +) as a `QIcon` from a `QPainter` in theme ink, like the other chrome glyphs; keep `pane.newByMouse` and its `keysFrom`.
3. **Overlay.** Replace `PaneInfoPopover` with `PaneInfoOverlay`: a frameless child of the pane anchored under ⓘ, closes on Esc/outside click/`agent.info` again, focus back to the composer. It requests the same info event the Info pane uses and renders `renderInfo(..., compact=true)`: no Cost row, no history, Session row = full ID with copy + the short pane ID in muted text. Route ⓘ/Alt+I/`/status` to it; `openInfo()` keeps serving Sessions and thread links. Remove the hover-popover code and its Dim plumbing from `paintDimming`.
4. **Header.** Remove `m_titleAuto` and its width reservation. Add a small copy icon after the title (hidden when `sessionId()` is empty) that copies `sessionId()` and flashes a ✓/status line. The header stays the drag handle; the icon swallows its own click.
5. **Docs and tests.** Update `Keymap` text for `agent.info` (no longer mentions history), `docs/ARCHITECTURE.md` chrome notes, the ⓘ comment in `PaneChrome`. Replace the popover tests in `conversations_test.cpp` with overlay tests (opens, no Cost/history, toggle closes, no Info pane created), add a chrome-row test (ⓘ present with a chain chip in the column; ☰ menu contents per pane kind), and a header test (no badge; copy icon copies the session ID). Offscreen grabs, light and dark, into `docs/qa_evidence/2026-09-25-pane-chrome-row-and-info-overlay/`.

**Risks.** `PaneChrome.h`, `RelayWindow.h` and `Pane.h` are being edited by other sessions right now (the chain chip is uncommitted); claim with `land.py begin` per step, keep each step's hunks small, and build through `land.py try`.

## Tasks

- [ ] ⓘ slot owned by PaneChrome; syncChrome stops digging through layouts (fixes the missing button) <!-- t:1f -->
- [ ] Row becomes ⓘ ☰ new-pane ×; ☰ menu with Move to new tab / Move to background / Dim; painted new-pane icon <!-- t:yb -->
- [ ] PaneInfoOverlay replaces the hover popover and the Info pane for ⓘ/Alt+I//status; compact info without Cost or history <!-- t:g3 -->
- [ ] Header: drop the auto badge; add copy-session-ID icon <!-- t:9e -->
- [ ] Tests, docs, Keymap text, light/dark evidence <!-- t:aw -->
