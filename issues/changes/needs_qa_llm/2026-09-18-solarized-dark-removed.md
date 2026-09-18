---
id: HECG
type: work
status: needs-qa-llm
labels: [change, theme]
component: [theme]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (Claude Code), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: 'data/theme/themes/solarized-dark.toml is gone, nothing in the app, tests or docs names it, and a profile still carrying theme/name=solarized-dark starts on Relay Dark with a whole palette'
source: 'owner, 2026-09-18: "remove the solarized dark theme"'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-actions-red-orange-and-lit-buttons/'], related: [N50J], github: null}
---
# Solarized Dark is removed

## Issue

remove the solarized dark theme

## Why

The legibility rules (#N50J, docs/ARCHITECTURE.md § 14) ask every text token for 4.5:1 on
background, surface and raised surface. Schoonover's palette does not reach that on Relay's raised
surfaces: its muted text measured 2.37:1 there and its state colours under 3.5:1. Meeting the rule
meant lightening the values until they were no longer the theme people recognise, so the owner's
answer was to drop it rather than ship a Solarized that is not Solarized, or exempt one theme from
the rule the others keep.

## Change

- `data/theme/themes/solarized-dark.toml` deleted. Five shipped themes remain: Relay Dark, Relay
  Light, Dark Copper, IBM Beige and Gruvbox Dark.
- `tests/theme_test.cpp` no longer names it, and its shipped-theme count is now "at least five".
- **A profile that still selects it falls back.** `resolveTheme()` already preferred `relay-dark`
  when the named theme is missing; nothing tested it, because no theme had ever been removed. New
  `tests/themeswitch_test.cpp` (ctest `themeswitch`) drives the live end of `src/Theme.cpp`: a
  config carrying `theme/name=solarized-dark` comes up on Relay Dark with a full palette, and a
  profile naming a theme that never existed does the same.

## Evidence

`docs/qa_evidence/2026-09-18-actions-red-orange-and-lit-buttons/` — the same drive script covers
this card and #SRM2; the theme list in the screenshots no longer offers Solarized Dark.

## QA checklist

1. `ls data/theme/themes` lists five files and no `solarized-dark.toml`.
2. `grep -ri solarized src tests docs data` finds nothing outside `docs/qa_evidence/` (historical
   evidence folders keep their own screenshots and are not rewritten).
3. Options › Appearance offers five themes; each one applies.
4. With `theme/name=solarized-dark` written into an isolated profile by hand, Relay starts on Relay
   Dark: text, terminal ground and the syntax colours are all present, nothing is black-on-black.
5. `ctest -R "theme|themeswitch|panestatus"` passes.
