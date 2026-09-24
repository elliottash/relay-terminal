---
id: SRM2
type: work
status: needs-qa-llm
labels: [change, feature, theme]
component: [gui, theme]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (Claude Code), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: The Actions pane wears a red-orange band and glyph in every shipped theme, told apart from the error state and the ssh band, at 4.5:1 for the label and 3:1 for the glyph; Options keeps the green
source: 'owner, 2026-09-18: "make actions red-orange"'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-18-actions-red-orange-and-lit-buttons/], related: [SPBN, N50J, HECG], github: null}
---
# Actions is red-orange, its own hue

## Issue

make actions red-orange

## Change

Actions and Options were one Settings pane until 2026-09-18, so in "by type" they shared the green
and were told apart by the glyph alone. Actions now has a fifth meaning hue of its own, between
`warning` and `error`.

- **A theme token, not a constant.** `[ui] action` in every shipped theme file, `theme::Action` in
  `src/Theme.h`, read by `relay::panestatus::Tokens::action` and the chrome's `tokens()`.
- **A user theme that omits it does not inherit Relay Dark's.** `parseTheme()` turns one out of that
  theme's own `error`, at its luminance (`redOrangeFrom()` in `src/ThemeFile.cpp`), because another
  theme's orange need not be legible on this one's ground.
- **Per-theme values,** each chosen against that theme's own reds and yellows: Relay Dark `#e5844f`,
  Relay Light `#a8450c`, Dark Copper a vermilion `#e56a30` (the copper accent is itself an orange),
  Gruvbox upstream's own bright orange `#fe8019`, IBM Beige a terracotta.
- **Told apart from the two reds it sits near:** `tests/panestatus_test.cpp` measures CIELAB ΔE76
  between `action`, `error` and the ssh band's red in every theme, and the band carries the bolt
  glyph and the word "Actions" besides.
- "By group" is unchanged: every tool pane shares the brass tint.

## Evidence

`docs/qa_evidence/2026-09-18-actions-red-orange-and-lit-buttons/` — `drive.sh` (scenes `buttons`,
`toggle`, `info`), and the screenshots named in the QA list. Measured from the shots: the Actions
band ground `#2B201D` on Relay Dark against Options' `#1D2924`, with the glyph at the token itself.

## QA checklist

1. Open Actions (Ctrl+Shift+A): its header band is red-orange, with a bolt glyph and "Actions".
2. Options beside it is still green, the Switchboard brass, Sessions blue, subagents violet.
3. An ssh terminal's red band and a failed turn's red glyph are not mistakable for the Actions band
   at a glance, in Relay Dark and Relay Light.
4. The Actions title-bar button wears the same red-orange while its pane is open (#E01Z).
5. Options › Appearance › Pane colours "by group": Actions goes brass with the other tool panes.
   "Off": no tint anywhere.
6. Each shipped theme in turn: the band label reads at 4.5:1, the glyph at 3:1 (the theme test
   asserts it; check by eye that none looks muddy).
7. `ctest -R "theme|panestatus"` passes.
