---
id: XZZB
type: work
status: needs-qa-llm
labels: [feature, settings, gui, ux]
rank: i
created: '2026-09-17'
source: pane 1, 2026-09-17
links: {plans: [], commits: [624e750, cec6427, f1cd2ea, 7e17fd3], evidence: [docs/qa_evidence/2026-09-19-options-menu-followups/], github: null, related: [05J2]}
---
# Improve the options (Settings) menu

## Request
analyze the options menu and propose ways to improve it

## Analysis of the current menu
Evidence gathered 2026-09-17 from `src/ModelSettings.cpp`, `RelayWindow::settingsSections()` / `settingsRowItems()` / `settingsMenuItems()` in `src/main.cpp`, and the palette code.

**What works — keep:** one row catalog with reader/writer closures (QSettings stays the source of truth) renders both the window and the palette; modeless dialog; honest per-row detail text; accessible names on controls; reopen reuses and rebuilds the window.

**Findings**
1. **No search in the window**, and the palette's search cannot reach settings rows: `renderPalette()` flattens only one level of submenu children, so rows two levels deep (Settings › Section › Row) never enter the search corpus — the rows' `aliases` field is effectively dead metadata.
2. **Coverage gaps / orphans:** `agent/show_tool_output` and `notifications/desktop` are read but no UI writes them; stall timeout, log detail and the log folder live only in the palette's Diagnostics section, not in the window; isolation keys exist only in `relay.conf`.
3. **Stale window:** nothing rebuilds an open Settings window when a setting changes elsewhere (palette toggle, Roles dialog, theme change); it only rebuilds on its own edits and on open.
4. **Rebuild resets scroll and focus:** every toggle/choice/button queues a full `rebuild()` that destroys and recreates all pages — long sections (Agent 11 rows, Voice 8) jump back to the top and lose keyboard focus after each change.
5. **Palette nested-toggle bounce:** the `stayOpen` restore searches `rootItems()` only, so after toggling a row inside Settings › <Section> the palette drops that stack frame and lands on the section list instead of showing the row's new state (`src/main.cpp` ~10325).
6. **Inconsistent commit semantics:** Number rows write on every `valueChanged` (a burst of QSettings writes + `agentOptionsChanged` per arrow click, and no rebuild); Text rows commit on `editingFinished`; the compaction-threshold row silently ignores invalid input with no feedback.
7. **Small hit targets:** only the right-hand QCheckBox toggles; the label is not its buddy and the row is not clickable.
8. **No reset-to-default and no changed indicator** per row or per section.
9. **Wall-of-text blurbs** (Voice, Appearance, Privacy) and ungrouped long sections.
10. **Entry point:** no toolbar button; in the warp/vscode presets `app.settings` is unbound (Ctrl+, goes to keybindings edit), leaving the palette as the only path.
11. **"Start a fresh window set" runs immediately** with only an after-the-fact notice.

## Proposals
Ordered by value/effort; each names the findings it fixes.

**Tier 1 — quick wins (each small and independent)**
- **Search box in the Settings window** (fixes 1): a filter row above the pages that matches label, detail and the already-existing `aliases`, hides non-matching rows and dims non-matching sections. Reuses the palette's `fuzzyScore`.
- **Make settings rows searchable from the palette root** (fixes 1): flatten two levels (Settings › Section › Row) or add one synthetic "Settings: all rows" submenu whose children are the flattened rows.
- **Preserve scroll and focus across rebuilds** (fixes 4): save each page's scrollbar position and the focused row id in `rebuild()` and restore them.
- **Refresh an open window on external changes** (fixes 3): rebuild on window activation, or emit a small settings-changed signal from the row writers.
- **Fix the palette nested-toggle bounce** (fixes 5): re-run the popped frame's `children()` instead of searching `rootItems()` by label.
- **Commit spinboxes on `editingFinished`** (or apply on popup close) instead of per-click `valueChanged` (fixes 6).
- **Whole toggle rows clickable** (fixes 7): label as checkbox buddy or a clickable row widget.
- **Validate text rows with feedback** (fixes 6): the compaction threshold should refuse bad input visibly (red outline or a notice), not silently ignore it.
- **Browse… button for path rows** (Plans folder) via `QFileDialog::getExistingDirectory`.

**Tier 2 — coverage and structure**
- **Add the missing rows** (fixes 2): "Show tool output" (Agent), "Desktop notifications" (General), and a Diagnostics section in the window (stall timeout, log detail, open log folder) so the window is the complete list; link to `relay.conf` for isolation.
- **Group long sections** (fixes 9): subheadings inside Agent (Instructions & skills / Turn limits) and Voice (Capture / Model / Key); cap blurbs at two lines with the rest behind a "?".
- **Per-row reset-to-default** (fixes 8): a small ↺ on rows whose value differs from the fallback; the catalog already knows every fallback.
- **"Applies to the next conversation" chip** as a consistent visual tag instead of prose in the detail text.
- **Toolbar ⚙ / "Settings…" button** (fixes 10) — with a shortcut hint per the WARP.md standing rule if a new fast path appears.
- **Confirm before "Start a fresh window set"** (fixes 11).

**Tier 3 — larger**
- **Changed-settings indicator** (fixes 8): dot on section names with non-default rows.
- **Export/import of settings** — already planned separately as #05J2; keep the two cards linked.
- **About/footer** in the window: version, engine in use, docs link.

## Notes
**Today's shape, for context:** Ctrl+, (Relay preset) or palette → Settings opens a modeless dialog — section list left (General, Appearance, Models, Terminal, Agent, Voice, Privacy, Shortcuts), rows right; the same row catalog renders as palette submenus, so every setting keeps a keyboard path. All proposals above stay inside that catalog design.

**Implementation/QA notes:** everything proposed is GUI/QSettings-only — no protocol changes (`docs/AGENT-SESSIONS-PROTOCOL.md` untouched). Verify under Xvfb with an isolated `XDG_CONFIG_HOME` per the project routine; any new fast path needs a shortcut-hint entry (WARP.md standing rule).

## Update (2026-09-18): the Settings pane

The owner asked for settings as a full pane rather than a strip (card SP4N,
`issues/changes/needs_qa_llm/2026-09-18-settings-as-a-full-pane.md`). `src/SettingsPane.{h,cpp}`
replaces both the palette overlay and the compact dialog. Of the findings above it settles:
1 (search in the pane, over settings rows and actions, aliases live), 2 (Show tool output, Desktop
notifications, Log detail, Log folder and the stall timeout are rows now), 4 (a rebuild keeps the tab,
scroll offset, highlight and focused control), 5 (no nested palette any more), 6 (spin boxes write once
per finished edit), 7 (the whole toggle row is clickable), 9 (headings inside Agent, Voice and General),
10 (the gear and Ctrl+Shift+A open it; Ctrl+, still does). Still open: 3 (a pane open in another tab
is redrawn on keymap reload and theme reload, not on every external write), 8 (per-row reset and a
changed indicator), 11 (confirm before "Start a fresh window set"), the Browse… button for path rows,
and export/import (#05J2).

## Update (2026-09-19): the rest of the list, except #05J2

Findings 3, 8 and 11 and the Browse… button are implemented (`624e750` the pane, and the catalog
half beside it). What each one turned into:

- **3 — a value is never one edit behind.** `relay::SettingsWatch` (src/SettingsPane.h) is one
  process-wide place to say a setting was written. Every live `SettingsPane` listens and redraws, so
  an Options pane in another tab or another window follows an edit made anywhere — a control on
  another pane, a page reset, a keymap or theme reload, a saved local model. Delivery is queued and
  a burst collapses into one, because the rebuild deletes the control whose signal asked for it.
  `RelayWindow::refreshSettingsPanes()` is now that notify, and a window coming forward while it
  shows an Options pane redraws it, which catches the writers that cannot announce themselves: a
  dialog that has just closed, a hand-edited `relay.conf`, a second Relay.
- **8 — the changed indicator and the per-row reset are one mark.** A row whose value is not what
  Relay ships with carries a ↺ between its words and its control, and its page carries a dot on its
  tab. The row says so itself (`SettingRow::changed`, set by every row helper and by the four rows
  written out by hand), so the pane knows no settings keys. The ↺ asks nothing first — one row, in
  front of you, set again in a click — where the page's "Reset to defaults" asks.
- **11 — "Start a fresh window set" asks first.** It is one row down an Actions list, nothing on
  screen changes when it runs, and what it throws away (every window, tab, pane and directory that
  would have come back) only shows at the next start.
- **Browse…** on the Plans folder row (`SettingRow::browse`), writing through the row's own callback
  so a folder picked and a folder typed are the same edit. `SettingsPane::setFolderChooser` is the
  seam tests use, since a QFileDialog cannot be clicked headless.

That leaves **only #05J2** (export/import, and optional sync) from the proposals above, which is its
own card. Tier 3's "About/footer" and the "Applies to the next conversation" chip were not done and
are not lost work: both are cosmetic and neither has been asked for since the pane replaced the
dialog — raise them again if the pane ever gets an About line.

Evidence: `docs/qa_evidence/2026-09-19-options-menu-followups/`.

## Resolution (found already delivered by the 2026-09-19 board sweep)

This card is an analysis plus a tiered proposal list, and both the analysis and the proposals it
called Tier 1 and Tier 2 were delivered — `624e750` and `cec6427` ("The Options pane marks what you
changed, browses for a folder, and never shows a stale value"), with the 2026-09-19 update on the
card itself (`f1cd2ea`) recording findings 3, 8 and 11 and the Plans-folder Browse…, and `7e17fd3`
adding the implementer evidence under
`docs/qa_evidence/2026-09-19-options-menu-followups/`.

The card's own closing note already says what is left: **#05J2** (settings export/import) is the one
proposal still open and is tracked on its own card, and the two cosmetic Tier 3 items are named as
not-done rather than left implied. Nothing on this card needs doing; it had simply never left
`inbox`.

## QA checklist
- [ ] The five hand checks named in `docs/qa_evidence/2026-09-19-options-menu-followups/NOTES.md`.
- [ ] Search in the Options window reaches rows by their `aliases`, and the palette finds Settings rows from the root.
- [ ] Toggling a row keeps scroll position and keyboard focus; an open window refreshes when a setting changes elsewhere.
- [ ] The ↺ appears only on rows that differ from the default and puts them back.
- [ ] "Start a fresh window set" asks first.
