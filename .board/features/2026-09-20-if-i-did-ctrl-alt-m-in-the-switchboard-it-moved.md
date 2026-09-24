---
id: GZAE
type: work
status: discussing
waiting_on: owner
rank: zzzzzzzzzzzzzzzz
created: '2026-09-20'
links: {related: [AGNT]}
---
# if i did ctrl alt m in the switchboard, it moved me to a pane

## Issue
if i did ctrl alt m in the switchboard, it moved me to a pane. it shouldnt do that. it should ideally use that for the switchboard agent. but otherwise just dont do anything.

## Plan
**Goal.** Ctrl+Alt+M (Keymap `agent.model`) pressed while the Switchboard is up must drive the Switchboard agent's model — the same picker its own model box and `/model` drive — or do nothing at all. It must never fire on `m_active`'s terminal pane and yank the user out of the board. Alt+M (`agent.modelBox`) gets the same treatment.

**Findings.**
- `src/RelayWindow.h` `runActionNow` (~line 1141): `agent.modelBox`/`agent.model` route to the helper only when `helperComposerHasFocus()` (line 1513, i.e. `focusedHelperModelBox() != nullptr`). Otherwise line 1155 calls `pane->openModelPicker()` with `pane = m_active` (line 1064) — the last active *terminal* pane — and showing that dialog pulls the user off the Switchboard onto that pane. That is the reported bug.
- `focusedHelperModelBox()` (lines 1502–1510) walks `QApplication::focusWidget()` up its parents: a `HelperChatPanel` returns its box (1505), a `ToolPane` returns `tool->board() ? tool->board()->focusedModelBox() : nullptr` (1508–1509). So the board branch exists, but only when keyboard focus is *inside* the board ToolPane. When the Switchboard is showing but focus is elsewhere in the window (tab bar, toolbar, card list edge cases, a just-clicked chrome widget), the helper branch misses and the key lands on `m_active`.
- `openHelperModelPicker()` (line 1453) is already tab-scoped (`helperModelState(m_tabs->currentWidget())`) and writes the persisted `switchboard` role — it is the right target. `openHelperModelBox()` (line 1514) depends on `focusedHelperModelBox()`, so it has the same gap.
- `BoardView` already has what a fix needs: `focusedModelBox()` (`src/BoardPane.cpp:4468`), `focusChat()` (4454), and the card page's own box (the card page's model box, `m_cardModelBox`, #BRD3).
- The key reaches `runActionNow` through the window `eventFilter` (798–922: `Keymap::instance().match(key)` at 900, deferred `runAction(id)` at 922), so it fires regardless of which widget has focus.

**Steps.**
1. Reproduce first: Relay under Xvfb with an isolated `XDG_CONFIG_HOME`, open the Switchboard, put focus on the card list / anywhere in the board, press Ctrl+Alt+M; confirm focus/visibility jumps to a terminal pane. Note exactly where focus was — this pins down which arm of step 2 is the live one.
2. In `runActionNow` (src/RelayWindow.h ~1141), widen the helper branch for `agent.model` and `agent.modelBox`: route to the helper when `helperComposerHasFocus()` **or** when the active leaf is a board ToolPane — `if (auto *tool = dynamic_cast<ToolPane *>(m_activeLeaf.data()); tool && tool->board())`. For `agent.model` call `openHelperModelPicker()` (unchanged; it already writes the `switchboard` role).
3. In `openHelperModelBox()` (1514), when `focusedHelperModelBox()` returns null but the active leaf is a board ToolPane, fall back to `tool->board()->focusedModelBox()`, focus it (via `BoardView::focusChat()` / the box itself) and drop it open — so Alt+M on the Switchboard opens the Switchboard agent's model box.
4. Keep the fall-through safe: if neither the helper branch nor a pane applies, return without touching `m_active` (the issue's "otherwise just don't do anything"). Do not add a broad guard for other `agent.*` actions — out of scope for this card.
5. Docs touch-up: `README.md` line 94's Alt+M / Ctrl+Alt+M row should say that on the Switchboard (and helper panels) these keys drive the Switchboard/helper agent's model, not the pane's. No new shortcut-hint registry entry — no new fast path is added.

**Risks.**
- The eventFilter fires window-wide: after the change, pressing Ctrl+Alt+M in *any* non-pane widget while a Switchboard leaf is active will open the helper picker. That is the intended reading of the issue, but if reproduction shows focus was in fact inside the board and the walk still missed (e.g. a detached popup or a focus proxy), the fix belongs in `focusedHelperModelBox()` instead — step 1 decides.
- `agent.effortBox` (Alt+E, line 1157) and other pane-scoped keys have the same shape; this card does not change them — flag as a follow-up card if the owner wants them too.

**Verify.**
- `scripts/relay-build`, then `ctest --test-dir build -R modelpicker`.
- Live under Xvfb (isolated `XDG_CONFIG_HOME`): with the Switchboard active and focus in (a) the composer, (b) the card list, (c) the card detail page — Ctrl+Alt+M opens the Switchboard agent's model picker and the view stays on the Switchboard; picking a model changes the board's model box; Alt+M drops the board's model box. Also confirm Ctrl+Alt+M in a plain terminal pane still opens the pane's own picker (no regression).
- Commit through `python3 scripts/land.py begin <me> src/RelayWindow.h README.md` / `land.py commit`, per WARP.md.
