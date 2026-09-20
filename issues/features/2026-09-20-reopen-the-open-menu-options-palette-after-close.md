---
id: XAME
type: work
status: needs-verification
labels: [feature, options]
assignee: agent
implemented_by: kimi/kimi-k3
session: 97f38dbd-c712-4c45-a429-5867cd7bc94d
rank: zzzzzzzzzzzy
created: '2026-09-20'
source: pane /deliver, 2026-09-22
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-20-reopen-open-menus/], related: [], github: null}
---
# Reopen the open menu (Options, palette…) after close and re-open

## Issue
when you close and re-open, if the options (or other menu) was open, it should re-open where you were

## Plan
**Goal.** When Relay closes with an Options or Actions pane open (and the Sessions manager), the restored window brings that pane back, in the section you were reading, with your search text and the row you were on.

**Findings.**
- `ToolPane::node()` (src/PaneChrome.h ~495) returns `{}` for `m_settingsView` and `m_hosted`, so `serializeNode` (src/RelayWindow.h:6132) drops them and the saved layout never mentions them. The Switchboard already persists (`board` node) — the model to follow.
- `SettingsPane` already exposes everything needed: `mode()`, `currentTab()`, `search()`, `currentRow()`, `visibleRowIds()` (src/SettingsPane.h). Restore has `showTab()`, `setSearch()`, `revealOption()`.
- Restore path: `RelayWindow::buildNode` (src/RelayWindow.h:6032), gated by `windowstate::isUsableNode` (src/WindowState.cpp).
- Sessions manager creation+wiring lives inline in `openSessionsFor` (src/RelayWindow.h:4492); restoring it needs that wiring factored so a pane built by `buildNode` can be bound to an owner pane after placement.

**Steps.**
1. src/PaneChrome.h `ToolPane::node()`: Settings panes save `{"settings": {mode, tab, search, row}}`; the Sessions pane saves `{"sessions": {cwd, query}}`.
2. src/WindowState.cpp `isUsableNode()`: accept the two new node kinds (settings always; sessions with a cwd), so they survive `usableWindows()`.
3. src/RelayWindow.h `buildNode()`: `settings` → `createSettingsPane(mode)` + queued `showTab`/`setSearch`/`revealOption`; `sessions` → bare pane, then a queued bind through a factored `bindSessionsPane(tool, owner)` used by both `openSessionsFor` and the restore.
4. WindowState.h comment: the two new node shapes documented with the others.
5. Tests: extend the window-state/settings-pane test coverage for serialize → restore round-trip (mode, tab, search, row), then verify live under Xvfb with an isolated XDG_CONFIG_HOME: open Options at a section + row, quit, relaunch, confirm the pane is back where it was; same for Actions with a search and for Sessions with a query.

**Risks.** The `openSessionsFor` refactor must not change its reuse-path behaviour (re-feed, rebind). A restored Sessions pane in a tab with no terminal pane has no owner to bind — it degrades to unbound (no resume target) rather than blocking restore. No schema bump: older Relay ignores unknown node kinds by dropping those panes (isUsableNode), same as it treats any transient pane today.

**Verify.** `ctest --test-dir build -R` on the settings/window-state tests; live Xvfb run with screenshots in docs/qa_evidence/2026-09-20-reopen-open-menus/.

## QA checklist
Scope note: the Sessions manager was descoped to card #8EXS (its restore needs the `openSessionsFor` wiring factored, and that function was contested by another session). This card covers the Options pane and the Actions pane.

- [ ] Code read: `ToolPane::node()` settings branch (src/PaneChrome.h), `buildNode` settings branch (src/RelayWindow.h), `isUsableNode` settings case (src/WindowState.cpp) — row reveal is skipped when a search was on, an empty/unknown tab or row id degrades quietly.
- [ ] `ctest --test-dir build -R 'windowstate|settings'` passes (new `usableNodes` assertions in tests/windowstate_test.cpp).
- [ ] Evidence rerun: `docs/qa_evidence/2026-09-20-reopen-open-menus/drive.sh <build-dir>` — 8/8 checks: Options › Models saved and restored; Actions + "theme" search saved and restored (screenshots + state-NN-windows.json in that folder).
- [ ] Older-Relay compatibility: a layout with `settings` nodes read by a build without this change drops just those panes (unknown node kind), never the window.
