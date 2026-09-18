---
id: 0EXJ
type: work
status: needs-qa-llm
labels: [feature]
assignee: agent
implemented_by: Claude Opus 5 (1M context) (Claude Code session), 2026-09-18
rank: zzzz107
created: '2026-09-18'
acceptance: /light switches to IBM Beige and /dark to Dark Copper
source: 'issues/feature_intake.txt, 2026-09-18'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-intake-fixes/'], related: [0JA7], github: null}
---
# /light and /dark commands

## Request
add /light and /dark commands. light activates beige; dark activates copper.

## Behavior as implemented

- `/light` switches to **IBM Beige** (`data/theme/themes/ibm-beige.toml`) and `/dark` to
  **Dark Copper** (`dark-copper.toml`). The owner named those two themes, so the commands reach for
  those two ids; they are not "any light theme" and "any dark theme".
- Both go through `relay::theme::setActiveTheme()`, the one call the settings theme picker makes.
  So one command restyles the chrome, both terminal engines' palettes and the prompt box's syntax
  colours, in every window and every pane, with no restart — and stores `theme/name`, so the choice
  survives one.
- They are entries in `slashCommands()`, which is also what fills the `/` popup (each with its
  theme's name in the description) and what reserves a name against a user alias of the same name.
- The status line reports the theme that took effect, from the theme file's own `name`.
- An unreadable or missing theme file says so and changes nothing.

## Implementer check (not a QA verdict)

`tests/theme_test.cpp` (`theLightAndDarkCommandsHaveTheirThemes`: the two ids exist, carry those
names and are the right way round — renaming either file is what would break the commands) and
`ctest -j16` green. Xvfb :190 with an isolated `XDG_CONFIG_HOME`:
`docs/qa_evidence/2026-09-18-intake-fixes/` (`0exj-…`), including `relay.conf` holding
`[theme] name=dark-copper` afterwards.

## QA checklist

1. Type `/li` in the prompt box: the popup offers `/light` with "Light theme: IBM Beige". `/da`
   offers `/dark` with "Dark theme: Dark Copper".
2. Enter on `/light`: the whole window turns beige at once — chrome, explorer, preview, the
   terminal's own colours and the prompt box's syntax colours — and the status line says
   "Theme: IBM Beige."
3. `/dark`: the same for Dark Copper.
4. Run one of them in a window with several tabs and panes: every pane changes, not just the one
   that ran it.
5. Restart Relay: it comes back in the theme the last command chose.
6. Open the theme picker in settings after `/light`: it shows IBM Beige as the current theme.
7. `/light` twice in a row is harmless.
8. An alias named `light` does not shadow the command (the built-in wins).

Implementer evidence: docs/qa_evidence/2026-09-18-intake-fixes/
