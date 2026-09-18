---
id: N50J
type: work
status: needs-qa-llm
labels: [change]
component: [gui, theme]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (Claude Code subagent), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: 'No UI text under 9pt (stylesheet in pt only, QPainter labels through theme::legible); the application font at least 10pt; every text token 4.5:1 on background, surface and surface_raised in every shipped theme; no italic + muted + monospace in the terminal notes, the subagent transcript, the thinking panel or the turn log; the Options "Log detail" row reads as a line, not a column; theme and buttonfit tests pass'
source: 'owner, 2026-09-18: "some of the fonts seem hard to read, eg in subagent panes"'
links: {plans: [], commits: [fac5dac, 6eb00d0], evidence: ['docs/qa_evidence/2026-09-18-legible-text/'], related: [SPBN, WD83, TK9C], github: null}
---
# Text is legible everywhere: a size floor, 4.5:1 on every surface, no italic muted mono

## Issue

some of the fonts seem hard to read, eg in subagent panes

## Change

What made text hard to read was the same few things in many places. The rules are now written
down in `docs/ARCHITECTURE.md` § 14, "Legible text", and tested:

1. **Sizes.** `theme::BodyPt` 10, `SecondaryPt` 9.5, `FloorPt` 9 (`src/Theme.h`). `applyTheme()`
   raises Qt's generic 9pt default application font to 10pt (a desktop's own 10–11pt is left
   alone). Every stylesheet `font-size` is in pt and at least 9: the pane-header `auto` badge was
   9px (6.75pt), the Switchboard's count, section checkboxes, key line and edit hint, the Options
   group headings, the notification time and dot, and the plan, prefix and secret chips were 8pt;
   the composer strip's chips, the work chip, key caps, setting details and footers were 11px
   (8.25pt). Painted text too: the Switchboard's section titles, counts, card ids and badges were
   0.8–0.85 of the row font (7.2–7.65pt) and now stop at 9pt (`theme::legible()`); the bell's count
   was 7px in an 8px dot and is now a 9pt pill. Transcripts (the subagent tab, the program-running
   overlay, the turn log, 9pt) are the mono face at 10pt.
2. **Pane header bands** (#SPBN) name the pane in the terminal title's face and weight, in sentence
   case ("Switchboard", "Options"), instead of 8pt letter-spaced mono capitals.
3. **Contrast, per theme, in the tokens.** A new test holds text, muted text, the accent and the
   state colours to 4.5:1 on `background`, `surface` and `surface_raised` in all six shipped
   themes, muted text on the terminal's ground, and the composer's syntax colours on both composer
   grounds. Failing tokens were lifted in their theme files: Solarized Dark (muted 2.37:1 on a raised
   surface, text 3.95:1, every state colour under 3.5:1; `surface_raised` moved a step toward base02),
   Gruvbox Dark (muted 4.17, error 3.37, accent/agent ~4.2–4.3), Relay Light (accent/shell 3.85 on a
   raised surface, success 4.47, the syntax operator 3.98). Relay Dark, Dark Copper and IBM Beige
   already passed and are unchanged.
4. **Hard-coded Relay Dark colours** read on a light theme: the subagent transcript's prose was
   `#e2e5eb` (1.22:1 on Relay Light), its user lines 2.49:1 and its grey 3.49:1 — the transcript now
   takes the live tokens (16.3, 6.4 and 5.8:1). Same fix in the turn log's `⚙` line (amber, 1.56:1
   on Relay Light) and the Markdown fallback highlighter (cyan headings, 1.81:1). The share dialog's
   link and note were `palette(mid)`, the border colour: 1.39:1; now `@muted` at 9.5pt.
5. **No italic + muted + mono.** The terminal's agent notes ("started in the background", the
   "(Ctrl+click)" after a run of calls) were SGR italic in the muted grey; the subagent transcript's notes
   (`── live ──`, outcomes) were italic grey; the thinking panel and the turn log's thinking and
   "(truncated)" lines too. All upright now; the muted colour and the glyph mark them.
6. **Faint ink — not changed here.** The engine draws SGR 2 and a fold's dim rows at 60% alpha,
   which takes the muted grey to 2.9:1 (Relay Dark) and 2.5:1 (Relay Light). The fix (fade toward
   the ground only as far as 4.5:1) belongs in `engine/view/TerminalView.cpp`, which another
   session is changing; it is handed to that session rather than done here.
7. **Options "Log detail"** wrapped one word per line: the choice box asked for its widest entry and
   a wrapping label gives way down to its longest word. The label column now keeps about eighteen
   characters (less if the text is shorter), and a choice box may shrink to about ten characters in
   a narrow pane; its popup still lists every entry in full.
8. **Switchboard section checkboxes** that are off drew their label in the disabled colour
   (3.0:1 dark, 2.2:1 light) though they are live controls; they stay `@muted`, the box says off.

Files: `src/Theme.{h,cpp}`, `data/theme/themes/{relay-light,gruvbox-dark,solarized-dark}.toml`,
`src/PaneChrome.h` (band label), `src/PaneStatus.{h,cpp}` (label case), `src/SubagentTranscript.{h,cpp}`,
`src/TurnTranscript.cpp`, `src/Pane.h` (the note ink's style byte, the program overlay's note format,
the thinking format — style constants only),
`src/BoardPane.cpp` (font helpers), `src/SettingsPane.cpp` (row layout), `src/WindowChrome.h`
(bell badge), `src/RemoteShare.cpp`, `src/FilePanes.cpp`, `tests/theme_test.cpp`,
`tests/buttonfit_test.cpp`, `tests/panestatus_test.cpp`, `docs/ARCHITECTURE.md`.

## The engine's part

The terminal engine drew faint text (SGR 2) and a fold's dim rows at 60% alpha, a flat 40% fade
that put the shipped themes' muted grey at about 2.9:1 (Relay Dark) and 2.5:1 (Relay Light). Landed
with #TK9C as 6eb00d0: `engine/view/FaintInk.h` fades the ink toward the ground the cell is drawn
on only as far as 4.5:1 allows and never worsens ink that was already under the floor;
`engine/tests/FaintInkTest.cpp` pins it on both themes' greys.

## Evidence

`docs/qa_evidence/2026-09-18-legible-text/` (README there): the same layouts before and after, in
Relay Dark and Relay Light — a terminal with agent notes, the subagent pane with a transcript, the
Switchboard, Options at Diagnostics, and the notification list — plus 100% before/after crops.

## QA checklist

- [ ] `ctest --test-dir build -R 'theme|buttonfit|panestatus'` passes; `everyShippedThemeKeepsItsTextLegible`
      and `stylesheetFontsStayAtOrAboveTheFloor` run (not skipped)
- [ ] Under Xvfb (Qt's 9pt default) the UI font is 10pt; on a desktop set to 11pt it stays 11pt
- [ ] Subagent pane, Relay Light: the agent's prose is dark on the light pane; `── live ──` and tool
      lines are grey and upright
- [ ] Terminal: "✦ … started in the background" and "(Ctrl+click)" lines are grey, not italic
- [ ] Pane bands read "Subagent", "Switchboard", "Options" in the title weight, not small caps
- [ ] Switchboard: section titles, counts, card ids, badges and the checkboxes are no smaller than
      the key line at the bottom; an unchecked section's label is still readable
- [ ] Options › General › Diagnostics in a narrow pane: "Log detail" is one line, its detail wraps in
      a readable column, the choice box is narrower
- [ ] Two or more unread notifications: the bell's count is readable
- [ ] Switch to Solarized Dark and Gruvbox Dark: muted text (setting details, the composer strip)
      is readable on every surface; the accent is still recognisably the theme's
- [ ] Nothing clips: the composer strip's chips, the Switchboard's tools row and the Options rows
      at 9pt (the strip chips grew from 8.25pt; in a narrow pane they elide as before)
