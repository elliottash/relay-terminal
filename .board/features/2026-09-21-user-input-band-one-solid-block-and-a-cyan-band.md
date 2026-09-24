---
id: 7QFW
type: work
status: needs-verification
labels: [feature, terminal, theme]
assignee: agent
implemented_by: kimi/kimi-k3
session: 2b383612-af3a-4008-b2e4-a015ea509464
rank: zzzzzzzzzzzzzzzzw
created: '2026-09-21'
links: {plans: [], commits: [7e8f3de55e0eb58bea6e6f38afe628c41993a40d], evidence: [docs/qa_evidence/2026-09-21-user-input-band/], related: [], github: null}
---
# User-input band: one solid block, and a cyan band for shell commands

## Issue
i want to change how the background box for user input looks. 

see here: @/home/elliott/.cache/RelayTerminal/relay/images/relay-paste-20260921-060949.png 

first, rather than separate lines with black input at line breaks , i want it to be a solid colored block.

second, i want the block done for agent prompts (in violet) to also apply to shell commands (but cyan)

## Plan
**Goal.** The band behind what the user typed becomes (1) one solid block across line breaks and (2) applies to shell commands in cyan, as agent prompts already are in violet.

**Findings.**
- `Pane::printInline` (src/Pane.h ~13354) marks each row of a `User`/`UserAgent` echo with OSC 7772;shell|agent — but deliberately skips *empty* rows so a trailing fragment can't hand the role to whatever prints next. Blank rows inside a multi-paragraph prompt therefore paint with the ground colour: the black gaps at line breaks in the screenshot.
- `TerminalView::paintRow` (engine/view/TerminalView.cpp ~696) already fills any marked row full-width with `userAgentBand`/`userShellBand` and the matching ink; `EngineBackend::applyThemeColors` (src/EngineBackend.cpp ~83) sets shell=cyan, agent=violet (option `terminal/echo_band`). So once shell-command rows carry `MarkUserShell`, the cyan block appears with no view change.
- Nothing emits OSC 7772;shell today: `Ink::User` has no call sites. A shell command is echoed by readline, not by Relay (docs/ARCHITECTURE.md ~3016), so the marks must come from the pane's own bash integration (shell/integration.bash).

**Steps.**
1. src/Pane.h: in the `Ink::User`/`Ink::UserAgent` row loop, mark an empty row too when it ends in `\r\n` (the cursor leaves it, so it is a blank row *inside* the block). Keep skipping only a fragment the cursor stays on.
2. shell/integration.bash: `__relay_load` counts the display rows the staged text will occupy (sum of max(1, ceil(len/COLUMNS)) per line) into `__relay_staged_rows`; the first-fire branch of the `__relay_debug` DEBUG trap then emits `CUU n` + OSC 7772;shell per row + CUD back, marking the rows directly above the cursor (the command's echo). Hand-typed commands fall back to a row count from `history 1`. Count is capped at LINES-1 (marks the visible tail); staged count is cleared at every prompt and after use.
3. Tests: extend tests/test_shell.py — staged single-line, staged multi-line, and hand-typed commands all emit the expected number of OSC 7772;shell marks in the right shape (CUU n, then mark+CUD).
4. Docs: update the "line you typed sits on a band" paragraph in docs/ARCHITECTURE.md (shell commands now get the row role from the pane integration; the block is solid across blank rows).

**Risks / decisions.**
- Row counting uses character count, not display cells (wide CJK/tabs miscount at wrap boundaries; under/over-mark by a row in rare cases). Accepted.
- Hand-typed multi-line commands count from history, where bash may join lines with `;` → under-marking only. Accepted.
- zsh (shell/relay-integration.zsh) not covered; pane shells run bash. Out of scope unless the owner asks.

**Verify.** `python3 -m pytest tests/test_shell.py` (new cases), `ctest --test-dir build -R 'Core|View'` (engine mark handling unchanged), build via scripts/relay-build, then a live check in a pane: multi-paragraph agent prompt shows one solid violet block; a composer-run shell command shows a solid cyan block.

## Execution Summary
- `src/Pane.h` (`printInline`, `Ink::User`/`Ink::UserAgent`): a blank row that ends in `\r\n` (the cursor leaves it) is now marked with the row's OSC 7772 role, so blank lines inside a multi-paragraph prompt carry the band — one solid block. Only a fragment the cursor stays on (after the last newline) goes unmarked, as before.
- `shell/integration.bash`: `__relay_load` counts the staged text's display rows into `__relay_staged_rows` (`__relay_rows_for`); the first-fire branch of the `__relay_debug` DEBUG trap calls `__relay_mark_typed_rows`, which moves the cursor up over the command echo's rows and marks each `OSC 7772;shell` (cyan band, painted by the existing `TerminalView`/`EngineBackend` path). Hand-typed commands count rows from `history 1`; the count is capped at `LINES-1` and cleared at every prompt and after use. Verified first that bash's DEBUG trap does not fire inside `bind -x` handlers beyond the `__relay_*` function call itself, so staging cannot trip the marking early.
- `docs/ARCHITECTURE.md`: the "line you typed sits on a band" paragraph now describes the solid block and the shell-command marking.
- No engine change: `TerminalView::paintRow` + `EngineBackend::applyThemeColors` already paint marked shell rows cyan (option `terminal/echo_band`).

Known limits (accepted in the plan): row counts use character width (wide glyphs/tabs can miscount at a wrap boundary); hand-typed multi-line commands under-mark when history joins lines with `;`; zsh integration not covered.

## Tests
- `python3 -m unittest tests.test_shell -v` — 11/11 pass, incl. new `test_staged_command_rows_marked`, `test_staged_multiline_rows_marked`, `test_typed_command_rows_marked` (real PTY bash with shell/integration.bash; assert CUU n + n × OSC 7772;shell + CUD)
- `ctest --test-dir build -R consolemode` — passed
- `scripts/relay-build` — clean build of the `src/Pane.h` change
- manual: live pane check (see QA checklist)

## QA checklist
Visual check in a live pane (needs eyes; the automated side is PTY byte-assertions):
1. Send the agent a multi-paragraph prompt (blank line between paragraphs): the violet band is one solid block — no ground-coloured rows inside it. Scroll up: the block is solid in scrollback too. Switch the tab's theme (`/theme`): the band repaints in the new theme's agent colour.
2. Run a shell command from the composer: the command's echo rows sit on a solid cyan block (text in the chip ink), as wide as the grid. A long command that wraps shows the band on every wrapped row.
3. Type a command directly at the terminal (native input) and Enter: same cyan block (history fallback).
4. A command taller than the screen marks only the visible tail (accepted limit); a hand-typed multi-line command may under-mark (accepted limit).
5. Options › Terminal › "Band behind what you typed" = none: no bands, destination colour as ink, for both agent prompts and shell commands.
6. Regression: the agent's reply, tool lines and asks render as before; `!` shell-mode guest input unaffected.
