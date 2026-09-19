---
id: QG60
type: work
status: needs-qa-llm
labels: [feature, switchboard]
assignee: agent
implemented_by: claude-opus-4-5
rank: zzzzzzw
created: '2026-09-19'
links: {plans: [], commits: [86440a6], evidence: [docs/qa_evidence/2026-09-19-put-shortcuts-in-parentheses-in-switchboard/], related: [], github: null}
---
# put shortcuts in parentheses in switchboard

## Issue
put shortcuts in parentheses in switchboard, eg (n) after new card. (x) after execute.

## Plan
**Goal.** Every clickable control in the Switchboard pane that has a keyboard shortcut shows it in parentheses in its visible label — "+  New card (n)", "Execute (x)" — using the keys the pane already handles. Tooltips, placeholder and the bottom key legend already carry them; the labels don't.

**Findings.** All UI is in `src/BoardPane.cpp`; keys are handled in `CardDetail`'s eventFilter (Enter/`e`/`p`/`x`/`d`/Esc, lines 1259-1281) and `BoardView::keyPressEvent` (`n`, `/`, `e`, `p`, `x` at 3232-3250; `m`, `c`, `y`, `t`, `o` at 3390-3415; Ctrl+Z at 3226). Labels are set at: list tools `m_add` "+  New card" (1912), `m_back` "←  Back to board" (1616), notice "Undo" (1656); card header "Edit" / "#ID → prompt" / "Open file" (742-747) and "×" (753); reply row Comment/Discuss/Plan/Execute (858-869, text re-set by the `set()` lambda in `setModeTips()`, 1338-1351 — running state becomes "Stop", 1341); edit frame "Cancel"/"Save" (824-827). "Clean up" (1922) and the cleanup panel's Apply/Changelog/Dismiss have no key. The reply placeholder (850) already teaches Enter discusses / Ctrl+Enter plans / Ctrl+Shift+Enter comments; `p` and `x` also fire from the board list (3248-3250). Tests find buttons by exact label via the `button()` helper (`tests/boardmodel_test.cpp:1334-1341`) and QCOMPARE "←  Back to board" (760); "Clean up"/"Stop" QCOMPAREs (966, 978, 1015, 1114, 1148) stay valid.

**Steps.**
1. List page: `m_add` → "+  New card (n)" (1912); `m_back` → "←  Back to board (Esc)" (1616); notice Undo → "Undo (Ctrl+Z)" (1656).
2. Card header (742-747): "Edit (e)", "#ID → prompt (t)", "Open file (o)". Leave the "×" close glyph bare — its tooltip keeps "(Esc)".
3. Reply row: labels "Discuss (Enter)", "Plan (p)", "Execute (x)", "Comment (Ctrl+Shift+Enter)" (858-869 and the `set()` calls at 1345-1350). Suffix with the key that actually fires that action from the card page; the running state stays plain "Stop" (1341).
4. Edit frame: "Cancel (Esc)" (824), "Save (Ctrl+Enter)" (827).
5. Leave unchanged: "Clean up", cleanup-panel buttons, quick-add placeholder, the `m_keys` legend (2813-2824).
6. Tests (`tests/boardmodel_test.cpp`): make the `button()` helper (1338) accept a suffixed label — `candidate->text() == text || candidate->text().startsWith(text + QStringLiteral(" ("))` — so "Stop" and the suffixed labels both match; update 760 to the new Back label; add assertions that a card's four reply buttons read "Discuss (Enter)" / "Plan (p)" / "Execute (x)" / "Comment (Ctrl+Shift+Enter)".
7. No shortcut-hint registry entry: no shortcut is added or changed, only labels (the WARP.md rule covers new fast paths).

**Risks.** The reply row and the wrapping tools row (2054-2067) grow a few characters per label — check a ~350 px pane for eliding. "Comment (Ctrl+Shift+Enter)" is long; recommendation is to keep it (it is the real key and the owner asked for shortcuts in parentheses), the alternative being modifier combos only in tooltips — say so if you prefer that. Only `src/BoardPane.cpp` and `tests/boardmodel_test.cpp` change.

**Verify.** `./scripts/test.sh` and `ctest --test-dir build` pass; under Xvfb with an isolated `XDG_CONFIG_HOME`, open a board and see "+  New card (n)", open a card and see the suffixed reply/header buttons, then narrow the pane to ~350 px and confirm the rows still fit or wrap cleanly.

## QA checklist
- [x] `cmake --build build` clean; `ctest -R '^board'` — board + boardworkspace pass with the suffixed labels and the new assertions (evidence: `docs/qa_evidence/2026-09-19-put-shortcuts-in-parentheses-in-switchboard/evidence.md`).
- [ ] `ctest --test-dir build` fully green — one run had `backend-and-bash` failing while this label-only change shared a dirty worktree with other cards' work; re-run for a clean signal.
- [ ] `./scripts/test.sh` passes (skipped this session at the owner's request).
- [ ] Under Xvfb, a live board shows `+  New card (n)`, `←  Back to board (Esc)`, `Undo (Ctrl+Z)`, and an open card shows `Edit (e)` / `#ID → prompt (t)` / `Open file (o)` / `Comment (Ctrl+Shift+Enter)` / `Discuss (Enter)` / `Plan (p)` / `Execute (x)` / `Cancel (Esc)` / `Save (Ctrl+Enter)`.
- [ ] At ~350 px pane width the reply row and header row still fit or wrap cleanly (skipped this session at the owner's request).
- [ ] `Clean up`, the cleanup panel, `Stop` and the `m_keys` legend are unchanged.

**Superseded in part by #VZ69 (2026-09-19):** the card detail lost its Comment and Discuss buttons
(Enter and Ctrl+Shift+Enter in the reply box do both now), the running mode's button no longer
becomes "Stop" (a strip over the reply box does it, labelled "✕ Stop planning" / "✕ Stop
discussing"), and "Edit (e)" is now the pencil "✎ Edit (e)" at the right of the title. QA this card
against the labels that still exist: `+  New card (n)`, `←  Back to board (Esc)`, `Undo (Ctrl+Z)`,
`✎ Edit (e)`, `#ID → prompt (t)`, `Open file (o)`, `Plan (p)`, `Execute (x)`, `Verify (v)`,
`Cancel (Esc)`, `Save (Ctrl+Enter)`.
