<!-- relay:entry 20260919T232925Z-mt author=owner kind=comment mode=discuss -->
check if this is stale

<!-- relay:entry 20260919T233339Z-nx author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=switchboard turn=45867f9ae65f48f89177dfafd2521a00/11294f4f4d6e4da985514a8a12f409c6 -->
- ✦ agent updated this card · appended to `## Approach`

<!-- relay:entry 20260919T233339Z-ny author=agent kind=note model=deepseek/deepseek-v4.1-flash pane=switchboard turn=45867f9ae65f48f89177dfafd2521a00/11294f4f4d6e4da985514a8a12f409c6 -->
Staleness check, 2026-09-19: **not stale — the fault is still there at HEAD, and the Approach section was out of date.** Read, not run (this pane cannot run commands):

- `src/Pane.h:11163` is the ink list the card names; `inkColor` (`:11174`) maps them to theme tokens and `inkCode` (`:11193`) still writes truecolour — `:11211` (`38;2` + `48;2` for the diff rows), `:11221` (`38;2`), with `Ink::Ask` the sole indexed ink (`:11202`). So `Tool`, `ToolOutput`, `Note`, `Recap`, `Agent`, `DiffAdd`, `DiffRemove` and the fold palette are frozen exactly as described.
- Row roles: still only `MarkUserShell` / `MarkUserAgent` (`engine/core/CellTypes.h:61`), resolved at paint time in `engine/view/TerminalView.cpp:629` and `:1002`; `relay_marks` is 8 bits, masked `& 0xFF` in `LibVtermCore.cpp:375` and `third_party/libvterm/src/state.c:2321`.
- What is new since the card: #R2WQ's prose blocks. `printInline` wraps each block in `relay://prose/…` and hands the logical lines to `setProseBlock` as spans carrying an SGR index (`engine/view/ProseSpans.h`; `FoldLayer::Cell::fgPacked`, `engine/view/FoldLayer.h:64`), painted from the current scheme (`TerminalView.cpp:990`+). But `FoldLayer` only takes the rows over while the pane is not at `printColumns` (`engine/view/FoldLayer.h:88`), and `setProseBlock` is called from one place only (`src/Pane.h:11383`), so a theme switch does not re-hand them: at the printed width the old RGB rows are still what is drawn.
- `src/MarkdownAnsi.h` already states the rule the rest of the inks break ("an absolute colour is burnt into the scrollback … indexed colours are resolved by the engine at paint time"), which is why agent prose follows a theme switch today.

Verdict: keep the card; the acceptance criterion is unmet. Recorded the finding in `## Approach` (the role bits are now the expensive option). Ready to Execute whenever you want it taken.

<!-- relay:entry 20260919T233345Z-me author=agent kind=comment mode=discuss model=deepseek/deepseek-v4.1-flash turn=45867f9ae65f48f89177dfafd2521a00/11294f4f4d6e4da985514a8a12f409c6 -->
**Not stale — still real at HEAD**, but its fix section had gone out of date. I read the code (no commands run from this pane) and put both the verdict and the correction on #K3RT.

- `inkCode` still writes truecolour for every ink but `Ask` (`src/Pane.h:11202` indexed, `:11211`/`:11221` `38;2`), and only `MarkUserShell`/`MarkUserAgent` exist as row roles — so tool lines, notes, recaps and diffs already in the grid keep the old theme's colours.
- New since the card was written: **#R2WQ prose blocks** now hand every inline block to the engine as spans carrying an SGR *index*, which the view resolves from the theme at paint time (`ProseSpans.h`, `FoldLayer.h:64`, `TerminalView.cpp:990`+) — but only while the pane is not at its print width, and nothing re-hands the blocks on a theme switch (`setProseBlock` has one call site, `src/Pane.h:11383`).
- So `MarkdownAnsi`'s rule ("an absolute colour is burnt into the scrollback … indexed colours are resolved by the engine at paint time") is now the cheap fix, and four new role bits are the expensive one.

**Card:** #K3RT — appended the check to `## Approach` and left it `ready` for Execute.

<!-- relay:entry 20260921T212352Z-aa author=codex kind=progress -->
### Codex · 2026-09-21 21:23
Claimed #K3RT for the renewed request; recorded the indexed-ink implementation plan.

<!-- relay:entry 20260921T213206Z-aa author=codex kind=evidence -->
### Codex · 2026-09-21 21:32
Implemented indexed inline/fold colors and found/fixed the libvterm history RGB conversion. Xvfb pixel checks pass for grid, rewrapped prose, expanded details and scrollback. Evidence and QA checklist recorded; moving to needs-verification with the implementation. Literal RGB from older saves cannot recover its original role.
