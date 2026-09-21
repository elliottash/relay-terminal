---
id: VJDD
type: work
status: needs-verification
labels: [bug, terminal, theme]
assignee: agent
implemented_by: glm/glm-5.3-flash
session: f69267a2-25bb-405e-98c9-556be087771d
rank: zzzzzzzzzzzzzzzzi
created: '2026-09-20'
source: pane 2, 2026-09-20
links: {plans: [], commits: [5e8bc844, d3f46e3d], evidence: [docs/qa_evidence/2026-09-20-restored-pane-formatting/], related: [SB7K, 0TJ9], github: null}
---
# Restored pane scrollback loses all formatting and colors

## Issue
bug: when panes are re-opened, all the formatting and colors are lost in the text.

## Plan
### Goal
Make "reopen where I left off" bring back the terminal's scrollback and visible screen *with* their SGR formatting and colours intact, instead of replaying plain text only.

### Findings
- The per-pane scrollback store is already in place (card #SB7K). It saves `paneTextLines()` — a plain-text merge of `TerminalBackend::scrollbackText()` and `screenText()` — and replays it through `Pane::replayRestoredScrollback()`, which runs every line through `sanitize()` and therefore strips escape sequences.
- `WindowState.h` documents the deliberate "text, not cells" decision, mainly because RGB colours written into a new pane are burnt into history and cannot follow a theme switch.
- The engine's `VtCore` already exposes styled history rows via `historyLines()` and styled screen rows via `updateFrame(&frame, true)`. A serializer can turn those `Line` cells into ANSI SGR sequences without touching each core's internals.

### Steps
1. **Engine serializer** — add `engine/core/AnsiSerializer.{h,cpp}` with `linesToAnsi(const std::vector<Line>&)` / `lineToAnsi(const Line&)`. It will emit ESC `[ ... m` for attributes (bold, italic, faint, underline, blink, reverse, conceal, strike), foreground/background (default, indexed 0-255, RGB truecolour), and text including clusters and wide characters, trimming trailing blanks the same way `Line::text()` does.
2. **Backend API** — add `TerminalBackend::Capability::FormattedText` plus virtual `formattedScreenText()` and `formattedScrollbackText(int)`. Defaults return plain text so the KonsolePart adapter stays unchanged. `VTermBackend` overrides them using `TerminalSession::withCore()` to fetch styled `Line`s and serialize them.
3. **Pane save path** — add `paneFormattedTextLines()` and make `saveScrollback()` use formatted text when the backend reports `FormattedText`, falling back to plain text otherwise. The per-session sidecar (`saveSessionText()`) gets the same treatment.
4. **Pane replay path** — add `sanitizeSgrOnly()` that strips C0/C1 controls but keeps CSI SGR sequences (`ESC [ ... m`) so a hand-edited file still cannot drive the terminal. Use it for restored lines that contain escape sequences; plain lines continue to use `sanitize()`.
5. **Restore-mark filtering** — update `dropRestoreMarks()` to strip SGR before comparing, so the replayed open/close rules (which are now coloured) are still filtered out on the next save and do not stack.
6. **Tests** — add engine tests for the serializer (default/attribute/colour/cluster/wide/trimming cases) and extend `windowstate_test.cpp` with a formatted-scrollback round trip that reads back lines containing escape sequences.
7. **Build & verify** — build with `scripts/relay-build` and run `ctest --test-dir build -R windowstate` plus `ctest --test-dir build -R relay-engine-tests`.

### Risks / tradeoffs
- RGB colours in saved scrollback will not adapt to later theme switches; they are replayed as the RGB the original program emitted. Indexed colours (0-255) follow the terminal palette and therefore follow palette-based theme changes. This is the same RGB-vs-theme tension documented in #SB7K; fixing the reported bug means accepting it.
- Saved files may become slightly larger because of escape sequences, but they are still clamped by the same 5,000-line / 512 KiB caps.
- The change touches the engine core, backend interface, and pane restore path, so both engine and GUI tests must pass.

## Execution Summary
Restored panes now keep SGR formatting and colours. The engine-neutral serializer
`engine/core/AnsiSerializer.{h,cpp}` turns `relay::Line` cells into ANSI SGR (attributes, indexed and
RGB foreground/background, clusters, wide characters, trailing blanks trimmed like `Line::text()`).
`TerminalBackend` gains `FormattedText` plus `formattedScreenText()` /
`formattedScrollbackText(int)`; `VTermBackend` implements them from `historyLines()` and
`updateFrame()`, and the default falls back to the plain methods for engines that cannot.
`Pane::saveScrollback()` and `Pane::sessionTextLines()` now save the formatted variants (falling
back to plain), and `Pane::replayRestoredScrollback()` keeps CSI SGR while dropping every other
control sequence through `sanitizeSgrOnly()`. `dropRestoreMarks()` and
`sessiontext::turnStart()` strip SGR before comparing, so Relay's own restore rules do not stack
and rewind still finds a turn's marker. Docs updated in `src/WindowState.h`,
`docs/ARCHITECTURE.md` and `engine/CMakeLists.txt`.

Files: `engine/core/AnsiSerializer.{h,cpp}`, `engine/CMakeLists.txt`, `engine/TerminalBackend.h`,
`engine/backend/VTermBackend.{h,cpp}`, `engine/tests/CoreTest.cpp`, `src/Pane.h`,
`src/WindowState.{h,cpp}`, `tests/windowstate_test.cpp`, `docs/ARCHITECTURE.md`.

## Tests
`ctest --test-dir build -R ^windowstate$`
`ctest --test-dir build -R ^relay-engine-tests$`

## QA checklist
1. **The report.** In a pane, print something coloured (`ls --color=always`, `git diff`, or
   `printf '\e[1;31mred\e[0m plain\n'`), quit Relay and start it again. The reopened pane shows the
   same text above its new prompt with the bold/colour it had, not plain white-on-theme.
2. **Formatted screen.** Leave coloured text on the visible screen (not only scrolled off), quit and
   restart: the visible rows come back coloured too, not just the history.
3. **Truecolour and 256-colour.** `printf '\e[38;2;255;0;0mtruecolour\e[0m\n\e[38;5;208m256\e[0m\n'`
   survives the restart. Indexed colours follow a palette-based theme switch; RGB keeps the colour
   it was printed in (documented tradeoff).
4. **No escape-sequence injection.** Hand-edit
   `~/.local/share/relay/state/scrollback/<id>.txt` to contain `\e]0;evil\a` or `\e[2J`; restart.
   The title does not change and the screen is not cleared — only SGR survives replay.
5. **Rules do not stack.** Quit and restart twice: exactly one pair of restored-scrollback rules is
   shown, not one pair per restart.
6. **A plain file still works.** An older scrollback file with no escape sequences replays exactly
   as before, and an engine without `FormattedText` still restores plain text.
7. **Rewind still finds its turn.** In a restored conversation, rewind to a turn whose marker was
   saved with colour: the rewind anchors on the turn's own line, not the wrong one.
8. **No regression.** `ctest --test-dir build -R '^(windowstate|relay-engine-tests)$'` passes; the
   full `scripts/relay-build` links `build/relay`.
