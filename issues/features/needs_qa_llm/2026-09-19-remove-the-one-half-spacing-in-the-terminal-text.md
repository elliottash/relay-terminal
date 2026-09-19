---
id: T7AH
type: work
status: needs-qa-llm
labels: [bug, terminal]
assignee: agent
implemented_by: glm/glm-5.3
rank: zzzzzzzw
created: '2026-09-19'
links: {commits: [8408b58b], evidence: [docs/qa_evidence/2026-09-19-single-spaced-terminal-rows/], github: null, plans: [], related: []}
---
# remove the one-half spacing in the terminal text.

## Issue
remove the one-half spacing in the terminal text. it should be single-spacing (while preserving the spacing between paragraphs etc)

## Plan
Terminal text is single-spaced: the extra per-row gap goes away and rows sit at the font's natural height. The blank lines between paragraphs are grid rows and keep their full height — nothing in this change touches them.

### Findings
- The one-and-a-half look is `LineSpacing=5` in `data/theme/terminal.conf:9` (`[Appearance]`), extra pixels added to every row. It is a holdover from Relay's old Konsole profile (see the file header) and was carried into the engine panes on 2026-09-19 (referenced as "the new five-pixel line spacing" in `docs/qa_evidence/2026-09-19-thinking-fold/`).
- Reader 1, the terminal: `EngineBackend::applyRelayProfile()` (`src/EngineBackend.cpp:52`) reads `Appearance/LineSpacing` — its fallback default is already 0 — and calls `TerminalView::setLineSpacing()` (`engine/view/TerminalView.cpp:288`, clamped 0..16). `updateMetrics()` (`engine/view/TerminalView.cpp:270`) adds it to the cell height `m_ch` and splits half above the baseline in `m_ascent`. `TerminalView.h:383` defaults `m_lineSpacing = 0`, so 0 is the code's own normal state.
- Reader 2, the remote viewer: `terminalFont()` in `src/RemotePane.cpp:469` reads the same key (fallback 2) and `RemoteScreen::fit()` adds it to `m_cellH`. One conf edit fixes both.
- No test asserts `LineSpacing`, and no doc names the value (`docs/ARCHITECTURE.md:2546` mentions the key generically). QA-evidence files that say "line spacing 5" are historical records and are not rewritten.

### Steps
1. In `data/theme/terminal.conf`, change `LineSpacing=5` to `LineSpacing=0`. Keep the key (the file's header promises old keys still read, and it stays a knob) — both readers already default to 0 when it is absent, so no code changes are needed.
2. Nothing else. Rebuild is not required for the conf, but the install step below may be.

### Risks
- **A stale installed copy can shadow the repo file.** `theme::themeDataDir()` (`src/Theme.cpp:824`) prefers `$RELAY_THEME_DIR`, then `<appdir>/../share/relay/theme`, then `/usr/local/share/relay/theme`, before the source tree. If an older `terminal.conf` sits in an earlier choice, the edit changes nothing there — re-install or run with `RELAY_THEME_DIR=$PWD/data/theme`.
- Rows get denser: `applyGeometry()` derives the row count from cell height, so a pane holds more lines and TUIs (vim, htop) redraw at the new cell size. Expected; screenshot goldens change size.
- At 0 extra px, consecutive Hack 11pt lines sit at the font's own leading (~1.2 em). No clipping by construction (`m_ch = ceil(fm.height())`), just tight — if the owner later wants a hair of air back, 1–2 px is the knob.

### Verify
1. `./scripts/test.sh` and `ctest --test-dir build` pass (no test covers the conf value).
2. Live check under Xvfb with an isolated `XDG_CONFIG_HOME` (project rule), `RELAY_THEME_DIR=$PWD/data/theme` to be sure the edited file is the one read: run `seq 1 5` and a command with blank lines between blocks — consecutive baselines one font-height apart, and each blank paragraph gap still one full row.
3. Re-open a remote-viewer pane to confirm its rows tightened the same way.

## QA checklist
Evidence: `docs/qa_evidence/2026-09-19-single-spaced-terminal-rows/` (drive script, four captures, measured numbers, notes).

1. **Rows are single-spaced on screen.** Re-run `qa-drive.sh` (or read `measure-before.txt` / `measure-after.txt`): row pitch 22.0px with `LineSpacing=5` vs 17.0px with `0` — exactly the removed 5px; 17px is Hack 11's own cell height. No clipping, glyphs whole in the captures.
2. **Paragraph gaps untouched.** In both cases the text-to-text distance across a blank line is 2.00 pitches (`44.0px` / `34.0px`): one full blank grid row still.
3. **Both readers read the edited file.** The drive pins `RELAY_THEME_DIR` at the repo theme, so the measured panes prove the engine reader; the remote viewer reads the same key from the same resolved file (`RemotePane.cpp:469` → `RemoteScreen::fit()`), same arithmetic — a live remote pane needs a second paired desktop and was not driven (see notes.txt).
4. **Suites.** `./scripts/test.sh` green (3411). `ctest` 62/63 — the `buttonfit` failure is pre-existing at HEAD (`tabProjectChip` 8.5pt under the 9pt floor, `src/Theme.cpp:542`), filed as #QAJQ; nothing here touches it. No test asserts `LineSpacing`.
5. **Nothing else regressed by this value:** `git diff` is the one conf line; historical QA-evidence files that mention "line spacing 5" are records and stay as they are.
