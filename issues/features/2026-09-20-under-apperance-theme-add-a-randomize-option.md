---
id: R4ND
type: work
status: needs-verification
labels: [feature, settings]
assignee: claude-code
rank: m
created: '2026-09-20'
source: Claude Code in a Relay pane, 2026-09-20
links: {plans: [], commits: [dae8796e, 260fb350], evidence: [docs/qa_evidence/2026-09-20-randomize-theme/, docs/qa_evidence/2026-09-20-tab-theme-marks/], related: [3C7N], github: null}
---
# Options › Appearance: a Randomize button that takes a theme at random

## Issue
under apperance / theme, add a "randomize" option

## What landed
`Randomize` is a button under Options › Appearance › Theme (`option:theme_random`). It takes a
theme at random that is **not** the one in use — `relay::theme::randomThemeId(avoid)` — and applies
it the way the picker does: this tab now, and the default Relay opens on and a new tab starts with.
The notice names the theme. It is a button, not a stored mode, so it declares no default and the
page's "Reset to defaults" count is unchanged; an agent may press it (`agentSafeButtons`), because
the theme list above undoes it in one click.

The same switch is on the fast path — `/theme random` (`randomize`, `shuffle` too) and **Random**
as the last row of the bare `/theme` picker — and pressing the button teaches the command once.

| file | change |
|---|---|
| `src/Theme.h`, `src/Theme.cpp` | `randomThemeId(avoid)` |
| `src/RelayWindow.h` | the Randomize row; `randomizeTheme()` |
| `src/Pane.h` | `/theme random`, the picker's Random row, `randomTheme()` |
| `tests/themeswitch_test.cpp` | `randomizeNeverGivesYouTheThemeYouAreOn` (300 draws) |
| `docs/ARCHITECTURE.md` | the per-tab themes section |

**The persistent mode** (owner, 2026-09-20: "i meant a persistent mode. it randomizes on each new
tab"): **Start each new tab on a random theme** (`theme/randomize_new_tab`, off by default), the
row directly under "Start each new tab on the next theme". A new tab takes a theme drawn at
random — never the default and never the previous tab's (`randomThemeId` now takes an avoid
list), with the default allowed back when only two themes are installed. The two new-tab modes
switch each other off in Options. The one-shot button above is unchanged. Note: the turn's own
summary claimed this landed as `6083b4cd`, but no such commit exists — this is the mode's first
real landing, in the same commit as #3C7N.

## QA checklist
- [ ] Options › Appearance shows **Randomize** directly under **Theme**; pressing it changes the
      whole window (chrome, terminal, prompt box) and a notice names the theme.
- [ ] Pressing it several times never lands on the theme already in use, and reaches every
      installed theme over enough presses.
- [ ] The theme it lands on is the default too: the Theme row above shows it, and a new tab and a
      restart come up on it.
- [ ] Searching Options for "randomize", "random" or "shuffle" finds the row.
- [ ] `/theme random` in the prompt box does the same thing, and the bare `/theme` picker has
      **Random** as its last row.
- [ ] The first button press shows the hint "Next time: /theme random in any prompt box".
- [ ] `ctest --test-dir build -R themeswitch` passes.
- [ ] Options › Appearance shows **Start each new tab on a random theme** under the cycling row,
      off by default. On: every new tab opens on a different theme, never the default, never the
      previous tab's; off: new tabs take the default again.
- [ ] Switching it on turns the cycling row off, and vice versa (the page redraws to show it).
- [ ] The mode survives a restart.
- [ ] `ctest --test-dir build -R themeswitch` passes (`theNewTabDrawAvoidsTheDefaultAndThePreviousTab`).
- [ ] Evidence for both this and the row above: `docs/qa_evidence/2026-09-20-tab-theme-marks/`.
